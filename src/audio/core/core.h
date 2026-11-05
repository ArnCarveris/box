/**
 * @file audio/core/core.h
 * @brief The audio core: a mixer of voices and buses, driven by a byte stream of commands.
 *
 * Plain C without flecs, box or platform code, so the same sources build into the main module
 * and into the standalone wasm the web build runs in an AudioWorklet. Rendering does not
 * allocate, lock, log or wait. See docs/audio.md for the commands and the transports.
 */

#ifndef BOX_AUDIO_CORE_H
#define BOX_AUDIO_CORE_H

#include <stddef.h>
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
#define BOX_AUDIO_EFFECT_SLOTS 4

/** Parameters of an effect command, read by each kind in its own way */
#define BOX_AUDIO_EFFECT_PARAMS 16

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

/** Largest detune spread of the unison voices of a synth layer in cents, a semitone */
#define BOX_AUDIO_SYNTH_DETUNE_MAX 100.0f
/** Lowest frequency of a pluck string in Hz; a lower one is clamped. At rates above 81.8 kHz the
 * delay line of BOX_AUDIO_SYNTH_STRING_FRAMES limits it further, to the rate over 2046 */
#define BOX_AUDIO_SYNTH_PLUCK_MIN 40.0f

/** Pluck strings the voices of a core play at once, each with its delay line. A pluck layer
 * takes one for each of its unison voices; a play that does not find a string for each is
 * ignored */
#define BOX_AUDIO_SYNTH_STRINGS 32

/** Frames of the delay line of a pluck string, a power of 2: a period of up to 2046 frames,
 * BOX_AUDIO_SYNTH_PLUCK_MIN up to a rate of 81.8 kHz */
#define BOX_AUDIO_SYNTH_STRING_FRAMES 2048

/** Shortest and longest decay of a pluck string in seconds, the time until -60 dB */
#define BOX_AUDIO_SYNTH_PLUCK_DECAY_MIN 0.01f
#define BOX_AUDIO_SYNTH_PLUCK_DECAY_MAX 60.0f
/** LFOs of a synth patch */
#define BOX_AUDIO_SYNTH_LFOS 2

/** Highest rate of a synth LFO in Hz */
#define BOX_AUDIO_SYNTH_LFO_RATE_MAX 100.0f

/** Largest depth of the pitch of a synth LFO in cents, up or down */
#define BOX_AUDIO_SYNTH_CENTS_MAX 2400.0f

/** Largest ratio of hard sync of a synth layer to its frequency */
#define BOX_AUDIO_SYNTH_SYNC_MAX 16.0f

/** Largest depth of the ratio of hard sync of a synth LFO in semitones, up or down */
#define BOX_AUDIO_SYNTH_SYNC_SEMITONES 48.0f

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
    BOX_AUDIO_CMD_LEGATO = 12        /**< box_audio_cmd_legato_t */
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
    BOX_AUDIO_PARAM_SEND_3 = 10  /**< Level of send 3 of a bus */
} box_audio_param_t;

/** Flags of BOX_AUDIO_CMD_PLAY */
#define BOX_AUDIO_PLAY_LOOP (1u << 0)

/** A synth voice sustains at the sustain level of its envelopes until a BOX_AUDIO_CMD_RELEASE,
 * the hold of its envelopes ignored; an envelope with a sustain of 0 still ends after its
 * decay. Sample voices ignore it */
#define BOX_AUDIO_PLAY_GATE (1u << 1)

/** A sample voice starts at its gain, without the ramp, so a sampled hit keeps its transient.
 * Synth voices start at their gain anyway */
#define BOX_AUDIO_PLAY_ATTACK (1u << 2)

/** Highest priority of a voice. A play, a legato or a stream that finds the voice cap full
 * steals the quietest playing voice of at most its priority, the first of equals; when every
 * playing voice ranks higher it is refused: the core reports BOX_AUDIO_EVENT_VOICE_END for its
 * handle with the refusals so far as value, and box_audio_core_refused counts it. Larger
 * values are clamped */
#define BOX_AUDIO_PRIORITY_MAX 255u

/** Shortest and longest fade of a BOX_AUDIO_CMD_RELEASE of a sample voice in seconds */
#define BOX_AUDIO_RELEASE_FADE_MIN 0.005f
#define BOX_AUDIO_RELEASE_FADE_MAX 10.0f

/** Header of every command: its type and its size in bytes, header included.
 *
 * PLAY, STOP, VOICE_SET, BUS_SET, RELEASE and LEGATO follow it with when_lo and when_hi, the
 * frame of the core clock (box_audio_core_clock) at which the command takes effect, 64 bits in
 * two words so that every field stays 32 bits.
 *
 * A when of 0, or of a frame that was already rendered, applies at the start of the next
 * block; a later one waits in the schedule of the core for its frame. Of the commands of one
 * frame, the releases apply last */
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
/** Kinds of effects. A kind is one file of src/audio/core and one row of the table of
 * effect.c */
typedef enum box_audio_effect_kind_t {
    BOX_AUDIO_EFFECT_NONE = 0,    /**< No effect: takes an effect off, its tail fades out */
    BOX_AUDIO_EFFECT_REVERB = 1,  /**< A reverb, box_audio_reverb_params_t */
    BOX_AUDIO_EFFECT_EQ = 2,      /**< An equalizer, BOX_AUDIO_EQ_BANDS box_audio_eq_band_t */
    BOX_AUDIO_EFFECT_FILTER = 3,  /**< A filter, box_audio_filter_params_t */
    BOX_AUDIO_EFFECT_COMPRESSOR = 4, /**< A compressor, box_audio_compressor_params_t, keyed on
                                   *   the key bus of its command */
    BOX_AUDIO_EFFECT_GATE = 5,    /**< A gate, box_audio_gate_params_t */
    BOX_AUDIO_EFFECT_DELAY = 6,   /**< A delay, box_audio_delay_params_t */
    BOX_AUDIO_EFFECT_PHASER = 7,  /**< A phaser, box_audio_phaser_params_t */
    BOX_AUDIO_EFFECT_CHORUS = 8,  /**< A chorus, box_audio_chorus_params_t */
    BOX_AUDIO_EFFECT_SATURATOR = 9, /**< A saturator, box_audio_saturator_params_t */
    BOX_AUDIO_EFFECT_TRANSIENT_SHAPER = 10, /**< A transient shaper,
                                  *   box_audio_transient_shaper_params_t */
    BOX_AUDIO_EFFECT_KINDS = 11   /**< Number of kinds, NONE included */
} box_audio_effect_kind_t;

/** The parameters of a reverb, the first floats of the params of its effect command */
typedef struct box_audio_reverb_params_t {
    float decay;     /**< Seconds until -60 dB, BOX_AUDIO_REVERB_DECAY_MIN..MAX */
    float damping;   /**< High frequency loss, 0..1 */
    float predelay;  /**< Seconds before the first reflection, 0..BOX_AUDIO_REVERB_PREDELAY_MAX */
    float size;      /**< Room scale, 0..1, which sets the lengths of the delay lines */
    float mix;       /**< Wet level, 0..1; the dry signal passes at 1 - mix */
} box_audio_reverb_params_t;

/** Bands of an equalizer; each takes 4 floats of the params of its effect command */
#define BOX_AUDIO_EQ_BANDS 4

/** Lowest frequency of a band of an equalizer in Hz; the highest is 0.49 of the rate */
#define BOX_AUDIO_EQ_FREQUENCY_MIN 10.0f

/** Largest gain of a band of an equalizer in dB, either way */
#define BOX_AUDIO_EQ_GAIN_MAX 24.0f

/** Lowest and highest Q of a band of an equalizer */
#define BOX_AUDIO_EQ_Q_MIN 0.1f
#define BOX_AUDIO_EQ_Q_MAX 18.0f

/** Seconds over which the coefficients of a band of an equalizer move to a change */
#define BOX_AUDIO_EQ_RAMP 0.02f

/** Kinds of a band of an equalizer, each an RBJ cookbook biquad */
typedef enum box_audio_eq_kind_t {
    BOX_AUDIO_EQ_OFF = 0,         /**< Off, passes its input */
    BOX_AUDIO_EQ_PEAK = 1,        /**< A peak or a dip around its frequency, as wide as its q */
    BOX_AUDIO_EQ_LOW_SHELF = 2,   /**< The gain under its frequency */
    BOX_AUDIO_EQ_HIGH_SHELF = 3,  /**< The gain above its frequency */
    BOX_AUDIO_EQ_LOW_CUT = 4,     /**< A highpass of 12 dB per octave, -3 dB at its frequency
                                   *   at a q of 0.7071 */
    BOX_AUDIO_EQ_HIGH_CUT = 5     /**< A lowpass of 12 dB per octave */
} box_audio_eq_kind_t;

/** A band of an equalizer, 4 floats of the params of its effect command, band after band */
typedef struct box_audio_eq_band_t {
    float kind;       /**< A box_audio_eq_kind_t; an unknown one is off */
    float frequency;  /**< Center or corner in Hz, BOX_AUDIO_EQ_FREQUENCY_MIN..0.49 of the rate */
    float gain;       /**< dB, -BOX_AUDIO_EQ_GAIN_MAX..BOX_AUDIO_EQ_GAIN_MAX, for a peak and the
                       *   shelves; a peak or a shelf at 0 dB is flat and costs nothing */
    float q;          /**< BOX_AUDIO_EQ_Q_MIN..BOX_AUDIO_EQ_Q_MAX: the width of a peak, the
                       *   resonance of a shelf or a cut at its corner */
} box_audio_eq_band_t;

/** Lowest cutoff of a filter effect in Hz; the highest is 0.49 of the rate */
#define BOX_AUDIO_FILTER_CUTOFF_MIN 10.0f

/** Seconds in which the cutoff and the resonance of a filter effect move most of the way to a
 * new value, exponentially in frequency */
#define BOX_AUDIO_FILTER_SMOOTH 0.01f

/** Seconds of a change of the mix or the drive of a filter effect, and of each half of the
 * crossfade through the dry signal of a change of its kind or slope */
#define BOX_AUDIO_FILTER_RAMP 0.02f

/** The parameters of a filter effect, the first floats of the params of its effect command:
 * the state variable filter of the synth */
typedef struct box_audio_filter_params_t {
    float kind;       /**< A box_audio_synth_filter_t, LOWPASS..NOTCH; NONE or an unknown one is
                       *   a lowpass */
    float cutoff;     /**< Hz, BOX_AUDIO_FILTER_CUTOFF_MIN..0.49 of the rate */
    float resonance;  /**< 0 (a Butterworth response) to 1 (rings the longest) */
    float slope;      /**< A box_audio_synth_slope_t: 0 for 12 dB per octave, 1 for 24 */
    float mix;        /**< Wet level, 0..1; the dry signal passes at 1 - mix */
    float drive;      /**< Saturation of the input and the resonance, 0 (linear) to 1; moves
                       *   over BOX_AUDIO_FILTER_RAMP */
} box_audio_filter_params_t;

/** Seconds of the windows of the peak detector of the compressor and the gate: the detector
 * holds the largest sample of the current and the last window, so that it reads the peak of a
 * sine of 100 Hz or more without ripple. A drop of the input reaches the detector after one to
 * two windows */
#define BOX_AUDIO_DYNAMICS_WINDOW 0.005f

/** Lowest level the compressor detects in dB; silence reads as this */
#define BOX_AUDIO_DYNAMICS_FLOOR (-120.0f)

/** Seconds of a change of the threshold, ratio, knee, makeup or mix of a compressor */
#define BOX_AUDIO_COMPRESSOR_RAMP 0.02f

/** Lowest threshold of a compressor in dB; the highest is 0 */
#define BOX_AUDIO_COMPRESSOR_THRESHOLD_MIN (-80.0f)

/** Largest ratio of a compressor; the smallest is 1, no compression */
#define BOX_AUDIO_COMPRESSOR_RATIO_MAX 20.0f

/** Widest knee of a compressor in dB */
#define BOX_AUDIO_COMPRESSOR_KNEE_MAX 24.0f

/** Largest makeup gain of a compressor in dB */
#define BOX_AUDIO_COMPRESSOR_MAKEUP_MAX 24.0f

/** Shortest and longest attack of a compressor or a gate in seconds */
#define BOX_AUDIO_DYNAMICS_ATTACK_MIN 0.0001f
#define BOX_AUDIO_DYNAMICS_ATTACK_MAX 1.0f

/** Shortest and longest release of a compressor or a gate in seconds */
#define BOX_AUDIO_DYNAMICS_RELEASE_MIN 0.001f
#define BOX_AUDIO_DYNAMICS_RELEASE_MAX 5.0f

/** dB under its threshold that the level of an open gate falls before it closes, so that a
 * signal at the threshold does not open and close it over and over */
#define BOX_AUDIO_GATE_HYSTERESIS 4.0f

/** Lowest threshold and range of a gate in dB; the highest are 0 */
#define BOX_AUDIO_GATE_MIN (-100.0f)

/** Longest hold of a gate in seconds */
#define BOX_AUDIO_GATE_HOLD_MAX 5.0f

/** The parameters of a compressor, the first floats of the params of its effect command: a
 * feed-forward, stereo linked compressor whose detector reads the peak of the louder channel,
 * of its own input or of the output of the key bus of its command in the same block. The key
 * bus must come before the bus of the compressor in the order; otherwise, or while it is not
 * in the order, the compressor keys on its own input */
typedef struct box_audio_compressor_params_t {
    float threshold;  /**< dB, BOX_AUDIO_COMPRESSOR_THRESHOLD_MIN..0 */
    float ratio;      /**< 1..BOX_AUDIO_COMPRESSOR_RATIO_MAX */
    float attack;     /**< Seconds in which the detected level rises 63% of a step up,
                       *   BOX_AUDIO_DYNAMICS_ATTACK_MIN..MAX */
    float release;    /**< Seconds in which it falls 63% of a step down,
                       *   BOX_AUDIO_DYNAMICS_RELEASE_MIN..MAX */
    float knee;       /**< Width of the soft knee around the threshold in dB, 0 for a hard
                       *   knee, 0..BOX_AUDIO_COMPRESSOR_KNEE_MAX */
    float makeup;     /**< Gain after the compression in dB, 0..BOX_AUDIO_COMPRESSOR_MAKEUP_MAX */
    float mix;        /**< Wet level, 0..1, for parallel compression; the dry signal passes at
                       *   1 - mix */
} box_audio_compressor_params_t;

/** The parameters of a gate, the first floats of the params of its effect command: it opens
 * when the peak of the louder channel reaches its threshold, stays open while it is above the
 * threshold less BOX_AUDIO_GATE_HYSTERESIS and for hold seconds after, and then closes to its
 * range */
typedef struct box_audio_gate_params_t {
    float threshold;  /**< dB, BOX_AUDIO_GATE_MIN..0 */
    float attack;     /**< Seconds in which the gain rises 63% of the way to open,
                       *   BOX_AUDIO_DYNAMICS_ATTACK_MIN..MAX */
    float hold;       /**< Seconds it stays open after the level fell, 0..BOX_AUDIO_GATE_HOLD_MAX */
    float release;    /**< Seconds in which the gain falls 63% of the way to closed,
                       *   BOX_AUDIO_DYNAMICS_RELEASE_MIN..MAX */
    float range;      /**< Gain when closed in dB, BOX_AUDIO_GATE_MIN..0 */
} box_audio_gate_params_t;

/** Longest time of a delay in seconds, which sizes its memory */
#define BOX_AUDIO_DELAY_MAX 4.0f

/** Largest feedback of a delay */
#define BOX_AUDIO_DELAY_FEEDBACK_MAX 0.95f

/** Lowest damping of a delay in Hz; a damping at 0.49 of the rate or above leaves the repeats
 * undamped */
#define BOX_AUDIO_DELAY_DAMPING_MIN 20.0f

/** Seconds of the crossfade from the old to the new read position of a change of the time of
 * a delay */
#define BOX_AUDIO_DELAY_CROSSFADE 0.05f

/** Seconds of a change of the feedback, the mix or the ping pong of a delay */
#define BOX_AUDIO_DELAY_RAMP 0.02f

/** The parameters of a delay, the first floats of the params of its effect command: a stereo
 * delay line read with linear interpolation, a one pole lowpass in its feedback path. A change
 * of the time crossfades from the old to the new read position over
 * BOX_AUDIO_DELAY_CROSSFADE, without a glide of the pitch */
typedef struct box_audio_delay_params_t {
    float time;       /**< Seconds, 0..BOX_AUDIO_DELAY_MAX; at least a frame */
    float feedback;   /**< Share of a repeat in the next, 0..BOX_AUDIO_DELAY_FEEDBACK_MAX */
    float damping;    /**< Cutoff of the lowpass in the feedback path in Hz,
                       *   BOX_AUDIO_DELAY_DAMPING_MIN and up */
    float ping_pong;  /**< 1 bounces the repeats between left and right, from a mono sum of
                       *   the input into the left; 0 keeps each channel to itself */
    float mix;        /**< Wet level, 0..1; the dry signal passes at 1 - mix */
} box_audio_delay_params_t;

/** Fastest LFO of a phaser or a chorus in Hz */
#define BOX_AUDIO_MODULATION_RATE_MAX 20.0f

/** Seconds of a change of the depth, the center, the feedback, the mix or the stereo offset of
 * a phaser, of the depth, the delay, the mix or the spread of a chorus, and of each half of the
 * fade of the wet signal around a change of the stages of a phaser or the voices of a chorus */
#define BOX_AUDIO_MODULATION_RAMP 0.02f

/** Frames between two updates of the coefficients of a phaser and of the read positions of a
 * chorus, which a chorus moves to linearly frame by frame */
#define BOX_AUDIO_MODULATION_CONTROL 8

/** Fewest and most allpass stages of a phaser; the count is even */
#define BOX_AUDIO_PHASER_STAGES_MIN 2
#define BOX_AUDIO_PHASER_STAGES_MAX 12

/** Lowest and highest center of a phaser in Hz; a swept frequency stays within 10 Hz and 0.45
 * of the rate */
#define BOX_AUDIO_PHASER_CENTER_MIN 20.0f
#define BOX_AUDIO_PHASER_CENTER_MAX 20000.0f

/** Octaves a phaser sweeps either way of its center at a depth of 1 */
#define BOX_AUDIO_PHASER_OCTAVES 2.0f

/** Largest feedback of a phaser, either way */
#define BOX_AUDIO_PHASER_FEEDBACK_MAX 0.9f

/** The parameters of a phaser, the first floats of the params of its effect command: a chain
 * of first order allpasses per channel whose break frequency a sine LFO sweeps exponentially
 * around the center, the output of the chain fed back into its input and mixed with the dry
 * signal. Stages allpasses give stages / 2 notches. The LFO starts at phase 0 when the effect
 * is put on and its phase stays continuous through a change of the rate */
typedef struct box_audio_phaser_params_t {
    float stages;     /**< Allpasses per channel, BOX_AUDIO_PHASER_STAGES_MIN..MAX, rounded to
                       *   an even count */
    float rate;       /**< Hz of the LFO, 0..BOX_AUDIO_MODULATION_RATE_MAX */
    float depth;      /**< 0..1: the sweep reaches BOX_AUDIO_PHASER_OCTAVES times depth octaves
                       *   either way of the center */
    float center;     /**< Break frequency of the allpasses at the middle of the sweep in Hz,
                       *   BOX_AUDIO_PHASER_CENTER_MIN..MAX */
    float feedback;   /**< Share of the output of the chain fed back into its input,
                       *   -BOX_AUDIO_PHASER_FEEDBACK_MAX..BOX_AUDIO_PHASER_FEEDBACK_MAX */
    float mix;        /**< Wet level, 0..1; the dry signal passes at 1 - mix. 0.5 gives the
                       *   deepest notches */
    float stereo;     /**< Phase offset of the LFO of the right channel in half turns, 0..1 */
} box_audio_phaser_params_t;

/** Most voices of a chorus */
#define BOX_AUDIO_CHORUS_VOICES_MAX 4

/** Longest read of a chorus in milliseconds, which sizes its memory */
#define BOX_AUDIO_CHORUS_MAX 50.0f

/** Largest depth of a chorus in milliseconds */
#define BOX_AUDIO_CHORUS_DEPTH_MAX 25.0f

/** The parameters of a chorus, the first floats of the params of its effect command: voices
 * taps of a stereo line on the memory of its slot, each read with linear interpolation at
 * delay plus depth times a sine LFO, the LFOs of the voices spread evenly in phase and the
 * voices panned across the stereo field by the spread. A read never goes further back than
 * BOX_AUDIO_CHORUS_MAX nor nearer than a frame. The LFO starts at phase 0 when the effect is
 * put on and its phase stays continuous through a change of the rate */
typedef struct box_audio_chorus_params_t {
    float voices;     /**< Taps, 1..BOX_AUDIO_CHORUS_VOICES_MAX, rounded */
    float rate;       /**< Hz of the LFOs, 0..BOX_AUDIO_MODULATION_RATE_MAX */
    float depth;      /**< Milliseconds the taps move either way, 0..BOX_AUDIO_CHORUS_DEPTH_MAX */
    float delay;      /**< Milliseconds of the taps at the middle, 0..BOX_AUDIO_CHORUS_MAX */
    float mix;        /**< Wet level, 0..1; the dry signal passes at 1 - mix */
    float spread;     /**< 0 keeps every voice in the center, so a mono input stays mono; 1 pans
                       *   the first voice left and the last right */
} box_audio_chorus_params_t;

/** Gain before the clipper of a saturator at a drive of 1 in dB; a drive of 0 is 0 dB */
#define BOX_AUDIO_SATURATOR_DRIVE_DB 30.0f

/** Offset a saturator at an asymmetry of 1 adds to its driven input before the clipper */
#define BOX_AUDIO_SATURATOR_BIAS 1.0f

/** Fewest and most bits of the bit crusher of a saturator; at the most it is off */
#define BOX_AUDIO_SATURATOR_BITS_MIN 1.0f
#define BOX_AUDIO_SATURATOR_BITS_MAX 16.0f

/** Cutoff of the DC blocker after the clipper of a saturator in Hz */
#define BOX_AUDIO_SATURATOR_DC 5.0f

/** Seconds of a change of the drive, the asymmetry, the bits or the mix of a saturator */
#define BOX_AUDIO_SATURATOR_RAMP 0.02f

/** The parameters of a saturator, the first floats of the params of its effect command: each
 * channel runs through a tanh soft clipper at twice the rate (saturate.h), its level
 * compensated so that the drive mostly changes the tone, then a DC blocker and a bit crusher at
 * the base rate. The wet signal comes BOX_AUDIO_SATURATE_LATENCY frames late and the dry
 * signal is delayed as much, so that a mix in between does not comb. Its reduction is the
 * largest drop in dB of the peak of the clipped signal against the peak of the driven input,
 * the slope of the clipper at silence taken out */
typedef struct box_audio_saturator_params_t {
    float drive;      /**< 0..1: a gain of 0 to BOX_AUDIO_SATURATOR_DRIVE_DB dB before the
                       *   clipper */
    float asymmetry;  /**< 0..1: a bias of up to BOX_AUDIO_SATURATOR_BIAS before the clipper,
                       *   shared by both channels, for even harmonics */
    float bits;       /**< BOX_AUDIO_SATURATOR_BITS_MIN..MAX: the wet signal is rounded to
                       *   2^bits levels over -1..1, 2^(bits - 1) steps of each sign; at
                       *   BOX_AUDIO_SATURATOR_BITS_MAX it is not rounded */
    float mix;        /**< Wet level, 0..1; the dry signal passes at 1 - mix */
} box_audio_saturator_params_t;

/** Attack and release in seconds of the fast envelope of a transient shaper */
#define BOX_AUDIO_TRANSIENT_FAST_ATTACK 0.001f
#define BOX_AUDIO_TRANSIENT_FAST_RELEASE 0.02f

/** Attack and release in seconds of the slow envelope of a transient shaper */
#define BOX_AUDIO_TRANSIENT_SLOW_ATTACK 0.02f
#define BOX_AUDIO_TRANSIENT_SLOW_RELEASE 0.2f

/** Largest gain of a transient shaper either way in dB, at an attack or sustain of 1 or -1 */
#define BOX_AUDIO_TRANSIENT_RANGE 12.0f

/** Seconds of the one pole that smooths the gain of a transient shaper in dB; the peak
 * detector and the envelopes already keep it from following the cycles of a low tone */
#define BOX_AUDIO_TRANSIENT_SMOOTH 0.0005f

/** Seconds of a change of the attack or the sustain of a transient shaper */
#define BOX_AUDIO_TRANSIENT_RAMP 0.02f

/** The parameters of a transient shaper, the first floats of the params of its effect
 * command: stereo linked, it follows the peak of the louder channel with a fast and a slow
 * envelope (BOX_AUDIO_TRANSIENT_FAST_ATTACK and so on), the same gain on both channels.
 *
 * The fast one over the slow one in dB is a transient. The slow one over the fast one, or
 * over the peak when that is higher, is sustain: it ends as soon as the next hit comes up,
 * before the fast envelope catches up. The gain in dB is
 * attack times the first plus sustain times the second, each at most
 * BOX_AUDIO_TRANSIENT_RANGE and smoothed; 0 and 0 pass the signal unchanged */
typedef struct box_audio_transient_shaper_params_t {
    float attack;     /**< -1..1: above 0 boosts transients, below 0 softens them */
    float sustain;    /**< -1..1: above 0 boosts the tail, below 0 shortens it */
} box_audio_transient_shaper_params_t;

/** Put an effect into an insert slot of a bus, change its parameters, move it or take it off.
 *
 * The inserts of a bus run in the order of their index on its block, before its width, pan and
 * volume; those of the master run before the limiter. A change of the parameters is smoothed by
 * the kind. A change of kind, bus or index fades the effect out of its bus, so the bus passes
 * its dry signal, and into its new place. Kind BOX_AUDIO_EFFECT_NONE, or bus
 * BOX_AUDIO_BUS_NONE, takes the effect off the same way; when its bus left the order, as a
 * deleted bus does, its tail fades out into the master instead. An effect whose kind needs
 * memory runs once the memory of its slot arrived (box_audio_core_effect_memory), another at
 * once.
 */
typedef struct box_audio_cmd_effect_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_EFFECT */
    uint32_t slot;                  /**< Slot of the effect, below BOX_AUDIO_EFFECT_MAX */
    uint32_t kind;                  /**< A box_audio_effect_kind_t */
    uint32_t bus;                   /**< Bus whose signal it processes, or BOX_AUDIO_BUS_NONE */
    uint32_t index;                 /**< Insert slot within the bus, below
                                     *   BOX_AUDIO_EFFECT_SLOTS */
    uint32_t key;                   /**< Sidechain bus, BOX_AUDIO_BUS_NONE for none: a
                                     *   compressor keys on its output in the same block */
    float params[BOX_AUDIO_EFFECT_PARAMS]; /**< The parameters of the kind, clamped by it, the
                                     *   ones it does not read 0 */
} box_audio_cmd_effect_t;

/** Waves of a synth oscillator */
typedef enum box_audio_synth_wave_t {
    BOX_AUDIO_SYNTH_SINE = 0,      /**< Sine */
    BOX_AUDIO_SYNTH_SAW = 1,       /**< Saw, band limited */
    BOX_AUDIO_SYNTH_SQUARE = 2,    /**< Square, band limited */
    BOX_AUDIO_SYNTH_TRIANGLE = 3,  /**< Triangle */
    BOX_AUDIO_SYNTH_NOISE = 4,     /**< White noise, seeded per play */
    BOX_AUDIO_SYNTH_PLUCK = 5      /**< A Karplus-Strong string plucked with seeded noise */
} box_audio_synth_wave_t;

/** Colors of the noise of a synth oscillator, each at about the RMS level of white noise */
typedef enum box_audio_synth_color_t {
    BOX_AUDIO_SYNTH_WHITE = 0,     /**< White, flat */
    BOX_AUDIO_SYNTH_PINK = 1,      /**< Pink, falls by 3 dB per octave */
    BOX_AUDIO_SYNTH_BROWN = 2      /**< Brown, falls by 6 dB per octave */
} box_audio_synth_color_t;

/** Kinds of the filter of a synth patch */
typedef enum box_audio_synth_filter_t {
    BOX_AUDIO_SYNTH_FILTER_NONE = 0,  /**< No filter */
    BOX_AUDIO_SYNTH_LOWPASS = 1,      /**< Lowpass, 12 dB per octave */
    BOX_AUDIO_SYNTH_HIGHPASS = 2,     /**< Highpass, 12 dB per octave */
    BOX_AUDIO_SYNTH_BANDPASS = 3,     /**< Bandpass, unity at the cutoff */
    BOX_AUDIO_SYNTH_NOTCH = 4         /**< Notch, silent at the cutoff */
} box_audio_synth_filter_t;

/** Slopes of the filter of a synth patch */
typedef enum box_audio_synth_slope_t {
    BOX_AUDIO_SYNTH_SLOPE_12 = 0,  /**< 12 dB per octave, one stage */
    BOX_AUDIO_SYNTH_SLOPE_24 = 1   /**< 24 dB per octave, two cascaded stages */
} box_audio_synth_slope_t;

/** Waves of a synth LFO; each starts at 0 rising at phase 0 but the square, which starts at 1,
 * and the random one, which holds a new random value each period */
typedef enum box_audio_synth_lfo_wave_t {
    BOX_AUDIO_SYNTH_LFO_SINE = 0,      /**< Sine */
    BOX_AUDIO_SYNTH_LFO_TRIANGLE = 1,  /**< Triangle */
    BOX_AUDIO_SYNTH_LFO_SQUARE = 2,    /**< Square, its edges smoothed over 2 ms */
    BOX_AUDIO_SYNTH_LFO_SAW = 3,       /**< Rising saw, its drop smoothed over 2 ms */
    BOX_AUDIO_SYNTH_LFO_RANDOM = 4     /**< Sample and hold of random values, seeded per play,
                                        * its steps smoothed over 2 ms */
} box_audio_synth_lfo_wave_t;

/** How a synth oscillator that modulates another one changes it */
typedef enum box_audio_synth_modulation_t {
    BOX_AUDIO_SYNTH_PHASE = 0,  /**< Adds to the phase of the carrier, FM as DX synths do it */
    BOX_AUDIO_SYNTH_RING = 1    /**< Multiplies the carrier */
} box_audio_synth_modulation_t;

/** An envelope of a synth patch: an attack to 1, a decay to the sustain level, a hold there
 * and a release to 0, each linear or curved and ending on time */
typedef struct box_audio_synth_envelope_t {
    float attack;        /**< Seconds from 0 to 1 */
    float decay;         /**< Seconds from 1 to the sustain level */
    float sustain;       /**< Level after the decay, 0..1 */
    float release;       /**< Seconds from the sustain level to 0 */
    float hold;          /**< Seconds at the sustain level before the release */
    float curve;         /**< Curve of the decay and the release, 0 linear, positive fast at
                          *   first, -BOX_AUDIO_SYNTH_CURVE_MAX..BOX_AUDIO_SYNTH_CURVE_MAX */
    float attack_curve;  /**< Curve of the attack, the same way */
    float key_scale;     /**< 0..1: the decay and the release are times 2^(-key_scale key / 12)
                          *   at the key of a note, 12 log2 of its pitch ratio, fixed at its
                          *   start; 0 for none and always 0 for the filter envelope */
} box_audio_synth_envelope_t;

/** An oscillator of a synth patch */
typedef struct box_audio_synth_layer_t {
    uint32_t wave;                        /**< A box_audio_synth_wave_t */
    uint32_t has_envelope;                /**< 1 when it has its own envelope, 0 for the one
                                           * of the patch */
    float frequency;                      /**< Hz, ignored by noise */
    float frequency_end;                  /**< Hz the pitch sweeps to, 0 for no sweep */
    float sweep;                          /**< Seconds of the sweep */
    float level;                          /**< Linear gain */
    box_audio_synth_envelope_t envelope;  /**< Its own envelope, when has_envelope */
    uint32_t modulates;                   /**< 1 + the layer it modulates, which comes before
                                           * it, or 0: a modulator is not heard unless it is
                                           * audible */
    uint32_t modulation;                  /**< A box_audio_synth_modulation_t */
    float index;                          /**< Phase: peak index in radians at the top of its
                                           * envelope; ring: depth, 0..1 */
    float feedback;                       /**< Radians of its own output on its phase */
    uint32_t audible;                     /**< 1 when a modulator is heard as well, at level */
    float pulse_width;                    /**< Square: share of the period it is high,
                                           * BOX_AUDIO_SYNTH_PULSE_MIN..1 minus it; 0 for
                                           * BOX_AUDIO_SYNTH_PULSE_DEFAULT */
    uint32_t voices;                      /**< Unison voices, 1..BOX_AUDIO_SYNTH_UNISON_MAX,
                                           * 0 counts as 1 */
    float detune;                         /**< Cents from the lowest unison voice to the
                                           * highest, 0..BOX_AUDIO_SYNTH_DETUNE_MAX */
    float width;                          /**< Stereo width of the unison voices, 0 (mono)
                                           * to 1 (from left to right) */
    float decay;                          /**< BOX_AUDIO_SYNTH_PLUCK: seconds the fundamental
                                           * of the string takes to fall by 60 dB */
    float brightness;                     /**< BOX_AUDIO_SYNTH_PLUCK: 0..1, 0 a lowpass in the
                                           * loop that damps the harmonics fast, 1 almost none */
    float hardness;                       /**< BOX_AUDIO_SYNTH_PLUCK: 0..1, lowpass of the noise
                                           * that plucks the string, 0 a soft finger, 1 a pick */
    uint32_t rings_free;                  /**< BOX_AUDIO_SYNTH_PLUCK: 1 when it uses no
                                           * envelope at all and the string alone ends it */
    float sub;                            /**< Level of the sub oscillator, a band limited
                                           * square under it, 0..1, 0 for none; ignored by
                                           * noise and pluck */
    uint32_t sub_octave;                  /**< Octaves the sub is under it, 1 or 2, 0 counts
                                           * as 1 */
    float index_velocity;                 /**< Share 0..1 of the index of a modulator that
                                           * velocity scales: index (1 - share + share v) */
    float hardness_velocity;              /**< BOX_AUDIO_SYNTH_PLUCK: share 0..1 of the
                                           * hardness that velocity scales the same way */
    float sync;                           /**< Hard sync: ratio of its wave to its frequency,
                                           * 1..BOX_AUDIO_SYNTH_SYNC_MAX, restarted on every
                                           * cycle of the frequency; 0 counts as 1, off.
                                           * Ignored by noise and pluck */
    uint32_t color;                       /**< BOX_AUDIO_SYNTH_NOISE: a
                                           * box_audio_synth_color_t, 0 white; ignored by
                                           * the other waves */
} box_audio_synth_layer_t;

/** An LFO of a synth patch: a wave at a rate that starts after a delay and fades in, with a
 * depth per destination; a depth of 0 leaves that destination alone */
typedef struct box_audio_synth_lfo_t {
    uint32_t wave;          /**< A box_audio_synth_lfo_wave_t */
    float rate;             /**< Hz, 0..BOX_AUDIO_SYNTH_LFO_RATE_MAX */
    float delay;            /**< Seconds before it starts, at depth 0 meanwhile */
    float fade;             /**< Seconds it fades in over after its delay */
    float phase;            /**< Phase at its start in turns, 0..1 */
    uint32_t random_phase;  /**< 1 to start at a random phase per play instead, seeded */
    uint32_t layer;         /**< 1 + the layer it modulates, 0 for every layer; the cutoff is
                             * of the patch either way */
    float pitch;            /**< Cents of the pitch at the top of the wave */
    float amplitude;        /**< Share of the level it takes at the bottom of the wave, 0..1 */
    float cutoff;           /**< Octaves of the cutoff at the top of the wave */
    float pulse_width;      /**< Share of the period the pulse width of squares moves by */
    float sync;             /**< Semitones the ratio of hard sync moves by at the top of the
                             * wave, -BOX_AUDIO_SYNTH_SYNC_SEMITONES..max; on a layer it syncs
                             * it, for every layer only those with a ratio over 1 */
} box_audio_synth_lfo_t;

/** A synth patch: up to BOX_AUDIO_SYNTH_LAYERS oscillators with envelopes, then one filter,
 * then an amp, and up to BOX_AUDIO_SYNTH_LFOS LFOs that move them. Plain values in seconds
 * and Hz, the same at every sample rate */
typedef struct box_audio_synth_patch_t {
    uint32_t layers;                      /**< Oscillators that play, 0..BOX_AUDIO_SYNTH_LAYERS */
    uint32_t seed;                        /**< Seed of the noise and of the variances */
    float pitch_variance;                 /**< Random share of the pitch per play, 0..1 */
    float level_variance;                 /**< Random share of the level per play, 0..1 */
    box_audio_synth_envelope_t envelope;  /**< Envelope of the layers without their own */
    uint32_t has_drive;                   /**< 1 to run the mix of the layers through the
                                           * drive before the filter, 0 for none */
    float pre_drive;                      /**< Drive 0..1: a gain of BOX_AUDIO_SYNTH_DRIVE_DB_MIN
                                           * to _MAX dB before a tanh soft clipper at twice
                                           * the rate, level compensated so that a sine at
                                           * BOX_AUDIO_SYNTH_DRIVE_REFERENCE keeps its RMS */
    float pre_asymmetry;                  /**< 0..1: an offset of up to
                                           * BOX_AUDIO_SYNTH_DRIVE_BIAS before the clipper,
                                           * even harmonics; its DC is blocked */
    uint32_t filter;                      /**< A box_audio_synth_filter_t */
    float cutoff;                         /**< Hz */
    float resonance;                      /**< 0..1, 1 rings the longest */
    uint32_t slope;                       /**< A box_audio_synth_slope_t */
    float drive;                          /**< Saturation of the filter, 0 (linear) to 1 */
    float filter_amount;                  /**< Octaves the filter envelope moves the cutoff at
                                           * its level 1, negative closes it; 0 for none */
    box_audio_synth_envelope_t filter_envelope; /**< Envelope of the cutoff, which does not
                                           * keep the voice playing */
    float filter_velocity;                /**< Octaves of the cutoff at velocity 1 over velocity
                                           * 0, -BOX_AUDIO_SYNTH_OCTAVES_MAX..max: the cutoff
                                           * is cutoff * 2^(filter_velocity (v - 1)); 0 for none */
    float filter_envelope_velocity;       /**< Share 0..1 of filter_amount that velocity
                                           * scales: amount (1 - share + share v) */
    float keytrack;                       /**< 0..1: the cutoff is times the current pitch
                                           * ratio of the voice, without its variance, to the
                                           * keytrack, at the control rate; 0 for none */
    float glide;                          /**< Seconds a change of the pitch of a voice takes,
                                           * exponential in frequency, 0 jumps at once */
    uint32_t lfos;                        /**< LFOs that run, 0..BOX_AUDIO_SYNTH_LFOS */
    box_audio_synth_lfo_t lfo[BOX_AUDIO_SYNTH_LFOS]; /**< The LFOs */
    box_audio_synth_layer_t layer[BOX_AUDIO_SYNTH_LAYERS]; /**< The oscillators, last: a
                                           * command sends only the layers of its patch */
} box_audio_synth_patch_t;

/** Make a sound id a synth patch, or replace the patch of a synth sound. Voices that play it
 * keep the patch they started with; the next play takes the new one.
 *
 * The command ends after the layers of its patch: it is BOX_AUDIO_CMD_SYNTH_SIZE(layers)
 * bytes, so a patch of 2 layers does not send the room of BOX_AUDIO_SYNTH_LAYERS */
typedef struct box_audio_cmd_synth_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_SYNTH */
    uint32_t sound;                 /**< Id of the sound, free or a synth */
    box_audio_synth_patch_t patch;  /**< The patch, clamped by the core, up to its layers */
} box_audio_cmd_synth_t;

/** Bytes of a synth command whose patch has a number of layers, at most
 * BOX_AUDIO_SYNTH_LAYERS */
#define BOX_AUDIO_CMD_SYNTH_SIZE(layers) \
    ((int32_t)(offsetof(box_audio_cmd_synth_t, patch.layer) + \
    (size_t)(layers) * sizeof(box_audio_synth_layer_t)))

/** The levels of a bus since the last box_audio_core_meters, measured at its output: after
 * its inserts, width, pan, volume and mute, what it sends on. The master is measured before the
 * limiter */
typedef struct box_audio_meter_t {
    float peak_left;      /**< Largest absolute sample of the left channel */
    float peak_right;     /**< Largest absolute sample of the right channel */
    double square_left;   /**< Sum of the squares of the left samples */
    double square_right;  /**< Sum of the squares of the right samples */
    uint32_t frames;      /**< Frames the sums hold, 0 for a bus that did not render */
    uint32_t unused;      /**< Padding, 0 */
} box_audio_meter_t;

/** The values of an effect since the last box_audio_core_effect_meters, which the kinds that
 * have them fill */
typedef struct box_audio_effect_meter_t {
    float reduction;      /**< Largest gain reduction in dB, 0 for none or a kind without */
} box_audio_effect_meter_t;

/** Events the core reports to its transport */
typedef enum box_audio_event_type_t {
    BOX_AUDIO_EVENT_VOICE_END = 1,      /**< A voice ended, id is its handle; value is 0, or
                                         * the refusals so far for a play the priorities
                                         * refused */
    BOX_AUDIO_EVENT_VOICE_STOLEN = 2,   /**< The cap or a play short of synth parts stole
                                         * a voice, id is its handle, value the steals so
                                         * far */
    BOX_AUDIO_EVENT_SOUND_FREE = 3,     /**< No voice uses a released sound, id is the sound */
    BOX_AUDIO_EVENT_ORDER_REFUSED = 4,  /**< An invalid order was ignored, value its count */
    BOX_AUDIO_EVENT_IGNORED = 5,        /**< A command was ignored, id is its type */
    BOX_AUDIO_EVENT_LOST = 6,           /**< The ring was full, value is the events lost since
                                         * the last LOST event was read; the last event of
                                         * the ring when it is full */
    BOX_AUDIO_EVENT_LATE = 7            /**< A command with a future when found the schedule
                                         * full and applied at once, id is its type */
} box_audio_event_type_t;

/** An event of the core */
typedef struct box_audio_event_t {
    int32_t type;    /**< A box_audio_event_type_t */
    uint32_t id;     /**< Voice handle, sound id or command type */
    int32_t value;   /**< Extra value of the event */
} box_audio_event_t;

/** How to create a core */
typedef struct box_audio_core_desc_t {
    float sample_rate;  /**< Frames per second of the output, 48000 when not positive */
    void* (*alloc)(size_t size, void *ctx);  /**< Allocator, malloc when NULL on native */
    void (*free)(void *ptr, void *ctx);      /**< Free of the allocator, free when NULL */
    void *ctx;          /**< Context of the allocator */
} box_audio_core_desc_t;

/** A core, opaque */
typedef struct box_audio_core_t box_audio_core_t;

/** A buffer that commands are written into, owned by the caller */
typedef struct box_audio_writer_t {
    uint8_t *data;     /**< The bytes */
    int32_t size;      /**< Bytes written */
    int32_t capacity;  /**< Bytes the buffer holds */
} box_audio_writer_t;

/** Create a core. The only allocation of the core, besides box_audio_core_free.
 *
 * @param desc How to create it
 * @return The core, NULL when the allocation failed
 */
box_audio_core_t* box_audio_core_new(
    const box_audio_core_desc_t *desc);

/** Free a core. The sounds it knows stay with their owner.
 *
 * @param core The core, may be NULL
 */
void box_audio_core_free(
    box_audio_core_t *core);

/** Get the sample rate of a core.
 *
 * @param core The core
 * @return Frames per second
 */
float box_audio_core_sample_rate(
    const box_audio_core_t *core);

/** Give the core a sound, on the thread that renders, between renders.
 *
 * The data is in the address space of the core and stays with the side that runs the core;
 * BOX_AUDIO_EVENT_SOUND_FREE after a release says when it may be freed.
 *
 * @param core The core
 * @param id Id of the sound, below BOX_AUDIO_SOUND_MAX and not in use
 * @param data Frames, interleaved when stereo
 * @param frames Number of frames
 * @param channels 1 or 2
 * @param sample_rate Frames per second of the sound, the rate of the core when not positive
 * @return Whether the core took the sound
 */
bool box_audio_core_sound(
    box_audio_core_t *core,
    uint32_t id,
    const float *data,
    int32_t frames,
    int32_t channels,
    float sample_rate);

/** Queue a chunk of a stream, on the thread that renders, between renders.
 *
 * The data is in the address space of the core and stays with the side that runs the core; the
 * consumed count of box_audio_core_stream_status says when it may be freed, and so does a stream
 * that is no longer open. Does not allocate.
 *
 * @param core The core
 * @param stream Index of an open stream
 * @param data Frames, interleaved when stereo
 * @param frames Number of frames, at most BOX_AUDIO_STREAM_CHUNK
 * @param last Whether the stream ends with this chunk
 * @return Whether the core took the chunk; false when the stream is not open, its ring is
 *         full or its last chunk came already
 */
bool box_audio_core_stream_chunk(
    box_audio_core_t *core,
    uint32_t stream,
    const float *data,
    int32_t frames,
    bool last);

/** Get what a stream does. Does not allocate.
 *
 * @param core The core
 * @param stream Index of the stream
 * @param status Receives the status, zero for an invalid stream
 */
void box_audio_core_stream_status(
    const box_audio_core_t *core,
    uint32_t stream,
    box_audio_stream_status_t *status);

/** Apply a batch of commands. They take effect at the start of the next render block, but
 * a PLAY, STOP, VOICE_SET, BUS_SET, RELEASE or LEGATO with a future when, which waits in the
 * schedule and takes effect on exactly its frame: the render splits its block there.
 *
 * Commands of an unknown type are skipped by their size; a truncated command ends the batch.
 * Does not allocate.
 *
 * @param core The core
 * @param commands The bytes
 * @param size Number of bytes
 */
void box_audio_core_apply(
    box_audio_core_t *core,
    const void *commands,
    int32_t size);

/** Render frames into the left and right channel. Does not allocate, lock, log or wait.
 *
 * Advances the clock by frames. The commands of the schedule whose frame is reached apply at
 * that frame, whatever the render sizes.
 *
 * @param core The core
 * @param left Receives frames samples of the left channel
 * @param right Receives frames samples of the right channel
 * @param frames Number of frames, any count
 */
void box_audio_core_render(
    box_audio_core_t *core,
    float *left,
    float *right,
    int32_t frames);

/** Take the events of the core, oldest first.
 *
 * A BOX_AUDIO_EVENT_SOUND_FREE that found the ring full is reported once there is room again,
 * in this call or a later one. Does not allocate.
 *
 * @param core The core
 * @param events Receives the events
 * @param max Room in events
 * @return Number of events written
 */
int32_t box_audio_core_read_events(
    box_audio_core_t *core,
    box_audio_event_t *events,
    int32_t max);

/** Get the clock of a core: the frames it rendered since it was created.
 *
 * @param core The core
 * @return Number of frames, 0 for a new core
 */
uint64_t box_audio_core_clock(
    const box_audio_core_t *core);

/** Get the number of commands that wait in the schedule for their frame.
 *
 * @param core The core
 * @return Number of commands
 */
int32_t box_audio_core_scheduled(
    const box_audio_core_t *core);

/** Get the number of events lost since the last BOX_AUDIO_EVENT_LOST was read.
 *
 * @param core The core
 * @return Number of events, 0 when none were lost
 */
int32_t box_audio_core_events_lost(
    const box_audio_core_t *core);

/** Get the number of voices that play, without the ones that fade out.
 *
 * @param core The core
 * @return Number of voices
 */
int32_t box_audio_core_voice_count(
    const box_audio_core_t *core);

/** Get the number of plays refused so far because every playing voice had a higher priority.
 *
 * @param core The core
 * @return Number of plays, legatos and streams
 */
int32_t box_audio_core_refused(
    const box_audio_core_t *core);

/** Take the levels of the buses since the last call and start them over. Every bus in the
 * order is measured per block at its output, a compare and a multiply-add per sample and
 * channel. Does not allocate.
 *
 * @param core The core
 * @param meters Receives the levels of buses 0 to max - 1, by index
 * @param max Room in meters, at most BOX_AUDIO_BUS_MAX are written
 * @return Number of meters written
 */
int32_t box_audio_core_meters(
    box_audio_core_t *core,
    box_audio_meter_t *meters,
    int32_t max);

/** Take the values of the effects since the last call and start them over. Does not
 * allocate.
 *
 * @param core The core
 * @param meters Receives the values of effect slots 0 to max - 1
 * @param max Room in meters, at most BOX_AUDIO_EFFECT_MAX are written
 * @return Number of meters written
 */
int32_t box_audio_core_effect_meters(
    box_audio_core_t *core,
    box_audio_effect_meter_t *meters,
    int32_t max);

/** Get the floats of memory an effect of a kind needs at a sample rate; for the reverb its
 * delay lines at the largest size and its longest predelay, for the delay
 * BOX_AUDIO_DELAY_MAX seconds of both channels, for the chorus BOX_AUDIO_CHORUS_MAX
 * milliseconds of both channels.
 *
 * @param kind A box_audio_effect_kind_t
 * @param sample_rate Frames per second of the core
 * @return Number of floats, 0 for a kind that needs none
 */
int32_t box_audio_effect_floats(
    uint32_t kind,
    float sample_rate);

/** Give an effect slot its memory, on the thread that renders, between renders.
 *
 * The memory is in the address space of the core and stays with the side that runs the core,
 * which frees it after the core. The core clears it. A slot takes memory when it has none, or
 * larger memory than it has for a kind that needs more; the effect then starts over on it and
 * the side that runs the core keeps the old memory until the core is freed.
 *
 * @param core The core
 * @param slot Slot of the effect, below BOX_AUDIO_EFFECT_MAX
 * @param data The memory
 * @param floats Floats of the memory
 * @return Whether the core took the memory
 */
bool box_audio_core_effect_memory(
    box_audio_core_t *core,
    uint32_t slot,
    float *data,
    int32_t floats);

/** Check whether an effect renders: it has a kind, the memory of its kind, a bus or a tail
 * that fades, and, for a kind with a tail, a tail that has not decayed to silence or a signal
 * that comes in.
 *
 * @param core The core
 * @param slot Slot of the effect
 * @return Whether it renders
 */
bool box_audio_core_effect_running(
    const box_audio_core_t *core,
    uint32_t slot);

/** Start a writer on a buffer of the caller.
 *
 * @param writer The writer
 * @param data The buffer
 * @param capacity Bytes of the buffer
 */
void box_audio_writer_init(
    box_audio_writer_t *writer,
    void *data,
    int32_t capacity);

/** Write a play command, at velocity 1.
 *
 * @param writer The writer
 * @param voice Handle of the new voice
 * @param sound Id of the sound
 * @param bus Bus of the voice
 * @param volume Volume
 * @param pan Pan, -1..1
 * @param pitch Pitch ratio
 * @param flags BOX_AUDIO_PLAY_ flags
 * @param when Frame of the core clock of its first sample, 0 for now
 * @return Whether the command fit
 */
bool box_audio_write_play(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pan,
    float pitch,
    uint32_t flags,
    uint64_t when);

/** Write a play command with a velocity and a priority.
 *
 * @param writer The writer
 * @param voice Handle of the new voice
 * @param sound Id of the sound
 * @param bus Bus of the voice
 * @param volume Volume
 * @param pan Pan, -1..1
 * @param pitch Pitch ratio
 * @param velocity Velocity of the note, 0..1, 1 for a play that is not a note
 * @param flags BOX_AUDIO_PLAY_ flags
 * @param priority Priority of the voice, 0..BOX_AUDIO_PRIORITY_MAX
 * @param when Frame of the core clock of its first sample, 0 for now
 * @return Whether the command fit
 */
bool box_audio_write_play_priority(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pan,
    float pitch,
    float velocity,
    uint32_t flags,
    uint32_t priority,
    uint64_t when);

/** Write a legato command at velocity 1: a note that follows the voice of a handle.
 *
 * @param writer The writer
 * @param voice New handle of the voice
 * @param from Handle of the voice the note follows
 * @param sound Id of the sound
 * @param bus Bus of the voice
 * @param volume Volume
 * @param pan Pan, -1..1
 * @param pitch Pitch ratio
 * @param flags BOX_AUDIO_PLAY_ flags of a new voice
 * @return Whether the command fit
 */
bool box_audio_write_legato(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint32_t from,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pan,
    float pitch,
    uint32_t flags);

/** Write a legato command at a frame and velocity 1: a note that follows the voice of a
 * handle.
 *
 * @param writer The writer
 * @param voice New handle of the voice
 * @param from Handle of the voice the note follows
 * @param sound Id of the sound
 * @param bus Bus of the voice
 * @param volume Volume
 * @param pan Pan, -1..1
 * @param pitch Pitch ratio
 * @param flags BOX_AUDIO_PLAY_ flags
 * @param when Frame of the core clock of the note, 0 for now
 * @return Whether the command fit
 */
bool box_audio_write_legato_at(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint32_t from,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pan,
    float pitch,
    uint32_t flags,
    uint64_t when);

/** Write a legato command at a frame with a velocity and a priority.
 *
 * @param writer The writer
 * @param voice New handle of the voice
 * @param from Handle of the voice the note follows
 * @param sound Id of the sound
 * @param bus Bus of the voice
 * @param volume Volume
 * @param pan Pan, -1..1
 * @param pitch Pitch ratio
 * @param velocity Velocity of the note, 0..1, of a new voice; a retargeted voice keeps its own
 * @param flags BOX_AUDIO_PLAY_ flags
 * @param priority Priority of the voice, 0..BOX_AUDIO_PRIORITY_MAX
 * @param when Frame of the core clock of the note, 0 for now
 * @return Whether the command fit
 */
bool box_audio_write_legato_priority(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint32_t from,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pan,
    float pitch,
    float velocity,
    uint32_t flags,
    uint32_t priority,
    uint64_t when);

/** Write a release command, the end of the note of a voice.
 *
 * @param writer The writer
 * @param voice Handle of the voice
 * @param fade Seconds of the fade of a sample voice
 * @param when Frame of the core clock of the release, 0 for now
 * @return Whether the command fit
 */
bool box_audio_write_voice_release(
    box_audio_writer_t *writer,
    uint32_t voice,
    float fade,
    uint64_t when);

/** Write a stop command.
 *
 * @param writer The writer
 * @param voice Handle of the voice
 * @param when Frame of the core clock its fade starts at, 0 for now, which also drops the
 *     scheduled commands of the handle
 * @return Whether the command fit
 */
bool box_audio_write_stop(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint64_t when);

/** Write a command that sets a parameter of a voice.
 *
 * @param writer The writer
 * @param voice Handle of the voice
 * @param param A box_audio_param_t
 * @param value The value
 * @param when Frame of the core clock its ramp starts at, 0 for now
 * @return Whether the command fit
 */
bool box_audio_write_voice_set(
    box_audio_writer_t *writer,
    uint32_t voice,
    box_audio_param_t param,
    float value,
    uint64_t when);

/** Write a command that sets a parameter of a bus.
 *
 * @param writer The writer
 * @param bus Index of the bus
 * @param param A box_audio_param_t
 * @param value The value
 * @param when Frame of the core clock its ramp starts at, 0 for now
 * @return Whether the command fit
 */
bool box_audio_write_bus_set(
    box_audio_writer_t *writer,
    uint32_t bus,
    box_audio_param_t param,
    float value,
    uint64_t when);

/** Write a command that replaces the processing order.
 *
 * @param writer The writer
 * @param routes The buses in processing order, the master last
 * @param count Number of routes
 * @return Whether the command fit
 */
bool box_audio_write_order(
    box_audio_writer_t *writer,
    const box_audio_route_t *routes,
    int32_t count);

/** Write a command that opens a stream and starts a voice that reads it.
 *
 * @param writer The writer
 * @param voice Handle of the new voice
 * @param stream Index of the stream
 * @param bus Bus of the voice
 * @param volume Volume
 * @param channels 1 or 2
 * @param sample_rate Frames per second of the chunks
 * @param priority Priority of the voice, 0..BOX_AUDIO_PRIORITY_MAX
 * @return Whether the command fit
 */
bool box_audio_write_stream(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint32_t stream,
    uint32_t bus,
    float volume,
    uint32_t channels,
    float sample_rate,
    uint32_t priority);

/** Write a command that releases a sound.
 *
 * @param writer The writer
 * @param sound Id of the sound
 * @return Whether the command fit
 */
bool box_audio_write_release(
    box_audio_writer_t *writer,
    uint32_t sound);

/** Write a command that puts an effect into an insert slot, changes it, moves it or takes it
 * off.
 *
 * @param writer The writer
 * @param slot Slot of the effect
 * @param kind A box_audio_effect_kind_t, BOX_AUDIO_EFFECT_NONE to take it off
 * @param bus Bus of the effect, BOX_AUDIO_BUS_NONE to take it off
 * @param index Insert slot within the bus
 * @param key Sidechain bus, BOX_AUDIO_BUS_NONE for none
 * @param params BOX_AUDIO_EFFECT_PARAMS parameters, NULL for all 0
 * @return Whether the command fit
 */
bool box_audio_write_effect(
    box_audio_writer_t *writer,
    uint32_t slot,
    uint32_t kind,
    uint32_t bus,
    uint32_t index,
    uint32_t key,
    const float *params);

/** Write a command that makes a sound id a synth patch or replaces its patch, of
 * BOX_AUDIO_CMD_SYNTH_SIZE bytes for the layers of the patch.
 *
 * @param writer The writer
 * @param sound Id of the sound
 * @param patch The patch
 * @return Whether the command fit
 */
bool box_audio_write_synth(
    box_audio_writer_t *writer,
    uint32_t sound,
    const box_audio_synth_patch_t *patch);

/** Write raw bytes, for commands the writer has no function for.
 *
 * @param writer The writer
 * @param data The bytes
 * @param size Number of bytes
 * @return Whether the bytes fit
 */
bool box_audio_write_bytes(
    box_audio_writer_t *writer,
    const void *data,
    int32_t size);

/** Sine of a phase in turns, without libm, so the wasm of the core needs no imports.
 *
 * @param turns The phase in turns, one turn is 2 pi
 * @return The sine, within 1e-6 of sin(2 pi turns)
 */
float box_audio_core_sin(
    double turns);

#endif
