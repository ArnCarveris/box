/**
 * @file Impostor.c
 * @brief Tests for the impostor bakes (Impostor): the octahedral views of a prefab land in the
 * cells of the albedo, normal and depth atlases, alpha tested shapes leave their coverage in
 * alpha, and a change of a material bakes again.
 *
 * The prefabs come from test/assets/impostor: ColorCube, a closed box with a color per face,
 * and DiscCard, an alpha tested quad that shows a disc. The atlases are written to PNG files in
 * the temporary folder and compared as stored. The card cases draw Panel and Tower of draw.flecs
 * beyond Impostor.distance and compare them with the full render of their hierarchy.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Views per axis of the bakes of the tests */
#define IMPOSTOR_FRAMES (5)

/** Pixels of a view of the bakes of the tests */
#define IMPOSTOR_RES (64)

/** Width and height of the atlases of the tests */
#define IMPOSTOR_ATLAS (IMPOSTOR_FRAMES * IMPOSTOR_RES)

/** Frames a test runs at most for a bake */
#define IMPOSTOR_FRAMES_MAX (400)

/** Largest difference of a channel from the expected color */
#define IMPOSTOR_TOLERANCE (12)

/** Create a headless world that renders with the GPU on the main thread, with alpha cutout,
 * and load a script of test/assets/impostor.
 *
 * @param script File name of the script
 * @return The world
 */
static ecs_world_t* impostor_world(
    const char *script)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Scene, {.alpha_cutout = true});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    box_render_use_gpu(world);

    char *rel = flecs_asprintf("test/assets/impostor/%s", script);
    char *file = box_app_locate(rel);
    test_assert(file != NULL);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "impostor_script" }),
        .filename = file
    }) != 0);
    ecs_os_free(file);
    ecs_os_free(rel);

    return world;
}

/** Give a prefab of the script an Impostor of the grid of the tests.
 *
 * @param world The world
 * @param name Name of the prefab
 * @param hemisphere Whether the views are from above only
 * @return The prefab
 */
static ecs_entity_t impostor_prefab(
    ecs_world_t *world,
    const char *name,
    bool hemisphere)
{
    ecs_entity_t result = ecs_lookup(world, name);
    test_assert(result != 0);
    test_assert(ecs_has_id(world, result, EcsPrefab));
    ecs_set(world, result, Impostor, {
        .frames = IMPOSTOR_FRAMES,
        .resolution = IMPOSTOR_RES,
        .hemisphere = hemisphere,
        .distance = 100,
        .fade = 10
    });

    return result;
}

/** Run frames until an impostor completed a bake of at least a revision, then a few more so the
 * renderer copied its last views, and wait for the renderer.
 *
 * @param world The world
 * @param e The impostor
 * @param revision The revision
 * @return Its status
 */
static const ImpostorStatus* impostor_wait(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t revision)
{
    const ImpostorStatus *s = NULL;
    int32_t i;
    for (i = 0; i < IMPOSTOR_FRAMES_MAX; i ++) {
        ecs_progress(world, 0);
        s = ecs_get(world, e, ImpostorStatus);
        if (s && s->ready && s->revision >= revision) {
            break;
        }
    }

    test_assert(s != NULL);
    test_bool(s->ready, true);
    test_assert(s->revision >= revision);
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);

    return ecs_get(world, e, ImpostorStatus);
}

/** Write an atlas of an impostor to a file and load its pixels.
 *
 * @param world The world
 * @param e The impostor
 * @param channel The atlas
 * @param name File name in the temporary folder
 * @return RGBA pixels of IMPOSTOR_ATLAS by IMPOSTOR_ATLAS, freed with stbi_image_free
 */
static unsigned char* impostor_read(
    ecs_world_t *world,
    ecs_entity_t e,
    ImpostorChannel channel,
    const char *name)
{
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, name);
    remove(path);
    test_int(box_impostor_write(world, e, channel, path), 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, IMPOSTOR_ATLAS);
    test_int(h, IMPOSTOR_ATLAS);
    ecs_os_free(path);

    return px;
}

/** Get a pixel of a view of an atlas.
 *
 * @param px The atlas
 * @param x Column of the view
 * @param y Row of the view
 * @param u Column in the view
 * @param v Row in the view
 * @return Its RGBA bytes
 */
static const unsigned char* impostor_pixel(
    const unsigned char *px,
    int32_t x,
    int32_t y,
    int32_t u,
    int32_t v)
{
    int32_t col = x * IMPOSTOR_RES + u, row = y * IMPOSTOR_RES + v;

    return &px[((size_t)row * IMPOSTOR_ATLAS + (size_t)col) * 4];
}

/** Check whether a pixel has a color within the tolerance.
 *
 * @param p The pixel
 * @param r Red
 * @param g Green
 * @param b Blue
 * @return Whether every channel is within IMPOSTOR_TOLERANCE
 */
static bool impostor_near(
    const unsigned char *p,
    int32_t r,
    int32_t g,
    int32_t b)
{
    return abs(p[0] - r) <= IMPOSTOR_TOLERANCE && abs(p[1] - g) <= IMPOSTOR_TOLERANCE &&
        abs(p[2] - b) <= IMPOSTOR_TOLERANCE;
}

/** Get the color of the face of ColorCube a direction looks at, when one axis clearly
 * dominates it.
 *
 * @param d The unit direction from the center toward the viewer
 * @param rgb Receives the color of the face
 * @return Whether one axis dominates
 */
static bool impostor_face(
    const vec3 d,
    int32_t rgb[3])
{
    static const int32_t colors[6][3] = {
        {255, 0, 0}, {0, 255, 0},   /* +X, -X */
        {255, 0, 255}, {0, 255, 255}, /* +Y, -Y */
        {0, 0, 255}, {255, 255, 0}  /* +Z, -Z */
    };
    int32_t axis, best = 0;
    for (axis = 1; axis < 3; axis ++) {
        if (fabsf(d[axis]) > fabsf(d[best])) {
            best = axis;
        }
    }

    if (fabsf(d[best]) < 0.75f) {
        return false;
    }

    int32_t face = best * 2 + (d[best] < 0.0f ? 1 : 0);
    rgb[0] = colors[face][0];
    rgb[1] = colors[face][1];
    rgb[2] = colors[face][2];

    return true;
}

/** Check that the center of every view of a ColorCube atlas whose direction looks at one face
 * shows that face, covered.
 *
 * @param px The albedo atlas
 * @param hemisphere Whether the bake covers the upper hemisphere
 * @return Views checked
 */
static int32_t impostor_check_faces(
    const unsigned char *px,
    bool hemisphere)
{
    int32_t x, y, result = 0;
    for (y = 0; y < IMPOSTOR_FRAMES; y ++) {
        for (x = 0; x < IMPOSTOR_FRAMES; x ++) {
            vec3 d;
            int32_t rgb[3];
            box_impostor_frame_direction(IMPOSTOR_FRAMES, hemisphere, x, y, d);
            if (!impostor_face(d, rgb)) {
                continue;
            }

            const unsigned char *p = impostor_pixel(px, x, y,
                IMPOSTOR_RES / 2, IMPOSTOR_RES / 2);
            test_assert(impostor_near(p, rgb[0], rgb[1], rgb[2]));
            test_int(p[3], 255);
            result ++;
        }
    }

    return result;
}

void Impostor_direction_round_trip(void) {
    int32_t frames, x, y, hemi;
    for (hemi = 0; hemi < 2; hemi ++) {
        for (frames = 2; frames <= 16; frames ++) {
            for (y = 0; y < frames; y ++) {
                for (x = 0; x < frames; x ++) {
                    vec3 d, back;
                    int32_t fx = -1, fy = -1;
                    box_impostor_frame_direction(frames, hemi != 0, x, y, d);
                    test_assert(fabsf(glm_vec3_norm(d) - 1.0f) < 1e-5f);
                    box_impostor_frame_of(frames, hemi != 0, d, &fx, &fy);

                    /* The border of the sphere layout repeats its directions mirrored, so
                     * a cell maps back to a cell with the same direction */
                    box_impostor_frame_direction(frames, hemi != 0, fx, fy, back);
                    test_assert(glm_vec3_distance(d, back) < 1e-4f);
                    if (hemi) {
                        test_int(fx, x);
                        test_int(fy, y);
                    }
                }
            }
        }
    }
}

void Impostor_hemisphere_layout(void) {
    int32_t x, y;
    vec3 d;
    for (y = 0; y < IMPOSTOR_FRAMES; y ++) {
        for (x = 0; x < IMPOSTOR_FRAMES; x ++) {
            box_impostor_frame_direction(IMPOSTOR_FRAMES, true, x, y, d);
            test_assert(d[1] >= -1e-5f);
        }
    }

    box_impostor_frame_direction(IMPOSTOR_FRAMES, true, 2, 2, d);
    test_flt(d[1], 1.0f);
    box_impostor_frame_direction(IMPOSTOR_FRAMES, true, 4, 4, d);
    test_flt(d[0], 1.0f);
    box_impostor_frame_direction(IMPOSTOR_FRAMES, true, 0, 0, d);
    test_flt(d[0], -1.0f);
    box_impostor_frame_direction(IMPOSTOR_FRAMES, true, 4, 0, d);
    test_flt(d[2], 1.0f);
    box_impostor_frame_direction(IMPOSTOR_FRAMES, true, 0, 4, d);
    test_flt(d[2], -1.0f);
    box_impostor_frame_direction(IMPOSTOR_FRAMES, true, 4, 2, d);
    test_assert(fabsf(d[1]) < 1e-5f);
}

void Impostor_sphere_layout(void) {
    vec3 d;
    int32_t x, y, below = 0;
    for (y = 0; y < IMPOSTOR_FRAMES; y ++) {
        for (x = 0; x < IMPOSTOR_FRAMES; x ++) {
            box_impostor_frame_direction(IMPOSTOR_FRAMES, false, x, y, d);
            below += d[1] < -0.5f;
        }
    }

    test_assert(below > 0);
    box_impostor_frame_direction(IMPOSTOR_FRAMES, false, 2, 2, d);
    test_flt(d[1], 1.0f);
    box_impostor_frame_direction(IMPOSTOR_FRAMES, false, 0, 0, d);
    test_flt(d[1], -1.0f);
    box_impostor_frame_direction(IMPOSTOR_FRAMES, false, 4, 4, d);
    test_flt(d[1], -1.0f);
    box_impostor_frame_direction(IMPOSTOR_FRAMES, false, 4, 2, d);
    test_flt(d[0], 1.0f);
    box_impostor_frame_direction(IMPOSTOR_FRAMES, false, 2, 4, d);
    test_flt(d[2], 1.0f);
}

void Impostor_status_after_bake(void) {
    ecs_world_t *world = impostor_world("color_cube.flecs");
    ecs_entity_t cube = impostor_prefab(world, "ColorCube", true);
    const ImpostorStatus *s = impostor_wait(world, cube, 1);
    test_int(s->revision, 1);
    test_bool(s->baking, false);
    test_int(s->atlas_size, IMPOSTOR_ATLAS);
    test_int(s->frames, IMPOSTOR_FRAMES);
    test_int(s->resolution, IMPOSTOR_RES);
    test_bool(s->hemisphere, true);
    test_int(s->baked, IMPOSTOR_FRAMES * IMPOSTOR_FRAMES);
    test_assert(s->bake_time >= 0.0f);
    test_assert(fabsf(s->center[0]) < 0.01f);
    test_assert(fabsf(s->center[1]) < 0.01f);
    test_assert(fabsf(s->center[2]) < 0.01f);
    test_assert(fabsf(s->radius - 1.01f * sqrtf(3.0f)) < 0.02f);
    test_assert(box_render_impostor_atlas(world, cube, ImpostorAlbedo) != 0);
    test_assert(box_render_impostor_atlas(world, cube, ImpostorNormal) != 0);
    test_assert(box_render_impostor_atlas(world, cube, ImpostorDepth) != 0);
    ecs_fini(world);
}

void Impostor_instances_share_prefab(void) {
    ecs_world_t *world = impostor_world("color_cube.flecs");
    ecs_entity_t cube = impostor_prefab(world, "ColorCube", true);
    ecs_entity_t a = ecs_new_w_pair(world, EcsIsA, cube);
    ecs_entity_t b = ecs_new_w_pair(world, EcsIsA, cube);
    ecs_set(world, b, Position, {10, 0, 0});
    impostor_wait(world, cube, 1);
    test_assert(!ecs_owns(world, a, Impostor));
    test_assert(!ecs_owns(world, b, Impostor));
    test_assert(!ecs_has(world, a, ImpostorStatus));
    test_int(box_render_impostor_atlas(world, a, ImpostorAlbedo), 0);
    test_assert(box_render_impostor_atlas(world, cube, ImpostorAlbedo) != 0);
    ecs_fini(world);
}

void Impostor_hemisphere_colors_match_directions(void) {
    ecs_world_t *world = impostor_world("color_cube.flecs");
    ecs_entity_t cube = impostor_prefab(world, "ColorCube", true);
    impostor_wait(world, cube, 1);
    unsigned char *px = impostor_read(world, cube, ImpostorAlbedo, "impostor_hemi.png");
    test_assert(impostor_check_faces(px, true) >= 5);

    /* No view of the upper hemisphere sees the bottom face */
    int32_t x, y;
    for (y = 0; y < IMPOSTOR_FRAMES; y ++) {
        for (x = 0; x < IMPOSTOR_FRAMES; x ++) {
            const unsigned char *p = impostor_pixel(px, x, y,
                IMPOSTOR_RES / 2, IMPOSTOR_RES / 2);
            test_assert(!impostor_near(p, 0, 255, 255));
        }
    }

    stbi_image_free(px);
    ecs_fini(world);
}

void Impostor_sphere_colors_match_directions(void) {
    ecs_world_t *world = impostor_world("color_cube.flecs");
    ecs_entity_t cube = impostor_prefab(world, "ColorCube", false);
    impostor_wait(world, cube, 1);
    unsigned char *px = impostor_read(world, cube, ImpostorAlbedo, "impostor_sphere.png");
    test_assert(impostor_check_faces(px, false) >= 9);

    /* The corners of the sphere layout look up from below at the bottom face */
    test_assert(impostor_near(impostor_pixel(px, 0, 0, IMPOSTOR_RES / 2, IMPOSTOR_RES / 2),
        0, 255, 255));
    test_assert(impostor_near(impostor_pixel(px, 4, 4, IMPOSTOR_RES / 2, IMPOSTOR_RES / 2),
        0, 255, 255));
    stbi_image_free(px);
    ecs_fini(world);
}

void Impostor_background_uncovered(void) {
    ecs_world_t *world = impostor_world("color_cube.flecs");
    ecs_entity_t cube = impostor_prefab(world, "ColorCube", true);
    impostor_wait(world, cube, 1);
    unsigned char *px = impostor_read(world, cube, ImpostorAlbedo, "impostor_background.png");
    int32_t x, y;
    for (y = 0; y < IMPOSTOR_FRAMES; y ++) {
        for (x = 0; x < IMPOSTOR_FRAMES; x ++) {
            test_int(impostor_pixel(px, x, y, 0, 0)[3], 0);
            test_int(impostor_pixel(px, x, y, IMPOSTOR_RES - 1, IMPOSTOR_RES - 1)[3], 0);
        }
    }

    stbi_image_free(px);
    ecs_fini(world);
}

void Impostor_dilation_fills_border(void) {
    ecs_world_t *world = impostor_world("color_cube.flecs");
    ecs_entity_t cube = impostor_prefab(world, "ColorCube", true);
    impostor_wait(world, cube, 1);
    unsigned char *px = impostor_read(world, cube, ImpostorAlbedo, "impostor_dilate.png");

    /* View (4, 4) looks along +X at the red face, which covers the middle half of the view:
     * the texels just outside it are uncovered but take the color of the edge of the box, not
     * the black of the background */
    int32_t u = IMPOSTOR_RES / 2, v;
    for (v = IMPOSTOR_RES / 2; v < IMPOSTOR_RES; v ++) {
        if (impostor_pixel(px, 4, 4, u, v)[3] == 0) {
            break;
        }
    }

    test_assert(v < IMPOSTOR_RES);
    const unsigned char *p = impostor_pixel(px, 4, 4, u, v);
    test_int(p[3], 0);
    test_assert(p[0] > 100 || p[1] > 100 || p[2] > 100);
    stbi_image_free(px);
    ecs_fini(world);
}

void Impostor_normal_and_depth(void) {
    ecs_world_t *world = impostor_world("color_cube.flecs");
    ecs_entity_t cube = impostor_prefab(world, "ColorCube", true);
    const ImpostorStatus *s = impostor_wait(world, cube, 1);
    float radius = s->radius;
    unsigned char *normal = impostor_read(world, cube, ImpostorNormal, "impostor_normal.png");
    unsigned char *depth = impostor_read(world, cube, ImpostorDepth, "impostor_depth.png");

    /* View (4, 4) looks along +X, view (2, 2) down along -Y at the top face */
    const unsigned char *n = impostor_pixel(normal, 4, 4, IMPOSTOR_RES / 2, IMPOSTOR_RES / 2);
    test_assert(impostor_near(n, 255, 128, 128));
    test_int(n[3], 255);
    n = impostor_pixel(normal, 2, 2, IMPOSTOR_RES / 2, IMPOSTOR_RES / 2);
    test_assert(impostor_near(n, 128, 255, 128));

    /* The face is 1.01 from the center, the near plane radius from it, the box 2 * radius
     * deep */
    float expected = (radius - 1.01f) / (2.0f * radius);
    const unsigned char *d = impostor_pixel(depth, 4, 4, IMPOSTOR_RES / 2, IMPOSTOR_RES / 2);
    test_assert(fabsf((float)d[0] / 255.0f - expected) < 0.03f);
    test_int(d[3], 255);
    stbi_image_free(normal);
    stbi_image_free(depth);
    ecs_fini(world);
}

void Impostor_cutout_coverage(void) {
    ecs_world_t *world = impostor_world("disc_card.flecs");
    ecs_entity_t card = impostor_prefab(world, "DiscCard", true);

    /* The load of the disc bakes again, so wait for it and for the bakes to settle */
    ecs_entity_t disc = ecs_lookup(world, "impostor_disc");
    test_assert(disc != 0);
    int32_t i;
    for (i = 0; i < IMPOSTOR_FRAMES_MAX; i ++) {
        const TextureStatus *ts = ecs_get(world, disc, TextureStatus);
        if (ts && ts->state == TextureReady) {
            break;
        }

        ecs_progress(world, 0);
    }

    const ImpostorStatus *s = NULL;
    for (i = 0; i < IMPOSTOR_FRAMES_MAX; i ++) {
        ecs_progress(world, 0);
        s = ecs_get(world, card, ImpostorStatus);
        if (s && s->ready && !s->baking) {
            break;
        }
    }

    s = impostor_wait(world, card, 1);
    test_bool(s->baking, false);
    float radius = s->radius;
    unsigned char *px = impostor_read(world, card, ImpostorAlbedo, "impostor_card.png");

    /* View (4, 0) looks along +Z at the card. The card spans half its width, 1 meter, in
     * pixels: the view frames 2 * radius in the resolution less the padding */
    int32_t pad = IMPOSTOR_RES / 16;
    float half = (float)(IMPOSTOR_RES - 2 * pad) / (2.0f * radius);
    int32_t c = IMPOSTOR_RES / 2;
    int32_t in = (int32_t)(0.6f * half), corner = (int32_t)(0.9f * half);
    test_int(impostor_pixel(px, 4, 0, c, c)[3], 255);
    test_int(impostor_pixel(px, 4, 0, c + in, c)[3], 255);
    test_int(impostor_pixel(px, 4, 0, c, c - in)[3], 255);
    test_int(impostor_pixel(px, 4, 0, c + corner, c + corner)[3], 0);
    test_int(impostor_pixel(px, 4, 0, c - corner, c - corner)[3], 0);
    test_assert(impostor_near(impostor_pixel(px, 4, 0, c, c), 255, 255, 255));

    /* Covered texels have a coverage near the area of the disc */
    int32_t u, v, covered = 0;
    for (v = 0; v < IMPOSTOR_RES; v ++) {
        for (u = 0; u < IMPOSTOR_RES; u ++) {
            covered += impostor_pixel(px, 4, 0, u, v)[3] > 127;
        }
    }

    float disc_area = GLM_PIf * half * half;
    test_assert((float)covered > 0.8f * disc_area);
    test_assert((float)covered < 1.2f * disc_area);
    stbi_image_free(px);
    ecs_fini(world);
}

void Impostor_material_change_rebakes(void) {
    ecs_world_t *world = impostor_world("color_cube.flecs");
    ecs_entity_t cube = impostor_prefab(world, "ColorCube", true);
    const ImpostorStatus *s = impostor_wait(world, cube, 1);
    int32_t revision = s->revision;
    uint32_t first = box_render_impostor_atlas(world, cube, ImpostorAlbedo);
    unsigned char *px = impostor_read(world, cube, ImpostorAlbedo, "impostor_before.png");
    test_assert(impostor_near(impostor_pixel(px, 4, 4, IMPOSTOR_RES / 2, IMPOSTOR_RES / 2),
        255, 0, 0));
    stbi_image_free(px);

    ecs_entity_t face = ecs_lookup(world, "ColorCube.px");
    test_assert(face != 0);
    ecs_set(world, face, Rgb, {255, 255, 255});

    /* The previous bake stays ready while the new one runs */
    ecs_progress(world, 0);
    s = ecs_get(world, cube, ImpostorStatus);
    test_bool(s->ready, true);
    test_bool(s->baking, true);
    test_int(s->revision, revision);
    test_int(box_render_impostor_atlas(world, cube, ImpostorAlbedo), first);

    s = impostor_wait(world, cube, revision + 1);
    test_int(s->revision, revision + 1);
    test_assert(box_render_impostor_atlas(world, cube, ImpostorAlbedo) != first);
    px = impostor_read(world, cube, ImpostorAlbedo, "impostor_after.png");
    test_assert(impostor_near(impostor_pixel(px, 4, 4, IMPOSTOR_RES / 2, IMPOSTOR_RES / 2),
        255, 255, 255));
    test_assert(impostor_near(impostor_pixel(px, 0, 0, IMPOSTOR_RES / 2, IMPOSTOR_RES / 2),
        0, 255, 0));
    stbi_image_free(px);
    ecs_fini(world);
}

void Impostor_unchanged_does_not_rebake(void) {
    ecs_world_t *world = impostor_world("color_cube.flecs");
    ecs_entity_t cube = impostor_prefab(world, "ColorCube", true);
    const ImpostorStatus *s = impostor_wait(world, cube, 1);
    int32_t revision = s->revision, i;
    for (i = 0; i < 20; i ++) {
        ecs_progress(world, 0);
    }

    s = ecs_get(world, cube, ImpostorStatus);
    test_int(s->revision, revision);
    test_bool(s->baking, false);
    ecs_fini(world);
}

void Impostor_budget_spreads_bake(void) {
    ecs_world_t *world = impostor_world("color_cube.flecs");
    ecs_singleton_set(world, ImpostorBudget, {.frames = 2});
    ecs_entity_t cube = impostor_prefab(world, "ColorCube", true);
    int32_t i, frames = 0;
    const ImpostorStatus *s = NULL;
    for (i = 0; i < IMPOSTOR_FRAMES_MAX; i ++) {
        ecs_progress(world, 0);
        s = ecs_get(world, cube, ImpostorStatus);
        if (s && s->baking && s->baked > 0) {
            test_assert(s->baked <= 2 * (frames + 1));
            frames ++;
        }

        if (s && s->ready) {
            break;
        }
    }

    test_assert(s != NULL);
    test_bool(s->ready, true);
    test_assert(frames >= (IMPOSTOR_FRAMES * IMPOSTOR_FRAMES) / 2 - 1);
    ecs_fini(world);
}

void Impostor_remove_frees_atlases(void) {
    ecs_world_t *world = impostor_world("color_cube.flecs");
    ecs_entity_t cube = impostor_prefab(world, "ColorCube", true);
    impostor_wait(world, cube, 1);
    test_assert(box_render_impostor_atlas(world, cube, ImpostorAlbedo) != 0);
    ecs_remove(world, cube, Impostor);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(box_render_impostor_atlas(world, cube, ImpostorAlbedo), 0);
    test_int(box_impostor_write(world, cube, ImpostorAlbedo, "unused.png"), -1);
    ecs_fini(world);
}

void Impostor_not_ready_cannot_write(void) {
    ecs_world_t *world = impostor_world("color_cube.flecs");
    ecs_entity_t cube = impostor_prefab(world, "ColorCube", true);
    test_int(box_impostor_write(world, cube, ImpostorAlbedo, "unused.png"), -1);
    ecs_fini(world);
}

/** Edge in pixels of the images of the card cases */
#define IMPOSTOR_IMAGE (64)

/** Edge in pixels of the images of the shadow case */
#define IMPOSTOR_SHADOW_IMAGE (128)

/** Create a camera and its view, cleared to black.
 *
 * @param world The world
 * @param position Position of the camera
 * @param pitch Rotation.x of the camera, 0 looks down -z
 * @param fov Vertical field of view in degrees
 * @param lit Whether the view is lit, with shadows
 * @return The view
 */
static ecs_entity_t impostor_view(
    ecs_world_t *world,
    Position position,
    float pitch,
    float fov,
    bool lit)
{
    ecs_entity_t camera = ecs_new(world);
    ecs_set_ptr(world, camera, Position, &position);
    ecs_set(world, camera, Rotation, {pitch, 0, 0});
    ecs_set(world, camera, Camera, {.fov = fov, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, View, {.camera = camera,
        .culling = {.enabled = true},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting.enabled = lit,
        .shadows = {.enabled = lit, .cascades = 2, .resolution = 1024, .max_distance = 100,
            .split_lambda = 0.5f, .depth_bias = 2, .normal_offset = 2, .filter_size = 3,
            .fade = 0.1f, .blend = 0.1f}});

    return result;
}

/** Change the distance and fade of an Impostor, keeping its grid, and run two frames so the
 * cards and the instances take their new ranges.
 *
 * @param world The world
 * @param e The impostor
 * @param distance Impostor.distance
 * @param fade Impostor.fade
 */
static void impostor_range(
    ecs_world_t *world,
    ecs_entity_t e,
    float distance,
    float fade)
{
    Impostor *imp = ecs_get_mut(world, e, Impostor);
    test_assert(imp != NULL);
    imp->distance = distance;
    imp->fade = fade;
    ecs_modified(world, e, Impostor);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);
}

/** Render a frame into an image file and load its pixels.
 *
 * @param world The world
 * @param file Image file in the temporary folder
 * @param size Width and height of the image
 * @return RGBA pixels, freed with stbi_image_free
 */
static unsigned char* impostor_capture(
    ecs_world_t *world,
    const char *file,
    int32_t size)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = size, .height = size,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, size);
    test_int(h, size);
    ecs_os_free(path);

    return px;
}

/** Check whether a pixel shows the black background.
 *
 * @param p The pixel
 * @return Whether every channel is below 20
 */
static bool impostor_background_px(
    const unsigned char *p)
{
    return p[0] < 20 && p[1] < 20 && p[2] < 20;
}

/** Count the pixels of an image that are not background.
 *
 * @param px The pixels
 * @param size Width and height
 * @return The covered pixels
 */
static int32_t impostor_covered(
    const unsigned char *px,
    int32_t size)
{
    int32_t i, result = 0;
    for (i = 0; i < size * size; i ++) {
        result += !impostor_background_px(&px[i * 4]);
    }

    return result;
}

/** Load draw.flecs and give a prefab of it an Impostor with an instance at a position.
 *
 * @param name Name of the prefab
 * @param position Position of the instance
 * @param prefab Receives the prefab
 * @param instance Receives the instance
 * @return The world
 */
static ecs_world_t* impostor_draw_world(
    const char *name,
    Position position,
    ecs_entity_t *prefab,
    ecs_entity_t *instance)
{
    ecs_world_t *world = impostor_world("draw.flecs");
    *prefab = impostor_prefab(world, name, true);
    *instance = ecs_new_w_pair(world, EcsIsA, *prefab);
    ecs_set_ptr(world, *instance, Position, &position);

    return world;
}

void Impostor_card_matches_full_render(void) {
    ecs_entity_t panel, inst;
    ecs_world_t *world = impostor_draw_world("Panel", (Position){0, 0, -40}, &panel, &inst);
    impostor_view(world, (Position){0, 0, 0}, 0, 12, false);
    impostor_wait(world, panel, 1);
    test_assert(ecs_owns(world, inst, DrawRange));

    /* Far beyond the instance the hierarchy draws, nearer than it the card */
    impostor_range(world, panel, 1000, 1);
    unsigned char *full = impostor_capture(world, "impostor_full.png", IMPOSTOR_IMAGE);
    impostor_range(world, panel, 10, 1);
    unsigned char *card = impostor_capture(world, "impostor_card_draw.png", IMPOSTOR_IMAGE);

    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(stats.impostors, 1);
    test_int(stats.impostor_batches, 1);

    int32_t covered = impostor_covered(full, IMPOSTOR_IMAGE);
    int32_t card_covered = impostor_covered(card, IMPOSTOR_IMAGE);
    test_assert(covered > 300);
    test_assert(abs(card_covered - covered) <= covered * 15 / 100);

    /* Where both show the panel, the card shows its color */
    int32_t i, both = 0, c;
    int64_t diff = 0;
    for (i = 0; i < IMPOSTOR_IMAGE * IMPOSTOR_IMAGE; i ++) {
        const unsigned char *a = &full[i * 4], *b = &card[i * 4];
        if (impostor_background_px(a) || impostor_background_px(b)) {
            continue;
        }

        for (c = 0; c < 3; c ++) {
            diff += abs(a[c] - b[c]);
        }

        both ++;
    }

    test_assert(both > covered * 80 / 100);
    test_assert(diff / (3 * both) <= IMPOSTOR_TOLERANCE);
    stbi_image_free(full);
    stbi_image_free(card);
    ecs_fini(world);
}

void Impostor_crossfade_covers_once(void) {
    ecs_entity_t panel, inst;
    ecs_world_t *world = impostor_draw_world("Panel", (Position){0, 0, -40}, &panel, &inst);
    impostor_view(world, (Position){0, 0, 0}, 0, 12, false);
    impostor_wait(world, panel, 1);
    test_assert(ecs_owns(world, inst, DrawRange));
    impostor_range(world, panel, 1000, 1);
    unsigned char *full = impostor_capture(world, "impostor_fade_full.png", IMPOSTOR_IMAGE);

    /* The instance is in the middle of the band: half of its pixels show the hierarchy, the
     * others the card, and the dithers of the two are complementary */
    impostor_range(world, panel, 40, 8);
    unsigned char *band = impostor_capture(world, "impostor_fade_band.png", IMPOSTOR_IMAGE);
    int32_t i, mask = 0, holes = 0, extra = 0;
    for (i = 0; i < IMPOSTOR_IMAGE * IMPOSTOR_IMAGE; i ++) {
        bool in = !impostor_background_px(&full[i * 4]);
        bool shown = !impostor_background_px(&band[i * 4]);
        mask += in;
        holes += in && !shown;
        extra += !in && shown;
    }

    test_assert(mask > 300);
    test_assert(holes <= mask * 5 / 100);
    test_assert(extra <= mask * 5 / 100);

    /* At the near and far ends of the band one of the two draws alone */
    impostor_range(world, panel, 30, 8);
    unsigned char *near = impostor_capture(world, "impostor_fade_near.png", IMPOSTOR_IMAGE);
    impostor_range(world, panel, 50, 8);
    unsigned char *far = impostor_capture(world, "impostor_fade_far.png", IMPOSTOR_IMAGE);
    test_assert(abs(impostor_covered(near, IMPOSTOR_IMAGE) - mask) <= mask * 15 / 100);
    test_assert(abs(impostor_covered(far, IMPOSTOR_IMAGE) - mask) <= mask * 5 / 100);
    stbi_image_free(full);
    stbi_image_free(band);
    stbi_image_free(near);
    stbi_image_free(far);
    ecs_fini(world);
}

/** Count the pixels of the lit floor in shadow: darker than a quarter of white.
 *
 * @param px The pixels
 * @return The dark pixels
 */
static int32_t impostor_shadowed(
    const unsigned char *px)
{
    int32_t i, result = 0;
    for (i = 0; i < IMPOSTOR_SHADOW_IMAGE * IMPOSTOR_SHADOW_IMAGE; i ++) {
        const unsigned char *p = &px[i * 4];
        result += p[0] + p[1] + p[2] < 3 * 64;
    }

    return result;
}

void Impostor_card_casts_shadow(void) {
    ecs_entity_t tower, inst;
    ecs_world_t *world = impostor_draw_world("Tower", (Position){0, 3, 0}, &tower, &inst);
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {80, 0.2f, 80});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {1.0f, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    impostor_view(world, (Position){0, 45, 0}, -(float)GLM_PI_2, 40, true);
    impostor_wait(world, tower, 1);

    impostor_range(world, tower, 1000, 1);
    unsigned char *full = impostor_capture(world, "impostor_shadow_full.png",
        IMPOSTOR_SHADOW_IMAGE);
    impostor_range(world, tower, 20, 1);
    unsigned char *card = impostor_capture(world, "impostor_shadow_card.png",
        IMPOSTOR_SHADOW_IMAGE);
    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(stats.impostors, 1);

    /* The tower shades a band of the floor along the light, the card about as much */
    int32_t shadow = impostor_shadowed(full);
    int32_t card_shadow = impostor_shadowed(card);
    test_assert(shadow > 40);
    test_assert(card_shadow > shadow / 2);
    test_assert(card_shadow < shadow * 3 / 2);

    /* Without the instance the floor is lit everywhere */
    ecs_delete(world, inst);
    unsigned char *none = impostor_capture(world, "impostor_shadow_none.png",
        IMPOSTOR_SHADOW_IMAGE);
    test_assert(impostor_shadowed(none) < shadow / 4);
    stbi_image_free(full);
    stbi_image_free(card);
    stbi_image_free(none);
    ecs_fini(world);
}
