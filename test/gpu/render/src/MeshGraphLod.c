/**
 * @file MeshGraphLod.c
 * @brief Tests for drawing the levels of detail of MeshGraph shapes on the GPU backend.
 *
 * A small broadleaf whose bark and crown shapes build four levels is placed at the distance of
 * each level of a view with four levels, and the cull pass of the view must draw both shapes at
 * that level.
 *
 * Adjacent levels are drawn at the distance where the view switches between them, forced by
 * the distances of the view, and their silhouettes, covered areas and colors must differ little.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <math.h>
#include "render.h"
#include "mesh_cache.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** A small broadleaf whose bark and crown build four levels of detail */
#define MESH_GRAPH_LOD_TREE "test/assets/mesh/lod_tree.flecs"

/** Levels of detail of the view: level 1 from 15 units, 2 from 40 and 3 from 80 */
static const LodSettings lod_four = {.levels = 4, .distances = {0, 15, 40, 80}};

/** Size of the images of the silhouettes */
#define LOD_IMAGE (256)

/** Field of view of the images of the silhouettes, in degrees: an image is the crop of a 60
 * degree view on a 1080 pixel high screen, so the tree covers the pixels it covers in a game */
#define LOD_FOV (2.0f * glm_deg(atanf(tanf(glm_rad(30.0f)) * (float)LOD_IMAGE / 1080.0f)))

/** Create a headless world that renders with the GPU on the main thread, a camera at a place
 * looking down -z and its view with levels of detail.
 *
 * @param fov Vertical field of view of the camera in degrees
 * @param camera_y Height of the camera
 * @param camera_z Distance of the camera along +z
 * @param lod Levels of detail of the view
 * @param file Image the surface writes, NULL for none
 * @param view Receives the view
 * @return The world
 */
static ecs_world_t* lod_world(
    float fov,
    float camera_y,
    float camera_z,
    LodSettings lod,
    const char *file,
    ecs_entity_t *view)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, camera_y, camera_z});
    ecs_set(world, camera, Camera, {.fov = fov, .near_ = 0.1f, .far_ = 1000});
    *view = ecs_new(world);
    ecs_set(world, *view, View, {.camera = camera, .lighting.enabled = true,
        .culling = {.enabled = true},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lod = lod});
    ecs_singleton_set(world, AmbientLight, {.sky = {160, 170, 200}, .ground = {60, 55, 50},
        .intensity = 1});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Rotation, {-0.6f, (float)GLM_PI * 0.75f, 0});
    ecs_set(world, light, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = LOD_IMAGE, .height = LOD_IMAGE,
        .write_to_file = (char*)file, .resolution_scale = 1});

    return world;
}

/** Run the script of the tree and place its bark and crown.
 *
 * @param world The world
 * @param z Its z position, negative in front of the camera
 * @param bark Receives the bark shape
 * @param crown Receives the crown shape
 */
static void lod_tree(
    ecs_world_t *world,
    float z,
    ecs_entity_t *bark,
    ecs_entity_t *crown)
{
    char *path = box_app_locate(MESH_GRAPH_LOD_TREE);
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);
    ecs_entity_t tree = ecs_lookup(world, "lod_tree");
    test_assert(tree != 0);
    *bark = ecs_lookup_child(world, tree, "bark");
    *crown = ecs_lookup_child(world, tree, "crown");
    test_assert(*bark != 0 && *crown != 0);
    ecs_set(world, *bark, Position, {0, 0, z});
    ecs_set(world, *bark, Rgb, {120, 90, 70});
    ecs_set(world, *bark, PbrMaterial, {.metallic = 0, .roughness = 1});
    ecs_set(world, *crown, Position, {0, 0, z});
    ecs_set(world, *crown, Rgb, {90, 200, 80});
    ecs_set(world, *crown, PbrMaterial, {.metallic = 0, .roughness = 0.8f});
}

/** Run frames until the shapes are ready and the backend rendered them.
 *
 * @param world The world
 * @param bark The bark shape
 * @param crown The crown shape
 */
static void lod_frames(
    ecs_world_t *world,
    ecs_entity_t bark,
    ecs_entity_t crown)
{
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    test_int(ecs_get(world, bark, MeshGraphStatus)->state, MeshGraphReady);
    test_int(ecs_get(world, crown, MeshGraphStatus)->state, MeshGraphReady);
    test_int(ecs_get(world, bark, MeshGraphStatus)->levels, 4);
    test_int(ecs_get(world, crown, MeshGraphStatus)->levels, 4);
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
}

/** Get the index count of a level of the mesh of a shape.
 *
 * @param world The world
 * @param e The shape
 * @param level The level
 * @return The indices of the mesh of the level
 */
static int64_t lod_indices(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t level)
{
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL && sm->mesh != 0);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, box_mesh_cache_lod(cache, sm->mesh, level));
    test_assert(mesh != NULL);

    return (int64_t)mesh->index_count;
}

/** Draw the tree at a distance and check that both its shapes draw at a level.
 *
 * @param z The z position of the tree
 * @param level The level it must draw at
 */
static void lod_tree_at(
    float z,
    int32_t level)
{
    ecs_entity_t view, bark, crown;
    ecs_world_t *world = lod_world(60, 2, 0, lod_four, NULL, &view);
    lod_tree(world, z, &bark, &crown);
    lod_frames(world, bark, crown);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    printf("tree at %f: levels %d %d %d %d, %d indices\n", (double)z, stats.levels[0],
        stats.levels[1], stats.levels[2], stats.levels[3], (int)stats.indices);
    test_int(stats.visible, 2);
    test_int(stats.levels[level], 2);
    test_int(stats.indices, lod_indices(world, bark, level) + lod_indices(world, crown, level));
    if (level) {
        test_assert(lod_indices(world, bark, level) < lod_indices(world, bark, level - 1));
        test_assert(lod_indices(world, crown, level) < lod_indices(world, crown, level - 1));
    }

    ecs_fini(world);
}

void MeshGraphLod_levels_by_distance(void) {
    lod_tree_at(-8, 0);
    lod_tree_at(-28, 1);
    lod_tree_at(-60, 2);
    lod_tree_at(-110, 3);
}

void MeshGraphLod_far_tree_small_fraction(void) {
    ecs_entity_t view, bark, crown;
    ecs_world_t *world = lod_world(60, 2, 0, lod_four, NULL, &view);
    lod_tree(world, -100, &bark, &crown);
    lod_frames(world, bark, crown);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.levels[3], 2);
    int64_t full = lod_indices(world, bark, 0) + lod_indices(world, crown, 0);
    printf("tree at 100 m draws %d of %d indices\n", (int)stats.indices, (int)full);
    test_assert(stats.indices * 100 < full * 15);
    ecs_fini(world);
}

/** Render the tree at a distance with the view forced to a level by its distances.
 *
 * @param level The level
 * @param distance Distance of the camera from the tree
 * @return The image, freed with stbi_image_free
 */
static unsigned char* lod_silhouette(
    int32_t level,
    float distance)
{
    LodSettings lod = {.levels = 4};
    ecs_entity_t view, bark, crown;
    int32_t i;
    for (i = 1; i < 4; i ++) {
        lod.distances[i] = i <= level ? 0.001f * (float)i : 100000.0f * (float)i;
    }

    char file[32];
    snprintf(file, sizeof(file), "mesh_graph_lod_%d_%d.png", level, (int)distance);
    ecs_world_t *world = lod_world(LOD_FOV, 2.2f, distance, lod, file, &view);
    lod_tree(world, 0, &bark, &crown);
    lod_frames(world, bark, crown);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.levels[level], 2);
    char *out = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(out, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_int(w, LOD_IMAGE);
    test_int(h, LOD_IMAGE);
    ecs_os_free(out);
    ecs_fini(world);

    return img;
}

/** Check whether a pixel is covered by the tree: a channel above the black background.
 *
 * @param img The image
 * @param i The pixel
 * @return Whether it is covered
 */
static bool lod_covered(
    const unsigned char *img,
    int32_t i)
{
    const unsigned char *p = &img[i * 4];

    return p[0] > 8 || p[1] > 8 || p[2] > 8;
}

/** Adjacent levels are drawn at the distance where the view switches from one to the other */
void MeshGraphLod_adjacent_silhouettes(void) {
    int32_t l, i;
    for (l = 0; l + 1 < 4; l ++) {
        float distance = lod_four.distances[l + 1];
        unsigned char *img[2] = {lod_silhouette(l, distance), lod_silhouette(l + 1, distance)};
        int32_t area[2] = {0}, diff = 0;
        double color[2][3] = {{0}};
        for (i = 0; i < LOD_IMAGE * LOD_IMAGE; i ++) {
            bool a = lod_covered(img[0], i), b = lod_covered(img[1], i);
            area[0] += a;
            area[1] += b;
            diff += a != b;
            for (int32_t k = 0; k < 2; k ++) {
                const unsigned char *p = &img[k][i * 4];
                if (lod_covered(img[k], i)) {
                    color[k][0] += p[0];
                    color[k][1] += p[1];
                    color[k][2] += p[2];
                }
            }
        }

        printf("levels %d and %d at %f: %d and %d covered pixels, %d differ\n", l, l + 1,
            (double)distance, area[0], area[1], diff);
        test_assert(area[0] > 200);
        test_assert(abs(area[1] - area[0]) * 100 < area[0] * 25);
        test_assert(diff * 100 < area[0] * 30);
        for (int32_t c = 0; c < 3; c ++) {
            double m0 = color[0][c] / area[0], m1 = color[1][c] / area[1];
            printf("  mean channel %d: %f and %f\n", c, m0, m1);
            test_assert(fabs(m0 - m1) < 25.0);
        }

        stbi_image_free(img[0]);
        stbi_image_free(img[1]);
    }
}
