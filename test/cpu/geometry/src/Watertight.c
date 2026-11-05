/**
 * @file Watertight.c
 * @brief Every round shape mesh is closed: each edge is shared by two triangles wound oppositely,
 * no T-junctions, and every face points outward.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "mesh_cache.h"

/** A vertex position with its index, sorted by x to weld coincident vertices */
typedef struct watertight_vertex_t {
    float p[3];    /**< Position */
    int32_t index; /**< Index of the vertex in the mesh */
} watertight_vertex_t;

/** Order welded vertices by x.
 *
 * @param a First vertex
 * @param b Second vertex
 * @return Negative, zero or positive as a sorts before, with or after b
 */
static int watertight_compare(
    const void *a,
    const void *b)
{
    float xa = ((const watertight_vertex_t*)a)->p[0];
    float xb = ((const watertight_vertex_t*)b)->p[0];

    return (xa > xb) - (xa < xb);
}

void Watertight_sphere(void) {
    // renderer/sphere_gaps
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    ecs_entity_t entities[512];
    int32_t count = 0;
    char name[96];
    int32_t segments[14] = { 3, 4, 5, 6, 7, 8, 9, 10, 12, 16, 20, 24, 32, 64 };
    for (int32_t s = 0; s < 14; s ++) {
        for (int32_t smooth = 0; smooth < 2; smooth ++) {
            snprintf(name, sizeof(name), "sphere segments %d smooth %d", segments[s], smooth);
            ecs_entity_t e = ecs_entity(world, { .name = name });
            ecs_set(world, e, Sphere,
                { .segments = segments[s], .smooth = smooth, .radius = 0.28f });
            entities[count ++] = e;
        }
    }

    for (int32_t m = 0; m < count; m ++) {
        const ShapeMesh *sm = ecs_get(world, entities[m], ShapeMesh);
        test_assert(sm != NULL);
        const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
        test_assert(mesh != NULL);
        test_assert(mesh->index_count > 0);
        test_int(mesh->index_count % 3, 0);
        int32_t n = mesh->vertex_count;
        watertight_vertex_t *sorted = ecs_os_malloc_n(watertight_vertex_t, n);
        int32_t *weld = ecs_os_malloc_n(int32_t, n);
        for (int32_t i = 0; i < n; i ++) {
            glm_vec3_copy(mesh->vertices[i].position, sorted[i].p);
            sorted[i].index = i;
        }

        qsort(sorted, (size_t)n, sizeof(watertight_vertex_t), watertight_compare);
        for (int32_t i = 0; i < n; i ++) {
            weld[sorted[i].index] = sorted[i].index;
            for (int32_t j = i - 1; j >= 0 && sorted[i].p[0] - sorted[j].p[0] < 1e-4f; j --) {
                if (glm_vec3_distance(sorted[i].p, sorted[j].p) < 1e-4f) {
                    weld[sorted[i].index] = weld[sorted[j].index];
                    break;
                }
            }
        }

        ecs_map_t edges;
        ecs_map_init(&edges, NULL);
        int32_t degenerate = 0, open = 0, inward = 0;
        double volume = 0.0;
        vec3 center;
        glm_vec3_center((float*)mesh->min, (float*)mesh->max, center);
        for (int32_t t = 0; t < mesh->index_count; t += 3) {
            uint64_t v[3];
            for (int32_t k = 0; k < 3; k ++) {
                v[k] = (uint64_t)weld[mesh->indices[t + k]];
            }

            if (v[0] == v[1] || v[1] == v[2] || v[2] == v[0]) {
                degenerate ++;
            }

            for (int32_t k = 0; k < 3; k ++) {
                ecs_map_val_t *c = ecs_map_ensure(&edges, (v[k] << 32) | v[(k + 1) % 3]);
                (*c) ++;
            }

            const float *a = mesh->vertices[mesh->indices[t]].position;
            const float *b = mesh->vertices[mesh->indices[t + 1]].position;
            const float *c = mesh->vertices[mesh->indices[t + 2]].position;
            vec3 ab, ac, face, mid, out;
            glm_vec3_sub((float*)b, (float*)a, ab);
            glm_vec3_sub((float*)c, (float*)a, ac);
            glm_vec3_cross(ab, ac, face);
            volume += (double)glm_vec3_dot((float*)a, face) / 6.0;
            glm_vec3_add((float*)a, (float*)b, mid);
            glm_vec3_add(mid, (float*)c, mid);
            glm_vec3_scale(mid, 1.0f / 3.0f, mid);
            glm_vec3_sub(mid, center, out);
            if (glm_vec3_dot(face, out) <= 0.0f) {
                inward ++;
            }
        }

        for (int32_t t = 0; t < mesh->index_count; t += 3) {
            for (int32_t k = 0; k < 3; k ++) {
                uint64_t a = (uint64_t)weld[mesh->indices[t + k]];
                uint64_t b = (uint64_t)weld[mesh->indices[t + (k + 1) % 3]];
                ecs_map_val_t *fwd = ecs_map_get(&edges, (a << 32) | b);
                ecs_map_val_t *back = ecs_map_get(&edges, (b << 32) | a);
                if (!fwd || *fwd != 1 || !back || *back != 1) {
                    open ++;
                }
            }
        }

        if (degenerate || open || inward || volume <= 0.0) {
            printf("%s: degenerate %d, open edges %d, inward faces %d, volume %f\n",
                ecs_get_name(world, entities[m]), degenerate, open, inward, volume);
        }

        test_int(degenerate, 0);
        test_int(open, 0);
        test_int(inward, 0);
        test_assert(volume > 0.0);
        ecs_map_fini(&edges);
        ecs_os_free(sorted);
        ecs_os_free(weld);
    }

    ecs_fini(world);
}

void Watertight_hemisphere(void) {
    // renderer/sphere_gaps
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    ecs_entity_t entities[512];
    int32_t count = 0;
    char name[96];
    int32_t segments[14] = { 3, 4, 5, 6, 7, 8, 9, 10, 12, 16, 20, 24, 32, 64 };
    for (int32_t s = 0; s < 14; s ++) {
        for (int32_t smooth = 0; smooth < 2; smooth ++) {
            snprintf(name, sizeof(name), "hemisphere segments %d smooth %d", segments[s], smooth);
            ecs_entity_t e = ecs_entity(world, { .name = name });
            ecs_set(world, e, HemiSphere,
                { .segments = segments[s], .smooth = smooth, .radius = 0.5f });
            entities[count ++] = e;
        }
    }

    for (int32_t m = 0; m < count; m ++) {
        const ShapeMesh *sm = ecs_get(world, entities[m], ShapeMesh);
        test_assert(sm != NULL);
        const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
        test_assert(mesh != NULL);
        test_assert(mesh->index_count > 0);
        test_int(mesh->index_count % 3, 0);
        int32_t n = mesh->vertex_count;
        watertight_vertex_t *sorted = ecs_os_malloc_n(watertight_vertex_t, n);
        int32_t *weld = ecs_os_malloc_n(int32_t, n);
        for (int32_t i = 0; i < n; i ++) {
            glm_vec3_copy(mesh->vertices[i].position, sorted[i].p);
            sorted[i].index = i;
        }

        qsort(sorted, (size_t)n, sizeof(watertight_vertex_t), watertight_compare);
        for (int32_t i = 0; i < n; i ++) {
            weld[sorted[i].index] = sorted[i].index;
            for (int32_t j = i - 1; j >= 0 && sorted[i].p[0] - sorted[j].p[0] < 1e-4f; j --) {
                if (glm_vec3_distance(sorted[i].p, sorted[j].p) < 1e-4f) {
                    weld[sorted[i].index] = weld[sorted[j].index];
                    break;
                }
            }
        }

        ecs_map_t edges;
        ecs_map_init(&edges, NULL);
        int32_t degenerate = 0, open = 0, inward = 0;
        double volume = 0.0;
        vec3 center;
        glm_vec3_center((float*)mesh->min, (float*)mesh->max, center);
        for (int32_t t = 0; t < mesh->index_count; t += 3) {
            uint64_t v[3];
            for (int32_t k = 0; k < 3; k ++) {
                v[k] = (uint64_t)weld[mesh->indices[t + k]];
            }

            if (v[0] == v[1] || v[1] == v[2] || v[2] == v[0]) {
                degenerate ++;
            }

            for (int32_t k = 0; k < 3; k ++) {
                ecs_map_val_t *c = ecs_map_ensure(&edges, (v[k] << 32) | v[(k + 1) % 3]);
                (*c) ++;
            }

            const float *a = mesh->vertices[mesh->indices[t]].position;
            const float *b = mesh->vertices[mesh->indices[t + 1]].position;
            const float *c = mesh->vertices[mesh->indices[t + 2]].position;
            vec3 ab, ac, face, mid, out;
            glm_vec3_sub((float*)b, (float*)a, ab);
            glm_vec3_sub((float*)c, (float*)a, ac);
            glm_vec3_cross(ab, ac, face);
            volume += (double)glm_vec3_dot((float*)a, face) / 6.0;
            glm_vec3_add((float*)a, (float*)b, mid);
            glm_vec3_add(mid, (float*)c, mid);
            glm_vec3_scale(mid, 1.0f / 3.0f, mid);
            glm_vec3_sub(mid, center, out);
            if (glm_vec3_dot(face, out) <= 0.0f) {
                inward ++;
            }
        }

        for (int32_t t = 0; t < mesh->index_count; t += 3) {
            for (int32_t k = 0; k < 3; k ++) {
                uint64_t a = (uint64_t)weld[mesh->indices[t + k]];
                uint64_t b = (uint64_t)weld[mesh->indices[t + (k + 1) % 3]];
                ecs_map_val_t *fwd = ecs_map_get(&edges, (a << 32) | b);
                ecs_map_val_t *back = ecs_map_get(&edges, (b << 32) | a);
                if (!fwd || *fwd != 1 || !back || *back != 1) {
                    open ++;
                }
            }
        }

        if (degenerate || open || inward || volume <= 0.0) {
            printf("%s: degenerate %d, open edges %d, inward faces %d, volume %f\n",
                ecs_get_name(world, entities[m]), degenerate, open, inward, volume);
        }

        test_int(degenerate, 0);
        test_int(open, 0);
        test_int(inward, 0);
        test_assert(volume > 0.0);
        ecs_map_fini(&edges);
        ecs_os_free(sorted);
        ecs_os_free(weld);
    }

    ecs_fini(world);
}

void Watertight_icosphere(void) {
    // renderer/sphere_gaps
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    ecs_entity_t entities[512];
    int32_t count = 0;
    char name[96];
    for (int32_t level = 0; level < 5; level ++) {
        for (int32_t smooth = 0; smooth < 2; smooth ++) {
            snprintf(name, sizeof(name), "icosphere level %d smooth %d", level, smooth);
            ecs_entity_t e = ecs_entity(world, { .name = name });
            ecs_set(world, e, IcoSphere, { .segments = level, .smooth = smooth, .radius = 1 });
            entities[count ++] = e;
        }
    }

    for (int32_t m = 0; m < count; m ++) {
        const ShapeMesh *sm = ecs_get(world, entities[m], ShapeMesh);
        test_assert(sm != NULL);
        const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
        test_assert(mesh != NULL);
        test_assert(mesh->index_count > 0);
        test_int(mesh->index_count % 3, 0);
        int32_t n = mesh->vertex_count;
        watertight_vertex_t *sorted = ecs_os_malloc_n(watertight_vertex_t, n);
        int32_t *weld = ecs_os_malloc_n(int32_t, n);
        for (int32_t i = 0; i < n; i ++) {
            glm_vec3_copy(mesh->vertices[i].position, sorted[i].p);
            sorted[i].index = i;
        }

        qsort(sorted, (size_t)n, sizeof(watertight_vertex_t), watertight_compare);
        for (int32_t i = 0; i < n; i ++) {
            weld[sorted[i].index] = sorted[i].index;
            for (int32_t j = i - 1; j >= 0 && sorted[i].p[0] - sorted[j].p[0] < 1e-4f; j --) {
                if (glm_vec3_distance(sorted[i].p, sorted[j].p) < 1e-4f) {
                    weld[sorted[i].index] = weld[sorted[j].index];
                    break;
                }
            }
        }

        ecs_map_t edges;
        ecs_map_init(&edges, NULL);
        int32_t degenerate = 0, open = 0, inward = 0;
        double volume = 0.0;
        vec3 center;
        glm_vec3_center((float*)mesh->min, (float*)mesh->max, center);
        for (int32_t t = 0; t < mesh->index_count; t += 3) {
            uint64_t v[3];
            for (int32_t k = 0; k < 3; k ++) {
                v[k] = (uint64_t)weld[mesh->indices[t + k]];
            }

            if (v[0] == v[1] || v[1] == v[2] || v[2] == v[0]) {
                degenerate ++;
            }

            for (int32_t k = 0; k < 3; k ++) {
                ecs_map_val_t *c = ecs_map_ensure(&edges, (v[k] << 32) | v[(k + 1) % 3]);
                (*c) ++;
            }

            const float *a = mesh->vertices[mesh->indices[t]].position;
            const float *b = mesh->vertices[mesh->indices[t + 1]].position;
            const float *c = mesh->vertices[mesh->indices[t + 2]].position;
            vec3 ab, ac, face, mid, out;
            glm_vec3_sub((float*)b, (float*)a, ab);
            glm_vec3_sub((float*)c, (float*)a, ac);
            glm_vec3_cross(ab, ac, face);
            volume += (double)glm_vec3_dot((float*)a, face) / 6.0;
            glm_vec3_add((float*)a, (float*)b, mid);
            glm_vec3_add(mid, (float*)c, mid);
            glm_vec3_scale(mid, 1.0f / 3.0f, mid);
            glm_vec3_sub(mid, center, out);
            if (glm_vec3_dot(face, out) <= 0.0f) {
                inward ++;
            }
        }

        for (int32_t t = 0; t < mesh->index_count; t += 3) {
            for (int32_t k = 0; k < 3; k ++) {
                uint64_t a = (uint64_t)weld[mesh->indices[t + k]];
                uint64_t b = (uint64_t)weld[mesh->indices[t + (k + 1) % 3]];
                ecs_map_val_t *fwd = ecs_map_get(&edges, (a << 32) | b);
                ecs_map_val_t *back = ecs_map_get(&edges, (b << 32) | a);
                if (!fwd || *fwd != 1 || !back || *back != 1) {
                    open ++;
                }
            }
        }

        if (degenerate || open || inward || volume <= 0.0) {
            printf("%s: degenerate %d, open edges %d, inward faces %d, volume %f\n",
                ecs_get_name(world, entities[m]), degenerate, open, inward, volume);
        }

        test_int(degenerate, 0);
        test_int(open, 0);
        test_int(inward, 0);
        test_assert(volume > 0.0);
        ecs_map_fini(&edges);
        ecs_os_free(sorted);
        ecs_os_free(weld);
    }

    ecs_fini(world);
}

void Watertight_cylinder(void) {
    // renderer/sphere_gaps
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    ecs_entity_t entities[512];
    int32_t count = 0;
    char name[96];
    int32_t segments[14] = { 3, 4, 5, 6, 7, 8, 9, 10, 12, 16, 20, 24, 32, 64 };
    for (int32_t s = 0; s < 14; s ++) {
        for (int32_t smooth = 0; smooth < 2; smooth ++) {
            snprintf(name, sizeof(name), "cylinder segments %d smooth %d", segments[s], smooth);
            ecs_entity_t e = ecs_entity(world, { .name = name });
            ecs_set(world, e, Cylinder,
                { .segments = segments[s], .smooth = smooth, .radius = 0.12f, .length = 2 });
            entities[count ++] = e;
        }
    }

    for (int32_t m = 0; m < count; m ++) {
        const ShapeMesh *sm = ecs_get(world, entities[m], ShapeMesh);
        test_assert(sm != NULL);
        const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
        test_assert(mesh != NULL);
        test_assert(mesh->index_count > 0);
        test_int(mesh->index_count % 3, 0);
        int32_t n = mesh->vertex_count;
        watertight_vertex_t *sorted = ecs_os_malloc_n(watertight_vertex_t, n);
        int32_t *weld = ecs_os_malloc_n(int32_t, n);
        for (int32_t i = 0; i < n; i ++) {
            glm_vec3_copy(mesh->vertices[i].position, sorted[i].p);
            sorted[i].index = i;
        }

        qsort(sorted, (size_t)n, sizeof(watertight_vertex_t), watertight_compare);
        for (int32_t i = 0; i < n; i ++) {
            weld[sorted[i].index] = sorted[i].index;
            for (int32_t j = i - 1; j >= 0 && sorted[i].p[0] - sorted[j].p[0] < 1e-4f; j --) {
                if (glm_vec3_distance(sorted[i].p, sorted[j].p) < 1e-4f) {
                    weld[sorted[i].index] = weld[sorted[j].index];
                    break;
                }
            }
        }

        ecs_map_t edges;
        ecs_map_init(&edges, NULL);
        int32_t degenerate = 0, open = 0, inward = 0;
        double volume = 0.0;
        vec3 center;
        glm_vec3_center((float*)mesh->min, (float*)mesh->max, center);
        for (int32_t t = 0; t < mesh->index_count; t += 3) {
            uint64_t v[3];
            for (int32_t k = 0; k < 3; k ++) {
                v[k] = (uint64_t)weld[mesh->indices[t + k]];
            }

            if (v[0] == v[1] || v[1] == v[2] || v[2] == v[0]) {
                degenerate ++;
            }

            for (int32_t k = 0; k < 3; k ++) {
                ecs_map_val_t *c = ecs_map_ensure(&edges, (v[k] << 32) | v[(k + 1) % 3]);
                (*c) ++;
            }

            const float *a = mesh->vertices[mesh->indices[t]].position;
            const float *b = mesh->vertices[mesh->indices[t + 1]].position;
            const float *c = mesh->vertices[mesh->indices[t + 2]].position;
            vec3 ab, ac, face, mid, out;
            glm_vec3_sub((float*)b, (float*)a, ab);
            glm_vec3_sub((float*)c, (float*)a, ac);
            glm_vec3_cross(ab, ac, face);
            volume += (double)glm_vec3_dot((float*)a, face) / 6.0;
            glm_vec3_add((float*)a, (float*)b, mid);
            glm_vec3_add(mid, (float*)c, mid);
            glm_vec3_scale(mid, 1.0f / 3.0f, mid);
            glm_vec3_sub(mid, center, out);
            if (glm_vec3_dot(face, out) <= 0.0f) {
                inward ++;
            }
        }

        for (int32_t t = 0; t < mesh->index_count; t += 3) {
            for (int32_t k = 0; k < 3; k ++) {
                uint64_t a = (uint64_t)weld[mesh->indices[t + k]];
                uint64_t b = (uint64_t)weld[mesh->indices[t + (k + 1) % 3]];
                ecs_map_val_t *fwd = ecs_map_get(&edges, (a << 32) | b);
                ecs_map_val_t *back = ecs_map_get(&edges, (b << 32) | a);
                if (!fwd || *fwd != 1 || !back || *back != 1) {
                    open ++;
                }
            }
        }

        if (degenerate || open || inward || volume <= 0.0) {
            printf("%s: degenerate %d, open edges %d, inward faces %d, volume %f\n",
                ecs_get_name(world, entities[m]), degenerate, open, inward, volume);
        }

        test_int(degenerate, 0);
        test_int(open, 0);
        test_int(inward, 0);
        test_assert(volume > 0.0);
        ecs_map_fini(&edges);
        ecs_os_free(sorted);
        ecs_os_free(weld);
    }

    ecs_fini(world);
}

void Watertight_cone(void) {
    // renderer/sphere_gaps
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    ecs_entity_t entities[512];
    int32_t count = 0;
    char name[96];
    int32_t segments[14] = { 3, 4, 5, 6, 7, 8, 9, 10, 12, 16, 20, 24, 32, 64 };
    for (int32_t s = 0; s < 14; s ++) {
        for (int32_t smooth = 0; smooth < 2; smooth ++) {
            snprintf(name, sizeof(name), "cone segments %d smooth %d", segments[s], smooth);
            ecs_entity_t e = ecs_entity(world, { .name = name });
            ecs_set(world, e, Cone,
                { .segments = segments[s], .smooth = smooth, .radius = 0.55f, .length = 1 });
            entities[count ++] = e;
        }
    }

    for (int32_t m = 0; m < count; m ++) {
        const ShapeMesh *sm = ecs_get(world, entities[m], ShapeMesh);
        test_assert(sm != NULL);
        const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
        test_assert(mesh != NULL);
        test_assert(mesh->index_count > 0);
        test_int(mesh->index_count % 3, 0);
        int32_t n = mesh->vertex_count;
        watertight_vertex_t *sorted = ecs_os_malloc_n(watertight_vertex_t, n);
        int32_t *weld = ecs_os_malloc_n(int32_t, n);
        for (int32_t i = 0; i < n; i ++) {
            glm_vec3_copy(mesh->vertices[i].position, sorted[i].p);
            sorted[i].index = i;
        }

        qsort(sorted, (size_t)n, sizeof(watertight_vertex_t), watertight_compare);
        for (int32_t i = 0; i < n; i ++) {
            weld[sorted[i].index] = sorted[i].index;
            for (int32_t j = i - 1; j >= 0 && sorted[i].p[0] - sorted[j].p[0] < 1e-4f; j --) {
                if (glm_vec3_distance(sorted[i].p, sorted[j].p) < 1e-4f) {
                    weld[sorted[i].index] = weld[sorted[j].index];
                    break;
                }
            }
        }

        ecs_map_t edges;
        ecs_map_init(&edges, NULL);
        int32_t degenerate = 0, open = 0, inward = 0;
        double volume = 0.0;
        vec3 center;
        glm_vec3_center((float*)mesh->min, (float*)mesh->max, center);
        for (int32_t t = 0; t < mesh->index_count; t += 3) {
            uint64_t v[3];
            for (int32_t k = 0; k < 3; k ++) {
                v[k] = (uint64_t)weld[mesh->indices[t + k]];
            }

            if (v[0] == v[1] || v[1] == v[2] || v[2] == v[0]) {
                degenerate ++;
            }

            for (int32_t k = 0; k < 3; k ++) {
                ecs_map_val_t *c = ecs_map_ensure(&edges, (v[k] << 32) | v[(k + 1) % 3]);
                (*c) ++;
            }

            const float *a = mesh->vertices[mesh->indices[t]].position;
            const float *b = mesh->vertices[mesh->indices[t + 1]].position;
            const float *c = mesh->vertices[mesh->indices[t + 2]].position;
            vec3 ab, ac, face, mid, out;
            glm_vec3_sub((float*)b, (float*)a, ab);
            glm_vec3_sub((float*)c, (float*)a, ac);
            glm_vec3_cross(ab, ac, face);
            volume += (double)glm_vec3_dot((float*)a, face) / 6.0;
            glm_vec3_add((float*)a, (float*)b, mid);
            glm_vec3_add(mid, (float*)c, mid);
            glm_vec3_scale(mid, 1.0f / 3.0f, mid);
            glm_vec3_sub(mid, center, out);
            if (glm_vec3_dot(face, out) <= 0.0f) {
                inward ++;
            }
        }

        for (int32_t t = 0; t < mesh->index_count; t += 3) {
            for (int32_t k = 0; k < 3; k ++) {
                uint64_t a = (uint64_t)weld[mesh->indices[t + k]];
                uint64_t b = (uint64_t)weld[mesh->indices[t + (k + 1) % 3]];
                ecs_map_val_t *fwd = ecs_map_get(&edges, (a << 32) | b);
                ecs_map_val_t *back = ecs_map_get(&edges, (b << 32) | a);
                if (!fwd || *fwd != 1 || !back || *back != 1) {
                    open ++;
                }
            }
        }

        if (degenerate || open || inward || volume <= 0.0) {
            printf("%s: degenerate %d, open edges %d, inward faces %d, volume %f\n",
                ecs_get_name(world, entities[m]), degenerate, open, inward, volume);
        }

        test_int(degenerate, 0);
        test_int(open, 0);
        test_int(inward, 0);
        test_assert(volume > 0.0);
        ecs_map_fini(&edges);
        ecs_os_free(sorted);
        ecs_os_free(weld);
    }

    ecs_fini(world);
}

void Watertight_capsule(void) {
    // renderer/sphere_gaps
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    ecs_entity_t entities[512];
    int32_t count = 0;
    char name[96];
    int32_t segments[14] = { 3, 4, 5, 6, 7, 8, 9, 10, 12, 16, 20, 24, 32, 64 };
    float lengths[3] = { 0.0f, 0.5f, 3.0f };
    for (int32_t s = 0; s < 14; s ++) {
        for (int32_t l = 0; l < 3; l ++) {
            for (int32_t smooth = 0; smooth < 2; smooth ++) {
                snprintf(name, sizeof(name), "capsule segments %d length %d smooth %d",
                    segments[s], (int)(lengths[l] * 100), smooth);
                ecs_entity_t e = ecs_entity(world, { .name = name });
                ecs_set(world, e, Capsule, { .segments = segments[s], .smooth = smooth,
                    .radius = 0.5f, .length = lengths[l] });
                entities[count ++] = e;
            }
        }
    }

    for (int32_t m = 0; m < count; m ++) {
        const ShapeMesh *sm = ecs_get(world, entities[m], ShapeMesh);
        test_assert(sm != NULL);
        const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
        test_assert(mesh != NULL);
        test_assert(mesh->index_count > 0);
        test_int(mesh->index_count % 3, 0);
        int32_t n = mesh->vertex_count;
        watertight_vertex_t *sorted = ecs_os_malloc_n(watertight_vertex_t, n);
        int32_t *weld = ecs_os_malloc_n(int32_t, n);
        for (int32_t i = 0; i < n; i ++) {
            glm_vec3_copy(mesh->vertices[i].position, sorted[i].p);
            sorted[i].index = i;
        }

        qsort(sorted, (size_t)n, sizeof(watertight_vertex_t), watertight_compare);
        for (int32_t i = 0; i < n; i ++) {
            weld[sorted[i].index] = sorted[i].index;
            for (int32_t j = i - 1; j >= 0 && sorted[i].p[0] - sorted[j].p[0] < 1e-4f; j --) {
                if (glm_vec3_distance(sorted[i].p, sorted[j].p) < 1e-4f) {
                    weld[sorted[i].index] = weld[sorted[j].index];
                    break;
                }
            }
        }

        ecs_map_t edges;
        ecs_map_init(&edges, NULL);
        int32_t degenerate = 0, open = 0, inward = 0;
        double volume = 0.0;
        vec3 center;
        glm_vec3_center((float*)mesh->min, (float*)mesh->max, center);
        for (int32_t t = 0; t < mesh->index_count; t += 3) {
            uint64_t v[3];
            for (int32_t k = 0; k < 3; k ++) {
                v[k] = (uint64_t)weld[mesh->indices[t + k]];
            }

            if (v[0] == v[1] || v[1] == v[2] || v[2] == v[0]) {
                degenerate ++;
            }

            for (int32_t k = 0; k < 3; k ++) {
                ecs_map_val_t *c = ecs_map_ensure(&edges, (v[k] << 32) | v[(k + 1) % 3]);
                (*c) ++;
            }

            const float *a = mesh->vertices[mesh->indices[t]].position;
            const float *b = mesh->vertices[mesh->indices[t + 1]].position;
            const float *c = mesh->vertices[mesh->indices[t + 2]].position;
            vec3 ab, ac, face, mid, out;
            glm_vec3_sub((float*)b, (float*)a, ab);
            glm_vec3_sub((float*)c, (float*)a, ac);
            glm_vec3_cross(ab, ac, face);
            volume += (double)glm_vec3_dot((float*)a, face) / 6.0;
            glm_vec3_add((float*)a, (float*)b, mid);
            glm_vec3_add(mid, (float*)c, mid);
            glm_vec3_scale(mid, 1.0f / 3.0f, mid);
            glm_vec3_sub(mid, center, out);
            if (glm_vec3_dot(face, out) <= 0.0f) {
                inward ++;
            }
        }

        for (int32_t t = 0; t < mesh->index_count; t += 3) {
            for (int32_t k = 0; k < 3; k ++) {
                uint64_t a = (uint64_t)weld[mesh->indices[t + k]];
                uint64_t b = (uint64_t)weld[mesh->indices[t + (k + 1) % 3]];
                ecs_map_val_t *fwd = ecs_map_get(&edges, (a << 32) | b);
                ecs_map_val_t *back = ecs_map_get(&edges, (b << 32) | a);
                if (!fwd || *fwd != 1 || !back || *back != 1) {
                    open ++;
                }
            }
        }

        if (degenerate || open || inward || volume <= 0.0) {
            printf("%s: degenerate %d, open edges %d, inward faces %d, volume %f\n",
                ecs_get_name(world, entities[m]), degenerate, open, inward, volume);
        }

        test_int(degenerate, 0);
        test_int(open, 0);
        test_int(inward, 0);
        test_assert(volume > 0.0);
        ecs_map_fini(&edges);
        ecs_os_free(sorted);
        ecs_os_free(weld);
    }

    ecs_fini(world);
}

void Watertight_torus(void) {
    // renderer/sphere_gaps
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    ecs_entity_t entities[512];
    int32_t count = 0;
    char name[96];
    int32_t segments[6] = { 3, 8, 12, 16, 20, 32 };
    int32_t rings[5] = { 3, 4, 6, 12, 24 };
    for (int32_t s = 0; s < 6; s ++) {
        for (int32_t r = 0; r < 5; r ++) {
            for (int32_t smooth = 0; smooth < 2; smooth ++) {
                snprintf(name, sizeof(name), "torus segments %d rings %d smooth %d",
                    segments[s], rings[r], smooth);
                ecs_entity_t e = ecs_entity(world, { .name = name });
                ecs_set(world, e, Torus, { .segments = segments[s], .rings = rings[r],
                    .smooth = smooth, .radius = 0.2f, .tube = 0.05f });
                entities[count ++] = e;
            }
        }
    }

    for (int32_t m = 0; m < count; m ++) {
        const ShapeMesh *sm = ecs_get(world, entities[m], ShapeMesh);
        test_assert(sm != NULL);
        const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
        test_assert(mesh != NULL);
        test_assert(mesh->index_count > 0);
        const Torus *torus = ecs_get(world, entities[m], Torus);
        test_assert(torus != NULL);
        test_int(mesh->index_count % 3, 0);
        int32_t n = mesh->vertex_count;
        watertight_vertex_t *sorted = ecs_os_malloc_n(watertight_vertex_t, n);
        int32_t *weld = ecs_os_malloc_n(int32_t, n);
        for (int32_t i = 0; i < n; i ++) {
            glm_vec3_copy(mesh->vertices[i].position, sorted[i].p);
            sorted[i].index = i;
        }

        qsort(sorted, (size_t)n, sizeof(watertight_vertex_t), watertight_compare);
        for (int32_t i = 0; i < n; i ++) {
            weld[sorted[i].index] = sorted[i].index;
            for (int32_t j = i - 1; j >= 0 && sorted[i].p[0] - sorted[j].p[0] < 1e-4f; j --) {
                if (glm_vec3_distance(sorted[i].p, sorted[j].p) < 1e-4f) {
                    weld[sorted[i].index] = weld[sorted[j].index];
                    break;
                }
            }
        }

        ecs_map_t edges;
        ecs_map_init(&edges, NULL);
        int32_t degenerate = 0, open = 0, inward = 0;
        double volume = 0.0;
        vec3 center;
        glm_vec3_center((float*)mesh->min, (float*)mesh->max, center);
        for (int32_t t = 0; t < mesh->index_count; t += 3) {
            uint64_t v[3];
            for (int32_t k = 0; k < 3; k ++) {
                v[k] = (uint64_t)weld[mesh->indices[t + k]];
            }

            if (v[0] == v[1] || v[1] == v[2] || v[2] == v[0]) {
                degenerate ++;
            }

            for (int32_t k = 0; k < 3; k ++) {
                ecs_map_val_t *c = ecs_map_ensure(&edges, (v[k] << 32) | v[(k + 1) % 3]);
                (*c) ++;
            }

            const float *a = mesh->vertices[mesh->indices[t]].position;
            const float *b = mesh->vertices[mesh->indices[t + 1]].position;
            const float *c = mesh->vertices[mesh->indices[t + 2]].position;
            vec3 ab, ac, face, mid, out;
            glm_vec3_sub((float*)b, (float*)a, ab);
            glm_vec3_sub((float*)c, (float*)a, ac);
            glm_vec3_cross(ab, ac, face);
            volume += (double)glm_vec3_dot((float*)a, face) / 6.0;
            glm_vec3_add((float*)a, (float*)b, mid);
            glm_vec3_add(mid, (float*)c, mid);
            glm_vec3_scale(mid, 1.0f / 3.0f, mid);
            float step = 2.0f * GLM_PIf / (float)torus->segments;
            float phi = atan2f(mid[2], mid[0]);
            phi = phi < 0.0f ? phi + 2.0f * GLM_PIf : phi;
            float chord = cosf(0.5f * step) / cosf(phi - (floorf(phi / step) + 0.5f) * step);
            vec3 ring = { chord * cosf(phi), 0.0f, chord * sinf(phi) };
            glm_vec3_sub(mid, ring, out);
            if (glm_vec3_dot(face, out) <= 0.0f) {
                inward ++;
            }
        }

        for (int32_t t = 0; t < mesh->index_count; t += 3) {
            for (int32_t k = 0; k < 3; k ++) {
                uint64_t a = (uint64_t)weld[mesh->indices[t + k]];
                uint64_t b = (uint64_t)weld[mesh->indices[t + (k + 1) % 3]];
                ecs_map_val_t *fwd = ecs_map_get(&edges, (a << 32) | b);
                ecs_map_val_t *back = ecs_map_get(&edges, (b << 32) | a);
                if (!fwd || *fwd != 1 || !back || *back != 1) {
                    open ++;
                }
            }
        }

        if (degenerate || open || inward || volume <= 0.0) {
            printf("%s: degenerate %d, open edges %d, inward faces %d, volume %f\n",
                ecs_get_name(world, entities[m]), degenerate, open, inward, volume);
        }

        test_int(degenerate, 0);
        test_int(open, 0);
        test_int(inward, 0);
        test_assert(volume > 0.0);
        ecs_map_fini(&edges);
        ecs_os_free(sorted);
        ecs_os_free(weld);
    }

    ecs_fini(world);
}

void Watertight_tube(void) {
    // renderer/sphere_gaps
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    ecs_entity_t entities[512];
    int32_t count = 0;
    char name[96];
    int32_t segments[14] = { 3, 4, 5, 6, 7, 8, 9, 10, 12, 16, 20, 24, 32, 64 };
    float thickness[3] = { 0.2f, 0.5f, 1.0f };
    float arcs[3] = { 0.0f, 1.5f, 4.0f };
    for (int32_t s = 0; s < 14; s ++) {
        for (int32_t w = 0; w < 3; w ++) {
            for (int32_t a = 0; a < 3; a ++) {
                for (int32_t smooth = 0; smooth < 2; smooth ++) {
                    snprintf(name, sizeof(name), "tube segments %d thickness %d arc %d smooth %d",
                        segments[s], (int)(thickness[w] * 100), (int)(arcs[a] * 100), smooth);
                    ecs_entity_t e = ecs_entity(world, { .name = name });
                    ecs_set(world, e, Tube, { .segments = segments[s], .smooth = smooth,
                        .radius = 1, .length = 1, .thickness = thickness[w], .arc = arcs[a] });
                    entities[count ++] = e;
                }
            }
        }
    }

    for (int32_t m = 0; m < count; m ++) {
        const ShapeMesh *sm = ecs_get(world, entities[m], ShapeMesh);
        test_assert(sm != NULL);
        const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
        test_assert(mesh != NULL);
        test_assert(mesh->index_count > 0);
        test_int(mesh->index_count % 3, 0);
        int32_t n = mesh->vertex_count;
        watertight_vertex_t *sorted = ecs_os_malloc_n(watertight_vertex_t, n);
        int32_t *weld = ecs_os_malloc_n(int32_t, n);
        for (int32_t i = 0; i < n; i ++) {
            glm_vec3_copy(mesh->vertices[i].position, sorted[i].p);
            sorted[i].index = i;
        }

        qsort(sorted, (size_t)n, sizeof(watertight_vertex_t), watertight_compare);
        for (int32_t i = 0; i < n; i ++) {
            weld[sorted[i].index] = sorted[i].index;
            for (int32_t j = i - 1; j >= 0 && sorted[i].p[0] - sorted[j].p[0] < 1e-4f; j --) {
                if (glm_vec3_distance(sorted[i].p, sorted[j].p) < 1e-4f) {
                    weld[sorted[i].index] = weld[sorted[j].index];
                    break;
                }
            }
        }

        ecs_map_t edges;
        ecs_map_init(&edges, NULL);
        int32_t degenerate = 0, open = 0, inward = 0;
        double volume = 0.0;
        vec3 center;
        glm_vec3_center((float*)mesh->min, (float*)mesh->max, center);
        for (int32_t t = 0; t < mesh->index_count; t += 3) {
            uint64_t v[3];
            for (int32_t k = 0; k < 3; k ++) {
                v[k] = (uint64_t)weld[mesh->indices[t + k]];
            }

            if (v[0] == v[1] || v[1] == v[2] || v[2] == v[0]) {
                degenerate ++;
            }

            for (int32_t k = 0; k < 3; k ++) {
                ecs_map_val_t *c = ecs_map_ensure(&edges, (v[k] << 32) | v[(k + 1) % 3]);
                (*c) ++;
            }

            const float *a = mesh->vertices[mesh->indices[t]].position;
            const float *b = mesh->vertices[mesh->indices[t + 1]].position;
            const float *c = mesh->vertices[mesh->indices[t + 2]].position;
            vec3 ab, ac, face, mid, out;
            glm_vec3_sub((float*)b, (float*)a, ab);
            glm_vec3_sub((float*)c, (float*)a, ac);
            glm_vec3_cross(ab, ac, face);
            volume += (double)glm_vec3_dot((float*)a, face) / 6.0;
            glm_vec3_add((float*)a, (float*)b, mid);
            glm_vec3_add(mid, (float*)c, mid);
            glm_vec3_scale(mid, 1.0f / 3.0f, mid);
            (void)out;
            (void)center;
        }

        for (int32_t t = 0; t < mesh->index_count; t += 3) {
            for (int32_t k = 0; k < 3; k ++) {
                uint64_t a = (uint64_t)weld[mesh->indices[t + k]];
                uint64_t b = (uint64_t)weld[mesh->indices[t + (k + 1) % 3]];
                ecs_map_val_t *fwd = ecs_map_get(&edges, (a << 32) | b);
                ecs_map_val_t *back = ecs_map_get(&edges, (b << 32) | a);
                if (!fwd || *fwd != 1 || !back || *back != 1) {
                    open ++;
                }
            }
        }

        if (degenerate || open || inward || volume <= 0.0) {
            printf("%s: degenerate %d, open edges %d, inward faces %d, volume %f\n",
                ecs_get_name(world, entities[m]), degenerate, open, inward, volume);
        }

        test_int(degenerate, 0);
        test_int(open, 0);
        test_int(inward, 0);
        test_assert(volume > 0.0);
        ecs_map_fini(&edges);
        ecs_os_free(sorted);
        ecs_os_free(weld);
    }

    ecs_fini(world);
}

void Watertight_frustum(void) {
    // renderer/sphere_gaps
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    ecs_entity_t entities[512];
    int32_t count = 0;
    char name[96];
    int32_t segments[14] = { 3, 4, 5, 6, 7, 8, 9, 10, 12, 16, 20, 24, 32, 64 };
    float radii[4][2] = { { 1.0f, 0.5f }, { 0.5f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f } };
    for (int32_t s = 0; s < 14; s ++) {
        for (int32_t r = 0; r < 4; r ++) {
            for (int32_t smooth = 0; smooth < 2; smooth ++) {
                snprintf(name, sizeof(name), "frustum segments %d radii %d %d smooth %d",
                    segments[s], (int)(radii[r][0] * 100), (int)(radii[r][1] * 100), smooth);
                ecs_entity_t e = ecs_entity(world, { .name = name });
                ecs_set(world, e, Frustum, { .segments = segments[s], .smooth = smooth,
                    .length = 1, .radius_bottom = radii[r][0], .radius_top = radii[r][1] });
                entities[count ++] = e;
            }
        }
    }

    for (int32_t m = 0; m < count; m ++) {
        const ShapeMesh *sm = ecs_get(world, entities[m], ShapeMesh);
        test_assert(sm != NULL);
        const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
        test_assert(mesh != NULL);
        test_assert(mesh->index_count > 0);
        test_int(mesh->index_count % 3, 0);
        int32_t n = mesh->vertex_count;
        watertight_vertex_t *sorted = ecs_os_malloc_n(watertight_vertex_t, n);
        int32_t *weld = ecs_os_malloc_n(int32_t, n);
        for (int32_t i = 0; i < n; i ++) {
            glm_vec3_copy(mesh->vertices[i].position, sorted[i].p);
            sorted[i].index = i;
        }

        qsort(sorted, (size_t)n, sizeof(watertight_vertex_t), watertight_compare);
        for (int32_t i = 0; i < n; i ++) {
            weld[sorted[i].index] = sorted[i].index;
            for (int32_t j = i - 1; j >= 0 && sorted[i].p[0] - sorted[j].p[0] < 1e-4f; j --) {
                if (glm_vec3_distance(sorted[i].p, sorted[j].p) < 1e-4f) {
                    weld[sorted[i].index] = weld[sorted[j].index];
                    break;
                }
            }
        }

        ecs_map_t edges;
        ecs_map_init(&edges, NULL);
        int32_t degenerate = 0, open = 0, inward = 0;
        double volume = 0.0;
        vec3 center;
        glm_vec3_center((float*)mesh->min, (float*)mesh->max, center);
        for (int32_t t = 0; t < mesh->index_count; t += 3) {
            uint64_t v[3];
            for (int32_t k = 0; k < 3; k ++) {
                v[k] = (uint64_t)weld[mesh->indices[t + k]];
            }

            if (v[0] == v[1] || v[1] == v[2] || v[2] == v[0]) {
                degenerate ++;
            }

            for (int32_t k = 0; k < 3; k ++) {
                ecs_map_val_t *c = ecs_map_ensure(&edges, (v[k] << 32) | v[(k + 1) % 3]);
                (*c) ++;
            }

            const float *a = mesh->vertices[mesh->indices[t]].position;
            const float *b = mesh->vertices[mesh->indices[t + 1]].position;
            const float *c = mesh->vertices[mesh->indices[t + 2]].position;
            vec3 ab, ac, face, mid, out;
            glm_vec3_sub((float*)b, (float*)a, ab);
            glm_vec3_sub((float*)c, (float*)a, ac);
            glm_vec3_cross(ab, ac, face);
            volume += (double)glm_vec3_dot((float*)a, face) / 6.0;
            glm_vec3_add((float*)a, (float*)b, mid);
            glm_vec3_add(mid, (float*)c, mid);
            glm_vec3_scale(mid, 1.0f / 3.0f, mid);
            glm_vec3_sub(mid, center, out);
            if (glm_vec3_dot(face, out) <= 0.0f) {
                inward ++;
            }
        }

        for (int32_t t = 0; t < mesh->index_count; t += 3) {
            for (int32_t k = 0; k < 3; k ++) {
                uint64_t a = (uint64_t)weld[mesh->indices[t + k]];
                uint64_t b = (uint64_t)weld[mesh->indices[t + (k + 1) % 3]];
                ecs_map_val_t *fwd = ecs_map_get(&edges, (a << 32) | b);
                ecs_map_val_t *back = ecs_map_get(&edges, (b << 32) | a);
                if (!fwd || *fwd != 1 || !back || *back != 1) {
                    open ++;
                }
            }
        }

        if (degenerate || open || inward || volume <= 0.0) {
            printf("%s: degenerate %d, open edges %d, inward faces %d, volume %f\n",
                ecs_get_name(world, entities[m]), degenerate, open, inward, volume);
        }

        test_int(degenerate, 0);
        test_int(open, 0);
        test_int(inward, 0);
        test_assert(volume > 0.0);
        ecs_map_fini(&edges);
        ecs_os_free(sorted);
        ecs_os_free(weld);
    }

    ecs_fini(world);
}

void Watertight_rounded_box(void) {
    // renderer/sphere_gaps
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    ecs_entity_t entities[512];
    int32_t count = 0;
    char name[96];
    float sizes[3][3] = { { 1, 1, 1 }, { 2, 1, 0.5f }, { 0.4f, 3, 1 } };
    float radii[3] = { 0.05f, 0.1f, 0.2f };
    int32_t segments[6] = { 1, 2, 3, 4, 6, 8 };
    for (int32_t z = 0; z < 3; z ++) {
        for (int32_t r = 0; r < 3; r ++) {
            for (int32_t s = 0; s < 6; s ++) {
                snprintf(name, sizeof(name), "rounded box size %d radius %d segments %d",
                    z, (int)(radii[r] * 100), segments[s]);
                ecs_entity_t e = ecs_entity(world, { .name = name });
                ecs_set(world, e, RoundedBox, { .x = sizes[z][0], .y = sizes[z][1],
                    .z = sizes[z][2], .radius = radii[r], .segments = segments[s] });
                entities[count ++] = e;
            }
        }
    }

    for (int32_t m = 0; m < count; m ++) {
        const ShapeMesh *sm = ecs_get(world, entities[m], ShapeMesh);
        test_assert(sm != NULL);
        const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
        test_assert(mesh != NULL);
        test_assert(mesh->index_count > 0);
        test_int(mesh->index_count % 3, 0);
        int32_t n = mesh->vertex_count;
        watertight_vertex_t *sorted = ecs_os_malloc_n(watertight_vertex_t, n);
        int32_t *weld = ecs_os_malloc_n(int32_t, n);
        for (int32_t i = 0; i < n; i ++) {
            glm_vec3_copy(mesh->vertices[i].position, sorted[i].p);
            sorted[i].index = i;
        }

        qsort(sorted, (size_t)n, sizeof(watertight_vertex_t), watertight_compare);
        for (int32_t i = 0; i < n; i ++) {
            weld[sorted[i].index] = sorted[i].index;
            for (int32_t j = i - 1; j >= 0 && sorted[i].p[0] - sorted[j].p[0] < 1e-4f; j --) {
                if (glm_vec3_distance(sorted[i].p, sorted[j].p) < 1e-4f) {
                    weld[sorted[i].index] = weld[sorted[j].index];
                    break;
                }
            }
        }

        ecs_map_t edges;
        ecs_map_init(&edges, NULL);
        int32_t degenerate = 0, open = 0, inward = 0;
        double volume = 0.0;
        vec3 center;
        glm_vec3_center((float*)mesh->min, (float*)mesh->max, center);
        for (int32_t t = 0; t < mesh->index_count; t += 3) {
            uint64_t v[3];
            for (int32_t k = 0; k < 3; k ++) {
                v[k] = (uint64_t)weld[mesh->indices[t + k]];
            }

            if (v[0] == v[1] || v[1] == v[2] || v[2] == v[0]) {
                degenerate ++;
            }

            for (int32_t k = 0; k < 3; k ++) {
                ecs_map_val_t *c = ecs_map_ensure(&edges, (v[k] << 32) | v[(k + 1) % 3]);
                (*c) ++;
            }

            const float *a = mesh->vertices[mesh->indices[t]].position;
            const float *b = mesh->vertices[mesh->indices[t + 1]].position;
            const float *c = mesh->vertices[mesh->indices[t + 2]].position;
            vec3 ab, ac, face, mid, out;
            glm_vec3_sub((float*)b, (float*)a, ab);
            glm_vec3_sub((float*)c, (float*)a, ac);
            glm_vec3_cross(ab, ac, face);
            volume += (double)glm_vec3_dot((float*)a, face) / 6.0;
            glm_vec3_add((float*)a, (float*)b, mid);
            glm_vec3_add(mid, (float*)c, mid);
            glm_vec3_scale(mid, 1.0f / 3.0f, mid);
            glm_vec3_sub(mid, center, out);
            if (glm_vec3_dot(face, out) <= 0.0f) {
                inward ++;
            }
        }

        for (int32_t t = 0; t < mesh->index_count; t += 3) {
            for (int32_t k = 0; k < 3; k ++) {
                uint64_t a = (uint64_t)weld[mesh->indices[t + k]];
                uint64_t b = (uint64_t)weld[mesh->indices[t + (k + 1) % 3]];
                ecs_map_val_t *fwd = ecs_map_get(&edges, (a << 32) | b);
                ecs_map_val_t *back = ecs_map_get(&edges, (b << 32) | a);
                if (!fwd || *fwd != 1 || !back || *back != 1) {
                    open ++;
                }
            }
        }

        if (degenerate || open || inward || volume <= 0.0) {
            printf("%s: degenerate %d, open edges %d, inward faces %d, volume %f\n",
                ecs_get_name(world, entities[m]), degenerate, open, inward, volume);
        }

        test_int(degenerate, 0);
        test_int(open, 0);
        test_int(inward, 0);
        test_assert(volume > 0.0);
        ecs_map_fini(&edges);
        ecs_os_free(sorted);
        ecs_os_free(weld);
    }

    ecs_fini(world);
}

/** Check that the mesh of an entity is closed, without degenerate or inward triangles.
 *
 * @param world The world
 * @param e The entity
 * @param label Description of the shape printed when the check fails
 * @return True when the mesh is watertight
 */
static bool watertight_closed(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *label,
    bool outward);

static bool watertight_check(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *label)
{
    return watertight_closed(world, e, label, true);
}

/** Check that the mesh of an entity is closed, without degenerate triangles.
 *
 * @param world The world
 * @param e The entity
 * @param label Description of the shape printed when the check fails
 * @param outward True to also require every face to point away from the center of the bounds,
 *   which only holds for convex shapes
 * @return True when the mesh is watertight
 */
static bool watertight_closed(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *label,
    bool outward)
{
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    const box_mesh_t *mesh = sm ? box_mesh_cache_get(cache, sm->mesh) : NULL;
    bool result = false;
    if (!mesh || !mesh->index_count || mesh->index_count % 3) {
        printf("%s: no mesh\n", label);
        goto done;
    }

    int32_t n = mesh->vertex_count;
    watertight_vertex_t *sorted = ecs_os_malloc_n(watertight_vertex_t, n);
    int32_t *weld = ecs_os_malloc_n(int32_t, n);
    for (int32_t i = 0; i < n; i ++) {
        glm_vec3_copy(mesh->vertices[i].position, sorted[i].p);
        sorted[i].index = i;
    }

    qsort(sorted, (size_t)n, sizeof(watertight_vertex_t), watertight_compare);
    for (int32_t i = 0; i < n; i ++) {
        weld[sorted[i].index] = sorted[i].index;
        for (int32_t j = i - 1; j >= 0 && sorted[i].p[0] - sorted[j].p[0] < 1e-4f; j --) {
            if (glm_vec3_distance(sorted[i].p, sorted[j].p) < 1e-4f) {
                weld[sorted[i].index] = weld[sorted[j].index];
                break;
            }
        }
    }

    ecs_map_t edges;
    ecs_map_init(&edges, NULL);
    int32_t degenerate = 0, open = 0, inward = 0;
    double volume = 0.0;
    vec3 center;
    glm_vec3_center((float*)mesh->min, (float*)mesh->max, center);
    for (int32_t t = 0; t < mesh->index_count; t += 3) {
        uint64_t v[3];
        for (int32_t k = 0; k < 3; k ++) {
            v[k] = (uint64_t)weld[mesh->indices[t + k]];
        }

        if (v[0] == v[1] || v[1] == v[2] || v[2] == v[0]) {
            degenerate ++;
        }

        for (int32_t k = 0; k < 3; k ++) {
            ecs_map_val_t *c = ecs_map_ensure(&edges, (v[k] << 32) | v[(k + 1) % 3]);
            (*c) ++;
        }

        const float *a = mesh->vertices[mesh->indices[t]].position;
        const float *b = mesh->vertices[mesh->indices[t + 1]].position;
        const float *c = mesh->vertices[mesh->indices[t + 2]].position;
        vec3 ab, ac, face, mid, out;
        glm_vec3_sub((float*)b, (float*)a, ab);
        glm_vec3_sub((float*)c, (float*)a, ac);
        glm_vec3_cross(ab, ac, face);
        volume += (double)glm_vec3_dot((float*)a, face) / 6.0;
        glm_vec3_add((float*)a, (float*)b, mid);
        glm_vec3_add(mid, (float*)c, mid);
        glm_vec3_scale(mid, 1.0f / 3.0f, mid);
        glm_vec3_sub(mid, center, out);
        if (outward && glm_vec3_dot(face, out) <= 0.0f) {
            inward ++;
        }
    }

    for (int32_t t = 0; t < mesh->index_count; t += 3) {
        for (int32_t k = 0; k < 3; k ++) {
            uint64_t a = (uint64_t)weld[mesh->indices[t + k]];
            uint64_t b = (uint64_t)weld[mesh->indices[t + (k + 1) % 3]];
            ecs_map_val_t *fwd = ecs_map_get(&edges, (a << 32) | b);
            ecs_map_val_t *back = ecs_map_get(&edges, (b << 32) | a);
            if (!fwd || *fwd != 1 || !back || *back != 1) {
                open ++;
            }
        }
    }

    result = !degenerate && !open && !inward && volume > 0.0;
    if (!result) {
        printf("%s: degenerate %d, open edges %d, inward faces %d, volume %f\n",
            label, degenerate, open, inward, volume);
    }

    ecs_map_fini(&edges);
    ecs_os_free(sorted);
    ecs_os_free(weld);
done:
    return result;
}

/** Set the edge radii of a Bevel in the order of its members.
 *
 * @param bevel The bevel
 * @param radii The 12 radii
 */
static void watertight_bevel_edges(
    Bevel *bevel,
    const float *radii)
{
    bevel->top_front = radii[0];
    bevel->top_back = radii[1];
    bevel->top_left = radii[2];
    bevel->top_right = radii[3];
    bevel->bottom_front = radii[4];
    bevel->bottom_back = radii[5];
    bevel->bottom_left = radii[6];
    bevel->bottom_right = radii[7];
    bevel->front_left = radii[8];
    bevel->front_right = radii[9];
    bevel->back_left = radii[10];
    bevel->back_right = radii[11];
}

void Watertight_bevel(void) {
    // geometry/bevel
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    ecs_entity_t e = ecs_new(world);
    char label[160];
    int32_t failures = 0;

    static const float patterns[2][3] = { { 1, 0.8f, 1.2f }, { 2, 0.1f, 1 } };
    int32_t segments[2] = { 4, 6 };
    for (int32_t s = 0; s < 3; s ++) {
        ecs_set(world, e, Box, { patterns[s / 2][0], patterns[s / 2][1], patterns[s / 2][2] });
        for (int32_t pattern = 0; pattern < 4096; pattern ++) {
            Bevel bevel = { .segments = segments[s % 2] };
            float radii[12];
            for (int32_t edge = 0; edge < 12; edge ++) {
                radii[edge] = (pattern >> edge) & 1 ? 0.15f : 0.0f;
            }

            watertight_bevel_edges(&bevel, radii);
            ecs_set_ptr(world, e, Bevel, &bevel);
            snprintf(label, sizeof(label), "bevel pattern %03x segments %d size %d",
                pattern, segments[s % 2], s / 2);
            failures += !watertight_check(world, e, label);
        }
    }

    static const float choices[5] = { 0.0f, 0.05f, 0.1f, 0.2f, 0.3f };
    static const float sizes[5][3] = {
        { 1, 0.8f, 1.2f }, { 2, 1, 1.5f }, { 1.4f, 2, 0.9f }, { 2, 0.1f, 1 }, { 0.15f, 1.5f, 1 }
    };
    uint32_t seed = 12345;
    for (int32_t z = 0; z < 5; z ++) {
        ecs_set(world, e, Box, { sizes[z][0], sizes[z][1], sizes[z][2] });
        for (int32_t round = 0; round < 400; round ++) {
            Bevel bevel = { .segments = 2 + 2 * (round % 3) };
            float radii[12];
            for (int32_t edge = 0; edge < 12; edge ++) {
                seed = seed * 1664525u + 1013904223u;
                radii[edge] = choices[(seed >> 16) % 5];
            }

            watertight_bevel_edges(&bevel, radii);
            ecs_set_ptr(world, e, Bevel, &bevel);
            snprintf(label, sizeof(label), "bevel size %d round %d", z, round);
            failures += !watertight_check(world, e, label);
        }
    }

    static const float levels[3] = { 0.0f, 0.1f, 0.2f };
    ecs_set(world, e, Box, { 1, 0.8f, 1.2f });
    for (int32_t corner = 0; corner < 8; corner ++) {
        int32_t right = corner & 1, top = (corner >> 1) & 1, front = (corner >> 2) & 1;
        int32_t x_edge = top ? (front ? 0 : 1) : (front ? 4 : 5);
        int32_t y_edge = front ? (right ? 9 : 8) : (right ? 11 : 10);
        int32_t z_edge = top ? (right ? 3 : 2) : (right ? 7 : 6);
        for (int32_t mix = 0; mix < 27; mix ++) {
            for (int32_t s = 1; s <= 2; s ++) {
                Bevel bevel = { .segments = 2 * s };
                float radii[12];
                for (int32_t edge = 0; edge < 12; edge ++) {
                    radii[edge] = 0.05f;
                }

                radii[x_edge] = levels[mix % 3];
                radii[y_edge] = levels[(mix / 3) % 3];
                radii[z_edge] = levels[mix / 9];
                watertight_bevel_edges(&bevel, radii);
                ecs_set_ptr(world, e, Bevel, &bevel);
                snprintf(label, sizeof(label), "bevel corner %d mix %d segments %d",
                    corner, mix, bevel.segments);
                failures += !watertight_check(world, e, label);
            }
        }
    }

    static const float large[4] = { 0.0f, 0.4f, 0.8f, 1.2f };
    ecs_set(world, e, Box, { 1, 0.6f, 0.8f });
    for (int32_t round = 0; round < 400; round ++) {
        Bevel bevel = { .segments = 4 };
        float radii[12];
        for (int32_t edge = 0; edge < 12; edge ++) {
            seed = seed * 1664525u + 1013904223u;
            radii[edge] = large[(seed >> 16) % 4];
        }

        watertight_bevel_edges(&bevel, radii);
        ecs_set_ptr(world, e, Bevel, &bevel);
        snprintf(label, sizeof(label), "bevel scaled round %d", round);
        failures += !watertight_check(world, e, label);
    }

    Bevel half = { .top_front = 0.6f, .top_back = 0.6f, .segments = 4 };
    ecs_set(world, e, Box, { 1, 1, 1.2f });
    ecs_set_ptr(world, e, Bevel, &half);
    failures += !watertight_check(world, e, "bevel half cylinder");

    test_int(failures, 0);
    ecs_fini(world);
}

void Watertight_profile_extrusion(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    test_int(ecs_script_run(world, "arch",
        "using box.geometry\n"
        "arch_outline { Path2D: {start: {-0.72, 0.16}, segments: ["
        "{kind: Line, end: {0.72, 0.16}}, {kind: Line, end: {0.72, 1.5}},"
        "{kind: CubicBezier, end: {0, 2.22}, control1: {0.72, 1.898}, control2: {0.398, 2.22}},"
        "{kind: CubicBezier, end: {-0.72, 1.5}, control1: {-0.398, 2.22},"
        " control2: {-0.72, 1.898}}], closed: true}}\n"
        "arch { Profile2D: {outer: arch_outline}}\n"
        "round_outline { Path2D: {start: {1, 0}, segments: ["
        "{kind: CubicBezier, end: {0, 1}, control1: {1, 0.5523}, control2: {0.5523, 1}},"
        "{kind: CubicBezier, end: {-1, 0}, control1: {-0.5523, 1}, control2: {-1, 0.5523}},"
        "{kind: CubicBezier, end: {0, -1}, control1: {-1, -0.5523}, control2: {-0.5523, -1}},"
        "{kind: CubicBezier, end: {1, 0}, control1: {0.5523, -1}, control2: {1, -0.5523}}"
        "], closed: true}}\n"
        "round { Profile2D: {outer: round_outline}}\n", NULL), 0);
    const char *profiles[2] = { "arch", "round" };
    ecs_entity_t e = ecs_new(world);
    char label[96];
    int32_t failures = 0;
    for (int32_t p = 0; p < 2; p ++) {
        for (int32_t smooth = 0; smooth < 2; smooth ++) {
            ecs_set(world, e, ProfileExtrusion, { .profile = ecs_lookup(world, profiles[p]),
                .depth = 0.76f, .cap_start = true, .cap_end = true, .smooth = smooth });
            snprintf(label, sizeof(label), "extrusion %s smooth %d", profiles[p], smooth);
            failures += !watertight_check(world, e, label);
        }
    }

    test_int(failures, 0);
    ecs_fini(world);
}

/** Rim radii the bevel cases sweep: sharp, small and clamped */
static const float watertight_rims[3] = { 0.0f, 0.05f, 5.0f };

/** Sweep the top and bottom rims and two segment counts of a Bevel on an entity whose host is
 * set, checking that every mesh is closed.
 *
 * @param world The world
 * @param e The entity
 * @param name Description of the host
 * @param outward True to require every face to point away from the center of the bounds
 * @return The number of failures
 */
static int32_t watertight_rims_sweep(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *name,
    bool outward)
{
    static const int32_t steps[2] = { 1, 4 };
    char label[160];
    int32_t failures = 0;
    for (int32_t t = 0; t < 3; t ++) {
        for (int32_t b = 0; b < 3; b ++) {
            for (int32_t s = 0; s < 2; s ++) {
                ecs_set(world, e, Bevel, { .top = watertight_rims[t],
                    .bottom = watertight_rims[b], .segments = steps[s] });
                snprintf(label, sizeof(label), "%s top %g bottom %g segments %d",
                    name, (double)watertight_rims[t], (double)watertight_rims[b], steps[s]);
                failures += !watertight_closed(world, e, label, outward);
            }
        }
    }

    return failures;
}

void Watertight_bevel_cylinder(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    ecs_entity_t e = ecs_new(world);
    static const int32_t segments[2] = { 8, 24 };
    char name[96];
    int32_t failures = 0;
    for (int32_t s = 0; s < 2; s ++) {
        for (int32_t smooth = 0; smooth < 2; smooth ++) {
            ecs_set(world, e, Cylinder, { .segments = segments[s], .smooth = smooth,
                .length = 1, .radius = 0.5f });
            snprintf(name, sizeof(name), "bevel cylinder segments %d smooth %d",
                segments[s], smooth);
            failures += watertight_rims_sweep(world, e, name, true);
        }
    }

    test_int(failures, 0);
    ecs_fini(world);
}

void Watertight_bevel_frustum(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    ecs_entity_t e = ecs_new(world);
    static const float shapes[4][3] = {
        { 1, 0.5f, 0.3f }, { 1, 0.5f, 0 }, { 1, 0, 0.4f }, { 0.2f, 1, 0.1f }
    };
    static const int32_t segments[2] = { 8, 24 };
    char name[96];
    int32_t failures = 0;
    for (int32_t f = 0; f < 4; f ++) {
        for (int32_t s = 0; s < 2; s ++) {
            for (int32_t smooth = 0; smooth < 2; smooth ++) {
                ecs_set(world, e, Frustum, { .segments = segments[s], .smooth = smooth,
                    .length = shapes[f][0], .radius_bottom = shapes[f][1],
                    .radius_top = shapes[f][2] });
                snprintf(name, sizeof(name), "bevel frustum %d segments %d smooth %d",
                    f, segments[s], smooth);
                failures += watertight_rims_sweep(world, e, name, true);
            }
        }
    }

    test_int(failures, 0);
    ecs_fini(world);
}

void Watertight_bevel_cone(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    ecs_entity_t e = ecs_new(world);
    static const int32_t segments[2] = { 8, 24 };
    static const float lengths[2] = { 1, 0.2f };
    char name[96];
    int32_t failures = 0;
    for (int32_t l = 0; l < 2; l ++) {
        for (int32_t s = 0; s < 2; s ++) {
            for (int32_t smooth = 0; smooth < 2; smooth ++) {
                ecs_set(world, e, Cone, { .segments = segments[s], .smooth = smooth,
                    .length = lengths[l], .radius = 0.5f });
                snprintf(name, sizeof(name), "bevel cone length %g segments %d smooth %d",
                    (double)lengths[l], segments[s], smooth);
                failures += watertight_rims_sweep(world, e, name, true);
            }
        }
    }

    test_int(failures, 0);
    ecs_fini(world);
}

void Watertight_bevel_tube(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    ecs_entity_t e = ecs_new(world);
    static const float arcs[3] = { 0, 1.5707964f, 3 };
    static const float walls[3] = { 0.2f, 0.04f, 0.6f };
    static const int32_t segments[2] = { 8, 24 };
    char name[128];
    int32_t failures = 0;
    for (int32_t a = 0; a < 3; a ++) {
        for (int32_t w = 0; w < 3; w ++) {
            for (int32_t s = 0; s < 2; s ++) {
                for (int32_t smooth = 0; smooth < 2; smooth ++) {
                    ecs_set(world, e, Tube, { .segments = segments[s], .smooth = smooth,
                        .length = 1, .radius = 0.5f, .thickness = walls[w], .arc = arcs[a] });
                    snprintf(name, sizeof(name),
                        "bevel tube arc %g wall %g segments %d smooth %d",
                        (double)arcs[a], (double)walls[w], segments[s], smooth);
                    failures += watertight_rims_sweep(world, e, name, false);
                }
            }
        }
    }

    test_int(failures, 0);
    ecs_fini(world);
}

void Watertight_bevel_prism(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    ecs_entity_t e = ecs_new(world);
    static const float sizes[3][3] = { { 1, 1, 1 }, { 2, 0.5f, 0.3f }, { 0.4f, 1.5f, 2 } };
    static const int32_t steps[2] = { 1, 4 };
    char label[128];
    int32_t failures = 0;
    for (int32_t right = 0; right < 2; right ++) {
        for (int32_t z = 0; z < 3; z ++) {
            for (int32_t r = 0; r < 3; r ++) {
                for (int32_t s = 0; s < 2; s ++) {
                    if (right) {
                        ecs_set(world, e, RightTrianglePrism,
                            { sizes[z][0], sizes[z][1], sizes[z][2] });
                    } else {
                        ecs_set(world, e, TrianglePrism,
                            { sizes[z][0], sizes[z][1], sizes[z][2] });
                    }

                    ecs_set(world, e, Bevel, { .radius = watertight_rims[r],
                        .segments = steps[s] });
                    snprintf(label, sizeof(label),
                        "bevel prism right %d size %d radius %g segments %d", right, z,
                        (double)watertight_rims[r], steps[s]);
                    failures += !watertight_closed(world, e, label, false);
                }
            }
        }

        ecs_remove(world, e, TrianglePrism);
        ecs_remove(world, e, RightTrianglePrism);
    }

    test_int(failures, 0);
    ecs_fini(world);
}

void Watertight_bevel_profile_extrusion(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    test_int(ecs_script_run(world, "profiles",
        "using box.geometry\n"
        "frame_outline { Path2D: {start: {-1, -0.5}, segments: ["
        "{kind: Line, end: {1, -0.5}}, {kind: Line, end: {1, 0.5}},"
        " {kind: Line, end: {-1, 0.5}}], closed: true}}\n"
        "frame_opening { Path2D: {start: {-0.3, -0.2}, segments: ["
        "{kind: Line, end: {0.3, -0.2}}, {kind: Line, end: {0.3, 0.2}},"
        " {kind: Line, end: {-0.3, 0.2}}], closed: true}}\n"
        "frame { Profile2D: {outer: frame_outline, holes: [frame_opening]}}\n"
        "l_outline { Path2D: {start: {0, 0}, segments: ["
        "{kind: Line, end: {2, 0}}, {kind: Line, end: {2, 1}}, {kind: Line, end: {1, 1}},"
        " {kind: Line, end: {1, 2}}, {kind: Line, end: {0, 2}}], closed: true}}\n"
        "l_shape { Profile2D: {outer: l_outline}}\n"
        "round_outline { Path2D: {start: {1, 0}, segments: ["
        "{kind: CubicBezier, end: {0, 1}, control1: {1, 0.5523}, control2: {0.5523, 1}},"
        "{kind: CubicBezier, end: {-1, 0}, control1: {-0.5523, 1}, control2: {-1, 0.5523}},"
        "{kind: CubicBezier, end: {0, -1}, control1: {-1, -0.5523}, control2: {-0.5523, -1}},"
        "{kind: CubicBezier, end: {1, 0}, control1: {0.5523, -1}, control2: {1, -0.5523}}"
        "], closed: true}}\n"
        "round_opening { Path2D: {start: {0.4, 0}, segments: ["
        "{kind: CubicBezier, end: {0, 0.4}, control1: {0.4, 0.2209}, control2: {0.2209, 0.4}},"
        "{kind: CubicBezier, end: {-0.4, 0}, control1: {-0.2209, 0.4},"
        " control2: {-0.4, 0.2209}},"
        "{kind: CubicBezier, end: {0, -0.4}, control1: {-0.4, -0.2209},"
        " control2: {-0.2209, -0.4}},"
        "{kind: CubicBezier, end: {0.4, 0}, control1: {0.2209, -0.4},"
        " control2: {0.4, -0.2209}}], closed: true}}\n"
        "ring { Profile2D: {outer: round_outline, holes: [round_opening]}}\n", NULL), 0);
    const char *profiles[3] = { "frame", "l_shape", "ring" };
    ecs_entity_t e = ecs_new(world);
    char name[96];
    int32_t failures = 0;
    for (int32_t p = 0; p < 3; p ++) {
        for (int32_t smooth = 0; smooth < 2; smooth ++) {
            ecs_set(world, e, ProfileExtrusion, { .profile = ecs_lookup(world, profiles[p]),
                .depth = 0.5f, .cap_start = true, .cap_end = true, .smooth = smooth });
            snprintf(name, sizeof(name), "bevel extrusion %s smooth %d", profiles[p], smooth);
            failures += watertight_rims_sweep(world, e, name, false);
        }
    }

    test_int(failures, 0);
    ecs_fini(world);
}
