/**
 * @file audio/module.h
 * @brief The state of the box.audio module and its sync system, shared by its sources.
 */

#ifndef BOX_AUDIO_MODULE_H
#define BOX_AUDIO_MODULE_H

#include <box.h>
#include "output.h"
#include "analyze.h"

/** One shot voices the module keeps track of at most, the oldest are forgotten beyond this */
#define BOX_AUDIO_ONESHOT_MAX (1024)

/** Priority of the voices of a Track by default, above the one shots at 0 */
#define BOX_AUDIO_TRACK_PRIORITY (1)

/** Priority of the voice of an AudioStream, the highest, so a play never steals the music */
#define BOX_AUDIO_STREAM_PRIORITY (255)

/** Bytes the module reserves in its batch before it writes a command, room for the largest,
 * a synth patch of BOX_AUDIO_SYNTH_LAYERS layers and its LFOs */
#define BOX_AUDIO_COMMAND_RESERVE (2048)

/** The sound id of a sound entity in the core of the current output */
typedef struct AudioSoundId {
    uint32_t id;      /**< Id of the sound in the core, 0 when not sent */
    uint32_t serial;  /**< Serial of the output it was sent to */
} AudioSoundId;

/** What box.audio sent for a bus */
typedef struct AudioBusState {
    uint8_t index;          /**< Index of the bus in the core, 0 when it has none */
    uint32_t serial;        /**< Serial of the output the parameters were sent to */
    ecs_entity_t output;    /**< AudioBus.output the order was compiled with */
    ecs_entity_t sends[BOX_AUDIO_SENDS]; /**< Bus of each AudioBus.sends the order was
                             *   compiled with, 0 for none */
    uint8_t pre_fader;      /**< Pre-fader sends the order was compiled with, bit j for send j */
    float volume;           /**< Volume sent */
    float pan;              /**< Pan sent */
    float width;            /**< Width sent */
    bool mute;              /**< Mute sent, a Disabled bus and the solo included */
    float send_level[BOX_AUDIO_SENDS]; /**< Send levels sent */
    bool sends_refused;     /**< The error of too many sends was logged */
} AudioBusState;

/** What box.audio sent for a source */
typedef struct AudioVoice {
    uint32_t handle;        /**< Voice handle, 0 when the source has no voice */
    uint32_t serial;        /**< Serial of the output the voice plays on */
    uint32_t sound;         /**< Sound id the voice plays */
    bool loop;              /**< Whether the voice loops */
    float volume;           /**< Volume sent, 0 for a Disabled source */
    float pan;              /**< Pan sent */
    float cutoff;           /**< Cutoff of the distance filter sent, 0 open */
    float pitch;            /**< Pitch sent */
    uint8_t bus;            /**< Bus index sent */
    bool parked;            /**< A looping spatial source out of range: no voice, its time
                             *   runs on from origin */
    double origin;          /**< Module time at which the sound was at its start, at pitch */
} AudioVoice;

/** The timer and counters of an AudioEmitter */
typedef struct AudioEmitterState {
    uint64_t next;          /**< Frame of the core clock of the next play */
    uint32_t serial;        /**< Serial of the output the timer and active belong to */
    bool running;           /**< The timer runs: the emitter was enabled and playing in the
                             *   last frame */
    uint32_t restarts;      /**< Starts of the timer, which seed its first interval */
    int64_t count;          /**< Plays that were due, played or skipped, which seed the next */
    int64_t plays;          /**< Plays that were sent */
    int64_t skipped;        /**< Plays that were due and skipped: out of range, over
                             *   max_active, a sound that is not ready, too late */
    int32_t active;         /**< Plays sent that did not end */
} AudioEmitterState;

extern ECS_COMPONENT_DECLARE(AudioEmitterState);

/** What box.audio did with an effect entity, one with an effect component such as Reverb */
typedef struct AudioEffectState {
    uint8_t slot;           /**< Slot of the effect in the core plus 1, 0 when it has none */
    bool refused;           /**< The error that every slot of the pool is taken was logged */
    bool twice;             /**< The error that two insert slots name it was logged */
    uint32_t frame;         /**< Frame of the module in which an insert slot named it last */
    uint32_t twice_frame;   /**< Frame in which a second insert slot named it last */
    ecs_entity_t channel;   /**< Bus entity of the insert slot that names it */
    uint8_t bus;            /**< Index of that bus in the core */
    uint8_t index;          /**< Index of the insert slot within the bus */
    bool unkeyed;           /**< The error that a vocoder has no modulator was logged */
    ecs_entity_t modulator; /**< Modulator of the vocoder when that error was logged */
} AudioEffectState;

/** What box.audio sent for an effect slot of the core */
typedef struct box_audio_effect_sent_t {
    ecs_entity_t entity;    /**< Effect entity of the slot, 0 for a free slot */
    uint32_t serial;        /**< Serial of the output the command was sent to */
    uint32_t memory;        /**< Serial of the output the memory was given to */
    int32_t floats;         /**< Floats of memory given on that output */
    uint32_t kind;          /**< Kind sent, BOX_AUDIO_EFFECT_NONE when off or bypassed */
    uint32_t bus;           /**< Bus index sent */
    uint32_t index;         /**< Insert slot sent */
    uint32_t key;           /**< Sidechain bus sent, BOX_AUDIO_BUS_NONE for none */
    float params[BOX_AUDIO_EFFECT_PARAMS]; /**< Parameters sent */
} box_audio_effect_sent_t;

/** What box.audio compiled for a SynthSound */
typedef struct AudioSynth {
    box_audio_synth_patch_t patch;  /**< The patch of the last compile, the sound ids of its
                                     *   wavetables those of the current output */
    bool compiled;          /**< The patch holds the components; false once one changed */
    bool pending;           /**< The patch changed since it was last sent */
    uint32_t table_layers;  /**< Bit of each SynthWavetable layer of the patch */
    ecs_entity_t tables[BOX_AUDIO_SYNTH_LAYERS]; /**< SynthTable.table of each wavetable
                                     *   layer, 0 for none */
} AudioSynth;

/** What box.audio sent for a Wavetable */
typedef struct AudioWavetable {
    uint32_t id;            /**< Sound id of the table in the core, 0 when not sent */
    uint32_t serial;        /**< Serial of the output the table was built for, 0 to build it
                             *   again */
    int32_t frames;         /**< Frames of the table sent, 0 while it has none */
    bool logged;            /**< The error or the warning of its sound was logged */
} AudioWavetable;

/** Samples per frame of the sound of a Wavetable: the default and the limits */
#define BOX_AUDIO_WAVETABLE_FRAME_SIZE 2048
#define BOX_AUDIO_WAVETABLE_FRAME_SIZE_MIN 64
#define BOX_AUDIO_WAVETABLE_FRAME_SIZE_MAX 4096

/** What box.audio did with an Instrument */
typedef struct AudioInstrumentState {
    bool clamped;           /**< The warning of a pitch past the limits of the core was logged */
} AudioInstrumentState;

/** What box.audio did with a Track */
typedef struct AudioTrackState {
    bool warned;            /**< The warning of an instrument that cannot play was logged */
    bool audio_warned;      /**< The warning of an audio clip whose sound is not ready was
                             *   logged */
} AudioTrackState;

extern ECS_COMPONENT_DECLARE(AudioSoundId);
extern ECS_COMPONENT_DECLARE(AudioTrackState);
extern ECS_COMPONENT_DECLARE(AudioSynth);
extern ECS_COMPONENT_DECLARE(AudioWavetable);
extern ECS_COMPONENT_DECLARE(AudioInstrumentState);
extern ECS_COMPONENT_DECLARE(AudioBusState);
extern ECS_COMPONENT_DECLARE(AudioVoice);
extern ECS_COMPONENT_DECLARE(AudioEffectState);

/** Largest sound file read, in bytes */
#define BOX_AUDIO_SAMPLE_MAX_FILE (256 * 1024 * 1024)

/** Largest decoded sound, in bytes of float frames */
#define BOX_AUDIO_SAMPLE_MAX_BYTES (512 * 1024 * 1024)

/** Seconds a headless frame waits at most for the sounds that decode */
#define BOX_AUDIO_SAMPLE_HEADLESS_WAIT (10.0)

/** Decodes in flight, shared by the module and its jobs, freed by the last of them */
typedef struct box_audio_sample_sync_t {
    ecs_os_mutex_t lock;    /**< Protects working */
    int32_t working;        /**< Jobs whose decode did not finish */
    int32_t refs;           /**< The module and every job, main thread only */
} box_audio_sample_sync_t;

/** One decoded copy of a sound file, shared by every SoundFile with the same path */
typedef struct box_audio_sample_t {
    uint64_t key;           /**< Serial of the sample, which its decode job finds it by */
    char *path;             /**< SoundFile.path as written */
    SoundState state;       /**< Progress of the decode */
    char *error;            /**< Why the decode failed, NULL unless it failed */
    float *data;            /**< Frames, interleaved when stereo, NULL until ready */
    int32_t frames;         /**< Number of frames */
    int32_t channels;       /**< 1 or 2 */
    float sample_rate;      /**< Frames per second of the file */
    int32_t refs;           /**< Sound entities that use it */
    uint32_t id;            /**< Id of the sound in the core, 0 when not sent */
    uint32_t serial;        /**< Serial of the output it was sent to */
} box_audio_sample_t;

/** Drop a reference to the counter of decodes, freeing it with the last one.
 *
 * @param sync The counter
 */
void box_audio_sample_sync_release(
    box_audio_sample_sync_t *sync);

/** The decode of a sound file on the job system */
typedef struct box_audio_sample_job_t {
    uint64_t key;           /**< Key of the sample */
    char *path;             /**< The file as written */
    box_audio_sample_sync_t *sync; /**< Decodes in flight */
    float *data;            /**< Decoded frames, NULL on failure */
    int32_t frames;         /**< Number of frames */
    int32_t channels;       /**< 1 or 2, after the mix down */
    int32_t source_channels;/**< Channels of the file */
    float sample_rate;      /**< Frames per second of the file */
    char *error;            /**< Why the decode failed */
} box_audio_sample_job_t;

/** The sample of a SoundFile entity */
typedef struct AudioSample {
    box_audio_sample_t *sample; /**< The shared sample, NULL before a SoundFile is set */
} AudioSample;

extern ECS_COMPONENT_DECLARE(AudioSample);

/** Seconds of music decoded ahead of what plays */
#define BOX_AUDIO_MUSIC_AHEAD (2.0)

/** Seconds decoded ahead below which a native stream decodes again; the web decodes every frame
 * it has less than BOX_AUDIO_MUSIC_AHEAD */
#define BOX_AUDIO_MUSIC_REFILL (1.5)

/** Seconds decoded before a stream starts, unless the file is shorter */
#define BOX_AUDIO_MUSIC_START (0.5)

/** Seconds the main thread of the web decodes at most per frame */
#define BOX_AUDIO_MUSIC_WEB_BUDGET (0.002)

/** Frames decoded at once, between two checks of the budget */
#define BOX_AUDIO_MUSIC_STEP (1024)

/** Largest music file read, in bytes */
#define BOX_AUDIO_MUSIC_MAX_FILE (256 * 1024 * 1024)

/** An open Ogg Vorbis file, in vorbis.c */
typedef struct box_audio_vorbis_t box_audio_vorbis_t;

/** The decoder of a stream and the chunk it fills. Owned by its stream, or by the job that
 * decodes while one runs */
typedef struct box_audio_music_decoder_t {
    box_audio_vorbis_t *vorbis; /**< The file */
    float *partial;         /**< The chunk being filled, NULL for none */
    int32_t filled;         /**< Frames in partial */
    int64_t partial_pos;    /**< Frame of the file partial starts at */
    bool ended;             /**< A stream that does not loop decoded its last frame */
} box_audio_music_decoder_t;

/** A decoded chunk of a stream */
typedef struct box_audio_music_chunk_t {
    float *data;            /**< Frames, interleaved when stereo; NULL once sent */
    int32_t frames;         /**< Number of frames */
    int64_t pos;            /**< Frame of the file it starts at */
    bool last;              /**< Whether the stream ends with it */
} box_audio_music_chunk_t;

/** A music stream: the file, its decoder, the chunks decoded and sent, and its voice */
typedef struct box_audio_music_t {
    uint64_t key;           /**< Serial of the stream, which its jobs find it by */
    ecs_entity_t entity;    /**< The entity, 0 once it retired after a change or a delete */
    char *path;             /**< AudioStream.path as written */
    AudioStreamState state; /**< What it does */
    char *error;            /**< Why it failed */
    box_audio_music_decoder_t *decoder; /**< NULL before the file opened and while a job runs */
    bool job;               /**< A decode job runs */
    int32_t generation;     /**< Changes when decoded chunks no longer fit, after a stop */
    int64_t seek;           /**< Frame the next job seeks to first, -1 for none */
    int32_t fetch;          /**< Fetch of the file on the web, 0 for none */
    int32_t frames;         /**< Frames of the file */
    int32_t channels;       /**< 1 or 2 */
    float sample_rate;      /**< Frames per second of the file */
    box_audio_music_chunk_t ready[BOX_AUDIO_STREAM_CHUNKS]; /**< Decoded, not sent, oldest
                                                              * first */
    int32_t ready_count;    /**< Chunks in ready */
    box_audio_music_chunk_t sent[BOX_AUDIO_STREAM_CHUNKS];  /**< Sent and not consumed, a
                                                              * ring */
    int32_t sent_first;     /**< Oldest of sent */
    int32_t sent_count;     /**< Chunks in sent */
    int32_t slot;           /**< Stream of the core it plays on, -1 for none */
    uint32_t voice;         /**< Handle of the voice, 0 for none */
    uint32_t serial;        /**< Serial of the output of the voice */
    uint32_t consumed;      /**< Consumed chunks seen in the status of the stream */
    int32_t at;             /**< Frames of the oldest sent chunk that played */
    int64_t position;       /**< Frame of the file that plays */
    bool stopping;          /**< A stop was sent, the stream waits for the core to close it */
    bool ended;             /**< It played to its end */
    float volume;           /**< Volume sent */
    uint8_t bus;            /**< Bus index sent */
    int32_t underruns;      /**< Underruns of the earlier voices of the stream */
    int32_t voice_underruns; /**< Underruns of the voice that plays */
    float decode_ms;        /**< Milliseconds of the last decode */
    float decode_max_ms;    /**< Largest decode_ms */
} box_audio_music_t;

/** The decode of a part of a stream on the job system */
typedef struct box_audio_music_job_t {
    uint64_t key;           /**< Key of the stream */
    int32_t generation;     /**< Generation of the stream at the submit */
    char *path;             /**< The file to open, NULL when the decoder is open */
    uint8_t *bytes;         /**< The file the web fetched, NULL to read path */
    int32_t size;           /**< Bytes of bytes */
    box_audio_music_decoder_t *decoder; /**< The decoder, NULL until the file opened */
    box_audio_sample_sync_t *sync; /**< Decodes in flight */
    bool loop;              /**< Whether the end goes on with the start */
    int64_t seek;           /**< Frame to seek to first, -1 for none */
    int64_t want;           /**< Frames to decode at most */
    int32_t max_chunks;     /**< Chunks it may complete at most */
    double budget;          /**< Seconds it may take, 0 for no limit */
    box_audio_music_chunk_t chunks[BOX_AUDIO_STREAM_CHUNKS]; /**< Completed chunks */
    int32_t count;          /**< Chunks in chunks */
    double seconds;         /**< Seconds it took */
    char *error;            /**< Why the file did not open */
} box_audio_music_job_t;

/** The stream of an entity with AudioStream */
typedef struct AudioMusic {
    box_audio_music_t *music; /**< The stream, NULL when none plays for the entity */
    bool refused;           /**< The entity is a second stream, whose error was logged */
} AudioMusic;

extern ECS_COMPONENT_DECLARE(AudioMusic);

/** A voice of box_audio_play, kept until it ends so a deleted bus can reroute it */
typedef struct box_audio_oneshot_t {
    uint32_t handle;        /**< Voice handle */
    uint32_t sound;         /**< Sound id it plays, so a deleted sound fades it */
    uint8_t bus;            /**< Bus index */
    bool spatial;           /**< A play of box_audio_play_at */
    float position[3];      /**< World position of a spatial play */
    AudioSpatial distances; /**< Distances of a spatial play */
    float volume;           /**< Volume of the play, before the distance */
    float sent_volume;      /**< Volume sent */
    float sent_pan;         /**< Pan sent */
    float sent_cutoff;      /**< Cutoff of the distance filter sent, 0 open */
    bool legato;            /**< The mono voice of a legato synth sound */
    int32_t channel;        /**< SynthGlide.channel of a legato voice */
    ecs_entity_t emitter;   /**< AudioEmitter that played it, 0 for none */
    float pitch;            /**< Pitch ratio of the play */
    uint64_t when;          /**< Frame of the core clock it starts at, 0 for at once */
} box_audio_oneshot_t;

/** A play of box_audio_play, sent with the next batch */
typedef struct box_audio_play_t {
    ecs_entity_t sound;     /**< The sound */
    float volume;           /**< The volume */
    bool spatial;           /**< A play of box_audio_play_at */
    float position[3];      /**< World position of a spatial play */
    AudioQuantize quantize; /**< Grid of a play of box_audio_play_quantized */
    float pitch;            /**< Pitch ratio, 0 for 1 */
    bool routed;            /**< The play has a bus of its own: output instead of the
                             *   AudioOutput of the sound */
    ecs_entity_t output;    /**< Bus of a routed play, 0 for the master */
    bool has_distances;     /**< distances apply instead of the AudioSpatial of the sound */
    AudioSpatial distances; /**< Distances of a spatial play that has them */
    ecs_entity_t emitter;   /**< AudioEmitter of the play, 0 for none */
} box_audio_play_t;

/** A note of box_audio_play_note, sent with the next batch */
typedef struct box_audio_note_t {
    ecs_entity_t instrument; /**< The instrument */
    float pitch;            /**< MIDI note number */
    float velocity;         /**< Velocity, 0..1 */
    float duration;         /**< Beats at the tempo of the Transport */
} box_audio_note_t;

/** Where a spatial source is heard from the listener */
typedef struct box_audio_place_t {
    float gain;             /**< Gain of the distance, 0 out of range */
    float pan;              /**< Pan, -1 left to 1 right */
    float cutoff;           /**< Cutoff of the distance filter in Hz, 0 open */
} box_audio_place_t;

/** The listener the spatial sources are heard from */
typedef struct box_audio_listener_t {
    ecs_entity_t entity;    /**< Entity that listens, 0 for none */
    mat4 m;                 /**< Its world transform, identity without a listener */
} box_audio_listener_t;

/** Counters of the module, for the tests */
typedef struct box_audio_module_stats_t {
    int64_t batches;        /**< Batches handed to an output */
    int64_t commands;       /**< Commands written */
    int64_t bytes;          /**< Bytes of the batches */
    int64_t orders;         /**< ORDER commands written */
    int64_t cycles;         /**< Compiles that found a cycle */
    int64_t dropped;        /**< Plays dropped before a gesture */
    int64_t spatial_updates; /**< Spatial sources and plays whose gain was computed */
    int64_t out_of_range;   /**< Spatial plays that did not start beyond max_distance */
    int64_t effects;        /**< EFFECT commands written */
    int64_t not_ready;      /**< Plays dropped while their sound file loaded */
    int64_t decodes;        /**< Decodes of sound files submitted */
    int64_t music_jobs;     /**< Decode jobs of streams submitted */
    int64_t music_starts;   /**< Voices of streams started */
    int64_t synths;         /**< Compiles of synth sounds */
    int64_t notes;          /**< Notes of instruments sent */
    int64_t emitter_plays;  /**< Plays of emitters sent */
    int64_t emitter_skipped; /**< Plays of emitters skipped */
} box_audio_module_stats_t;

/** A capture that waits for its frames */
typedef struct box_audio_capture_job_t {
    float seconds;          /**< Seconds to record */
    char *path;             /**< File to write, NULL when no capture waits */
    uint32_t serial;        /**< Serial of the output that records, 0 before it started */
    bool has_song;          /**< Whether it started while a song played */
    box_audio_wav_song_t song; /**< The beat and tempo it started at, when has_song */
} box_audio_capture_job_t;

/** Hooks of the timeline at most */
#define BOX_AUDIO_TIMELINE_HOOKS 8

/** Why the timeline cancels what it scheduled ahead */
typedef enum box_audio_timeline_reset_t {
    BOX_AUDIO_TIMELINE_STOP = 1,    /**< The transport stopped */
    BOX_AUDIO_TIMELINE_SEEK = 2,    /**< The application wrote another position */
    BOX_AUDIO_TIMELINE_OUTPUT = 3   /**< A new output, whose core knows none of the handles */
} box_audio_timeline_reset_t;

/** A linear piece of the timeline: from its frame on, the beats advance at one tempo. The
 * timeline anchors one when it starts, on a seek, a new output, a tempo or loop change and at
 * every loop wrap */
typedef struct box_audio_anchor_t {
    uint64_t frame;         /**< Frame of the core clock the piece starts at */
    double beat;            /**< Beat at that frame */
    double frames_per_beat; /**< rate * 60 / tempo */
} box_audio_anchor_t;

/** A part of the lookahead window on one piece of the timeline. The window of a frame is a
 * list of them, split at a loop wrap and a change of tempo; consecutive segments and windows
 * follow each other without a gap or an overlap, in frames and in beats */
typedef struct box_audio_segment_t {
    double start;           /**< Beat at frame */
    double end;             /**< Beat at frame_end, loop_end for a segment that wraps */
    uint64_t frame;         /**< First frame of the segment */
    uint64_t frame_end;     /**< Frame after its last one */
    box_audio_anchor_t anchor; /**< The piece, which box_audio_anchor_frame maps its beats with */
} box_audio_segment_t;

struct box_audio_module_t;

/** Called every frame the transport plays with the segments of the window, after the
 * metronome; an event belongs to the segment whose beat range [start, end) holds it */
typedef void (*box_audio_timeline_window_t)(
    ecs_world_t *world,
    struct box_audio_module_t *state,
    const box_audio_segment_t *segments,
    int32_t count,
    void *ctx);

/** Called on a stop, a seek and a new output, after the module cancelled its plays */
typedef void (*box_audio_timeline_reset_cb_t)(
    ecs_world_t *world,
    struct box_audio_module_t *state,
    box_audio_timeline_reset_t why,
    void *ctx);

/** A user of the timeline, a later part of box.audio that schedules into its window */
typedef struct box_audio_timeline_hook_t {
    box_audio_timeline_window_t window;   /**< Gets the window, NULL for none */
    box_audio_timeline_reset_cb_t reset;  /**< Gets the stops, seeks and new outputs */
    void *ctx;              /**< Context of the callbacks */
} box_audio_timeline_hook_t;

/** A play the timeline scheduled ahead, which a stop or a seek cancels */
typedef struct box_audio_scheduled_t {
    uint32_t handle;        /**< Voice handle */
    uint64_t when;          /**< Frame it starts at */
} box_audio_scheduled_t;

/** Counters of the timeline, for the tests */
typedef struct box_audio_timeline_stats_t {
    int64_t clicks;         /**< Clicks of the metronome scheduled */
    int64_t accents;        /**< Accented clicks among them */
    int64_t cancels;        /**< Plays cancelled with a STOP */
    int64_t anchors;        /**< Anchors set, wraps included */
    int64_t beats;          /**< BEAT commands of the beat clock of the core written */
} box_audio_timeline_stats_t;

/** The timeline of the Transport singleton, in tempo.c */
typedef struct box_audio_timeline_t {
    bool playing;           /**< Whether it played in the last frame */
    uint32_t serial;        /**< Serial of the output it is anchored to */
    double written;         /**< Position the module wrote last, a different one is a seek */
    float tempo;            /**< Tempo applied, clamped */
    bool loop;              /**< Loop applied */
    double loop_start;      /**< Loop start applied */
    double loop_end;        /**< Loop end applied */
    float rate;             /**< Frames per second of the output */
    uint64_t clock;         /**< Clock of the output in the last update */
    ecs_vec_t anchors;      /**< box_audio_anchor_t, oldest first: the one of the clock and
                             *   the later ones up to window_end */
    uint64_t window_end;    /**< Frame the next window starts at */
    ecs_vec_t segments;     /**< box_audio_segment_t, the window of the frame */
    ecs_vec_t scheduled;    /**< box_audio_scheduled_t, plays that may not have started */
    box_audio_timeline_hook_t hooks[BOX_AUDIO_TIMELINE_HOOKS]; /**< Users of the window */
    int32_t hook_count;     /**< Hooks in hooks */
    uint32_t click;         /**< Sound id of the click of the metronome, 0 before it is sent */
    uint32_t click_serial;  /**< Serial of the output the click was sent to */
    bool beat_wanted;       /**< Something in the core reads its beat clock, which the
                             *   timeline then keeps in step with BEAT commands; set each
                             *   frame before the update, by the modulators and the
                             *   effects that read it */
    uint32_t beat_serial;   /**< Serial of the output whose beat clock the timeline keeps, 0
                             *   for none */
    box_audio_timeline_stats_t stats; /**< Counters */
} box_audio_timeline_t;

/** Slices of output the window of a meter keeps, one per frame that rendered: at 60 frames
 * per second over a second, so the window of BOX_AUDIO_METER_WINDOW is full up to 200 */
#define BOX_AUDIO_METER_SLICES 64

/** The sums of squares of a bus over the frames of one frame of the module */
typedef struct box_audio_meter_slice_t {
    double square_left;     /**< Sum of the squares of the left samples */
    double square_right;    /**< Sum of the squares of the right samples */
    uint32_t frames;        /**< Frames of the sums */
} box_audio_meter_slice_t;

/** The levels of a bus index, which the module keeps for every bus and writes into the
 * AudioMeter of its entity when it has one */
typedef struct box_audio_meter_window_t {
    ecs_entity_t entity;    /**< Bus entity of the index the window measures; another one
                             *   starts it over */
    box_audio_meter_slice_t slices[BOX_AUDIO_METER_SLICES]; /**< Newest at head */
    int32_t head;           /**< Slice written last */
    int32_t count;          /**< Slices that hold frames */
    float peak_left;        /**< Left peak of the last frame */
    float peak_right;       /**< Right peak of the last frame */
    float rms_left;         /**< Left RMS over the window */
    float rms_right;        /**< Right RMS over the window */
} box_audio_meter_window_t;

/** Shortest pass of a looping placement in beats; a shorter clip loops at this length */
#define BOX_AUDIO_CLIP_LENGTH_MIN (1.0 / 64.0)

/** The notes of a NoteClip sorted by start, or the sound of an AudioClip, rebuilt when the
 * clip changes */
typedef struct box_audio_clip_t {
    ecs_entity_t entity;    /**< The clip */
    uint32_t generation;    /**< Generation of the clips it was built at */
    double length;          /**< NoteClip.length, else AudioClip.length, 0 when not finite or
                             *   not positive */
    ecs_vec_t notes;        /**< Note, sorted by start, with finite values */
    ecs_entity_t sound;     /**< AudioClip.sound, 0 for a clip without AudioClip */
    double offset;          /**< AudioClip.offset in seconds, at least 0 */
} box_audio_clip_t;

/** A placement in the index of the sequencer */
typedef struct box_audio_placement_t {
    ecs_entity_t entity;    /**< The entity with ClipPlacement */
    ecs_entity_t track;     /**< Its parent with Track */
    double start;           /**< First beat on the timeline */
    double end;             /**< Beat after its last one */
    double reach;           /**< Largest end of this and the placements before it */
    double scanned;         /**< Beat of the timeline up to which its notes were picked,
                             *   -INFINITY when none since the last jump */
} box_audio_placement_t;

/** A note that the sequencer scheduled and that has not ended */
typedef struct box_audio_sounding_t {
    uint32_t handle;        /**< Voice handle */
    uint32_t serial;        /**< Serial of the output it plays on */
    uint64_t start;         /**< Frame it starts at */
    uint64_t release;       /**< Frame of the RELEASE sent, 0 while none was sent */
    double end;             /**< Beat at which its release starts */
    float fade;             /**< Seconds of the fade of a sampler */
    float pitch;            /**< MIDI note number played, the transpose included */
    ecs_entity_t track;     /**< The track */
    ecs_entity_t placement; /**< The placement */
} box_audio_sounding_t;

/** A ramp of the expression of a note that the sequencer scheduled, which it sends once the
 * beat at which it starts falls in a window */
typedef struct box_audio_seq_ramp_t {
    uint32_t handle;        /**< Voice handle of the note */
    uint32_t serial;        /**< Serial of the output it plays on */
    uint32_t param;         /**< BOX_AUDIO_PARAM_BEND, _MOD or _PRESSURE */
    float value;            /**< Value at its end */
    double beat;            /**< Beat of the timeline at which it starts */
    double end;             /**< Beat at which it ends */
    uint64_t first;         /**< Frame of the start of the note, the earliest of the ramp */
    ecs_entity_t track;     /**< The track */
    ecs_entity_t placement; /**< The placement */
} box_audio_seq_ramp_t;

/** A note with expression whose ramps the sequencer sent ahead, which a stop, a seek or the
 * end of its placement cancels in the core */
typedef struct box_audio_seq_expressed_t {
    uint32_t handle;        /**< Voice handle of the note */
    uint32_t serial;        /**< Serial of the output it plays on */
    uint64_t last;          /**< Frame of the last ramp sent */
    ecs_entity_t track;     /**< The track */
    ecs_entity_t placement; /**< The placement */
} box_audio_seq_expressed_t;

/** A voice of an audio clip that the sequencer scheduled and that has not stopped */
typedef struct box_audio_clip_voice_t {
    uint32_t handle;        /**< Voice handle */
    uint32_t serial;        /**< Serial of the output it plays on */
    uint64_t start;         /**< Frame it starts at */
    uint64_t sound_end;     /**< Frame at which its sound ends */
    uint64_t stop;          /**< Frame of the STOP sent, 0 while none was sent */
    double end;             /**< Beat at which it stops: the end of its pass, of the placement
                             *   or loop_end */
    ecs_entity_t track;     /**< The track */
    ecs_entity_t placement; /**< The placement */
} box_audio_clip_voice_t;

/** Counters of the sequencer, for the tests */
typedef struct box_audio_sequencer_stats_t {
    int64_t notes;          /**< Notes scheduled */
    int64_t releases;       /**< RELEASE commands written */
    int64_t dropped;        /**< Notes dropped: an instrument that cannot play or a full batch */
    int64_t visits;         /**< Placements visited by the windows */
    int64_t rebuilds;       /**< Rebuilds of the index of the placements */
    int64_t clips;          /**< Rebuilds of the sorted notes of a clip */
    int64_t passes;         /**< Passes of audio clips started */
    int64_t seeks;          /**< Passes among them that started in the middle, with a
                             *   POSITION */
    int64_t stops;          /**< STOP commands of audio clip voices written */
    int64_t skipped;        /**< Passes of audio clips skipped: a sound that is not ready or a
                             *   full batch */
    int64_t ramps;          /**< VOICE_RAMP commands of the expression of notes written, the
                             *   initial values included */
    int64_t ramps_dropped;  /**< Ramps dropped before they were sent: a stop, a seek, a jump
                             *   or the end of their placement */
    int64_t ramp_cancels;   /**< CANCEL commands of the ramps of a voice written */
} box_audio_sequencer_stats_t;

/** The clips, tracks and placements of the timeline, in sequencer.c */
typedef struct box_audio_sequencer_t {
    ecs_query_t *placements; /**< ClipPlacement, disabled ones included, uncached */
    ecs_vec_t index;        /**< box_audio_placement_t, sorted by start */
    ecs_vec_t scans;        /**< box_audio_placement_t, the index before a rebuild sorted by
                             *   entity, which carries scanned over */
    bool dirty;             /**< The index needs a rebuild */
    ecs_map_t clips;        /**< Clip entity to box_audio_clip_t* */
    uint32_t generation;    /**< Changes with every change of a NoteClip */
    ecs_vec_t sounding;     /**< box_audio_sounding_t */
    ecs_vec_t voices;       /**< box_audio_clip_voice_t, the voices of audio clips */
    ecs_vec_t ramps;        /**< box_audio_seq_ramp_t, the ramps of the expression of notes
                             *   still to send */
    ecs_vec_t expressed;    /**< box_audio_seq_expressed_t, the notes whose ramps were sent */
    double expect;          /**< Beat the next segment starts at, NAN after a reset */
    box_audio_sequencer_stats_t stats; /**< Counters */
} box_audio_sequencer_t;

/** Seconds between the values an automation samples from its curve, besides its keys */
#define BOX_AUDIO_AUTOMATION_STEP (0.01)

/** Bytes of the largest effect component an automation copies */
#define BOX_AUDIO_AUTOMATION_COMPONENT (256)

/** What an automation moves in the core */
typedef enum box_audio_target_kind_t {
    BOX_AUDIO_TARGET_BUS = 1,       /**< A parameter of a bus, with BUS_SET */
    BOX_AUDIO_TARGET_EFFECT = 2,    /**< The parameters of an effect slot, with EFFECT_PARAMS */
    BOX_AUDIO_TARGET_MACRO = 3      /**< The value of an AudioMacro, with MACRO */
} box_audio_target_kind_t;

/** A parameter of a bus, an effect entity or a macro that automation lanes move, kept across the
 * frames to cancel and restore what was scheduled for it */
typedef struct box_audio_target_t {
    box_audio_target_kind_t kind; /**< What it is */
    ecs_entity_t entity;    /**< The bus, the effect entity or the macro */
    ecs_entity_t component; /**< AudioBus, the effect component or AudioMacro */
    uint32_t param;         /**< The box_audio_param_t of a bus */
    int32_t send;           /**< Index of the send of a send level, -1 for another parameter */
    uint32_t index;         /**< Bus index, effect slot or macro slot the last value went to */
    uint32_t effect;        /**< Kind of effect the last value went to */
    uint32_t serial;        /**< Serial of the output the last value went to, 0 for none */
    uint64_t ahead;         /**< Frame of the last value scheduled, 0 for none */
    bool held;              /**< The last value sent is the value of its component */
    bool live;              /**< Some lane of this rebuild moves it */
    bool busy;              /**< A lane of the segment moves it, or it is not held */
} box_audio_target_t;

/** A ClipPlacement of an AutomationClip on a Track, resolved */
typedef struct box_audio_lane_t {
    ecs_entity_t placement; /**< The placement */
    ecs_entity_t track;     /**< Its track */
    ecs_entity_t clip;      /**< The entity with AutomationClip */
    double start;           /**< First beat on the timeline */
    double end;             /**< Beat after the last one */
    double offset;          /**< Beats into the clip at which the placement starts */
    double length;          /**< Beats of a pass of a placement that loops, 0 when it does
                             *   not */
    int32_t target;         /**< Index of its target */
    bool on;                /**< Its track and placement play and its clip exists, as of the
                             *   window */
} box_audio_lane_t;

/** A frame a segment samples its automation at, with the beat it samples */
typedef struct box_audio_automation_point_t {
    uint64_t frame;         /**< Frame of the value */
    double beat;            /**< Beat of the timeline the value is taken at */
} box_audio_automation_point_t;

/** Counters of the automation, for the tests */
typedef struct box_audio_automation_stats_t {
    int64_t bus_sets;       /**< BUS_SET commands of automated values written */
    int64_t effect_sets;    /**< EFFECT_PARAMS commands written */
    int64_t macro_sets;     /**< MACRO commands of automated values written */
    int64_t restores;       /**< Values of a component sent after a lane ended */
    int64_t cancels;        /**< CANCEL commands written */
    int64_t rebuilds;       /**< Rebuilds of the lanes */
    int64_t errors;         /**< Clips whose error was logged */
} box_audio_automation_stats_t;

/** The automation lanes of the timeline, in automation.c */
typedef struct box_audio_automation_t {
    ecs_vec_t lanes;        /**< box_audio_lane_t, in the order of the placements */
    ecs_vec_t targets;      /**< box_audio_target_t */
    ecs_vec_t past;         /**< box_audio_segment_t of the earlier windows that did not
                             *   play yet, which a change schedules again */
    ecs_vec_t points;       /**< box_audio_automation_point_t, what a segment samples */
    ecs_map_t logged;       /**< Clips whose error was logged, until they change */
    bool dirty;             /**< The lanes need a rebuild and what they scheduled ahead a
                             *   new schedule */
    double expect;          /**< Beat the next segment starts at, NAN after a reset */
    box_audio_automation_stats_t stats; /**< Counters */
} box_audio_automation_t;

/** What became of an AudioModulator in the last frame */
typedef enum box_audio_mod_status_t {
    BOX_AUDIO_MOD_STATUS_NONE = 0,  /**< Not looked at yet */
    BOX_AUDIO_MOD_STATUS_RUNS = 1,  /**< It runs in the core */
    BOX_AUDIO_MOD_STATUS_WAITS = 2, /**< Its target is not in the output: a bus without an
                                     *   index yet, an effect in no insert slot or disabled */
    BOX_AUDIO_MOD_STATUS_ERROR = 3, /**< Its target, member or settings are not valid */
    BOX_AUDIO_MOD_STATUS_FULL = 4,  /**< Every slot of the core is taken */
    BOX_AUDIO_MOD_STATUS_CROWDED = 5, /**< BOX_AUDIO_MOD_PER_TARGET others move its member */
    BOX_AUDIO_MOD_STATUS_DISABLED = 6 /**< It is disabled */
} box_audio_mod_status_t;

/** What the module did for an AudioModulator, added with it */
typedef struct AudioModulatorState {
    uint8_t slot;           /**< Slot of the core plus 1, 0 for none */
    uint8_t status;         /**< A box_audio_mod_status_t */
    bool logged;            /**< Its error was logged, until it changes */
    uint32_t frame;         /**< Frame of the module it was looked at last */
} AudioModulatorState;

extern ECS_COMPONENT_DECLARE(AudioModulatorState);

/** What a modulator slot of the core holds and got */
typedef struct box_audio_mod_sent_t {
    ecs_entity_t entity;    /**< Modulator entity of the slot, 0 for a free slot */
    uint32_t serial;        /**< Serial of the output cmd went to, 0 for none */
    box_audio_cmd_modulator_t cmd; /**< The last command, target NONE once released */
} box_audio_mod_sent_t;

/** Counters of the modulators, for the tests */
typedef struct box_audio_mod_stats_t {
    int64_t commands;       /**< MODULATOR commands that start or change a modulator */
    int64_t releases;       /**< MODULATOR commands that release one */
    int64_t errors;         /**< Errors logged */
} box_audio_mod_stats_t;

/** The modulators of the module, in modulator.c */
typedef struct box_audio_modulators_t {
    ecs_query_t *query;     /**< AudioModulator with its state, disabled ones included */
    box_audio_mod_sent_t slots[BOX_AUDIO_MOD_MAX]; /**< The slots of the core */
    uint32_t frame;         /**< Frames of the module, which tell the modulators of this frame
                             *   from older ones */
    box_audio_mod_stats_t stats; /**< Counters */
} box_audio_modulators_t;

/** Seconds the voices and buses of a song fade out before it unloads, past the ramp of a stop
 * and a mute of the core */
#define BOX_AUDIO_SONG_FADE_TIME (0.012)

/** Seconds a swap waits at most for the slots of the old song before it gives up */
#define BOX_AUDIO_SONG_TIMEOUT (5.0)

/** Where the load of a song is */
typedef enum box_audio_song_stage_t {
    BOX_AUDIO_SONG_IDLE = 0,    /**< Nothing in flight: a song plays, failed, or none is loaded */
    BOX_AUDIO_SONG_FADE = 1,    /**< The voices and buses of the old song fade out */
    BOX_AUDIO_SONG_DRAIN = 2,   /**< The old song is deleted, its slots are not reusable yet */
    BOX_AUDIO_SONG_DECODE = 3   /**< The new song is loaded, its sound files decode */
} box_audio_song_stage_t;

/** Counters of the songs, for the tests */
typedef struct box_audio_song_stats_t {
    int64_t loads;              /**< Song scripts loaded */
    int64_t unloads;            /**< Song scripts deleted */
    int64_t failures;           /**< Loads that ended in SongFailed */
    int64_t reloads;            /**< Reloads of a song whose file or script changed */
    int64_t waits;              /**< Frames a swap waited for the slots of the old song */
} box_audio_song_stats_t;

/** The song of the module, in song.c */
typedef struct box_audio_song_t {
    ecs_entity_t root;          /**< box.audio.songs, the parent of the song scripts */
    ecs_entity_t script;        /**< Managed script of the loaded song, 0 for none */
    char *file;                 /**< AudioSong.file of the loaded song */
    char *code;                 /**< Code the song script was evaluated with */
    char *next_code;            /**< Code an evaluation outside the engine gave the song
                                 *   script, which its reload loads instead of the file */
    char *target;               /**< The file the engine works towards, NULL for none */
    box_audio_song_stage_t stage; /**< Where the load is */
    double elapsed;             /**< Seconds in the stage */
    int32_t frames;             /**< Frames in the stage */
    bool pending;               /**< The target waits to be loaded once no song is loaded */
    bool settled;               /**< The sound files of the new song were ready a frame ago */
    bool reload;                /**< The file of the song changed on disk, or one of its
                                 *   scripts was evaluated again outside the engine */
    bool loading;               /**< The engine evaluates the song script */
    bool dropping;              /**< The engine deletes the song scripts or takes entities
                                 *   from them */
    ecs_vec_t baseline;         /**< ecs_id_t, the type of box.audio.Song before the load */
    uint64_t held_buses;        /**< Bus indices the unloaded song held, still marked freed */
    ecs_entity_t held_effects[BOX_AUDIO_EFFECT_MAX]; /**< Deleted effect of each effect slot
                                 *   the unloaded song held that the sync did not free yet */
    ecs_vec_t held_sounds;      /**< uint32_t sound ids the unloaded song held whose free the
                                 *   core did not report yet */
    uint32_t held_serial;       /**< Serial of the output the held slots belong to */
    char *held_file;            /**< File of the song whose slots are held, for the error */
    ecs_vec_t foreign;          /**< ecs_entity_t, entities the song changed that it did not
                                 *   create, collected during its load */
    ecs_entity_t observer;      /**< Observer of the changes during a load, 0 outside it */
    int32_t watch;              /**< Id of the file watch kind of the songs, 0 without box.app */
    bool hold;                  /**< Keep the timeline stopped: a song swaps or loads */
    uint64_t silenced;          /**< Bus indices of a song that fades out, sent muted */
    box_audio_song_stats_t stats; /**< Counters */
} box_audio_song_t;

/** What became of an AudioMacro or an AudioMacroMap in the last frame */
typedef enum box_audio_macro_status_t {
    BOX_AUDIO_MACRO_STATUS_NONE = 0,  /**< Not looked at yet */
    BOX_AUDIO_MACRO_STATUS_RUNS = 1,  /**< A macro has a slot of the core, a map runs */
    BOX_AUDIO_MACRO_STATUS_WAITS = 2, /**< A map whose target is not in the output, or whose
                                       *   macro has no slot */
    BOX_AUDIO_MACRO_STATUS_ERROR = 3, /**< A map whose parent, target, member or range is not
                                       *   valid */
    BOX_AUDIO_MACRO_STATUS_FULL = 4,  /**< Every macro slot is taken, or its macro has
                                       *   BOX_AUDIO_MACRO_MAPS maps before it */
    BOX_AUDIO_MACRO_STATUS_TAKEN = 5, /**< A map of a member a newer map moves */
    BOX_AUDIO_MACRO_STATUS_DISABLED = 6 /**< It is disabled */
} box_audio_macro_status_t;

/** What the module did for an AudioMacro, added with it */
typedef struct AudioMacroState {
    uint8_t slot;           /**< Slot of the core plus 1, 0 for none */
    uint8_t status;         /**< A box_audio_macro_status_t */
    bool logged;            /**< The error of a full core was logged */
    bool maps_logged;       /**< The error of too many maps was logged, until it has at most
                             *   BOX_AUDIO_MACRO_MAPS again */
    int32_t maps;           /**< Maps that can run in the last frame, the ones past the limit
                             *   included */
    uint32_t frame;         /**< Frame of the module it was looked at last */
} AudioMacroState;

extern ECS_COMPONENT_DECLARE(AudioMacroState);

/** What the module did for an AudioMacroMap, added with it */
typedef struct AudioMacroMapState {
    uint16_t slot;          /**< Map slot of the core plus 1, 0 for none */
    uint8_t status;         /**< A box_audio_macro_status_t */
    bool logged;            /**< Its error was logged, until it changes */
    bool warned;            /**< The warning of a member a newer map takes was logged, until
                             *   it changes */
    uint32_t frame;         /**< Frame of the module it was looked at last */
} AudioMacroMapState;

extern ECS_COMPONENT_DECLARE(AudioMacroMapState);

/** What a macro slot of the core holds and got */
typedef struct box_audio_macro_sent_t {
    ecs_entity_t entity;    /**< Macro entity of the slot, 0 for a free slot */
    uint32_t serial;        /**< Serial of the output value went to, 0 for none */
    float value;            /**< The value of the component sent last */
} box_audio_macro_sent_t;

/** What a map slot of the core holds and got */
typedef struct box_audio_map_sent_t {
    ecs_entity_t entity;    /**< Map entity of the slot, 0 for a free slot */
    uint32_t serial;        /**< Serial of the output cmd went to, 0 for none */
    box_audio_cmd_macro_map_t cmd; /**< The last command, target NONE once released */
} box_audio_map_sent_t;

/** A map that can run in a frame, before the maps of one member are sorted out */
typedef struct box_audio_map_candidate_t {
    ecs_entity_t entity;    /**< The map entity */
    ecs_entity_t macro;     /**< Its macro */
    box_audio_cmd_macro_map_t cmd; /**< Its command, its slot aside */
    bool taken;             /**< A newer map moves its member */
} box_audio_map_candidate_t;

/** Counters of the macros, for the tests */
typedef struct box_audio_macro_stats_t {
    int64_t values;         /**< MACRO commands of the values of the components */
    int64_t maps;           /**< MACRO_MAP commands that start or change a map */
    int64_t releases;       /**< MACRO_MAP commands that release one */
    int64_t errors;         /**< Errors logged */
    int64_t warnings;       /**< Warnings logged */
} box_audio_macro_stats_t;

/** The macros of the module, in macro_sync.c */
typedef struct box_audio_macros_t {
    ecs_query_t *query;     /**< AudioMacro with its state, disabled ones included */
    ecs_query_t *map_query; /**< AudioMacroMap with its state, disabled ones included */
    box_audio_macro_sent_t slots[BOX_AUDIO_MACRO_MAX]; /**< The macro slots of the core */
    box_audio_map_sent_t maps[BOX_AUDIO_MACRO_MAP_MAX]; /**< The map slots of the core */
    box_audio_map_candidate_t candidates[BOX_AUDIO_MACRO_MAP_MAX]; /**< The maps of the frame */
    uint32_t frame;         /**< Frames of the module, which tell the macros of this frame from
                             *   older ones */
    box_audio_macro_stats_t stats; /**< Counters */
} box_audio_macros_t;

/** The state of the module in a world */
typedef struct box_audio_module_t {
    ecs_query_t *buses;           /**< AudioBus, disabled ones included */
    ecs_query_t *sources;         /**< AudioSource without AudioSpatial, disabled ones included */
    ecs_query_t *spatial;         /**< AudioSource with AudioSpatial, change detected */
    ecs_query_t *listeners;       /**< AudioListener with WorldTransform */
    ecs_query_t *views;           /**< Enabled views, for a camera that listens */
    ecs_query_t *sounds;          /**< AudioPcm */
    ecs_query_t *synths;          /**< SynthSound, prefabs included */
    ecs_query_t *wavetables;      /**< Wavetable, prefabs included */
    ecs_query_t *meter_query;     /**< AudioMeter, disabled entities included */
    ecs_query_t *emitters;        /**< AudioEmitter with its state, disabled ones included */
    box_audio_meter_window_t meters[BOX_AUDIO_BUS_MAX]; /**< Levels of each bus index */
    float reduction[BOX_AUDIO_EFFECT_MAX]; /**< Gain reduction of each effect slot in the last
                                   *   frame, dB */
    box_audio_effect_sent_t effects[BOX_AUDIO_EFFECT_MAX]; /**< What each effect slot of the
                                   *   core holds and got */
    uint32_t effect_frame;        /**< Frames of the module, which tell the insert slots of
                                   *   this frame from older ones */
    bool effect_beat;             /**< An effect of this frame reads the beat clock of the
                                   *   core: a pattern gate or a beat repeat */
    uint32_t serial;              /**< Serial of the output the state was sent to */
    box_audio_writer_t writer;    /**< The batch of the frame, grown as needed */
    uint32_t next_voice;          /**< Next voice handle */
    uint8_t sound_used[BOX_AUDIO_SOUND_MAX];   /**< Sound ids in use in the core */
    ecs_entity_t bus_entity[BOX_AUDIO_BUS_MAX]; /**< Entity of each bus index, 0 for free */
    uint64_t bus_freed;           /**< Bus indices freed this frame, not reused before next */
    ecs_entity_t master;          /**< box.audio.master, whose AudioBus sets bus 0 */
    uint8_t route_out[BOX_AUDIO_BUS_MAX]; /**< Output of each bus index at the last compile,
                                   *   cycles included, 0 for the master */
    uint8_t route_sends[BOX_AUDIO_BUS_MAX][BOX_AUDIO_SENDS]; /**< Sends of each bus index at
                                   *   the last compile, BOX_AUDIO_BUS_NONE for none */
    ecs_entity_t sidechains[BOX_AUDIO_BUS_MAX][BOX_AUDIO_EFFECT_SLOTS]; /**< Sidechain of the
                                   *   Compressor, or modulator of the Vocoder, in each insert
                                   *   slot of each bus index at the last compile, 0 for none */
    ecs_entity_t sidechain_refused[BOX_AUDIO_BUS_MAX][BOX_AUDIO_EFFECT_SLOTS]; /**< Sidechain
                                   *   whose cycle was logged for each insert slot, 0 for none */
    uint8_t keys[BOX_AUDIO_BUS_MAX][BOX_AUDIO_EFFECT_SLOTS]; /**< Key bus of the compressor or
                                   *   the vocoder in each insert slot of each bus index,
                                   *   BOX_AUDIO_BUS_NONE for none */
    int32_t sidechain_count;      /**< Insert slots with a sidechain at the last compile */
    uint64_t solo;                /**< Bus indices soloed this frame */
    uint64_t solo_safe;           /**< Bus indices of returns and SFX buses this frame */
    uint64_t audible;             /**< Bus indices the solo keeps, all when none is soloed */
    bool order_dirty;             /**< The buses or their routes changed */
    bool bus_overflow;            /**< The error of too many buses was logged */
    bool sound_overflow;          /**< The error of too many sounds was logged */
    uint8_t order[BOX_AUDIO_BUS_MAX]; /**< Bus indices of the last valid order, master last */
    int32_t order_count;          /**< Buses in order */
    ecs_map_t voices;             /**< Voice handle to source entity */
    ecs_map_t spread_notes;       /**< Instrument entity to the count of the notes it played,
                                   *   for the random and alternate spread */
    ecs_vec_t oneshots;           /**< box_audio_oneshot_t */
    ecs_vec_t plays;              /**< box_audio_play_t */
    ecs_vec_t notes;              /**< box_audio_note_t */
    ecs_vec_t stops;              /**< uint32_t voice handles of deleted sources */
    ecs_vec_t releases;           /**< uint32_t sound ids */
    ecs_vec_t moved;              /**< ecs_entity_t spatial sources whose static transform moved */
    ecs_vec_t fades;              /**< uint32_t ids of deleted sounds whose one shots fade */
    ecs_vec_t samples;            /**< box_audio_sample_t*, the decoded sound files */
    uint64_t next_sample;         /**< Key of the next sample */
    box_audio_sample_sync_t *sample_sync; /**< Decodes in flight */
    box_task_track_t sample_task; /**< Report of the decodes in flight */
    ecs_query_t *sample_users;    /**< AudioSample, to update the status of its entities */
    ecs_query_t *music_query;     /**< AudioStream with AudioMusic, disabled ones included */
    ecs_vec_t music;              /**< box_audio_music_t*, the streams, retired ones included */
    uint64_t next_music;          /**< Key of the next stream */
    box_audio_music_t *music_active; /**< The stream of an entity, the one that may play */
    AudioSettings settings;       /**< Settings sent */
    bool settings_sent;           /**< settings holds what the output has */
    AudioBus master_bus;          /**< AudioBus of the master sent, its sends left empty */
    int64_t voices_stolen;        /**< Steals the core reported */
    int64_t voices_refused;       /**< Refusals the core reported */
    box_audio_capture_job_t capture; /**< The capture that waits */
    box_audio_listener_t listener;   /**< The listener of the last frame */
    box_audio_timeline_t timeline;   /**< The timeline of the Transport */
    box_audio_sequencer_t sequencer; /**< The tracks of the timeline */
    box_audio_automation_t automation; /**< The automation lanes of the timeline */
    box_audio_modulators_t modulators; /**< The modulators */
    box_audio_macros_t macros;    /**< The macros and their maps */
    box_audio_song_t song;           /**< The song of AudioSong */
    bool listener_warned;         /**< The warning of several listeners was logged */
    bool spatial_dirty;           /**< Update every spatial source, not only changed ones */
    bool waiting;                 /**< The page waited for a gesture in the last frame */
    double time;                  /**< Seconds of output since the module started */
    float rate;                   /**< Frames per second of the output */
    box_audio_module_stats_t stats;  /**< Counters */
} box_audio_module_t;

/** Get the state of the module of a world.
 *
 * @param world The world
 * @return The state, NULL when box.audio is not imported
 */
box_audio_module_t* box_audio_module_get(
    const ecs_world_t *world);

/** Install the lifecycle hooks of Song, SongLane, AudioSong and AudioSongStatus, right after
 * their registration.
 *
 * @param world The world
 */
void box_audio_song_hooks(
    ecs_world_t *world);

/** Register the song singletons, the root of the song scripts, the observer of what the song
 * scripts own and the system that loads and unloads songs.
 *
 * The root of the songs owns box.audio.Song, so no script takes it and deletes it when it
 * unloads.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_song_init(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Free what the song of the module holds.
 *
 * @param state The state
 */
void box_audio_song_fini(
    box_audio_module_t *state);

/** Tell the song that the core freed a sound id, from the events of the output.
 *
 * @param state The state
 * @param id The sound id
 */
void box_audio_song_sound_free(
    box_audio_module_t *state,
    uint32_t id);

/** Count the slots a swap still waits for: the bus, effect and sound slots of the song that
 * unloaded that are not reusable yet, for the tests.
 *
 * @param world The world
 * @return Slots, 0 without a swap
 */
int32_t box_audio_song_held(
    const ecs_world_t *world);

/** Get the counters of the songs, for the tests.
 *
 * @param world The world
 * @return The counters, zero when box.audio is not imported
 */
box_audio_song_stats_t box_audio_song_stats(
    const ecs_world_t *world);

/** Count the bus indices in use, the master excluded, for the tests.
 *
 * @param world The world
 * @return Bus slots that have an entity
 */
int32_t box_audio_module_buses(
    const ecs_world_t *world);

/** Count the effect slots in use, for the tests.
 *
 * @param world The world
 * @return Effect slots that have an entity
 */
int32_t box_audio_module_effects(
    const ecs_world_t *world);

/** Get the counters of the module of a world, for the tests.
 *
 * @param world The world
 * @return The counters, zero when box.audio is not imported
 */
box_audio_module_stats_t box_audio_module_stats(
    const ecs_world_t *world);

/** Get the buses of the last valid order, in processing order, without the master.
 *
 * @param world The world
 * @param buses Receives the bus entities
 * @param max Room in buses
 * @return Number of buses written
 */
int32_t box_audio_module_order(
    const ecs_world_t *world,
    ecs_entity_t *buses,
    int32_t max);

/** Get the index of a bus in the core.
 *
 * @param world The world
 * @param bus The bus entity
 * @return The index, 0 for the master or an entity that is no bus
 */
int32_t box_audio_module_bus_index(
    const ecs_world_t *world,
    ecs_entity_t bus);

/** Get the clock of the output of a world: the frame of its core that renders now, as far as
 * the main thread can tell, which commands with a when are scheduled against.
 *
 * @param world The world
 * @return Frames of the core clock, 0 when the world has no output
 */
uint64_t box_audio_module_clock(
    const ecs_world_t *world);

/** Find the listener of the frame: the first AudioListener, else the camera of the first
 * enabled View that has one.
 *
 * @param world The world
 * @param state The state
 * @param listener Receives the listener
 */
void box_audio_listener_find(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_listener_t *listener);

/** Get the slot of an effect entity in the pool of the core.
 *
 * @param world The world
 * @param e The entity with an effect component
 * @return The slot, -1 when it has none
 */
int32_t box_audio_module_effect_slot(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Get the kind of effect of an entity: the kind of its effect component.
 *
 * @param world The world
 * @param e The entity
 * @param params Receives the BOX_AUDIO_EFFECT_PARAMS parameters of its kind, may be NULL
 * @return A box_audio_effect_kind_t, BOX_AUDIO_EFFECT_NONE for an entity that is no effect
 */
uint32_t box_audio_module_effect_kind(
    const ecs_world_t *world,
    ecs_entity_t e,
    float *params);

/** Largest number of effect components */
#define BOX_AUDIO_EFFECT_COMPONENTS (17)

/** Get the effect components: Reverb, Equalizer, AudioFilter, Compressor, Gate, Delay, Phaser,
 * Chorus, Saturator, TransientShaper, FrequencyShifter, PitchShifter, PatternGate, Rotary,
 * BeatRepeat, TapeStop and Vocoder, in the order an entity that has several takes the first of.
 * A new effect is added here and to box_audio_module_effect_params.
 *
 * @param out Receives up to BOX_AUDIO_EFFECT_COMPONENTS components
 * @return Number of components
 */
int32_t box_audio_module_effect_components(
    ecs_entity_t *out);

/** Get the effect component of an entity: the first of box_audio_module_effect_components it
 * has, which gives its kind.
 *
 * @param world The world
 * @param e The entity
 * @return The component, 0 for an entity that is no effect
 */
ecs_entity_t box_audio_module_effect_component(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Build the parameters of an effect from a value of its component, the builder of the EFFECT
 * commands; the automation of the timeline runs it on a copy with an automated member.
 *
 * @param world The world, for the tempo of a synced Delay
 * @param component The effect component, such as ecs_id(AudioFilter)
 * @param ptr The value of the component, NULL for the kind alone
 * @param params Receives the BOX_AUDIO_EFFECT_PARAMS parameters, may be NULL
 * @return A box_audio_effect_kind_t, BOX_AUDIO_EFFECT_NONE for a component that is no effect
 */
uint32_t box_audio_module_effect_params(
    const ecs_world_t *world,
    ecs_entity_t component,
    const void *ptr,
    float *params);


/** Walk a member path, segments separated by dots and elements in brackets such as
 * bands[2].gain, with a meta cursor to a float member.
 *
 * @param world The world
 * @param component The component
 * @param ptr A value of the component
 * @param member The path
 * @param cur Receives the cursor at the member
 * @return NULL when the member is a float, else the reason, to free with ecs_os_free
 */
char* box_audio_member_cursor(
    const ecs_world_t *world,
    ecs_entity_t component,
    void *ptr,
    const char *member,
    ecs_meta_cursor_t *cur);

/** Resolve a float member of a bus, an effect or a macro that automation, modulators and the
 * maps of macros move: volume, pan, width and sends[i].level of an AudioBus or of the master, a
 * float member of the effect component of an effect entity, or the value of an AudioMacro,
 * which the maps of a macro may not move.
 *
 * @param world The world
 * @param state The state
 * @param target The entity with the AudioBus or the effect component
 * @param component AudioBus or the effect component
 * @param member Path of the member
 * @param out Receives the kind, entity, component, parameter and send of the target
 * @return NULL when the member can be moved, else the reason, to free with ecs_os_free
 */
char* box_audio_target_resolve(
    ecs_world_t *world,
    box_audio_module_t *state,
    ecs_entity_t target,
    ecs_entity_t component,
    const char *member,
    box_audio_target_t *out);

/** Send the AudioModulator entities to the modulators of the core: resolve their target, give
 * them a slot, send what changed and release what no longer runs. Each one that cannot run
 * logs one error that names it, until it changes. Sets whether the timeline keeps the beat
 * clock of the core, so it runs before box_audio_timeline_update.
 *
 * @param world The world
 * @param state The state, whose bus indices and effect slots are current
 * @param t The transport
 */
void box_audio_modulators_sync(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t);

/** Register the query and the observer of the modulators.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_modulators_init(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Free what the modulators hold.
 *
 * @param state The state
 */
void box_audio_modulators_fini(
    box_audio_module_t *state);

/** Describe what became of a modulator, for the audio command.
 *
 * @param world The world
 * @param e The modulator entity
 * @return A text that lives as long as the program
 */
const char* box_audio_modulator_status(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Find the index of the parameter of an effect a float member of its component becomes, by
 * writing two marks into copies of the component and looking for them in the params its
 * builder makes.
 *
 * @param world The world
 * @param component The effect component
 * @param ptr The value of the component
 * @param size Bytes of the component, at most BOX_AUDIO_AUTOMATION_COMPONENT
 * @param member The path of the member
 * @return The index, -1 when the member reaches no parameter as it is
 */
int32_t box_audio_effect_member_param(
    const ecs_world_t *world,
    ecs_entity_t component,
    const void *ptr,
    ecs_size_t size,
    const char *member);

/** Check whether a member of an effect is a frequency, which AudioModOctaves applies to: a
 * cutoff, a center, the frequency of a band or the damping of a Delay.
 *
 * @param component The effect component
 * @param member The path of the member
 * @return Whether it is a frequency
 */
bool box_audio_member_frequency(
    ecs_entity_t component,
    const char *member);

/** Get the macro slot of the core of an AudioMacro, as of the last box_audio_macros_sync.
 *
 * @param world The world
 * @param e The macro entity
 * @return The slot, -1 when it has none: disabled, past the limit or not looked at yet
 */
int32_t box_audio_macro_slot(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Send the AudioMacro entities and their AudioMacroMap children to the core: give each macro
 * a slot and send its value at once when its component changes; resolve each map, sort out the
 * maps of one member (the newer wins), give the others a slot, send what changed and release
 * what no longer runs. A macro past the limit, one with too many maps and a map that cannot
 * run log one error that names them, a map whose member a newer map takes one warning. Runs
 * before box_audio_modulators_sync and box_audio_timeline_update, which address the macros by
 * their slots.
 *
 * @param world The world
 * @param state The state, whose bus indices and effect slots are current
 * @param t The transport
 */
void box_audio_macros_sync(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t);

/** Register the queries and the observer of the macros.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_macros_init(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Free what the macros hold.
 *
 * @param state The state
 */
void box_audio_macros_fini(
    box_audio_module_t *state);

/** Describe what became of a macro or a map, for the audio command.
 *
 * @param world The world
 * @param e The macro or map entity
 * @return A text that lives as long as the program
 */
const char* box_audio_macro_status(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Give the effects that the insert slots of the channels name their slots of the pool and
 * their memory, and send their kind, place and parameters when they changed, into the batch of
 * the frame. A Disabled effect is sent as taken off, so it is bypassed with a fade.
 *
 * @param world The world
 * @param state The state, whose bus indices are current
 * @param t The transport
 */
void box_audio_effects_sync(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t);

/** Take the meters of the output, update the levels of every bus and write them into each
 * entity with AudioMeter: the master, a channel with a bus index, an effect with a slot.
 *
 * @param world The world
 * @param state The state, whose bus indices and effect slots are current
 * @param t The transport, NULL for none
 */
void box_audio_meters_update(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t);

/** Get the levels of a channel the module measured in the last frame, with or without an
 * AudioMeter.
 *
 * @param world The world
 * @param channel The master or a bus entity
 * @param meter Receives the levels, zero for an entity that is no channel
 * @return Whether the entity is a channel with a bus index
 */
bool box_audio_module_meter(
    const ecs_world_t *world,
    ecs_entity_t channel,
    AudioMeter *meter);

/** Register the sync system and the observers of the module.
 *
 * @param world The world
 * @param state The state, freed when the world is
 */
void box_audio_sync_init(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Register the observers that mark a synth sound for a new compile when a component of it
 * or of one of its layers changes.
 *
 * @param world The world
 */
void box_audio_synth_init(
    ecs_world_t *world);

/** Compile a synth sound into a patch: the root and its children with a SynthOscillator as
 * layers, the root first. Logs one error per problem: too many layers, a layer that would
 * never end, an envelope longer than the core plays.
 *
 * @param world The world
 * @param state The state, for the counters
 * @param e The root, with SynthSound
 * @param synth What was compiled for it, receives the patch
 * @return Whether the patch differs from the one compiled before
 */
bool box_audio_synth_compile(
    ecs_world_t *world,
    box_audio_module_t *state,
    ecs_entity_t e,
    AudioSynth *synth);

/** Look up the sound ids of the wavetables of the layers of a compiled synth sound in the
 * current output: the id of the Wavetable of each SynthTable.table, 0 while it has none.
 *
 * @param world The world
 * @param state The state
 * @param synth What was compiled for the sound, whose patch takes the ids
 * @return Whether an id of the patch changed
 */
bool box_audio_synth_tables(
    ecs_world_t *world,
    const box_audio_module_t *state,
    AudioSynth *synth);

/** Frames of a built-in wavetable.
 *
 * @param kind A WavetableKind
 * @return Frames, 1..BOX_AUDIO_TABLE_FRAMES_MAX
 */
int32_t box_audio_wavetable_frames(
    int32_t kind);

/** Build the mip levels of wavetable frames from their harmonics, in the layout the core reads
 * (BOX_AUDIO_TABLE_FLOATS): level k of each frame keeps the harmonics up to
 * BOX_AUDIO_TABLE_HARMONICS >> k, without DC, so a loaded table fits as well as a built-in one.
 *
 * @param frames Frames of the table, 1..BOX_AUDIO_TABLE_FRAMES_MAX
 * @param cosines Amplitude of the cosine of harmonic n of frame j at
 *        j (BOX_AUDIO_TABLE_HARMONICS + 1) + n, n 1..BOX_AUDIO_TABLE_HARMONICS
 * @param sines Amplitude of the sine of each harmonic, the same way
 * @return The floats, freed with ecs_os_free, NULL for an invalid count of frames
 */
float* box_audio_wavetable_build(
    int32_t frames,
    const double *cosines,
    const double *sines);

/** Samples per frame of the sound of a Wavetable: 0 for the default, else clamped.
 *
 * @param frame_size Wavetable.frame_size
 * @return Samples, BOX_AUDIO_WAVETABLE_FRAME_SIZE_MIN..BOX_AUDIO_WAVETABLE_FRAME_SIZE_MAX
 */
int32_t box_audio_wavetable_frame_size(
    int32_t frame_size);

/** Build a wavetable from the frames of a sound: cut into frames of frame_size samples, at
 * most BOX_AUDIO_TABLE_FRAMES_MAX, summed to mono, each without its DC, normalized to a peak
 * of 1 over all frames.
 *
 * A forward FFT band limits each frame to the harmonics under the Nyquist of a frame, at most
 * BOX_AUDIO_TABLE_HARMONICS; a frame_size that is not a power of 2 is resampled to the next
 * one first.
 *
 * @param data Frames of the sound, interleaved when it has more than one channel
 * @param frames Number of frames of the sound
 * @param channels Channels, 1 or more
 * @param frame_size Samples per frame, from box_audio_wavetable_frame_size
 * @param table_frames Receives the frames of the table
 * @param floats Receives the floats of the table, BOX_AUDIO_TABLE_FLOATS of its frames
 * @return The floats, freed with ecs_os_free, NULL when the sound is shorter than a frame
 */
float* box_audio_wavetable_load(
    const float *data,
    int32_t frames,
    int32_t channels,
    int32_t frame_size,
    int32_t *table_frames,
    int32_t *floats);

/** Build the table of a Wavetable: of its kind, or from its sound when it has one. A sound
 * that fails to load, is not a sound or is shorter than one frame logs one error that names
 * the Wavetable, one of more frames than a table holds one warning.
 *
 * @param world The world
 * @param e The Wavetable entity
 * @param table Its Wavetable
 * @param w What was sent for it
 * @param frames Receives the frames of the table
 * @param floats Receives the floats of the table
 * @param loading Receives whether the sound still decodes
 * @return The floats, freed with ecs_os_free, NULL for none
 */
float* box_audio_wavetable_of(
    ecs_world_t *world,
    ecs_entity_t e,
    const Wavetable *table,
    AudioWavetable *w,
    int32_t *frames,
    int32_t *floats,
    bool *loading);

/** Register the observers that build the Wavetables of a sound again when it becomes ready,
 * changes or is removed. Needs the query of the wavetables of the state.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_wavetable_init(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Build a built-in wavetable by additive synthesis.
 *
 * @param kind A WavetableKind
 * @param floats Receives the floats of the table, BOX_AUDIO_TABLE_FLOATS of its frames
 * @return The floats, freed with ecs_os_free
 */
float* box_audio_wavetable_kind(
    int32_t kind,
    int32_t *floats);

/** Register the sound file components' observers and the headless wait of their decodes.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_sample_init(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Free the samples of the module and drop its reference to the decodes in flight.
 *
 * @param state The state
 */
void box_audio_sample_fini(
    box_audio_module_t *state);

/** Get the sample of a sound entity with a SoundFile.
 *
 * @param world The world
 * @param sound The sound entity
 * @return The sample, NULL when the entity has no SoundFile
 */
box_audio_sample_t* box_audio_sample_of(
    const ecs_world_t *world,
    ecs_entity_t sound);

/** Get the decoded frames of a sound file, for the tests.
 *
 * @param world The world
 * @param sound The sound entity
 * @param frames Receives the number of frames
 * @param channels Receives 1 or 2
 * @param sample_rate Receives the rate of the file
 * @return The frames, NULL while the sound is not ready
 */
const float* box_audio_sample_frames(
    const ecs_world_t *world,
    ecs_entity_t sound,
    int32_t *frames,
    int32_t *channels,
    float *sample_rate);

/** Count the decoded copies of sound files the module holds, loading or not.
 *
 * @param world The world
 * @return Number of samples
 */
int32_t box_audio_sample_count(
    const ecs_world_t *world);

/** Count the sound ids in use in the core of the current output.
 *
 * @param world The world
 * @return Number of sounds sent and not yet freed
 */
int32_t box_audio_module_sounds(
    const ecs_world_t *world);

/** Make room in the batch of the frame for one more command.
 *
 * @param state The state
 * @return Whether the batch has room
 */
bool box_audio_reserve(
    box_audio_module_t *state);

/** Pick a free sound id of the core, or log once that every id is taken. The caller marks
 * it in sound_used.
 *
 * @param state The state
 * @return The id, 0 when every id is taken
 */
uint32_t box_audio_sound_id(
    box_audio_module_t *state);

/** Pick the handle of a new voice.
 *
 * @param state The state
 * @return The handle, never 0
 */
uint32_t box_audio_new_voice(
    box_audio_module_t *state);

/** Register the observers of AudioStream.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_music_init(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Free the streams of the module; the jobs in flight free what they hold.
 *
 * @param state The state
 */
void box_audio_music_fini(
    box_audio_module_t *state);

/** Forget the voices of the streams after the output changed; they start again from where
 * they were.
 *
 * @param state The state
 */
void box_audio_music_reset(
    box_audio_module_t *state);

/** Start, stop and update the voices of the streams, into the batch of the frame.
 *
 * @param world The world
 * @param state The state
 * @param t The transport, NULL for none
 * @param waiting Whether the page waits for a gesture
 */
void box_audio_music_update(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t,
    bool waiting);

/** After the batch went out: send the decoded chunks, submit decodes and write the status.
 *
 * @param world The world
 * @param state The state
 * @param t The transport, NULL for none
 */
void box_audio_music_send(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t);

/** Count the streams the module holds, retired ones that fade out included.
 *
 * @param world The world
 * @return Number of streams
 */
int32_t box_audio_music_count(
    const ecs_world_t *world);

/** Open an Ogg Vorbis file in memory, which the decoder takes.
 *
 * @param bytes The file, freed with ecs_os_free by the decoder, also on failure
 * @param size Bytes of the file
 * @param error Receives the error code of stb_vorbis on failure
 * @return The decoder, NULL when the bytes are no Ogg Vorbis stream
 */
box_audio_vorbis_t* box_audio_vorbis_open(
    uint8_t *bytes,
    int32_t size,
    int32_t *error);

/** Close a decoder and free its file.
 *
 * @param v The decoder, may be NULL
 */
void box_audio_vorbis_close(
    box_audio_vorbis_t *v);

/** Get the frames of the file of a decoder.
 *
 * @param v The decoder
 * @return Number of frames
 */
int32_t box_audio_vorbis_frames(
    const box_audio_vorbis_t *v);

/** Get the channels a decoder writes: 1 or 2, more mixed down to 2.
 *
 * @param v The decoder
 * @return Number of channels
 */
int32_t box_audio_vorbis_channels(
    const box_audio_vorbis_t *v);

/** Get the sample rate of the file of a decoder.
 *
 * @param v The decoder
 * @return Frames per second
 */
float box_audio_vorbis_rate(
    const box_audio_vorbis_t *v);

/** Get the frame of the file the next read starts at.
 *
 * @param v The decoder
 * @return The frame
 */
int64_t box_audio_vorbis_position(
    const box_audio_vorbis_t *v);

/** Seek a decoder to a frame of the file.
 *
 * @param v The decoder
 * @param frame The frame, wrapped into the file
 * @return Whether the seek succeeded
 */
bool box_audio_vorbis_seek(
    box_audio_vorbis_t *v,
    int64_t frame);

/** Decode frames with the pull API of stb_vorbis. A loop goes on with the first frame after the
 * last, without a gap.
 *
 * @param v The decoder
 * @param out Receives frames times channels floats, interleaved
 * @param frames Frames to decode
 * @param loop Whether the end goes on with the start
 * @return Frames decoded, fewer than frames only at the end of a file that does not loop
 */
int32_t box_audio_vorbis_read(
    box_audio_vorbis_t *v,
    float *out,
    int32_t frames,
    bool loop);

/** Read a file into memory.
 *
 * @param path The file
 * @param size Receives the bytes read, 0 for an empty file, -1 when it cannot be read
 * @return The bytes, NULL when the file cannot be read, is empty or too large
 */
uint8_t* box_audio_music_read_file(
    const char *path,
    int32_t *size);

/** Decode the file of a job on a worker: WAV or FLAC to float frames, more than two channels
 * mixed down to stereo, at the sample rate of the file.
 *
 * @param job The job, receives the frames or an error naming the file
 */
void box_audio_decode(
    box_audio_sample_job_t *job);

/** Write a capture as a 16 bit stereo WAV file, through a temporary file and one rename.
 * A capture of a song gets a BOX_AUDIO_WAV_SONG_CHUNK after its data.
 *
 * @param path The file
 * @param data Interleaved stereo frames
 * @param frames Number of frames
 * @param sample_rate Frames per second
 * @param song The beat and tempo the capture started at, NULL when no song played
 * @return 0 on success
 */
int box_audio_wav_write(
    const char *path,
    const float *data,
    int32_t frames,
    int32_t sample_rate,
    const box_audio_wav_song_t *song);

/** Set up the timeline of the module.
 *
 * @param state The state
 */
void box_audio_timeline_init(
    box_audio_module_t *state);

/** Free the timeline of the module.
 *
 * @param state The state
 */
void box_audio_timeline_fini(
    box_audio_module_t *state);

/** Once per frame, into the batch of the frame: follow the Transport (start, stop, seek,
 * tempo and loop changes), compute the lookahead window, schedule the metronome, call the
 * hooks and write the position from the clock. While a song swaps or loads the timeline
 * stays stopped, whatever Transport.playing says.
 *
 * @param world The world
 * @param state The state
 * @param t The transport
 * @param waiting Whether the page waits for a gesture, which schedules nothing
 */
void box_audio_timeline_update(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t,
    bool waiting);

/** Add a user of the timeline.
 *
 * @param state The state
 * @param hook The callbacks, copied
 * @return Whether there was room, BOX_AUDIO_TIMELINE_HOOKS
 */
bool box_audio_timeline_hook(
    box_audio_module_t *state,
    const box_audio_timeline_hook_t *hook);

/** Get the lookahead window of the last frame.
 *
 * @param world The world
 * @param count Receives the number of segments
 * @return The segments, NULL when there are none
 */
const box_audio_segment_t* box_audio_timeline_window(
    const ecs_world_t *world,
    int32_t *count);

/** Get the frame of a beat on the line of a piece: anchor frame + round((beat - anchor beat)
 * * frames_per_beat).
 *
 * @param anchor The piece
 * @param beat The beat
 * @return The frame, negative before the clock started
 */
int64_t box_audio_anchor_frame(
    const box_audio_anchor_t *anchor,
    double beat);

/** Get the beat at a frame on the line of a piece.
 *
 * @param anchor The piece
 * @param frame The frame
 * @return The beat
 */
double box_audio_anchor_beat(
    const box_audio_anchor_t *anchor,
    uint64_t frame);

/** Convert a frame of the core clock to a beat of the timeline, through the pieces up to the
 * end of the window, loop wraps included; a later frame goes on along the last piece.
 *
 * @param world The world
 * @param frame The frame
 * @return The beat, the position while the transport is stopped
 */
double box_audio_timeline_beat(
    const ecs_world_t *world,
    uint64_t frame);

/** Get the beat and the tempo of a play at a frame, which synced synth LFOs lock to: through
 * the pieces of the timeline while the Transport plays, its position and tempo while it is
 * stopped, beat 0 at 120 bpm without one.
 *
 * @param world The world
 * @param state The state
 * @param when Frame of the play, 0 or a past one for the clock
 * @param beat Receives the beat, wrapped into 0..BOX_AUDIO_BEAT_WRAP
 * @param tempo Receives the beats per second
 */
void box_audio_timeline_grid(
    const ecs_world_t *world,
    const box_audio_module_t *state,
    uint64_t when,
    float *beat,
    float *tempo);

/** Convert a beat of the timeline to a frame of the core clock: the first piece that plays the
 * beat at or after the clock, else the first that played it, else the line of the last piece.
 *
 * @param world The world
 * @param beat The beat
 * @return The frame, -1 while the transport is stopped
 */
int64_t box_audio_timeline_frame(
    const ecs_world_t *world,
    double beat);

/** Send the plays of box_audio_play_quantized, after the timeline followed the Transport of
 * the frame, each with the frame of its boundary as its when, then clear the plays. The
 * timeline does not remember them, so a stop or a seek of the transport leaves them be.
 *
 * A play starts on the first beat or bar boundary at or after the clock, through the pieces of
 * the window and the loop wraps after it; a loop without a boundary starts it at its first
 * wrap. Without a transport, or a stopped one, it starts at once.
 *
 * @param world The world
 * @param state The state
 * @param waiting Whether the page waits for a gesture
 */
void box_audio_timeline_quantized(
    ecs_world_t *world,
    box_audio_module_t *state,
    bool waiting);

/** Send a play of box_audio_play, box_audio_play_at, box_audio_play_quantized or an
 * AudioEmitter as a one shot.
 *
 * @param world The world
 * @param state The state
 * @param play The play
 * @param waiting Whether the page waits for a gesture, which drops it
 * @param when Frame of its first sample, 0 for now; a later play does not glide a legato voice
 * @return The handle of the voice, 0 when the play was dropped: a sound that is not ready, a
 * gesture that is awaited, a spatial play beyond max_distance
 */
uint32_t box_audio_play_send(
    ecs_world_t *world,
    box_audio_module_t *state,
    const box_audio_play_t *play,
    bool waiting,
    uint64_t when);

/** Mark the order dirty when the sidechain of a Compressor, or the modulator of a Vocoder, in
 * an insert slot changed since the last compile. Costs nothing while no Compressor and no
 * Vocoder exists and none had a sidechain.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_sidechains_check(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Find the sidechains of the compressors and the modulators of the vocoders in the insert
 * slots, after the routes of the compile are known: an edge from each key to the bus it ducks
 * or vocodes, so the key runs first. A sidechain that would form a cycle, or that is the
 * master, logs one error and keys on the own input; so does a modulator, or one that is the bus
 * of its vocoder, and the vocoder passes its input.
 *
 * @param world The world
 * @param state The state, with route_out and route_sends of this compile
 * @param keyed Receives the buses each bus index is the sidechain of, a bit each
 */
void box_audio_sidechains_compile(
    ecs_world_t *world,
    box_audio_module_t *state,
    uint64_t *keyed);

/** Get the bus index of a bus entity.
 *
 * @param world The world
 * @param state The state
 * @param bus The entity
 * @return The index, 0 when the entity is not a live bus with an index
 */
uint8_t box_audio_bus_of(
    ecs_world_t *world,
    const box_audio_module_t *state,
    ecs_entity_t bus);

/** Get the sound id of a sound entity in the current output.
 *
 * @param world The world
 * @param state The state
 * @param sound The sound entity
 * @return The id, 0 when the output does not have the sound
 */
uint32_t box_audio_sound_of(
    ecs_world_t *world,
    const box_audio_module_t *state,
    ecs_entity_t sound);

/** Check whether the SoundFile of a sound still decodes.
 *
 * @param world The world
 * @param sound The sound entity
 * @return Whether its sample is loading
 */
bool box_audio_sound_loading(
    ecs_world_t *world,
    ecs_entity_t sound);

/** Get the bus a sound plays on by default: the bus of its AudioOutput.
 *
 * @param world The world
 * @param state The state
 * @param sound The sound entity
 * @return The bus index, 0 for the master
 */
uint8_t box_audio_sound_bus(
    ecs_world_t *world,
    const box_audio_module_t *state,
    ecs_entity_t sound);

/** Check whether a sound is a legato synth sound.
 *
 * @param world The world
 * @param sound The sound entity
 * @param channel Receives its SynthGlide.channel, may be NULL
 * @return Whether it has SynthSound and a SynthGlide with legato
 */
bool box_audio_legato_of(
    ecs_world_t *world,
    ecs_entity_t sound,
    int32_t *channel);

/** Write a note of an Instrument, in instrument.c: the PLAY of its sound at the pitch ratio
 * 2^((pitch - root) / 12) and the gain velocity^velocity_curve from the start frame, and with
 * a gate its RELEASE at the end frame.
 *
 * A synth plays with BOX_AUDIO_PLAY_GATE when gated, a sampler with BOX_AUDIO_PLAY_ATTACK. A
 * note of a legato synth sound follows the voice of its channel with a LEGATO at the start
 * frame, which glides that voice on when it holds.
 *
 * The note is a one shot of the module, so a deleted bus reroutes it and its end is read. The
 * transport does not cancel it.
 *
 * @param world The world
 * @param state The state
 * @param instrument The entity with Instrument
 * @param pitch MIDI note number, fractions for microtones
 * @param velocity Velocity, 0..1
 * @param pan Pan of the voice, -1..1
 * @param start Frame of the core clock the note starts at
 * @param end Frame its release starts at, ignored without a gate
 * @param bus Bus index of the voice
 * @param handle Handle of the voice, 0 to pick a new one
 * @return The handle, 0 when the note did not go out: no instrument, a sound the output does
 *     not have or a full batch
 */
uint32_t box_audio_instrument_note(
    ecs_world_t *world,
    box_audio_module_t *state,
    ecs_entity_t instrument,
    float pitch,
    float velocity,
    float pan,
    uint64_t start,
    uint64_t end,
    uint8_t bus,
    uint32_t handle);

/** Write a note of an Instrument as box_audio_instrument_note does, with a gain on the gain
 * of the velocity and, when release is false, without the RELEASE, which the caller sends.
 *
 * @param world The world
 * @param state The state
 * @param instrument The entity with Instrument
 * @param pitch MIDI note number
 * @param velocity Velocity, 0..1
 * @param gain Linear gain times the gain of the velocity
 * @param pan Pan of the voice, -1..1
 * @param start Frame of the core clock the note starts at
 * @param end Frame its release starts at, ignored without a gate or a release
 * @param bus Bus index of the voice
 * @param handle Handle of the voice, 0 to pick a new one
 * @param release Whether a gated note writes its RELEASE at end
 * @param priority Priority of the voice, 0 for a one shot, the priority of a Track for its
 *     notes
 * @return The handle, 0 when the note did not go out
 */
uint32_t box_audio_instrument_voice(
    ecs_world_t *world,
    box_audio_module_t *state,
    ecs_entity_t instrument,
    float pitch,
    float velocity,
    float gain,
    float pan,
    uint64_t start,
    uint64_t end,
    uint8_t bus,
    uint32_t handle,
    bool release,
    uint8_t priority);

/** Seconds between the plays of an AudioEmitter at least */
#define BOX_AUDIO_EMITTER_INTERVAL_MIN (0.01)

/** Seconds a native or web output schedules the plays of emitters ahead of the frame, so a
 * batch arrives before their frame */
#define BOX_AUDIO_EMITTER_LOOKAHEAD (0.05)

/** Seconds a play of an emitter may be late, after a stall of the frame, before it is skipped
 * instead of sent at once */
#define BOX_AUDIO_EMITTER_LATE_MAX (0.1)

/** Register the query of the emitters.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_emitter_init(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Send the plays of the emitters that fall before the end of the next frame, each at its
 * frame of the core clock. An emitter that is not due costs one comparison.
 *
 * @param world The world
 * @param state The state
 * @param t The transport
 * @param waiting Whether the page waits for a gesture
 * @param delta Seconds of the frame, which the output renders next
 */
void box_audio_emitters_update(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t,
    bool waiting,
    float delta);

/** Forget a one shot that ended or is no longer tracked: its emitter has one play fewer that
 * sounds.
 *
 * @param world The world
 * @param state The state
 * @param shot The one shot
 */
void box_audio_oneshot_forget(
    ecs_world_t *world,
    const box_audio_module_t *state,
    const box_audio_oneshot_t *shot);

/** Point of an emitter shape in the space of the emitter, from three uniform numbers in [0, 1):
 * uniform over the area of a disc and the volume of the other shapes.
 *
 * @param shape The shape
 * @param radius Radius of disc, sphere and hemisphere, its absolute value is used
 * @param extents Half size of the box, their absolute values are used
 * @param u Three uniform numbers
 * @param out Receives the point
 */
void box_audio_emitter_point(
    AudioEmitterShape shape,
    float radius,
    const vec3 extents,
    const double u[3],
    vec3 out);

/** Register the observer that drops the spread note count of a removed Instrument.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_instrument_init(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Seconds of the longest ramp of a curved span of the expression of a note */
#define BOX_AUDIO_EXPRESS_STEP (0.01)

/** Give the curves of a note copies of their keys of its own, after a copy bit for bit.
 *
 * @param note The note
 */
void box_audio_note_dup(
    Note *note);

/** Free the keys of the curves of a note.
 *
 * @param note The note
 */
void box_audio_note_fini(
    Note *note);

/** Register the observers and the timeline hook of the clips and tracks.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_sequencer_init(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Free the clips and tracks of the module.
 *
 * @param state The state
 */
void box_audio_sequencer_fini(
    box_audio_module_t *state);

/** Register the observers and the timeline hook of the automation clips. After
 * box_audio_sequencer_init, whose query of the placements it shares.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_automation_init(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Free the automation lanes of the module.
 *
 * @param state The state
 */
void box_audio_automation_fini(
    box_audio_module_t *state);

/** Send the notes of box_audio_play_note, each at the clock of the output with its release
 * its duration later, on the bus of the AudioOutput of the instrument, else of its sound,
 * then clear them. Before a gesture and while the sound loads a note is dropped.
 *
 * @param world The world
 * @param state The state
 * @param waiting Whether the page waits for a gesture
 */
void box_audio_notes_send(
    ecs_world_t *world,
    box_audio_module_t *state,
    bool waiting);

/** Write a PLAY at a frame and remember its handle, so that a stop, a seek or a new output
 * cancels it while it has not started.
 *
 * @param state The state
 * @param sound Id of the sound
 * @param bus Bus of the voice
 * @param volume Volume
 * @param pitch Pitch ratio
 * @param when Frame of its first sample
 * @return The handle, 0 when the batch had no room
 */
uint32_t box_audio_timeline_play(
    box_audio_module_t *state,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pitch,
    uint64_t when);

#endif
