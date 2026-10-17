/**
 * @file Clip.c
 * @brief ClipChildren: 2D shapes and text cut to the area of an element, in screen, world and
 * texture UI, and picking that follows the clip.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Create a headless world with an orthographic UI camera of 1 point per pixel, a view that
 * clears to black and a 64x64 surface that writes a file every frame.
 *
 * @param file The file of the surface
 * @return The world
 */
static ecs_world_t* clip_world(
    const char *file)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    if (file) {
        ecs_entity_t s = ecs_new(world);
        ecs_set(world, s, Surface, {.width = 64, .height = 64,
            .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    }

    return world;
}

/** Render frames and load the image a surface wrote.
 *
 * @param world The world
 * @param file The file of the surface
 * @param frames Frames to render
 * @return The pixels, RGBA, free with stbi_image_free
 */
static unsigned char* clip_render(
    ecs_world_t *world,
    const char *file,
    int32_t frames)
{
    int32_t i;
    int w = 0, h = 0, n = 0;
    for (i = 0; i < frames; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    ecs_os_free(path);
    test_assert(px != NULL);

    return px;
}

/** Check whether a pixel is the green of the clipped content.
 *
 * @param c The pixel
 * @return Whether it is mostly green
 */
static bool clip_green(
    const unsigned char *c)
{
    return c[1] > 128 && c[0] < 64 && c[2] < 64;
}

/** Check whether a pixel is black.
 *
 * @param c The pixel
 * @return Whether every channel is below 3
 */
static bool clip_black(
    const unsigned char *c)
{
    return c[0] < 3 && c[1] < 3 && c[2] < 3;
}

/** Get a pixel of a 64 pixel wide image.
 *
 * @param px The pixels
 * @param x The column
 * @param y The row
 * @return The pixel
 */
static const unsigned char* clip_at(
    const unsigned char *px,
    int32_t x,
    int32_t y)
{
    return &px[(y * 64 + x) * 4];
}

/** Count the green pixels of a row of a 64x64 image.
 *
 * @param px The pixels
 * @param y The row
 * @return The green pixels
 */
static int32_t clip_row(
    const unsigned char *px,
    int32_t y)
{
    int32_t x, result = 0;
    for (x = 0; x < 64; x ++) {
        result += clip_green(clip_at(px, x, y));
    }

    return result;
}

/** Count the green pixels of a column of a 64x64 image.
 *
 * @param px The pixels
 * @param x The column
 * @return The green pixels
 */
static int32_t clip_column(
    const unsigned char *px,
    int32_t x)
{
    int32_t y, result = 0;
    for (y = 0; y < 64; y ++) {
        result += clip_green(clip_at(px, x, y));
    }

    return result;
}

/** Create a clip parent of 20x20 points at the origin with a 60x10 green child.
 *
 * @param world The world
 * @param child Receives the child
 * @return The parent
 */
static ecs_entity_t clip_panel(
    ecs_world_t *world,
    ecs_entity_t *child)
{
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_set(world, parent, Rect, {20, 20});
    ecs_set(world, parent, Rgb, {0, 0, 0});
    ecs_add_id(world, parent, ClipChildren);
    *child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, *child, Position, {0, 0, 0});
    ecs_set(world, *child, Rect, {60, 10});
    ecs_set(world, *child, Rgb, {0, 255, 0});

    return parent;
}

void Clip_child_outside_parent_hidden(void) {
    // ui/clip
    ecs_world_t *world = clip_world("clip.png");
    ecs_entity_t child;
    clip_panel(world, &child);
    unsigned char *px = clip_render(world, "clip.png", 2);
    int32_t x, y;
    test_int(clip_row(px, 31), 20);
    test_assert(clip_green(clip_at(px, 22, 31)));
    test_assert(clip_green(clip_at(px, 41, 31)));
    for (y = 0; y < 64; y ++) {
        for (x = 0; x < 64; x ++) {
            if (x < 22 || x >= 42 || y < 22 || y >= 42) {
                test_assert(clip_black(clip_at(px, x, y)));
            }
        }
    }

    stbi_image_free(px);
    ecs_fini(world);
}

void Clip_rounded_corners_empty(void) {
    // ui/clip
    ecs_world_t *world = clip_world("rounded.png");
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_set(world, parent, Rect, {40, 40});
    ecs_set(world, parent, Border, {.width = 0, .radius = 15});
    ecs_set(world, parent, Rgb, {0, 0, 0});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, Position, {0, 0, 0});
    ecs_set(world, child, Rect, {60, 60});
    ecs_set(world, child, Rgb, {0, 255, 0});
    unsigned char *px = clip_render(world, "rounded.png", 2);
    test_assert(clip_green(clip_at(px, 12, 12)));
    test_assert(clip_green(clip_at(px, 14, 14)));
    stbi_image_free(px);

    ecs_add_id(world, parent, ClipChildren);
    px = clip_render(world, "rounded.png", 1);
    test_assert(clip_green(clip_at(px, 32, 32)));
    test_assert(clip_green(clip_at(px, 12, 31)));
    test_assert(clip_green(clip_at(px, 31, 51)));
    test_assert(clip_black(clip_at(px, 12, 12)));
    test_assert(clip_black(clip_at(px, 14, 14)));
    test_assert(clip_black(clip_at(px, 51, 51)));
    test_assert(clip_black(clip_at(px, 51, 12)));
    test_assert(clip_black(clip_at(px, 12, 51)));
    test_assert(clip_black(clip_at(px, 8, 31)));
    stbi_image_free(px);
    ecs_fini(world);
}

void Clip_nested_clips_intersect(void) {
    // ui/clip
    ecs_world_t *world = clip_world("nested.png");
    ecs_entity_t outer = ecs_new(world);
    ecs_set(world, outer, Position, {-10, 0, 0});
    ecs_set(world, outer, LayoutBounds, {40, 40});
    ecs_add_id(world, outer, ClipChildren);
    ecs_entity_t inner = ecs_new_w_pair(world, EcsChildOf, outer);
    ecs_set(world, inner, Position, {20, 10, 0});
    ecs_set(world, inner, LayoutBounds, {40, 40});
    ecs_add_id(world, inner, ClipChildren);
    ecs_entity_t content = ecs_new_w_pair(world, EcsChildOf, inner);
    ecs_set(world, content, Position, {0, 0, 0});
    ecs_set(world, content, Rect, {100, 100});
    ecs_set(world, content, Rgb, {0, 255, 0});
    unsigned char *px = clip_render(world, "nested.png", 2);

    test_int(clip_row(px, 31), 20);
    test_assert(clip_green(clip_at(px, 22, 31)));
    test_assert(clip_green(clip_at(px, 41, 31)));
    test_int(clip_column(px, 32), 30);
    test_assert(clip_green(clip_at(px, 32, 12)));
    test_assert(clip_green(clip_at(px, 32, 41)));
    test_assert(clip_black(clip_at(px, 32, 11)));
    test_assert(clip_black(clip_at(px, 32, 42)));
    stbi_image_free(px);
    ecs_fini(world);
}

/** Count the ink of a text inside and outside of the columns of a clip.
 *
 * @param px The pixels
 * @param inside Receives the ink in columns 22 to 41
 * @param outside Receives the ink in the other columns
 */
static void clip_ink(
    const unsigned char *px,
    int32_t *inside,
    int32_t *outside)
{
    int32_t x, y;
    *inside = *outside = 0;
    for (y = 0; y < 64; y ++) {
        for (x = 0; x < 64; x ++) {
            bool ink = clip_at(px, x, y)[0] > 40;
            if (x >= 22 && x < 42) {
                *inside += ink;
            } else {
                *outside += ink;
            }
        }
    }
}

void Clip_text_cut_mid_glyph(void) {
    // ui/clip
    ecs_world_t *world = clip_world("text.png");
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_set(world, parent, LayoutBounds, {20, 64});
    ecs_entity_t text = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, text, Position, {0, 0, 0});
    ecs_set(world, text, Text, {.value = "W", .size = 60,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_set(world, text, Rgb, {255, 255, 255});
    unsigned char *px = clip_render(world, "text.png", 3);
    int32_t inside, outside;
    clip_ink(px, &inside, &outside);
    test_assert(inside > 20);
    test_assert(outside > 20);
    stbi_image_free(px);

    ecs_add_id(world, parent, ClipChildren);
    px = clip_render(world, "text.png", 1);
    int32_t clipped_inside, clipped_outside;
    clip_ink(px, &clipped_inside, &clipped_outside);
    test_int(clipped_outside, 0);
    test_assert(clipped_inside > 20);
    test_assert(abs(clipped_inside - inside) <= 2);
    stbi_image_free(px);
    ecs_fini(world);
}

/** Project a point of the local plane of an entity to a pixel of a surface.
 *
 * @param world The world
 * @param camera The camera
 * @param e The entity
 * @param x Local x
 * @param y Local y
 * @param size Width and height of the surface
 * @param out Receives the column and the row
 */
static void clip_project(
    ecs_world_t *world,
    ecs_entity_t camera,
    ecs_entity_t e,
    float x,
    float y,
    float size,
    int32_t out[2])
{
    mat4 m, cam, inv, proj, vp;
    vec4 p, clip;
    glm_mat4_copy(ECS_CONST_CAST(vec4*, ecs_get(world, e, WorldTransform)->m), m);
    glm_mat4_copy(ECS_CONST_CAST(vec4*, ecs_get(world, camera, WorldTransform)->m), cam);
    glm_mat4_inv(cam, inv);
    box_camera_projection(ecs_get(world, camera, Camera), size, size, proj);
    glm_mat4_mul(proj, inv, vp);
    glm_mat4_mulv(m, (vec4){x, y, 0, 1}, p);
    glm_mat4_mulv(vp, p, clip);
    out[0] = (int32_t)((clip[0] / clip[3] * 0.5f + 0.5f) * size);
    out[1] = (int32_t)((0.5f - clip[1] / clip[3] * 0.5f) * size);
}

void Clip_world_space_rotated_panel(void) {
    // ui/clip
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 2.5f});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t panel = ecs_new(world);
    ecs_set(world, panel, Position, {0, 0, 0});
    ecs_set(world, panel, Rotation, {0.3f, 0.9f, 0.2f});
    ecs_set(world, panel, Scale, {0.01f, 0.01f, 0.01f});
    ecs_set(world, panel, Rect, {100, 100});
    ecs_set(world, panel, Border, {.width = 0, .radius = 20});
    ecs_set(world, panel, Rgb, {0, 0, 0});
    ecs_entity_t content = ecs_new_w_pair(world, EcsChildOf, panel);
    ecs_set(world, content, Position, {0, 0, 0});
    ecs_set(world, content, Rect, {300, 300});
    ecs_set(world, content, Rgb, {0, 255, 0});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 128, .height = 128, .write_to_file = "world.png",
        .resolution_scale = 1});

    float inside[3][2] = {{0, 0}, {35, 30}, {-35, -30}};
    float outside[4][2] = {{85, 0}, {0, -85}, {-90, 60}, {49, 49}};
    int32_t at[2], i;
    int w = 0, h = 0, n = 0;
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);
    char *path = flecs_asprintf("%s/world.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (i = 0; i < 4; i ++) {
        clip_project(world, camera, panel, outside[i][0], outside[i][1], 128, at);
        test_assert(at[0] >= 0 && at[0] < 128 && at[1] >= 0 && at[1] < 128);
        test_assert(clip_green(&px[(at[1] * 128 + at[0]) * 4]));
    }

    stbi_image_free(px);

    ecs_add_id(world, panel, ClipChildren);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (i = 0; i < 3; i ++) {
        clip_project(world, camera, panel, inside[i][0], inside[i][1], 128, at);
        test_assert(clip_green(&px[(at[1] * 128 + at[0]) * 4]));
    }

    for (i = 0; i < 4; i ++) {
        clip_project(world, camera, panel, outside[i][0], outside[i][1], 128, at);
        test_assert(clip_black(&px[(at[1] * 128 + at[0]) * 4]));
    }

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Clip_render_to_texture(void) {
    // ui/clip
    ecs_world_t *world = clip_world("main.png");
    ecs_entity_t texture = ecs_new(world);
    ecs_entity_t image = ecs_new(world);
    ecs_set(world, image, Position, {0, 0, 0});
    ecs_set(world, image, Rect, {64, 64});
    ecs_set(world, image, Image, {.texture = texture, .u1 = 1, .v1 = 1});

    ecs_entity_t ui = ecs_new(world);
    ecs_set(world, ui, Scene, {0});
    ecs_entity_t ui_camera = ecs_new_w_pair(world, EcsChildOf, ui);
    ecs_set(world, ui_camera, Position, {0, 0, 100});
    ecs_set(world, ui_camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t parent = ecs_new_w_pair(world, EcsChildOf, ui);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_set(world, parent, Rect, {10, 10});
    ecs_set(world, parent, Rgb, {0, 0, 0});
    ecs_add_id(world, parent, ClipChildren);
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, Position, {0, 0, 0});
    ecs_set(world, child, Rect, {30, 30});
    ecs_set(world, child, Rgb, {0, 255, 0});
    ecs_entity_t root = ecs_new(world);
    ecs_entity_t ui_view = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, ui_view, View, {.camera = ui_camera,
        .clear = {.enabled = true, .color = {0, 0, 255, 255}}});
    ecs_entity_t screen = ecs_new(world);
    ecs_set(world, screen, Surface, {.width = 32, .height = 32, .texture = texture,
        .view_root = root, .resolution_scale = 1});
    unsigned char *px = clip_render(world, "main.png", 3);

    const unsigned char *c = clip_at(px, 32, 32);
    test_assert(clip_green(c));
    c = clip_at(px, 25, 38);
    test_assert(clip_green(c));
    c = clip_at(px, 46, 32);
    test_assert(c[0] < 3 && c[1] < 3 && c[2] > 252);
    c = clip_at(px, 32, 18);
    test_assert(c[0] < 3 && c[1] < 3 && c[2] > 252);
    c = clip_at(px, 16, 46);
    test_assert(c[0] < 3 && c[1] < 3 && c[2] > 252);
    stbi_image_free(px);
    ecs_fini(world);
}

void Clip_remove_shows_full_child(void) {
    // ui/clip
    ecs_world_t *world = clip_world("remove.png");
    ecs_entity_t child;
    ecs_entity_t parent = clip_panel(world, &child);
    unsigned char *px = clip_render(world, "remove.png", 2);
    test_int(clip_row(px, 31), 20);
    stbi_image_free(px);

    ecs_remove_id(world, parent, ClipChildren);
    px = clip_render(world, "remove.png", 1);
    test_int(clip_row(px, 31), 60);
    test_assert(clip_green(clip_at(px, 2, 31)));
    test_assert(clip_green(clip_at(px, 61, 31)));
    stbi_image_free(px);

    ecs_add_id(world, parent, ClipChildren);
    px = clip_render(world, "remove.png", 1);
    test_int(clip_row(px, 31), 20);
    stbi_image_free(px);
    ecs_fini(world);
}

void Clip_moving_parent_moves_clip(void) {
    // ui/clip
    ecs_world_t *world = clip_world("move.png");
    ecs_entity_t child;
    ecs_entity_t parent = clip_panel(world, &child);
    unsigned char *px = clip_render(world, "move.png", 2);
    test_int(clip_row(px, 31), 20);
    test_assert(clip_green(clip_at(px, 22, 31)));
    stbi_image_free(px);

    ecs_set(world, parent, Position, {10, 0, 0});
    px = clip_render(world, "move.png", 1);
    test_int(clip_row(px, 31), 20);
    test_assert(!clip_green(clip_at(px, 31, 31)));
    test_assert(clip_green(clip_at(px, 32, 31)));
    test_assert(clip_green(clip_at(px, 51, 31)));
    test_assert(!clip_green(clip_at(px, 52, 31)));
    stbi_image_free(px);

    ecs_set(world, child, Position, {-20, 0, 0});
    ecs_set(world, parent, Rect, {40, 20});
    px = clip_render(world, "move.png", 2);
    test_int(clip_row(px, 31), 30);
    test_assert(clip_green(clip_at(px, 22, 31)));
    test_assert(clip_green(clip_at(px, 51, 31)));
    test_assert(!clip_green(clip_at(px, 21, 31)));
    test_assert(!clip_green(clip_at(px, 52, 31)));
    stbi_image_free(px);
    ecs_fini(world);
}

void Clip_picking_follows_clip(void) {
    // ui/clip
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script(world, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.geometry\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.layout\n"
        "using box.picking\n"
        "template Probe {\n"
        "  mut clicks: i32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      await on.click(this)\n"
        "      clicks = clicks + 1\n"
        "    }\n"
        "  }\n"
        "}\n"
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  behind {\n"
        "    Position: {30, 0, 0}\n"
        "    Rect: {40, 40}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "  panel {\n"
        "    Position: {-10, 0, 0}\n"
        "    Rect: {40, 40}\n"
        "    child {\n"
        "      Position: {0, 0, 0}\n"
        "      Rect: {120, 20}\n"
        "      Interactive\n"
        "      Probe: {}\n"
        "    }\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n" }) != 0);

    typedef struct { int32_t clicks; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t behind = ecs_lookup(world, "hud.behind");
    ecs_entity_t panel = ecs_lookup(world, "hud.panel");
    ecs_entity_t child = ecs_lookup(world, "hud.panel.child");
    test_assert(mut != 0);
    ecs_progress(world, 0);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "mouse.click left [670, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, child, mut))->clicks, 1);
    test_int(((const Mut*)ecs_get_id(world, behind, mut))->clicks, 0);

    ecs_add_id(world, panel, ClipChildren);
    ecs_progress(world, 0);
    ecs_os_free(box_terminal_run(world, s, "mouse.click left [670, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, child, mut))->clicks, 1);
    test_int(((const Mut*)ecs_get_id(world, behind, mut))->clicks, 1);

    ecs_os_free(box_terminal_run(world, s, "mouse.click left [640, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, child, mut))->clicks, 2);
    test_int(((const Mut*)ecs_get_id(world, behind, mut))->clicks, 1);

    ecs_os_free(box_terminal_run(world, s, "mouse.click left [580, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, child, mut))->clicks, 2);
    test_int(((const Mut*)ecs_get_id(world, behind, mut))->clicks, 1);

    ecs_fini(world);
}

void Clip_dynamic_child_moves_out(void) {
    // ui/clip
    ecs_world_t *world = clip_world("dynamic.png");
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_set(world, parent, Rect, {20, 20});
    ecs_set(world, parent, Rgb, {0, 0, 0});
    ecs_add_id(world, parent, ClipChildren);
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_add_id(world, child, DynamicTransform);
    ecs_set(world, child, Position, {0, 0, 0});
    ecs_set(world, child, Rect, {8, 8});
    ecs_set(world, child, Rgb, {0, 255, 0});
    unsigned char *px = clip_render(world, "dynamic.png", 2);
    test_int(clip_row(px, 31), 8);
    stbi_image_free(px);

    ecs_set(world, child, Position, {8, 0, 0});
    px = clip_render(world, "dynamic.png", 1);
    test_int(clip_row(px, 31), 6);
    test_assert(clip_green(clip_at(px, 41, 31)));
    test_assert(!clip_green(clip_at(px, 42, 31)));
    stbi_image_free(px);

    ecs_set(world, child, Position, {20, 0, 0});
    px = clip_render(world, "dynamic.png", 1);
    int32_t i;
    for (i = 0; i < 64 * 64; i ++) {
        test_assert(!clip_green(&px[i * 4]));
    }

    stbi_image_free(px);
    ecs_fini(world);
}
