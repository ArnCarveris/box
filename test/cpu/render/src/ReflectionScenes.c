/**
 * @file ReflectionScenes.c
 * @brief Tests for the hand placed reflection probes of the Japanese garden scene.
 *
 * The scene runs without its Surface and its LightBake, so no GPU backend starts and no probe
 * volume is placed; the reflection probe registry places the probes as it does in boxrun.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "render.h"
#include "spatial.h"

/** Path of the garden scene in the repository */
#define GARDEN_PATH "etc/scenes/japanese_garden.flecs"

/** Path of this file in the repository, which locates the repository root */
#define GARDEN_SOURCE "test/cpu/render/src/"

/** Hand placed probes of the garden */
#define GARDEN_PROBES (14)

/** Local probes that may cover one point of the garden at most */
#define GARDEN_OVERLAP_MAX (3)

/** Roughness below which a surface shows its reflections */
#define GARDEN_GLOSSY (0.3f)

/** Read a file of the repository into a string.
 *
 * @param file Path of the file in the repository
 * @return The contents, owned by the caller, NULL when the file cannot be read
 */
static char* file_read(
    const char *file)
{
    char *result = NULL;
    const char *source = strstr(__FILE__, GARDEN_SOURCE);
    int32_t root = source ? (int32_t)(source - __FILE__) : 0;
    char *path = flecs_asprintf("%.*s%s", root, __FILE__, file);
    FILE *f = fopen(path, "rb");
    ecs_os_free(path);
    if (!f) {
        goto done;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    result = ecs_os_malloc((ecs_size_t)size + 1);
    size_t read = fread(result, 1, (size_t)size, f);
    result[read] = '\0';
    fclose(f);
done:
    return result;
}

/** Blank every line of a text that contains a string, keeping the line breaks.
 *
 * @param text The text, edited in place
 * @param match The string a line to blank contains
 */
static void lines_blank(
    char *text,
    const char *match)
{
    char *at = strstr(text, match);
    while (at) {
        char *start = at;
        while (start > text && start[-1] != '\n') {
            start --;
        }

        char *end = strchr(at, '\n');
        if (!end) {
            end = start + strlen(start);
        }

        ecs_os_memset(start, ' ', (ecs_size_t)(end - start));
        at = strstr(end, match);
    }
}

/** Load the garden scene without its Surface and LightBake and run two frames.
 *
 * @param scene Receives the garden scene root
 * @return The world
 */
static ecs_world_t* garden_new(
    ecs_entity_t *scene)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *text = file_read(GARDEN_PATH);
    test_assert(text != NULL);
    lines_blank(text, "Surface:");
    lines_blank(text, "LightBake:");
    test_assert(ecs_script_run(world, GARDEN_PATH, text, NULL) == 0);
    ecs_os_free(text);

    *scene = ecs_lookup(world, "japanese_garden");
    test_assert(*scene != 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    return world;
}

/** Get the resident probes of the garden, the scene probe included.
 *
 * @param world The world
 * @param scene The garden scene root
 * @param count Receives the number of probes
 * @return The probes in blend order
 */
static const box_render_reflection_probe_t* garden_probes(
    ecs_world_t *world,
    ecs_entity_t scene,
    int32_t *count)
{
    const box_render_reflection_probe_t *result = box_render_reflect_probes(
        world, scene, count);
    test_assert(result != NULL);

    return result;
}

/** Check whether a point is inside a box of a probe.
 *
 * @param p The probe
 * @param half Half size of the box, oriented by the axes of the probe
 * @param point The point
 * @return Whether the point is inside
 */
static bool box_contains(
    const box_render_reflection_probe_t *p,
    const float *half,
    const float *point)
{
    bool result = true;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        float local = (point[0] - p->center[0]) * p->axes[i][0] +
            (point[1] - p->center[1]) * p->axes[i][1] +
            (point[2] - p->center[2]) * p->axes[i][2];
        if (fabsf(local) > half[i] + 1e-4f) {
            result = false;
        }
    }

    return result;
}

/** Get the world axis aligned bounds of the influence box of a probe.
 *
 * @param p The probe
 * @param min Receives the minimum corner
 * @param max Receives the maximum corner
 */
static void influence_bounds(
    const box_render_reflection_probe_t *p,
    vec3 min,
    vec3 max)
{
    int32_t i, j;
    for (i = 0; i < 3; i ++) {
        float half = 0;
        for (j = 0; j < 3; j ++) {
            half += fabsf(p->axes[j][i]) * p->extent[j];
        }

        min[i] = p->center[i] - half;
        max[i] = p->center[i] + half;
    }
}

/** Get the volume of the influence box of a probe.
 *
 * @param p The probe
 * @return The volume
 */
static float influence_volume(
    const box_render_reflection_probe_t *p)
{
    return 8.0f * p->extent[0] * p->extent[1] * p->extent[2];
}

void ReflectionScenes_garden_places_probes(void) {
    // reflection_probes/scenes
    ecs_entity_t scene;
    ecs_world_t *world = garden_new(&scene);

    const ReflectionBake *bake = ecs_get(world, scene, ReflectionBake);
    test_assert(bake != NULL);
    test_int(bake->resolution, 128);
    test_bool(bake->scene_probe, true);
    test_int(ecs_count(world, ReflectionProbe), GARDEN_PROBES);

    const ReflectionBakeStatus *status = ecs_get(world, scene, ReflectionBakeStatus);
    test_assert(status != NULL);
    test_int(status->probes, GARDEN_PROBES + 1);
    test_int(status->resident, GARDEN_PROBES + 1);
    test_assert(status->error == NULL);

    ecs_fini(world);
}

void ReflectionScenes_garden_capture_points_inside_boxes(void) {
    // reflection_probes/scenes
    ecs_entity_t scene;
    ecs_world_t *world = garden_new(&scene);

    int32_t i, count;
    const box_render_reflection_probe_t *probes = garden_probes(world, scene, &count);
    test_int(count, GARDEN_PROBES + 1);
    for (i = 0; i < count; i ++) {
        const box_render_reflection_probe_t *p = &probes[i];
        if (p->scene) {
            continue;
        }

        test_assert(box_contains(p, p->extent, p->capture));
        test_assert(p->projection[0] > 0);
        test_assert(p->projection[1] > 0);
        test_assert(p->projection[2] > 0);
        test_assert(box_contains(p, p->projection, p->capture));
    }

    ecs_fini(world);
}

void ReflectionScenes_garden_capture_points_in_open_space(void) {
    // reflection_probes/scenes
    ecs_entity_t scene;
    ecs_world_t *world = garden_new(&scene);

    ecs_vec_t hits;
    ecs_vec_init_t(NULL, &hits, ecs_entity_t, 0);
    int32_t i, count;
    const box_render_reflection_probe_t *probes = garden_probes(world, scene, &count);
    for (i = 0; i < count; i ++) {
        const box_render_reflection_probe_t *p = &probes[i];
        if (p->scene) {
            continue;
        }

        vec3 capture = {p->capture[0], p->capture[1], p->capture[2]};
        ecs_vec_clear(&hits);
        int32_t found = box_spatial_overlap_sphere(world, scene, capture, 0.1f, &hits);
        test_int(found, 0);
    }

    ecs_vec_fini_t(NULL, &hits, ecs_entity_t);
    ecs_fini(world);
}

void ReflectionScenes_garden_inner_probes_higher_priority(void) {
    // reflection_probes/scenes
    ecs_entity_t scene;
    ecs_world_t *world = garden_new(&scene);

    int32_t i, j, k, count, overlaps = 0;
    const box_render_reflection_probe_t *probes = garden_probes(world, scene, &count);
    for (i = 0; i < count; i ++) {
        const box_render_reflection_probe_t *a = &probes[i];
        if (a->scene) {
            continue;
        }

        vec3 a_min, a_max;
        influence_bounds(a, a_min, a_max);
        for (j = 0; j < count; j ++) {
            const box_render_reflection_probe_t *b = &probes[j];
            if (b->scene || i == j || influence_volume(a) >= influence_volume(b)) {
                continue;
            }

            vec3 b_min, b_max;
            influence_bounds(b, b_min, b_max);
            bool overlap = true;
            for (k = 0; k < 3; k ++) {
                if (a_max[k] <= b_min[k] || b_max[k] <= a_min[k]) {
                    overlap = false;
                }
            }

            if (overlap) {
                test_assert(a->priority >= b->priority);
                overlaps ++;
            }
        }
    }

    test_assert(overlaps > 0);

    ecs_fini(world);
}

void ReflectionScenes_garden_few_overlapping_probes(void) {
    // reflection_probes/scenes
    ecs_entity_t scene;
    ecs_world_t *world = garden_new(&scene);

    const float heights[] = {0.3f, 1.0f, 1.8f};
    int32_t h, i, count, most = 0;
    const box_render_reflection_probe_t *probes = garden_probes(world, scene, &count);
    float x, z;
    for (h = 0; h < 3; h ++) {
        for (x = -16.0f; x <= 16.0f; x += 0.25f) {
            for (z = -14.0f; z <= 14.0f; z += 0.25f) {
                const float point[3] = {x, heights[h], z};
                int32_t covering = 0;
                for (i = 0; i < count; i ++) {
                    if (!probes[i].scene &&
                        box_contains(&probes[i], probes[i].extent, point))
                    {
                        covering ++;
                    }
                }

                most = covering > most ? covering : most;
            }
        }
    }

    test_assert(most > 1);
    test_assert(most <= GARDEN_OVERLAP_MAX);

    ecs_fini(world);
}

void ReflectionScenes_garden_probes_have_glossy_surfaces(void) {
    // reflection_probes/scenes
    ecs_entity_t scene;
    ecs_world_t *world = garden_new(&scene);

    int32_t i, count;
    const box_render_reflection_probe_t *probes = garden_probes(world, scene, &count);
    int32_t glossy[GARDEN_PROBES + 1] = {0};
    test_int(count, GARDEN_PROBES + 1);
    ecs_iter_t it = ecs_each(world, PbrMaterial);
    while (ecs_each_next(&it)) {
        const PbrMaterial *m = ecs_field(&it, PbrMaterial, 0);
        int32_t e;
        for (e = 0; e < it.count; e ++) {
            vec3 min, max;
            if (m[e].roughness > GARDEN_GLOSSY ||
                !box_spatial_bounds(world, it.entities[e], NULL, min, max))
            {
                continue;
            }

            const float center[3] = {
                (min[0] + max[0]) * 0.5f, (min[1] + max[1]) * 0.5f, (min[2] + max[2]) * 0.5f
            };
            for (i = 0; i < count; i ++) {
                if (box_contains(&probes[i], probes[i].extent, center)) {
                    glossy[i] ++;
                }
            }
        }
    }

    for (i = 0; i < count; i ++) {
        if (!probes[i].scene) {
            test_assert(glossy[i] > 0);
        }
    }

    ecs_fini(world);
}
