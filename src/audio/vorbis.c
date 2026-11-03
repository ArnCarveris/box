/**
 * @file audio/vorbis.c
 * @brief Decodes Ogg Vorbis files in parts with the pull API of stb_vorbis, for music streams.
 *
 * The only file that compiles stb_vorbis, without its push API and stdio: a stream opens its
 * file in memory and pulls the frames of the next chunk, so a track is never decoded whole. A
 * loop seeks to the start at the end in the same read, so the first frame follows the last.
 */

#include "module.h"
#include <stdio.h>

#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_INTEGER_CONVERSION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#pragma clang diagnostic ignored "-Wunused-value"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wcast-qual"
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wdouble-promotion"
#pragma clang diagnostic ignored "-Wbad-function-cast"
#pragma clang diagnostic ignored "-Wcomma"
#pragma clang diagnostic ignored "-Wfloat-equal"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#pragma clang diagnostic ignored "-Wshadow"
#pragma clang diagnostic ignored "-Wtautological-compare"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wbad-function-cast"
#pragma GCC diagnostic ignored "-Wfloat-equal"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include <stb_vorbis.c>
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/** Channels of a file the decoder reads at most */
#define BOX_AUDIO_VORBIS_CHANNELS (8)

/** Side of each channel by the channel order of Vorbis, for 1 to 8 channels: -1 left, 1 right,
 * 0 both (a center), 2 none (the LFE) */
static const int8_t box_audio_vorbis_sides[BOX_AUDIO_VORBIS_CHANNELS + 1][
    BOX_AUDIO_VORBIS_CHANNELS] =
{
    { 0 },
    { 0 },
    { -1, 1 },
    { -1, 0, 1 },
    { -1, 1, -1, 1 },
    { -1, 0, 1, -1, 1 },
    { -1, 0, 1, -1, 1, 2 },
    { -1, 0, 1, -1, 1, 0, 2 },
    { -1, 0, 1, -1, 1, -1, 1, 2 }
};

struct box_audio_vorbis_t {
    stb_vorbis *vorbis;     /**< The decoder of stb_vorbis */
    uint8_t *bytes;         /**< The file, which the decoder reads */
    int32_t frames;         /**< Frames of the file */
    int32_t channels;       /**< Channels of the file, at most BOX_AUDIO_VORBIS_CHANNELS */
    int32_t out_channels;   /**< Channels written: 1 or 2 */
    float rate;             /**< Frames per second */
    int64_t position;       /**< Frame the next read starts at */
    float *temp[BOX_AUDIO_VORBIS_CHANNELS]; /**< BOX_AUDIO_MUSIC_STEP frames per channel */
};

uint8_t* box_audio_music_read_file(
    const char *path,
    int32_t *size)
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

    *size = len > BOX_AUDIO_MUSIC_MAX_FILE ? BOX_AUDIO_MUSIC_MAX_FILE + 1 : (int32_t)len;
    if (len <= 0 || len > BOX_AUDIO_MUSIC_MAX_FILE || fseek(f, 0, SEEK_SET)) {
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

box_audio_vorbis_t* box_audio_vorbis_open(
    uint8_t *bytes,
    int32_t size,
    int32_t *error)
{
    box_audio_vorbis_t *result = NULL;
    stb_vorbis_info info;
    stb_vorbis *vorbis = NULL;
    int err = 0, i;
    *error = 0;
    if (!bytes || size <= 0) {
        goto fail;
    }

    vorbis = stb_vorbis_open_memory(bytes, size, &err, NULL);
    if (!vorbis) {
        *error = err ? err : -1;
        goto fail;
    }

    info = stb_vorbis_get_info(vorbis);
    if (info.channels <= 0 || info.channels > BOX_AUDIO_VORBIS_CHANNELS ||
        info.sample_rate <= 0)
    {
        *error = -1;
        goto fail;
    }

    result = ecs_os_calloc_t(box_audio_vorbis_t);
    result->vorbis = vorbis;
    result->bytes = bytes;
    result->channels = info.channels;
    result->out_channels = info.channels > 2 ? 2 : info.channels;
    result->rate = (float)info.sample_rate;
    result->frames = (int32_t)stb_vorbis_stream_length_in_samples(vorbis);
    for (i = 0; i < info.channels; i ++) {
        result->temp[i] = ecs_os_malloc_n(float, BOX_AUDIO_MUSIC_STEP);
    }

    stb_vorbis_seek_start(vorbis);
    goto done;
fail:
    if (vorbis) {
        stb_vorbis_close(vorbis);
    }

    ecs_os_free(bytes);
done:
    return result;
}

void box_audio_vorbis_close(
    box_audio_vorbis_t *v)
{
    int32_t i;
    if (!v) {
        goto done;
    }

    stb_vorbis_close(v->vorbis);
    for (i = 0; i < v->channels; i ++) {
        ecs_os_free(v->temp[i]);
    }

    ecs_os_free(v->bytes);
    ecs_os_free(v);
done:
    return;
}

int32_t box_audio_vorbis_frames(
    const box_audio_vorbis_t *v)
{
    return v->frames;
}

int32_t box_audio_vorbis_channels(
    const box_audio_vorbis_t *v)
{
    return v->out_channels;
}

float box_audio_vorbis_rate(
    const box_audio_vorbis_t *v)
{
    return v->rate;
}

int64_t box_audio_vorbis_position(
    const box_audio_vorbis_t *v)
{
    return v->position;
}

bool box_audio_vorbis_seek(
    box_audio_vorbis_t *v,
    int64_t frame)
{
    bool result;
    if (v->frames > 0) {
        frame %= v->frames;
        frame = frame < 0 ? frame + v->frames : frame;
    } else {
        frame = 0;
    }

    result = frame ? stb_vorbis_seek(v->vorbis, (unsigned int)frame) != 0 :
        stb_vorbis_seek_start(v->vorbis) != 0;
    v->position = result ? frame : 0;
    if (!result) {
        stb_vorbis_seek_start(v->vorbis);
    }

    return result;
}

/** Write decoded frames of the temporary channels interleaved, more than two mixed down: each
 * side is the mean of the channels on it, a center counts on both and the LFE on none.
 *
 * @param v The decoder
 * @param out Receives the frames
 * @param frames Number of frames
 */
static void box_audio_vorbis_write(
    const box_audio_vorbis_t *v,
    float *out,
    int32_t frames)
{
    int32_t i, c, left = 0, right = 0;
    if (v->channels == 1) {
        memcpy(out, v->temp[0], (size_t)frames * sizeof(float));
    } else if (v->channels == 2) {
        for (i = 0; i < frames; i ++) {
            out[i * 2] = v->temp[0][i];
            out[i * 2 + 1] = v->temp[1][i];
        }
    } else {
        const int8_t *sides = box_audio_vorbis_sides[v->channels];
        for (c = 0; c < v->channels; c ++) {
            left += sides[c] <= 0;
            right += sides[c] == 0 || sides[c] == 1;
        }

        for (i = 0; i < frames; i ++) {
            float l = 0, r = 0;
            for (c = 0; c < v->channels; c ++) {
                if (sides[c] <= 0) {
                    l += v->temp[c][i];
                }

                if (sides[c] == 0 || sides[c] == 1) {
                    r += v->temp[c][i];
                }
            }

            out[i * 2] = left ? l / (float)left : 0.0f;
            out[i * 2 + 1] = right ? r / (float)right : 0.0f;
        }
    }
}

int32_t box_audio_vorbis_read(
    box_audio_vorbis_t *v,
    float *out,
    int32_t frames,
    bool loop)
{
    int32_t done = 0;
    bool restarted = false;
    while (done < frames) {
        int32_t n = frames - done < BOX_AUDIO_MUSIC_STEP ? frames - done : BOX_AUDIO_MUSIC_STEP;
        int32_t got = stb_vorbis_get_samples_float(v->vorbis, v->channels, v->temp, n);
        got = got > 0 ? got : 0;
        if (got) {
            box_audio_vorbis_write(v, out + (int64_t)done * v->out_channels, got);
            done += got;
            v->position += got;
            restarted = false;
        }

        if (got < n) {
            if (!loop || restarted || !stb_vorbis_seek_start(v->vorbis)) {
                break;
            }

            v->position = 0;
            restarted = true;
        }
    }

    return done;
}
