/**
 * @file audio/synth_sound.c
 * @brief Compiles a SynthSound and its layers into the patch of the synth of the core, and
 * marks it for a new compile when one of its components changes.
 *
 * The layers are the root and its children with a SynthOscillator, as particle effect layers
 * are, and the children of layers with one, the modulators of their parent.
 *
 * The sync system sends the patch with a SYNTH command when it changed; the core plays every
 * voice from a copy of it, so a play allocates nothing.
 */

#include "module.h"

/** Seconds of the release of a sound that would never end, after BOX_AUDIO_SYNTH_ENDLESS */
#define BOX_AUDIO_SYNTH_ENDLESS_RELEASE (0.01f)

/** Mark the synth sound of an entity, itself or its nearest ancestor with one, for a new
 * compile.
 *
 * @param it The iterator of the observer
 */
static void BoxAudioSynthDirty(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    int32_t i;
    if (ecs_is_fini(world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        ecs_entity_t root = it->entities[i];
        AudioSynth *synth;
        while (root && ecs_is_alive(world, root) && !ecs_has(world, root, SynthSound)) {
            root = ecs_get_parent(world, root);
        }

        if (!root || !ecs_is_alive(world, root) || !ecs_has(world, root, SynthSound)) {
            continue;
        }

        synth = ecs_get_mut(world, root, AudioSynth);
        if (synth) {
            synth->compiled = false;
        }
    }

done:
    return;
}

void box_audio_synth_init(
    ecs_world_t *world)
{
    ecs_entity_t ids[] = {
        ecs_id(SynthSound), ecs_id(SynthOscillator), ecs_id(SynthEnvelope), ecs_id(SynthFilter),
        ecs_id(SynthFilterEnvelope)
    };
    const char *names[] = {
        "AudioSynthSoundDirty", "AudioSynthOscillatorDirty", "AudioSynthEnvelopeDirty",
        "AudioSynthFilterDirty", "AudioSynthFilterEnvelopeDirty"
    };
    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_observer(world, {
            .entity = ecs_entity(world, { .name = names[i] }),
            .query.flags = EcsQueryMatchPrefab,
            .query.terms = {{ .id = ids[i], .src.id = EcsSelf }},
            .events = { EcsOnSet, EcsOnRemove },
            .callback = BoxAudioSynthDirty
        });
    }
}

/** Copy an envelope of the components into a patch.
 *
 * @param env The component
 * @return The envelope of the patch
 */
static box_audio_synth_envelope_t box_audio_synth_envelope(
    const SynthEnvelope *env)
{
    return (box_audio_synth_envelope_t){
        .attack = env->attack, .decay = env->decay, .sustain = env->sustain,
        .release = env->release, .hold = env->hold, .curve = env->curve,
        .attack_curve = env->attack_curve };
}

/** Check whether an envelope fits in a pass of the core; NaN, infinite and negative times do
 * not.
 *
 * @param env The envelope
 * @return Whether it fits
 */
static bool box_audio_synth_envelope_fits(
    const box_audio_synth_envelope_t *env)
{
    float sum = env->attack + env->decay + env->hold + env->release;

    return env->attack >= 0 && env->decay >= 0 && env->hold >= 0 && env->release >= 0 &&
        sum <= BOX_AUDIO_SYNTH_LENGTH_MAX;
}

/** Add a layer to a patch.
 *
 * @param p The patch
 * @param osc The oscillator
 * @param env Its own envelope, NULL for the one of the root
 * @param carrier 1 + the layer its parent is, 0 when its parent is not a layer
 */
static void box_audio_synth_layer(
    box_audio_synth_patch_t *p,
    const SynthOscillator *osc,
    const SynthEnvelope *env,
    uint32_t carrier)
{
    box_audio_synth_layer_t *l = &p->layer[p->layers ++];
    l->wave = (uint32_t)osc->wave;
    l->frequency = osc->frequency;
    l->frequency_end = osc->frequency_end;
    l->sweep = osc->sweep;
    l->level = osc->level;
    l->has_envelope = env != NULL;
    if (env) {
        l->envelope = box_audio_synth_envelope(env);
    }

    if (osc->modulation != SynthMix) {
        l->modulates = carrier;
        l->modulation = osc->modulation == SynthRing ? BOX_AUDIO_SYNTH_RING :
            BOX_AUDIO_SYNTH_PHASE;
        l->index = osc->index;
        l->audible = osc->audible;
    }

    l->feedback = osc->feedback;
}

/** Count the layers of an entity and its descendants that are left out of a full patch.
 *
 * @param world The world
 * @param e The entity, a layer
 * @return 1 and the layers under it
 */
static int32_t box_audio_synth_count(
    ecs_world_t *world,
    ecs_entity_t e)
{
    int32_t result = 1, i;
    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            if (ecs_has(world, it.entities[i], SynthOscillator)) {
                result += box_audio_synth_count(world, it.entities[i]);
            }
        }
    }

    return result;
}

/** Add the layers among the children of an entity to a patch, each before its own children,
 * so a modulator always comes after the layer it modulates.
 *
 * @param world The world
 * @param p The patch
 * @param parent The entity, the root or a layer
 * @param carrier 1 + the layer the entity is, 0 when it is not one
 * @param extra Counts the layers past BOX_AUDIO_SYNTH_LAYERS
 * @param orphans Counts the modulators whose parent is not a layer
 */
static void box_audio_synth_children(
    ecs_world_t *world,
    box_audio_synth_patch_t *p,
    ecs_entity_t parent,
    uint32_t carrier,
    int32_t *extra,
    int32_t *orphans)
{
    int32_t i;
    ecs_iter_t it = ecs_children(world, parent);
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t child = it.entities[i];
            const SynthOscillator *osc = ecs_get(world, child, SynthOscillator);
            if (!osc) {
                continue;
            }

            if (osc->modulation != SynthMix && !carrier) {
                (*orphans) ++;
            } else if (p->layers < BOX_AUDIO_SYNTH_LAYERS) {
                box_audio_synth_layer(p, osc, ecs_get(world, child, SynthEnvelope), carrier);
                box_audio_synth_children(world, p, child, p->layers, extra, orphans);
            } else {
                *extra += box_audio_synth_count(world, child);
            }
        }
    }
}

bool box_audio_synth_compile(
    ecs_world_t *world,
    box_audio_module_t *state,
    ecs_entity_t e,
    AudioSynth *synth)
{
    box_audio_synth_patch_t p = {0};
    const SynthSound *sound = ecs_get(world, e, SynthSound);
    const SynthEnvelope *env = ecs_get(world, e, SynthEnvelope);
    const SynthFilter *filter = ecs_get(world, e, SynthFilter);
    const SynthFilterEnvelope *fenv = ecs_get(world, e, SynthFilterEnvelope);
    const SynthOscillator *osc = ecs_get(world, e, SynthOscillator);
    int32_t extra = 0, orphans = 0, i;
    bool endless = false, fits = true, result;
    if (sound) {
        p.seed = sound->seed;
        p.pitch_variance = sound->pitch_variance;
        p.level_variance = sound->level_variance;
    }

    if (env) {
        p.envelope = box_audio_synth_envelope(env);
        fits = box_audio_synth_envelope_fits(&p.envelope);
    }

    if (filter) {
        p.filter = (uint32_t)filter->kind + 1;
        p.cutoff = filter->cutoff;
        p.resonance = filter->resonance;
        p.slope = (uint32_t)filter->slope;
    }

    if (filter && fenv && fenv->amount != 0) {
        p.filter_amount = fenv->amount;
        p.filter_envelope = (box_audio_synth_envelope_t){
            .attack = fenv->attack, .decay = fenv->decay, .sustain = fenv->sustain,
            .release = fenv->release, .hold = fenv->hold, .curve = fenv->curve,
            .attack_curve = fenv->attack_curve };
        fits = fits && box_audio_synth_envelope_fits(&p.filter_envelope);
    }

    if (osc && osc->modulation != SynthMix) {
        orphans ++;
    } else if (osc) {
        box_audio_synth_layer(&p, osc, NULL, 0);
    }

    box_audio_synth_children(world, &p, e, p.layers, &extra, &orphans);

    for (i = 0; i < (int32_t)p.layers; i ++) {
        if (p.layer[i].has_envelope) {
            fits = fits && box_audio_synth_envelope_fits(&p.layer[i].envelope);
        } else if (!env) {
            endless = true;
        }
    }

    if (extra || endless || !fits || orphans) {
        char *path = ecs_get_path(world, e);
        if (extra) {
            ecs_err("box.audio: synth '%s' has %d layers, more than %d, the rest are left out",
                path, (int)p.layers + extra, BOX_AUDIO_SYNTH_LAYERS);
        }

        if (orphans) {
            ecs_err("box.audio: synth '%s' has %d modulators whose parent is not a layer, "
                "they are left out", path, (int)orphans);
        }

        if (endless) {
            ecs_err("box.audio: synth '%s' has a layer without a SynthEnvelope and would never "
                "end, it plays for %.0f seconds", path, (double)BOX_AUDIO_SYNTH_ENDLESS);
            p.envelope = (box_audio_synth_envelope_t){
                .sustain = 1.0f, .hold = BOX_AUDIO_SYNTH_ENDLESS,
                .release = BOX_AUDIO_SYNTH_ENDLESS_RELEASE };
        }

        if (!fits) {
            ecs_err("box.audio: an envelope of synth '%s' is not a time of 0 to %.0f seconds, "
                "it is cut", path, (double)BOX_AUDIO_SYNTH_LENGTH_MAX);
        }

        ecs_os_free(path);
    }

    result = ecs_os_memcmp_t(&synth->patch, &p, box_audio_synth_patch_t) != 0;
    synth->patch = p;
    synth->compiled = true;
    state->stats.synths ++;

    return result;
}
