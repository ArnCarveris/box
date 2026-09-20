/**
 * @file Shapes2D.c
 * @brief Tests for the 2D shapes: rect, circle, line, image and border.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include "../../../src/geometry/geometry_internal.h"
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void Shapes2D_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
}

void Shapes2D_teardown(void)
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

void Shapes2D_rect_builds_a_quad(void)
{
    // shapes_2d/primitives
    ecs_entity_t rect = ecs_new(w);
    ecs_entity_t quad = ecs_new(w);

    ecs_set(w, rect, Rect, {3, 2});
    ecs_set(w, quad, Quad, {1, 1});

    const RenderMesh *a = ecs_get(w, rect, RenderMesh);
    const RenderMesh *b = ecs_get(w, quad, RenderMesh);
    test_assert(a != NULL);
    test_assert(b != NULL);
    test_uint(a->handle, b->handle);
    test_flt(a->scale.x, 3);
    test_flt(a->scale.y, 2);
    test_flt(a->scale.z, 1);
}

void Shapes2D_circle_writes_a_render_mesh(void)
{
    // shapes_2d/primitives
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Circle, {.radius = 2});

    const RenderMesh *mesh = ecs_get(w, e, RenderMesh);
    test_assert(mesh != NULL);
    test_flt(mesh->scale.x, 2);
    test_flt(mesh->scale.y, 2);
    test_flt(mesh->scale.z, 1);
    test_flt(mesh->bounds_min.z, 0);
    test_flt(mesh->bounds_max.z, 0);
    test_assert(mesh->bounds_max.x > 0.99f);
}

void Shapes2D_circle_segments_follow_the_radius(void)
{
    // shapes_2d/primitives
    test_int(box_circle_segments(0.01f), 16);
    test_int(box_circle_segments(1.0f), 16);
    test_int(box_circle_segments(4.0f), 32);
    test_int(box_circle_segments(16.0f), 64);
    test_int(box_circle_segments(1000.0f), 128);
    test_assert(box_circle_segments(9.0f) > box_circle_segments(1.0f));
}

void Shapes2D_circle_shares_the_mesh_of_an_ngon(void)
{
    // shapes_2d/primitives
    ecs_entity_t circle = ecs_new(w);
    ecs_entity_t ngon = ecs_new(w);

    ecs_set(w, circle, Circle, {.radius = 1});
    ecs_set(w, ngon, NGon, {.sides = 16, .radius = 1});
    box_render_extract(w, &packet);
    test_uint(ecs_get(w, circle, RenderMesh)->handle, ecs_get(w, ngon, RenderMesh)->handle);
    test_int(packet.mesh_create.count, 1);
}

void Shapes2D_circle_uses_a_given_segment_count(void)
{
    // shapes_2d/primitives
    ecs_entity_t circle = ecs_new(w);
    ecs_entity_t ngon = ecs_new(w);

    ecs_set(w, circle, Circle, {.radius = 4, .segments = 5});
    ecs_set(w, ngon, NGon, {.sides = 5, .radius = 1});
    test_uint(ecs_get(w, circle, RenderMesh)->handle, ecs_get(w, ngon, RenderMesh)->handle);
    test_flt(ecs_get(w, circle, RenderMesh)->scale.x, 4);
}

void Shapes2D_circles_of_one_size_share_a_mesh(void)
{
    // shapes_2d/primitives
    ecs_entity_t small = ecs_new(w);
    ecs_entity_t large = ecs_new(w);
    ecs_entity_t twin = ecs_new(w);

    ecs_set(w, small, Circle, {.radius = 1});
    ecs_set(w, large, Circle, {.radius = 16});
    ecs_set(w, twin, Circle, {.radius = 1.1f});
    test_uint(ecs_get(w, small, RenderMesh)->handle, ecs_get(w, twin, RenderMesh)->handle);
    test_assert(ecs_get(w, small, RenderMesh)->handle !=
        ecs_get(w, large, RenderMesh)->handle);
}

void Shapes2D_line_writes_a_render_mesh(void)
{
    // shapes_2d/primitives
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Line, {.x1 = 0, .y1 = 0, .x2 = 4, .y2 = 0, .thickness = 0.5f});

    const RenderMesh *mesh = ecs_get(w, e, RenderMesh);
    test_assert(mesh != NULL);
    test_flt(mesh->scale.x, 4);
    test_flt(mesh->scale.y, 4);
    test_flt(mesh->bounds_min.x, 0);
    test_flt(mesh->bounds_max.x, 1);
    test_flt(mesh->bounds_max.y, 0.0625f);
    test_flt(mesh->bounds_min.y, -0.0625f);
}

void Shapes2D_line_shares_one_mesh_per_outline(void)
{
    // shapes_2d/primitives
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t b = ecs_new(w);
    ecs_entity_t c = ecs_new(w);

    ecs_set(w, a, Line, {.x1 = 0, .y1 = 0, .x2 = 2, .y2 = 0, .thickness = 0.2f});
    ecs_set(w, b, Line, {.x1 = 0, .y1 = 0, .x2 = 4, .y2 = 0, .thickness = 0.4f});
    ecs_set(w, c, Line, {.x1 = 0, .y1 = 0, .x2 = 0, .y2 = 2, .thickness = 0.2f});
    box_render_extract(w, &packet);
    test_uint(ecs_get(w, a, RenderMesh)->handle, ecs_get(w, b, RenderMesh)->handle);
    test_assert(ecs_get(w, a, RenderMesh)->handle != ecs_get(w, c, RenderMesh)->handle);
    test_int(packet.mesh_create.count, 2);
}

void Shapes2D_line_uvs_run_along_the_segment(void)
{
    // shapes_2d/primitives
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Line, {.x1 = -1, .y1 = -1, .x2 = 1, .y2 = 1, .thickness = 0.5f});
    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 1);

    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    test_int(cmd->vertex_count, 4);
    test_int(cmd->index_count, 6);

    float *v = cmd->vertices;
    test_flt(v[6], 0.0f);
    test_flt(v[7], 0.0f);
    test_flt(v[BOX_VERTEX_FLOATS + 6], 1.0f);
    test_flt(v[BOX_VERTEX_FLOATS + 7], 0.0f);
    test_flt(v[BOX_VERTEX_FLOATS * 2 + 6], 1.0f);
    test_flt(v[BOX_VERTEX_FLOATS * 2 + 7], 1.0f);
    test_flt(v[BOX_VERTEX_FLOATS * 3 + 6], 0.0f);
    test_flt(v[BOX_VERTEX_FLOATS * 3 + 7], 1.0f);
}

void Shapes2D_image_adds_a_rect(void)
{
    // shapes_2d/primitives
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Image, {.u1 = 1, .v1 = 1});
    test_assert(ecs_has(w, e, Rect));
    test_assert(ecs_get(w, e, RenderMesh) != NULL);
}

void Shapes2D_image_uvs_show_the_region(void)
{
    // shapes_2d/primitives
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Rect, {2, 2});
    ecs_set(w, e, Image, {.u0 = 0.25f, .v0 = 0.25f, .u1 = 0.75f, .v1 = 0.75f});
    box_render_extract(w, &packet);

    box_mesh_cmd_t *cmd = NULL;
    uint32_t handle = ecs_get(w, e, RenderMesh)->handle;
    uint32_t i;
    for (i = 0; i < packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *candidate = box_vec_at(&packet.mesh_create, i);

        if (candidate->handle == handle) {
            cmd = candidate;
        }
    }

    test_assert(cmd != NULL);

    float *v = cmd->vertices;
    test_flt(v[6], 0.25f);
    test_flt(v[7], 0.25f);
    test_flt(v[BOX_VERTEX_FLOATS * 2 + 6], 0.75f);
    test_flt(v[BOX_VERTEX_FLOATS * 2 + 7], 0.75f);
}

void Shapes2D_image_cover_crops_the_region(void)
{
    // shapes_2d/primitives
    ecs_entity_t tex = ecs_new(w);
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, tex, RenderTexture, {.handle = 1, .width = 64, .height = 64});
    ecs_set(w, e, Rect, {2, 1});
    ecs_set(w, e, Image, {.texture = tex, .fit = ImageCover, .u1 = 1, .v1 = 1});
    box_render_extract(w, &packet);

    box_mesh_cmd_t *cmd = NULL;
    uint32_t handle = ecs_get(w, e, RenderMesh)->handle;
    uint32_t i;
    for (i = 0; i < packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *candidate = box_vec_at(&packet.mesh_create, i);

        if (candidate->handle == handle) {
            cmd = candidate;
        }
    }

    test_assert(cmd != NULL);

    float *v = cmd->vertices;
    test_flt(v[6], 0.0f);
    test_flt(v[7], 0.25f);
    test_flt(v[BOX_VERTEX_FLOATS * 2 + 6], 1.0f);
    test_flt(v[BOX_VERTEX_FLOATS * 2 + 7], 0.75f);
}

void Shapes2D_image_contain_widens_the_region(void)
{
    // shapes_2d/primitives
    ecs_entity_t tex = ecs_new(w);
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, tex, RenderTexture, {.handle = 1, .width = 64, .height = 64});
    ecs_set(w, e, Rect, {2, 1});
    ecs_set(w, e, Image, {.texture = tex, .fit = ImageContain, .u1 = 1, .v1 = 1});
    box_render_extract(w, &packet);

    box_mesh_cmd_t *cmd = NULL;
    uint32_t handle = ecs_get(w, e, RenderMesh)->handle;
    uint32_t i;
    for (i = 0; i < packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *candidate = box_vec_at(&packet.mesh_create, i);

        if (candidate->handle == handle) {
            cmd = candidate;
        }
    }

    test_assert(cmd != NULL);

    float *v = cmd->vertices;
    test_flt(v[6], -0.5f);
    test_flt(v[7], 0.0f);
    test_flt(v[BOX_VERTEX_FLOATS * 2 + 6], 1.5f);
    test_flt(v[BOX_VERTEX_FLOATS * 2 + 7], 1.0f);
}

void Shapes2D_image_fit_follows_the_texture_aspect(void)
{
    // shapes_2d/primitives
    ecs_entity_t tex = ecs_new(w);
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, tex, RenderTexture, {.handle = 1, .width = 128, .height = 64});
    ecs_set(w, e, Rect, {2, 1});
    ecs_set(w, e, Image, {.texture = tex, .fit = ImageCover, .u1 = 1, .v1 = 1});
    box_render_extract(w, &packet);

    box_mesh_cmd_t *cmd = NULL;
    uint32_t handle = ecs_get(w, e, RenderMesh)->handle;
    uint32_t i;
    for (i = 0; i < packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *candidate = box_vec_at(&packet.mesh_create, i);

        if (candidate->handle == handle) {
            cmd = candidate;
        }
    }

    test_assert(cmd != NULL);

    float *v = cmd->vertices;
    test_flt(v[6], 0.0f);
    test_flt(v[7], 0.0f);
    test_flt(v[BOX_VERTEX_FLOATS * 2 + 6], 1.0f);
    test_flt(v[BOX_VERTEX_FLOATS * 2 + 7], 1.0f);
}

void Shapes2D_image_rotation_turns_the_uvs(void)
{
    // shapes_2d/primitives
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Rect, {2, 2});
    ecs_set(w, e, Image, {.u1 = 1, .v1 = 1, .rotation = 90});
    box_render_extract(w, &packet);

    box_mesh_cmd_t *cmd = NULL;
    uint32_t handle = ecs_get(w, e, RenderMesh)->handle;
    uint32_t i;
    for (i = 0; i < packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *candidate = box_vec_at(&packet.mesh_create, i);

        if (candidate->handle == handle) {
            cmd = candidate;
        }
    }

    test_assert(cmd != NULL);

    float *v = cmd->vertices;
    test_assert(fabsf(v[6] - 1.0f) < 1e-5f);
    test_assert(fabsf(v[7] - 0.0f) < 1e-5f);
    test_assert(fabsf(v[BOX_VERTEX_FLOATS * 2 + 6] - 0.0f) < 1e-5f);
    test_assert(fabsf(v[BOX_VERTEX_FLOATS * 2 + 7] - 1.0f) < 1e-5f);
}

void Shapes2D_border_adds_a_rect(void)
{
    // shapes_2d/primitives
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Border, {.radius = 4, .width = 1});
    test_assert(ecs_has(w, e, Rect));
    test_assert(ecs_has(w, e, RenderShape2D));
}

void Shapes2D_border_shorthand_fills_every_corner(void)
{
    // shapes_2d/primitives
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Rect, {4, 4});
    ecs_set(w, e, Border, {.radius = 0.5f, .radius_top_left = 1.5f, .width = 0.25f,
        .left_width = 0.75f});

    const RenderShape2D *shape = ecs_get(w, e, RenderShape2D);
    test_assert(shape != NULL);
    test_flt(shape->radii[0], 1.5f);
    test_flt(shape->radii[1], 0.5f);
    test_flt(shape->radii[2], 0.5f);
    test_flt(shape->radii[3], 0.5f);
    test_flt(shape->widths[0], 0.25f);
    test_flt(shape->widths[1], 0.25f);
    test_flt(shape->widths[2], 0.25f);
    test_flt(shape->widths[3], 0.75f);
}

void Shapes2D_border_keeps_the_quad_mesh(void)
{
    // shapes_2d/primitives
    ecs_entity_t plain = ecs_new(w);
    ecs_entity_t bordered = ecs_new(w);

    ecs_set(w, plain, Rect, {4, 4});
    ecs_set(w, bordered, Rect, {4, 4});
    ecs_set(w, bordered, Border, {.radius = 1, .width = 0.25f});
    box_render_extract(w, &packet);
    test_uint(ecs_get(w, plain, RenderMesh)->handle,
        ecs_get(w, bordered, RenderMesh)->handle);
    test_int(packet.mesh_create.count, 1);
}

void Shapes2D_changing_a_radius_keeps_the_mesh(void)
{
    // shapes_2d/primitives
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Rect, {4, 4});
    ecs_set(w, e, Border, {.radius = 1, .width = 0.25f});

    uint32_t before = ecs_get(w, e, RenderMesh)->handle;
    box_render_extract(w, &packet);

    uint32_t created = packet.mesh_create.count;
    test_uint(created, 1);

    ecs_set(w, e, Border, {.radius = 1.9f, .width = 0.25f});
    box_render_extract(w, &packet);
    test_uint(ecs_get(w, e, RenderMesh)->handle, before);
    test_uint(packet.mesh_create.count, created);
    test_flt(ecs_get(w, e, RenderShape2D)->radii[0], 1.9f);
}

void Shapes2D_removing_a_border_drops_the_shape(void)
{
    // shapes_2d/primitives
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Rect, {4, 4});
    ecs_set(w, e, Border, {.radius = 1, .width = 0.25f});
    test_assert(ecs_has(w, e, RenderShape2D));

    ecs_remove(w, e, Border);
    test_assert(!ecs_has(w, e, RenderShape2D));
}
