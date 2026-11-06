/**
 * @file audio/song.c
 * @brief Song files: the Song singleton, AudioSong and AudioSongStatus, and the staged load of
 * one song at a time.
 *
 * A song is a managed script of the engine, box.audio.songs.<name>, evaluated in the root
 * scope like a scene; the scripts it includes are managed scripts next to it. Unloading clears
 * every script of the song, which deletes what each created, and resets box.audio.Song.
 *
 * A swap runs over several frames with the timeline stopped: the old song fades out and
 * unloads, the engine waits until every slot it held is reusable, then loads the new song.
 * A song that does not fit, parse or evaluate is unloaded at once and fails.
 */

#include "module.h"
#include "../app/watch.h"
#include "../render/preprocess.h"
#include <ctype.h>
#include <string.h>

/** Ancestors a walk up the hierarchy visits at most */
#define BOX_AUDIO_SONG_DEPTH (64)

/** Check whether an entity is a script of a song: a descendant of box.audio.songs.
 *
 * @param world The world
 * @param song The song
 * @param e The entity
 * @return Whether it is below the root of the songs
 */
static bool box_audio_song_is_script(
    const ecs_world_t *world,
    const box_audio_song_t *song,
    ecs_entity_t e)
{
    int32_t depth;
    bool result = false;
    for (depth = 0; e && !result && depth < BOX_AUDIO_SONG_DEPTH; depth ++) {
        if (!ecs_is_alive(world, e)) {
            break;
        }

        e = ecs_get_target(world, e, EcsChildOf, 0);
        result = e && e == song->root;
    }

    return result;
}

/** Check whether the song owns an entity: a script of the song created it or one of its
 * parents, or it is below the root of the songs.
 *
 * @param world The world
 * @param song The song
 * @param e The entity
 * @return Whether the song owns it
 */
static bool box_audio_song_owns(
    const ecs_world_t *world,
    const box_audio_song_t *song,
    ecs_entity_t e)
{
    int32_t depth;
    bool result = false;
    for (depth = 0; e && !result && depth < BOX_AUDIO_SONG_DEPTH; depth ++) {
        ecs_entity_t owner;
        if (!ecs_is_alive(world, e)) {
            break;
        }

        owner = ecs_get_target(world, e, ecs_id(EcsScript), 0);
        result = e == song->root || box_audio_song_is_script(world, song, e) ||
            (owner && box_audio_song_is_script(world, song, owner));
        e = ecs_get_target(world, e, EcsChildOf, 0);
    }

    return result;
}

/** Free the title and the lanes of a Song value and give it the defaults.
 *
 * @param value The value
 */
static void box_audio_song_value_reset(
    Song *value)
{
    SongLane *lanes = ecs_vec_first(&value->lanes);
    int32_t i;
    for (i = 0; i < ecs_vec_count(&value->lanes); i ++) {
        ecs_os_free(lanes[i].name);
    }

    ecs_vec_fini_t(NULL, &value->lanes, SongLane);
    ecs_os_free(value->title);
    *value = (Song){ .tempo = 120.0f, .beats_per_bar = 4, .beat_unit = 4, .loop = true };
}

/** Construct Song values: no title, 120 quarter notes per minute in 4/4, of unknown length,
 * looping, without lanes.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_song_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Song *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Song){ .tempo = 120.0f, .beats_per_bar = 4, .beat_unit = 4,
            .loop = true };
    }
}

/** Free Song values, their title and the names of their lanes.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_song_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Song *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        box_audio_song_value_reset(&values[i]);
    }
}

/** Copy Song values: the destination gets copies of the title, the lanes and their names.
 *
 * @param dst_ptr The destination components
 * @param src_ptr The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_song_copy(
    void *dst_ptr,
    const void *src_ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Song *dst = dst_ptr;
    const Song *src = src_ptr;
    SongLane *lanes;
    int32_t i, k;
    (void)ti;
    for (i = 0; i < count; i ++) {
        if (&dst[i] == &src[i]) {
            continue;
        }

        box_audio_song_value_reset(&dst[i]);
        dst[i] = src[i];
        dst[i].title = src[i].title ? ecs_os_strdup(src[i].title) : NULL;
        if (src[i].lanes.array) {
            dst[i].lanes = ecs_vec_copy_t(NULL, &src[i].lanes, SongLane);
        } else {
            ecs_os_memset_t(&dst[i].lanes, 0, ecs_vec_t);
        }

        lanes = ecs_vec_first(&dst[i].lanes);
        for (k = 0; k < ecs_vec_count(&dst[i].lanes); k ++) {
            lanes[k].name = lanes[k].name ? ecs_os_strdup(lanes[k].name) : NULL;
        }
    }
}

/** Move Song values: the destination takes the title and the lanes of the source.
 *
 * @param dst_ptr The destination components
 * @param src_ptr The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_audio_song_move(
    void *dst_ptr,
    void *src_ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Song *dst = dst_ptr, *src = src_ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        if (&dst[i] != &src[i]) {
            box_audio_song_value_reset(&dst[i]);
            dst[i] = src[i];
            src[i].title = NULL;
            ecs_os_memset_t(&src[i].lanes, 0, ecs_vec_t);
        }
    }
}

void box_audio_song_hooks(
    ecs_world_t *world)
{
    box_owned_hooks(world, SongLane, BOX_OWNED_STR(SongLane, name));
    ecs_set_hooks(world, Song, {
        .ctor = box_audio_song_ctor,
        .dtor = box_audio_song_dtor,
        .copy = box_audio_song_copy,
        .move = box_audio_song_move
    });
    box_owned_hooks(world, AudioSong, BOX_OWNED_STR(AudioSong, file));
    box_owned_hooks(world, AudioSongStatus, BOX_OWNED_STR(AudioSongStatus, file),
        BOX_OWNED_STR(AudioSongStatus, error));
}

/** Write AudioSongStatus.
 *
 * @param world The world
 * @param state The progress
 * @param file The file it is about, NULL for none
 * @param error Why the load failed, NULL for none
 * @param foreign Entities the song changed that it did not create
 */
static void box_audio_song_status(
    ecs_world_t *world,
    SongState state,
    const char *file,
    const char *error,
    int32_t foreign)
{
    AudioSongStatus *s = ecs_singleton_ensure(world, AudioSongStatus);
    s->state = state;
    ecs_os_strset(&s->file, file);
    ecs_os_strset(&s->error, error);
    s->foreign = foreign;
    ecs_singleton_modified(world, AudioSongStatus);
}

/** Remember the type of box.audio.Song before a load, which an unload returns it to.
 *
 * @param world The world
 * @param song The song
 */
static void box_audio_song_baseline(
    ecs_world_t *world,
    box_audio_song_t *song)
{
    const ecs_type_t *type = ecs_get_type(world, ecs_id(Song));
    int32_t i;
    ecs_vec_clear(&song->baseline);
    for (i = 0; type && i < type->count; i ++) {
        ecs_vec_append_t(NULL, &song->baseline, ecs_id_t)[0] = type->array[i];
    }
}

/** Reset Song to its default value and remove from box.audio.Song every component the song
 * added, before the entities of the song are deleted.
 *
 * @param world The world
 * @param song The song
 */
static void box_audio_song_entity_reset(
    ecs_world_t *world,
    box_audio_song_t *song)
{
    const ecs_type_t *type = ecs_get_type(world, ecs_id(Song));
    const ecs_id_t *base = ecs_vec_first(&song->baseline);
    ecs_vec_t extra;
    Song *value;
    int32_t i, k;
    ecs_vec_init_t(NULL, &extra, ecs_id_t, 0);
    for (i = 0; type && ecs_vec_count(&song->baseline) && i < type->count; i ++) {
        ecs_id_t id = type->array[i];
        bool known = id == ecs_id(Song);
        for (k = 0; !known && k < ecs_vec_count(&song->baseline); k ++) {
            known = base[k] == id;
        }

        if (!known) {
            ecs_vec_append_t(NULL, &extra, ecs_id_t)[0] = id;
        }
    }

    value = ecs_singleton_ensure(world, Song);
    box_audio_song_value_reset(value);
    ecs_singleton_modified(world, Song);
    for (i = 0; i < ecs_vec_count(&extra); i ++) {
        ecs_remove_id(world, ecs_id(Song), ecs_vec_get_t(&extra, ecs_id_t, i)[0]);
    }

    ecs_vec_fini_t(NULL, &extra, ecs_id_t);
}

/** Collect the managed scripts below an entity of the root of the songs.
 *
 * @param world The world
 * @param parent The entity
 * @param out Receives the script entities
 * @param depth Depth of the parent below the root
 */
static void box_audio_song_collect(
    ecs_world_t *world,
    ecs_entity_t parent,
    ecs_vec_t *out,
    int32_t depth)
{
    ecs_iter_t it = ecs_children(world, parent);
    ecs_vec_t children;
    int32_t i;
    ecs_vec_init_t(NULL, &children, ecs_entity_t, 0);
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            ecs_vec_append_t(NULL, &children, ecs_entity_t)[0] = it.entities[i];
        }
    }

    for (i = 0; i < ecs_vec_count(&children); i ++) {
        ecs_entity_t e = ecs_vec_get_t(&children, ecs_entity_t, i)[0];
        if (ecs_has(world, e, EcsScript)) {
            ecs_vec_append_t(NULL, out, ecs_entity_t)[0] = e;
        }

        if (depth < BOX_AUDIO_SONG_DEPTH) {
            box_audio_song_collect(world, e, out, depth + 1);
        }
    }

    ecs_vec_fini_t(NULL, &children, ecs_entity_t);
}

/** Watch the files of the scripts of the loaded song: the song file and those it includes.
 *
 * @param world The world
 * @param song The song
 */
static void box_audio_song_watch(
    ecs_world_t *world,
    box_audio_song_t *song)
{
    ecs_vec_t found;
    int32_t i;
    ecs_vec_init_t(NULL, &found, ecs_entity_t, 0);
    if (song->watch && song->script) {
        box_audio_song_collect(world, song->root, &found, 0);
    }

    for (i = 0; i < ecs_vec_count(&found); i ++) {
        ecs_entity_t e = ecs_vec_get_t(&found, ecs_entity_t, i)[0];
        const EcsScript *s = ecs_get(world, e, EcsScript);
        if (s && s->filename) {
            box_file_stamp_t stamp;
            box_file_stamp(s->filename, &stamp);
            box_file_watch_add(world, song->watch, song->script, s->filename, &stamp);
        }
    }

    ecs_vec_fini_t(NULL, &found, ecs_entity_t);
}

/** Collect the bus indices and the sound ids of the song in the current output.
 *
 * @param world The world
 * @param state The state
 * @param sounds Receives the sound ids
 * @return The bus indices, a bit each
 */
static uint64_t box_audio_song_slots(
    ecs_world_t *world,
    box_audio_module_t *state,
    ecs_vec_t *sounds)
{
    box_audio_song_t *song = &state->song;
    uint64_t result = 0;
    ecs_iter_t it = ecs_query_iter(world, state->buses);
    int32_t i;
    while (ecs_query_next(&it)) {
        const AudioBusState *s = ecs_field(&it, AudioBusState, 1);
        for (i = 0; i < it.count; i ++) {
            if (s[i].index && state->bus_entity[s[i].index] == it.entities[i] &&
                box_audio_song_owns(world, song, it.entities[i]))
            {
                result |= (uint64_t)1 << s[i].index;
            }
        }
    }

    it = ecs_query_iter(world, state->sounds);
    while (ecs_query_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            uint32_t id = box_audio_sound_of(world, state, it.entities[i]);
            if (id && box_audio_song_owns(world, song, it.entities[i])) {
                ecs_vec_append_t(NULL, sounds, uint32_t)[0] = id;
            }
        }
    }

    it = ecs_query_iter(world, state->synths);
    while (ecs_query_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            uint32_t id = box_audio_sound_of(world, state, it.entities[i]);
            if (id && box_audio_song_owns(world, song, it.entities[i])) {
                ecs_vec_append_t(NULL, sounds, uint32_t)[0] = id;
            }
        }
    }

    it = ecs_query_iter(world, state->sample_users);
    while (ecs_query_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            uint32_t id = box_audio_sound_of(world, state, it.entities[i]);
            if (id && box_audio_song_owns(world, song, it.entities[i])) {
                ecs_vec_append_t(NULL, sounds, uint32_t)[0] = id;
            }
        }
    }

    return result;
}

/** Check whether a sound id is in a list.
 *
 * @param ids The list of uint32_t
 * @param id The id
 * @return Whether the list has it
 */
static bool box_audio_song_has_id(
    const ecs_vec_t *ids,
    uint32_t id)
{
    const uint32_t *array = ecs_vec_first(ids);
    int32_t i;
    bool result = false;
    for (i = 0; !result && i < ecs_vec_count(ids); i ++) {
        result = array[i] == id;
    }

    return result;
}

/** Fade out the voices of the song: mute its buses, which ramps, and stop with the fade of a
 * stop every one shot and note that plays on one of its buses or plays one of its sounds. The
 * stopped voices are forgotten, so the delete of their bus does not send them to the master.
 *
 * @param world The world
 * @param state The state
 */
static void box_audio_song_silence(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    ecs_vec_t sounds;
    uint64_t buses;
    int32_t i;
    ecs_vec_init_t(NULL, &sounds, uint32_t, 0);
    buses = box_audio_song_slots(world, state, &sounds);
    state->song.silenced |= buses;
    for (i = ecs_vec_count(&state->oneshots) - 1; i >= 0; i --) {
        box_audio_oneshot_t *shot = ecs_vec_get_t(&state->oneshots, box_audio_oneshot_t, i);
        bool mine = (shot->bus && (buses & ((uint64_t)1 << shot->bus))) ||
            box_audio_song_has_id(&sounds, shot->sound);
        if (!mine) {
            continue;
        }

        if (state->serial && box_audio_reserve(state)) {
            box_audio_write_stop(&state->writer, shot->handle, 0);
        }

        box_audio_oneshot_forget(world, state, shot);
        ecs_vec_remove_ordered_t(&state->oneshots, box_audio_oneshot_t, i);
    }

    ecs_vec_fini_t(NULL, &sounds, uint32_t);
}

/** Unload the song: reset Song and box.audio.Song, clear every script of the song, which
 * deletes the entities each created, and delete the scripts.
 *
 * Remembers the slots it held that are not reusable yet: the bus indices freed this frame,
 * the effect slots the sync did not free yet and the sound ids released.
 *
 * @param world The world
 * @param state The state
 */
static void box_audio_song_drop(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    box_audio_song_t *song = &state->song;
    uint64_t freed = state->bus_freed;
    int32_t released = ecs_vec_count(&state->releases), i;
    const uint32_t *ids;
    ecs_vec_t scripts;
    bool any;
    ecs_vec_init_t(NULL, &scripts, ecs_entity_t, 0);
    box_audio_song_collect(world, song->root, &scripts, 0);
    any = song->script || ecs_vec_count(&scripts);
    if (!any) {
        goto done;
    }

    box_audio_song_silence(world, state);
    box_audio_song_entity_reset(world, song);
    if (song->script && song->watch) {
        box_file_watch_clear(world, song->watch, song->script);
    }

    song->dropping = true;
    for (i = 0; i < ecs_vec_count(&scripts); i ++) {
        ecs_entity_t e = ecs_vec_get_t(&scripts, ecs_entity_t, i)[0];
        if (ecs_is_alive(world, e)) {
            ecs_script_clear(world, e, 0);
        }
    }

    ecs_delete_with(world, ecs_pair(EcsChildOf, song->root));
    song->dropping = false;

    if (song->held_serial != state->serial) {
        song->held_buses = 0;
        ecs_os_memset_n(song->held_effects, 0, ecs_entity_t, BOX_AUDIO_EFFECT_MAX);
        ecs_vec_clear(&song->held_sounds);
    }

    song->held_serial = state->serial;
    song->held_buses |= state->bus_freed & ~freed;
    for (i = 0; i < BOX_AUDIO_EFFECT_MAX; i ++) {
        ecs_entity_t e = state->effects[i].entity;
        if (e && !ecs_is_alive(world, e)) {
            song->held_effects[i] = e;
        }
    }

    ids = ecs_vec_first(&state->releases);
    for (i = released; i < ecs_vec_count(&state->releases); i ++) {
        if (!box_audio_song_has_id(&song->held_sounds, ids[i])) {
            ecs_vec_append_t(NULL, &song->held_sounds, uint32_t)[0] = ids[i];
        }
    }

    ecs_os_strset(&song->held_file, song->file);
    song->stats.unloads ++;
done:
    ecs_os_free(song->file);
    ecs_os_free(song->code);
    song->file = NULL;
    song->code = NULL;
    song->script = 0;
    ecs_vec_fini_t(NULL, &scripts, ecs_entity_t);
}

/** Drop the slots of the unloaded song that are reusable again: bus indices no longer marked
 * freed this frame, effect slots the sync freed, sound ids whose free the core reported. A new
 * output knows none of them.
 *
 * @param state The state
 * @return Slots still held
 */
static int32_t box_audio_song_drain(
    box_audio_module_t *state)
{
    box_audio_song_t *song = &state->song;
    int32_t i, result = 0;
    if (song->held_serial != state->serial || !state->serial) {
        song->held_buses = 0;
        ecs_os_memset_n(song->held_effects, 0, ecs_entity_t, BOX_AUDIO_EFFECT_MAX);
        ecs_vec_clear(&song->held_sounds);
        goto done;
    }

    song->held_buses &= state->bus_freed;
    for (i = 0; i < BOX_AUDIO_EFFECT_MAX; i ++) {
        if (song->held_effects[i] && state->effects[i].entity != song->held_effects[i]) {
            song->held_effects[i] = 0;
        }

        result += song->held_effects[i] != 0;
    }

    for (i = 1; i < BOX_AUDIO_BUS_MAX; i ++) {
        result += (song->held_buses & ((uint64_t)1 << i)) != 0;
    }

    result += ecs_vec_count(&song->held_sounds);
done:
    return result;
}

void box_audio_song_sound_free(
    box_audio_module_t *state,
    uint32_t id)
{
    box_audio_song_t *song = &state->song;
    int32_t i;
    for (i = ecs_vec_count(&song->held_sounds) - 1; i >= 0; i --) {
        if (ecs_vec_get_t(&song->held_sounds, uint32_t, i)[0] == id) {
            ecs_vec_remove_t(&song->held_sounds, uint32_t, i);
        }
    }
}

/** Count the buses, effects and sounds the song will ask for that the module did not give it
 * yet, and compare them with the free slots. Runs right after the load, before the sync, so a
 * song that does not fit fails without an overflow.
 *
 * @param world The world
 * @param state The state
 * @return NULL when the song fits, else why not, to free with ecs_os_free
 */
static char* box_audio_song_fits(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    box_audio_song_t *song = &state->song;
    ecs_iter_t it = ecs_query_iter(world, state->buses);
    ecs_vec_t effects, samples;
    int32_t buses = 0, free_buses = 0, free_effects = 0, sounds = 0, free_sounds = 0, i, j;
    int32_t synths = 0, used_synths = 0;
    char *result = NULL;
    ecs_vec_init_t(NULL, &effects, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &samples, box_audio_sample_t*, 0);
    while (ecs_query_next(&it)) {
        const AudioBus *bus = ecs_field(&it, AudioBus, 0);
        for (i = 0; i < it.count; i ++) {
            if (!box_audio_song_owns(world, song, it.entities[i])) {
                continue;
            }

            buses += it.entities[i] != state->master;
            for (j = 0; j < BOX_AUDIO_EFFECT_SLOTS; j ++) {
                ecs_entity_t e = bus[i].effects[j];
                const ecs_entity_t *seen = ecs_vec_first(&effects);
                bool known = false;
                int32_t k;
                if (!e || !ecs_is_alive(world, e) || box_audio_module_effect_slot(world, e) >= 0 ||
                    box_audio_module_effect_kind(world, e, NULL) == BOX_AUDIO_EFFECT_NONE)
                {
                    continue;
                }

                for (k = 0; !known && k < ecs_vec_count(&effects); k ++) {
                    known = seen[k] == e;
                }

                if (!known) {
                    ecs_vec_append_t(NULL, &effects, ecs_entity_t)[0] = e;
                }
            }
        }
    }

    for (i = 1; i < BOX_AUDIO_BUS_MAX; i ++) {
        free_buses += !state->bus_entity[i] && !(state->bus_freed & ((uint64_t)1 << i));
    }

    if (buses > free_buses) {
        result = flecs_asprintf("song '%s' needs %d buses, more than the %d of the %d buses of "
            "the engine that are free", song->target, buses, free_buses, BOX_AUDIO_BUS_MAX - 1);
        goto done;
    }

    if (!state->serial) {
        goto done;
    }

    for (i = 0; i < BOX_AUDIO_EFFECT_MAX; i ++) {
        free_effects += !state->effects[i].entity;
    }

    if (ecs_vec_count(&effects) > free_effects) {
        result = flecs_asprintf("song '%s' needs %d effects, more than the %d of the %d effects "
            "of the engine that are free", song->target, ecs_vec_count(&effects), free_effects,
            BOX_AUDIO_EFFECT_MAX);
        goto done;
    }

    it = ecs_query_iter(world, state->sounds);
    while (ecs_query_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            sounds += box_audio_song_owns(world, song, it.entities[i]) &&
                !box_audio_sound_of(world, state, it.entities[i]);
        }
    }

    it = ecs_query_iter(world, state->synths);
    while (ecs_query_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            bool given = box_audio_sound_of(world, state, it.entities[i]) != 0;
            if (box_audio_song_owns(world, song, it.entities[i])) {
                synths += !given;
            } else {
                used_synths += given;
            }
        }
    }

    sounds += synths;
    if (synths > BOX_AUDIO_SYNTH_MAX - used_synths) {
        result = flecs_asprintf("song '%s' needs %d synth sounds, more than the %d of the %d "
            "synths of the engine that are free", song->target, synths,
            BOX_AUDIO_SYNTH_MAX - used_synths, BOX_AUDIO_SYNTH_MAX);
        goto done;
    }

    it = ecs_query_iter(world, state->wavetables);
    while (ecs_query_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            sounds += box_audio_song_owns(world, song, it.entities[i]);
        }
    }

    it = ecs_query_iter(world, state->sample_users);
    while (ecs_query_next(&it)) {
        const AudioSample *s = ecs_field(&it, AudioSample, 0);
        for (i = 0; i < it.count; i ++) {
            box_audio_sample_t *sample = s[i].sample;
            box_audio_sample_t **seen = ecs_vec_first(&samples);
            bool known = false;
            if (!sample || (sample->id && sample->serial == state->serial) ||
                !box_audio_song_owns(world, song, it.entities[i]))
            {
                continue;
            }

            for (j = 0; !known && j < ecs_vec_count(&samples); j ++) {
                known = seen[j] == sample;
            }

            if (!known) {
                ecs_vec_append_t(NULL, &samples, box_audio_sample_t*)[0] = sample;
                sounds ++;
            }
        }
    }

    for (i = 1; i < BOX_AUDIO_SOUND_MAX; i ++) {
        free_sounds += !state->sound_used[i];
    }

    if (sounds > free_sounds) {
        result = flecs_asprintf("song '%s' needs %d sounds, more than the %d of the %d sounds of "
            "the engine that are free", song->target, sounds, free_sounds,
            BOX_AUDIO_SOUND_MAX - 1);
    }

done:
    ecs_vec_fini_t(NULL, &effects, ecs_entity_t);
    ecs_vec_fini_t(NULL, &samples, box_audio_sample_t*);

    return result;
}

/** Check that the sync gave the song every slot it asked for, once its sound files are ready.
 *
 * @param world The world
 * @param state The state
 * @return NULL when every bus, effect and sound of the song has a slot, else why not, to free
 * with ecs_os_free
 */
static char* box_audio_song_allocated(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    box_audio_song_t *song = &state->song;
    ecs_iter_t it = ecs_query_iter(world, state->buses);
    int32_t buses = 0, missing_buses = 0, effects = 0, missing_effects = 0, sounds = 0;
    int32_t missing_sounds = 0, i, j;
    char *result = NULL;
    while (ecs_query_next(&it)) {
        const AudioBus *bus = ecs_field(&it, AudioBus, 0);
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            if (e == state->master || !box_audio_song_owns(world, song, e)) {
                continue;
            }

            buses ++;
            missing_buses += !box_audio_module_bus_index(world, e);
            for (j = 0; state->serial && j < BOX_AUDIO_EFFECT_SLOTS; j ++) {
                ecs_entity_t fx = bus[i].effects[j];
                const AudioEffectState *es = fx && ecs_is_alive(world, fx) ?
                    ecs_get(world, fx, AudioEffectState) : NULL;
                if (es && box_audio_module_effect_kind(world, fx, NULL) !=
                    BOX_AUDIO_EFFECT_NONE)
                {
                    effects ++;
                    missing_effects += box_audio_module_effect_slot(world, fx) < 0 &&
                        es->refused;
                }
            }
        }
    }

    if (missing_buses) {
        result = flecs_asprintf("song '%s' needs %d buses, %d more than the %d buses of the "
            "engine had free", song->target, buses, missing_buses, BOX_AUDIO_BUS_MAX - 1);
        goto done;
    }

    if (missing_effects) {
        result = flecs_asprintf("song '%s' needs %d effects, %d more than the %d effects of the "
            "engine had free", song->target, effects, missing_effects, BOX_AUDIO_EFFECT_MAX);
        goto done;
    }

    if (!state->serial) {
        goto done;
    }

    it = ecs_query_iter(world, state->sounds);
    while (ecs_query_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            if (box_audio_song_owns(world, song, it.entities[i])) {
                sounds ++;
                missing_sounds += !box_audio_sound_of(world, state, it.entities[i]);
            }
        }
    }

    it = ecs_query_iter(world, state->synths);
    while (ecs_query_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            if (box_audio_song_owns(world, song, it.entities[i])) {
                sounds ++;
                missing_sounds += !box_audio_sound_of(world, state, it.entities[i]);
            }
        }
    }

    it = ecs_query_iter(world, state->sample_users);
    while (ecs_query_next(&it)) {
        const AudioSample *s = ecs_field(&it, AudioSample, 0);
        for (i = 0; i < it.count; i ++) {
            if (s[i].sample && s[i].sample->state == SoundReady &&
                box_audio_song_owns(world, song, it.entities[i]))
            {
                sounds ++;
                missing_sounds += !box_audio_sound_of(world, state, it.entities[i]);
            }
        }
    }

    if (missing_sounds) {
        result = flecs_asprintf("song '%s' needs %d sounds, %d more than the %d sounds of the "
            "engine had free", song->target, sounds, missing_sounds, BOX_AUDIO_SOUND_MAX - 1);
    }

done:
    return result;
}

/** Check whether a sound file of the song still decodes.
 *
 * @param world The world
 * @param state The state
 * @return Whether one is loading
 */
static bool box_audio_song_decoding(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    ecs_iter_t it = ecs_query_iter(world, state->sample_users);
    bool result = false;
    int32_t i;
    while (ecs_query_next(&it)) {
        const AudioSample *s = ecs_field(&it, AudioSample, 0);
        for (i = 0; i < it.count && !result; i ++) {
            result = s[i].sample && s[i].sample->state == SoundLoading &&
                box_audio_song_owns(world, &state->song, it.entities[i]);
        }

        if (result) {
            ecs_iter_fini(&it);
            break;
        }
    }

    return result;
}

/** Apply the Song the song file set to the Transport: tempo, time signature, position 0 and a
 * loop over its beats. Transport.playing stays as the host set it.
 *
 * @param world The world
 */
static void box_audio_song_transport(
    ecs_world_t *world)
{
    const Song *s = ecs_singleton_get(world, Song);
    Transport *tr = ecs_singleton_ensure(world, Transport);
    if (s) {
        tr->tempo = s->tempo > 0 ? s->tempo : 120.0f;
        tr->beats_per_bar = s->beats_per_bar > 0 ? s->beats_per_bar : 4;
        tr->beat_unit = s->beat_unit > 0 ? s->beat_unit : 4;
        tr->loop = s->loop && s->beats > 0;
        tr->loop_start = 0;
        tr->loop_end = s->beats > 0 ? s->beats : 0;
    }

    tr->position = 0;
    ecs_singleton_modified(world, Transport);
}

/** Fail the load of the target: log the error, unload what loaded and write the status.
 *
 * @param world The world
 * @param state The state
 * @param error Why
 */
static void box_audio_song_fail(
    ecs_world_t *world,
    box_audio_module_t *state,
    const char *error)
{
    box_audio_song_t *song = &state->song;
    ecs_err("box.audio: %s", error);
    box_audio_song_drop(world, state);
    song->hold = false;
    song->stats.failures ++;
    box_audio_song_status(world, SongFailed, song->target, error, 0);
}

/** Check whether a table holds only what a script gives an entity it creates: a name, a
 * parent with its depth and the prefab tag.
 *
 * @param world The world
 * @param table The table of the entity before the script took it, NULL for a new entity
 * @return Whether the entity is new
 */
static bool box_audio_song_fresh(
    const ecs_world_t *world,
    ecs_table_t *table)
{
    const ecs_type_t *type = table ? ecs_table_get_type(table) : NULL;
    int32_t i;
    bool result = true;
    for (i = 0; type && result && i < type->count; i ++) {
        ecs_id_t id = type->array[i];
        if (ECS_IS_PAIR(id)) {
            ecs_entity_t first = ecs_pair_first(world, id);
            result = first == ecs_id(EcsIdentifier) || first == EcsChildOf ||
                first == EcsParentDepth;
        } else {
            result = id == EcsPrefab || id == ecs_id(EcsParent);
        }
    }

    return result;
}

/** Remember an entity the song changed that it did not create.
 *
 * @param song The song
 * @param e The entity
 */
static void box_audio_song_foreign_add(
    box_audio_song_t *song,
    ecs_entity_t e)
{
    const ecs_entity_t *seen = ecs_vec_first(&song->foreign);
    int32_t i;
    bool known = false;
    for (i = 0; !known && i < ecs_vec_count(&song->foreign); i ++) {
        known = seen[i] == e;
    }

    if (!known) {
        ecs_vec_append_t(NULL, &song->foreign, ecs_entity_t)[0] = e;
    }
}

/** During a load, note the components a song sets on entities it did not create: a host
 * entity it names, a singleton such as the Transport. box.audio.Song is the song's to set.
 *
 * A host script that reads Song reactively evaluates again while the song loads, so what this
 * notes on entities of other scripts is checked against the code of the song after the load.
 *
 * @param it The iterator of the observer
 */
static void BoxAudioSongChange(
    ecs_iter_t *it)
{
    box_audio_module_t *state = it->ctx;
    box_audio_song_t *song = &state->song;
    int32_t i;
    if (!song->loading || ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (e == ecs_id(Song) || ecs_has_id(it->world, e, EcsObserver) ||
            ecs_has_id(it->world, e, EcsQuery) || box_audio_song_owns(it->world, song, e))
        {
            continue;
        }

        box_audio_song_foreign_add(song, e);
    }

done:
    return;
}

/** Check whether the code of a script of the song names an entity: its name appears in the
 * code as a whole identifier.
 *
 * @param world The world
 * @param scripts The script entities of the song
 * @param e The entity
 * @return Whether a script names it
 */
static bool box_audio_song_names(
    ecs_world_t *world,
    const ecs_vec_t *scripts,
    ecs_entity_t e)
{
    const char *name = ecs_get_name(world, e);
    int32_t len = name ? ecs_os_strlen(name) : 0, i;
    bool result = false;
    for (i = 0; len && !result && i < ecs_vec_count(scripts); i ++) {
        const EcsScript *s = ecs_get(world, ecs_vec_get_t(scripts, ecs_entity_t, i)[0],
            EcsScript);
        const char *code = s ? s->code : NULL, *at = code;
        while (at && !result && (at = strstr(at, name))) {
            char before = at > code ? at[-1] : ' ', after = at[len];
            result = !(isalnum((unsigned char)before) || before == '_') &&
                !(isalnum((unsigned char)after) || after == '_');
            at += len;
        }
    }

    return result;
}

/** Drop the entities the song did not change from the ones the load noted: those of other
 * scripts that the code of the song does not name, which a reactive host script changed.
 *
 * @param world The world
 * @param song The song
 */
static void box_audio_song_foreign_filter(
    ecs_world_t *world,
    box_audio_song_t *song)
{
    ecs_vec_t scripts;
    int32_t i;
    ecs_vec_init_t(NULL, &scripts, ecs_entity_t, 0);
    box_audio_song_collect(world, song->root, &scripts, 0);
    for (i = ecs_vec_count(&song->foreign) - 1; i >= 0; i --) {
        ecs_entity_t f = ecs_vec_get_t(&song->foreign, ecs_entity_t, i)[0];
        ecs_entity_t owner = ecs_is_alive(world, f) ?
            ecs_get_target(world, f, ecs_id(EcsScript), 0) : 0;
        if (owner && !box_audio_song_is_script(world, song, owner) &&
            !box_audio_song_names(world, &scripts, f))
        {
            ecs_vec_remove_t(&song->foreign, ecs_entity_t, i);
        }
    }

    ecs_vec_fini_t(NULL, &scripts, ecs_entity_t);
}

/** Follow the entities that the scripts of the song take and give up.
 *
 * During a load an entity the song takes that existed before is one it did not create.
 * Outside a load and an unload a script of the song was evaluated again by someone else,
 * which the engine follows with a reload of the song.
 *
 * @param it The iterator of the observer
 */
static void BoxAudioSongOwner(
    ecs_iter_t *it)
{
    box_audio_module_t *state = it->ctx;
    box_audio_song_t *song = &state->song;
    ecs_id_t id = ecs_field_id(it, 0);
    ecs_entity_t script;
    int32_t i;
    if (ecs_is_fini(it->world) || song->dropping || !ECS_IS_PAIR(id)) {
        goto done;
    }

    script = ecs_pair_second(it->world, id);
    if (!script || !box_audio_song_is_script(it->world, song, script)) {
        goto done;
    }

    if (!song->loading) {
        song->reload |= song->script != 0 && song->stage != BOX_AUDIO_SONG_FADE &&
            song->stage != BOX_AUDIO_SONG_DRAIN;
        goto done;
    }

    if (it->event != EcsOnAdd || box_audio_song_fresh(it->world, it->other_table)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        box_audio_song_foreign_add(song, it->entities[i]);
    }

done:
    return;
}

/** The file of the song or of a script it includes changed on disk: reload the song.
 *
 * @param world The world
 * @param ctx The state
 * @param owner The script entity of the song
 */
static void box_audio_song_changed(
    ecs_world_t *world,
    void *ctx,
    ecs_entity_t owner)
{
    box_audio_module_t *state = ctx;
    (void)world;
    if (owner && owner == state->song.script) {
        state->song.reload = true;
    }
}

/** Get the name of the script entity of a song file: its file name without the extension.
 *
 * @param file The file
 * @return The name, to free with ecs_os_free
 */
static char* box_audio_song_name(
    const char *file)
{
    const char *slash = strrchr(file, '/'), *start = slash ? slash + 1 : file;
    const char *dot = strrchr(start, '.');
    int32_t len = dot && dot != start ? (int32_t)(dot - start) : ecs_os_strlen(start);
    char *result;
    if (!len) {
        result = ecs_os_strdup("song");
    } else {
        result = ecs_os_malloc(len + 1);
        ecs_os_memcpy(result, start, len);
        result[len] = '\0';
    }

    return result;
}

/** Load the target: evaluate the song file as a managed script of the engine in the root
 * scope, note the entities it changed that it did not create, check that it fits and apply it
 * to the Transport. A song that fails to load, parse, evaluate or fit is unloaded and fails.
 *
 * @param world The world
 * @param state The state
 */
static void box_audio_song_load_script(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    box_audio_song_t *song = &state->song;
    char *path = box_shader_locate(song->target), *name = NULL, *error = NULL;
    ecs_entity_t scope, e;
    const EcsScript *loaded;
    int32_t i;
    if (!path) {
        error = flecs_asprintf("cannot find song file '%s'", song->target);
        box_audio_song_fail(world, state, error);
        goto done;
    }

    if (!song->watch) {
        song->watch = box_file_watch_kind(world, "song script", box_audio_song_changed, state);
    }

    song->hold = true;
    box_audio_song_status(world, SongLoading, song->target, NULL, 0);
    box_audio_song_baseline(world, song);
    ecs_vec_clear(&song->foreign);
    name = box_audio_song_name(song->target);
    scope = ecs_set_scope(world, 0);
    e = ecs_entity(world, { .parent = song->root, .name = name, .sep = "/" });
    song->script = e;
    ecs_os_strset(&song->file, song->target);
    song->observer = ecs_observer(world, {
        .query.terms = {{ .id = EcsWildcard, .src.id = EcsSelf }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnAdd, EcsOnSet },
        .callback = BoxAudioSongChange,
        .ctx = state
    });
    song->loading = true;
    if (song->next_code) {
        ecs_os_strset(&ecs_ensure(world, e, EcsScript)->filename, path);
        ecs_script_update(world, e, 0, song->next_code);
        ecs_os_free(song->next_code);
        song->next_code = NULL;
    } else {
        ecs_script(world, { .entity = e, .filename = path });
    }

    song->loading = false;
    ecs_delete(world, song->observer);
    song->observer = 0;
    ecs_set_scope(world, scope);
    song->stats.loads ++;

    loaded = ecs_is_alive(world, e) ? ecs_get(world, e, EcsScript) : NULL;
    if (!loaded || !loaded->script || loaded->error) {
        error = loaded && loaded->error
            ? flecs_asprintf("song '%s' failed to load: %s", song->target, loaded->error)
            : flecs_asprintf("song '%s' failed to load", song->target);
        box_audio_song_fail(world, state, error);
        goto done;
    }

    box_audio_song_foreign_filter(world, song);
    song->dropping = true;
    for (i = 0; i < ecs_vec_count(&song->foreign); i ++) {
        ecs_entity_t f = ecs_vec_get_t(&song->foreign, ecs_entity_t, i)[0];
        char *fpath = ecs_is_alive(world, f) ? ecs_get_path(world, f) : NULL;
        ecs_warn("box.audio: song '%s' changes '%s', which it did not create; the change "
            "stays after the song unloads", song->target, fpath ? fpath : "?");
        ecs_os_free(fpath);
        if (ecs_is_alive(world, f)) {
            ecs_entity_t owner = ecs_get_target(world, f, ecs_id(EcsScript), 0);
            if (owner && box_audio_song_is_script(world, song, owner)) {
                ecs_remove_pair(world, f, ecs_id(EcsScript), owner);
            }
        }
    }

    song->dropping = false;

    error = box_audio_song_fits(world, state);
    if (error) {
        box_audio_song_fail(world, state, error);
        goto done;
    }

    loaded = ecs_get(world, e, EcsScript);
    ecs_os_strset(&song->code, loaded ? loaded->code : NULL);
    box_audio_song_watch(world, song);
    box_audio_song_transport(world);
    box_audio_song_status(world, SongLoading, song->target, NULL,
        ecs_vec_count(&song->foreign));
    song->stage = BOX_AUDIO_SONG_DECODE;
    song->settled = false;
done:
    ecs_os_free(name);
    ecs_os_free(path);
    ecs_os_free(error);
}

/** Start to work towards a new target, or towards the same one again after a reload: a
 * loaded song fades out first; without one the next step loads at once.
 *
 * @param world The world
 * @param state The state
 */
static void box_audio_song_begin(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    box_audio_song_t *song = &state->song;
    song->pending = true;
    if (song->stage == BOX_AUDIO_SONG_FADE || song->stage == BOX_AUDIO_SONG_DRAIN) {
        goto done;
    }

    if (song->script) {
        song->stage = BOX_AUDIO_SONG_FADE;
        song->elapsed = 0;
        song->frames = 0;
        song->hold = true;
        box_audio_song_silence(world, state);
        box_audio_song_status(world, SongLoading, song->target, NULL, 0);
    } else {
        song->stage = BOX_AUDIO_SONG_IDLE;
    }

done:
    return;
}

/** Take the next step of the load: end the fade with the unload, end the wait for the slots,
 * load the target, and make a song whose sound files decoded ready.
 *
 * @param world The world
 * @param state The state
 * @param dt Seconds of the frame
 */
static void box_audio_song_step(
    ecs_world_t *world,
    box_audio_module_t *state,
    double dt)
{
    box_audio_song_t *song = &state->song;
    char *error = NULL;
    if (song->stage == BOX_AUDIO_SONG_FADE) {
        if (song->frames > 0 && song->elapsed >= BOX_AUDIO_SONG_FADE_TIME) {
            box_audio_song_drop(world, state);
            song->stage = BOX_AUDIO_SONG_DRAIN;
            song->elapsed = 0;
            song->frames = 0;
        } else {
            song->frames ++;
            song->elapsed += dt;
        }

        goto done;
    }

    if (song->stage == BOX_AUDIO_SONG_DRAIN) {
        if (song->frames > 0 && !box_audio_song_drain(state)) {
            song->stage = BOX_AUDIO_SONG_IDLE;
        } else if (song->elapsed > BOX_AUDIO_SONG_TIMEOUT) {
            error = flecs_asprintf("the slots of song '%s' are not free after %.1f s: %d "
                "remain, '%s' is not loaded", song->held_file ? song->held_file : "",
                song->elapsed, box_audio_song_drain(state),
                song->target ? song->target : "");
            song->held_buses = 0;
            ecs_os_memset_n(song->held_effects, 0, ecs_entity_t, BOX_AUDIO_EFFECT_MAX);
            ecs_vec_clear(&song->held_sounds);
            song->stage = BOX_AUDIO_SONG_IDLE;
            song->pending = false;
            box_audio_song_fail(world, state, error);
            goto done;
        } else {
            song->frames ++;
            song->elapsed += dt;
            song->stats.waits ++;
            goto done;
        }
    }

    if (song->stage == BOX_AUDIO_SONG_IDLE && song->pending && !song->script) {
        song->pending = false;
        if (song->target) {
            box_audio_song_load_script(world, state);
        } else {
            song->hold = false;
            box_audio_song_status(world, SongNone, NULL, NULL, 0);
        }
    }

    if (song->stage == BOX_AUDIO_SONG_DECODE) {
        if (box_audio_song_decoding(world, state)) {
            song->settled = false;
        } else if (!song->settled) {
            song->settled = true;
        } else {
            song->stage = BOX_AUDIO_SONG_IDLE;
            error = box_audio_song_allocated(world, state);
            if (error) {
                box_audio_song_fail(world, state, error);
                song->stage = BOX_AUDIO_SONG_DRAIN;
                song->elapsed = 0;
                song->frames = 0;
            } else {
                song->hold = false;
                box_audio_song_status(world, SongReady, song->target, NULL,
                    ecs_vec_count(&song->foreign));
            }
        }
    }

done:
    ecs_os_free(error);
}

/** Check whether two files name the same song.
 *
 * @param a A file, NULL for none
 * @param b A file, NULL for none
 * @return Whether both are none or equal
 */
static bool box_audio_song_same(
    const char *a,
    const char *b)
{
    return (!a && !b) || (a && b && !strcmp(a, b));
}

/** The song system: follow AudioSong, the reloads of the song and the stages of a swap.
 *
 * @param it The iterator of the system
 */
static void BoxAudioSongUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    box_audio_module_t *state = it->ctx;
    box_audio_song_t *song = &state->song;
    const AudioSong *as = ecs_singleton_get(world, AudioSong);
    const char *want = as && as->file && as->file[0] ? as->file : NULL;
    const EcsScript *script;
    if (!box_audio_song_same(want, song->target)) {
        ecs_os_free(song->next_code);
        song->next_code = NULL;
        song->reload = false;
        ecs_os_strset(&song->target, want);
        box_audio_song_begin(world, state);
    } else if (song->reload) {
        script = song->script && ecs_is_alive(world, song->script) ?
            ecs_get(world, song->script, EcsScript) : NULL;
        if (script && script->code && (!song->code || strcmp(script->code, song->code))) {
            ecs_os_strset(&song->next_code, script->code);
        }

        song->stats.reloads ++;
        song->reload = false;
        box_audio_song_begin(world, state);
    }

    box_audio_song_step(world, state, (double)it->delta_time);
}

int32_t box_audio_song_held(
    const ecs_world_t *world)
{
    box_audio_module_t *state = box_audio_module_get(world);
    int32_t result = 0, i;
    if (!state || state->song.stage != BOX_AUDIO_SONG_DRAIN) {
        goto done;
    }

    for (i = 0; i < BOX_AUDIO_EFFECT_MAX; i ++) {
        result += state->song.held_effects[i] && state->effects[i].entity ==
            state->song.held_effects[i];
    }

    for (i = 1; i < BOX_AUDIO_BUS_MAX; i ++) {
        result += (state->song.held_buses & state->bus_freed & ((uint64_t)1 << i)) != 0;
    }

    result += ecs_vec_count(&state->song.held_sounds);
done:
    return result;
}

box_audio_song_stats_t box_audio_song_stats(
    const ecs_world_t *world)
{
    box_audio_module_t *state = box_audio_module_get(world);
    box_audio_song_stats_t result = {0};
    if (state) {
        result = state->song.stats;
    }

    return result;
}

void box_audio_song_load(
    ecs_world_t *world,
    const char *file)
{
    AudioSong *song;
    if (!file || !file[0]) {
        box_audio_song_unload(world);
        goto done;
    }

    song = ecs_singleton_ensure(world, AudioSong);
    ecs_os_strset(&song->file, file);
    ecs_singleton_modified(world, AudioSong);
done:
    return;
}

void box_audio_song_unload(
    ecs_world_t *world)
{
    ecs_singleton_remove(world, AudioSong);
}

void box_audio_song_init(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    box_audio_song_t *song = &state->song;
    ecs_vec_init_t(NULL, &song->baseline, ecs_id_t, 0);
    ecs_vec_init_t(NULL, &song->held_sounds, uint32_t, 0);
    ecs_vec_init_t(NULL, &song->foreign, ecs_entity_t, 0);
    song->root = ecs_entity(world, { .name = "songs" });
    ecs_singleton_add(world, Song);
    ecs_add_pair(world, ecs_id(Song), ecs_id(EcsScript), song->root);
    ecs_singleton_set(world, AudioSongStatus, { .state = SongNone });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "AudioSongOwner" }),
        .query.terms = {{ .id = ecs_pair(ecs_id(EcsScript), EcsWildcard), .src.id = EcsSelf }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = BoxAudioSongOwner,
        .ctx = state
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "AudioSongUpdate" }),
        .phase = EcsOnLoad,
        .run = BoxAudioSongUpdate,
        .ctx = state,
        .immediate = true
    });
}

void box_audio_song_fini(
    box_audio_module_t *state)
{
    box_audio_song_t *song = &state->song;
    ecs_os_free(song->file);
    ecs_os_free(song->code);
    ecs_os_free(song->next_code);
    ecs_os_free(song->target);
    ecs_os_free(song->held_file);
    ecs_vec_fini_t(NULL, &song->baseline, ecs_id_t);
    ecs_vec_fini_t(NULL, &song->held_sounds, uint32_t);
    ecs_vec_fini_t(NULL, &song->foreign, ecs_entity_t);
}
