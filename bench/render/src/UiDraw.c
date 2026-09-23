/**
 * @file UiDraw.c
 * @brief Benchmarks for the frames a UI tree adds to: its glyph draws and the scene shadows.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/render/render_internal.h"
#include "../../../src/renderer/renderer.h"

/** Text nodes of the tree, each a line of glyphs */
#define BENCH_UI_LINES (40)

/** Characters in the line of every text node */
#define BENCH_UI_LINE "The quick brown fox jumps over the lazy dog 0123"

/** Glyphs the text nodes place, the characters of every line but its spaces */
#define BENCH_UI_GLYPHS (BENCH_UI_LINES * 39)

/** Static casters of the scene the shadow benchmark draws next to the tree */
#define BENCH_UI_CASTERS (2000)

/** Frames the world progresses before the measurement, which builds the meshes and atlas */
#define BENCH_UI_WARMUP (60)

/** Edge of the offscreen target the benchmarks render into */
#define BENCH_UI_SIZE (512)

/** Build a world with a UI tree of text lines, next to a scene when asked.
 *
 * @param scene Add a camera, a sun, shadows and a grid of static casters
 * @param panel Receives the element whose rectangle the benchmark changes
 * @return The world
 */
static ecs_world_t* box_bench_ui_world(
    bool scene,
    ecs_entity_t *panel)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    Surface *s = ecs_singleton_ensure(world, Surface);
    s->width = BENCH_UI_SIZE;
    s->height = BENCH_UI_SIZE;
    if (scene) {
        ecs_entity_t cam = ecs_new(world);
        ecs_set(world, cam, Position, {0, 30, -30});
        ecs_set(world, cam, LookAt, {0, 0, 0});
        ecs_set(world, cam, Camera, {.fov = 60});
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {
            .camera = cam, .enabled = true, .shadows = {.enabled = true}});
        ecs_entity_t sun = ecs_new(world);
        ecs_set(world, sun, Rotation, {0.9f, -0.6f, 0});
        ecs_set(world, sun, DirectionalLight, {.intensity = 1});
        int32_t i;
        for (i = 0; i < BENCH_UI_CASTERS; i ++) {
            ecs_entity_t e = ecs_new(world);
            ecs_set(world, e, Position, {
                (float)(i % 50) - 25.0f, 0.5f, (float)(i / 50) - 20.0f});
            ecs_set(world, e, Box, {0.8f, 0.8f, 0.8f});
        }
    }

    ecs_entity_t root = ecs_new(world);
    ecs_add(world, root, Ui);
    *panel = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, *panel, Rect, {200, 100});
    int32_t line;
    for (line = 0; line < BENCH_UI_LINES; line ++) {
        ecs_entity_t text = ecs_new_w_pair(world, EcsChildOf, root);
        ecs_set(world, text, Text, {.value = ecs_os_strdup(BENCH_UI_LINE)});
    }

    return world;
}

/** Progress the world and render one frame of it.
 *
 * @param world The world
 * @param r The renderer
 * @param b The benchmark, NULL outside the measurement
 */
static void box_bench_ui_frame(
    ecs_world_t *world,
    box_renderer_t *r,
    bench_t *b)
{
    if (b) {
        bench_pause(b);
    }

    ecs_progress(world, 0.016f);
    box_packet_t *p = box_renderer_acquire(r);
    p->width = BENCH_UI_SIZE;
    p->height = BENCH_UI_SIZE;
    box_render_extract(world, p);
    if (b) {
        bench_resume(b);
    }

    box_renderer_submit(r, p);
}

/** Render frames of a UI tree, changing the rectangle of one element every frame.
 *
 * @param b The benchmark
 * @param scene Draw the tree next to a scene with cached shadows
 */
static void box_bench_ui(
    bench_t *b,
    bool scene)
{
    ecs_entity_t panel = 0;
    ecs_world_t *world = box_bench_ui_world(scene, &panel);
    box_renderer_desc_t desc = { .width = BENCH_UI_SIZE, .height = BENCH_UI_SIZE };
    box_renderer_t *r = box_renderer_create(&desc);
    if (!r) {
        goto done;
    }

    int32_t i;
    for (i = 0; i < BENCH_UI_WARMUP; i ++) {
        box_bench_ui_frame(world, r, NULL);
    }

    float width = 200;
    bench_set_items(b, scene ? BENCH_UI_CASTERS : BENCH_UI_GLYPHS);
    while (bench_iter(b)) {
        bench_pause(b);
        width = width < 300 ? width + 1 : 200;
        ecs_set(world, panel, Rect, {width, 100});
        bench_resume(b);
        box_bench_ui_frame(world, r, b);
    }

    bench_keep(box_renderer_frames(r));
    box_renderer_destroy(r);
done:
    ecs_fini(world);
}

void UiDraw_record_1560_glyphs(
    bench_t *b)
{
    // ui/ui_draw_cost
    box_bench_ui(b, false);
}

void UiDraw_changing_panel_next_to_2000_casters(
    bench_t *b)
{
    // ui/ui_draw_cost
    box_bench_ui(b, true);
}
