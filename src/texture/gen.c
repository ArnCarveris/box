/**
 * @file texture/gen.c
 * @brief Generates the images of TextureGen on a worker thread: periodic height fields of noise,
 * blocks and cells, and the albedo and normal maps derived from them.
 *
 * Every pattern repeats over the texture: the noise lattices have a whole number of cells across
 * it, the block cuts and the cell points wrap.
 *
 * The randomness is integer hashing, and no library function but the exact sqrt and floor of IEEE
 * 754 is used, so a description gives the same bytes on every platform.
 */

#include "texture.h"
#include <math.h>

/* A fused multiply add rounds once where two operations round twice, which would change the
 * bytes between platforms that contract and those that do not. GCC ignores the standard pragma
 * and contracts by default unless a strict ISO C mode is selected, so it is told directly. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

/** Width a normal map strength refers to, in pixels */
#define BOX_GEN_STRENGTH_WIDTH (512.0)

/** Largest number of courses, blocks per course or cells along an axis */
#define BOX_GEN_MAX_COUNT (4096)

/** Largest number of noise octaves */
#define BOX_GEN_MAX_OCTAVES (16)

/** Unit gradients of the noise lattice, 22.5 degrees apart */
static const double box_gen_grad[16][2] = {
    { 1.0, 0.0 }, { 0.92387953251128674, 0.38268343236508978 },
    { 0.70710678118654752, 0.70710678118654752 }, { 0.38268343236508978, 0.92387953251128674 },
    { 0.0, 1.0 }, { -0.38268343236508978, 0.92387953251128674 },
    { -0.70710678118654752, 0.70710678118654752 }, { -0.92387953251128674, 0.38268343236508978 },
    { -1.0, 0.0 }, { -0.92387953251128674, -0.38268343236508978 },
    { -0.70710678118654752, -0.70710678118654752 }, { -0.38268343236508978, -0.92387953251128674 },
    { 0.0, -1.0 }, { 0.38268343236508978, -0.92387953251128674 },
    { 0.70710678118654752, -0.70710678118654752 }, { 0.92387953251128674, -0.38268343236508978 }
};

/** Scramble the bits of a value (lowbias32).
 *
 * @param x The value
 * @return The scrambled value
 */
static uint32_t box_gen_mix(
    uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;

    return x;
}

/** Combine a hash with a value.
 *
 * @param h The hash
 * @param v The value
 * @return The combined hash
 */
static uint32_t box_gen_hash(
    uint32_t h,
    uint32_t v)
{
    return box_gen_mix(h ^ box_gen_mix(v + 0x9e3779b9U));
}

/** Turn a hash into a number in [0, 1).
 *
 * @param h The hash
 * @return 24 bits of it as a fraction
 */
static double box_gen_unit(
    uint32_t h)
{
    return (double)(h >> 8) * (1.0 / 16777216.0);
}

/** Round to the nearest whole number, halves up.
 *
 * @param v The value
 * @return The rounded value
 */
static int32_t box_gen_round(
    double v)
{
    return (int32_t)floor(v + 0.5);
}

/** Wrap an index into 0..n - 1.
 *
 * @param i The index
 * @param n The period
 * @return The index modulo n, not negative
 */
static int32_t box_gen_wrap(
    int32_t i,
    int32_t n)
{
    int32_t r = i % n;

    return r < 0 ? r + n : r;
}

/** Hermite smoothstep from lo to hi.
 *
 * @param lo Value that gives 0
 * @param hi Value that gives 1
 * @param x The value
 * @return 0..1
 */
static double box_gen_smooth(
    double lo,
    double hi,
    double x)
{
    double t = (x - lo) / (hi - lo);
    t = t < 0 ? 0 : (t > 1 ? 1 : t);

    return t * t * (3.0 - 2.0 * t);
}

/** Clamp the tones of a layer.
 *
 * @param layer The layer
 * @return Its number of tones, 0 for none
 */
static int32_t box_gen_tones(
    const TextureGenLayer *layer)
{
    int32_t tones = layer->tones;

    return tones < 0 ? 0 : (tones > BOX_GEN_MAX_COUNT ? BOX_GEN_MAX_COUNT : tones);
}

/** Pick a random tone.
 *
 * @param r A number in [0, 1)
 * @param tones Number of tones, 0 for none
 * @return floor(r * tones), 0 without tones
 */
static int32_t box_gen_random_tone(
    double r,
    int32_t tones)
{
    int32_t tone = (int32_t)floor(r * tones);

    return tones < 1 ? 0 : (tone >= tones ? tones - 1 : tone);
}

/** Get the seed of a layer from the seed of its generator.
 *
 * @param layer The layer
 * @param seed Seed of the generator
 * @return The seed of the randomness of the layer
 */
static uint32_t box_gen_layer_seed(
    const TextureGenLayer *layer,
    int32_t seed)
{
    uint32_t h = box_gen_hash(0x5bd1e995U, (uint32_t)seed);
    h = box_gen_hash(h, (uint32_t)layer->seed);

    return box_gen_hash(h, (uint32_t)layer->kind);
}

/** Evaluate periodic gradient noise.
 *
 * @param seed Seed of the lattice
 * @param x Position across, in lattice cells, 0..px
 * @param y Position down, in lattice cells, 0..py
 * @param px Lattice cells across, the period
 * @param py Lattice cells down, the period
 * @return The noise, within +-sqrt(2) / 2
 */
static double box_gen_perlin(
    uint32_t seed,
    double x,
    double y,
    int32_t px,
    int32_t py)
{
    int32_t ix = (int32_t)floor(x), iy = (int32_t)floor(y), c;
    double fx = x - ix, fy = y - iy, dot[4];
    for (c = 0; c < 4; c ++) {
        int32_t cx = c & 1, cy = c >> 1;
        uint32_t h = box_gen_hash(seed, (uint32_t)box_gen_wrap(ix + cx, px));
        const double *g = box_gen_grad[box_gen_hash(h, (uint32_t)box_gen_wrap(iy + cy, py)) & 15];
        dot[c] = g[0] * (fx - cx) + g[1] * (fy - cy);
    }

    double u = fx * fx * fx * (fx * (fx * 6.0 - 15.0) + 10.0);
    double v = fy * fy * fy * (fy * (fy * 6.0 - 15.0) + 10.0);
    double top = dot[0] + (dot[1] - dot[0]) * u;
    double bottom = dot[2] + (dot[3] - dot[2]) * u;

    return top + (bottom - top) * v;
}

/** Fill a GenNoise layer: fbm of periodic gradient noise.
 *
 * @param layer The layer
 * @param seed Seed of the layer
 * @param width Width in pixels
 * @param height Height in pixels
 * @param value Receives the noise of each pixel, 0..1
 */
static void box_gen_noise(
    const TextureGenLayer *layer,
    uint32_t seed,
    int32_t width,
    int32_t height,
    float *value)
{
    int32_t octaves = layer->octaves > 0 ? layer->octaves : 1, o, x, y;
    double frequency = layer->frequency > 0 ? layer->frequency : 4.0;
    double lacunarity = layer->lacunarity > 0 ? layer->lacunarity : 2.0;
    double gain = layer->gain > 0 ? layer->gain : 0.5;
    double amp = 1.0, norm = 0.0;
    int32_t count = width * height, i;
    octaves = octaves > BOX_GEN_MAX_OCTAVES ? BOX_GEN_MAX_OCTAVES : octaves;
    double *sum = ecs_os_calloc_n(double, count);
    for (o = 0; o < octaves; o ++) {
        int32_t px = box_gen_round(frequency);
        px = px < 1 ? 1 : (px > BOX_GEN_MAX_COUNT ? BOX_GEN_MAX_COUNT : px);
        int32_t py = box_gen_round((double)px * height / width);
        py = py < 1 ? 1 : (py > BOX_GEN_MAX_COUNT ? BOX_GEN_MAX_COUNT : py);
        uint32_t s = box_gen_hash(box_gen_hash(seed, (uint32_t)o), (uint32_t)px);
        for (y = 0; y < height; y ++) {
            double ny = (y + 0.5) * py / height;
            for (x = 0; x < width; x ++) {
                double nx = (x + 0.5) * px / width;
                sum[y * width + x] += amp * box_gen_perlin(s, nx, ny, px, py);
            }
        }

        norm += amp;
        amp *= gain;
        frequency *= lacunarity;
    }

    for (i = 0; i < count; i ++) {
        double v = 0.5 + 0.70710678118654752 * sum[i] / norm;
        value[i] = (float)(v < 0 ? 0 : (v > 1 ? 1 : v));
    }

    ecs_os_free(sum);
}

/** Fill a GenBlocks layer: courses of blocks with beveled joints.
 *
 * The block that starts at a cut keeps the index of its cut through the sort of the cuts, so a
 * cut that wraps past 1 moves the block in the order but not its tone.
 *
 * @param layer The layer
 * @param seed Seed of the layer
 * @param width Width in pixels
 * @param height Height in pixels
 * @param value Receives the face mask of each pixel, 0..1
 * @param id Receives the random id of the block of each pixel, 0..1
 * @param tone Receives the tone of the block of each pixel, or NULL
 */
static void box_gen_blocks(
    const TextureGenLayer *layer,
    uint32_t seed,
    int32_t width,
    int32_t height,
    float *value,
    float *id,
    int32_t *tone)
{
    int32_t courses = layer->courses > 0 ? layer->courses : 8;
    int32_t n = layer->per_course > 0 ? layer->per_course : 4;
    int32_t k, i, j, x, y;
    courses = courses > BOX_GEN_MAX_COUNT ? BOX_GEN_MAX_COUNT : courses;
    n = n > BOX_GEN_MAX_COUNT ? BOX_GEN_MAX_COUNT : n;
    double half = 0.5 * layer->gap * width;
    double bevel = layer->softness > 0 ? layer->softness * width : 1.3 * layer->gap * width;
    double *cuts = ecs_os_malloc_n(double, courses * n);
    float *ids = ecs_os_malloc_n(float, courses * n);
    int32_t tones = box_gen_tones(layer);
    int32_t *order = ecs_os_malloc_n(int32_t, n);
    int32_t *block_tones = ecs_os_malloc_n(int32_t, courses * n);
    for (k = 0; k < courses; k ++) {
        double *c = &cuts[k * n];
        uint32_t hk = box_gen_hash(seed, (uint32_t)k);
        for (i = 0; i < n; i ++) {
            order[i] = i;
        }

        double shift = (k % 2) ? layer->stagger : 0.0;
        for (i = 0; i < n; i ++) {
            double jitter = (box_gen_unit(box_gen_hash(hk, (uint32_t)i)) * 2.0 - 1.0)
                * layer->jitter;
            double cut = (i + jitter + shift) / n;
            c[i] = cut - floor(cut);
        }

        for (i = 1; i < n; i ++) {
            double v = c[i];
            int32_t o = order[i];
            for (j = i; j > 0 && c[j - 1] > v; j --) {
                c[j] = c[j - 1];
                order[j] = order[j - 1];
            }

            c[j] = v;
            order[j] = o;
        }

        uint32_t hid = box_gen_hash(hk ^ 0xa511e9b3U, 1);
        uint32_t htone = box_gen_hash(hk ^ 0x3c6ef372U, 2);
        for (i = 0; i < n; i ++) {
            int32_t b = order[i], t = 0;
            ids[k * n + i] = (float)box_gen_unit(box_gen_hash(hid, (uint32_t)i));
            if (tones > 0) {
                if (layer->tone_order == GenToneChecker) {
                    t = (k + b) % tones;
                } else if (layer->tone_order == GenToneCourses) {
                    t = k % tones;
                } else if (layer->tone_order == GenToneColumns) {
                    t = b % tones;
                } else {
                    t = box_gen_random_tone(
                        box_gen_unit(box_gen_hash(htone, (uint32_t)b)), tones);
                }
            }

            block_tones[k * n + i] = t;
        }
    }

    for (y = 0; y < height; y ++) {
        double v = (y + 0.5) * courses / height;
        k = (int32_t)floor(v);
        k = k >= courses ? courses - 1 : k;
        double dy = v - k < k + 1 - v ? v - k : k + 1 - v;
        dy *= (double)height / courses;
        const double *c = &cuts[k * n];
        for (x = 0; x < width; x ++) {
            double u = (x + 0.5) / width, start, end;
            for (i = n - 1; i >= 0 && c[i] > u; i --) { }
            if (i < 0) {
                i = n - 1;
                start = c[n - 1] - 1.0;
            } else {
                start = c[i];
            }

            end = i + 1 < n ? c[i + 1] : c[0] + 1.0;
            double dx = (u - start < end - u ? u - start : end - u) * width;
            double d = dx < dy ? dx : dy;
            double mask = bevel > 0 ? box_gen_smooth(half, half + bevel, d)
                : (d >= half ? 1.0 : 0.0);
            value[y * width + x] = (float)mask;
            id[y * width + x] = ids[k * n + i];
            if (tone) {
                tone[y * width + x] = block_tones[k * n + i];
            }
        }
    }

    ecs_os_free(cuts);
    ecs_os_free(ids);
    ecs_os_free(order);
    ecs_os_free(block_tones);
}

/** Fill a GenCells layer: periodic Voronoi cells of jittered grid points.
 *
 * @param layer The layer
 * @param seed Seed of the layer
 * @param width Width in pixels
 * @param height Height in pixels
 * @param value Receives the distance of each pixel, 0..1
 * @param id Receives the random id of the cell of each pixel, 0..1
 * @param tone Receives the random tone of the cell of each pixel, or NULL
 */
static void box_gen_cells(
    const TextureGenLayer *layer,
    uint32_t seed,
    int32_t width,
    int32_t height,
    float *value,
    float *id,
    int32_t *tone)
{
    double count = layer->count > 0 ? layer->count : 64;
    double squash = layer->squash > 0 ? layer->squash : 1.0;
    double edge = layer->edge > 0 ? layer->edge : 0.25;
    int32_t gx = box_gen_round(sqrt(count * width / height / squash)), gy, i, j, x, y;
    gx = gx < 1 ? 1 : (gx > BOX_GEN_MAX_COUNT ? BOX_GEN_MAX_COUNT : gx);
    gy = box_gen_round(count / gx);
    gy = gy < 1 ? 1 : (gy > BOX_GEN_MAX_COUNT ? BOX_GEN_MAX_COUNT : gy);
    double cw = (double)width / gx, ch = (double)height / gy;
    double size = sqrt(cw * ch * squash);
    double *jx = ecs_os_malloc_n(double, gx * gy);
    double *jy = ecs_os_malloc_n(double, gx * gy);
    float *ids = ecs_os_malloc_n(float, gx * gy);
    int32_t *cell_tones = ecs_os_malloc_n(int32_t, gx * gy);
    int32_t tones = box_gen_tones(layer);
    for (j = 0; j < gy; j ++) {
        for (i = 0; i < gx; i ++) {
            uint32_t h = box_gen_hash(box_gen_hash(seed, (uint32_t)i), (uint32_t)j);
            jx[j * gx + i] = 0.15 + 0.7 * box_gen_unit(box_gen_hash(h, 0));
            jy[j * gx + i] = 0.15 + 0.7 * box_gen_unit(box_gen_hash(h, 1));
            ids[j * gx + i] = (float)box_gen_unit(box_gen_hash(h, 2));
            cell_tones[j * gx + i] = box_gen_random_tone(box_gen_unit(box_gen_hash(h, 3)), tones);
        }
    }

    for (y = 0; y < height; y ++) {
        double py = y + 0.5;
        int32_t cj = (int32_t)floor(py / ch);
        for (x = 0; x < width; x ++) {
            double px = x + 0.5, d1 = 1e30, d2 = 1e30;
            int32_t ci = (int32_t)floor(px / cw), oi, oj, nearest = 0;
            for (oj = -2; oj <= 2; oj ++) {
                int32_t wj = box_gen_wrap(cj + oj, gy);
                for (oi = -2; oi <= 2; oi ++) {
                    int32_t cell = wj * gx + box_gen_wrap(ci + oi, gx);
                    double dx = px - (ci + oi + jx[cell]) * cw;
                    double dy = (py - (cj + oj + jy[cell]) * ch) * squash;
                    double d = sqrt(dx * dx + dy * dy);
                    if (d < d1) {
                        d2 = d1;
                        d1 = d;
                        nearest = cell;
                    } else if (d < d2) {
                        d2 = d;
                    }
                }
            }

            double v;
            if (layer->distance == GenEdge) {
                v = box_gen_smooth(0.0, edge, (d2 - d1) / size);
            } else {
                v = d1 / size;
                v = v > 1 ? 1 : v;
            }

            value[y * width + x] = (float)v;
            id[y * width + x] = ids[nearest];
            if (tone) {
                tone[y * width + x] = cell_tones[nearest];
            }
        }
    }

    ecs_os_free(jx);
    ecs_os_free(jy);
    ecs_os_free(ids);
    ecs_os_free(cell_tones);
}

void box_texture_gen_pattern(
    const TextureGenLayer *layer,
    int32_t seed,
    int32_t width,
    int32_t height,
    float *value,
    float *id,
    int32_t *tone)
{
    uint32_t s = box_gen_layer_seed(layer, seed);
    int32_t count = width * height;
    if (layer->kind == GenBlocks) {
        box_gen_blocks(layer, s, width, height, value, id, tone);
    } else if (layer->kind == GenCells) {
        box_gen_cells(layer, s, width, height, value, id, tone);
    } else {
        box_gen_noise(layer, s, width, height, value);
        ecs_os_memset(id, 0, count * ECS_SIZEOF(float));
        if (tone) {
            ecs_os_memset(tone, 0, count * ECS_SIZEOF(int32_t));
        }
    }
}

void box_texture_gen_height(
    const box_texture_gen_t *gen,
    float *height,
    int32_t *tone)
{
    int32_t count = gen->width * gen->height, l, i;
    double *sum = ecs_os_calloc_n(double, count);
    float *value = ecs_os_malloc_n(float, count);
    float *id = ecs_os_malloc_n(float, count);
    int32_t *scratch = NULL;
    if (tone) {
        ecs_os_memset(tone, 0, count * ECS_SIZEOF(int32_t));
    }

    for (l = 0; l < gen->layer_count; l ++) {
        TextureGenLayer layer = gen->layers[l];
        double amount = layer.amount, id_amount = layer.id_amount;
        double tone_amount = layer.tone_amount;
        int32_t tones = box_gen_tones(&layer);
        if (amount == 0 && id_amount == 0 && tone_amount == 0) {
            amount = 1.0;
        }

        tone_amount = tones > 1 ? tone_amount / (tones - 1) : 0.0;
        int32_t *t = NULL;
        if (tone && l == gen->palette_layer) {
            t = tone;
            if (!tones) {
                layer.tones = gen->palette_count;
            }
        } else if (tone_amount != 0) {
            if (!scratch) {
                scratch = ecs_os_malloc_n(int32_t, count);
            }

            t = scratch;
        }

        box_texture_gen_pattern(&layer, gen->seed, gen->width, gen->height, value, id, t);
        for (i = 0; i < count; i ++) {
            double v = amount * value[i] + id_amount * id[i];
            if (tone_amount != 0) {
                v += tone_amount * t[i];
            }

            if (layer.op == GenMultiply) {
                sum[i] *= v;
            } else if (layer.op == GenMax) {
                sum[i] = v > sum[i] ? v : sum[i];
            } else {
                sum[i] += v;
            }
        }
    }

    double lo = count ? sum[0] : 0, hi = lo;
    for (i = 1; i < count; i ++) {
        lo = sum[i] < lo ? sum[i] : lo;
        hi = sum[i] > hi ? sum[i] : hi;
    }

    double range = hi - lo;
    for (i = 0; i < count; i ++) {
        height[i] = range > 1e-12 ? (float)((sum[i] - lo) / range) : 0.5f;
    }

    ecs_os_free(sum);
    ecs_os_free(value);
    ecs_os_free(id);
    ecs_os_free(scratch);
}

/** Store a value in 0..1 as a byte.
 *
 * @param v The value
 * @return round(255 v), clamped
 */
static uint8_t box_gen_byte(
    double v)
{
    int32_t b = box_gen_round(v * 255.0);

    return (uint8_t)(b < 0 ? 0 : (b > 255 ? 255 : b));
}

/** Map a height through a color ramp.
 *
 * @param keys The keys in order of position, at least one
 * @param key_count Number of keys
 * @param h The height
 * @param p Receives the display encoded red, green and blue
 */
static void box_gen_ramp(
    const TextureGenKey *keys,
    int32_t key_count,
    double h,
    uint8_t *p)
{
    int32_t k;
    const rgb *a = &keys[0].color, *b = a;
    double t = 0;
    if (h >= keys[key_count - 1].position) {
        a = b = &keys[key_count - 1].color;
    } else if (h > keys[0].position) {
        for (k = 0; k + 1 < key_count && keys[k + 1].position <= h; k ++) { }
        double p0 = keys[k].position, p1 = keys[k + 1].position;
        a = &keys[k].color;
        b = &keys[k + 1].color;
        t = p1 > p0 ? (h - p0) / (p1 - p0) : 1.0;
    }

    p[0] = (uint8_t)box_gen_round(a->r + (b->r - a->r) * t);
    p[1] = (uint8_t)box_gen_round(a->g + (b->g - a->g) * t);
    p[2] = (uint8_t)box_gen_round(a->b + (b->b - a->b) * t);
}

void box_texture_gen_albedo(
    const float *height,
    int32_t count,
    const TextureGenKey *keys,
    int32_t key_count,
    const int32_t *tone,
    const rgb *palette,
    int32_t palette_count,
    uint8_t *rgba)
{
    int32_t i;
    bool tinted = palette && palette_count > 0;
    for (i = 0; i < count; i ++) {
        double h = height[i];
        uint8_t *p = &rgba[i * 4];
        p[3] = 255;
        if (!key_count) {
            p[0] = p[1] = p[2] = tinted ? 255 : box_gen_byte(h);
        } else {
            box_gen_ramp(keys, key_count, h, p);
        }

        if (tinted) {
            const rgb *c = &palette[(tone ? tone[i] : 0) % palette_count];
            p[0] = (uint8_t)box_gen_round((double)c->r * p[0] / 255.0);
            p[1] = (uint8_t)box_gen_round((double)c->g * p[1] / 255.0);
            p[2] = (uint8_t)box_gen_round((double)c->b * p[2] / 255.0);
        }
    }
}

void box_texture_gen_normal(
    const float *height,
    int32_t width,
    int32_t h,
    float strength,
    uint8_t *rgba)
{
    double scale = (double)strength * width / BOX_GEN_STRENGTH_WIDTH;
    int32_t x, y;
    for (y = 0; y < h; y ++) {
        const float *row = &height[y * width];
        const float *up = &height[box_gen_wrap(y - 1, h) * width];
        const float *down = &height[box_gen_wrap(y + 1, h) * width];
        for (x = 0; x < width; x ++) {
            double dx = ((double)row[box_gen_wrap(x + 1, width)]
                - row[box_gen_wrap(x - 1, width)]) * scale;
            double dy = ((double)down[x] - up[x]) * scale;
            double len = sqrt(dx * dx + dy * dy + 1.0);
            uint8_t *p = &rgba[(y * width + x) * 4];
            p[0] = box_gen_byte(0.5 - 0.5 * dx / len);
            p[1] = box_gen_byte(0.5 + 0.5 * dy / len);
            p[2] = box_gen_byte(0.5 + 0.5 / len);
            p[3] = 255;
        }
    }
}

void box_texture_gen_free(
    box_texture_gen_t *gen)
{
    if (gen) {
        ecs_os_free(gen->layers);
        ecs_os_free(gen->colors);
        ecs_os_free(gen->palette);
        ecs_os_free(gen);
    }
}

void box_texture_generate(
    box_texture_job_t *job)
{
    const box_texture_gen_t *gen = job->gen;
    int32_t count = gen->width * gen->height, i;
    float *height = ecs_os_malloc_n(float, count);
    int32_t *tone = NULL;
    if (gen->output == GenAlbedo && gen->palette_count) {
        tone = ecs_os_malloc_n(int32_t, count);
    }

    box_texture_gen_height(gen, height, tone);
    job->width = gen->width;
    job->height = gen->height;
    job->single_channel = gen->output == GenHeight;
    if (gen->output == GenAlbedo) {
        job->pixels = ecs_os_malloc(count * 4);
        box_texture_gen_albedo(height, count, gen->colors, gen->color_count, tone,
            tone ? gen->palette : NULL, tone ? gen->palette_count : 0, job->pixels);
    } else if (gen->output == GenNormal) {
        job->pixels = ecs_os_malloc(count * 4);
        box_texture_gen_normal(height, gen->width, gen->height, gen->strength, job->pixels);
    } else {
        job->pixels = ecs_os_malloc(count);
        for (i = 0; i < count; i ++) {
            job->pixels[i] = box_gen_byte(height[i]);
        }
    }

    ecs_os_free(height);
    ecs_os_free(tone);
}
