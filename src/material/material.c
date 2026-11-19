/**
 * @file material/material.c
 * @brief Registers the components of the box.material module.
 */

#define BOX_MATERIAL_IMPL
#include <box.h>
#include <math.h>

ECS_TAG_DECLARE(DynamicMaterial);
ECS_TAG_DECLARE(TextureFrame);
ECS_TAG_DECLARE(NoShadow);
ECS_TAG_DECLARE(LightFixture);

/** Singleton through which box_material_ripple_at finds the queue of a world */
typedef struct MaterialRippleQueue {
    ecs_vec_t *ripples;     /**< box_material_ripple_t, freed with the world */
} MaterialRippleQueue;

static ECS_COMPONENT_DECLARE(MaterialRippleQueue);

/** Construct Opacity values that are fully opaque.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_opacity_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Opacity *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i].value = 1.0f;
    }
}

/** Construct MaterialId values that were never handed a slot.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_material_id_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    MaterialId *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i].value = UINT32_MAX;
    }
}

/** Construct AlphaCutoff values that discard below BOX_MATERIAL_CUTOFF.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_alpha_cutoff_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    AlphaCutoff *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i].value = BOX_MATERIAL_CUTOFF;
    }
}

/** Construct PbrMaterial values: the material of a shape without PbrMaterial, a matte dielectric.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_pbr_material_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    PbrMaterial *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (PbrMaterial){
            .metallic = BOX_MATERIAL_METALLIC, .roughness = BOX_MATERIAL_ROUGHNESS };
    }
}

/** Construct ClearCoat values: a full, smooth coat.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_clear_coat_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ClearCoat *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (ClearCoat){
            .strength = BOX_MATERIAL_CLEAR_COAT_STRENGTH,
            .roughness = BOX_MATERIAL_CLEAR_COAT_ROUGHNESS };
    }
}

/** Construct SpecularAnisotropy values: a fully stretched highlight along +U.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_specular_anisotropy_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    SpecularAnisotropy *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (SpecularAnisotropy){
            .strength = BOX_MATERIAL_ANISOTROPY_STRENGTH,
            .rotation = BOX_MATERIAL_ANISOTROPY_ROTATION };
    }
}

/** Construct Sheen values: a white sheen of medium roughness.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_sheen_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Sheen *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Sheen){
            .color = { 255, 255, 255 },
            .roughness = BOX_MATERIAL_SHEEN_ROUGHNESS };
    }
}

/** Construct DiffuseTransmission values: half of the diffuse light passes through, untinted,
 * a thin sheet, no distance aware blur.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_diffuse_transmission_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    DiffuseTransmission *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (DiffuseTransmission){
            .factor = BOX_MATERIAL_DIFFUSE_TRANSMISSION_FACTOR,
            .color = { 255, 255, 255 },
            .thickness = 0.0f,
            .blur = 0.0f };
    }
}

/** Construct Fresnel values: a white rim of strength 1 and power 5, no bias nor opacity.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_fresnel_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Fresnel *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Fresnel){
            .color = { 255, 255, 255 },
            .strength = BOX_MATERIAL_FRESNEL_STRENGTH,
            .power = BOX_MATERIAL_FRESNEL_POWER,
            .bias = BOX_MATERIAL_FRESNEL_BIAS,
            .opacity = BOX_MATERIAL_FRESNEL_OPACITY };
    }
}

/** Construct Transmission values of a surface without volume: no transmission, the index of
 * refraction of glass and no absorption.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_transmission_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Transmission *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Transmission){
            .ior = BOX_MATERIAL_IOR,
            .attenuation_distance = BOX_MATERIAL_ATTENUATION_DISTANCE,
            .attenuation_color = { 255, 255, 255 }
        };
    }
}

/** Construct Distortion values: a strength of 0.004, a scale of 1 and a speed of 0.5.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_distortion_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Distortion *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Distortion){
            .strength = BOX_MATERIAL_DISTORTION_STRENGTH,
            .scale = BOX_MATERIAL_DISTORTION_SCALE,
            .speed = BOX_MATERIAL_DISTORTION_SPEED };
    }
}

/** Construct WaterWaves values: no waves (every wavelength 0) and a detail of 1.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_water_waves_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    WaterWaves *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (WaterWaves){ .detail = BOX_MATERIAL_WATER_WAVES_DETAIL };
    }
}

/** Construct WaterCaustics values: a strength of 1, cells of 1 world unit, a speed of 0.5 and a
 * depth of 2.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_water_caustics_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    WaterCaustics *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (WaterCaustics){
            .strength = BOX_MATERIAL_WATER_CAUSTICS_STRENGTH,
            .scale = BOX_MATERIAL_WATER_CAUSTICS_SCALE,
            .speed = BOX_MATERIAL_WATER_CAUSTICS_SPEED,
            .depth = BOX_MATERIAL_WATER_CAUSTICS_DEPTH };
    }
}

/** Construct WaterRipples values: a field of 32 world units and 256 cells on the Water entity, a
 * wave speed of 2, a damping of 0.5, a strength of 1, a foam decay of 0.4, a foam spread of 0.02
 * and no crest foam.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_water_ripples_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    WaterRipples *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (WaterRipples){
            .size = BOX_MATERIAL_RIPPLES_SIZE,
            .resolution = BOX_MATERIAL_RIPPLES_RESOLUTION,
            .follow = 0,
            .wave_speed = BOX_MATERIAL_RIPPLES_WAVE_SPEED,
            .damping = BOX_MATERIAL_RIPPLES_DAMPING,
            .strength = BOX_MATERIAL_RIPPLES_STRENGTH,
            .foam_decay = BOX_MATERIAL_RIPPLES_FOAM_DECAY,
            .foam_spread = BOX_MATERIAL_RIPPLES_FOAM_SPREAD,
            .crest_foam = 0.0f };
    }
}

/** Construct RippleSource values: an ellipse of half size 0.5 by 0.5, a strength of 1 and no
 * foam.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_ripple_source_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    RippleSource *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (RippleSource){
            .extents = { BOX_MATERIAL_RIPPLE_SOURCE_EXTENT, BOX_MATERIAL_RIPPLE_SOURCE_EXTENT },
            .strength = BOX_MATERIAL_RIPPLE_SOURCE_STRENGTH,
            .foam = 0.0f };
    }
}

ecs_vec_t* box_material_ripples_queue(
    const ecs_world_t *world)
{
    const ecs_world_t *real = ecs_get_world(world);
    const MaterialRippleQueue *q = ecs_id(MaterialRippleQueue)
        ? ecs_singleton_get(real, MaterialRippleQueue) : NULL;

    return q ? q->ripples : NULL;
}

/** Queue a disturbance of the field of a Water after checking its arguments.
 *
 * @param world The world
 * @param what Name of the function for the errors
 * @param water Entity with Water and WaterRipples
 * @param position World position of the center
 * @param radius World units of the radius
 * @param strength World units the water at the center sinks
 * @param foam Foam added at the center
 * @return Whether it was queued
 */
static bool box_material_disturb(
    ecs_world_t *world,
    const char *what,
    ecs_entity_t water,
    const float *position,
    float radius,
    float strength,
    float foam)
{
    const ecs_world_t *real = ecs_get_world(world);
    ecs_vec_t *queue = box_material_ripples_queue(real);
    bool result = false;
    if (!queue) {
        goto done;
    }

    if (!water || !ecs_is_alive(real, water) || !ecs_has(real, water, WaterRipples) ||
        !ecs_has(real, water, Water))
    {
        char *name = water && ecs_is_alive(real, water) ? ecs_get_path(real, water) : NULL;
        ecs_err("%s: '%s' is not a Water with WaterRipples", what, name ? name : "0");
        ecs_os_free(name);
        goto done;
    }

    if (!position || !isfinite(position[0]) || !isfinite(position[1]) ||
        !isfinite(position[2]))
    {
        ecs_err("%s: the position is not finite", what);
        goto done;
    }

    if (!isfinite(radius) || radius <= 0.0f) {
        ecs_err("%s: the radius %g is not above 0", what, (double)radius);
        goto done;
    }

    if (!isfinite(strength) || !isfinite(foam)) {
        ecs_err("%s: the %s is not finite", what, isfinite(strength) ? "amount" : "strength");
        goto done;
    }

    box_material_ripple_t *r = ecs_vec_append_t(NULL, queue, box_material_ripple_t);
    r->water = water;
    r->position[0] = position[0];
    r->position[1] = position[1];
    r->position[2] = position[2];
    r->radius = radius;
    r->strength = strength;
    r->foam = foam;
    result = true;
done:
    return result;
}

bool box_material_ripple_at(
    ecs_world_t *world,
    ecs_entity_t water,
    const float *position,
    float radius,
    float strength)
{
    return box_material_disturb(world, "ripple_at", water, position, radius, strength, 0.0f);
}

bool box_material_foam_at(
    ecs_world_t *world,
    ecs_entity_t water,
    const float *position,
    float radius,
    float amount)
{
    return box_material_disturb(world, "foam_at", water, position, radius, 0.0f, amount);
}

/** The box.material.ripple_at script function.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The water, the position, the radius and the strength
 * @param result Receives whether the disturbance was queued
 */
static void box_material_ripple_at_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_entity_t water = *(ecs_entity_t*)argv[0].ptr;
    const float *position = argv[1].ptr;
    float radius = *(float*)argv[2].ptr;
    float strength = *(float*)argv[3].ptr;
    (void)argc;
    *(bool*)result->ptr = box_material_ripple_at(ctx->world, water, position, radius,
        strength);
}

/** The box.material.foam_at script function.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The water, the position, the radius and the amount
 * @param result Receives whether the splash was queued
 */
static void box_material_foam_at_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_entity_t water = *(ecs_entity_t*)argv[0].ptr;
    const float *position = argv[1].ptr;
    float radius = *(float*)argv[2].ptr;
    float amount = *(float*)argv[3].ptr;
    (void)argc;
    *(bool*)result->ptr = box_material_foam_at(ctx->world, water, position, radius, amount);
}

/** Free the ripple queue of a world.
 *
 * @param world The world
 * @param ctx The queue
 */
static void box_material_ripples_free(
    ecs_world_t *world,
    void *ctx)
{
    ecs_vec_t *queue = ctx;
    (void)world;
    ecs_vec_fini_t(NULL, queue, box_material_ripple_t);
    ecs_os_free(queue);
}

/** Construct Water values: a teal shallow and a dark blue deep body, depth 3, visibility 2, a
 * white absorption, a refraction of 0.02 and an off white foam band of width 0.3 without
 * texture or ripples.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_water_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Water *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Water){
            .shallow_color = { BOX_MATERIAL_WATER_SHALLOW_R, BOX_MATERIAL_WATER_SHALLOW_G,
                BOX_MATERIAL_WATER_SHALLOW_B },
            .deep_color = { BOX_MATERIAL_WATER_DEEP_R, BOX_MATERIAL_WATER_DEEP_G,
                BOX_MATERIAL_WATER_DEEP_B },
            .depth = BOX_MATERIAL_WATER_DEPTH,
            .visibility = BOX_MATERIAL_WATER_VISIBILITY,
            .absorption = { BOX_MATERIAL_WATER_ABSORPTION_R, BOX_MATERIAL_WATER_ABSORPTION_G,
                BOX_MATERIAL_WATER_ABSORPTION_B },
            .refraction = BOX_MATERIAL_WATER_REFRACTION,
            .foam_color = { BOX_MATERIAL_WATER_FOAM_R, BOX_MATERIAL_WATER_FOAM_G,
                BOX_MATERIAL_WATER_FOAM_B },
            .foam_width = BOX_MATERIAL_WATER_FOAM_WIDTH,
            .foam_texture = 0,
            .foam_scale = BOX_MATERIAL_WATER_FOAM_SCALE,
            .ripple_scale = BOX_MATERIAL_WATER_RIPPLE_SCALE,
            .ripple_velocity = { 0.0f, 0.0f } };
    }
}

/** Construct TextureTransform values that leave the texture coordinates unchanged.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_texture_transform_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    TextureTransform *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (TextureTransform){ .scale_x = 1.0f, .scale_y = 1.0f };
    }
}

/** Construct TextureScroll values that do not move.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_texture_scroll_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    TextureScroll *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (TextureScroll){
            .velocity = { BOX_MATERIAL_SCROLL_VELOCITY, BOX_MATERIAL_SCROLL_VELOCITY },
            .normal_velocity = { BOX_MATERIAL_SCROLL_VELOCITY, BOX_MATERIAL_SCROLL_VELOCITY } };
    }
}

/** Construct WindSway values that do not bend, with a height of 1 at a base of 0 and a 2 Hz
 * flutter of no amplitude that reaches full over 0.3 world units.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_wind_sway_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    WindSway *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (WindSway){
            .height = BOX_MATERIAL_SWAY_HEIGHT,
            .flutter_frequency = BOX_MATERIAL_SWAY_FLUTTER_FREQUENCY,
            .flutter_reach = BOX_MATERIAL_SWAY_FLUTTER_REACH };
    }
}

/** Construct TextureMapping values that keep the UVs of the mesh, with a size of 1 and no
 * blend.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_texture_mapping_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    TextureMapping *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (TextureMapping){ .mode = MappingUv, .size = 1.0f, .blend = 0.0f };
    }
}

/** Construct TextureLayer values that cover everything facing up by any amount, mixing the
 * albedo and roughness of the layer, with the default contrast.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_texture_layer_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    TextureLayer *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (TextureLayer){
            .amount = BOX_MATERIAL_LAYER_AMOUNT,
            .direction = { 0.0f, 1.0f, 0.0f },
            .contrast = BOX_MATERIAL_LAYER_CONTRAST,
            .albedo = LayerMix,
            .roughness = LayerMix,
            .metallic = LayerKeep,
            .occlusion = LayerKeep };
    }
}

void BoxMaterialImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxMaterial);
    ECS_IMPORT(world, BoxTypes);

    ecs_set_name_prefix(world, "Material");
    ECS_META_COMPONENT(world, MaterialId);
    ecs_set_name_prefix(world, NULL);
    ECS_META_COMPONENT(world, Rgb);
    ECS_META_COMPONENT(world, Opacity);
    ECS_META_COMPONENT(world, AlphaCutoff);
    ECS_META_COMPONENT(world, Tint);
    ECS_META_COMPONENT(world, PbrMaterial);
    ECS_META_COMPONENT(world, ClearCoat);
    ECS_META_COMPONENT(world, SpecularAnisotropy);
    ECS_META_COMPONENT(world, Sheen);
    ECS_META_COMPONENT(world, DiffuseTransmission);
    ECS_META_COMPONENT(world, Emissive);
    ECS_META_COMPONENT(world, Fresnel);
    ECS_META_COMPONENT(world, Transmission);
    ECS_META_COMPONENT(world, Distortion);
    ECS_META_COMPONENT(world, Water);
    ECS_META_COMPONENT(world, WaterWave);
    ECS_META_COMPONENT(world, WaterWaves);
    ECS_META_COMPONENT(world, WaterFlow);
    ECS_META_COMPONENT(world, WaterCaustics);
    ECS_META_COMPONENT(world, WaterRipples);
    ECS_META_COMPONENT(world, RippleSource);
    ECS_META_COMPONENT(world, WindSway);
    ECS_META_COMPONENT(world, VertexReveal);
    ECS_META_COMPONENT(world, BlendMode);
    ECS_META_COMPONENT(world, BackFaceMode);
    ECS_META_COMPONENT(world, PbrTextures);

    ecs_entity_t module = ecs_get_scope(world);
    ecs_entity_t texture = ecs_entity(world, { .name = "texture" });
    ecs_add_id(world, texture, EcsModule);
    ecs_set_scope(world, texture);
    ecs_set_name_prefix(world, "Texture");
    ECS_META_COMPONENT(world, TextureWrap);
    ECS_META_COMPONENT(world, TextureTransform);
    ECS_META_COMPONENT(world, TextureScroll);
    ECS_META_COMPONENT(world, TextureMappingMode);
    ECS_META_COMPONENT(world, TextureMapping);
    ECS_META_COMPONENT(world, TextureBlend);
    ECS_META_COMPONENT(world, TextureLayerBlend);
    ECS_META_COMPONENT(world, TextureLayer);
    ECS_META_COMPONENT(world, TextureLayers);
    ECS_TAG_DEFINE(world, TextureFrame);
    ecs_set_name_prefix(world, NULL);
    ecs_set_scope(world, module);

    ecs_set_hooks(world, Opacity, { .ctor = box_opacity_ctor });
    ecs_set_hooks(world, AlphaCutoff, { .ctor = box_alpha_cutoff_ctor });
    ecs_set_hooks(world, PbrMaterial, { .ctor = box_pbr_material_ctor });
    ecs_set_hooks(world, ClearCoat, { .ctor = box_clear_coat_ctor });
    ecs_set_hooks(world, SpecularAnisotropy, { .ctor = box_specular_anisotropy_ctor });
    ecs_set_hooks(world, Sheen, { .ctor = box_sheen_ctor });
    ecs_set_hooks(world, DiffuseTransmission, { .ctor = box_diffuse_transmission_ctor });
    ecs_set_hooks(world, TextureTransform, { .ctor = box_texture_transform_ctor });
    ecs_set_hooks(world, TextureMapping, { .ctor = box_texture_mapping_ctor });
    ecs_set_hooks(world, TextureScroll, { .ctor = box_texture_scroll_ctor });
    ecs_set_hooks(world, Transmission, { .ctor = box_transmission_ctor });
    ecs_set_hooks(world, Fresnel, { .ctor = box_fresnel_ctor });
    ecs_set_hooks(world, Distortion, { .ctor = box_distortion_ctor });
    ecs_set_hooks(world, Water, { .ctor = box_water_ctor });
    ecs_set_hooks(world, WaterWaves, { .ctor = box_water_waves_ctor });
    ecs_add_pair(world, ecs_id(WaterWaves), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(WaterFlow), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(WaterFlow), EcsWith, ecs_id(MaterialId));
    ecs_set_hooks(world, WaterCaustics, { .ctor = box_water_caustics_ctor });
    ecs_add_pair(world, ecs_id(WaterCaustics), EcsOnInstantiate, EcsInherit);
    ecs_set_hooks(world, WaterRipples, { .ctor = box_water_ripples_ctor });
    ecs_set_hooks(world, RippleSource, { .ctor = box_ripple_source_ctor });
    ecs_set_hooks(world, WindSway, { .ctor = box_wind_sway_ctor });
    ecs_set_hooks(world, TextureLayer, { .ctor = box_texture_layer_ctor });
    ecs_set_hooks(world, MaterialId, { .ctor = box_material_id_ctor });
    ecs_add_pair(world, ecs_id(MaterialId), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(PbrTextures), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(TextureBlend), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(TextureLayers), EcsOnInstantiate, EcsInherit);

    ecs_entity_t materials[17] = {
        ecs_id(Rgb), ecs_id(PbrMaterial), ecs_id(Emissive), ecs_id(TextureTransform),
        ecs_id(AlphaCutoff), ecs_id(Transmission), ecs_id(ClearCoat), ecs_id(SpecularAnisotropy),
        ecs_id(Sheen), ecs_id(DiffuseTransmission), ecs_id(TextureMapping), ecs_id(TextureScroll),
        ecs_id(Fresnel), ecs_id(Water), ecs_id(Distortion), ecs_id(WindSway),
        ecs_id(VertexReveal)
    };
    int32_t i;
    for (i = 0; i < 17; i ++) {
        ecs_add_pair(world, materials[i], EcsOnInstantiate, EcsInherit);
        ecs_add_pair(world, materials[i], EcsWith, ecs_id(MaterialId));
    }

    ECS_TAG_DEFINE(world, DynamicMaterial);
    ECS_TAG_DEFINE(world, NoShadow);
    ECS_TAG_DEFINE(world, LightFixture);

    ecs_vec_t *queue = ecs_os_calloc_t(ecs_vec_t);
    ecs_vec_init_t(NULL, queue, box_material_ripple_t, 0);
    ECS_COMPONENT_DEFINE(world, MaterialRippleQueue);
    ecs_singleton_set(world, MaterialRippleQueue, { .ripples = queue });
    ecs_atfini(world, box_material_ripples_free, queue);

    ecs_function(world, {
        .name = "ripple_at",
        .parent = ecs_get_scope(world),
        .return_type = ecs_id(ecs_bool_t),
        .params = {
            { .name = "water", .type = ecs_id(ecs_entity_t) },
            { .name = "position", .type = ecs_id(vec3) },
            { .name = "radius", .type = ecs_id(ecs_f32_t) },
            { .name = "strength", .type = ecs_id(ecs_f32_t) }
        },
        .callback = box_material_ripple_at_function
    });

    ecs_function(world, {
        .name = "foam_at",
        .parent = ecs_get_scope(world),
        .return_type = ecs_id(ecs_bool_t),
        .params = {
            { .name = "water", .type = ecs_id(ecs_entity_t) },
            { .name = "position", .type = ecs_id(vec3) },
            { .name = "radius", .type = ecs_id(ecs_f32_t) },
            { .name = "amount", .type = ecs_id(ecs_f32_t) }
        },
        .callback = box_material_foam_at_function
    });
}
