/**
 * @file box/audio_synth.h
 * @brief Synth sounds: oscillators, strings, wavetables, envelopes, filters, LFOs and the
 * modulation matrix.
 */

#ifndef BOX_AUDIO_SYNTH_H
#define BOX_AUDIO_SYNTH_H

#include "types.h"

#ifdef BOX_AUDIO_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

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

/** Lowest frequency of a SynthPluck or SynthBowed layer in Hz; a lower one, its frequency_end
 * included, is clamped with one warning */
#define BOX_AUDIO_SYNTH_PLUCK_LOWEST (40.0f)

/** Octaves the pressure of a Note moves the cutoff of a synth voice up by at pressure 1, the
 * same in the core */
#define BOX_AUDIO_SYNTH_PRESSURE_OCTAVES 2.0f

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
    SynthWavetable,        /**< The frames of the Wavetable of the SynthTable of the layer,
                            *   morphed by its position; a sine without one */
    SynthBowed             /**< A bowed string, a waveguide driven by a friction bow, set by
                            *   the SynthBow of the layer; the bow moves with the envelope of
                            *   the layer and the string rings out after its release */
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
                            *   formants of SynthFilter.vowel: vowels, talking pads, wah. A
                            *   SynthFilter only; an AudioFilter takes it as SynthLowPass */
    SynthComb              /**< A feedback comb with peaks at the harmonics of SynthFilter.tune
                            *   times the note: metallic, flanged and resonator sounds. A
                            *   SynthFilter only; an AudioFilter takes it as SynthLowPass */
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
    SynthTargetIndex,          /**< Radians of the index of a SynthPhase modulator, -32..32,
                                *   or the depth of a SynthRing one, clamped as its index.
                                *   Nothing on a layer that does not modulate */
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
    SynthTargetHardSync,       /**< Semitones of the ratio of SynthOscillator.sync, -48..48,
                                *   clamped to 1..16. Nothing on a layer without sync: a ratio
                                *   over 1 or a SynthLfo.hard_sync of the layer */
    SynthTargetPosition,       /**< Frames of the position of a SynthWavetable layer,
                                *   -256..256; the moved position is clamped to 0..N-1 of its
                                *   table. Nothing on a layer that is not a wavetable */
    SynthTargetVowel,          /**< Vowels of the SynthFormant filter, -4..4; the moved vowel
                                *   is clamped to 0..4. Of the sound on a layer too, as the
                                *   cutoff. Nothing without a SynthFormant filter */
    SynthTargetBowPressure,    /**< Share of SynthBow.pressure, -1..1; the moved pressure is
                                *   clamped to 0..1. Nothing on a layer that is not SynthBowed */
    SynthTargetBowSpeed,       /**< Share of SynthBow.speed, -1..1; the moved speed is clamped
                                *   to 0..1. Nothing on a layer that is not SynthBowed */
    SynthTargetBowPosition     /**< Share of the string of SynthBow.position, -0.5..0.5; the
                                *   moved position is clamped to 0.02..0.5. Nothing on a layer
                                *   that is not SynthBowed */
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
    float sub;             /**< Level 0..1 of a band limited square sub_octave octaves under
                            *   the layer, default 0 (off). It follows the pitch of the layer
                            *   but not its phase modulation; ignored by SynthNoise, SynthPluck */
    int32_t sub_octave;    /**< Octaves the sub is under the layer, 1 or 2, default 1; 0
                            *   counts as 1 and other values are clamped */
    float velocity;        /**< SynthPhase, SynthRing: share 0..1 of the index that the
                            *   velocity of a note scales, index (1 - share + share v), so
                            *   soft notes have fewer sidebands; default 0 */
    float sync;            /**< Hard sync: ratio 1..16 of the wave to the note, default 0 (off).
                            *   The wave restarts on every cycle of the note frequency, band
                            *   limited, for sync leads; ignored by SynthNoise and SynthPluck */
    SynthNoiseColor color; /**< SynthNoise: color of the noise, default SynthNoiseWhite,
                            *   seeded as white noise; each unison voice filters its own.
                            *   Ignored by the other waves */
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

/** The bow and the string of a SynthBowed layer, on the layer: a digital waveguide string driven
 * by a bow through a friction curve that sticks and slips. Without one a SynthBowed layer takes
 * the defaults.
 *
 * The bow speed follows the envelope of the layer, its own or the one of the root: the attack
 * starts the bow and the release lifts it, after which the string rings out. The voice ends once
 * the envelope ended and the string is silent. Pitch changes move the string without a restart.
 */
ECS_STRUCT(SynthBow, {
    float pressure;        /**< Force of the bow, 0..1, default 0.5: low gives a soft, airy
                            *   tone, the middle the Helmholtz motion, high a harder tone with
                            *   more upper harmonics */
    float speed;           /**< Velocity of the bow, 0..1, default 0.5: the level, the tone
                            *   stays; 0 is silent */
    float position;        /**< Distance of the bow from the bridge as a share of the string,
                            *   0.02..0.5, default 0.12: nearer is brighter. It notches the
                            *   harmonics at the multiples of 1 / position */
    float brightness;      /**< Damping of the loop, 0 dark to 1 bright, default 0.5, as
                            *   SynthString.brightness: a lowpass that follows the note */
    float noise;           /**< Noise of the bow hair added at the contact point, 0..1,
                            *   default 0.1 */
    float velocity;        /**< Share 0..1 of the pressure and the speed that the velocity of a
                            *   note scales, as SynthString.velocity; default 0 */
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
 * The main thread builds it when it is set, each frame as band limited mip levels, one per
 * octave, and hands it to the core. Voices that play a table that changes or is deleted play the
 * old one until they end.
 *
 * A table from a sound is cut into frames of frame_size samples, at most 256, mixed to mono and
 * normalized. Until the sound is ready its layers play a sine; a sound that fails to load or is
 * shorter than one frame logs one error that names the Wavetable.
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
 * them. The envelope of the layer and SynthLfo.position move it, clamped to the table.
 *
 * A SynthWavetable layer without a SynthTable, or whose table is not an entity with Wavetable,
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
 * Each segment is linear by default. A positive curve bends it toward an exponential shape, fast
 * at first and slow toward its target; a negative one bends it the other way. Plucks use a curve
 * of 4 to 6, pads 2 to 3.
 *
 * On the root it applies to every layer without its own, on a layer to that layer. A sound ends
 * once every envelope reached silence; a layer without any envelope logs one error and the sound
 * plays for BOX_AUDIO_SYNTH_ENDLESS seconds.
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
                            *   times 2^(-key_scale key / 12) at the key of the note, so at 1
                            *   they halve per octave above the root */
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
 * and panned the same way, and on a SynthBowed layer a bowed string; each takes one of the 32
 * strings of the core, and a play that does not find one for each copy is ignored.
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
 * SynthFormant runs three bandpasses at the formants of a vowel, and SynthComb a feedback comb
 * tuned to the note whose cutoff damps its loop; see docs/audio.md for which members apply to
 * each.
 *
 * The key of a voice, which keytrack and SynthEnvelope.key_scale follow, is 12 log2 of the pitch
 * ratio of its play: relative to Instrument.root for a note of an instrument, to a ratio of 1 for
 * any other play.
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
                             *   transistor ladder that self-oscillates at resonance 1; its
                             *   drive is the gain into it, -6 dB to +12 dB */
    float vowel;           /**< SynthFormant: the vowel, 0 a, 1 e, 2 i, 3 o, 4 u, default 0;
                            *   a fraction interpolates the two nearest vowels. SynthLfo.vowel
                            *   and SynthFilterEnvelope.vowel move it, clamped to 0..4 */
    float feedback;        /**< SynthComb: gain of the delayed output fed back, -0.98..0.98,
                            *   default 0.5: positive peaks at the harmonics of the comb,
                            *   negative at its odd harmonics an octave down */
    float tune;            /**< SynthComb: ratio of the frequency of the comb to the note,
                            *   0.125..8, default 1; 2 an octave up, 1.5 a fifth */
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

/** A pitch envelope of a synth sound, relative to the note: the pitch starts amount semitones
 * off the note and returns to it over the decay, the drop of a kick or the scoop of brass.
 *
 * Over the release of the amplitude envelope of the layer the offset moves to release_amount. It
 * adds to the glide, the LFO pitch and the sweep of SynthOscillator.
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
    float sync;            /**< Semitones the ratio of SynthOscillator.sync moves by at the
                            *   start of a note, -48..48, default 0, returning with the decay.
                            *   Only on layers with a ratio over 1, clamped to 1..16 */
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
                            *   top of the wave, -48..48, default 0, clamped to 1..16. On a
                            *   layer it syncs that layer; on the root every synced layer */
    float vowel;           /**< Vowels of the SynthFormant filter at the top of the wave,
                            *   -4..4, default 0; the moved vowel is clamped to 0..4. Of the
                            *   sound on a layer too, as the cutoff */
    float position;        /**< Frames the position of SynthWavetable layers moves by at the
                            *   top of the wave, -256..256, default 0; the moved position is
                            *   clamped to the frames of the table */
    float sync;            /**< Cycle in beats of the Transport, 0..256, default 0 for rate;
                            *   when positive it wins over rate. The phase follows the beat at
                            *   the start of a note; delay and fade stay in seconds */
});

/** A route of the modulation matrix of a synth sound, on an entity of its own: the value of a
 * source times an amount moves a target, on top of the fixed routes, once per control block.
 *
 * A child of the root moves every layer; a child of a layer moves that layer, and takes its amp
 * envelope as SynthSourceAmpEnvelope. The cutoff, resonance, vowel and pan are of the sound.
 *
 * A route to a target that does not apply does nothing. Up to BOX_AUDIO_SYNTH_ROUTES per sound,
 * see the modulation matrix in docs/audio.md.
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
 * file: SynthOscillator layers and their SynthEnvelope, SynthFilter, SynthLfo and other synth
 * components. Usually a prefab.
 *
 * It is compiled once, and again when one of its components changes, into a patch the core
 * holds; box_audio_play and AudioSource play it like any sound, with no allocation per play.
 */
ECS_STRUCT(SynthSound, {
    uint32_t seed;         /**< Seed of the noise and the variances, with a count of plays */
    float pitch_variance;  /**< Random share of the pitch per play, 0..1, default 0 */
    float level_variance;  /**< Random share of the level per play, 0..1, default 0 */
    float drift;           /**< Cents of analog drift, 0..50, default 0: the pitch of each
                            *   voice wanders smoothly within this, and the cutoff by a quarter
                            *   of it; seeded from seed and the count of plays */
    bool free_phase;       /**< Start the oscillators of each play, and their subs, at a
                            *   random phase instead of 0, seeded as drift; default false.
                            *   Unison voices always start at random phases */
});

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
