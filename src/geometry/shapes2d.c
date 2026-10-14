/**
 * @file geometry/shapes2d.c
 * @brief Resolves Rect, Circle and Line with their Border and Image into Shape2D and ShapeMesh.
 *
 * Every 2D shape shares one cached unit quad; the renderer shades it with a signed distance.
 */

#include "shape2d.h"

ECS_COMPONENT_DECLARE(Shape2D);

/** Key of the 2D quad, which has one mesh for every shape.
 *
 * @param value The shape value
 * @param key The key
 */
static void box_shape2d_key(
    const void *value,
    box_shape_key_t *key)
{
    (void)value;
    (void)key;
}

/** Generate a unit quad in the XY plane facing +Z, v 0 at the top.
 *
 * @param value The shape value
 * @param key The key
 * @param mesh The mesh
 */
static void box_shape2d_generate(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    vec3 normal = { 0.0f, 0.0f, 1.0f };
    (void)value;
    (void)key;
    int32_t a = box_mesh_vertex(mesh, (vec3){ -0.5f, -0.5f, 0.0f }, normal, 0.0f, 1.0f);
    int32_t b = box_mesh_vertex(mesh, (vec3){ 0.5f, -0.5f, 0.0f }, normal, 1.0f, 1.0f);
    int32_t c = box_mesh_vertex(mesh, (vec3){ 0.5f, 0.5f, 0.0f }, normal, 1.0f, 0.0f);
    int32_t d = box_mesh_vertex(mesh, (vec3){ -0.5f, 0.5f, 0.0f }, normal, 0.0f, 0.0f);
    box_mesh_triangle(mesh, a, b, c);
    box_mesh_triangle(mesh, a, c, d);
}

/** Get a component of an entity unless it is the one being removed.
 *
 * @param world The world
 * @param e The entity
 * @param id The component
 * @param ignore The component being removed, 0 for none
 * @return The value, NULL when absent or ignored
 */
static const void* box_shape2d_get(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_id_t id,
    ecs_id_t ignore)
{
    return id == ignore ? NULL : ecs_get_id(world, e, id);
}

/** Resolve the geometry of an entity: a Rect, else a Circle, else a Line.
 *
 * @param world The world
 * @param e The entity
 * @param ignore The component being removed, 0 for none
 * @param out The resolved shape
 * @param scale Receives the instance scale of the unit quad
 * @return Whether the shape has an area to draw
 */
static bool box_shape2d_geometry(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_id_t ignore,
    Shape2D *out,
    vec3 scale)
{
    const Rect *rect = box_shape2d_get(world, e, ecs_id(Rect), ignore);
    const Circle *circle = box_shape2d_get(world, e, ecs_id(Circle), ignore);
    const Line *line = box_shape2d_get(world, e, ecs_id(Line), ignore);
    bool result = false;
    glm_vec3_one(scale);
    if (rect) {
        out->kind = BOX_SHAPE2D_RECT;
        out->size[0] = rect->width;
        out->size[1] = rect->height;
        result = rect->width > 0.0f && rect->height > 0.0f;
    } else if (circle) {
        out->kind = BOX_SHAPE2D_RECT;
        out->size[0] = circle->radius * 2.0f;
        out->size[1] = circle->radius * 2.0f;
        out->radius = circle->radius;
        result = circle->radius > 0.0f;
    } else if (line) {
        out->kind = BOX_SHAPE2D_LINE;
        out->line[0] = line->x1;
        out->line[1] = line->y1;
        out->line[2] = line->x2;
        out->line[3] = line->y2;
        out->width = line->width > 0.0f ? line->width : 1.0f;
        result = line->x1 != line->x2 || line->y1 != line->y2;
    }

    if (out->kind == BOX_SHAPE2D_RECT) {
        scale[0] = out->size[0];
        scale[1] = out->size[1];
    }

    return result;
}

/** Resolve the Border and Image of an entity into its shape.
 *
 * @param world The world
 * @param e The entity
 * @param ignore The component being removed, 0 for none
 * @param out The resolved shape
 */
static void box_shape2d_fill(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_id_t ignore,
    Shape2D *out)
{
    const Border *border = box_shape2d_get(world, e, ecs_id(Border), ignore);
    const Image *image = box_shape2d_get(world, e, ecs_id(Image), ignore);
    out->uv[0] = 1.0f;
    out->uv[1] = 1.0f;
    if (border) {
        out->border = border->width > 0.0f ? border->width : 0.0f;
        out->border_color = border->color;
        if (out->kind == BOX_SHAPE2D_RECT && out->radius == 0.0f) {
            out->radius = border->radius > 0.0f ? border->radius : 0.0f;
        }
    }

    if (image) {
        float u1 = image->u1 != 0.0f ? image->u1 : 1.0f;
        float v1 = image->v1 != 0.0f ? image->v1 : 1.0f;
        out->texture = image->texture;
        out->uv[0] = u1 - image->u0;
        out->uv[1] = v1 - image->v0;
        out->uv[2] = image->u0;
        out->uv[3] = image->v0;
    }
}

/** Bring the ShapeMesh and Shape2D of an entity up to date with its 2D components.
 *
 * Both come with the shape through the With trait, so they are written in place; an entity
 * that lost its last 2D shape loses them through the stage.
 *
 * @param it The observer iterator
 * @param shape The registered quad
 * @param e The entity
 * @param ignore The component being removed, 0 for none
 */
static void box_shape2d_resolve(
    ecs_iter_t *it,
    const box_shape_t *shape,
    ecs_entity_t e,
    ecs_id_t ignore)
{
    Shape2D resolved = {0};
    vec3 scale;
    bool drawn = box_shape2d_geometry(it->world, e, ignore, &resolved, scale);
    ShapeMesh *mesh = ecs_get_mut(it->world, e, ShapeMesh);
    Shape2D *dst = ecs_get_mut(it->world, e, Shape2D);
    if (!resolved.kind) {
        if (dst && ignore) {
            ecs_remove(it->stage, e, ShapeMesh);
            ecs_remove(it->stage, e, Shape2D);
        }

        goto done;
    }

    box_shape2d_fill(it->world, e, ignore, &resolved);
    if (dst) {
        *dst = resolved;
        ecs_modified(it->stage, e, Shape2D);
    }

    if (mesh) {
        uint32_t handle = drawn ? box_mesh_cache_acquire(shape->cache, shape, NULL) : 0;
        box_mesh_cache_release(shape->cache, mesh->mesh);
        mesh->mesh = handle;
        glm_vec3_copy(scale, mesh->scale);
        ecs_modified(it->stage, e, ShapeMesh);
    }

done:
    return;
}

/** Resolve the entities whose 2D shape, Border or Image was set.
 *
 * @param it The iterator
 */
static void box_shape2d_on_set(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_shape2d_resolve(it, it->ctx, it->entities[i], 0);
    }
}

/** Resolve the entities that lose a 2D shape, Border or Image, unless they are deleted.
 *
 * @param it The iterator
 */
static void box_shape2d_on_remove(
    ecs_iter_t *it)
{
    int32_t i;
    if (!it->other_table || !ecs_table_get_type(it->other_table)->count) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        box_shape2d_resolve(it, it->ctx, it->entities[i], ecs_field_id(it, 0));
    }

done:
    return;
}

/** Construct Border values with an opaque black border.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti The type info
 */
static void box_border_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Border *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Border){ .color = { 0, 0, 0, 255 } };
    }
}

void box_geometry_shapes2d_init(
    ecs_world_t *world)
{
    ecs_entity_t shapes[3] = { ecs_id(Rect), ecs_id(Circle), ecs_id(Line) };
    ecs_entity_t parts[5] = {
        ecs_id(Rect), ecs_id(Circle), ecs_id(Line), ecs_id(Border), ecs_id(Image)
    };
    int32_t i;
    ECS_COMPONENT_DEFINE(world, Shape2D);
    ecs_set_hooks(world, Shape2D, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, Border, { .ctor = box_border_ctor });
    box_shape_t *quad = box_mesh_cache_add_shape(box_mesh_cache_of(world),
        &(box_shape_desc_t){
            .component = ecs_id(Rect),
            .key = box_shape2d_key,
            .generate = box_shape2d_generate
        });
    for (i = 0; i < 3; i ++) {
        ecs_add_pair(world, shapes[i], EcsWith, ecs_id(ShapeMesh));
        ecs_add_pair(world, shapes[i], EcsWith, ecs_id(Shape2D));
    }

    for (i = 0; i < 5; i ++) {
        ecs_observer(world, {
            .query.terms = {{ .id = parts[i] }},
            .events = { EcsOnSet },
            .callback = box_shape2d_on_set,
            .ctx = quad
        });
        ecs_observer(world, {
            .query.terms = {{ .id = parts[i] }},
            .events = { EcsOnRemove },
            .callback = box_shape2d_on_remove,
            .ctx = quad
        });
    }
}
