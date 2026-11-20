/**
 * @file controllers/route.c
 * @brief The arc length table of the closed route of a vessel, seen from above.
 */

#include "controllers.h"
#include "../geometry/paths.h"
#include <float.h>
#include <math.h>

/** Fill the cubics and the arc length samples of a route from a valid closed path.
 *
 * @param path The path
 * @param count Its segments, at least one
 * @param route The table, written
 */
static void box_route_fill(
    const Path3D *path,
    int32_t count,
    box_route_t *route)
{
    route->count = count;
    route->samples = count * BOX_ROUTE_STEPS;
    route->cubics = ecs_os_malloc(count * (int32_t)sizeof(vec3[4]));
    route->lengths = ecs_os_malloc_n(float, route->samples);
    route->params = ecs_os_malloc_n(float, route->samples);

    for (int32_t i = 0; i < count; i ++) {
        box_path3_cubic(path, i, route->cubics[i]);
    }

    double length = 0;
    float last[2] = { route->cubics[0][0][0], route->cubics[0][0][2] };
    for (int32_t i = 0; i < count; i ++) {
        vec3 *c = route->cubics[i];
        for (int32_t j = 0; j < BOX_ROUTE_STEPS; j ++) {
            float t = (float)j / BOX_ROUTE_STEPS;
            vec3 p;
            box_curve_cubic3(c[0], c[1], c[2], c[3], t, p);

            length += sqrt((double)(p[0] - last[0]) * (p[0] - last[0]) +
                (double)(p[2] - last[1]) * (p[2] - last[1]));
            last[0] = p[0];
            last[1] = p[2];

            int32_t k = i * BOX_ROUTE_STEPS + j;
            route->lengths[k] = (float)length;
            route->params[k] = (float)i + t;
        }
    }

    vec3 *first = route->cubics[0];
    length += sqrt((double)(first[0][0] - last[0]) * (first[0][0] - last[0]) +
        (double)(first[0][2] - last[1]) * (first[0][2] - last[1]));
    route->total = (float)length;
}

bool box_route_init(
    const Path3D *path,
    box_route_t *route)
{
    ecs_os_zeromem(route);
    int32_t count = path && path->closed && box_path3_valid(path) ? box_path3_count(path) : 0;
    if (count > 0) {
        box_route_fill(path, count, route);
    }

    bool valid = route->total > 0;
    if (count > 0 && !valid) {
        box_route_fini(route);
    }

    return valid;
}

void box_route_fini(
    box_route_t *route)
{
    ecs_os_free(route->cubics);
    ecs_os_free(route->lengths);
    ecs_os_free(route->params);
    ecs_os_zeromem(route);
}

float box_route_wrap(
    const box_route_t *route,
    float distance)
{
    float d = isfinite(distance) ? fmodf(distance, route->total) : 0;
    if (d < 0) {
        d += route->total;
    }

    if (d >= route->total) {
        d = 0;
    }

    return d;
}

void box_route_at(
    const box_route_t *route,
    float distance,
    float *x,
    float *z,
    float *heading)
{
    float d = box_route_wrap(route, distance);
    const float *lengths = route->lengths;
    int32_t lo = 0;
    int32_t hi = route->samples;
    while (hi - lo > 1) {
        int32_t mid = (lo + hi) / 2;
        if (lengths[mid] <= d) {
            lo = mid;
        } else {
            hi = mid;
        }
    }

    float next = lo + 1 < route->samples ? lengths[lo + 1] : route->total;
    float span = next - lengths[lo];
    float f = span > 0 ? glm_clamp((d - lengths[lo]) / span, 0, 1) : 0;
    float param = route->params[lo] + f / BOX_ROUTE_STEPS;
    int32_t index = (int32_t)floorf(param);
    if (index > route->count - 1) {
        index = route->count - 1;
    }

    float t = glm_clamp(param - (float)index, 0, 1);
    vec3 *c = route->cubics[index];
    vec3 p;
    box_curve_cubic3(c[0], c[1], c[2], c[3], t, p);

    float u = 1 - t;
    float tangent[2];
    for (int32_t i = 0; i < 2; i ++) {
        int32_t a = i * 2;
        tangent[i] = 3 * u * u * (c[1][a] - c[0][a]) + 6 * u * t * (c[2][a] - c[1][a]) +
            3 * t * t * (c[3][a] - c[2][a]);
    }

    if (tangent[0] * tangent[0] + tangent[1] * tangent[1] <= 1e-12f) {
        tangent[0] = c[3][0] - c[0][0];
        tangent[1] = c[3][2] - c[0][2];
    }

    *x = p[0];
    *z = p[2];
    *heading = atan2f(tangent[0], tangent[1]);
}

float box_route_nearest(
    const box_route_t *route,
    float x,
    float z)
{
    float best = FLT_MAX;
    float distance = 0;
    for (int32_t i = 0; i < route->count; i ++) {
        vec3 *c = route->cubics[i];
        for (int32_t j = 0; j < BOX_ROUTE_STEPS; j ++) {
            vec3 p;
            box_curve_cubic3(c[0], c[1], c[2], c[3], (float)j / BOX_ROUTE_STEPS, p);

            float dx = p[0] - x;
            float dz = p[2] - z;
            float d2 = dx * dx + dz * dz;
            if (d2 < best) {
                best = d2;
                distance = route->lengths[i * BOX_ROUTE_STEPS + j];
            }
        }
    }

    return distance;
}

float box_route_waypoint(
    const box_route_t *route,
    int32_t index)
{
    int32_t i = index % route->count;
    if (i < 0) {
        i += route->count;
    }

    return route->lengths[i * BOX_ROUTE_STEPS];
}
