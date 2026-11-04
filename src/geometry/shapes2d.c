/**
 * @file geometry/shapes2d.c
 * @brief Resolves Rect, Circle, Line and Polygon2D with their Border and Image into Shape2D and
 * ShapeMesh.
 *
 * Every 2D shape shares one cached unit quad; the renderer shades it with a signed distance.
 * The nine parts of an ImageSlice are resolved here too, for the renderer and picking, and the
 * edges of a Polygon2D, sampled from its path as the profiles sample theirs.
 */

#include "shape2d.h"
#include "paths.h"
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

/** Append the edges of one closed or open ring of points, scaled and offset.
 *
 * @param points The points
 * @param count Number of points
 * @param closed Whether the ring closes back to its first point
 * @param polygon The polygon, its scale and offset
 * @param edges Receives the edges
 */
static void box_shape2d_ring(
    const vec2 *points,
    int32_t count,
    bool closed,
    const Polygon2D *polygon,
    ecs_vec_t *edges)
{
    int32_t i, last = closed ? count : count - 1;
    for (i = 0; i < last; i ++) {
        const float *a = points[i], *b = points[(i + 1) % count];
        float *out = *ecs_vec_append_t(NULL, edges, vec4);
        out[0] = a[0] * polygon->scale[0] + polygon->offset[0];
        out[1] = a[1] * polygon->scale[1] + polygon->offset[1];
        out[2] = b[0] * polygon->scale[0] + polygon->offset[0];
        out[3] = b[1] * polygon->scale[1] + polygon->offset[1];
    }
}

/** Append the edges of a Path3D seen from above, its x and z as the x and y of the polygon.
 *
 * @param world The world
 * @param asset The entity with the Path3D
 * @param polygon The polygon, its scale and offset
 * @param edges Receives the edges
 * @return Whether the path has enough points
 */
static bool box_shape2d_path3(
    const ecs_world_t *world,
    ecs_entity_t asset,
    const Polygon2D *polygon,
    ecs_vec_t *edges)
{
    const Path3D *path = ecs_get(world, asset, Path3D);
    ecs_vec_t samples = {0};
    bool result = box_path3_sample(world, asset, &samples);
    int32_t i, count = ecs_vec_count(&samples);
    result = result && count >= (path->closed ? 3 : 2);
    if (result) {
        vec2 *points = ecs_os_malloc_n(vec2, count);
        const vec3 *p = ecs_vec_first(&samples);
        for (i = 0; i < count; i ++) {
            points[i][0] = p[i][0];
            points[i][1] = p[i][2];
        }

        box_shape2d_ring(points, count, path->closed, polygon, edges);
        ecs_os_free(points);
    }

    ecs_vec_fini_t(NULL, &samples, vec3);

    return result;
}

bool box_shape2d_polygon_edges(
    const ecs_world_t *world,
    const Polygon2D *polygon,
    ecs_vec_t *edges,
    bool *open)
{
    ecs_entity_t asset = polygon->path;
    bool alive = asset && ecs_is_alive(world, asset);
    const Profile2D *profile = alive ? ecs_get(world, asset, Profile2D) : NULL;
    const Path2D *path = alive && !profile ? ecs_get(world, asset, Path2D) : NULL;
    const Path3D *path3 = alive && !profile && !path ? ecs_get(world, asset, Path3D) : NULL;
    int32_t holes = profile ? ecs_vec_count(&profile->holes) : 0;
    const ecs_entity_t *hole = profile ? ecs_vec_first(&profile->holes) : NULL;
    int32_t ring, first = ecs_vec_count(edges);
    ecs_vec_t samples = {0};
    bool result = (profile || path) && (!holes || hole);
    *open = (path && !path->closed) || (path3 && !path3->closed);
    if (path3) {
        result = box_shape2d_path3(world, asset, polygon, edges);
        holes = -1;
    }

    for (ring = 0; result && ring <= holes; ring ++) {
        ecs_entity_t e = profile ? (ring ? hole[ring - 1] : profile->outer) : asset;
        const Path2D *p = e && ecs_is_alive(world, e) ? ecs_get(world, e, Path2D) : NULL;
        result = p && (p->closed || (!profile && *open));
        ecs_vec_clear(&samples);
        result = result && box_path2_sample(world, e, &samples);
        int32_t count = ecs_vec_count(&samples);
        result = result && count >= (*open ? 2 : 3);
        if (result) {
            box_shape2d_ring(ecs_vec_first(&samples), count, !*open, polygon, edges);
        }
    }

    if (!result) {
        ecs_vec_set_count_t(NULL, edges, vec4, first);
    }

    ecs_vec_fini_t(NULL, &samples, vec2);

    return result;
}

float box_shape2d_polygon_distance(
    const vec4 *edges,
    int32_t count,
    float stroke,
    float x,
    float y)
{
    float best = INFINITY;
    bool inside = false;
    int32_t i;
    for (i = 0; i < count; i ++) {
        float ax = edges[i][0], ay = edges[i][1], ex = edges[i][2] - ax, ey = edges[i][3] - ay;
        float wx = x - ax, wy = y - ay, len = ex * ex + ey * ey;
        float t = len > 0.0f ? glm_clamp((wx * ex + wy * ey) / len, 0.0f, 1.0f) : 0.0f;
        float dx = wx - ex * t, dy = wy - ey * t;
        best = glm_min(best, dx * dx + dy * dy);
        if ((ay > y) != (edges[i][3] > y) && x < ax + (y - ay) * ex / ey) {
            inside = !inside;
        }
    }

    float d = sqrtf(best);

    return stroke > 0.0f ? d - stroke : (inside ? -d : d);
}

bool box_shape2d_polygon_contains(
    const ecs_world_t *world,
    ecs_entity_t e,
    float x,
    float y)
{
    const Shape2D *shape = ecs_get(world, e, Shape2D);
    const Polygon2D *polygon = shape && shape->kind == BOX_SHAPE2D_POLYGON
        ? ecs_get(world, e, Polygon2D) : NULL;
    ecs_vec_t edges = {0};
    bool open, result = false;
    if (polygon && box_shape2d_polygon_edges(world, polygon, &edges, &open)) {
        result = box_shape2d_polygon_distance(ecs_vec_first(&edges), ecs_vec_count(&edges),
            open ? shape->width * 0.5f : 0.0f, x, y) <= 0.0f;
    }

    ecs_vec_fini_t(NULL, &edges, vec4);

    return result;
}

/** Resolve the bounds of a polygon: of its edges, grown by half the width of a stroke.
 *
 * @param world The world
 * @param polygon The polygon
 * @param out The resolved shape
 * @return Whether the polygon has an outline of at most BOX_POLYGON2D_MAX_EDGES edges with an
 * area, or a stroke of a width
 */
static bool box_shape2d_polygon_bounds(
    const ecs_world_t *world,
    const Polygon2D *polygon,
    Shape2D *out)
{
    ecs_vec_t edges = {0};
    float lo[2] = { INFINITY, INFINITY }, hi[2] = { -INFINITY, -INFINITY };
    int32_t i, count = 0;
    bool open = false;
    bool result = box_shape2d_polygon_edges(world, polygon, &edges, &open);
    const vec4 *e = ecs_vec_first(&edges);
    count = ecs_vec_count(&edges);
    for (i = 0; i < count; i ++) {
        lo[0] = glm_min(lo[0], glm_min(e[i][0], e[i][2]));
        lo[1] = glm_min(lo[1], glm_min(e[i][1], e[i][3]));
        hi[0] = glm_max(hi[0], glm_max(e[i][0], e[i][2]));
        hi[1] = glm_max(hi[1], glm_max(e[i][1], e[i][3]));
    }

    float grow = 0.0f;
    if (open) {
        out->width = isfinite(polygon->width) ? glm_max(polygon->width, 0.0f) : 0.0f;
        grow = out->width * 0.5f;
        result = result && out->width > 0.0f;
    }

    if (count && result) {
        out->center[0] = (lo[0] + hi[0]) * 0.5f;
        out->center[1] = (lo[1] + hi[1]) * 0.5f;
        out->size[0] = hi[0] - lo[0] + 2.0f * grow;
        out->size[1] = hi[1] - lo[1] + 2.0f * grow;
    }

    out->edges = count;
    result = result && count > 0 && count <= BOX_POLYGON2D_MAX_EDGES &&
        isfinite(out->size[0]) && isfinite(out->size[1]) && isfinite(out->center[0]) &&
        isfinite(out->center[1]) && out->size[0] > 0.0f && out->size[1] > 0.0f;
    if (count > BOX_POLYGON2D_MAX_EDGES) {
        ecs_warn("Polygon2D: %d edges, more than the %d drawn", count,
            BOX_POLYGON2D_MAX_EDGES);
    }

    ecs_vec_fini_t(NULL, &edges, vec4);

    return result;
}

/** Resolve the geometry of an entity: a Rect, else a Circle, else a Line, else a Polygon2D.
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
    const Polygon2D *polygon = box_shape2d_get(world, e, ecs_id(Polygon2D), ignore);
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
    } else if (polygon) {
        out->kind = BOX_SHAPE2D_POLYGON;
        result = box_shape2d_polygon_bounds(world, polygon, out);
    }

    if (out->kind == BOX_SHAPE2D_RECT) {
        scale[0] = out->size[0];
        scale[1] = out->size[1];
    } else if (out->kind == BOX_SHAPE2D_LINE) {
        scale[0] = 2.0f * glm_max(fabsf(out->line[0]), fabsf(out->line[2])) + out->width;
        scale[1] = 2.0f * glm_max(fabsf(out->line[1]), fabsf(out->line[3])) + out->width;
    } else if (out->kind == BOX_SHAPE2D_POLYGON) {
        scale[0] = 2.0f * (fabsf(out->center[0]) + out->size[0] * 0.5f);
        scale[1] = 2.0f * (fabsf(out->center[1]) + out->size[1] * 0.5f);
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

/** Construct Polygon2D values at a scale of 1 with a stroke one unit wide.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti The type info
 */
static void box_polygon2d_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Polygon2D *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Polygon2D){ .scale = { 1.0f, 1.0f }, .width = 1.0f };
    }
}

/** Check whether a polygon draws an asset: its Path2D or Path3D, its Profile2D or a path of it.
 *
 * @param world The world
 * @param polygon The polygon
 * @param asset The asset entity
 * @return Whether the polygon uses it
 */
static bool box_polygon2d_uses(
    const ecs_world_t *world,
    const Polygon2D *polygon,
    ecs_entity_t asset)
{
    const Profile2D *profile;
    bool result = polygon->path == asset;
    if (!result && polygon->path && ecs_is_alive(world, polygon->path)) {
        profile = ecs_get(world, polygon->path, Profile2D);
        if (profile) {
            const ecs_entity_t *holes = ecs_vec_first(&profile->holes);
            int32_t i;
            result = profile->outer == asset;
            for (i = 0; !result && i < ecs_vec_count(&profile->holes); i ++) {
                result = holes[i] == asset;
            }
        }
    }

    return result;
}

/** Resolve the polygons again whose Path2D, Path3D or Profile2D changed.
 *
 * @param it The observer iterator
 */
static void box_polygon2d_asset_changed(
    ecs_iter_t *it)
{
    ecs_query_t *query = ecs_query(it->world, {
        .terms = {{ .id = ecs_id(Polygon2D), .src.id = EcsSelf }},
        .flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled
    });
    ecs_iter_t qit = ecs_query_iter(it->world, query);
    while (ecs_query_next(&qit)) {
        const Polygon2D *polygons = ecs_field(&qit, Polygon2D, 0);
        int32_t i, j;
        for (i = 0; i < qit.count; i ++) {
            bool affected = false;
            for (j = 0; !affected && j < it->count; j ++) {
                affected = box_polygon2d_uses(it->world, &polygons[i], it->entities[j]);
            }

            if (affected) {
                ecs_modified(it->world, qit.entities[i], Polygon2D);
            }
        }
    }

    ecs_query_fini(query);
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
    ecs_entity_t shapes[4] = { ecs_id(Rect), ecs_id(Circle), ecs_id(Line), ecs_id(Polygon2D) };
    ecs_entity_t parts[7] = {
        ecs_id(Rect), ecs_id(Circle), ecs_id(Line), ecs_id(Polygon2D), ecs_id(Border),
        ecs_id(Image), ecs_id(ImageSlice)
    };
    ecs_entity_t assets[3] = { ecs_id(Path2D), ecs_id(Path3D), ecs_id(Profile2D) };
    int32_t i;
    ECS_COMPONENT_DEFINE(world, Shape2D);
    ecs_set_hooks(world, Shape2D, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, Border, { .ctor = box_border_ctor });
    ecs_set_hooks(world, BoxShadow, { .ctor = box_box_shadow_ctor });
    ecs_set_hooks(world, Line, { .ctor = box_line_ctor });
    ecs_set_hooks(world, Polygon2D, { .ctor = box_polygon2d_ctor });
    ecs_set_hooks(world, Image, { .ctor = box_image_ctor });
    ecs_set_hooks(world, ImageSlice, { .ctor = box_image_slice_ctor });
    box_shape_t *quad = box_mesh_cache_add_shape(box_mesh_cache_of(world),
        &(box_shape_desc_t){
            .component = ecs_id(Rect),
            .key = box_shape2d_key,
            .generate = box_shape2d_generate
        });
    for (i = 0; i < 4; i ++) {
        ecs_add_pair(world, shapes[i], EcsWith, ecs_id(ShapeMesh));
        ecs_add_pair(world, shapes[i], EcsWith, ecs_id(Shape2D));
    }

    for (i = 0; i < 7; i ++) {
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

    for (i = 0; i < 3; i ++) {
        ecs_observer(world, {
            .query.terms = {{ .id = assets[i] }},
            .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
            .events = { EcsOnSet },
            .callback = box_polygon2d_asset_changed
        });
    }
}
