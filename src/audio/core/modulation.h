/**
 * @file audio/core/modulation.h
 * @brief Commands of the audio core for the schedule, the beat clock, modulators and macros.
 */

#ifndef BOX_AUDIO_CORE_MODULATION_H
#define BOX_AUDIO_CORE_MODULATION_H

#include "effect_params.h"

/** Drop scheduled commands of one target at once.
 *
 * Drops the BUS_SET commands of a bus and a parameter, the EFFECT_PARAMS commands of a slot,
 * the VOICE_RAMP commands of a voice handle, the MACRO commands of a macro, or the BEAT
 * commands from a frame on. Automation cancels what it scheduled ahead with it */
typedef struct box_audio_cmd_cancel_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_CANCEL */
    uint32_t type;                  /**< BOX_AUDIO_CMD_BUS_SET, BOX_AUDIO_CMD_EFFECT_PARAMS,
                                     *   BOX_AUDIO_CMD_VOICE_RAMP, BOX_AUDIO_CMD_MACRO or
                                     *   BOX_AUDIO_CMD_BEAT */
    uint32_t target;                /**< Index of the bus, slot of the effect or of the macro,
                                     *   or handle of the voice; of a BEAT, the low word of
                                     *   the first frame whose BEAT commands are dropped */
    uint32_t param;                 /**< The box_audio_param_t of a BUS_SET, unused for an
                                     *   effect and a voice; of a BEAT, the high word of the
                                     *   frame */
} box_audio_cmd_cancel_t;

/** Set the beat clock of the core at a frame: the beat of the Transport at that frame and the
 * tempo from there on.
 *
 * The core derives the beat of a later frame linearly from the last one that applied. The
 * transport sends one at a start, a seek, a stop, a tempo change and each loop wrap. Before
 * the first one the clock stands at beat 0 at frame 0 at BOX_AUDIO_SYNTH_TEMPO.
 *
 * The synced modulators and anything else that must stay phase locked to the Transport read
 * the clock, through box_audio_core_beat */
typedef struct box_audio_cmd_beat_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_BEAT */
    uint32_t when_lo;               /**< Low word of the frame of the beat, 0 for now */
    uint32_t when_hi;               /**< High word of the frame */
    int32_t whole;                  /**< Whole beats at the frame, the floor of the beat */
    float fraction;                 /**< Fraction of a beat at the frame, 0..1 */
    float tempo;                    /**< Beats per second from the frame on, 0 (or a value
                                     *   that is not positive) for BOX_AUDIO_SYNTH_TEMPO, the
                                     *   120 bpm of a stopped or missing Transport */
} box_audio_cmd_beat_t;

/** Modulators of the core */
#define BOX_AUDIO_MOD_MAX 64

/** Modulators the module lets move one parameter */
#define BOX_AUDIO_MOD_PER_TARGET 4

/** Values of a step modulator */
#define BOX_AUDIO_MOD_STEP_MAX 16

/** Frames of the control block of the modulators: they compute their value for the end of
 * each block of the core clock, which ramps the parameter across it */
#define BOX_AUDIO_MOD_CONTROL 32

/** Fewest frames of the ramp that follows an edge of a hard wave or a step */
#define BOX_AUDIO_MOD_EDGE 16

/** Seconds a modulator fades in over when it starts, and out over when it is released */
#define BOX_AUDIO_MOD_FADE 0.05f

/** Level in dB a follower maps to 0; 0 dB maps to 1 */
#define BOX_AUDIO_MOD_FLOOR_DB (-60.0f)

/** Shortest and longest attack and release of a follower in seconds */
#define BOX_AUDIO_MOD_FOLLOW_MIN 0.0005f
#define BOX_AUDIO_MOD_FOLLOW_MAX 5.0f

/** What a modulator moves */
typedef enum box_audio_mod_target_t {
    BOX_AUDIO_MOD_TARGET_NONE = 0,  /**< Nothing: releases the modulator */
    BOX_AUDIO_MOD_TARGET_BUS = 1,   /**< A parameter of a bus */
    BOX_AUDIO_MOD_TARGET_EFFECT = 2, /**< A parameter of an effect slot */
    BOX_AUDIO_MOD_TARGET_MACRO = 3  /**< The value of a macro, index is its slot: the modulator
                                     *   adds to it before its maps shape it, linearly */
} box_audio_mod_target_t;

/** The source of a modulator */
typedef enum box_audio_mod_source_t {
    BOX_AUDIO_MOD_LFO = 0,          /**< A wave, -1..1, or 0..1 when unipolar */
    BOX_AUDIO_MOD_STEPS = 1,        /**< A sequence of values, -1..1 */
    BOX_AUDIO_MOD_FOLLOWER = 2      /**< The level of a bus, 0..1 */
} box_audio_mod_source_t;

/** Waves of an LFO modulator, at phase 0: the sine and the triangle at 0 rising, the square
 * at its top, the saws at their start */
typedef enum box_audio_mod_wave_t {
    BOX_AUDIO_MOD_SINE = 0,         /**< Sine */
    BOX_AUDIO_MOD_TRIANGLE = 1,     /**< Triangle */
    BOX_AUDIO_MOD_SQUARE = 2,       /**< Square, 1 the first half of a cycle, -1 the second */
    BOX_AUDIO_MOD_SAW_UP = 3,       /**< Rising saw, -1 to 1 */
    BOX_AUDIO_MOD_SAW_DOWN = 4,     /**< Falling saw, 1 to -1 */
    BOX_AUDIO_MOD_RANDOM = 5        /**< Sample and hold: a random value per cycle, the same
                                     *   for the same cycle of the beat clock */
} box_audio_mod_wave_t;

/** How the depth of a modulator applies */
typedef enum box_audio_mod_scale_t {
    BOX_AUDIO_MOD_LINEAR = 0,       /**< The parameter plus depth times the source */
    BOX_AUDIO_MOD_OCTAVES = 1       /**< The parameter times 2^(depth times the source), for a
                                     *   frequency; a parameter that is not positive is left */
} box_audio_mod_scale_t;

/** Put a modulator into a slot, change it or release it.
 *
 * A modulator moves a parameter of a bus, of an effect slot or the value of a macro, once per
 * BOX_AUDIO_MOD_CONTROL frames. Its value adds to the value from the commands or the maps,
 * clamped to the range of the parameter; the modulators of one parameter add up.
 *
 * It fades in and out over BOX_AUDIO_MOD_FADE, so a start, a release (target NONE) or a move
 * to another target does not click. A command for the same target keeps its phase locked */
typedef struct box_audio_cmd_modulator_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_MODULATOR */
    uint32_t slot;                  /**< Slot of the modulator, below BOX_AUDIO_MOD_MAX */
    uint32_t target;                /**< A box_audio_mod_target_t */
    uint32_t index;                 /**< Bus index or effect slot */
    uint32_t param;                 /**< box_audio_param_t of a bus, index of a param of an
                                     *   effect, below BOX_AUDIO_EFFECT_PARAMS */
    uint32_t kind;                  /**< box_audio_effect_kind_t the effect slot must hold,
                                     *   else the modulator waits */
    uint32_t source;                /**< A box_audio_mod_source_t */
    uint32_t scale;                 /**< A box_audio_mod_scale_t */
    uint32_t wave;                  /**< A box_audio_mod_wave_t of an LFO */
    uint32_t unipolar;              /**< Nonzero: an LFO moves 0..1 instead of -1..1 */
    uint32_t key;                   /**< Bus a follower measures, before its inserts and
                                     *   fader, BOX_AUDIO_BUS_NONE for none */
    uint32_t step_count;            /**< Steps that play, 1..BOX_AUDIO_MOD_STEP_MAX */
    float depth;                    /**< Swing of the parameter either way at a source of 1,
                                     *   in its units, or in octaves */
    float rate;                     /**< Hz of an LFO, steps per second of a step modulator,
                                     *   when sync is not positive */
    float sync;                     /**< Beats of a cycle of an LFO or a step, 0 for rate:
                                     *   the phase at a frame is phase + fract(beat / sync),
                                     *   at the beat of the beat clock at the frame */
    float phase;                    /**< Phase at the start in turns, 0..1, of a cycle of an
                                     *   LFO, of the whole sequence of a step modulator */
    float smooth;                   /**< Share of a step a step modulator glides from the
                                     *   value of the step before over, 0 hard .. 1 */
    float attack;                   /**< Seconds of the rise of a follower */
    float release;                  /**< Seconds of the fall of a follower */
    float steps[BOX_AUDIO_MOD_STEP_MAX]; /**< Values of a step modulator, -1..1 */
} box_audio_cmd_modulator_t;

/** Macros of the core */
#define BOX_AUDIO_MACRO_MAX 32

/** Maps the module lets one macro have */
#define BOX_AUDIO_MACRO_MAP_EACH 8

/** Map slots of the core, shared by every macro */
#define BOX_AUDIO_MACRO_MAP_MAX (BOX_AUDIO_MACRO_MAX * BOX_AUDIO_MACRO_MAP_EACH)

/** Bits a curve of 1 bends a map by: shape(x) = (2^(k x) - 1) / (2^k - 1) with k this times
 * the curve, so at curve 1 the middle of a macro gives 7 / 63 of the range of a map */
#define BOX_AUDIO_MACRO_CURVE 6.0f

/** Set the value of a macro at a frame: one value of 0..1 that its maps turn into the
 * parameters of buses and effect slots (box_audio_cmd_macro_map_t).
 *
 * Its modulators add to it, clamped to 0..1. The parameters follow on exactly its frame, as a
 * MACRO command brings the update of the modulators forward. Every macro starts at 0 */
typedef struct box_audio_cmd_macro_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_MACRO */
    uint32_t when_lo;               /**< Low word of the frame it applies at, 0 for now */
    uint32_t when_hi;               /**< High word of the frame */
    uint32_t macro;                 /**< Slot of the macro, below BOX_AUDIO_MACRO_MAX */
    float value;                    /**< The value, clamped to 0..1, NaN for 0 */
} box_audio_cmd_macro_t;

/** Put a map of a macro into a slot, change it or release it.
 *
 * A map sets a parameter of a bus or an effect slot from the value of its macro through
 * box_audio_macro_map_value, replacing the value of the commands; modulators add to it after.
 * The maps update with the modulators.
 *
 * A map fades in and out over BOX_AUDIO_MOD_FADE, so a start, a release (target NONE) or a move
 * does not click. A command with the same target and macro changes its range and curve at once */
typedef struct box_audio_cmd_macro_map_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_MACRO_MAP */
    uint32_t slot;                  /**< Slot of the map, below BOX_AUDIO_MACRO_MAP_MAX */
    uint32_t macro;                 /**< Slot of its macro, below BOX_AUDIO_MACRO_MAX */
    uint32_t target;                /**< BOX_AUDIO_MOD_TARGET_BUS, BOX_AUDIO_MOD_TARGET_EFFECT
                                     *   or BOX_AUDIO_MOD_TARGET_NONE to release it */
    uint32_t index;                 /**< Bus index or effect slot */
    uint32_t param;                 /**< box_audio_param_t of a bus, index of a param of an
                                     *   effect, below BOX_AUDIO_EFFECT_PARAMS */
    uint32_t kind;                  /**< box_audio_effect_kind_t the effect slot must hold,
                                     *   else the map waits */
    uint32_t scale;                 /**< BOX_AUDIO_MOD_LINEAR, or BOX_AUDIO_MOD_OCTAVES for a
                                     *   frequency of an effect, with min and max positive */
    float min;                      /**< Value at a macro of 0, in the units of the parameter */
    float max;                      /**< Value at a macro of 1; below min inverts the map */
    float curve;                    /**< -1 logarithmic .. 0 linear .. 1 exponential */
} box_audio_cmd_macro_map_t;

/** Shape the value of a macro into the value of a map. Does not allocate.
 *
 * @param value The value of the macro, clamped to 0..1, NaN for 0
 * @param min Value of the map at 0
 * @param max Value of the map at 1
 * @param curve -1..1, clamped: shape(x) = (2^(k x) - 1) / (2^k - 1), k = curve times
 *        BOX_AUDIO_MACRO_CURVE, x at 0
 * @param scale BOX_AUDIO_MOD_LINEAR: min + (max - min) * shape(x); BOX_AUDIO_MOD_OCTAVES:
 *        min * (max / min)^shape(x), linear when min or max is not positive
 * @return The value of the map
 */
float box_audio_macro_map_value(
    float value,
    float min,
    float max,
    float curve,
    uint32_t scale);

#endif
