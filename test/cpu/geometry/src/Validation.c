#include <box.h>
#include <bake_test.h>
#include <math.h>
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

void Validation_valid_bevel(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t entity = ecs_entity(world, {.name = "subject"});
    ecs_set(world, entity, Box, {.x = 1, .y = 2, .z = 3});
    ecs_set(world, entity, Bevel, {.top_front = 0.2f, .front_left = 0.1f, .segments = 4});
    ecs_entity_t session = ecs_shell_session_init(world, NULL);
    char *out = box_command_run(world, session, "logs error 20", NULL);
    test_str(out, "");
    ecs_os_free(out);
    ecs_fini(world);
}

void Validation_negative_bevel(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t entity = ecs_entity(world, {.name = "subject"});
    ecs_set(world, entity, Box, {.x = 1, .y = 2, .z = 3});
    ecs_set(world, entity, Bevel, {.top_front = 0.2f, .back_right = -0.1f, .segments = 4});
    Validation_logs(world, "invalid Bevel on subject: radii must not be negative");
    ecs_fini(world);
}

void Validation_infinite_bevel(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t entity = ecs_entity(world, {.name = "subject"});
    ecs_set(world, entity, Box, {.x = 1, .y = 2, .z = 3});
    ecs_set(world, entity, Bevel, {.radius = INFINITY, .segments = 4});
    Validation_logs(world, "invalid Bevel on subject: radii must be finite");
    ecs_fini(world);
}

void Validation_bevel_before_box(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t entity = ecs_entity(world, {.name = "subject"});
    ecs_set(world, entity, Bevel, {.top_front = 0.2f, .segments = 4});
    ecs_entity_t session = ecs_shell_session_init(world, NULL);
    char *out = box_command_run(world, session, "logs error 20", NULL);
    test_str(out, "");
    ecs_os_free(out);
    ecs_set(world, entity, Box, {.x = 1, .y = 2, .z = 3});
    out = box_command_run(world, session, "logs error 20", NULL);
    test_str(out, "");
    ecs_os_free(out);
    test_assert(ecs_get(world, entity, ShapeMesh)->mesh != 0);
    ecs_fini(world);
}

static const char *Validation_surface_assets =
    "using box.geometry\n"
    "edge { Path2D: {start: {-1,-1}, segments: [{kind: Line, end: {1,-1}},"
    "{kind: Line, end: {1,1}}, {kind: Line, end: {-1,1}}], closed: true}}\n"
    "wall { Path2D: {start: {0.5,0}, segments: [{kind: Line, end: {1,0.5}},"
    "{kind: Line, end: {0.5,1}}]}}\n"
    "profile { Profile2D: {outer: edge}}\n"
    "route { Path3D: {segments: [{kind: Line, end: {0,1,0}}]}}\n";

static const char *Validation_surface_children[] = {
    "sweep", "loft", "lathe", "extrusion", "prism", "ellipsoid", "torus"
};

#define Validation_surface_count \
    ((int32_t)(sizeof(Validation_surface_children) / sizeof(Validation_surface_children[0])))

/* Returns the child of an instance spawned from a prefab child. Such a child has no name, only
 * an IsA pair to the prefab child. */
static ecs_entity_t Validation_spawned_child(
    ecs_world_t *world,
    ecs_entity_t instance,
    ecs_entity_t base_child)
{
    ecs_entity_t result = 0;
    ecs_iter_t it = ecs_children(world, instance);
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            if (ecs_has_pair(world, it.entities[i], EcsIsA, base_child)) {
                result = it.entities[i];
            }
        }
    }

    return result;
}

/* Checks that every child of the instance inherits the surface of its prefab child and
 * resolves to the mesh of the prefab child, and that nothing was logged. */
static void Validation_test_inherited_surfaces(
    ecs_world_t *world,
    ecs_entity_t prefab,
    ecs_entity_t instance)
{
    for (int32_t i = 0; i < Validation_surface_count; i ++) {
        const char *name = Validation_surface_children[i];
        ecs_entity_t base = ecs_lookup_child(world, prefab, name);
        test_assert(base != 0);
        const ShapeMesh *base_mesh = ecs_get(world, base, ShapeMesh);
        test_assert(base_mesh != NULL);
        test_assert(base_mesh->mesh != 0);

        ecs_entity_t child = Validation_spawned_child(world, instance, base);
        test_assert(child != 0);
        const ShapeMesh *mesh = ecs_get(world, child, ShapeMesh);
        test_assert(mesh != NULL);
        test_uint(mesh->mesh, base_mesh->mesh);
    }

    ecs_entity_t session = ecs_shell_session_init(world, NULL);
    char *out = box_command_run(world, session, "logs error 20", NULL);
    test_str(out, "");
    ecs_os_free(out);
}

void Validation_inherited_surface_child(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_int(ecs_script_run(world, "assets", Validation_surface_assets, NULL), 0);
    ecs_entity_t edge = ecs_lookup(world, "edge"), wall = ecs_lookup(world, "wall");
    ecs_entity_t profile = ecs_lookup(world, "profile"), route = ecs_lookup(world, "route");

    ecs_entity_t prefab = ecs_entity(world, {.name = "Shapes"});
    ecs_add_id(world, prefab, EcsPrefab);
    ecs_entity_t children[Validation_surface_count];
    for (int32_t i = 0; i < Validation_surface_count; i ++) {
        children[i] = ecs_new_w_parent(world, prefab, Validation_surface_children[i]);
    }

    ecs_set(world, children[0], Sweep, {.path = route, .profile = profile,
        .scale = {1, 1}, .taper = 1, .cap_start = true, .cap_end = true});

    Loft loft = {.cap_start = true, .cap_end = true};
    ecs_vec_init_t(NULL, &loft.sections, LoftSection, 2);
    *ecs_vec_append_t(NULL, &loft.sections, LoftSection) =
        (LoftSection){.profile = profile, .scale = {1, 1}};
    *ecs_vec_append_t(NULL, &loft.sections, LoftSection) =
        (LoftSection){.profile = profile, .offset = {0, 2, 0}, .scale = {1, 1}};
    ecs_set_ptr(world, children[1], Loft, &loft);
    ecs_vec_fini_t(NULL, &loft.sections, LoftSection);

    ecs_set(world, children[2], Lathe, {.profile = wall});
    ecs_set(world, children[3], ProfileExtrusion, {.profile = profile, .depth = 0.2f,
        .cap_start = true, .cap_end = true});

    BeveledPrism prism = {.depth = 2, .inset = 0.2f, .bevel = 0.1f};
    ecs_vec_init_t(NULL, &prism.footprint, vec2, 4);
    ecs_vec_set_count_t(NULL, &prism.footprint, vec2, 4);
    vec2 points[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    memcpy(ecs_vec_first(&prism.footprint), points, sizeof(points));
    ecs_set_ptr(world, children[4], BeveledPrism, &prism);
    ecs_vec_fini_t(NULL, &prism.footprint, vec2);

    ecs_set(world, children[5], EllipsoidShell, {.radius = {1, 1, 1},
        .longitude_end = 6.2831853f, .latitude_start = -1.5707963f,
        .latitude_end = 1.5707963f});
    ecs_set(world, children[6], TorusShell, {.radius = 1, .tube = 0.25f,
        .ring_end = 6.2831853f, .tube_end = 6.2831853f});

    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    Validation_test_inherited_surfaces(world, prefab, instance);
    ecs_fini(world);
}

void Validation_inherited_surface_child_script(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_int(ecs_script_run(world, "assets", Validation_surface_assets, NULL), 0);
    test_int(ecs_script_run(world, "shapes",
        "using box.geometry\n"
        "prefab Shapes {\n"
        "  sweep { Sweep: {path: route, profile: profile} }\n"
        "  loft { Loft: {sections: [{profile: profile},"
        " {profile: profile, offset: {0,2,0}}], cap_start: true, cap_end: true} }\n"
        "  lathe { Lathe: {profile: wall} }\n"
        "  extrusion { ProfileExtrusion: {profile: profile, depth: 0.2,"
        " cap_start: true, cap_end: true} }\n"
        "  prism { BeveledPrism: {footprint: [{-1,-1}, {1,-1}, {1,1}, {-1,1}],"
        " depth: 2, inset: 0.2, bevel: 0.1} }\n"
        "  ellipsoid { EllipsoidShell: {radius: {1,1,1}, longitude_end: 6.2831853,"
        " latitude_start: -1.5707963, latitude_end: 1.5707963} }\n"
        "  torus { TorusShell: {radius: 1, tube: 0.25, ring_end: 6.2831853,"
        " tube_end: 6.2831853} }\n"
        "}\n"
        "instance { (IsA, Shapes) }\n", NULL), 0);
    Validation_test_inherited_surfaces(world,
        ecs_lookup(world, "Shapes"), ecs_lookup(world, "instance"));
    ecs_fini(world);
}

void Validation_inherited_invalid_surface_child(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t prefab = ecs_entity(world, {.name = "Shapes"});
    ecs_add_id(world, prefab, EcsPrefab);
    ecs_entity_t child = ecs_new_w_parent(world, prefab, "extrusion");
    ecs_set(world, child, ProfileExtrusion, {.depth = 1});
    ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_new_w_pair(world, EcsIsA, prefab);

    ecs_entity_t session = ecs_shell_session_init(world, NULL);
    char *out = box_command_run(world, session, "logs error 20", NULL);
    test_assert(out != NULL);
    const char *expected = "invalid ProfileExtrusion on Shapes.extrusion: missing Profile2D";
    const char *first = strstr(out, expected);
    test_assert(first != NULL);
    test_assert(strstr(first + 1, "invalid ProfileExtrusion") == NULL);
    ecs_os_free(out);
    ecs_fini(world);
}
