/**
 * @file audio/wavetable.c
 * @brief Builds wavetables on the main thread: the harmonics of each frame into one band
 * limited period per mip level, with an inverse FFT, in the layout the core reads; the
 * built-in tables of WavetableKind; and the tables of a sound of single cycle frames.
 *
 * The harmonics of a frame of a sound are the inverse FFT of the real period, the conjugate of
 * its forward FFT, whose real and imaginary parts are the sums by the cosine and the sine.
 *
 * Level k of a frame keeps the harmonics up to BOX_AUDIO_TABLE_HARMONICS >> k in
 * BOX_AUDIO_TABLE_LEVEL_LENGTH(k) samples, so the core picks the first level whose highest
 * harmonic stays under Nyquist. Each harmonic is divided by the gain of the interpolator.
 */

#include "module.h"
#include <math.h>

/** Pi */
#define BOX_AUDIO_WAVETABLE_PI 3.14159265358979323846

/** Frames of WavetableBasic, WavetablePulse and WavetableHarmonics */
#define BOX_AUDIO_WAVETABLE_BASIC 4
#define BOX_AUDIO_WAVETABLE_PULSE 16
#define BOX_AUDIO_WAVETABLE_HARMONICS 32

/** Widths of the pulse of the first and the last frame of WavetablePulse */
#define BOX_AUDIO_WAVETABLE_PULSE_WIDE 0.5
#define BOX_AUDIO_WAVETABLE_PULSE_NARROW 0.05

/** Steps of the integral of the gain of the interpolator of the core */
#define BOX_AUDIO_WAVETABLE_STEPS 128

/** Gains of the interpolator of the core at each harmonic of each level, 0 until computed */
static double box_audio_wavetable_gains[BOX_AUDIO_TABLE_LEVELS][BOX_AUDIO_TABLE_HARMONICS + 1];

/** Weights of the 4 samples around a read point of the interpolator of the core,
 * box_audio_synth_table_tap: the optimal 4 point 3rd order interpolator for audio oversampled
 * twice of Niemitalo.
 *
 * @param x Place of the read point after the second sample, 0..1
 * @param w Receives the weights of the samples before, at, after and two after it
 */
static void box_audio_wavetable_weights(
    double x,
    double *w)
{
    double z = x - 0.5;
    int32_t j;
    for (j = 0; j < 4; j ++) {
        double ym1 = j == 0, y0 = j == 1, y1 = j == 2, y2 = j == 3;
        double even1 = y1 + y0, odd1 = y1 - y0, even2 = y2 + ym1, odd2 = y2 - ym1;
        double c0 = even1 * 0.45868970870461956 + even2 * 0.04131401926395584;
        double c1 = odd1 * 0.48068024766578432 + odd2 * 0.17577925564495955;
        double c2 = even1 * -0.246185007019907091 + even2 * 0.24614027139700284;
        double c3 = odd1 * -0.36030925263849456 + odd2 * 0.10174985775982505;
        w[j] = ((c3 * z + c2) * z + c1) * z + c0;
    }
}

/** Compute the gain of the interpolator of the core at each harmonic of each level once: the
 * transform of its kernel at the frequency of the harmonic in cycles per sample of the level,
 * the amplitude at which it reads a sinusoid there.
 */
static void box_audio_wavetable_gains_init(void)
{
    double w[BOX_AUDIO_WAVETABLE_STEPS][4], r, g, x;
    int32_t level, n, q, j;
    if (box_audio_wavetable_gains[0][1] != 0) {
        goto done;
    }

    for (q = 0; q < BOX_AUDIO_WAVETABLE_STEPS; q ++) {
        box_audio_wavetable_weights(((double)q + 0.5) / BOX_AUDIO_WAVETABLE_STEPS, w[q]);
    }

    for (level = 0; level < BOX_AUDIO_TABLE_LEVELS; level ++) {
        for (n = 1; n <= BOX_AUDIO_TABLE_HARMONICS >> level; n ++) {
            r = (double)n / (double)BOX_AUDIO_TABLE_LEVEL_LENGTH(level);
            g = 0;
            for (q = 0; q < BOX_AUDIO_WAVETABLE_STEPS; q ++) {
                x = ((double)q + 0.5) / BOX_AUDIO_WAVETABLE_STEPS;
                for (j = 0; j < 4; j ++) {
                    g += w[q][j] * cos(2 * BOX_AUDIO_WAVETABLE_PI * r * (x - (double)(j - 1)));
                }
            }

            box_audio_wavetable_gains[level][n] = g / BOX_AUDIO_WAVETABLE_STEPS;
        }
    }

done:
    return;
}

/** Inverse FFT of a power of 2 of complex values in place, radix 2: x[t] = sum X[n] e^(2 pi i n
 * t / len), unscaled.
 *
 * @param re Real parts
 * @param im Imaginary parts
 * @param len Values, a power of 2
 */
static void box_audio_wavetable_ifft(
    double *re,
    double *im,
    int32_t len)
{
    int32_t i, j, k, m, half;
    double tr, ti, wr, wi, sr, si, t;
    for (i = 1, j = 0; i < len; i ++) {
        int32_t bit = len >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }

        j ^= bit;
        if (i < j) {
            t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }

    for (m = 2; m <= len; m <<= 1) {
        half = m >> 1;
        for (k = 0; k < half; k ++) {
            wr = cos(2 * BOX_AUDIO_WAVETABLE_PI * (double)k / (double)m);
            wi = sin(2 * BOX_AUDIO_WAVETABLE_PI * (double)k / (double)m);
            for (i = k; i < len; i += m) {
                j = i + half;
                sr = re[j] * wr - im[j] * wi;
                si = re[j] * wi + im[j] * wr;
                tr = re[i];
                ti = im[i];
                re[i] = tr + sr;
                im[i] = ti + si;
                re[j] = tr - sr;
                im[j] = ti - si;
            }
        }
    }
}

float* box_audio_wavetable_build(
    int32_t frames,
    const double *cosines,
    const double *sines)
{
    double re[BOX_AUDIO_TABLE_LENGTH], im[BOX_AUDIO_TABLE_LENGTH];
    float *result = NULL, *out;
    int32_t level, j, n, t, len, top, offset = 0;
    if (frames < 1 || frames > BOX_AUDIO_TABLE_FRAMES_MAX || !cosines || !sines) {
        goto done;
    }

    box_audio_wavetable_gains_init();
    result = ecs_os_malloc_n(float, BOX_AUDIO_TABLE_FLOATS(frames));
    result[0] = BOX_AUDIO_TABLE_MAGIC;
    result[1] = (float)frames;
    result[2] = (float)BOX_AUDIO_TABLE_LEVELS;
    result[3] = (float)BOX_AUDIO_TABLE_LENGTH;
    out = result + BOX_AUDIO_TABLE_HEADER;
    for (level = 0; level < BOX_AUDIO_TABLE_LEVELS; level ++) {
        len = BOX_AUDIO_TABLE_LEVEL_LENGTH(level);
        top = BOX_AUDIO_TABLE_HARMONICS >> level;
        for (j = 0; j < frames; j ++) {
            const double *a = cosines + j * (BOX_AUDIO_TABLE_HARMONICS + 1);
            const double *b = sines + j * (BOX_AUDIO_TABLE_HARMONICS + 1);
            for (t = 0; t < len; t ++) {
                re[t] = 0;
                im[t] = 0;
            }

            for (n = 1; n <= top && n < len / 2; n ++) {
                double g = 0.5 / box_audio_wavetable_gains[level][n];
                re[n] = g * a[n];
                im[n] = -g * b[n];
                re[len - n] = g * a[n];
                im[len - n] = g * b[n];
            }

            box_audio_wavetable_ifft(re, im, len);
            for (t = 0; t < len; t ++) {
                out[offset + j * len + t] = (float)re[t];
            }
        }

        offset += frames * len;
    }

done:
    return result;
}

int32_t box_audio_wavetable_frames(
    int32_t kind)
{
    return kind == WavetablePulse ? BOX_AUDIO_WAVETABLE_PULSE :
        (kind == WavetableHarmonics ? BOX_AUDIO_WAVETABLE_HARMONICS : BOX_AUDIO_WAVETABLE_BASIC);
}

float* box_audio_wavetable_kind(
    int32_t kind,
    int32_t *floats)
{
    int32_t frames = box_audio_wavetable_frames(kind), j, n, stride = BOX_AUDIO_TABLE_HARMONICS + 1;
    double *a = ecs_os_calloc_n(double, frames * stride);
    double *b = ecs_os_calloc_n(double, frames * stride);
    double pi = BOX_AUDIO_WAVETABLE_PI, w;
    float *result;
    for (j = 0; j < frames; j ++) {
        double *aj = a + j * stride, *bj = b + j * stride;
        for (n = 1; n <= BOX_AUDIO_TABLE_HARMONICS; n ++) {
            if (kind == WavetablePulse) {
                w = BOX_AUDIO_WAVETABLE_PULSE_WIDE - (BOX_AUDIO_WAVETABLE_PULSE_WIDE -
                    BOX_AUDIO_WAVETABLE_PULSE_NARROW) * (double)j / (double)(frames - 1);
                aj[n] = 2 * sin(2 * pi * n * w) / (pi * n);
                bj[n] = 2 * (1 - cos(2 * pi * n * w)) / (pi * n);
            } else if (kind == WavetableHarmonics) {
                bj[n] = n <= j + 1 ? -2 / (pi * n) : 0.0;
            } else if (j == 0) {
                bj[n] = n == 1 ? 1.0 : 0.0;
            } else if (j == 1) {
                aj[n] = n % 2 ? -8 / (pi * pi * n * n) : 0.0;
            } else if (j == 2) {
                bj[n] = -2 / (pi * n);
            } else {
                bj[n] = n % 2 ? 4 / (pi * n) : 0.0;
            }
        }
    }

    result = box_audio_wavetable_build(frames, a, b);
    *floats = BOX_AUDIO_TABLE_FLOATS(frames);
    ecs_os_free(a);
    ecs_os_free(b);

    return result;
}

int32_t box_audio_wavetable_frame_size(
    int32_t frame_size)
{
    return frame_size <= 0 ? BOX_AUDIO_WAVETABLE_FRAME_SIZE :
        (frame_size < BOX_AUDIO_WAVETABLE_FRAME_SIZE_MIN ? BOX_AUDIO_WAVETABLE_FRAME_SIZE_MIN :
        (frame_size > BOX_AUDIO_WAVETABLE_FRAME_SIZE_MAX ? BOX_AUDIO_WAVETABLE_FRAME_SIZE_MAX :
        frame_size));
}

/** Resample one period to a power of 2 of samples with a periodic Catmull-Rom spline.
 *
 * @param x Samples of the period
 * @param size Samples of the period
 * @param out Receives the resampled period
 * @param len Samples of out, at least size
 */
static void box_audio_wavetable_resample(
    const double *x,
    int32_t size,
    double *out,
    int32_t len)
{
    int32_t t, i;
    for (t = 0; t < len; t ++) {
        double p = (double)t * (double)size / (double)len, f, y0, y1, y2, y3;
        i = (int32_t)p;
        f = p - (double)i;
        y0 = x[(i + size - 1) % size];
        y1 = x[i % size];
        y2 = x[(i + 1) % size];
        y3 = x[(i + 2) % size];
        out[t] = y1 + 0.5 * f * (y2 - y0 + f * (2 * y0 - 5 * y1 + 4 * y2 - y3 +
            f * (3 * (y1 - y2) + y3 - y0)));
    }
}

float* box_audio_wavetable_load(
    const float *data,
    int32_t frames,
    int32_t channels,
    int32_t frame_size,
    int32_t *table_frames,
    int32_t *floats)
{
    int32_t count, len, top, j, t, c, n, stride = BOX_AUDIO_TABLE_HARMONICS + 1;
    double *x = NULL, *re = NULL, *im = NULL, *a = NULL, *b = NULL, peak = 0, scale;
    float *result = NULL;
    *table_frames = 0;
    *floats = 0;
    if (!data || channels < 1 || frame_size < 1 || frames < frame_size) {
        goto done;
    }

    count = frames / frame_size;
    count = count > BOX_AUDIO_TABLE_FRAMES_MAX ? BOX_AUDIO_TABLE_FRAMES_MAX : count;
    for (len = 1; len < frame_size; len <<= 1) { }
    top = (frame_size - 1) / 2;
    top = top > BOX_AUDIO_TABLE_HARMONICS ? BOX_AUDIO_TABLE_HARMONICS : top;

    x = ecs_os_malloc_n(double, count * frame_size);
    for (j = 0; j < count; j ++) {
        double *xj = x + j * frame_size, mean = 0;
        for (t = 0; t < frame_size; t ++) {
            const float *s = data + ((int64_t)j * frame_size + t) * channels;
            double v = 0;
            for (c = 0; c < channels; c ++) {
                v += (double)s[c];
            }

            xj[t] = isfinite(v) ? v : 0.0;
            mean += xj[t];
        }

        mean /= (double)frame_size;
        for (t = 0; t < frame_size; t ++) {
            xj[t] -= mean;
            peak = fabs(xj[t]) > peak ? fabs(xj[t]) : peak;
        }
    }

    scale = peak > 0 ? 2.0 / ((double)len * peak) : 0.0;
    re = ecs_os_malloc_n(double, len);
    im = ecs_os_malloc_n(double, len);
    a = ecs_os_calloc_n(double, count * stride);
    b = ecs_os_calloc_n(double, count * stride);
    for (j = 0; j < count; j ++) {
        const double *xj = x + j * frame_size;
        if (len == frame_size) {
            for (t = 0; t < len; t ++) {
                re[t] = xj[t];
            }
        } else {
            box_audio_wavetable_resample(xj, frame_size, re, len);
        }

        for (t = 0; t < len; t ++) {
            im[t] = 0;
        }

        box_audio_wavetable_ifft(re, im, len);
        for (n = 1; n <= top; n ++) {
            a[j * stride + n] = scale * re[n];
            b[j * stride + n] = scale * im[n];
        }
    }

    result = box_audio_wavetable_build(count, a, b);
    if (result) {
        *table_frames = count;
        *floats = BOX_AUDIO_TABLE_FLOATS(count);
    }

done:
    ecs_os_free(x);
    ecs_os_free(re);
    ecs_os_free(im);
    ecs_os_free(a);
    ecs_os_free(b);

    return result;
}

float* box_audio_wavetable_of(
    ecs_world_t *world,
    ecs_entity_t e,
    const Wavetable *table,
    AudioWavetable *w,
    int32_t *frames,
    int32_t *floats,
    bool *loading)
{
    const box_audio_sample_t *sample = NULL;
    const AudioPcm *pcm = NULL;
    const float *data = NULL;
    int32_t size = box_audio_wavetable_frame_size(table->frame_size);
    int32_t length = 0, channels = 1;
    char *why = NULL;
    float *result = NULL;
    *frames = 0;
    *floats = 0;
    *loading = false;
    if (!table->sound) {
        result = box_audio_wavetable_kind((int32_t)table->kind, floats);
        *frames = box_audio_wavetable_frames((int32_t)table->kind);
        goto done;
    }

    if (ecs_is_alive(world, table->sound)) {
        sample = box_audio_sample_of(world, table->sound);
        pcm = sample ? NULL : ecs_get(world, table->sound, AudioPcm);
    }

    if (sample && sample->state == SoundLoading) {
        *loading = true;
        goto done;
    }

    if (sample && sample->state == SoundReady) {
        data = sample->data;
        length = sample->frames;
        channels = sample->channels;
    } else if (sample) {
        why = flecs_asprintf("it failed to load: %s",
            sample->error ? sample->error : "unknown error");
    } else if (pcm) {
        channels = pcm->channels > 0 ? pcm->channels : 1;
        data = ecs_vec_first_t(&pcm->samples, float);
        length = ecs_vec_count(&pcm->samples) / channels;
    } else {
        why = ecs_os_strdup("it is not an entity with Pcm or SoundFile");
    }

    if (!why && length < size) {
        why = flecs_asprintf("its %d frames are shorter than one frame of %d samples",
            (int)length, (int)size);
    }

    if (!why) {
        result = box_audio_wavetable_load(data, length, channels, size, frames, floats);
        if (!result) {
            why = ecs_os_strdup("its frames make no table");
        }
    }

    if (!w->logged && (why || length / size > BOX_AUDIO_TABLE_FRAMES_MAX)) {
        char *path = ecs_get_path(world, e);
        char *sound = table->sound && ecs_is_alive(world, table->sound) ?
            ecs_get_path(world, table->sound) : NULL;
        if (why) {
            ecs_err("box.audio: the sound '%s' of wavetable '%s' makes no table, %s; its "
                "layers play a sine", sound ? sound : "0", path, why);
        } else {
            ecs_warn("box.audio: the sound '%s' of wavetable '%s' has %d frames of %d "
                "samples, the table keeps the first %d", sound ? sound : "0", path,
                (int)(length / size), (int)size, BOX_AUDIO_TABLE_FRAMES_MAX);
        }

        w->logged = true;
        ecs_os_free(sound);
        ecs_os_free(path);
    }

    ecs_os_free(why);

done:
    return result;
}

/** Build the Wavetables of a sound again when it becomes ready, changes or is removed: release
 * the table each sent, which voices that play it keep until they end, so the next sync builds
 * it from the sound and the synth sounds that name it take the new id.
 *
 * @param it The iterator of the observer
 */
static void BoxAudioWavetableSound(
    ecs_iter_t *it)
{
    box_audio_module_t *state = it->ctx;
    ecs_iter_t q;
    int32_t i, j;
    if (ecs_is_fini(it->world) || !state->wavetables) {
        goto done;
    }

    q = ecs_query_iter(it->world, state->wavetables);
    while (ecs_query_next(&q)) {
        const Wavetable *table = ecs_field(&q, Wavetable, 0);
        AudioWavetable *w = ecs_field(&q, AudioWavetable, 1);
        for (j = 0; j < q.count; j ++) {
            for (i = 0; i < it->count; i ++) {
                if (!table[j].sound || table[j].sound != it->entities[i]) {
                    continue;
                }

                if (w[j].id && w[j].serial == state->serial && state->serial) {
                    ecs_vec_append_t(NULL, &state->releases, uint32_t)[0] = w[j].id;
                }

                w[j].id = 0;
                w[j].serial = 0;
                w[j].frames = 0;
                w[j].logged = false;
                break;
            }
        }
    }

done:
    return;
}

void box_audio_wavetable_init(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "WavetablePcm" }),
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .query.terms = {{ .id = ecs_id(AudioPcm), .src.id = EcsSelf }},
        .events = { EcsOnSet, EcsOnRemove },
        .callback = BoxAudioWavetableSound,
        .ctx = state
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "WavetableSoundStatus" }),
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .query.terms = {{ .id = ecs_id(SoundStatus), .src.id = EcsSelf }},
        .events = { EcsOnSet, EcsOnRemove },
        .callback = BoxAudioWavetableSound,
        .ctx = state
    });
}
