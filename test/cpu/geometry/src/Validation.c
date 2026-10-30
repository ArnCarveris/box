#include <box.h>
#include <bake_test.h>
#include <string.h>
#include "mesh_cache.h"

static void Validation_logs(ecs_world_t *world, const char *expected)
{
    ecs_entity_t session = ecs_shell_session_init(world, NULL);
    char *out = box_command_run(world, session, "logs error 20", NULL);
    test_assert(out != NULL);
    test_assert(strstr(out, expected) != NULL);
    ecs_os_free(out);
}

void Validation_valid_mesh(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t entity = ecs_entity(world, {.name = "subject"});
    ecs_set(world, entity, Box, {.x = 1, .y = 2, .z = 3});
    ecs_entity_t session = ecs_shell_session_init(world, NULL);
    char *out = box_command_run(world, session, "logs error 20", NULL);
    test_str(out, "");
    ecs_os_free(out);
    ecs_fini(world);
}

void Validation_empty_mesh(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t entity = ecs_entity(world, {.name = "subject"});
    ecs_set(world, entity, Box, {.x = 0, .y = 0, .z = 0});
    Validation_logs(world, "all triangles collapse after shape scale");
    ecs_fini(world);
}

void Validation_valid_to_empty(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t entity = ecs_entity(world, {.name = "subject"});
    ecs_set(world, entity, Box, {.x = 1, .y = 2, .z = 3});
    test_flt(ecs_get(world, entity, ShapeMesh)->scale[0], 1);
    ecs_set(world, entity, Box, {.x = 0, .y = 0, .z = 0});
    test_flt(ecs_get(world, entity, ShapeMesh)->scale[0], 0);
    Validation_logs(world, "empty ShapeMesh on subject");
    ecs_fini(world);
}

void Validation_invalid_footprint(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t entity = ecs_entity(world, {.name = "subject"});
    ecs_set(world, entity, BeveledPrism, {.depth = 1});
    Validation_logs(world, "invalid BeveledPrism on subject: footprint needs 3 or 4 vertices");
    ecs_fini(world);
}

void Validation_missing_profile(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t entity = ecs_entity(world, {.name = "subject"});
    ecs_set(world, entity, ProfileExtrusion, {.depth = 1});
    test_assert(ecs_has_id(world, entity, ecs_lookup(world, "box.geometry.ShapeMesh")));
    Validation_logs(world, "invalid ProfileExtrusion on subject: missing Profile2D");
    ecs_fini(world);
}

void Validation_degenerate_path(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t path = ecs_entity(world, {.name = "path"});
    Path3D value = {0};
    *ecs_vec_append_t(NULL, &value.segments, PathSegment3D) =
        (PathSegment3D){.kind = PathLine};
    ecs_set_ptr(world, path, Path3D, &value);
    ecs_vec_fini_t(NULL, &value.segments, PathSegment3D);
    Validation_logs(world, "invalid Path3D on path: path is degenerate");
    ecs_fini(world);
}

void Validation_unsupported_sweep_inputs(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t entity = ecs_entity(world, {.name = "subject"});
    ecs_set(world, entity, Sweep, {.profile = ecs_new(world), .contour = ecs_new(world)});
    Validation_logs(world, "invalid Sweep on subject: set exactly one of profile or contour");
    ecs_fini(world);
}
