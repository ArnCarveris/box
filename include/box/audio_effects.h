/**
 * @file box/audio_effects.h
 * @brief Effects that run in the insert slots of an AudioBus.
 */

#ifndef BOX_AUDIO_EFFECTS_H
#define BOX_AUDIO_EFFECTS_H

#include "types.h"
#include "audio_synth.h"

#ifdef BOX_AUDIO_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** A filter, an effect in an insert slot of an AudioBus, as a Reverb is: the state variable
 * filter of SynthFilter on the signal of a bus, for a sweep, a muffled bus or a telephone voice.
 * music { AudioBus: {effects: [music]}; AudioFilter: {cutoff: 400, resonance: 0.3} }.
 *
 * The cutoff and the resonance move toward a new value over about 10 ms, so a sweep driven from
 * a script every frame is smooth; the mix and the drive move over 20 ms. A change of the kind or
 * the slope fades the filtered signal out and back in over 40 ms.
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
 * stereo linked, it reduces the gain of the louder channel over its threshold by its ratio, with
 * a soft knee. An AudioMeter on its entity reads its gain reduction.
 *
 * With a sidechain it keys on the output of that channel instead of its own input, to duck one
 * channel under another: music { AudioBus: {effects: [music]};
 * Compressor: {threshold: -30, ratio: 8, sidechain: sfx} }.
 *
 * The sidechain runs first; one that would form a cycle logs one error, and the compressor keys
 * on its own input, as it does when its sidechain is deleted.
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
 * of the louder channel reaches its threshold, stays open while the level is above the threshold
 * less 4 dB and for its hold after, and then closes to its range.
 *
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
 * every time seconds, each feedback times the last and darkened by a lowpass at damping Hz.
 *
 * With sync it follows the tempo of the Transport, for a dotted eighth on a return:
 * echo { AudioBus: {kind: AudioChannelReturn, effects: [echo]};
 * Delay: {sync: 0.75, feedback: 0.4, mix: 1} }.
 *
 * Every delay has its own memory of BOX_AUDIO_DELAY_MAX seconds. A change of the time crossfades
 * to the new echo over 50 ms; feedback, mix and ping pong move over 20 ms.
 */
ECS_STRUCT(Delay, {
    float time;            /**< Seconds between the repeats, default 0.25, 0 to
                            *   BOX_AUDIO_DELAY_MAX (4) */
    float sync;            /**< Beats between the repeats, 0 (the default) to use time; when
                            *   positive it wins over time at the tempo of the Transport (120
                            *   without one): 0.5 is an eighth, 0.75 a dotted eighth */
    float feedback;        /**< Share of a repeat in the next, 0 to 0.95, default 0.35 */
    float mix;             /**< Wet level, 0 to 1, default 0.3; the dry signal passes at
                            *   1 - mix */
    bool ping_pong;        /**< Bounce the repeats between left and right, default false: the
                            *   input, summed to mono, repeats on the left first */
    float damping;         /**< Cutoff in Hz of a one pole lowpass in the feedback path, default
                            *   6000, at least 20; at 0.49 of the sample rate or above the
                            *   repeats stay bright */
});

/** A phaser, an effect in an insert slot of an AudioBus, as a Reverb is: a chain of first order
 * allpasses swept around a center by a sine LFO, mixed with the dry signal:
 * keys { AudioBus: {effects: [keys]}; Phaser: {stages: 6, rate: 0.3, feedback: 0.5} }.
 *
 * The LFO runs on through a change of the rate without a jump; the right channel runs it stereo
 * half turns later. Depth, center, feedback, mix and stereo move over 20 ms; a change of the
 * stages fades the phased signal out and back in over 40 ms.
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

/** A chorus, an effect in an insert slot of an AudioBus, as a Reverb is: up to 4 taps of a delay
 * line that sine LFOs move around delay by depth, panned across the stereo field by spread:
 * pad { AudioBus: {effects: [pad]}; Chorus: {voices: 3, spread: 1, mix: 0.4} }.
 *
 * With feedback it is a flanger: the first tap of each channel goes back into its line, so a
 * short delay resonates at multiples of 1000 / delay Hz, and a negative feedback sounds hollow:
 * drums { AudioBus: {effects: [drums]}; Chorus: {voices: 1, delay: 2, feedback: 0.7} }.
 *
 * Every chorus has its own memory of BOX_AUDIO_CHORUS_MAX milliseconds. Depth, delay, mix, spread
 * and feedback move over 20 ms; a change of the voices fades the wet signal out and back in.
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
 * lo-fi. Each channel runs through the clipper of the mode at twice the sample rate, its level
 * compensated so that the drive mostly changes the tone and not the loudness.
 *
 * low_cut and tone shape what distorts before the clipper; high_cut, bits and rate shape the
 * output: drums { AudioBus: {effects: [drums]}; Saturator: {drive: 0.6, asymmetry: 0.2} }.
 * Lo-fi drums: {drive: 0.4, bits: 8, rate: 8000, high_cut: 6000}.
 *
 * The oversampling delays the channel, its dry signal too, by 15 frames (23 in SaturatorFold).
 * Parameters move over 20 ms, a change of the mode is at once. An AudioMeter on its entity reads
 * how hard the clipper squashes.
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
    float tone;            /**< -1 to 1, default 0: a tilt of up to 9 dB around 1 kHz before
                            *   the clipper, the inverse after it; above 0 the highs distort
                            *   most, below 0 the lows */
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

/** A vocoder, an effect in an insert slot of an AudioBus, as a Reverb is: the spectrum of its
 * modulator, a channel keyed as the sidechain of a Compressor, shapes the input of its own
 * channel: pad { AudioBus: {effects: [pad]}; Vocoder: {modulator: drums, bands: 16} }.
 *
 * Both run through bands log spaced bandpasses; an envelope follower on each modulator band
 * scales the carrier band of the same index. Shift moves the carrier bands against the modulator
 * bands, and unvoiced adds noise to the high carrier bands so consonants come through.
 *
 * The modulator runs first. A missing, deleted or cyclic modulator logs one error and the vocoder
 * passes its input. Parameters move over 20 ms; a change of the bands fades the signal.
 */
ECS_STRUCT(Vocoder, {
    ecs_entity_t modulator; /**< Channel (an AudioBus) whose output is the modulator, the key */
    int32_t bands;         /**< Bands, 8 to 32, default 16 */
    float low;             /**< Center of the lowest band in Hz, default 100, at least 20 */
    float high;            /**< Center of the highest band in Hz, default 8000, at least an
                            *   octave over low and under 0.45 of the sample rate */
    float q;               /**< Sharpness of the bands, 0.25 to 8, default 1: at 1 neighboring
                            *   bands cross at -3 dB, above 1 they are narrower */
    float attack;          /**< Seconds in which a band envelope rises 63% of a step, default
                            *   0.005, 0.0001 to 1 */
    float release;         /**< Seconds in which it falls 63% of a step, default 0.05, 0.001 to
                            *   5 */
    float shift;           /**< -1 to 1, default 0: moves the carrier bands up to an octave up
                            *   or down against the modulator bands, for a formant shift */
    float unvoiced;        /**< 0 to 1, default 0: white noise added to the carrier of the high
                            *   bands, so consonants come through */
    float mix;             /**< Wet level, 0 to 1, default 1; the dry signal passes at
                            *   1 - mix */
});

/** Mode of a FrequencyShifter */
ECS_ENUM(FrequencyShifterMode, {
    FrequencyShift,        /**< Every partial moves up or down by the shift in Hz */
    FrequencyRing          /**< Ring modulation by a sine at the magnitude of the shift: each
                            *   partial splits into one shift above and one below it at half
                            *   the level, the partial itself gone */
});

/** A frequency shifter, an effect in an insert slot of an AudioBus, as a Reverb is: every
 * partial moves by the same number of Hz, so harmonic sounds turn inharmonic. The ring mode is a
 * ring modulator: pad { AudioBus: {effects: [pad]}; FrequencyShifter: {shift: 2, stereo: 1} }.
 *
 * It shifts the analytic signal of an allpass Hilbert transformer, so a shift of 0 at a mix of 1
 * changes the phase only. The feedback runs through a DC blocker and a soft limit.
 *
 * Changes move over 20 ms; a change of the mode crossfades. Once its input is silent and its
 * allpasses decayed it stops until its input sounds again.
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

/** Longest window of a PitchShifter in milliseconds, which sizes its memory */
#define BOX_AUDIO_PITCH_SHIFTER_WINDOW_MAX 100.0f

/** A pitch shifter, an effect in an insert slot of an AudioBus, as a Reverb is: thickens a lead
 * by a few cents, adds octaves and makes harmonies without changing the speed of what plays:
 * lead { AudioBus: {effects: [lead]}; PitchShifter: {cents: 8, stereo: 1, window: 30} }.
 *
 * Each channel is a delay line read by two crossfaded taps that slide through the window; shorter
 * windows suit percussive material, longer ones pads. Feedback shifts the signal again on each
 * pass, soft clipped so it stays bounded.
 *
 * The shifted signal comes up to a window late and the dry signal is not delayed to match. All
 * parameters move over 20 ms.
 */
ECS_STRUCT(PitchShifter, {
    float semitones;       /**< Shift, -24 to 24, default 0 */
    float cents;           /**< Detune on top of the semitones, -100 to 100, default 0 */
    float stereo;          /**< 0 to 1, default 0: the cents of the right channel go from cents
                            *   at 0 to -cents at 1 */
    float window;          /**< Milliseconds of the window, 10 to 100, default 40 */
    float feedback;        /**< Share of the shifted signal fed back into the line, 0 to 0.9,
                            *   default 0 */
    float mix;             /**< Wet level, 0 to 1, default 0.5; the dry signal passes at
                            *   1 - mix */
});

/** A pattern gate, an effect in an insert slot of an AudioBus, as a Reverb is: a tempo-synced
 * level pattern that chops a sustained sound into rhythm: pad { AudioBus: {effects: [pad]};
 * PatternGate: {steps: [1, 0, 1, 1, 0, 1, 0, 1], length: 8, release: 0.05} }.
 *
 * The pattern is locked to the beat of the Transport, at 120 bpm without one that plays. A step
 * is open for its gate share at its level and closed at the floor after; the gain moves linearly
 * over attack and release.
 *
 * Swing bends the step grid as Track.swing does. An AudioMeter on its entity reads its gain
 * reduction. It stops while its input is silent.
 */
ECS_STRUCT(PatternGate, {
    float steps[16];       /**< Level of each step, 0 (closed) to 1, default all 1; an accent
                            *   is a level under 1 */
    int32_t length;        /**< Steps in the pattern, 1 to 16, default 16 */
    float rate;            /**< Beats per step, default 0.25 (sixteenths); 0.375 is a dotted
                            *   sixteenth, 1 / 6 a sixteenth triplet, 1/64 to 16 */
    float swing;           /**< 0 to 0.75, default 0: the second step of every pair starts
                            *   later by swing steps, as Track.swing */
    float attack;          /**< Seconds of an edge from closed to open, default 0.002, 0.0005
                            *   to 2 */
    float release;         /**< Seconds of an edge from open to closed, default 0.03, 0.0005
                            *   to 2; a longer one gives a softer pulse */
    float gate;            /**< Share of each step that is open, 0.05 to 1, default 0.5; at 1
                            *   neighbouring open steps run together */
    float floor;           /**< Gain when closed in dB, -80 (default) to 0; a higher floor
                            *   pumps instead of chopping */
    float mix;             /**< Wet level, 0 to 1, default 1; the dry signal passes at
                            *   1 - mix */
});

/** A rotary, an effect in an insert slot of an AudioBus, as a Reverb is: a rotating speaker
 * cabinet with a tube preamp, a drum below 800 Hz and a horn above, heard by two virtual mics:
 * organ { AudioBus: {effects: [organ]}; Rotary: {speed: 0, drive: 0.3} }.
 *
 * The rotors turn at their slow rates at speed 0 and their fast rates at speed 1. The horn reaches
 * a new speed within acceleration seconds and the drum three times slower, as a real cabinet.
 *
 * Balance, spread, drive and mix move over 20 ms. Once its input was silent for 50 ms it stops
 * until its input sounds again, its rotors turning on.
 */
ECS_STRUCT(Rotary, {
    float speed;           /**< 0 to 1, default 0: 0 is slow (chorale), 1 fast (tremolo) */
    float horn_slow;       /**< Hz of the horn at speed 0, default 0.8, 0 to 20 */
    float horn_fast;       /**< Hz of the horn at speed 1, default 6.7, 0 to 20 */
    float drum_slow;       /**< Hz of the drum at speed 0, default 0.7, 0 to 20 */
    float drum_fast;       /**< Hz of the drum at speed 1, default 5.9, 0 to 20 */
    float acceleration;    /**< Seconds of the horn to a new speed, default 1, 0.01 to 20; the
                            *   drum takes three times as long */
    float balance;         /**< -1 to 1, default 0: -1 is the drum alone, 1 the horn alone */
    float spread;          /**< 0 to 1, default 1: the stereo width of the mics; 0 is mono, 1
                            *   puts them half a turn apart */
    float drive;           /**< 0 to 1, default 0: the share of the tube preamp, its gain up to
                            *   +24 dB */
    float mix;             /**< Wet level, 0 to 1, default 1; the dry signal passes at
                            *   1 - mix */
});

/** Longest slice of a BeatRepeat and longest lag of a TapeStop in seconds, which size their
 * memory: a slice of 4 beats under 60 bpm is held to it */
#define BOX_AUDIO_BEAT_REPEAT_MAX 4.0f
#define BOX_AUDIO_TAPE_STOP_MAX 4.0f

/** A beat repeat, an effect in an insert slot of an AudioBus, as a Reverb is: the stutter of a
 * fill, which captures a slice of the beat and repeats it while active is up:
 * drums { AudioBus: {effects: [drums]}; BeatRepeat: {slice: 0.125, decay: 0.8} }.
 *
 * When active rises to 0.5 or above, the capture waits for the next grid point of the Transport
 * and holds slice beats. Each repeat is decay times the gain and pitch semitones from the one
 * before it, reversed with reverse, and sounds for gate of the slice.
 *
 * When active falls under 0.5 the dry input returns at once over a 2 ms crossfade. Every beat
 * repeat has its own memory of BOX_AUDIO_BEAT_REPEAT_MAX seconds per channel.
 */
ECS_STRUCT(BeatRepeat, {
    float active;          /**< 0 to 1, default 0: 0.5 and above repeats */
    float slice;           /**< Beats of the slice, 1/32 to 4, default 0.25 */
    float grid;            /**< Beats of the grid the capture waits for, 0 to 4; 0 captures
                            *   at once; default -1, any negative grid, is the slice */
    float decay;           /**< Gain of a repeat against the one before it, 0 to 1, default
                            *   1: 0.5 drops each repeat by 6 dB */
    float pitch;           /**< Semitones of a repeat against the one before it, -12 to 12,
                            *   default 0; the repeats add up to at most 48 either way */
    float reverse;         /**< 0 to 1, default 0: 0.5 and above plays the repeats after the
                            *   first reversed */
    float gate;            /**< Share of each repeat that sounds, 0.1 to 1, default 1 */
    float mix;             /**< Level of the repeats while it repeats, 0 to 1, default 1: the
                            *   input is replaced; the dry signal passes at 1 - mix */
});

/** A tape stop, an effect in an insert slot of an AudioBus, as a Reverb is: slows what plays
 * through it down to a halt, pitch and time together, and spins it back up:
 * master_fx { AudioBus: {effects: [master_fx]}; TapeStop: {stop_time: 1, curve: 0.5} }.
 *
 * While active is 0.5 or above the speed falls from 1 to 0 over stop_time; under 0.5 it rises
 * back over start_time. curve shapes both ramps: the speed is 1 - p^(4^curve) at a phase p.
 *
 * Under a tenth of the speed the output fades out. At full speed the input passes unchanged.
 * Every tape stop has its own memory of BOX_AUDIO_TAPE_STOP_MAX seconds per channel.
 */
ECS_STRUCT(TapeStop, {
    float active;          /**< 0 to 1, default 0: 0.5 and above stops, under it starts */
    float stop_time;       /**< Seconds from full speed to the halt, 0.01 to 4, default 0.5 */
    float start_time;      /**< Seconds from the halt to full speed, 0.01 to 4, default 0.2 */
    float curve;           /**< Shape of the ramps, -1 to 1, default 0 (linear) */
});

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
