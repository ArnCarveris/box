/**
 * @file box/audio.h
 * @brief Data driven audio: buses, sources, sound files, music streams, one shot plays,
 * settings, status and captures.
 *
 * Components that a script, the REST API or the set command write, which one system at the end
 * of the frame turns into one command batch for the audio core (docs/audio.md). The same
 * script plays the same on native, on the web and headless.
 */

#ifndef BOX_AUDIO_H
#define BOX_AUDIO_H

#include "types.h"

#ifdef BOX_AUDIO_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Longest capture in seconds */
#define BOX_AUDIO_CAPTURE_MAX (60.0f)

/** Oscillators of a synth sound: the root and its children with a SynthOscillator. A voice
 * renders and stores only the layers its sound has */
#define BOX_AUDIO_SYNTH_LAYERS 8

/** LFOs of a synth sound: the SynthLfo of the root, of its layers and of its other children */
#define BOX_AUDIO_SYNTH_LFOS 2

/** Seconds a synth sound plays when it would never end, a layer without any envelope */
#define BOX_AUDIO_SYNTH_ENDLESS (10.0f)

/** Lowest frequency of a SynthPluck layer in Hz; a lower one, its frequency_end included, is
 * clamped with one warning */
#define BOX_AUDIO_SYNTH_PLUCK_LOWEST (40.0f)

/** What the audio output does */
ECS_ENUM(AudioState, {
    AudioOff = 0,               /**< No output: a world that runs no app, or --no-audio */
    AudioNoDevice = 1,          /**< No device, or it was lost: the main thread renders the
                                 *   frame deltas, as headless runs do */
    AudioWaitingForGesture = 2, /**< The page waits for a key or a click to start its audio;
                                 *   plays that do not loop are dropped meanwhile */
    AudioRunning = 3,           /**< A device or the AudioWorklet of the page plays */
    AudioFailed = 4,            /**< The page could not start its audio, it runs silent */
    AudioStarting = 5,          /**< The page loads the worklet of the core */
    AudioSuspended = 6          /**< The page is hidden: its audio faded out and suspended */
});

/** A mixer bus. Sources and plays mix into a bus, a bus into its output, the master last.
 *
 * Up to 31 buses besides the master, which scripts can name master (box.audio.master), an
 * entity without components. A cycle through outputs and sends logs one error that names the
 * buses and keeps the last valid order. A Disabled bus is silent.
 */
ECS_STRUCT(AudioBus, {
    ecs_entity_t output;   /**< Bus it feeds, 0 (or a deleted bus) for the master */
    float volume;          /**< Linear gain, default 1 */
    float pan;             /**< -1 left to 1 right, default 0 */
    bool mute;             /**< Silence the bus, default false */
    ecs_entity_t send;     /**< Bus that also receives the output, after the volume, 0 for none */
    float send_level;      /**< Linear gain of the send, default 0 */
});

/** A reverb on the bus of the same entity, which sources reach through the send of their bus.
 *
 * One reverb runs at once (BOX_AUDIO_REVERB_MAX of the core); another logs one error and its
 * bus stays dry. Decay, damping and mix change smoothly; a change of size or predelay fades the
 * wet signal out and in. Deleting the entity, as unloading its scene does, fades the tail out.
 */
ECS_STRUCT(Reverb, {
    float decay;           /**< Seconds until -60 dB, default 1.5 */
    float damping;         /**< High frequency loss, 0 to 1, default 0.5 */
    float predelay;        /**< Seconds before the first reflection, default 0.02 */
    float size;            /**< Room scale, 0 to 1, default 0.5 */
    float mix;             /**< Wet level, 0 to 1, default 1 on a send bus; the dry signal
                            *   passes at 1 - mix */
});

/** The bus a sound or a source plays on. On a source it wins over the one of its sound */
ECS_STRUCT(AudioOutput, {
    ecs_entity_t bus;      /**< Bus entity, 0 for the master */
});

/** Samples of a sound, the sound kind of this module. Owns its samples */
ECS_STRUCT(AudioPcm, {
    ecs_vec(float) samples; /**< Frames, interleaved when stereo */
    int32_t channels;      /**< 1 or 2, 0 for 1 */
    float sample_rate;     /**< Frames per second, 0 for the rate of the output */
});

/** A sound decoded from a WAV or FLAC file on the job system, found as a TexturePath is.
 *
 * Decoded fully at the rate of the file, mono or stereo, more channels mixed down; every
 * SoundFile with the same path shares one copy. A play of a sound that still loads starts once
 * ready when it loops and is dropped otherwise. On the web the decode stalls its frame.
 */
ECS_STRUCT(SoundFile, {
    char *path;            /**< Audio file, resolved as a texture path is */
});

/** Progress of the load of a SoundFile */
ECS_ENUM(SoundState, {
    SoundLoading = 0,      /**< The file decodes */
    SoundReady = 1,        /**< The samples are decoded and play */
    SoundFailed = 2        /**< The load failed, error says why */
});

/** Outcome of the load of a SoundFile, written by box.audio and read only for applications */
ECS_STRUCT(SoundStatus, {
    SoundState state;      /**< Progress of the load */
    int32_t frames;        /**< Frames of the decoded sound, 0 unless ready */
    int32_t channels;      /**< 1 or 2 after the mix down, 0 unless ready */
    float sample_rate;     /**< Frames per second of the file, 0 unless ready */
    char *error;           /**< Why the load failed, NULL unless state is SoundFailed */
});

/** A sound that plays while the entity exists and playing is true.
 *
 * A sound that does not loop sets playing to false when it ends; set it to true to play again.
 * A Disabled source is silent and deleting a source stops it with a short fade.
 */
ECS_STRUCT(AudioSource, {
    ecs_entity_t sound;    /**< The sound to play, an entity with AudioPcm, SoundFile or
                            *   SynthSound; a looping synth starts over at the end of its
                            *   envelopes */
    float volume;          /**< Linear gain, default 1 */
    float pitch;           /**< Ratio of the playback speed, default 1 */
    bool loop;             /**< Play the sound over and over, default false */
    bool playing;          /**< Whether it plays, default true */
});

/** Music: one Ogg Vorbis file that streams while the entity exists and playing is true.
 *
 * Decoded ahead in chunks, never whole: on a job natively, on the main thread for a bounded
 * time per frame on the web, which fetches the file next to the page if its file system lacks it.
 *
 * One stream plays at once; a second one logs one error and is silent. A new path fades the old
 * stream out and starts the new one. At its end a stream that does not loop sets playing to
 * false; a delete fades it out and frees it. With an AudioOutput it plays on that bus.
 */
ECS_STRUCT(AudioStream, {
    char *path;            /**< An .ogg file, found as a TexturePath is */
    float volume;          /**< Linear gain, default 1 */
    bool loop;             /**< Start over at the end without a gap, default true */
    bool playing;          /**< Whether it plays, default true; false fades it out and true
                            *   resumes it where it stopped */
});

/** Progress of an AudioStream */
ECS_ENUM(AudioStreamState, {
    AudioStreamLoading = 0,  /**< The file is fetched or opened, or decodes ahead of its start */
    AudioStreamPlaying = 1,  /**< It plays */
    AudioStreamStopped = 2,  /**< playing is false, or no output plays it */
    AudioStreamEnded = 3,    /**< A stream that does not loop played to its end */
    AudioStreamFailed = 4    /**< It cannot play, error says why */
});

/** What an AudioStream does, written by box.audio and read only for applications */
ECS_STRUCT(AudioStreamStatus, {
    AudioStreamState state; /**< What it does */
    int32_t frames;        /**< Frames of the file, 0 until it opened */
    int32_t channels;      /**< 1 or 2 after the mix down, 0 until it opened */
    float sample_rate;     /**< Frames per second of the file, 0 until it opened */
    float position;        /**< Seconds into the file of what plays */
    float buffered;        /**< Seconds decoded ahead of what plays */
    int32_t underruns;     /**< Times the output ran out of decoded frames and faded to silence */
    float decode_ms;       /**< Milliseconds the last decode took; on the web the main thread
                            *   spends it in its frame */
    float decode_max_ms;   /**< Largest decode_ms so far */
    char *error;           /**< Why it failed, NULL unless state is AudioStreamFailed */
});

/** Wave of a synth oscillator */
ECS_ENUM(SynthWave, {
    SynthSine,             /**< Sine */
    SynthSaw,              /**< Saw, band limited */
    SynthSquare,           /**< Square, band limited */
    SynthTriangle,         /**< Triangle */
    SynthNoise,            /**< White noise, seeded per play from SynthSound.seed */
    SynthPluck             /**< A plucked string (Karplus-Strong), set by the SynthString of
                            *   the layer; it rings out on its own and ends the sound when it
                            *   is silent, so it needs no envelope */
});

/** Kind of the filter of a synth sound */
ECS_ENUM(SynthFilterKind, {
    SynthLowPass,          /**< Lowpass */
    SynthHighPass,         /**< Highpass */
    SynthBandPass          /**< Bandpass, unity at the cutoff */
});

/** Slope of the filter of a synth sound */
ECS_ENUM(SynthFilterSlope, {
    SynthSlope12,          /**< 12 dB per octave, one stage */
    SynthSlope24           /**< 24 dB per octave, two cascaded stages */
});

/** Wave of a synth LFO */
ECS_ENUM(SynthLfoWave, {
    SynthLfoSine,          /**< Sine, from 0 rising */
    SynthLfoTriangle,      /**< Triangle, from 0 rising */
    SynthLfoSquare,        /**< Square, from its top, its edges smoothed over 2 ms */
    SynthLfoSaw,           /**< Rising saw, from 0, its drop smoothed over 2 ms */
    SynthLfoRandom         /**< Sample and hold: a random value each period, seeded per play
                            *   from SynthSound.seed, its steps smoothed over 2 ms */
});

/** What a synth oscillator does with its signal */
ECS_ENUM(SynthModulation, {
    SynthMix,              /**< It is heard, added to the mix of the layers */
    SynthPhase,            /**< It modulates the phase of its parent layer (FM), not heard */
    SynthRing              /**< It multiplies its parent layer (ring modulation), not heard */
});

/** An oscillator of a synth sound, a layer: on the root of the sound, on a child of it, or on
 * a child of a layer.
 *
 * Up to BOX_AUDIO_SYNTH_LAYERS layers, the root first, then each layer before its children;
 * more log one error and are left out.
 *
 * A layer with the modulation SynthPhase or SynthRing modulates its parent layer by its wave
 * times its own envelope, so the index follows that envelope. It is heard only when audible.
 */
ECS_STRUCT(SynthOscillator, {
    SynthWave wave;        /**< The wave, default SynthSine */
    float frequency;       /**< Hz, default 440, ignored by noise */
    float frequency_end;   /**< Hz the pitch sweeps to, 0 for no sweep */
    float sweep;           /**< Seconds of the sweep, exponential in frequency */
    float level;           /**< Linear gain, default 1 */
    SynthModulation modulation; /**< What it does with its signal, default SynthMix */
    float index;           /**< SynthPhase: peak index in radians at the top of its envelope,
                            *   0..32, limited against the frequency so FM aliases little;
                            *   SynthRing: depth 0..1. Default 1 */
    float feedback;        /**< Radians of its own output on its phase, 0..1.5, a sawlike tone */
    bool audible;          /**< Whether a modulator is heard as well, at its level */
    float pulse_width;     /**< SynthSquare: share of the period it is high, 0.01..0.99,
                            *   default 0.5; band limited at every width, without DC */
});

/** The string of a SynthPluck layer, on the layer: a delay line a period long, plucked with a
 * burst of seeded noise, whose loop damps it. Without one a SynthPluck layer takes the defaults.
 *
 * The fundamental falls by 60 dB over decay seconds; the harmonics fall faster the darker the
 * string. The pitch is tuned to a fraction of a frame, so it is exact at high notes too. The
 * frequency, frequency_end, sweep and level of the layer apply; phase modulation does not.
 */
ECS_STRUCT(SynthString, {
    float decay;           /**< Seconds the fundamental takes to fall by 60 dB, 0.01..60,
                            *   default 2 */
    float brightness;      /**< Lowpass of the pluck and in the loop: 0 dark, the harmonics
                            *   start soft and die fast, to 1 bright, a metallic ring;
                            *   default 0.5 */
    float hardness;        /**< Lowpass of the noise that plucks: 0 a soft finger, 1 a hard
                            *   pick; default 0.8 */
});

/** An envelope: from 0 to 1 over attack, to sustain over decay, hold seconds there, then to
 * 0 over release.
 *
 * Each segment is linear by default. A positive curve bends it toward an exponential shape,
 * fast at first and slow toward its target, as analog envelopes do, and still reaches the
 * target at the end of its time; a negative one bends it the other way. Plucks use a curve of
 * 4 to 6, pads 2 to 3.
 *
 * On the root it applies to every layer without its own, on a layer to that layer. A sound
 * ends once every envelope reached silence, so a one shot without a release still ends. A
 * layer without any envelope would never end: it logs one error and the sound plays for
 * BOX_AUDIO_SYNTH_ENDLESS seconds.
 */
ECS_STRUCT(SynthEnvelope, {
    float attack;          /**< Seconds from 0 to 1, at least 0.5 ms so it does not click */
    float decay;           /**< Seconds from 1 to the sustain level */
    float sustain;         /**< Level after the decay, 0..1, default 0 */
    float release;         /**< Seconds from the sustain level to 0 */
    float hold;            /**< Seconds at the sustain level before the release */
    float curve;           /**< Curve of the decay and the release, 0 linear (default),
                            *   -16..16 */
    float attack_curve;    /**< Curve of the attack, 0 linear (default), -16..16 */
});

/** Unison of a synth layer, on the entity of its SynthOscillator: the layer plays as several
 * copies of its oscillator, detuned evenly over a spread in cents and placed from left to right
 * over the stereo width, each starting at a seeded random phase.
 *
 * The voices share the level of the layer: each plays at 1 / sqrt(voices), so the level of the
 * layer stays about the same as the count changes. One voice is the layer without unison. A
 * sound in which a layer has voices and width renders in stereo, see docs/audio.md.
 */
ECS_STRUCT(SynthUnison, {
    int32_t voices;        /**< Copies of the oscillator, 1 to 7, default 3 */
    float detune;          /**< Cents from the lowest copy to the highest, 0 to 100,
                            *   default 10 */
    float width;           /**< 0 (every copy in the center, mono) to 1 (the lowest copy
                            *   left, the highest right), default 0 */
});

/** The filter of a synth sound, on its root: a state variable filter on the mix of the layers,
 * stable at any cutoff and resonance */
ECS_STRUCT(SynthFilter, {
    SynthFilterKind kind;  /**< The kind, default SynthLowPass */
    float cutoff;          /**< Hz, default 1000, clamped under Nyquist */
    float resonance;       /**< 0 (a Butterworth response) to 1 (rings the longest) */
    SynthFilterSlope slope; /**< Steepness, default SynthSlope12; at SynthSlope24 the resonance
                            *   applies to the second stage only */
});

/** An envelope of the cutoff of the SynthFilter of the same root, with the segments of a
 * SynthEnvelope: the cutoff is cutoff times 2 to the amount times the level of the envelope,
 * so it rises over the attack, settles at the sustain and returns over the release.
 *
 * It does not keep a sound playing: the sound ends with its SynthEnvelope. Without a
 * SynthFilter it does nothing.
 */
ECS_STRUCT(SynthFilterEnvelope, {
    float attack;          /**< Seconds from 0 to 1, at least 0.5 ms */
    float decay;           /**< Seconds from 1 to the sustain level */
    float sustain;         /**< Level after the decay, 0..1, default 0 */
    float release;         /**< Seconds from the sustain level to 0 */
    float hold;            /**< Seconds at the sustain level before the release */
    float curve;           /**< Curve of the decay and the release, 0 linear (default),
                            *   -16..16 */
    float attack_curve;    /**< Curve of the attack, 0 linear (default), -16..16 */
    float amount;          /**< Octaves the cutoff moves at level 1, -10..10, negative closes
                            *   the filter, default 0 */
});

/** Portamento and mono legato of a synth sound, on its root.
 *
 * With a time, a change of the pitch of a voice, as a change of AudioSource.pitch, glides
 * exponentially in frequency, linear in cents, over that time instead of jumping; a time of 0
 * jumps as without the component.
 *
 * With legato, a play of the sound while the last one shot of its channel holds, or a new
 * sound of an AudioSource that plays one, glides that voice to the new frequencies without a
 * restart of its envelopes. After its release a note starts a new voice. See docs/audio.md.
 */
ECS_STRUCT(SynthGlide, {
    float time;            /**< Seconds of a glide, 0 to 10, default 0 */
    bool legato;           /**< Play as a note of a mono voice, default false */
    int32_t channel;       /**< Mono voice of the one shots of legato sounds, default 0:
                            *   legato sounds of the same channel share one voice */
});

/** An LFO of a synth sound: a wave at a rate that moves the pitch, the level, the cutoff and
 * the pulse width of the sound, each by its own depth, after a delay and a fade in.
 *
 * On the root, or on a child of the root without a SynthOscillator, it moves every layer; on
 * a layer that layer, the cutoff aside. Up to BOX_AUDIO_SYNTH_LFOS per sound, see LFOs in
 * docs/audio.md for their order, the depths and the restart of a looping sound.
 */
ECS_STRUCT(SynthLfo, {
    SynthLfoWave wave;     /**< The wave, default SynthLfoSine */
    float rate;            /**< Hz, 0..100, default 5 */
    float delay;           /**< Seconds after the start of the note before it moves anything */
    float fade;            /**< Seconds it fades in over after its delay, 0 at once */
    float phase;           /**< Phase it starts at in turns, 0..1, 0.25 is the top of a sine */
    bool random_phase;     /**< Start at a random phase per play instead, seeded from
                            *   SynthSound.seed and the count of plays */
    float pitch;           /**< Cents of vibrato at the top of the wave, -2400..2400 */
    float amplitude;       /**< Tremolo: share of the level it takes at the bottom of the
                            *   wave, 0..1 */
    float cutoff;          /**< Octaves of the cutoff of the SynthFilter at the top of the
                            *   wave, -10..10 */
    float pulse_width;     /**< Share of the period the pulse width of squares moves by at the
                            *   top of the wave, -0.49..0.49 */
});

/** A sound made by the synth from the components of this entity and its children, without a
 * file: SynthOscillator layers, each with an optional SynthUnison, a SynthEnvelope, a
 * SynthFilter, a SynthFilterEnvelope, a SynthGlide and SynthLfo. Usually a prefab.
 *
 * It is compiled once, and again when one of its components changes, into a patch the core
 * holds; box_audio_play and AudioSource play it like any sound, with no allocation per play.
 */
ECS_STRUCT(SynthSound, {
    uint32_t seed;         /**< Seed of the noise and the variances, with a count of plays */
    float pitch_variance;  /**< Random share of the pitch per play, 0..1, default 0 */
    float level_variance;  /**< Random share of the level per play, 0..1, default 0 */
});

/** The ears of the world: sources with AudioSpatial are heard from its WorldTransform.
 *
 * Without one the camera of the first enabled View that has one listens. With several the
 * first, by entity id, listens and one warning is logged.
 */
extern ECS_TAG_DECLARE(AudioListener);

/** Places an AudioSource in the world: it follows the WorldTransform of the entity, parents
 * included, gets quieter with its distance from the listener and pans with its direction.
 *
 * The gain is inverse distance, min / (min + rolloff * (d - min)) beyond min_distance, faded so
 * that it is exactly 0 at max_distance. Front and back sound alike.
 *
 * A looping source beyond max_distance uses no voice and keeps its place in time; a source
 * that does not loop does not start beyond it. On a sound it sets the distances of
 * box_audio_play_at.
 */
ECS_STRUCT(AudioSpatial, {
    float min_distance;    /**< Full volume within this distance, default 1 */
    float max_distance;    /**< Silent from this distance, default 50 */
    float rolloff;         /**< How fast the volume falls beyond min_distance, default 1 */
});

/** Settings of the audio of the world, a singleton */
ECS_STRUCT(AudioSettings, {
    float master_volume;   /**< Linear gain of the master bus, default 1 */
    bool mute;             /**< Silence everything, default false */
});

/** What the audio output does, a singleton written by box.audio and read only for apps */
ECS_STRUCT(AudioStatus, {
    AudioState state;      /**< What the output does */
    int32_t sample_rate;   /**< Frames per second of the output, 0 while not known */
    int32_t voices;        /**< Voices that play, without the ones that fade out */
    int64_t voices_stolen; /**< Voices the voice cap of 64 stopped so far */
    float load;            /**< Share of real time the render takes, 0 where it is not
                            *   measured (the web) */
    int32_t buses;         /**< Buses in the processing order, the master included */
    int64_t clock;         /**< Frame of the core that renders now, as far as the main thread
                            *   can tell; stops while the output does not run, 0 for a new
                            *   output */
});

/** The transport of the timeline in beats, a singleton: the tempo, the time signature, the
 * position, play and stop, the loop and the metronome. A beat is a quarter note in every time
 * signature, so a bar is beats_per_bar * 4 / beat_unit beats.
 *
 * While it plays, box.audio writes position every frame from the clock of the output, the
 * beat the core renders now. A write of another position is a seek. See Transport in
 * docs/audio.md.
 */
ECS_STRUCT(Transport, {
    float tempo;           /**< Quarter notes per minute, 20..999, default 120 */
    int32_t beats_per_bar; /**< Top of the time signature, 1..64, default 4 */
    int32_t beat_unit;     /**< Bottom of the time signature, 1..64, default 4 */
    double position;       /**< Beats; written by box.audio while it plays, a write seeks */
    bool playing;          /**< Whether the timeline plays, default false: false stops at the
                            *   position, true plays on from it */
    bool loop;             /**< Wrap from loop_end to loop_start, default false */
    double loop_start;     /**< First beat of the loop */
    double loop_end;       /**< Beat the loop wraps at, after loop_start */
    float lookahead;       /**< Seconds scheduled ahead of the clock, 0.05..0.5, default
                            *   0.15 */
    bool metronome;        /**< Click on every beat while playing, accented on the first of a
                            *   bar, default false */
});

/** Give an entity the samples of a sound, to play with AudioSource or box_audio_play.
 *
 * @param world The world
 * @param sound The entity, 0 to create one
 * @param data Frames, interleaved when stereo, copied
 * @param frames Number of frames
 * @param channels 1 or 2
 * @param sample_rate Frames per second, 0 for the rate of the output
 * @return The sound, 0 when the arguments are invalid
 */
ecs_entity_t box_audio_sound_from_pcm(
    ecs_world_t *world,
    ecs_entity_t sound,
    const float *data,
    int32_t frames,
    int32_t channels,
    float sample_rate);

/** Play a sound once on the bus of its AudioOutput, without an entity per play.
 *
 * The play goes out with the command batch at the end of the frame. While the page waits for a
 * gesture, or while the SoundFile of the sound still loads, it is dropped. Scripts call it as
 * box.audio.play(sound, volume).
 *
 * @param world The world
 * @param sound The sound, an entity with AudioPcm, SoundFile or SynthSound
 * @param volume Linear gain
 * @return Whether the play was queued
 */
bool box_audio_play(
    ecs_world_t *world,
    ecs_entity_t sound,
    float volume);

/** Play a sound once at a place in the world, heard from the listener as an AudioSpatial
 * source that stays where it started.
 *
 * The distances are those of the AudioSpatial of the sound, the defaults without one. A play
 * beyond max_distance of the listener does not start. Scripts call it as
 * box.audio.play_at(sound, position, volume).
 *
 * @param world The world
 * @param sound The sound, an entity with AudioPcm, SoundFile or SynthSound
 * @param position World position of the play
 * @param volume Linear gain
 * @return Whether the play was queued
 */
bool box_audio_play_at(
    ecs_world_t *world,
    ecs_entity_t sound,
    const vec3 position,
    float volume);

/** Gain and pan of a spatial source, as box.audio computes them.
 *
 * The pan is the component of the direction along the right axis of the listener over the
 * distance; within min_distance it is divided by min_distance instead, so a source that passes
 * through the listener moves across instead of jumping from one side to the other.
 *
 * @param spatial The distances, NULL for the defaults
 * @param listener World transform of the listener
 * @param position World position of the source
 * @param pan Receives the pan, -1 left to 1 right, may be NULL
 * @return The gain, 0 at and beyond max_distance
 */
float box_audio_spatial_gain(
    const AudioSpatial *spatial,
    const mat4 listener,
    const vec3 position,
    float *pan);

/** Record the master output of the next seconds to a 16 bit stereo WAV file.
 *
 * The capture taps the core wherever it runs, so it works with every output. The file is
 * written in one rename once the frames are in. Headless with a fixed delta time two runs
 * write identical files.
 *
 * @param world The world
 * @param seconds Seconds to record, at most BOX_AUDIO_CAPTURE_MAX
 * @param path The file
 * @return 0 when the capture started, nonzero with an error logged when it did not
 */
int box_audio_capture(
    ecs_world_t *world,
    float seconds,
    const char *path);

/** Check whether a capture still records.
 *
 * @param world The world
 * @return Whether a capture has not written its file yet
 */
bool box_audio_capture_pending(
    const ecs_world_t *world);

/** Register the box.audio module. Importing it opens no device.
 *
 * @param world The world
 */
void BoxAudioImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
