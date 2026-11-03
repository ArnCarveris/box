/**
 * @file audio/sample.c
 * @brief Sound files: SoundFile decodes on the job system into a sample shared by path.
 *
 * Setting a SoundFile takes a reference to the sample of its path; the first user creates it
 * and submits its decode. The completion keeps the frames on the main thread and writes
 * SoundStatus on every entity of the sample, and the sync system sends them to the output.
 *
 * The last reference releases the sound in the core, which fades the voices that play it.
 */

#include "module.h"
#include "../jobs/jobs.h"

void box_audio_sample_sync_release(
    box_audio_sample_sync_t *sync)
{
    if (-- sync->refs) {
        goto done;
    }

    ecs_os_mutex_free(sync->lock);
    ecs_os_free(sync);
done:
    return;
}

/** Get the number of decodes in flight.
 *
 * @param sync The counter
 * @return Jobs whose decode did not finish
 */
static int32_t box_audio_sample_working(
    box_audio_sample_sync_t *sync)
{
    ecs_os_mutex_lock(sync->lock);
    int32_t result = sync->working;
    ecs_os_mutex_unlock(sync->lock);

    return result;
}

/** Free a sample and its frames.
 *
 * @param sample The sample
 */
static void box_audio_sample_free(
    box_audio_sample_t *sample)
{
    ecs_os_free(sample->path);
    ecs_os_free(sample->error);
    ecs_os_free(sample->data);
    ecs_os_free(sample);
}

/** Write the status of a sound entity from its sample.
 *
 * @param world The world or a stage
 * @param e The sound entity
 * @param sample The sample
 */
static void box_audio_sample_status(
    ecs_world_t *world,
    ecs_entity_t e,
    const box_audio_sample_t *sample)
{
    bool ready = sample->state == SoundReady;
    ecs_set(world, e, SoundStatus, {
        .state = sample->state,
        .frames = ready ? sample->frames : 0,
        .channels = ready ? sample->channels : 0,
        .sample_rate = ready ? sample->sample_rate : 0,
        .error = sample->state == SoundFailed ? sample->error : NULL
    });
}

/** Find the sample of a key.
 *
 * @param state The state
 * @param key The key
 * @return The sample, NULL when it was released
 */
static box_audio_sample_t* box_audio_sample_find(
    const box_audio_module_t *state,
    uint64_t key)
{
    box_audio_sample_t **samples = ecs_vec_first(&state->samples);
    box_audio_sample_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->samples); i ++) {
        if (samples[i]->key == key) {
            result = samples[i];
            break;
        }
    }

    return result;
}

/** Decode the file of a job on a worker thread.
 *
 * @param ctx The job
 */
static void box_audio_sample_work(
    void *ctx)
{
    box_audio_sample_job_t *job = ctx;
    box_audio_decode(job);
    ecs_os_mutex_lock(job->sync->lock);
    job->sync->working --;
    ecs_os_mutex_unlock(job->sync->lock);
}

/** Hand the frames of a decode to its sample, unless the sample was released meanwhile, and
 * write the status of the entities that use it, after the query since completions run in an
 * immediate system. A failure logs one error.
 *
 * @param world The world
 * @param ctx The job
 */
static void box_audio_sample_complete(
    ecs_world_t *world,
    void *ctx)
{
    box_audio_sample_job_t *job = ctx;
    box_audio_module_t *state = box_audio_module_get(world);
    box_audio_sample_t *sample = state ? box_audio_sample_find(state, job->key) : NULL;
    int32_t i;
    if (!sample) {
        goto done;
    }

    if (job->error) {
        ecs_err("box.audio: %s", job->error);
        sample->state = SoundFailed;
        sample->error = job->error;
        job->error = NULL;
    } else {
        sample->state = SoundReady;
        sample->data = job->data;
        sample->frames = job->frames;
        sample->channels = job->channels;
        sample->sample_rate = job->sample_rate;
        job->data = NULL;
    }

    ecs_vec_t users;
    ecs_vec_init_t(NULL, &users, ecs_entity_t, 0);
    ecs_iter_t it = ecs_query_iter(world, state->sample_users);
    while (ecs_query_next(&it)) {
        const AudioSample *s = ecs_field(&it, AudioSample, 0);
        for (i = 0; i < it.count; i ++) {
            if (s[i].sample == sample) {
                ecs_vec_append_t(NULL, &users, ecs_entity_t)[0] = it.entities[i];
            }
        }
    }

    for (i = 0; i < ecs_vec_count(&users); i ++) {
        box_audio_sample_status(world, ecs_vec_get_t(&users, ecs_entity_t, i)[0], sample);
    }

    ecs_vec_fini_t(NULL, &users, ecs_entity_t);

done:
    return;
}

/** Free a job after its completion, or at shutdown instead of it.
 *
 * @param ctx The job
 */
static void box_audio_sample_job_free(
    void *ctx)
{
    box_audio_sample_job_t *job = ctx;
    ecs_os_free(job->path);
    ecs_os_free(job->data);
    ecs_os_free(job->error);
    box_audio_sample_sync_release(job->sync);
    ecs_os_free(job);
}

/** Submit the decode of a new sample.
 *
 * @param world The world or a stage
 * @param state The state
 * @param sample The sample
 */
static void box_audio_sample_submit(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_sample_t *sample)
{
    box_audio_sample_sync_t *sync = state->sample_sync;
    box_audio_sample_job_t *job = ecs_os_calloc_t(box_audio_sample_job_t);
    job->key = sample->key;
    job->path = ecs_os_strdup(sample->path);
    job->sync = sync;
    sync->refs ++;
    ecs_os_mutex_lock(sync->lock);
    sync->working ++;
    ecs_os_mutex_unlock(sync->lock);
    state->stats.decodes ++;
    if (box_job_submit(world, &(box_job_desc_t){
        .work = box_audio_sample_work,
        .complete = box_audio_sample_complete,
        .ctx_free = box_audio_sample_job_free,
        .ctx = job
    })) {
        ecs_os_mutex_lock(sync->lock);
        sync->working --;
        ecs_os_mutex_unlock(sync->lock);
        sample->state = SoundFailed;
        sample->error = flecs_asprintf("cannot submit the decode of sound '%s'", sample->path);
        ecs_err("box.audio: %s", sample->error);
        box_audio_sample_job_free(job);
    }
}

/** Take a reference to the sample of a path, creating it and submitting its decode for the
 * first user.
 *
 * @param world The world or a stage
 * @param state The state
 * @param path The path as written
 * @return The sample
 */
static box_audio_sample_t* box_audio_sample_acquire(
    ecs_world_t *world,
    box_audio_module_t *state,
    const char *path)
{
    box_audio_sample_t **samples = ecs_vec_first(&state->samples);
    box_audio_sample_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->samples); i ++) {
        if (!strcmp(samples[i]->path, path)) {
            result = samples[i];
            break;
        }
    }

    if (!result) {
        result = ecs_os_calloc_t(box_audio_sample_t);
        result->key = ++ state->next_sample;
        result->path = ecs_os_strdup(path);
        result->state = SoundLoading;
        ecs_vec_append_t(NULL, &state->samples, box_audio_sample_t*)[0] = result;
        box_audio_sample_submit(world, state, result);
    }

    result->refs ++;

    return result;
}

/** Drop a reference to a sample. The last one releases its sound in the core, which fades the
 * voices that play it and frees the frames of the output once they ended, and frees the frames
 * of the main thread.
 *
 * @param state The state
 * @param sample The sample
 */
static void box_audio_sample_release(
    box_audio_module_t *state,
    box_audio_sample_t *sample)
{
    box_audio_sample_t **samples = ecs_vec_first(&state->samples);
    int32_t i;
    if (-- sample->refs) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&state->samples); i ++) {
        if (samples[i] == sample) {
            ecs_vec_remove_t(&state->samples, box_audio_sample_t*, i);
            break;
        }
    }

    if (sample->id && sample->serial == state->serial && state->serial) {
        ecs_vec_append_t(NULL, &state->releases, uint32_t)[0] = sample->id;
        ecs_vec_append_t(NULL, &state->fades, uint32_t)[0] = sample->id;
    }

    box_audio_sample_free(sample);
done:
    return;
}

box_audio_sample_t* box_audio_sample_of(
    const ecs_world_t *world,
    ecs_entity_t sound)
{
    const AudioSample *s = sound && ecs_id(AudioSample) && ecs_is_alive(world, sound)
        ? ecs_get(world, sound, AudioSample)
        : NULL;

    return s ? s->sample : NULL;
}

const float* box_audio_sample_frames(
    const ecs_world_t *world,
    ecs_entity_t sound,
    int32_t *frames,
    int32_t *channels,
    float *sample_rate)
{
    const box_audio_sample_t *sample = box_audio_sample_of(world, sound);
    const float *result = NULL;
    if (sample && sample->state == SoundReady) {
        result = sample->data;
        *frames = sample->frames;
        *channels = sample->channels;
        *sample_rate = sample->sample_rate;
    }

    return result;
}

int32_t box_audio_sample_count(
    const ecs_world_t *world)
{
    box_audio_module_t *state = box_audio_module_get(world);

    return state ? ecs_vec_count(&state->samples) : 0;
}

/** Take the sample of the path of every set SoundFile and release the previous one.
 *
 * @param it The iterator of the observer
 */
static void BoxAudioSoundFileSet(
    ecs_iter_t *it)
{
    box_audio_module_t *state = it->ctx;
    const SoundFile *files = ecs_field(it, SoundFile, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        AudioSample *s = ecs_get_mut(it->world, e, AudioSample);
        box_audio_sample_t *prev = s ? s->sample : NULL, *next = NULL;
        if (!s) {
            continue;
        }

        if (files[i].path && files[i].path[0]) {
            next = box_audio_sample_acquire(it->stage, state, files[i].path);
        }

        s->sample = next;
        if (prev) {
            box_audio_sample_release(state, prev);
        }

        if (next) {
            box_audio_sample_status(it->stage, e, next);
        } else {
            char *path = ecs_get_path(it->world, e);
            ecs_err("box.audio: the SoundFile of '%s' has no path", path);
            ecs_os_free(path);
            ecs_set(it->stage, e, SoundStatus, {
                .state = SoundFailed, .error = "the SoundFile has no path" });
        }
    }
}

/** Release the sample of every removed SoundFile, on deletes too.
 *
 * @param it The iterator of the observer
 */
static void BoxAudioSoundFileRemove(
    ecs_iter_t *it)
{
    box_audio_module_t *state = it->ctx;
    bool alive = it->other_table && ecs_table_get_type(it->other_table)->count;
    int32_t i;
    if (ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        AudioSample *s = ecs_get_mut(it->world, it->entities[i], AudioSample);
        if (s && s->sample) {
            box_audio_sample_release(state, s->sample);
            s->sample = NULL;
        }

        if (alive) {
            ecs_remove(it->stage, it->entities[i], SoundStatus);
        }
    }

done:
    return;
}

/** In a headless run, wait for the decodes in flight so a frame always hears the same sounds,
 * and warn when the wait gives up.
 *
 * @param it The iterator of the system
 */
static void BoxAudioSampleFrame(
    ecs_iter_t *it)
{
    box_audio_module_t *state = it->ctx;
    const App *app = ecs_singleton_get(it->world, App);
    ecs_time_t start = {0};
    if (!app || !app->headless) {
        goto done;
    }

    ecs_time_measure(&start);
    while (box_audio_sample_working(state->sample_sync) || box_jobs_running(it->world)) {
        ecs_time_t now = start;
        double waited = ecs_time_measure(&now);
        if (waited > BOX_AUDIO_SAMPLE_HEADLESS_WAIT) {
            ecs_warn("box.audio: headless sound wait timed out after %.1f s: %d decode(s) "
                "in flight", waited, box_audio_sample_working(state->sample_sync));
            break;
        }

        ecs_os_sleep(0, 1000 * 1000);
    }

done:
    return;
}

void box_audio_sample_init(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    ecs_vec_init_t(NULL, &state->samples, box_audio_sample_t*, 0);
    ecs_vec_init_t(NULL, &state->fades, uint32_t, 0);
    state->sample_sync = ecs_os_calloc_t(box_audio_sample_sync_t);
    state->sample_sync->lock = ecs_os_mutex_new();
    state->sample_sync->refs = 1;

    state->sample_users = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "AudioSampleUsers" }),
        .terms = {{ .id = ecs_id(AudioSample), .inout = EcsIn, .src.id = EcsSelf }},
        .flags = EcsQueryMatchDisabled,
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "SoundFileSet" }),
        .query.terms = {{ .id = ecs_id(SoundFile), .src.id = EcsSelf }},
        .query.flags = EcsQueryMatchDisabled,
        .events = { EcsOnSet },
        .callback = BoxAudioSoundFileSet,
        .ctx = state
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "SoundFileRemove" }),
        .query.terms = {{ .id = ecs_id(SoundFile), .src.id = EcsSelf }},
        .query.flags = EcsQueryMatchDisabled,
        .events = { EcsOnRemove },
        .callback = BoxAudioSoundFileRemove,
        .ctx = state
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "AudioSampleFrame" }),
        .phase = EcsPreFrame,
        .run = BoxAudioSampleFrame,
        .ctx = state
    });
}

void box_audio_sample_fini(
    box_audio_module_t *state)
{
    box_audio_sample_t **samples = ecs_vec_first(&state->samples);
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->samples); i ++) {
        box_audio_sample_free(samples[i]);
    }

    ecs_vec_fini_t(NULL, &state->samples, box_audio_sample_t*);
    ecs_vec_fini_t(NULL, &state->fades, uint32_t);
    if (state->sample_sync) {
        box_audio_sample_sync_release(state->sample_sync);
    }
}
