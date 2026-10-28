#ifndef FLECS_ENGINE_RENDERER_H
#define FLECS_ENGINE_RENDERER_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_RENDERER_IMPL
#define ECS_META_IMPL EXTERN
#endif

#define FLECS_ENGINE_SHADOW_CASCADE_COUNT 4
#define FLECS_ENGINE_CLUSTER_X 16
#define FLECS_ENGINE_CLUSTER_Y 9
#define FLECS_ENGINE_CLUSTER_Z 48
#define FLECS_ENGINE_CLUSTER_TOTAL \
    (FLECS_ENGINE_CLUSTER_X * FLECS_ENGINE_CLUSTER_Y * FLECS_ENGINE_CLUSTER_Z)

#define FLECS_ENGINE_CLUSTER_MAX_PER_CLUSTER 64
#define FLECS_ENGINE_CLUSTER_MAX_PER_CLUSTER_LIMIT 1024

#define FLECS_ENGINE_SHADOW_NORMAL_BIAS_DEFAULT 2.0f

#define FLECS_ENGINE_LIGHT_PX_DEFAULT 4.0f
#define FLECS_ENGINE_LIGHT_RANGE_FADE_DEFAULT 0.2f
#define FLECS_ENGINE_LIGHT_RANGE_FADE_MIN 0.02f
#define FLECS_ENGINE_LIGHT_RANGE_FADE_MAX 0.95f

typedef struct {
    uint32_t grid_size[4];   /* x, y, z, total */
    float screen_info[4];    /* width, height, near, log(far/near) */
} FlecsClusterInfo;

typedef struct {
    uint32_t light_offset;
    uint32_t light_count;
    uint32_t _pad0;
    uint32_t _pad1;
} FlecsClusterEntry;

typedef struct {
    const char *source;
    const char *vertex_entry;
    const char *fragment_entry;
} FlecsShader;

extern ECS_COMPONENT_DECLARE(FlecsShader);

extern ECS_TAG_DECLARE(FlecsCustomShader);

ECS_STRUCT(FlecsRenderBatchSet, {
    ecs_vec_t batches;
});

ECS_STRUCT(FlecsHdri, {
    const char *file;
    uint32_t filter_sample_count;
    uint32_t lut_sample_count;
});

extern ECS_COMPONENT_DECLARE(FlecsHdri);

ECS_STRUCT(flecs_engine_shadow_params_t, {
    ecs_bool_t enabled;
    int32_t map_size;
    float bias;
    float normal_bias;
    float max_range;
});

/* Fullscreen post-process effect entry. Input uses chain indexing:
 * 0 = batches framebuffer, k > 0 = output of effect[k - 1]. */
ECS_STRUCT(flecs_render_view_effect_t, {
    ecs_bool_t enabled;
    int32_t input;
    ecs_entity_t effect;
});

/* `max_range` is the light distance limit: the view depth past which a point
 * or spot light is not clustered at all, the counterpart of `shadow.max_range`
 * and in the same units. 0 keeps the camera far plane, so a scene that says
 * nothing renders exactly as it did before the knob existed. The clustering
 * far plane follows it, so the Z slices are fitted inside the capped range
 * rather than over empty air.
 *
 * A hard cut at that distance makes a light wink out as the camera backs away,
 * which is the artifact clustering exists to avoid, so the cut arrives as a
 * fade: over the last `range_fade` fraction of `max_range` a light's intensity
 * is smoothstepped to zero, reaching zero exactly where the cut fires. It is a
 * fraction and not a width in world units for the same reason
 * `surface_effects.range_fade` is: a band of fixed world width covers fewer
 * pixels the further out it sits and reads as a hard edge again. 0 takes
 * FLECS_ENGINE_LIGHT_RANGE_FADE_DEFAULT, negative means no band at all, and
 * the fade is measured on the near edge of a light's sphere so a large light
 * is not dimmed while it still reaches inside the cap. Without a `max_range`
 * there is nothing to fade against and the field does nothing.
 *
 * `light_px_threshold` is the other, complementary cut: it fades a light out
 * once its projected diameter drops under that many pixels. Distance is the
 * cheaper criterion, projected size the more honest one; see
 * docs/PERF_NOTES.md for which to reach for. */
ECS_STRUCT(flecs_engine_cluster_params_t, {
    int32_t max_lights_per_cluster;
    ecs_f32_t light_px_threshold;
    ecs_f32_t max_range;
    ecs_f32_t range_fade;
});

ECS_STRUCT(FlecsRenderView, {
    ecs_entity_t camera;
    ecs_entity_t atmosphere;   /* entity with FlecsAtmosphere component; when
                                * set, supplies sky + aerial perspective + IBL
                                * and takes precedence over `hdri` */
    ecs_entity_t hdri;
    ecs_f32_t ambient_intensity;
    ecs_f32_t screen_px_threshold;  /* min projected bounding-sphere diameter
                                      * in pixels; instances smaller than this
                                      * are culled. 0 disables. */
    ecs_f32_t detail_radius;
    ecs_f32_t detail_px_threshold;
    ecs_f32_t shadow_px_threshold;
    ecs_f32_t reference_height;
    ecs_f32_t dynamic_radius;
    ecs_f32_t transparent_sort_bucket;
    flecs_engine_cluster_params_t cluster;
    flecs_engine_shadow_params_t shadow;
    /* Weathering and cloud shadow are configuration for a render, not state of
     * the world, so they sit next to `shadow` rather than in singletons: the
     * same set of objects can be dusty in the main view and clean in a
     * thumbnail render. See modules/surface_effects.h for the layers and for
     * the per-layer `enabled` switches. */
    flecs_engine_surface_effects_t surface_effects;
    flecs_engine_cloud_shadow_t cloud_shadow;
    /* Root of the render graph to use: an entity with a RenderGraph component
     * whose children are the passes. When 0, `effects` is the graph, handed
     * over in serialized order. Either way the compiler derives the ordered
     * RenderPasses array the executor walks. See modules/render_graph.h. */
    ecs_entity_t graph;
    ecs_bool_t wireframe;
    ecs_vec_t effects;
});

ecs_entity_t flecsEngine_createHdri(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    const char *file,
    uint32_t filterSampleCount,
    uint32_t lutSampleCount);

ecs_entity_t flecsEngine_createShader(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    const FlecsShader *shader);

ecs_entity_t flecsEngine_shader_splineMesh(
    ecs_world_t *world);

ecs_entity_t flecsEngine_shader_splineMeshShadow(
    ecs_world_t *world);

ecs_entity_t flecsEngine_createMeshShaderBatch(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    ecs_entity_t shader,
    bool needs_transmission);

void flecsEngine_renderBatchSetAppend(
    ecs_world_t *world,
    ecs_entity_t batch_set,
    ecs_entity_t batch);

void flecsEngine_renderBatchSetInsertBefore(
    ecs_world_t *world,
    ecs_entity_t batch_set,
    ecs_entity_t batch,
    ecs_entity_t before);

void flecsEngine_renderBatchSetDepthWrite(
    ecs_world_t *world,
    ecs_entity_t batch,
    bool enabled);

void flecsEngine_captureRequest(void);

bool flecsEngine_captureReady(void);

int flecsEngine_captureWritePng(
    const char *path,
    int32_t *width_out,
    int32_t *height_out);

int flecsEngine_captureRgb(
    uint8_t **rgb_out,
    int32_t *width_out,
    int32_t *height_out);

int flecsEngine_writePng(
    const char *path,
    int32_t width,
    int32_t height,
    int32_t channels,
    const uint8_t *pixels);

#endif
