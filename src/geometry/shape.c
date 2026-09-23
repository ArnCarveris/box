/**
 * @file geometry/shape.c
 * @brief The shape registration point: Shape installs an observer for a project component.
 *
 * Setting a Shape creates an OnSet observer for the component it names. The
 * observer builds the key of every changed value, gets its mesh from the mesh
 * cache and writes the RenderMesh, like the builtin shapes do.
 */

#include "geometry_internal.h"
#include "mesh_gen_internal.h"

ECS_COMPONENT_DECLARE(Shape);

/** One installed registration, owned by the observer that reads it */
typedef struct box_shape_reg_t {
    Shape desc;      /**< The registration the shape entity holds */
    ecs_size_t size; /**< Size of the shape component, which strides the field */
} box_shape_reg_t;

/** The observer a shape entity installed */
typedef struct ShapeImpl {
    ecs_entity_t observer; /**< The observer watching the shape component */
} ShapeImpl;

ECS_COMPONENT_DECLARE(ShapeImpl);

void box_shape_reserve(
    box_shape_mesh_t *mesh,
    int32_t vertices,
    int32_t indices)
{
    box_mesh_builder_t *builder = (box_mesh_builder_t*)mesh;

    box_mesh_builder_init(builder, builder->mesh, (uint32_t)vertices, (uint32_t)indices);
}

uint32_t box_shape_vertex(
    box_shape_mesh_t *mesh,
    vec3 position,
    vec3 normal,
    float u,
    float v)
{
    return box_mesh_builder_vertex((box_mesh_builder_t*)mesh, position, normal, u, v);
}

void box_shape_triangle(
    box_shape_mesh_t *mesh,
    uint32_t a,
    uint32_t b,
    uint32_t c)
{
    box_mesh_builder_triangle((box_mesh_builder_t*)mesh, a, b, c);
}

/** Release the registration of an observer that is being deleted.
 *
 * @param ptr The registration
 */
static void box_shape_reg_free(
    void *ptr)
{
    ecs_os_free(ptr);
}

/** Give one entity the mesh its shape value asks for.
 *
 * The mesh is written through the stage of the observer, because a shape that
 * arrives with an instantiated prefab is set while the entity is being moved to
 * its table, where a direct set would move it a second time.
 *
 * @param it The observer iterator
 * @param reg The registration
 * @param e The entity
 * @param value Its shape component value
 */
static void box_shape_apply_value(
    ecs_iter_t *it,
    const box_shape_reg_t *reg,
    ecs_entity_t e,
    const void *value)
{
    box_shape_key_t key = { .id = (uint64_t)reg->desc.component };
    RenderMesh mesh = { .scale = vec3_new(1.0f, 1.0f, 1.0f) };

    reg->desc.key(value, &key, &mesh.scale);
    mesh.handle = box_mesh_cache_shape(it->world, &key, reg->desc.build,
        &mesh.bounds_min, &mesh.bounds_max);
    ecs_set_ptr(it->stage, e, RenderMesh, &mesh);
}

/** Observer of a registered shape component.
 *
 * @param it Iterator over the entities whose shape value changed
 */
static void box_shape_on_value(
    ecs_iter_t *it)
{
    const box_shape_reg_t *reg = it->ctx;
    const uint8_t *values = ecs_field_w_size(it, (size_t)reg->size, 0);
    bool self = ecs_field_is_self(it, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        const void *value = values + (size_t)reg->size * (size_t)(self ? i : 0);

        box_shape_apply_value(it, reg, it->entities[i], value);
    }
}

/** Install the observer of one registration on the shape entity.
 *
 * @param it The observer iterator
 * @param e The shape entity
 * @param desc The registration
 */
static void box_shape_install(
    ecs_iter_t *it,
    ecs_entity_t e,
    const Shape *desc)
{
    const ecs_type_info_t *ti = NULL;
    const ShapeImpl *impl = NULL;
    const char *problem = NULL;

    if (!desc->component || !desc->key || !desc->build) {
        problem = "needs a component, a key function and a build function";
    } else {
        ti = ecs_get_type_info(it->world, desc->component);

        if (!ti || !ti->size) {
            problem = "names something that is not a component with a value";
        }
    }

    if (problem) {
        char *path = ecs_get_path(it->world, e);

        ecs_err("box.geometry: Shape of '%s' %s", path, problem);
        ecs_os_free(path);
        goto done;
    }

    impl = ecs_get(it->world, e, ShapeImpl);
    if (impl && ecs_is_alive(it->world, impl->observer)) {
        ecs_delete(it->stage, impl->observer);
    }

    box_shape_reg_t *reg = ecs_os_calloc_t(box_shape_reg_t);
    reg->desc = *desc;
    reg->size = ti->size;

    ecs_entity_t observer = ecs_observer(it->world, {
        .query.terms = {{ .id = desc->component, .inout = EcsIn }},
        .events = { EcsOnSet },
        .callback = box_shape_on_value,
        .ctx = reg,
        .ctx_free = box_shape_reg_free
    });

    ecs_set(it->stage, e, ShapeImpl, { .observer = observer });
done:
    return;
}

/** A shape registration was set: install the observer of its component.
 *
 * @param it Iterator over the changed registrations
 */
static void box_shape_on_set(
    ecs_iter_t *it)
{
    const Shape *shape = ecs_field(it, Shape, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_shape_install(it, it->entities[i], &shape[i]);
    }
}

/** A shape registration went away: take its observer with it.
 *
 * @param it Iterator over the entities that lost the registration
 */
static void box_shape_on_remove(
    ecs_iter_t *it)
{
    if (ecs_is_fini(it->world) || ecs_should_quit(it->world)) {
        goto done;
    }

    bool deleting = !it->other_table || !ecs_table_get_type(it->other_table)->count;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        const ShapeImpl *impl = ecs_get(it->world, it->entities[i], ShapeImpl);

        if (!impl) {
            continue;
        }

        if (ecs_is_alive(it->world, impl->observer)) {
            ecs_delete(it->stage, impl->observer);
        }

        if (!deleting) {
            ecs_remove(it->stage, it->entities[i], ShapeImpl);
        }
    }
done:
    return;
}

void box_shape_init(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, Shape);
    ECS_COMPONENT_DEFINE(world, ShapeImpl);

    ecs_set_hooks(world, Shape, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, ShapeImpl, { .ctor = flecs_default_ctor });
    ecs_add_pair(world, ecs_id(Shape), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(ShapeImpl), EcsOnInstantiate, EcsDontInherit);

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Shape), .inout = EcsIn }},
        .events = { EcsOnSet },
        .callback = box_shape_on_set
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Shape), .inout = EcsIn }},
        .events = { EcsOnRemove },
        .callback = box_shape_on_remove
    });
}
