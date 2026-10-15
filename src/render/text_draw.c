/**
 * @file render/text_draw.c
 * @brief Glyphs of the texts as instances of the 2D batches and the atlas pages they sample.
 *
 * Every glyph of a Text is a slot of a glyph batch keyed like a 2D shape plus its atlas page,
 * drawn with etc/shaders/shape2d.wgsl, which reads the page as a multi channel distance field.
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
    key->blend = box_draws_blend(ecs_get(world, e, BlendMode), alpha);
    key->back_face = back_face ? (uint8_t)*back_face : (uint8_t)BackFaceCull;
    key->wrap = WrapClamp;
    key->dynamic = ecs_has_id(world, e, DynamicTransform) ||
        ecs_has_id(world, e, DynamicMaterial) ||
        ecs_get_target_for_id(world, e, EcsChildOf, DynamicMaterial) != 0;
}

/** Write the instance of a glyph: its quad in world space, the material and the atlas area.
 *
 * @param out The instance
 * @param transform World transform of the text, NULL for the identity
 * @param g The glyph
 * @param material Material components of the text
 */
static void box_draws_glyph(
    box_render_instance_t *out,
    const WorldTransform *transform,
    const box_text_glyph_t *g,
    const box_draw_material_t *material)
{
    mat4 m;
    int32_t i;
    if (transform) {
        glm_mat4_copy(ECS_CONST_CAST(vec4*, transform->m), m);
    } else {
        glm_mat4_identity(m);
    }

    box_draws_material(out, material);
    for (i = 0; i < 4; i ++) {
        out->model[0][i] = m[0][i] * g->width;
        out->model[1][i] = m[1][i] * g->height;
        out->model[2][i] = m[2][i];
        out->model[3][i] = m[0][i] * g->x + m[1][i] * g->y + m[3][i];
    }

    out->emissive[3] = 0.0f;
    out->material[0] = g->range;
    out->material[1] = g->range;
    out->material[2] = 0.0f;
    out->material[3] = 0.0f;
    ecs_os_memcpy(out->uv, g->uv, ECS_SIZEOF(g->uv));
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

void box_draws_text_update(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e)
{
    const TextGlyphs *tg = ecs_get(world, e, TextGlyphs);
    bool drawn = tg && ecs_has(world, e, Text) && !ecs_has_id(world, e, EcsPrefab);
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
    box_draws_text_key(world, d, e, box_draws_alpha(&material), &key);
    TextSlots *ts = ecs_get_mut(world, e, TextSlots);
    if (!box_draws_text_fits(d, ts, glyphs, count, &key)) {
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
    float layer = count ? box_draws_layer(world, e, true) : 0.0f;
    for (i = 0; i < count; i ++) {
        const box_draw_glyph_slot_t *at = ecs_vec_get_t(&ts->slots, box_draw_glyph_slot_t, i);
        box_draw_batch_t *b = box_draws_batch(d, at->batch);
        box_render_instance_t *inst = ecs_vec_get_t(
            &b->instances, box_render_instance_t, at->slot);
        box_draws_glyph(inst, transform, &glyphs[i], &material);
        box_draws_lift(inst, transform, layer);
        box_draws_slot_changed(b, at->slot);
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
            box_draws_mark(world, d, it.entities[i]);
        }
    }

    it = ecs_query_iter(world, d->dynamic_text_materials);
    while (ecs_query_next(&it)) {
        int32_t count = box_transform_dynamic(world, it.table) ? 0 : it.count;
        for (i = 0; i < count; i ++) {
            box_draws_mark(world, d, it.entities[i]);
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
        .terms = {
            { .id = ecs_id(Text), .inout = EcsInOutNone },
            { .id = ecs_id(TextSlots), .inout = EcsInOutNone },
            { .id = DynamicTransform, .inout = EcsInOutNone }
        },
        .cache_kind = EcsQueryCacheAuto
    });
    d->dynamic_text_materials = ecs_query(world, {
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
