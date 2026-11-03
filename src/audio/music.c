/**
 * @file audio/music.c
 * @brief Music: AudioStream plays one Ogg Vorbis file as a stream of decoded chunks.
 *
 * Chunks decode ahead on a job and go to the ring of a stream of the core after the batch of
 * the frame; a loop decodes in one piece, so it has no seam. On the web a decode stops at
 * BOX_AUDIO_MUSIC_WEB_BUDGET and goes on in the next frame, since jobs run inline there.
 *
 * One stream plays at once; a changed path or a delete retires a stream, which fades out and
 * is freed once the core closed it.
 */

#include "module.h"
#include "platform.h"
#include "../jobs/jobs.h"
#include "../render/preprocess.h"

/** Write a stop command into the batch of the frame.
 *
 * @param state The state
 * @param voice Handle of the voice
 */
static void box_audio_stop_voice(
    box_audio_module_t *state,
    uint32_t voice)
{
    if (box_audio_reserve(state)) {
        box_audio_write_stop(&state->writer, voice);
    }
}

/** Write a command that sets a parameter of a voice into the batch of the frame.
 *
 * @param state The state
 * @param voice Handle of the voice
 * @param param The parameter
 * @param value The value
 */
static void box_audio_set_voice(
    box_audio_module_t *state,
    uint32_t voice,
    box_audio_param_t param,
    float value)
{
    if (box_audio_reserve(state)) {
        box_audio_write_voice_set(&state->writer, voice, param, value);
    }
}

/** Free a decoder, its file and the chunk it fills.
 *
 * @param d The decoder, may be NULL
 */
static void box_audio_music_decoder_free(
    box_audio_music_decoder_t *d)
{
    if (d) {
        box_audio_vorbis_close(d->vorbis);
        ecs_os_free(d->partial);
        ecs_os_free(d);
    }
}

/** Free the decoded chunks that were not sent.
 *
 * @param m The stream
 */
static void box_audio_music_drop_ready(
    box_audio_music_t *m)
{
    int32_t i;
    for (i = 0; i < m->ready_count; i ++) {
        ecs_os_free(m->ready[i].data);
        m->ready[i].data = NULL;
    }

    m->ready_count = 0;
}

/** Free a stream and what it holds; a job in flight frees what it holds itself.
 *
 * @param m The stream
 */
static void box_audio_music_free(
    box_audio_music_t *m)
{
    box_audio_music_drop_ready(m);
    box_audio_music_decoder_free(m->decoder);
    if (m->fetch) {
        box_audio_web_fetch_cancel(m->fetch);
    }

    ecs_os_free(m->path);
    ecs_os_free(m->error);
    ecs_os_free(m);
}

/** Find the stream of a key.
 *
 * @param state The state
 * @param key The key
 * @return The stream, NULL when it was freed
 */
static box_audio_music_t* box_audio_music_find(
    const box_audio_module_t *state,
    uint64_t key)
{
    box_audio_music_t **list = ecs_vec_first(&state->music);
    box_audio_music_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->music); i ++) {
        if (list[i]->key == key) {
            result = list[i];
            break;
        }
    }

    return result;
}

/** Mark a stream failed and log its error once.
 *
 * @param m The stream
 * @param error Why, taken by the stream
 */
static void box_audio_music_fail(
    box_audio_music_t *m,
    char *error)
{
    ecs_err("box.audio: %s", error);
    ecs_os_free(m->error);
    m->error = error;
    m->state = AudioStreamFailed;
}

/** Get the frames a stream holds decoded ahead of what plays.
 *
 * @param m The stream
 * @return Frames of the chunks ready, sent and not played, and of the chunk being filled
 */
static int64_t box_audio_music_ahead(
    const box_audio_music_t *m)
{
    int64_t result = 0;
    int32_t i;
    for (i = 0; i < m->ready_count; i ++) {
        result += m->ready[i].frames;
    }

    for (i = 0; i < m->sent_count; i ++) {
        result += m->sent[(m->sent_first + i) % BOX_AUDIO_STREAM_CHUNKS].frames;
    }

    result -= m->sent_count ? m->at : 0;
    result += m->decoder ? m->decoder->filled : 0;

    return result > 0 ? result : 0;
}

/** Get the frames a stream decodes ahead at most: BOX_AUDIO_MUSIC_AHEAD seconds, as far as the
 * ring of the core holds them.
 *
 * @param rate Frames per second of the file
 * @return Frames
 */
static int64_t box_audio_music_target(
    float rate)
{
    int64_t result = (int64_t)(BOX_AUDIO_MUSIC_AHEAD * (double)rate);
    int64_t most = (int64_t)(BOX_AUDIO_STREAM_CHUNKS - 1) * BOX_AUDIO_STREAM_CHUNK;

    return result < most ? result : most;
}

/** Decode a part of a stream on a worker: open its file first if needed, seek, then fill
 * chunks until it decoded what was asked for, the chunks it may complete or its budget.
 *
 * @param ctx The job
 */
static void box_audio_music_work(
    void *ctx)
{
    box_audio_music_job_t *job = ctx;
    box_audio_music_decoder_t *d = job->decoder;
    ecs_time_t start = {0};
    int64_t want = job->want, produced = 0;
    int32_t channels;
    ecs_time_measure(&start);
    if (!d) {
        uint8_t *bytes = job->bytes;
        int32_t size = job->size, error = 0;
        box_audio_vorbis_t *v;
        job->bytes = NULL;
        if (!bytes) {
            char *file = job->path ? box_shader_locate(job->path) : NULL;
            if (!file) {
                job->error = flecs_asprintf("cannot find music file '%s'",
                    job->path ? job->path : "");
                goto done;
            }

            bytes = box_audio_music_read_file(file, &size);
            ecs_os_free(file);
            if (!bytes) {
                job->error = size == 0
                    ? flecs_asprintf("music file '%s' is empty", job->path)
                    : size > BOX_AUDIO_MUSIC_MAX_FILE
                    ? flecs_asprintf("music file '%s' is larger than %d MB", job->path,
                        BOX_AUDIO_MUSIC_MAX_FILE / (1024 * 1024))
                    : flecs_asprintf("cannot read music file '%s'", job->path);
                goto done;
            }
        }

        v = box_audio_vorbis_open(bytes, size, &error);
        if (!v) {
            job->error = flecs_asprintf(
                "cannot decode music file '%s': not an Ogg Vorbis file (stb_vorbis error %d)",
                job->path ? job->path : "", error);
            goto done;
        }

        d = ecs_os_calloc_t(box_audio_music_decoder_t);
        d->vorbis = v;
        job->decoder = d;
    }

    if (job->seek >= 0) {
        box_audio_vorbis_seek(d->vorbis, job->seek);
        ecs_os_free(d->partial);
        d->partial = NULL;
        d->filled = 0;
        d->ended = false;
    }

    channels = box_audio_vorbis_channels(d->vorbis);
    if (want < 0) {
        want = box_audio_music_target(box_audio_vorbis_rate(d->vorbis));
    }

    while (produced < want && job->count < job->max_chunks && !d->ended) {
        int32_t n, got;
        if (!d->partial) {
            d->partial = ecs_os_malloc_n(float, BOX_AUDIO_STREAM_CHUNK * channels);
            d->filled = 0;
            d->partial_pos = box_audio_vorbis_position(d->vorbis);
        }

        n = BOX_AUDIO_STREAM_CHUNK - d->filled;
        n = n < BOX_AUDIO_MUSIC_STEP ? n : BOX_AUDIO_MUSIC_STEP;
        got = box_audio_vorbis_read(d->vorbis, d->partial + (int64_t)d->filled * channels, n,
            job->loop);
        d->filled += got;
        produced += got;
        if (got < n) {
            d->ended = true;
        }

        if (d->filled == BOX_AUDIO_STREAM_CHUNK || d->ended) {
            if (d->filled) {
                job->chunks[job->count ++] = (box_audio_music_chunk_t){
                    .data = d->partial, .frames = d->filled, .pos = d->partial_pos,
                    .last = d->ended };
            } else {
                ecs_os_free(d->partial);
                if (job->count) {
                    job->chunks[job->count - 1].last = true;
                }
            }

            d->partial = NULL;
            d->filled = 0;
        }

        if (job->budget > 0) {
            ecs_time_t now = start;
            if (ecs_time_measure(&now) > job->budget) {
                break;
            }
        }
    }

done:
    job->seconds = ecs_time_measure(&start);
    ecs_os_mutex_lock(job->sync->lock);
    job->sync->working --;
    ecs_os_mutex_unlock(job->sync->lock);
}

/** Hand the decoder and the chunks of a job back to its stream, unless the stream was freed
 * meanwhile; chunks decoded before a stop or a seek are dropped. A failed open logs one error.
 *
 * @param world The world
 * @param ctx The job
 */
static void box_audio_music_complete(
    ecs_world_t *world,
    void *ctx)
{
    box_audio_music_job_t *job = ctx;
    box_audio_module_t *state = box_audio_module_get(world);
    box_audio_music_t *m = state ? box_audio_music_find(state, job->key) : NULL;
    int32_t i;
    if (!m) {
        goto done;
    }

    m->job = false;
    m->decode_ms = (float)(job->seconds * 1000.0);
    m->decode_max_ms = m->decode_ms > m->decode_max_ms ? m->decode_ms : m->decode_max_ms;
    if (job->error) {
        box_audio_music_fail(m, job->error);
        job->error = NULL;
        goto done;
    }

    m->decoder = job->decoder;
    job->decoder = NULL;
    m->frames = box_audio_vorbis_frames(m->decoder->vorbis);
    m->channels = box_audio_vorbis_channels(m->decoder->vorbis);
    m->sample_rate = box_audio_vorbis_rate(m->decoder->vorbis);
    if (job->generation != m->generation) {
        goto done;
    }

    for (i = 0; i < job->count && m->ready_count < BOX_AUDIO_STREAM_CHUNKS; i ++) {
        m->ready[m->ready_count ++] = job->chunks[i];
        job->chunks[i].data = NULL;
    }

done:
    return;
}

/** Free a job after its completion, or at shutdown instead of it.
 *
 * @param ctx The job
 */
static void box_audio_music_job_free(
    void *ctx)
{
    box_audio_music_job_t *job = ctx;
    int32_t i;
    for (i = 0; i < job->count; i ++) {
        ecs_os_free(job->chunks[i].data);
    }

    box_audio_music_decoder_free(job->decoder);
    ecs_os_free(job->path);
    ecs_os_free(job->bytes);
    ecs_os_free(job->error);
    box_audio_sample_sync_release(job->sync);
    ecs_os_free(job);
}

/** Submit a decode of a stream, which takes its decoder until it completes.
 *
 * @param world The world or a stage
 * @param state The state
 * @param m The stream
 * @param bytes The file the web fetched, NULL to read the path when the file is not open yet
 * @param size Bytes of bytes
 */
static void box_audio_music_submit(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_music_t *m,
    uint8_t *bytes,
    int32_t size)
{
    box_audio_sample_sync_t *sync = state->sample_sync;
    box_audio_music_job_t *job = ecs_os_calloc_t(box_audio_music_job_t);
    const AudioStream *stream = m->entity && ecs_is_alive(world, m->entity) ?
        ecs_get(world, m->entity, AudioStream) : NULL;
    job->key = m->key;
    job->generation = m->generation;
    job->path = m->decoder || bytes ? NULL : ecs_os_strdup(m->path);
    job->bytes = bytes;
    job->size = size;
    job->decoder = m->decoder;
    job->sync = sync;
    job->loop = stream ? stream->loop : false;
    job->seek = m->seek;
    job->want = m->decoder ?
        box_audio_music_target(m->sample_rate) - box_audio_music_ahead(m) : -1;
    job->max_chunks = BOX_AUDIO_STREAM_CHUNKS - 1 - m->ready_count - m->sent_count;
#ifdef __EMSCRIPTEN__
    job->budget = BOX_AUDIO_MUSIC_WEB_BUDGET;
#endif
    m->decoder = NULL;
    m->seek = -1;
    m->job = true;
    sync->refs ++;
    ecs_os_mutex_lock(sync->lock);
    sync->working ++;
    ecs_os_mutex_unlock(sync->lock);
    state->stats.music_jobs ++;
    if (box_job_submit(world, &(box_job_desc_t){
        .work = box_audio_music_work,
        .complete = box_audio_music_complete,
        .ctx_free = box_audio_music_job_free,
        .ctx = job
    })) {
        ecs_os_mutex_lock(sync->lock);
        sync->working --;
        ecs_os_mutex_unlock(sync->lock);
        m->job = false;
        m->decoder = job->decoder;
        job->decoder = NULL;
        box_audio_music_fail(m, flecs_asprintf("cannot submit the decode of music '%s'",
            m->path));
        box_audio_music_job_free(job);
    }
}

/** Create the stream of an entity: fetch its file on the web when the page's file system does
 * not have it, else open and decode it on a job.
 *
 * @param world The world or a stage
 * @param state The state
 * @param e The entity
 * @param path The file
 * @return The stream
 */
static box_audio_music_t* box_audio_music_new(
    ecs_world_t *world,
    box_audio_module_t *state,
    ecs_entity_t e,
    const char *path)
{
    box_audio_music_t *m = ecs_os_calloc_t(box_audio_music_t);
    m->key = ++ state->next_music;
    m->entity = e;
    m->path = ecs_os_strdup(path);
    m->state = AudioStreamLoading;
    m->seek = -1;
    m->slot = -1;
    ecs_vec_append_t(NULL, &state->music, box_audio_music_t*)[0] = m;
#ifdef __EMSCRIPTEN__
    char *file = box_shader_locate(path);
    if (!file) {
        m->fetch = box_audio_web_fetch(path);
    }

    ecs_os_free(file);
    if (m->fetch) {
        goto done;
    }
#endif

    box_audio_music_submit(world, state, m, NULL, 0);
#ifdef __EMSCRIPTEN__
done:
#endif
    return m;
}

/** Retire a stream after its path changed or its entity went away: it fades out and is freed
 * once the core closed it and no job holds its decoder.
 *
 * @param state The state
 * @param m The stream
 */
static void box_audio_music_retire(
    box_audio_module_t *state,
    box_audio_music_t *m)
{
    m->entity = 0;
    if (m->fetch) {
        box_audio_web_fetch_cancel(m->fetch);
        m->fetch = 0;
    }

    if (state->music_active == m) {
        state->music_active = NULL;
    }
}

/** Forget the decoded chunks and decode again from a frame, for a stream that stopped.
 *
 * @param m The stream
 * @param frame Frame of the file to go on from
 */
static void box_audio_music_rewind(
    box_audio_music_t *m,
    int64_t frame)
{
    box_audio_music_drop_ready(m);
    if (m->decoder) {
        ecs_os_free(m->decoder->partial);
        m->decoder->partial = NULL;
        m->decoder->filled = 0;
        m->decoder->ended = false;
    }

    m->generation ++;
    m->seek = frame;
    m->position = frame;
}

/** Forget the voice of a stream whose stream the core closed.
 *
 * @param m The stream
 */
static void box_audio_music_unvoice(
    box_audio_music_t *m)
{
    m->underruns += m->voice_underruns;
    m->voice_underruns = 0;
    m->slot = -1;
    m->voice = 0;
    m->sent_first = 0;
    m->sent_count = 0;
    m->consumed = 0;
    m->at = 0;
    m->stopping = false;
}

/** Read the status of the stream of the core that plays a stream: drop the chunks it consumed
 * and notice when it closed, after a stop, at the end or after a steal.
 *
 * @param world The world
 * @param state The state
 * @param t The transport
 * @param m The stream
 */
static void box_audio_music_poll(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t,
    box_audio_music_t *m)
{
    box_audio_stream_status_t status;
    bool stopping = m->stopping;
    if (!t || m->slot < 0 || m->serial != state->serial) {
        goto done;
    }

    t->stream_status(t, (uint32_t)m->slot, &status);
    if (status.voice != m->voice) {
        goto done;
    }

    while (m->consumed < status.consumed && m->sent_count) {
        m->sent_first = (m->sent_first + 1) % BOX_AUDIO_STREAM_CHUNKS;
        m->sent_count --;
        m->consumed ++;
    }

    m->at = m->sent_count ? status.at : 0;
    m->voice_underruns = status.underruns;
    if (m->sent_count) {
        const box_audio_music_chunk_t *c = &m->sent[m->sent_first];
        m->position = c->pos + m->at;
    } else if (m->ready_count) {
        m->position = m->ready[0].pos;
    }

    if (m->frames > 0) {
        m->position %= m->frames;
    }

    if (status.open) {
        goto done;
    }

    box_audio_music_unvoice(m);
    if (status.ended && !stopping) {
        m->ended = true;
        AudioStream *stream = m->entity && ecs_is_alive(world, m->entity) ?
            ecs_get_mut(world, m->entity, AudioStream) : NULL;
        if (stream && stream->playing) {
            stream->playing = false;
            ecs_modified(world, m->entity, AudioStream);
        }

        box_audio_music_rewind(m, 0);
    } else {
        box_audio_music_rewind(m, m->position);
    }

done:
    return;
}

/** Find a stream of the core that no stream of the module plays on.
 *
 * @param state The state
 * @return Its index, -1 when every one is taken
 */
static int32_t box_audio_music_slot(
    const box_audio_module_t *state)
{
    box_audio_music_t **list = ecs_vec_first(&state->music);
    int32_t slot, i, result = -1;
    for (slot = 0; slot < BOX_AUDIO_STREAM_MAX && result < 0; slot ++) {
        bool used = false;
        for (i = 0; i < ecs_vec_count(&state->music); i ++) {
            used |= list[i]->slot == slot;
        }

        if (!used) {
            result = slot;
        }
    }

    return result;
}

/** Check the fetch of the file of a stream on the web: submit its decode once it arrived.
 *
 * @param world The world
 * @param state The state
 * @param m The stream
 */
static void box_audio_music_fetch(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_music_t *m)
{
    int32_t size = 0, status = 0, done = box_audio_web_fetch_poll(m->fetch, &size, &status);
    uint8_t *bytes;
    if (!done) {
        goto out;
    }

    if (done < 0 || size <= 0 || size > BOX_AUDIO_MUSIC_MAX_FILE) {
        box_audio_web_fetch_cancel(m->fetch);
        m->fetch = 0;
        box_audio_music_fail(m, status
            ? flecs_asprintf("cannot fetch music file '%s' (HTTP %d)", m->path, status)
            : flecs_asprintf("cannot fetch music file '%s'", m->path));
        goto out;
    }

    bytes = ecs_os_malloc(size);
    box_audio_web_fetch_take(m->fetch, bytes);
    m->fetch = 0;
    box_audio_music_submit(world, state, m, bytes, size);
out:
    return;
}

/** Start, stop and update the voice of the stream of an entity.
 *
 * @param world The world
 * @param state The state
 * @param m The stream
 * @param waiting Whether the page waits for a gesture
 */
static void box_audio_music_voice(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_music_t *m,
    bool waiting)
{
    AudioStream *stream = ecs_get_mut(world, m->entity, AudioStream);
    const AudioOutput *out = ecs_get(world, m->entity, AudioOutput);
    bool muted = ecs_has_id(world, m->entity, EcsDisabled);
    float volume = muted || !stream ? 0.0f : stream->volume;
    uint8_t bus = out ? (uint8_t)box_audio_module_bus_index(world, out->bus) : 0;
    bool play = stream && stream->playing && state->serial;
    int64_t ready = 0, start;
    int32_t i, slot;
    if (!stream) {
        goto done;
    }

    if (m->voice && !m->stopping && !play) {
        box_audio_stop_voice(state, m->voice);
        m->stopping = true;
    }

    if (m->voice && !m->stopping) {
        if (m->volume != volume) {
            box_audio_set_voice(state, m->voice, BOX_AUDIO_PARAM_VOLUME, volume);
            m->volume = volume;
        }

        if (m->bus != bus) {
            box_audio_set_voice(state, m->voice, BOX_AUDIO_PARAM_BUS, (float)bus);
            m->bus = bus;
        }

        goto done;
    }

    if (!play || m->voice || !m->sample_rate) {
        goto done;
    }

    m->ended = false;
    if (waiting && !stream->loop) {
        stream->playing = false;
        ecs_modified(world, m->entity, AudioStream);
        state->stats.dropped ++;
        goto done;
    }

    for (i = 0; i < m->ready_count; i ++) {
        ready += m->ready[i].frames;
    }

    start = (int64_t)(BOX_AUDIO_MUSIC_START * (double)m->sample_rate);
    if (ready < start && !(m->ready_count && m->ready[m->ready_count - 1].last)) {
        goto done;
    }

    slot = box_audio_music_slot(state);
    if (slot < 0 || !box_audio_reserve(state)) {
        goto done;
    }

    m->slot = slot;
    m->voice = box_audio_new_voice(state);
    m->serial = state->serial;
    m->consumed = 0;
    m->at = 0;
    m->volume = volume;
    m->bus = bus;
    m->stopping = false;
    box_audio_write_stream(&state->writer, m->voice, (uint32_t)slot, bus, volume,
        (uint32_t)m->channels, m->sample_rate);
    state->stats.music_starts ++;
done:
    return;
}

void box_audio_music_update(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t,
    bool waiting)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->music); i ++) {
        box_audio_music_t *m = ecs_vec_get_t(&state->music, box_audio_music_t*, i)[0];
        box_audio_music_poll(world, state, t, m);
        if (m->entity && !ecs_is_alive(world, m->entity)) {
            box_audio_music_retire(state, m);
        }

        if (!m->entity) {
            if (m->voice && !m->stopping && t && m->serial == state->serial) {
                box_audio_stop_voice(state, m->voice);
                m->stopping = true;
            }

            if (m->slot < 0 && !m->job) {
                box_audio_music_free(m);
                ecs_vec_remove_t(&state->music, box_audio_music_t*, i);
                i --;
            }

            continue;
        }

        if (m->fetch) {
            box_audio_music_fetch(world, state, m);
        }

        if (m->state != AudioStreamFailed && t) {
            box_audio_music_voice(world, state, m, waiting);
        }
    }
}

/** Write the status of the entity of a stream when it changed.
 *
 * @param world The world
 * @param m The stream
 */
static void box_audio_music_status(
    ecs_world_t *world,
    box_audio_music_t *m)
{
    const AudioStream *stream = ecs_get(world, m->entity, AudioStream);
    const AudioStreamStatus *prev = ecs_get(world, m->entity, AudioStreamStatus);
    float rate = m->sample_rate > 0 ? m->sample_rate : 1.0f;
    AudioStreamStatus status = {
        .frames = m->frames,
        .channels = m->channels,
        .sample_rate = m->sample_rate,
        .position = (float)((double)m->position / rate),
        .buffered = (float)((double)box_audio_music_ahead(m) / rate),
        .underruns = m->underruns + m->voice_underruns,
        .decode_ms = m->decode_ms,
        .decode_max_ms = m->decode_max_ms
    };
    if (m->state == AudioStreamFailed) {
        status.state = AudioStreamFailed;
        status.error = m->error;
    } else if (m->voice && !m->stopping) {
        status.state = AudioStreamPlaying;
    } else if (m->ended && !(stream && stream->playing)) {
        status.state = AudioStreamEnded;
    } else if (!stream || !stream->playing || (m->voice && m->stopping)) {
        status.state = AudioStreamStopped;
    } else {
        status.state = AudioStreamLoading;
    }

    m->state = status.state;
    if (prev && prev->state == status.state && prev->frames == status.frames &&
        prev->channels == status.channels && prev->sample_rate == status.sample_rate &&
        prev->position == status.position && prev->buffered == status.buffered &&
        prev->underruns == status.underruns && prev->decode_ms == status.decode_ms &&
        prev->decode_max_ms == status.decode_max_ms &&
        (prev->error == status.error || (prev->error && status.error &&
            !strcmp(prev->error, status.error))))
    {
        goto done;
    }

    ecs_set_ptr(world, m->entity, AudioStreamStatus, &status);
done:
    return;
}

void box_audio_music_send(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->music); i ++) {
        box_audio_music_t *m = ecs_vec_get_t(&state->music, box_audio_music_t*, i)[0];
        bool sending = t && m->voice && !m->stopping && m->serial == state->serial;
        while (sending && m->ready_count && m->sent_count < BOX_AUDIO_STREAM_CHUNKS) {
            box_audio_music_chunk_t c = m->ready[0];
            if (!t->stream_chunk(t, (uint32_t)m->slot, m->voice, c.data, c.frames,
                m->channels, c.last))
            {
                break;
            }

            ecs_os_free(c.data);
            c.data = NULL;
            m->sent[(m->sent_first + m->sent_count) % BOX_AUDIO_STREAM_CHUNKS] = c;
            m->sent_count ++;
            m->ready_count --;
            memmove(m->ready, m->ready + 1, (size_t)m->ready_count * sizeof(m->ready[0]));
        }

        if (!m->entity) {
            continue;
        }

        if (m->state != AudioStreamFailed && m->decoder && !m->job &&
            (m->seek >= 0 || !m->decoder->ended) &&
            m->ready_count + m->sent_count < BOX_AUDIO_STREAM_CHUNKS - 1)
        {
            int64_t ahead = box_audio_music_ahead(m);
#ifdef __EMSCRIPTEN__
            int64_t low = box_audio_music_target(m->sample_rate);
#else
            int64_t low = (int64_t)(BOX_AUDIO_MUSIC_REFILL * (double)m->sample_rate);
#endif
            if (m->seek >= 0 || ahead < low) {
                box_audio_music_submit(world, state, m, NULL, 0);
            }
        }

        box_audio_music_status(world, m);
    }
}

void box_audio_music_reset(
    box_audio_module_t *state)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->music); i ++) {
        box_audio_music_t *m = ecs_vec_get_t(&state->music, box_audio_music_t*, i)[0];
        if (m->voice) {
            box_audio_music_unvoice(m);
            box_audio_music_rewind(m, m->position);
        }
    }
}

int32_t box_audio_music_count(
    const ecs_world_t *world)
{
    box_audio_module_t *state = box_audio_module_get(world);

    return state ? ecs_vec_count(&state->music) : 0;
}

/** Write the failed status of an entity without a stream.
 *
 * @param world The world or a stage
 * @param e The entity
 * @param error Why
 */
static void box_audio_music_refuse(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *error)
{
    ecs_set(world, e, AudioStreamStatus, {
        .state = AudioStreamFailed, .error = ECS_CONST_CAST(char*, error) });
}

/** Start the stream of every set AudioStream whose path changed, retiring the previous one.
 * A second stream logs one error and is silent.
 *
 * @param it The iterator of the observer
 */
static void BoxAudioStreamSet(
    ecs_iter_t *it)
{
    box_audio_module_t *state = it->ctx;
    const AudioStream *streams = ecs_field(it, AudioStream, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        AudioMusic *am = ecs_get_mut(it->world, e, AudioMusic);
        const char *path = streams[i].path;
        box_audio_music_t *m = am ? am->music : NULL;
        if (!am || (m && path && !strcmp(m->path, path))) {
            continue;
        }

        if (m) {
            box_audio_music_retire(state, m);
            am->music = NULL;
        }

        if (!path || !path[0]) {
            char *name = ecs_get_path(it->world, e);
            ecs_err("box.audio: the AudioStream of '%s' has no path", name);
            ecs_os_free(name);
            box_audio_music_refuse(it->stage, e, "the AudioStream has no path");
            continue;
        }

        if (state->music_active && state->music_active->entity != e) {
            if (!am->refused) {
                char *name = ecs_get_path(it->world, e);
                ecs_err("box.audio: one AudioStream plays at once, '%s' is silent", name);
                ecs_os_free(name);
                am->refused = true;
            }

            box_audio_music_refuse(it->stage, e, "one AudioStream plays at once");
            continue;
        }

        am->refused = false;
        am->music = box_audio_music_new(it->stage, state, e, path);
        state->music_active = am->music;
        ecs_set(it->stage, e, AudioStreamStatus, { .state = AudioStreamLoading });
    }
}

/** Retire the stream of every removed AudioStream, on deletes too: it fades out and is freed.
 *
 * @param it The iterator of the observer
 */
static void BoxAudioStreamRemove(
    ecs_iter_t *it)
{
    box_audio_module_t *state = it->ctx;
    bool alive = it->other_table && ecs_table_get_type(it->other_table)->count;
    int32_t i;
    if (ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        AudioMusic *am = ecs_get_mut(it->world, it->entities[i], AudioMusic);
        if (am && am->music) {
            box_audio_music_retire(state, am->music);
            am->music = NULL;
        }

        if (am) {
            am->refused = false;
        }

        if (alive) {
            ecs_remove(it->stage, it->entities[i], AudioStreamStatus);
        }
    }

done:
    return;
}

void box_audio_music_init(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    ecs_vec_init_t(NULL, &state->music, box_audio_music_t*, 0);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "AudioStreamSet" }),
        .query.terms = {{ .id = ecs_id(AudioStream), .src.id = EcsSelf }},
        .query.flags = EcsQueryMatchDisabled,
        .events = { EcsOnSet },
        .callback = BoxAudioStreamSet,
        .ctx = state
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "AudioStreamRemove" }),
        .query.terms = {{ .id = ecs_id(AudioStream), .src.id = EcsSelf }},
        .query.flags = EcsQueryMatchDisabled,
        .events = { EcsOnRemove },
        .callback = BoxAudioStreamRemove,
        .ctx = state
    });
}

void box_audio_music_fini(
    box_audio_module_t *state)
{
    box_audio_music_t **list = ecs_vec_first(&state->music);
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->music); i ++) {
        box_audio_music_free(list[i]);
    }

    ecs_vec_fini_t(NULL, &state->music, box_audio_music_t*);
    state->music_active = NULL;
}
