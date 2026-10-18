/**
 * @file Hud.c
 * @brief The HUD of the bunny hopper scene: score, lives, hint and game over panel.
 *
 * The game scene loads headless with its Surface, so the HUD lays out against the window. The
 * tests change the game state as the systems do and check what the HUD template keeps up to date.
 *
 * Elements are placed in points from the center of their parent, y up. The captures look for the
 * colors of the life icon in the row of lives, 14 px from the top right corner, 12 px padding.
 */

#include <bunnyhopper_game.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Path of the game scene in the repository */
#define HUD_SCENE "etc/bunnyhopper/bunnyhopper.flecs"

/** Path of this file in the repository, which locates the repository root */
#define HUD_SOURCE "test/gpu/bunnyhopper/src/"

/** Size of the window of the scene */
#define WINDOW_WIDTH (1280)
#define WINDOW_HEIGHT (1024)

/** Fixed delta of a frame */
#define GAME_DT (1.0f / 60.0f)

/** Lives of a run */
#define MAX_LIVES (3)

/** Opacity of the icon of a lost life */
#define LOST_ALPHA (70.0f / 255.0f)

/** Get the path of a file of the repository.
 *
 * @param file Path of the file in the repository
 * @return The absolute path, owned by the caller
 */
static char* repo_path(
    const char *file)
{
    const char *source = strstr(__FILE__, HUD_SOURCE);
    int32_t root = source ? (int32_t)(source - __FILE__) : 0;

    return flecs_asprintf("%.*s%s", root, __FILE__, file);
}

/** Step the world by frames of the fixed delta.
 *
 * @param world The world
 * @param frames Number of frames
 */
static void step(
    ecs_world_t *world,
    int32_t frames)
{
    for (int32_t i = 0; i < frames; i ++) {
        ecs_progress(world, GAME_DT);
    }
}

/** Load the game scene headless with its Surface and run its first frames.
 *
 * @return The world
 */
static ecs_world_t* hud_new(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ECS_IMPORT(world, Bunnyhopper);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *path = repo_path(HUD_SCENE);
    ecs_entity_t script = ecs_script(world, { .filename = path });
    ecs_os_free(path);
    test_assert(script != 0);
    const EcsScript *s = ecs_get(world, script, EcsScript);
    test_assert(s != NULL);
    test_assert(s->script != NULL);
    test_assert(s->error == NULL);

    step(world, 2);

    return world;
}

/** Look up an element of the HUD.
 *
 * @param world The world
 * @param path Path of the element below hud.ui.screen
 * @return The element, 0 when it does not exist
 */
static ecs_entity_t element(
    ecs_world_t *world,
    const char *path)
{
    char *full = flecs_asprintf("hud.ui.screen.%s", path);
    ecs_entity_t result = ecs_lookup(world, full);
    ecs_os_free(full);

    return result;
}

/** Get the game singleton.
 *
 * @param world The world
 * @return The game
 */
static const BunnyGame* game(
    ecs_world_t *world)
{
    const BunnyGame *g = ecs_singleton_get(world, BunnyGame);
    test_assert(g != NULL);

    return g;
}

/** Get the bunny.
 *
 * @param world The world
 * @return The player entity
 */
static ecs_entity_t player(
    ecs_world_t *world)
{
    ecs_entity_t e = game(world)->player;
    test_assert(e != 0);

    return e;
}

/** Change the carrots that passed the bunny, as the systems do.
 *
 * @param world The world
 * @param carrots The carrots
 */
static void carrots_set(
    ecs_world_t *world,
    int32_t carrots)
{
    BunnyGame *g = ecs_singleton_get_mut(world, BunnyGame);
    g->carrots = carrots;
    ecs_singleton_modified(world, BunnyGame);
}

/** Move the bunny out of the way of the carrots, so a run never ends.
 *
 * @param world The world
 */
static void clear_path(
    ecs_world_t *world)
{
    ecs_entity_t p = player(world);
    const Position *pos = ecs_get(world, p, Position);
    ecs_set(world, p, Position, { -100.0f, pos->y, pos->z });
}

/** Place a carrot of a prefab on the ground at the bunny.
 *
 * @param world The world
 * @param name Name of the carrot prefab
 */
static void carrot_at_player(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t prefab = ecs_lookup(world, name);
    test_assert(prefab != 0);
    ecs_entity_t e = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_add_pair(world, e, EcsChildOf, game(world)->carrot_parent);
    ecs_set(world, e, Position, { ecs_get(world, player(world), Position)->x, 0.0f, 0.0f });
}

/** Check the state of the run.
 *
 * @param world The world
 * @param name Name of the state constant
 * @return Whether (BunnyGameState, name) is on the singleton
 */
static bool state_is(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t c = ecs_lookup_child(world, ecs_id(BunnyGameState), name);
    test_assert(c != 0);

    return ecs_has_pair(world, ecs_id(BunnyGameState), ecs_id(BunnyGameState), c);
}

/** End the run: the bunny on its last life touches a carrot.
 *
 * @param world The world
 */
static void game_over(
    ecs_world_t *world)
{
    ecs_set(world, player(world), BunnyLives, { 1 });
    ecs_remove(world, player(world), BunnyInvulnerable);
    carrot_at_player(world, "SmallCarrot");
    step(world, 1);
    test_assert(state_is(world, "Over"));
    step(world, 1);
}

/** Run a command in a new terminal session and check that it succeeds.
 *
 * @param world The world
 * @param line The command line
 */
static void command(
    ecs_world_t *world,
    const char *line)
{
    ecs_entity_t session = box_terminal_new(world);
    test_assert(session != 0);
    bool error = true;
    char *out = box_terminal_run(world, session, line, &error);
    test_bool(error, false);
    ecs_os_free(out);
}

/** Get the opacity of a life icon.
 *
 * @param world The world
 * @param index Index of the icon in the row
 * @return Its Opacity
 */
static float life_alpha(
    ecs_world_t *world,
    int32_t index)
{
    char *name = flecs_asprintf("lives.life_%d", index);
    ecs_entity_t e = element(world, name);
    ecs_os_free(name);
    test_assert(e != 0);
    const Opacity *o = ecs_get(world, e, Opacity);
    test_assert(o != NULL);

    return o->value;
}

/** Get the edges of an element from its Position and LayoutBounds, in points from the center
 * of the window, y up.
 *
 * @param world The world
 * @param e The element, a direct child of hud.ui.screen
 * @param out Receives left, right, top and bottom
 */
static void edges(
    ecs_world_t *world,
    ecs_entity_t e,
    float out[4])
{
    const Position *p = ecs_get(world, e, Position);
    const LayoutBounds *b = ecs_get(world, e, LayoutBounds);
    test_assert(p != NULL);
    test_assert(b != NULL);
    out[0] = p->x - b->width / 2;
    out[1] = p->x + b->width / 2;
    out[2] = p->y + b->height / 2;
    out[3] = p->y - b->height / 2;
}

/** Step until the life icon texture is loaded.
 *
 * @param world The world
 */
static void icon_wait(
    ecs_world_t *world)
{
    ecs_entity_t icon = ecs_lookup(world, "life_icon");
    test_assert(icon != 0);
    int32_t frames = 0;
    const TextureStatus *ts = ecs_get(world, icon, TextureStatus);
    while ((!ts || ts->state != TextureReady) && frames < 600) {
        test_assert(!ts || ts->state != TextureFailed);
        step(world, 1);
        ts = ecs_get(world, icon, TextureStatus);
        frames ++;
    }

    test_assert(ts != NULL);
    test_int(ts->state, TextureReady);
}

/** Pixels of the colors of the icon in a region of an image.
 *
 * @param px RGBA pixels of the image
 * @param w Width of the image
 * @param x0 Left of the region
 * @param y0 Top of the region
 * @param x1 Right of the region, excluded
 * @param y1 Bottom of the region, excluded
 * @param pink Receives the pixels of the pink of the ears and nose
 * @param fur Receives the pixels of the white fur
 */
static void icon_pixels(
    const unsigned char *px,
    int32_t w,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    int32_t *pink,
    int32_t *fur)
{
    *pink = 0;
    *fur = 0;
    for (int32_t y = y0; y < y1; y ++) {
        for (int32_t x = x0; x < x1; x ++) {
            const unsigned char *c = &px[(y * w + x) * 4];
            *pink += c[0] > 215 && c[1] > 120 && c[1] < 180 && c[2] > 140 && c[2] < 200;
            *fur += c[0] > 225 && c[1] > 225 && c[2] > 225;
        }
    }
}

void Hud_score_follows_carrots(void) {
    // bunnyhopper/hud
    ecs_world_t *world = hud_new();
    clear_path(world);

    ecs_entity_t score = element(world, "score");
    test_assert(score != 0);
    const Text *t = ecs_get(world, score, Text);
    test_str(t->value, "Carrots: 0");
    test_flt(t->size, 30.0f);
    const Rgb *c = ecs_get(world, score, Rgb);
    test_int(c->r, 255);
    test_int(c->g, 255);
    test_int(c->b, 255);

    float e[4];
    edges(world, score, e);
    test_assert(fabsf(e[0] - (-WINDOW_WIDTH / 2.0f + 24)) < 0.5f);
    test_assert(fabsf(e[2] - (WINDOW_HEIGHT / 2.0f - 24)) < 0.5f);

    carrots_set(world, 7);
    step(world, 1);
    test_str(ecs_get(world, score, Text)->value, "Carrots: 7");
    test_uint(element(world, "score"), score);

    carrots_set(world, 12);
    step(world, 1);
    test_str(ecs_get(world, score, Text)->value, "Carrots: 12");

    ecs_fini(world);
}

void Hud_lost_lives_dimmed(void) {
    // bunnyhopper/hud
    ecs_world_t *world = hud_new();
    BunnyGame *g = ecs_singleton_get_mut(world, BunnyGame);
    g->speed = 0;
    ecs_singleton_modified(world, BunnyGame);

    ecs_entity_t lives = element(world, "lives");
    test_assert(lives != 0);
    const Layout *l = ecs_get(world, lives, Layout);
    test_int(l->direction, DirectionRow);
    test_flt(l->spacing, 12.0f);
    test_flt(l->padding.top, 12.0f);
    test_flt(l->padding.left, 12.0f);
    const LayoutBounds *b = ecs_get(world, lives, LayoutBounds);
    test_flt(b->width, 3 * 46 + 2 * 12 + 2 * 12);
    test_flt(b->height, 46 + 2 * 12);

    float e[4];
    edges(world, lives, e);
    test_assert(fabsf(e[1] - (WINDOW_WIDTH / 2.0f - 14)) < 0.5f);
    test_assert(fabsf(e[2] - (WINDOW_HEIGHT / 2.0f - 14)) < 0.5f);

    ecs_entity_t texture = ecs_lookup(world, "life_icon");
    test_assert(texture != 0);
    const TexturePath *tp = ecs_get(world, texture, TexturePath);
    test_str(tp->path, "etc/bunnyhopper/icons/bunny_life.svg");
    test_int(tp->width, 128);
    test_int(tp->height, 128);

    for (int32_t i = 0; i < MAX_LIVES; i ++) {
        char *name = flecs_asprintf("lives.life_%d", i);
        ecs_entity_t icon = element(world, name);
        ecs_os_free(name);
        test_assert(icon != 0);
        test_assert(ecs_has_pair(world, icon, EcsChildOf, lives));
        test_uint(ecs_get(world, icon, Image)->texture, texture);
        test_flt(ecs_get(world, icon, LayoutBounds)->width, 46.0f);
        test_flt(ecs_get(world, icon, LayoutBounds)->height, 46.0f);
        test_flt(life_alpha(world, i), 1.0f);
    }

    test_assert(element(world, "lives.life_3") == 0);

    carrot_at_player(world, "SmallCarrot");
    step(world, 1);
    test_int(ecs_get(world, player(world), BunnyLives)->lives_left, MAX_LIVES - 1);
    test_flt(life_alpha(world, 0), 1.0f);
    test_flt(life_alpha(world, 1), 1.0f);
    test_flt(life_alpha(world, 2), LOST_ALPHA);

    ecs_set(world, player(world), BunnyLives, { 1 });
    step(world, 1);
    test_flt(life_alpha(world, 0), 1.0f);
    test_flt(life_alpha(world, 1), LOST_ALPHA);
    test_flt(life_alpha(world, 2), LOST_ALPHA);

    ecs_fini(world);
}

void Hud_hint_until_three_carrots(void) {
    // bunnyhopper/hud
    ecs_world_t *world = hud_new();
    clear_path(world);

    ecs_entity_t hint = element(world, "hint");
    test_assert(hint != 0);
    const Text *t = ecs_get(world, hint, Text);
    test_str(t->value, "SPACE to hop - hop again in the air for big carrots!");
    test_flt(t->size, 24.0f);

    float e[4];
    edges(world, hint, e);
    test_assert(fabsf((e[0] + e[1]) / 2) < 0.5f);
    test_assert(fabsf(e[3] - (-WINDOW_HEIGHT / 2.0f + 60)) < 0.5f);

    carrots_set(world, 2);
    step(world, 1);
    test_assert(element(world, "hint") != 0);

    carrots_set(world, 3);
    step(world, 1);
    test_assert(element(world, "hint") == 0);

    carrots_set(world, 4);
    step(world, 1);
    test_assert(element(world, "hint") == 0);

    ecs_fini(world);
}

void Hud_game_over_panel(void) {
    // bunnyhopper/hud
    ecs_world_t *world = hud_new();
    step(world, 30);
    test_assert(element(world, "game_over") == 0);

    game_over(world);
    ecs_entity_t panel = element(world, "game_over");
    test_assert(panel != 0);
    test_assert(element(world, "hint") == 0);
    test_flt(life_alpha(world, 0), LOST_ALPHA);

    float e[4];
    edges(world, panel, e);
    test_assert(fabsf(e[1] - e[0] - 560) < 0.5f);
    test_assert(fabsf(e[2] - e[3] - 170) < 0.5f);
    test_assert(fabsf((e[0] + e[1]) / 2) < 0.5f);
    test_assert(fabsf(e[2] - (WINDOW_HEIGHT / 2.0f - 256)) < 0.5f);

    ecs_entity_t back = element(world, "game_over.background");
    test_assert(back != 0);
    test_flt(ecs_get(world, back, LayoutBounds)->width, 560.0f);
    test_flt(ecs_get(world, back, LayoutBounds)->height, 170.0f);
    test_flt(ecs_get(world, back, Border)->radius, 8.0f);
    test_flt(ecs_get(world, back, Opacity)->value, 170.0f / 255.0f);
    test_int(ecs_get(world, back, Rgb)->r, 20);
    test_int(ecs_get(world, back, Rgb)->g, 30);
    test_int(ecs_get(world, back, Rgb)->b, 20);

    ecs_entity_t title = element(world, "game_over.title");
    ecs_entity_t prompt = element(world, "game_over.prompt");
    test_assert(title != 0);
    test_assert(prompt != 0);
    test_str(ecs_get(world, title, Text)->value, "GAME OVER");
    test_flt(ecs_get(world, title, Text)->size, 64.0f);
    test_str(ecs_get(world, prompt, Text)->value, "Press R to try again");
    test_flt(ecs_get(world, prompt, Text)->size, 26.0f);
    test_assert(!ecs_has(world, title, Opacity));
    test_assert(!ecs_has(world, prompt, Opacity));

    const Position *tp = ecs_get(world, title, Position);
    const LayoutBounds *tb = ecs_get(world, title, LayoutBounds);
    test_assert(fabsf(tp->x) < 0.5f);
    test_assert(fabsf(tp->y + tb->height / 2 - (85 - 28)) < 0.5f);
    const Position *pp = ecs_get(world, prompt, Position);
    const LayoutBounds *pb = ecs_get(world, prompt, LayoutBounds);
    test_assert(fabsf(pp->x) < 0.5f);
    test_assert(fabsf(pp->y + pb->height / 2 - (85 - 110)) < 0.5f);

    step(world, 10);
    test_uint(element(world, "game_over"), panel);

    command(world, "key.press R");
    step(world, 2);
    test_assert(state_is(world, "Playing"));
    test_assert(element(world, "game_over") == 0);
    test_assert(element(world, "hint") != 0);
    test_flt(life_alpha(world, 0), 1.0f);
    test_flt(life_alpha(world, 2), 1.0f);

    game_over(world);
    test_assert(element(world, "game_over") != 0);

    ecs_fini(world);
}

void Hud_follows_resize(void) {
    // bunnyhopper/hud
    ecs_world_t *world = hud_new();
    clear_path(world);

    ecs_entity_t window = ecs_lookup(world, "window");
    test_assert(window != 0);
    ecs_entity_t lives = element(world, "lives");
    ecs_entity_t score = element(world, "score");
    float e[4];
    edges(world, lives, e);
    test_assert(fabsf(e[1] - (WINDOW_WIDTH / 2.0f - 14)) < 0.5f);

    Surface *s = ecs_get_mut(world, window, Surface);
    s->width = 800;
    s->height = 600;
    ecs_modified(world, window, Surface);
    step(world, 2);

    const LayoutBounds *screen = ecs_get(world, ecs_lookup(world, "hud.ui.screen"),
        LayoutBounds);
    test_flt(screen->width, 800.0f);
    test_flt(screen->height, 600.0f);
    edges(world, lives, e);
    test_assert(fabsf(e[1] - (400.0f - 14)) < 0.5f);
    test_assert(fabsf(e[2] - (300.0f - 14)) < 0.5f);
    edges(world, score, e);
    test_assert(fabsf(e[0] - (-400.0f + 24)) < 0.5f);
    test_assert(fabsf(e[2] - (300.0f - 24)) < 0.5f);
    edges(world, element(world, "hint"), e);
    test_assert(fabsf(e[3] - (-300.0f + 60)) < 0.5f);

    ecs_fini(world);
}

void Hud_capture_shows_icons(void) {
    // bunnyhopper/hud
    ecs_world_t *world = hud_new();
    clear_path(world);
    icon_wait(world);

    ecs_entity_t window = ecs_lookup(world, "window");
    Surface *s = ecs_get_mut(world, window, Surface);
    ecs_os_strset(&s->write_to_file, "hud.png");
    ecs_modified(world, window, Surface);
    step(world, 2);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/hud.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, WINDOW_WIDTH);
    test_int(h, WINDOW_HEIGHT);

    int32_t right = WINDOW_WIDTH - 14 - 12, top = 14 + 12, pink, fur;
    icon_pixels(px, w, right - 3 * 46 - 2 * 12, top, right, top + 46, &pink, &fur);
    test_assert(pink > 3 * 40);
    test_assert(fur > 3 * 400);

    int32_t last_fur = 0;
    for (int32_t i = 0; i < MAX_LIVES; i ++) {
        int32_t x = right - (MAX_LIVES - i) * 46 - (MAX_LIVES - 1 - i) * 12;
        icon_pixels(px, w, x, top, x + 46, top + 46, &pink, &fur);
        test_assert(pink > 40);
        test_assert(fur > 400);
        last_fur = fur;
    }

    icon_pixels(px, w, WINDOW_WIDTH - 400, top, WINDOW_WIDTH - 300, top + 46, &pink, &fur);
    test_int(pink, 0);
    test_int(fur, 0);
    stbi_image_free(px);

    ecs_set(world, player(world), BunnyLives, { MAX_LIVES - 1 });
    step(world, 2);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    int32_t x = right - 46;
    icon_pixels(px, w, x, top, x + 46, top + 46, &pink, &fur);
    test_assert(fur < last_fur / 10);
    stbi_image_free(px);

    ecs_os_free(path);
    ecs_fini(world);
}
