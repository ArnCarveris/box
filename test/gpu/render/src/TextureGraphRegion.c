/**
 * @file TextureGraphRegion.c
 * @brief Tests for the longitude and latitude range of a TextureGraph on DomainSphere: a
 * ranged graph is a crop of the whole sphere graph of the same nodes, its neighbourhood nodes
 * match the whole sphere away from its edges, clamp at them, and it lines up on an
 * EllipsoidShell of the same ranges with the whole graph on a whole shell.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"

/** Frames a test waits at most for an outcome */
#define TR_FRAMES (40)

/** Pi */
#define TR_PI (3.14159265358979)

/** Ticket of the next readback */
static uint32_t tr_ticket = 1;

/** Longitude and latitude range of a graph, as the members of TextureGraph */
typedef struct tr_region_t {
    float lon0;         /**< longitude_start */
    float lon1;         /**< longitude_end */
    float lat0;         /**< latitude_start */
    float lat1;         /**< latitude_end */
} tr_region_t;

/** Nodes of a test graph */
typedef struct tr_graph_t {
    ecs_entity_t tex;       /**< The texture entity */
    ecs_entity_t noise;     /**< Scalar fbm noise of the direction */
    ecs_entity_t blur;      /**< TexBlur of the noise */
    ecs_entity_t gradient;  /**< TexGradient of the noise */
    ecs_entity_t flow;      /**< TexFlow of the noise along a constant field */
    ecs_entity_t normal;    /**< TexNormal of the noise */
    ecs_entity_t distance;  /**< TexDistance of the noise */
    ecs_entity_t coords;    /**< TexCoords */
    ecs_entity_t coords_blur; /**< TexBlur of the coordinates */
    int32_t width;          /**< Width of the image */
    int32_t height;         /**< Height of the image */
    tr_region_t region;     /**< Range of the graph, resolved */
} tr_graph_t;

/** Create a headless world that renders with the GPU backend on the main thread, with a small
 * surface without views.
 *
 * @return The world
 */
static ecs_world_t* tr_world(void)
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
static void tr_frames(
    ecs_world_t *world,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }
}

/** Run frames until a texture is not loading and has at least a revision.
 *
 * @param world The world
 * @param tex The texture entity
 * @param revision Revision to wait for
 * @return The status
 */
static const TextureStatus* tr_wait(
    ecs_world_t *world,
    ecs_entity_t tex,
    int32_t revision)
{
    const TextureStatus *status = NULL;
    int32_t i;
    for (i = 0; i < TR_FRAMES; i ++) {
        tr_frames(world, 1);
        status = ecs_get(world, tex, TextureStatus);
        if (status && status->state == TextureFailed) {
            break;
        }

        if (status && status->state == TextureReady && status->revision >= revision) {
            break;
        }
    }

    test_assert(status != NULL);
    if (status->state == TextureFailed) {
        printf("error: %s\n", status->error ? status->error : "(none)");
    }

    return status;
}

/** Read back the value of a node of a texture graph.
 *
 * @param world The world
 * @param g The graph
 * @param node The node entity, 0 for the image
 * @return Four floats per texel, freed with ecs_os_free
 */
static float* tr_read(
    ecs_world_t *world,
    const tr_graph_t *g,
    ecs_entity_t node)
{
    uint32_t ticket = tr_ticket ++;
    float *result = NULL;
    int32_t i, j;
    ecs_vec_t reads;
    ecs_vec_init_t(NULL, &reads, box_render_texture_read_t, 0);
    test_bool(box_render_texture_graph_read(world, g->tex, node, ticket), true);
    for (i = 0; i < TR_FRAMES && !result; i ++) {
        tr_frames(world, 1);
        ecs_vec_clear(&reads);
        test_bool(box_render_texture_graph_reads(world, &reads), true);
        for (j = 0; j < ecs_vec_count(&reads); j ++) {
            box_render_texture_read_t *r = ecs_vec_get_t(&reads, box_render_texture_read_t, j);
            if (r->ticket == ticket && !result) {
                test_assert(r->texels != NULL);
                test_int(r->width, g->width);
                test_int(r->height, g->height);
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

/** Create a graph on the sphere of a noise and the neighbourhood nodes of it, all reached
 * from its output, with new node entities.
 *
 * @param world The world
 * @param width Width
 * @param height Height
 * @param region Range, NULL for the default (the whole sphere)
 * @param frequency Frequency of the noise
 * @param octaves Octaves of the noise
 * @return The graph
 */
static tr_graph_t tr_graph(
    ecs_world_t *world,
    int32_t width,
    int32_t height,
    const tr_region_t *region,
    float frequency,
    int32_t octaves)
{
    tr_graph_t g = {0};
    ecs_entity_t sum0 = ecs_new(world), sum1 = ecs_new(world), sum2 = ecs_new(world);
    ecs_entity_t sum3 = ecs_new(world), sum4 = ecs_new(world), sum5 = ecs_new(world);
    g.tex = ecs_new(world);
    g.noise = ecs_new(world);
    g.blur = ecs_new(world);
    g.gradient = ecs_new(world);
    g.flow = ecs_new(world);
    g.normal = ecs_new(world);
    g.distance = ecs_new(world);
    g.coords = ecs_new(world);
    g.coords_blur = ecs_new(world);
    g.width = width;
    g.height = height;
    ecs_set(world, g.noise, TexNoise, {.frequency = frequency, .octaves = octaves,
        .lacunarity = 2, .gain = 0.5f, .seed = 7, .offset = 1, .channels = 1});
    ecs_set(world, g.blur, TexBlur, {.input = g.noise, .radius = 8});
    ecs_set(world, g.gradient, TexGradient, {.height = g.noise, .strength = 1});
    ecs_set(world, g.flow, TexFlow, {.input = g.noise, .field_value = {0, 1, 0, 0},
        .length_value = 1, .reach = 8});
    ecs_set(world, g.normal, TexNormal, {.height = g.noise, .strength = 1});
    ecs_set(world, g.distance, TexDistance, {.input = g.noise, .range = 16});
    ecs_set(world, g.coords, TexCoords, {.scale = 1});
    ecs_set(world, g.coords_blur, TexBlur, {.input = g.coords, .radius = 24});

    /* Every node reaches the output, so every node is evaluated and can be read back */
    ecs_set(world, sum0, TexAdd, {.a = g.blur, .b = g.flow});
    ecs_set(world, sum1, TexAdd, {.a = sum0, .b = g.distance});
    ecs_set(world, sum2, TexAdd, {.a = g.gradient, .b = g.normal});
    ecs_set(world, sum3, TexAdd, {.a = sum2, .b = g.coords_blur});
    ecs_set(world, sum4, TexMultiply, {.a = sum1, .b_value = {0, 0, 0, 0}});
    ecs_set(world, sum5, TexAdd, {.a = sum3, .b = sum4});

    TextureGraph desc = {.output = sum5, .width = width, .height = height,
        .domain = DomainSphere, .format = GraphHdr};
    g.region = (tr_region_t){0, (float)(2 * TR_PI), (float)(-TR_PI / 2), (float)(TR_PI / 2)};
    if (region) {
        desc.longitude_start = region->lon0;
        desc.longitude_end = region->lon1;
        desc.latitude_start = region->lat0;
        desc.latitude_end = region->lat1;
        g.region = *region;
    }

    ecs_set_ptr(world, g.tex, TextureGraph, &desc);

    return g;
}

/** Range of the texels x0..x0 + w and rows y0..y0 + h of an image of the whole sphere of a
 * size, so that a graph of that range at size w x h has the texels of the whole one.
 *
 * @param width Width of the whole sphere image
 * @param height Height of the whole sphere image
 * @param x0 First column
 * @param y0 First row
 * @param w Columns
 * @param h Rows
 * @return The range
 */
static tr_region_t tr_crop(
    int32_t width,
    int32_t height,
    int32_t x0,
    int32_t y0,
    int32_t w,
    int32_t h)
{
    return (tr_region_t){
        (float)(2 * TR_PI * x0 / width), (float)(2 * TR_PI * (x0 + w) / width),
        (float)(-TR_PI / 2 + TR_PI * y0 / height),
        (float)(-TR_PI / 2 + TR_PI * (y0 + h) / height)
    };
}

/** Longitude and latitude of the center of a texel of a graph.
 *
 * @param g The graph
 * @param x Column
 * @param y Row
 * @param lon Receives the longitude
 * @param lat Receives the latitude
 */
static void tr_lonlat(
    const tr_graph_t *g,
    int32_t x,
    int32_t y,
    double *lon,
    double *lat)
{
    double u = (x + 0.5) / g->width, v = (y + 0.5) / g->height;
    *lon = g->region.lon0 + u * (g->region.lon1 - g->region.lon0);
    *lat = g->region.lat0 + v * (g->region.lat1 - g->region.lat0);
}

/** Bilinear sample of a channel of an image of the whole sphere at a longitude and latitude,
 * wrapped in longitude and clamped at the poles.
 *
 * @param img Four floats per texel
 * @param w Width
 * @param h Height
 * @param channel Channel
 * @param lon Longitude
 * @param lat Latitude
 * @return The value
 */
static double tr_sample_whole(
    const float *img,
    int32_t w,
    int32_t h,
    int32_t channel,
    double lon,
    double lat)
{
    double u = lon / (2 * TR_PI), v = (lat + TR_PI / 2) / TR_PI;
    double px = u * w - 0.5, py = v * h - 0.5;
    double fx = floor(px), fy = floor(py);
    double tx = px - fx, ty = py - fy;
    int32_t x0 = (int32_t)fx, y0 = (int32_t)fy, i, j;
    double c[2][2];
    for (j = 0; j < 2; j ++) {
        for (i = 0; i < 2; i ++) {
            int32_t x = ((x0 + i) % w + w) % w;
            int32_t y = y0 + j;
            y = y < 0 ? 0 : y >= h ? h - 1 : y;
            c[j][i] = img[(y * w + x) * 4 + channel];
        }
    }

    return (c[0][0] * (1 - tx) + c[0][1] * tx) * (1 - ty) +
        (c[1][0] * (1 - tx) + c[1][1] * tx) * ty;
}

/** Compare a channel of a node of a crop graph with the texels of the whole sphere graph it
 * has, away from the edges of the crop.
 *
 * @param crop Values of the crop graph
 * @param whole Values of the whole graph
 * @param cw Width of the crop
 * @param ch Height of the crop
 * @param ww Width of the whole graph
 * @param x0 First column of the crop in the whole graph
 * @param y0 First row of the crop in the whole graph
 * @param margin Texels left out at each edge of the crop
 * @param channels Channels compared
 * @param tolerance Largest difference
 */
static void tr_expect_crop(
    const float *crop,
    const float *whole,
    int32_t cw,
    int32_t ch,
    int32_t ww,
    int32_t x0,
    int32_t y0,
    int32_t margin,
    int32_t channels,
    float tolerance)
{
    int32_t x, y, c, checked = 0;
    for (y = margin; y < ch - margin; y ++) {
        for (x = margin; x < cw - margin; x ++) {
            for (c = 0; c < channels; c ++) {
                float a = crop[(y * cw + x) * 4 + c];
                float b = whole[((y0 + y) * ww + x0 + x) * 4 + c];
                if (!(fabsf(a - b) <= tolerance)) {
                    printf("texel %d, %d channel %d: %f, whole %f\n", x, y, c, a, b);
                    test_assert(false);
                }
            }

            checked ++;
        }
    }

    test_assert(checked > 0);
}

/** Check that every channel of every texel of a node is finite.
 *
 * @param img Four floats per texel
 * @param w Width
 * @param h Height
 */
static void tr_expect_finite(
    const float *img,
    int32_t w,
    int32_t h)
{
    int32_t i;
    for (i = 0; i < w * h * 4; i ++) {
        if (!isfinite(img[i])) {
            printf("texel %d, %d channel %d is %f\n", (i / 4) % w, (i / 4) / w, i % 4, img[i]);
            test_assert(false);
        }
    }
}

/** A noise graph with a range at the size of the whole one has its texels, and at four times
 * the resolution the values of the whole one between them. */
void TextureGraphRegion_noise_matches_whole(void) {
    // textures/graph
    ecs_world_t *world = tr_world();
    int32_t ww = 256, wh = 128, x0 = 64, y0 = 40, cw = 48, ch = 32, x, y;
    tr_region_t r = tr_crop(ww, wh, x0, y0, cw, ch);
    tr_graph_t whole = tr_graph(world, ww, wh, NULL, 4, 3);
    tr_graph_t crop = tr_graph(world, cw, ch, &r, 4, 3);
    tr_graph_t fine = tr_graph(world, cw * 4, ch * 4, &r, 4, 3);
    test_int(tr_wait(world, whole.tex, 1)->state, TextureReady);
    test_int(tr_wait(world, crop.tex, 1)->state, TextureReady);
    test_int(tr_wait(world, fine.tex, 1)->state, TextureReady);

    float *w_noise = tr_read(world, &whole, whole.noise);
    float *c_noise = tr_read(world, &crop, crop.noise);
    float *f_noise = tr_read(world, &fine, fine.noise);
    tr_expect_crop(c_noise, w_noise, cw, ch, ww, x0, y0, 0, 1, 1e-3f);

    /* The fine graph resamples the whole one: its texels between the texels of the whole one
     * differ from their bilinear interpolation by no more than the noise varies over a texel */
    double largest = 0, total = 0;
    for (y = 0; y < fine.height; y ++) {
        for (x = 0; x < fine.width; x ++) {
            double lon, lat;
            tr_lonlat(&fine, x, y, &lon, &lat);
            double expect = tr_sample_whole(w_noise, ww, wh, 0, lon, lat);
            double d = fabs(f_noise[(y * fine.width + x) * 4] - expect);
            largest = d > largest ? d : largest;
            total += d;
        }
    }

    double mean = total / (fine.width * fine.height);
    printf("fine noise against the whole sphere: largest %f, mean %f\n", largest, mean);
    test_assert(largest < 0.05);
    test_assert(mean < 0.01);

    ecs_os_free(f_noise);
    ecs_os_free(c_noise);
    ecs_os_free(w_noise);
    ecs_fini(world);
}

/** Blur, Gradient, Flow and Normal of a crop match the whole sphere away from its edges */
void TextureGraphRegion_neighbourhood_matches_whole(void) {
    // textures/graph
    ecs_world_t *world = tr_world();
    int32_t ww = 256, wh = 128, x0 = 100, y0 = 44, cw = 64, ch = 40, margin = 10;
    tr_region_t r = tr_crop(ww, wh, x0, y0, cw, ch);
    tr_graph_t whole = tr_graph(world, ww, wh, NULL, 4, 3);
    tr_graph_t crop = tr_graph(world, cw, ch, &r, 4, 3);
    test_int(tr_wait(world, whole.tex, 1)->state, TextureReady);
    test_int(tr_wait(world, crop.tex, 1)->state, TextureReady);

    float *w_blur = tr_read(world, &whole, whole.blur);
    float *c_blur = tr_read(world, &crop, crop.blur);
    tr_expect_crop(c_blur, w_blur, cw, ch, ww, x0, y0, margin, 1, 1e-3f);

    float *w_gradient = tr_read(world, &whole, whole.gradient);
    float *c_gradient = tr_read(world, &crop, crop.gradient);
    tr_expect_crop(c_gradient, w_gradient, cw, ch, ww, x0, y0, margin, 3, 5e-3f);

    float *w_flow = tr_read(world, &whole, whole.flow);
    float *c_flow = tr_read(world, &crop, crop.flow);
    tr_expect_crop(c_flow, w_flow, cw, ch, ww, x0, y0, margin, 1, 1e-3f);

    /* The crop is the uv of a shell of its range, not of a whole one: the normal differs by the
     * factors of the uv frame, so only its sign of the slope along u is compared */
    float *w_normal = tr_read(world, &whole, whole.normal);
    float *c_normal = tr_read(world, &crop, crop.normal);
    tr_expect_finite(c_normal, cw, ch);
    int32_t x, y, agree = 0, count = 0;
    for (y = margin; y < ch - margin; y ++) {
        for (x = margin; x < cw - margin; x ++) {
            float a = c_normal[(y * cw + x) * 4] - 0.5f;
            float b = w_normal[((y0 + y) * ww + x0 + x) * 4] - 0.5f;
            if (fabsf(a) > 0.02f && fabsf(b) > 0.02f) {
                agree += (a > 0) == (b > 0);
                count ++;
            }
        }
    }

    test_assert(count > 0);
    test_assert(agree == count);

    ecs_os_free(c_normal);
    ecs_os_free(w_normal);
    ecs_os_free(c_flow);
    ecs_os_free(w_flow);
    ecs_os_free(c_gradient);
    ecs_os_free(w_gradient);
    ecs_os_free(c_blur);
    ecs_os_free(w_blur);
    ecs_fini(world);
}

/** At the edges of a range the neighbourhood nodes are finite and clamp instead of wrapping:
 * the blur of the coordinates at the first column stays near the coordinates there although
 * the last column is far away, on a range of part of the longitude and latitude, one that
 * contains a pole and one of the whole longitude, which wraps. */
void TextureGraphRegion_edges(void) {
    // textures/graph
    ecs_world_t *world = tr_world();
    tr_region_t part = {0.5f, 2.5f, -0.4f, 0.6f};
    tr_region_t pole = {1.0f, 3.0f, (float)(-TR_PI / 2), -0.6f};
    tr_region_t ring = {0, (float)(2 * TR_PI), -0.5f, 0.5f};
    tr_graph_t g[3] = {
        tr_graph(world, 128, 64, &part, 4, 3),
        tr_graph(world, 128, 64, &pole, 4, 3),
        tr_graph(world, 256, 32, &ring, 4, 3)
    };
    int32_t i, y;
    for (i = 0; i < 3; i ++) {
        test_int(tr_wait(world, g[i].tex, 1)->state, TextureReady);
    }

    for (i = 0; i < 3; i ++) {
        ecs_entity_t nodes[] = {g[i].noise, g[i].blur, g[i].gradient, g[i].flow,
            g[i].normal, g[i].distance, g[i].coords_blur};
        int32_t n;
        for (n = 0; n < 7; n ++) {
            float *img = tr_read(world, &g[i], nodes[n]);
            tr_expect_finite(img, g[i].width, g[i].height);
            ecs_os_free(img);
        }
    }

    /* Part of the longitude: z of the direction is cos(lat) cos(lon), 0.88 cos(lat) at the
     * first column and -0.80 cos(lat) at the last. Clamped, the blur at an edge column stays
     * near the coordinates of the edge. */
    {
        tr_graph_t *p = &g[0];
        float *coords = tr_read(world, p, p->coords);
        float *blur = tr_read(world, p, p->coords_blur);
        int32_t w = p->width;
        for (y = 0; y < p->height; y ++) {
            float first = coords[(y * w) * 4 + 2], last = coords[(y * w + w - 1) * 4 + 2];
            test_assert(fabsf(blur[(y * w) * 4 + 2] - first) < 0.15f);
            test_assert(fabsf(blur[(y * w + w - 1) * 4 + 2] - last) < 0.15f);
        }

        ecs_os_free(blur);
        ecs_os_free(coords);
    }

    /* The whole longitude wraps: the blur at the first column and at the last are near each
     * other, as their coordinates are */
    {
        tr_graph_t *p = &g[2];
        float *blur = tr_read(world, p, p->coords_blur);
        int32_t w = p->width, c;
        for (y = 0; y < p->height; y ++) {
            for (c = 0; c < 3; c ++) {
                float a = blur[(y * w) * 4 + c], b = blur[(y * w + w - 1) * 4 + c];
                float inner = fabsf(blur[(y * w + 1) * 4 + c] - blur[(y * w) * 4 + c]);
                test_assert(fabsf(a - b) <= 2 * inner + 1e-3f);
            }
        }

        ecs_os_free(blur);
    }

    ecs_fini(world);
}

/** The range spelled out as the whole sphere makes the same values as the default */
void TextureGraphRegion_full_range_equals_default(void) {
    // textures/graph
    ecs_world_t *world = tr_world();
    tr_region_t full = {0, (float)(2 * TR_PI), (float)(-TR_PI / 2), (float)(TR_PI / 2)};
    tr_graph_t a = tr_graph(world, 128, 64, NULL, 4, 3);
    tr_graph_t b = tr_graph(world, 128, 64, &full, 4, 3);
    test_int(tr_wait(world, a.tex, 1)->state, TextureReady);
    test_int(tr_wait(world, b.tex, 1)->state, TextureReady);

    ecs_entity_t na[] = {a.noise, a.blur, a.gradient, a.flow, a.normal, a.distance};
    ecs_entity_t nb[] = {b.noise, b.blur, b.gradient, b.flow, b.normal, b.distance};
    int32_t n, i;
    for (n = 0; n < 6; n ++) {
        float *va = tr_read(world, &a, na[n]);
        float *vb = tr_read(world, &b, nb[n]);
        for (i = 0; i < a.width * a.height * 4; i ++) {
            if (va[i] != vb[i]) {
                printf("node %d texel %d channel %d: %f, default %f\n", n, i / 4, i % 4,
                    vb[i], va[i]);
                test_assert(false);
            }
        }

        ecs_os_free(vb);
        ecs_os_free(va);
    }

    ecs_fini(world);
}

/** Editing the range of a graph changes its values: the graph, first of the whole sphere at the
 * size of a crop, has the texels of the crop of a whole graph once its range is the crop's,
 * although its nodes and their parameters are the same */
void TextureGraphRegion_range_edit(void) {
    // textures/graph
    ecs_world_t *world = tr_world();
    int32_t ww = 128, wh = 64, x0 = 32, y0 = 16, cw = 64, ch = 32;
    tr_region_t r = tr_crop(ww, wh, x0, y0, cw, ch);
    tr_graph_t whole = tr_graph(world, ww, wh, NULL, 4, 3);
    tr_graph_t g = tr_graph(world, cw, ch, NULL, 4, 3);
    test_int(tr_wait(world, whole.tex, 1)->state, TextureReady);
    const TextureStatus *status = tr_wait(world, g.tex, 1);
    test_int(status->state, TextureReady);
    int32_t revision = status->revision;

    float *w_noise = tr_read(world, &whole, whole.noise);
    float *before = tr_read(world, &g, g.noise);

    TextureGraph *desc = ecs_get_mut(world, g.tex, TextureGraph);
    desc->longitude_start = r.lon0;
    desc->longitude_end = r.lon1;
    desc->latitude_start = r.lat0;
    desc->latitude_end = r.lat1;
    ecs_modified(world, g.tex, TextureGraph);
    g.region = r;
    test_int(tr_wait(world, g.tex, revision + 1)->state, TextureReady);

    float *after = tr_read(world, &g, g.noise);
    tr_expect_crop(after, w_noise, cw, ch, ww, x0, y0, 0, 1, 1e-3f);
    int32_t i, changed = 0;
    for (i = 0; i < cw * ch; i ++) {
        changed += fabsf(after[i * 4] - before[i * 4]) > 1e-3f;
    }

    test_assert(changed > cw * ch / 2);

    ecs_os_free(after);
    ecs_os_free(before);
    ecs_os_free(w_noise);
    ecs_fini(world);
}

/** A ranged graph on an EllipsoidShell of the same ranges lines up with the whole sphere graph
 * on a whole shell, as the planet stage's SkyBackground draws it: the point of the shell at uv
 * (surface_position in surface.wgsl: longitude start + u range, latitude start + v range, at
 * (cos lon cos lat, sin lat, sin lon cos lat)), which samples the image at uv, has the value of
 * the whole graph at the uv of the same point on a whole shell (longitude 0 to 2 pi, latitude
 * -pi / 2 to pi / 2). */
void TextureGraphRegion_shell_lines_up(void) {
    // textures/graph
    ecs_world_t *world = tr_world();
    tr_region_t r = {0.7f, 1.9f, -0.2f, 0.6f};
    tr_graph_t whole = tr_graph(world, 512, 256, NULL, 3, 2);
    tr_graph_t crop = tr_graph(world, 128, 96, &r, 3, 2);
    test_int(tr_wait(world, whole.tex, 1)->state, TextureReady);
    test_int(tr_wait(world, crop.tex, 1)->state, TextureReady);

    float *w_noise = tr_read(world, &whole, whole.noise);
    float *c_noise = tr_read(world, &crop, crop.noise);
    double largest = 0, total = 0;
    int32_t x, y;
    for (y = 0; y < crop.height; y ++) {
        for (x = 0; x < crop.width; x ++) {
            double u = (x + 0.5) / crop.width, v = (y + 0.5) / crop.height;
            double a = r.lon0 + u * (r.lon1 - r.lon0);
            double b = r.lat0 + v * (r.lat1 - r.lat0);
            double px = cos(a) * cos(b), py = sin(b), pz = sin(a) * cos(b);

            /* The same point on the whole shell */
            double lon = atan2(pz, px);
            if (lon < 0) {
                lon += 2 * TR_PI;
            }
            double lat = asin(py < -1 ? -1 : py > 1 ? 1 : py);
            double expect = tr_sample_whole(w_noise, whole.width, whole.height, 0, lon, lat);
            double d = fabs(c_noise[(y * crop.width + x) * 4] - expect);
            largest = d > largest ? d : largest;
            total += d;
        }
    }

    double mean = total / (crop.width * crop.height);
    printf("ranged shell against the whole shell: largest %f, mean %f\n", largest, mean);
    test_assert(largest < 0.03);
    test_assert(mean < 0.005);

    ecs_os_free(c_noise);
    ecs_os_free(w_noise);
    ecs_fini(world);
}
