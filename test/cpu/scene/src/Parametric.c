#include <box.h>
#include <bake_test.h>
#include "spatial.h"
#include "../../../geometry_assets.h"

void Parametric_geometry_edits_update_bounds(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    Sweep sweep = test_sweep(world);
    ecs_set_ptr(world, e, Sweep, &sweep);
    ecs_set(world, e, Position, {10, 0, 0});
    ecs_progress(world, 0);
    vec3 min, max;
    test_bool(box_spatial_bounds(world, e, NULL, min, max), true);
    test_assert(min[0] < 9);
    test_assert(max[1] > 3);
    float old_max = max[0];
    test_route_bend(world, sweep.path, 4, 0);
    ecs_set_ptr(world, e, Sweep, &sweep);
    ecs_progress(world, 0);
    test_bool(box_spatial_bounds(world, e, NULL, min, max), true);
    test_assert(max[0] > old_max + 3);
    ecs_fini(world);
}

void Parametric_attachments_update_bounds(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t target = ecs_new(world), attached = ecs_new(world);
    Sweep sweep = test_sweep(world);
    ecs_set_ptr(world, target, Sweep, &sweep);
    ecs_set(world, attached, Box, {0.1f, 0.1f, 0.1f});
    ecs_set(world, attached, SurfaceAttachment, {.target = target, .u = 0.25f, .v = 0.5f});
    ecs_progress(world, 0);
    vec3 min, max;
    test_bool(box_spatial_bounds(world, attached, NULL, min, max), true);
    test_assert(min[0] < -0.9f && max[0] < -0.9f);
    sweep.scale[0] = sweep.scale[1] = 2;
    ecs_set_ptr(world, target, Sweep, &sweep);
    ecs_progress(world, 0);
    test_bool(box_spatial_bounds(world, attached, NULL, min, max), true);
    test_assert(min[0] < -1.9f && max[0] < -1.9f);
    ecs_fini(world);
}
