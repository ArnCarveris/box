/**
 * @file portal/light.c
 * @brief Light through portals: openings, the lights that shine into an area through them, the
 * sun through portals to the outdoors, and whether a light reaches a seen area.
 *
 * A light reaches through a run of openings when it is on the far side of each of them and
 * within its range of each; the shader then lets it reach a point only along rays through all.
 */

#include "light.h"
#include <math.h>

/** Part of its opening a door has to leave clear for light to pass */
#define BOX_PORTAL_LIGHT_CLEAR (0.01f)

/** Smallest cosine between the sun and the normal of an opening the sun passes */
#define BOX_PORTAL_SUN_GRAZE (0.02f)

/** Smallest weight of a light that shines into an area */
#define BOX_PORTAL_LIGHT_FAINT (1e-4f)

/** A walk from an area out through its portals: what it looks for and what it found */
typedef struct box_portal_walk_t {
    const box_portal_index_t *index;          /**< The index */
    const box_portal_aperture_t *apertures;   /**< The openings of its portals */
    const box_portal_light_t *lights;         /**< The lights of the scene */
    int32_t light_count;                      /**< Lights */
    const float *sun;                         /**< Unit direction toward the sun, NULL for none */
    const float *eye;                         /**< World position of the eye */
    int32_t area;                             /**< Area the light shines into */
    box_portal_through_t *out;                /**< Receives the lights */
    int32_t count;                            /**< Lights found */
    int32_t max;                              /**< Lights to keep at most */
} box_portal_walk_t;

/** A run of openings from the side a light comes from, the one into the area last */
typedef struct box_portal_path_t {
    int32_t count;                            /**< Openings */
    int32_t portals[BOX_PORTAL_LIGHT_DEPTH];  /**< Indices of their portals */
    float far[BOX_PORTAL_LIGHT_DEPTH];        /**< Side of each plane the light comes from: 1
                                               *   along the normal, -1 against it */
} box_portal_path_t;

/** Shrink the opening of a portal to the part its cover left clear: a partly open portal is
 * clear on the side away from where its cover slid.
 *
 * @param out The opening, updated
 * @param p Its portal
 */
static void box_portal_aperture_clear(
    box_portal_aperture_t *out,
    const box_portal_portal_t *p)
{
    float clear = glm_clamp(p->open, 0.0f, 1.0f);
    float *axis = p->slide == PortalSlideSide ? out->right : out->up;
    float sign = p->slide == PortalSlideDown ? -1.0f : 1.0f;

    if (p->open < BOX_PORTAL_SHUT || clear < BOX_PORTAL_LIGHT_CLEAR) {
        out->open = false;
    } else if (clear < 1.0f) {
        glm_vec3_muladds(axis, -sign * (1.0f - clear), out->center);
        glm_vec3_scale(axis, clear, axis);
    }
}

void box_portal_apertures(
    const ecs_world_t *world,
    const box_portal_index_t *index,
    ecs_vec_t *out)
{
    int32_t i, count = ecs_vec_count(&index->portals);

    ecs_vec_set_count_t(NULL, out, box_portal_aperture_t, count);

    for (i = 0; i < count; i ++) {
        const box_portal_portal_t *p = ecs_vec_get_t(&index->portals, box_portal_portal_t, i);
        box_portal_aperture_t *a = ecs_vec_get_t(out, box_portal_aperture_t, i);
        const VisPortal *portal = ecs_is_alive(world, p->entity)
            ? ecs_get(world, p->entity, VisPortal)
            : NULL;
        vec4 glass;
        int32_t k;

        glm_vec3_copy(ECS_CONST_CAST(float*, p->center), a->center);
        glm_vec3_scale(ECS_CONST_CAST(float*, p->right), p->width * 0.5f, a->right);
        glm_vec3_scale(ECS_CONST_CAST(float*, p->up), p->height * 0.5f, a->up);
        glm_vec3_copy(ECS_CONST_CAST(float*, p->normal), a->normal);
        glm_vec3_one(a->pass);
        a->open = true;
        box_portal_aperture_clear(a, p);

        if (portal && portal->glass) {
            glm_vec4_copy(ECS_CONST_CAST(float*, portal->glass_tint), glass);
            if (glass[3] <= 0) {
                glm_vec4_copy(BOX_PORTAL_GLASS_DEFAULT, glass);
            }

            for (k = 0; k < 3; k ++) {
                a->pass[k] = 1.0f - glass[3] + glass[3] * glass[k];
            }
        }
    }
}

int32_t box_portal_light_area(
    const box_portal_index_t *index,
    ecs_entity_t scene,
    const float *p)
{
    int32_t result = box_portal_index_area_at(index, scene, p);

    if (!result) {
        vec3 below = { p[0], p[1] - BOX_PORTAL_LIGHT_SIZE * 3.0f, p[2] };
        result = box_portal_index_area_at(index, scene, below);
    }

    return result;
}

/** Get the distance from a point to the rectangle of an opening.
 *
 * @param p The point
 * @param a The opening
 * @return The distance
 */
static float box_portal_light_reach(
    const float *p,
    const box_portal_aperture_t *a)
{
    vec3 d;
    float rl = glm_max(glm_vec3_norm(ECS_CONST_CAST(float*, a->right)), 1e-6f);
    float ul = glm_max(glm_vec3_norm(ECS_CONST_CAST(float*, a->up)), 1e-6f);
    float x, y, z;

    glm_vec3_sub(ECS_CONST_CAST(float*, p), ECS_CONST_CAST(float*, a->center), d);
    x = glm_max(0.0f, fabsf(glm_vec3_dot(d, ECS_CONST_CAST(float*, a->right)) / rl) - rl);
    y = glm_max(0.0f, fabsf(glm_vec3_dot(d, ECS_CONST_CAST(float*, a->up)) / ul) - ul);
    z = glm_vec3_dot(d, ECS_CONST_CAST(float*, a->normal));

    return sqrtf(x * x + y * y + z * z);
}

/** Check whether sun rays through one opening reach the rectangle of another: the corners of
 * the first, cast along the rays onto the plane of the second, overlap it.
 *
 * @param a The opening the rays come through
 * @param s Unit direction toward the sun
 * @param b The opening they have to reach
 * @return Whether they reach it
 */
static bool box_portal_light_beam(
    const box_portal_aperture_t *a,
    const float *s,
    const box_portal_aperture_t *b)
{
    float sn = glm_vec3_dot(ECS_CONST_CAST(float*, s), ECS_CONST_CAST(float*, b->normal));
    float rl = glm_vec3_norm(ECS_CONST_CAST(float*, b->right));
    float ul = glm_vec3_norm(ECS_CONST_CAST(float*, b->up));
    float x0 = INFINITY, x1 = -INFINITY, y0 = INFINITY, y1 = -INFINITY;
    bool result = fabsf(sn) >= 1e-4f;
    int32_t k;

    for (k = 0; k < 4 && result; k ++) {
        float sx = (k == 1 || k == 2) ? 1.0f : -1.0f;
        float sy = k >= 2 ? 1.0f : -1.0f;
        vec3 q, d, h;

        glm_vec3_copy(ECS_CONST_CAST(float*, a->center), q);
        glm_vec3_muladds(ECS_CONST_CAST(float*, a->right), sx, q);
        glm_vec3_muladds(ECS_CONST_CAST(float*, a->up), sy, q);
        glm_vec3_sub(q, ECS_CONST_CAST(float*, b->center), d);
        glm_vec3_copy(q, h);
        glm_vec3_muladds(ECS_CONST_CAST(float*, s),
            -glm_vec3_dot(d, ECS_CONST_CAST(float*, b->normal)) / sn, h);
        glm_vec3_sub(h, ECS_CONST_CAST(float*, b->center), h);

        float x = glm_vec3_dot(h, ECS_CONST_CAST(float*, b->right)) / rl;
        float y = glm_vec3_dot(h, ECS_CONST_CAST(float*, b->up)) / ul;
        x0 = glm_min(x0, x);
        x1 = glm_max(x1, x);
        y0 = glm_min(y0, y);
        y1 = glm_max(y1, y);
    }

    return result && x1 >= -rl && x0 <= rl && y1 >= -ul && y0 <= ul;
}

/** Get how much more an opening counts the nearer it is to the eye.
 *
 * @param w The walk
 * @param a The opening
 * @return 1 at the eye, falling off with the distance
 */
static float box_portal_light_near(
    const box_portal_walk_t *w,
    const box_portal_aperture_t *a)
{
    float d = glm_vec3_distance(ECS_CONST_CAST(float*, w->eye),
        ECS_CONST_CAST(float*, a->center)) / BOX_PORTAL_LIGHT_NEAR;

    return 1.0f / (1.0f + d * d);
}

/** Keep a light of a walk: append it, or replace the faintest kept one when the walk is full.
 *
 * @param w The walk
 * @param path The openings it comes through
 * @param light Index of the light, -1 for the sun
 * @param weight How bright it is
 */
static void box_portal_light_keep(
    box_portal_walk_t *w,
    const box_portal_path_t *path,
    int32_t light,
    float weight)
{
    box_portal_through_t *t = NULL;
    int32_t i, k;

    if (w->count < w->max) {
        t = &w->out[w->count ++];
    } else {
        for (i = 0; i < w->count; i ++) {
            if (w->out[i].weight < weight && (!t || w->out[i].weight < t->weight)) {
                t = &w->out[i];
            }
        }
    }

    if (t) {
        t->light = light;
        t->weight = weight;
        t->count = path->count;
        glm_vec3_one(t->pass);

        for (k = 0; k < path->count; k ++) {
            const box_portal_aperture_t *a = &w->apertures[path->portals[k]];
            t->portals[k] = path->portals[k];
            glm_vec3_mul(t->pass, ECS_CONST_CAST(float*, a->pass), t->pass);
        }
    }
}

/** Keep the lights of an area that shine through a run of openings: those on the far side of
 * each opening and within their range of it.
 *
 * @param w The walk
 * @param area The area of the lights
 * @param path The openings, the one into the area of the walk last
 */
static void box_portal_light_lights(
    box_portal_walk_t *w,
    int32_t area,
    const box_portal_path_t *path)
{
    const box_portal_aperture_t *last = &w->apertures[path->portals[path->count - 1]];
    float near = box_portal_light_near(w, last);
    int32_t i, k;

    for (i = 0; i < w->light_count; i ++) {
        const box_portal_light_t *l = &w->lights[i];
        float weight = l->intensity * near;

        if (l->area != area || l->range <= 0) {
            continue;
        }

        for (k = 0; k < path->count && weight > 0; k ++) {
            const box_portal_aperture_t *a = &w->apertures[path->portals[k]];
            vec3 d;
            float reach = box_portal_light_reach(l->position, a);

            glm_vec3_sub(ECS_CONST_CAST(float*, l->position), ECS_CONST_CAST(float*, a->center),
                d);
            if (glm_vec3_dot(d, ECS_CONST_CAST(float*, a->normal)) * path->far[k] <= 0 ||
                reach >= l->range)
            {
                weight = 0;
            } else {
                weight *= (1.0f - reach / l->range) * (1.0f - reach / l->range);
            }
        }

        if (weight > BOX_PORTAL_LIGHT_FAINT) {
            box_portal_light_keep(w, path, i, weight);
        }
    }
}

/** Keep the sun when it shines from the outdoors through a run of openings.
 *
 * @param w The walk
 * @param path The openings, the first one to the outdoors
 */
static void box_portal_light_sun(
    box_portal_walk_t *w,
    const box_portal_path_t *path)
{
    bool shines = w->sun != NULL;
    int32_t k;

    for (k = 0; k < path->count && shines; k ++) {
        const box_portal_aperture_t *a = &w->apertures[path->portals[k]];
        shines = glm_vec3_dot(ECS_CONST_CAST(float*, w->sun),
            ECS_CONST_CAST(float*, a->normal)) * path->far[k] > BOX_PORTAL_SUN_GRAZE;
    }

    if (shines && path->count > 1) {
        shines = box_portal_light_beam(&w->apertures[path->portals[0]], w->sun,
            &w->apertures[path->portals[1]]);
    }

    if (shines) {
        box_portal_light_keep(w, path, -1, BOX_PORTAL_SUN_WEIGHT *
            box_portal_light_near(w, &w->apertures[path->portals[path->count - 1]]));
    }
}

/** Walk out from an area through its open portals, keeping the lights of the areas behind them
 * and the sun.
 *
 * @param w The walk
 * @param area The area to walk out of
 * @param path The openings that led to it, empty for the area of the walk
 * @param from The portal it was entered by, -1 for none
 */
static void box_portal_light_walk(
    box_portal_walk_t *w,
    int32_t area,
    const box_portal_path_t *path,
    int32_t from)
{
    const box_portal_area_t *a = ecs_vec_get_t(&w->index->areas, box_portal_area_t, area);
    const int32_t *links = ecs_vec_first_t(&w->index->links, int32_t);
    int32_t i, k;

    for (i = 0; i < a->link_count; i ++) {
        int32_t portal = links[a->link_first + i];
        const box_portal_portal_t *p = ecs_vec_get_t(
            &w->index->portals, box_portal_portal_t, portal);
        int32_t across = p->front == area ? p->back : p->front;
        box_portal_path_t next;

        if (across == w->area || across == area || portal == from ||
            !w->apertures[portal].open)
        {
            continue;
        }

        next.count = path->count + 1;
        next.portals[0] = portal;
        next.far[0] = p->back == across ? 1.0f : -1.0f;

        for (k = 0; k < path->count; k ++) {
            next.portals[k + 1] = path->portals[k];
            next.far[k + 1] = path->far[k];
        }

        if (across == 0) {
            box_portal_light_sun(w, &next);
        }

        box_portal_light_lights(w, across, &next);

        if (next.count < BOX_PORTAL_LIGHT_DEPTH) {
            box_portal_light_walk(w, across, &next, portal);
        }
    }
}

/** Order lights brightest first.
 *
 * @param a The first light
 * @param b The second light
 * @return Negative when the first is brighter
 */
static int box_portal_through_compare(
    const void *a,
    const void *b)
{
    float wa = ((const box_portal_through_t*)a)->weight;
    float wb = ((const box_portal_through_t*)b)->weight;

    return (wa < wb) - (wa > wb);
}

int32_t box_portal_through(
    const box_portal_index_t *index,
    const ecs_vec_t *apertures,
    int32_t area,
    const box_portal_light_t *lights,
    int32_t light_count,
    const float *sun,
    const float *eye,
    box_portal_through_t *out,
    int32_t max)
{
    box_portal_walk_t w = {
        .index = index,
        .apertures = ecs_vec_first_t(apertures, box_portal_aperture_t),
        .lights = lights,
        .light_count = light_count,
        .sun = sun,
        .eye = eye,
        .area = area,
        .out = out,
        .max = max
    };
    box_portal_path_t path = { 0 };

    if (max > 0 && area >= 0 && area < ecs_vec_count(&index->areas) &&
        ecs_vec_count(apertures) == ecs_vec_count(&index->portals))
    {
        box_portal_light_walk(&w, area, &path, -1);
        qsort(out, (size_t)w.count, sizeof(box_portal_through_t), box_portal_through_compare);
    }

    return w.count;
}

/** Check whether a light reaches a seen area through the portals of an area it shines into.
 *
 * @param index The index
 * @param apertures The openings of its portals
 * @param light The light
 * @param seen Per area, whether it is seen
 * @param area The area it shines into
 * @param from The portal it came by, -1 for none
 * @param depth Portals it passed
 * @return Whether it reaches one
 */
static bool box_portal_light_spreads(
    const box_portal_index_t *index,
    const box_portal_aperture_t *apertures,
    const box_portal_light_t *light,
    const bool *seen,
    int32_t area,
    int32_t from,
    int32_t depth)
{
    const box_portal_area_t *a = ecs_vec_get_t(&index->areas, box_portal_area_t, area);
    const int32_t *links = ecs_vec_first_t(&index->links, int32_t);
    bool result = false;
    int32_t i;

    for (i = 0; i < a->link_count && !result; i ++) {
        int32_t portal = links[a->link_first + i];
        const box_portal_portal_t *p = ecs_vec_get_t(&index->portals, box_portal_portal_t, portal);
        const box_portal_aperture_t *x = &apertures[portal];
        int32_t across = p->front == area ? p->back : p->front;
        float side = p->back == across ? 1.0f : -1.0f;
        vec3 d;

        if (portal == from || across == area || !x->open) {
            continue;
        }

        glm_vec3_sub(ECS_CONST_CAST(float*, light->position), ECS_CONST_CAST(float*, x->center),
            d);
        if (glm_vec3_dot(d, ECS_CONST_CAST(float*, x->normal)) * side > 0 ||
            box_portal_light_reach(light->position, x) >= light->range)
        {
            continue;
        }

        result = seen[across] || (depth + 1 < BOX_PORTAL_LIGHT_DEPTH &&
            box_portal_light_spreads(index, apertures, light, seen, across, portal, depth + 1));
    }

    return result;
}

bool box_portal_light_reaches(
    const box_portal_index_t *index,
    const ecs_vec_t *apertures,
    const box_portal_light_t *light,
    const bool *seen)
{
    bool result = light->area < 0 || light->area >= ecs_vec_count(&index->areas) ||
        seen[light->area];

    if (!result && ecs_vec_count(apertures) == ecs_vec_count(&index->portals)) {
        result = box_portal_light_spreads(index, ecs_vec_first_t(apertures,
            box_portal_aperture_t), light, seen, light->area, -1, 0);
    }

    return result;
}
