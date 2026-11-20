/**
 * @file MeshGraph.c
 * @brief Tests for drawing a MeshGraph shape on the GPU backend.
 *
 * A tube along a path with spheres instanced on three points of it is one shape. The image is
 * read where a sphere, the tube between two spheres and the background should be. A small
 * three level tree grown by Stem, Branches and Prune is read at its trunk and around it.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** A three level tree graph as a script */
#define MESH_GRAPH_TREE "test/assets/mesh/tree.flecs"

/** Check whether a pixel of an RGBA8 image is lit.
 *
 * @param img The image
 * @param width Width of the image
 * @param x Column
 * @param y Row
 * @return Whether a channel is above 32
 */
static bool mesh_graph_lit(
    const unsigned char *img,
    int width,
    int x,
    int y)
{
    const unsigned char *p = &img[(y * width + x) * 4];
    printf("pixel %d, %d: %d %d %d\n", x, y, p[0], p[1], p[2]);

    return p[0] > 32 || p[1] > 32 || p[2] > 32;
}

void MeshGraph_tube_with_spheres(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Rotation, {0, (float)GLM_PI, 0});
    ecs_set(world, light, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 32,
        .write_to_file = "mesh_graph_rail.png", .resolution_scale = 1});

    ecs_entity_t path = ecs_new(world);
    ecs_set(world, path, Path3D, {.start = {-3, 0, 0},
        .segments = BOX_VEC_VIEW(PathSegment3D, {.kind = PathLine, .end = {3, 0, 0}})});
    ecs_entity_t curve = ecs_new(world);
    ecs_set(world, curve, MeshPath, {.path = path, .radius = 0.4f});
    ecs_entity_t tube = ecs_new(world);
    ecs_set(world, tube, MeshTube, {.curves = curve, .sides = 16});
    ecs_entity_t points = ecs_new(world);
    ecs_set(world, points, MeshPointsOnCurves, {.curves = curve, .count = 3});
    ecs_entity_t ball_shape = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, ball_shape, IcoSphere, {.segments = 3, .smooth = true, .radius = 0.9f});
    ecs_entity_t ball = ecs_new(world);
    ecs_set(world, ball, MeshShape, {.entity = ball_shape});
    ecs_entity_t balls = ecs_new(world);
    ecs_set(world, balls, MeshInstanceOnPoints, {.points = points, .instance = ball});
    ecs_entity_t all = ecs_new(world);
    ecs_set(world, all, MeshMerge, {.inputs = BOX_VEC_VIEW(ecs_entity_t, tube, balls)});

    ecs_entity_t rail = ecs_new(world);
    ecs_set(world, rail, Position, {0, 0, 0});
    ecs_set(world, rail, Rgb, {255, 255, 255});
    ecs_set(world, rail, PbrMaterial, {.metallic = 0, .roughness = 1});
    ecs_set(world, rail, MeshGraph, {.output = all});

    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }

    test_int(ecs_get(world, rail, MeshGraphStatus)->state, MeshGraphReady);
    box_render_sync(world);
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    char *file = flecs_asprintf("%s/mesh_graph_rail.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(file, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_int(w, 64);
    test_int(h, 32);
    test_assert(mesh_graph_lit(img, w, 32, 13));
    test_assert(mesh_graph_lit(img, w, 39, 16));
    test_assert(!mesh_graph_lit(img, w, 39, 8));
    test_assert(!mesh_graph_lit(img, w, 2, 2));

    stbi_image_free(img);
    ecs_os_free(file);
    ecs_fini(world);
}

void MeshGraph_branching_tree(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 2.5f, 9});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Rotation, {0, (float)GLM_PI, 0});
    ecs_set(world, light, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 64,
        .write_to_file = "mesh_graph_tree.png", .resolution_scale = 1});

    char *path = box_app_locate(MESH_GRAPH_TREE);
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);
    ecs_entity_t tree = ecs_lookup(world, "tree");
    test_assert(tree != 0);
    ecs_set(world, tree, Position, {0, 0, 0});
    ecs_set(world, tree, Rgb, {255, 255, 255});
    ecs_set(world, tree, PbrMaterial, {.metallic = 0, .roughness = 1});

    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }

    test_int(ecs_get(world, tree, MeshGraphStatus)->state, MeshGraphReady);
    box_render_sync(world);
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    char *file = flecs_asprintf("%s/mesh_graph_tree.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(file, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_int(w, 64);
    test_int(h, 64);
    test_assert(mesh_graph_lit(img, w, 32, 41));
    test_assert(!mesh_graph_lit(img, w, 2, 2));
    test_assert(!mesh_graph_lit(img, w, 61, 2));
    test_assert(!mesh_graph_lit(img, w, 2, 61));

    stbi_image_free(img);
    ecs_os_free(file);
    ecs_fini(world);
}
