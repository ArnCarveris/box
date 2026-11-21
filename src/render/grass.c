/**
 * @file render/grass.c
 * @brief Extract of the Grass entities: their values with the defaults resolved, their colors
 * linear, their ground in the local frame and the Wind of the frame, appended to the packet for
 * the backend, which places and draws their blades in the lit views of their scene
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

/** Fill the ground of a Grass on a terrain: the local y range of its heights.
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
    double local[3];
    float low = BOX_RENDER_GRASS_MIN_HEIGHT, high = BOX_RENDER_GRASS_MAX_HEIGHT;
    if (t) {
        box_terrain_local(world, terrain, local);
        if (t->max_height > t->min_height) {
            low = t->min_height;
            high = t->max_height;
        } else if (t->max_height > 0.0f) {
            high = t->max_height;
        }

        out->ground[0] = (float)local[1] + low;
        out->ground[1] = (float)local[1] + high;
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
        out.plane = t ? t->m[3][1] : 0.0f;
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

bool box_render_grass_use(
    const box_render_packet_t *p,
    uint32_t handle)
{
    return (handle == BOX_RENDER_SHADER_GRASS_PLACE || handle == BOX_RENDER_SHADER_GRASS) &&
        ecs_vec_count(&p->grasses) != 0;
}
