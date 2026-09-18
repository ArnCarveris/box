/**
 * @file view/view.c
 * @brief Registers the box.view module and the defaults of a new View.
 */

#define BOX_VIEW_IMPL
#include "view_internal.h"

ECS_COMPONENT_DECLARE(ViewImpl);
ECS_COMPONENT_DECLARE(RenderTargetImpl);

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
        v[i].clear.r = BOX_VIEW_CLEAR_R;
        v[i].clear.g = BOX_VIEW_CLEAR_G;
        v[i].clear.b = BOX_VIEW_CLEAR_B;
        v[i].clear_alpha = 1.0f;
        v[i].priority = 0;
        v[i].shadows = true;
        v[i].effects = true;
        v[i].material_layers = true;
        v[i].enabled = true;
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
    ECS_COMPONENT_DEFINE(world, ViewImpl);
    ECS_COMPONENT_DEFINE(world, RenderTargetImpl);

    ecs_set_hooks(world, View, { .ctor = box_view_ctor });
    ecs_set_hooks(world, RenderTarget, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, ViewImpl, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, RenderTargetImpl, { .ctor = flecs_default_ctor });

    ecs_add_pair(world, ecs_id(View), EcsWith, ecs_id(ViewImpl));
    ecs_add_pair(world, ecs_id(RenderTarget), EcsWith, ecs_id(RenderTargetImpl));
    ecs_add_pair(world, ecs_id(ViewImpl), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(RenderTargetImpl), EcsOnInstantiate, EcsDontInherit);
}
