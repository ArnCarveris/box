/**
 * @file render/shadow_atlas.c
 * @brief Tile allocation of the shadow atlases and the projections of spot and point light tiles.
 *
 * Lights get power of two tiles nearest first; the tiles are placed by size and entity in
 * quadtree order, so where a tile lies depends only on the set of lights and their sizes. Spot
 * and point lights share the allocation, one tile each.
 */

#include "render.h"
#include <math.h>

/** Edge of a shadow atlas without ShadowAtlas */
#define BOX_SHADOW_ATLAS_DEFAULT (2048)

/** Smallest tile of a shadow atlas without ShadowAtlas */
#define BOX_SHADOW_ATLAS_MIN_TILE (64)

/** Above this vertical component a spot light tile takes +Z as its up axis */
#define BOX_SHADOW_SPOT_POLE (0.99f)

/** Tangent of the half field of view of an octant of a point light tile, sqrt(6) */
#define BOX_SHADOW_OCTAHEDRAL_TANGENT (2.4494897427831781f)

/** Round a value down to a power of two.
 *
 * @param value The value
 * @return The largest power of two not above value, 0 for a value below 1
 */
static int32_t box_shadow_pow2(
    int32_t value)
{
    int32_t result = 0;
    if (value >= 1) {
        result = 1;
        while (result <= value / 2) {
            result <<= 1;
        }
    }

    return result;
}

/** Clamp an integer to a range.
 *
 * @param value The value
 * @param lo The lower bound
 * @param hi The upper bound
 * @return The clamped value
 */
static int32_t box_shadow_clamp(
    int32_t value,
    int32_t lo,
    int32_t hi)
{
    return value < lo ? lo : (value > hi ? hi : value);
}

void box_render_shadow_atlas_settings(
    const ShadowAtlas *in,
    ShadowAtlas *out)
{
    ShadowAtlas value = in ? *in : (ShadowAtlas){
        .resolution = BOX_SHADOW_ATLAS_DEFAULT,
        .min_tile = BOX_SHADOW_ATLAS_MIN_TILE
    };
    out->resolution = box_shadow_pow2(box_shadow_clamp(value.resolution,
        BOX_RENDER_SHADOW_ATLAS_MIN, BOX_RENDER_SHADOW_ATLAS_MAX));
    out->min_tile = box_shadow_pow2(box_shadow_clamp(value.min_tile, 1, out->resolution));
}

int32_t box_render_shadow_request_size(
    int32_t resolution,
    const ShadowAtlas *atlas)
{
    return box_shadow_clamp(box_shadow_pow2(resolution), atlas->min_tile, atlas->resolution);
}

/** Check whether a request comes before another in priority: nearer, then larger, then the
 * lower entity.
 *
 * @param a The first request
 * @param b The second request
 * @return Whether a comes first
 */
static bool box_shadow_before(
    const box_render_shadow_request_t *a,
    const box_render_shadow_request_t *b)
{
    bool result;
    if (a->distance != b->distance) {
        result = a->distance < b->distance;
    } else if (a->size != b->size) {
        result = a->size > b->size;
    } else {
        result = a->light < b->light;
    }

    return result;
}

/** Check whether a tile is placed before another: larger, then of the lower entity.
 *
 * @param a The light of the first tile
 * @param sa The edge of the first tile
 * @param b The light of the second tile
 * @param sb The edge of the second tile
 * @return Whether the first tile is placed first
 */
static bool box_shadow_placed_before(
    ecs_entity_t a,
    int32_t sa,
    ecs_entity_t b,
    int32_t sb)
{
    return sa != sb ? sa > sb : a < b;
}

/** Get the coordinate in the even bits of a Morton index.
 *
 * @param index The index
 * @return The coordinate
 */
static int32_t box_shadow_morton(
    int64_t index)
{
    int32_t result = 0, bit;
    for (bit = 0; bit < 31; bit ++) {
        result |= (int32_t)((index >> (2 * bit)) & 1) << bit;
    }

    return result;
}

int32_t box_render_shadow_allocate(
    const box_render_shadow_request_t *requests,
    int32_t count,
    const ShadowAtlas *atlas,
    box_render_shadow_place_t *out,
    int32_t *dropped)
{
    int32_t *order = count > 0 ? ecs_os_malloc_n(int32_t, count) : NULL;
    int64_t left = (int64_t)atlas->resolution * atlas->resolution, cursor = 0;
    int32_t i, j, tiles = 0, lost = 0, min_tile = glm_max(atlas->min_tile, 1);
    for (i = 0; i < count; i ++) {
        out[i] = (box_render_shadow_place_t){0};
        for (j = i; j > 0 && box_shadow_before(&requests[i], &requests[order[j - 1]]); j --) {
            order[j] = order[j - 1];
        }

        order[j] = i;
    }

    for (i = 0; i < count; i ++) {
        const box_render_shadow_request_t *r = &requests[order[i]];
        int32_t size = box_shadow_pow2(glm_min(glm_max(r->size, min_tile), atlas->resolution));
        while (size >= min_tile && (int64_t)size * size > left) {
            size >>= 1;
        }

        if (tiles >= BOX_RENDER_SHADOW_TILES || size < min_tile) {
            lost ++;
            continue;
        }

        out[order[i]].size = size;
        left -= (int64_t)size * size;
        tiles ++;
    }

    for (i = 0, j = 0; i < count; i ++) {
        int32_t k;
        if (!out[i].size) {
            continue;
        }

        for (k = j; k > 0 && box_shadow_placed_before(requests[i].light, out[i].size,
            requests[order[k - 1]].light, out[order[k - 1]].size); k --)
        {
            order[k] = order[k - 1];
        }

        order[k] = i;
        j ++;
    }

    for (i = 0; i < tiles; i ++) {
        box_render_shadow_place_t *place = &out[order[i]];
        int64_t area = (int64_t)place->size * place->size;
        int64_t cell = cursor / area;
        place->x = box_shadow_morton(cell) * place->size;
        place->y = box_shadow_morton(cell >> 1) * place->size;
        cursor += area;
    }

    if (dropped) {
        *dropped = lost;
    }

    ecs_os_free(order);

    return tiles;
}

float box_render_shadow_near(
    float range)
{
    return glm_max(range * BOX_RENDER_SHADOW_NEAR_FRACTION, BOX_RENDER_SHADOW_NEAR_MIN);
}

float box_render_shadow_far(
    float range)
{
    return glm_max(range, box_render_shadow_near(range) * 2.0f);
}

void box_render_shadow_spot(
    const float *position,
    const float *direction,
    float range,
    float outer,
    const box_render_shadow_place_t *place,
    int32_t resolution,
    box_render_shadow_record_t *out)
{
    vec3 forward, right, up, pole = {0, 1, 0};
    float near_ = box_render_shadow_near(range);
    float far_ = box_render_shadow_far(range);
    float size = (float)glm_max(place->size, 1);
    float margin = 0.5f * (float)BOX_RENDER_SHADOW_FILTER_MAX + 1.0f;
    float widen = size > 4.0f * margin ? size / (size - 2.0f * margin) : 2.0f;
    float tangent = tanf(glm_rad(glm_clamp(outer, 0.01f, 89.9f))) * widen;
    float res = (float)glm_max(resolution, 1);
    int32_t i;
    tangent = glm_min(tangent, tanf(glm_rad(BOX_RENDER_SHADOW_SPOT_ANGLE_MAX)));
    glm_vec3_copy(ECS_CONST_CAST(float*, direction), forward);
    if (glm_vec3_norm(forward) < 1e-6f) {
        glm_vec3_copy((vec3){0, 0, 1}, forward);
    }

    glm_vec3_normalize(forward);
    if (fabsf(forward[1]) > BOX_SHADOW_SPOT_POLE) {
        glm_vec3_copy((vec3){0, 0, 1}, pole);
    }

    glm_vec3_crossn(forward, pole, right);
    glm_vec3_cross(right, forward, up);
    float px = glm_vec3_dot(right, ECS_CONST_CAST(float*, position));
    float py = glm_vec3_dot(up, ECS_CONST_CAST(float*, position));
    float pz = glm_vec3_dot(forward, ECS_CONST_CAST(float*, position));
    float depth = near_ / (far_ - near_);
    glm_mat4_zero(out->matrix);
    for (i = 0; i < 3; i ++) {
        out->matrix[i][0] = right[i] / tangent;
        out->matrix[i][1] = up[i] / tangent;
        out->matrix[i][2] = -forward[i] * depth;
        out->matrix[i][3] = forward[i];
    }

    out->matrix[3][0] = -px / tangent;
    out->matrix[3][1] = -py / tangent;
    out->matrix[3][2] = (far_ + pz) * depth;
    out->matrix[3][3] = -pz;
    out->rect[0] = (float)place->x / res;
    out->rect[1] = (float)place->y / res;
    out->rect[2] = size / res;
    out->rect[3] = 1.0f / res;
    out->params[0] = near_;
    out->params[1] = far_;
    out->params[2] = 2.0f * tangent / size;
    out->params[3] = size;
    for (i = 0; i < 3; i ++) {
        out->light[i] = position[i];
    }

    out->light[3] = (float)BOX_RENDER_SHADOW_PERSPECTIVE;
}

void box_render_shadow_point(
    const float *position,
    float range,
    const box_render_shadow_place_t *place,
    int32_t resolution,
    box_render_shadow_record_t *out)
{
    float size = (float)glm_max(place->size, 1);
    float res = (float)glm_max(resolution, 1);
    int32_t i;
    glm_mat4_zero(out->matrix);
    out->rect[0] = (float)place->x / res;
    out->rect[1] = (float)place->y / res;
    out->rect[2] = size / res;
    out->rect[3] = 1.0f / res;
    out->params[0] = box_render_shadow_near(range);
    out->params[1] = box_render_shadow_far(range);
    out->params[2] = 2.0f * BOX_SHADOW_OCTAHEDRAL_TANGENT / size;
    out->params[3] = size;
    for (i = 0; i < 3; i ++) {
        out->light[i] = position[i];
    }

    out->light[3] = (float)BOX_RENDER_SHADOW_OCTAHEDRAL;
}

void box_render_shadow_point_planes(
    const box_render_shadow_tile_t *tile,
    vec4 *out)
{
    int32_t i;
    for (i = 0; i < 6; i ++) {
        float sign = (i & 1) ? -1.0f : 1.0f;
        glm_vec4_zero(out[i]);
        out[i][i / 2] = sign;
        out[i][3] = tile->far_ - sign * tile->position[i / 2];
    }
}
