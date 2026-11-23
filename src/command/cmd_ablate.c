/**
 * @file command/cmd_ablate.c
 * @brief The ablate command: times the uncapped frames of a view with each of its features off,
 * or at another quality, one at a time, to find what the frames of a scene spend their time on.
 *
 * Every variant waits until the shaders compiled, the loads finished, no pipeline is created
 * and the bakes settled before its frames are timed. A baseline is timed between the sections.
 */

#include "command.h"
#include "../render/render.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>

/** Frames a variant is timed over by default */
#define BOX_CMD_ABLATE_FRAMES (120)

/** Frames in a row without a load, a compile or a new pipeline before a variant is timed */
#define BOX_CMD_ABLATE_SETTLE (30)

/** Frames a variant may take to settle before it is timed anyway, marked unsettled */
#define BOX_CMD_ABLATE_TIMEOUT (3000)

/** Frames a bake may go without progress before a variant is timed without it */
#define BOX_CMD_ABLATE_BAKE_STALL (300)

/** Frame rate cap of a run, high enough that no frame waits for it */
#define BOX_CMD_ABLATE_UNCAPPED (100000.0f)

/** Options the command takes */
#define BOX_CMD_ABLATE_OPTIONS_MAX (6)

/** Rounds of every step by default, whose median is reported */
#define BOX_CMD_ABLATE_ROUNDS (3)

/** Most rounds of a run */
#define BOX_CMD_ABLATE_ROUNDS_MAX (9)

/** Variants timed at most between two baselines */
#define BOX_CMD_ABLATE_BASELINE_EVERY (4)

/** Fewest frames of the warmup before the first baseline, which lets the GPU clock up */
#define BOX_CMD_ABLATE_WARMUP (300)

/** Smallest difference from the baseline, as a fraction of it, that is not noise */
#define BOX_CMD_ABLATE_NOISE (0.01)

/** Variants listed under the largest costs of the report */
#define BOX_CMD_ABLATE_RANKED (10)

/** Components a variant changes: the View, the Scene of its camera and its Surface */
#define BOX_CMD_ABLATE_COMPONENTS (3)

/** Width of the label column of the report */
#define BOX_CMD_ABLATE_LABEL_WIDTH (40)

/** What a step of a run changes */
typedef enum box_cmd_ablate_target_t {
    BoxCmdAblateNone = 0,  /**< Nothing, a baseline */
    BoxCmdAblateView,      /**< A member of the View */
    BoxCmdAblateEffect,    /**< An effect of the View, which it turns off */
    BoxCmdAblateScene,     /**< A member of the Scene of the camera */
    BoxCmdAblateSurface    /**< A member of the Surface the view renders to */
} box_cmd_ablate_target_t;

/** Type of the member a step writes */
typedef enum box_cmd_ablate_kind_t {
    BoxCmdAblateBool = 0,  /**< bool */
    BoxCmdAblateFloat,     /**< float */
    BoxCmdAblateInt        /**< int32_t or an enum */
} box_cmd_ablate_kind_t;

/** Where a run is */
typedef enum box_cmd_ablate_phase_t {
    BoxCmdAblateIdle = 0,  /**< No run started */
    BoxCmdAblateSettle,    /**< Waiting for the step to settle */
    BoxCmdAblateMeasure,   /**< Timing the frames of the step */
    BoxCmdAblateDone,      /**< Finished, the report is ready */
    BoxCmdAblateStopped    /**< Stopped before it finished */
} box_cmd_ablate_phase_t;

/** A step of a run: a baseline or a variant, and what its frames measured */
typedef struct box_cmd_ablate_step_t {
    char label[48];                 /**< What it changes, such as "shadows off" */
    char note[64];                  /**< What else it changes or what it is, may be empty */
    int32_t section;                /**< Section of the report, -1 for a baseline */
    int32_t depth;                  /**< Depth under the variants it depends on */
    box_cmd_ablate_target_t target; /**< What it changes */
    box_cmd_ablate_kind_t kind;     /**< Type of the member */
    int32_t offset;                 /**< Offset of the member, the index of an effect */
    float value;                    /**< Value written to the member */
    int32_t before;                 /**< Baseline timed before it */
    int32_t after;                  /**< Baseline timed after it */
    double median_ms;               /**< Median frame interval */
    double mean_ms;                 /**< Mean frame interval */
    int32_t frames;                 /**< Frames timed */
    int32_t stalls;                 /**< Frames that waited for a free render packet */
    int32_t settled_in;             /**< Frames it took to settle */
    bool unsettled;                 /**< Timed without settling */
    bool done;                      /**< Timed */
    bool warmup;                    /**< A baseline that only warms up, not reported */
    int32_t variant;                /**< Index of the variant in its round, -1 for a baseline */
    int32_t round;                  /**< Round of the step */
} box_cmd_ablate_step_t;

/** State of the ablate command */
typedef struct box_cmd_ablate_t {
    ecs_world_t *world;             /**< The world, to free the values with */
    box_cmd_ablate_phase_t phase;   /**< Where the run is */
    ecs_entity_t entities[3];       /**< View, entity with the Scene, Surface; 0 for none */
    ecs_entity_t types[3];          /**< The components of entities */
    ecs_value_t orig[3];            /**< The values before the run */
    ecs_value_t base[3];            /**< The values the steps start from */
    bool touched[3];                /**< The run wrote the component */
    ecs_entity_t camera;            /**< Camera of the view */
    float app_fps;                  /**< App.fps before the run */
    bool app_unpaced;               /**< App.unpaced before the run */
    ecs_vec_t steps;                /**< box_cmd_ablate_step_t */
    ecs_vec_t samples;              /**< Frame intervals of the step in ms */
    int32_t current;                /**< Step being settled or timed */
    int32_t stable;                 /**< Settled frames in a row */
    int32_t waited;                 /**< Frames since the step was applied */
    int32_t pipelines;              /**< Pipelines the GPU created at the last frame */
    int32_t compiles;               /**< Shader modules the GPU compiled at the last frame */
    int32_t bake_left;              /**< Probes the bakes left at the last frame */
    int32_t bake_stalled;           /**< Frames the bakes left the same probes */
    int32_t stalls;                 /**< RenderStatus.stalls when the timing began */
    int32_t frames;                 /**< Frames each step is timed over */
    int32_t settle;                 /**< Settled frames in a row before a step is timed */
    int32_t rounds;                 /**< Times every variant is timed */
    int32_t variants;               /**< Variants of a round */
    double mark;                    /**< Clock at the end of the last frame */
    double started;                 /**< Clock when the run started */
    double elapsed;                 /**< Seconds the run took */
} box_cmd_ablate_t;

/** What the rounds of a variant measured */
typedef struct box_cmd_ablate_result_t {
    const box_cmd_ablate_step_t *step; /**< The variant in the first round */
    double median_ms;       /**< Median over the rounds of its median frame interval */
    double base_ms;         /**< Median over the rounds of its baseline */
    double delta_ms;        /**< Median over the rounds of its difference from its baseline */
    double noise_ms;        /**< Median over the rounds of its noise */
    int32_t rounds;         /**< Rounds that timed it */
    bool unsettled;         /**< A round timed it without settling */
} box_cmd_ablate_result_t;

/** Names of the sections of the report */
static const char *box_cmd_ablate_sections[] = {
    "Resolution", "Textures", "Lighting", "Effects", "Scene features"
};

/** Scene members the lighting section toggles under the view settings they depend on */
static const char *box_cmd_ablate_lit_members[] = {
    "baked", "reflections", "punctual_shadows", "cookies", "environment",
    "aerial_perspective", "planet_light", NULL
};

/** Read the clock.
 *
 * @return Seconds since an arbitrary epoch
 */
static double box_cmd_ablate_now(void)
{
    ecs_time_t t;
    ecs_os_get_time(&t);

    return (double)t.sec + (double)t.nanosec * 1e-9;
}

/** Get the difference from its baseline below which a variant is noise: the drift between the
 * baselines around it, at least BOX_CMD_ABLATE_NOISE of the baseline.
 *
 * @param steps The steps
 * @param step The variant
 * @param base The baseline of the variant in ms
 * @return The difference in ms
 */
static double box_cmd_ablate_noise(
    const box_cmd_ablate_step_t *steps,
    const box_cmd_ablate_step_t *step,
    double base)
{
    const box_cmd_ablate_step_t *before = &steps[step->before];
    const box_cmd_ablate_step_t *after = &steps[step->after];
    double drift = after->done ? fabs(after->median_ms - before->median_ms) : 0;

    return fmax(drift, base * BOX_CMD_ABLATE_NOISE);
}

/** Get the name of an entity for the report.
 *
 * @param world The world
 * @param e The entity
 * @return The name, "?" when it is not alive or has none
 */
static const char* box_cmd_ablate_name(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const char *result = e && ecs_is_alive(world, e) ? ecs_get_name(world, e) : NULL;

    return result ? result : "?";
}

/** Get the component of a target.
 *
 * @param target The target
 * @return Index into the components of the state, -1 for none
 */
static int32_t box_cmd_ablate_comp(
    box_cmd_ablate_target_t target)
{
    int32_t result = -1;
    if (target == BoxCmdAblateView || target == BoxCmdAblateEffect) {
        result = 0;
    } else if (target == BoxCmdAblateScene) {
        result = 1;
    } else if (target == BoxCmdAblateSurface) {
        result = 2;
    }

    return result;
}

/** Free a copy of a component, with its hooks while its type is still registered.
 *
 * @param world The world
 * @param v The copy, its ptr NULL for none
 */
static void box_cmd_ablate_value_free(
    ecs_world_t *world,
    ecs_value_t *v)
{
    if (v->ptr && ecs_is_alive(world, v->type) && ecs_get_type_info(world, v->type)) {
        ecs_value_fini(world, v);
    } else if (v->ptr) {
        ecs_os_free(v->ptr);
    }

    v->ptr = NULL;
}

/** Free the copies of the components of a run, once they are put back.
 *
 * @param world The world
 * @param abl The state
 */
static void box_cmd_ablate_release(
    ecs_world_t *world,
    box_cmd_ablate_t *abl)
{
    int32_t i;
    for (i = 0; i < BOX_CMD_ABLATE_COMPONENTS; i ++) {
        box_cmd_ablate_value_free(world, &abl->orig[i]);
        box_cmd_ablate_value_free(world, &abl->base[i]);
    }
}

/** Release the values and the steps of a run.
 *
 * @param world The world
 * @param abl The state
 */
static void box_cmd_ablate_clear(
    ecs_world_t *world,
    box_cmd_ablate_t *abl)
{
    int32_t i;
    box_cmd_ablate_release(world, abl);
    for (i = 0; i < BOX_CMD_ABLATE_COMPONENTS; i ++) {
        abl->entities[i] = 0;
        abl->touched[i] = false;
    }

    ecs_vec_clear(&abl->steps);
    ecs_vec_clear(&abl->samples);
}

/** Free the state.
 *
 * @param ctx The state
 */
static void box_cmd_ablate_free(
    void *ctx)
{
    box_cmd_ablate_t *abl = ctx;
    box_cmd_ablate_release(abl->world, abl);
    ecs_vec_fini_t(NULL, &abl->steps, box_cmd_ablate_step_t);
    ecs_vec_fini_t(NULL, &abl->samples, double);
    ecs_os_free(abl);
}

/** Write the change of a step into a value of its component.
 *
 * @param step The step
 * @param ptr The value
 */
static void box_cmd_ablate_mutate(
    const box_cmd_ablate_step_t *step,
    void *ptr)
{
    void *member = ECS_OFFSET(ptr, step->offset);
    if (step->target == BoxCmdAblateEffect) {
        View *view = ptr;
        if (step->offset < ecs_vec_count(&view->effects)) {
            ecs_vec_get_t(&view->effects, ViewEffect, step->offset)->enabled = false;
        }
    } else if (step->kind == BoxCmdAblateBool) {
        *(bool*)member = step->value != 0;
    } else if (step->kind == BoxCmdAblateFloat) {
        *(float*)member = step->value;
    } else {
        *(int32_t*)member = (int32_t)step->value;
    }
}

/** Set a component to the value the steps start from, changed by a step that targets it.
 *
 * @param world The world
 * @param abl The state
 * @param comp Index of the component
 * @param step The step, NULL for the value the steps start from
 */
static void box_cmd_ablate_write(
    ecs_world_t *world,
    box_cmd_ablate_t *abl,
    int32_t comp,
    const box_cmd_ablate_step_t *step)
{
    ecs_entity_t e = abl->entities[comp];
    ecs_entity_t type = abl->types[comp];
    const ecs_type_info_t *ti = ecs_get_type_info(world, type);
    ecs_value_t value;
    if (!e || !ti || !ecs_is_alive(world, e) || !abl->base[comp].ptr) {
        goto done;
    }

    value = ecs_value_init(world, type, abl->base[comp].ptr);
    if (step && box_cmd_ablate_comp(step->target) == comp) {
        box_cmd_ablate_mutate(step, value.ptr);
    }

    ecs_set_id(world, e, type, (size_t)ti->size, value.ptr);
    ecs_value_fini(world, &value);
    abl->touched[comp] = true;
done:
    return;
}

/** Undo the change of one step and make the change of the next.
 *
 * @param world The world
 * @param abl The state
 * @param from The step whose change is in place, NULL for none
 * @param to The step to change to
 */
static void box_cmd_ablate_apply(
    ecs_world_t *world,
    box_cmd_ablate_t *abl,
    const box_cmd_ablate_step_t *from,
    const box_cmd_ablate_step_t *to)
{
    int32_t from_comp = from ? box_cmd_ablate_comp(from->target) : -1;
    int32_t to_comp = box_cmd_ablate_comp(to->target);
    int32_t i;
    for (i = 0; i < BOX_CMD_ABLATE_COMPONENTS; i ++) {
        if (i == from_comp || i == to_comp) {
            box_cmd_ablate_write(world, abl, i, to);
        }
    }
}

/** Put back the components and the frame rate cap as they were before the run and free the
 * copies of the components.
 *
 * @param world The world
 * @param abl The state
 */
static void box_cmd_ablate_restore(
    ecs_world_t *world,
    box_cmd_ablate_t *abl)
{
    App *app = ecs_singleton_get_mut(world, App);
    int32_t i;
    for (i = 0; i < BOX_CMD_ABLATE_COMPONENTS; i ++) {
        ecs_entity_t e = abl->entities[i];
        const ecs_type_info_t *ti = ecs_get_type_info(world, abl->types[i]);
        if (abl->touched[i] && e && ti && ecs_is_alive(world, e) && abl->orig[i].ptr) {
            ecs_set_id(world, e, abl->types[i], (size_t)ti->size, abl->orig[i].ptr);
        }

        abl->touched[i] = false;
    }

    box_cmd_ablate_release(world, abl);
    if (app) {
        app->fps = abl->app_fps;
        app->unpaced = abl->app_unpaced;
        ecs_singleton_modified(world, App);
    }
}

/** Append a step to the run.
 *
 * @param abl The state
 * @param section Section of the report, -1 for a baseline
 * @param depth Depth under the variants it depends on
 * @param target What it changes
 * @param kind Type of the member
 * @param offset Offset of the member, the index of an effect
 * @param value Value written to the member
 * @param label What it changes
 * @param note What else it changes or what it is, NULL for nothing
 */
static void box_cmd_ablate_push(
    box_cmd_ablate_t *abl,
    int32_t section,
    int32_t depth,
    box_cmd_ablate_target_t target,
    box_cmd_ablate_kind_t kind,
    size_t offset,
    float value,
    const char *label,
    const char *note)
{
    box_cmd_ablate_step_t *step = ecs_vec_append_t(NULL, &abl->steps, box_cmd_ablate_step_t);
    ecs_os_memset_t(step, 0, box_cmd_ablate_step_t);
    step->section = section;
    step->depth = depth;
    step->target = target;
    step->kind = kind;
    step->offset = (int32_t)offset;
    step->value = value;
    ecs_os_snprintf(step->label, ECS_SIZEOF(step->label), "%s", label);
    ecs_os_snprintf(step->note, ECS_SIZEOF(step->note), "%s", note ? note : "");
}

/** Add a baseline to the run when variants were added after the last one.
 *
 * @param abl The state
 */
static void box_cmd_ablate_baseline(
    box_cmd_ablate_t *abl)
{
    int32_t count = ecs_vec_count(&abl->steps);
    const box_cmd_ablate_step_t *last = count ?
        ecs_vec_get_t(&abl->steps, box_cmd_ablate_step_t, count - 1) : NULL;
    if (!last || last->section >= 0 || last->warmup) {
        box_cmd_ablate_push(abl, -1, 0, BoxCmdAblateNone, BoxCmdAblateBool, 0, 0, "baseline",
            NULL);
    }
}

/** Add a variant to the run, after a baseline when BOX_CMD_ABLATE_BASELINE_EVERY variants
 * were added since the last one.
 *
 * @param abl The state
 * @param section Section of the report
 * @param depth Depth under the variants it depends on
 * @param target What it changes
 * @param kind Type of the member
 * @param offset Offset of the member, the index of an effect
 * @param value Value written to the member
 * @param label What it changes
 * @param note What else it changes or what it is, NULL for nothing
 */
static void box_cmd_ablate_add(
    box_cmd_ablate_t *abl,
    int32_t section,
    int32_t depth,
    box_cmd_ablate_target_t target,
    box_cmd_ablate_kind_t kind,
    size_t offset,
    float value,
    const char *label,
    const char *note)
{
    const box_cmd_ablate_step_t *steps = ecs_vec_first_t(&abl->steps, box_cmd_ablate_step_t);
    int32_t i = ecs_vec_count(&abl->steps) - 1, since = 0;
    for (; i >= 0 && steps[i].section >= 0; i --) {
        since ++;
    }

    if (since >= BOX_CMD_ABLATE_BASELINE_EVERY) {
        box_cmd_ablate_baseline(abl);
    }

    box_cmd_ablate_push(abl, section, depth, target, kind, offset, value, label, note);
}

/** Plan the resolution scale and the texture filtering and quality of the surface.
 *
 * @param abl The state
 */
static void box_cmd_ablate_plan_surface(
    box_cmd_ablate_t *abl)
{
    static const Anisotropy anisotropy[] = {
        AnisotropyOff, AnisotropyLow, AnisotropyMedium, AnisotropyHigh, AnisotropyUltra };
    static const char *anisotropy_names[] = { "Off", "Low", "Medium", "High", "Ultra" };
    static const TextureQuality quality[] = {
        TextureQualityLow, TextureQualityMedium, TextureQualityHigh, TextureQualityVeryHigh };
    static const char *quality_names[] = { "Low", "Medium", "High", "VeryHigh" };
    const Surface *s = abl->orig[2].ptr;
    char label[48];
    int32_t i;
    if (!s) {
        goto done;
    }

    if ((s->resolution_scale > 0 ? s->resolution_scale : 1.0f) < 2) {
        box_cmd_ablate_add(abl, 0, 0, BoxCmdAblateSurface, BoxCmdAblateFloat,
            offsetof(Surface, resolution_scale), 2, "resolution_scale 2",
            "a quarter of the pixels");
    }

    box_cmd_ablate_baseline(abl);
    for (i = 0; i < 5; i ++) {
        Anisotropy current = s->anisotropy ? s->anisotropy : AnisotropyMedium;
        if (anisotropy[i] != current) {
            ecs_os_snprintf(label, ECS_SIZEOF(label), "anisotropy %s", anisotropy_names[i]);
            box_cmd_ablate_add(abl, 1, 0, BoxCmdAblateSurface, BoxCmdAblateInt,
                offsetof(Surface, anisotropy), (float)anisotropy[i], label, NULL);
        }
    }

    for (i = 0; i < 4; i ++) {
        TextureQuality current = s->texture_quality ? s->texture_quality : TextureQualityVeryHigh;
        if (quality[i] != current) {
            ecs_os_snprintf(label, ECS_SIZEOF(label), "texture_quality %s", quality_names[i]);
            box_cmd_ablate_add(abl, 1, 0, BoxCmdAblateSurface, BoxCmdAblateInt,
                offsetof(Surface, texture_quality), (float)quality[i], label,
                "loads the textures again");
        }
    }

    box_cmd_ablate_baseline(abl);
done:
    return;
}

/** Test whether any entity has a component.
 *
 * @param world The world
 * @param id The component, 0 when its module is not imported
 * @return Whether an entity has it
 */
static bool box_cmd_ablate_any(
    ecs_world_t *world,
    ecs_id_t id)
{
    return id && ecs_count_id(world, id) > 0;
}

/** Test whether the view has an enabled effect with a component.
 *
 * @param world The world
 * @param view The view
 * @param id The component, 0 when its module is not imported
 * @return Whether an enabled effect of the view has it
 */
static bool box_cmd_ablate_has_effect(
    ecs_world_t *world,
    const View *view,
    ecs_id_t id)
{
    bool result = false;
    int32_t i;
    for (i = 0; id && i < ecs_vec_count(&view->effects); i ++) {
        const ViewEffect *e = ecs_vec_get_t(&view->effects, ViewEffect, i);
        result = result || (e->enabled && e->effect && ecs_is_alive(world, e->effect) &&
            ecs_has_id(world, e->effect, id));
    }

    return result;
}

/** Plan the view toggle of a feature the Scene enables and the Scene member under it.
 *
 * @param abl The state
 * @param on The view setting is on and the scene has the feature
 * @param scene_on The Scene member is on
 * @param view_offset Offset of the view setting in View
 * @param scene_offset Offset of the member in Scene
 * @param label Label of the view toggle
 * @param scene_label Label of the Scene toggle
 */
static void box_cmd_ablate_plan_pair(
    box_cmd_ablate_t *abl,
    bool on,
    bool scene_on,
    size_t view_offset,
    size_t scene_offset,
    const char *label,
    const char *scene_label)
{
    if (on) {
        box_cmd_ablate_add(abl, 2, 1, BoxCmdAblateView, BoxCmdAblateBool, view_offset, 0,
            label, NULL);
        if (scene_on) {
            box_cmd_ablate_add(abl, 2, 2, BoxCmdAblateScene, BoxCmdAblateBool, scene_offset,
                0, scene_label, "shader feature");
        }
    }
}

/** Plan the lighting of the view: lighting as a whole, then under it the shadows, the depth
 * prepass, the baked light, the reflections, the atmosphere, the fog volumes, the environment
 * and the lighting features of the Scene, each only when it is on and the scene has it.
 *
 * @param world The world
 * @param abl The state
 */
static void box_cmd_ablate_plan_lighting(
    ecs_world_t *world,
    box_cmd_ablate_t *abl)
{
    const View *v = abl->orig[0].ptr;
    const Scene none = {0};
    const Scene *s = abl->orig[1].ptr ? abl->orig[1].ptr : &none;
    const LightSettings *l = &v->lighting;
    bool atmosphere = box_cmd_ablate_any(world, ecs_id(Atmosphere)) ||
        box_cmd_ablate_any(world, ecs_id(PlanetAtmosphere));
    bool environment = l->environment.source != 0;
    if (!l->enabled) {
        goto done;
    }

    box_cmd_ablate_add(abl, 2, 0, BoxCmdAblateView, BoxCmdAblateBool,
        offsetof(View, lighting.enabled), 0, "lighting off", "and everything under it");
    box_cmd_ablate_plan_pair(abl, v->shadows.enabled, s->punctual_shadows,
        offsetof(View, shadows.enabled), offsetof(Scene, punctual_shadows),
        "shadows off", "Scene.punctual_shadows off");
    if (l->depth_prepass) {
        box_cmd_ablate_add(abl, 2, 1, BoxCmdAblateView, BoxCmdAblateBool,
            offsetof(View, lighting.depth_prepass), 0, "depth_prepass off",
            box_cmd_ablate_has_effect(world, v, ecs_id(Gtao)) ? "Gtao needs it, off too" : NULL);
    }

    box_cmd_ablate_plan_pair(abl, l->baked && s->baked, true,
        offsetof(View, lighting.baked), offsetof(Scene, baked),
        "baked off", "Scene.baked off");
    box_cmd_ablate_plan_pair(abl, l->reflections && s->reflections, true,
        offsetof(View, lighting.reflections), offsetof(Scene, reflections),
        "reflections off", "Scene.reflections off");
    box_cmd_ablate_plan_pair(abl, l->atmosphere && atmosphere, s->aerial_perspective,
        offsetof(View, lighting.atmosphere), offsetof(Scene, aerial_perspective),
        "atmosphere off", "Scene.aerial_perspective off");
    box_cmd_ablate_plan_pair(abl, l->fog_volumes && box_cmd_ablate_any(world, ecs_id(FogVolume)),
        false, offsetof(View, lighting.fog_volumes), 0, "fog_volumes off", NULL);
    box_cmd_ablate_plan_pair(abl, environment && l->environment.background, false,
        offsetof(View, lighting.environment.background), 0, "environment.background off", NULL);
    if (environment && s->environment) {
        box_cmd_ablate_add(abl, 2, 1, BoxCmdAblateScene, BoxCmdAblateBool,
            offsetof(Scene, environment), 0, "Scene.environment off", "hemisphere ambient");
    }

    if (s->cookies) {
        box_cmd_ablate_add(abl, 2, 1, BoxCmdAblateScene, BoxCmdAblateBool,
            offsetof(Scene, cookies), 0, "Scene.cookies off", NULL);
    }

    if (s->planet_light) {
        box_cmd_ablate_add(abl, 2, 1, BoxCmdAblateScene, BoxCmdAblateBool,
            offsetof(Scene, planet_light), 0, "Scene.planet_light off", NULL);
    }

    box_cmd_ablate_baseline(abl);
done:
    return;
}

/** Get the name of the effect component of an effect entity: its first component of the
 * box.effects module.
 *
 * @param world The world
 * @param effect The effect entity
 * @return The name, NULL when it has none
 */
static const char* box_cmd_ablate_effect_kind(
    ecs_world_t *world,
    ecs_entity_t effect)
{
    ecs_entity_t module = ecs_lookup(world, "box.effects");
    const ecs_type_t *type = ecs_get_type(world, effect);
    const char *result = NULL;
    int32_t i;
    for (i = 0; module && type && i < type->count && !result; i ++) {
        ecs_id_t id = type->array[i];
        if (!ECS_IS_PAIR(id) && ecs_is_alive(world, id) && ecs_get_parent(world, id) == module) {
            result = ecs_get_name(world, id);
        }
    }

    return result;
}

/** Plan the effects of the view: each enabled effect off on its own.
 *
 * @param world The world
 * @param abl The state
 */
static void box_cmd_ablate_plan_effects(
    ecs_world_t *world,
    box_cmd_ablate_t *abl)
{
    const View *v = abl->orig[0].ptr;
    char label[48];
    int32_t i;
    for (i = 0; i < ecs_vec_count(&v->effects); i ++) {
        const ViewEffect *e = ecs_vec_get_t(&v->effects, ViewEffect, i);
        const char *name = e->effect && ecs_is_alive(world, e->effect) ?
            ecs_get_name(world, e->effect) : NULL;
        if (!e->enabled || !e->effect || !ecs_is_alive(world, e->effect)) {
            continue;
        }

        if (name) {
            ecs_os_snprintf(label, ECS_SIZEOF(label), "%s off", name);
        } else {
            ecs_os_snprintf(label, ECS_SIZEOF(label), "effects[%d] off", i);
        }

        box_cmd_ablate_add(abl, 3, 0, BoxCmdAblateEffect, BoxCmdAblateBool, (size_t)i, 0,
            label, box_cmd_ablate_effect_kind(world, e->effect));
    }

    box_cmd_ablate_baseline(abl);
}

/** Test whether a Scene member is toggled by the lighting section.
 *
 * @param name The name of the member
 * @return Whether the lighting section toggles it
 */
static bool box_cmd_ablate_lit_member(
    const char *name)
{
    bool result = false;
    int32_t i;
    for (i = 0; box_cmd_ablate_lit_members[i] && !result; i ++) {
        result = !ecs_os_strcmp(name, box_cmd_ablate_lit_members[i]);
    }

    return result;
}

/** Plan the bool members of a struct at an offset in the Scene that are on, each toggled off.
 *
 * @param world The world
 * @param abl The state
 * @param type The struct type
 * @param base Offset of the struct in Scene
 * @param prefix Prefix of the labels
 */
static void box_cmd_ablate_plan_bools(
    ecs_world_t *world,
    box_cmd_ablate_t *abl,
    ecs_entity_t type,
    size_t base,
    const char *prefix)
{
    const Scene *s = abl->orig[1].ptr;
    const EcsStruct *st = ecs_get(world, type, EcsStruct);
    const ecs_member_t *members = st ? ecs_vec_first_t(&st->members, ecs_member_t) : NULL;
    int32_t i, count = st ? ecs_vec_count(&st->members) : 0;
    char label[48];
    for (i = 0; s && i < count; i ++) {
        const ecs_member_t *m = &members[i];
        size_t offset = base + (size_t)m->offset;
        if (m->type != ecs_id(ecs_bool_t) || (!base && box_cmd_ablate_lit_member(m->name)) ||
            !*(const bool*)ECS_OFFSET(s, offset))
        {
            continue;
        }

        ecs_os_snprintf(label, ECS_SIZEOF(label), "%s.%s off", prefix, m->name);
        box_cmd_ablate_add(abl, 4, 0, BoxCmdAblateScene, BoxCmdAblateBool, offset, 0, label,
            NULL);
    }
}

/** Plan the shader features of the Scene of the camera that do not depend on the lighting:
 * every bool member that is on, and those of the material features of each shader.
 *
 * @param world The world
 * @param abl The state
 */
static void box_cmd_ablate_plan_scene(
    ecs_world_t *world,
    box_cmd_ablate_t *abl)
{
    box_cmd_ablate_plan_bools(world, abl, ecs_id(Scene), 0, "Scene");
    box_cmd_ablate_plan_bools(world, abl, ecs_id(ShaderFeatures),
        offsetof(Scene, shader_features.Extended), "Extended");
    box_cmd_ablate_plan_bools(world, abl, ecs_id(ShaderFeatures),
        offsetof(Scene, shader_features.Terrain), "Terrain");
    box_cmd_ablate_baseline(abl);
}

/** Link every variant to the baselines timed before and after it.
 *
 * @param abl The state
 */
static void box_cmd_ablate_link(
    box_cmd_ablate_t *abl)
{
    box_cmd_ablate_step_t *steps = ecs_vec_first_t(&abl->steps, box_cmd_ablate_step_t);
    int32_t i, count = ecs_vec_count(&abl->steps), last = 0;
    for (i = 0; i < count; i ++) {
        last = steps[i].section < 0 && !steps[i].warmup ? i : last;
        steps[i].before = last;
    }

    for (i = count - 1; i >= 0; i --) {
        last = steps[i].section < 0 && !steps[i].warmup ? i : last;
        steps[i].after = last;
    }
}

/** Plan the steps of a run: a warmup and a baseline, then every round of the sections, each
 * section with a baseline after it.
 *
 * @param world The world
 * @param abl The state
 */
static void box_cmd_ablate_plan(
    ecs_world_t *world,
    box_cmd_ablate_t *abl)
{
    box_cmd_ablate_step_t *steps;
    int32_t r, i, first, variant;
    box_cmd_ablate_push(abl, -1, 0, BoxCmdAblateNone, BoxCmdAblateBool, 0, 0, "warmup", NULL);
    ecs_vec_last_t(&abl->steps, box_cmd_ablate_step_t)->warmup = true;
    box_cmd_ablate_baseline(abl);
    for (r = 0; r < abl->rounds; r ++) {
        first = ecs_vec_count(&abl->steps);
        box_cmd_ablate_plan_surface(abl);
        box_cmd_ablate_plan_lighting(world, abl);
        box_cmd_ablate_plan_effects(world, abl);
        box_cmd_ablate_plan_scene(world, abl);
        steps = ecs_vec_first_t(&abl->steps, box_cmd_ablate_step_t);
        for (i = first, variant = 0; i < ecs_vec_count(&abl->steps); i ++) {
            steps[i].round = r;
            steps[i].variant = steps[i].section >= 0 ? variant ++ : -1;
        }

        abl->variants = variant;
    }

    box_cmd_ablate_link(abl);
}

/** Check whether a light or reflection bake that can progress is still baking. A bake that
 * leaves the same probes for BOX_CMD_ABLATE_BAKE_STALL frames is given up on.
 *
 * @param world The world
 * @param abl The state
 * @return Whether a bake is progressing
 */
static bool box_cmd_ablate_baking(
    ecs_world_t *world,
    box_cmd_ablate_t *abl)
{
    int32_t left = box_render_bake_pending(world) + box_render_reflect_pending(world);
    bool result = false;
    if (!left) {
        abl->bake_left = 0;
        abl->bake_stalled = 0;
    } else if (left != abl->bake_left) {
        abl->bake_left = left;
        abl->bake_stalled = 0;
        result = true;
    } else {
        result = ++ abl->bake_stalled < BOX_CMD_ABLATE_BAKE_STALL;
    }

    return result;
}

/** Check whether the frame rendered the scene as it settled: no blocking task such as a shader
 * compile or a load, no pipeline created or being created, no shader compiled, no bake.
 *
 * @param world The world
 * @param abl The state
 * @return Whether the frame is settled
 */
static bool box_cmd_ablate_stable(
    ecs_world_t *world,
    box_cmd_ablate_t *abl)
{
    const InProgressTask *tasks = ecs_id(InProgressTask) ?
        ecs_singleton_get(world, InProgressTask) : NULL;
    box_render_gpu_stats_t stats;
    bool result = !tasks || !tasks->blocking;
    if (ecs_id(RenderStatus) && box_render_gpu_stats(world, &stats)) {
        result = result && !stats.pipelines_building && stats.pipelines == abl->pipelines &&
            stats.compiles == abl->compiles;
        abl->pipelines = stats.pipelines;
        abl->compiles = stats.compiles;
    }

    if (ecs_id(RenderStatus)) {
        result = !box_cmd_ablate_baking(world, abl) && result;
    }

    return result;
}

/** Get the frames since the start that waited for a free render packet.
 *
 * @param world The world
 * @return The frames, 0 without box.render
 */
static int32_t box_cmd_ablate_stalls(
    ecs_world_t *world)
{
    const RenderStatus *r = ecs_id(RenderStatus) ?
        ecs_singleton_get(world, RenderStatus) : NULL;

    return r ? r->stalls : 0;
}

/** Compare two frame intervals for qsort.
 *
 * @param a The first interval
 * @param b The second interval
 * @return Negative when a is shorter, positive when longer, 0 when equal
 */
static int box_cmd_ablate_cmp(
    const void *a,
    const void *b)
{
    double x = *(const double*)a, y = *(const double*)b;

    return (x > y) - (x < y);
}

/** Get the median of values, which it sorts.
 *
 * @param values The values
 * @param count Number of values
 * @return The median, 0 for no values
 */
static double box_cmd_ablate_median(
    double *values,
    int32_t count)
{
    double result = 0;
    if (count) {
        qsort(values, (size_t)count, sizeof(double), box_cmd_ablate_cmp);
        result = count % 2 ? values[count / 2] : (values[count / 2 - 1] + values[count / 2]) / 2;
    }

    return result;
}

/** Store what the frames of the step measured.
 *
 * @param world The world
 * @param abl The state
 * @param step The step
 */
static void box_cmd_ablate_store(
    ecs_world_t *world,
    box_cmd_ablate_t *abl,
    box_cmd_ablate_step_t *step)
{
    double *samples = ecs_vec_first_t(&abl->samples, double);
    int32_t i, count = ecs_vec_count(&abl->samples);
    double sum = 0;
    for (i = 0; i < count; i ++) {
        sum += samples[i];
    }

    step->frames = count;
    step->mean_ms = count ? sum / count : 0;
    step->median_ms = box_cmd_ablate_median(samples, count);
    step->stalls = box_cmd_ablate_stalls(world) - abl->stalls;
    step->done = true;
}

/** Get the frames a step is timed over: those of the run, more for the warmup.
 *
 * @param abl The state
 * @param step The step
 * @return The frames
 */
static int32_t box_cmd_ablate_frames(
    const box_cmd_ablate_t *abl,
    const box_cmd_ablate_step_t *step)
{
    int32_t result = abl->frames;
    if (step->warmup) {
        result = abl->frames * 2 > BOX_CMD_ABLATE_WARMUP ? abl->frames * 2 :
            BOX_CMD_ABLATE_WARMUP;
    }

    return result;
}

/** Finish the step that was timed and apply the next, or end the run after the last.
 *
 * @param world The world
 * @param abl The state
 */
static void box_cmd_ablate_next(
    ecs_world_t *world,
    box_cmd_ablate_t *abl)
{
    box_cmd_ablate_step_t *steps = ecs_vec_first_t(&abl->steps, box_cmd_ablate_step_t);
    int32_t count = ecs_vec_count(&abl->steps);
    box_cmd_ablate_store(world, abl, &steps[abl->current]);
    abl->current ++;
    if (abl->current >= count) {
        box_cmd_ablate_restore(world, abl);
        abl->phase = BoxCmdAblateDone;
        abl->elapsed = box_cmd_ablate_now() - abl->started;
        ecs_trace("ablate: done after %.0f s, run ablate for the report", abl->elapsed);
    } else {
        box_cmd_ablate_apply(world, abl, &steps[abl->current - 1], &steps[abl->current]);
        abl->phase = BoxCmdAblateSettle;
        abl->stable = 0;
        abl->waited = 0;
        abl->bake_stalled = 0;
    }
}

/** Test whether the entities of a run are still alive.
 *
 * @param world The world
 * @param abl The state
 * @return Whether the view and the camera are alive
 */
static bool box_cmd_ablate_alive(
    ecs_world_t *world,
    box_cmd_ablate_t *abl)
{
    return ecs_is_alive(world, abl->entities[0]) && ecs_has(world, abl->entities[0], View) &&
        ecs_is_alive(world, abl->camera);
}

/** Settle and time the step of a run at the end of every frame. A frame of the timing that is
 * not settled starts the settling over, until the step waited BOX_CMD_ABLATE_TIMEOUT frames.
 *
 * @param it The iterator of the system
 */
static void BoxCmdAblate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    box_cmd_ablate_t *abl = it->ctx;
    double now = box_cmd_ablate_now();
    double interval = abl->mark > 0 ? (now - abl->mark) * 1000.0 : 0;
    box_cmd_ablate_step_t *step;
    bool stable;
    abl->mark = now;
    if (abl->phase != BoxCmdAblateSettle && abl->phase != BoxCmdAblateMeasure) {
        goto done;
    }

    if (!box_cmd_ablate_alive(world, abl)) {
        ecs_err("ablate: the view or its camera was deleted, stopped the run");
        box_cmd_ablate_restore(world, abl);
        abl->phase = BoxCmdAblateStopped;
        goto done;
    }

    step = ecs_vec_get_t(&abl->steps, box_cmd_ablate_step_t, abl->current);
    stable = box_cmd_ablate_stable(world, abl);
    abl->waited ++;
    if (abl->phase == BoxCmdAblateSettle) {
        abl->stable = stable ? abl->stable + 1 : 0;
        if (abl->stable >= abl->settle || abl->waited >= BOX_CMD_ABLATE_TIMEOUT) {
            step->unsettled = abl->stable < abl->settle;
            step->settled_in = abl->waited;
            abl->phase = BoxCmdAblateMeasure;
            abl->stalls = box_cmd_ablate_stalls(world);
            ecs_vec_clear(&abl->samples);
        }
    } else if (!stable && abl->waited < BOX_CMD_ABLATE_TIMEOUT) {
        abl->phase = BoxCmdAblateSettle;
        abl->stable = 0;
    } else {
        ecs_vec_append_t(NULL, &abl->samples, double)[0] = interval;
        if (ecs_vec_count(&abl->samples) >= box_cmd_ablate_frames(abl, step)) {
            box_cmd_ablate_next(world, abl);
        }
    }

done:
    return;
}

/** Find the entity whose Scene holds the features of the scene of a camera: its nearest
 * ancestor with Scene, else the Scene singleton.
 *
 * @param world The world
 * @param camera The camera
 * @return The entity, 0 when the scene has no Scene
 */
static ecs_entity_t box_cmd_ablate_scene(
    ecs_world_t *world,
    ecs_entity_t camera)
{
    ecs_entity_t result = ecs_get_target_for_id(world, camera, EcsChildOf, ecs_id(Scene));
    if (!result && ecs_has(world, ecs_id(Scene), Scene)) {
        result = ecs_id(Scene);
    }

    return result;
}

/** Copy the components of a run before it changes them: the View, the Scene of its camera and
 * the Surface it renders to. The steps start from them with the vsync of the surface off.
 *
 * @param world The world
 * @param abl The state
 * @param view The view
 */
static void box_cmd_ablate_snapshot(
    ecs_world_t *world,
    box_cmd_ablate_t *abl,
    ecs_entity_t view)
{
    int32_t i;
    abl->entities[0] = view;
    abl->entities[1] = box_cmd_ablate_scene(world, abl->camera);
    abl->entities[2] = box_surface_of_view(world, NULL, view);
    abl->types[0] = ecs_id(View);
    abl->types[1] = ecs_id(Scene);
    abl->types[2] = ecs_id(Surface);
    for (i = 0; i < BOX_CMD_ABLATE_COMPONENTS; i ++) {
        const void *ptr = abl->entities[i] ?
            ecs_get_id(world, abl->entities[i], abl->types[i]) : NULL;
        if (!ptr) {
            abl->entities[i] = 0;
            continue;
        }

        abl->orig[i] = ecs_value_init(world, abl->types[i], ptr);
        abl->base[i] = ecs_value_init(world, abl->types[i], ptr);
    }

    if (abl->base[2].ptr) {
        ((Surface*)abl->base[2].ptr)->vsync = false;
    }
}

/** Parse the options of the command.
 *
 * @param world The world
 * @param argc Number of options
 * @param argv The options
 * @param abl Receives frames and settle
 * @param view Receives the view of view=, 0 when not given
 * @param stop Receives whether stop was given
 * @return 0 on success, -1 with an error logged
 */
static int box_cmd_ablate_options(
    ecs_world_t *world,
    int32_t argc,
    const ecs_value_t *argv,
    box_cmd_ablate_t *abl,
    ecs_entity_t *view,
    bool *stop)
{
    int result = 0;
    int32_t i;
    abl->frames = BOX_CMD_ABLATE_FRAMES;
    abl->settle = BOX_CMD_ABLATE_SETTLE;
    abl->rounds = BOX_CMD_ABLATE_ROUNDS;
    for (i = 0; i < argc && !result; i ++) {
        const char *arg = *(const char**)argv[i].ptr;
        arg = arg ? arg : "";
        if (!ecs_os_strcmp(arg, "stop")) {
            *stop = true;
        } else if (!ecs_os_strcmp(arg, "start")) {
            continue;
        } else if (!ecs_os_strncmp(arg, "frames=", 7) && atoi(arg + 7) > 0) {
            abl->frames = atoi(arg + 7);
        } else if (!ecs_os_strncmp(arg, "settle=", 7) && atoi(arg + 7) > 0) {
            abl->settle = atoi(arg + 7);
        } else if (!ecs_os_strncmp(arg, "rounds=", 7) && atoi(arg + 7) > 0 &&
            atoi(arg + 7) <= BOX_CMD_ABLATE_ROUNDS_MAX)
        {
            abl->rounds = atoi(arg + 7);
        } else if (!ecs_os_strncmp(arg, "view=", 5)) {
            view[0] = ecs_lookup_from(world, box_command_scope(world), arg + 5);
            if (!view[0] || !ecs_has(world, view[0], View)) {
                ecs_err("ablate: '%s' is not a view", arg + 5);
                result = -1;
            }
        } else {
            ecs_err("ablate: unknown option '%s', expected start, stop, frames=n, settle=n, "
                "rounds=1..%d or view=path", arg, BOX_CMD_ABLATE_ROUNDS_MAX);
            result = -1;
        }
    }

    return result;
}

/** Start a run on a view: copy its components, uncap the frames and plan the steps.
 *
 * @param world The world
 * @param abl The state
 * @param view The view, 0 for the view of the active camera
 * @return The text, which the caller frees, NULL with an error logged
 */
static char* box_cmd_ablate_start(
    ecs_world_t *world,
    box_cmd_ablate_t *abl,
    ecs_entity_t view)
{
    App *app = ecs_singleton_get_mut(world, App);
    ecs_entity_t camera = view ? ecs_get(world, view, View)->camera :
        box_cmd_render_camera(world, 0, NULL);
    char *result = NULL;
    if (!app) {
        ecs_err("ablate: the world has no box.app.App");
        goto done;
    }

    if (!camera || !ecs_is_alive(world, camera)) {
        ecs_err("ablate: no view with a camera");
        goto done;
    }

    view = view ? view : box_cmd_render_view(world, camera, 0, false);
    if (!view) {
        ecs_err("ablate: no view of the camera");
        goto done;
    }

    box_cmd_ablate_clear(world, abl);
    abl->camera = camera;
    box_cmd_ablate_snapshot(world, abl, view);
    box_cmd_ablate_plan(world, abl);
    abl->app_fps = app->fps;
    abl->app_unpaced = app->unpaced;
    app->fps = BOX_CMD_ABLATE_UNCAPPED;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    if (abl->entities[2]) {
        box_cmd_ablate_write(world, abl, 2, NULL);
    }

    abl->phase = BoxCmdAblateSettle;
    abl->current = 0;
    abl->stable = 0;
    abl->waited = 0;
    abl->bake_left = 0;
    abl->bake_stalled = 0;
    abl->started = box_cmd_ablate_now();
    result = flecs_asprintf("ablate: started on %s, %d variants in %d rounds, %d steps of %d "
        "frames; run ablate for the progress and the report, ablate stop to stop\n",
        box_cmd_ablate_name(world, view), abl->variants, abl->rounds,
        ecs_vec_count(&abl->steps), abl->frames);
done:
    return result;
}

/** Describe the progress of a run.
 *
 * @param world The world
 * @param abl The state
 * @return The text, which the caller frees
 */
static char* box_cmd_ablate_progress(
    ecs_world_t *world,
    const box_cmd_ablate_t *abl)
{
    const box_cmd_ablate_step_t *step = ecs_vec_get_t(
        &abl->steps, box_cmd_ablate_step_t, abl->current);
    const InProgressTask *tasks = ecs_id(InProgressTask) ?
        ecs_singleton_get(world, InProgressTask) : NULL;
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_strbuf_append(&buf, "ablate: step %d of %d, round %d of %d, %s: ", abl->current + 1,
        ecs_vec_count(&abl->steps), step->round + 1, abl->rounds, step->label);
    if (abl->phase == BoxCmdAblateMeasure) {
        ecs_strbuf_append(&buf, "timing frame %d of %d", ecs_vec_count(&abl->samples),
            box_cmd_ablate_frames(abl, step));
    } else {
        ecs_strbuf_append(&buf, "settling, %d of %d frames settled, waited %d",
            abl->stable, abl->settle, abl->waited);
        if (tasks && tasks->count && tasks->tasks[0].name) {
            ecs_strbuf_append(&buf, " (%s)", tasks->tasks[0].name);
        }
    }

    ecs_strbuf_append(&buf, ", %.0f s\n", box_cmd_ablate_now() - abl->started);

    return ecs_strbuf_get(&buf);
}

/** Get the baseline a variant is compared to: the mean of the baselines before and after it.
 *
 * @param steps The steps
 * @param step The variant
 * @return The median frame interval of the baseline in ms
 */
static double box_cmd_ablate_base(
    const box_cmd_ablate_step_t *steps,
    const box_cmd_ablate_step_t *step)
{
    const box_cmd_ablate_step_t *before = &steps[step->before];
    const box_cmd_ablate_step_t *after = &steps[step->after];
    double result = before->median_ms;
    if (after->done && after != before) {
        result = (before->median_ms + after->median_ms) / 2;
    }

    return result;
}

/** Combine the rounds of every variant of a run.
 *
 * @param abl The state
 * @return One result per variant, abl->variants of them, which the caller frees
 */
static box_cmd_ablate_result_t* box_cmd_ablate_results(
    const box_cmd_ablate_t *abl)
{
    const box_cmd_ablate_step_t *steps = ecs_vec_first_t(&abl->steps, box_cmd_ablate_step_t);
    int32_t i, v, count = ecs_vec_count(&abl->steps);
    box_cmd_ablate_result_t *result = ecs_os_calloc_n(box_cmd_ablate_result_t,
        abl->variants ? abl->variants : 1);
    double m[BOX_CMD_ABLATE_ROUNDS_MAX], b[BOX_CMD_ABLATE_ROUNDS_MAX];
    double d[BOX_CMD_ABLATE_ROUNDS_MAX], n[BOX_CMD_ABLATE_ROUNDS_MAX];
    for (v = 0; v < abl->variants; v ++) {
        box_cmd_ablate_result_t *r = &result[v];
        for (i = 0; i < count; i ++) {
            const box_cmd_ablate_step_t *s = &steps[i];
            if (s->variant != v || s->section < 0) {
                continue;
            }

            r->step = r->step ? r->step : s;
            if (s->done && r->rounds < BOX_CMD_ABLATE_ROUNDS_MAX) {
                b[r->rounds] = box_cmd_ablate_base(steps, s);
                m[r->rounds] = s->median_ms;
                d[r->rounds] = s->median_ms - b[r->rounds];
                n[r->rounds] = box_cmd_ablate_noise(steps, s, b[r->rounds]);
                r->unsettled = r->unsettled || s->unsettled;
                r->rounds ++;
            }
        }

        r->median_ms = box_cmd_ablate_median(m, r->rounds);
        r->base_ms = box_cmd_ablate_median(b, r->rounds);
        r->delta_ms = box_cmd_ablate_median(d, r->rounds);
        r->noise_ms = box_cmd_ablate_median(n, r->rounds);
    }

    return result;
}

/** Append the header of the report: the view, the surface, the baselines and their drift.
 *
 * @param world The world
 * @param abl The state
 * @param buf The output
 */
static void box_cmd_ablate_header(
    ecs_world_t *world,
    const box_cmd_ablate_t *abl,
    ecs_strbuf_t *buf)
{
    const box_cmd_ablate_step_t *steps = ecs_vec_first_t(&abl->steps, box_cmd_ablate_step_t);
    const SurfaceStatus *ss = abl->entities[2] ?
        ecs_get(world, abl->entities[2], SurfaceStatus) : NULL;
    double lo = 0, hi = 0, sum = 0, drift;
    int32_t i, count = ecs_vec_count(&abl->steps), baselines = 0, stalls = 0, frames = 0;
    for (i = 0; i < count; i ++) {
        if (steps[i].section < 0 && steps[i].done && !steps[i].warmup) {
            lo = baselines ? fmin(lo, steps[i].median_ms) : steps[i].median_ms;
            hi = baselines ? fmax(hi, steps[i].median_ms) : steps[i].median_ms;
            sum += steps[i].median_ms;
            stalls += steps[i].stalls;
            frames += steps[i].frames;
            baselines ++;
        }
    }

    sum = baselines ? sum / baselines : 0;
    drift = hi - lo;
    ecs_strbuf_append(buf, "ablate %s (camera %s)",
        box_cmd_ablate_name(world, abl->entities[0]), box_cmd_ablate_name(world, abl->camera));
    if (ss) {
        ecs_strbuf_append(buf, ", %dx%d rendered at %dx%d", ss->width, ss->height,
            ss->render_width, ss->render_height);
    }

    ecs_strbuf_append(buf, ", %d rounds of %d frames per step, uncapped without vsync, %.0f s\n",
        abl->rounds, abl->frames, abl->elapsed);
    ecs_strbuf_append(buf, "baseline %.2f ms (%.1f fps), mean of %d baselines from %.2f to "
        "%.2f ms (%.1f%%)", sum, sum > 0 ? 1000.0 / sum : 0.0, baselines, lo, hi,
        sum > 0 ? 100.0 * drift / sum : 0.0);
    if (frames) {
        ecs_strbuf_append(buf, "; %.0f%% of their frames waited for the renderer (GPU bound)",
            100.0 * stalls / frames);
    }

    ecs_strbuf_append(buf, "\ndelta: the median over the rounds of the step minus the baselines "
        "around it, negative is faster, the time the feature costs; ~noise when within the "
        "drift of those baselines or %.0f%%\n", BOX_CMD_ABLATE_NOISE * 100);
}

/** Append the line of a variant.
 *
 * @param buf The output
 * @param r The rounds of the variant
 */
static void box_cmd_ablate_line(
    ecs_strbuf_t *buf,
    const box_cmd_ablate_result_t *r)
{
    int32_t indent = 2 + r->step->depth * 2;
    int32_t width = BOX_CMD_ABLATE_LABEL_WIDTH - indent;
    ecs_strbuf_append(buf, "%*s%-*s", indent, "", width, r->step->label);
    if (!r->rounds) {
        ecs_strbuf_appendstr(buf, "   not timed\n");
        goto done;
    }

    ecs_strbuf_append(buf, "%7.2f ms %+7.2f ms %+6.1f%%", r->median_ms, r->delta_ms,
        r->base_ms > 0 ? 100.0 * r->delta_ms / r->base_ms : 0.0);
    if (fabs(r->delta_ms) <= r->noise_ms) {
        ecs_strbuf_appendstr(buf, "  ~noise");
    }

    if (r->unsettled) {
        ecs_strbuf_appendstr(buf, "  unsettled");
    }

    if (r->step->note[0]) {
        ecs_strbuf_append(buf, "  (%s)", r->step->note);
    }

    ecs_strbuf_appendch(buf, '\n');
done:
    return;
}

/** Append the variants that save the most time beyond their noise, most first.
 *
 * @param buf The output
 * @param results The rounds of every variant
 * @param count Number of variants
 */
static void box_cmd_ablate_rank(
    ecs_strbuf_t *buf,
    const box_cmd_ablate_result_t *results,
    int32_t count)
{
    double last = -INFINITY;
    int32_t n, i;
    ecs_strbuf_appendstr(buf, "\nLargest costs\n");
    for (n = 0; n < BOX_CMD_ABLATE_RANKED; n ++) {
        const box_cmd_ablate_result_t *best = NULL;
        for (i = 0; i < count; i ++) {
            const box_cmd_ablate_result_t *r = &results[i];
            if (r->rounds && r->delta_ms < -r->noise_ms && r->delta_ms > last &&
                (!best || r->delta_ms < best->delta_ms))
            {
                best = r;
            }
        }

        if (!best) {
            break;
        }

        last = best->delta_ms;
        ecs_strbuf_append(buf, "  %2d. %-*s %6.2f ms  %s\n", n + 1,
            BOX_CMD_ABLATE_LABEL_WIDTH - 6, best->step->label, -best->delta_ms,
            box_cmd_ablate_sections[best->step->section]);
    }

    if (!n) {
        ecs_strbuf_appendstr(buf, "  no step is faster beyond the noise\n");
    }
}

/** Write the report of a finished or stopped run.
 *
 * @param world The world
 * @param abl The state
 * @return The text, which the caller frees
 */
static char* box_cmd_ablate_report(
    ecs_world_t *world,
    const box_cmd_ablate_t *abl)
{
    box_cmd_ablate_result_t *results = box_cmd_ablate_results(abl);
    int32_t i, section = -1;
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    box_cmd_ablate_header(world, abl, &buf);
    if (abl->phase == BoxCmdAblateStopped) {
        ecs_strbuf_append(&buf, "stopped after %d of %d steps\n", abl->current,
            ecs_vec_count(&abl->steps));
    }

    for (i = 0; i < abl->variants; i ++) {
        if (results[i].step->section != section) {
            section = results[i].step->section;
            ecs_strbuf_append(&buf, "\n%-*s    median     delta\n", BOX_CMD_ABLATE_LABEL_WIDTH,
                box_cmd_ablate_sections[section]);
        }

        box_cmd_ablate_line(&buf, &results[i]);
    }

    box_cmd_ablate_rank(&buf, results, abl->variants);
    ecs_os_free(results);

    return ecs_strbuf_get(&buf);
}

/** The ablate command: start a run, stop it, print its progress or its report.
 *
 * @param ctx The call context
 * @param argc Number of options
 * @param argv The options
 * @param result Receives the output
 */
static void box_cmd_ablate(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_world_t *world = ctx->world;
    ecs_entity_t system = ecs_lookup(world, "box.command.Ablate");
    const ecs_system_t *s = system ? ecs_system_get(world, system) : NULL;
    box_cmd_ablate_t *abl = s ? s->ctx : NULL;
    box_cmd_ablate_t opts = {0};
    ecs_entity_t view = 0;
    bool stop = false;
    bool running;
    if (!abl || !ecs_id(View) || !ecs_id(Scene) || !ecs_id(Surface)) {
        ecs_err("ablate: box.view and box.surface are not imported");
        goto done;
    }

    if (box_cmd_ablate_options(world, argc, argv, &opts, &view, &stop)) {
        goto done;
    }

    running = abl->phase == BoxCmdAblateSettle || abl->phase == BoxCmdAblateMeasure;
    if (stop) {
        if (running) {
            box_cmd_ablate_restore(world, abl);
            abl->phase = BoxCmdAblateStopped;
            abl->elapsed = box_cmd_ablate_now() - abl->started;
        }

        *(char**)result->ptr = ecs_os_strdup(running ? "ablate: stopped\n" : "ablate: idle\n");
    } else if (argc || abl->phase == BoxCmdAblateIdle) {
        if (running) {
            box_cmd_ablate_restore(world, abl);
        }

        abl->frames = opts.frames;
        abl->settle = opts.settle;
        abl->rounds = opts.rounds;
        *(char**)result->ptr = box_cmd_ablate_start(world, abl, view);
    } else if (running) {
        *(char**)result->ptr = box_cmd_ablate_progress(world, abl);
    } else {
        *(char**)result->ptr = box_cmd_ablate_report(world, abl);
    }

done:
    return;
}

void box_cmd_ablate_init(
    ecs_world_t *world)
{
    box_cmd_ablate_t *abl = ecs_os_calloc_t(box_cmd_ablate_t);
    ecs_function_desc_t desc = {
        .name = "ablate",
        .return_type = ecs_id(ecs_string_t),
        .callback = box_cmd_ablate
    };
    int32_t i;
    abl->world = world;
    ecs_vec_init_t(NULL, &abl->steps, box_cmd_ablate_step_t, 0);
    ecs_vec_init_t(NULL, &abl->samples, double, 0);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Ablate" }),
        .phase = EcsPostFrame,
        .run = BoxCmdAblate,
        .ctx = abl,
        .ctx_free = box_cmd_ablate_free,
        .immediate = true
    });

    for (i = 0; i < BOX_CMD_ABLATE_OPTIONS_MAX; i ++) {
        desc.params[i] = (ecs_script_parameter_t){
            .name = "options", .type = ecs_id(ecs_string_t), .optional = true,
            .brief = "start (default when idle), stop, frames=n timed per step (default 120), "
                "settle=n settled frames before timing (default 30), rounds=n of every step "
                "(default 3), view=path (default: the view of the active camera)" };
    }

    box_command_register(world,
        "Find what a scene spends its frames on: time the uncapped frames of the view with each "
        "feature off or at another quality, one at a time; run again for the progress and the "
        "report", desc);
}
