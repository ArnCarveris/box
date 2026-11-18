/**
 * @file render/portal_lights.c
 * @brief Lighting of portal-room scenes in the light extract: the area of every light, light
 * culling by what the views see through portals, and the shading block of every view.
 *
 * The traversal here is the views' own, independent of whether they cull by portals: a view
 * with CullSettings.portals off still lights and fogs its areas.
 */

#include "portal_lights.h"

box_render_portal_lights_t* box_render_portal_lights_new(void)
{
    box_render_portal_lights_t *result = ecs_os_calloc_t(box_render_portal_lights_t);

    ecs_vec_init_t(NULL, &result->views, box_render_portal_view_t, 0);
    ecs_vec_init_t(NULL, &result->apertures, box_portal_aperture_t, 0);
    ecs_vec_init_t(NULL, &result->lights, box_portal_light_t, 0);

    return result;
}

void box_render_portal_lights_free(
    box_render_portal_lights_t *s)
{
    int32_t i;

    if (s) {
        for (i = 0; i < ecs_vec_size(&s->views); i ++) {
            box_render_portal_view_t *v = &ecs_vec_first_t(
                &s->views, box_render_portal_view_t)[i];
            box_portal_vis_fini(&v->vis);
            ecs_os_free(v->seen);
        }

        ecs_vec_fini_t(NULL, &s->views, box_render_portal_view_t);
        ecs_vec_fini_t(NULL, &s->apertures, box_portal_aperture_t);
        ecs_vec_fini_t(NULL, &s->lights, box_portal_light_t);
        ecs_os_free(s);
    }
}

/** Make room for the views of a frame, keeping the traversal results of earlier frames.
 *
 * @param s The state
 * @param count Views of the frame
 */
static void box_render_portal_lights_reserve(
    box_render_portal_lights_t *s,
    int32_t count)
{
    int32_t i, size = ecs_vec_size(&s->views);

    if (count > size) {
        ecs_vec_set_size_t(NULL, &s->views, box_render_portal_view_t, count);
        for (i = size; i < ecs_vec_size(&s->views); i ++) {
            box_render_portal_view_t *v = &ecs_vec_first_t(
                &s->views, box_render_portal_view_t)[i];
            ecs_os_memset_t(v, 0, box_render_portal_view_t);
            box_portal_vis_init(&v->vis);
        }
    }

    ecs_vec_set_count_t(NULL, &s->views, box_render_portal_view_t, count);
}

/** Walk the areas one view sees.
 *
 * @param world The world
 * @param s The state
 * @param v The view of the packet
 * @param out Receives what it sees
 */
static void box_render_portal_lights_walk(
    ecs_world_t *world,
    const box_render_portal_lights_t *s,
    const box_render_view_t *v,
    box_render_portal_view_t *out)
{
    const Camera *c = &v->camera_data;
    const View *view = v->view && ecs_is_alive(world, v->view)
        ? ecs_get(world, v->view, View)
        : NULL;
    int32_t count = ecs_vec_count(&s->index->areas), i;
    box_portal_camera_t camera;
    mat4 proj, view_proj;

    out->scene = v->scene;
    out->walked = view && !c->ortho && box_portal_index_has(s->index, v->scene);
    if (!out->walked) {
        goto done;
    }

    box_camera_projection(c, glm_max(BOX_RENDER_PORTAL_LIGHTS_ASPECT, c->aspect_ratio), 1.0f,
        proj);
    glm_mat4_mul(proj, ECS_CONST_CAST(vec4*, v->view_matrix), view_proj);
    box_portal_camera_init(&camera, view_proj, v->camera_transform[3]);
    box_portal_traverse(s->index, v->scene, &camera, &out->vis);
    out->walked = !out->vis.truncated;

    if (out->seen_count < count) {
        ecs_os_free(out->seen);
        out->seen = ecs_os_malloc_n(bool, count);
        out->seen_count = count;
    }

    ecs_os_memset(out->seen, 0, count * ECS_SIZEOF(bool));
    for (i = 0; i < ecs_vec_count(&out->vis.entries); i ++) {
        const box_portal_entry_t *e = ecs_vec_get_t(&out->vis.entries, box_portal_entry_t, i);
        if (!e->sky_only) {
            out->seen[e->area] = true;
        }
    }
done:
    return;
}

void box_render_portal_lights_begin(
    ecs_world_t *world,
    box_render_portal_lights_t *s,
    const box_render_packet_t *p)
{
    int32_t i, count = ecs_vec_count(&p->views);

    s->index = box_portal_index(world);
    if (s->index && ecs_vec_count(&s->index->areas) < 2) {
        s->index = NULL;
    }

    box_render_portal_lights_reserve(s, s->index ? count : 0);
    if (!s->index) {
        goto done;
    }

    box_portal_apertures(world, s->index, &s->apertures);
    for (i = 0; i < count; i ++) {
        box_render_portal_lights_walk(world, s,
            ecs_vec_get_t(&p->views, box_render_view_t, i),
            ecs_vec_get_t(&s->views, box_render_portal_view_t, i));
    }
done:
    return;
}

bool box_render_portal_lights_seen(
    const box_render_portal_lights_t *s,
    int32_t view,
    ecs_entity_t scene,
    const float *center,
    float range)
{
    const box_render_portal_view_t *v = s->index && view >= 0 &&
        view < ecs_vec_count(&s->views)
            ? ecs_vec_get_t(&s->views, box_render_portal_view_t, view)
            : NULL;
    bool result = !v || !v->walked || v->scene != scene;

    if (!result) {
        box_portal_light_t light = {
            .position = { center[0], center[1], center[2] },
            .range = range,
            .area = box_portal_light_area(s->index, scene, center)
        };

        result = box_portal_light_reaches(s->index, &s->apertures, &light, v->seen);
    }

    return result;
}

void box_render_portal_lights_tag(
    const box_render_portal_lights_t *s,
    ecs_entity_t scene,
    ecs_vec_t *lights)
{
    int32_t i, count = ecs_vec_count(lights);
    bool areas = s->index && box_portal_index_has(s->index, scene);

    for (i = 0; i < count; i ++) {
        box_render_light_t *l = ecs_vec_get_t(lights, box_render_light_t, i);
        l->area = 0;
        if (areas && l->range > 0) {
            l->area = (uint32_t)box_portal_light_area(s->index, scene, l->position) + 1u;
        }
    }
}

/** Fill the scratch lights with the lights of a view as the portals see them.
 *
 * @param s The state
 * @param lights box_render_light_t, the lights of the view, NULL for none
 */
static void box_render_portal_lights_fill(
    box_render_portal_lights_t *s,
    const ecs_vec_t *lights)
{
    int32_t i, count = lights ? ecs_vec_count(lights) : 0;

    ecs_vec_set_count_t(NULL, &s->lights, box_portal_light_t, count);
    for (i = 0; i < count; i ++) {
        const box_render_light_t *l = ecs_vec_get_t(lights, box_render_light_t, i);
        box_portal_light_t *out = ecs_vec_get_t(&s->lights, box_portal_light_t, i);

        glm_vec3_copy(ECS_CONST_CAST(float*, l->position), out->position);
        out->range = l->range;
        out->intensity = glm_max(l->color[0], glm_max(l->color[1], l->color[2]));
        out->area = l->area ? (int32_t)l->area - 1 : -1;
    }
}

void box_render_portal_lights_view(
    ecs_world_t *world,
    box_render_portal_lights_t *s,
    box_render_packet_t *p,
    int32_t view,
    const ecs_vec_t *lights,
    const float *sun)
{
    box_render_view_t *v = ecs_vec_get_t(&p->views, box_render_view_t, view);
    const box_render_portal_view_t *pv = s->index && view < ecs_vec_count(&s->views)
        ? ecs_vec_get_t(&s->views, box_render_portal_view_t, view)
        : NULL;

    v->area_first = ecs_vec_count(&p->areas);
    v->area_count = 0;
    if (pv && !v->camera_data.ortho && box_portal_index_has(s->index, v->scene)) {
        box_portal_shade_desc_t desc;

        box_render_portal_lights_fill(s, lights);
        desc = (box_portal_shade_desc_t){
            .index = s->index,
            .scene = v->scene,
            .vis = pv->walked ? &pv->vis : NULL,
            .apertures = &s->apertures,
            .lights = ecs_vec_first_t(&s->lights, box_portal_light_t),
            .light_count = ecs_vec_count(&s->lights),
            .sun = sun,
            .eye = v->camera_transform[3]
        };

        if (box_portal_shade_pack(world, &desc, &p->areas, BOX_PORTAL_SHADE_MAX)) {
            v->area_count = ecs_vec_count(&p->areas) - v->area_first;
        }
    }
}
