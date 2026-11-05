/**
 * @file TextureGen.c
 * @brief Tests for TextureGen: seamless height, albedo and normal maps generated from noise,
 * blocks and cells on the job system, deterministic per seed.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "texture.h"

/** Fixed descriptions whose bytes are hashed, one per pattern */
#define GEN_NOISE "test/assets/textures/gen/noise.flecs"
#define GEN_BLOCKS "test/assets/textures/gen/blocks.flecs"
#define GEN_CELLS "test/assets/textures/gen/cells.flecs"
#define GEN_BLOCKS_TONES "test/assets/textures/gen/blocks_tones.flecs"

/** Create a headless world, which waits for its texture loads.
 *
 * @return The world
 */
static ecs_world_t* world_new(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    return world;
}

/** Run frames until a texture is no longer loading.
 *
 * @param world The world
 * @param tex The texture entity
 * @return The status
 */
static const TextureStatus* wait_texture(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    int32_t frames = 0;
    ecs_progress(world, 0);
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    return ecs_get(world, tex, TextureStatus);
}

/** Get the registry entry of a texture entity.
 *
 * @param world The world
 * @param tex The texture entity
 * @return The entry
 */
static const box_texture_t* entry_of(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    box_textures_t *t = box_textures_get(world);
    test_assert(t != NULL);
    box_texture_t *entry = ecs_map_get_deref(&t->textures, box_texture_t, tex);
    test_assert(entry != NULL);

    return entry;
}

/** Generate a texture and copy its bytes.
 *
 * @param world The world
 * @param tex The texture entity with a TextureGen
 * @param size Receives the number of bytes
 * @return The bytes, freed by the caller
 */
static uint8_t* gen_bytes(
    ecs_world_t *world,
    ecs_entity_t tex,
    int32_t *size)
{
    test_int(wait_texture(world, tex)->state, TextureReady);
    const box_texture_t *entry = entry_of(world, tex);
    test_assert(entry->pixels != NULL);
    *size = entry->width * entry->height * (entry->single_channel ? 1 : 4);

    return ecs_os_memdup(entry->pixels, *size);
}

/** Generate the height field of a single layer at 64 by 48 pixels.
 *
 * @param layer The layer
 * @param seed Seed of the generator
 * @param size Receives the number of bytes
 * @return The R8 bytes, freed by the caller
 */
static uint8_t* layer_bytes(
    const TextureGenLayer *layer,
    int32_t seed,
    int32_t *size)
{
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_new(world);
    TextureGen gen = { .width = 64, .height = 48, .seed = seed };
    ecs_vec_init_t(NULL, &gen.layers, TextureGenLayer, 1);
    *ecs_vec_append_t(NULL, &gen.layers, TextureGenLayer) = *layer;
    ecs_set_ptr(world, tex, TextureGen, &gen);
    ecs_vec_fini_t(NULL, &gen.layers, TextureGenLayer);
    uint8_t *result = gen_bytes(world, tex, size);
    test_int(*size, 64 * 48);
    ecs_fini(world);

    return result;
}

/** Check that a single channel image continues across its wrap: the mean difference between the
 * first and last column (row) is not much larger than between neighbors inside.
 *
 * @param px The R8 pixels
 * @param w Width
 * @param h Height
 */
static void test_tiles(
    const uint8_t *px,
    int32_t w,
    int32_t h)
{
    double wrap = 0, inside = 0;
    int32_t x, y;
    for (y = 0; y < h; y ++) {
        wrap += abs(px[y * w] - px[y * w + w - 1]);
        for (x = 0; x + 1 < w; x ++) {
            inside += abs(px[y * w + x] - px[y * w + x + 1]);
        }
    }

    wrap /= h;
    inside /= (double)h * (w - 1);
    test_assert(wrap <= 2.0 * inside + 1.0);

    wrap = 0;
    inside = 0;
    for (x = 0; x < w; x ++) {
        wrap += abs(px[x] - px[(h - 1) * w + x]);
        for (y = 0; y + 1 < h; y ++) {
            inside += abs(px[y * w + x] - px[(y + 1) * w + x]);
        }
    }

    wrap /= w;
    inside /= (double)w * (h - 1);
    test_assert(wrap <= 2.0 * inside + 1.0);
}

/** Hash bytes with 32 bit FNV-1a.
 *
 * @param hash The hash so far, 2166136261 to start
 * @param data The bytes
 * @param size Number of bytes
 * @return The hash
 */
static uint32_t fnv1a(
    uint32_t hash,
    const uint8_t *data,
    int32_t size)
{
    int32_t i;
    for (i = 0; i < size; i ++) {
        hash ^= data[i];
        hash *= 16777619U;
    }

    return hash;
}

/** Hash the height, albedo and normal map of a fixed description.
 *
 * @param file The script, named from the repository root
 * @param prefix Prefix of its entities: prefix_height, prefix_albedo and prefix_normal
 * @return The hash of the bytes of the three
 */
static uint32_t hash_file(
    const char *file,
    const char *prefix)
{
    static const char *outputs[3] = { "height", "albedo", "normal" };
    ecs_world_t *world = world_new();
    uint32_t hash = 2166136261U;
    int32_t i;
    char *path = box_app_locate(file);
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);
    for (i = 0; i < 3; i ++) {
        char name[64];
        int32_t size = 0;
        snprintf(name, sizeof(name), "%s_%s", prefix, outputs[i]);
        ecs_entity_t tex = ecs_lookup(world, name);
        test_assert(tex != 0);
        uint8_t *bytes = gen_bytes(world, tex, &size);
        test_int(size, 64 * 48 * (i ? 4 : 1));
        hash = fnv1a(hash, bytes, size);
        ecs_os_free(bytes);
    }

    ecs_fini(world);

    return hash;
}

/** Check the hash of a fixed description, printing the actual one when it differs.
 *
 * @param file The script
 * @param prefix Prefix of its entities
 * @param expect The recorded hash
 */
static void test_hash(
    const char *file,
    const char *prefix,
    uint32_t expect)
{
    uint32_t hash = hash_file(file, prefix);
    if (hash != expect) {
        printf("%s: hash 0x%08xU, recorded 0x%08xU\n", file, hash, expect);
    }

    test_assert(hash == expect);
    test_assert(hash_file(file, prefix) == hash);
}

/** Run a script and generate one of its textures.
 *
 * @param world The world
 * @param code The script
 * @param name The texture entity
 * @param size Receives the number of bytes
 * @return The bytes, freed by the caller
 */
static uint8_t* script_bytes(
    ecs_world_t *world,
    const char *code,
    const char *name,
    int32_t *size)
{
    if (code) {
        test_assert(ecs_script_run(world, "gen", code, NULL) == 0);
    }

    ecs_entity_t tex = ecs_lookup(world, name);
    test_assert(tex != 0);

    return gen_bytes(world, tex, size);
}

/** Check that an RGBA8 pixel has a color.
 *
 * @param px The pixels
 * @param w Width
 * @param x Column
 * @param y Row
 * @param c The color
 */
static void test_pixel(
    const uint8_t *px,
    int32_t w,
    int32_t x,
    int32_t y,
    rgb c)
{
    const uint8_t *p = &px[(y * w + x) * 4];
    test_int(p[0], c.r);
    test_int(p[1], c.g);
    test_int(p[2], c.b);
    test_int(p[3], 255);
}

/** Check whether two RGBA8 pixels have the same color.
 *
 * @param px The pixels
 * @param w Width
 * @param a Index of the first pixel
 * @param b Index of the second pixel
 * @return Whether red, green and blue are equal
 */
static bool same_color(
    const uint8_t *px,
    int32_t w,
    int32_t a,
    int32_t b)
{
    (void)w;
    return !memcmp(&px[a * 4], &px[b * 4], 3);
}

/** Palette of the tone tests, three distinct colors */
static const rgb tone_colors[3] = { { 200, 0, 0 }, { 0, 0, 200 }, { 0, 180, 0 } };

/** A noise layer of a few octaves */
static const TextureGenLayer noise_layer = {
    .kind = GenNoise, .frequency = 4, .octaves = 3, .amount = 1 };

/** Staggered, jittered blocks */
static const TextureGenLayer blocks_layer = {
    .kind = GenBlocks, .courses = 4, .per_course = 3, .gap = 0.04f, .jitter = 0.22f,
    .stagger = 0.5f, .amount = 0.8f, .id_amount = 0.2f };

/** Cells by the distance to their edge, with their ids */
static const TextureGenLayer cells_layer = {
    .kind = GenCells, .count = 12, .distance = GenEdge, .edge = 0.3f, .amount = 0.8f,
    .id_amount = 0.2f };

void TextureGen_noise_tiles(void) {
    // materials/textures
    int32_t size = 0;
    uint8_t *px = layer_bytes(&noise_layer, 5, &size);
    test_tiles(px, 64, 48);
    ecs_os_free(px);
}

void TextureGen_blocks_tiles(void) {
    // materials/textures
    int32_t size = 0;
    uint8_t *px = layer_bytes(&blocks_layer, 5, &size);
    test_tiles(px, 64, 48);
    ecs_os_free(px);
}

void TextureGen_cells_tiles(void) {
    // materials/textures
    int32_t size = 0;
    uint8_t *px = layer_bytes(&cells_layer, 5, &size);
    test_tiles(px, 64, 48);
    ecs_os_free(px);
}

void TextureGen_cells_nearest_tiles(void) {
    // materials/textures
    TextureGenLayer layer = { .kind = GenCells, .count = 9, .squash = 1.5f };
    int32_t size = 0;
    uint8_t *px = layer_bytes(&layer, 2, &size);
    test_tiles(px, 64, 48);
    ecs_os_free(px);
}

void TextureGen_pattern_wraps(void) {
    // materials/textures
    static const TextureGenLayer *layers[3] = { &noise_layer, &blocks_layer, &cells_layer };
    float value[64 * 48], id[64 * 48];
    int32_t l, x, y;

    ecs_world_t *world = world_new();
    for (l = 0; l < 3; l ++) {
        box_texture_gen_pattern(layers[l], 9, 64, 48, value, id, NULL);
        for (y = 0; y < 48; y ++) {
            for (x = 0; x < 64; x ++) {
                test_assert(value[y * 64 + x] >= 0 && value[y * 64 + x] <= 1);
                test_assert(id[y * 64 + x] >= 0 && id[y * 64 + x] < 1);
            }
        }
    }

    box_texture_gen_pattern(&noise_layer, 9, 64, 48, value, id, NULL);
    for (y = 0; y < 48; y ++) {
        test_assert(fabsf(value[y * 64] - value[y * 64 + 63]) < 0.1f);
    }

    for (x = 0; x < 64; x ++) {
        test_assert(fabsf(value[x] - value[47 * 64 + x]) < 0.1f);
    }

    ecs_fini(world);
}

void TextureGen_same_seed_same_bytes(void) {
    // materials/textures
    static const TextureGenLayer *layers[3] = { &noise_layer, &blocks_layer, &cells_layer };
    int32_t l;
    for (l = 0; l < 3; l ++) {
        int32_t a_size = 0, b_size = 0;
        uint8_t *a = layer_bytes(layers[l], 21, &a_size);
        uint8_t *b = layer_bytes(layers[l], 21, &b_size);
        test_int(a_size, b_size);
        test_assert(!memcmp(a, b, a_size));
        ecs_os_free(a);
        ecs_os_free(b);
    }
}

void TextureGen_other_seed_other_bytes(void) {
    // materials/textures
    static const TextureGenLayer *layers[3] = { &noise_layer, &blocks_layer, &cells_layer };
    int32_t l;
    for (l = 0; l < 3; l ++) {
        int32_t a_size = 0, b_size = 0;
        uint8_t *a = layer_bytes(layers[l], 21, &a_size);
        uint8_t *b = layer_bytes(layers[l], 22, &b_size);
        test_int(a_size, b_size);
        test_assert(memcmp(a, b, a_size) != 0);
        ecs_os_free(a);
        ecs_os_free(b);
    }
}

void TextureGen_height_spans_unit(void) {
    // materials/textures
    static const TextureGenLayer *layers[3] = { &noise_layer, &blocks_layer, &cells_layer };
    int32_t l, i;
    for (l = 0; l < 3; l ++) {
        int32_t size = 0, lo = 255, hi = 0;
        uint8_t *px = layer_bytes(layers[l], 4, &size);
        for (i = 0; i < size; i ++) {
            lo = px[i] < lo ? px[i] : lo;
            hi = px[i] > hi ? px[i] : hi;
        }

        test_int(lo, 0);
        test_int(hi, 255);
        ecs_os_free(px);
    }
}

void TextureGen_flat_normal(void) {
    // materials/textures
    float height[8 * 6];
    uint8_t rgba[8 * 6 * 4];
    int32_t i;
    for (i = 0; i < 8 * 6; i ++) {
        height[i] = 0.3f;
    }

    box_texture_gen_normal(height, 8, 6, 3, rgba);
    for (i = 0; i < 8 * 6; i ++) {
        test_int(rgba[i * 4 + 0], 128);
        test_int(rgba[i * 4 + 1], 128);
        test_int(rgba[i * 4 + 2], 255);
        test_int(rgba[i * 4 + 3], 255);
    }

    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TextureGen, { .width = 16, .height = 16, .output = GenNormal });
    int32_t size = 0;
    uint8_t *px = gen_bytes(world, tex, &size);
    test_int(size, 16 * 16 * 4);
    for (i = 0; i < 16 * 16; i ++) {
        test_int(px[i * 4 + 0], 128);
        test_int(px[i * 4 + 1], 128);
        test_int(px[i * 4 + 2], 255);
    }

    ecs_os_free(px);
    ecs_fini(world);
}

void TextureGen_normal_green_up(void) {
    // materials/textures
    float height[8 * 8];
    uint8_t rgba[8 * 8 * 4];
    int32_t x, y;

    for (y = 0; y < 8; y ++) {
        for (x = 0; x < 8; x ++) {
            height[y * 8 + x] = y / 8.0f;
        }
    }

    box_texture_gen_normal(height, 8, 8, 64, rgba);
    for (y = 1; y < 7; y ++) {
        for (x = 0; x < 8; x ++) {
            const uint8_t *p = &rgba[(y * 8 + x) * 4];
            test_int(p[0], 128);
            test_assert(p[1] > 140);
            test_assert(p[2] < 255);
        }
    }

    for (y = 0; y < 8; y ++) {
        for (x = 0; x < 8; x ++) {
            height[y * 8 + x] = 1.0f - y / 8.0f;
        }
    }

    box_texture_gen_normal(height, 8, 8, 64, rgba);
    for (y = 1; y < 7; y ++) {
        test_assert(rgba[(y * 8) * 4 + 1] < 116);
    }

    for (y = 0; y < 8; y ++) {
        for (x = 0; x < 8; x ++) {
            height[y * 8 + x] = x / 8.0f;
        }
    }

    box_texture_gen_normal(height, 8, 8, 64, rgba);
    for (x = 1; x < 7; x ++) {
        const uint8_t *p = &rgba[(3 * 8 + x) * 4];
        test_assert(p[0] < 116);
        test_int(p[1], 128);
    }
}

void TextureGen_albedo_ramp(void) {
    // materials/textures
    static const float height[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    static const TextureGenKey keys[2] = {
        { 0.25f, { 0, 100, 200 } }, { 0.75f, { 200, 100, 0 } } };
    uint8_t rgba[5 * 4];
    box_texture_gen_albedo(height, 5, keys, 2, NULL, NULL, 0, rgba);
    test_int(rgba[0], 0);
    test_int(rgba[2], 200);
    test_int(rgba[4], 0);
    test_int(rgba[8], 100);
    test_int(rgba[9], 100);
    test_int(rgba[10], 100);
    test_int(rgba[11], 255);
    test_int(rgba[12], 200);
    test_int(rgba[16], 200);
    test_int(rgba[18], 0);

    box_texture_gen_albedo(height, 5, NULL, 0, NULL, NULL, 0, rgba);
    test_int(rgba[0], 0);
    test_int(rgba[8], 128);
    test_int(rgba[9], 128);
    test_int(rgba[16], 255);
    test_int(rgba[19], 255);
}

void TextureGen_writes_texture_and_status(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const char *code =
        "using box.texture\n"
        "stone {\n"
        "  TextureGen: {width: 32, height: 16, seed: 2, layers: [\n"
        "    {kind: GenBlocks, courses: 2, per_course: 2, gap: 0.05, amount: 1}\n"
        "    {kind: GenNoise, frequency: 8, octaves: 2, amount: 0.3}\n"
        "  ]}\n"
        "}\n"
        "stone_color { TextureGen: {source: stone, output: GenAlbedo,\n"
        "  colors: [{0, {10, 20, 30}}, {1, {200, 210, 220}}]} }\n"
        "stone_n { TextureGen: {source: stone, output: GenNormal, strength: 2} }\n";
    test_assert(ecs_script_run(world, "gen", code, NULL) == 0);
    ecs_entity_t stone = ecs_lookup(world, "stone");
    ecs_entity_t color = ecs_lookup(world, "stone_color");
    ecs_entity_t normal = ecs_lookup(world, "stone_n");
    test_assert(stone && color && normal);
    test_int(ecs_vec_count(&ecs_get(world, stone, TextureGen)->layers), 2);
    test_int(ecs_vec_count(&ecs_get(world, color, TextureGen)->colors), 2);

    const ecs_entity_t all[3] = { stone, color, normal };
    int32_t i;
    for (i = 0; i < 3; i ++) {
        const TextureStatus *status = wait_texture(world, all[i]);
        test_int(status->state, TextureReady);
        test_int(status->revision, 1);
        test_assert(status->error == NULL);
        const Texture *size = ecs_get(world, all[i], Texture);
        test_assert(size != NULL);
        test_int(size->width, 32);
        test_int(size->height, 16);
        const box_texture_t *entry = entry_of(world, all[i]);
        test_bool(entry->single_channel, i == 0);
        test_int(entry->width, 32);
        test_int(entry->height, 16);
        test_assert(entry->pixels != NULL);
    }

    const box_texture_t *h = entry_of(world, stone);
    const box_texture_t *c = entry_of(world, color);
    for (i = 0; i < 32 * 16; i ++) {
        if (h->pixels[i] == 0) {
            test_int(c->pixels[i * 4], 10);
        } else if (h->pixels[i] == 255) {
            test_int(c->pixels[i * 4], 200);
        }

        test_int(c->pixels[i * 4 + 3], 255);
    }

    ecs_fini(world);
}

void TextureGen_change_regenerates(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_new(world);
    TextureGen gen = { .width = 32, .height = 32, .seed = 1 };
    ecs_vec_init_t(NULL, &gen.layers, TextureGenLayer, 1);
    *ecs_vec_append_t(NULL, &gen.layers, TextureGenLayer) = noise_layer;
    ecs_set_ptr(world, tex, TextureGen, &gen);
    int32_t size = 0;
    uint8_t *before = gen_bytes(world, tex, &size);
    int32_t revision = ecs_get(world, tex, TextureStatus)->revision;
    test_int(revision, 1);

    gen.seed = 2;
    ecs_set_ptr(world, tex, TextureGen, &gen);
    uint8_t *after = gen_bytes(world, tex, &size);
    test_int(ecs_get(world, tex, TextureStatus)->revision, revision + 1);
    test_assert(memcmp(before, after, size) != 0);
    ecs_os_free(after);

    gen.width = 16;
    ecs_set_ptr(world, tex, TextureGen, &gen);
    after = gen_bytes(world, tex, &size);
    test_int(ecs_get(world, tex, TextureStatus)->revision, revision + 2);
    test_int(ecs_get(world, tex, Texture)->width, 16);
    test_int(size, 16 * 32);

    ecs_os_free(before);
    ecs_os_free(after);
    ecs_vec_fini_t(NULL, &gen.layers, TextureGenLayer);
    ecs_fini(world);
}

void TextureGen_source_change_regenerates(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const char *code =
        "using box.texture\n"
        "field { TextureGen: {width: 32, height: 32, seed: 1,\n"
        "  layers: [{kind: GenCells, count: 6, amount: 1}]} }\n"
        "field_n { TextureGen: {source: field, output: GenNormal} }\n";
    test_assert(ecs_script_run(world, "gen", code, NULL) == 0);
    ecs_entity_t field = ecs_lookup(world, "field");
    ecs_entity_t normal = ecs_lookup(world, "field_n");
    int32_t size = 0;
    uint8_t *before = gen_bytes(world, normal, &size);
    test_int(ecs_get(world, normal, TextureStatus)->revision, 1);

    TextureGen *gen = ecs_ensure(world, field, TextureGen);
    gen->seed = 2;
    ecs_modified(world, field, TextureGen);
    uint8_t *after = gen_bytes(world, normal, &size);
    test_int(ecs_get(world, normal, TextureStatus)->revision, 2);
    test_assert(memcmp(before, after, size) != 0);

    ecs_os_free(before);
    ecs_os_free(after);
    ecs_fini(world);
}

void TextureGen_source_missing(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t plain = ecs_entity(world, { .name = "plain" });
    ecs_entity_t tex = ecs_entity(world, { .name = "derived" });
    ecs_log_set_level(-4);
    ecs_set(world, tex, TextureGen, { .source = plain, .output = GenAlbedo });
    const TextureStatus *status = wait_texture(world, tex);
    test_int(status->state, TextureFailed);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "derived") != NULL);
    test_assert(strstr(status->error, "plain") != NULL);

    TextureGen gen = { .width = 8, .height = 8 };
    ecs_vec_init_t(NULL, &gen.layers, TextureGenLayer, 1);
    *ecs_vec_append_t(NULL, &gen.layers, TextureGenLayer) = noise_layer;
    ecs_set_ptr(world, plain, TextureGen, &gen);
    ecs_vec_fini_t(NULL, &gen.layers, TextureGenLayer);
    test_int(wait_texture(world, tex)->state, TextureReady);
    test_int(ecs_get(world, tex, Texture)->width, 8);

    ecs_fini(world);
}

void TextureGen_source_declared_after(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const char *code =
        "using box.texture\n"
        "ashlar_height {}\n"
        "ashlar { TextureGen: {source: ashlar_height, output: GenNormal} }\n"
        "ashlar_height { TextureGen: {width: 16, height: 16, seed: 3,\n"
        "  layers: [{kind: GenNoise, frequency: 4, octaves: 2, amount: 1}]} }\n";
    test_assert(ecs_script_run(world, "ashlar", code, NULL) == 0);
    ecs_entity_t tex = ecs_lookup(world, "ashlar");
    test_assert(tex != 0);
    const TextureStatus *status = wait_texture(world, tex);
    test_int(status->state, TextureReady);
    test_assert(status->error == NULL);
    test_int(ecs_get(world, tex, Texture)->width, 16);

    ecs_fini(world);
}

void TextureGen_source_gen_removed(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const char *code =
        "using box.texture\n"
        "field { TextureGen: {width: 16, height: 16, seed: 1,\n"
        "  layers: [{kind: GenNoise, frequency: 4, octaves: 2, amount: 1}]} }\n"
        "field_n { TextureGen: {source: field, output: GenNormal} }\n";
    test_assert(ecs_script_run(world, "gen", code, NULL) == 0);
    ecs_entity_t field = ecs_lookup(world, "field");
    ecs_entity_t normal = ecs_lookup(world, "field_n");
    test_int(wait_texture(world, normal)->state, TextureReady);

    ecs_log_set_level(-4);
    ecs_remove(world, field, TextureGen);
    const TextureStatus *status = wait_texture(world, normal);
    test_int(status->state, TextureFailed);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "field_n") != NULL);
    test_assert(strstr(status->error, "'field'") != NULL);
    test_assert(strstr(status->error, "TextureGen") != NULL);

    ecs_fini(world);
}

void TextureGen_source_deleted(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const char *code =
        "using box.texture\n"
        "field { TextureGen: {width: 16, height: 16, seed: 1,\n"
        "  layers: [{kind: GenNoise, frequency: 4, octaves: 2, amount: 1}]} }\n"
        "field_a { TextureGen: {source: field, output: GenAlbedo} }\n";
    test_assert(ecs_script_run(world, "gen", code, NULL) == 0);
    ecs_entity_t field = ecs_lookup(world, "field");
    ecs_entity_t albedo = ecs_lookup(world, "field_a");
    test_int(wait_texture(world, albedo)->state, TextureReady);

    ecs_log_set_level(-4);
    ecs_delete(world, field);
    test_assert(!ecs_is_alive(world, field));
    const TextureStatus *status = wait_texture(world, albedo);
    test_int(status->state, TextureFailed);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "field_a") != NULL);
    test_assert(strstr(status->error, "'field'") != NULL);

    ecs_fini(world);
}

void TextureGen_with_path_error(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const char *code =
        "using box.texture\n"
        "both {\n"
        "  TexturePath: {path: \"test/assets/textures/gray.png\"}\n"
        "  TextureGen: {width: 8, height: 8}\n"
        "}\n";
    ecs_log_set_level(-4);
    test_assert(ecs_script_run(world, "both", code, NULL) == 0);
    ecs_entity_t both = ecs_lookup(world, "both");
    test_assert(both != 0);
    const TextureStatus *status = wait_texture(world, both);
    test_int(status->state, TextureFailed);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "both") != NULL);
    test_assert(strstr(status->error, "TexturePath") != NULL);
    test_assert(strstr(status->error, "TextureGen") != NULL);
    test_assert(ecs_get(world, both, Texture) == NULL);

    ecs_remove(world, both, TexturePath);
    status = wait_texture(world, both);
    test_int(status->state, TextureReady);
    test_int(ecs_get(world, both, Texture)->width, 8);

    ecs_fini(world);
}

void TextureGen_remove(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TextureGen, { .width = 8 });
    test_int(wait_texture(world, tex)->state, TextureReady);
    test_int(ecs_get(world, tex, Texture)->height, 8);
    ecs_remove(world, tex, TextureGen);
    test_assert(ecs_get(world, tex, Texture) == NULL);
    test_assert(ecs_get(world, tex, TextureStatus) == NULL);
    box_textures_t *t = box_textures_get(world);
    test_assert(ecs_map_get(&t->textures, tex) == NULL);

    ecs_fini(world);
}

void TextureGen_default_size(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TextureGen, { .height = 0 });
    test_int(wait_texture(world, tex)->state, TextureReady);
    test_int(ecs_get(world, tex, Texture)->width, 512);
    test_int(ecs_get(world, tex, Texture)->height, 512);

    ecs_fini(world);
}

void TextureGen_capped_by_quality(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {
        .width = 64, .height = 64, .texture_quality = TextureQualityLow });
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TextureGen, { .width = 1024, .height = 256 });
    test_int(wait_texture(world, tex)->state, TextureReady);
    test_int(ecs_get(world, tex, Texture)->width, 512);
    test_int(ecs_get(world, tex, Texture)->height, 128);

    ecs_fini(world);
}

void TextureGen_hash_noise(void) {
    // materials/textures
    test_hash(GEN_NOISE, "noise", 0xaee82167U);
}

void TextureGen_hash_blocks(void) {
    // materials/textures
    test_hash(GEN_BLOCKS, "blocks", 0xd1e196bfU);
}

void TextureGen_hash_cells(void) {
    // materials/textures
    test_hash(GEN_CELLS, "cells", 0x68a56d43U);
}

void TextureGen_source_deleted_with_derived(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const char *code =
        "using box.texture\n"
        "stone {\n"
        "  field { TextureGen: {width: 16, height: 16, seed: 1,\n"
        "    layers: [{kind: GenNoise, frequency: 4, octaves: 2, amount: 1}]} }\n"
        "  field_n { TextureGen: {source: field, output: GenNormal} }\n"
        "}\n";
    test_assert(ecs_script_run(world, "gen", code, NULL) == 0);
    ecs_entity_t stone = ecs_lookup(world, "stone");
    ecs_entity_t normal = ecs_lookup(world, "stone.field_n");
    test_int(wait_texture(world, normal)->state, TextureReady);

    /* In another table than its source, so its OnRemove runs in another batch */
    ecs_add_id(world, normal, ecs_new(world));
    ecs_delete(world, stone);
    test_assert(!ecs_is_alive(world, normal));
    ecs_progress(world, 0);

    ecs_fini(world);
}

void TextureGen_tone_checker(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const char *code =
        "using box.texture\n"
        "floor_height { TextureGen: {width: 64, height: 48, seed: 3, layers: [\n"
        "  {kind: GenBlocks, courses: 4, per_course: 4, gap: 0.02, jitter: 0,\n"
        "   tones: 2, tone_order: GenToneChecker, amount: 1, id_amount: 0.2}\n"
        "]} }\n"
        "floor { TextureGen: {source: floor_height, output: GenAlbedo,\n"
        "  palette: [{200, 0, 0}, {0, 0, 200}]} }\n";
    int32_t size = 0, k, i;
    uint8_t *px = script_bytes(world, code, "floor", &size);
    test_int(size, 64 * 48 * 4);
    for (k = 0; k < 4; k ++) {
        int32_t y = k * 12 + 6;
        for (i = 0; i < 4; i ++) {
            int32_t x = i * 16 + 8;
            test_pixel(px, 64, x, y, tone_colors[(k + i) % 2]);
            test_assert(!same_color(px, 64, y * 64 + x, y * 64 + (x + 16) % 64));
            test_assert(!same_color(px, 64, y * 64 + x, ((y + 12) % 48) * 64 + x));
        }
    }

    ecs_os_free(px);
    ecs_fini(world);
}

void TextureGen_tone_checker_staggered(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const char *code =
        "using box.texture\n"
        "floor_height { TextureGen: {width: 64, height: 64, seed: 7, layers: [\n"
        "  {kind: GenBlocks, courses: 8, per_course: 4, gap: 0.02, jitter: 0.3, stagger: 0.5,\n"
        "   tones: 2, tone_order: GenToneChecker, amount: 1, id_amount: 0.2}\n"
        "]} }\n"
        "floor { TextureGen: {source: floor_height, output: GenAlbedo,\n"
        "  palette: [{200, 0, 0}, {0, 0, 200}]} }\n";
    int32_t size = 0, k, i, x;
    uint8_t *px = script_bytes(world, code, "floor", &size);
    test_int(size, 64 * 64 * 4);
    for (k = 0; k < 8; k ++) {
        int32_t y = k * 8 + 4, changes = 0;
        double shift = (k % 2) ? 0.5 : 0.0;

        /* Cuts move by at most 0.3 blocks, so the middle of the unjittered block is inside
         * the block of its original index */
        for (i = 0; i < 4; i ++) {
            double u = (i + 0.5 + shift) / 4.0;
            x = (int32_t)floor((u - floor(u)) * 64.0);
            test_pixel(px, 64, x, y, tone_colors[(k + i) % 2]);
        }

        for (x = 0; x < 64; x ++) {
            changes += !same_color(px, 64, y * 64 + x, y * 64 + (x + 1) % 64);
        }

        test_int(changes, 4);
    }

    ecs_os_free(px);
    ecs_fini(world);
}

void TextureGen_tone_courses(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const char *code =
        "using box.texture\n"
        "wall_height { TextureGen: {width: 64, height: 48, seed: 3, layers: [\n"
        "  {kind: GenBlocks, courses: 6, per_course: 4, gap: 0.02,\n"
        "   tones: 3, tone_order: GenToneCourses, amount: 1}\n"
        "]} }\n"
        "wall { TextureGen: {source: wall_height, output: GenAlbedo,\n"
        "  palette: [{200, 0, 0}, {0, 0, 200}, {0, 180, 0}]} }\n";
    int32_t size = 0, k, i;
    uint8_t *px = script_bytes(world, code, "wall", &size);
    for (k = 0; k < 6; k ++) {
        for (i = 0; i < 4; i ++) {
            test_pixel(px, 64, i * 16 + 8, k * 8 + 4, tone_colors[k % 3]);
        }
    }

    ecs_os_free(px);
    ecs_fini(world);
}

void TextureGen_tone_columns(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const char *code =
        "using box.texture\n"
        "planks_height { TextureGen: {width: 96, height: 48, seed: 3, layers: [\n"
        "  {kind: GenBlocks, courses: 4, per_course: 6, gap: 0.02,\n"
        "   tones: 3, tone_order: GenToneColumns, amount: 1}\n"
        "]} }\n"
        "planks { TextureGen: {source: planks_height, output: GenAlbedo,\n"
        "  palette: [{200, 0, 0}, {0, 0, 200}, {0, 180, 0}]} }\n";
    int32_t size = 0, k, i;
    uint8_t *px = script_bytes(world, code, "planks", &size);
    test_int(size, 96 * 48 * 4);
    for (k = 0; k < 4; k ++) {
        for (i = 0; i < 6; i ++) {
            test_pixel(px, 96, i * 16 + 8, k * 12 + 6, tone_colors[i % 3]);
        }
    }

    ecs_os_free(px);
    ecs_fini(world);
}

void TextureGen_tone_random(void) {
    // materials/textures
    TextureGenLayer layer = {
        .kind = GenBlocks, .courses = 16, .per_course = 16, .gap = 0.01f, .amount = 1,
        .id_amount = 0.2f, .tones = 4 };
    static float value[64 * 64], id[64 * 64];
    static int32_t tone[64 * 64], again[64 * 64], other[64 * 64];
    int32_t used[4] = { 0 }, matches = 0, k, i;

    ecs_world_t *world = world_new();
    box_texture_gen_pattern(&layer, 9, 64, 64, value, id, tone);
    box_texture_gen_pattern(&layer, 9, 64, 64, value, id, again);
    test_assert(!memcmp(tone, again, sizeof(tone)));
    box_texture_gen_pattern(&layer, 10, 64, 64, value, id, other);
    test_assert(memcmp(tone, other, sizeof(tone)) != 0);

    box_texture_gen_pattern(&layer, 9, 64, 64, value, id, tone);
    for (k = 0; k < 16; k ++) {
        for (i = 0; i < 16; i ++) {
            int32_t p = (k * 4 + 2) * 64 + i * 4 + 2;
            int32_t bucket = (int32_t)floor(id[p] * 4);
            test_assert(tone[p] >= 0 && tone[p] < 4);
            used[tone[p]] ++;
            matches += tone[p] == bucket;
        }
    }

    for (i = 0; i < 4; i ++) {
        test_assert(used[i] > 0);
    }

    /* A tone that followed the id would match its bucket for every block, an independent one
     * for about a quarter of them */
    test_assert(matches < 128);

    ecs_fini(world);
}

void TextureGen_tone_tiles(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const char *code =
        "using box.texture\n"
        "floor_height { TextureGen: {width: 64, height: 48, seed: 3, layers: [\n"
        "  {kind: GenBlocks, courses: 4, per_course: 4, gap: 0.02,\n"
        "   tones: 2, tone_order: GenToneChecker, amount: 1}\n"
        "]} }\n"
        "floor { TextureGen: {source: floor_height, output: GenAlbedo,\n"
        "  palette: [{200, 0, 0}, {0, 0, 200}]} }\n";
    int32_t size = 0, x, y;
    uint8_t *px = script_bytes(world, code, "floor", &size);

    /* Across the wrap the checker alternates as it does between neighbors inside */
    for (x = 0; x < 64; x ++) {
        test_assert(!same_color(px, 64, x, 47 * 64 + x));
    }

    for (y = 0; y < 48; y ++) {
        test_assert(!same_color(px, 64, y * 64, y * 64 + 63));
    }

    ecs_os_free(px);
    ecs_fini(world);
}

void TextureGen_tone_amount(void) {
    // materials/textures
    TextureGenLayer layer = {
        .kind = GenBlocks, .courses = 4, .per_course = 4, .gap = 0.04f, .amount = 1,
        .tones = 2, .tone_order = GenToneChecker, .tone_amount = 0.5f };
    int32_t size = 0, k, i;
    uint8_t *px = layer_bytes(&layer, 3, &size);

    /* Faces sum to 1 for tone 0 and 1.5 for tone 1, the joints of tone 0 to 0 */
    for (k = 0; k < 4; k ++) {
        for (i = 0; i < 4; i ++) {
            int32_t v = px[(k * 12 + 6) * 64 + i * 16 + 8];
            if ((k + i) % 2) {
                test_int(v, 255);
            } else {
                test_assert(abs(v - 170) <= 1);
            }
        }
    }

    ecs_os_free(px);

    TextureGenLayer plain = blocks_layer, toned = blocks_layer;
    toned.tones = 2;
    toned.tone_order = GenToneChecker;
    int32_t a_size = 0, b_size = 0;
    uint8_t *a = layer_bytes(&plain, 5, &a_size);
    uint8_t *b = layer_bytes(&toned, 5, &b_size);
    test_int(a_size, b_size);
    test_assert(!memcmp(a, b, a_size));
    ecs_os_free(a);
    ecs_os_free(b);
}

void TextureGen_palette_ramp_multiply(void) {
    // materials/textures
    static const float height[3] = { 0.0f, 1.0f, 0.0f };
    static const int32_t tone[3] = { 0, 1, 3 };
    static const TextureGenKey keys[2] = {
        { 0.0f, { 100, 200, 50 } }, { 1.0f, { 255, 255, 255 } } };
    static const rgb palette[2] = { { 200, 100, 255 }, { 10, 20, 30 } };
    uint8_t rgba[3 * 4];

    box_texture_gen_albedo(height, 3, keys, 2, tone, palette, 2, rgba);
    test_int(rgba[0], 78);
    test_int(rgba[1], 78);
    test_int(rgba[2], 50);
    test_int(rgba[3], 255);
    test_int(rgba[4], 10);
    test_int(rgba[5], 20);
    test_int(rgba[6], 30);
    test_int(rgba[8], 4);
    test_int(rgba[9], 16);
    test_int(rgba[10], 6);
    test_int(rgba[11], 255);

    box_texture_gen_albedo(height, 3, NULL, 0, tone, palette, 2, rgba);
    test_int(rgba[0], 200);
    test_int(rgba[1], 100);
    test_int(rgba[2], 255);
    test_int(rgba[4], 10);
    test_int(rgba[5], 20);
    test_int(rgba[6], 30);
    test_int(rgba[8], 10);
    test_int(rgba[9], 20);
    test_int(rgba[10], 30);
}

void TextureGen_palette_cells(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const char *code =
        "using box.texture\n"
        "cobble_height { TextureGen: {width: 64, height: 64, seed: 3, layers: [\n"
        "  {kind: GenCells, count: 36, distance: GenEdge, tones: 3, amount: 1, id_amount: 0.2}\n"
        "]} }\n"
        "cobble { TextureGen: {source: cobble_height, output: GenAlbedo,\n"
        "  palette: [{200, 0, 0}, {0, 0, 200}, {0, 180, 0}]} }\n";
    int32_t size = 0, used[3] = { 0 }, i, c;
    uint8_t *px = script_bytes(world, code, "cobble", &size);
    test_int(size, 64 * 64 * 4);
    for (i = 0; i < 64 * 64; i ++) {
        const uint8_t *p = &px[i * 4];
        bool found = false;
        for (c = 0; c < 3; c ++) {
            if (p[0] == tone_colors[c].r && p[1] == tone_colors[c].g &&
                p[2] == tone_colors[c].b)
            {
                used[c] ++;
                found = true;
            }
        }

        test_assert(found);
    }

    for (c = 0; c < 3; c ++) {
        test_assert(used[c] > 0);
    }

    ecs_os_free(px);
    ecs_fini(world);
}

void TextureGen_palette_layer_invalid(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const char *code =
        "using box.texture\n"
        "field { TextureGen: {width: 16, height: 16, seed: 1, layers: [\n"
        "  {kind: GenBlocks, courses: 2, per_course: 2, tones: 2, amount: 1}\n"
        "  {kind: GenNoise, frequency: 4, amount: 0.2}\n"
        "]} }\n"
        "field_far { TextureGen: {source: field, output: GenAlbedo, palette_layer: 5,\n"
        "  palette: [{200, 0, 0}, {0, 0, 200}]} }\n"
        "field_noise { TextureGen: {source: field, output: GenAlbedo, palette_layer: 1,\n"
        "  palette: [{200, 0, 0}, {0, 0, 200}]} }\n";
    ecs_log_set_level(-4);
    test_assert(ecs_script_run(world, "gen", code, NULL) == 0);
    ecs_entity_t far = ecs_lookup(world, "field_far");
    ecs_entity_t noise = ecs_lookup(world, "field_noise");
    test_assert(far && noise);

    const TextureStatus *status = wait_texture(world, far);
    test_int(status->state, TextureFailed);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "field_far") != NULL);
    test_assert(strstr(status->error, "5") != NULL);

    status = wait_texture(world, noise);
    test_int(status->state, TextureFailed);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "field_noise") != NULL);
    test_assert(strstr(status->error, "1") != NULL);
    test_assert(strstr(status->error, "GenNoise") != NULL);

    ecs_fini(world);
}

void TextureGen_palette_ignored_for_normal(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const char *code =
        "using box.texture\n"
        "field { TextureGen: {width: 32, height: 32, seed: 1, layers: [\n"
        "  {kind: GenBlocks, courses: 4, per_course: 4, gap: 0.04, tones: 2,\n"
        "   tone_order: GenToneChecker, amount: 1, id_amount: 0.2}\n"
        "]} }\n"
        "field_n { TextureGen: {source: field, output: GenNormal, strength: 2} }\n"
        "field_np { TextureGen: {source: field, output: GenNormal, strength: 2,\n"
        "  palette: [{200, 0, 0}, {0, 0, 200}], palette_layer: 7} }\n"
        "field_h { TextureGen: {source: field} }\n"
        "field_hp { TextureGen: {source: field, palette: [{200, 0, 0}]} }\n";
    int32_t a_size = 0, b_size = 0;
    uint8_t *a = script_bytes(world, code, "field_n", &a_size);
    uint8_t *b = script_bytes(world, NULL, "field_np", &b_size);
    test_int(a_size, b_size);
    test_assert(!memcmp(a, b, a_size));
    ecs_os_free(a);
    ecs_os_free(b);

    a = script_bytes(world, NULL, "field_h", &a_size);
    b = script_bytes(world, NULL, "field_hp", &b_size);
    test_int(a_size, b_size);
    test_assert(!memcmp(a, b, a_size));
    ecs_os_free(a);
    ecs_os_free(b);

    ecs_fini(world);
}

void TextureGen_hash_blocks_tones(void) {
    // materials/textures
    test_hash(GEN_BLOCKS_TONES, "blocks_tones", 0xc39e2c64U);
}
