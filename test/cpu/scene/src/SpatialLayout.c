#include <box.h>
#include <bake_test.h>
#include "spatial.h"

static ecs_world_t* setup(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    return world;
}

static ecs_entity_t shape(ecs_world_t *world, float x, float y, float z) {
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Box, {x, y, z});
    return e;
}

static void near(float actual, float expected) {
    test_assert(fabsf(actual - expected) < 0.0001f);
}

static const WorldTransform* transform(ecs_world_t *world, ecs_entity_t e) {
    const WorldTransform *t = ecs_get(world, e, WorldTransform);
    test_assert(t != NULL);
    return t;
}

void SpatialLayout_fit_stretch(void) {
    ecs_world_t *world = setup();
    ecs_entity_t hull = shape(world, 8, 3, 18), armor = shape(world, 1, 0.2f, 1);
    ecs_set(world, armor, Fit, {.target = hull, .axes = SpatialXZ, .padding = {0.4f, 0, 0.8f}});
    ecs_progress(world, 0);
    const ResolvedBox *b = ecs_get(world, armor, ResolvedBox);
    test_assert(b != NULL);
    near(b->x, 7.2f); near(b->y, 0.2f); near(b->z, 16.4f);
    near(ecs_get(world, armor, Box)->x, 1);
    near(ecs_get(world, armor, IntrinsicBounds)->max[0], 0.5f);
    vec3 min, max;
    test_assert(box_spatial_bounds(world, armor, NULL, min, max));
    near(max[0] - min[0], 7.2f);
    ecs_fini(world);
}

void SpatialLayout_fit_bevel(void) {
    ecs_world_t *world = setup();
    ecs_entity_t hull = shape(world, 8, 3, 18), armor = shape(world, 1, 0.2f, 1);
    ecs_set(world, armor, Bevel, {.top_front = 0.05f, .top_back = 0.05f, .segments = 4});
    ecs_set(world, armor, Fit, {.target = hull, .axes = SpatialXZ, .padding = {0.4f, 0, 0.8f}});
    ecs_progress(world, 0);
    const ResolvedBox *b = ecs_get(world, armor, ResolvedBox);
    test_assert(b != NULL);
    near(b->x, 7.2f); near(b->y, 0.2f); near(b->z, 16.4f);
    near(ecs_get(world, armor, Box)->x, 1);
    near(ecs_get(world, armor, IntrinsicBounds)->max[0], 0.5f);
    vec3 min, max;
    test_assert(box_spatial_bounds(world, armor, NULL, min, max));
    near(max[0] - min[0], 7.2f);
    ecs_fini(world);
}

void SpatialLayout_fit_modes(void) {
    ecs_world_t *world = setup();
    ecs_entity_t target = shape(world, 8, 3, 18), e = shape(world, 2, 1, 3);
    ecs_set(world, e, Fit, {.target = target, .axes = SpatialXZ, .mode = SpatialContain});
    ecs_progress(world, 0);
    near(ecs_get(world, e, ResolvedBox)->x, 8);
    near(ecs_get(world, e, ResolvedBox)->z, 12);
    ecs_set(world, e, Fit, {.target = target, .axes = SpatialXZ, .mode = SpatialCover});
    ecs_progress(world, 0);
    near(ecs_get(world, e, ResolvedBox)->x, 12);
    near(ecs_get(world, e, ResolvedBox)->z, 18);
    ecs_fini(world);
}

void SpatialLayout_fit_chained_resize(void) {
    ecs_world_t *world = setup();
    ecs_entity_t a = shape(world, 10, 4, 6), b = shape(world, 1, 1, 1), c = shape(world, 1, 1, 1);
    ecs_set(world, c, Fit, {.target = b, .padding = {1, 1, 1}});
    ecs_set(world, b, Fit, {.target = a, .padding = {0.5f, 0.5f, 0.5f}});
    ecs_progress(world, 0);
    near(ecs_get(world, c, ResolvedBox)->x, 7);
    ecs_set(world, a, Box, {20, 8, 12});
    for (int i = 0; i < 4; i ++) ecs_progress(world, 0);
    near(ecs_get(world, b, ResolvedBox)->x, 19);
    near(ecs_get(world, c, ResolvedBox)->x, 17);
    ecs_set(world, b, Box, {2, 3, 4});
    ecs_progress(world, 0);
    near(ecs_get(world, b, IntrinsicBounds)->max[0], 1);
    near(ecs_get(world, b, ResolvedBox)->x, 19);
    ecs_remove(world, b, Fit);
    ecs_progress(world, 0);
    vec3 min, max;
    test_assert(box_spatial_bounds(world, b, NULL, min, max));
    near(max[0] - min[0], 2);
    ecs_fini(world);
}

void SpatialLayout_align_bounds_keep(void) {
    ecs_world_t *world = setup();
    ecs_entity_t hull = shape(world, 8, 3, 18), cockpit = shape(world, 3, 1, 4);
    ecs_set(world, cockpit, SpatialAlign, {.target = hull,
        .self = {.y = SpatialMin}, .other = {.y = SpatialMax}, .offset = {0, 0.1f, 2}});
    ecs_progress(world, 0);
    const WorldTransform *t = transform(world, cockpit);
    near(t->position[0], 0); near(t->position[1], 2.1f); near(t->position[2], 2);
    ecs_set(world, hull, Box, {8, 5, 18});
    ecs_progress(world, 0);
    near(transform(world, cockpit)->position[1], 3.1f);
    ecs_fini(world);
}

void SpatialLayout_align_rotated_nested(void) {
    ecs_world_t *world = setup();
    ecs_entity_t parent = ecs_new(world), target = shape(world, 8, 3, 0.3f), e = shape(world, 2, 1.5f, 0.1f);
    ecs_set(world, parent, Position, {10, 2, 4});
    ecs_set(world, parent, Rotation, {0, GLM_PI_2f, 0});
    ecs_add_pair(world, target, EcsChildOf, parent);
    ecs_set(world, target, Position, {0, 0, 2});
    ecs_entity_t other_parent = ecs_new(world);
    ecs_set(world, other_parent, Position, {-3, 8, 0});
    ecs_add_pair(world, e, EcsChildOf, other_parent);
    ecs_set(world, e, SpatialAlign, {.target = target, .self = {.face = SpatialBack},
        .other = {.face = SpatialFront}, .rotation = SpatialMatch, .offset = {0, 0, 0.05f}, .rotation_offset = {0, 0, GLM_PI_2f}});
    ecs_progress(world, 0);
    const WorldTransform *t = transform(world, e);
    near(t->position[0], 12.25f); near(t->position[1], 2); near(t->position[2], 4);
    near(t->basis[2][0], 1); near(t->basis[0][1], 1);
    ecs_set(world, parent, Position, {20, 2, 4});
    ecs_progress(world, 0);
    near(transform(world, e)->position[0], 22.25f);
    ecs_fini(world);
}

void SpatialLayout_align_named_frames(void) {
    ecs_world_t *world = setup();
    ecs_entity_t target = shape(world, 2, 2, 2), e = shape(world, 1, 1, 1);
    ecs_entity_t socket = ecs_new_w_pair(world, EcsChildOf, target), plug = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_set(world, socket, AttachmentFrame, {.position = {3, 2, 1}});
    ecs_set(world, plug, AttachmentFrame, {.position = {0, 0, -1}, .rotation = {0, GLM_PIf, 0}});
    ecs_set(world, e, SpatialAlign, {.target = target, .self = {.frame = plug}, .other = {.frame = socket}, .rotation = SpatialMatch, .offset = {0, 0, 0.2f}});
    ecs_progress(world, 0);
    const WorldTransform *t = transform(world, e);
    near(t->position[0], 3); near(t->position[1], 2); near(t->position[2], 2.2f);
    near(t->basis[2][2], 1);
    ecs_fini(world);
}

void SpatialLayout_align_remove(void) {
    ecs_world_t *world = setup();
    ecs_entity_t target = shape(world, 2, 2, 2), e = shape(world, 1, 1, 1);
    ecs_set(world, e, Position, {3, 4, 5});
    ecs_set(world, e, Rotation, {0, 0.2f, 0});
    ecs_set(world, e, SpatialAlign, {.target = target});
    ecs_progress(world, 0);
    near(transform(world, e)->position[0], 0);
    near(ecs_get(world, e, Position)->x, 3);
    ecs_remove(world, e, SpatialAlign);
    ecs_progress(world, 0);
    near(transform(world, e)->position[0], 3);
    near(transform(world, e)->basis[2][0], sinf(0.2f));
    test_assert(!ecs_has_id(world, e, ManualTransform));
    ecs_fini(world);
}

static ecs_entity_t instance_at(ecs_world_t *world, ecs_entity_t owner, int index) {
    ecs_entity_t result = 0;
    ecs_iter_t it = ecs_each(world, ArrayInstance);
    while (ecs_each_next(&it)) {
        const ArrayInstance *instances = ecs_field(&it, ArrayInstance, 0);
        for (int i = 0; i < it.count; i ++) if (instances[i].owner == owner && instances[i].index == index) result = it.entities[i];
    }
    return result;
}

void SpatialLayout_array_stable(void) {
    ecs_world_t *world = setup();
    ecs_entity_t owner = ecs_new(world), prototype = shape(world, 0.15f, 0.3f, 0.4f);
    ecs_add_pair(world, prototype, EcsChildOf, owner);
    ecs_set(world, owner, Array, {.axis = SpatialZ, .count = 8, .spacing = 0.7f});
    ecs_progress(world, 0);
    ecs_entity_t e = instance_at(world, owner, 3);
    test_assert(e != 0);
    test_int(ecs_count(world, ArrayInstance), 8);
    near(transform(world, e)->position[2], 2.1f);
    ecs_set(world, owner, Array, {.axis = SpatialZ, .count = 10, .spacing = 0.5f});
    ecs_set(world, prototype, Box, {0.2f, 0.4f, 0.6f});
    ecs_progress(world, 0);
    test_uint(instance_at(world, owner, 3), e);
    near(transform(world, e)->position[2], 1.5f);
    near(ecs_get(world, e, Box)->x, 0.2f);
    ecs_set(world, owner, Array, {.axis = SpatialZ, .count = 4, .spacing = 0.5f});
    ecs_progress(world, 0);
    test_int(ecs_count(world, ArrayInstance), 4);
    test_uint(instance_at(world, owner, 3), e);
    ecs_remove(world, owner, Array);
    ecs_progress(world, 0);
    test_int(ecs_count(world, ArrayInstance), 0);
    test_assert(!ecs_has_id(world, prototype, EcsPrefab));
    ecs_fini(world);
}

void SpatialLayout_array_progression(void) {
    ecs_world_t *world = setup();
    ecs_entity_t owner = ecs_new(world), prototype = shape(world, 1, 1, 1);
    ecs_add_pair(world, prototype, EcsChildOf, owner);
    ecs_set(world, owner, Array, {.axis = SpatialX, .count = 3, .spacing = 2, .rotation_step = {0, 0, GLM_PI_2f}, .scale_step = {0.5f, 0, 0}});
    ecs_progress(world, 0);
    const WorldTransform *t = transform(world, instance_at(world, owner, 1));
    near(t->position[0], 2); near(t->basis[0][1], 1.5f);
    ecs_set(world, owner, Array, {.axis = SpatialX, .count = 0});
    ecs_progress(world, 0);
    test_int(ecs_count(world, ArrayInstance), 0);
    ecs_fini(world);
}

void SpatialLayout_distribute_modes(void) {
    ecs_world_t *world = setup();
    ecs_entity_t owner = ecs_new(world), a = shape(world, 1, 1, 1), b = shape(world, 2, 1, 1), c = shape(world, 1, 1, 1);
    ecs_add_pair(world, a, EcsChildOf, owner); ecs_add_pair(world, b, EcsChildOf, owner); ecs_add_pair(world, c, EcsChildOf, owner);
    float expected[][3] = {{-3.5f, 0, 3.5f}, {-2.833333f, 0, 2.833333f}, {-2.5f, 0, 2.5f}, {-2, 0, 2}};
    for (int mode = 0; mode < 4; mode ++) {
        ecs_set(world, owner, Distribute, {.axis = SpatialX, .span = 8, .mode = mode, .gap = 0.5f});
        ecs_progress(world, 0);
        near(transform(world, a)->position[0], expected[mode][0]);
        near(transform(world, b)->position[0], expected[mode][1]);
        near(transform(world, c)->position[0], expected[mode][2]);
    }
    ecs_fini(world);
}

void SpatialLayout_distribute_rotated_resize(void) {
    ecs_world_t *world = setup();
    ecs_entity_t owner = ecs_new(world), a = shape(world, 1, 1, 2), b = shape(world, 1, 1, 1);
    ecs_set(world, owner, Position, {10, 0, 0});
    ecs_set(world, owner, Rotation, {0, GLM_PI_2f, 0});
    ecs_add_pair(world, a, EcsChildOf, owner); ecs_add_pair(world, b, EcsChildOf, owner);
    ecs_set(world, a, Rotation, {0, GLM_PI_2f, 0});
    ecs_set(world, owner, Distribute, {.axis = SpatialX, .span = 8});
    ecs_progress(world, 0);
    near(transform(world, a)->position[2], 3);
    near(transform(world, b)->position[2], -3.5f);
    ecs_set(world, a, Box, {1, 1, 4});
    ecs_progress(world, 0);
    near(transform(world, a)->position[2], 2);
    ecs_fini(world);
}

void SpatialLayout_cycles(void) {
    ecs_world_t *world = setup();
    ecs_entity_t a = shape(world, 1, 1, 1), b = shape(world, 1, 1, 1);
    ecs_set(world, a, Fit, {.target = b});
    ecs_set(world, b, Fit, {.target = a});
    ecs_log_set_level(-4);
    test_assert(!box_spatial_layout_update(world));
    test_int(ecs_get(world, a, SpatialDiagnostic)->code, SpatialCycle);
    test_int(ecs_get(world, b, SpatialDiagnostic)->code, SpatialCycle);
    ecs_remove(world, b, Fit);
    test_assert(box_spatial_layout_update(world));
    test_assert(!ecs_has(world, a, SpatialDiagnostic));
    ecs_set(world, a, SpatialAlign, {.target = b});
    ecs_set(world, b, SpatialAlign, {.target = a});
    test_assert(!box_spatial_layout_update(world));
    test_int(ecs_get(world, a, SpatialDiagnostic)->code, SpatialCycle);
    ecs_fini(world);
}

void SpatialLayout_invalid_layouts(void) {
    ecs_world_t *world = setup();
    ecs_entity_t a = shape(world, 1, 1, 1), b = shape(world, 1, 1, 1);
    ecs_log_set_level(-4);
    ecs_set(world, a, Fit, {.target = b, .padding = {1, 0, 0}});
    test_assert(!box_spatial_layout_update(world));
    test_int(ecs_get(world, a, SpatialDiagnostic)->code, SpatialImpossible);
    ecs_set(world, a, Fit, {.target = b});
    ecs_delete(world, b);
    test_assert(!box_spatial_layout_update(world));
    test_int(ecs_get(world, a, SpatialDiagnostic)->code, SpatialInvalidTarget);
    ecs_fini(world);
}

void SpatialLayout_script(void) {
    ecs_world_t *world = setup();
    const char *script =
        "using box.geometry\nusing box.transform\nusing box.spatial\n"
        "hull { Box: {8, 3, 18} }\n"
        "armor { Box: {1, 0.2, 1}\n Fit: {target: hull, axes: XZ, padding: {0.4, 0, 0.8}, mode: Stretch} }\n"
        "cockpit { Box: {3, 1, 4}\n Align: {target: hull, self: {y: Min}, other: {y: Max}, offset: {0, 0.1, 2}} }\n"
        "wall { Box: {8, 3, 0.3}\n Rotation: {0, 0.5235988, 0} }\n"
        "window { Box: {2, 1.5, 0.1}\n Align: {target: wall, self: Back, other: Front, rotation: Match, offset: {0, 0, 0.05}, rotation_offset: {0, 0, 1.5707963}} }\n"
        "vents { Array: {axis: Z, count: 8, spacing: 0.7}\n rib { Box: {0.15, 0.3, 0.4} } }\n"
        "sensors { Distribute: {axis: X, span: 6, mode: SpaceBetween}\n left { Box: {0.5, 0.5, 0.5} }\n center { Box: {0.5, 0.5, 0.5} }\n right { Box: {0.5, 0.5, 0.5} } }\n";
    test_int(ecs_script_run(world, NULL, script, NULL), 0);
    ecs_progress(world, 0);
    test_int(ecs_count(world, SpatialDiagnostic), 0);
    near(ecs_get(world, ecs_lookup(world, "armor"), ResolvedBox)->x, 7.2f);
    near(transform(world, ecs_lookup(world, "cockpit"))->position[1], 2.1f);
    near(transform(world, ecs_lookup(world, "sensors.left"))->position[0], -2.75f);
    test_int(ecs_count(world, ArrayInstance), 8);
    ecs_fini(world);
}

void SpatialLayout_fit_nested_scale(void) {
    ecs_world_t *world = setup();
    ecs_entity_t parent = ecs_new(world), a = shape(world, 4, 2, 6), b = shape(world, 1, 1, 1);
    ecs_set(world, parent, Scale, {2, 1, 3});
    ecs_add_pair(world, a, EcsChildOf, parent);
    ecs_set(world, a, Rotation, {0, GLM_PI_2f, 0});
    ecs_set(world, b, Scale, {2, 1, 1});
    ecs_set(world, b, Fit, {.target = a, .axes = SpatialXZ});
    ecs_progress(world, 0);
    near(ecs_get(world, b, ResolvedBox)->x, 6);
    near(ecs_get(world, b, ResolvedBox)->z, 12);
    ecs_set(world, parent, Scale, {4, 1, 6});
    ecs_progress(world, 0);
    near(ecs_get(world, b, ResolvedBox)->x, 12);
    near(ecs_get(world, b, ResolvedBox)->z, 24);
    ecs_fini(world);
}

void SpatialLayout_align_array_group(void) {
    ecs_world_t *world = setup();
    ecs_entity_t target = shape(world, 8, 3, 18), group = ecs_new(world), prototype = shape(world, 1, 1, 1);
    ecs_add_pair(world, prototype, EcsChildOf, group);
    ecs_set(world, group, Array, {.axis = SpatialZ, .count = 3, .spacing = 2});
    ecs_set(world, group, SpatialAlign, {.target = target, .self = {.y = SpatialMin}, .other = {.y = SpatialMax}});
    ecs_progress(world, 0);
    test_int(ecs_count(world, SpatialDiagnostic), 0);
    near(transform(world, group)->position[1], 2);
    near(transform(world, instance_at(world, group, 0))->position[2], -2);
    near(transform(world, instance_at(world, group, 2))->position[2], 2);
    ecs_fini(world);
}

void SpatialLayout_group_target(void) {
    ecs_world_t *world = setup();
    ecs_entity_t group = ecs_new(world), a = shape(world, 2, 2, 2), b = shape(world, 2, 2, 2), e = shape(world, 1, 1, 1);
    ecs_add_pair(world, a, EcsChildOf, group); ecs_add_pair(world, b, EcsChildOf, group);
    ecs_set(world, a, Position, {-2, 0, 0}); ecs_set(world, b, Position, {2, 0, 0});
    ecs_set(world, group, Position, {5, 0, 0});
    ecs_set(world, e, SpatialAlign, {.target = group, .self = {.x = SpatialMin}, .other = {.x = SpatialMax}});
    ecs_progress(world, 0);
    near(transform(world, e)->position[0], 8.5f);
    ecs_set(world, b, Box, {4, 2, 2});
    ecs_progress(world, 0);
    near(transform(world, e)->position[0], 9.5f);
    ecs_fini(world);
}

void SpatialLayout_distribute_fit_chain(void) {
    ecs_world_t *world = setup();
    ecs_entity_t target = shape(world, 2, 1, 1), group = ecs_new(world), a = shape(world, 1, 1, 1), b = shape(world, 1, 1, 1);
    ecs_add_pair(world, a, EcsChildOf, group); ecs_add_pair(world, b, EcsChildOf, group);
    ecs_set(world, a, Fit, {.target = target});
    ecs_set(world, group, Distribute, {.axis = SpatialX, .span = 6});
    ecs_progress(world, 0);
    test_int(ecs_count(world, SpatialDiagnostic), 0);
    near(transform(world, a)->position[0], -2);
    near(transform(world, b)->position[0], 2.5f);
    ecs_set(world, target, Box, {4, 1, 1});
    ecs_progress(world, 0);
    near(transform(world, a)->position[0], -1);
    ecs_fini(world);
}

void SpatialLayout_distribute_remove(void) {
    ecs_world_t *world = setup();
    ecs_entity_t owner = ecs_new(world), a = shape(world, 1, 1, 1);
    ecs_add_pair(world, a, EcsChildOf, owner);
    ecs_set(world, a, Position, {2, 3, 4});
    ecs_set(world, owner, Distribute, {.axis = SpatialX, .span = 6});
    ecs_progress(world, 0);
    near(transform(world, a)->position[0], 0);
    near(transform(world, a)->position[1], 3);
    ecs_remove(world, owner, Distribute);
    ecs_progress(world, 0);
    near(transform(world, a)->position[0], 2);
    ecs_fini(world);
}

void SpatialLayout_feedback_cycle(void) {
    ecs_world_t *world = setup();
    ecs_entity_t group = ecs_new(world), a = shape(world, 1, 1, 1);
    ecs_add_pair(world, a, EcsChildOf, group);
    ecs_set(world, a, Fit, {.target = group});
    ecs_log_set_level(-4);
    test_assert(!box_spatial_layout_update(world));
    test_int(ecs_get(world, a, SpatialDiagnostic)->code, SpatialCycle);
    ecs_fini(world);
}

void SpatialLayout_mesh_bounds(void) {
    ecs_world_t *world = setup();
    ecs_entity_t target = shape(world, 4, 4, 4), e = ecs_new(world);
    ecs_set(world, e, Sphere, {.radius = 2});
    ecs_set(world, e, Fit, {.target = target});
    ecs_progress(world, 0);
    const ResolvedBounds *b = ecs_get(world, e, ResolvedBounds);
    test_assert(b != NULL);
    near(b->max[0] - b->min[0], 4);
    test_assert(!ecs_has(world, e, ResolvedBox));
    ecs_fini(world);
}

void SpatialLayout_dynamic_target(void) {
    ecs_world_t *world = setup();
    ecs_entity_t a = shape(world, 2, 2, 2), b = shape(world, 1, 1, 1), c = shape(world, 1, 1, 1);
    ecs_set(world, a, Position, {1, 0, 0});
    ecs_add(world, a, DynamicTransform);
    ecs_set(world, c, SpatialAlign, {.target = b, .offset = {0, 0, 2}});
    ecs_set(world, b, SpatialAlign, {.target = a, .offset = {0, 0, 2}});
    ecs_progress(world, 0);
    ecs_get_mut(world, a, Position)->x = 5;
    ecs_get_mut(world, a, Position)->z = 3;
    ecs_progress(world, 0);
    near(transform(world, c)->position[0], 5);
    near(transform(world, c)->position[2], 7);
    ecs_fini(world);
}

void SpatialLayout_named_frame_script(void) {
    ecs_world_t *world = setup();
    test_int(ecs_script_run(world, NULL,
        "using box.geometry\nusing box.spatial\n"
        "hull { Box: {2, 2, 2}\n mount { AttachmentFrame: {position: {0, 2, 0}} } }\n"
        "part { Box: {1, 1, 1}\n Align: {target: hull, other: {frame: hull.mount}} }", NULL), 0);
    ecs_progress(world, 0);
    near(transform(world, ecs_lookup(world, "part"))->position[1], 2);
    ecs_fini(world);
}

void SpatialLayout_array_nested_prototype(void) {
    ecs_world_t *world = setup();
    ecs_entity_t owner = ecs_new(world);
    ecs_entity_t prototype = ecs_entity(world, {.name = "part", .parent = owner});
    ecs_entity_t child = ecs_entity(world, {.name = "detail", .parent = prototype});
    ecs_set(world, child, Box, {1, 1, 1});
    ecs_set(world, child, Position, {0, 2, 0});
    ecs_set(world, owner, Array, {.axis = SpatialX, .count = 2, .spacing = 3});
    ecs_progress(world, 0);
    ecs_entity_t instance = instance_at(world, owner, 1), detail = ecs_lookup_child(world, instance, "detail");
    test_assert(detail != 0);
    near(transform(world, detail)->position[0], 3);
    near(transform(world, detail)->position[1], 2);
    ecs_set(world, child, Box, {2, 2, 2});
    ecs_set(world, child, Position, {0, 4, 0});
    ecs_progress(world, 0);
    near(ecs_get(world, detail, Box)->x, 2);
    near(transform(world, detail)->position[1], 4);
    ecs_fini(world);
}

void SpatialLayout_nested_implicit_position(void) {
    ecs_world_t *world = setup();
    ecs_entity_t parent = ecs_new(world), a = shape(world, 2, 2, 2), b = shape(world, 1, 1, 1);
    ecs_set(world, parent, Position, {10, 2, 4});
    ecs_add_pair(world, a, EcsChildOf, parent);
    ecs_set(world, b, SpatialAlign, {.target = a, .self = {.y = SpatialMin}, .other = {.y = SpatialMax}});
    ecs_progress(world, 0);
    vec3 min, max;
    test_assert(box_spatial_bounds(world, a, NULL, min, max));
    near(min[0], 9); near(max[1], 3);
    test_assert(box_spatial_bounds(world, b, NULL, min, max));
    near(min[1], 3);
    ecs_fini(world);
}

void SpatialLayout_array_after_indexed(void) {
    ecs_world_t *world = setup();
    ecs_entity_t owner = ecs_new(world), prototype = shape(world, 1, 1, 1);
    ecs_add_pair(world, prototype, EcsChildOf, owner);
    ecs_set(world, prototype, Position, {0, 0, 0});
    ecs_progress(world, 0);
    vec3 min, max;
    test_assert(box_spatial_bounds(world, prototype, NULL, min, max));
    ecs_set(world, owner, Array, {.axis = SpatialX, .count = 2, .spacing = 2});
    ecs_progress(world, 0);
    test_assert(!box_spatial_bounds(world, prototype, NULL, min, max));
    ecs_remove(world, owner, Array);
    ecs_progress(world, 0);
    test_assert(box_spatial_bounds(world, prototype, NULL, min, max));
    ecs_fini(world);
}

void SpatialLayout_diagnostic_cleanup(void) {
    ecs_world_t *world = setup();
    ecs_entity_t e = shape(world, 1, 1, 1);
    ecs_set(world, e, Fit, {0});
    ecs_log_set_level(-4);
    test_assert(!box_spatial_layout_update(world));
    test_assert(ecs_has(world, e, SpatialDiagnostic));
    ecs_remove(world, e, Fit);
    test_assert(box_spatial_layout_update(world));
    test_assert(!ecs_has(world, e, SpatialDiagnostic));
    test_assert(!ecs_has(world, e, ResolvedBounds));
    ecs_fini(world);
}

void SpatialLayout_fit_in_aligned_group(void) {
    ecs_world_t *world = setup();
    ecs_entity_t hull = shape(world, 8, 3, 18), group = ecs_new(world), a = shape(world, 1, 1, 1), b = shape(world, 1, 1, 1);
    ecs_add_pair(world, a, EcsChildOf, group); ecs_add_pair(world, b, EcsChildOf, group);
    ecs_set(world, a, Fit, {.target = hull, .axes = SpatialZ});
    ecs_set(world, group, Distribute, {.axis = SpatialX, .span = 6});
    ecs_set(world, group, SpatialAlign, {.target = hull, .self = {.y = SpatialMin}, .other = {.y = SpatialMax}});
    ecs_progress(world, 0);
    test_int(ecs_count(world, SpatialDiagnostic), 0);
    near(transform(world, a)->position[0], -2.5f);
    near(transform(world, a)->position[1], 2);
    near(ecs_get(world, a, ResolvedBox)->z, 18);
    ecs_fini(world);
}

void SpatialLayout_nested_arrays(void) {
    ecs_world_t *world = setup();
    ecs_entity_t owner = ecs_new(world);
    ecs_entity_t row = ecs_entity(world, {.name = "row", .parent = owner});
    ecs_entity_t cell = ecs_entity(world, {.name = "cell", .parent = row});
    ecs_set(world, cell, Box, {1, 1, 1});
    ecs_set(world, row, Array, {.axis = SpatialX, .count = 3, .spacing = 2});
    ecs_set(world, owner, Array, {.axis = SpatialZ, .count = 2, .spacing = 4});
    ecs_progress(world, 0);
    test_int(ecs_count(world, SpatialDiagnostic), 0);
    test_int(ecs_count(world, ArrayInstance), 8);
    ecs_entity_t row1 = instance_at(world, owner, 1);
    ecs_entity_t cell2 = instance_at(world, row1, 2);
    near(transform(world, cell2)->position[0], 4);
    near(transform(world, cell2)->position[2], 4);
    ecs_set(world, row, Array, {.axis = SpatialX, .count = 2, .spacing = 3});
    ecs_progress(world, 0);
    test_int(ecs_count(world, ArrayInstance), 6);
    near(transform(world, instance_at(world, row1, 1))->position[0], 3);
    ecs_fini(world);
}

void SpatialLayout_empty_distribution(void) {
    ecs_world_t *world = setup();
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Distribute, {.axis = SpatialX, .span = 6});
    ecs_progress(world, 0);
    test_assert(!ecs_has(world, e, SpatialDiagnostic));
    ecs_fini(world);
}

/* Run frames until the layout has nothing left to solve, and check that it gets there. */
static void converge(ecs_world_t *world) {
    box_spatial_layout_stats_t stats = {0};
    int32_t i;
    for (i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
        box_spatial_layout_stats(world, &stats);
        if (!stats.solved && !stats.synced) {
            break;
        }
    }
    test_assert(i < 10);
}

static box_spatial_layout_stats_t layout_frame(ecs_world_t *world) {
    box_spatial_layout_stats_t stats = {0};
    ecs_progress(world, 0);
    box_spatial_layout_stats(world, &stats);
    return stats;
}

void SpatialLayout_incremental_unchanged(void) {
    // engine/spatial_layout_incremental
    ecs_world_t *world = setup();
    ecs_entity_t hull = shape(world, 8, 3, 18), armor = shape(world, 1, 0.2f, 1), cockpit = shape(world, 3, 1, 4);
    ecs_set(world, armor, Fit, {.target = hull, .axes = SpatialXZ});
    ecs_set(world, cockpit, SpatialAlign, {.target = hull, .self = {.y = SpatialMin}, .other = {.y = SpatialMax}});
    ecs_entity_t owner = ecs_new(world), prototype = shape(world, 0.5f, 0.5f, 0.5f);
    ecs_add_pair(world, prototype, EcsChildOf, owner);
    ecs_set(world, owner, Array, {.axis = SpatialX, .count = 6, .spacing = 1});
    ecs_entity_t group = ecs_new(world), a = shape(world, 1, 1, 1), b = shape(world, 1, 1, 1);
    ecs_add_pair(world, a, EcsChildOf, group); ecs_add_pair(world, b, EcsChildOf, group);
    ecs_set(world, group, Distribute, {.axis = SpatialX, .span = 6});
    converge(world);
    box_spatial_layout_stats_t stats = layout_frame(world);
    test_assert(stats.nodes > 0);
    test_int(stats.solved, 0);
    test_int(stats.synced, 0);
    test_bool(box_spatial_layout_update(world), true);
    int64_t before = ecs_os_api_malloc_count + ecs_os_api_calloc_count + ecs_os_api_realloc_count;
    for (int32_t i = 0; i < 10; i ++) {
        test_bool(box_spatial_layout_update(world), true);
    }
    int64_t after = ecs_os_api_malloc_count + ecs_os_api_calloc_count + ecs_os_api_realloc_count;
    test_int(after - before, 0);
    test_int(ecs_count(world, ArrayInstance), 6);
    near(transform(world, cockpit)->position[1], 2);
    near(ecs_get(world, armor, ResolvedBox)->z, 18);
    near(transform(world, a)->position[0], -2.5f);
    ecs_fini(world);
}

void SpatialLayout_incremental_fit_target_moved(void) {
    // engine/spatial_layout_incremental
    ecs_world_t *world = setup();
    ecs_entity_t hull = shape(world, 8, 3, 18), armor = shape(world, 1, 0.2f, 1), cockpit = shape(world, 3, 1, 4);
    ecs_set(world, armor, Fit, {.target = hull, .axes = SpatialXZ});
    ecs_set(world, cockpit, SpatialAlign, {.target = hull, .self = {.y = SpatialMin}, .other = {.y = SpatialMax}});
    ecs_entity_t other = shape(world, 4, 4, 4), other_armor = shape(world, 1, 1, 1);
    ecs_set(world, other, Position, {20, 0, 0});
    ecs_set(world, other_armor, Fit, {.target = other});
    converge(world);
    ecs_set(world, hull, Position, {4, 5, 0});
    box_spatial_layout_stats_t stats = layout_frame(world);
    test_assert(stats.solved > 0);
    test_assert(stats.solved < stats.nodes);
    near(transform(world, cockpit)->position[0], 4);
    near(transform(world, cockpit)->position[1], 7);
    ecs_set(world, hull, Box, {10, 3, 20});
    stats = layout_frame(world);
    test_assert(stats.solved < stats.nodes);
    near(ecs_get(world, armor, ResolvedBox)->x, 10);
    near(ecs_get(world, armor, ResolvedBox)->z, 20);
    near(ecs_get(world, other_armor, ResolvedBox)->x, 4);
    converge(world);
    near(transform(world, cockpit)->position[1], 7);
    test_int(ecs_count(world, SpatialDiagnostic), 0);
    ecs_fini(world);
}

void SpatialLayout_incremental_array_count(void) {
    // engine/spatial_layout_incremental
    ecs_world_t *world = setup();
    ecs_entity_t owner = ecs_new(world), prototype = shape(world, 1, 1, 1);
    ecs_add_pair(world, prototype, EcsChildOf, owner);
    ecs_set(world, owner, Array, {.axis = SpatialX, .count = 3, .spacing = 2});
    ecs_entity_t other = ecs_new(world), other_prototype = shape(world, 1, 1, 1);
    ecs_add_pair(world, other_prototype, EcsChildOf, other);
    ecs_set(world, other, Array, {.axis = SpatialZ, .count = 2, .spacing = 2});
    converge(world);
    test_int(ecs_count(world, ArrayInstance), 5);
    ecs_entity_t kept = instance_at(world, owner, 1);
    ecs_set(world, owner, Array, {.axis = SpatialX, .count = 5, .spacing = 2});
    box_spatial_layout_stats_t stats = layout_frame(world);
    test_int(stats.synced, 1);
    test_int(ecs_count(world, ArrayInstance), 7);
    test_uint(instance_at(world, owner, 1), kept);
    near(transform(world, instance_at(world, owner, 4))->position[0], 8);
    converge(world);
    ecs_set(world, owner, Array, {.axis = SpatialX, .count = 2, .spacing = 3});
    layout_frame(world);
    test_int(ecs_count(world, ArrayInstance), 4);
    test_uint(instance_at(world, owner, 1), kept);
    near(transform(world, kept)->position[0], 3);
    near(transform(world, instance_at(world, other, 1))->position[2], 2);
    converge(world);
    test_int(ecs_count(world, SpatialDiagnostic), 0);
    ecs_fini(world);
}

void SpatialLayout_incremental_prototype_edit(void) {
    // engine/spatial_layout_incremental
    ecs_world_t *world = setup();
    ecs_entity_t target = shape(world, 8, 3, 8), owner = ecs_new(world), prototype = shape(world, 1, 1, 1);
    ecs_add_pair(world, prototype, EcsChildOf, owner);
    ecs_set(world, owner, Array, {.axis = SpatialX, .count = 3, .spacing = 2});
    ecs_set(world, owner, SpatialAlign, {.target = target, .self = {.y = SpatialMin}, .other = {.y = SpatialMax}});
    converge(world);
    ecs_entity_t e = instance_at(world, owner, 2);
    near(transform(world, e)->position[1], 2);
    ecs_set(world, prototype, Box, {1, 3, 1});
    ecs_set(world, prototype, Position, {0, 0, 1});
    box_spatial_layout_stats_t stats = layout_frame(world);
    test_assert(stats.synced > 0);
    near(ecs_get(world, e, Box)->y, 3);
    near(ecs_get(world, e, Position)->z, 1);
    converge(world);
    near(ecs_get(world, e, Box)->y, 3);
    near(transform(world, e)->position[0], 2);
    near(transform(world, e)->position[1], 3);
    near(transform(world, e)->position[2], 0);
    test_int(ecs_count(world, SpatialDiagnostic), 0);
    ecs_fini(world);
}

void SpatialLayout_incremental_reparent(void) {
    // engine/spatial_layout_incremental
    ecs_world_t *world = setup();
    ecs_entity_t group = ecs_new(world), a = shape(world, 2, 2, 2), b = shape(world, 2, 2, 2), e = shape(world, 1, 1, 1);
    ecs_add_pair(world, a, EcsChildOf, group);
    ecs_set(world, a, Position, {-2, 0, 0});
    ecs_set(world, b, Position, {2, 0, 0});
    ecs_set(world, e, SpatialAlign, {.target = group, .self = {.x = SpatialMin}, .other = {.x = SpatialMax}});
    converge(world);
    near(transform(world, e)->position[0], -0.5f);
    ecs_add_pair(world, b, EcsChildOf, group);
    layout_frame(world);
    near(transform(world, e)->position[0], 3.5f);
    converge(world);
    ecs_remove_pair(world, b, EcsChildOf, group);
    layout_frame(world);
    near(transform(world, e)->position[0], -0.5f);

    ecs_entity_t row = ecs_new(world), c = shape(world, 1, 1, 1), d = shape(world, 1, 1, 1);
    ecs_add_pair(world, c, EcsChildOf, row); ecs_add_pair(world, d, EcsChildOf, row);
    ecs_set(world, row, Distribute, {.axis = SpatialX, .span = 6});
    converge(world);
    near(transform(world, c)->position[0], -2.5f);
    near(transform(world, d)->position[0], 2.5f);
    ecs_remove_pair(world, d, EcsChildOf, row);
    layout_frame(world);
    near(transform(world, c)->position[0], 0);
    test_assert(!ecs_has_id(world, d, ManualTransform));
    converge(world);
    near(transform(world, d)->position[0], 0);
    test_int(ecs_count(world, SpatialDiagnostic), 0);
    ecs_fini(world);
}

void SpatialLayout_incremental_delete_prototype(void) {
    // engine/spatial_layout_incremental
    ecs_world_t *world = setup();
    ecs_entity_t owner = ecs_new(world), first = shape(world, 1, 1, 1), second = shape(world, 1, 2, 1);
    ecs_add_pair(world, first, EcsChildOf, owner);
    ecs_add_pair(world, second, EcsChildOf, owner);
    ecs_set(world, owner, Array, {.axis = SpatialX, .count = 3, .spacing = 2});
    ecs_entity_t e = shape(world, 1, 1, 1);
    ecs_set(world, e, SpatialAlign, {.target = owner, .self = {.y = SpatialMin}, .other = {.y = SpatialMax}});
    converge(world);
    test_int(ecs_count(world, ArrayInstance), 6);
    near(transform(world, e)->position[1], 1.5f);
    ecs_delete(world, second);
    layout_frame(world);
    test_int(ecs_count(world, ArrayInstance), 3);
    ecs_iter_t it = ecs_each(world, ArrayInstance);
    while (ecs_each_next(&it)) {
        const ArrayInstance *instances = ecs_field(&it, ArrayInstance, 0);
        for (int i = 0; i < it.count; i ++) test_uint(instances[i].prototype, first);
    }
    near(transform(world, e)->position[1], 1);
    converge(world);
    test_int(ecs_count(world, SpatialDiagnostic), 0);
    ecs_fini(world);
}

void SpatialLayout_incremental_dynamic_parent(void) {
    // engine/spatial_layout_incremental
    ecs_world_t *world = setup();
    ecs_entity_t parent = ecs_new(world), target = shape(world, 2, 2, 2), e = shape(world, 1, 1, 1);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_add(world, parent, DynamicTransform);
    ecs_add_pair(world, target, EcsChildOf, parent);
    ecs_set(world, e, SpatialAlign, {.target = target, .self = {.y = SpatialMin}, .other = {.y = SpatialMax}});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    near(transform(world, e)->position[1], 1.5f);
    for (int32_t i = 1; i <= 3; i ++) {
        ecs_get_mut(world, parent, Position)->x = (float)i;
        ecs_progress(world, 0);
        near(transform(world, e)->position[0], (float)i);
    }
    ecs_fini(world);
}

void SpatialLayout_incremental_hot_reload(void) {
    // engine/spatial_layout_incremental
    ecs_world_t *world = setup();
    const char *code =
        "using box.geometry\nusing box.spatial\n"
        "hull { Box: {8, 3, 18} }\n"
        "armor { Box: {1, 0.2, 1}\n Fit: {target: hull, axes: XZ} }\n"
        "vents { Array: {axis: Z, count: 4, spacing: 1}\n rib { Box: {0.2, 0.2, 0.2} } }\n";
    const char *edited =
        "using box.geometry\nusing box.spatial\n"
        "hull { Box: {6, 3, 12} }\n"
        "armor { Box: {1, 0.2, 1}\n Fit: {target: hull, axes: XZ} }\n"
        "vents { Array: {axis: Z, count: 2, spacing: 3}\n rib { Box: {0.2, 0.4, 0.2} } }\n";
    ecs_entity_t script = ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = code
    });
    test_assert(script != 0);
    converge(world);
    near(ecs_get(world, ecs_lookup(world, "armor"), ResolvedBox)->z, 18);
    test_int(ecs_count(world, ArrayInstance), 4);
    test_assert(ecs_script_update(world, script, 0, edited) == 0);
    converge(world);
    ecs_entity_t armor = ecs_lookup(world, "armor"), vents = ecs_lookup(world, "vents");
    near(ecs_get(world, armor, ResolvedBox)->x, 6);
    near(ecs_get(world, armor, ResolvedBox)->z, 12);
    test_int(ecs_count(world, ArrayInstance), 2);
    ecs_entity_t e = instance_at(world, vents, 1);
    near(transform(world, e)->position[2], 3);
    near(ecs_get(world, e, Box)->y, 0.4f);
    test_assert(ecs_script_update(world, script, 0, code) == 0);
    converge(world);
    near(ecs_get(world, ecs_lookup(world, "armor"), ResolvedBox)->z, 18);
    test_int(ecs_count(world, ArrayInstance), 4);
    test_int(ecs_count(world, SpatialDiagnostic), 0);
    ecs_fini(world);
}

void SpatialLayout_incremental_parent_component(void) {
    // engine/spatial_layout_incremental
    ecs_world_t *world = setup();
    ecs_entity_t group = ecs_new(world), a = shape(world, 2, 2, 2), e = shape(world, 1, 1, 1);
    ecs_set(world, a, EcsParent, {group});
    ecs_set(world, a, Position, {-2, 0, 0});
    ecs_set(world, e, SpatialAlign, {.target = group, .self = {.x = SpatialMin}, .other = {.x = SpatialMax}});
    converge(world);
    near(transform(world, e)->position[0], -0.5f);
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, EcsParent, {group});
    ecs_set(world, b, Position, {2, 0, 0});
    ecs_set(world, b, Box, {2, 2, 2});
    layout_frame(world);
    near(transform(world, e)->position[0], 3.5f);
    converge(world);
    ecs_delete(world, b);
    layout_frame(world);
    near(transform(world, e)->position[0], -0.5f);
    converge(world);
    ecs_log_set_level(-4);
    ecs_delete(world, group);
    layout_frame(world);
    test_assert(!ecs_is_alive(world, a));
    test_int(ecs_get(world, e, SpatialDiagnostic)->code, SpatialInvalidTarget);
    ecs_fini(world);
}
