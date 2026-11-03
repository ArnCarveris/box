/**
 * @file geometry/shapes2d.c
 * @brief Resolves Rect, Circle and Line with their Border and Image into Shape2D and ShapeMesh.
 *
 * Every 2D shape shares one cached unit quad; the renderer shades it with a signed distance.
 * The nine parts of an ImageSlice are resolved here too, for the renderer and picking.
 */

#include "shape2d.h"
#include <math.h>

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
        out->width = glm_max(line->width, 0.0f);
        result = out->width > 0.0f && (line->x1 != line->x2 || line->y1 != line->y2);
    }

    if (out->kind == BOX_SHAPE2D_RECT) {
        scale[0] = out->size[0];
        scale[1] = out->size[1];
    } else if (out->kind == BOX_SHAPE2D_LINE) {
        scale[0] = 2.0f * glm_max(fabsf(out->line[0]), fabsf(out->line[2])) + out->width;
        scale[1] = 2.0f * glm_max(fabsf(out->line[1]), fabsf(out->line[3])) + out->width;
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
    const ImageSlice *slice = box_shape2d_get(world, e, ecs_id(ImageSlice), ignore);
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
        out->texture = image->texture;
        out->uv[0] = image->u1 - image->u0;
        out->uv[1] = image->v1 - image->v0;
        out->uv[2] = image->u0;
        out->uv[3] = image->v0;
        out->sliced = slice && out->kind == BOX_SHAPE2D_RECT &&
            box_shape2d_get(world, e, ecs_id(Rect), ignore) != NULL;
    }
}

/** Bring the ShapeMesh and Shape2D of an entity up to date with its 2D components.
 *
 * Both come with the shape through the With trait, so they are written in place; ShapeMesh
 * only when its mesh or scale changes. An entity that lost its last 2D shape loses them through
 * the stage. A line scales the quad to bounds centered on the origin that hold both ends.
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

    if (mesh && (!mesh->mesh != !drawn || ecs_os_memcmp(mesh->scale, scale, ECS_SIZEOF(vec3)))) {
        uint32_t handle = drawn ? box_mesh_cache_acquire(shape->cache, shape, NULL) : 0;
        box_shape_mesh_release(shape->cache, mesh);
        mesh->mesh = handle;
        mesh->render_mesh = 0;
        glm_vec3_copy(scale, mesh->scale);
        ecs_modified(it->stage, e, ShapeMesh);
    }

done:
    return;
}

/** Resolve the slices of one axis of a sliced Image: the edges of the three parts before the
 * corners are fit and the texture coordinates at them.
 *
 * @param lo Inset at the start in texels
 * @param hi Inset at the end in texels
 * @param texels Texels of the area along the axis
 * @param c0 Texture coordinate of the start of the area
 * @param c1 Texture coordinate of the end of the area
 * @param coords Receives the texture coordinates at the four edges
 * @param insets Receives the clamped insets in texels
 */
static void box_image_slice_axis(
    float lo,
    float hi,
    float texels,
    float c0,
    float c1,
    float coords[4],
    float insets[2])
{
    lo = isfinite(lo) ? glm_clamp(lo, 0.0f, texels) : 0.0f;
    hi = isfinite(hi) ? glm_clamp(hi, 0.0f, texels - lo) : 0.0f;
    insets[0] = lo;
    insets[1] = hi;
    coords[0] = c0;
    coords[1] = c0 + (c1 - c0) * (lo / texels);
    coords[2] = c1 - (c1 - c0) * (hi / texels);
    coords[3] = c1;
}

bool box_image_slice_resolve(
    const Image *image,
    const ImageSlice *slice,
    float texture_width,
    float texture_height,
    float width,
    float height,
    box_image_slice_t *out)
{
    float tw = fabsf(image->u1 - image->u0) * texture_width;
    float th = fabsf(image->v1 - image->v0) * texture_height;
    float ix[2], iy[2];
    bool result = isfinite(tw) && isfinite(th) && tw > 0.0f && th > 0.0f &&
        isfinite(width) && isfinite(height) && width > 0.0f && height > 0.0f;
    ecs_os_zeromem(out);
    out->x[2] = out->x[3] = width;
    out->y[2] = out->y[3] = height;
    out->u[0] = out->u[1] = image->u0;
    out->u[2] = out->u[3] = image->u1;
    out->v[0] = out->v[1] = image->v0;
    out->v[2] = out->v[3] = image->v1;
    out->scale = 1.0f;
    if (!result) {
        goto done;
    }

    float scale = isfinite(slice->scale) && slice->scale > 0.0f ? slice->scale : 1.0f;
    box_image_slice_axis(slice->left, slice->right, tw, image->u0, image->u1, out->u, ix);
    box_image_slice_axis(slice->top, slice->bottom, th, image->v0, image->v1, out->v, iy);
    float fit = 1.0f, across = (ix[0] + ix[1]) * scale, down = (iy[0] + iy[1]) * scale;
    if (across > width) {
        fit = width / across;
    }

    if (down > height) {
        fit = glm_min(fit, height / down);
    }

    scale *= fit;
    out->scale = scale;
    out->x[1] = ix[0] * scale;
    out->x[2] = glm_max(width - ix[1] * scale, out->x[1]);
    out->y[1] = iy[0] * scale;
    out->y[2] = glm_max(height - iy[1] * scale, out->y[1]);
    float mid_x = (tw - ix[0] - ix[1]) * scale, mid_y = (th - iy[0] - iy[1]) * scale;
    if (slice->edges == SliceTile) {
        out->period[0] = mid_x;
        out->period[1] = mid_y;
    }

    if (slice->center == SliceTile) {
        out->period[2] = mid_x;
        out->period[3] = mid_y;
    }

    out->hollow = slice->hollow;
done:
    return result;
}

/** Map a position along one axis of a sliced Image to its texture coordinate.
 *
 * @param p The position
 * @param edges The edges of the parts
 * @param coords The texture coordinates at the edges
 * @param period Length the middle part repeats at, 0 when it stretches
 * @return The texture coordinate
 */
static float box_image_slice_axis_map(
    float p,
    const float edges[4],
    const float coords[4],
    float period)
{
    float t, result;
    if (p < edges[1]) {
        t = (p - edges[0]) / glm_max(edges[1] - edges[0], 1e-9f);
        result = coords[0] + (coords[1] - coords[0]) * t;
    } else if (p > edges[2]) {
        t = (p - edges[2]) / glm_max(edges[3] - edges[2], 1e-9f);
        result = coords[2] + (coords[3] - coords[2]) * t;
    } else {
        if (period > 0.0f) {
            t = (p - edges[1]) / period;
            t -= floorf(t);
        } else {
            t = (p - edges[1]) / glm_max(edges[2] - edges[1], 1e-9f);
        }

        result = coords[1] + (coords[2] - coords[1]) * t;
    }

    return result;
}

bool box_image_slice_map(
    const box_image_slice_t *slice,
    float x,
    float y,
    float uv[2])
{
    bool mid_x = x >= slice->x[1] && x <= slice->x[2];
    bool mid_y = y >= slice->y[1] && y <= slice->y[2];
    uv[0] = box_image_slice_axis_map(x, slice->x, slice->u,
        slice->period[mid_y ? 2 : 0]);
    uv[1] = box_image_slice_axis_map(y, slice->y, slice->v,
        slice->period[mid_x ? 3 : 1]);

    return !(slice->hollow && mid_x && mid_y);
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

/** Construct BoxShadow values with an opaque black shadow.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti The type info
 */
static void box_box_shadow_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    BoxShadow *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (BoxShadow){ .color = { 0, 0, 0, 255 } };
    }
}

/** Construct Line values one unit wide.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti The type info
 */
static void box_line_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Line *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Line){ .width = 1.0f };
    }
}

/** Construct Image values that show the whole texture.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti The type info
 */
static void box_image_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Image *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Image){ .u1 = 1.0f, .v1 = 1.0f };
    }
}

/** Construct ImageSlice values without insets at a scale of 1.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti The type info
 */
static void box_image_slice_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ImageSlice *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (ImageSlice){ .scale = 1.0f };
    }
}

void box_geometry_shapes2d_init(
    ecs_world_t *world)
{
    ecs_entity_t shapes[3] = { ecs_id(Rect), ecs_id(Circle), ecs_id(Line) };
    ecs_entity_t parts[6] = {
        ecs_id(Rect), ecs_id(Circle), ecs_id(Line), ecs_id(Border), ecs_id(Image),
        ecs_id(ImageSlice)
    };
    int32_t i;
    ECS_COMPONENT_DEFINE(world, Shape2D);
    ecs_set_hooks(world, Shape2D, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, Border, { .ctor = box_border_ctor });
    ecs_set_hooks(world, BoxShadow, { .ctor = box_box_shadow_ctor });
    ecs_set_hooks(world, Line, { .ctor = box_line_ctor });
    ecs_set_hooks(world, Image, { .ctor = box_image_ctor });
    ecs_set_hooks(world, ImageSlice, { .ctor = box_image_slice_ctor });
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

    for (i = 0; i < 6; i ++) {
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
