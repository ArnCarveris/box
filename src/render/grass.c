/**
 * @file render/grass.c
 * @brief Extract of the Grass entities: their values with the defaults resolved, their colors
 * linear, their ground relative to the render origin and the Wind of the frame, appended to the
 * packet for the backend, which places and draws their blades in the lit views of their scene
 * (gpu_grass.c).
 */

#include "render.h"
#include "../grass/grass.h"
#include "../terrain/terrain.h"
#include <math.h>

/** Lowest height of a Terrain whose heights have no range */
#define BOX_RENDER_GRASS_MIN_HEIGHT (-256.0f)

/** Highest height of a Terrain whose heights have no range */
#define BOX_RENDER_GRASS_MAX_HEIGHT (1024.0f)

/** Convert a display encoded channel to linear.
 *
 * @param v The channel, 0 to 255
 * @return The linear value, 0 to 1
 */
static float box_render_grass_linear(
    uint8_t v)
{
    float c = (float)v / 255.0f;

    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

/** Fill the ground of a Grass on a terrain: the y range of its heights relative to the render
 * origin.
 *
 * @param world The world
 * @param terrain The terrain
 * @param out The grass
 * @return Whether the terrain is a Terrain
 */
static bool box_render_grass_terrain(
    const ecs_world_t *world,
    ecs_entity_t terrain,
    box_render_grass_t *out)
{
    const Terrain *t = ecs_is_alive(world, terrain) ? ecs_get(world, terrain, Terrain) : NULL;
    double position[3], origin[3];
    float low = BOX_RENDER_GRASS_MIN_HEIGHT, high = BOX_RENDER_GRASS_MAX_HEIGHT;
    if (t) {
        box_terrain_position(world, terrain, position);
        box_render_origin(world, origin);
        if (t->max_height > t->min_height) {
            low = t->min_height;
            high = t->max_height;
        } else if (t->max_height > 0.0f) {
            high = t->max_height;
        }

        out->ground[0] = (float)(position[1] - origin[1]) + low;
        out->ground[1] = (float)(position[1] - origin[1]) + high;
    }

    return t != NULL;
}

/** Fill the wind of a Grass from the Wind of the frame.
 *
 * @param wind The Wind of the frame (box_render_wind_frame)
 * @param out The grass
 */
static void box_render_grass_wind(
    const box_render_wind_t *wind,
    box_render_grass_t *out)
{
    double phase = isfinite(wind->clock) ? fmod(wind->clock * BOX_RENDER_GRASS_WIND_RATE, 1.0)
        : 0.0;
    phase = phase < 0.0 ? phase + 1.0 : phase;
    out->wind_direction[0] = wind->direction[0];
    out->wind_direction[1] = wind->direction[1];
    out->wind_offset[0] = wind->offset[0];
    out->wind_offset[1] = wind->offset[1];
    out->wind_speed = wind->speed;
    out->wind_gust = wind->gust;
    out->wind_frequency = wind->frequency;
    out->wind_sway = wind->sway;
    out->wind_phase = (float)phase < 1.0f ? (float)phase : 0.0f;
}

/** Append a Grass to the packet.
 *
 * @param world The world
 * @param p The packet
 * @param e The Grass entity
 * @param value The Grass
 * @param wind The Wind of the frame
 */
static void box_render_grass(
    ecs_world_t *world,
    box_render_packet_t *p,
    ecs_entity_t e,
    const Grass *value,
    const box_render_wind_t *wind)
{
    Grass g;
    WorldTransform buf;
    box_render_grass_t out;
    int32_t i;
    box_grass_resolve(value, &g);
    ecs_os_memset_t(&out, 0, box_render_grass_t);
    out.grass = e;
    out.terrain = g.terrain;
    out.scene = box_render_scene_of(world, e);
    if (g.terrain) {
        if (!box_render_grass_terrain(world, g.terrain, &out)) {
            goto done;
        }
    } else {
        const WorldTransform *t = box_transform_world(world, e, &buf);
        out.plane = t ? (float)(t->position[1] - p->origin[1]) : (float)-p->origin[1];
        out.ground[0] = out.plane;
        out.ground[1] = out.plane;
    }

    out.mask = g.mask;
    out.density = g.density;
    out.height[0] = g.min_height;
    out.height[1] = g.max_height;
    out.fold = g.fold_height;
    out.width[0] = g.min_width;
    out.width[1] = g.max_width;
    out.base[0] = box_render_grass_linear(g.base_color.r);
    out.base[1] = box_render_grass_linear(g.base_color.g);
    out.base[2] = box_render_grass_linear(g.base_color.b);
    out.tip[0] = box_render_grass_linear(g.tip_color.r);
    out.tip[1] = box_render_grass_linear(g.tip_color.g);
    out.tip[2] = box_render_grass_linear(g.tip_color.b);
    out.clump[0] = g.clump_size;
    out.clump[1] = g.clump_follow;
    out.variation = g.variation;
    for (i = 0; i < BOX_GRASS_LODS; i ++) {
        out.lod[i] = g.lod[i];
    }

    out.seed = g.seed;
    out.shadow = g.shadow_distance;
    out.shadow_lights = g.shadow_lights && g.shadow_distance > 0.0f;
    box_render_grass_wind(wind, &out);
    *ecs_vec_append_t(NULL, &p->grasses, box_render_grass_t) = out;
done:
    return;
}

/** Append every Grass to the packet.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractGrass(
    ecs_iter_t *it)
{
    const Grass *grass = ecs_field(it, Grass, 0);
    box_render_packet_t *p = box_render_packet(it->world);
    box_render_wind_t wind;
    int32_t i;
    box_render_wind_frame(it->world, &wind);
    for (i = 0; i < it->count; i ++) {
        box_render_grass(it->world, p, it->entities[i], &grass[i], &wind);
    }
}

void box_render_grass_init(
    ecs_world_t *world)
{
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "extract.Grass" }),
        .phase = RenderExtract,
        .query.terms = {{ .id = ecs_id(Grass), .inout = EcsIn }},
        .callback = BoxRenderExtractGrass
    });
}

void box_render_grass_far(
    const box_render_grass_t *g,
    box_render_grass_far_t *out)
{
    float width = 0.5f * (g->width[0] + g->width[1]);
    float height = 0.5f * (g->height[0] + g->height[1]);
    float cover = 1.0f - expf(-g->density * width * height * BOX_RENDER_GRASS_FAR_AREA);
    float full = g->lod[2] * BOX_RENDER_GRASS_FAR_FULL;
    float start = g->lod[0] > full ? full : g->lod[0];
    cover = cover < BOX_RENDER_GRASS_FAR_COVER_MIN ? BOX_RENDER_GRASS_FAR_COVER_MIN : cover;
    cover = cover > BOX_RENDER_GRASS_FAR_COVER_MAX ? BOX_RENDER_GRASS_FAR_COVER_MAX : cover;
    ecs_os_memset_t(out, 0, box_render_grass_far_t);
    ecs_os_memcpy_n(out->base, g->base, float, 3);
    out->base[3] = g->variation;
    ecs_os_memcpy_n(out->tip, g->tip, float, 3);
    out->tip[3] = cover;
    out->range[0] = start;
    out->range[1] = full;
    out->range[2] = g->lod[2];
    out->range[3] = g->height[1];
    out->wind_field[0] = g->wind_direction[0];
    out->wind_field[1] = g->wind_direction[1];
    out->wind_field[2] = g->wind_offset[0];
    out->wind_field[3] = g->wind_offset[1];
    out->wind_strength[0] = g->wind_speed;
    out->wind_strength[1] = g->wind_gust;
    out->wind_strength[2] = g->wind_frequency;
    out->wind_strength[3] = g->wind_sway;
    out->wind_time[0] = g->wind_phase;
    out->config[0] = g->mask >= 0 ? (uint32_t)g->mask + 1u : 0u;
    out->config[1] = 1u;
}

bool box_render_grass_far_of(
    const box_render_packet_t *p,
    uint32_t mesh,
    box_render_grass_far_t *out)
{
    ecs_entity_t terrain = 0;
    bool result = false;
    int32_t i, count = ecs_vec_count(&p->terrain_samples);
    ecs_os_memset_t(out, 0, box_render_grass_far_t);
    for (i = 0; mesh && i < count && !terrain; i ++) {
        const box_render_terrain_sample_t *s = ecs_vec_get_t(&p->terrain_samples,
            box_render_terrain_sample_t, i);
        terrain = s->mesh == mesh ? s->terrain : 0;
    }

    count = terrain ? ecs_vec_count(&p->grasses) : 0;
    for (i = 0; i < count && !result; i ++) {
        const box_render_grass_t *g = ecs_vec_get_t(&p->grasses, box_render_grass_t, i);
        if (g->terrain == terrain) {
            box_render_grass_far(g, out);
            result = true;
        }
    }

    return result;
}

uint32_t box_render_grass_cascades(
    const box_render_grass_t *g,
    const box_render_view_shadows_t *s)
{
    uint32_t result = 0;
    int32_t i, n = s->cascades;
    if (!s->enabled || !(g->shadow > 0.0f) || n < 1 || n > BOX_RENDER_SHADOW_CASCADES) {
        goto done;
    }

    for (i = 0; i < n; i ++) {
        float start = i ? s->splits[i - 1] : s->near_;
        if (start < g->shadow) {
            result |= 1u << i;
        }
    }

done:
    return result;
}

float box_render_grass_shadow_fade(
    const box_render_grass_t *g)
{
    return g->shadow > 0.0f ? g->shadow * BOX_RENDER_GRASS_SHADOW_FADE : 0.0f;
}

float box_render_grass_shadow_thin(
    const box_render_grass_t *g)
{
    float fade = box_render_grass_shadow_fade(g);

    return fade < g->lod[0] ? fade : g->lod[0];
}

float box_render_grass_shadow_reach(
    const box_render_grass_t *g,
    const float direction[3])
{
    float up = isfinite(direction[1]) ? direction[1] : 1.0f;
    float result = 0.0f;
    if (!(g->shadow > 0.0f)) {
        goto done;
    }

    up = up < BOX_RENDER_GRASS_SHADOW_LOW ? BOX_RENDER_GRASS_SHADOW_LOW : up;
    result = BOX_RENDER_GRASS_SHADOW_SLACK * g->height[1] / up;
    result = result > g->shadow ? g->shadow : result;
done:
    return result;
}

bool box_render_grass_use(
    const box_render_packet_t *p,
    uint32_t handle)
{
    return (handle == BOX_RENDER_SHADER_GRASS_PLACE || handle == BOX_RENDER_SHADER_GRASS) &&
        ecs_vec_count(&p->grasses) != 0;
}
