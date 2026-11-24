/**
 * @file WorldOriginShading.c
 * @brief Tests that the world space patterns of the shaders sample the absolute position: two
 * worlds render the same scene for the same frames, one of them shifting its world origin, and
 * their last images match within a small tolerance; and a world mapped texture at a world origin
 * of a million meters renders as the same scene at origin 0 shifted by whole texture repeats.
 *
 * The shifts are whole steps of the default WorldOrigin.step (256) that are no multiple of the
 * period of the pattern of the case, so a pattern that sampled the local position would jump.
 * The textured shapes are emissive on a black, rough metal so the light does not take part.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Noise texture the textured cases map, from the repository root */
#define WOS_NOISE_PNG "test/assets/textures/cloud_noise_256.png"

/** Width and height in pixels of the images */
#define WOS_SIZE (96)

/** Seconds of a frame */
#define WOS_DELTA (1.0f / 60.0f)

/** Frames each world renders, and the frame before which the second one shifts */
#define WOS_FRAMES (6)
#define WOS_SHIFT_AT (3)

/** Channel difference above which a pixel counts as far off, and the pixels that may be, per
 * ten thousand: an edge pixel can rasterize differently after a shift */
#define WOS_MAX_DIFF (64)
#define WOS_FAR_PER_10K (20)

/** Channel difference above which a pixel counts as different, and the pixels that may, per
 * thousand */
#define WOS_PIXEL_DIFF (12)
#define WOS_DIFF_PERMILLE (15)

/** Largest channel difference of two images that differ only by rounding */
#define WOS_CLOSE_DIFF (4)

/** Builds the content of a case in a world and returns the texture it waits for, 0 for none */
typedef ecs_entity_t (*wos_build_t)(
    ecs_world_t *world);

/** Create a headless world that renders on the main thread with the features of the cases.
 *
 * @return The world
 */
static ecs_world_t* wos_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, Scene, {.shader_features.Extended.triplanar = true,
        .shader_features.Extended.wind = true, .shader_features.Extended.texture_layers = true});
    ecs_singleton_set(world, AmbientLight, {.sky = {255, 255, 255}, .ground = {255, 255, 255},
        .intensity = 1});

    return world;
}

/** Add a camera and a lit view with fog volumes that clears to black.
 *
 * @param world The world
 * @param position Position of the camera
 * @param pitch Rotation of the camera about X
 * @param ortho Height of an orthographic view, 0 for a perspective one of 60 degrees
 * @return The camera
 */
static ecs_entity_t wos_view(
    ecs_world_t *world,
    const float *position,
    float pitch,
    float ortho)
{
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {position[0], position[1], position[2]});
    ecs_set(world, camera, Rotation, {pitch, 0, 0});
    if (ortho > 0) {
        ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = ortho, .near_ = 0.1f,
            .far_ = 100});
    } else {
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 100});
    }

    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true, .fog_volumes = true}});

    return camera;
}

/** Create the noise texture.
 *
 * @param world The world
 * @return The texture
 */
static ecs_entity_t wos_texture(
    ecs_world_t *world)
{
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = WOS_NOISE_PNG});

    return tex;
}

/** Make a shape black, rough metal that emits a texture with a mapping.
 *
 * @param world The world
 * @param e The shape
 * @param tex The texture
 * @param mode The mapping
 * @param size TextureMapping.size
 * @param blend TextureMapping.blend
 */
static void wos_emissive(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t tex,
    TextureMappingMode mode,
    float size,
    float blend)
{
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, e, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, e, PbrTextures, {.emissive = tex});
    ecs_set(world, e, TextureMapping, {.mode = mode, .size = size, .blend = blend});
    ecs_set(world, e, TextureWrap, {WrapRepeat});
}

/** Render the next frame into an image file of the temporary folder and load it.
 *
 * @param world The world
 * @param file Image file
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* wos_frame(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = WOS_SIZE, .height = WOS_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, WOS_DELTA);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, WOS_SIZE);
    test_int(h, WOS_SIZE);
    ecs_os_free(path);

    return result;
}

/** Wait until a texture loaded, with frames of no time so the scene does not advance.
 *
 * @param world The world
 * @param tex The texture, 0 for none
 */
static void wos_ready(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    int32_t frames = 0;
    if (!tex) {
        return;
    }

    ecs_progress(world, 0);
    while (ecs_get(world, tex, TextureStatus) &&
        ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 200)
    {
        ecs_progress(world, 0);
        frames ++;
    }

    test_int(ecs_get(world, tex, TextureStatus)->state, TextureReady);
}

/** Check that two images match within the tolerance of a shift.
 *
 * @param a The first image
 * @param b The second image
 */
static void wos_expect_same(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t worst = 0, differ = 0, far = 0, i, c;
    for (i = 0; i < WOS_SIZE * WOS_SIZE; i ++) {
        int32_t d = 0;
        for (c = 0; c < 3; c ++) {
            int32_t delta = abs((int)a[i * 4 + c] - (int)b[i * 4 + c]);
            d = delta > d ? delta : d;
        }

        worst = d > worst ? d : worst;
        differ += d > WOS_PIXEL_DIFF;
        far += d > WOS_MAX_DIFF;
    }

    if (far * 10000 > WOS_FAR_PER_10K * WOS_SIZE * WOS_SIZE ||
        differ * 1000 > WOS_DIFF_PERMILLE * WOS_SIZE * WOS_SIZE)
    {
        printf("worst channel difference %d, %d pixels differ, %d far\n", worst, differ, far);
    }

    test_assert(far * 10000 <= WOS_FAR_PER_10K * WOS_SIZE * WOS_SIZE);
    test_assert(differ * 1000 <= WOS_DIFF_PERMILLE * WOS_SIZE * WOS_SIZE);
}

/** Check that two images match to within WOS_CLOSE_DIFF in every channel.
 *
 * @param a The first image
 * @param b The second image
 */
static void wos_expect_close(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t worst = 0, i;
    for (i = 0; i < WOS_SIZE * WOS_SIZE * 4; i ++) {
        int32_t d = abs((int)a[i] - (int)b[i]);
        worst = d > worst ? d : worst;
    }

    if (worst > WOS_CLOSE_DIFF) {
        printf("worst channel difference %d\n", worst);
    }

    test_assert(worst <= WOS_CLOSE_DIFF);
}

/** Check that an image shows a pattern: enough pixels are lit and they are not all alike.
 *
 * @param px The image
 */
static void wos_expect_pattern(
    const unsigned char *px)
{
    int32_t lit = 0, lo = 255, hi = 0, i;
    for (i = 0; i < WOS_SIZE * WOS_SIZE; i ++) {
        int32_t v = px[i * 4] > px[i * 4 + 1] ? px[i * 4] : px[i * 4 + 1];
        v = px[i * 4 + 2] > v ? px[i * 4 + 2] : v;
        if (v > 8) {
            lit ++;
            lo = v < lo ? v : lo;
            hi = v > hi ? v : hi;
        }
    }

    test_assert(lit > WOS_SIZE * WOS_SIZE / 8);
    test_assert(hi - lo > 32);
}

/** Render a case in two worlds for WOS_FRAMES frames, the second shifting its world origin
 * before frame WOS_SHIFT_AT, and check that the last images match.
 *
 * @param build Builds the case
 * @param shift The offset of the shift
 */
static void wos_shift_same(
    wos_build_t build,
    const double *shift)
{
    ecs_world_t *a = wos_world();
    ecs_world_t *b = wos_world();
    wos_ready(a, build(a));
    wos_ready(b, build(b));
    unsigned char *pa = NULL, *pb = NULL;
    int32_t i;
    for (i = 0; i < WOS_FRAMES; i ++) {
        if (i == WOS_SHIFT_AT) {
            box_world_origin_shift(b, 0, shift);
        }

        stbi_image_free(pa);
        stbi_image_free(pb);
        pa = wos_frame(a, "wos_a.png");
        pb = wos_frame(b, "wos_b.png");
    }

    test_int(ecs_singleton_get(b, WorldOriginStatus)->shifts, 1);
    test_assert(ecs_singleton_get(b, WorldOrigin)->x == shift[0]);
    wos_expect_pattern(pa);
    wos_expect_same(pa, pb);
    stbi_image_free(pa);
    stbi_image_free(pb);
    ecs_fini(a);
    ecs_fini(b);
}

/** A MappingWorld plane of 3 units per repeat seen from above in an orthographic view.
 *
 * @param world The world
 * @return The texture
 */
static ecs_entity_t wos_plane(
    ecs_world_t *world)
{
    ecs_entity_t tex = wos_texture(world);
    wos_view(world, (float[]){0.3f, 10, 0.2f}, -(float)GLM_PI_2, 8);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Box, {20, 0.2f, 20});
    wos_emissive(world, e, tex, MappingWorld, 3, 0);

    return tex;
}

/** A sphere with a triplanar MappingWorld seen from the side and above.
 *
 * @param world The world
 * @return The texture
 */
static ecs_entity_t wos_triplanar(
    ecs_world_t *world)
{
    ecs_entity_t tex = wos_texture(world);
    wos_view(world, (float[]){0, 2, 4}, -0.45f, 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0.2f, 0, -0.1f});
    ecs_set(world, e, Sphere, {.segments = 32, .smooth = true, .radius = 1.5f});
    wos_emissive(world, e, tex, MappingWorld, 1.7f, 0.5f);

    return tex;
}

/** A box with a MappingFrame and no texture frame, which projects its world position, seen
 * from the side and above.
 *
 * @param world The world
 * @return The texture
 */
static ecs_entity_t wos_frame_unframed(
    ecs_world_t *world)
{
    ecs_entity_t tex = wos_texture(world);
    wos_view(world, (float[]){0, 3, 5}, -0.5f, 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0.1f, 0, 0});
    ecs_set(world, e, Rotation, {0, 0.4f, 0});
    ecs_set(world, e, Box, {2, 2, 2});
    wos_emissive(world, e, tex, MappingFrame, 2.3f, 0);

    return tex;
}

/** A white floor with a TextureLayer whose albedo is the noise in world space, 3 units per
 * repeat, seen from above, lit by the white ambient alone.
 *
 * @param world The world
 * @return The texture
 */
static ecs_entity_t wos_layer(
    ecs_world_t *world)
{
    ecs_entity_t tex = wos_texture(world);
    wos_view(world, (float[]){0.3f, 10, 0.2f}, -(float)GLM_PI_2, 8);
    ecs_entity_t layer = ecs_new(world);
    ecs_set(world, layer, TextureLayer, {.amount = 1, .direction = {0, 1, 0}, .contrast = 8,
        .albedo = LayerMultiply, .roughness = LayerKeep, .metallic = LayerKeep,
        .occlusion = LayerKeep});
    ecs_set(world, layer, PbrTextures, {.albedo = tex});
    ecs_set(world, layer, TextureMapping, {.mode = MappingWorld, .size = 3});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Box, {20, 0.2f, 20});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_set(world, e, PbrMaterial, {.roughness = 1});
    ecs_set(world, e, TextureLayers, {.layers = {layer}});

    return tex;
}

/** Three wide poles of different brightness with a WindSway in a gusty wind whose gust field
 * repeats every 160 units, seen from the side.
 *
 * @param world The world
 * @return 0, no texture
 */
static ecs_entity_t wos_wind(
    ecs_world_t *world)
{
    int32_t i;
    wos_view(world, (float[]){0, 1.5f, 5}, 0, 0);
    ecs_singleton_set(world, Wind, {.direction = {0.8f, 0.6f}, .speed = 8, .gust = 0.8f,
        .gust_scale = 5});
    for (i = 0; i < 3; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {-2 + 2 * (float)i, 1.5f, -(float)i});
        ecs_set(world, e, Box, {1.5f, 3, 0.3f});
        ecs_set(world, e, Rgb, {0, 0, 0});
        ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 1});
        ecs_set(world, e, Emissive, {.strength = 1,
            .color = {(uint8_t)(255 - 80 * i), (uint8_t)(40 * i), 40}});
        ecs_set(world, e, WindSway, {.bend = 0.3f, .height = 3, .base = 0, .flutter = 0.1f,
            .flutter_frequency = 3, .flutter_reach = 0.5f});
    }

    return 0;
}

/** A noisy FogVolume of 1.7 units per noise cell in front of an emissive wall, seen in a
 * perspective view.
 *
 * @param world The world
 * @return 0, no texture
 */
static ecs_entity_t wos_fog(
    ecs_world_t *world)
{
    wos_view(world, (float[]){0, 0, 6}, 0, 0);
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -4});
    ecs_set(world, wall, Box, {30, 30, 0.2f});
    ecs_set(world, wall, Rgb, {0, 0, 0});
    ecs_set(world, wall, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, wall, Emissive, {.strength = 1, .color = {200, 200, 200}});

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_add(world, e, FogVolume);
    FogVolume *f = ecs_get_mut(world, e, FogVolume);
    f->extent[0] = 10;
    f->extent[1] = 10;
    f->extent[2] = 2;
    f->density = 0.5f;
    f->absorption = 1;
    f->edge_fade = 0;
    f->noise_strength = 1;
    f->noise_scale = 1.7f;
    ecs_modified(world, e, FogVolume);

    return 0;
}

void WorldOriginShading_mapping_world_shift(void) {
    // render/world_origin_shading
    wos_shift_same(wos_plane, (double[3]){ 2816, 0, -256 });
}

void WorldOriginShading_triplanar_shift(void) {
    // render/world_origin_shading
    wos_shift_same(wos_triplanar, (double[3]){ 256, 512, -768 });
}

void WorldOriginShading_mapping_frame_shift(void) {
    // render/world_origin_shading
    wos_shift_same(wos_frame_unframed, (double[3]){ -768, 256, 256 });
}

void WorldOriginShading_texture_layer_shift(void) {
    // render/world_origin_shading
    wos_shift_same(wos_layer, (double[3]){ 2816, 0, 512 });
}

void WorldOriginShading_wind_sway_shift(void) {
    // render/world_origin_shading
    wos_shift_same(wos_wind, (double[3]){ 256, 256, -512 });
}

void WorldOriginShading_fog_volume_shift(void) {
    // render/world_origin_shading
    wos_shift_same(wos_fog, (double[3]){ 256, 0, 768 });
}

void WorldOriginShading_fog_volume_shift_next_frame(void) {
    // render/world_origin_shading
    /* The fog volumes blend every frame with the cells of the previous one, found through the
     * previous view projection and slicing; after a shift both are taken into the new frame
     * (temporal.c, gpu_fog_volume.c), so every frame from the shift on matches a world that never
     * shifted to within rounding. With a stale history the frame after the shift drops it and
     * differs by up to 10 in a channel. */
    ecs_world_t *a = wos_world();
    ecs_world_t *b = wos_world();
    wos_fog(a);
    wos_fog(b);
    unsigned char *pa = NULL, *pb = NULL;
    int32_t i;
    for (i = 0; i < WOS_SHIFT_AT; i ++) {
        stbi_image_free(pa);
        stbi_image_free(pb);
        pa = wos_frame(a, "wos_a.png");
        pb = wos_frame(b, "wos_b.png");
    }

    box_world_origin_shift(b, 0, (double[3]){ 256, 0, 768 });
    for (i = WOS_SHIFT_AT; i < WOS_FRAMES; i ++) {
        stbi_image_free(pa);
        stbi_image_free(pb);
        pa = wos_frame(a, "wos_a.png");
        pb = wos_frame(b, "wos_b.png");
        wos_expect_pattern(pa);
        wos_expect_close(pa, pb);
    }

    stbi_image_free(pa);
    stbi_image_free(pb);
    ecs_fini(a);
    ecs_fini(b);
}

/** Render a magnified MappingWorld plane, its content and camera at x and z of offset, with a
 * world origin that is not shifted to.
 *
 * @param origin X and Z of the WorldOrigin, 0 for none
 * @param offset Local X and Z of the content
 * @param file Image file
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* wos_far(
    double origin,
    float offset,
    const char *file)
{
    ecs_world_t *world = wos_world();
    unsigned char *result = NULL;
    int32_t i;
    if (origin != 0) {
        ecs_singleton_set(world, WorldOrigin, {.x = origin, .z = origin, .threshold = 2048,
            .step = 256, .enabled = false});
    }

    ecs_entity_t tex = wos_texture(world);
    wos_view(world, (float[]){offset + 0.3f, 10, offset + 0.2f}, -(float)GLM_PI_2, 1.5f);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {offset, 0, offset});
    ecs_set(world, e, Box, {20, 0.2f, 20});
    wos_emissive(world, e, tex, MappingWorld, 2, 0);
    wos_ready(world, tex);
    for (i = 0; i < 3; i ++) {
        stbi_image_free(result);
        result = wos_frame(world, file);
    }

    ecs_fini(world);

    return result;
}

void WorldOriginShading_far_origin_not_blocky(void) {
    // render/world_origin_shading
    /* At an absolute position of a million and one units a texture of 2 units per repeat has the
     * phase of local position 1 at origin 0: whole repeats apart. A projection of the absolute
     * position in f32 would step by 1 / 16 unit, 4 pixels of the 1.5 unit high view. */
    unsigned char *far = wos_far(1e6 + 1, 0, "wos_far.png");
    unsigned char *near = wos_far(0, 1, "wos_near.png");
    wos_expect_pattern(far);
    wos_expect_same(far, near);
    stbi_image_free(far);
    stbi_image_free(near);
}
