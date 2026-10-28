/**
 * @file particle/simulate.c
 * @brief Samples the point table of an EmitterMesh over the surface of a mesh and draws the
 * properties of a particle as the shaders do.
 */

#include "simulate.h"

/** Get a corner of a triangle of a mesh, scaled by the shape.
 *
 * @param mesh The mesh
 * @param i Index of the corner in the triangle list
 * @param scale Scale of the shape
 * @param out Receives the position
 */
static void box_particle_corner(
    const box_mesh_t *mesh,
    int32_t i,
    const float *scale,
    float *out)
{
    uint32_t v = mesh->index_count ? mesh->indices[i] : (uint32_t)i;
    int32_t c;
    for (c = 0; c < 3; c ++) {
        out[c] = (int32_t)v < mesh->vertex_count
            ? mesh->vertices[v].position[c] * scale[c]
            : 0.0f;
    }
}

/** Get the corners of a triangle of a mesh, scaled by the shape.
 *
 * @param mesh The mesh
 * @param t Index of the triangle
 * @param scale Scale of the shape
 * @param out Receives the three corners
 */
static void box_particle_triangle(
    const box_mesh_t *mesh,
    int32_t t,
    const float *scale,
    vec3 *out)
{
    box_particle_corner(mesh, t * 3, scale, out[0]);
    box_particle_corner(mesh, t * 3 + 1, scale, out[1]);
    box_particle_corner(mesh, t * 3 + 2, scale, out[2]);
}

int32_t box_particle_mesh_table(
    const box_mesh_t *mesh,
    const float *scale,
    uint32_t seed,
    box_particle_point_t *out,
    int32_t count)
{
    int32_t corners = mesh ? (mesh->index_count ? mesh->index_count : mesh->vertex_count) : 0;
    int32_t triangles = corners / 3, t, j, result = 0;
    double *cdf = NULL, total = 0;
    if (!triangles || count <= 0) {
        goto done;
    }

    cdf = ecs_os_malloc_n(double, triangles);
    for (t = 0; t < triangles; t ++) {
        vec3 v[3], e1, e2, n;
        box_particle_triangle(mesh, t, scale, v);
        glm_vec3_sub(v[1], v[0], e1);
        glm_vec3_sub(v[2], v[0], e2);
        glm_vec3_cross(e1, e2, n);
        total += 0.5 * (double)glm_vec3_norm(n);
        cdf[t] = total;
    }

    if (!(total > 0)) {
        goto done;
    }

    for (j = 0; j < count; j ++) {
        double pick = (double)box_particle_unit(
            box_particle_hash(seed, (uint32_t)j, BOX_PARTICLE_STREAM_TRIANGLE)) * total;
        float r1 = box_particle_unit(
            box_particle_hash(seed, (uint32_t)j, BOX_PARTICLE_STREAM_BARY));
        float r2 = box_particle_unit(
            box_particle_hash(seed, (uint32_t)j, BOX_PARTICLE_STREAM_BARY + 1u));
        int32_t lo = 0, hi = triangles - 1, c;
        while (lo < hi) {
            int32_t mid = (lo + hi) / 2;
            if (cdf[mid] > pick) {
                hi = mid;
            } else {
                lo = mid + 1;
            }
        }

        vec3 v[3], e1, e2, n;
        float s = sqrtf(r1);
        float w[3] = { 1.0f - s, s * (1.0f - r2), s * r2 };
        box_particle_triangle(mesh, lo, scale, v);
        glm_vec3_sub(v[1], v[0], e1);
        glm_vec3_sub(v[2], v[0], e2);
        glm_vec3_cross(e1, e2, n);
        glm_vec3_normalize(n);
        for (c = 0; c < 3; c ++) {
            out[j].position[c] = w[0] * v[0][c] + w[1] * v[1][c] + w[2] * v[2][c];
            out[j].normal[c] = n[c];
        }

        out[j].position[3] = 0.0f;
        out[j].normal[3] = 0.0f;
    }

    result = count;
done:
    ecs_os_free(cdf);

    return result;
}

void box_particle_sample(
    const ParticleEmitter *emitter,
    uint32_t index,
    box_particle_sample_t *out)
{
    uint32_t seed = emitter->seed;
    uint32_t life_min = box_particle_us(emitter->lifetime.min);
    uint32_t life_max = box_particle_us(emitter->lifetime.max);
    float spread = glm_clamp(emitter->spread, 0.0f, 180.0f);
    float n[3] = { emitter->direction[0], emitter->direction[1], emitter->direction[2] };
    float u[2] = {
        box_particle_unit(box_particle_hash(seed, index, BOX_PARTICLE_STREAM_DIRECTION)),
        box_particle_unit(box_particle_hash(seed, index, BOX_PARTICLE_STREAM_DIRECTION + 1u))
    };
    float color[3] = {
        (float)emitter->color.r / 255.0f,
        (float)emitter->color.g / 255.0f,
        (float)emitter->color.b / 255.0f
    };
    float variance[3] = {
        (float)emitter->color_variance.r / 255.0f,
        (float)emitter->color_variance.g / 255.0f,
        (float)emitter->color_variance.b / 255.0f
    };
    if (glm_vec3_norm2(n) > 0) {
        glm_vec3_normalize(n);
    } else {
        glm_vec3_zero(n);
    }

    box_particle_cone(n, cosf(glm_rad(spread)), u, out->direction);
    out->speed = box_particle_draw(seed, index, BOX_PARTICLE_STREAM_SPEED,
        emitter->speed.min, emitter->speed.max);
    out->radial = box_particle_draw(seed, index, BOX_PARTICLE_STREAM_RADIAL,
        emitter->radial.min, emitter->radial.max);
    out->life = box_particle_life(seed, index, life_min,
        life_max > life_min ? life_max - life_min : 0);
    out->size = box_particle_draw(seed, index, BOX_PARTICLE_STREAM_SIZE,
        emitter->size.min, emitter->size.max);
    out->rotation = box_particle_draw(seed, index, BOX_PARTICLE_STREAM_ROTATION,
        emitter->rotation.min, emitter->rotation.max);
    out->spin = box_particle_draw(seed, index, BOX_PARTICLE_STREAM_SPIN,
        emitter->spin.min, emitter->spin.max);
    out->opacity = box_particle_draw(seed, index, BOX_PARTICLE_STREAM_OPACITY,
        emitter->opacity.min, emitter->opacity.max);
    box_particle_color(seed, index, color, variance, out->color);
}
