/**
 * @file render/text_draw.c
 * @brief Glyphs of the texts as instances of the 2D batches and the atlas pages they sample.
 *
 * Every glyph of a Text is a slot of a glyph batch keyed like a 2D shape plus its atlas page,
 * drawn with etc/shaders/shape2d.wgsl, which reads the page as a multi channel distance field.
 * TextSpans color glyphs through the color of their instances, in the same batches.
 *
 * Pages reach the backend as texture updates: whole when new or grown, then per frame the area
 * of the glyphs completed since the last one.
 */

#include "draw.h"

ECS_COMPONENT_DECLARE(TextSlots);

/** State of the atlas page extract */
typedef struct box_draws_atlas_t {
    const void *backend;    /**< Context of the backend the pages were sent to */
    void (*render)(void*, const box_render_packet_t*); /**< Render callback of that backend */
    int32_t generation;         /**< Generation of that backend, see box_render_backend_t */
} box_draws_atlas_t;

/** Construct TextSlots values without slots.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti The type info
 */
static void box_text_slots_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    TextSlots *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        ecs_vec_init_t(NULL, &values[i].slots, box_draw_glyph_slot_t, 0);
        values[i].rank = 0;
    }
}

/** Free TextSlots values.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti The type info
 */
static void box_text_slots_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    TextSlots *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        ecs_vec_fini_t(NULL, &values[i].slots, box_draw_glyph_slot_t);
    }
}

/** Move TextSlots values with the entity they belong to.
 *
 * @param dst The destination values
 * @param src The source values
 * @param count Number of values
 * @param ti The type info
 */
static void box_text_slots_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    box_text_slots_dtor(dst, count, ti);
    ecs_os_memcpy(dst, src, ti->size * count);
    box_text_slots_ctor(src, count, ti);
}

/** Copy TextSlots values: a copy is a new entity that has no slots yet.
 *
 * @param dst The destination values
 * @param src The source values
 * @param count Number of values
 * @param ti The type info
 */
static void box_text_slots_copy(
    void *dst,
    const void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    TextSlots *values = dst;
    int32_t i;
    (void)src;
    (void)ti;
    for (i = 0; i < count; i ++) {
        ecs_vec_clear(&values[i].slots);
        values[i].rank = 0;
    }
}

/** Key of the glyph quad, which has one mesh.
 *
 * @param value Unused
 * @param key The key
 */
static void box_text_quad_key(
    const void *value,
    box_shape_key_t *key)
{
    (void)value;
    (void)key;
}

/** Generate a unit quad in the XY plane facing +Z, v 0 at the top.
 *
 * @param value Unused
 * @param key Unused
 * @param mesh The mesh
 */
static void box_text_quad_generate(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    vec3 normal = { 0.0f, 0.0f, 1.0f };
    (void)value;
    (void)key;
    int32_t a = box_mesh_vertex(mesh, (vec3){ -0.5f, -0.5f, 0.0f }, normal, 0.0f, 1.0f);
    int32_t b = box_mesh_vertex(mesh, (vec3){ 0.5f, -0.5f, 0.0f }, normal, 1.0f, 1.0f);
    int32_t c = box_mesh_vertex(mesh, (vec3){ 0.5f, 0.5f, 0.0f }, normal, 1.0f, 0.0f);
    int32_t d = box_mesh_vertex(mesh, (vec3){ -0.5f, 0.5f, 0.0f }, normal, 0.0f, 0.0f);
    box_mesh_triangle(mesh, a, b, c);
    box_mesh_triangle(mesh, a, c, d);
}

void box_draws_text_remove(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e)
{
    TextSlots *ts = ecs_get_mut(world, e, TextSlots);
    int32_t i;
    if (!ts) {
        goto done;
    }

    for (i = ecs_vec_count(&ts->slots) - 1; i >= 0; i --) {
        box_draw_glyph_slot_t at = *ecs_vec_get_t(&ts->slots, box_draw_glyph_slot_t, i);
        box_draws_slot_remove(world, d, at.batch, at.slot);
        ts = ecs_get_mut(world, e, TextSlots);
        ecs_vec_remove_last(&ts->slots);
    }

done:
    return;
}

/** Compute the batch key of the glyphs of a Text, without the atlas page.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 * @param alpha Resolved alpha of the text
 * @param key Receives the key
 */
static void box_draws_text_key(
    ecs_world_t *world,
    const box_render_draws_t *d,
    ecs_entity_t e,
    float alpha,
    box_draw_key_t *key)
{
    const BackFaceMode *back_face = ecs_get(world, e, BackFaceMode);
    ecs_os_memset_t(key, 0, box_draw_key_t);
    key->scene = box_render_scene_of(world, e);
    key->mesh = d->text_mesh;
    key->pipeline = BOX_RENDER_SHADER_SHAPE2D;
    key->blend = box_draws_blend(ecs_get(world, e, BlendMode), alpha, false);
    key->back_face = back_face ? (uint8_t)*back_face : (uint8_t)BackFaceCull;
    key->wrap = WrapClamp;
    key->dynamic_transform = box_transform_dynamic(world, ecs_get_table(world, e)) ||
        ecs_has_id(world, e, TransitionTransform);
    key->dynamic_material = ecs_has_id(world, e, DynamicMaterial) ||
        ecs_get_target_for_id(world, e, EcsChildOf, DynamicMaterial) != 0;
}

/** Write the instance of a glyph: its quad in world space into the transform, the material and
 * the atlas area into the shading.
 *
 * @param transform_out The transform of the glyph, NULL to leave it
 * @param shading_out The shading of the glyph, NULL to leave it
 * @param transform World transform of the text, NULL for the identity
 * @param g The glyph
 * @param material Material components of the text
 */
static void box_draws_glyph(
    box_render_transform_t *transform_out,
    box_render_shading_t *shading_out,
    const WorldTransform *transform,
    const box_text_glyph_t *g,
    const box_draw_material_t *material)
{
    mat4 m;
    int32_t r;
    if (transform) {
        glm_mat4_copy(ECS_CONST_CAST(vec4*, transform->m), m);
    } else {
        glm_mat4_identity(m);
    }

    for (r = 0; transform_out && r < 3; r ++) {
        transform_out->rows[r][0] = m[0][r] * g->width;
        transform_out->rows[r][1] = m[1][r] * g->height;
        transform_out->rows[r][2] = m[2][r];
        transform_out->rows[r][3] = m[0][r] * g->x + m[1][r] * g->y + m[3][r];
    }

    if (shading_out) {
        box_draws_material(shading_out, material);
        shading_out->emissive[3] = 0.0f;
        shading_out->material[0] = g->range;
        shading_out->material[1] = g->range;
        shading_out->material[2] = 0.0f;
        shading_out->material[3] = 0.0f;
        ecs_os_memcpy(shading_out->uv, g->uv, ECS_SIZEOF(g->uv));
    }
}

/** Check whether the glyphs of a Text keep the batches their slots are in.
 *
 * @param d The state
 * @param ts The slots of the Text
 * @param glyphs The glyphs
 * @param count Number of glyphs
 * @param key The key of the text without the atlas page
 * @return Whether every glyph has a slot in the batch of its key
 */
static bool box_draws_text_fits(
    box_render_draws_t *d,
    const TextSlots *ts,
    const box_text_glyph_t *glyphs,
    int32_t count,
    box_draw_key_t *key)
{
    int32_t i;
    bool result = ecs_vec_count(&ts->slots) == count;
    for (i = 0; i < count && result; i ++) {
        const box_draw_glyph_slot_t *at = ecs_vec_get_t(&ts->slots, box_draw_glyph_slot_t, i);
        key->atlas = glyphs[i].page;
        result = !ecs_os_memcmp_t(&box_draws_batch(d, at->batch)->key, key, box_draw_key_t);
    }

    return result;
}

/** Get the lowest span alpha the glyphs of a Text take, which decides how its batch blends.
 *
 * @param spans The spans of the Text
 * @param length Byte length of Text.value
 * @param glyphs The glyphs
 * @param count Number of glyphs
 * @return The alpha from 0 to 1, 1 when no glyph is in a span
 */
static float box_draws_text_spans_alpha(
    const TextSpans *spans,
    int32_t length,
    const box_text_glyph_t *glyphs,
    int32_t count)
{
    uint8_t lowest = 255;
    int32_t i;
    for (i = 0; i < count && lowest; i ++) {
        const rgba *color = box_text_span_color(spans, length, glyphs[i].offset);
        if (color && color->a < lowest) {
            lowest = color->a;
        }
    }

    return (float)lowest / 255.0f;
}

/** Write the instance of a glyph in the color of the span it falls in.
 *
 * @param transform_out The transform of the glyph, NULL to leave it
 * @param shading_out The shading of the glyph, NULL to leave it
 * @param transform World transform of the text, NULL for the identity
 * @param g The glyph
 * @param material Material components of the text
 * @param color Display encoded color of the span, NULL for the color of the entity
 */
static void box_draws_glyph_span(
    box_render_transform_t *transform_out,
    box_render_shading_t *shading_out,
    const WorldTransform *transform,
    const box_text_glyph_t *g,
    const box_draw_material_t *material,
    const rgba *color)
{
    box_draw_material_t m;
    Rgb rgb;
    Opacity opacity;
    if (!color) {
        box_draws_glyph(transform_out, shading_out, transform, g, material);
        goto done;
    }

    m = *material;
    rgb = (Rgb){ color->r, color->g, color->b };
    opacity.value = material->opacity ? glm_clamp(material->opacity->value, 0.0f, 1.0f) : 1.0f;
    opacity.value *= (float)color->a / 255.0f;
    m.color = &rgb;
    m.opacity = &opacity;
    box_draws_glyph(transform_out, shading_out, transform, g, &m);
done:
    return;
}

void box_draws_text_update(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    uint8_t kinds)
{
    const TextGlyphs *tg = ecs_get(world, e, TextGlyphs);
    bool drawn = tg && ecs_has(world, e, Text) && !ecs_has_id(world, e, EcsPrefab) &&
        !ecs_has_id(world, e, EcsDisabled);
    int32_t i, count = drawn ? ecs_vec_count(&tg->glyphs) : 0;
    const box_text_glyph_t *glyphs = drawn ? ecs_vec_first(&tg->glyphs) : NULL;
    box_draw_tint_t tint;
    box_draw_key_t key;
    float fade = count ? box_draws_fade(world, e, &tint) : 1.0f;
    box_draw_material_t material = {
        .color = ecs_get(world, e, Rgb),
        .opacity = ecs_get(world, e, Opacity),
        .emissive = ecs_get(world, e, Emissive),
        .tint = ecs_get(world, e, Tint),
        .inherited = count && tint.keep < 1.0f ? &tint : NULL,
        .fade = fade
    };
    const TextSpans *spans = count ? ecs_get(world, e, TextSpans) : NULL;
    const Text *text = spans && ecs_vec_count(&spans->spans) ? ecs_get(world, e, Text) : NULL;
    int32_t length = text && text->value ? (int32_t)strlen(text->value) : 0;
    float alpha = box_draws_alpha(&material);
    if (length) {
        alpha *= box_draws_text_spans_alpha(spans, length, glyphs, count);
    } else {
        spans = NULL;
    }

    box_draws_text_key(world, d, e, alpha, &key);
    TextSlots *ts = ecs_get_mut(world, e, TextSlots);
    if (!box_draws_text_fits(d, ts, glyphs, count, &key)) {
        kinds = BOX_DRAW_ALL;
        box_draws_text_remove(world, d, e);
        for (i = 0; i < count; i ++) {
            key.atlas = glyphs[i].page;
            int32_t batch = box_draws_batch_ensure(d, &key);
            int32_t slot = box_draws_glyph_add(d, batch, e, i);
            ts = ecs_get_mut(world, e, TextSlots);
            *ecs_vec_append_t(NULL, &ts->slots, box_draw_glyph_slot_t) =
                (box_draw_glyph_slot_t){ batch, slot };
        }
    }

    const WorldTransform *transform = ecs_get(world, e, WorldTransform);
    material.clip = count ? box_draws_clip_of(world, d, e) : 0;
    material.rank = ts ? ts->rank : 0;
    for (i = 0; i < count; i ++) {
        const box_draw_glyph_slot_t *at = ecs_vec_get_t(&ts->slots, box_draw_glyph_slot_t, i);
        box_draw_batch_t *b = box_draws_batch(d, at->batch);
        uint8_t written = kinds | box_draws_dynamic_kinds(b);
        box_render_transform_t *t = written & BOX_DRAW_TRANSFORM
            ? box_draws_slot_transform(b, at->slot) : NULL;
        box_render_shading_t *sh = written & BOX_DRAW_SHADING
            ? box_draws_slot_shading(b, at->slot) : NULL;
        if (spans) {
            box_draws_glyph_span(t, sh, transform, &glyphs[i], &material,
                box_text_span_color(spans, length, glyphs[i].offset));
        } else {
            box_draws_glyph(t, sh, transform, &glyphs[i], &material);
        }

        box_draws_slot_changed(b, at->slot, kinds & BOX_DRAW_BOTH);
    }
}

void box_draws_text_dynamic(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    ecs_iter_t it = ecs_query_iter(world, d->dynamic_texts);
    int32_t i;
    while (ecs_query_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            box_draws_mark(world, d, it.entities[i], 0);
        }
    }

    it = ecs_query_iter(world, d->dynamic_text_materials);
    while (ecs_query_next(&it)) {
        int32_t count = box_transform_dynamic(world, it.table) ? 0 : it.count;
        for (i = 0; i < count; i ++) {
            box_draws_mark(world, d, it.entities[i], 0);
        }
    }
}

/** Send the area of an atlas page the backend does not have: the whole page when the backend
 * has none of its size, else the area the glyphs completed since the last frame cover.
 *
 * @param p The packet
 * @param page The page
 * @param whole Send the whole page
 */
static void box_draws_atlas_page(
    box_render_packet_t *p,
    box_text_page_t *page,
    bool whole)
{
    int32_t x = page->dirty[0], y = page->dirty[1], row;
    int32_t w = page->dirty[2] - x, h = page->dirty[3] - y;
    if (whole || page->sent != page->size) {
        x = y = 0;
        w = h = page->size;
    }

    if (w <= 0 || h <= 0) {
        goto done;
    }

    box_render_texture_region_t *region = box_render_packet_resource(p,
        BOX_RENDER_TEXTURE, BOX_RENDER_UPDATE, page->handle,
        (uint32_t)(sizeof(box_render_texture_region_t) + (size_t)w * (size_t)h * 4));
    region->texture.width = (uint32_t)page->size;
    region->texture.height = (uint32_t)page->size;
    region->texture.single_channel = false;
    region->x = (uint32_t)x;
    region->y = (uint32_t)y;
    region->width = (uint32_t)w;
    region->height = (uint32_t)h;
    uint8_t *dst = (uint8_t*)(region + 1);
    for (row = 0; row < h; row ++) {
        ecs_os_memcpy(dst + row * w * 4, page->pixels + ((y + row) * page->size + x) * 4, w * 4);
    }

    page->sent = page->size;
    ecs_os_memset(page->dirty, 0, ECS_SIZEOF(page->dirty));
done:
    return;
}

/** Send the areas of the atlas pages the backend does not have and destroy the released pages.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractTextAtlas(
    ecs_iter_t *it)
{
    box_draws_atlas_t *state = it->ctx;
    box_text_state_t *s = box_text_state_get(it->world);
    box_render_packet_t *p = box_render_packet(it->world);
    const box_render_backend_t *backend = box_render_get_backend(it->world);
    bool resend = backend->ctx != state->backend || backend->render != state->render ||
        backend->generation != state->generation;
    int32_t i;
    if (!s) {
        goto done;
    }

    state->backend = backend->ctx;
    state->render = backend->render;
    state->generation = backend->generation;
    for (i = 0; i < ecs_vec_count(&s->released) && !resend; i ++) {
        box_render_packet_resource(p, BOX_RENDER_TEXTURE, BOX_RENDER_DESTROY,
            *ecs_vec_get_t(&s->released, uint32_t, i), 0);
    }

    ecs_vec_clear(&s->released);
    ecs_map_iter_t mit = ecs_map_iter(&s->fonts);
    while (ecs_map_next(&mit)) {
        box_text_font_t *font = ecs_map_ptr(&mit);
        for (i = 0; i < ecs_vec_count(&font->pages); i ++) {
            box_draws_atlas_page(p, *ecs_vec_get_t(&font->pages, box_text_page_t*, i), resend);
        }
    }

done:
    return;
}

void box_draws_text_init(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    ECS_COMPONENT_DEFINE(world, TextSlots);
    ecs_set_hooks(world, TextSlots, {
        .ctor = box_text_slots_ctor,
        .dtor = box_text_slots_dtor,
        .move = box_text_slots_move,
        .copy = box_text_slots_copy
    });
    ecs_add_pair(world, ecs_id(Text), EcsWith, ecs_id(DrawSlot));
    ecs_add_pair(world, ecs_id(Text), EcsWith, ecs_id(TextSlots));
    if (cache) {
        box_shape_t *quad = box_mesh_cache_add_shape(cache, &(box_shape_desc_t){
            .component = ecs_id(TextSlots),
            .key = box_text_quad_key,
            .generate = box_text_quad_generate
        });
        d->text_mesh = quad ? box_mesh_cache_acquire(cache, quad, NULL) : 0;
    }

    d->dynamic_texts = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "RenderDynamicTexts" }),
        .terms = {
            { .id = ecs_id(Text), .inout = EcsInOutNone },
            { .id = ecs_id(TextSlots), .inout = EcsInOutNone },
            { .id = DynamicTransform, .inout = EcsInOutNone }
        },
        .cache_kind = EcsQueryCacheAuto
    });
    d->dynamic_text_materials = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "RenderDynamicTextMaterials" }),
        .terms = {
            { .id = ecs_id(Text), .inout = EcsInOutNone },
            { .id = ecs_id(TextSlots), .inout = EcsInOutNone },
            {
                .id = DynamicMaterial,
                .src.id = EcsSelf | EcsUp,
                .trav = EcsChildOf,
                .inout = EcsInOutNone
            },
            { .id = DynamicTransform, .src.id = EcsSelf, .inout = EcsInOutNone, .oper = EcsNot }
        },
        .cache_kind = EcsQueryCacheAuto
    });
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RenderExtractTextAtlas" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractTextAtlas,
        .ctx = ecs_os_calloc_t(box_draws_atlas_t),
        .ctx_free = ecs_os_api.free_
    });
}
