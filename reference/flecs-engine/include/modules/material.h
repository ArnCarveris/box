#ifndef FLECS_ENGINE_MATERIAL_H
#define FLECS_ENGINE_MATERIAL_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_MATERIAL_IMPL
#define ECS_META_IMPL EXTERN
#endif

typedef flecs_rgba_t FlecsRgba;

extern ECS_COMPONENT_DECLARE(FlecsRgba);

typedef flecs_rgba_t FlecsTint;

extern ECS_COMPONENT_DECLARE(FlecsTint);

typedef flecs_rgba_t FlecsActualTint;

extern ECS_COMPONENT_DECLARE(FlecsActualTint);

ECS_STRUCT(FlecsPbrMaterial, {
    float metallic;
    float roughness;
});

extern ECS_COMPONENT_DECLARE(FlecsPbrMaterial);

ECS_STRUCT(FlecsEmissive, {
    float strength;
    flecs_rgba_t color;
});

typedef FlecsEmissive EcsEmissive;

extern ECS_COMPONENT_DECLARE(FlecsEmissive);

ECS_STRUCT(FlecsMaterialId, {
    uint32_t value;
});

extern ECS_COMPONENT_DECLARE(FlecsMaterialId);

ECS_STRUCT(FlecsTexture, {
    const char *path;
    int32_t width;
    int32_t height;
});

extern ECS_COMPONENT_DECLARE(FlecsTexture);

typedef struct FlecsSvg {
    const char *file;
    const char *data;
    int32_t width;
    int32_t height;
} FlecsSvg;

extern ECS_COMPONENT_DECLARE(FlecsSvg);

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t mip_count;
    const char *format;
} flecs_texture_info_t;

ECS_STRUCT(FlecsTextureInfo, {
    flecs_texture_info_t source;
    flecs_texture_info_t actual;
});

extern ECS_COMPONENT_DECLARE(FlecsTextureInfo);

ECS_STRUCT(FlecsPbrTextures, {
    ecs_entity_t albedo;
    ecs_entity_t emissive;
    ecs_entity_t roughness;
    ecs_entity_t normal;
});

extern ECS_COMPONENT_DECLARE(FlecsPbrTextures);

extern ECS_TAG_DECLARE(FlecsAlphaBlend);

extern ECS_TAG_DECLARE(FlecsAlphaCutout);

extern ECS_TAG_DECLARE(FlecsAdditive);

ECS_STRUCT(FlecsMaterialAnim, {
    float uv_scroll_x;
    float uv_scroll_y;
    float emissive_pulse_hz;
    float emissive_pulse_depth;
    float flicker_hz;
    float flicker_depth;
    int32_t flipbook_frames;
    float flipbook_fps;
    float emissive_fade_start;
    float emissive_fade_end;
    /* Vertex sway, for foliage. The PBR vertex shaders bend a vertex
     * sideways by (local y)^2 * wind_amplitude, so a mesh authored on
     * [0, 1] in y has a rigid base and a moving top, and the entity's
     * Scale3 turns the amplitude into metres. The phase comes from the
     * instance's world position, so identical objects do not sway in
     * lockstep. Shadows do not sway: the shadow pass is depth only. */
    float wind_amplitude;
    float wind_speed;
});

extern ECS_COMPONENT_DECLARE(FlecsMaterialAnim);

ECS_STRUCT(FlecsInteriorMap, {
    ecs_entity_t atlas;
    int32_t rooms;
    int32_t cells_x;
    int32_t cells_y;
    float depth;
    float emissive;
    float lit_ratio;
    bool random_rooms;
    int32_t seed;
});

extern ECS_COMPONENT_DECLARE(FlecsInteriorMap);

/* Which of the view's surface effects this material takes part in. The view
 * still owns what every effect *looks* like (FlecsRenderView.surface_effects:
 * the dust colour, the pool coverage, the scratch width); this says only
 * whether the effect is allowed to touch this material at all.
 *
 * Every switch is off by default, so a material without this component takes
 * part in nothing no matter what the view enables - which is what lets a
 * lamp lens stay dry in the rain and a painted sign stay clean under a scene
 * that dusts everything around it. The switches ride to the GPU as a bit per
 * layer in the material record, and each layer tests its bit beside the
 * view's own switch before it takes a single noise tap: a layer a material
 * opted out of costs one compare against a value that is uniform across the
 * draw, which is what the whole pass already cost when a view switched a
 * layer off.
 *
 * `absorption` is how fast water leaves this material, not how wet it can
 * get: 0 is a sealed surface - asphalt, glass, painted steel - that holds a
 * puddle for as long as the weather leaves it there, and 1 is sand or bare
 * soil, which drinks the rain as it lands and is dry again minutes after it
 * stops. The wetness layer keeps two global reservoirs (FlecsWetness) and
 * this value picks which one the material follows, so a road and the sand
 * beside it dry at their own speeds without either of them owning a
 * simulation. Quantized to 4 bits in the material's spare flag lane, so
 * sixteen steps: anything finer would not survive the trip to the GPU. It
 * only means anything while `wetness` is on. */
ECS_STRUCT(FlecsSurfaceEffects, {
    bool wetness;
    float absorption;
    bool dust;
    bool grime;
    bool scratch;
    bool grain;
    bool streak;
    bool edge;
    bool burn;
});

extern ECS_COMPONENT_DECLARE(FlecsSurfaceEffects);

extern ECS_TAG_DECLARE(FlecsNoShadow);

extern ECS_TAG_DECLARE(FlecsDynamicVisuals);

/* Routes an entity to the vertex-color mesh batch: its (owned or inherited)
 * FlecsMesh3 must have per-vertex colors. rgb multiplies albedo, alpha
 * multiplies material roughness. */
extern ECS_TAG_DECLARE(FlecsVertexColors);

ECS_STRUCT(FlecsTransmission, {
    float transmission_factor;
    float ior;
    float thickness_factor;
    float attenuation_distance;
    flecs_rgba_t attenuation_color;
});

extern ECS_COMPONENT_DECLARE(FlecsTransmission);

ECS_STRUCT(FlecsVolumeGlow, {
    float edge_power;
    float axis_falloff;
    float head_fade;
    float near_fade;
    float intensity;
});

extern ECS_COMPONENT_DECLARE(FlecsVolumeGlow);

ECS_STRUCT(FlecsTextureTransform, {
    float scale_x;
    float scale_y;
    float offset_x;
    float offset_y;
});

extern ECS_COMPONENT_DECLARE(FlecsTextureTransform);

ECS_ENUM(FlecsTilingShape, {
    FlecsTilingBox,
    FlecsTilingCylinder,
    FlecsTilingRadial,
    FlecsTilingMeters
});

extern ECS_COMPONENT_DECLARE(FlecsTilingShape);

ECS_STRUCT(FlecsTextureTiling, {
    float meters_x;
    float meters_y;
    bool fit;
    int32_t min_tiles;
    FlecsTilingShape shape;
});

extern ECS_COMPONENT_DECLARE(FlecsTextureTiling);

ECS_ENUM(FlecsWrapMode, {
    FlecsWrapRepeat,
    FlecsWrapClamp,
    FlecsWrapMirror
});

extern ECS_COMPONENT_DECLARE(FlecsWrapMode);

ECS_STRUCT(FlecsTextureWrap, {
    FlecsWrapMode mode;
});

extern ECS_COMPONENT_DECLARE(FlecsTextureWrap);

/* Create a texture entity from raw RGBA8 pixels (width * height * 4 bytes,
 * row-major, top row first). The resulting entity can be referenced from
 * FlecsPbrTextures like a file-loaded texture. Mips are generated. Requires
 * an initialized GPU device (i.e. a surface must exist); returns 0 when the
 * device is not ready or texture creation failed. */
ecs_entity_t flecsEngine_texturePixels(
    ecs_world_t *world,
    const char *name,
    int32_t width,
    int32_t height,
    const uint8_t *rgba_pixels);

ecs_entity_t flecsEngine_textureSvg(
    ecs_world_t *world,
    const char *name,
    const char *svg,
    int32_t width,
    int32_t height);

ecs_entity_t flecsEngine_textureResource(
    const ecs_world_t *world,
    ecs_entity_t texture);

bool flecsEngine_texturePixelsInto(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t width,
    int32_t height,
    const uint8_t *rgba_pixels);

bool flecsEngine_textureFileInto(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *path,
    int32_t width,
    int32_t height);

bool flecsEngine_texture_restoreSource(
    ecs_world_t *world,
    ecs_entity_t e);

/* While held, the renderer keeps its texture arrays as they are instead of
 * rebuilding them every frame new textures arrive. Batches whose textures
 * are not in the arrays yet are skipped until the hold is released. */
void flecsEngine_textureArrays_hold(
    ecs_world_t *world,
    bool hold);

#endif
