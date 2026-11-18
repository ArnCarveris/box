/**
 * @file material/material.c
 * @brief Registers the components of the box.material module.
 */

#define BOX_MATERIAL_IMPL
#include <box.h>

ECS_TAG_DECLARE(DynamicMaterial);
ECS_TAG_DECLARE(TextureFrame);
ECS_TAG_DECLARE(NoShadow);
ECS_TAG_DECLARE(LightFixture);

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
    ECS_META_COMPONENT(world, WindSway);
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
    ecs_set_hooks(world, WindSway, { .ctor = box_wind_sway_ctor });
    ecs_set_hooks(world, MaterialId, { .ctor = box_material_id_ctor });
    ecs_add_pair(world, ecs_id(MaterialId), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(PbrTextures), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(TextureBlend), EcsOnInstantiate, EcsInherit);

    ecs_entity_t materials[16] = {
        ecs_id(Rgb), ecs_id(PbrMaterial), ecs_id(Emissive), ecs_id(TextureTransform),
        ecs_id(AlphaCutoff), ecs_id(Transmission), ecs_id(ClearCoat), ecs_id(SpecularAnisotropy),
        ecs_id(Sheen), ecs_id(DiffuseTransmission), ecs_id(TextureMapping), ecs_id(TextureScroll),
        ecs_id(Fresnel), ecs_id(Water), ecs_id(Distortion), ecs_id(WindSway)
    };
    int32_t i;
    for (i = 0; i < 16; i ++) {
        ecs_add_pair(world, materials[i], EcsOnInstantiate, EcsInherit);
        ecs_add_pair(world, materials[i], EcsWith, ecs_id(MaterialId));
    }

    ECS_TAG_DEFINE(world, DynamicMaterial);
    ECS_TAG_DEFINE(world, NoShadow);
    ECS_TAG_DEFINE(world, LightFixture);
}
