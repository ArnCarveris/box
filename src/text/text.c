/**
 * @file text/text.c
 * @brief Text components, the font registry and the systems that keep texts laid out.
 *
 * Setting a Text lays it out at once; a font that loads or an atlas page that grows lays out
 * the texts of its font again. Headless frames wait for the font and glyph jobs in flight.
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
    int32_t i;
    if (!it->other_table || !ecs_table_get_type(it->other_table)->count) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        TextGlyphs *glyphs = ecs_get_mut(it->world, it->entities[i], TextGlyphs);
        if (glyphs) {
            ecs_vec_clear(&glyphs->glyphs);
        }

        ecs_set(it->stage, it->entities[i], TextBounds, {0});
    }

done:
    return;
}

/** Load a font again when its Font changes and a Text uses it.
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
        if (font && font->requested) {
            box_text_font_load(it->world, s, font);
        }
    }

    s->relayout = true;
done:
    return;
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

/** In a headless run, wait for the font and glyph jobs in flight so a frame always sees the
 * same atlas.
 *
 * @param it The iterator of the system
 */
static void BoxTextFrame(
    ecs_iter_t *it)
{
    box_text_state_t *s = box_text_state_get(it->world);
    const App *app = ecs_singleton_get(it->world, App);
    ecs_time_t start = {0};
    if (!s || !app || !app->headless) {
        goto done;
    }

    ecs_time_measure(&start);
    while (box_text_working(s->sync) || box_jobs_running(it->world)) {
        ecs_time_t now = start;
        if (ecs_time_measure(&now) > BOX_TEXT_HEADLESS_WAIT) {
            break;
        }

        ecs_os_sleep(0, 200 * 1000);
    }

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
    ecs_vec_init_t(NULL, &s->texts, ecs_entity_t, 0);
    s->next_page = BOX_TEXT_PAGE_FIRST;
    s->sync = ecs_os_calloc_t(box_text_sync_t);
    s->sync->lock = ecs_os_mutex_new();
    s->sync->refs = 1;
    s->query = ecs_query(world, {
        .terms = {{ .id = ecs_id(Text), .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto
    });
    ecs_singleton_ensure(world, TextRegistry)->state = s;
    s->default_font = ecs_entity(world, { .name = "DefaultFont" });
    ecs_set(world, s->default_font, Font, { .path = BOX_TEXT_DEFAULT_FONT });
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

/** Free the default font entity the Text constructor reads.
 *
 * @param ctx The entity
 */
static void box_text_ctor_free(
    void *ctx)
{
    ecs_os_free(ctx);
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
        .events = { EcsOnSet },
        .callback = BoxTextOnSet
    });
    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TextOnRemove" }),
        .query.terms = {{ .id = ecs_id(Text) }},
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
    box_owned_hooks(world, Font, BOX_OWNED_STR(Font, path));
    box_owned_hooks(world, FontStatus, BOX_OWNED_STR(FontStatus, error));
    box_owned_hooks(world, Text, BOX_OWNED_STR(Text, value));

    ECS_COMPONENT_DEFINE(world, TextGlyphs);
    box_owned_hooks(world, TextGlyphs, BOX_OWNED_VEC(TextGlyphs, glyphs, box_text_glyph_t));
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
