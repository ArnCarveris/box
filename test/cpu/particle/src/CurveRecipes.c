/**
 * @file CurveRecipes.c
 * @brief Tests for the recipes of docs/curves.md in test/assets/curves: every recipe loads, runs
 * and logs nothing, the mistakes of test/assets/curves/mistakes log the errors the guide quotes,
 * and a script reports only its first error.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <string.h>
#include "render.h"

/** Frame delta of the tests */
#define DELTA (0.1f)

/** Frames a recipe runs, three seconds so the bursts at 1.5 s and 2.5 s go off */
#define FRAMES (30)

/** Warnings and errors logged while capturing */
static char recipe_log[4096];

/** Log level before capturing */
static int32_t recipe_level;

/** Log function before capturing */
static ecs_os_api_log_t recipe_log_fn;

/** Whether the log was colored before capturing */
static bool recipe_colors;

/** Log function that keeps the warnings and errors.
 *
 * @param level The level
 * @param file The file
 * @param line The line
 * @param msg The message
 */
static void recipe_capture(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    if (level <= -2 && strlen(recipe_log) + strlen(msg) + 2 < sizeof(recipe_log)) {
        strcat(recipe_log, msg);
        strcat(recipe_log, "\n");
    }
}

/** Start keeping the warnings and errors in recipe_log. */
static void log_begin(void)
{
    recipe_log[0] = '\0';
    recipe_level = ecs_log_set_level(-1);
    recipe_colors = ecs_log_enable_colors(false);
    recipe_log_fn = ecs_os_api.log_;
    ecs_os_api.log_ = recipe_capture;
}

/** Stop keeping the warnings and errors. */
static void log_end(void)
{
    ecs_os_api.log_ = recipe_log_fn;
    ecs_log_enable_colors(recipe_colors);
    ecs_log_set_level(recipe_level);
}

/** Check that the log holds a message.
 *
 * @param text The message
 */
static void logged(
    const char *text)
{
    if (!strstr(recipe_log, text)) {
        printf("expected '%s' in:\n%s", text, recipe_log);
        test_assert(false);
    }
}

/** A world that renders into a null backend, with a recipe loaded and run */
typedef struct recipe_t {
    ecs_world_t *world;     /**< The world */
    box_render_null_t *null; /**< The backend */
} recipe_t;

/** Load a recipe of test/assets/curves and run FRAMES frames, keeping the log.
 *
 * @param r Receives the world
 * @param name File name of the recipe in test/assets/curves
 * @return Whether the script ran
 */
static bool recipe_load(
    recipe_t *r,
    const char *name)
{
    char path[256];
    int32_t i;
    snprintf(path, sizeof(path), "test/assets/curves/%s", name);
    r->world = ecs_init();
    char *file = box_app_locate(path);
    ECS_IMPORT(r->world, Box);
    r->null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(r->null);
    box_render_set_backend(r->world, &backend);
    log_begin();
    bool result = ecs_script(r->world, { .filename = file }) != 0;
    for (i = 0; result && i < FRAMES; i ++) {
        ecs_progress(r->world, DELTA);
        box_render_sync(r->world);
    }

    log_end();
    ecs_os_free(file);

    return result;
}

/** Free the world of a recipe.
 *
 * @param r The recipe
 */
static void recipe_fini(
    recipe_t *r)
{
    ecs_fini(r->world);
    box_render_null_free(r->null);
}

/** Check that a recipe loads and runs without a warning or an error, and that its entity has
 * the component that holds its curves.
 *
 * @param name File name of the recipe
 * @param entity Path of the entity of the recipe
 * @param component Path of the component, such as "box.particle.ParticleCurves"
 * @param particles Whether the recipe has particles alive after its frames
 */
static void recipe_check(
    const char *name,
    const char *entity,
    const char *component,
    bool particles)
{
    recipe_t r;
    test_assert(recipe_load(&r, name));
    if (recipe_log[0]) {
        printf("%s logged:\n%s", name, recipe_log);
    }

    test_str(recipe_log, "");
    ecs_entity_t e = ecs_lookup(r.world, entity);
    ecs_entity_t c = ecs_lookup(r.world, component);
    test_assert(e != 0);
    test_assert(c != 0);
    test_assert(ecs_has_id(r.world, e, c));
    if (particles) {
        test_assert(ecs_singleton_get(r.world, ParticleStats)->particles > 0);
    }

    recipe_fini(&r);
}

void CurveRecipes_format(void) {
    // particle/curve_docs
    recipe_check("format.flecs", "smoke", "box.particle.ParticleCurves", true);
}

void CurveRecipes_fade(void) {
    // particle/curve_docs
    recipe_check("fade.flecs", "fade", "box.particle.ParticleCurves", true);
}

void CurveRecipes_puff(void) {
    // particle/curve_docs
    recipe_check("puff.flecs", "puff", "box.particle.ParticleCurves", true);
}

void CurveRecipes_spark(void) {
    // particle/curve_docs
    recipe_check("spark.flecs", "spark", "box.particle.ParticleCurves", true);
}

void CurveRecipes_flame(void) {
    // particle/curve_docs
    recipe_check("flame.flecs", "flame", "box.particle.ParticleCurves", true);
}

void CurveRecipes_burst(void) {
    // particle/curve_docs
    recipe_check("burst.flecs", "burst", "box.particle.ParticleMotion", true);
}

void CurveRecipes_pulse(void) {
    // particle/curve_docs
    recipe_check("pulse.flecs", "pulse", "box.particle.ParticleTimeline", true);
}

void CurveRecipes_flicker(void) {
    // particle/curve_docs
    recipe_check("flicker.flecs", "campfire", "box.particle.ParticleLight", true);
}

void CurveRecipes_random_size(void) {
    // particle/curve_docs
    recipe_check("random_size.flecs", "bubbles", "box.particle.ParticleCurves", true);
}

void CurveRecipes_path(void) {
    // particle/curve_docs
    recipe_check("path.flecs", "vase_outer", "box.geometry.Path2D", false);
    recipe_check("path.flecs", "tube_route", "box.geometry.Path3D", false);
}

void CurveRecipes_ease(void) {
    // particle/curve_docs
    recipe_check("ease.flecs", "panel", "box.transition.Transition", false);
}

void CurveRecipes_showcase_campfire(void) {
    // particle/curve_docs
    recipe_check("showcase/campfire.flecs", "campfire.flame", "box.particle.ParticleLight", true);
    recipe_check("showcase/campfire.flecs", "campfire.embers", "box.particle.ParticleMotion", true);
}

void CurveRecipes_showcase_shockwave(void) {
    // particle/curve_docs
    recipe_check("showcase/shockwave.flecs", "shockwave.core", "box.particle.ParticleLight", true);
    recipe_check("showcase/shockwave.flecs", "shockwave.dust", "box.particle.ParticleMotion", true);
    recipe_check("showcase/shockwave.flecs", "shockwave.debris", "box.particle.ParticleCollision",
        true);
}

void CurveRecipes_showcase_orb(void) {
    // particle/curve_docs
    recipe_check("showcase/orb.flecs", "orb.halo", "box.particle.ParticleTimeline", true);
    recipe_check("showcase/orb.flecs", "orb.motes", "box.particle.ParticleMotion", true);
}

void CurveRecipes_showcase_chimney(void) {
    // particle/curve_docs
    recipe_check("showcase/chimney.flecs", "chimney.smoke", "box.particle.ParticleTimeline", true);
}

/** Check that a mistake of test/assets/curves/mistakes loads and logs an error.
 *
 * @param name File name of the mistake
 * @param error The error it logs
 */
static void mistake_check(
    const char *name,
    const char *error)
{
    char path[64];
    recipe_t r;
    snprintf(path, sizeof(path), "mistakes/%s", name);
    test_assert(recipe_load(&r, path));
    logged(error);
    recipe_fini(&r);
}

void CurveRecipes_mistakes(void) {
    // particle/curve_docs
    mistake_check("late.flecs", "invalid ParticleCurves on late: size key 2: time 1.5 is "
        "outside of 0 to 1, drawn as the default of size");
    mistake_check("unordered.flecs", "invalid ParticleCurves on unordered: opacity key 2: time "
        "0.5 is not after time 0.6 of key 1, drawn as the default of opacity");
    mistake_check("jump.flecs", "invalid ParticleCurves on jump: spin max_keys key 1: time 0 is "
        "not after time 0 of key 0, drawn as the default of spin");
    mistake_check("lamp.flecs", "invalid ParticleLight on lamp: curve max_keys is not allowed, a "
        "light has no random draw to blend two curves by, drawn as the default of curve");
    mistake_check("seconds.flecs", "invalid ParticleTimeline on seconds: rate key 1: time 2 is "
        "outside of 0 to 1, drawn as the default of rate");
}

/** Read a file of the repository.
 *
 * @param path Path from the repository root
 * @return The contents, to free with ecs_os_free, NULL when it cannot be read
 */
static char* file_read(
    const char *path)
{
    char *file = box_app_locate(path);
    char *result = NULL;
    FILE *f = fopen(file, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        result = ecs_os_malloc((ecs_size_t)size + 1);
        result[fread(result, 1, (size_t)size, f)] = '\0';
        fclose(f);
    }

    ecs_os_free(file);

    return result;
}

void CurveRecipes_guide_quotes_recipes(void) {
    // particle/curve_docs
    static const char *recipes[] = {
        "format", "fade", "puff", "spark", "flame", "burst", "pulse", "flicker",
        "random_size", "path", "ease"
    };
    ecs_world_t *world = ecs_init();
    char *guide = file_read("docs/curves.md");
    test_assert(guide != NULL);
    int32_t i;
    for (i = 0; i < (int32_t)(sizeof(recipes) / sizeof(recipes[0])); i ++) {
        char path[64];
        snprintf(path, sizeof(path), "test/assets/curves/%s.flecs", recipes[i]);
        char *recipe = file_read(path);
        test_assert(recipe != NULL);
        char *body = recipe;
        while (!strncmp(body, "//", 2)) {
            char *end = strchr(body, '\n');
            body = end ? end + 1 : body + strlen(body);
        }

        if (!strstr(guide, body)) {
            printf("docs/curves.md does not quote %s as it is\n", path);
            test_assert(false);
        }

        ecs_os_free(recipe);
    }

    ecs_os_free(guide);
    ecs_fini(world);
}

void CurveRecipes_first_mistake_only(void) {
    // particle/curve_docs
    recipe_t r;
    test_assert(recipe_load(&r, "mistakes/two.flecs"));
    logged("invalid ParticleCurves on first: size key 2");
    test_assert(strstr(recipe_log, "second") == NULL);
    recipe_fini(&r);
}
