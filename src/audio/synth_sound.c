/**
 * @file audio/synth_sound.c
 * @brief Compiles a SynthSound and its layers into the patch of the synth of the core, and
 * marks it for a new compile when one of its components changes.
 *
 * The layers are the root and its children with a SynthOscillator, and the children of layers
 * with one, the modulators of their parent; a SynthPluck layer takes its SynthString, a
 * SynthWavetable layer its SynthTable, whose table the patch names by the sound id of its
 * Wavetable in the current output. The LFOs are the SynthLfo of the root, of the layers and of
 * the other children of the root.
 *
 * The sync system sends the patch with a SYNTH command when it changed; the core plays every
 * voice from a copy of it, so a play allocates nothing.
 */

#include "module.h"

/** Seconds of the release of a sound that would never end, after BOX_AUDIO_SYNTH_ENDLESS */
#define BOX_AUDIO_SYNTH_ENDLESS_RELEASE (0.01f)

/** The string of a SynthPluck layer without a SynthString */
static const SynthString box_audio_synth_string_default = {
    .decay = 2.0f, .brightness = 0.5f, .hardness = 0.8f };

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
        ecs_id(SynthFilterEnvelope), ecs_id(SynthGlide), ecs_id(SynthUnison),
        ecs_id(SynthString), ecs_id(SynthLfo), ecs_id(SynthPitchEnvelope), ecs_id(SynthDrive),
        ecs_id(SynthTable)
    };
    const char *names[] = {
        "AudioSynthSoundDirty", "AudioSynthOscillatorDirty", "AudioSynthEnvelopeDirty",
        "AudioSynthFilterDirty", "AudioSynthFilterEnvelopeDirty", "AudioSynthGlideDirty",
        "AudioSynthUnisonDirty", "AudioSynthStringDirty", "AudioSynthLfoDirty",
        "AudioSynthPitchEnvelopeDirty", "AudioSynthDriveDirty", "AudioSynthTableDirty"
    };
    int32_t i;
    for (i = 0; i < 12; i ++) {
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
        .attack_curve = env->attack_curve, .key_scale = env->key_scale };
}

/** Copy a pitch envelope of the components into a patch.
 *
 * @param env The component
 * @return The pitch envelope of the patch
 */
static box_audio_synth_pitch_envelope_t box_audio_synth_pitch_envelope(
    const SynthPitchEnvelope *env)
{
    return (box_audio_synth_pitch_envelope_t){
        .amount = env->amount, .decay = env->decay, .curve = env->curve,
        .release_amount = env->release_amount, .sync = env->sync };
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
 * @param bend Its own pitch envelope, NULL for the one of the root
 * @param unison Its unison, NULL for one voice
 * @param string Its string, NULL for the defaults; used by SynthPluck only
 * @param table Its table, NULL for none; used by SynthWavetable only
 * @param tables Receives SynthTable.table of the layer by its index, 0 without a table
 * @param carrier 1 + the layer its parent is, 0 when its parent is not a layer
 */
static void box_audio_synth_layer(
    box_audio_synth_patch_t *p,
    const SynthOscillator *osc,
    const SynthEnvelope *env,
    const SynthPitchEnvelope *bend,
    const SynthUnison *unison,
    const SynthString *string,
    const SynthTable *table,
    ecs_entity_t *tables,
    uint32_t carrier)
{
    box_audio_synth_layer_t *l = &p->layer[p->layers];
    tables[p->layers ++] = 0;
    l->wave = (uint32_t)osc->wave;
    l->frequency = osc->frequency;
    l->frequency_end = osc->frequency_end;
    l->sweep = osc->sweep;
    l->level = osc->level;
    l->has_envelope = env != NULL;
    if (env) {
        l->envelope = box_audio_synth_envelope(env);
    }

    l->has_pitch_envelope = bend != NULL;
    if (bend) {
        l->pitch_envelope = box_audio_synth_pitch_envelope(bend);
    }

    if (osc->modulation != SynthMix) {
        l->modulates = carrier;
        l->modulation = osc->modulation == SynthRing ? BOX_AUDIO_SYNTH_RING :
            BOX_AUDIO_SYNTH_PHASE;
        l->index = osc->index;
        l->index_velocity = osc->velocity;
        l->audible = osc->audible;
    }

    l->feedback = osc->feedback;
    l->pulse_width = osc->pulse_width;
    l->sub = osc->sub;
    l->sub_octave = osc->sub_octave >= 2 ? 2 : 1;
    l->sync = osc->sync;
    l->color = osc->color == SynthNoisePink ? BOX_AUDIO_SYNTH_PINK :
        (osc->color == SynthNoiseBrown ? BOX_AUDIO_SYNTH_BROWN : BOX_AUDIO_SYNTH_WHITE);
    l->voices = 1;
    if (unison) {
        l->voices = unison->voices > 1 ? (uint32_t)unison->voices : 1;
        l->detune = unison->detune;
        l->width = unison->width;
    }

    if (osc->wave == SynthPluck) {
        string = string ? string : &box_audio_synth_string_default;
        l->decay = string->decay;
        l->brightness = string->brightness;
        l->hardness = string->hardness;
        l->hardness_velocity = string->velocity;
    }

    if (osc->wave == SynthWavetable && table) {
        tables[p->layers - 1] = table->table;
        l->position = table->position;
        l->position_envelope = table->envelope;
    }
}

/** Add an LFO to a patch, or count it when the patch has BOX_AUDIO_SYNTH_LFOS.
 *
 * @param p The patch
 * @param lfo The LFO, NULL for none
 * @param layer 1 + the layer it moves, 0 for every layer
 * @param extra Counts the LFOs past BOX_AUDIO_SYNTH_LFOS
 */
static void box_audio_synth_lfo(
    box_audio_synth_patch_t *p,
    const SynthLfo *lfo,
    uint32_t layer,
    int32_t *extra)
{
    if (!lfo) {
        goto done;
    }

    if (p->lfos >= BOX_AUDIO_SYNTH_LFOS) {
        (*extra) ++;
        goto done;
    }

    p->lfo[p->lfos ++] = (box_audio_synth_lfo_t){
        .wave = (uint32_t)lfo->wave, .rate = lfo->rate, .delay = lfo->delay,
        .fade = lfo->fade, .phase = lfo->phase, .random_phase = lfo->random_phase,
        .layer = layer, .pitch = lfo->pitch, .amplitude = lfo->amplitude,
        .cutoff = lfo->cutoff, .pulse_width = lfo->pulse_width,
        .hard_sync = lfo->hard_sync, .sync = lfo->sync, .vowel = lfo->vowel,
        .position = lfo->position };
done:
    return;
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
 * @param lfos Counts the LFOs past BOX_AUDIO_SYNTH_LFOS
 * @param tables Receives SynthTable.table of each layer by its index
 */
static void box_audio_synth_children(
    ecs_world_t *world,
    box_audio_synth_patch_t *p,
    ecs_entity_t *tables,
    ecs_entity_t parent,
    uint32_t carrier,
    int32_t *extra,
    int32_t *orphans,
    int32_t *lfos)
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
                box_audio_synth_layer(p, osc, ecs_get(world, child, SynthEnvelope),
                    ecs_get(world, child, SynthPitchEnvelope),
                    ecs_get(world, child, SynthUnison), ecs_get(world, child, SynthString),
                    ecs_get(world, child, SynthTable), tables, carrier);
                box_audio_synth_lfo(p, ecs_get(world, child, SynthLfo), p->layers, lfos);
                box_audio_synth_children(world, p, tables, child, p->layers, extra, orphans,
                    lfos);
            } else {
                *extra += box_audio_synth_count(world, child);
            }
        }
    }
}

bool box_audio_synth_tables(
    ecs_world_t *world,
    const box_audio_module_t *state,
    AudioSynth *synth)
{
    bool result = false;
    int32_t i;
    for (i = 0; i < (int32_t)synth->patch.layers; i ++) {
        ecs_entity_t table = synth->tables[i];
        const AudioWavetable *w;
        uint32_t id = 0;
        if (!(synth->table_layers & (1u << i))) {
            continue;
        }

        if (table && ecs_is_alive(world, table) && ecs_has(world, table, Wavetable)) {
            w = ecs_get(world, table, AudioWavetable);
            id = w && state->serial && w->serial == state->serial ? w->id : 0;
        }

        if (synth->patch.layer[i].table != id) {
            synth->patch.layer[i].table = id;
            result = true;
        }
    }

    return result;
}

bool box_audio_synth_compile(
    ecs_world_t *world,
    box_audio_module_t *state,
    ecs_entity_t e,
    AudioSynth *synth)
{
    box_audio_synth_patch_t p = {0}, old;
    ecs_entity_t tables[BOX_AUDIO_SYNTH_LAYERS] = {0};
    uint32_t table_layers = 0;
    int32_t untabled = 0;
    const SynthSound *sound = ecs_get(world, e, SynthSound);
    const SynthEnvelope *env = ecs_get(world, e, SynthEnvelope);
    const SynthFilter *filter = ecs_get(world, e, SynthFilter);
    const SynthFilterEnvelope *fenv = ecs_get(world, e, SynthFilterEnvelope);
    const SynthOscillator *osc = ecs_get(world, e, SynthOscillator);
    const SynthGlide *glide = ecs_get(world, e, SynthGlide);
    const SynthPitchEnvelope *bend = ecs_get(world, e, SynthPitchEnvelope);
    const SynthDrive *drive = ecs_get(world, e, SynthDrive);
    int32_t extra = 0, orphans = 0, low = 0, lfos = 0, i;
    ecs_iter_t it;
    bool endless = false, fits = true, formant, result;
    if (sound) {
        p.seed = sound->seed;
        p.pitch_variance = sound->pitch_variance;
        p.level_variance = sound->level_variance;
        p.drift = sound->drift;
        p.free_phase = sound->free_phase;
    }

    if (glide) {
        p.glide = glide->time;
    }

    if (bend) {
        p.pitch_envelope = box_audio_synth_pitch_envelope(bend);
    }

    if (drive) {
        p.has_drive = 1;
        p.pre_drive = drive->drive;
        p.pre_asymmetry = drive->asymmetry;
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
        p.drive = filter->drive;
        p.filter_velocity = filter->velocity;
        p.keytrack = filter->keytrack;
        p.model = (uint32_t)filter->model;
        p.vowel = filter->vowel;
    }

    formant = filter && filter->kind == SynthFormant;
    if (filter && fenv && (fenv->amount != 0 || (formant && fenv->vowel != 0))) {
        p.filter_amount = formant ? 0.0f : fenv->amount;
        p.filter_vowel = formant ? fenv->vowel : 0.0f;
        p.filter_envelope_velocity = fenv->velocity;
        p.filter_envelope = (box_audio_synth_envelope_t){
            .attack = fenv->attack, .decay = fenv->decay, .sustain = fenv->sustain,
            .release = fenv->release, .hold = fenv->hold, .curve = fenv->curve,
            .attack_curve = fenv->attack_curve };
        fits = fits && box_audio_synth_envelope_fits(&p.filter_envelope);
    }

    if (osc && osc->modulation != SynthMix) {
        orphans ++;
    } else if (osc) {
        box_audio_synth_layer(&p, osc, NULL, NULL, ecs_get(world, e, SynthUnison),
            ecs_get(world, e, SynthString), ecs_get(world, e, SynthTable), tables, 0);
    }

    box_audio_synth_lfo(&p, ecs_get(world, e, SynthLfo), 0, &lfos);
    box_audio_synth_children(world, &p, tables, e, p.layers, &extra, &orphans, &lfos);
    it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            if (!ecs_has(world, it.entities[i], SynthOscillator)) {
                box_audio_synth_lfo(&p, ecs_get(world, it.entities[i], SynthLfo), 0, &lfos);
            }
        }
    }

    for (i = 0; i < (int32_t)p.layers; i ++) {
        box_audio_synth_layer_t *l = &p.layer[i];
        if (l->wave == BOX_AUDIO_SYNTH_PLUCK) {
            if (!(l->frequency >= BOX_AUDIO_SYNTH_PLUCK_LOWEST)) {
                l->frequency = BOX_AUDIO_SYNTH_PLUCK_LOWEST;
                low ++;
            }

            if (l->frequency_end > 0 && l->frequency_end < BOX_AUDIO_SYNTH_PLUCK_LOWEST) {
                l->frequency_end = BOX_AUDIO_SYNTH_PLUCK_LOWEST;
                low ++;
            }
        }

        if (l->wave == BOX_AUDIO_SYNTH_WAVETABLE) {
            table_layers |= 1u << i;
            untabled += !tables[i] || !ecs_is_alive(world, tables[i]) ||
                !ecs_has(world, tables[i], Wavetable);
        }

        if (l->has_envelope) {
            fits = fits && box_audio_synth_envelope_fits(&l->envelope);
        } else if (!env && l->wave == BOX_AUDIO_SYNTH_PLUCK) {
            l->rings_free = 1;
        } else if (!env) {
            endless = true;
        }
    }

    if (low) {
        char *path = ecs_get_path(world, e);
        ecs_warn("box.audio: synth '%s' plucks a string under %.0f Hz, it plays at %.0f Hz",
            path, (double)BOX_AUDIO_SYNTH_PLUCK_LOWEST, (double)BOX_AUDIO_SYNTH_PLUCK_LOWEST);
        ecs_os_free(path);
    }

    if (extra || endless || !fits || orphans || lfos || untabled) {
        char *path = ecs_get_path(world, e);
        if (extra) {
            ecs_err("box.audio: synth '%s' has %d layers, more than %d, the rest are left out",
                path, (int)p.layers + extra, BOX_AUDIO_SYNTH_LAYERS);
        }

        if (lfos) {
            ecs_err("box.audio: synth '%s' has %d LFOs, more than %d, the rest are left out",
                path, (int)p.lfos + lfos, BOX_AUDIO_SYNTH_LFOS);
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

        if (untabled) {
            ecs_err("box.audio: synth '%s' has %d wavetable layers without a SynthTable that "
                "names a Wavetable, they play a sine", path, (int)untabled);
        }

        ecs_os_free(path);
    }

    old = synth->patch;
    synth->patch = p;
    synth->table_layers = table_layers;
    ecs_os_memcpy_n(synth->tables, tables, ecs_entity_t, BOX_AUDIO_SYNTH_LAYERS);
    box_audio_synth_tables(world, state, synth);
    result = ecs_os_memcmp_t(&old, &synth->patch, box_audio_synth_patch_t) != 0;
    synth->compiled = true;
    state->stats.synths ++;

    return result;
}
