/**
 * @file Tiling.c
 * @brief Tests for texture transforms, world space tiling and wrap modes.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void Tiling_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
}

void Tiling_teardown(void)
{
    uint32_t i;
    for (i = 0; i < packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, i);

        free(cmd->vertices);
        free(cmd->indices);
    }

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Tiling_component_defaults(void)
{
    // textures/tiling
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, TextureTransform);
    ecs_add(w, e, TextureTiling);

    const TextureTransform *xform = ecs_get(w, e, TextureTransform);
    test_flt(xform->scale_x, 1.0f);
    test_flt(xform->scale_y, 1.0f);
    test_flt(xform->offset_x, 0.0f);
    test_flt(xform->offset_y, 0.0f);

    const TextureTiling *tiling = ecs_get(w, e, TextureTiling);
    test_flt(tiling->meters_x, 1.0f);
    test_flt(tiling->meters_y, 1.0f);
    test_bool(tiling->fit, false);
    test_bool(tiling->allow_partial, false);
    test_int(tiling->min_tiles, 0);
    test_int(tiling->shape, TilingBox);
}

void Tiling_untiled_instance_is_the_identity(void)
{
    // textures/tiling
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {4, 1, 2});
    box_render_extract(w, &packet);

    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_flt(u->data.uv_transform[0], 1.0f);
    test_flt(u->data.uv_transform[1], 1.0f);
    test_flt(u->data.uv_transform[2], 0.0f);
    test_flt(u->data.uv_transform[3], 0.0f);
    test_int(u->data.uv_mode, 0);

    float local[3] = { 2.0f, 0.5f, -1.0f };
    float normal[3] = { 0.0f, 0.0f, -1.0f };
    float mesh_uv[2] = { 0.25f, 0.75f };
    float uv[2];
    box_tiling_uv(u->data.uv_mode, u->data.uv_transform, local, normal, mesh_uv, uv);
    test_flt(uv[0], 0.25f);
    test_flt(uv[1], 0.75f);
}

void Tiling_transform_reaches_the_instance(void)
{
    // textures/tiling
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, TextureTransform, {2.0f, 3.0f, 0.25f, 0.5f});
    box_render_extract(w, &packet);

    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_flt(u->data.uv_transform[0], 2.0f);
    test_flt(u->data.uv_transform[1], 3.0f);
    test_flt(u->data.uv_transform[2], 0.25f);
    test_flt(u->data.uv_transform[3], 0.5f);

    float local[3] = { 0.0f, 0.0f, 0.0f };
    float normal[3] = { 0.0f, 0.0f, -1.0f };
    float mesh_uv[2] = { 0.5f, 0.5f };
    float uv[2];
    box_tiling_uv(u->data.uv_mode, u->data.uv_transform, local, normal, mesh_uv, uv);
    test_flt(uv[0], 1.25f);
    test_flt(uv[1], 2.0f);
}

void Tiling_box_projection_tiles_every_face(void)
{
    // textures/tiling
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {4, 1, 2});
    ecs_set(w, e, TextureTiling, { .meters_x = 1, .meters_y = 1, .shape = TilingBox });
    box_render_extract(w, &packet);

    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_int(box_tiling_projection(u->data.uv_mode), BOX_TILE_BOX);
    test_flt(u->data.uv_transform[0], 1.0f);
    test_flt(u->data.uv_transform[1], 1.0f);

    float size[3] = { 4.0f, 1.0f, 2.0f };
    float span[2];
    box_tiling_span(BOX_TILE_BOX, size, span);
    test_flt(span[0], 4.0f);
    test_flt(span[1], 1.0f);
    test_flt(box_tiling_count(span[0], 1.0f, false, false, 0), 4.0f);

    float mesh_uv[2] = { 0.5f, 0.5f };
    float uv[2];
    float front[3] = { 2.0f, 0.5f, -1.0f };
    float front_n[3] = { 0.0f, 0.0f, -1.0f };
    box_tiling_uv(u->data.uv_mode, u->data.uv_transform, front, front_n, mesh_uv, uv);
    test_flt(uv[0], 2.0f);
    test_flt(uv[1], 0.5f);

    float side[3] = { 2.0f, 0.5f, 1.0f };
    float side_n[3] = { 1.0f, 0.0f, 0.0f };
    box_tiling_uv(u->data.uv_mode, u->data.uv_transform, side, side_n, mesh_uv, uv);
    test_flt(uv[0], 1.0f);
    test_flt(uv[1], 0.5f);

    float top_n[3] = { 0.0f, 1.0f, 0.0f };
    box_tiling_uv(u->data.uv_mode, u->data.uv_transform, side, top_n, mesh_uv, uv);
    test_flt(uv[0], 2.0f);
    test_flt(uv[1], 1.0f);
}

void Tiling_cylinder_projection_tiles_the_circumference(void)
{
    // textures/tiling
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {4, 1, 2});
    ecs_set(w, e, TextureTiling, { .meters_x = 1, .meters_y = 1, .shape = TilingCylinder });
    box_render_extract(w, &packet);

    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_int(box_tiling_projection(u->data.uv_mode), BOX_TILE_CYLINDER);
    test_flt(u->data.uv_transform[0], 1.0f);
    test_flt(u->data.uv_transform[1], 1.0f);

    float size[3] = { 4.0f, 1.0f, 2.0f };
    float span[2];
    box_tiling_span(BOX_TILE_CYLINDER, size, span);
    test_flt(span[0], BOX_TILING_PI * 4.0f);
    test_flt(span[1], 1.0f);
    test_flt(box_tiling_count(span[0], 1.0f, false, false, 0), BOX_TILING_PI * 4.0f);

    float mesh_uv[2] = { 0.5f, 0.5f };
    float uv[2];
    float quarter[3] = { 0.0f, 0.5f, 2.0f };
    float normal[3] = { 0.0f, 0.0f, 1.0f };
    box_tiling_uv(u->data.uv_mode, u->data.uv_transform, quarter, normal, mesh_uv, uv);
    test_flt(uv[0], BOX_TILING_PI * 0.5f * 2.0f);
    test_flt(uv[1], 0.5f);
}

void Tiling_radial_projection_tiles_radius_and_arc(void)
{
    // textures/tiling
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {4, 1, 2});
    ecs_set(w, e, TextureTiling, { .meters_x = 1, .meters_y = 1, .shape = TilingRadial });
    box_render_extract(w, &packet);

    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_int(box_tiling_projection(u->data.uv_mode), BOX_TILE_RADIAL);
    test_flt(u->data.uv_transform[0], 1.0f);
    test_flt(u->data.uv_transform[1], 1.0f);

    float size[3] = { 4.0f, 1.0f, 2.0f };
    float span[2];
    box_tiling_span(BOX_TILE_RADIAL, size, span);
    test_flt(span[0], 2.0f);
    test_flt(span[1], BOX_TILING_PI * 4.0f);
    test_flt(box_tiling_count(span[0], 1.0f, false, false, 0), 2.0f);

    float mesh_uv[2] = { 0.5f, 0.5f };
    float uv[2];
    float rim[3] = { 2.0f, 0.0f, 0.0f };
    float normal[3] = { 0.0f, 1.0f, 0.0f };
    box_tiling_uv(u->data.uv_mode, u->data.uv_transform, rim, normal, mesh_uv, uv);
    test_flt(uv[0], 2.0f);
    test_flt(uv[1], 0.0f);
}

void Tiling_meters_projection_stretches_the_mesh_uv(void)
{
    // textures/tiling
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {4, 1, 2});
    ecs_set(w, e, TextureTiling, { .meters_x = 1, .meters_y = 1, .shape = TilingMeters });
    box_render_extract(w, &packet);

    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_int(box_tiling_projection(u->data.uv_mode), BOX_TILE_MESH);
    test_flt(u->data.uv_transform[0], 4.0f);
    test_flt(u->data.uv_transform[1], 1.0f);

    float local[3] = { 2.0f, 0.5f, -1.0f };
    float normal[3] = { 0.0f, 0.0f, -1.0f };
    float mesh_uv[2] = { 1.0f, 1.0f };
    float uv[2];
    box_tiling_uv(u->data.uv_mode, u->data.uv_transform, local, normal, mesh_uv, uv);
    test_flt(uv[0], 4.0f);
    test_flt(uv[1], 1.0f);
}

void Tiling_fit_rounds_the_tile_count(void)
{
    // textures/tiling
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {2.6f, 1, 1});
    ecs_set(w, e, TextureTiling, {
        .meters_x = 1, .meters_y = 1, .fit = true, .shape = TilingMeters });
    box_render_extract(w, &packet);

    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_flt(u->data.uv_transform[0], 3.0f);
    test_flt(u->data.uv_transform[1], 1.0f);
    test_flt(box_tiling_count(2.6f, 1.0f, true, false, 0), 3.0f);
    test_flt(box_tiling_count(2.4f, 1.0f, true, false, 0), 2.0f);
    test_flt(box_tiling_count(0.3f, 1.0f, true, false, 0), 1.0f);
}

void Tiling_partial_tiles_keep_the_fraction(void)
{
    // textures/tiling
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {2.6f, 1, 1});
    ecs_set(w, e, TextureTiling, { .meters_x = 1, .meters_y = 1, .fit = true,
        .allow_partial = true, .shape = TilingBox });
    box_render_extract(w, &packet);

    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_flt(u->data.uv_transform[0], 1.0f);
    test_flt(box_tiling_count(2.6f, 1.0f, true, true, 0), 2.6f);
    test_flt(box_tiling_count(2.6f, 1.0f, false, false, 0), 2.6f);
}

void Tiling_min_tiles_raises_the_count(void)
{
    // textures/tiling
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {0.5f, 0.5f, 0.5f});
    ecs_set(w, e, TextureTiling, {
        .meters_x = 1, .meters_y = 1, .min_tiles = 2, .shape = TilingBox });
    box_render_extract(w, &packet);

    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_flt(u->data.uv_transform[0], 4.0f);
    test_flt(u->data.uv_transform[1], 4.0f);
    test_flt(box_tiling_count(0.5f, 1.0f, false, false, 2), 2.0f);
    test_flt(box_tiling_count(8.0f, 1.0f, false, false, 2), 8.0f);
}

void Tiling_wrap_mode_reaches_the_instance(void)
{
    // textures/tiling
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, TextureWrap, {WrapMirror});
    ecs_set(w, e, TextureTiling, { .meters_x = 1, .meters_y = 1, .shape = TilingBox });
    box_render_extract(w, &packet);

    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_int(box_tiling_wrap(u->data.uv_mode), BOX_WRAP_MIRROR);
    test_int(box_tiling_projection(u->data.uv_mode), BOX_TILE_BOX);
    test_int(box_tiling_mode(BOX_TILE_BOX, BOX_WRAP_MIRROR), u->data.uv_mode);
}

void Tiling_mirror_wrap_folds_the_uv(void)
{
    // textures/tiling
    float xform[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
    float normal[3] = { 0.0f, 0.0f, -1.0f };
    float mesh_uv[2] = { 0.0f, 0.0f };
    uint32_t mode = box_tiling_mode(BOX_TILE_BOX, BOX_WRAP_MIRROR);
    float uv[2];

    float a[3] = { 0.25f, 0.25f, 0.0f };
    box_tiling_uv(mode, xform, a, normal, mesh_uv, uv);
    test_flt(uv[0], 0.25f);

    float b[3] = { 1.25f, 0.0f, 0.0f };
    box_tiling_uv(mode, xform, b, normal, mesh_uv, uv);
    test_flt(uv[0], 0.75f);

    float c[3] = { 2.25f, 0.0f, 0.0f };
    box_tiling_uv(mode, xform, c, normal, mesh_uv, uv);
    test_flt(uv[0], 0.25f);

    float d[3] = { -0.25f, 0.0f, 0.0f };
    box_tiling_uv(mode, xform, d, normal, mesh_uv, uv);
    test_flt(uv[0], 0.25f);
}

void Tiling_clamp_wrap_holds_the_range(void)
{
    // textures/tiling
    float xform[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
    float normal[3] = { 0.0f, 0.0f, -1.0f };
    float mesh_uv[2] = { 0.0f, 0.0f };
    uint32_t mode = box_tiling_mode(BOX_TILE_BOX, BOX_WRAP_CLAMP);
    float uv[2];

    float a[3] = { 2.5f, -1.5f, 0.0f };
    box_tiling_uv(mode, xform, a, normal, mesh_uv, uv);
    test_flt(uv[0], 1.0f);
    test_flt(uv[1], 0.0f);

    float b[3] = { 0.75f, 0.5f, 0.0f };
    box_tiling_uv(mode, xform, b, normal, mesh_uv, uv);
    test_flt(uv[0], 0.75f);
    test_flt(uv[1], 0.5f);
}

void Tiling_inherited_from_a_prefab(void)
{
    // textures/tiling
    ecs_entity_t prefab = ecs_new_w_id(w, EcsPrefab);
    ecs_set(w, prefab, TextureTiling, { .meters_x = 2, .meters_y = 2, .shape = TilingMeters });
    ecs_set(w, prefab, TextureTransform, {1.0f, 1.0f, 0.25f, 0.5f});
    ecs_set(w, prefab, TextureWrap, {WrapClamp});

    ecs_entity_t e = ecs_new_w_pair(w, EcsIsA, prefab);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {4, 1, 2});
    box_render_extract(w, &packet);

    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_flt(u->data.uv_transform[0], 2.0f);
    test_flt(u->data.uv_transform[1], 0.5f);
    test_flt(u->data.uv_transform[2], 0.25f);
    test_flt(u->data.uv_transform[3], 0.5f);
    test_int(box_tiling_wrap(u->data.uv_mode), BOX_WRAP_CLAMP);
}

void Tiling_scale_change_updates_the_tiling(void)
{
    // textures/tiling
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {2, 1, 1});
    ecs_set(w, e, TextureTiling, { .meters_x = 1, .meters_y = 1, .shape = TilingMeters });
    box_render_extract(w, &packet);

    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_flt(u->data.uv_transform[0], 2.0f);

    box_packet_reset(&packet);
    ecs_set(w, e, Scale, {3, 1, 1});
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    u = box_vec_at(&packet.static_updates, 0);
    test_flt(u->data.uv_transform[0], 6.0f);
    test_flt(u->data.uv_transform[1], 1.0f);
}
