/**
 * @file look.c
 * @brief The look: the render quality tier of OreQuality applied to the surfaces and the world
 * view, the script function oreelse.cycleQuality() the pause screen's button calls, and the
 * script function oreelse.srgb() of the colors computed in script.
 *
 * The settings of every tier are cfg.quality (etc/config.flecs). The tier starts High at every
 * start of the app (oreelse.flecs) and a restart keeps it.
 *
 * The original reads a color byte as linear light, box decodes it from sRGB, so the port's colors
 * are the sRGB encoding of the original's (README.md, Colors); a color computed in script
 * computes the original's bytes and encodes them with srgb().
 */

#include "game.h"
#include <math.h>

/** Set a float when it differs.
 *
 * @param value The value
 * @param next The value to set
 * @return Whether the value changed
 */
static bool ore_look_float(
    float *value,
    float next)
{
    bool result = *value != next;
    *value = next;

    return result;
}

/** Set an integer when it differs.
 *
 * @param value The value
 * @param next The value to set
 * @return Whether the value changed
 */
static bool ore_look_i32(
    int32_t *value,
    int32_t next)
{
    bool result = *value != next;
    *value = next;

    return result;
}

/** Turn the entries of an effect in the effects of a view on or off.
 *
 * @param view The view
 * @param effect The effect entity, 0 for none
 * @param enabled Whether the effect runs
 * @return Whether an entry changed
 */
static bool ore_look_effect(
    View *view,
    ecs_entity_t effect,
    bool enabled)
{
    ViewEffect *effects = ecs_vec_first_t(&view->effects, ViewEffect);
    int32_t i, count = ecs_vec_count(&view->effects);
    bool result = false;
    for (i = 0; effect && i < count; i ++) {
        if (effects[i].effect == effect && effects[i].enabled != enabled) {
            effects[i].enabled = enabled;
            result = true;
        }
    }

    return result;
}

/** Apply the tier of OreQuality: the resolution scale of every surface that does not render
 * into a texture, the shadow map resolution and distance of the world view and whether its Gtao
 * and Bloom run.
 *
 * It runs every frame in every state, the pause screen included, and writes only what differs
 * from the tier, so it also covers surfaces and views set after a change.
 *
 * @param it Iterator with the OreQuality and OreLook singletons
 */
static void OreQualityApply(
    ecs_iter_t *it)
{
    const OreQuality *quality = ecs_field(it, OreQuality, 0);
    const OreLook *look = ecs_field(it, OreLook, 1);
    OreQualityTiers tiers = ecs_const_var_get_t(it->world, "cfg.quality", OreQualityTiers);
    int32_t i, tier = (int32_t)quality->tier;
    View *view = NULL;
    bool changed = false;
    ecs_iter_t surfaces = ecs_each_id(it->world, ecs_id(Surface));
    tier = tier < 0 ? 0 : tier;
    tier = tier >= ORE_QUALITY_TIERS ? ORE_QUALITY_TIERS - 1 : tier;

    while (ecs_each_next(&surfaces)) {
        Surface *s = ecs_field(&surfaces, Surface, 0);
        for (i = 0; i < surfaces.count; i ++) {
            if (!s[i].texture &&
                ore_look_float(&s[i].resolution_scale, tiers.resolution_scale[tier]))
            {
                ecs_modified(it->stage, surfaces.entities[i], Surface);
            }
        }
    }

    if (look->view && ecs_is_alive(it->world, look->view)) {
        view = ecs_get_mut(it->world, look->view, View);
    }

    if (view) {
        changed |= ore_look_i32(&view->shadows.resolution, tiers.shadow_resolution[tier]);
        changed |= ore_look_float(&view->shadows.max_distance, tiers.shadow_distance[tier]);
        changed |= ore_look_effect(view, look->gtao, tiers.gtao[tier]);
        changed |= ore_look_effect(view, look->bloom, tiers.bloom[tier]);
        if (changed) {
            ecs_modified(it->stage, look->view, View);
        }
    }
}

/** Cycle the render quality tier down, High to Medium to Low and back to High, as the
 * original's pause screen button does.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv No arguments
 * @param future Resolves with whether there was a tier to cycle
 */
static void ore_cycle_quality(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    ecs_world_t *world = ctx->world;
    OreQuality *quality = ecs_singleton_get_mut(world, OreQuality);
    int32_t tier = 0;
    (void)argc;
    (void)argv;

    if (quality) {
        tier = (int32_t)quality->tier;
        tier = tier < 0 || tier >= ORE_QUALITY_TIERS ? ORE_QUALITY_TIERS - 1 : tier;
        quality->tier = (OreRenderQuality)((tier + ORE_QUALITY_TIERS - 1) % ORE_QUALITY_TIERS);
        ecs_singleton_modified(world, OreQuality);
    }

    ore_resolve(future, quality != NULL);
}

/** The oreelse.srgb(v) script function: the byte value from 0 to 255 box decodes to the linear
 * light v / 255 of a color byte v of the original, not rounded: the conversion of
 * tools/linear_colors.py for the colors computed in script.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The linear byte value
 * @param result Receives the sRGB encoded byte value
 */
static void ore_srgb_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    double c = *(double*)argv[0].ptr / 255.0;
    (void)ctx;
    (void)argc;
    c = c < 0.0 ? 0.0 : (c > 1.0 ? 1.0 : c);
    c = c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
    *(double*)result->ptr = c * 255.0;
}

void ore_look_import(
    ecs_world_t *world)
{
    ecs_entity_t module = ecs_get_scope(world);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreQualityApply" }),
        .phase = EcsPreStore,
        .query.terms = {
            { .id = ecs_id(OreQuality), .src.id = ecs_id(OreQuality), .inout = EcsIn },
            { .id = ecs_id(OreLook), .src.id = ecs_id(OreLook), .inout = EcsIn }
        },
        .callback = OreQualityApply
    });

    ecs_async_function(world, {
        .name = "cycleQuality",
        .parent = module,
        .return_type = ecs_id(ecs_bool_t),
        .callback = ore_cycle_quality
    });

    ecs_function(world, {
        .name = "srgb",
        .parent = module,
        .return_type = ecs_id(ecs_f64_t),
        .params = {{ .name = "value", .type = ecs_id(ecs_f64_t) }},
        .callback = ore_srgb_function
    });

}
