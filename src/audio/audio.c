/**
 * @file audio/audio.c
 * @brief Registers the box.audio components, the play and capture functions and their script
 * functions, and writes the WAV files of captures. Sound files are in sample.c, music streams
 * in music.c.
 */

#define BOX_AUDIO_IMPL
#include "module.h"
#include "../jobs/jobs.h"
#include <stdio.h>
#include <math.h>

/** Singleton through which the functions of the module find its state in a world */
typedef struct AudioModuleState {
    box_audio_module_t *state;  /**< The state */
} AudioModuleState;

static ECS_COMPONENT_DECLARE(AudioModuleState);
ECS_TAG_DECLARE(AudioListener);
ECS_COMPONENT_DECLARE(AudioSoundId);
ECS_COMPONENT_DECLARE(AudioBusState);
ECS_COMPONENT_DECLARE(AudioVoice);
ECS_COMPONENT_DECLARE(AudioReverbState);
ECS_COMPONENT_DECLARE(AudioSample);
ECS_COMPONENT_DECLARE(AudioMusic);
ECS_COMPONENT_DECLARE(AudioSynth);

/** Construct AudioBus values: unity volume to the master, centered, no send.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_bus_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    AudioBus *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (AudioBus){ .volume = 1.0f };
    }
}

/** Construct AudioSource values: a sound that plays at once at unity volume and pitch.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_source_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    AudioSource *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (AudioSource){ .volume = 1.0f, .pitch = 1.0f, .playing = true };
    }
}

/** Construct AudioStream values: a stream that loops and plays at once at unity volume.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_stream_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    AudioStream *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (AudioStream){ .volume = 1.0f, .loop = true, .playing = true };
    }
}

/** Construct AudioSpatial values: full volume within 1, silent from 50, rolloff 1.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_spatial_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    AudioSpatial *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (AudioSpatial){
            .min_distance = 1.0f, .max_distance = 50.0f, .rolloff = 1.0f };
    }
}

/** Construct Reverb values: a decay of 1.5 seconds, half damped, 20 ms of predelay, a middle
 * size, fully wet.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_reverb_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Reverb *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Reverb){
            .decay = 1.5f, .damping = 0.5f, .predelay = 0.02f, .size = 0.5f, .mix = 1.0f };
    }
}

/** Construct SynthOscillator values: a sine at 440 Hz at unity level, without a sweep.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_synth_oscillator_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    SynthOscillator *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (SynthOscillator){ .wave = SynthSine, .frequency = 440.0f, .level = 1.0f };
    }
}

/** Construct SynthFilter values: a lowpass at 1000 Hz and 12 dB per octave without
 * resonance.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_synth_filter_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    SynthFilter *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (SynthFilter){ .kind = SynthLowPass, .cutoff = 1000.0f };
    }
}

/** Construct AudioSettings values: unity master volume, not muted.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_settings_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    AudioSettings *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (AudioSettings){ .master_volume = 1.0f };
    }
}

/** Copy what box.audio sent for an entity: a copy starts with nothing sent. A prefab
 * instance or a clone gets a sound, a bus or a voice of its own this way.
 *
 * @param dst The copies
 * @param src The originals
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_track_copy(
    void *dst,
    const void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)src;
    ecs_os_memset(dst, 0, ti->size * count);
}

box_audio_module_t* box_audio_module_get(
    const ecs_world_t *world)
{
    const AudioModuleState *s = ecs_id(AudioModuleState) ?
        ecs_singleton_get(world, AudioModuleState) : NULL;

    return s ? s->state : NULL;
}

box_audio_module_stats_t box_audio_module_stats(
    const ecs_world_t *world)
{
    box_audio_module_t *state = box_audio_module_get(world);
    box_audio_module_stats_t result = {0};
    if (state) {
        result = state->stats;
    }

    return result;
}

int32_t box_audio_module_order(
    const ecs_world_t *world,
    ecs_entity_t *buses,
    int32_t max)
{
    box_audio_module_t *state = box_audio_module_get(world);
    int32_t i, result = 0;
    for (i = 0; state && i < state->order_count && result < max; i ++) {
        if (state->order[i]) {
            buses[result ++] = state->bus_entity[state->order[i]];
        }
    }

    return result;
}

int32_t box_audio_module_reverb_slot(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    box_audio_module_t *state = box_audio_module_get(world);
    const AudioReverbState *s = NULL;
    int32_t result = -1;
    if (!state || !e || !ecs_is_alive(world, e)) {
        goto done;
    }

    s = ecs_get(world, e, AudioReverbState);
    if (s && s->slot && state->reverb_entity[s->slot - 1] == e) {
        result = s->slot - 1;
    }

done:
    return result;
}

int32_t box_audio_module_sounds(
    const ecs_world_t *world)
{
    box_audio_module_t *state = box_audio_module_get(world);
    int32_t i, result = 0;
    for (i = 0; state && i < BOX_AUDIO_SOUND_MAX; i ++) {
        result += state->sound_used[i] != 0;
    }

    return result;
}

int32_t box_audio_module_bus_index(
    const ecs_world_t *world,
    ecs_entity_t bus)
{
    box_audio_module_t *state = box_audio_module_get(world);
    const AudioBusState *s = NULL;
    int32_t result = 0;
    if (!state || !bus || !ecs_is_alive(world, bus)) {
        goto done;
    }

    s = ecs_get(world, bus, AudioBusState);
    if (s && s->index && state->bus_entity[s->index] == bus) {
        result = s->index;
    }

done:
    return result;
}

ecs_entity_t box_audio_sound_from_pcm(
    ecs_world_t *world,
    ecs_entity_t sound,
    const float *data,
    int32_t frames,
    int32_t channels,
    float sample_rate)
{
    ecs_entity_t result = 0;
    AudioPcm *pcm;
    if (!data || frames <= 0 || (channels != 1 && channels != 2) || sample_rate < 0) {
        ecs_err("box_audio_sound_from_pcm: invalid samples");
        goto done;
    }

    result = sound ? sound : ecs_new(world);
    pcm = ecs_ensure(world, result, AudioPcm);
    ecs_vec_init_if_t(&pcm->samples, float);
    ecs_vec_set_count_t(NULL, &pcm->samples, float, frames * channels);
    ecs_os_memcpy_n(ecs_vec_first_t(&pcm->samples, float), data, float, frames * channels);
    pcm->channels = channels;
    pcm->sample_rate = sample_rate;
    ecs_modified(world, result, AudioPcm);
done:
    return result;
}

/** Whether an entity is a sound that box_audio_play and box_audio_play_at can play.
 *
 * @param world The world
 * @param sound The entity
 * @return Whether it is alive and has AudioPcm, SoundFile or SynthSound
 */
static bool box_audio_is_sound(
    ecs_world_t *world,
    ecs_entity_t sound)
{
    return sound && ecs_is_alive(world, sound) && (ecs_has(world, sound, AudioPcm) ||
        ecs_has(world, sound, SoundFile) || ecs_has(world, sound, SynthSound));
}

bool box_audio_play(
    ecs_world_t *world,
    ecs_entity_t sound,
    float volume)
{
    box_audio_module_t *state = box_audio_module_get(world);
    bool result = false;
    if (!state || !box_audio_is_sound(world, sound)) {
        goto done;
    }

    box_audio_play_t *play = ecs_vec_append_t(NULL, &state->plays, box_audio_play_t);
    *play = (box_audio_play_t){ .sound = sound, .volume = volume };
    result = true;
done:
    return result;
}

bool box_audio_play_at(
    ecs_world_t *world,
    ecs_entity_t sound,
    const vec3 position,
    float volume)
{
    box_audio_module_t *state = box_audio_module_get(world);
    bool result = false;
    if (!state || !position || !box_audio_is_sound(world, sound)) {
        goto done;
    }

    box_audio_play_t *play = ecs_vec_append_t(NULL, &state->plays, box_audio_play_t);
    *play = (box_audio_play_t){ .sound = sound, .volume = volume, .spatial = true,
        .position = { position[0], position[1], position[2] } };
    result = true;
done:
    return result;
}

int box_audio_capture(
    ecs_world_t *world,
    float seconds,
    const char *path)
{
    box_audio_module_t *state = box_audio_module_get(world);
    int result = -1;
    if (!state || !path || !path[0]) {
        ecs_err("box.audio: a capture needs a file");
        goto done;
    }

    if (!(seconds > 0) || seconds > BOX_AUDIO_CAPTURE_MAX) {
        ecs_err("box.audio: a capture takes more than 0 and at most %.0f seconds",
            (double)BOX_AUDIO_CAPTURE_MAX);
        goto done;
    }

    if (state->capture.path) {
        ecs_err("box.audio: a capture is already recording");
        goto done;
    }

    state->capture.seconds = seconds;
    state->capture.path = ecs_os_strdup(path);
    state->capture.serial = 0;
    result = 0;
done:
    return result;
}

bool box_audio_capture_pending(
    const ecs_world_t *world)
{
    box_audio_module_t *state = box_audio_module_get(world);

    return state && state->capture.path;
}

/** Write a little endian 16 bit value.
 *
 * @param out The bytes
 * @param value The value
 */
static void box_audio_wav_u16(
    uint8_t *out,
    uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFF);
    out[1] = (uint8_t)((value >> 8) & 0xFF);
}

/** Write a little endian 32 bit value.
 *
 * @param out The bytes
 * @param value The value
 */
static void box_audio_wav_u32(
    uint8_t *out,
    uint32_t value)
{
    box_audio_wav_u16(out, value & 0xFFFF);
    box_audio_wav_u16(out + 2, value >> 16);
}

/** Convert a sample to 16 bits, clamped and rounded to the nearest.
 *
 * @param x The sample
 * @return The 16 bit sample
 */
static int16_t box_audio_wav_sample(
    float x)
{
    float v = x != x ? 0.0f : (x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x));

    return (int16_t)floorf(v * 32767.0f + 0.5f);
}

int box_audio_wav_write(
    const char *path,
    const float *data,
    int32_t frames,
    int32_t sample_rate)
{
    uint32_t bytes = (uint32_t)frames * 4;
    uint8_t *wav = ecs_os_malloc((ecs_size_t)bytes + 44);
    char *tmp = flecs_asprintf("%s.tmp", path);
    FILE *f = NULL;
    int32_t i;
    int result = -1;
    if (!wav) {
        goto done;
    }

    ecs_os_memcpy(wav, "RIFF", 4);
    box_audio_wav_u32(wav + 4, 36 + bytes);
    ecs_os_memcpy(wav + 8, "WAVEfmt ", 8);
    box_audio_wav_u32(wav + 16, 16);
    box_audio_wav_u16(wav + 20, 1);
    box_audio_wav_u16(wav + 22, 2);
    box_audio_wav_u32(wav + 24, (uint32_t)sample_rate);
    box_audio_wav_u32(wav + 28, (uint32_t)sample_rate * 4);
    box_audio_wav_u16(wav + 32, 4);
    box_audio_wav_u16(wav + 34, 16);
    ecs_os_memcpy(wav + 36, "data", 4);
    box_audio_wav_u32(wav + 40, bytes);
    for (i = 0; i < frames * 2; i ++) {
        box_audio_wav_u16(wav + 44 + i * 2, (uint16_t)box_audio_wav_sample(data[i]));
    }

    f = fopen(tmp, "wb");
    if (!f) {
        goto done;
    }

    if (fwrite(wav, 1, (size_t)bytes + 44, f) != (size_t)bytes + 44) {
        fclose(f);
        remove(tmp);
        goto done;
    }

    fclose(f);
    if (rename(tmp, path)) {
        remove(tmp);
        goto done;
    }

    result = 0;
done:
    if (result) {
        ecs_err("box.audio: cannot write the capture '%s'", path);
    }

    ecs_os_free(wav);
    ecs_os_free(tmp);

    return result;
}

/** The box.audio.play script function.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The sound and the volume
 * @param result Receives whether the play was queued
 */
static void box_audio_play_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_entity_t sound = *(ecs_entity_t*)argv[0].ptr;
    float volume = *(float*)argv[1].ptr;
    (void)argc;
    *(bool*)result->ptr = box_audio_play(ctx->world, sound, volume);
}

/** The box.audio.play_at script function.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The sound, the position and the volume
 * @param result Receives whether the play was queued
 */
static void box_audio_play_at_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_entity_t sound = *(ecs_entity_t*)argv[0].ptr;
    const float *position = argv[1].ptr;
    float volume = *(float*)argv[2].ptr;
    (void)argc;
    *(bool*)result->ptr = box_audio_play_at(ctx->world, sound, position, volume);
}

/** The box.audio.capture script function.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The seconds and the file
 * @param result Receives whether the capture was queued
 */
static void box_audio_capture_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    float seconds = *(float*)argv[0].ptr;
    const char *path = *(const char**)argv[1].ptr;
    (void)argc;
    *(bool*)result->ptr = box_audio_capture(ctx->world, seconds, path) == 0;
}

/** Free the state of the module once the world deleted its entities.
 *
 * @param world The world
 * @param ctx The state
 */
static void box_audio_module_free(
    ecs_world_t *world,
    void *ctx)
{
    box_audio_module_t *state = ctx;
    (void)world;
    ecs_os_free(state->writer.data);
    ecs_map_fini(&state->voices);
    ecs_vec_fini_t(NULL, &state->oneshots, box_audio_oneshot_t);
    ecs_vec_fini_t(NULL, &state->plays, box_audio_play_t);
    ecs_vec_fini_t(NULL, &state->stops, uint32_t);
    ecs_vec_fini_t(NULL, &state->releases, uint32_t);
    box_audio_music_fini(state);
    ecs_vec_fini_t(NULL, &state->moved, ecs_entity_t);
    box_audio_sample_fini(state);
    ecs_os_free(state->capture.path);
    ecs_os_free(state);
}

/** Register the script functions of the module.
 *
 * @param world The world
 */
static void box_audio_functions_init(
    ecs_world_t *world)
{
    ecs_function(world, {
        .name = "play",
        .parent = ecs_get_scope(world),
        .return_type = ecs_id(ecs_bool_t),
        .params = {
            { .name = "sound", .type = ecs_id(ecs_entity_t) },
            { .name = "volume", .type = ecs_id(ecs_f32_t) }
        },
        .callback = box_audio_play_function
    });

    ecs_function(world, {
        .name = "play_at",
        .parent = ecs_get_scope(world),
        .return_type = ecs_id(ecs_bool_t),
        .params = {
            { .name = "sound", .type = ecs_id(ecs_entity_t) },
            { .name = "position", .type = ecs_id(vec3) },
            { .name = "volume", .type = ecs_id(ecs_f32_t) }
        },
        .callback = box_audio_play_at_function
    });

    ecs_function(world, {
        .name = "capture",
        .parent = ecs_get_scope(world),
        .return_type = ecs_id(ecs_bool_t),
        .params = {
            { .name = "seconds", .type = ecs_id(ecs_f32_t) },
            { .name = "path", .type = ecs_id(ecs_string_t) }
        },
        .callback = box_audio_capture_function
    });
}

void BoxAudioImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxAudio);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxJobs);

    ECS_META_COMPONENT(world, AudioState);
    ECS_META_COMPONENT(world, AudioBus);
    ecs_set_hooks(world, AudioBus, { .ctor = box_audio_bus_ctor });
    ECS_META_COMPONENT(world, AudioOutput);
    ECS_META_COMPONENT(world, AudioPcm);
    box_owned_hooks(world, AudioPcm, BOX_OWNED_VEC(AudioPcm, samples, float));
    ECS_META_COMPONENT(world, SoundFile);
    box_owned_hooks(world, SoundFile, BOX_OWNED_STR(SoundFile, path));
    ECS_META_COMPONENT(world, SoundState);
    ECS_META_COMPONENT(world, SoundStatus);
    box_owned_hooks(world, SoundStatus, BOX_OWNED_STR(SoundStatus, error));
    ECS_META_COMPONENT(world, AudioSource);
    ecs_set_hooks(world, AudioSource, { .ctor = box_audio_source_ctor });
    ECS_META_COMPONENT(world, AudioStream);
    box_owned_hooks(world, AudioStream, BOX_OWNED_STR(AudioStream, path));
    ecs_set_hooks(world, AudioStream, { .ctor = box_audio_stream_ctor });
    ECS_META_COMPONENT(world, AudioStreamState);
    ECS_META_COMPONENT(world, AudioStreamStatus);
    box_owned_hooks(world, AudioStreamStatus, BOX_OWNED_STR(AudioStreamStatus, error));
    ECS_TAG_DEFINE(world, AudioListener);
    ECS_META_COMPONENT(world, AudioSpatial);
    ecs_set_hooks(world, AudioSpatial, { .ctor = box_audio_spatial_ctor });
    ECS_META_COMPONENT(world, Reverb);
    ecs_set_hooks(world, Reverb, { .ctor = box_audio_reverb_ctor });
    ECS_META_COMPONENT(world, SynthWave);
    ECS_META_COMPONENT(world, SynthFilterKind);
    ECS_META_COMPONENT(world, SynthFilterSlope);
    ECS_META_COMPONENT(world, SynthOscillator);
    ecs_set_hooks(world, SynthOscillator, { .ctor = box_audio_synth_oscillator_ctor });
    ECS_META_COMPONENT(world, SynthEnvelope);
    ECS_META_COMPONENT(world, SynthFilter);
    ecs_set_hooks(world, SynthFilter, { .ctor = box_audio_synth_filter_ctor });
    ECS_META_COMPONENT(world, SynthFilterEnvelope);
    ECS_META_COMPONENT(world, SynthGlide);
    ECS_META_COMPONENT(world, SynthSound);
    ECS_META_COMPONENT(world, AudioSettings);
    ecs_set_hooks(world, AudioSettings, { .ctor = box_audio_settings_ctor });
    ECS_META_COMPONENT(world, AudioStatus);

    ECS_COMPONENT_DEFINE(world, AudioSoundId);
    ECS_COMPONENT_DEFINE(world, AudioBusState);
    ECS_COMPONENT_DEFINE(world, AudioVoice);
    ECS_COMPONENT_DEFINE(world, AudioReverbState);
    ECS_COMPONENT_DEFINE(world, AudioSample);
    ECS_COMPONENT_DEFINE(world, AudioMusic);
    ECS_COMPONENT_DEFINE(world, AudioSynth);
    ecs_set_hooks(world, AudioSoundId, { .copy = box_audio_track_copy });
    ecs_set_hooks(world, AudioSynth, { .copy = box_audio_track_copy });
    ecs_set_hooks(world, AudioBusState, { .copy = box_audio_track_copy });
    ecs_set_hooks(world, AudioVoice, { .copy = box_audio_track_copy });
    ecs_set_hooks(world, AudioReverbState, { .copy = box_audio_track_copy });
    ecs_set_hooks(world, AudioSample, { .copy = box_audio_track_copy });
    ecs_set_hooks(world, AudioMusic, { .copy = box_audio_track_copy });
    ecs_add_pair(world, ecs_id(AudioPcm), EcsWith, ecs_id(AudioSoundId));
    ecs_add_pair(world, ecs_id(AudioBus), EcsWith, ecs_id(AudioBusState));
    ecs_add_pair(world, ecs_id(AudioSource), EcsWith, ecs_id(AudioVoice));
    ecs_add_pair(world, ecs_id(Reverb), EcsWith, ecs_id(AudioReverbState));
    ecs_add_pair(world, ecs_id(SoundFile), EcsWith, ecs_id(AudioSample));
    ecs_add_pair(world, ecs_id(AudioStream), EcsWith, ecs_id(AudioMusic));
    ecs_add_pair(world, ecs_id(SynthSound), EcsWith, ecs_id(AudioSoundId));
    ecs_add_pair(world, ecs_id(SynthSound), EcsWith, ecs_id(AudioSynth));

    ecs_entity(world, { .name = "master" });
    ecs_singleton_add(world, AudioSettings);
    ecs_singleton_set(world, AudioStatus, {0});

    box_audio_module_t *state = ecs_os_calloc_t(box_audio_module_t);
    state->next_voice = 1;
    ecs_map_init(&state->voices, NULL);
    ecs_vec_init_t(NULL, &state->oneshots, box_audio_oneshot_t, 0);
    ecs_vec_init_t(NULL, &state->plays, box_audio_play_t, 0);
    ecs_vec_init_t(NULL, &state->stops, uint32_t, 0);
    ecs_vec_init_t(NULL, &state->releases, uint32_t, 0);
    ecs_vec_init_t(NULL, &state->moved, ecs_entity_t, 0);
    ECS_COMPONENT_DEFINE(world, AudioModuleState);
    ecs_singleton_set(world, AudioModuleState, { .state = state });
    ecs_atfini(world, box_audio_module_free, state);

    box_audio_sync_init(world, state);
    box_audio_sample_init(world, state);
    box_audio_music_init(world, state);
    box_audio_synth_init(world);
    box_audio_functions_init(world);
}
