/**
 * @file TextureCells.c
 * @brief Tests for the TexCells node of TextureGraph on the GPU backend: distances of a regular
 * lattice against a reference in both domains, ids per cell, tiling on the plane, continuity on
 * the sphere, the number of cells over each domain, evolve and the coords input.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"

/** Voronoi cells authored from a script, with the default jitter */
#define GRAPH_CELLS "test/assets/textures/graph/cells.flecs"

/** Frames a test waits at most for an outcome */
#define TC_FRAMES (40)

/** Difference a half float distance of up to about 2 cell sizes may have from its value */
#define TC_TOLERANCE (3e-3f)

/** Difference two half float ids of the same cell may have */
#define TC_ID_TOLERANCE (1e-3f)

/** Pi */
#define TC_PI (3.14159265358979f)

/** Ticket of the next readback */
static uint32_t tc_ticket = 1;

/** Create a headless world that renders with the GPU backend on the main thread, with a small
 * surface without views.
 *
 * @return The world
 */
static ecs_world_t* tc_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 16, .height = 16, .resolution_scale = 1});

    return world;
}

/** Run frames and wait for each to render.
 *
 * @param world The world
 * @param count Frames to run
 */
static void tc_frames(
    ecs_world_t *world,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }
}

/** Run frames until a texture is ready or failed.
 *
 * @param world The world
 * @param tex The texture entity
 * @return The state
 */
static TextureState tc_wait(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    const TextureStatus *status = NULL;
    int32_t i;
    for (i = 0; i < TC_FRAMES; i ++) {
        tc_frames(world, 1);
        status = ecs_get(world, tex, TextureStatus);
        if (status && (status->state == TextureFailed ||
            (status->state == TextureReady && status->revision >= 1)))
        {
            break;
        }
    }

    test_assert(status != NULL);
    if (status->state == TextureFailed) {
        printf("texture failed: %s\n", status->error ? status->error : "");
    }

    return status->state;
}

/** Read back the image of a texture graph.
 *
 * @param world The world
 * @param tex The texture entity
 * @param width Expected width
 * @param height Expected height
 * @return Four floats per texel, freed with ecs_os_free
 */
static float* tc_read(
    ecs_world_t *world,
    ecs_entity_t tex,
    int32_t width,
    int32_t height)
{
    uint32_t ticket = tc_ticket ++;
    float *result = NULL;
    int32_t i, j;
    ecs_vec_t reads;
    ecs_vec_init_t(NULL, &reads, box_render_texture_read_t, 0);
    test_bool(box_render_texture_graph_read(world, tex, 0, ticket), true);
    for (i = 0; i < TC_FRAMES && !result; i ++) {
        tc_frames(world, 1);
        ecs_vec_clear(&reads);
        test_bool(box_render_texture_graph_reads(world, &reads), true);
        for (j = 0; j < ecs_vec_count(&reads); j ++) {
            box_render_texture_read_t *r = ecs_vec_get_t(&reads, box_render_texture_read_t, j);
            if (r->ticket == ticket && !result) {
                test_assert(r->texels != NULL);
                test_int(r->width, width);
                test_int(r->height, height);
                result = r->texels;
            } else {
                ecs_os_free(r->texels);
            }
        }
    }

    ecs_vec_fini_t(NULL, &reads, box_render_texture_read_t);
    test_assert(result != NULL);

    return result;
}

/** Get the direction of the center of a texel of the sphere domain, as box_texgraph_coords.
 *
 * @param x Column
 * @param y Row
 * @param width Width of the image
 * @param height Height of the image
 * @param out Receives (x, y, z)
 */
static void tc_sphere(
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    double out[3])
{
    double u = ((double)x + 0.5) / width;
    double v = ((double)y + 0.5) / height;
    double phi = (u - 0.5) * 2.0 * TC_PI, theta = v * TC_PI;
    out[0] = sin(theta) * sin(phi);
    out[1] = cos(theta);
    out[2] = -sin(theta) * cos(phi);
}

/** Get the scale of the sphere coordinates to the lattice of TexCells.
 *
 * @param count Number of cells
 * @return Lattice cells per unit
 */
static double tc_sphere_scale(
    int32_t count)
{
    return sqrt((double)count / (6.0 * TC_PI));
}

/** Get the lattice of TexCells on the plane.
 *
 * @param count Number of cells
 * @param width Width of the image
 * @param height Height of the image
 * @param gx Receives the cells across
 * @param gy Receives the cells down
 * @param ratio Receives the square root of the width over the height of a cell in pixels
 */
static void tc_plane_lattice(
    int32_t count,
    int32_t width,
    int32_t height,
    double *gx,
    double *gy,
    double *ratio)
{
    *gx = floor(sqrt((double)count * width / height) + 0.5);
    *gx = *gx < 1 ? 1 : *gx;
    *gy = floor(count / *gx + 0.5);
    *gy = *gy < 1 ? 1 : *gy;
    *ratio = sqrt(((double)width / *gx) / ((double)height / *gy));
}

/** Hermite smoothstep from lo to hi.
 *
 * @param lo Value that gives 0
 * @param hi Value that gives 1
 * @param x The value
 * @return 0..1
 */
static double tc_smooth(
    double lo,
    double hi,
    double x)
{
    double t = (x - lo) / (hi - lo);
    t = t < 0 ? 0 : (t > 1 ? 1 : t);

    return t * t * (3.0 - 2.0 * t);
}

/** Get the expected distance of TexCells with jitter 0 at a point in lattice units: the points
 * are at the centers of the cells.
 *
 * @param q The point in lattice units
 * @param weight Factor of each axis of a difference
 * @param reach Cells searched along z on each side, 0 on the plane
 * @param distance What is measured
 * @param edge Width of the smoothstep of CellsEdge
 * @return The distance
 */
static double tc_lattice_distance(
    const double q[3],
    const double weight[3],
    int32_t reach,
    TextureCellsDistance distance,
    double edge)
{
    double f1 = 1e30, f2 = 1e30, result;
    int32_t x, y, z, a;
    for (z = -reach; z <= reach; z ++) {
        for (y = -2; y <= 2; y ++) {
            for (x = -2; x <= 2; x ++) {
                const int32_t o[3] = {x, y, z};
                double d = 0;
                for (a = 0; a < 3; a ++) {
                    double c = floor(q[a]) + o[a] + 0.5;
                    double v = (c - q[a]) * weight[a];
                    d += v * v;
                }

                d = sqrt(d);
                if (d < f1) {
                    f2 = f1;
                    f1 = d;
                } else if (d < f2) {
                    f2 = d;
                }
            }
        }
    }

    if (distance == CellsSecond) {
        result = f2;
    } else if (distance == CellsEdge) {
        result = tc_smooth(0, edge, f2 - f1);
    } else {
        result = f1;
    }

    return result;
}

/** Create a graph of one TexCells node.
 *
 * @param world The world
 * @param cells The node
 * @param width Width
 * @param height Height
 * @param domain Domain
 * @param node Receives the node entity, or NULL
 * @return The texture entity
 */
static ecs_entity_t tc_graph(
    ecs_world_t *world,
    const TexCells *cells,
    int32_t width,
    int32_t height,
    TextureGraphDomain domain,
    ecs_entity_t *node)
{
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, TexCells, cells);
    ecs_set(world, tex, TextureGraph, {.output = e, .width = width, .height = height,
        .domain = domain, .format = GraphHdr});
    if (node) {
        *node = e;
    }

    return tex;
}

/** Evaluate a graph of one TexCells node and read back its image.
 *
 * @param world The world
 * @param cells The node
 * @param width Width
 * @param height Height
 * @param domain Domain
 * @return Four floats per texel, freed with ecs_os_free
 */
static float* tc_eval(
    ecs_world_t *world,
    const TexCells *cells,
    int32_t width,
    int32_t height,
    TextureGraphDomain domain)
{
    ecs_entity_t tex = tc_graph(world, cells, width, height, domain, NULL);
    test_int(tc_wait(world, tex), TextureReady);

    return tc_read(world, tex, width, height);
}

/** Check whether two texels have the ids of the same cell.
 *
 * @param a The first texel
 * @param b The second texel
 * @return Whether y, z and w are the same within TC_ID_TOLERANCE
 */
static bool tc_same_ids(
    const float *a,
    const float *b)
{
    return fabsf(a[1] - b[1]) <= TC_ID_TOLERANCE && fabsf(a[2] - b[2]) <= TC_ID_TOLERANCE &&
        fabsf(a[3] - b[3]) <= TC_ID_TOLERANCE;
}

/** Count the cells of an image by their distinct ids.
 *
 * @param texels The texels
 * @param count Number of texels
 * @return Number of distinct (y, z, w)
 */
static int32_t tc_count_cells(
    const float *texels,
    int32_t count)
{
    int32_t i, j, result = 0;
    const float **found = ecs_os_malloc_n(const float*, count);
    for (i = 0; i < count; i ++) {
        const float *t = &texels[i * 4];
        bool seen = false;
        for (j = 0; j < result && !seen; j ++) {
            seen = t[1] == found[j][1] && t[2] == found[j][2] && t[3] == found[j][3];
        }

        if (!seen) {
            found[result ++] = t;
        }
    }

    ecs_os_free(found);

    return result;
}

/** With jitter 0 on the plane the distances of each measure match a regular lattice with the
 * points at the cell centers, with cells of 16 by 16 texels. */
void TextureCells_plane_distances(void) {
    // textures/graph
    ecs_world_t *world = tc_world();
    int32_t x, y, w = 64, h = 32, count = 8, m;
    double gx, gy, ratio;
    tc_plane_lattice(count, w, h, &gx, &gy, &ratio);
    test_assert(gx == 4 && gy == 2);
    for (m = CellsNearest; m <= CellsEdge; m ++) {
        TexCells cells = {.distance = m, .count = count, .jitter = 0, .seed = 3, .edge = 0.3f};
        float *texels = tc_eval(world, &cells, w, h, DomainPlane);
        for (y = 0; y < h; y ++) {
            for (x = 0; x < w; x ++) {
                const double q[3] = {(x + 0.5) / w * gx, (y + 0.5) / h * gy, 0};
                const double weight[3] = {ratio, 1.0 / ratio, 0};
                double expect = tc_lattice_distance(q, weight, 0, m, 0.3);
                float v = texels[(y * w + x) * 4];
                if (fabs(v - expect) > TC_TOLERANCE) {
                    printf("mode %d texel %d, %d: %f, expected %f\n", m, x, y, v, expect);
                }

                test_assert(fabs(v - expect) <= TC_TOLERANCE);
            }
        }

        ecs_os_free(texels);
    }

    ecs_fini(world);
}

/** With jitter 0 on the sphere the distances of each measure match a regular 3D lattice
 * evaluated at the direction of the texel, scaled so count cells cover the sphere. */
void TextureCells_sphere_distances(void) {
    // textures/graph
    ecs_world_t *world = tc_world();
    int32_t x, y, w = 64, h = 32, count = 64, m, a;
    double s = tc_sphere_scale(count);
    for (m = CellsNearest; m <= CellsEdge; m ++) {
        TexCells cells = {.distance = m, .count = count, .jitter = 0, .seed = 9, .edge = 0.25f};
        float *texels = tc_eval(world, &cells, w, h, DomainSphere);
        for (y = 0; y < h; y ++) {
            for (x = 0; x < w; x ++) {
                double q[3];
                const double weight[3] = {1, 1, 1};
                tc_sphere(x, y, w, h, q);
                for (a = 0; a < 3; a ++) {
                    q[a] *= s;
                }

                double expect = tc_lattice_distance(q, weight, 2, m, 0.25);
                float v = texels[(y * w + x) * 4];
                if (fabs(v - expect) > TC_TOLERANCE) {
                    printf("mode %d texel %d, %d: %f, expected %f\n", m, x, y, v, expect);
                }

                test_assert(fabs(v - expect) <= TC_TOLERANCE);
            }
        }

        ecs_os_free(texels);
    }

    ecs_fini(world);
}

/** With jitter 0 a cell is a square of the lattice: its texels share the ids, in 0..1, and the
 * ids of two cells differ. */
void TextureCells_ids_per_cell(void) {
    // textures/graph
    ecs_world_t *world = tc_world();
    int32_t x, y, i, j, w = 64, h = 64, c;
    TexCells cells = {.count = 16, .jitter = 0, .seed = 4};
    float *texels = tc_eval(world, &cells, w, h, DomainPlane);
    const float *first[16] = {0};
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            const float *t = &texels[(y * w + x) * 4];
            int32_t cell = (y / 16) * 4 + x / 16;
            for (c = 1; c < 4; c ++) {
                test_assert(t[c] >= 0 && t[c] <= 1);
            }

            if (!first[cell]) {
                first[cell] = t;
            }

            test_assert(tc_same_ids(t, first[cell]));
        }
    }

    for (i = 0; i < 16; i ++) {
        for (j = i + 1; j < 16; j ++) {
            test_assert(!tc_same_ids(first[i], first[j]));
        }
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

/** The ids are stable for a seed and another seed gives other ids. */
void TextureCells_seed_changes_ids(void) {
    // textures/graph
    ecs_world_t *world = tc_world();
    int32_t x, y, w = 32, h = 32, changed = 0, n = w * h, i;
    TexCells a = {.count = 16, .jitter = 0, .seed = 1};
    TexCells b = {.count = 16, .jitter = 0, .seed = 2};
    float *first = tc_eval(world, &a, w, h, DomainPlane);
    float *again = tc_eval(world, &a, w, h, DomainPlane);
    float *other = tc_eval(world, &b, w, h, DomainPlane);
    for (i = 0; i < n; i ++) {
        test_assert(tc_same_ids(&first[i * 4], &again[i * 4]));
        test_assert(fabsf(first[i * 4] - other[i * 4]) <= TC_TOLERANCE);
    }

    for (y = 4; y < h; y += 8) {
        for (x = 4; x < w; x += 8) {
            changed += !tc_same_ids(&first[(y * w + x) * 4], &other[(y * w + x) * 4]);
        }
    }

    test_assert(changed >= 14);
    ecs_os_free(first);
    ecs_os_free(again);
    ecs_os_free(other);
    ecs_fini(world);
}

/** The plane tiles: coordinates shifted by whole periods give the same cells, and the
 * distances change across the borders of the image no more than between neighbours. */
void TextureCells_plane_tiles(void) {
    // textures/graph
    ecs_world_t *world = tc_world();
    int32_t x, y, w = 64, h = 64, count = 20, mismatched = 0;
    double gx, gy, ratio;
    TexCells cells = {.count = count, .jitter = 1, .seed = 3};
    tc_plane_lattice(count, w, h, &gx, &gy, &ratio);
    float *base = tc_eval(world, &cells, w, h, DomainPlane);

    ecs_entity_t uv = ecs_new(world);
    ecs_entity_t shifted = ecs_new(world);
    ecs_set(world, uv, TexCoords, {.scale = 1});
    ecs_set(world, shifted, TexAdd, {.a = uv, .b_value = {1, -2, 0, 0}});
    cells.coords = shifted;
    float *moved = tc_eval(world, &cells, w, h, DomainPlane);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            const float *a = &base[(y * w + x) * 4], *b = &moved[(y * w + x) * 4];
            test_assert(fabsf(a[0] - b[0]) <= TC_TOLERANCE);
            mismatched += !tc_same_ids(a, b);
        }
    }

    test_assert(mismatched <= 2);

    double step_x = gx / w * ratio + TC_TOLERANCE, step_y = gy / h / ratio + TC_TOLERANCE;
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            float v = base[(y * w + x) * 4];
            float right = base[(y * w + (x + 1) % w) * 4];
            float down = base[(((y + 1) % h) * w + x) * 4];
            test_assert(fabs(v - right) <= step_x);
            test_assert(fabs(v - down) <= step_y);
        }
    }

    ecs_os_free(base);
    ecs_os_free(moved);
    ecs_fini(world);
}

/** Check that F1 or F2 of the sphere changes between two texels no more than the distance of
 * their directions in lattice units, as a distance to the nearest points does.
 *
 * @param texels The texels
 * @param w Width
 * @param h Height
 * @param s Scale of the lattice
 * @param x0 Column of the first texel
 * @param y0 Row of the first texel
 * @param x1 Column of the second texel
 * @param y1 Row of the second texel
 */
static void tc_sphere_lipschitz(
    const float *texels,
    int32_t w,
    int32_t h,
    double s,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1)
{
    double a[3], b[3];
    tc_sphere(x0, y0, w, h, a);
    tc_sphere(x1, y1, w, h, b);
    double chord = sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) +
        (a[2] - b[2]) * (a[2] - b[2]));
    double change = fabs(texels[(y0 * w + x0) * 4] - texels[(y1 * w + x1) * 4]);
    if (change > s * chord + TC_TOLERANCE) {
        printf("texels %d, %d and %d, %d: change %f, at most %f\n", x0, y0, x1, y1, change,
            s * chord);
    }

    test_assert(change <= s * chord + TC_TOLERANCE);
}

/** The sphere is seamless: F1 and F2 change between neighbouring texels, across the seam of
 * longitude and across the poles, no more than the distance of their directions. */
void TextureCells_sphere_continuous(void) {
    // textures/graph
    ecs_world_t *world = tc_world();
    int32_t x, y, w = 128, h = 64, count = 64, m;
    double s = tc_sphere_scale(count);
    for (m = CellsNearest; m <= CellsSecond; m ++) {
        TexCells cells = {.distance = m, .count = count, .jitter = 1, .seed = 5};
        float *texels = tc_eval(world, &cells, w, h, DomainSphere);
        for (y = 0; y < h; y ++) {
            for (x = 0; x < w; x ++) {
                tc_sphere_lipschitz(texels, w, h, s, x, y, (x + 1) % w, y);
                if (y + 1 < h) {
                    tc_sphere_lipschitz(texels, w, h, s, x, y, x, y + 1);
                }
            }
        }

        for (x = 0; x < w / 2; x ++) {
            tc_sphere_lipschitz(texels, w, h, s, x, 0, x + w / 2, 0);
            tc_sphere_lipschitz(texels, w, h, s, x, h - 1, x + w / 2, h - 1);
        }

        ecs_os_free(texels);
    }

    ecs_fini(world);
}

/** count cells cover the plane: a 6 by 6 lattice of jittered points, each visible. */
void TextureCells_plane_cell_count(void) {
    // textures/graph
    ecs_world_t *world = tc_world();
    int32_t w = 128, h = 128;
    TexCells cells = {.count = 36, .jitter = 1, .seed = 11};
    float *texels = tc_eval(world, &cells, w, h, DomainPlane);
    int32_t found = tc_count_cells(texels, w * h);
    printf("plane cells: %d of 36\n", found);
    test_assert(found >= 32);
    test_assert(found <= 36);

    TexCells wide = {.count = 32, .jitter = 1, .seed = 11};
    float *other = tc_eval(world, &wide, 128, 64, DomainPlane);
    found = tc_count_cells(other, 128 * 64);
    printf("wide plane cells: %d of 32\n", found);
    test_assert(found >= 28);
    test_assert(found <= 32);

    ecs_os_free(texels);
    ecs_os_free(other);
    ecs_fini(world);
}

/** count cells cover the sphere, approximately. */
void TextureCells_sphere_cell_count(void) {
    // textures/graph
    ecs_world_t *world = tc_world();
    int32_t w = 256, h = 128;
    TexCells cells = {.count = 100, .jitter = 1, .seed = 13};
    float *texels = tc_eval(world, &cells, w, h, DomainSphere);
    int32_t found = tc_count_cells(texels, w * h);
    printf("sphere cells: %d of about 100\n", found);
    test_assert(found >= 65);
    test_assert(found <= 140);

    ecs_os_free(texels);
    ecs_fini(world);
}

/** Check that evolve moves the points continuously in a domain: a small change of evolve
 * changes F1 no more than the points can move, a large one moves them.
 *
 * @param domain The domain
 * @param w Width
 * @param h Height
 * @param bound Largest change of F1 for a change of evolve of 0.01
 */
static void tc_evolve_in(
    TextureGraphDomain domain,
    int32_t w,
    int32_t h,
    float bound)
{
    ecs_world_t *world = tc_world();
    int32_t i, n = w * h;
    double moved = 0;
    TexCells cells = {.count = 16, .jitter = 1, .seed = 21, .evolve_value = 3};
    float *start = tc_eval(world, &cells, w, h, domain);
    cells.evolve_value = 3.01f;
    float *small_step = tc_eval(world, &cells, w, h, domain);
    cells.evolve_value = 3.5f;
    float *large_step = tc_eval(world, &cells, w, h, domain);
    for (i = 0; i < n; i ++) {
        float change = fabsf(start[i * 4] - small_step[i * 4]);
        if (change > bound + TC_TOLERANCE) {
            printf("texel %d: change %f, at most %f\n", i, change, bound);
        }

        test_assert(change <= bound + TC_TOLERANCE);
        moved += fabsf(start[i * 4] - large_step[i * 4]);
    }

    printf("mean change of F1 for 0.5 of evolve: %f\n", moved / n);
    test_assert(moved / n > 0.02);

    ecs_os_free(start);
    ecs_os_free(small_step);
    ecs_os_free(large_step);
    ecs_fini(world);
}

/** evolve moves the points of the plane continuously: a point moves at most 0.02 cells per
 * axis for 0.01 of evolve. */
void TextureCells_evolve_plane(void) {
    // textures/graph
    tc_evolve_in(DomainPlane, 64, 64, 0.02f * 1.4143f);
}

/** evolve moves the points of the sphere continuously. */
void TextureCells_evolve_sphere(void) {
    // textures/graph
    tc_evolve_in(DomainSphere, 64, 32, 0.02f * 1.7321f);
}

/** The coords input replaces the coordinates of the texel: on the plane a shift by whole
 * texels shifts the image, on the sphere the opposite directions give the antipodal texels. */
void TextureCells_coords_input(void) {
    // textures/graph
    ecs_world_t *world = tc_world();
    int32_t x, y, w = 64, h = 32;
    TexCells cells = {.count = 24, .jitter = 1, .seed = 8};
    float *plane = tc_eval(world, &cells, w, h, DomainPlane);
    float *sphere = tc_eval(world, &cells, w, h, DomainSphere);

    ecs_entity_t uv = ecs_new(world);
    ecs_entity_t shifted = ecs_new(world);
    ecs_entity_t opposite = ecs_new(world);
    ecs_set(world, uv, TexCoords, {.scale = 1});
    ecs_set(world, shifted, TexAdd, {.a = uv,
        .b_value = {5.0f / w, 3.0f / h, 0, 0}});
    ecs_set(world, opposite, TexMultiply, {.a = uv,
        .b_value = {-1, -1, -1, 1}});
    cells.coords = shifted;
    float *plane_shifted = tc_eval(world, &cells, w, h, DomainPlane);
    cells.coords = opposite;
    float *sphere_opposite = tc_eval(world, &cells, w, h, DomainSphere);
    int32_t mismatched = 0;
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            const float *a = &plane_shifted[(y * w + x) * 4];
            const float *b = &plane[(((y + 3) % h) * w + (x + 5) % w) * 4];
            test_assert(fabsf(a[0] - b[0]) <= TC_TOLERANCE);
            mismatched += !tc_same_ids(a, b);

            const float *c = &sphere_opposite[(y * w + x) * 4];
            const float *d = &sphere[((h - 1 - y) * w + (x + w / 2) % w) * 4];
            test_assert(fabsf(c[0] - d[0]) <= TC_TOLERANCE);
            mismatched += !tc_same_ids(c, d);
        }
    }

    test_assert(mismatched <= 4);
    ecs_os_free(plane);
    ecs_os_free(sphere);
    ecs_os_free(plane_shifted);
    ecs_os_free(sphere_opposite);
    ecs_fini(world);
}

/** A TexCells from a script without jitter takes full jitter, and its CellsEdge value is 0 on
 * the borders and 1 inside the cells. */
void TextureCells_script_defaults(void) {
    // textures/graph
    ecs_world_t *world = tc_world();
    int32_t i, n = 64 * 64, low = 0, high = 0, c;
    char *path = box_app_locate(GRAPH_CELLS);
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);
    ecs_entity_t tex = ecs_lookup(world, "graph_cells");
    ecs_entity_t node = ecs_lookup(world, "graph_cells.cells");
    test_assert(tex != 0);
    test_assert(node != 0);
    const TexCells *cells = ecs_get(world, node, TexCells);
    test_assert(cells != NULL);
    test_flt(cells->jitter, 1);
    test_int(cells->count, 25);
    test_int(cells->seed, 7);
    test_int(cells->distance, CellsEdge);
    test_int(tc_wait(world, tex), TextureReady);

    float *texels = tc_read(world, tex, 64, 64);
    for (i = 0; i < n; i ++) {
        for (c = 0; c < 4; c ++) {
            test_assert(texels[i * 4 + c] >= 0 && texels[i * 4 + c] <= 1);
        }

        low += texels[i * 4] < 0.25f;
        high += texels[i * 4] > 0.999f;
    }

    test_assert(low > 0);
    test_assert(high > 0);
    ecs_os_free(texels);
    ecs_fini(world);
}
