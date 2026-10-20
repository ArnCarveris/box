#include "mesh_cache.h"

void box_surface_materials_apply(
    const ecs_world_t *world,
    ecs_entity_t e,
    ShapeMesh *shape)
{
    if (!shape->surface.p[0][0]) {
        return;
    }

    const SurfaceMaterials *materials = ecs_get(world, e, SurfaceMaterials);
    ecs_os_memset(shape->surface.p[20], 0, sizeof(vec4) * 2);
    if (!materials) {
        return;
    }

    const SurfaceMaterial *regions[4] = {
        &materials->outer, &materials->inner, &materials->rim, &materials->cap
    };
    for (int i = 0; i < 4; i ++) {
        const SurfaceMaterial *material = regions[i];
        uint32_t color = material->color.r | ((uint32_t)material->color.g << 8) |
            ((uint32_t)material->color.b << 16) | ((uint32_t)material->color.a << 24);
        uint32_t metallic = (uint32_t)(glm_clamp(material->metallic, 0, 1) * 32767.0f);
        uint32_t roughness = (uint32_t)(glm_clamp(material->roughness, 0, 1) * 65535.0f);
        uint32_t parameters = 0x80000000u | (metallic << 16) | roughness;
        ecs_os_memcpy(&shape->surface.p[20][i], &color, sizeof(color));
        ecs_os_memcpy(&shape->surface.p[21][i], &parameters, sizeof(parameters));
    }
}

static void box_surface_materials_changed(
    ecs_iter_t *it)
{
    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        ShapeMesh *shape = ecs_get_mut(it->world, e, ShapeMesh);
        if (shape && shape->surface.p[0][0]) {
            if (it->event == EcsOnRemove) {
                ecs_os_memset(shape->surface.p[20], 0, sizeof(vec4) * 2);
            } else {
                box_surface_materials_apply(it->world, e, shape);
            }

            ecs_modified(it->world, e, ShapeMesh);
        }
    }
}

static void box_surface_materials_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    SurfaceMaterials *values = ptr;
    SurfaceMaterial material = {.color = {255, 255, 255, 255}, .roughness = 0.5f};
    for (int i = 0; i < count; i ++) {
        values[i] = (SurfaceMaterials){material, material, material, material};
    }
}

void box_surface_materials_init(
    ecs_world_t *world)
{
    ecs_set_hooks(world, SurfaceMaterials, {.ctor = box_surface_materials_ctor});
    ecs_observer(world, {.query.terms = {{.id = ecs_id(SurfaceMaterials)}},
        .events = {EcsOnSet, EcsOnRemove}, .callback = box_surface_materials_changed});
}
