/**
 * @file PickTexture.c
 * @brief Picking onto the texture of a shape that shows a surface: a shape with a
 * TextureBlend maps the hit to the coordinates of its mask, an Image with an ImageSlice through
 * its corners at the size of the surface in pixels.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "picking.h"
#include "render.h"

/** A screen UI scene rendered into screen_texture, 200 x 100 points, with a button at x 20..80
 * and y 20..80 of the surface, a camera at 0, 0, 10 and a Quad of 4 x 2 at the origin, whose UV
 * u runs from 0 at x -2 to 1 at x 2 */
#define PICK_SCENE \
    "using box.transform\n" \
    "using box.geometry\n" \
    "using box.material\n" \
    "using box.camera\n" \
    "using box.view\n" \
    "using box.surface\n" \
    "using box.picking\n" \
    "using box.input\n" \
    "screen_texture {}\n" \
    "screen_ui {\n" \
    "  Scene: {}\n" \
    "  camera {\n" \
    "    Position: {0, 0, 100}\n" \
    "    Camera: {ortho: true, ortho_scale: 1}\n" \
    "  }\n" \
    "  button {\n" \
    "    Position: {-50, 0, 0}\n" \
    "    Rect: {60, 60}\n" \
    "    Interactive\n" \
    "  }\n" \
    "}\n" \
    "screen_views {\n" \
    "  view {\n" \
    "    View: {camera: screen_ui.camera}\n" \
    "  }\n" \
    "}\n" \
    "screen_surface {\n" \
    "  Surface: {width: 200, height: 100, texture: screen_texture, view_root: screen_views}\n" \
    "}\n" \
    "camera {\n" \
    "  Position: {0, 0, 10}\n" \
    "  Camera: {}\n" \
    "}\n" \
    "main_view {\n" \
    "  View: {camera: camera}\n" \
    "}\n" \
    "quad {\n" \
    "  Position: {0, 0, 0}\n" \
    "  Quad: {4, 2}\n" \
    "}\n"

/** Look up an entity that must exist.
 *
 * @param world The world
 * @param path Its path
 * @return The entity
 */
static ecs_entity_t pick_e(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t result = ecs_lookup(world, path);
    test_assert(result != 0);

    return result;
}

/** Move the pointer to a point of the window on the horizontal center line, run a frame and get
 * the target.
 *
 * @param world The world
 * @param x Cursor x in window points
 * @return The target of the pointer events
 */
static ecs_entity_t pick_at(
    ecs_world_t *world,
    float x)
{
    box_input_mouse_move(world, x, 360, false);
    ecs_progress(world, 0);

    return ecs_singleton_get(world, PickStatus)->target;
}

/** A pick onto a blended plane whose layer shows the surface maps the hit to the coordinates of
 * the mask: the UV of the quad through its own TextureTransform, not that of the layer.
 *
 * At x 0.8 (window x 690) u 0.7 plus the offset of the quad wraps to surface x 40, on the
 * button; the layer transform would give x 170. At x -0.9 (584) the mask is at x 155, beside it.
 *
 * Without the blend the quad shows its own albedo, which no surface renders. */
void PickTexture_blend_mask_coordinates(void) {
    // ui/picking
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script(world, { .code = PICK_SCENE }) != 0);
    ecs_entity_t texture = pick_e(world, "screen_texture");
    ecs_entity_t quad = pick_e(world, "quad");
    ecs_entity_t button = pick_e(world, "screen_ui.button");
    ecs_entity_t other = ecs_new(world);
    ecs_entity_t layer = ecs_new(world);
    ecs_set(world, layer, PbrTextures, {.albedo = texture});
    ecs_set(world, layer, TextureTransform, {.scale_x = 3, .scale_y = 1, .offset_x = 0.25f});
    ecs_set(world, quad, PbrTextures, {.albedo = other});
    ecs_set(world, quad, TextureBlend, {.layers = {layer}});
    ecs_set(world, quad, TextureTransform, {.scale_x = 1, .scale_y = 1, .offset_x = 0.5f});
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    test_uint(pick_at(world, 690), button);

    test_assert(pick_at(world, 584) != button);
    test_uint(ecs_singleton_get(world, PickStatus)->hit, quad);

    ecs_remove(world, quad, TextureBlend);
    ecs_progress(world, 0);
    test_assert(pick_at(world, 690) != button);
    test_uint(ecs_singleton_get(world, PickStatus)->hit, quad);

    ecs_fini(world);
}

/** A screen UI rendered into screen_texture, 200 x 100 points at 2 pixels per point, with a
 * button at x 10..20 and y 10..20 of the surface, in its top left corner, a corner_button at
 * x 180..190 and y 80..90, in its bottom right corner, and a HUD whose panel
 * of 800 x 400 at the center of the window shows the surface with the corners sliced: insets of
 * 40 texels, 20 points of the surface, at a scale of 1 */
#define PICK_SLICE_SCENE \
    "using box.transform\n" \
    "using box.geometry\n" \
    "using box.camera\n" \
    "using box.view\n" \
    "using box.surface\n" \
    "using box.picking\n" \
    "screen_texture {}\n" \
    "screen_ui {\n" \
    "  Scene: {}\n" \
    "  camera {\n" \
    "    Position: {0, 0, 100}\n" \
    "    Camera: {ortho: true, ortho_scale: 1}\n" \
    "  }\n" \
    "  button {\n" \
    "    Position: {-85, 35, 0}\n" \
    "    Rect: {10, 10}\n" \
    "    Interactive\n" \
    "  }\n" \
    "  corner_button {\n" \
    "    Position: {85, -35, 0}\n" \
    "    Rect: {10, 10}\n" \
    "    Interactive\n" \
    "  }\n" \
    "}\n" \
    "screen_views {\n" \
    "  view {\n" \
    "    View: {camera: screen_ui.camera}\n" \
    "  }\n" \
    "}\n" \
    "screen_surface {\n" \
    "  Surface: {width: 200, height: 100, pixel_ratio: 2, texture: screen_texture,\n" \
    "    view_root: screen_views}\n" \
    "}\n" \
    "hud {\n" \
    "  Scene: {}\n" \
    "  camera {\n" \
    "    Position: {0, 0, 100}\n" \
    "    Camera: {ortho: true, ortho_scale: 1}\n" \
    "  }\n" \
    "  panel {\n" \
    "    Position: {0, 0, 0}\n" \
    "    Rect: {800, 400}\n" \
    "    Image: {texture: screen_texture}\n" \
    "    ImageSlice: {left: 40, top: 40, right: 40, bottom: 40}\n" \
    "  }\n" \
    "}\n" \
    "hud_view {\n" \
    "  View: {camera: hud.camera}\n" \
    "}\n"

/** A pick in the corner of an Image with an ImageSlice that shows a surface maps through the
 * corner, at the size of the texture in pixels of the surface: 30, 30 points into the panel
 * (window 270, 190) is 3 / 4 into the 40 unit corner, surface point 15, 15, on the button. As a
 * plain stretched Image it would be surface point 7.5, 7.5; with the insets counted in points of
 * the surface instead of its 400 x 200 pixels, 30, 30. */
void PickTexture_image_slice_corner(void) {
    // ui/picking
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    test_assert(ecs_script(world, { .code = PICK_SLICE_SCENE }) != 0);
    ecs_entity_t panel = pick_e(world, "hud.panel");
    ecs_entity_t button = pick_e(world, "screen_ui.button");
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    const SurfaceStatus *st = ecs_get(world, pick_e(world, "screen_surface"), SurfaceStatus);
    test_assert(st != NULL);
    test_int(st->width, 400);
    test_int(st->height, 200);

    box_input_mouse_move(world, 270, 190, false);
    ecs_progress(world, 0);
    const PickStatus *ps = ecs_singleton_get(world, PickStatus);
    test_uint(ps->target, button);
    test_assert(fabsf(ps->point[0] - -85.0f) < 0.5f);
    test_assert(fabsf(ps->point[1] - 35.0f) < 0.5f);

    /* The bottom right corner maps the same way to surface point 185, 85 */
    box_input_mouse_move(world, 1010, 530, false);
    ecs_progress(world, 0);
    ps = ecs_singleton_get(world, PickStatus);
    test_uint(ps->hit, panel);
    test_uint(ps->target, pick_e(world, "screen_ui.corner_button"));
    test_assert(fabsf(ps->point[0] - 85.0f) < 0.5f);
    test_assert(fabsf(ps->point[1] - -35.0f) < 0.5f);

    /* Without the ImageSlice the panel stretches the surface and the pick misses the button */
    ecs_remove(world, panel, ImageSlice);
    ecs_progress(world, 0);
    box_input_mouse_move(world, 270, 190, false);
    ecs_progress(world, 0);
    ps = ecs_singleton_get(world, PickStatus);
    test_assert(ps->target != button);
    test_uint(ps->hit, panel);

    ecs_fini(world);
    box_render_null_free(null);
}
