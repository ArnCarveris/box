/**
 * @file TextureNodesNoise.c
 * @brief Tests for the TexNoise node of TextureGraph on the GPU backend: range and mean, tiling
 * on the plane, continuity on the sphere, seeds, channels, evolve, the ridged and billow
 * fractals and octave edits that compile nothing.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"

/** Frames a test waits at most for an outcome */
#define TN_FRAMES (40)

/** Difference a half float channel of a value up to 1 may have from its value */
#define TN_TOLERANCE_16 (2e-3f)

/** Node of a scalar kind with a constant value, the evolve input of the tests */
typedef struct TnScalar {
    float value;        /**< The value */
} TnScalar;

/** WGSL of TnScalar */
static const char *tn_scalar_wgsl =
    "fn tn_scalar(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    return vec4f(bitcast<f32>(p[0].x), 0.0, 0.0, 0.0);\n"
    "}\n";

/** Ticket of the next readback */
static uint32_t tn_ticket = 1;

/** Create a headless world that renders with the GPU backend on the main thread, with a small
 * surface without views.
 *
 * @return The world
 */
static ecs_world_t* tn_world(void)
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
static void tn_frames(
    ecs_world_t *world,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }
}

/** Run frames until a texture is ready with at least a revision, and check that it is.
 *
 * @param world The world
 * @param tex The texture entity
 * @param revision Revision to wait for
 */
static void tn_wait(
    ecs_world_t *world,
    ecs_entity_t tex,
    int32_t revision)
{
    const TextureStatus *status = NULL;
    int32_t i;
    for (i = 0; i < TN_FRAMES; i ++) {
        tn_frames(world, 1);
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
        printf("error: %s\n", status->error);
    }

    test_int(status->state, TextureReady);
    test_assert(status->revision >= revision);
}

/** Read back the image of a texture graph.
 *
 * @param world The world
 * @param tex The texture entity
 * @param width Expected width
 * @param height Expected height
 * @return Four floats per texel, freed with ecs_os_free
 */
static float* tn_read(
    ecs_world_t *world,
    ecs_entity_t tex,
    int32_t width,
    int32_t height)
{
    uint32_t ticket = tn_ticket ++;
    float *result = NULL;
    int32_t i, j;
    ecs_vec_t reads;
    ecs_vec_init_t(NULL, &reads, box_render_texture_read_t, 0);
    test_bool(box_render_texture_graph_read(world, tex, 0, ticket), true);
    for (i = 0; i < TN_FRAMES && !result; i ++) {
        tn_frames(world, 1);
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

/** Create a graph whose output is a noise node, stored as HDR.
 *
 * @param world The world
 * @param noise The noise
 * @param node Receives the noise node, or NULL
 * @param width Width
 * @param height Height
 * @param domain Domain
 * @return The texture entity
 */
static ecs_entity_t tn_graph(
    ecs_world_t *world,
    const TexNoise *noise,
    ecs_entity_t *node,
    int32_t width,
    int32_t height,
    TextureGraphDomain domain)
{
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t n = ecs_new(world);
    ecs_set_ptr(world, n, TexNoise, noise);
    ecs_set(world, tex, TextureGraph, {.output = n, .width = width, .height = height,
        .domain = domain, .format = GraphHdr});
    if (node) {
        *node = n;
    }

    return tex;
}

/** Evaluate a noise in a new graph and read back its image.
 *
 * @param world The world
 * @param noise The noise
 * @param width Width
 * @param height Height
 * @param domain Domain
 * @return Four floats per texel, freed with ecs_os_free
 */
static float* tn_eval(
    ecs_world_t *world,
    const TexNoise *noise,
    int32_t width,
    int32_t height,
    TextureGraphDomain domain)
{
    ecs_entity_t tex = tn_graph(world, noise, NULL, width, height, domain);
    tn_wait(world, tex, 1);

    return tn_read(world, tex, width, height);
}

/** Register the TnScalar kind.
 *
 * @param world The world
 * @return The component
 */
static ecs_entity_t tn_register_scalar(
    ecs_world_t *world)
{
    ecs_entity_t c = ecs_component(world, {
        .entity = ecs_entity(world, { .name = "TnScalar" }),
        .type.size = ECS_SIZEOF(TnScalar),
        .type.alignment = ECS_ALIGNOF(TnScalar)
    });
    test_assert(box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = c,
        .output = TexScalar,
        .params = {{ "value", offsetof(TnScalar, value), 4, BOX_TEXTURE_PARAM_F32 }},
        .function = "tn_scalar",
        .source = tn_scalar_wgsl
    }) > 0);

    return c;
}

/** Statistics of one channel of an image */
typedef struct tn_stats_t {
    float min;          /**< Smallest value */
    float max;          /**< Largest value */
    double mean;        /**< Mean */
    double deviation;   /**< Standard deviation */
    float median;       /**< Median */
} tn_stats_t;

/** Order floats for qsort.
 *
 * @param a The first float
 * @param b The second float
 * @return -1, 0 or 1
 */
static int tn_compare(
    const void *a,
    const void *b)
{
    float x = *(const float*)a, y = *(const float*)b;

    return x < y ? -1 : (x > y ? 1 : 0);
}

/** Compute the statistics of a channel of an image.
 *
 * @param texels The texels
 * @param count Number of texels
 * @param channel The channel
 * @return The statistics
 */
static tn_stats_t tn_stats(
    const float *texels,
    int32_t count,
    int32_t channel)
{
    tn_stats_t result = { .min = 1e9f, .max = -1e9f };
    float *sorted = ecs_os_malloc_n(float, count);
    double sum = 0, sum2 = 0;
    int32_t i;
    for (i = 0; i < count; i ++) {
        float v = texels[i * 4 + channel];
        sorted[i] = v;
        result.min = v < result.min ? v : result.min;
        result.max = v > result.max ? v : result.max;
        sum += v;
        sum2 += (double)v * v;
    }

    result.mean = sum / count;
    result.deviation = sqrt(fmax(0.0, sum2 / count - result.mean * result.mean));
    qsort(sorted, (size_t)count, sizeof(float), tn_compare);
    result.median = sorted[count / 2];
    ecs_os_free(sorted);

    return result;
}

/** Compute the correlation of two channels of two images.
 *
 * @param a The first texels
 * @param ca Channel of the first
 * @param b The second texels
 * @param cb Channel of the second
 * @param count Number of texels
 * @return The Pearson correlation
 */
static double tn_correlation(
    const float *a,
    int32_t ca,
    const float *b,
    int32_t cb,
    int32_t count)
{
    double ma = 0, mb = 0, sab = 0, saa = 0, sbb = 0;
    int32_t i;
    for (i = 0; i < count; i ++) {
        ma += a[i * 4 + ca];
        mb += b[i * 4 + cb];
    }

    ma /= count;
    mb /= count;
    for (i = 0; i < count; i ++) {
        double x = a[i * 4 + ca] - ma, y = b[i * 4 + cb] - mb;
        sab += x * y;
        saa += x * x;
        sbb += y * y;
    }

    return sab / sqrt(saa * sbb + 1e-30);
}

/** Get the largest difference of the first channel between neighbouring texels inside an
 * image, along x or y.
 *
 * @param texels The texels
 * @param width Width
 * @param height Height
 * @param dx 1 for neighbours along x
 * @param dy 1 for neighbours along y
 * @return The difference
 */
static float tn_neighbour_step(
    const float *texels,
    int32_t width,
    int32_t height,
    int32_t dx,
    int32_t dy)
{
    float result = 0;
    int32_t x, y;
    for (y = 0; y + dy < height; y ++) {
        for (x = 0; x + dx < width; x ++) {
            float d = fabsf(texels[(y * width + x) * 4] -
                texels[((y + dy) * width + x + dx) * 4]);
            result = d > result ? d : result;
        }
    }

    return result;
}

/** Check the range, the mean and the spread of fbm noise in a domain.
 *
 * @param domain The domain
 */
static void tn_range_in(
    TextureGraphDomain domain)
{
    ecs_world_t *world = tn_world();
    int32_t w = 128, h = domain == DomainSphere ? 64 : 128;
    TexNoise noise = {.frequency = 8, .octaves = 4, .lacunarity = 2, .gain = 0.5f,
        .seed = 1, .offset = 1};
    float *texels = tn_eval(world, &noise, w, h, domain);
    tn_stats_t s = tn_stats(texels, w * h, 0);
    int32_t i;
    test_assert(s.min >= 0.0f);
    test_assert(s.max <= 1.0f);
    test_assert(fabs(s.mean - 0.5) < 0.05);
    test_assert(s.deviation > 0.04);
    test_assert(s.deviation < 0.3);
    test_assert(s.max - s.min > 0.3f);
    for (i = 0; i < w * h; i ++) {
        test_flt(texels[i * 4 + 1], texels[i * 4]);
        test_flt(texels[i * 4 + 2], texels[i * 4]);
        test_flt(texels[i * 4 + 3], 1.0f);
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

void TextureNodesNoise_range_mean_plane(void) {
    // textures/graph/noise
    tn_range_in(DomainPlane);
}

void TextureNodesNoise_range_mean_sphere(void) {
    // textures/graph/noise
    tn_range_in(DomainSphere);
}

/** Check that noise on the plane tiles: the noise at coordinates moved by a whole period in u
 * or v is the noise at the coordinates, and the opposite edges continue each other.
 *
 * @param noise The noise
 * @param w Width
 * @param h Height
 */
static void tn_tiles(
    const TexNoise *noise,
    int32_t w,
    int32_t h)
{
    ecs_world_t *world = tn_world();
    const float shifts[2][4] = {{1, 0, 0, 0}, {0, -1, 0, 0}};
    int32_t i, k, x, y;
    float *base = tn_eval(world, noise, w, h, DomainPlane);
    for (k = 0; k < 2; k ++) {
        ecs_entity_t uv = ecs_new(world);
        ecs_entity_t moved = ecs_new(world);
        ecs_set(world, uv, TexCoords, {.scale = 1});
        ecs_set(world, moved, TexAdd, {.a = uv,
            .b_value = {shifts[k][0], shifts[k][1], shifts[k][2], shifts[k][3]}});
        TexNoise shifted = *noise;
        shifted.coords = moved;
        float *texels = tn_eval(world, &shifted, w, h, DomainPlane);
        for (i = 0; i < w * h; i ++) {
            if (fabsf(texels[i * 4] - base[i * 4]) > TN_TOLERANCE_16) {
                printf("texel %d: %f, unshifted %f\n", i, texels[i * 4], base[i * 4]);
            }

            test_assert(fabsf(texels[i * 4] - base[i * 4]) <= TN_TOLERANCE_16);
        }

        ecs_os_free(texels);
    }

    float step_x = tn_neighbour_step(base, w, h, 1, 0);
    float step_y = tn_neighbour_step(base, w, h, 0, 1);
    test_assert(step_x > 0);
    test_assert(step_y > 0);
    for (y = 0; y < h; y ++) {
        float d = fabsf(base[(y * w) * 4] - base[(y * w + w - 1) * 4]);
        test_assert(d <= step_x * 1.5f + TN_TOLERANCE_16);
    }

    for (x = 0; x < w; x ++) {
        float d = fabsf(base[x * 4] - base[((h - 1) * w + x) * 4]);
        test_assert(d <= step_y * 1.5f + TN_TOLERANCE_16);
    }

    ecs_os_free(base);
    ecs_fini(world);
}

void TextureNodesNoise_tiles_plane(void) {
    // textures/graph/noise
    TexNoise noise = {.frequency = 4, .octaves = 5, .lacunarity = 2, .gain = 0.5f,
        .seed = 2, .offset = 1};
    tn_tiles(&noise, 64, 64);
}

void TextureNodesNoise_tiles_plane_rounded(void) {
    // textures/graph/noise
    TexNoise noise = {.frequency = 3.3f, .octaves = 4, .lacunarity = 1.9f, .gain = 0.6f,
        .seed = 5, .fractal = NoiseRidged, .offset = 1};
    tn_tiles(&noise, 96, 48);
}

/** On the sphere the noise continues across the seam of the equirectangular image and around
 * the poles: the texels next to the seam differ as little as neighbours inside, and a row next
 * to a pole spans as little as one step down. */
void TextureNodesNoise_sphere_continuous(void) {
    // textures/graph/noise
    ecs_world_t *world = tn_world();
    int32_t w = 128, h = 64, x, y;
    TexNoise noise = {.frequency = 2, .octaves = 3, .lacunarity = 2, .gain = 0.5f,
        .seed = 4, .offset = 1};
    float *texels = tn_eval(world, &noise, w, h, DomainSphere);
    float step_x = tn_neighbour_step(texels, w, h, 1, 0);
    float step_y = tn_neighbour_step(texels, w, h, 0, 1);
    test_assert(step_x > 0);
    test_assert(step_y > 0);
    for (y = 0; y < h; y ++) {
        float d = fabsf(texels[(y * w) * 4] - texels[(y * w + w - 1) * 4]);
        test_assert(d <= step_x * 1.5f + TN_TOLERANCE_16);
    }

    for (y = 0; y < h; y += h - 1) {
        float lo = 1e9f, hi = -1e9f;
        for (x = 0; x < w; x ++) {
            float v = texels[(y * w + x) * 4];
            lo = v < lo ? v : lo;
            hi = v > hi ? v : hi;
        }

        test_assert(hi - lo <= step_y * 2.0f + TN_TOLERANCE_16);
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

/** The same parameters give the same values, in another graph too */
void TextureNodesNoise_same_seed_same_values(void) {
    // textures/graph/noise
    ecs_world_t *world = tn_world();
    int32_t w = 64, h = 32, i, c;
    TexNoise noise = {.frequency = 5, .octaves = 3, .lacunarity = 2, .gain = 0.5f,
        .seed = 11, .fractal = NoiseBillow, .offset = 1};
    float *a = tn_eval(world, &noise, w, h, DomainSphere);
    float *b = tn_eval(world, &noise, w, h, DomainSphere);
    for (i = 0; i < w * h; i ++) {
        for (c = 0; c < 4; c ++) {
            test_flt(a[i * 4 + c], b[i * 4 + c]);
        }
    }

    ecs_os_free(a);
    ecs_os_free(b);
    ecs_fini(world);
}

/** Different seeds give uncorrelated noise */
void TextureNodesNoise_seeds_uncorrelated(void) {
    // textures/graph/noise
    ecs_world_t *world = tn_world();
    int32_t w = 128, h = 128;
    TexNoise noise = {.frequency = 16, .octaves = 1, .lacunarity = 2, .gain = 0.5f,
        .seed = 1, .offset = 1};
    float *a = tn_eval(world, &noise, w, h, DomainPlane);
    noise.seed = 2;
    float *b = tn_eval(world, &noise, w, h, DomainPlane);
    noise.seed = -7;
    float *c = tn_eval(world, &noise, w, h, DomainPlane);
    test_assert(fabs(tn_correlation(a, 0, b, 0, w * h)) < 0.2);
    test_assert(fabs(tn_correlation(a, 0, c, 0, w * h)) < 0.2);
    test_assert(fabs(tn_correlation(b, 0, c, 0, w * h)) < 0.2);
    test_assert(tn_correlation(a, 0, a, 0, w * h) > 0.99);

    ecs_os_free(a);
    ecs_os_free(b);
    ecs_os_free(c);
    ecs_fini(world);
}

/** The channels of a vector noise are uncorrelated, the first is the scalar noise of the same
 * seed, and with three channels w is 1 */
void TextureNodesNoise_channels_uncorrelated(void) {
    // textures/graph/noise
    ecs_world_t *world = tn_world();
    int32_t w = 128, h = 128, i, j;
    TexNoise noise = {.frequency = 16, .octaves = 2, .lacunarity = 2, .gain = 0.5f,
        .seed = 3, .channels = 4, .offset = 1};
    float *v = tn_eval(world, &noise, w, h, DomainPlane);
    for (i = 0; i < 4; i ++) {
        tn_stats_t s = tn_stats(v, w * h, i);
        test_assert(fabs(s.mean - 0.5) < 0.06);
        test_assert(s.deviation > 0.04);
        for (j = i + 1; j < 4; j ++) {
            test_assert(fabs(tn_correlation(v, i, v, j, w * h)) < 0.2);
        }
    }

    noise.channels = 1;
    float *s = tn_eval(world, &noise, w, h, DomainPlane);
    noise.channels = 3;
    float *v3 = tn_eval(world, &noise, w, h, DomainPlane);
    for (i = 0; i < w * h; i ++) {
        test_assert(fabsf(s[i * 4] - v[i * 4]) <= 1e-5f);
        for (j = 0; j < 3; j ++) {
            test_assert(fabsf(v3[i * 4 + j] - v[i * 4 + j]) <= 1e-5f);
        }

        test_flt(v3[i * 4 + 3], 1.0f);
    }

    ecs_os_free(v);
    ecs_os_free(s);
    ecs_os_free(v3);
    ecs_fini(world);
}

/** An evolve node moves the noise through time continuously: at 0 it is the noise without
 * one, a small step changes it a little and a large step a lot. */
static void tn_evolve_in(
    TextureGraphDomain domain)
{
    ecs_world_t *world = tn_world();
    ecs_entity_t scalar_c = tn_register_scalar(world);
    int32_t w = 64, h = domain == DomainSphere ? 32 : 64, i;
    TexNoise noise = {.frequency = 4, .octaves = 4, .lacunarity = 2, .gain = 0.5f,
        .seed = 9, .offset = 1};
    float *still = tn_eval(world, &noise, w, h, domain);
    ecs_entity_t time = ecs_new(world);
    TnScalar t = {.value = 0};
    ecs_set_id(world, time, scalar_c, ECS_SIZEOF(TnScalar), &t);
    noise.evolve = time;
    ecs_entity_t node;
    ecs_entity_t tex = tn_graph(world, &noise, &node, w, h, domain);
    tn_wait(world, tex, 1);
    float *at0 = tn_read(world, tex, w, h);
    for (i = 0; i < w * h; i ++) {
        test_assert(fabsf(at0[i * 4] - still[i * 4]) <= TN_TOLERANCE_16);
    }

    const float steps[3] = {0.01f, 0.02f, 0.5f};
    double mean_diff[3];
    float max_diff[3];
    int32_t k;
    for (k = 0; k < 3; k ++) {
        t.value = steps[k];
        ecs_set_id(world, time, scalar_c, ECS_SIZEOF(TnScalar), &t);
        tn_wait(world, tex, 2 + k);
        float *texels = tn_read(world, tex, w, h);
        mean_diff[k] = 0;
        max_diff[k] = 0;
        for (i = 0; i < w * h; i ++) {
            float d = fabsf(texels[i * 4] - at0[i * 4]);
            mean_diff[k] += d;
            max_diff[k] = d > max_diff[k] ? d : max_diff[k];
        }

        mean_diff[k] /= w * h;
        ecs_os_free(texels);
    }

    test_assert(max_diff[0] < 0.05f);
    test_assert(mean_diff[0] > 0);
    test_assert(mean_diff[1] > mean_diff[0]);
    test_assert(max_diff[1] < 0.1f);
    test_assert(mean_diff[2] > 0.02);
    test_assert(mean_diff[2] > mean_diff[1] * 4);

    ecs_os_free(still);
    ecs_os_free(at0);
    ecs_fini(world);
}

void TextureNodesNoise_evolve_plane(void) {
    // textures/graph/noise
    tn_evolve_in(DomainPlane);
}

void TextureNodesNoise_evolve_sphere(void) {
    // textures/graph/noise
    tn_evolve_in(DomainSphere);
}

/** With one octave billow is |2 fbm - 1| and ridged (1 - |2 fbm - 1|)^2 of the same noise:
 * billow gathers at low values with a long tail up, ridged at high values with a long tail
 * down. */
void TextureNodesNoise_ridged_billow(void) {
    // textures/graph/noise
    ecs_world_t *world = tn_world();
    int32_t w = 128, h = 128, i;
    TexNoise noise = {.frequency = 8, .octaves = 1, .lacunarity = 2, .gain = 0.5f,
        .seed = 6, .offset = 1};
    float *fbm = tn_eval(world, &noise, w, h, DomainPlane);
    noise.fractal = NoiseBillow;
    float *billow = tn_eval(world, &noise, w, h, DomainPlane);
    noise.fractal = NoiseRidged;
    float *ridged = tn_eval(world, &noise, w, h, DomainPlane);
    for (i = 0; i < w * h; i ++) {
        float q = fabsf(2 * fbm[i * 4] - 1);
        test_assert(fabsf(billow[i * 4] - q) <= 3 * TN_TOLERANCE_16);
        test_assert(fabsf(ridged[i * 4] - (1 - q) * (1 - q)) <= 4 * TN_TOLERANCE_16);
    }

    tn_stats_t f = tn_stats(fbm, w * h, 0);
    tn_stats_t b = tn_stats(billow, w * h, 0);
    tn_stats_t r = tn_stats(ridged, w * h, 0);
    test_assert(b.min >= 0.0f && b.max <= 1.0f);
    test_assert(r.min >= 0.0f && r.max <= 1.0f);
    test_assert(b.mean < f.mean - 0.1);
    test_assert(b.median < b.mean);
    test_assert(r.mean > f.mean + 0.1);
    test_assert(r.median > r.mean);
    ecs_os_free(fbm);
    ecs_os_free(billow);
    ecs_os_free(ridged);

    noise.octaves = 5;
    noise.fractal = NoiseRidged;
    ridged = tn_eval(world, &noise, w, h, DomainPlane);
    r = tn_stats(ridged, w * h, 0);
    test_assert(r.min >= 0.0f && r.max <= 1.0f);
    test_assert(r.deviation > 0.02);
    noise.offset = 0.8f;
    float *sharp = tn_eval(world, &noise, w, h, DomainPlane);
    tn_stats_t s = tn_stats(sharp, w * h, 0);
    test_assert(s.mean < r.mean);
    ecs_os_free(ridged);
    ecs_os_free(sharp);

    noise.fractal = NoiseBillow;
    billow = tn_eval(world, &noise, w, h, DomainPlane);
    b = tn_stats(billow, w * h, 0);
    test_assert(b.min >= 0.0f && b.max <= 1.0f);
    test_assert(b.mean < 0.45);
    ecs_os_free(billow);
    ecs_fini(world);
}

/** An edit of the octaves, or of any other parameter, writes the parameter buffer: no module
 * compiles and the image changes. */
void TextureNodesNoise_octaves_no_recompile(void) {
    // textures/graph/noise
    ecs_world_t *world = tn_world();
    int32_t w = 64, h = 64, i;
    box_render_gpu_stats_t before, after;
    ecs_entity_t node;
    TexNoise noise = {.frequency = 4, .octaves = 1, .lacunarity = 2, .gain = 0.5f,
        .seed = 2, .offset = 1};
    ecs_entity_t tex = tn_graph(world, &noise, &node, w, h, DomainPlane);
    tn_wait(world, tex, 1);
    float *one = tn_read(world, tex, w, h);
    test_bool(box_render_gpu_stats(world, &before), true);

    noise.octaves = 6;
    ecs_set_ptr(world, node, TexNoise, &noise);
    tn_wait(world, tex, 2);
    float *six = tn_read(world, tex, w, h);
    noise.octaves = 16;
    noise.fractal = NoiseRidged;
    noise.seed = 3;
    ecs_set_ptr(world, node, TexNoise, &noise);
    tn_wait(world, tex, 3);
    test_bool(box_render_gpu_stats(world, &after), true);
    test_int(after.compiles, before.compiles);
    test_int(after.texture_graph_programs, 1);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 3);

    double diff = 0;
    for (i = 0; i < w * h; i ++) {
        diff += fabsf(six[i * 4] - one[i * 4]);
    }

    test_assert(diff / (w * h) > 0.005);
    ecs_os_free(one);
    ecs_os_free(six);
    ecs_fini(world);
}

/** Gain 0 leaves only the first octave: five octaves of gain 0 give the image of one octave, for
 * every fractal. */
void TextureNodesNoise_zero_gain_first_octave(void) {
    // textures/graph/noise
    ecs_world_t *world = tn_world();
    int32_t w = 64, h = 64, i, f;
    const TextureNoiseFractal fractals[3] = {NoiseFbm, NoiseBillow, NoiseRidged};
    for (f = 0; f < 3; f ++) {
        TexNoise noise = {.frequency = 8, .octaves = 1, .lacunarity = 2, .gain = 0.5f,
            .seed = 5, .fractal = fractals[f], .offset = 1};
        float *one = tn_eval(world, &noise, w, h, DomainPlane);
        noise.octaves = 5;
        noise.gain = 0;
        float *five = tn_eval(world, &noise, w, h, DomainPlane);
        for (i = 0; i < w * h; i ++) {
            if (fabsf(five[i * 4] - one[i * 4]) > TN_TOLERANCE_16) {
                printf("fractal %d texel %d: %f, expected %f\n", f, i, five[i * 4],
                    one[i * 4]);
            }

            test_assert(fabsf(five[i * 4] - one[i * 4]) <= TN_TOLERANCE_16);
        }

        ecs_os_free(one);
        ecs_os_free(five);
    }

    ecs_fini(world);
}
