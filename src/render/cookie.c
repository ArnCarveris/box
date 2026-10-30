/**
 * @file render/cookie.c
 * @brief Resolves the LightCookie of the first directional light of a view for the backend.
 *
 * The offset the velocity moves the pattern by is computed from the total time in double
 * precision and wrapped to the size, so the uv the shader computes stays precise after hours.
 */

#include "render.h"
#include "../texture/texture.h"
#include <math.h>

void box_render_cookie_offset(
    const LightCookie *cookie,
    double time,
    float *out)
{
    double size = cookie->size > 0 ? (double)cookie->size : 1.0;
    int32_t i;
    for (i = 0; i < 2; i ++) {
        double v = fmod((double)cookie->offset[i] + (double)cookie->velocity[i] * time, size);
        out[i] = (float)(v < 0 ? v + size : v);
        if (out[i] >= (float)size) {
            out[i] = 0.0f;
        }
    }
}

void box_render_cookie_resolve(
    const ecs_world_t *world,
    ecs_entity_t light,
    double time,
    box_render_view_cookie_t *out)
{
    const LightCookie *c = light && ecs_is_alive(world, light)
        ? ecs_get(world, light, LightCookie)
        : NULL;
    const WorldTransform *transform = c ? ecs_get(world, light, WorldTransform) : NULL;
    uint32_t handle = c && c->texture && c->size > 0 && c->strength != 0
        ? box_textures_handle(box_textures_get(world), c->texture)
        : 0;
    ecs_os_memset_t(out, 0, box_render_view_cookie_t);
    if (!handle) {
        goto done;
    }

    out->texture = handle;
    out->projection = (uint32_t)c->projection;
    out->inv_size = 1.0f / c->size;
    out->height = c->height;
    out->threshold = c->threshold;
    out->softness = c->softness;
    out->strength = c->strength;
    box_render_cookie_offset(c, time, out->offset);
    glm_vec3_copy((vec3){1, 0, 0}, out->axis_x);
    glm_vec3_copy((vec3){0, 1, 0}, out->axis_y);
    if (transform) {
        glm_vec3_copy(ECS_CONST_CAST(float*, transform->m[0]), out->axis_x);
        glm_vec3_copy(ECS_CONST_CAST(float*, transform->m[1]), out->axis_y);
        glm_vec3_normalize(out->axis_x);
        glm_vec3_normalize(out->axis_y);
    }

done:
    return;
}
