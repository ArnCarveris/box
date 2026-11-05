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
#include "curve.h"

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

/** Routes of the modulation matrix of a synth sound: its SynthModRoute children and those of
 * its layers. More log one error and are left out */
#define BOX_AUDIO_SYNTH_ROUTES 8

/** Seconds a synth sound plays when it would never end, a layer without any envelope */
#define BOX_AUDIO_SYNTH_ENDLESS (10.0f)

/** Lowest frequency of a SynthPluck layer in Hz; a lower one, its frequency_end included, is
 * clamped with one warning */
#define BOX_AUDIO_SYNTH_PLUCK_LOWEST (40.0f)

/** Octaves the pressure of a Note moves the cutoff of a synth voice up by at pressure 1, the
 * same in the core */
#define BOX_AUDIO_SYNTH_PRESSURE_OCTAVES 2.0f

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
#define BOX_AUDIO_EFFECT_SLOTS 4

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
 * Up to 63 buses besides the master, which scripts can name master (box.audio.master). The
 * master may have an AudioBus of its own for its pan and width; its volume multiplies
 * AudioSettings.master_volume and its output, sends, solo and kind are ignored. A cycle through
 * outputs and sends logs one error that names the buses and keeps the last valid order. A
 * Disabled bus is silent.
 *
 * When any channel is soloed, a channel is heard when it is soloed, on the output path of a
 * soloed channel, feeds a soloed channel, is a return or an SFX bus, or is downstream of one of
 * these. mute always wins.
 *
 * Its effects are entities with an effect component, such as Reverb, in up to
 * BOX_AUDIO_EFFECT_SLOTS insert slots that run in order on its signal before its width, pan
 * and volume; the master runs its own before the limiter. An empty slot (0) is skipped. The
 * channel may name itself. An entity in two slots logs one error that names both channels, and
 * the second slot stays empty. See Effects in docs/audio.md.
 */
ECS_STRUCT(AudioBus, {
    ecs_entity_t output;   /**< Bus it feeds, 0 (or a deleted bus) for the master */
    float volume;          /**< Linear gain, default 1 */
    float pan;             /**< -1 left to 1 right, default 0 */
    float width;           /**< Stereo width, 0 mono, 1 as is (default), 2 doubles the side; a
                            *   mid/side gain after the inserts and before the pan */
    bool mute;             /**< Silence the bus, default false */
    bool solo;             /**< Solo the channel, default false */
    AudioChannelKind kind; /**< What the channel is, default AudioChannelGroup */
    ecs_vec(AudioSend) sends; /**< Up to BOX_AUDIO_SENDS sends to other buses */
    ecs_entity_t effects[4]; /**< Effect entities of the BOX_AUDIO_EFFECT_SLOTS insert slots,
                            *   in the order they run, 0 for an empty slot */
});

/** A reverb, an effect: it runs in the insert slot of an AudioBus that names its entity in
 * effects, and does nothing while no slot names it. Usually on a return bus that sources
 * reach through a send of their bus, which may name itself:
 * hall { AudioBus: {effects: [hall]}; Reverb: {decay: 2} }.
 *
 * Every reverb runs with its own delay memory, up to BOX_AUDIO_EFFECT_MAX effects. Decay,
 * damping and mix change smoothly; a change of size or predelay fades the wet signal out and
 * in, and so does a move to another slot. Deleting the entity, as unloading its scene does,
 * taking it out of its slot or disabling it takes it off with a fade.
 */
ECS_STRUCT(Reverb, {
    float decay;           /**< Seconds until -60 dB, default 1.5 */
    float damping;         /**< High frequency loss, 0 to 1, default 0.5 */
    float predelay;        /**< Seconds before the first reflection, default 0.02 */
    float size;            /**< Room scale, 0 to 1, default 0.5 */
    float mix;             /**< Wet level, 0 to 1, default 1 on a send bus; the dry signal
                            *   passes at 1 - mix */
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
 * BOX_AUDIO_EQ_BANDS bands in a row. By default every band is off, at 100, 400, 2000 and 8000
 * Hz, and the equalizer passes its input bit for bit.
 * vocals { AudioBus: {effects: [vocals]}; Equalizer: {bands: [{kind: AudioEqLowCut,
 * frequency: 120}]} }.
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
 * entity that has it, the master (box.audio.master) included, and on effect entities.
 *
 * Metering is opt-in: a mixer UI, a test or an agent adds the component, and an entity without
 * it is not written.
 *
 * A channel is measured at its output, after its inserts, width, pan, volume and mute: what it
 * sends on. The master is measured after its volume and before the limiter, so it reads over 1
 * when the limiter reduces the gain. The levels follow the clock of the output: a suspended
 * page, a page that waits for a gesture and a world without output read silence.
 */
ECS_STRUCT(AudioMeter, {
    float peak_left;       /**< Linear, the largest absolute left sample since the last frame */
    float peak_right;      /**< Linear, the largest absolute right sample since the last frame */
    float rms_left;        /**< Linear, the RMS of the left channel over the last
                            *   BOX_AUDIO_METER_WINDOW seconds of output */
    float rms_right;       /**< Linear, the RMS of the right channel over the window */
    float reduction;       /**< dB, on an effect entity: the largest gain reduction since the
                            *   last frame, for dynamics effects, and how hard the clipper of
                            *   a Saturator squashes; 0 for others */
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
    SynthNoise,            /**< Noise, seeded per play from SynthSound.seed, white or of the
                            *   SynthOscillator.color */
    SynthPluck,            /**< A plucked string (Karplus-Strong), set by the SynthString of
                            *   the layer; it rings out on its own and ends the sound when it
                            *   is silent, so it needs no envelope */
    SynthWavetable         /**< The frames of the Wavetable of the SynthTable of the layer,
                            *   morphed by its position; a sine without one */
});

/** Color of the noise of a SynthNoise layer. Each is at about the RMS level of white noise at
 * the same level, and keeps its slope at every sample rate */
ECS_ENUM(SynthNoiseColor, {
    SynthNoiseWhite,       /**< White, flat, the same power in every Hz */
    SynthNoisePink,        /**< Pink, falls by 3 dB per octave, the same power in every
                            *   octave: wind, surf, rain */
    SynthNoiseBrown        /**< Brown, falls by 6 dB per octave, a leaky integral of white
                            *   noise without DC: rumble, distant surf, soft hits */
});

/** Kind of the filter of a synth sound */
ECS_ENUM(SynthFilterKind, {
    SynthLowPass,          /**< Lowpass */
    SynthHighPass,         /**< Highpass */
    SynthBandPass,         /**< Bandpass, unity at the cutoff */
    SynthNotch,            /**< Notch, silent at the cutoff */
    SynthFormant,          /**< Three resonant bandpasses in parallel at the first three
                            *   formants of SynthFilter.vowel, from the tenor table of the
                            *   Csound manual: vowels, talking pads, wah. A SynthFilter only;
                            *   an AudioFilter takes it as SynthLowPass */
    SynthComb              /**< A feedback comb tuned to the note, its peaks at the harmonics
                            *   of SynthFilter.tune times the note: metallic, flanged and
                            *   resonator sounds, and plucks from noise bursts. A SynthFilter
                            *   only; an AudioFilter takes it as SynthLowPass */
});

/** Slope of the filter of a synth sound */
ECS_ENUM(SynthFilterSlope, {
    SynthSlope12,          /**< 12 dB per octave, one stage */
    SynthSlope24           /**< 24 dB per octave, two cascaded stages */
});

/** Model of the filter of a synth sound */
ECS_ENUM(SynthFilterModel, {
    SynthFilterSvf,        /**< A state variable filter, clean and stable, that rings but never
                            *   oscillates */
    SynthFilterLadder      /**< A 4-pole transistor ladder with a tanh in each stage and in its
                            *   feedback: a fat lowpass that self-oscillates at resonance 1 */
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

/** Source of a SynthModRoute, see the modulation matrix in docs/audio.md */
ECS_ENUM(SynthModSource, {
    SynthSourceAmpEnvelope,    /**< The amp envelope, 0..1: of the layer for a route on a
                                *   layer, else the SynthEnvelope of the root, or of the first
                                *   layer when the root has none */
    SynthSourceFilterEnvelope, /**< The SynthFilterEnvelope, 0..1; it runs for the route
                                *   without a SynthFilter too */
    SynthSourceModEnvelope,    /**< The SynthModEnvelope of the root, 0..1 */
    SynthSourceLfo1,           /**< The first LFO of the patch, -1..1, after its delay and
                                *   fade; see LFOs in docs/audio.md for their order */
    SynthSourceLfo2,           /**< The second LFO of the patch, -1..1 */
    SynthSourceVelocity,       /**< The velocity of the note, 0..1, 1 for a play */
    SynthSourceKey,            /**< The key of the note in octaves from the root, the key of
                                *   SynthFilter.keytrack over 12 */
    SynthSourceRandom,         /**< A random value per note, -1..1, seeded from
                                *   SynthSound.seed and the count of plays */
    SynthSourceMod,            /**< Note.mod, 0..1, 1 without expression; a route from it turns
                                *   off the vibrato depth that mod scales */
    SynthSourcePressure        /**< Note.pressure, 0..1; a route from it turns off the
                                *   BOX_AUDIO_SYNTH_PRESSURE_OCTAVES of the cutoff */
});

/** Target of a SynthModRoute, with the unit of its amount at a source value of 1 */
ECS_ENUM(SynthModTarget, {
    SynthTargetCutoff,         /**< Octaves of the cutoff of the SynthFilter, -10..10; nothing
                                *   on a SynthFormant filter */
    SynthTargetResonance,      /**< Share of the resonance of the SynthFilter, -1..1; the moved
                                *   resonance is clamped to 0..1. Nothing on a SynthComb */
    SynthTargetIndex,          /**< Radians of the index of a SynthPhase modulator, -32..32, or
                                *   the depth of a SynthRing one; the moved index is clamped as
                                *   SynthOscillator.index. Nothing on a layer that does not
                                *   modulate */
    SynthTargetPitch,          /**< Semitones of the pitch, -48..48 */
    SynthTargetPan,            /**< Pan of the voice, -2..2, on top of its own; the moved pan
                                *   is clamped to -1..1 and smoothed, and a sound with a route to
                                *   it renders in stereo */
    SynthTargetDetune,         /**< Cents of SynthUnison.detune, -100..100; the moved detune is
                                *   clamped to 0..100. Nothing on a layer without unison */
    SynthTargetPulseWidth,     /**< Share of the period of the pulse width of squares,
                                *   -0.49..0.49 */
    SynthTargetLevel,          /**< Share of the level of the layer, -4..4: the level is times
                                *   1 + amount value, clamped at 0, and smoothed */
    SynthTargetHardSync,       /**< Semitones of the ratio of SynthOscillator.sync, -48..48;
                                *   the moved ratio is clamped to 1..16. Nothing on a layer
                                *   without sync: a ratio over 1 or a SynthLfo.hard_sync of
                                *   the layer */
    SynthTargetPosition,       /**< Frames of the position of a SynthWavetable layer,
                                *   -256..256; the moved position is clamped to 0..N-1 of its
                                *   table. Nothing on a layer that is not a wavetable */
    SynthTargetVowel           /**< Vowels of the SynthFormant filter, -4..4; the moved vowel
                                *   is clamped to 0..4. Of the sound on a layer too, as the
                                *   cutoff. Nothing without a SynthFormant filter */
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
    float sub;             /**< Level of the sub oscillator, 0..1, default 0 (off): a band
                            *   limited square sub_octave octaves under the layer, added to
                            *   its signal before its level and envelope. It is derived from
                            *   the phase of the layer, so it follows its pitch, sweep, glide,
                            *   vibrato and unison, but not its phase modulation or feedback;
                            *   a modulator modulates with the sub included. Ignored by
                            *   SynthNoise and SynthPluck */
    int32_t sub_octave;    /**< Octaves the sub is under the layer, 1 or 2, default 1; 0
                            *   counts as 1 and other values are clamped */
    float velocity;        /**< SynthPhase, SynthRing: share 0..1 of the index that the
                            *   velocity of a note scales, index (1 - share + share v), so
                            *   soft notes have fewer sidebands; default 0 */
    float sync;            /**< Hard sync: ratio 1..16 of the wave heard to the note, default
                            *   0, which counts as 1 (off). The note frequency of the layer,
                            *   its sweep, glide, vibrato, unison detune and the pitch of the
                            *   voice included, is the master: the wave runs at the master
                            *   times the ratio and restarts at phase 0 on every cycle of the
                            *   master, band limited at the restart, for sync leads and, with
                            *   SynthLfo.hard_sync, sync sweeps. Each unison voice syncs to its own
                            *   master; the sub stays at the master and phase modulation adds
                            *   to the synced phase. Ignored by SynthNoise and SynthPluck */
    SynthNoiseColor color; /**< SynthNoise: color of the noise, default SynthNoiseWhite. The
                            *   white noise it filters is seeded as white noise is, so a
                            *   colored layer is as deterministic; each unison voice filters
                            *   its own. Ignored by the other waves */
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
    float velocity;        /**< Share 0..1 of the hardness that the velocity of a note
                            *   scales, hardness (1 - share + share v), so soft notes are
                            *   plucked softer; default 0 */
});

/** A built-in wavetable, generated by additive synthesis, so it needs no file */
ECS_ENUM(WavetableKind, {
    WavetableBasic,        /**< 4 frames: sine, triangle, saw, square, at the phase and the
                            *   level of the SynthSine, SynthTriangle, SynthSaw and SynthSquare
                            *   waves */
    WavetablePulse,        /**< 16 frames of a pulse without DC, its width from 50% at frame 0
                            *   to 5% at frame 15 in even steps */
    WavetableHarmonics     /**< 32 frames: frame k holds the first k + 1 harmonics of the saw
                            *   of WavetableBasic at 1/n, a sweep like that of a lowpass */
});

/** A wavetable, on an entity of its own, usually a prefab, that the SynthTable of wavetable
 * layers names: frames of one period each that a layer morphs through.
 *
 * The main thread builds it when it is set: each frame as mip levels, one per octave, each of
 * which holds only the harmonics that stay under Nyquist for the highest fundamental of its
 * octave, 2048 samples at level 0, and hands it to the core, as the frames of a sound. A table
 * of up to 256 frames takes 16.6 KB per frame, a table of 64 frames about 1 MB. Voices that
 * play a table that changes or is deleted play the old one until they end.
 *
 * A table loaded from a sound, a WAV or FLAC of single cycle frames, is cut into
 * floor(frames / frame_size) frames, at most 256, stereo summed to mono, each frame without
 * its DC, the table normalized to a peak of 1 over all frames. Each frame keeps the harmonics
 * under the Nyquist of its frame_size, at most 512, so a frame of other than 2048 samples is
 * resampled to the frames of the table. Until the sound is ready the table has no frames and
 * its layers play a sine; when the sound becomes ready or changes the table is built again,
 * and the next play of the synth sounds that name it takes it. A sound that fails to load,
 * that is not an entity with AudioPcm or SoundFile, or that is shorter than one frame logs
 * one error that names the Wavetable; a sound of more than 256 frames logs one warning.
 */
ECS_STRUCT(Wavetable, {
    WavetableKind kind;    /**< The table, default WavetableBasic; ignored with a sound */
    ecs_entity_t sound;    /**< An entity with AudioPcm or SoundFile whose frames make the
                            *   table in place of kind, 0 for none, the default */
    int32_t frame_size;    /**< Samples per frame of the sound, 64..4096, default 2048; 0 is
                            *   the default */
});

/** The wavetable of a SynthWavetable layer, on the entity of its SynthOscillator.
 *
 * The position is a frame of the table; between two frames the layer plays a linear mix of
 * them. The envelope of the layer, its own SynthEnvelope or the one of the root, and
 * SynthLfo.position move it, and the moved position is clamped to the frames of the table. A
 * SynthWavetable layer without a SynthTable, or whose table is not an entity with Wavetable,
 * plays a sine and logs one error that names the sound.
 */
ECS_STRUCT(SynthTable, {
    ecs_entity_t table;    /**< The entity with the Wavetable */
    float position;        /**< Frame it plays, 0..frames - 1, default 0 */
    float envelope;        /**< Frames the position moves by at level 1 of the envelope of
                            *   the layer, either way, default 0 */
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
    float key_scale;       /**< Key scaling 0..1, default 0: the decay and the release are
                            *   times 2^(-key_scale key / 12) at the key of the note (see
                            *   SynthFilter), fixed at its start, so at 1 they halve per
                            *   octave above the root and double per octave below; clamped
                            *   to the times of an envelope */
});

/** Unison of a synth layer, on the entity of its SynthOscillator: the layer plays as several
 * copies of its oscillator, detuned evenly over a spread in cents and placed from left to right
 * over the stereo width, each starting at a seeded random phase.
 *
 * The voices share the level of the layer: each plays at 1 / sqrt(voices), so the level of the
 * layer stays about the same as the count changes. One voice is the layer without unison. A
 * sound in which a layer has voices and width renders in stereo, see docs/audio.md.
 *
 * On a SynthPluck layer each copy is a string of its own, plucked with its own noise, detuned
 * and panned the same way; each takes one of the 32 strings of the core, and a play that does
 * not find one for each copy is ignored.
 */
ECS_STRUCT(SynthUnison, {
    int32_t voices;        /**< Copies of the oscillator, 1 to 7, default 3 */
    float detune;          /**< Cents from the lowest copy to the highest, 0 to 100,
                            *   default 10 */
    float width;           /**< 0 (every copy in the center, mono) to 1 (the lowest copy
                            *   left, the highest right), default 0 */
});

/** The filter of a synth sound, on its root: a state variable filter on the mix of the layers,
 * stable at any cutoff and resonance.
 *
 * SynthFormant is three resonant bandpasses of the SVF in parallel at the fixed formants of a
 * vowel, see SynthFormant in docs/audio.md: resonance 0 has the widths of the table and 1 a
 * tenth of them, drive applies to each bandpass, and the cutoff, slope, model, velocity and
 * keytrack do nothing; the filter envelope and SynthLfo.cutoff do not move it either, only
 * their vowel depths do. The level is normalized so a saw at 110 Hz is about as loud as
 * without the filter.
 *
 * SynthComb is a feedback comb, y = x + feedback * y delayed by one period of the note times
 * tune, see SynthComb in docs/audio.md. The note is the frequency of the first layer times
 * the pitch of the voice: its pitch ratio, glide, pitch variance, bend, SynthPitchEnvelope and
 * the pitch of a SynthLfo of every layer, but not the unison detune, drift or the LFOs of
 * single layers. A fractional delay keeps it in tune at high notes. The cutoff damps its loop
 * with a one pole lowpass, so SynthFilterEnvelope, SynthLfo.cutoff, velocity and keytrack
 * darken it over time; a cutoff of 0.49 of the rate or above, such as 20000 Hz at 44.1 kHz
 * and up, leaves it undamped. The output is times 1 - |feedback|, so its peaks stay near
 * unity at any feedback. Once the envelopes end it rings on to silence, so a short burst of
 * noise rings as a pluck. The resonance, slope, drive and model do nothing. The core holds
 * BOX_AUDIO_SYNTH_COMBS (32) delay lines for 40 Hz and up: a voice takes one, two on the stereo
 * path of SynthUnison with width, and a play that finds them taken is ignored, as a
 * SynthString play without a free string.
 *
 * The key of a voice, which keytrack and SynthEnvelope.key_scale follow, is 12 log2 of the
 * pitch ratio of its play in semitones, without the pitch variance of SynthSound: relative to
 * Instrument.root for a note of an instrument, to a ratio of 1 for any other play, as the
 * pitch of an AudioSource or of box_audio_play.
 */
ECS_STRUCT(SynthFilter, {
    SynthFilterKind kind;  /**< The kind, default SynthLowPass */
    float cutoff;          /**< Hz, default 1000, clamped under Nyquist */
    float resonance;       /**< 0 (a Butterworth response) to 1 (rings the longest) */
    SynthFilterSlope slope; /**< Steepness, default SynthSlope12; at SynthSlope24 the resonance
                            *   applies to the second stage only */
    float drive;           /**< Saturation, 0 to 1, default 0 (linear): soft clips the input
                            *   and the resonance of each stage, so a high resonance compresses
                            *   and growls instead of whistling; 1 drives it hard */
    float velocity;        /**< Octaves of the cutoff at velocity 1 over velocity 0, -10..10,
                            *   default 0: the cutoff applies as authored at velocity 1 and is
                            *   cutoff * 2^(velocity (v - 1)) at the velocity v of a note */
    float keytrack;        /**< Key tracking 0..1, default 0: the cutoff is times the pitch
                            *   ratio of the voice to the keytrack, so 1 moves it 1:1 with the
                            *   pitch; it follows the pitch of the voice, glides included */
    SynthFilterModel model; /**< The model, default SynthFilterSvf. SynthFilterLadder is a
                            *   transistor ladder: SynthSlope24 is its 4-pole lowpass and
                            *   SynthSlope12 the 2-pole tap; the highpass, bandpass and notch
                            *   mix the poles as a multimode ladder. Its resonance oscillates
                            *   at 1 as a near sine at the cutoff, its lowpass keeps its
                            *   passband within 2 dB as the resonance rises, and drive is the
                            *   gain into it, from -6 dB to +12 dB, the output scaled back */
    float vowel;           /**< SynthFormant: the vowel, 0 a, 1 e, 2 i, 3 o, 4 u, default 0;
                            *   a fraction interpolates the frequencies, levels and widths of
                            *   the formants of the two nearest vowels. SynthLfo.vowel and
                            *   SynthFilterEnvelope.vowel move it, clamped to 0..4 */
    float feedback;        /**< SynthComb: gain of the delayed output fed back to the input,
                            *   -0.98..0.98, default 0.5: positive peaks at the harmonics of
                            *   the comb, negative at the odd harmonics of half of it, hollow
                            *   like a closed pipe; near 1 either way rings long */
    float tune;            /**< SynthComb: ratio of the frequency of the comb to the note,
                            *   0.125..8, default 1; 2 an octave up, 1.5 a fifth */
});

/** A filter, an effect in an insert slot of an AudioBus, as a Reverb is: the state variable
 * filter of SynthFilter on the signal of a bus, for a sweep on a music group, a muffled SFX
 * bus or a telephone voice.
 * music { AudioBus: {effects: [music]}; AudioFilter: {cutoff: 400, resonance: 0.3} }.
 *
 * The cutoff and the resonance move exponentially toward a new value over about 10 ms, so a
 * sweep driven from a script every frame is smooth; the mix and the drive move over 20 ms. A
 * change of the kind or the slope fades the filtered signal out and back in over 40 ms.
 */
ECS_STRUCT(AudioFilter, {
    SynthFilterKind kind;  /**< The kind, default SynthLowPass; SynthNotch for a notch */
    float cutoff;          /**< Hz, default 1000, 10 to 0.49 of the sample rate */
    float resonance;       /**< 0 (a Butterworth response, the default) to 1 (rings the
                            *   longest) */
    SynthFilterSlope slope; /**< Steepness, default SynthSlope12; at SynthSlope24 the resonance
                            *   applies to the second stage only */
    float mix;             /**< Wet level, 0 to 1, default 1; the dry signal passes at
                            *   1 - mix */
    float drive;           /**< Saturation, 0 to 1, default 0 (linear), as the drive of
                            *   SynthFilter; moves over 20 ms */
});

/** A compressor, an effect in an insert slot of an AudioBus, as a Reverb is: feed-forward and
 * stereo linked, it detects the peak of the louder channel, smooths it with its attack and
 * release in dB and reduces the gain over its threshold by its ratio, with a soft knee.
 *
 * With a sidechain it keys on the output of that channel instead of its own input, to duck
 * one channel under another, such as music under dialog:
 * music { AudioBus: {effects: [music]}; Compressor: {threshold: -30, ratio: 8, sidechain: sfx} }.
 * The order of the channels runs the sidechain first; a sidechain that would form a cycle logs
 * one error that names the channels, and the compressor keys on its own input, as it does
 * when its sidechain is deleted. An AudioMeter on its entity reads its gain reduction.
 */
ECS_STRUCT(Compressor, {
    float threshold;       /**< dB, default -18, -80 to 0 */
    float ratio;           /**< 1 to 20, default 4 */
    float attack;          /**< Seconds in which the detected level rises 63% of a step,
                            *   default 0.01, 0.0001 to 1 */
    float release;         /**< Seconds in which it falls 63% of a step, default 0.15, 0.001 to
                            *   5 */
    float knee;            /**< Width of the soft knee in dB, default 6, 0 to 24 */
    float makeup;          /**< Gain after the compression in dB, default 0, 0 to 24 */
    float mix;             /**< Wet level, 0 to 1, default 1; lower for parallel compression */
    ecs_entity_t sidechain; /**< Channel (an AudioBus) whose output is the key, 0 for its own
                            *   input */
});

/** A gate, an effect in an insert slot of an AudioBus, as a Reverb is: it opens when the peak
 * of the louder channel reaches its threshold, stays open while the level is above the
 * threshold less 4 dB of hysteresis and for its hold after, and then closes to its range.
 * drums { AudioBus: {effects: [drums]}; Gate: {threshold: -40, hold: 0.02} }.
 * An AudioMeter on its entity reads its gain reduction.
 */
ECS_STRUCT(Gate, {
    float threshold;       /**< dB, default -50, -100 to 0 */
    float attack;          /**< Seconds in which it opens 63% of the way, default 0.001 */
    float hold;            /**< Seconds it stays open after the level fell, default 0.05, 0
                            *   to 5 */
    float release;         /**< Seconds in which it closes 63% of the way, default 0.1 */
    float range;           /**< Gain when closed in dB, default -80, -100 to 0 */
});

/** Longest time of a Delay in seconds, which sizes its memory */
#define BOX_AUDIO_DELAY_MAX 4.0f

/** A delay, an effect in an insert slot of an AudioBus, as a Reverb is: echoes of its input
 * every time seconds, each feedback times the last and darker by a lowpass at damping Hz in
 * the feedback path. With sync it follows the tempo of the Transport, for a dotted eighth on a
 * return: echo { AudioBus: {kind: AudioChannelReturn, effects: [echo]};
 * Delay: {sync: 0.75, feedback: 0.4, mix: 1} }.
 *
 * Every delay runs with its own memory of BOX_AUDIO_DELAY_MAX seconds. A change of the time,
 * as a change of the tempo of a synced delay, crossfades from the old to the new echo over 50
 * ms without a glide of the pitch; feedback, mix and ping pong move over 20 ms. Once its
 * repeats decayed under -140 dB it stops until its input sounds again.
 */
ECS_STRUCT(Delay, {
    float time;            /**< Seconds between the repeats, default 0.25, 0 to
                            *   BOX_AUDIO_DELAY_MAX (4) */
    float sync;            /**< Beats between the repeats, 0 (the default) to use time; when
                            *   positive it wins over time, which becomes sync * 60 / tempo at
                            *   the tempo of the Transport (120 without one): 0.5 is an eighth,
                            *   0.75 a dotted eighth */
    float feedback;        /**< Share of a repeat in the next, 0 to 0.95, default 0.35 */
    float mix;             /**< Wet level, 0 to 1, default 0.3; the dry signal passes at
                            *   1 - mix */
    bool ping_pong;        /**< Bounce the repeats between left and right, default false: the
                            *   input, summed to mono, repeats on the left first */
    float damping;         /**< Cutoff in Hz of a one pole lowpass in the feedback path, default
                            *   6000, at least 20; at 0.49 of the sample rate or above the
                            *   repeats stay bright */
});

/** A phaser, an effect in an insert slot of an AudioBus, as a Reverb is: a chain of first
 * order allpasses whose break frequency a sine LFO sweeps around a center, mixed with the dry
 * signal, so its notches move up and down at the rate:
 * keys { AudioBus: {effects: [keys]}; Phaser: {stages: 6, rate: 0.3, feedback: 0.5} }.
 *
 * The LFO starts when the phaser is put on a channel and runs on through a change of the rate
 * without a jump; the right channel runs it stereo half turns later. Depth, center, feedback,
 * mix and stereo move over 20 ms; a change of the stages fades the phased signal out and back
 * in over 40 ms. Once its input is silent and its allpasses decayed it stops until its input
 * sounds again.
 */
ECS_STRUCT(Phaser, {
    int32_t stages;        /**< Allpass stages, 2 to 12 and even, default 4: stages / 2
                            *   notches */
    float rate;            /**< Hz of the LFO, default 0.5, 0 to 20 */
    float depth;           /**< 0 to 1, default 0.7: the sweep reaches 2 * depth octaves either
                            *   way of the center */
    float center;          /**< Hz at the middle of the sweep, default 800, 20 to 20000 */
    float feedback;        /**< Share of the phased signal fed back into the chain, -0.9 to 0.9,
                            *   default 0; more sharpens the peaks between the notches */
    float mix;             /**< Wet level, 0 to 1, default 0.5, which gives the deepest notches;
                            *   the dry signal passes at 1 - mix */
    float stereo;          /**< Phase offset of the LFO of the right channel in half turns, 0
                            *   to 1, default 0.5 (a quarter of a period) */
});

/** Longest read of a Chorus in milliseconds, delay and depth together, which sizes its
 * memory */
#define BOX_AUDIO_CHORUS_MAX 50.0f

/** A chorus, an effect in an insert slot of an AudioBus, as a Reverb is: up to 4 voices, each a
 * tap of a delay line that a sine LFO moves around delay by depth, their LFOs spread evenly in
 * phase and the voices panned across the stereo field by spread:
 * pad { AudioBus: {effects: [pad]}; Chorus: {voices: 3, spread: 1, mix: 0.4} }.
 *
 * With feedback it is a flanger: the first tap of each channel goes back into its line, so a
 * delay of 0.1 to 10 ms resonates at multiples of 1000 / delay Hz, and a negative feedback
 * moves the peaks to the odd multiples of half that, which sounds hollow. Only the first tap
 * feeds back, so the loop stays stable at any voices; the feedback reads its tap with a cubic
 * interpolation, so the highs keep resonating while the sweep falls between frames. A jet sweep
 * on drums:
 * drums { AudioBus: {effects: [drums]};
 *         Chorus: {voices: 1, rate: 0.2, delay: 2, depth: 1.8, feedback: 0.7, mix: 0.5} }.
 *
 * Every chorus runs with its own memory of BOX_AUDIO_CHORUS_MAX milliseconds. The LFOs start
 * when the chorus is put on a channel and run on through a change of the rate without a jump.
 * Depth, delay, mix, spread and feedback move over 20 ms; a change of the voices fades the wet
 * signal out and back in over 40 ms. Once its input was silent for 50 ms and its feedback rang
 * out it stops until its input sounds again.
 */
ECS_STRUCT(Chorus, {
    int32_t voices;        /**< Taps, 1 to 4, default 2 */
    float rate;            /**< Hz of the LFOs, default 0.8, 0 to 20 */
    float depth;           /**< Milliseconds the taps move either way, default 3, 0 to 25 */
    float delay;           /**< Milliseconds of the taps at the middle, default 12, 0 to 50;
                            *   a tap reads at most 50 ms and at least a frame back, so a
                            *   flanger goes down to about 0.1 ms */
    float mix;             /**< Wet level, 0 to 1, default 0.5; the dry signal passes at
                            *   1 - mix */
    float spread;          /**< 0 to 1, default 0.5: 0 keeps the voices in the center, so a mono
                            *   input stays mono, 1 pans the first voice left and the last
                            *   right */
    float feedback;        /**< Share of the first tap fed back into the line, -0.95 to 0.95,
                            *   default 0; 0 renders exactly as without it */
});

/** Curve of the clipper of a Saturator */
ECS_ENUM(SaturatorMode, {
    SaturatorTanh,         /**< tanh soft clipper, symmetric: warmth on a bus, smooth grit when
                            *   driven. asymmetry biases it for even harmonics */
    SaturatorTube,         /**< A softer asymmetric tanh whose positive half clips earlier than
                            *   its negative half, so its bias grows with the level: even
                            *   harmonics even at asymmetry 0, warmer than SaturatorTanh */
    SaturatorHard,         /**< Hard clip with a slightly rounded corner: fuzz, a square wave
                            *   at full drive. Anti-aliased (ADAA) on top of the oversampling */
    SaturatorFold,         /**< Sine wavefolder: the driven signal wraps over and back, the more
                            *   the drive, the more folds and the brighter. Anti-aliased (ADAA)
                            *   at four times the rate, 8 frames later than the others */
    SaturatorRectify       /**< A tanh rectifier: asymmetry blends from half wave at 0 to full
                            *   wave at 1, octave up grit; the DC it makes is taken out */
});

/** A saturator, an effect in an insert slot of an AudioBus, as a Reverb is: warmth, grit or
 * lo-fi for a drum bus, a synth bus or the master. Each channel runs through the clipper of
 * the mode at twice the sample rate, its level compensated so that the drive mostly changes
 * the tone and not the loudness: a sine at -14 dBFS keeps its RMS within about 1.5 dB at any
 * drive in every mode but tanh, whose older compensation holds that at -12 dBFS. The
 * asymmetry biases the clipper for even harmonics, with the DC taken out after it, and below
 * 16 bits a bit crusher rounds the saturated signal:
 * drums { AudioBus: {effects: [drums]}; Saturator: {drive: 0.6, asymmetry: 0.2} }.
 *
 * Around the clipper, low_cut takes the bass out before it so that the bass stays clean (with
 * a mix under 1) and does not muddy the distortion, and tone tilts the input around 1 kHz
 * with the inverse tilt after the clipper, so it sets which band distorts most and leaves a
 * clean signal as it was. After it, high_cut tames the fizz and rate holds each sample for
 * 1 / rate seconds, the aliasing of an old sampler that bits alone does not give. Recipes:
 * - Tape warmth on a bus: {mode: SaturatorTanh, drive: 0.3}
 * - Tube on a synth or vocal: {mode: SaturatorTube, drive: 0.5, high_cut: 9000}
 * - Fuzz on a guitar or bass: {mode: SaturatorHard, drive: 0.8, low_cut: 150,
 *   high_cut: 5000}
 * - Wavefolded lead: {mode: SaturatorFold, drive: 0.6, tone: 0.3, high_cut: 8000}
 * - Octave up grit: {mode: SaturatorRectify, drive: 0.4, asymmetry: 1, mix: 0.5}
 * - Lo-fi drums: {drive: 0.4, bits: 8, rate: 8000, high_cut: 6000}
 *
 * The oversampling delays the channel by 15 frames (23 in SaturatorFold), its dry signal too,
 * so that a mix in between does not comb. Drive, asymmetry, bits, mix, the cuts, tone and
 * rate move over 20 ms; a change of the mode is at once. An AudioMeter on its entity reads
 * how hard the clipper squashes as its reduction: the largest drop in dB of the clipped peak
 * against the driven peak, 0 while the clipper is all but linear. Once its input is silent
 * and its state decayed it stops until its input sounds again.
 */
ECS_STRUCT(Saturator, {
    float drive;           /**< 0 to 1, default 0.3: a gain of 0 to +30 dB into the clipper */
    float asymmetry;       /**< 0 to 1, default 0: a bias before the clipper, shared by both
                            *   channels, that adds even harmonics; in SaturatorRectify the
                            *   blend from half wave (0) to full wave (1) */
    float bits;            /**< Bits of the bit crusher, 1 to 16, default 16, which is off;
                            *   bits b round the output to 2^b levels */
    float mix;             /**< Wet level, 0 to 1, default 1; the dry signal passes at
                            *   1 - mix */
    SaturatorMode mode;    /**< Curve of the clipper, default SaturatorTanh */
    float low_cut;         /**< Hz of a 12 dB highpass before the clipper, 0 to 20000, default
                            *   0, which is off */
    float high_cut;        /**< Hz of a 12 dB lowpass after the clipper, 0 to 20000, default
                            *   0, which is off; held under 0.45 times the sample rate */
    float tone;            /**< -1 to 1, default 0: a tilt of up to 9 dB either way at the
                            *   ends of the spectrum around 1 kHz before the clipper, the
                            *   inverse after it; above 0 the highs distort most, below 0 the
                            *   lows */
    float rate;            /**< Hz of a sample and hold after the clipper, 0 or 100 to the
                            *   sample rate, default 0, which is off */
});

/** A transient shaper, an effect in an insert slot of an AudioBus, as a Reverb is: more or
 * less punch and tail on drums and plucks, without a threshold, at most 12 dB either way:
 * drums { AudioBus: {effects: [drums]}; TransientShaper: {attack: 0.6, sustain: -0.4} }.
 *
 * Stereo linked as a Compressor, it compares a fast envelope (1 ms up, 20 ms down) of the
 * louder channel with a slow one (20 ms up, 200 ms down) in dB. 0 and 0 pass the signal
 * unchanged; changes move over 20 ms. It has no meter and stops while its input is silent.
 */
ECS_STRUCT(TransientShaper, {
    float attack;          /**< -1 to 1, default 0: above 0 boosts the transients, below 0
                            *   softens them */
    float sustain;         /**< -1 to 1, default 0: above 0 boosts the tail, below 0 shortens
                            *   it */
});

/** Mode of a FrequencyShifter */
ECS_ENUM(FrequencyShifterMode, {
    FrequencyShift,        /**< Every partial moves up or down by the shift in Hz */
    FrequencyRing          /**< Ring modulation by a sine at the magnitude of the shift: each
                            *   partial splits into one shift above and one below it at half
                            *   the level, the partial itself gone */
});

/** A frequency shifter, an effect in an insert slot of an AudioBus, as a Reverb is: every
 * partial moves by the same number of Hz, so harmonic sounds turn inharmonic. A few Hz thicken
 * and, with stereo, widen; feedback gives an endlessly rising or falling barber pole sweep;
 * hundreds of Hz give metallic and bell tones. The ring mode is a classic ring modulator:
 * pad { AudioBus: {effects: [pad]}; FrequencyShifter: {shift: 2, stereo: 1, mix: 0.5} }.
 *
 * It shifts the analytic signal from an allpass pair Hilbert transformer, whose two paths stay 90
 * degrees apart from 20 Hz to 20 Hz under the Nyquist frequency, with the image of a shift about
 * 60 dB under it there at 48 kHz. The allpasses turn the phase of the signal but not its level, so
 * a shift of 0 at a mix of 1 changes the phase only. The oscillators start when the shifter is put
 * on a channel and run on through a change of the shift without a jump. The feedback runs through a
 * 20 Hz DC blocker and a soft limit at +12 dB. Changes move over 20 ms; a change of the mode
 * crossfades. Once its input is silent and its allpasses decayed it stops until its input sounds
 * again.
 */
ECS_STRUCT(FrequencyShifter, {
    float shift;           /**< Hz, -5000 to 5000, default 0; in the ring mode the frequency of
                            *   the sine is its magnitude */
    FrequencyShifterMode mode; /**< FrequencyShift (default) or FrequencyRing */
    float stereo;          /**< 0 to 1, default 0: the right channel shifts by shift times
                            *   1 - 2 stereo, so at 1 the opposite way of the left */
    float feedback;        /**< Share of the shifted signal fed back into the input, -0.95 to
                            *   0.95, default 0 */
    float mix;             /**< Wet level, 0 to 1, default 1; the dry signal passes at
                            *   1 - mix */
});

/** An envelope of the cutoff of the SynthFilter of the same root, with the segments of a
 * SynthEnvelope: the cutoff is cutoff times 2 to the amount times the level of the envelope,
 * so it rises over the attack, settles at the sustain and returns over the release.
 *
 * It does not keep a sound playing: the sound ends with its SynthEnvelope. Without a
 * SynthFilter it does nothing.
 *
 * On a SynthFormant filter the amount does not move the formants, which are fixed; the
 * envelope moves the vowel by its vowel depth instead, and runs whenever that is not 0.
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
    float velocity;        /**< Share 0..1 of the amount that the velocity of a note scales,
                            *   amount (1 - share + share v), default 0; it scales vowel too */
    float vowel;           /**< SynthFormant: vowels the vowel moves by at level 1, -4..4,
                            *   default 0; the moved vowel is clamped to 0..4 */
});

/** A pitch envelope of a synth sound, relative to the note so it fits every pitch: the pitch
 * starts amount semitones off the note and returns to it over the decay with a curve, the
 * drop of a kick or a tom, the blip of a zap or the scoop of brass. Over the release of the
 * amplitude envelope of the layer, its own or the one of the root, the offset moves from where
 * it is to release_amount. It adds to the glide, the LFO pitch and the sweep of
 * SynthOscillator, and moves SynthPluck strings as vibrato does.
 *
 * On the root it moves every layer; on a layer that layer, in place of the one of the root.
 * It does not keep a sound playing, and a legato note does not restart it.
 */
ECS_STRUCT(SynthPitchEnvelope, {
    float amount;          /**< Semitones at the start of a note, -48..48, default 0 */
    float decay;           /**< Seconds back to the note, default 0 */
    float curve;           /**< Curve of the decay and the release, 0 linear (default),
                            *   -16..16, as SynthEnvelope.curve: positive moves fast at first */
    float release_amount;  /**< Semitones the pitch moves to over the release, -48..48,
                            *   default 0 */
    float sync;            /**< Semitones the ratio of SynthOscillator.sync moves by at the start
                            *   of a note, -48..48, default 0, returning to 0 with the decay: the
                            *   sweep of a sync lead. Only on layers with a ratio over 1; the
                            *   moved ratio is clamped to 1..16 */
});

/** A drive stage of a synth sound, on its root: the mix of the layers of each voice runs
 * through a tanh soft clipper before the SynthFilter, or before the amp without one, where an
 * analog synth gets most of its thickness. Plucks, unison and FM pass through it as they mix.
 *
 * The clipper runs at twice the rate and is level compensated: a sine at level 1 keeps its
 * RMS at every drive. The driven mix is 15 frames late; with width both channels drive alike.
 * Without the component there is no drive. See docs/audio.md.
 */
ECS_STRUCT(SynthDrive, {
    float drive;           /**< 0..1, default 0.3: a gain of -6 to +24 dB before the clipper */
    float asymmetry;       /**< 0..1, default 0: an offset before the clipper that adds even
                            *   harmonics; its DC is blocked */
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

/** An LFO of a synth sound: a wave at a rate that moves the pitch, the level, the cutoff, the
 * pulse width of squares, the ratio of hard sync and the position of wavetables of the sound,
 * each by its own depth, after a delay and a fade in.
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
                            *   wave, -10..10; nothing on a SynthFormant filter */
    float pulse_width;     /**< Share of the period the pulse width of squares moves by at the
                            *   top of the wave, -0.49..0.49 */
    float hard_sync;       /**< Semitones the ratio of SynthOscillator.sync moves by at the
                            *   top of the wave, -48..48, default 0; the moved ratio is clamped
                            *   to 1..16. On a layer it syncs that layer, from a ratio of 1
                            *   when it has none; on the root it moves every layer with a
                            *   ratio over 1 */
    float vowel;           /**< Vowels of the SynthFormant filter at the top of the wave,
                            *   -4..4, default 0; the moved vowel is clamped to 0..4. Of the
                            *   sound on a layer too, as the cutoff */
    float position;        /**< Frames the position of SynthWavetable layers moves by at the
                            *   top of the wave, -256..256, default 0; the moved position is
                            *   clamped to the frames of the table */
    float sync;            /**< Cycle in beats of the Transport, 0..256, default 0 for rate.
                            *   When positive it wins over rate, like Delay.sync: a beat is a
                            *   quarter note at the tempo of the Transport, 120 without one,
                            *   so 0.25 is a sixteenth and 1.5 a dotted quarter. The LFO
                            *   starts at phase + fract(beat / sync), beat being the position
                            *   of the Transport at the start of the note, so every note sees
                            *   the same phase of the grid. A voice keeps the tempo it started
                            *   at: a tempo change applies to the next notes. Delay and fade
                            *   stay in seconds; SynthLfoRandom steps once per cycle */
});

/** A route of the modulation matrix of a synth sound, on an entity of its own: the value of a
 * source times an amount moves a target, on top of the fixed routes (the depths of SynthLfo,
 * SynthFilterEnvelope.amount, the velocity and keytrack fields), once per control block of
 * the LFOs.
 *
 * A child of the root moves every layer; a child of a layer moves that layer, and takes its
 * amp envelope as SynthSourceAmpEnvelope. The cutoff, resonance, vowel and pan are of the
 * sound either way. A route to a target that does not apply to its layer or to the sound
 * does nothing, without an error. Up to BOX_AUDIO_SYNTH_ROUTES per sound, see the modulation
 * matrix in docs/audio.md.
 */
ECS_STRUCT(SynthModRoute, {
    SynthModSource source; /**< The source, default SynthSourceAmpEnvelope */
    SynthModTarget target; /**< The target, default SynthTargetCutoff */
    float amount;          /**< Amount at a source value of 1, in the unit of the target,
                            *   default 0 */
});

/** A third envelope of a synth sound, on its root, with the segments of a SynthEnvelope: a
 * source of SynthModRoute, SynthSourceModEnvelope. It runs only when a route uses it, and does
 * not keep a sound playing: the sound ends with its SynthEnvelope.
 */
ECS_STRUCT(SynthModEnvelope, {
    float attack;          /**< Seconds from 0 to 1, at least 0.5 ms */
    float decay;           /**< Seconds from 1 to the sustain level */
    float sustain;         /**< Level after the decay, 0..1, default 0 */
    float release;         /**< Seconds from the sustain level to 0 */
    float hold;            /**< Seconds at the sustain level before the release */
    float curve;           /**< Curve of the decay and the release, 0 linear (default),
                            *   -16..16 */
    float attack_curve;    /**< Curve of the attack, 0 linear (default), -16..16 */
    float key_scale;       /**< Key scaling 0..1, default 0, as SynthEnvelope.key_scale */
});

/** A sound made by the synth from the components of this entity and its children, without a
 * file: SynthOscillator layers, each with an optional SynthUnison, a SynthTable, a
 * SynthEnvelope, a SynthDrive, a SynthFilter, a SynthFilterEnvelope, a SynthPitchEnvelope,
 * a SynthGlide, SynthLfo, a SynthModEnvelope and SynthModRoute children. Usually a prefab.
 *
 * It is compiled once, and again when one of its components changes, into a patch the core
 * holds; box_audio_play and AudioSource play it like any sound, with no allocation per play.
 */
ECS_STRUCT(SynthSound, {
    uint32_t seed;         /**< Seed of the noise and the variances, with a count of plays */
    float pitch_variance;  /**< Random share of the pitch per play, 0..1, default 0 */
    float level_variance;  /**< Random share of the level per play, 0..1, default 0 */
    float drift;           /**< Cents of analog drift, 0..50, default 0: the pitch of each
                            *   voice and of each unison voice of each layer wanders
                            *   smoothly within this, over about 0.2 to 2 seconds, and the
                            *   cutoff of its SynthFilter by a quarter of it; seeded from
                            *   seed and the count of plays */
    bool free_phase;       /**< Start the oscillators of each play, and their subs, at a
                            *   random phase instead of 0, seeded as drift; default false.
                            *   Unison voices always start at random phases */
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
    int64_t voices_refused; /**< Plays refused so far at the voice cap because every voice
                            *   that played had a higher priority */
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

/** How an Instrument spreads its notes over the stereo field. Each mode gives an offset that
 * is added to the pan of a note, the sum clamped to -1..1. */
ECS_ENUM(InstrumentSpreadMode, {
    InstrumentSpreadPitch,     /**< spread * clamp((pitch - root) / 24, -1, 1): lower notes to
                                *   the left, higher ones to the right, all the way two octaves
                                *   from the root */
    InstrumentSpreadRandom,    /**< spread * a random value in -1..1 per note, seeded from the
                                *   instrument and the count of its notes, so the same song
                                *   renders the same */
    InstrumentSpreadAlternate  /**< +spread and -spread on successive notes */
});

/** An instrument: a synth or a sampler that plays notes with a pitch, a velocity, a pan and a
 * duration, ad hoc with box_audio_play_note, on the bus of its AudioOutput, else of its sound.
 *
 * A note plays at the ratio 2^((pitch - root) / 12), clamped to 1/64..16 with one warning, at
 * the gain velocity^velocity_curve. A sampler starts without the 5 ms ramp of a play, so a drum
 * keeps its transient.
 *
 * With a gate the duration ends a note: a synth sustains, its holds ignored, and releases with
 * its envelopes, a sampler fades out over release. A gated note of a legato synth glides into
 * the voice of its channel while it holds, so overlapping notes are one phrase.
 */
ECS_STRUCT(Instrument, {
    ecs_entity_t sound;    /**< The sound that plays: SynthSound for a synth, AudioPcm or
                            *   SoundFile for a sampler */
    float root;            /**< MIDI note at which the sound plays as authored, default 69
                            *   (A4, 440 Hz) */
    float velocity_curve;  /**< Exponent of the velocity in the gain, default 2 */
    bool gate;             /**< The duration of a note ends it, default true; false plays the
                            *   sound as authored, like a drum hit */
    float release;         /**< Seconds a sampler note fades out over after its end, default
                            *   0.05; a synth uses the release of its envelopes */
    float spread;          /**< 0..1, default 0: how far the notes spread over the stereo
                            *   field, an offset added to the pan of each note */
    InstrumentSpreadMode spread_mode; /**< How the notes spread, default
                            *   InstrumentSpreadPitch */
});

/** A note of a NoteClip, in beats from the start of the clip.
 *
 * Its expression, in the style of MPE, is three curves whose key times are in beats from the
 * start of the note, sent to its voice only: a pitch bend, a slide or a fall in bend, a
 * vibrato swell in mod and a swell of brightness in pressure. A curve without keys is no
 * expression on its channel. The voice starts at the value of each curve at the start of the
 * note and follows it with timed ramps: one for each linear span, and ramps of at most 10 ms
 * for a curved span, until the end of the note; past its last key a curve holds. A legato
 * note gives its expression to the voice it shares, and a note without expression resets it.
 *
 * In script: {start: 0, duration: 2, pitch: 60, bend: {keys: [{0, 0}, {2, 12}]}}.
 */
ECS_STRUCT(Note, {
    float start;           /**< Beats from the start of the clip */
    float duration;        /**< Beats */
    float pitch;           /**< MIDI note number, a float so microtones work */
    float velocity;        /**< 0..1, default 1 */
    float pan;             /**< Pan of the voice, -1 left to 1 right */
    Curve bend;            /**< Pitch bend in semitones, -48..48, over beats of the note: the
                            *   pitch of the voice times 2^(bend / 12), its glide, oscillators,
                            *   pluck strings and key tracking included; synth and sample
                            *   voices. Without keys 0 */
    Curve mod;             /**< 0..1 over beats of the note: scales the vibrato of a synth
                            *   voice, the pitch depth of its LFOs. Without keys 1, so a sound
                            *   keeps its vibrato */
    Curve pressure;        /**< 0..1 over beats of the note: moves the cutoff of a synth voice
                            *   up by pressure times 2 octaves
                            *   (BOX_AUDIO_SYNTH_PRESSURE_OCTAVES). Without keys 0 */
});

/** A clip of notes, reusable: usually a prefab or a plain entity that any number of
 * ClipPlacement on any tracks name.
 *
 * box.audio keeps its own copy of the notes sorted by start, rebuilt when the clip is set, so
 * a window finds its notes without a scan of the clip. A change applies to what has not been
 * scheduled yet. Notes that start at or past length play only in a placement that does not
 * loop.
 */
ECS_STRUCT(NoteClip, {
    float length;          /**< Beats, default 4: the length of a pass when a placement loops
                            *   the clip, and of a placement of duration 0 */
    ecs_vec(Note) notes;   /**< The notes, in any order; the clip owns the keys of their
                            *   curves, copying the clip copies them */
});

/** A clip of recorded audio, reusable as a NoteClip is: a part of a sound that a ClipPlacement
 * plays on the bus of its track, a vocal, a loop or a stem.
 *
 * The offset is in seconds, not beats: a sound does not stretch with the tempo, so an offset in
 * beats would land on another sample after a tempo change. A pass of the clip lasts length
 * beats, or less when the sound ends first.
 */
ECS_STRUCT(AudioClip, {
    ecs_entity_t sound;    /**< The sound, an entity with AudioPcm or SoundFile */
    float offset;          /**< Seconds into the sound at which the clip starts */
    float length;          /**< Beats, default 4: the length of a pass when a placement loops
                            *   the clip, and of a placement of duration 0 */
});

/** A clip of automation, reusable as a NoteClip is: a Curve over beats that a ClipPlacement
 * on a Track plays into one float member of a bus or an effect, for a filter sweep, a send
 * that opens for a chorus or the fade of a track:
 * sweep { AutomationClip: {target: synth, component: AudioFilter, member: "cutoff",
 * curve: {keys: [{0, 200}, {4, 5000}]}} }.
 *
 * The keys are in beats from the start of the clip and in the units of the member; the kind
 * of the curve shapes the spans, and outside its keys the curve holds the first or the last.
 * A placement starts, lasts, offsets and loops it as it does a NoteClip; its gain and
 * transpose are ignored, and a track of only automation needs no instrument.
 *
 * Targets: the volume, pan and width of an AudioBus and the level of its sends
 * (sends[1].level), and every float member of the effect component of an effect entity, such
 * as AudioFilter.cutoff, Reverb.mix, Delay.feedback or bands[2].gain of an Equalizer. Members
 * that are not continuous (kinds, slopes, stages, voices, entities and bools) are not
 * automatable. A clip whose target, component or member is unknown or not automatable logs
 * one error that names it, and does nothing until it changes.
 *
 * While the Transport plays, the curve is sampled within the lookahead window every 10 ms of
 * output and at its keys, and each value is scheduled sample accurately at its frame: a bus
 * ramps to it as it does to any set, an effect gets all its parameters, built from a copy of
 * its component with the automated members written, and smooths the change over its usual
 * 10 to 20 ms. At a loop wrap the lanes go on from loop_start. A seek, a stop and a change
 * of the clip, the placement or the component of the target cancel what was scheduled ahead
 * and schedule again.
 *
 * The automation owns the value the output hears, never the component: game code keeps
 * reading and writing the authored value. While no automation covers the target, and while
 * the transport is stopped, the output has the value of the component again; the end of a
 * placement and a stop send it. A write of the component applies at once, until the next
 * value of the automation. See Automation in docs/audio.md.
 */
ECS_STRUCT(AutomationClip, {
    ecs_entity_t target;    /**< The entity with the AudioBus or the effect component */
    ecs_entity_t component; /**< The component: AudioBus or an effect component */
    char *member;           /**< Path of a float member, such as "cutoff", "volume" or
                             *   "sends[1].level" */
    float length;           /**< Beats, default 4: the length of a pass when a placement loops
                             *   the clip, and of a placement of duration 0 */
    Curve curve;            /**< The values: time in beats from the start of the clip, value
                             *   in the units of the member */
});

/** Source of an AudioModulator */
ECS_ENUM(AudioModSource, {
    AudioModLfo,           /**< A wave: AudioModulator.wave at rate or sync */
    AudioModSteps,         /**< A sequence of up to 16 values, one per step of sync beats */
    AudioModFollower       /**< The level of a bus, -60..0 dB mapped to 0..1 */
});

/** Wave of an AudioModulator. Its own enum rather than SynthLfoWave, which has no falling saw
 * and whose square and saw smooth their edges in the synth */
ECS_ENUM(AudioModWave, {
    AudioModSine,          /**< Sine, from 0 rising */
    AudioModTriangle,      /**< Triangle, from 0 rising */
    AudioModSquare,        /**< Square, 1 for the first half of a cycle, -1 for the second */
    AudioModSawUp,         /**< Rising saw, -1 to 1 */
    AudioModSawDown,       /**< Falling saw, 1 to -1 */
    AudioModRandom         /**< Sample and hold: a random value per cycle, the same for the
                            *   same cycle of the Transport when synced */
});

/** How the depth of an AudioModulator applies */
ECS_ENUM(AudioModScale, {
    AudioModLinear,        /**< The value plus depth times the source, in the units of the
                            *   member */
    AudioModOctaves        /**< The value times 2^(depth times the source): depth in octaves,
                            *   for a member that is a frequency (cutoff, center, frequency
                            *   of a band, damping of a Delay), whose linear sweep sounds
                            *   lopsided */
});

/** Modulators the core runs; more log one error that names the modulator and do nothing */
#define BOX_AUDIO_MODULATORS 64

/** Modulators that may move one member; more log one error that names the modulator and do
 * nothing */
#define BOX_AUDIO_MODULATORS_PER_TARGET 4

/** A modulator, on an entity of its own: moves a float member of a bus or an effect on its own,
 * in time with the song, for a filter that wobbles in eighths, a send that pulses, a tremolo,
 * an auto-pan or an auto-wah:
 * wobble { AudioModulator: {target: bass_filter, component: AudioFilter, member: "cutoff",
 * scale: AudioModOctaves, depth: 2, sync: 0.5} }.
 *
 * target, component and member name what it moves as an AutomationClip does, with the same
 * targets: volume, pan, width and sends[i].level of an AudioBus or of the master, and every
 * float member of the effect component of an effect entity.
 *
 * Its value adds to the value the output has, the one of the component or of an automation
 * lane that covers the member, and the sum is clamped to the range of the member. depth is the
 * swing either way at the top of the source, in the units of the member, or in octaves with
 * AudioModOctaves. The modulator never writes the component. Up to
 * BOX_AUDIO_MODULATORS_PER_TARGET modulators of one member add up.
 *
 * A positive sync locks it to the Transport: the phase at a frame is phase + fract(beat /
 * sync), beat being the position of the Transport at that frame, so a seek, a loop wrap and a
 * tempo change keep it on the grid. A stopped Transport, or none, leaves the beat where it
 * stands and moves it on at 120 bpm. Without sync an LFO runs at rate from the moment it
 * starts.
 *
 * It runs in the audio core, once per 32 frames and at the edges of its square, saws, steps
 * and samples: a bus ramps to each value, an effect smooths it as any change. It fades in over
 * 50 ms when it starts; disabling, deleting or retargeting it fades it out over 50 ms, and the
 * member returns to its value without a click. A modulator whose target, component or member
 * is unknown logs one error that names it until it changes; one whose effect is in no insert
 * slot waits for one.
 *
 * There is no stereo phase offset: every member it can move has one value for both channels
 * (one cutoff for the filter of a bus, one pan of a bus), so the core has no right channel to
 * move apart. An auto-pan is an LFO on pan. See Modulators in docs/audio.md.
 */
ECS_STRUCT(AudioModulator, {
    ecs_entity_t target;    /**< The entity with the AudioBus or the effect component */
    ecs_entity_t component; /**< The component: AudioBus or an effect component */
    char *member;           /**< Path of a float member, such as "cutoff", "volume" or
                             *   "sends[1].level" */
    AudioModSource source;  /**< What moves it, default AudioModLfo */
    float depth;            /**< Swing either way at a source of 1, in the units of the member,
                             *   or in octaves; negative turns it upside down. Default 0 */
    AudioModScale scale;    /**< Linear (default) or in octaves, for a frequency */
    AudioModWave wave;      /**< Wave of an LFO, default AudioModSine */
    float rate;             /**< Hz of an LFO, or steps per second, when sync is 0, 0..100,
                             *   default 1 */
    float sync;             /**< Beats of a cycle of an LFO, or of a step, 0..256, default 0 for
                             *   rate; when positive it wins over rate, like SynthLfo.sync and
                             *   Delay.sync: 0.5 is an eighth, 0.25 a sixteenth */
    float phase;            /**< Phase in turns, 0..1: of a cycle of an LFO, of the whole
                             *   sequence of steps */
    bool unipolar;          /**< An LFO moves 0..1 instead of -1..1, for a tremolo that only
                             *   dips: depth -1 on a volume of 1 */
    float steps[16];        /**< Values of the steps, -1..1 */
    int32_t step_count;     /**< Steps that play, 1..16, default 16 */
    float smooth;           /**< Share of a step the value glides from the step before over, 0
                             *   for hard steps (default) .. 1 */
    ecs_entity_t key;       /**< Bus whose level a follower follows, measured before its
                             *   inserts and volume; 0 for the bus that holds the target effect,
                             *   or the target bus */
    float attack;           /**< Seconds of the rise of a follower, default 0.01 */
    float release;          /**< Seconds of the fall of a follower, default 0.1 */
});

/** The grid a Track swings */
ECS_ENUM(TrackSwingGrid, {
    TrackSwing8 = 0,        /**< Eighths, a step of 0.5 beat */
    TrackSwing16 = 1        /**< Sixteenths, a step of 0.25 beat */
});

/** How far the notes of a Track move at random from where they are programmed. The jitter is
 * seeded from the track, the placement, the pass of the clip and the note, so a render is
 * deterministic and every pass of a looping clip differs.
 */
ECS_STRUCT(TrackHumanize, {
    float timing;          /**< Milliseconds, 0..50, default 0: the most a note start moves
                            *   either way */
    float velocity;        /**< Share, 0..1, default 0: the most the velocity changes either
                            *   way, relative, clamped to 0..1 */
});

/** A track of the timeline: a mixer channel whose notes play on an instrument. It brings an
 * AudioBus (the With trait), so it has a volume, a pan, a mute, an output and sends, and its
 * notes play on its own bus.
 *
 * Its ClipPlacement children place clips on the timeline. While the Transport plays, every
 * enabled track and placement schedules the notes that start in the lookahead window, each a
 * note of the instrument with the frame of its start. A Disabled or deleted track releases its
 * notes and schedules nothing more; a track whose instrument sound is not ready drops its
 * notes and logs once. See Clips and tracks in docs/audio.md.
 *
 * Swing works on the grid of the timeline, from its beat 0, so clips placed anywhere swing
 * alike: a start p in a pair of steps [0, 2 * step) goes to p * (1 + swing) before step and to
 * step * (1 + swing) + (p - step) * (1 - swing) after it. Humanize then moves the start by up
 * to timing ms either way and scales the velocity by 1 + r * velocity. A note never starts
 * before its pass of the clip began or before the frame the transport started or seeked to,
 * and still ends at the earliest of the end of its pass, of the placement and loop_end.
 */
ECS_STRUCT(Track, {
    ecs_entity_t instrument; /**< The entity with Instrument that plays the notes of its note
                              *   clips; a track of only audio clips and automation needs
                              *   none */
    int32_t priority;        /**< Priority of the voices of its notes and audio clips, 0..255,
                              *   default 1: at the voice cap of 64 a play steals only a
                              *   voice of at most its priority, so one shots (0) cannot steal
                              *   the voices of a track; streams play at 255 */
    float swing;             /**< 0..0.75, default 0: the off-beat of every pair of steps of
                              *   swing_grid moves later by swing * step, the downbeats stay
                              *   put; notes keep their duration, audio clips do not swing */
    TrackSwingGrid swing_grid; /**< The step of the swing, default TrackSwing8 */
    TrackHumanize humanize;  /**< Random timing and velocity of the notes, default none */
});

/** A clip on the timeline, on a child of a Track: a NoteClip, an AudioClip or an
 * AutomationClip.
 *
 * A note ends at the earliest of its own end, the end of its pass of the clip, the end of the
 * placement and the loop_end of a Transport that loops. A Disabled or deleted placement
 * releases its notes.
 *
 * An AudioClip plays its sound on the bus of the track at the gain and the pitch ratio
 * 2^(transpose / 12), each pass from its frame to the earliest of the end of the pass, of the
 * sound, of the placement and loop_end. See Audio clips in docs/audio.md.
 *
 * An AutomationClip moves its target from the start to the end of the placement, at the
 * value of its curve at the beat of the clip; at the end its target returns to the value of
 * its component.
 */
ECS_STRUCT(ClipPlacement, {
    ecs_entity_t clip;     /**< The entity with NoteClip, AudioClip or AutomationClip */
    double start;          /**< Beats on the timeline where the placement starts */
    double duration;       /**< Beats, 0 for the length of the clip minus the offset */
    bool loop;             /**< Repeat the clip every length beats until the duration ends */
    double offset;         /**< Beats into the clip at which the placement starts; with a loop
                            *   the passes go on from there and wrap at length */
    float gain;            /**< Linear gain, default 1, times the gain of the velocity */
    float transpose;       /**< Semitones added to every pitch; of an audio clip the pitch
                            *   ratio 2^(transpose / 12) */
});

/** Where box_audio_play_quantized starts a play on the grid of the Transport */
ECS_ENUM(AudioQuantize, {
    AudioQuantizeNone = 0,  /**< At once, as box_audio_play */
    AudioQuantizeBeat = 1,  /**< On the next beat, a quarter note */
    AudioQuantizeBar = 2    /**< On the next bar of the time signature of the transport */
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

/** Play a sound once on the bus of its AudioOutput, starting on the next beat or bar of the
 * Transport.
 *
 * It starts sample accurately on the first boundary at or after the clock of the output, after
 * a loop wrap on the timeline from loop_start on. With the transport stopped, without a
 * Transport or with AudioQuantizeNone it starts at once, as box_audio_play.
 *
 * The transport never stops or moves it once queued. The rules of box_audio_play apply.
 * Scripts call it as box.audio.play_quantized(sound, volume, quantize).
 *
 * @param world The world
 * @param sound The sound, an entity with AudioPcm, SoundFile or SynthSound
 * @param volume Linear gain
 * @param quantize Where it starts
 * @return Whether the play was queued
 */
bool box_audio_play_quantized(
    ecs_world_t *world,
    ecs_entity_t sound,
    float volume,
    AudioQuantize quantize);

/** Play a note of an Instrument, without an entity per note.
 *
 * The note starts at the clock of the output, so it applies at the next block as a play does,
 * and with a gate ends duration beats later at the tempo of the Transport (120 without one),
 * also while the transport is stopped, which does not affect the note.
 *
 * It plays on the bus of the AudioOutput of the instrument, else of its sound, else the
 * master. The rules of box_audio_play apply. Scripts call it as
 * box.audio.play_note(instrument, pitch, velocity, duration).
 *
 * @param world The world
 * @param instrument The entity with Instrument
 * @param pitch MIDI note number, a float so microtones work
 * @param velocity Velocity, 0..1
 * @param duration Beats
 * @return Whether the note was queued
 */
bool box_audio_play_note(
    ecs_world_t *world,
    ecs_entity_t instrument,
    float pitch,
    float velocity,
    float duration);

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
