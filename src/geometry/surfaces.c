/**
 * @file geometry/surfaces.c
 * @brief Parametric surface shapes (ellipsoid shell, torus shell, beveled prism).
 */

#include "mesh_cache.h"
#include <math.h>

/** Clamp a tessellation step count, using a fallback for non-positive values.
 *
 * @param value The requested step count.
 * @param fallback The step count used when value is not positive.
 * @return The step count to use.
 */
static int box_surface_steps(
    int value,
    int fallback)
{
    return value <= 0 ? fallback : glm_clamp(value, 3, 256);
}

/** Generate a grid of (u, v, region) vertices for each enabled surface region.
 *
 * @param value The shape component (unused).
 * @param key The cache key with step counts and the enabled region mask.
 * @param mesh Output mesh.
 */
static void box_surface_grid(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    (void)value;
    int nu = key->i[0], nv = key->i[1], regions = key->i[2];
    for (int region = 0; region < 8; region ++) {
        if (!(regions & (1 << region))) {
            continue;
        }

        int rows = region < 2 ? nv : 1;
        int base = mesh->vertex_count;
        for (int y = 0; y <= rows; y ++) {
            for (int x = 0; x <= nu; x ++) {
                float u = (float)x / nu, v = (float)y / rows;
                box_mesh_vertex(mesh, (vec3){u, v, (float)region}, (vec3){0}, u, v);
            }
        }

        bool reverse = region == 1 || region == 3 || region == 4 || region == 7;
        for (int y = 0; y < rows; y ++) {
            for (int x = 0; x < nu; x ++) {
                int a = base + y * (nu + 1) + x, b = a + nu + 1;
                if (reverse) {
                    box_mesh_triangle(mesh, a, a + 1, b);
                    box_mesh_triangle(mesh, a + 1, b + 1, b);
                } else {
                    box_mesh_triangle(mesh, a, b, a + 1);
                    box_mesh_triangle(mesh, a + 1, b, b + 1);
                }
            }
        }
    }
}

static void box_surface_key(box_shape_key_t *key, int segments, int rings,
    bool hollow, bool start, bool end)
{
    key->i[0] = box_surface_steps(segments, 32);
    key->i[1] = box_surface_steps(rings, 24);
    key->i[2] = 1 | (hollow ? 2 : 0) | (start ? 4 : 0) | (end ? 8 : 0);
}

static void box_surface_bound_points(const box_surface_data_t *d, int first, int count,
    float radius, vec3 min, vec3 max)
{
    glm_vec3_broadcast(INFINITY, min);
    glm_vec3_broadcast(-INFINITY, max);
    for (int i = first; i < first + count; i ++) {
        for (int j = 0; j < 3; j ++) {
            min[j] = fminf(min[j], d->p[i][j] - radius);
            max[j] = fmaxf(max[j], d->p[i][j] + radius);
        }
    }
}

static void box_shell_key(box_shape_key_t *key, int segments, int rings,
    float thickness, bool rims)
{
    box_surface_key(key, segments, rings, thickness > 0, false, false);
    if (rims && thickness > 0) {
        key->i[2] |= 0xf0;
    }
}

/** Compute the mesh cache key for a ellipsoid shell.
 *
 * @param value The EllipsoidShell component.
 * @param key Output cache key.
 */
static void box_ellipsoid_key(
    const void *value,
    box_shape_key_t *key)
{
    const EllipsoidShell *s = value;
    box_shell_key(key, s->segments, s->rings, s->thickness, s->rims);
}

/** Fill the surface parameters and local bounds for a ellipsoid shell.
 *
 * @param value The EllipsoidShell component.
 * @param d Output surface parameters.
 * @param min Output minimum bound.
 * @param max Output maximum bound.
 */
static void box_ellipsoid_data(
    const void *value,
    box_surface_data_t *d,
    vec3 min,
    vec3 max)
{
    const EllipsoidShell *s = value;
    d->p[0][0] = 4;
    float smallest = fminf(fabsf(s->radius[0]), fminf(fabsf(s->radius[1]), fabsf(s->radius[2])));
    d->p[1][0] = glm_clamp(s->thickness, 0, smallest);
    glm_vec4_copy((vec4){s->longitude_start, s->longitude_end, s->latitude_start,
        s->latitude_end}, d->p[2]);
    for (int j = 0; j < 3; j ++) {
        d->p[3][j] = max[j] = fabsf(s->radius[j]);
        min[j] = -max[j];
    }
}

/** Compute the mesh cache key for a torus shell.
 *
 * @param value The TorusShell component.
 * @param key Output cache key.
 */
static void box_torus_shell_key(
    const void *value,
    box_shape_key_t *key)
{
    const TorusShell *s = value;
    box_shell_key(key, s->segments, s->rings, s->thickness, s->rims);
}

/** Fill the surface parameters and local bounds for a torus shell.
 *
 * @param value The TorusShell component.
 * @param d Output surface parameters.
 * @param min Output minimum bound.
 * @param max Output maximum bound.
 */
static void box_torus_shell_data(
    const void *value,
    box_surface_data_t *d,
    vec3 min,
    vec3 max)
{
    const TorusShell *s = value;
    d->p[0][0] = 5;
    d->p[1][0] = glm_clamp(s->thickness, 0, fabsf(s->tube));
    glm_vec4_copy((vec4){s->ring_start, s->ring_end, s->tube_start, s->tube_end}, d->p[2]);
    d->p[3][0] = fabsf(s->radius);
    d->p[3][1] = fabsf(s->tube);
    glm_vec3_copy((vec3){fabsf(s->radius) + fabsf(s->tube), fabsf(s->tube),
        fabsf(s->radius) + fabsf(s->tube)}, max);
    glm_vec3_negate_to(max, min);
}

/** Test whether a prism footprint is a strictly convex triangle or quad.
 *
 * @param s The BeveledPrism component.
 * @return True if the footprint is valid.
 */
static bool box_prism_valid(
    const BeveledPrism *s)
{
    int count = ecs_vec_count(&s->footprint);
    if (count != 3 && count != 4) {
        goto error;
    }

    const vec2 *points = ecs_vec_first(&s->footprint);
    float sign = 0;
    for (int i = 0; i < count; i ++) {
        const float *a = points[i], *b = points[(i + 1) % count], *c = points[(i + 2) % count];
        float cross = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]);
        if (!isfinite(cross) || fabsf(cross) < 0.000001f || (i && sign * cross < 0)) {
            goto error;
        }

        sign = cross;
    }

    return true;
error:
    return false;
}

/** Compute the mesh cache key for a beveled prism.
 *
 * @param value The BeveledPrism component.
 * @param key Output cache key.
 */
static void box_prism_key(
    const void *value,
    box_shape_key_t *key)
{
    const BeveledPrism *s = value;
    int count = ecs_vec_count(&s->footprint);
    box_surface_key(key, count * 8, 12, false, true, true);
    if (!box_prism_valid(s)) {
        key->i[2] = 0;
    }
}

/** Fill the surface parameters and local bounds for a beveled prism.
 *
 * @param value The BeveledPrism component.
 * @param d Output surface parameters.
 * @param min Output minimum bound.
 * @param max Output maximum bound.
 */
static void box_prism_data(
    const void *value,
    box_surface_data_t *d,
    vec3 min,
    vec3 max)
{
    const BeveledPrism *s = value;
    int count = ecs_vec_count(&s->footprint);
    if (!box_prism_valid(s)) {
        goto done;
    }

    const vec2 *points = ecs_vec_first(&s->footprint);
    d->p[0][0] = 6;
    d->p[0][2] = count;
    d->p[1][0] = glm_clamp(s->bevel, 0, fabsf(s->depth) * 0.5f);
    d->p[1][1] = fabsf(s->depth);
    d->p[1][2] = fmaxf(s->inset, 0);
    d->p[2][0] = s->offset[0];
    d->p[2][1] = s->offset[1];
    float area = 0;
    for (int i = 0; i < count; i ++) {
        area += points[i][0] * points[(i + 1) % count][1] -
            points[(i + 1) % count][0] * points[i][1];
    }

    for (int i = 0; i < count; i ++) {
        int index = area > 0 ? i : count - 1 - i;
        d->p[4 + i][0] = points[index][0];
        d->p[4 + i][2] = points[index][1];
        d->p[8][0] += points[i][0] / count;
        d->p[8][2] += points[i][1] / count;
    }

    box_surface_bound_points(d, 4, count, fabsf(s->depth) + fabsf(s->offset[0]) +
        fabsf(s->offset[1]) + fabsf(s->inset) + fabsf(s->bevel), min, max);

done:
    return;
}

bool box_surface_evaluate(const ecs_world_t *world, ecs_entity_t entity,
    SurfaceRegion region, float u, float v, box_surface_sample_t *sample)
{
    if (!sample || !entity || !ecs_is_alive(world, entity)) {
        return false;
    }

    const ShapeMesh *shape = ecs_get(world, entity, ShapeMesh);
    if (!shape || !shape->mesh || shape->surface.p[0][0] == 0 || region < 0 || region > 7 ||
        !isfinite(u) || !isfinite(v) || u < 0 || u > 1 || v < 0 || v > 1)
    {
        return false;
    }
    if (!((int)shape->surface.p[3][3] & (1 << region))) {
        return false;
    }
    int kind = (int)shape->surface.p[0][0];
    if ((kind == 1 || kind == 2) && (region == SurfaceStart || region == SurfaceEnd)) {
        const float *bounds = shape->surface.p[6];
        if (!box_profile_contains(&shape->surface, bounds[0] + (bounds[1] - bounds[0]) * u,
            bounds[2] + (bounds[3] - bounds[2]) * v))
        {
            return false;
        }
    }
    box_surface_sample(&shape->surface, region, u, v, sample);
    if (shape->surface.p[0][0] == 7 && region >= 2 &&
        !box_profile_contains(&shape->surface, sample->position[0], sample->position[2]))
    {
        return false;
    }
    for (int j = 0; j < 3; j ++) {
        if (!shape->scale[j]) {
            return false;
        }
        sample->position[j] *= shape->scale[j];
        sample->normal[j] /= shape->scale[j];
        sample->tangent[j] *= shape->scale[j];
    }
    glm_vec3_normalize(sample->normal);
    glm_vec3_normalize(sample->tangent);

    return true;
}

void box_geometry_surfaces_init(
    ecs_world_t *world)
{
    box_owned_hooks(world, BeveledPrism, BOX_OWNED_VEC(BeveledPrism, footprint, vec2));
    box_shape_register(world, &(box_shape_desc_t){.component = ecs_id(EllipsoidShell),
        .key = box_ellipsoid_key, .generate = box_surface_grid, .surface = box_ellipsoid_data});
    box_shape_register(world, &(box_shape_desc_t){.component = ecs_id(TorusShell),
        .key = box_torus_shell_key, .generate = box_surface_grid, .surface = box_torus_shell_data});
    box_shape_register(world, &(box_shape_desc_t){.component = ecs_id(BeveledPrism),
        .key = box_prism_key, .generate = box_surface_grid, .surface = box_prism_data});
}
