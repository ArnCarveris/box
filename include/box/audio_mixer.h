/**
 * @file box/audio_mixer.h
 * @brief Audio output state, mixer buses, sends, reverb, equalizer, meters, sounds, sources and
 * streams.
 */

#ifndef BOX_AUDIO_MIXER_H
#define BOX_AUDIO_MIXER_H

#include "types.h"

#ifdef BOX_AUDIO_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

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

/** Sends of a mixer channel; more log one error that names the channel and the first play */
#ifndef BOX_AUDIO_SENDS
#define BOX_AUDIO_SENDS 4
#endif

/** Insert slots of a mixer channel, AudioBus.effects */
#define BOX_AUDIO_EFFECT_SLOTS 8

/** Effect instances that run at once, in every channel together; more log one error that
 * names the effect */
#ifndef BOX_AUDIO_EFFECT_MAX
#define BOX_AUDIO_EFFECT_MAX 64
#endif

/** What a mixer channel is, for the solo. A track is identified by Track and the master by
 * box.audio.master; both keep the default */
ECS_ENUM(AudioChannelKind, {
    AudioChannelGroup = 0,      /**< A group or any other bus, the default */
    AudioChannelReturn = 1,     /**< A return, such as a reverb bus: solo safe, so the reverb
                                 *   of a soloed channel is heard */
    AudioChannelSfx = 2         /**< A bus of the game sounds: a solo never silences it */
});

/** A send of a mixer channel to another bus */
ECS_STRUCT(AudioSend, {
    ecs_entity_t bus;      /**< Bus that receives the send, 0 (or a deleted bus) for none */
    float level;           /**< Linear gain of the send, 0 to 4 */
    bool pre_fader;        /**< Tap before the volume and pan of the channel instead of after
                            *   them; the mute and the solo still silence it */
});

/** A mixer bus, the component of a mixer channel: a group, a return, an SFX bus, the bus of a
 * track and the master. Sources and plays mix into a bus, a bus into its output, the master
 * last.
 *
 * Up to 63 buses besides the master (box.audio.master), whose AudioBus sets its pan and width
 * and whose volume multiplies AudioSettings.master_volume. A cycle through outputs and sends logs
 * one error and keeps the last valid order. A Disabled bus is silent.
 *
 * Effects run in order in its insert slots before its width, pan and volume; the master runs its
 * own before the limiter. An entity in two slots logs one error and the second slot stays empty.
 * See Effects in docs/audio.md.
 */
ECS_STRUCT(AudioBus, {
    ecs_entity_t output;   /**< Bus it feeds, 0 (or a deleted bus) for the master */
    float volume;          /**< Linear gain, default 1 */
    float pan;             /**< -1 left to 1 right, default 0 */
    float width;           /**< Stereo width, 0 mono, 1 as is (default), 2 doubles the side; a
                            *   mid/side gain after the inserts and before the pan */
    bool mute;             /**< Silence the bus, default false */
    bool solo;             /**< Solo the channel, default false: while any channel is soloed,
                            *   only soloed channels, channels on or feeding their path, returns,
                            *   SFX buses and what these feed are heard. mute always wins */
    AudioChannelKind kind; /**< What the channel is, default AudioChannelGroup */
    ecs_vec(AudioSend) sends; /**< Up to BOX_AUDIO_SENDS sends to other buses */
    ecs_entity_t effects[8]; /**< Effect entities of the BOX_AUDIO_EFFECT_SLOTS insert slots,
                            *   in the order they run, 0 for an empty slot */
});

/** A reverb, an effect: it runs in the insert slot of an AudioBus that names its entity in
 * effects, usually on a return bus that sources reach through a send:
 * hall { Bus: {effects: [hall]}; Reverb: {decay: 2} }.
 *
 * Every reverb has its own delay memory. Decay, damping and mix change smoothly; a change of size
 * or predelay, a move to another slot, a delete or a disable fades the wet signal.
 *
 * Shimmer runs a share of the feedback through a pitch shifter, so each pass of the tail rises
 * by shimmer_pitch semitones; the reverb stays bounded at every decay. At a shimmer of 0 the
 * reverb renders the same as without one.
 */
ECS_STRUCT(Reverb, {
    float decay;           /**< Seconds until -60 dB, default 1.5 */
    float damping;         /**< High frequency loss, 0 to 1, default 0.5 */
    float predelay;        /**< Seconds before the first reflection, default 0.02 */
    float size;            /**< Room scale, 0 to 1, default 0.5 */
    float mix;             /**< Wet level, 0 to 1, default 1 on a send bus; the dry signal
                            *   passes at 1 - mix */
    float shimmer;         /**< Share of the feedback that is pitch shifted, 0 to 1, default 0
                            *   (off) */
    float shimmer_pitch;   /**< Semitones each pass of the shimmer shifts, -24 to 24, default
                            *   12 (an octave up) */
});

/** Bands of an Equalizer */
#define BOX_AUDIO_EQ_BANDS 4

/** Kind of a band of an Equalizer */
ECS_ENUM(AudioEqKind, {
    AudioEqOff,            /**< Off, the default: the band passes its input and costs nothing */
    AudioEqPeak,           /**< A peak or a dip of gain around frequency, as wide as q */
    AudioEqLowShelf,       /**< gain under frequency */
    AudioEqHighShelf,      /**< gain above frequency */
    AudioEqLowCut,         /**< A highpass of 12 dB per octave from frequency, -3 dB there at a
                            *   q of 0.7071; gain is unused */
    AudioEqHighCut         /**< A lowpass of 12 dB per octave from frequency */
});

/** A band of an Equalizer: an RBJ cookbook biquad */
ECS_STRUCT(AudioEqBand, {
    AudioEqKind kind;      /**< What the band does, default AudioEqOff */
    float frequency;       /**< Hz, the center of a peak or the corner of a shelf or a cut, 10
                            *   to 0.49 of the sample rate */
    float gain;            /**< dB, -24 to 24, of a peak or a shelf, default 0; a peak or a
                            *   shelf at 0 dB is flat and costs nothing */
    float q;               /**< 0.1 to 18, default 0.7071: the width of a peak (higher is
                            *   narrower), the resonance of a shelf or a cut at its corner */
});

/** An equalizer, an effect in an insert slot of an AudioBus, as a Reverb is: up to
 * BOX_AUDIO_EQ_BANDS bands in a row.
 *
 * By default every band is off, at 100, 400, 2000 and 8000 Hz, and the equalizer passes its
 * input bit for bit: vocals { Bus: {effects: [vocals]};
 * Equalizer: {bands: [{kind: EqLowCut, frequency: 120}]} }.
 *
 * A change of a band moves it over 20 ms, so a script may automate it every frame without
 * zipper noise or clicks.
 */
ECS_STRUCT(Equalizer, {
    AudioEqBand bands[4];  /**< The BOX_AUDIO_EQ_BANDS bands, in the order they run */
});

/** Seconds of output the RMS of an AudioMeter is measured over */
#define BOX_AUDIO_METER_WINDOW (0.3f)

/** The levels of a mixer channel, read only: box.audio writes it each frame on every channel
 * entity that has it, the master and effect entities included. Metering is opt-in.
 *
 * A channel is measured at its output, after its inserts, width, pan, volume and mute. The master
 * is measured after its volume and before the limiter, so it reads over 1 when the limiter
 * reduces the gain.
 *
 * The levels follow the clock of the output: a suspended page, a page that waits for a gesture
 * and a world without output read silence.
 */
ECS_STRUCT(AudioMeter, {
    float peak_left;       /**< Linear, the largest absolute left sample since the last frame */
    float peak_right;      /**< Linear, the largest absolute right sample since the last frame */
    float rms_left;        /**< Linear, the RMS of the left channel over the last
                            *   BOX_AUDIO_METER_WINDOW seconds of output */
    float rms_right;       /**< Linear, the RMS of the right channel over the window */
    float reduction;       /**< dB, on an effect entity: the largest gain reduction since the
                            *   last frame, for dynamics effects and a PatternGate, and how
                            *   hard the clipper of a Saturator squashes; 0 for others */
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
    int64_t voices_refused; /**< Plays refused so far at the voice cap because every voice
                            *   that played had a higher priority */
    float load;            /**< Share of real time the render takes, 0 where it is not
                            *   measured (the web) */
    int32_t buses;         /**< Buses in the processing order, the master included */
    int64_t clock;         /**< Frame of the core that renders now, as far as the main thread
                            *   can tell; stops while the output does not run, 0 for a new
                            *   output */
});

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
