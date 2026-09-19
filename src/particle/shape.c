/**
 * @file particle/shape.c
 * @brief Samples the spawn points of the mesh and spline shapes of an emitter.
 *
 * The other shapes are closed forms the spawn pass evaluates itself; a mesh and
 * a curve are not, so the module samples a table of points once and the packet
 * carries it. The points are taken in the space of the source, which the
 * transform of the emitter then places like any other spawn volume.
 */

#include "particle_internal.h"
#include "../spline/spline_internal.h"
#include <math.h>

/** Next value of the sampler, a xorshift that only depends on the seed.
 *
 * @param state The state of the sampler, never zero
 * @return A value from 0 up to but not including 1
 */
static float box_particle_random(
    uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;

    return (float)(x >> 8) * (1.0f / 16777216.0f);
}

/** Triangles a Mesh3 holds, indexed or in the order of its vertices.
 *
 * @param mesh The mesh
 * @return The triangle count
 */
static int32_t box_particle_triangles(
    const Mesh3 *mesh)
{
    int32_t indices = ecs_vec_count(&mesh->indices);
    int32_t result = indices ? indices / 3 : ecs_vec_count(&mesh->vertices) / 3;

    return result;
}

/** Corners of one triangle of a Mesh3.
 *
 * @param mesh The mesh
 * @param triangle Index of the triangle
 * @param out Receives its three corners
 */
static void box_particle_triangle(
    const Mesh3 *mesh,
    int32_t triangle,
    vec3 *out)
{
    int32_t corner;
    for (corner = 0; corner < 3; corner ++) {
        int32_t index = triangle * 3 + corner;
        if (ecs_vec_count(&mesh->indices)) {
            index = (int32_t)ecs_vec_get_t(&mesh->indices, uint32_t, index)[0];
        }

        out[corner] = ecs_vec_get_t(&mesh->vertices, vec3, index)[0];
    }
}

/** Sample points over the surface of the mesh of an emitter.
 *
 * A point is drawn on a triangle picked by its share of the surface, so a
 * stretched triangle receives its share of the particles.
 *
 * @param mesh The mesh
 * @param impl The bookkeeping receiving the table
 */
static void box_particle_mesh(
    const Mesh3 *mesh,
    ParticleEmitterImpl *impl)
{
    int32_t triangles = box_particle_triangles(mesh);
    if (triangles <= 0) {
        goto done;
    }

    float *area = ecs_os_malloc_n(float, triangles);
    float total = 0;
    int32_t i;
    for (i = 0; i < triangles; i ++) {
        vec3 corner[3];
        box_particle_triangle(mesh, i, corner);
        vec3 cross = vec3_cross(vec3_sub(corner[1], corner[0]),
            vec3_sub(corner[2], corner[0]));
        total += vec3_length(cross) * 0.5f;
        area[i] = total;
    }

    uint32_t state = impl->seed * 747796405u + 2891336453u;
    state |= 1u;
    for (i = 0; i < BOX_PARTICLE_POINTS; i ++) {
        float pick = box_particle_random(&state) * total;
        int32_t triangle = 0;
        while (triangle < triangles - 1 && area[triangle] < pick) {
            triangle ++;
        }

        vec3 corner[3];
        box_particle_triangle(mesh, triangle, corner);
        float u = box_particle_random(&state);
        float v = box_particle_random(&state);
        if (u + v > 1.0f) {
            u = 1.0f - u;
            v = 1.0f - v;
        }

        vec3 edge0 = vec3_sub(corner[1], corner[0]);
        vec3 edge1 = vec3_sub(corner[2], corner[0]);
        vec3 point = vec3_add(corner[0],
            vec3_add(vec3_scale(edge0, u), vec3_scale(edge1, v)));
        vec3 normal = vec3_cross(edge0, edge1);
        if (vec3_length(normal) > 0.0f) {
            normal = vec3_normalize(normal);
        } else {
            normal = vec3_new(0, 1, 0);
        }

        impl->table[i].position[0] = point.x;
        impl->table[i].position[1] = point.y;
        impl->table[i].position[2] = point.z;
        impl->table[i].normal[0] = normal.x;
        impl->table[i].normal[1] = normal.y;
        impl->table[i].normal[2] = normal.z;
    }

    ecs_os_free(area);
    impl->points = BOX_PARTICLE_POINTS;
done:
    return;
}

/** Sample points evenly along the curve of an emitter.
 *
 * @param world The world
 * @param spline The curve entity
 * @param impl The bookkeeping receiving the table
 */
static void box_particle_spline(
    ecs_world_t *world,
    ecs_entity_t spline,
    ParticleEmitterImpl *impl)
{
    int32_t i;
    for (i = 0; i < BOX_PARTICLE_POINTS; i ++) {
        box_spline_sample_t sample;
        float t = ((float)i + 0.5f) / (float)BOX_PARTICLE_POINTS;
        if (!box_spline_sample(world, spline, t, &sample)) {
            goto done;
        }

        impl->table[i].position[0] = sample.position.x;
        impl->table[i].position[1] = sample.position.y;
        impl->table[i].position[2] = sample.position.z;
        impl->table[i].normal[0] = sample.normal.x;
        impl->table[i].normal[1] = sample.normal.y;
        impl->table[i].normal[2] = sample.normal.z;
    }

    impl->points = BOX_PARTICLE_POINTS;
done:
    return;
}

void box_particle_sample(
    ecs_world_t *world,
    const ParticleEmitter *emitter,
    ParticleEmitterImpl *impl)
{
    bool table = emitter->shape == EmitterMesh || emitter->shape == EmitterSpline;
    if (impl->shape == (int32_t)emitter->shape && impl->source == emitter->source &&
        impl->seed == emitter->seed)
    {
        goto done;
    }

    memset(impl->table, 0, sizeof(impl->table));
    impl->points = 0;
    impl->shape = (int32_t)emitter->shape;
    impl->source = emitter->source;
    impl->seed = emitter->seed;
    if (!table || !emitter->source || !ecs_is_alive(world, emitter->source)) {
        goto done;
    }

    if (emitter->shape == EmitterMesh) {
        const Mesh3 *mesh = ecs_get(world, emitter->source, Mesh3);
        if (mesh) {
            box_particle_mesh(mesh, impl);
        }
    } else {
        box_particle_spline(world, emitter->source, impl);
    }

done:
    return;
}
