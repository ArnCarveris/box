/**
 * @file geometry/shape2d.c
 * @brief The 2D shapes: rectangles, circles, lines, texture regions and borders.
 *
 * A rectangle, a circle and a line pick a cached mesh like every other shape.
 * An image picks a quad whose uvs carry the region of the texture it shows, so
 * the region and its rotation are baked once and cost nothing per frame. A
 * border carries no geometry at all: it writes the corner radii, the edge
 * widths and its color into a RenderShape2D, which extract packs into the
 * instance and the fragment stage evaluates, which is why changing a radius
 * never rebuilds a mesh.
 */

#include "geometry_internal.h"
#include "../render/render_internal.h"

/** Assign the cached mesh of a key to an entity with the given instance scale.
 *
 * The mesh is set through the stage of the observer, because a shape that
 * arrives with an instantiated prefab is set while the entity is being moved to
 * its table, where a direct set would move it a second time.
 *
 * @param it The observer iterator
 * @param e The entity
 * @param key The mesh key
 * @param scale The instance scale
 */
static void box_shape2d_apply(
    ecs_iter_t *it,
    ecs_entity_t e,
    const box_mesh_key_t *key,
    vec3 scale)
{
    RenderMesh mesh = { .scale = scale };
    mesh.handle = box_mesh_cache_get(it->world, key, &mesh.bounds_min, &mesh.bounds_max);
    ecs_set_ptr(it->stage, e, RenderMesh, &mesh);
}

/** Pixel size of the texture an image measures its fit against.
 *
 * @param world The world
 * @param e The image entity
 * @param img The image
 * @param width Receives the width, left alone when no texture is uploaded
 * @param height Receives the height, left alone when no texture is uploaded
 */
static void box_shape2d_texture_size(
    ecs_world_t *world,
    ecs_entity_t e,
    const Image *img,
    int32_t *width,
    int32_t *height)
{
    ecs_entity_t tex = img->texture;
    if (!tex) {
        const PbrTextures *slots = ecs_get(world, e, PbrTextures);

        tex = slots ? slots->albedo : 0;
    }

    if (!tex || !ecs_is_alive(world, tex)) {
        goto done;
    }

    const RenderTexture *uploaded = ecs_get(world, tex, RenderTexture);
    if (uploaded && uploaded->width > 0 && uploaded->height > 0) {
        *width = uploaded->width;
        *height = uploaded->height;
    }
done:
    return;
}

/** Resolve the uv region an image shows, applying its fit mode.
 *
 * Stretch takes the region as it is. Cover shrinks the longer side of the
 * region until what is left has the aspect of the rectangle, which crops the
 * texture, and Contain grows the shorter side instead, which shows more of the
 * texture around the region. Both need the pixel size of the texture, because
 * a region is a fraction of an image whose own aspect decides what undistorted
 * means.
 *
 * @param world The world
 * @param e The image entity
 * @param img The image
 * @param rect The rectangle it is drawn on
 * @param out Four floats receiving u0, v0, u1 and v1
 */
static void box_shape2d_region(
    ecs_world_t *world,
    ecs_entity_t e,
    const Image *img,
    const Rect *rect,
    float *out)
{
    out[0] = img->u0;
    out[1] = img->v0;
    out[2] = img->u1 > img->u0 ? img->u1 : 1.0f;
    out[3] = img->v1 > img->v0 ? img->v1 : 1.0f;
    if (img->fit == ImageStretch || rect->width <= 0.0f || rect->height <= 0.0f) {
        goto done;
    }

    int32_t tex_w = 1;
    int32_t tex_h = 1;
    box_shape2d_texture_size(world, e, img, &tex_w, &tex_h);

    float span_u = (out[2] - out[0]) * (float)tex_w;
    float span_v = (out[3] - out[1]) * (float)tex_h;
    if (span_u <= 0.0f || span_v <= 0.0f) {
        goto done;
    }

    float ratio = (span_u / span_v) / (rect->width / rect->height);
    float scale_u = 1.0f;
    float scale_v = 1.0f;
    if (img->fit == ImageCover) {
        scale_u = ratio > 1.0f ? 1.0f / ratio : 1.0f;
        scale_v = ratio > 1.0f ? 1.0f : ratio;
    } else {
        scale_u = ratio > 1.0f ? 1.0f : 1.0f / ratio;
        scale_v = ratio > 1.0f ? ratio : 1.0f;
    }

    float center_u = (out[0] + out[2]) * 0.5f;
    float center_v = (out[1] + out[3]) * 0.5f;
    float half_u = (out[2] - out[0]) * 0.5f * scale_u;
    float half_v = (out[3] - out[1]) * 0.5f * scale_v;

    out[0] = center_u - half_u;
    out[1] = center_v - half_v;
    out[2] = center_u + half_u;
    out[3] = center_v + half_v;
done:
    return;
}

/** Give one entity the mesh of its rectangle, showing an image when it has one.
 *
 * @param it The observer iterator
 * @param e The entity
 * @param rect Its rectangle
 */
static void box_shape2d_rect(
    ecs_iter_t *it,
    ecs_entity_t e,
    const Rect *rect)
{
    const Image *img = ecs_get(it->world, e, Image);
    box_mesh_key_t key = { .kind = BoxShapeQuad };

    if (img) {
        float region[4];

        box_shape2d_region(it->world, e, img, rect, region);
        key.kind = BoxShapeImage;
        key.f0 = region[0];
        key.f1 = region[1];
        key.f2 = region[2];
        key.f3 = region[3];
        key.f4 = img->rotation;
    }

    box_shape2d_apply(it, e, &key, vec3_new(rect->width, rect->height, 1.0f));
}

int32_t box_circle_segments(
    float radius)
{
    float r = radius > 0.0f ? radius : 1.0f;
    int32_t segments = (int32_t)(16.0f * sqrtf(r));

    segments = (segments + 7) & ~7;
    if (segments < 16) {
        segments = 16;
    }

    if (segments > 128) {
        segments = 128;
    }

    return segments;
}

float box_shape2d_distance(
    float x,
    float y,
    float half_width,
    float half_height,
    const float *radii)
{
    float top = x >= 0.0f ? radii[1] : radii[0];
    float bottom = x >= 0.0f ? radii[2] : radii[3];
    float r = y >= 0.0f ? top : bottom;
    float limit = half_width < half_height ? half_width : half_height;

    if (r > limit) {
        r = limit;
    }

    if (r < 0.0f) {
        r = 0.0f;
    }

    float qx = fabsf(x) - half_width + r;
    float qy = fabsf(y) - half_height + r;
    float outside_x = qx > 0.0f ? qx : 0.0f;
    float outside_y = qy > 0.0f ? qy : 0.0f;
    float inside = qx > qy ? qx : qy;

    return sqrtf(outside_x * outside_x + outside_y * outside_y) +
        (inside < 0.0f ? inside : 0.0f) - r;
}

float box_shape2d_coverage(
    float distance,
    float softness)
{
    float width = softness > 1e-6f ? softness : 1e-6f;
    float result = 0.5f - distance / width;

    if (result < 0.0f) {
        result = 0.0f;
    }

    if (result > 1.0f) {
        result = 1.0f;
    }

    return result;
}

/** Rect observer, which also picks up a change of the rectangle of an image.
 *
 * @param it Iterator over changed rectangles
 */
static void box_shape2d_on_rect(
    ecs_iter_t *it)
{
    Rect *r = ecs_field(it, Rect, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_shape2d_rect(it, it->entities[i], &r[i]);
    }
}

/** Circle observer.
 *
 * @param it Iterator over changed circles
 */
static void box_shape2d_on_circle(
    ecs_iter_t *it)
{
    Circle *c = ecs_field(it, Circle, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        float radius = c[i].radius > 0.0f ? c[i].radius : 1.0f;
        int32_t segments = c[i].segments > 0 ? c[i].segments : box_circle_segments(radius);
        box_mesh_key_t key = { .kind = BoxShapeNGon, .i0 = segments };

        box_shape2d_apply(it, it->entities[i], &key, vec3_new(radius, radius, 1.0f));
    }
}

/** Line observer.
 *
 * The endpoints and the thickness are divided by the largest of them, so two
 * lines of the same outline at different sizes share one cached mesh and only
 * differ in their instance scale.
 *
 * @param it Iterator over changed lines
 */
static void box_shape2d_on_line(
    ecs_iter_t *it)
{
    Line *l = ecs_field(it, Line, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        float span[5] = { l[i].x1, l[i].y1, l[i].x2, l[i].y2, l[i].thickness };
        float scale = 1e-6f;
        int32_t axis;
        for (axis = 0; axis < 5; axis ++) {
            float v = fabsf(span[axis]);

            scale = v > scale ? v : scale;
        }

        box_mesh_key_t key = {
            .kind = BoxShapeLine,
            .f0 = span[0] / scale, .f1 = span[1] / scale, .f2 = span[2] / scale,
            .f3 = span[3] / scale, .f4 = span[4] / scale
        };

        box_shape2d_apply(it, it->entities[i], &key, vec3_new(scale, scale, 1.0f));
    }
}

/** Image observer.
 *
 * @param it Iterator over changed images
 */
static void box_shape2d_on_image(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        const Rect *rect = ecs_get(it->world, it->entities[i], Rect);
        Rect unit = { 1.0f, 1.0f };

        box_shape2d_rect(it, it->entities[i], rect ? rect : &unit);
    }
}

/** Border observer, which resolves the shorthands and hands extract the shape.
 *
 * @param it Iterator over changed borders
 */
static void box_shape2d_on_border(
    ecs_iter_t *it)
{
    Border *b = ecs_field(it, Border, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        RenderShape2D shape;

        shape.radii[0] = b[i].radius_top_left > 0.0f ? b[i].radius_top_left : b[i].radius;
        shape.radii[1] = b[i].radius_top_right > 0.0f ? b[i].radius_top_right : b[i].radius;
        shape.radii[2] = b[i].radius_bottom_right > 0.0f ?
            b[i].radius_bottom_right : b[i].radius;
        shape.radii[3] = b[i].radius_bottom_left > 0.0f ? b[i].radius_bottom_left : b[i].radius;
        shape.widths[0] = b[i].top_width > 0.0f ? b[i].top_width : b[i].width;
        shape.widths[1] = b[i].right_width > 0.0f ? b[i].right_width : b[i].width;
        shape.widths[2] = b[i].bottom_width > 0.0f ? b[i].bottom_width : b[i].width;
        shape.widths[3] = b[i].left_width > 0.0f ? b[i].left_width : b[i].width;
        shape.color = b[i].color;
        ecs_set_ptr(it->stage, it->entities[i], RenderShape2D, &shape);
    }
}

/** A border went away: take its shape with it and re-extract the entity.
 *
 * @param it Iterator over the entities that lost the border
 */
static void box_shape2d_on_border_remove(
    ecs_iter_t *it)
{
    if (ecs_is_fini(it->world) || ecs_should_quit(it->world)) {
        goto done;
    }

    bool deleting = !it->other_table || !ecs_table_get_type(it->other_table)->count;
    int32_t i;
    for (i = 0; !deleting && i < it->count; i ++) {
        if (!ecs_has(it->world, it->entities[i], RenderShape2D)) {
            continue;
        }

        ecs_remove(it->stage, it->entities[i], RenderShape2D);
        box_render_mark_dirty(it->world, it->entities[i]);
    }
done:
    return;
}

/** Release the image query an observer holds.
 *
 * @param ptr The query
 */
static void box_shape2d_query_free(
    void *ptr)
{
    ecs_query_fini(ptr);
}

/** A texture finished uploading: images measuring their fit against it refit.
 *
 * @param it Iterator over the uploaded textures
 */
static void box_shape2d_on_texture(
    ecs_iter_t *it)
{
    ecs_iter_t images = ecs_query_iter(it->world, it->ctx);
    while (ecs_query_next(&images)) {
        int32_t i;
        for (i = 0; i < images.count; i ++) {
            const Rect *rect = ecs_get(it->world, images.entities[i], Rect);
            Rect unit = { 1.0f, 1.0f };

            box_shape2d_rect(it, images.entities[i], rect ? rect : &unit);
        }
    }
}

/** Register an OnSet observer for a 2D shape component.
 *
 * @param world The world
 * @param component The shape component
 * @param callback The observer callback
 */
static void box_shape2d_watch(
    ecs_world_t *world,
    ecs_entity_t component,
    ecs_iter_action_t callback)
{
    ecs_add_pair(world, component, EcsOnInstantiate, EcsInherit);
    ecs_observer(world, {
        .query.terms = {{ .id = component, .inout = EcsIn }},
        .events = { EcsOnSet },
        .callback = callback
    });
}

void box_shape2d_init(
    ecs_world_t *world)
{
    ecs_add_pair(world, ecs_id(Border), EcsWith, ecs_id(Rect));
    ecs_add_pair(world, ecs_id(Image), EcsWith, ecs_id(Rect));

    box_shape2d_watch(world, ecs_id(Rect), box_shape2d_on_rect);
    box_shape2d_watch(world, ecs_id(Circle), box_shape2d_on_circle);
    box_shape2d_watch(world, ecs_id(Line), box_shape2d_on_line);
    box_shape2d_watch(world, ecs_id(Image), box_shape2d_on_image);
    box_shape2d_watch(world, ecs_id(Border), box_shape2d_on_border);

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Border), .inout = EcsIn }},
        .events = { EcsOnRemove },
        .callback = box_shape2d_on_border_remove
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(RenderTexture), .inout = EcsIn }},
        .events = { EcsOnSet },
        .callback = box_shape2d_on_texture,
        .ctx = ecs_query(world, {
            .terms = {{ .id = ecs_id(Image), .inout = EcsIn }}
        }),
        .ctx_free = box_shape2d_query_free
    });
}
