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

/** Bytes the module reserves in its batch before it writes a command */
#define BOX_AUDIO_COMMAND_RESERVE (256)

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
    ecs_entity_t send;      /**< AudioBus.send the order was compiled with */
    float volume;           /**< Volume sent */
    float pan;              /**< Pan sent */
    bool mute;              /**< Mute sent, a Disabled bus included */
    float send_level;       /**< Send level sent */
} AudioBusState;

/** What box.audio sent for a source */
typedef struct AudioVoice {
    uint32_t handle;        /**< Voice handle, 0 when the source has no voice */
    uint32_t serial;        /**< Serial of the output the voice plays on */
    uint32_t sound;         /**< Sound id the voice plays */
    bool loop;              /**< Whether the voice loops */
    float volume;           /**< Volume sent, 0 for a Disabled source */
    float pitch;            /**< Pitch sent */
    uint8_t bus;            /**< Bus index sent */
} AudioVoice;

extern ECS_COMPONENT_DECLARE(AudioSoundId);
extern ECS_COMPONENT_DECLARE(AudioBusState);
extern ECS_COMPONENT_DECLARE(AudioVoice);

/** A voice of box_audio_play, kept until it ends so a deleted bus can reroute it */
typedef struct box_audio_oneshot_t {
    uint32_t handle;        /**< Voice handle */
    uint8_t bus;            /**< Bus index */
} box_audio_oneshot_t;

/** A play of box_audio_play, sent with the next batch */
typedef struct box_audio_play_t {
    ecs_entity_t sound;     /**< The sound */
    float volume;           /**< The volume */
} box_audio_play_t;

/** Counters of the module, for the tests */
typedef struct box_audio_module_stats_t {
    int64_t batches;        /**< Batches handed to an output */
    int64_t commands;       /**< Commands written */
    int64_t bytes;          /**< Bytes of the batches */
    int64_t orders;         /**< ORDER commands written */
    int64_t cycles;         /**< Compiles that found a cycle */
    int64_t dropped;        /**< Plays dropped before a gesture */
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
    ecs_query_t *sources;         /**< AudioSource, disabled ones included */
    ecs_query_t *sounds;          /**< AudioPcm */
    uint32_t serial;              /**< Serial of the output the state was sent to */
    box_audio_writer_t writer;    /**< The batch of the frame, grown as needed */
    uint32_t next_voice;          /**< Next voice handle */
    uint8_t sound_used[BOX_AUDIO_SOUND_MAX];   /**< Sound ids in use in the core */
    ecs_entity_t bus_entity[BOX_AUDIO_BUS_MAX]; /**< Entity of each bus index, 0 for free */
    uint32_t bus_freed;           /**< Bus indices freed this frame, not reused before next */
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
    AudioSettings settings;       /**< Settings sent */
    bool settings_sent;           /**< settings holds what the output has */
    int64_t voices_stolen;        /**< Steals the core reported */
    box_audio_capture_job_t capture; /**< The capture that waits */
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

/** Register the sync system and the observers of the module.
 *
 * @param world The world
 * @param state The state, freed when the world is
 */
void box_audio_sync_init(
    ecs_world_t *world,
    box_audio_module_t *state);

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
