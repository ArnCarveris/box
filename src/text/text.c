/**
 * @file text/text.c
 * @brief Text components, the font registry and the systems that keep texts laid out.
 *
 * Setting a Text lays it out at once; a font that loads or an atlas page that grows lays out
 * the texts of its font again. The file watch loads a font again when its file changes.
 * Headless frames wait for the font and glyph jobs in flight.
 */

#define BOX_TEXT_IMPL
#include "text.h"
#include "../jobs/jobs.h"
#include "../layout/layout_text.h"

ECS_COMPONENT_DECLARE(TextGlyphs);
ECS_COMPONENT_DECLARE(TextRegistry);

box_text_state_t* box_text_state_get(
    const ecs_world_t *world)
{
    const TextRegistry *r = ecs_singleton_get(world, TextRegistry);

    return r ? r->state : NULL;
}

const rgba* box_text_span_color(
    const TextSpans *spans,
    int32_t length,
    int32_t offset)
{
    const TextSpan *first = spans ? ecs_vec_first(&spans->spans) : NULL;
    int32_t i = first ? ecs_vec_count(&spans->spans) : 0;
    const rgba *result = NULL;
    if (offset < 0 || offset >= length) {
        goto done;
    }

    while (i -- > 0) {
        int32_t start = first[i].start > 0 ? first[i].start : 0;
        int32_t end = first[i].end < length ? first[i].end : length;
        if (offset >= start && offset < end) {
            result = &first[i].color;
            break;
        }
    }

done:
    return result;
}

/** Free the registry of a world.
 *
 * @param s The registry
 */
static void box_text_state_free(
    box_text_state_t *s)
{
    ecs_map_iter_t it = ecs_map_iter(&s->fonts);
    while (ecs_map_next(&it)) {
        box_text_font_free(s, ecs_map_ptr(&it));
    }

    ecs_map_fini(&s->fonts);
    ecs_vec_fini_t(NULL, &s->released, uint32_t);
    ecs_vec_fini_t(NULL, &s->placed, box_text_placed_t);
    ecs_vec_fini_t(NULL, &s->lines, float);
    ecs_vec_fini_t(NULL, &s->carets, box_text_caret_t);
    ecs_vec_fini_t(NULL, &s->rows, box_text_row_t);
    ecs_vec_fini_t(NULL, &s->texts, ecs_entity_t);
    if (-- s->sync->refs == 0) {
        ecs_os_mutex_free(s->sync->lock);
        ecs_os_free(s->sync);
    }

    ecs_os_free(s);
}

/** Free the registries of TextRegistry values.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti The type info
 */
static void box_text_registry_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    TextRegistry *r = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        if (r[i].state) {
            box_text_state_free(r[i].state);
        }
    }
}

/** Move TextRegistry values, the source no longer owns its registry.
 *
 * @param dst The destination values
 * @param src The source values
 * @param count Number of values
 * @param ti The type info
 */
static void box_text_registry_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ecs_os_memcpy(dst, src, ti->size * count);
    ecs_os_memset(src, 0, ti->size * count);
}

/** Lay out every set Text.
 *
 * @param it The iterator of the observer
 */
static void BoxTextOnSet(
    ecs_iter_t *it)
{
    box_text_state_t *s = box_text_state_get(it->world);
    const Text *texts = ecs_field(it, Text, 0);
    int32_t i;
    if (!s) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        box_text_layout(it->world, s, it->entities[i], &texts[i]);
    }

done:
    return;
}

/** Drop the glyphs of a removed Text and zero its bounds, unless the entity is deleted.
 *
 * @param it The iterator of the observer
 */
static void BoxTextOnRemove(
    ecs_iter_t *it)
{
    box_text_state_t *s = box_text_state_get(it->world);
    int32_t i;
    if (s) {
        s->trim = true;
    }

    if (!it->other_table || !ecs_table_get_type(it->other_table)->count) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        TextGlyphs *glyphs = ecs_get_mut(it->world, it->entities[i], TextGlyphs);
        if (glyphs) {
            ecs_vec_clear(&glyphs->glyphs);
            ecs_vec_clear(&glyphs->carets);
            ecs_vec_clear(&glyphs->rows);
            glyphs->ready = false;
        }

        ecs_set(it->stage, it->entities[i], TextBounds, {0});
    }

done:
    return;
}

/** Load a font again when the path of its Font changes and a Text uses it; a new atlas budget
 * only releases idle pages.
 *
 * @param it The iterator of the observer
 */
static void BoxFontOnSet(
    ecs_iter_t *it)
{
    box_text_state_t *s = box_text_state_get(it->world);
    int32_t i;
    if (!s) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        box_text_font_t *font = ecs_map_get_deref(&s->fonts, box_text_font_t, it->entities[i]);
        const Font *f = ecs_get(it->world, it->entities[i], Font);
        const char *path = f && f->path ? f->path : "";
        if (font && font->requested && strcmp(font->path ? font->path : "", path)) {
            box_file_watch_clear(it->world, s->watch, it->entities[i]);
            box_text_font_load(it->world, s, font);
            s->relayout = true;
        }
    }

    s->trim = true;
done:
    return;
}

/** Load a font in use again whose file changed, called by the file watch. A load that
 * succeeds drops the atlas of the old face and lays out the texts of the font again.
 *
 * @param world The world
 * @param ctx Unused
 * @param owner The font entity
 */
static void box_text_font_changed(
    ecs_world_t *world,
    void *ctx,
    ecs_entity_t owner)
{
    box_text_state_t *s = box_text_state_get(world);
    box_text_font_t *font = s ? ecs_map_get_deref(&s->fonts, box_text_font_t, owner) : NULL;
    (void)ctx;
    if (font && font->requested && ecs_is_alive(world, owner) && ecs_has(world, owner, Font)) {
        box_text_font_load(world, s, font);
    }
}

/** Forget a font whose Font is removed; its texts lay out with the default font.
 *
 * @param it The iterator of the observer
 */
static void BoxFontOnRemove(
    ecs_iter_t *it)
{
    box_text_state_t *s = box_text_state_get(it->world);
    int32_t i;
    if (!s || ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        box_text_font_t *font = ecs_map_get_deref(&s->fonts, box_text_font_t, it->entities[i]);
        box_file_watch_clear(it->world, s->watch, it->entities[i]);
        if (font) {
            ecs_map_remove(&s->fonts, it->entities[i]);
            box_text_font_free(s, font);
            s->relayout = true;
        }
    }

done:
    return;
}

/** Count the font and glyph jobs in flight.
 *
 * @param sync The counter
 * @return Jobs whose work did not finish
 */
static int32_t box_text_working(
    box_text_sync_t *sync)
{
    ecs_os_mutex_lock(sync->lock);
    int32_t result = sync->working;
    ecs_os_mutex_unlock(sync->lock);

    return result;
}

/** Loading fonts a timed out headless wait names in its warning */
#define BOX_TEXT_WARN_NAMES (4)

/** In a headless run, wait for the font and glyph jobs in flight so a frame always sees the
 * same atlas, and warn when the wait gives up, naming the font loads not applied yet while
 * font or glyph jobs are in flight.
 *
 * @param it The iterator of the system
 */
static void BoxTextFrame(
    ecs_iter_t *it)
{
    box_text_state_t *s = box_text_state_get(it->world);
    const App *app = ecs_singleton_get(it->world, App);
    ecs_time_t start = {0};
    double waited = 0;
    if (!s || !app || !app->headless) {
        goto done;
    }

    ecs_time_measure(&start);
    while (box_text_working(s->sync) || box_jobs_running(it->world)) {
        ecs_time_t now = start;
        waited = ecs_time_measure(&now);
        if (waited > BOX_TEXT_HEADLESS_WAIT) {
            break;
        }

        ecs_os_sleep(0, 200 * 1000);
    }

    if (waited <= BOX_TEXT_HEADLESS_WAIT) {
        goto done;
    }

    ecs_strbuf_t names = ECS_STRBUF_INIT;
    int32_t loading = 0, i, working = box_text_working(s->sync);
    ecs_iter_t fit = ecs_each_id(it->world, ecs_id(FontStatus));
    while (ecs_each_next(&fit)) {
        const FontStatus *status = ecs_field(&fit, FontStatus, 0);
        for (i = 0; i < fit.count; i ++) {
            const Font *font = ecs_get(it->world, fit.entities[i], Font);
            if (status[i].state != FontLoading) {
                continue;
            }

            if (working && loading < BOX_TEXT_WARN_NAMES) {
                char *name = ecs_get_path(it->world, fit.entities[i]);
                ecs_strbuf_appendstr(&names, loading ? ", " : " (");
                ecs_strbuf_appendstr(&names, name);
                if (font && font->path) {
                    ecs_strbuf_append(&names, " %s", font->path);
                }

                ecs_os_free(name);
            }

            loading ++;
        }
    }

    if (working && loading > BOX_TEXT_WARN_NAMES) {
        ecs_strbuf_append(&names, ", %d more", loading - BOX_TEXT_WARN_NAMES);
    }

    if (working && loading) {
        ecs_strbuf_appendstr(&names, ")");
    }

    char *list = ecs_strbuf_get(&names);
    ecs_warn("headless text wait timed out after %.1f s: %d font or glyph job(s) in flight, "
        "%d job(s) running, %d font load(s) not applied yet%s", waited, working,
        box_jobs_running(it->world), loading, list ? list : "");
    ecs_os_free(list);
done:
    return;
}

/** Lay out the texts whose atlas page grew or whose font changed.
 *
 * @param it The iterator of the system
 */
static void BoxTextRefresh(
    ecs_iter_t *it)
{
    box_text_state_t *s = box_text_state_get(it->world);
    if (s) {
        box_text_relayout(it->world, s, false);
    }
}

/** Create the registry of a world and box.text.DefaultFont.
 *
 * @param world The world
 */
static void box_text_state_init(
    ecs_world_t *world)
{
    box_text_state_t *s = ecs_os_calloc_t(box_text_state_t);
    ecs_map_init(&s->fonts, NULL);
    ecs_vec_init_t(NULL, &s->released, uint32_t, 0);
    ecs_vec_init_t(NULL, &s->placed, box_text_placed_t, 0);
    ecs_vec_init_t(NULL, &s->lines, float, 0);
    ecs_vec_init_t(NULL, &s->carets, box_text_caret_t, 0);
    ecs_vec_init_t(NULL, &s->rows, box_text_row_t, 0);
    ecs_vec_init_t(NULL, &s->texts, ecs_entity_t, 0);
    s->next_page = BOX_TEXT_PAGE_FIRST;
    s->watch = box_file_watch_kind(world, "font", box_text_font_changed, NULL);
    s->sync = ecs_os_calloc_t(box_text_sync_t);
    s->sync->lock = ecs_os_mutex_new();
    s->sync->refs = 1;
    s->query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "TextTexts" }),
        .terms = {{ .id = ecs_id(Text), .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto,
        .flags = EcsQueryMatchDisabled
    });
    s->shown = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "TextShown" }),
        .terms = {{ .id = ecs_id(TextGlyphs), .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto,
        .flags = EcsQueryMatchDisabled
    });
    ecs_singleton_ensure(world, TextRegistry)->state = s;
    s->default_font = ecs_entity(world, { .name = "DefaultFont" });
    ecs_set(world, s->default_font, Font, {
        .path = BOX_TEXT_DEFAULT_FONT,
        .atlas_pages = BOX_TEXT_ATLAS_PAGES_DEFAULT
    });
}

/** Construct Text values with the documented size and box.text.DefaultFont.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info, whose context points to the default font entity
 */
static void box_text_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    const ecs_entity_t *font = ti->hooks.ctx;
    Text *values = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        values[i] = (Text){ .size = BOX_TEXT_SIZE_DEFAULT, .font = *font };
    }
}

/** Construct Font values without a path and with the documented atlas budget.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Unused
 */
static void box_font_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Font *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Font){ .atlas_pages = BOX_TEXT_ATLAS_PAGES_DEFAULT };
    }
}

/** Free the default font entity the Text constructor reads.
 *
 * @param ctx The entity
 */
static void box_text_ctor_free(
    void *ctx)
{
    ecs_os_free(ctx);
}

/** Release idle atlas pages beyond the budget of each font once a layout, a removed Text or a
 * Font change may have left some.
 *
 * @param it The iterator of the system
 */
static void BoxTextTrim(
    ecs_iter_t *it)
{
    box_text_state_t *s = box_text_state_get(it->world);
    if (s && s->trim) {
        s->trim = false;
        box_text_atlas_trim(it->world, s);
    }
}

/** Register the observers and systems of box.text.
 *
 * @param world The world
 */
static void box_text_systems_init(
    ecs_world_t *world)
{
    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TextOnSet" }),
        .query.terms = {{ .id = ecs_id(Text) }},
        .query.flags = EcsQueryMatchDisabled,
        .events = { EcsOnSet },
        .callback = BoxTextOnSet
    });
    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TextOnRemove" }),
        .query.terms = {{ .id = ecs_id(Text) }},
        .query.flags = EcsQueryMatchDisabled,
        .events = { EcsOnRemove },
        .callback = BoxTextOnRemove
    });
    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "FontOnSet" }),
        .query.terms = {{ .id = ecs_id(Font) }},
        .events = { EcsOnSet },
        .callback = BoxFontOnSet
    });
    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "FontOnRemove" }),
        .query.terms = {{ .id = ecs_id(Font) }},
        .events = { EcsOnRemove },
        .callback = BoxFontOnRemove
    });
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "TextFrame" }),
        .phase = EcsPreFrame,
        .run = BoxTextFrame
    });
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "TextRefresh" }),
        .phase = EcsOnStore,
        .run = BoxTextRefresh,
        .immediate = true
    });
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "TextTrim" }),
        .phase = EcsOnStore,
        .run = BoxTextTrim
    });
}

void BoxTextImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxText);
    ECS_IMPORT(world, BoxLayout);
    ECS_IMPORT(world, BoxApp);
    ECS_IMPORT(world, BoxJobs);

    ECS_META_COMPONENT(world, Font);
    ECS_META_COMPONENT(world, FontState);
    ECS_META_COMPONENT(world, FontStatus);
    ECS_META_COMPONENT(world, TextWrap);
    ECS_META_COMPONENT(world, Text);
    ECS_META_COMPONENT(world, TextBounds);
    ECS_META_COMPONENT(world, TextCaret);
    ECS_META_COMPONENT(world, TextSpan);
    ECS_META_COMPONENT(world, TextSpans);
    box_owned_hooks(world, Font, BOX_OWNED_STR(Font, path));
    ecs_set_hooks(world, Font, { .ctor = box_font_ctor });
    box_owned_hooks(world, FontStatus, BOX_OWNED_STR(FontStatus, error));
    box_owned_hooks(world, Text, BOX_OWNED_STR(Text, value));
    box_owned_hooks(world, TextSpans, BOX_OWNED_VEC(TextSpans, spans, TextSpan));

    ECS_COMPONENT_DEFINE(world, TextGlyphs);
    box_owned_hooks(world, TextGlyphs,
        BOX_OWNED_VEC(TextGlyphs, glyphs, box_text_glyph_t),
        BOX_OWNED_VEC(TextGlyphs, carets, box_text_caret_t),
        BOX_OWNED_VEC(TextGlyphs, rows, box_text_row_t));
    ECS_COMPONENT_DEFINE(world, TextRegistry);
    ecs_set_hooks(world, TextRegistry, {
        .ctor = flecs_default_ctor,
        .dtor = box_text_registry_dtor,
        .move = box_text_registry_move
    });
    ecs_add_pair(world, ecs_id(Text), EcsWith, ecs_id(TextGlyphs));
    ecs_add_pair(world, ecs_id(Text), EcsWith, ecs_id(TextBounds));

    box_text_systems_init(world);
    box_text_state_init(world);
    box_layout_text_register(world, &(box_layout_text_t){
        .component = ecs_id(Text),
        .bounds = ecs_id(TextBounds),
        .measure = box_text_measure,
        .wrap = box_text_wrap_to
    });

    ecs_entity_t *font = ecs_os_malloc_t(ecs_entity_t);
    *font = box_text_state_get(world)->default_font;
    ecs_set_hooks(world, Text, {
        .ctor = box_text_ctor,
        .ctx = font,
        .ctx_free = box_text_ctor_free
    });
}
