/**
 * @file audio/decode.c
 * @brief Decodes WAV and FLAC files into float frames with the decoders of miniaudio.
 *
 * Only the decoders are compiled: no device IO, engine, resource manager, encoders, MP3 or
 * threading, and every function of miniaudio is static so it does not clash with the device
 * part that native.c builds. The web compiles this file too, with -std=c11 (docs/audio.md).
 */

#include "module.h"
#include "../render/preprocess.h"
#include <stdio.h>

#define MA_NO_DEVICE_IO
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_MP3
#define MA_NO_VORBIS
#define MA_NO_THREADING
#define MA_API static
#define MA_MALLOC(size) ecs_os_malloc((ecs_size_t)(size))
#define MA_REALLOC(ptr, size) ecs_os_realloc(ptr, (ecs_size_t)(size))
#define MA_FREE(ptr) ecs_os_free(ptr)
/* native.c defines the same global for the device part */
#define ma_atomic_global_lock box_audio_decode_global_lock
#define MINIAUDIO_IMPLEMENTATION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wcast-qual"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wdouble-promotion"
#pragma clang diagnostic ignored "-Wbad-function-cast"
#pragma clang diagnostic ignored "-Wcomma"
#pragma clang diagnostic ignored "-Wfloat-equal"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wbad-function-cast"
#pragma GCC diagnostic ignored "-Wfloat-equal"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#ifdef _WIN32
#define AlphaBlend box_win32_AlphaBlend
#endif
#include <miniaudio.h>
#ifdef _WIN32
#undef AlphaBlend
#endif
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/** Frames read from the decoder at once */
#define BOX_AUDIO_DECODE_CHUNK (4096)

/** Channels of a file the decoder reads at most */
#define BOX_AUDIO_DECODE_CHANNELS (MA_MAX_CHANNELS)

/** Read a file into memory.
 *
 * @param path The file
 * @param size Receives the bytes read, 0 for an empty file
 * @return The bytes, NULL when the file cannot be read or is empty
 */
static uint8_t* box_audio_read_file(
    const char *path,
    int64_t *size)
{
    uint8_t *result = NULL;
    long len = -1;
    FILE *f = fopen(path, "rb");
    *size = -1;
    if (!f) {
        goto done;
    }

    if (!fseek(f, 0, SEEK_END)) {
        len = ftell(f);
    }

    *size = len;
    if (len <= 0 || len > BOX_AUDIO_SAMPLE_MAX_FILE || fseek(f, 0, SEEK_SET)) {
        goto close;
    }

    result = ecs_os_malloc((ecs_size_t)len);
    if (result && fread(result, 1, (size_t)len, f) != (size_t)len) {
        ecs_os_free(result);
        result = NULL;
        *size = -1;
    }

close:
    fclose(f);
done:
    return result;
}

/** Get the side a channel of a file mixes to when it has more than two.
 *
 * @param position The position of the channel
 * @return -1 for the left, 1 for the right, 0 for both, 2 for none (the LFE)
 */
static int32_t box_audio_side(
    ma_channel position)
{
    int32_t result = 0;
    switch (position) {
    case MA_CHANNEL_FRONT_LEFT:
    case MA_CHANNEL_BACK_LEFT:
    case MA_CHANNEL_SIDE_LEFT:
    case MA_CHANNEL_FRONT_LEFT_CENTER:
    case MA_CHANNEL_TOP_FRONT_LEFT:
    case MA_CHANNEL_TOP_BACK_LEFT:
        result = -1;
        break;
    case MA_CHANNEL_FRONT_RIGHT:
    case MA_CHANNEL_BACK_RIGHT:
    case MA_CHANNEL_SIDE_RIGHT:
    case MA_CHANNEL_FRONT_RIGHT_CENTER:
    case MA_CHANNEL_TOP_FRONT_RIGHT:
    case MA_CHANNEL_TOP_BACK_RIGHT:
        result = 1;
        break;
    case MA_CHANNEL_LFE:
        result = 2;
        break;
    default:
        break;
    }

    return result;
}

/** Mix frames of more than two channels down to stereo, in place: each side is the mean of
 * the channels on it, a channel without a side (a center) counts on both and the LFE on none.
 *
 * @param data Interleaved frames, the stereo result is written to the start
 * @param frames Number of frames
 * @param channels Channels of the frames, more than 2
 * @param map Position of each channel
 */
static void box_audio_mix_down(
    float *data,
    int64_t frames,
    int32_t channels,
    const ma_channel *map)
{
    int32_t side[BOX_AUDIO_DECODE_CHANNELS], c, left = 0, right = 0;
    int64_t i;
    for (c = 0; c < channels; c ++) {
        side[c] = box_audio_side(map[c]);
        left += side[c] <= 0;
        right += side[c] == 0 || side[c] == 1;
    }

    for (i = 0; i < frames; i ++) {
        const float *in = data + i * channels;
        float l = 0, r = 0;
        for (c = 0; c < channels; c ++) {
            if (side[c] <= 0) {
                l += in[c];
            }

            if (side[c] == 0 || side[c] == 1) {
                r += in[c];
            }
        }

        data[i * 2] = left ? l / (float)left : 0.0f;
        data[i * 2 + 1] = right ? r / (float)right : 0.0f;
    }
}

/** Decode a file of a supported format in memory.
 *
 * @param job The job, receives the frames or an error
 * @param bytes The file
 * @param size Bytes of the file
 */
static void box_audio_decode_memory(
    box_audio_sample_job_t *job,
    const uint8_t *bytes,
    int64_t size)
{
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_channel map[BOX_AUDIO_DECODE_CHANNELS];
    ma_decoder decoder;
    ma_uint32 channels = 0, rate = 0;
    ma_format format;
    float *data = NULL;
    int64_t frames = 0, capacity = 0;
    config.encodingFormat = ma_encoding_format_unknown;
    if (ma_decoder_init_memory(bytes, (size_t)size, &config, &decoder) != MA_SUCCESS) {
        job->error = flecs_asprintf("cannot decode sound '%s': not a WAV or FLAC file",
            job->path);
        goto done;
    }

    if (ma_decoder_get_data_format(&decoder, &format, &channels, &rate, map,
        BOX_AUDIO_DECODE_CHANNELS) != MA_SUCCESS || !channels || !rate ||
        channels > BOX_AUDIO_DECODE_CHANNELS)
    {
        job->error = flecs_asprintf("cannot decode sound '%s': unsupported format", job->path);
        goto uninit;
    }

    for (;;) {
        ma_uint64 read = 0;
        if (frames + BOX_AUDIO_DECODE_CHUNK > capacity) {
            int64_t grown = capacity ? capacity * 2 : BOX_AUDIO_DECODE_CHUNK * 4;
            if (grown * channels * (int64_t)sizeof(float) > BOX_AUDIO_SAMPLE_MAX_BYTES) {
                job->error = flecs_asprintf("sound '%s' is longer than the %d MB of a sound",
                    job->path, BOX_AUDIO_SAMPLE_MAX_BYTES / (1024 * 1024));
                goto uninit;
            }

            float *next = ecs_os_realloc(data,
                (ecs_size_t)(grown * channels * (int64_t)sizeof(float)));
            if (!next) {
                job->error = flecs_asprintf("out of memory decoding sound '%s'", job->path);
                goto uninit;
            }

            data = next;
            capacity = grown;
        }

        ma_result res = ma_decoder_read_pcm_frames(&decoder, data + frames * channels,
            BOX_AUDIO_DECODE_CHUNK, &read);
        frames += (int64_t)read;
        if (res != MA_SUCCESS || read < BOX_AUDIO_DECODE_CHUNK) {
            break;
        }
    }

    if (frames <= 0) {
        job->error = flecs_asprintf("sound '%s' has no samples", job->path);
        goto uninit;
    }

    if (channels > 2) {
        box_audio_mix_down(data, frames, (int32_t)channels, map);
        job->source_channels = (int32_t)channels;
        channels = 2;
    } else {
        job->source_channels = (int32_t)channels;
    }

    job->data = ecs_os_realloc(data, (ecs_size_t)(frames * channels * (int64_t)sizeof(float)));
    job->data = job->data ? job->data : data;
    data = NULL;
    job->frames = (int32_t)frames;
    job->channels = (int32_t)channels;
    job->sample_rate = (float)rate;
uninit:
    ma_decoder_uninit(&decoder);
done:
    ecs_os_free(data);
}

void box_audio_decode(
    box_audio_sample_job_t *job)
{
    int64_t size = 0;
    char *file = job->path ? box_shader_locate(job->path) : NULL;
    uint8_t *bytes = NULL;
    if (!file) {
        job->error = flecs_asprintf("cannot find sound file '%s'", job->path ? job->path : "");
        goto done;
    }

    bytes = box_audio_read_file(file, &size);
    if (!size) {
        job->error = flecs_asprintf("sound file '%s' is empty", job->path);
        goto done;
    }

    if (!bytes) {
        job->error = size > BOX_AUDIO_SAMPLE_MAX_FILE
            ? flecs_asprintf("sound file '%s' is larger than %d MB", job->path,
                BOX_AUDIO_SAMPLE_MAX_FILE / (1024 * 1024))
            : flecs_asprintf("cannot read sound file '%s'", job->path);
        goto done;
    }

    box_audio_decode_memory(job, bytes, size);
done:
    ecs_os_free(bytes);
    ecs_os_free(file);
}
