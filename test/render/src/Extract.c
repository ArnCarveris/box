/**
 * @file Extract.c
 * @brief Tests for the render module: slots, deltas, dynamic instances, camera and light.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void Extract_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
}

void Extract_teardown(void)
{
    box_packet_fini(&packet);
    ecs_fini(w);
}

void Extract_mesh_uploaded_once(void)
{
    // scene_core/render
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Box, {1, 1, 1});
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Box, {2, 2, 2});
    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 1);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    test_int(cmd->vertex_count, 24);
    test_int(cmd->index_count, 36);
    free(cmd->vertices);
    free(cmd->indices);
    box_packet_reset(&packet);
    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 0);
}

void Extract_static_entity_gets_slot_and_update(void)
{
    // scene_core/render
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {1, 2, 3});
    ecs_set(w, e, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_int(u->slot, 0);
    test_flt(u->data.transform[3], 1);
    test_flt(u->data.transform[7], 2);
    test_flt(u->data.transform[11], 3);
    test_int(packet.static_counts.count, 1);
    box_static_count_t *c = box_vec_at(&packet.static_counts, 0);
    test_int(c->mesh, u->mesh);
    test_int(c->count, 1);
    const RenderInstance *inst = ecs_get(w, e, RenderInstance);
    test_assert(inst != NULL);
    test_int(inst->slot, 0);
    test_bool(inst->dynamic, false);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
}

void Extract_unchanged_entity_not_reextracted(void)
{
    // scene_core/render
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {1, 2, 3});
    ecs_set(w, e, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_packet_reset(&packet);
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 0);
    test_int(packet.static_counts.count, 0);
    test_int(packet.dynamic_instances.count, 0);
}

void Extract_position_change_reextracts_static(void)
{
    // scene_core/render
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {1, 2, 3});
    ecs_set(w, e, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_packet_reset(&packet);
    ecs_set(w, e, Position, {5, 6, 7});
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_int(u->slot, 0);
    test_flt(u->data.transform[3], 5);
    test_flt(u->data.transform[7], 6);
    test_flt(u->data.transform[11], 7);
    test_int(packet.static_counts.count, 0);
}

void Extract_color_change_reextracts_static(void)
{
    // scene_core/render
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_packet_reset(&packet);
    ecs_set(w, e, Rgb, {255, 0, 0});
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_flt(u->data.color[0], 1.0f);
    test_flt(u->data.color[1], 0.0f);
    test_flt(u->data.color[2], 0.0f);
    test_flt(u->data.color[3], 1.0f);
}

void Extract_delete_swaps_last_into_slot(void)
{
    // scene_core/render
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {1, 0, 0});
    ecs_set(w, a, Box, {1, 1, 1});
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Position, {2, 0, 0});
    ecs_set(w, b, Box, {1, 1, 1});
    ecs_entity_t c = ecs_new(w);
    ecs_set(w, c, Position, {3, 0, 0});
    ecs_set(w, c, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 3);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_packet_reset(&packet);
    test_int(ecs_get(w, a, RenderInstance)->slot, 0);
    test_int(ecs_get(w, b, RenderInstance)->slot, 1);
    test_int(ecs_get(w, c, RenderInstance)->slot, 2);
    ecs_delete(w, a);
    box_render_extract(w, &packet);
    test_int(ecs_get(w, c, RenderInstance)->slot, 0);
    test_int(packet.static_updates.count, 1);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_int(u->slot, 0);
    test_flt(u->data.transform[3], 3);
    test_int(packet.static_counts.count, 1);
    test_int(((box_static_count_t*)box_vec_at(&packet.static_counts, 0))->count, 2);
}

void Extract_dynamic_entity_extracted_every_frame(void)
{
    // scene_core/render
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {1, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add(w, e, DynamicTransform);
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    test_int(packet.static_updates.count, 0);
    test_int(packet.dynamic_instances.count, 1);
    test_int(packet.dynamic_batches.count, 1);
    box_packet_reset(&packet);
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    test_int(packet.dynamic_instances.count, 1);
    box_instance_t *inst = box_vec_at(&packet.dynamic_instances, 0);
    test_flt(inst->transform[3], 1);
    test_bool(ecs_get(w, e, RenderInstance)->dynamic, true);
}

void Extract_dynamic_child_of_dynamic_parent(void)
{
    // scene_core/render
    ecs_entity_t parent = ecs_new(w);
    ecs_set(w, parent, Position, {10, 0, 0});
    ecs_add(w, parent, DynamicTransform);
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, parent);
    ecs_set(w, child, Position, {1, 0, 0});
    ecs_set(w, child, Box, {1, 1, 1});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    test_int(packet.static_updates.count, 0);
    test_int(packet.dynamic_instances.count, 1);
    box_instance_t *inst = box_vec_at(&packet.dynamic_instances, 0);
    test_flt(inst->transform[3], 11);
}

void Extract_static_to_dynamic_releases_slot(void)
{
    // scene_core/render
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {1, 0, 0});
    ecs_set(w, a, Box, {1, 1, 1});
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Position, {2, 0, 0});
    ecs_set(w, b, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_packet_reset(&packet);
    ecs_add(w, a, DynamicTransform);
    box_render_extract(w, &packet);
    test_bool(ecs_get(w, a, RenderInstance)->dynamic, true);
    test_int(ecs_get(w, b, RenderInstance)->slot, 0);
    test_int(packet.dynamic_instances.count, 1);
    test_int(packet.static_counts.count, 1);
    test_int(((box_static_count_t*)box_vec_at(&packet.static_counts, 0))->count, 1);
}

void Extract_dynamic_to_static_gets_slot(void)
{
    // scene_core/render
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {1, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add(w, e, DynamicTransform);
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_packet_reset(&packet);
    ecs_remove(w, e, DynamicTransform);
    box_render_extract(w, &packet);
    test_bool(ecs_get(w, e, RenderInstance)->dynamic, false);
    test_int(packet.dynamic_instances.count, 0);
    test_int(packet.static_updates.count, 1);
    test_int(packet.static_counts.count, 1);
    test_int(((box_static_count_t*)box_vec_at(&packet.static_counts, 0))->count, 1);
}

void Extract_instance_transform_includes_shape_scale(void)
{
    // scene_core/render
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Sphere, {.segments = 8, .radius = 2.5f});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_flt(u->data.transform[0], 2.5f);
    test_flt(u->data.transform[5], 2.5f);
    test_flt(u->data.transform[10], 2.5f);
}

void Extract_aabb_written_for_static(void)
{
    // scene_core/render
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {10, 0, 0});
    ecs_set(w, e, Box, {2, 4, 6});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    const AABB *aabb = ecs_get(w, e, AABB);
    test_assert(aabb != NULL);
    test_assert(fabsf(aabb->min.x - 9) < 1e-4f);
    test_assert(fabsf(aabb->max.x - 11) < 1e-4f);
    test_assert(fabsf(aabb->min.y + 2) < 1e-4f);
    test_assert(fabsf(aabb->max.y - 2) < 1e-4f);
    test_assert(fabsf(aabb->min.z + 3) < 1e-4f);
    test_assert(fabsf(aabb->max.z - 3) < 1e-4f);
}

void Extract_bounds_written_in_the_delta(void)
{
    // culling/frustum_culling
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {10, 0, 0});
    ecs_set(w, e, Box, {2, 4, 6});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    test_int(packet.static_updates.count, 1);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_flt(u->data.bounds[0], 10);
    test_flt(u->data.bounds[1], 0);
    test_flt(u->data.bounds[2], 0);
    test_assert(fabsf(u->data.bounds[3] - sqrtf(1 + 4 + 9)) < 1e-4f);
}

void Extract_bounds_cover_a_rotated_instance(void)
{
    // culling/frustum_culling
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Rotation, {0, 0.7853981f, 0});
    ecs_set(w, e, Box, {2, 2, 8});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    test_int(packet.static_updates.count, 1);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    const AABB *aabb = ecs_get(w, e, AABB);
    test_assert(aabb != NULL);
    test_assert(u->data.bounds[3] >= aabb->max.x);
    test_assert(u->data.bounds[3] >= aabb->max.z);
}

void Extract_camera_fills_view(void)
{
    // scene_core/render
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 90});
    box_render_extract(w, &packet);
    test_bool(packet.has_view, true);
    test_flt(packet.view.eye[2], -5);
    test_assert(fabsf(packet.view.view[14] - 5) < 1e-4f);
    test_assert(fabsf(packet.view.proj[5] - 1.0f) < 1e-4f);
    test_assert(fabsf(packet.view.proj[0] - 480.0f / 640.0f) < 1e-4f);
    test_flt(packet.view.proj[11], 1);
    test_assert(fabsf(packet.view.proj[14] - 0.1f) < 1e-6f);
}

void Extract_default_camera_without_entity(void)
{
    // scene_core/render
    box_render_extract(w, &packet);
    test_bool(packet.has_view, true);
    test_assert(packet.view.eye[2] < 0);
    test_assert(packet.view.sun_intensity > 0);
}

void Extract_sun_from_directional_light(void)
{
    // scene_core/render
    ecs_entity_t sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {3.14159265f / 2, 0, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 7});
    ecs_set(w, sun, Rgb, {255, 0, 0});
    box_render_extract(w, &packet);
    test_flt(packet.view.sun_intensity, 7);
    test_assert(fabsf(packet.view.sun_dir[1] + 1) < 1e-4f);
    test_flt(packet.view.sun_color[0], 1);
    test_flt(packet.view.sun_color[1], 0);
}

void Extract_mesh_shared_between_same_shapes(void)
{
    // scene_core/render
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Sphere, {.segments = 12, .smooth = true, .radius = 1});
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Sphere, {.segments = 12, .smooth = true, .radius = 4});
    test_int(ecs_get(w, a, RenderMesh)->handle, ecs_get(w, b, RenderMesh)->handle);
    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 1);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
}

void Extract_different_segments_different_mesh(void)
{
    // scene_core/render
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Sphere, {.segments = 12, .smooth = true, .radius = 1});
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Sphere, {.segments = 6, .smooth = true, .radius = 1});
    test_assert(ecs_get(w, a, RenderMesh)->handle != ecs_get(w, b, RenderMesh)->handle);
    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 2);
    uint32_t i;
    for (i = 0; i < packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, i);
        free(cmd->vertices);
        free(cmd->indices);
    }
}

void Extract_delete_before_first_extract(void)
{
    // command/integration
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {1, 0, 0});
    ecs_set(w, a, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_packet_reset(&packet);
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Position, {2, 0, 0});
    ecs_set(w, b, Box, {1, 1, 1});
    ecs_delete(w, b);
    test_int(ecs_get(w, a, RenderInstance)->slot, 0);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 0);
    test_int(ecs_get(w, a, RenderInstance)->slot, 0);
}

void Extract_delete_before_first_extract_empty_batch(void)
{
    // command/integration
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {1, 0, 0});
    ecs_set(w, a, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_packet_reset(&packet);
    ecs_delete(w, a);
    box_render_extract(w, &packet);
    box_packet_reset(&packet);
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Position, {2, 0, 0});
    ecs_set(w, b, Sphere, {.segments = 8, .radius = 1});
    ecs_delete(w, b);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 0);
}

void Extract_parent_tree_uploaded_once(void)
{
    // scene_core/parent_hierarchy
    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {1, 0, 0});
    ecs_set(w, root, Box, {1, 1, 1});
    ecs_entity_t a = ecs_new_w_parent(w, root, "a");
    ecs_set(w, a, Position, {1, 0, 0});
    ecs_set(w, a, Box, {1, 1, 1});
    ecs_entity_t b = ecs_new_w_parent(w, a, "b");
    ecs_set(w, b, Position, {1, 0, 0});
    ecs_set(w, b, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    test_int(packet.static_updates.count, 3);
    test_int(packet.dynamic_instances.count, 0);
    box_packet_reset(&packet);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 0);
    test_int(packet.dynamic_instances.count, 0);
    box_packet_reset(&packet);
    ecs_set(w, root, Position, {5, 0, 0});
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 3);
}

void Extract_dynamic_parent_node_children_dynamic(void)
{
    // scene_core/parent_hierarchy
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, DynamicTransform);
    ecs_set(w, root, Position, {10, 0, 0});
    ecs_set(w, root, Box, {1, 1, 1});
    ecs_entity_t a = ecs_new_w_parent(w, root, "a");
    ecs_set(w, a, Position, {1, 0, 0});
    ecs_set(w, a, Box, {1, 1, 1});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    test_int(packet.static_updates.count, 0);
    test_int(packet.dynamic_instances.count, 2);
    box_packet_reset(&packet);
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 0);
    test_int(packet.dynamic_instances.count, 2);
    box_instance_t *inst = box_vec_at(&packet.dynamic_instances, 1);
    test_flt(inst->transform[3], 11);
}

void Extract_mixed_tree_only_dynamic_reuploaded(void)
{
    // scene_core/parent_hierarchy
    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {1, 0, 0});
    ecs_set(w, root, Box, {1, 1, 1});
    ecs_entity_t mover = ecs_new_w_parent(w, root, "mover");
    ecs_add(w, mover, DynamicTransform);
    ecs_set(w, mover, Position, {1, 0, 0});
    ecs_set(w, mover, Box, {1, 1, 1});
    ecs_entity_t leaf = ecs_new_w_pair(w, EcsChildOf, mover);
    ecs_set(w, leaf, Position, {1, 0, 0});
    ecs_set(w, leaf, Box, {1, 1, 1});
    ecs_entity_t sibling = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, sibling, Position, {5, 0, 0});
    ecs_set(w, sibling, Box, {1, 1, 1});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    test_int(packet.static_updates.count, 2);
    test_int(packet.dynamic_instances.count, 2);
    box_packet_reset(&packet);
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 0);
    test_int(packet.dynamic_instances.count, 2);
    box_packet_reset(&packet);
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 0);
    test_int(packet.dynamic_instances.count, 2);
}

void Extract_reparent_between_storages_requeues_subtree(void)
{
    // scene_core/parent_hierarchy
    ecs_entity_t first = ecs_new(w);
    ecs_set(w, first, Position, {10, 0, 0});
    ecs_entity_t second = ecs_new(w);
    ecs_set(w, second, Position, {100, 0, 0});
    ecs_entity_t node = ecs_new_w_pair(w, EcsChildOf, first);
    ecs_set(w, node, Position, {1, 0, 0});
    ecs_set(w, node, Box, {1, 1, 1});
    ecs_entity_t leaf = ecs_new_w_parent(w, node, "leaf");
    ecs_set(w, leaf, Position, {1, 0, 0});
    ecs_set(w, leaf, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    test_int(packet.static_updates.count, 2);
    box_packet_reset(&packet);
    ecs_set(w, node, EcsParent, { second });
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 2);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    box_static_update_t *v = box_vec_at(&packet.static_updates, 1);
    float x0 = u->data.transform[3], x1 = v->data.transform[3];
    test_assert(fabsf(x0 - 101) < 1e-4f || fabsf(x0 - 102) < 1e-4f);
    test_assert(fabsf(x1 - 101) < 1e-4f || fabsf(x1 - 102) < 1e-4f);
}

void Extract_delete_parent_node_frees_slots(void)
{
    // scene_core/parent_hierarchy
    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {1, 0, 0});
    ecs_set(w, root, Box, {1, 1, 1});
    ecs_entity_t node = ecs_new_w_parent(w, root, "node");
    ecs_set(w, node, Position, {1, 0, 0});
    ecs_set(w, node, Box, {1, 1, 1});
    ecs_entity_t leaf = ecs_new_w_pair(w, EcsChildOf, node);
    ecs_set(w, leaf, Position, {1, 0, 0});
    ecs_set(w, leaf, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    test_int(packet.static_updates.count, 3);
    box_packet_reset(&packet);
    ecs_delete(w, node);
    test_assert(!ecs_is_alive(w, leaf));
    box_render_extract(w, &packet);
    test_int(packet.static_counts.count, 1);
    test_int(((box_static_count_t*)box_vec_at(&packet.static_counts, 0))->count, 1);
    test_int(ecs_get(w, root, RenderInstance)->slot, 0);
}

void Extract_instance_color_is_the_composed_tint(void)
{
    // materials/tint_compose
    ecs_entity_t parent = ecs_new(w);
    ecs_set(w, parent, Tint, {0, 0, 255, 128});
    ecs_entity_t e = ecs_new_w_pair(w, EcsChildOf, parent);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, Rgb, {255, 255, 255});
    ecs_set(w, e, Tint, {0, 0, 0, 128});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    float strength = 192.0f / 255.0f;
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_assert(fabsf(u->data.color[0] - (1.0f - strength)) < 1e-5f);
    test_assert(fabsf(u->data.color[1] - (1.0f - strength)) < 1e-5f);
    test_assert(fabsf(u->data.color[2] -
        (1.0f + (85.0f / 255.0f - 1.0f) * strength)) < 1e-5f);
    test_flt(u->data.color[3], 1.0f);
}

void Extract_tinted_opaque_entity_stays_static(void)
{
    // materials/tint_compose
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, Rgb, {255, 255, 255});
    ecs_set(w, e, Tint, {0, 0, 255, 128});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    test_int(packet.transparent.count, 0);
    test_int(packet.static_updates.count, 1);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_flt(u->data.color[3], 1.0f);
    test_bool(ecs_get(w, e, RenderInstance)->transparent, false);
}

void Extract_primitive_vertices_are_white(void)
{
    // meshes/vertex_colors
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    test_int(packet.mesh_create.count, 1);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    const uint8_t *bytes = (const uint8_t*)cmd->vertices;
    uint32_t v, c;
    for (v = 0; v < cmd->vertex_count; v ++) {
        const uint8_t *color = bytes + (size_t)v * BOX_VERTEX_FLOATS * sizeof(float) +
            BOX_VERTEX_COLOR_OFFSET;

        for (c = 0; c < 4; c ++) {
            test_int(color[c], 255);
        }
    }

    free(cmd->vertices);
    free(cmd->indices);
}

void Extract_vertex_color_is_kept_beside_the_instance_color(void)
{
    // meshes/vertex_colors
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    Mesh3 *m = ecs_ensure(w, e, Mesh3);
    ecs_vec_init_t(NULL, &m->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(1, 0, 0);
    ecs_vec_append_t(NULL, &m->vertices, vec3)[0] = vec3_new(0, 1, 0);
    ecs_vec_init_t(NULL, &m->colors, rgb, 3);
    ecs_vec_append_t(NULL, &m->colors, rgb)[0] = (rgb){0, 255, 0};
    ecs_vec_append_t(NULL, &m->colors, rgb)[0] = (rgb){0, 255, 0};
    ecs_vec_append_t(NULL, &m->colors, rgb)[0] = (rgb){0, 255, 0};
    ecs_modified(w, e, Mesh3);
    ecs_set(w, e, Rgb, {255, 0, 0});
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_flt(u->data.color[0], 1.0f);
    test_flt(u->data.color[1], 0.0f);
    test_int(packet.mesh_create.count, 1);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    const uint8_t *color = (const uint8_t*)cmd->vertices + BOX_VERTEX_COLOR_OFFSET;
    test_int(color[0], 0);
    test_int(color[1], 255);
    test_int(color[2], 0);
    test_int(color[3], 255);
    free(cmd->vertices);
    free(cmd->indices);
}
