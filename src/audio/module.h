/**
 * @file audio/module.h
 * @brief The state of the box.audio module and its sync system, shared by its sources.
 */

#ifndef BOX_AUDIO_MODULE_H
#define BOX_AUDIO_MODULE_H

#include <box.h>
#include "output.h"

/** One shot voices the module keeps track of at most, the oldest are forgotten beyond this */
#define BOX_AUDIO_ONESHOT_MAX (1024)

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
    float pitch;            /**< Pitch sent */
    uint8_t bus;            /**< Bus index sent */
    bool parked;            /**< A looping spatial source out of range: no voice, its time
                             *   runs on from origin */
    double origin;          /**< Module time at which the sound was at its start, at pitch */
} AudioVoice;

/** What box.audio did with a Reverb */
typedef struct AudioReverbState {
    uint8_t slot;           /**< Slot of the reverb in the core plus 1, 0 when it has none */
    bool refused;           /**< The error that every slot is taken was logged */
} AudioReverbState;

/** What box.audio sent for a reverb slot */
typedef struct box_audio_reverb_sent_t {
    uint32_t serial;        /**< Serial of the output the parameters were sent to */
    uint32_t memory;        /**< Serial of the output the delay memory was given to */
    uint32_t bus;           /**< Bus index sent, BOX_AUDIO_BUS_NONE for none */
    box_audio_reverb_params_t params; /**< Parameters sent */
} box_audio_reverb_sent_t;

/** What box.audio compiled for a SynthSound */
typedef struct AudioSynth {
    box_audio_synth_patch_t patch;  /**< The patch of the last compile */
    bool compiled;          /**< The patch holds the components; false once one changed */
    bool pending;           /**< The patch changed since it was last sent */
} AudioSynth;

extern ECS_COMPONENT_DECLARE(AudioSoundId);
extern ECS_COMPONENT_DECLARE(AudioSynth);
extern ECS_COMPONENT_DECLARE(AudioBusState);
extern ECS_COMPONENT_DECLARE(AudioVoice);
extern ECS_COMPONENT_DECLARE(AudioReverbState);

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
    bool legato;            /**< The mono voice of a legato synth sound */
    int32_t channel;        /**< SynthGlide.channel of a legato voice */
} box_audio_oneshot_t;

/** A play of box_audio_play, sent with the next batch */
typedef struct box_audio_play_t {
    ecs_entity_t sound;     /**< The sound */
    float volume;           /**< The volume */
    bool spatial;           /**< A play of box_audio_play_at */
    float position[3];      /**< World position of a spatial play */
} box_audio_play_t;

/** Where a spatial source is heard from the listener */
typedef struct box_audio_place_t {
    float gain;             /**< Gain of the distance, 0 out of range */
    float pan;              /**< Pan, -1 left to 1 right */
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
    int64_t reverbs;        /**< REVERB commands written */
    int64_t not_ready;      /**< Plays dropped while their sound file loaded */
    int64_t decodes;        /**< Decodes of sound files submitted */
    int64_t music_jobs;     /**< Decode jobs of streams submitted */
    int64_t music_starts;   /**< Voices of streams started */
    int64_t synths;         /**< Compiles of synth sounds */
} box_audio_module_stats_t;

/** A capture that waits for its frames */
typedef struct box_audio_capture_job_t {
    float seconds;          /**< Seconds to record */
    char *path;             /**< File to write, NULL when no capture waits */
    uint32_t serial;        /**< Serial of the output that records, 0 before it started */
} box_audio_capture_job_t;

/** The state of the module in a world */
typedef struct box_audio_module_t {
    ecs_query_t *buses;           /**< AudioBus, disabled ones included */
    ecs_query_t *sources;         /**< AudioSource without AudioSpatial, disabled ones included */
    ecs_query_t *spatial;         /**< AudioSource with AudioSpatial, change detected */
    ecs_query_t *listeners;       /**< AudioListener with WorldTransform */
    ecs_query_t *views;           /**< Enabled views, for a camera that listens */
    ecs_query_t *sounds;          /**< AudioPcm */
    ecs_query_t *synths;          /**< SynthSound, prefabs included */
    ecs_query_t *reverbs;         /**< Reverb on an AudioBus, disabled ones included */
    ecs_entity_t reverb_entity[BOX_AUDIO_REVERB_MAX]; /**< Entity of each reverb slot */
    box_audio_reverb_sent_t reverb_sent[BOX_AUDIO_REVERB_MAX]; /**< What each slot got */
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
    uint64_t solo;                /**< Bus indices soloed this frame */
    uint64_t solo_safe;           /**< Bus indices of returns and SFX buses this frame */
    uint64_t audible;             /**< Bus indices the solo keeps, all when none is soloed */
    bool order_dirty;             /**< The buses or their routes changed */
    bool bus_overflow;            /**< The error of too many buses was logged */
    bool sound_overflow;          /**< The error of too many sounds was logged */
    uint8_t order[BOX_AUDIO_BUS_MAX]; /**< Bus indices of the last valid order, master last */
    int32_t order_count;          /**< Buses in order */
    ecs_map_t voices;             /**< Voice handle to source entity */
    ecs_vec_t oneshots;           /**< box_audio_oneshot_t */
    ecs_vec_t plays;              /**< box_audio_play_t */
    ecs_vec_t stops;              /**< uint32_t voice handles of deleted sources */
    ecs_vec_t releases;           /**< uint32_t sound ids */
    ecs_vec_t moved;              /**< ecs_entity_t spatial sources whose static transform moved */
    ecs_vec_t fades;              /**< uint32_t ids of deleted sounds whose one shots fade */
    ecs_vec_t samples;            /**< box_audio_sample_t*, the decoded sound files */
    uint64_t next_sample;         /**< Key of the next sample */
    box_audio_sample_sync_t *sample_sync; /**< Decodes in flight */
    ecs_query_t *sample_users;    /**< AudioSample, to update the status of its entities */
    ecs_query_t *music_query;     /**< AudioStream with AudioMusic, disabled ones included */
    ecs_vec_t music;              /**< box_audio_music_t*, the streams, retired ones included */
    uint64_t next_music;          /**< Key of the next stream */
    box_audio_music_t *music_active; /**< The stream of an entity, the one that may play */
    AudioSettings settings;       /**< Settings sent */
    bool settings_sent;           /**< settings holds what the output has */
    AudioBus master_bus;          /**< AudioBus of the master sent, its sends left empty */
    int64_t voices_stolen;        /**< Steals the core reported */
    box_audio_capture_job_t capture; /**< The capture that waits */
    box_audio_listener_t listener;   /**< The listener of the last frame */
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

/** Get the slot of the reverb of an entity in the core.
 *
 * @param world The world
 * @param e The entity with Reverb
 * @return The slot, -1 when it has none
 */
int32_t box_audio_module_reverb_slot(
    const ecs_world_t *world,
    ecs_entity_t e);

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
 *
 * @param path The file
 * @param data Interleaved stereo frames
 * @param frames Number of frames
 * @param sample_rate Frames per second
 * @return 0 on success
 */
int box_audio_wav_write(
    const char *path,
    const float *data,
    int32_t frames,
    int32_t sample_rate);

#endif
