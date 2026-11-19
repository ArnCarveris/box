/**
 * @file Impostor.c
 * @brief Tests for the impostor bakes (Impostor): the octahedral views of a prefab land in the
 * cells of the albedo, normal and depth atlases, alpha tested shapes leave their coverage in
 * alpha, and a change of a material bakes again.
 *
 * The prefabs come from test/assets/impostor: ColorCube, a closed box with a color per face,
 * and DiscCard, an alpha tested quad that shows a disc. The atlases are written to PNG files in
 * the temporary folder and compared as stored.
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
