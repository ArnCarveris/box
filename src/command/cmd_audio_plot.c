/**
 * @file command/cmd_audio_plot.c
 * @brief The audio plot action: a PNG of a WAV, a resource of the session or a file, as a
 * spectrogram, waveform, average spectrum or loudness over time, with labeled axes and the
 * bars of a capture of a song.
 *
 * The image is drawn on the CPU by audio/plot.c, so it works headless and in the browser.
 * The same WAV and options give the same PNG bytes.
 */

#include "command.h"
#include "../audio/plot.h"
#include <stb_image_write.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/** Folder of the plots in the file system of the page */
#define BOX_CMD_PLOT_WEB_DIR "/tmp"

/** Lowest min_db of a plot */
#define BOX_CMD_PLOT_MIN_DB (-200.0)

/** Bytes of an encoded PNG */
typedef struct box_cmd_plot_png_t {
    uint8_t *data;          /**< The bytes */
    int32_t size;           /**< Bytes written */
    int32_t capacity;       /**< Bytes allocated */
    bool failed;            /**< Out of memory */
} box_cmd_plot_png_t;

/** The options of a plot */
typedef struct box_cmd_plot_opts_t {
    box_audio_plot_kind_t kind; /**< The kind */
    int32_t width;          /**< Width in pixels */
    int32_t height;         /**< Height in pixels */
    double from;            /**< First second */
    double to;              /**< Last second, -1 for the end */
    float min_db;           /**< Bottom of the scale, 0 for the default */
} box_cmd_plot_opts_t;

/** Append bytes of the PNG, the write function of stb_image_write.
 *
 * @param ctx The PNG
 * @param data The bytes
 * @param size Number of bytes
 */
static void box_cmd_plot_png_write(
    void *ctx,
    void *data,
    int size)
{
    box_cmd_plot_png_t *png = ctx;
    if (png->failed || size <= 0) {
        goto done;
    }

    if (png->size + size > png->capacity) {
        int32_t capacity = png->capacity ? png->capacity : 64 * 1024;
        while (capacity < png->size + size) {
            capacity *= 2;
        }

        uint8_t *grown = ecs_os_realloc(png->data, capacity);
        if (!grown) {
            png->failed = true;
            goto done;
        }

        png->data = grown;
        png->capacity = capacity;
    }

    ecs_os_memcpy(png->data + png->size, data, size);
    png->size += size;
done:
    return;
}

/** Parse a number of seconds of the span.
 *
 * @param text The text
 * @param out Receives the seconds
 * @return Whether the text is a number of seconds of at least 0
 */
static bool box_cmd_plot_seconds(
    const char *text,
    double *out)
{
    char *end = NULL;
    double v = strtod(text, &end);
    bool result = text[0] && end && !end[0] && v >= 0 && isfinite(v);
    *out = result ? v : *out;

    return result;
}

/** Parse the kind and the options of a plot.
 *
 * @param argc Number of arguments
 * @param argv The kind and the options, after the WAV
 * @param opts Receives the options
 * @return 0 on success, -1 with an error logged
 */
static int box_cmd_plot_parse(
    int32_t argc,
    const char **argv,
    box_cmd_plot_opts_t *opts)
{
    int32_t i;
    int result = -1;
    for (i = 0; i < argc; i ++) {
        const char *a = argv[i];
        char *end = NULL;
        if (!a || !a[0]) {
            continue;
        } else if (!ecs_os_strcmp(a, "spectrogram")) {
            opts->kind = BoxAudioPlotSpectrogram;
        } else if (!ecs_os_strcmp(a, "waveform")) {
            opts->kind = BoxAudioPlotWaveform;
        } else if (!ecs_os_strcmp(a, "spectrum")) {
            opts->kind = BoxAudioPlotSpectrum;
        } else if (!ecs_os_strcmp(a, "loudness")) {
            opts->kind = BoxAudioPlotLoudness;
        } else if (!ecs_os_strncmp(a, "size=", 5)) {
            long w = strtol(a + 5, &end, 10), h = 0;
            if (end && (end[0] == 'x' || end[0] == 'X')) {
                h = strtol(end + 1, &end, 10);
            }

            if (!end || end[0] || w < BOX_AUDIO_PLOT_MIN_WIDTH ||
                h < BOX_AUDIO_PLOT_MIN_HEIGHT || w > BOX_AUDIO_PLOT_MAX_SIZE ||
                h > BOX_AUDIO_PLOT_MAX_SIZE)
            {
                ecs_err("invalid size '%s', expected WxH of %dx%d to %dx%d pixels", a + 5,
                    BOX_AUDIO_PLOT_MIN_WIDTH, BOX_AUDIO_PLOT_MIN_HEIGHT,
                    BOX_AUDIO_PLOT_MAX_SIZE, BOX_AUDIO_PLOT_MAX_SIZE);
                goto done;
            }

            opts->width = (int32_t)w;
            opts->height = (int32_t)h;
        } else if (!ecs_os_strncmp(a, "from=", 5)) {
            if (!box_cmd_plot_seconds(a + 5, &opts->from)) {
                ecs_err("invalid from '%s', expected seconds", a + 5);
                goto done;
            }
        } else if (!ecs_os_strncmp(a, "to=", 3)) {
            if (!box_cmd_plot_seconds(a + 3, &opts->to)) {
                ecs_err("invalid to '%s', expected seconds", a + 3);
                goto done;
            }
        } else if (!ecs_os_strncmp(a, "min_db=", 7)) {
            double v = strtod(a + 7, &end);
            if (!end || end[0] || !(v < 0) || v < BOX_CMD_PLOT_MIN_DB) {
                ecs_err("invalid min_db '%s', expected dB below 0 and at least %.0f", a + 7,
                    BOX_CMD_PLOT_MIN_DB);
                goto done;
            }

            opts->min_db = (float)v;
        } else {
            ecs_err("unknown plot argument '%s', expected spectrogram, waveform, spectrum, "
                "loudness, size=WxH, from=seconds, to=seconds or min_db=dB", a);
            goto done;
        }
    }

    result = 0;
done:
    return result;
}

/** Store the PNG of a plot: a resource of the session, a file of the page in the browser.
 *
 * @param world The world
 * @param png The bytes
 * @return The name of the resource or the file, which the caller frees, NULL on an error
 */
static char* box_cmd_plot_store(
    ecs_world_t *world,
    const box_cmd_plot_png_t *png)
{
    char *path = NULL, *result = NULL;
    FILE *f = NULL;
#ifdef __EMSCRIPTEN__
    static int32_t box_cmd_audio_plots;
    (void)world;
    path = flecs_asprintf("%s/audio_plot_%d.png", BOX_CMD_PLOT_WEB_DIR, ++ box_cmd_audio_plots);
#else
    ecs_shell_resource_t resource = {0};
    path = box_cmd_render_resource(world, "image/png", &resource);
#endif
    if (!path) {
        goto done;
    }

    f = fopen(path, "wb");
    if (!f || fwrite(png->data, 1, (size_t)png->size, f) != (size_t)png->size) {
        ecs_err("cannot write the plot '%s'", path);
        goto done;
    }

#ifdef __EMSCRIPTEN__
    result = path;
    path = NULL;
#else
    result = ecs_os_strdup(resource.name);
#endif
done:
    if (f) {
        fclose(f);
    }

#ifndef __EMSCRIPTEN__
    ecs_os_free(resource.name);
    ecs_os_free(resource.content_type);
#endif
    ecs_os_free(path);

    return result;
}

char* box_cmd_audio_plot(
    ecs_world_t *world,
    int32_t argc,
    const char **argv)
{
    box_cmd_plot_opts_t opts = { .kind = BoxAudioPlotSpectrogram, .to = -1 };
    box_audio_wav_t file = {0};
    box_audio_plot_desc_t desc = {0};
    box_audio_plot_t plot = {0};
    box_cmd_plot_png_t png = {0};
    const char *wav = argc > 0 ? argv[0] : NULL, *error;
    uint8_t *bytes = NULL;
    int64_t size = 0;
    double length, to;
    int32_t start, end;
    char *title = NULL, *result = NULL;
    if (!wav || !wav[0]) {
        ecs_err("audio plot needs a WAV: a resource of the session or a file");
        goto done;
    }

    if (box_cmd_plot_parse(argc - 1, argv + 1, &opts)) {
        goto done;
    }

    bytes = box_cmd_audio_wav_load(world, wav, &size);
    if (!bytes) {
        goto done;
    }

    error = box_audio_wav_read(bytes, (size_t)size, &file);
    if (error) {
        ecs_err("cannot plot '%s': %s", wav, error);
        goto done;
    }

    length = file.frames / (double)file.sample_rate;
    to = opts.to < 0 || opts.to > length ? length : opts.to;
    start = (int32_t)floor(opts.from * file.sample_rate + 0.5);
    end = (int32_t)floor(to * file.sample_rate + 0.5);
    end = end < file.frames ? end : file.frames;
    if (end <= start) {
        ecs_err("empty span %.3f to %.3f s of '%s', which is %.3f s long", opts.from, to, wav,
            length);
        goto done;
    }

    title = flecs_asprintf("%s %s, %.0f Hz %s, %.3f to %.3f s",
        box_audio_plot_kind_name(opts.kind), wav, (double)file.sample_rate,
        file.channels == 1 ? "mono" : "stereo", start / (double)file.sample_rate,
        end / (double)file.sample_rate);
    desc = (box_audio_plot_desc_t){
        .kind = opts.kind,
        .data = file.data + (int64_t)start * file.channels,
        .frames = end - start,
        .channels = file.channels,
        .sample_rate = file.sample_rate,
        .start = start / (double)file.sample_rate,
        .width = opts.width,
        .height = opts.height,
        .min_db = opts.min_db,
        .title = title,
        .has_song = file.has_song,
        .song = file.song
    };

    if (box_audio_plot(&desc, &plot)) {
        ecs_err("cannot plot '%s': out of memory", wav);
        goto done;
    }

    if (!stbi_write_png_to_func(box_cmd_plot_png_write, &png, plot.width, plot.height, 3,
        plot.pixels, plot.width * 3) || png.failed)
    {
        ecs_err("cannot plot '%s': cannot encode the PNG", wav);
        goto done;
    }

    result = box_cmd_plot_store(world, &png);
done:
    ecs_os_free(png.data);
    ecs_os_free(title);
    ecs_os_free(bytes);
    box_audio_plot_fini(&plot);
    box_audio_wav_fini(&file);

    return result;
}
