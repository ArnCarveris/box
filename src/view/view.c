/**
 * @file view/view.c
 * @brief Registers the box.view module and the defaults of a new View.
 */

#define BOX_VIEW_IMPL
#include "view_internal.h"

ECS_COMPONENT_DECLARE(ViewImpl);
ECS_COMPONENT_DECLARE(RenderTargetImpl);
ECS_TAG_DECLARE(Layer);

/** View constructor: everything a view draws is on and the view is enabled.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_view_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    View *v = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        v[i].camera = 0;
        v[i].target = 0;
        memset(v[i].layers, 0, sizeof(v[i].layers));
        memset(v[i].shadow_layers, 0, sizeof(v[i].shadow_layers));
        memset(v[i].effect_list, 0, sizeof(v[i].effect_list));
        v[i].clear.r = BOX_VIEW_CLEAR_R;
        v[i].clear.g = BOX_VIEW_CLEAR_G;
        v[i].clear.b = BOX_VIEW_CLEAR_B;
        v[i].clear_alpha = 1.0f;
        v[i].priority = 0;
        v[i].strict_layers = false;
        v[i].shadows = true;
        v[i].effects = true;
        v[i].material_layers = true;
        v[i].enabled = true;
    }
}

/** CullSettings constructor: culling and occlusion are on, large batches go to the GPU.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_cull_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    CullSettings *s = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        s[i].enabled = true;
        s[i].gpu_threshold = BOX_CULL_GPU_THRESHOLD;
        s[i].occlusion = false;
        s[i].hiz_levels = BOX_CULL_HIZ_LEVELS;
    }
}

/** LodSettings constructor: selection is on at quality one with the default band.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_lod_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    LodSettings *s = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        s[i].enabled = true;
        s[i].quality = BOX_VIEW_LOD_QUALITY;
        s[i].error_px = BOX_VIEW_LOD_ERROR;
        s[i].shadow_error_px = BOX_VIEW_LOD_SHADOW_ERROR;
        s[i].hysteresis = BOX_VIEW_LOD_HYSTERESIS;
    }
}

/** ShadowSettings constructor: four cascades of 2048 texels over 200 units.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_shadow_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    ShadowSettings *s = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        s[i].cascades = BOX_SHADOW_CASCADES;
        s[i].resolution = BOX_SHADOW_RESOLUTION;
        s[i].max_distance = BOX_SHADOW_DISTANCE;
        s[i].split_lambda = BOX_SHADOW_LAMBDA;
        s[i].depth_bias = BOX_SHADOW_BIAS;
        s[i].normal_offset = BOX_SHADOW_NORMAL;
        s[i].filter_size = BOX_SHADOW_FILTER;
        s[i].fade = BOX_SHADOW_FADE;
    }
}

void BoxViewImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxView);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxCamera);

    ECS_META_COMPONENT(world, RenderTarget);
    ECS_META_COMPONENT(world, View);
    ECS_META_COMPONENT(world, CullSettings);
    ECS_META_COMPONENT(world, ShadowSettings);
    ECS_META_COMPONENT(world, LodSettings);
    ECS_COMPONENT_DEFINE(world, ViewImpl);
    ECS_COMPONENT_DEFINE(world, RenderTargetImpl);
    ECS_TAG_DEFINE(world, Layer);

    ecs_set_hooks(world, View, { .ctor = box_view_ctor });
    ecs_set_hooks(world, CullSettings, { .ctor = box_cull_ctor });
    ecs_set_hooks(world, ShadowSettings, { .ctor = box_shadow_ctor });
    ecs_set_hooks(world, LodSettings, { .ctor = box_lod_ctor });
    ecs_set_hooks(world, RenderTarget, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, ViewImpl, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, RenderTargetImpl, { .ctor = flecs_default_ctor });

    ecs_add_pair(world, ecs_id(View), EcsWith, ecs_id(ViewImpl));
    ecs_add_pair(world, ecs_id(RenderTarget), EcsWith, ecs_id(RenderTargetImpl));
    ecs_add_pair(world, ecs_id(ViewImpl), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(RenderTargetImpl), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, Layer, EcsOnInstantiate, EcsInherit);
}
