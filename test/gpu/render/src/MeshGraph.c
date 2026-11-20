/**
 * @file MeshGraph.c
 * @brief Tests for drawing a MeshGraph shape on the GPU backend.
 *
 * A tube along a path with spheres instanced on three points of it is one shape. The image is
 * read where a sphere, the tube between two spheres and the background should be. A small
 * three level tree grown by Stem, Branches and Prune is read at its trunk and around it. A small
 * broadleaf and a small conifer crown dressed by Leaves, Needles and Cards are drawn with and
 * without CrownNormals, read in their crowns and around them and compared.
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

/** A small broadleaf with leaf cards as a script */
#define MESH_GRAPH_BROADLEAF "test/assets/mesh/broadleaf.flecs"

/** A small conifer with spruce fronds as a script */
#define MESH_GRAPH_CONIFER "test/assets/mesh/conifer.flecs"

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

/** Count the lit pixels of a rectangle of an RGBA8 image.
 *
 * @param img The image
 * @param width Width of the image
 * @param x0 First column
 * @param y0 First row
 * @param x1 Column after the last
 * @param y1 Row after the last
 * @return The pixels with a channel above 32
 */
static int32_t mesh_graph_lit_count(
    const unsigned char *img,
    int width,
    int x0,
    int y0,
    int x1,
    int y1)
{
    int32_t result = 0;
    int x, y;
    for (y = y0; y < y1; y ++) {
        for (x = x0; x < x1; x ++) {
            const unsigned char *p = &img[(y * width + x) * 4];
            result += p[0] > 32 || p[1] > 32 || p[2] > 32;
        }
    }

    return result;
}

/** Get the mean green of the foliage pixels of a half of the crown region of a 64 by 64
 * image: pixels whose green is above 32 and 20 above their red, which leaves out the bark.
 *
 * @param img The image
 * @param x0 First column
 * @param x1 Column after the last
 * @return The mean green, 0 without foliage
 */
static float mesh_graph_foliage_mean(
    const unsigned char *img,
    int x0,
    int x1)
{
    int64_t sum = 0;
    int32_t count = 0;
    int x, y;
    for (y = 8; y < 56; y ++) {
        for (x = x0; x < x1; x ++) {
            const unsigned char *p = &img[(y * 64 + x) * 4];
            if (p[1] > 32 && p[1] > p[0] + 20) {
                sum += p[1];
                count ++;
            }
        }
    }

    return count ? (float)sum / (float)count : 0.0f;
}

/** Render a plant of a script with bark and crown shapes into a 64 by 64 image, with the
 * CrownNormals of its graph or with their strength set to 0, lit by a sun from the left and a
 * hemisphere ambient whose indirect light the VertexOcclusion of the crown darkens.
 *
 * @param script The script
 * @param name Name of the plant entity
 * @param file Name of the image
 * @param crown_normals Keep the CrownNormals of the graph
 * @return The image, freed with stbi_image_free
 */
static unsigned char* mesh_graph_crown(
    const char *script,
    const char *name,
    const char *file,
    bool crown_normals)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 2.2f, 7.5f});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_singleton_set(world, AmbientLight, {.sky = {160, 170, 200}, .ground = {60, 55, 50},
        .intensity = 1});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Rotation, {-0.6f, (float)GLM_PI * 0.75f, 0});
    ecs_set(world, light, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 64,
        .write_to_file = (char*)file, .resolution_scale = 1});

    char *path = box_app_locate(script);
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);
    ecs_entity_t plant = ecs_lookup(world, name);
    test_assert(plant != 0);
    ecs_entity_t bark = ecs_lookup_child(world, plant, "bark");
    ecs_entity_t crown = ecs_lookup_child(world, plant, "crown");
    ecs_entity_t normals = ecs_lookup_child(world, plant, "normals");
    test_assert(bark != 0 && crown != 0 && normals != 0);
    ecs_set(world, bark, Position, {0, 0, 0});
    ecs_set(world, bark, Rgb, {120, 90, 70});
    ecs_set(world, bark, PbrMaterial, {.metallic = 0, .roughness = 1});
    ecs_set(world, crown, Position, {0, 0, 0});
    ecs_set(world, crown, Rgb, {90, 200, 80});
    ecs_set(world, crown, PbrMaterial, {.metallic = 0, .roughness = 0.8f});
    ecs_add(world, crown, VertexOcclusion);
    if (!crown_normals) {
        MeshCrownNormals *n = ecs_get_mut(world, normals, MeshCrownNormals);
        n->strength = 0;
        ecs_modified(world, normals, MeshCrownNormals);
    }

    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }

    test_int(ecs_get(world, crown, MeshGraphStatus)->state, MeshGraphReady);
    test_int(ecs_get(world, bark, MeshGraphStatus)->state, MeshGraphReady);
    box_render_sync(world);
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    char *out = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(out, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_int(w, 64);
    test_int(h, 64);
    ecs_os_free(out);
    ecs_fini(world);

    return img;
}

/** Check the images of a crown with and without CrownNormals: both have foliage in the crown
 * and none in the corners, and the bent normals shade the crown as a volume, lit on the side
 * of the sun.
 *
 * @param flat The image without CrownNormals
 * @param bent The image with CrownNormals
 * @param top Row of the top of the crown region
 * @param bottom Row after the bottom of the crown region
 */
static void mesh_graph_crown_check(
    const unsigned char *flat,
    const unsigned char *bent,
    int top,
    int bottom)
{
    int32_t lit_flat = mesh_graph_lit_count(flat, 64, 20, top, 44, bottom);
    int32_t lit_bent = mesh_graph_lit_count(bent, 64, 20, top, 44, bottom);
    int64_t diff = 0;
    int i;
    printf("lit %d without and %d with crown normals\n", lit_flat, lit_bent);
    test_assert(lit_flat > (44 - 20) * (bottom - top) / 4);
    test_assert(lit_bent > (44 - 20) * (bottom - top) / 4);
    test_assert(!mesh_graph_lit(flat, 64, 2, 2));
    test_assert(!mesh_graph_lit(flat, 64, 61, 2));
    test_assert(!mesh_graph_lit(bent, 64, 2, 2));
    test_assert(!mesh_graph_lit(bent, 64, 61, 2));
    for (i = 0; i < 64 * 64 * 4; i ++) {
        diff += abs((int)flat[i] - (int)bent[i]);
    }

    printf("difference %d\n", (int)diff);
    test_assert(diff > 64 * 64);

    /* The sun comes from the left: with the normals of the envelope the crown shades as one
     * volume, bright on the left and dark on the right, while flat cards shade alike on both */
    float flat_left = mesh_graph_foliage_mean(flat, 16, 32);
    float flat_right = mesh_graph_foliage_mean(flat, 32, 48);
    float bent_left = mesh_graph_foliage_mean(bent, 16, 32);
    float bent_right = mesh_graph_foliage_mean(bent, 32, 48);
    printf("left %f right %f without, left %f right %f with crown normals\n",
        (double)flat_left, (double)flat_right, (double)bent_left, (double)bent_right);
    test_assert(flat_right > 0 && bent_right > 0);
    test_assert(bent_left > 1.2f * bent_right);
    test_assert(flat_left < 1.1f * flat_right);
}

void MeshGraph_broadleaf_crown(void) {
    unsigned char *flat = mesh_graph_crown(MESH_GRAPH_BROADLEAF, "broadleaf",
        "mesh_graph_broadleaf_flat.png", false);
    unsigned char *bent = mesh_graph_crown(MESH_GRAPH_BROADLEAF, "broadleaf",
        "mesh_graph_broadleaf.png", true);
    mesh_graph_crown_check(flat, bent, 14, 34);
    stbi_image_free(flat);
    stbi_image_free(bent);
}

void MeshGraph_conifer_crown(void) {
    unsigned char *flat = mesh_graph_crown(MESH_GRAPH_CONIFER, "conifer",
        "mesh_graph_conifer_flat.png", false);
    unsigned char *bent = mesh_graph_crown(MESH_GRAPH_CONIFER, "conifer",
        "mesh_graph_conifer.png", true);
    mesh_graph_crown_check(flat, bent, 14, 44);
    stbi_image_free(flat);
    stbi_image_free(bent);
}
