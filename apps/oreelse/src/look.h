/**
 * @file look.h
 * @brief The look: the render quality tiers and the cloud shadows of the sun, their reflected
 * components and the import of src/look.c, included by game.h, which holds the rest of the
 * game's.
 */

#ifndef OREELSE_LOOK_H
#define OREELSE_LOOK_H

/** Render quality tiers, the type of OreQuality.tier. The pause screen's button cycles
 * them down, High to Medium to Low and back to High (oreelse.cycleQuality()).
 */
ECS_ENUM(OreRenderQuality, {
    OreRenderQualityLow,    /**< Half resolution, small and short shadows, no Gtao or Bloom */
    OreRenderQualityMedium, /**< Full resolution, smaller shadow map, no Gtao */
    OreRenderQualityHigh    /**< Full resolution and every effect, the tier at every start */
});

/** Number of render quality tiers, the size of the arrays of OreQualityTiers */
#define ORE_QUALITY_TIERS (3)

/** The render quality tier, a singleton oreelse.flecs sets to High. A restart keeps it.
 * Modified when cycleQuality() changes it.
 */
ECS_STRUCT(OreQuality, {
    OreRenderQuality tier; /**< The tier the look applies */
});

/** The settings of every render quality tier (cfg.quality), indexed by OreRenderQuality */
ECS_STRUCT(OreQualityTiers, {
    float resolution_scale[3];    /**< Surface.resolution_scale of the surfaces */
    int32_t shadow_resolution[3]; /**< ShadowSettings.resolution of the world view */
    float shadow_distance[3];     /**< ShadowSettings.max_distance of the world view */
    bool gtao[3];                 /**< The Gtao effect of the world view runs */
    bool bloom[3];                /**< The Bloom effect of the world view runs */
    bool cloud_shadows[3];        /**< The LightCookie of the sun casts the cloud shadows */
});

/** What the render quality tier applies to, a singleton oreelse.flecs sets: the world view and
 * its effects. Every surface that does not render into a texture takes the resolution scale.
 */
ECS_STRUCT(OreLook, {
    ecs_entity_t view;  /**< The world view, its shadows and effects follow the tier */
    ecs_entity_t gtao;  /**< Gtao effect of the world view */
    ecs_entity_t bloom; /**< Bloom effect of the world view */
});

/** The cloud shadows of the original, on the sun next to its LightCookie (oreelse.flecs).
 *
 * OreCloudShadowApply sets the cookie's strength to strength at the tiers with cloud_shadows in
 * cfg.quality and to 0 at the others, and its offset to velocity times OreClock.elapsed, so the
 * clouds drift with the game's clock and stand still while paused, as the original's did.
 */
ECS_STRUCT(OreCloudShadow, {
    float strength;    /**< LightCookie.strength where the tier has cloud shadows */
    float velocity[2]; /**< World units per second the drift moves the offset by, X and Z */
});

/** Register the look: the render quality tiers, the cloud shadows of the sun
 * (OreCloudShadow), oreelse.cycleQuality(), and oreelse.srgb(), which encodes a world color byte
 * of the original (linear light) for box.
 *
 * @param world The world
 */
void ore_look_import(
    ecs_world_t *world);

#endif
