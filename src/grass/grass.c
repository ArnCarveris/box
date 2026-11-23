/**
 * @file grass/grass.c
 * @brief Registers the box.grass components, makes the terrains of Grass entities sampled on
 * the GPU and hands the Grass entities to the render extract.
 */

#define BOX_GRASS_IMPL
#include "grass.h"
#include <math.h>

/** Default distances where the levels of detail end */
static const float box_grass_lod[BOX_GRASS_LODS] = { 20.0f, 60.0f, 200.0f };

/** Construct Grass values: a dense field of short green blades on the plane of the entity,
 * without a mask.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_grass_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Grass *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Grass){
            .mask = -1,
            .density = BOX_GRASS_DENSITY,
            .min_height = 0.25f,
            .max_height = 0.6f,
            .fold_height = BOX_GRASS_FOLD_HEIGHT,
            .min_width = 0.03f,
            .max_width = 0.06f,
            .base_color = {38, 66, 18},
            .tip_color = {150, 175, 70},
            .clump_size = BOX_GRASS_CLUMP_SIZE,
            .clump_follow = BOX_GRASS_CLUMP_FOLLOW,
            .variation = BOX_GRASS_VARIATION,
            .lod = { box_grass_lod[0], box_grass_lod[1], box_grass_lod[2] },
            .shadow_distance = BOX_GRASS_SHADOW_DISTANCE
        };
    }
}

/** Get a positive and finite value, or a default.
 *
 * @param v The value
 * @param fallback The default
 * @return v when it is positive and finite, else fallback
 */
static float box_grass_positive(
    float v,
    float fallback)
{
    return isfinite(v) && v > 0.0f ? v : fallback;
}

/** Get a finite value clamped to 0 to 1, or a default.
 *
 * @param v The value
 * @param fallback The default
 * @return v clamped to 0 to 1 when it is finite, else fallback
 */
static float box_grass_unit(
    float v,
    float fallback)
{
    return !isfinite(v) ? fallback : v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

void box_grass_resolve(
    const Grass *value,
    Grass *out)
{
    int32_t i;
    *out = *value;
    out->density = box_grass_positive(value->density, BOX_GRASS_DENSITY);
    out->density = out->density > BOX_GRASS_DENSITY_MAX ? BOX_GRASS_DENSITY_MAX : out->density;
    out->min_height = box_grass_positive(value->min_height, 0.25f);
    out->max_height = box_grass_positive(value->max_height, 0.6f);
    out->max_height = out->max_height < out->min_height ? out->min_height : out->max_height;
    out->fold_height = !isfinite(value->fold_height) ? BOX_GRASS_FOLD_HEIGHT :
        value->fold_height < 0.0f ? 0.0f : value->fold_height;
    out->min_width = box_grass_positive(value->min_width, 0.03f);
    out->max_width = box_grass_positive(value->max_width, 0.06f);
    out->max_width = out->max_width < out->min_width ? out->min_width : out->max_width;
    out->clump_size = box_grass_positive(value->clump_size, BOX_GRASS_CLUMP_SIZE);
    out->clump_follow = box_grass_unit(value->clump_follow, BOX_GRASS_CLUMP_FOLLOW);
    out->variation = box_grass_unit(value->variation, BOX_GRASS_VARIATION);
    out->mask = value->mask >= 0 && value->mask < 4 ? value->mask : -1;
    for (i = 0; i < BOX_GRASS_LODS; i ++) {
        float d = box_grass_positive(value->lod[i], box_grass_lod[i]);
        float before = i ? out->lod[i - 1] : 0.0f;
        d = d < before ? before : d;
        out->lod[i] = d > BOX_GRASS_DISTANCE_MAX ? BOX_GRASS_DISTANCE_MAX : d;
    }

    out->shadow_distance = !isfinite(value->shadow_distance) ? BOX_GRASS_SHADOW_DISTANCE :
        value->shadow_distance < 0.0f ? 0.0f : value->shadow_distance;
    out->shadow_distance = out->shadow_distance > out->lod[2] ? out->lod[2] :
        out->shadow_distance;
}

/** Make the terrain of a Grass sampled on the GPU, which its compute pass reads.
 *
 * @param it The iterator of the observer
 */
static void BoxGrassOnSet(
    ecs_iter_t *it)
{
    const Grass *grass = ecs_field(it, Grass, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t terrain = grass[i].terrain;
        if (terrain && ecs_is_alive(it->world, terrain) &&
            ecs_has(it->world, terrain, Terrain) &&
            !ecs_has_id(it->world, terrain, TerrainSampled))
        {
            ecs_add_id(it->world, terrain, TerrainSampled);
        }
    }
}

void BoxGrassImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxGrass);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxTerrain);
    ECS_IMPORT(world, BoxRender);

    ECS_META_COMPONENT(world, Grass);
    ecs_set_hooks(world, Grass, { .ctor = box_grass_ctor });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OnSet" }),
        .query.terms = {{ .id = ecs_id(Grass) }},
        .events = { EcsOnSet },
        .callback = BoxGrassOnSet
    });

    box_render_grass_init(world);
}
