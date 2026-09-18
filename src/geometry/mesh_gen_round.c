/**
 * @file geometry/mesh_gen_round.c
 * @brief Round shapes: sphere, hemisphere and icosphere.
 */

#include "mesh_gen_internal.h"

/** Golden ratio, used to place the base icosahedron vertices */
#define BOX_MESH_PHI (1.6180339887498949f)

/** Base icosahedron vertex positions, unnormalized */
static const float box_mesh_ico_verts[12][3] = {
    { -1.0f, BOX_MESH_PHI, 0.0f }, { 1.0f, BOX_MESH_PHI, 0.0f },
    { -1.0f, -BOX_MESH_PHI, 0.0f }, { 1.0f, -BOX_MESH_PHI, 0.0f },
    { 0.0f, -1.0f, BOX_MESH_PHI }, { 0.0f, 1.0f, BOX_MESH_PHI },
    { 0.0f, -1.0f, -BOX_MESH_PHI }, { 0.0f, 1.0f, -BOX_MESH_PHI },
    { BOX_MESH_PHI, 0.0f, -1.0f }, { BOX_MESH_PHI, 0.0f, 1.0f },
    { -BOX_MESH_PHI, 0.0f, -1.0f }, { -BOX_MESH_PHI, 0.0f, 1.0f }
};

/** Base icosahedron faces, indexing box_mesh_ico_verts, wound outward */
static const uint32_t box_mesh_ico_faces[20][3] = {
    { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 },
    { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
    { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 },
    { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 }
};

/** Compute the equirectangular UV of a point on the unit sphere.
 *
 * @param p The point
 * @param u Output U coordinate
 * @param v Output V coordinate
 */
static void box_mesh_ico_uv(
    vec3 p,
    float *u,
    float *v)
{
    *u = atan2f(p.z, p.x) / BOX_MESH_TAU + 0.5f;
    *v = asinf(p.y) / BOX_MESH_PI + 0.5f;
}

/** Fetch or create the vertex halfway between two existing vertices, on the unit sphere.
 *
 * @param b The builder
 * @param cache Map from an edge key to a midpoint vertex index plus one
 * @param ia First vertex index
 * @param ib Second vertex index
 * @return The midpoint vertex index
 */
static uint32_t box_mesh_ico_midpoint(
    box_mesh_builder_t *b,
    ecs_map_t *cache,
    uint32_t ia,
    uint32_t ib)
{
    uint64_t key = ia < ib
        ? (((uint64_t)ia << 32) | (uint64_t)ib)
        : (((uint64_t)ib << 32) | (uint64_t)ia);
    ecs_map_val_t *slot = ecs_map_ensure(cache, key);
    uint32_t result;

    if (*slot != 0) {
        result = (uint32_t)(*slot - 1);
    } else {
        vec3 pa = box_mesh_builder_position(b, ia);
        vec3 pb = box_mesh_builder_position(b, ib);
        vec3 mid = vec3_normalize(vec3_scale(vec3_add(pa, pb), 0.5f));
        float u, v;

        box_mesh_ico_uv(mid, &u, &v);
        result = box_mesh_builder_vertex(b, mid, mid, u, v);
        *slot = (ecs_map_val_t)(result + 1);
    }

    return result;
}

/** Split every face of an icosphere into four, on the unit sphere.
 *
 * @param b The builder
 * @param cache Edge midpoint cache
 * @param faces The current faces, three indices each, freed by this call
 * @param face_count In: current face count, out: the new face count
 * @return The new, four times larger face array
 */
static uint32_t* box_mesh_ico_subdivide(
    box_mesh_builder_t *b,
    ecs_map_t *cache,
    uint32_t *faces,
    int32_t *face_count)
{
    int32_t count = *face_count;
    uint32_t *next = ecs_os_malloc_n(uint32_t, (uint32_t)(count * 4 * 3));
    int32_t i;

    for (i = 0; i < count; i ++) {
        uint32_t a = faces[i * 3 + 0];
        uint32_t bi = faces[i * 3 + 1];
        uint32_t c = faces[i * 3 + 2];
        uint32_t ab = box_mesh_ico_midpoint(b, cache, a, bi);
        uint32_t bc = box_mesh_ico_midpoint(b, cache, bi, c);
        uint32_t ca = box_mesh_ico_midpoint(b, cache, c, a);
        uint32_t *dst = &next[i * 12];

        dst[0] = a;  dst[1] = ab; dst[2] = ca;
        dst[3] = ab; dst[4] = bi; dst[5] = bc;
        dst[6] = ca; dst[7] = bc; dst[8] = c;
        dst[9] = ab; dst[10] = bc; dst[11] = ca;
    }

    ecs_os_free(faces);
    *face_count = count * 4;

    return next;
}

void box_mesh_sphere(
    box_mesh_data_t *out,
    int32_t segments,
    bool smooth)
{
    int32_t n = box_mesh_clamp_min_i32(segments, 3);
    uint32_t vertex_count = (uint32_t)(2 * (n + 1) * (n + 1));
    uint32_t index_count = (uint32_t)(6 * n * (2 * n - 1));
    box_mesh_data_t shared;
    box_mesh_builder_t b;

    box_mesh_builder_init(&b, &shared, vertex_count, index_count);
    box_mesh_hemisphere_shell(&b, vec3_new(0.0f, 0.0f, 0.0f), 1.0f, n, true, 0.5f, 1.0f);
    box_mesh_hemisphere_shell(&b, vec3_new(0.0f, 0.0f, 0.0f), 1.0f, n, false, 0.5f, 0.0f);
    box_mesh_builder_finish(&b);

    if (smooth) {
        *out = shared;
    } else {
        box_mesh_facet(&shared, out);
        box_mesh_data_fini(&shared);
    }
}

void box_mesh_hemisphere(
    box_mesh_data_t *out,
    int32_t segments,
    bool smooth)
{
    int32_t n = box_mesh_clamp_min_i32(segments, 3);
    uint32_t vertex_count = (uint32_t)((n + 1) * (n + 1) + n + 1);
    uint32_t index_count = (uint32_t)(6 * n * n);
    box_mesh_data_t shared;
    box_mesh_builder_t b;

    box_mesh_builder_init(&b, &shared, vertex_count, index_count);
    box_mesh_hemisphere_shell(&b, vec3_new(0.0f, 0.0f, 0.0f), 1.0f, n, true, 0.0f, 1.0f);
    box_mesh_disc_cap(&b, 1.0f, 0.0f, -1.0f, n);
    box_mesh_builder_finish(&b);

    if (smooth) {
        *out = shared;
    } else {
        box_mesh_facet(&shared, out);
        box_mesh_data_fini(&shared);
    }
}

void box_mesh_icosphere(
    box_mesh_data_t *out,
    int32_t subdivisions,
    bool smooth)
{
    int32_t s = subdivisions < 0 ? 0 : (subdivisions > 5 ? 5 : subdivisions);
    int32_t pow4 = 1;
    int32_t face_count = 20;
    uint32_t *faces = ecs_os_malloc_n(uint32_t, (uint32_t)face_count * 3);
    box_mesh_data_t shared;
    box_mesh_builder_t b;
    ecs_map_t cache;
    int32_t i, level;
    uint32_t vertex_count, index_count;

    for (level = 0; level < s; level ++) {
        pow4 *= 4;
    }

    vertex_count = (uint32_t)(10 * pow4 + 2);
    index_count = (uint32_t)(60 * pow4);
    for (i = 0; i < face_count; i ++) {
        faces[i * 3 + 0] = box_mesh_ico_faces[i][0];
        faces[i * 3 + 1] = box_mesh_ico_faces[i][1];
        faces[i * 3 + 2] = box_mesh_ico_faces[i][2];
    }

    box_mesh_builder_init(&b, &shared, vertex_count, index_count);
    ecs_map_init(&cache, NULL);
    for (i = 0; i < 12; i ++) {
        vec3 p = vec3_normalize(vec3_new(
            box_mesh_ico_verts[i][0], box_mesh_ico_verts[i][1], box_mesh_ico_verts[i][2]));
        float u, v;

        box_mesh_ico_uv(p, &u, &v);
        box_mesh_builder_vertex(&b, p, p, u, v);
    }

    for (level = 0; level < s; level ++) {
        faces = box_mesh_ico_subdivide(&b, &cache, faces, &face_count);
    }

    for (i = 0; i < face_count; i ++) {
        box_mesh_builder_triangle(&b, faces[i * 3 + 0], faces[i * 3 + 1], faces[i * 3 + 2]);
    }

    box_mesh_builder_finish(&b);
    ecs_map_fini(&cache);
    ecs_os_free(faces);

    if (smooth) {
        *out = shared;
    } else {
        box_mesh_facet(&shared, out);
        box_mesh_data_fini(&shared);
    }
}
