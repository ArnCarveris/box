/**
 * @file Polygon2D.c
 * @brief Benchmarks for the GPU cost of Polygon2D shapes next to the other 2D shapes.
 *
 * A frame moves the camera, renders a 1280x720 headless surface and waits for the GPU (it
 * reads back the first page of the 2D instances), so a case measures the whole frame.
 *
 * The 10k cases fill the surface with a grid of small shapes, the cost of a map of many
 * outlines; the large cases overlap 12 translucent shapes that each cover most of it, the cost
 * of the per pixel edge loop of a polygon (rings of 512 edges) against Rects.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

/** The scenes of the benchmarks */
typedef enum bench_polygon2d_scene_t {
    BENCH_POLYGON2D_SHAPES_10K,     /**< Grid of bordered Rects, Circles and Lines */
    BENCH_POLYGON2D_POLYGONS_10K,   /**< Grid of L outlines, curved plazas and stroked roads */
    BENCH_POLYGON2D_RECTS_LARGE,    /**< 12 overlapping translucent bordered Rects */
    BENCH_POLYGON2D_RINGS_LARGE     /**< 12 overlapping translucent rings of 512 edges */
} bench_polygon2d_scene_t;

/** The outlines and the camera of every scene, one unit per pixel */
#define BENCH_POLYGON2D_PRELUDE \
    "using box.transform\n" \
    "using box.geometry\n" \
    "using box.material\n" \
    "using box.camera\n" \
    "using box.view\n" \
    "camera {\n" \
    "  Position: {0, 0, 100}\n" \
    "  Camera: {ortho: true, ortho_scale: 1, near_: 0.1, far_: 1000}\n" \
    "}\n" \
    "main_view {\n" \
    "  View: {camera: camera, clear: {enabled: true, color: {24, 28, 38, 255}}}\n" \
    "}\n" \
    "l_path {\n" \
    "  Path2D: {start: {0, 0}, closed: true, segments: [\n" \
    "    {kind: Line, end: {4, 0}}, {kind: Line, end: {4, 2}}, {kind: Line, end: {2, 2}},\n" \
    "    {kind: Line, end: {2, 4}}, {kind: Line, end: {0, 4}}\n" \
    "  ]}\n" \
    "}\n" \
    "plaza_path {\n" \
    "  Path2D: {start: {-6, -4}, closed: true, segments: [\n" \
    "    {kind: Line, end: {6, -4}}, {kind: CatmullRom, end: {7, 3}},\n" \
    "    {kind: CatmullRom, end: {0, 6}}, {kind: CatmullRom, end: {-7, 3}}\n" \
    "  ]}\n" \
    "}\n" \
    "road_path {\n" \
    "  Path2D: {start: {-6, -3}, segments: [\n" \
    "    {kind: CatmullRom, end: {-2, 3}}, {kind: CatmullRom, end: {2, -3}},\n" \
    "    {kind: Line, end: {6, 3}}\n" \
    "  ]}\n" \
    "}\n" \
    "ring_outer {\n" \
    "  Path2D: {start: {10, 0}, closed: true, segments: [\n" \
    "    {kind: CatmullRom, end: {6.467, 2.679}}, {kind: CatmullRom, end: {7.071, 7.071}},\n" \
    "    {kind: CatmullRom, end: {2.679, 6.467}}, {kind: CatmullRom, end: {0, 10}},\n" \
    "    {kind: CatmullRom, end: {-2.679, 6.467}}, {kind: CatmullRom, end: {-7.071, 7.071}},\n" \
    "    {kind: CatmullRom, end: {-6.467, 2.679}}, {kind: CatmullRom, end: {-10, 0}},\n" \
    "    {kind: CatmullRom, end: {-6.467, -2.679}}, {kind: CatmullRom, end: {-7.071, -7.071}},\n" \
    "    {kind: CatmullRom, end: {-2.679, -6.467}}, {kind: CatmullRom, end: {0, -10}},\n" \
    "    {kind: CatmullRom, end: {2.679, -6.467}}, {kind: CatmullRom, end: {7.071, -7.071}},\n" \
    "    {kind: CatmullRom, end: {6.467, -2.679}}\n" \
    "  ]}\n" \
    "}\n" \
    "ring_hole {\n" \
    "  Path2D: {start: {3, 0}, closed: true, segments: [\n" \
    "    {kind: CatmullRom, end: {2.772, 1.148}}, {kind: CatmullRom, end: {2.121, 2.121}},\n" \
    "    {kind: CatmullRom, end: {1.148, 2.772}}, {kind: CatmullRom, end: {0, 3}},\n" \
    "    {kind: CatmullRom, end: {-1.148, 2.772}}, {kind: CatmullRom, end: {-2.121, 2.121}},\n" \
    "    {kind: CatmullRom, end: {-2.772, 1.148}}, {kind: CatmullRom, end: {-3, 0}},\n" \
    "    {kind: CatmullRom, end: {-2.772, -1.148}}, {kind: CatmullRom, end: {-2.121, -2.121}},\n" \
    "    {kind: CatmullRom, end: {-1.148, -2.772}}, {kind: CatmullRom, end: {0, -3}},\n" \
    "    {kind: CatmullRom, end: {1.148, -2.772}}, {kind: CatmullRom, end: {2.121, -2.121}},\n" \
    "    {kind: CatmullRom, end: {2.772, -1.148}}\n" \
    "  ]}\n" \
    "}\n" \
    "ring { Profile2D: {outer: ring_outer, holes: [ring_hole]} }\n"

/** 128 by 78 cells of 10 pixels, a third bordered Rects, a third bordered Circles, a third
 * Lines, each about 8 pixels */
#define BENCH_POLYGON2D_SHAPES \
    "for x in 0..128 {\n" \
    "  for y in 0..26 {\n" \
    "    \"r_{x}_{y}\" {\n" \
    "      Position: {x * 10 - 635, y * 30 - 385, 0}\n" \
    "      Rect: {8, 8}\n" \
    "      Rgb: {90, 160, 250}\n" \
    "      Border: {width: 1, radius: 2, color: {255, 255, 255, 255}}\n" \
    "    }\n" \
    "    \"c_{x}_{y}\" {\n" \
    "      Position: {x * 10 - 635, y * 30 - 375, 0}\n" \
    "      Circle: {4}\n" \
    "      Rgb: {250, 190, 60}\n" \
    "    }\n" \
    "    \"l_{x}_{y}\" {\n" \
    "      Position: {x * 10 - 635, y * 30 - 365, 0}\n" \
    "      Line: {x1: -4, y1: -3, x2: 4, y2: 3, width: 1}\n" \
    "      Rgb: {120, 220, 140}\n" \
    "    }\n" \
    "  }\n" \
    "}\n"

/** The cells of BENCH_POLYGON2D_SHAPES with polygons: bordered L outlines of 6 edges, curved
 * plazas of 34 edges and stroked roads of 33 edges */
#define BENCH_POLYGON2D_POLYGONS \
    "for x in 0..128 {\n" \
    "  for y in 0..26 {\n" \
    "    \"l_{x}_{y}\" {\n" \
    "      Position: {x * 10 - 635, y * 30 - 385, 0}\n" \
    "      Polygon2D: {path: l_path, scale: {2, 2}, offset: {-4, -4}}\n" \
    "      Rgb: {90, 160, 250}\n" \
    "      Border: {width: 1, color: {255, 255, 255, 255}}\n" \
    "    }\n" \
    "    \"p_{x}_{y}\" {\n" \
    "      Position: {x * 10 - 635, y * 30 - 375, 0}\n" \
    "      Polygon2D: {path: plaza_path, scale: {0.6, 0.6}}\n" \
    "      Rgb: {250, 190, 60}\n" \
    "    }\n" \
    "    \"s_{x}_{y}\" {\n" \
    "      Position: {x * 10 - 635, y * 30 - 365, 0}\n" \
    "      Polygon2D: {path: road_path, scale: {0.66, 0.66}, width: 1}\n" \
    "      Rgb: {120, 220, 140}\n" \
    "    }\n" \
    "  }\n" \
    "}\n"

/** 12 overlapping translucent bordered Rects of 1000 by 640 pixels */
#define BENCH_POLYGON2D_RECTS \
    "for i in 0..12 {\n" \
    "  \"s_{i}\" {\n" \
    "    Position: {0, 0, i * 0.01}\n" \
    "    Rect: {1000, 640}\n" \
    "    Rgb: {90 + i * 12, 160, 250 - i * 12}\n" \
    "    Opacity: {0.6}\n" \
    "    Border: {width: 6, radius: 40, color: {255, 255, 255, 255}}\n" \
    "  }\n" \
    "}\n"

/** 12 overlapping translucent bordered rings of 512 edges, each about 1000 pixels across */
#define BENCH_POLYGON2D_RINGS \
    "for i in 0..12 {\n" \
    "  \"s_{i}\" {\n" \
    "    Position: {0, 0, i * 0.01}\n" \
    "    Rotation: {0, 0, i * 0.26}\n" \
    "    Polygon2D: {path: ring, scale: {50 + i * 2, 50 + i * 2}}\n" \
    "    Rgb: {90 + i * 12, 160, 250 - i * 12}\n" \
    "    Opacity: {0.6}\n" \
    "    Border: {width: 6, color: {255, 255, 255, 255}}\n" \
    "  }\n" \
    "}\n"

/** Render frames of a scene at 1280x720, waiting for the GPU every frame.
 *
 * @param b The benchmark
 * @param scene The scene
 */
static void bench_polygon2d(
    bench_t *b,
    bench_polygon2d_scene_t scene)
{
    static const char *content[] = {
        BENCH_POLYGON2D_SHAPES, BENCH_POLYGON2D_POLYGONS, BENCH_POLYGON2D_RECTS,
        BENCH_POLYGON2D_RINGS
    };
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    char *script = flecs_asprintf("%s%s", BENCH_POLYGON2D_PRELUDE, content[scene]);
    int result = ecs_script_run(world, "polygons", script, NULL);
    ecs_os_free(script);
    if (result) {
        ecs_abort(ECS_INTERNAL_ERROR, "the scene of the benchmark does not run");
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 1280, .height = 720, .resolution_scale = 1});
    uint32_t word = 0;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }

    ecs_entity_t camera = ecs_lookup(world, "camera");
    box_render_draws_stats_t stats = {0};
    float x = 0;
    bench_set_items(b, 1);
    while (bench_iter(b)) {
        x = x > 0 ? -0.25f : 0.25f;
        ecs_set(world, camera, Position, {x, 0, 100});
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST_2D, 0, sizeof(word), &word);
        bench_keep(word);
    }

    box_render_draws_stats(world, &stats);
    bench_counter(b, "batches", (double)stats.batches);
    ecs_fini(world);
}

void Polygon2D_shapes_10k(
    bench_t *b)
{
    // ui/polygon_2d
    bench_polygon2d(b, BENCH_POLYGON2D_SHAPES_10K);
}

void Polygon2D_polygons_10k(
    bench_t *b)
{
    // ui/polygon_2d
    bench_polygon2d(b, BENCH_POLYGON2D_POLYGONS_10K);
}

void Polygon2D_rects_large(
    bench_t *b)
{
    // ui/polygon_2d
    bench_polygon2d(b, BENCH_POLYGON2D_RECTS_LARGE);
}

void Polygon2D_rings_large(
    bench_t *b)
{
    // ui/polygon_2d
    bench_polygon2d(b, BENCH_POLYGON2D_RINGS_LARGE);
}
