/**
 * @file audio/core/commands.h
 * @brief Limits of the audio core and its commands for voices, buses, sounds and streams.
 */

#ifndef BOX_AUDIO_CORE_COMMANDS_H
#define BOX_AUDIO_CORE_COMMANDS_H

#include <stdint.h>
#include <stdbool.h>

/** Frames the web renders per call, the render quantum of an AudioWorklet */
#define BOX_AUDIO_CORE_QUANTUM 128

/** Frames the core mixes at once; a render of more frames runs in blocks of this size */
#define BOX_AUDIO_CORE_BLOCK 128

/** Seconds a change of a volume, pan, send level or mute takes, so changes do not click */
#define BOX_AUDIO_CORE_RAMP 0.005f

/** Seconds a stolen voice takes to fade out */
#define BOX_AUDIO_CORE_STEAL_FADE 0.002f

/** Voices that play at once, the voice cap */
#define BOX_AUDIO_VOICE_MAX 64

/** Extra voice slots for voices that fade out after a stop or a steal */
#define BOX_AUDIO_VOICE_TAIL_MAX 16

/** Buses of a core; bus 0 is the master. A bus that is not in the order costs nothing */
#define BOX_AUDIO_BUS_MAX 64

/** Sends of a bus */
#ifndef BOX_AUDIO_SENDS
#define BOX_AUDIO_SENDS 4
#endif

/** Sounds a core knows at once; a sound id is below this */
#define BOX_AUDIO_SOUND_MAX 1024

/** Events a core holds until read_events. The last slot is kept for BOX_AUDIO_EVENT_LOST:
 * newer events are lost when the others are full, and that event counts them.
 * BOX_AUDIO_EVENT_SOUND_FREE is never lost; it waits in the core until there is room */
#define BOX_AUDIO_EVENT_MAX 256

/** Commands with a when in the future that the core holds until their frame. A command that
 * finds the schedule full applies at once and reports BOX_AUDIO_EVENT_LATE */
#define BOX_AUDIO_SCHEDULE_MAX 4096

/** Largest volume of a voice or a bus */
#define BOX_AUDIO_VOLUME_MAX 4.0f

/** Smallest and largest pitch ratio of a voice */
#define BOX_AUDIO_PITCH_MIN (1.0f / 64.0f)
#define BOX_AUDIO_PITCH_MAX 16.0f

/** Level over which the limiter of the master reduces the gain; quieter signals pass as is */
#define BOX_AUDIO_LIMITER_THRESHOLD 0.95f

/** Seconds the limiter takes to return from full reduction to a gain of 1 */
#define BOX_AUDIO_LIMITER_RELEASE 0.1f

/** No bus, for the output of the master and a bus without a send */
#define BOX_AUDIO_BUS_NONE 0xFF

/** Streams a core reads at once: one that plays and one that fades out after a change */
#define BOX_AUDIO_STREAM_MAX 2

/** Chunks the ring of a stream holds */
#define BOX_AUDIO_STREAM_CHUNKS 32

/** Frames of a chunk at most */
#define BOX_AUDIO_STREAM_CHUNK 4096

/** Seconds of the fade of a stream that runs out of chunks, and of its recovery */
#define BOX_AUDIO_STREAM_FADE 0.005f
/** Effect instances a core holds; an effect is addressed by a slot below this */
#ifndef BOX_AUDIO_EFFECT_MAX
#define BOX_AUDIO_EFFECT_MAX 64
#endif

/** Insert slots of a bus; its effects run in the order of their index */
#define BOX_AUDIO_EFFECT_SLOTS 8

/** Parameters of an effect command, read by each kind in its own way.
 *
 * 32 hold the 16 steps of a pattern gate and its other parameters as plain floats, so
 * automation and modulators reach each step. An EFFECT_PARAMS command is 148 bytes, the
 * largest command of the schedule */
#define BOX_AUDIO_EFFECT_PARAMS 32

/** Seconds of the fade of the wet signal around a change of the kind, bus or index of an
 * effect, out and in, and of the tail of an effect taken off */
#define BOX_AUDIO_EFFECT_FADE 0.1f

/** Delay lines of the feedback delay network of a reverb */
#define BOX_AUDIO_REVERB_LINES 8

/** Longest predelay of a reverb in seconds */
#define BOX_AUDIO_REVERB_PREDELAY_MAX 0.5f

/** Shortest and longest decay of a reverb in seconds, the time until -60 dB */
#define BOX_AUDIO_REVERB_DECAY_MIN 0.05f
#define BOX_AUDIO_REVERB_DECAY_MAX 60.0f

/** Seconds of the fade of the wet signal around a change of size or predelay, out and in */
#define BOX_AUDIO_REVERB_FADE BOX_AUDIO_EFFECT_FADE

/** Oscillators of a synth patch, each with an envelope, mixed into one filter and an amp */
#define BOX_AUDIO_SYNTH_LAYERS 8

/** Synth patches a core holds at once */
#define BOX_AUDIO_SYNTH_MAX 256

/** Longest pass of a synth envelope in seconds; a longer one loses its hold first */
#define BOX_AUDIO_SYNTH_LENGTH_MAX 60.0f

/** Shortest attack, and shortest decay and release that change the level, in seconds, so an
 * envelope never jumps */
#define BOX_AUDIO_SYNTH_EDGE 0.0005f

/** Largest curve of a segment of a synth envelope, either way; a larger one is clamped */
#define BOX_AUDIO_SYNTH_CURVE_MAX 16.0f

/** Smallest curve of a segment of a synth envelope that bends it; a smaller one is linear */
#define BOX_AUDIO_SYNTH_CURVE_MIN 0.001f

/** Highest frequency of an oscillator in Hz, the top of the audible range */
#define BOX_AUDIO_SYNTH_FREQUENCY_MAX 20000.0f

/** Largest index of phase modulation in radians, before the limit against the frequency */
#define BOX_AUDIO_SYNTH_INDEX_MAX 32.0f

/** Largest feedback of an oscillator onto its own phase in radians, under the point where the
 * tone turns to noise */
#define BOX_AUDIO_SYNTH_FEEDBACK_MAX 1.5f

/** Largest amount of the filter envelope in octaves, up or down */
#define BOX_AUDIO_SYNTH_OCTAVES_MAX 10.0f

/** Longest glide of a synth patch in seconds */
#define BOX_AUDIO_SYNTH_GLIDE_MAX 10.0f

/** Most unison voices of a synth layer */
#define BOX_AUDIO_SYNTH_UNISON_MAX 7

/** Largest analog drift of the pitch of a synth sound in cents */
#define BOX_AUDIO_SYNTH_DRIFT_MAX 50.0f

/** Largest detune spread of the unison voices of a synth layer in cents, a semitone */
#define BOX_AUDIO_SYNTH_DETUNE_MAX 100.0f
/** Lowest frequency of a pluck string in Hz; a lower one is clamped. At rates above 81.8 kHz the
 * delay line of BOX_AUDIO_SYNTH_STRING_FRAMES limits it further, to the rate over 2046 */
#define BOX_AUDIO_SYNTH_PLUCK_MIN 40.0f

/** Strings the voices of a core play at once, each with its delay lines, shared by pluck and
 * bowed layers. A BOX_AUDIO_SYNTH_PLUCK or BOX_AUDIO_SYNTH_BOWED layer takes one for each of
 * its unison voices; a play that does not find a string for each is ignored */
#define BOX_AUDIO_SYNTH_STRINGS 32

/** Frames of the delay line of a pluck string, a power of 2: a period of up to 2046 frames,
 * BOX_AUDIO_SYNTH_PLUCK_MIN up to a rate of 81.8 kHz */
#define BOX_AUDIO_SYNTH_STRING_FRAMES 2048

/** Smallest, largest and default bow position of a BOX_AUDIO_SYNTH_BOWED layer, the distance
 * of the bow from the bridge as a share of the string */
#define BOX_AUDIO_SYNTH_BOW_POSITION_MIN 0.02f
#define BOX_AUDIO_SYNTH_BOW_POSITION_MAX 0.5f
#define BOX_AUDIO_SYNTH_BOW_POSITION_DEFAULT 0.12f

/** Shortest and longest decay of a pluck string in seconds, the time until -60 dB */
#define BOX_AUDIO_SYNTH_PLUCK_DECAY_MIN 0.01f
#define BOX_AUDIO_SYNTH_PLUCK_DECAY_MAX 60.0f

/** Lowest frequency of a comb filter in Hz, its note times its tune; a lower one is clamped.
 * At rates above 81.8 kHz the delay line of BOX_AUDIO_SYNTH_COMB_FRAMES limits it further, to
 * the rate over 2045 */
#define BOX_AUDIO_SYNTH_COMB_MIN 40.0f

/** Delay lines of comb filters of a core: a voice with a BOX_AUDIO_SYNTH_COMB filter takes one
 * per channel, two with unison and width; a play that does not find them free is ignored */
#define BOX_AUDIO_SYNTH_COMBS 32

/** Frames of the delay line of a comb filter, a power of 2: a delay of up to 2045 frames,
 * BOX_AUDIO_SYNTH_COMB_MIN up to a rate of 81.8 kHz */
#define BOX_AUDIO_SYNTH_COMB_FRAMES 2048

/** Largest feedback of a comb filter, either way */
#define BOX_AUDIO_SYNTH_COMB_FEEDBACK_MAX 0.98f

/** Smallest and largest tune of a comb filter, a ratio to the frequency of its note */
#define BOX_AUDIO_SYNTH_TUNE_MIN 0.125f
#define BOX_AUDIO_SYNTH_TUNE_MAX 8.0f
/** LFOs of a synth patch */
#define BOX_AUDIO_SYNTH_LFOS 2

/** Highest rate of a synth LFO in Hz */
#define BOX_AUDIO_SYNTH_LFO_RATE_MAX 100.0f

/** Longest cycle of a synth LFO synced to the tempo, in beats */
#define BOX_AUDIO_SYNTH_LFO_SYNC_MAX 256.0f

/** Tempo of a play without one, in beats per second: 120 quarter notes per minute */
#define BOX_AUDIO_SYNTH_TEMPO 2.0f

/** Slowest and fastest tempo of a play in beats per second, those of the timeline, 20 and 999
 * quarter notes per minute */
#define BOX_AUDIO_SYNTH_TEMPO_MIN (20.0f / 60.0f)
#define BOX_AUDIO_SYNTH_TEMPO_MAX (999.0f / 60.0f)

/** Beats the beat of a play wraps at, so that a float stays precise: a multiple of every
 * synced LFO cycle of whole, dotted and triplet notes up to 256 beats */
#define BOX_AUDIO_BEAT_WRAP 768.0

/** Largest depth of the pitch of a synth LFO in cents, up or down */
#define BOX_AUDIO_SYNTH_CENTS_MAX 2400.0f

/** Largest ratio of hard sync of a synth layer to its frequency */
#define BOX_AUDIO_SYNTH_SYNC_MAX 16.0f

/** Largest depth of the ratio of hard sync of a synth LFO in semitones, up or down */
#define BOX_AUDIO_SYNTH_SYNC_SEMITONES 48.0f

/** Largest amount of a synth pitch envelope in semitones, up or down, of the pitch at its start,
 * of its release and of the ratio of hard sync */
#define BOX_AUDIO_SYNTH_PITCH_SEMITONES 48.0f

/** Narrowest pulse width of a square, as a share of its period; the widest is 1 minus it */
#define BOX_AUDIO_SYNTH_PULSE_MIN 0.01f

/** Default pulse width of a square, the symmetric square */
#define BOX_AUDIO_SYNTH_PULSE_DEFAULT 0.5f

/** Gain in dB before the clipper of the drive of a synth voice at a drive of 0 and of 1 */
#define BOX_AUDIO_SYNTH_DRIVE_DB_MIN (-6.0f)
#define BOX_AUDIO_SYNTH_DRIVE_DB_MAX 24.0f

/** Offset the drive of a synth voice at an asymmetry of 1 adds to its driven input */
#define BOX_AUDIO_SYNTH_DRIVE_BIAS 1.0f

/** Peak of the sine whose RMS the drive of a synth voice keeps at every drive: a layer at
 * level 1 */
#define BOX_AUDIO_SYNTH_DRIVE_REFERENCE 1.0f

/** Cutoff of the DC blocker after the drive of a synth voice with asymmetry in Hz */
#define BOX_AUDIO_SYNTH_DRIVE_DC 5.0f

/** Highest vowel of a formant filter: 0 a, 1 e, 2 i, 3 o, 4 u; also the largest depth in
 * vowels of an LFO or the filter envelope on it */
#define BOX_AUDIO_SYNTH_VOWEL_MAX 4.0f

/** Routes of the modulation matrix of a synth patch */
#define BOX_AUDIO_SYNTH_ROUTES 8

/** Largest amount of a route to the level of a synth layer, a share of its level either way */
#define BOX_AUDIO_SYNTH_ROUTE_LEVEL_MAX 4.0f

/** Most frames of a wavetable; a table takes a sound id of the core, so a core holds at most
 * BOX_AUDIO_SOUND_MAX - 1 sounds, tables and synth patches together */
#define BOX_AUDIO_TABLE_FRAMES_MAX 256

/** Samples of a frame of a wavetable at mip level 0 */
#define BOX_AUDIO_TABLE_LENGTH 2048

/** Shortest frame of a mip level of a wavetable: the top levels, of few harmonics, keep this
 * many samples, so their interpolation stays clean */
#define BOX_AUDIO_TABLE_LENGTH_MIN 32

/** Mip levels of a wavetable, one per octave of the turns per frame of the oscillator */
#define BOX_AUDIO_TABLE_LEVELS 10

/** Harmonics a frame of a wavetable holds at level 0; level k holds this over 2^k.
 *
 * Level k serves an oscillator of at most 2^k / (2 BOX_AUDIO_TABLE_HARMONICS) turns per frame,
 * so its highest harmonic stays under Nyquist. Over the upper half of the octave of a level the
 * oscillator crossfades into the next level, so a sweep has no step at the boundaries */
#define BOX_AUDIO_TABLE_HARMONICS 512

/** Samples of a frame of a wavetable at a mip level k, 0..BOX_AUDIO_TABLE_LEVELS - 1, a power
 * of 2 of at least four samples per period of the highest harmonic of the level */
#define BOX_AUDIO_TABLE_LEVEL_LENGTH(k) \
    ((BOX_AUDIO_TABLE_LENGTH >> (k)) > BOX_AUDIO_TABLE_LENGTH_MIN ? \
    (BOX_AUDIO_TABLE_LENGTH >> (k)) : BOX_AUDIO_TABLE_LENGTH_MIN)

/** Samples of one frame of a wavetable over all its mip levels */
#define BOX_AUDIO_TABLE_FRAME_FLOATS 4160

/** Floats of the header of a wavetable before its levels */
#define BOX_AUDIO_TABLE_HEADER 4

/** First float of the header of a wavetable, which tells it from the frames of a sound */
#define BOX_AUDIO_TABLE_MAGIC 6371329.0f

/** Floats of a wavetable of a number of frames: the header, then for each mip level from 0
 * its frames, frame after frame, each BOX_AUDIO_TABLE_LEVEL_LENGTH(level) samples of one
 * period. A table of 64 frames takes 1064976 bytes, about 1 MB */
#define BOX_AUDIO_TABLE_FLOATS(frames) \
    (BOX_AUDIO_TABLE_HEADER + (frames) * BOX_AUDIO_TABLE_FRAME_FLOATS)

/** Commands of the byte stream. Each starts with a box_audio_cmd_header_t */
typedef enum box_audio_cmd_type_t {
    BOX_AUDIO_CMD_PLAY = 1,          /**< box_audio_cmd_play_t */
    BOX_AUDIO_CMD_STOP = 2,          /**< box_audio_cmd_stop_t */
    BOX_AUDIO_CMD_VOICE_SET = 3,     /**< box_audio_cmd_set_t on a voice */
    BOX_AUDIO_CMD_BUS_SET = 4,       /**< box_audio_cmd_set_t on a bus */
    BOX_AUDIO_CMD_ORDER = 5,         /**< box_audio_cmd_order_t and its routes */
    BOX_AUDIO_CMD_SOUND_RELEASE = 6, /**< box_audio_cmd_release_t */
    BOX_AUDIO_CMD_SYNTH = 8,         /**< box_audio_cmd_synth_t */
    BOX_AUDIO_CMD_STREAM = 9,        /**< box_audio_cmd_stream_t */
    BOX_AUDIO_CMD_RELEASE = 10,      /**< box_audio_cmd_voice_release_t */
    BOX_AUDIO_CMD_EFFECT = 11,       /**< box_audio_cmd_effect_t; 7, the reverb command of
                                      *   before, is skipped as an unknown type */
    BOX_AUDIO_CMD_LEGATO = 12,       /**< box_audio_cmd_legato_t */
    BOX_AUDIO_CMD_EFFECT_PARAMS = 13, /**< box_audio_cmd_effect_params_t */
    BOX_AUDIO_CMD_CANCEL = 14,       /**< box_audio_cmd_cancel_t */
    BOX_AUDIO_CMD_VOICE_RAMP = 15,   /**< box_audio_cmd_ramp_t */
    BOX_AUDIO_CMD_BEAT = 16,         /**< box_audio_cmd_beat_t */
    BOX_AUDIO_CMD_MODULATOR = 17,    /**< box_audio_cmd_modulator_t */
    BOX_AUDIO_CMD_MACRO = 18,        /**< box_audio_cmd_macro_t */
    BOX_AUDIO_CMD_MACRO_MAP = 19     /**< box_audio_cmd_macro_map_t */
} box_audio_cmd_type_t;

/** Parameters of BOX_AUDIO_CMD_VOICE_SET and BOX_AUDIO_CMD_BUS_SET */
typedef enum box_audio_param_t {
    BOX_AUDIO_PARAM_VOLUME = 0,  /**< Volume, 0..BOX_AUDIO_VOLUME_MAX, ramped */
    BOX_AUDIO_PARAM_PAN = 1,     /**< Pan, -1 left .. 1 right, equal power, ramped */
    BOX_AUDIO_PARAM_PITCH = 2,   /**< Pitch ratio of a voice */
    BOX_AUDIO_PARAM_BUS = 3,     /**< Bus a voice plays into */
    BOX_AUDIO_PARAM_MUTE = 4,    /**< Mute of a bus, nonzero mutes, ramped */
    BOX_AUDIO_PARAM_SEND = 5,    /**< Level of send 0 of a bus, 0..BOX_AUDIO_VOLUME_MAX, ramped */
    BOX_AUDIO_PARAM_POSITION = 6, /**< Read position of a voice in frames of its sound, wrapped
                                  *   into a looping sound, at once */
    BOX_AUDIO_PARAM_WIDTH = 7,   /**< Stereo width of a bus, 0 mono .. 1 as is .. 2 double
                                  *   side, a mid/side gain before the volume and pan, ramped */
    BOX_AUDIO_PARAM_SEND_1 = 8,  /**< Level of send 1 of a bus */
    BOX_AUDIO_PARAM_SEND_2 = 9,  /**< Level of send 2 of a bus */
    BOX_AUDIO_PARAM_SEND_3 = 10, /**< Level of send 3 of a bus */
    BOX_AUDIO_PARAM_BEND = 11,   /**< Pitch bend of a voice in semitones, up to
                                  *   BOX_AUDIO_BEND_MAX either way, default 0: multiplies the
                                  *   pitch, glide included, by 2^(bend / 12) */
    BOX_AUDIO_PARAM_MOD = 12,    /**< Mod of a synth voice, 0..1, default 1: scales the depth
                                  *   of the pitch of its LFOs, its vibrato */
    BOX_AUDIO_PARAM_PRESSURE = 13, /**< Pressure of a synth voice, 0..1, default 0: moves its
                                  *   cutoff up by pressure * BOX_AUDIO_SYNTH_PRESSURE_OCTAVES */
    BOX_AUDIO_PARAM_CUTOFF = 14, /**< Cutoff in Hz of the 12 dB per octave low-pass of a voice,
                                  *   after its source, ramped over BOX_AUDIO_CUTOFF_RAMP; the
                                  *   first cutoff of a voice that has not rendered applies at once.
                                  *
                                  *   0 (the default), a negative value or one of at least
                                  *   BOX_AUDIO_CUTOFF_OPEN opens the filter; others are clamped
                                  *   to BOX_AUDIO_CUTOFF_MIN..0.45 of the rate */
    BOX_AUDIO_PARAM_LOUDNESS = 15 /**< Loudness meter of a bus, nonzero turns it on: the
                                  *   K-weighting of BS.1770 into box_audio_meter_t.loudness.
                                  *   Off by default, as it costs two biquads per channel */
} box_audio_param_t;

/** A cutoff of BOX_AUDIO_PARAM_CUTOFF of at least this many Hz opens the filter of a voice */
#define BOX_AUDIO_CUTOFF_OPEN 20000.0f

/** Lowest cutoff of BOX_AUDIO_PARAM_CUTOFF in Hz */
#define BOX_AUDIO_CUTOFF_MIN 10.0f

/** Seconds a cutoff of BOX_AUDIO_PARAM_CUTOFF ramps over, longer than the ramp of the gains:
 * a jump of octaves sweeps the phase of what passes, which clicks when it is too fast */
#define BOX_AUDIO_CUTOFF_RAMP 0.05f

/** Largest pitch bend of a voice in semitones, either way */
#define BOX_AUDIO_BEND_MAX 48.0f

/** Octaves the cutoff of a synth voice moves up by at pressure 1 */
#define BOX_AUDIO_SYNTH_PRESSURE_OCTAVES 2.0f

/** Flags of BOX_AUDIO_CMD_PLAY */
#define BOX_AUDIO_PLAY_LOOP (1u << 0)

/** A synth voice sustains at the sustain level of its envelopes until a BOX_AUDIO_CMD_RELEASE,
 * the hold of its envelopes ignored; an envelope with a sustain of 0 still ends after its
 * decay. Sample voices ignore it */
#define BOX_AUDIO_PLAY_GATE (1u << 1)

/** A sample voice starts at its gain, without the ramp, so a sampled hit keeps its transient.
 * Synth voices start at their gain anyway */
#define BOX_AUDIO_PLAY_ATTACK (1u << 2)

/** Highest priority of a voice; larger values are clamped.
 *
 * A play, a legato or a stream that finds the voice cap full steals the quietest playing voice
 * of at most its priority. When every playing voice ranks higher it is refused, reported as
 * BOX_AUDIO_EVENT_VOICE_END and counted by box_audio_core_refused */
#define BOX_AUDIO_PRIORITY_MAX 255u

/** Shortest and longest fade of a BOX_AUDIO_CMD_RELEASE of a sample voice in seconds */
#define BOX_AUDIO_RELEASE_FADE_MIN 0.005f
#define BOX_AUDIO_RELEASE_FADE_MAX 10.0f

/** Header of every command: its type and its size in bytes, header included.
 *
 * PLAY, STOP, VOICE_SET, BUS_SET, RELEASE, LEGATO, EFFECT_PARAMS, VOICE_RAMP, BEAT and MACRO
 * follow it with when_lo and when_hi, the 64 bit frame of the core clock at which the command
 * takes effect. A when of 0, or in the past, applies at the start of the next block.
 *
 * A later when waits in the schedule. Of the commands of one frame, ramps apply after the
 * others, so a ramp at the frame of a play applies to the voice it starts; releases apply last */
typedef struct box_audio_cmd_header_t {
    uint16_t type;  /**< A box_audio_cmd_type_t; an unknown type is skipped */
    uint16_t size;  /**< Bytes of the command, a multiple of 4, at least 4 */
} box_audio_cmd_header_t;

/** Start a voice that plays a sound */
typedef struct box_audio_cmd_play_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_PLAY */
    uint32_t when_lo;               /**< Low word of the frame it starts at, 0 for now */
    uint32_t when_hi;               /**< High word of the frame */
    uint32_t voice;                 /**< Handle the sender picks to address the voice */
    uint32_t sound;                 /**< Id of the sound */
    uint32_t bus;                   /**< Bus the voice plays into */
    float volume;                   /**< Volume, ramped from 0 so the start does not click */
    float pan;                      /**< Pan, -1..1 */
    float pitch;                    /**< Pitch ratio, 1 plays the sound as is */
    uint32_t flags;                 /**< BOX_AUDIO_PLAY_ flags */
    uint32_t priority;              /**< Priority of the voice, 0..BOX_AUDIO_PRIORITY_MAX:
                                     * at the voice cap the play steals only a voice of at
                                     * most this priority, else it is refused */
    float velocity;                 /**< Velocity of the note, 0..1, which a synth voice
                                     * keeps for the velocity fields of its patch; 1 for a
                                     * play that is not a note. Sample voices ignore it */
    float beat;                     /**< Beat of the Transport at its start frame, wrapped into
                                     * 0..BOX_AUDIO_BEAT_WRAP, which phase locks the synced
                                     * LFOs of a synth voice; sample voices ignore it */
    float tempo;                    /**< Beats per second of the Transport at that frame, at
                                     * which the synced LFOs of a synth voice run for its
                                     * whole life; 0 for BOX_AUDIO_SYNTH_TEMPO, 120 bpm */
} box_audio_cmd_play_t;

/** Play a note of a mono voice: retarget the synth voice of a handle that has not reached its
 * release to a synth sound without a restart of its envelopes and give it a new handle, or
 * else start a new voice with the new handle (docs/audio.md, Synth).
 *
 * With BOX_AUDIO_PLAY_GATE in its flags the voice it retargets sustains until a release,
 * without it the voice holds for the hold of its envelopes again */
typedef struct box_audio_cmd_legato_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_LEGATO */
    uint32_t when_lo;               /**< Low word of the frame of the note, 0 for now */
    uint32_t when_hi;               /**< High word of the frame */
    uint32_t voice;                 /**< New handle of the voice, picked by the sender */
    uint32_t from;                  /**< Handle of the voice the note follows */
    uint32_t sound;                 /**< Id of the sound */
    uint32_t bus;                   /**< Bus the voice plays into */
    float volume;                   /**< Volume */
    float pan;                      /**< Pan, -1..1 */
    float pitch;                    /**< Pitch ratio, 1 plays the sound as is */
    uint32_t flags;                 /**< BOX_AUDIO_PLAY_ flags of a new voice */
    uint32_t priority;              /**< Priority of the voice, as of a play */
    float velocity;                 /**< Velocity of the note, 0..1, of a new voice; a
                                     * retargeted voice keeps the velocity of the note that
                                     * started it, as its envelopes do not restart */
    float beat;                     /**< Beat of the note, as of a play, of a new voice; a
                                     * retargeted voice keeps its LFOs */
    float tempo;                    /**< Beats per second, as of a play, of a new voice */
} box_audio_cmd_legato_t;

/** Stop a voice: it fades out and ends. A stop that applies now, with a when of 0 or in the
 * past, also drops every command of the schedule for its handle, so a play that waits for its
 * frame never starts; a stop with a future when only stops */
typedef struct box_audio_cmd_stop_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_STOP */
    uint32_t when_lo;               /**< Low word of the frame its fade starts at, 0 for now */
    uint32_t when_hi;               /**< High word of the frame */
    uint32_t voice;                 /**< Handle of the voice */
} box_audio_cmd_stop_t;

/** End the note of a voice: a synth voice moves every envelope it has into its release from
 * its current level, a sample voice fades out linearly over fade and ends. A release of a voice
 * that ended, or of an unknown handle, is ignored */
typedef struct box_audio_cmd_voice_release_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_RELEASE */
    uint32_t when_lo;               /**< Low word of the frame of the release, 0 for now */
    uint32_t when_hi;               /**< High word of the frame */
    uint32_t voice;                 /**< Handle of the voice */
    float fade;                     /**< Seconds of the fade of a sample voice, clamped to
                                     * BOX_AUDIO_RELEASE_FADE_MIN..MAX; synth voices use the
                                     * release of their envelopes */
} box_audio_cmd_voice_release_t;

/** Set a parameter of a voice or a bus */
typedef struct box_audio_cmd_set_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_VOICE_SET or BOX_AUDIO_CMD_BUS_SET */
    uint32_t when_lo;               /**< Low word of the frame its ramp starts at, 0 for now */
    uint32_t when_hi;               /**< High word of the frame */
    uint32_t target;                /**< Handle of the voice or index of the bus */
    uint32_t param;                 /**< A box_audio_param_t */
    float value;                    /**< The new value */
} box_audio_cmd_set_t;

/** Move the bend, mod or pressure of a voice linearly to a value over a number of frames.
 *
 * A ramp replaces the one of its parameter that runs; the voice takes its value every
 * BOX_AUDIO_EXPRESS_FRAMES frames. A PLAY, or a LEGATO that retargets the voice, resets the
 * expression to its defaults */
typedef struct box_audio_cmd_ramp_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_VOICE_RAMP */
    uint32_t when_lo;               /**< Low word of the frame the ramp starts at, 0 for now */
    uint32_t when_hi;               /**< High word of the frame */
    uint32_t voice;                 /**< Handle of the voice */
    uint32_t param;                 /**< A box_audio_param_t */
    float value;                    /**< Value at the end of the ramp, clamped to the range
                                     *   of the parameter, NaN for its default */
    uint32_t frames;                /**< Frames of the ramp, 0 to jump */
} box_audio_cmd_ramp_t;

/** Frames of the blocks in which a voice with a ramp that runs takes the values of its
 * ramps, the control block of a synth voice */
#define BOX_AUDIO_EXPRESS_FRAMES 8

/** A bus in the processing order, where it outputs and where it sends.
 *
 * Send j of a bus has the level of BOX_AUDIO_PARAM_SEND (j 0) or BOX_AUDIO_PARAM_SEND_1 + j - 1.
 * A post-fader send taps the bus after its volume and pan; a pre-fader send taps it before them,
 * after its inserts and width, and follows its mute */
typedef struct box_audio_route_t {
    uint8_t bus;     /**< Index of the bus */
    uint8_t output;  /**< Bus the bus outputs to, BOX_AUDIO_BUS_NONE for the master */
    uint8_t sends[BOX_AUDIO_SENDS]; /**< Bus of each send, BOX_AUDIO_BUS_NONE for none */
    uint8_t pre_fader; /**< Bit j set: send j is pre-fader */
    uint8_t unused;  /**< Padding, 0 */
} box_audio_route_t;

/** Replace the processing order, followed by count box_audio_route_t.
 *
 * The master comes last and outputs and sends nowhere; every other bus outputs and sends only
 * to buses after it, so the routes have no cycle. A bus not in the order is silent.
 */
typedef struct box_audio_cmd_order_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_ORDER */
    uint32_t count;                 /**< Routes that follow, 1..BOX_AUDIO_BUS_MAX */
} box_audio_cmd_order_t;

/** Release a sound: once no voice plays it, BOX_AUDIO_EVENT_SOUND_FREE tells the owner */
typedef struct box_audio_cmd_release_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_SOUND_RELEASE */
    uint32_t sound;                 /**< Id of the sound */
} box_audio_cmd_release_t;

/** Open a stream and start a voice that reads it. The chunks of the stream come with
 * box_audio_core_stream_chunk; a stop fades the voice out and closes the stream */
typedef struct box_audio_cmd_stream_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_STREAM */
    uint32_t voice;                 /**< Handle the sender picks to address the voice */
    uint32_t stream;                /**< Index of the stream, below BOX_AUDIO_STREAM_MAX, closed */
    uint32_t bus;                   /**< Bus the voice plays into */
    float volume;                   /**< Volume, ramped from 0 so the start does not click */
    uint32_t channels;              /**< 1 or 2, interleaved in the chunks */
    float sample_rate;              /**< Frames per second of the chunks, the rate of the core
                                     * when not positive */
    uint32_t priority;              /**< Priority of the voice, as of a play */
} box_audio_cmd_stream_t;

/** What a stream does, for the side that owns its chunks */
typedef struct box_audio_stream_status_t {
    uint32_t voice;      /**< Handle of the voice that opened the stream last, 0 for none */
    int32_t open;        /**< 1 while that voice reads the stream; once 0 the core holds none of
                          * its chunks */
    int32_t ended;       /**< 1 when the voice read the last chunk to its end */
    uint32_t consumed;   /**< Chunks read to their end since the stream opened, oldest first;
                          * the core holds none of them any more */
    int32_t at;          /**< Frames read of the oldest chunk that is not consumed */
    int32_t underruns;   /**< Times the stream ran out of chunks since it opened */
    int32_t starved;     /**< 1 while it plays silence for want of chunks */
} box_audio_stream_status_t;

#endif
