/**
 * @file particle/effect.c
 * @brief Plays effect templates once at a point from a pool of instances per template, within
 * the budgets of ParticleSettings, and returns an instance to its pool once it played out.
 *
 * The flashes of an instance are written at its play and after every step of its clock, so the
 * light extract of the next frame lights them at the time of the play the frame shows.
 *
 * A burst from a system that runs deferred makes its instance when the commands merge; a
 * system before the transforms collects its layers then, so it plays in that frame as a burst
 * outside a system does.
 */

#include "effect.h"
#include "simulate.h"
#include "../render/particles.h"

/** An emitter of an instance and when its emission ends */
typedef struct box_particle_layer_t {
    ecs_entity_t entity;    /**< The emitter */
    uint64_t emit;          /**< Microseconds into the play its last burst goes off */
    uint64_t life;          /**< Longest lifetime of its particles in microseconds */
    bool stopped;           /**< Its emission ended in this play */
} box_particle_layer_t;

/** A light an EffectFlash of an instance flashes */
typedef struct box_particle_flash_t {
    ecs_entity_t entity;    /**< Entity with the EffectFlash and the PointLight */
    float own;              /**< Intensity of its light before the first flash */
} box_particle_flash_t;

typedef struct box_particle_effect_t box_particle_effect_t;

/** An instance of a template and its play */
typedef struct box_particle_instance_t {
    ecs_entity_t root;      /**< Root of the instance */
    box_particle_effect_t *effect; /**< Pool of its template */
    bool playing;           /**< Plays, else waits disabled in the pool */
    bool collected;         /**< The layers and the entities were collected */
    bool held;              /**< Recycled in this frame: the clock holds while the layers reset */
    int64_t frame;          /**< Frame of the effects the play started in */
    uint64_t time;          /**< Microseconds into the play */
    uint64_t emit;          /**< Microseconds into the play the last layer stops emitting */
    uint64_t retire;        /**< emit plus the longest lifetime of the layers, or the longest
                             *   flash when that is later */
    uint64_t flash;         /**< Microseconds of the longest flash */
    vec3 position;          /**< World position of the play */
    float scale;            /**< Scale of the play */
    ecs_vec_t layers;       /**< box_particle_layer_t */
    ecs_vec_t entities;     /**< ecs_entity_t, the root and its descendants */
    ecs_vec_t flashes;      /**< box_particle_flash_t */
} box_particle_instance_t;

/** The pool of instances of one template */
struct box_particle_effect_t {
    ecs_entity_t template_; /**< The template */
    bool prefab;            /**< The template is a prefab, else a script template */
    bool empty;             /**< An instance of the script template had no emitter */
    int32_t playing;        /**< Instances playing */
    ecs_vec_t instances;    /**< box_particle_instance_t* */
};

/** State of the effects of a world */
typedef struct box_particle_effects_t {
    ecs_map_t by_template;  /**< Template to box_particle_effect_t* */
    ecs_vec_t effects;      /**< box_particle_effect_t* */
    int32_t playing;        /**< Instances of every template playing */
    int32_t recycled;       /**< Bursts recycled since the last frame */
    int32_t merged;         /**< Bursts merged since the last frame */
    int32_t dropped;        /**< Bursts dropped since the last frame */
    int32_t pending;        /**< Instances made while the world was deferred, whose layers
                             *   were not collected yet */
    int64_t frame;          /**< Frames the effects ran */
    ecs_query_t *views;     /**< View, for EffectSkip */
    ecs_query_t *surfaces;  /**< Surface and an optional SurfaceStatus, for EffectSkip */
    const box_render_particles_t *extract; /**< State of the particle extract, NULL for none */
    ecs_vec_t stack;        /**< ecs_entity_t, scratch of the walks */
} box_particle_effects_t;

/** Singleton through which box_particle_burst_at finds the state of a world */
typedef struct ParticleEffectState {
    box_particle_effects_t *state; /**< The state, owned by the effects system */
} ParticleEffectState;

static ECS_COMPONENT_DECLARE(ParticleEffectState);

/** Check whether an entity is a script template.
 *
 * @param world The world
 * @param e The entity
 * @return Whether it is
 */
static bool box_particle_is_template(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const EcsScript *script = ecs_get(world, e, EcsScript);

    return script && script->template_;
}

/** Push the children of an entity on the scratch stack.
 *
 * @param world The world
 * @param s The state
 * @param e The entity
 */
static void box_particle_push_children(
    const ecs_world_t *world,
    box_particle_effects_t *s,
    ecs_entity_t e)
{
    int32_t i;
    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            *ecs_vec_append_t(NULL, &s->stack, ecs_entity_t) = it.entities[i];
        }
    }
}

/** Check whether a prefab or one of its descendants is an emitter.
 *
 * @param world The world
 * @param s The state
 * @param e The prefab
 * @return Whether it holds an emitter
 */
static bool box_particle_has_emitter(
    const ecs_world_t *world,
    box_particle_effects_t *s,
    ecs_entity_t e)
{
    bool result = false;
    ecs_vec_clear(&s->stack);
    *ecs_vec_append_t(NULL, &s->stack, ecs_entity_t) = e;
    while (!result && ecs_vec_count(&s->stack)) {
        ecs_entity_t next = *ecs_vec_last_t(&s->stack, ecs_entity_t);
        ecs_vec_remove_last(&s->stack);
        result = ecs_has(world, next, ParticleEmitter);
        box_particle_push_children(world, s, next);
    }

    return result;
}

/** Get the microseconds into a play at which a burst goes off for the last time; one that
 * repeats forever goes off once.
 *
 * @param burst The burst
 * @return The microseconds
 */
static uint64_t box_particle_burst_last(
    const ParticleBurst *burst)
{
    uint64_t period = box_particle_us(burst->at);

    return burst->repeat > 0 ? period * ((uint64_t)burst->repeat + 1) : period;
}

/** Find the layer of the nearest emitter at or above an entity of an instance.
 *
 * @param world The world
 * @param inst The instance
 * @param e The entity
 * @return The layer, NULL when no emitter of the instance is at or above it
 */
static box_particle_layer_t* box_particle_layer_of(
    const ecs_world_t *world,
    box_particle_instance_t *inst,
    ecs_entity_t e)
{
    box_particle_layer_t *layers = ecs_vec_first_t(&inst->layers, box_particle_layer_t);
    box_particle_layer_t *result = NULL;
    int32_t i, count = ecs_vec_count(&inst->layers);
    while (e && !result) {
        for (i = 0; i < count; i ++) {
            if (layers[i].entity == e) {
                result = &layers[i];
                break;
            }
        }

        e = e == inst->root ? 0 : ecs_get_parent(world, e);
    }

    return result;
}

/** Get the microseconds an EffectFlash fades over.
 *
 * @param flash The EffectFlash
 * @return The microseconds, a fifth of a second for seconds of 0
 */
static uint64_t box_particle_flash_us(
    const EffectFlash *flash)
{
    return box_particle_us(flash->seconds > 0 ? flash->seconds : 0.2f);
}

/** Write the intensity of the flashes of an instance at the time of its play.
 *
 * @param world The world
 * @param inst The instance
 */
static void box_particle_flash_write(
    ecs_world_t *world,
    const box_particle_instance_t *inst)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&inst->flashes); i ++) {
        const box_particle_flash_t *f = ecs_vec_get_t(&inst->flashes, box_particle_flash_t, i);
        const EffectFlash *flash = ecs_is_alive(world, f->entity)
            ? ecs_get(world, f->entity, EffectFlash)
            : NULL;
        const PointLight *light = flash ? ecs_get(world, f->entity, PointLight) : NULL;
        uint64_t span;
        float from, value;
        if (!light) {
            continue;
        }

        span = box_particle_flash_us(flash);
        from = flash->intensity > 0 ? flash->intensity : f->own;
        value = inst->time < span
            ? from * (float)(1.0 - (double)inst->time / (double)span)
            : 0.0f;
        if (light->intensity != value) {
            PointLight copy = *light;
            copy.intensity = value;
            ecs_set_ptr(world, f->entity, PointLight, &copy);
        }
    }
}

/** Collect the entities, the layers and the flashes of an instance, when its template made
 * them, give every layer a playback control and every entity a Position, so the layers follow
 * the root.
 *
 * @param world The world
 * @param s The state
 * @param inst The instance
 */
static void box_particle_collect(
    ecs_world_t *world,
    box_particle_effects_t *s,
    box_particle_instance_t *inst)
{
    int32_t i;
    ecs_vec_clear(&inst->entities);
    ecs_vec_clear(&inst->layers);
    ecs_vec_clear(&inst->flashes);
    ecs_vec_clear(&s->stack);
    *ecs_vec_append_t(NULL, &s->stack, ecs_entity_t) = inst->root;
    while (ecs_vec_count(&s->stack)) {
        ecs_entity_t e = *ecs_vec_last_t(&s->stack, ecs_entity_t);
        ecs_vec_remove_last(&s->stack);
        *ecs_vec_append_t(NULL, &inst->entities, ecs_entity_t) = e;
        const ParticleEmitter *emitter = ecs_get(world, e, ParticleEmitter);
        if (emitter) {
            *ecs_vec_append_t(NULL, &inst->layers, box_particle_layer_t) =
                (box_particle_layer_t){
                    .entity = e,
                    .life = box_particle_us(emitter->lifetime.max > emitter->lifetime.min
                        ? emitter->lifetime.max : emitter->lifetime.min)
                };
        }

        box_particle_push_children(world, s, e);
    }

    inst->emit = 0;
    inst->retire = 0;
    inst->flash = 0;
    for (i = 0; i < ecs_vec_count(&inst->entities); i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&inst->entities, ecs_entity_t, i);
        if (!ecs_has(world, e, Position)) {
            ecs_set(world, e, Position, {0, 0, 0});
        }

        const EffectFlash *flash = ecs_get(world, e, EffectFlash);
        const PointLight *light = flash ? ecs_get(world, e, PointLight) : NULL;
        if (light) {
            uint64_t span = box_particle_flash_us(flash);
            *ecs_vec_append_t(NULL, &inst->flashes, box_particle_flash_t) =
                (box_particle_flash_t){ .entity = e, .own = light->intensity };
            inst->flash = span > inst->flash ? span : inst->flash;
        }

        const ParticleBurst *burst = ecs_get(world, e, ParticleBurst);
        box_particle_layer_t *layer = burst && burst->count > 0
            ? box_particle_layer_of(world, inst, e)
            : NULL;
        if (layer) {
            uint64_t last = box_particle_burst_last(burst);
            layer->emit = last > layer->emit ? last : layer->emit;
        }
    }

    for (i = 0; i < ecs_vec_count(&inst->layers); i ++) {
        box_particle_layer_t *layer = ecs_vec_get_t(&inst->layers, box_particle_layer_t, i);
        inst->emit = layer->emit > inst->emit ? layer->emit : inst->emit;
        if (layer->emit + layer->life > inst->retire) {
            inst->retire = layer->emit + layer->life;
        }

        if (!ecs_has(world, layer->entity, ParticlePlayback)) {
            ecs_set(world, layer->entity, ParticlePlayback, { .state = ParticlePlaying });
        }
    }

    inst->retire = inst->flash > inst->retire ? inst->flash : inst->retire;
    inst->collected = true;
}

/** Write the ParticleEffect of the root of an instance.
 *
 * @param world The world
 * @param inst The instance
 */
static void box_particle_effect_write(
    ecs_world_t *world,
    const box_particle_instance_t *inst)
{
    ecs_set(world, inst->root, ParticleEffect, {
        .effect = inst->effect->template_,
        .scale = inst->scale,
        .time = (float)((double)inst->time * 1e-6),
        .emit = (float)((double)inst->emit * 1e-6),
        .retire = (float)((double)inst->retire * 1e-6)
    });
}

/** Enable or disable every entity of an instance.
 *
 * @param world The world
 * @param inst The instance
 * @param enabled Whether to enable
 */
static void box_particle_enable(
    ecs_world_t *world,
    const box_particle_instance_t *inst,
    bool enabled)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&inst->entities); i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&inst->entities, ecs_entity_t, i);
        if (ecs_is_alive(world, e)) {
            ecs_enable(world, e, enabled);
        }
    }
}

/** End the play of an instance: disable it and return it to its pool.
 *
 * @param world The world
 * @param s The state
 * @param inst The instance, playing
 */
static void box_particle_retire(
    ecs_world_t *world,
    box_particle_effects_t *s,
    box_particle_instance_t *inst)
{
    box_particle_enable(world, inst, false);
    inst->playing = false;
    inst->effect->playing --;
    s->playing --;
}

/** Make an instance of a template, enabled. Its layers are collected at once unless the
 * world is deferred or the template did not make them yet, else by the next effects system.
 *
 * @param world The world
 * @param s The state
 * @param effect The pool of the template
 * @return The instance
 */
static box_particle_instance_t* box_particle_instance_new(
    ecs_world_t *world,
    box_particle_effects_t *s,
    box_particle_effect_t *effect)
{
    box_particle_instance_t *result = ecs_os_calloc_t(box_particle_instance_t);
    result->effect = effect;
    result->root = ecs_new(world);
    if (effect->prefab) {
        ecs_add_pair(world, result->root, EcsIsA, effect->template_);
    } else {
        ecs_add_id(world, result->root, effect->template_);
    }

    ecs_vec_init_t(NULL, &result->layers, box_particle_layer_t, 0);
    ecs_vec_init_t(NULL, &result->entities, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &result->flashes, box_particle_flash_t, 0);
    *ecs_vec_append_t(NULL, &effect->instances, box_particle_instance_t*) = result;
    if (ecs_has_id(world, result->root, effect->prefab
        ? ecs_pair(EcsIsA, effect->template_)
        : effect->template_))
    {
        box_particle_collect(world, s, result);
        result->collected = ecs_vec_count(&result->layers) != 0;
        effect->empty = !result->collected;
    } else {
        s->pending ++;
    }

    return result;
}

/** Free an instance, deleting its entities when asked.
 *
 * @param world The world
 * @param s The state
 * @param inst The instance
 * @param destroy Whether to delete the root of the instance
 */
static void box_particle_instance_free(
    ecs_world_t *world,
    box_particle_effects_t *s,
    box_particle_instance_t *inst,
    bool destroy)
{
    if (inst->playing) {
        inst->effect->playing --;
        s->playing --;
    }

    if (destroy && ecs_is_alive(world, inst->root)) {
        ecs_delete(world, inst->root);
    }

    ecs_vec_fini_t(NULL, &inst->layers, box_particle_layer_t);
    ecs_vec_fini_t(NULL, &inst->entities, ecs_entity_t);
    ecs_vec_fini_t(NULL, &inst->flashes, box_particle_flash_t);
    ecs_os_free(inst);
}

/** Free the pool of a template and delete its instances.
 *
 * @param world The world, NULL when the world deletes them
 * @param s The state
 * @param index Index of the pool
 */
static void box_particle_effect_free(
    ecs_world_t *world,
    box_particle_effects_t *s,
    int32_t index)
{
    box_particle_effect_t *effect = *ecs_vec_get_t(&s->effects, box_particle_effect_t*, index);
    int32_t i;
    for (i = 0; i < ecs_vec_count(&effect->instances); i ++) {
        box_particle_instance_free(world, s,
            *ecs_vec_get_t(&effect->instances, box_particle_instance_t*, i), world != NULL);
    }

    ecs_map_remove(&s->by_template, effect->template_);
    ecs_vec_remove_t(&s->effects, box_particle_effect_t*, index);
    ecs_vec_fini_t(NULL, &effect->instances, box_particle_instance_t*);
    ecs_os_free(effect);
}

/** Get the pool of a template, making it.
 *
 * @param s The state
 * @param template_ The template
 * @param prefab Whether it is a prefab
 * @return The pool
 */
static box_particle_effect_t* box_particle_effect_get(
    box_particle_effects_t *s,
    ecs_entity_t template_,
    bool prefab)
{
    box_particle_effect_t *result = ecs_map_get_deref(
        &s->by_template, box_particle_effect_t, template_);
    if (!result) {
        result = ecs_os_calloc_t(box_particle_effect_t);
        result->template_ = template_;
        result->prefab = prefab;
        ecs_vec_init_t(NULL, &result->instances, box_particle_instance_t*, 0);
        *ecs_vec_append_t(NULL, &s->effects, box_particle_effect_t*) = result;
        ecs_map_insert_ptr(&s->by_template, template_, result);
    }

    return result;
}

/** Find the size of the surface a view renders to: the first surface whose view_root is 0 or
 * the parent of the view, in points, else the default surface size.
 *
 * @param world The world
 * @param s The state
 * @param view The view
 * @param width Receives the width in points
 * @param height Receives the height in points
 */
static void box_particle_surface_size(
    const ecs_world_t *world,
    box_particle_effects_t *s,
    ecs_entity_t view,
    float *width,
    float *height)
{
    ecs_entity_t parent = ecs_get_parent(world, view);
    bool found = false;
    int32_t i;
    *width = BOX_SURFACE_WIDTH;
    *height = BOX_SURFACE_HEIGHT;
    ecs_iter_t it = ecs_query_iter(world, s->surfaces);
    while (!found && ecs_query_next(&it)) {
        const Surface *surfaces = ecs_field(&it, Surface, 0);
        const SurfaceStatus *status = ecs_field(&it, SurfaceStatus, 1);
        for (i = 0; i < it.count && !found; i ++) {
            if (surfaces[i].view_root && surfaces[i].view_root != parent) {
                continue;
            }

            found = true;
            box_surface_point_size(&surfaces[i], status ? &status[i] : NULL, width, height);
        }
    }

    if (found) {
        ecs_iter_fini(&it);
    }
}

/** Check whether a view sees a point within a distance of its camera.
 *
 * @param world The world
 * @param s The state
 * @param view The view entity
 * @param v The view
 * @param position The point
 * @param distance Largest distance from the camera, 0 or less for any
 * @return Whether the point is inside the frustum of the camera and within the distance
 */
static bool box_particle_view_sees(
    const ecs_world_t *world,
    box_particle_effects_t *s,
    ecs_entity_t view,
    const View *v,
    const vec3 position,
    float distance)
{
    const Camera *c = v->camera && ecs_is_alive(world, v->camera)
        ? ecs_get(world, v->camera, Camera)
        : NULL;
    const WorldTransform *t = c ? ecs_get(world, v->camera, WorldTransform) : NULL;
    mat4 transform, inverse, proj, view_proj;
    vec4 point = { position[0], position[1], position[2], 1.0f }, clip;
    float width, height;
    bool result = false;
    if (!c) {
        goto done;
    }

    if (t) {
        glm_mat4_copy(ECS_CONST_CAST(vec4*, t->m), transform);
    } else {
        glm_mat4_identity(transform);
    }

    if (distance > 0 &&
        glm_vec3_distance(ECS_CONST_CAST(float*, position), transform[3]) > distance)
    {
        goto done;
    }

    glm_mat4_inv(transform, inverse);
    box_particle_surface_size(world, s, view, &width, &height);
    box_camera_projection(c, width, height, proj);
    glm_mat4_mul(proj, inverse, view_proj);
    glm_mat4_mulv(view_proj, point, clip);
    if (clip[3] <= 1e-6f) {
        goto done;
    }

    result = fabsf(clip[0]) <= clip[3] && fabsf(clip[1]) <= clip[3] &&
        clip[2] >= 0.0f && clip[2] <= clip[3];
done:
    return result;
}

/** Check whether any view sees a burst, for EffectSkip.
 *
 * @param world The world
 * @param s The state
 * @param position The point of the burst
 * @param distance Largest distance from the camera of a view, 0 or less for any
 * @return Whether a view sees it
 */
static bool box_particle_seen(
    const ecs_world_t *world,
    box_particle_effects_t *s,
    const vec3 position,
    float distance)
{
    bool result = false;
    int32_t i;
    ecs_iter_t it = ecs_query_iter(world, s->views);
    while (!result && ecs_query_next(&it)) {
        const View *views = ecs_field(&it, View, 0);
        for (i = 0; i < it.count && !result; i ++) {
            result = box_particle_view_sees(world, s, it.entities[i], &views[i], position,
                distance);
        }
    }

    if (result) {
        ecs_iter_fini(&it);
    }

    return result;
}

/** Find the playing instance of a template nearest to a point, for EffectMerge.
 *
 * @param effect The pool of the template
 * @param position The point
 * @param distance Largest distance
 * @return The instance, NULL when none plays within the distance
 */
static box_particle_instance_t* box_particle_nearby(
    const box_particle_effect_t *effect,
    const vec3 position,
    float distance)
{
    box_particle_instance_t *result = NULL;
    float best = distance > 0 ? distance : 0.0f;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&effect->instances); i ++) {
        box_particle_instance_t *inst = *ecs_vec_get_t(
            &effect->instances, box_particle_instance_t*, i);
        float d;
        if (!inst->playing) {
            continue;
        }

        d = glm_vec3_distance(ECS_CONST_CAST(float*, position), inst->position);
        if (d <= best) {
            best = d;
            result = inst;
        }
    }

    return result;
}

/** Find the instance that has played longest, of a template or of any, that did not start in
 * this frame.
 *
 * @param s The state
 * @param effect The pool of the template, NULL for every template
 * @return The instance, NULL when every playing instance started in this frame
 */
static box_particle_instance_t* box_particle_oldest(
    box_particle_effects_t *s,
    const box_particle_effect_t *effect)
{
    box_particle_instance_t *result = NULL;
    int32_t e, i;
    for (e = 0; e < ecs_vec_count(&s->effects); e ++) {
        const box_particle_effect_t *pool = *ecs_vec_get_t(
            &s->effects, box_particle_effect_t*, e);
        if (effect && pool != effect) {
            continue;
        }

        for (i = 0; i < ecs_vec_count(&pool->instances); i ++) {
            box_particle_instance_t *inst = *ecs_vec_get_t(
                &pool->instances, box_particle_instance_t*, i);
            if (!inst->playing || inst->frame == s->frame) {
                continue;
            }

            if (!result || inst->frame < result->frame ||
                (inst->frame == result->frame && inst->time > result->time))
            {
                result = inst;
            }
        }
    }

    return result;
}

/** Find a free instance of a template.
 *
 * @param effect The pool of the template
 * @return The instance, NULL when every instance plays
 */
static box_particle_instance_t* box_particle_free_instance(
    const box_particle_effect_t *effect)
{
    box_particle_instance_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&effect->instances); i ++) {
        box_particle_instance_t *inst = *ecs_vec_get_t(
            &effect->instances, box_particle_instance_t*, i);
        if (!inst->playing) {
            result = inst;
            break;
        }
    }

    return result;
}

/** Place, scale and play an instance from the start.
 *
 * A free instance is enabled and its layers play again from the start; a recycled one resets
 * its layers, which empties their pools in this frame and plays them from the next. Its
 * flashes light up at once.
 *
 * @param world The world
 * @param s The state
 * @param inst The instance
 * @param position World position of the play
 * @param scale Scale of the play, above 0
 */
static void box_particle_play(
    ecs_world_t *world,
    box_particle_effects_t *s,
    box_particle_instance_t *inst,
    const vec3 position,
    float scale)
{
    const Scale *base = ecs_get(world, inst->effect->template_, Scale);
    bool recycle = inst->playing;
    int32_t i;
    if (!recycle) {
        box_particle_enable(world, inst, true);
        inst->effect->playing ++;
        s->playing ++;
    }

    glm_vec3_copy(ECS_CONST_CAST(float*, position), inst->position);
    inst->scale = scale;
    inst->playing = true;
    inst->held = recycle;
    inst->frame = s->frame;
    inst->time = 0;
    ecs_set(world, inst->root, Position, { position[0], position[1], position[2] });
    ecs_set(world, inst->root, Scale, {
        (base ? base->x : 1.0f) * scale,
        (base ? base->y : 1.0f) * scale,
        (base ? base->z : 1.0f) * scale
    });
    for (i = 0; i < ecs_vec_count(&inst->layers); i ++) {
        box_particle_layer_t *layer = ecs_vec_get_t(&inst->layers, box_particle_layer_t, i);
        layer->stopped = false;
        if (ecs_is_alive(world, layer->entity)) {
            ecs_set(world, layer->entity, ParticlePlayback, {
                .state = ParticlePlaying,
                .reset = recycle
            });
        }
    }

    box_particle_flash_write(world, inst);
    box_particle_effect_write(world, inst);
}

ecs_entity_t box_particle_burst_at(
    ecs_world_t *world,
    ecs_entity_t effect,
    const vec3 position,
    float scale)
{
    if (ecs_stage_is_readonly(world)) {
        world = ecs_get_stage(world, 0);
    }

    const ParticleEffectState *state = ecs_singleton_get(world, ParticleEffectState);
    box_particle_effects_t *s = state ? state->state : NULL;
    const ParticleSettings *settings = ecs_singleton_get(world, ParticleSettings);
    ParticleSettings budget = settings ? *settings : (ParticleSettings){0};
    box_particle_instance_t *inst = NULL;
    box_particle_effect_t *pool;
    ecs_entity_t result = 0;
    bool prefab, own, total;
    if (!s || !effect || !ecs_is_alive(world, effect)) {
        goto done;
    }

    prefab = ecs_has_id(world, effect, EcsPrefab);
    if (!prefab && !box_particle_is_template(world, effect)) {
        goto done;
    }

    if (prefab && !box_particle_has_emitter(world, s, effect)) {
        goto done;
    }

    pool = box_particle_effect_get(s, effect, prefab);
    if (pool->empty) {
        goto done;
    }

    scale = scale > 0 ? scale : 1.0f;
    own = budget.effect_max > 0 && pool->playing >= budget.effect_max;
    total = budget.effect_total > 0 && s->playing >= budget.effect_total;
    if (own || total) {
        if (budget.effect_overflow == EffectSkip &&
            !box_particle_seen(world, s, position, budget.effect_distance))
        {
            s->dropped ++;
            goto done;
        }

        if (budget.effect_overflow == EffectMerge) {
            inst = box_particle_nearby(pool, position, budget.effect_merge);
            if (inst) {
                s->merged ++;
                result = inst->root;
                goto done;
            }
        }

        inst = box_particle_oldest(s, own ? pool : NULL);
        if (!inst) {
            s->dropped ++;
            goto done;
        }

        if (inst->effect != pool) {
            box_particle_retire(world, s, inst);
            inst = NULL;
        }

        s->recycled ++;
    }

    if (!inst) {
        inst = box_particle_free_instance(pool);
    }

    if (!inst) {
        inst = box_particle_instance_new(world, s, pool);
        if (pool->empty) {
            ecs_vec_remove_last(&pool->instances);
            box_particle_instance_free(world, s, inst, true);
            goto done;
        }
    }

    box_particle_play(world, s, inst, position, scale);
    result = inst->root;
done:
    return result;
}

/** Advance the play of an instance after the particle extract ran: stop the emission of each
 * layer after its last burst, fade its flashes and return the instance to its pool once its
 * layers stopped, their last particle died and its flashes faded.
 *
 * @param world The world
 * @param s The state
 * @param extract The state of the particle extract
 * @param inst The instance, playing
 * @param delta Microseconds of the frame
 */
static void box_particle_advance(
    ecs_world_t *world,
    box_particle_effects_t *s,
    const box_render_particles_t *extract,
    box_particle_instance_t *inst,
    uint64_t delta)
{
    bool done = true, held = inst->held;
    int32_t i;
    if (!inst->collected) {
        box_particle_collect(world, s, inst);
        if (!ecs_vec_count(&inst->layers)) {
            inst->effect->empty = true;
        }
    }

    inst->held = false;
    if (!held) {
        inst->time += delta;
    }

    box_particle_flash_write(world, inst);
    if (inst->time < inst->flash) {
        done = false;
    }

    for (i = 0; i < ecs_vec_count(&inst->layers); i ++) {
        box_particle_layer_t *layer = ecs_vec_get_t(&inst->layers, box_particle_layer_t, i);
        if (!ecs_is_alive(world, layer->entity)) {
            continue;
        }

        if (!layer->stopped && !held && inst->time >= layer->emit) {
            ParticlePlayback *playback = ecs_ensure(world, layer->entity, ParticlePlayback);
            playback->state = ParticleStopped;
            ecs_modified(world, layer->entity, ParticlePlayback);
            layer->stopped = true;
        }

        if (!layer->stopped || box_render_particles_live(extract, layer->entity) > 0) {
            done = false;
        }
    }

    if (done) {
        box_particle_retire(world, s, inst);
    }

    box_particle_effect_write(world, inst);
}

/** Collect the layers of the instances that bursts made while the world was deferred, once their
 * commands merged, and write their flashes and their play as a burst outside a system does.
 *
 * Runs before the transforms, so the layers have their Position in the frame of the burst.
 *
 * @param it The iterator of the system
 */
static void BoxParticleEffectsCollect(
    ecs_iter_t *it)
{
    box_particle_effects_t *s = it->ctx;
    ecs_world_t *world = it->world;
    int32_t e, i;
    if (!s->pending) {
        goto done;
    }

    for (e = 0; e < ecs_vec_count(&s->effects); e ++) {
        box_particle_effect_t *effect = *ecs_vec_get_t(&s->effects, box_particle_effect_t*, e);
        for (i = 0; i < ecs_vec_count(&effect->instances); i ++) {
            box_particle_instance_t *inst = *ecs_vec_get_t(
                &effect->instances, box_particle_instance_t*, i);
            if (inst->collected || !inst->playing || !ecs_is_alive(world, inst->root)) {
                continue;
            }

            box_particle_collect(world, s, inst);
            if (!ecs_vec_count(&inst->layers)) {
                effect->empty = true;
            }

            box_particle_flash_write(world, inst);
            box_particle_effect_write(world, inst);
        }
    }

    s->pending = 0;
done:
    return;
}

/** Advance every playing instance, release the pools of deleted templates and write the
 * effect counters of ParticleStats.
 *
 * Runs after the particle extract of the frame, which counted the particles of the layers.
 *
 * @param it The iterator of the system
 */
static void BoxParticleEffects(
    ecs_iter_t *it)
{
    box_particle_effects_t *s = it->ctx;
    ecs_world_t *world = it->world;
    const box_render_particles_t *extract = s->extract;
    uint64_t delta = box_particle_us(it->delta_time);
    int32_t e, i;
    for (e = ecs_vec_count(&s->effects) - 1; e >= 0; e --) {
        box_particle_effect_t *effect = *ecs_vec_get_t(&s->effects, box_particle_effect_t*, e);
        if (!ecs_is_alive(world, effect->template_)) {
            box_particle_effect_free(world, s, e);
            continue;
        }

        for (i = ecs_vec_count(&effect->instances) - 1; i >= 0; i --) {
            box_particle_instance_t *inst = *ecs_vec_get_t(
                &effect->instances, box_particle_instance_t*, i);
            if (!ecs_is_alive(world, inst->root)) {
                box_particle_instance_free(world, s, inst, false);
                ecs_vec_remove_t(&effect->instances, box_particle_instance_t*, i);
            } else if (inst->playing) {
                box_particle_advance(world, s, extract, inst, delta);
            }
        }
    }

    ParticleStats *stats = ecs_singleton_ensure(world, ParticleStats);
    stats->effects = s->playing;
    stats->recycled = s->recycled;
    stats->merged = s->merged;
    stats->dropped = s->dropped;
    ecs_singleton_modified(world, ParticleStats);
    s->recycled = 0;
    s->merged = 0;
    s->dropped = 0;
    s->frame ++;
}

/** Free the state of the effects; the world deletes the instances.
 *
 * @param ctx The state
 */
static void box_particle_effects_free(
    void *ctx)
{
    box_particle_effects_t *s = ctx;
    while (ecs_vec_count(&s->effects)) {
        box_particle_effect_free(NULL, s, ecs_vec_count(&s->effects) - 1);
    }

    ecs_map_fini(&s->by_template);
    ecs_vec_fini_t(NULL, &s->effects, box_particle_effect_t*);
    ecs_vec_fini_t(NULL, &s->stack, ecs_entity_t);
    ecs_os_free(s);
}

/** The box.particle.burst_at script function.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The template, the position and the scale
 * @param result Receives the root of the instance, 0 for none
 */
static void box_particle_burst_at_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_entity_t effect = *(ecs_entity_t*)argv[0].ptr;
    const float *position = argv[1].ptr;
    float scale = *(float*)argv[2].ptr;
    (void)argc;
    *(ecs_entity_t*)result->ptr = box_particle_burst_at(ctx->world, effect, position, scale);
}

void box_particle_effects_import(
    ecs_world_t *world)
{
    box_particle_effects_t *s = ecs_os_calloc_t(box_particle_effects_t);
    ecs_map_init(&s->by_template, NULL);
    ecs_vec_init_t(NULL, &s->effects, box_particle_effect_t*, 0);
    ecs_vec_init_t(NULL, &s->stack, ecs_entity_t, 0);
    s->views = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "ParticleEffectViews" }),
        .terms = {{ .id = ecs_id(View), .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto
    });
    s->surfaces = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "ParticleEffectSurfaces" }),
        .terms = {
            { .id = ecs_id(Surface), .inout = EcsIn },
            { .id = ecs_id(SurfaceStatus), .inout = EcsIn, .oper = EcsOptional }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    s->extract = box_render_particles_get(world);

    ECS_COMPONENT_DEFINE(world, ParticleEffectState);
    ecs_singleton_set(world, ParticleEffectState, { .state = s });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ParticleEffectsCollect" }),
        .phase = EcsPostUpdate,
        .run = BoxParticleEffectsCollect,
        .ctx = s,
        .immediate = true
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ParticleEffects" }),
        .phase = RenderExtract,
        .run = BoxParticleEffects,
        .ctx = s,
        .ctx_free = box_particle_effects_free,
        .immediate = true
    });

    ecs_function(world, {
        .name = "burst_at",
        .parent = ecs_get_scope(world),
        .return_type = ecs_id(ecs_entity_t),
        .params = {
            { .name = "effect", .type = ecs_id(ecs_entity_t) },
            { .name = "position", .type = ecs_id(vec3) },
            { .name = "scale", .type = ecs_id(ecs_f32_t) }
        },
        .callback = box_particle_burst_at_function
    });
}
