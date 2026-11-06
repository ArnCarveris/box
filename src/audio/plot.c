/**
 * @file audio/plot.c
 * @brief Images of recordings: a spectrogram, a waveform, an average spectrum and the
 * loudness over time, drawn into RGB pixels with axes labeled in a 5x7 bitmap font.
 *
 * Plain C without Flecs or platform code. The FFT, the Hann window and the K-weighting are
 * those of analyze.c. Buffers are allocated once per plot, never in a loop that runs per
 * sample. The same input gives the same pixels.
 */

#include "plot.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Background of the image */
#define BOX_PLOT_BG (0x121216)

/** Background of the data area of a waveform, spectrum and loudness plot */
#define BOX_PLOT_AREA (0x1A1A20)

/** Grid lines */
#define BOX_PLOT_GRID (0x34343E)

/** Frame of the data area and the ticks */
#define BOX_PLOT_FRAME (0x6A6A74)

/** Text */
#define BOX_PLOT_TEXT (0xD0D0D8)

/** Bar lines */
#define BOX_PLOT_BAR (0xE0B040)

/** Beat lines */
#define BOX_PLOT_BEAT (0x8A7448)

/** Line of a spectrum */
#define BOX_PLOT_LINE (0x8CC8FF)

/** Area under the line of a spectrum */
#define BOX_PLOT_FILL (0x284A6C)

/** Band edges of a spectrum */
#define BOX_PLOT_BAND (0x58A858)

/** Short-term loudness */
#define BOX_PLOT_SHORT (0xFFB050)

/** Momentary loudness */
#define BOX_PLOT_MOMENTARY (0x7A5E3A)

/** Hops of a second of the loudness plot */
#define BOX_PLOT_HOPS_PER_S (100)

/** Hops of the short-term loudness window of 3 s */
#define BOX_PLOT_SHORT_HOPS (300)

/** Hops of the momentary loudness window of 400 ms */
#define BOX_PLOT_MOMENTARY_HOPS (40)

/** Shortest window of the RMS envelope of a waveform in seconds */
#define BOX_PLOT_RMS_WINDOW (0.02)

/** First character of the font */
#define BOX_PLOT_FONT_FIRST (32)

/** Characters of the font */
#define BOX_PLOT_FONT_COUNT (95)

/** The 5x7 font of printable ASCII, 5 columns per character, bit 0 the top row */
static const uint8_t box_plot_font[BOX_PLOT_FONT_COUNT][5] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14}, {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00}, {0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00}, {0x08,0x2A,0x1C,0x2A,0x08}, {0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x60,0x60,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02}, {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31}, {0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}, {0x00,0x36,0x36,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00}, {0x08,0x14,0x22,0x41,0x00}, {0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x51,0x09,0x06}, {0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}, {0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20}, {0x00,0x41,0x41,0x7F,0x00}, {0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40}, {0x00,0x01,0x02,0x04,0x00}, {0x20,0x54,0x54,0x54,0x78},
    {0x7F,0x48,0x44,0x44,0x38}, {0x38,0x44,0x44,0x44,0x20}, {0x38,0x44,0x44,0x48,0x7F},
    {0x38,0x54,0x54,0x54,0x18}, {0x08,0x7E,0x09,0x01,0x02}, {0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,0x08,0x04,0x04,0x78}, {0x00,0x44,0x7D,0x40,0x00}, {0x20,0x40,0x44,0x3D,0x00},
    {0x7F,0x10,0x28,0x44,0x00}, {0x00,0x41,0x7F,0x40,0x00}, {0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78}, {0x38,0x44,0x44,0x44,0x38}, {0x7C,0x14,0x14,0x14,0x08},
    {0x08,0x14,0x14,0x18,0x7C}, {0x7C,0x08,0x04,0x04,0x08}, {0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20}, {0x3C,0x40,0x40,0x20,0x7C}, {0x1C,0x20,0x40,0x20,0x1C},
    {0x3C,0x40,0x30,0x40,0x3C}, {0x44,0x28,0x10,0x28,0x44}, {0x0C,0x50,0x50,0x50,0x3C},
    {0x44,0x64,0x54,0x4C,0x44}, {0x00,0x08,0x36,0x41,0x00}, {0x00,0x00,0x7F,0x00,0x00},
    {0x00,0x41,0x36,0x08,0x00}, {0x08,0x04,0x08,0x10,0x08}
};

/** Stops of the color scale of a spectrogram, dark to bright, with a rising luminance */
static const uint32_t box_plot_scale[5] = {
    0x000000, 0x3C0F6E, 0xB4285A, 0xFA8C1E, 0xFFFFC8
};

/** The frequencies of the labels of a frequency axis */
static const float box_plot_hz_major[10] = {
    20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000
};

/** The labels of the frequencies of box_plot_hz_major */
static const char *box_plot_hz_labels[10] = {
    "20 Hz", "50 Hz", "100 Hz", "200 Hz", "500 Hz", "1 kHz", "2 kHz", "5 kHz", "10 kHz",
    "20 kHz"
};

/** What a plot is drawn with: the image, its font size and its axes */
typedef struct box_plot_ctx_t {
    const box_audio_plot_desc_t *desc; /**< What to plot */
    box_audio_plot_t *p;    /**< The image */
    int32_t s;              /**< Scale of the font */
    int32_t cw;             /**< Width of a character with its gap */
    int32_t ch;             /**< Height of a line of text */
    double t0;              /**< Seconds at the left of the data area */
    double t1;              /**< Seconds at the right of the data area */
} box_plot_ctx_t;

/** The bins of the FFT a row or column of a log frequency axis covers */
typedef struct box_plot_bins_t {
    int32_t lo;             /**< First bin, -1 above the Nyquist frequency */
    int32_t hi;             /**< Last bin, lo - 1 when no bin is inside: then interpolate */
    float at;               /**< Fractional bin of the center, to interpolate */
} box_plot_bins_t;

/** Set a pixel, nothing outside the image.
 *
 * @param p The image
 * @param x The column
 * @param y The row
 * @param rgb The color, 0xRRGGBB
 */
static void box_plot_pixel(
    box_audio_plot_t *p,
    int32_t x,
    int32_t y,
    uint32_t rgb)
{
    uint8_t *px;
    if (x >= 0 && y >= 0 && x < p->width && y < p->height) {
        px = p->pixels + ((int64_t)y * p->width + x) * 3;
        px[0] = (uint8_t)(rgb >> 16);
        px[1] = (uint8_t)(rgb >> 8);
        px[2] = (uint8_t)rgb;
    }
}

/** Mix a color into a pixel, nothing outside the image.
 *
 * @param p The image
 * @param x The column
 * @param y The row
 * @param rgb The color, 0xRRGGBB
 * @param alpha Weight of the color, 0..256
 */
static void box_plot_blend(
    box_audio_plot_t *p,
    int32_t x,
    int32_t y,
    uint32_t rgb,
    int32_t alpha)
{
    uint8_t *px;
    int32_t i;
    if (x >= 0 && y >= 0 && x < p->width && y < p->height) {
        px = p->pixels + ((int64_t)y * p->width + x) * 3;
        for (i = 0; i < 3; i ++) {
            int32_t c = (int32_t)((rgb >> (16 - 8 * i)) & 0xFF);
            px[i] = (uint8_t)((px[i] * (256 - alpha) + c * alpha) >> 8);
        }
    }
}

/** Fill a rectangle.
 *
 * @param p The image
 * @param x0 Left column
 * @param y0 Top row
 * @param x1 Column after the right
 * @param y1 Row after the bottom
 * @param rgb The color
 */
static void box_plot_rect(
    box_audio_plot_t *p,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    uint32_t rgb)
{
    int32_t x, y;
    for (y = y0; y < y1; y ++) {
        for (x = x0; x < x1; x ++) {
            box_plot_pixel(p, x, y, rgb);
        }
    }
}

/** Draw a vertical line, mixed into the image.
 *
 * @param p The image
 * @param x The column
 * @param y0 Top row
 * @param y1 Row after the bottom
 * @param rgb The color
 * @param alpha Weight of the color, 0..256
 * @param dash Pixels of a dash and of a gap, 0 for a solid line
 */
static void box_plot_vline(
    box_audio_plot_t *p,
    int32_t x,
    int32_t y0,
    int32_t y1,
    uint32_t rgb,
    int32_t alpha,
    int32_t dash)
{
    int32_t y;
    for (y = y0; y < y1; y ++) {
        if (!dash || ((y - y0) / dash) % 2 == 0) {
            box_plot_blend(p, x, y, rgb, alpha);
        }
    }
}

/** Draw a horizontal line, mixed into the image.
 *
 * @param p The image
 * @param x0 Left column
 * @param x1 Column after the right
 * @param y The row
 * @param rgb The color
 * @param alpha Weight of the color, 0..256
 */
static void box_plot_hline(
    box_audio_plot_t *p,
    int32_t x0,
    int32_t x1,
    int32_t y,
    uint32_t rgb,
    int32_t alpha)
{
    int32_t x;
    for (x = x0; x < x1; x ++) {
        box_plot_blend(p, x, y, rgb, alpha);
    }
}

/** Get the width of a text.
 *
 * @param ctx The plot
 * @param text The text
 * @return Its width in pixels
 */
static int32_t box_plot_text_width(
    const box_plot_ctx_t *ctx,
    const char *text)
{
    int32_t len = (int32_t)strlen(text);

    return len ? len * ctx->cw - ctx->s : 0;
}

/** Draw a text, its top left at a pixel. Characters outside printable ASCII draw as '?'.
 *
 * @param ctx The plot
 * @param x Left column
 * @param y Top row
 * @param text The text
 * @param rgb The color
 */
static void box_plot_text(
    const box_plot_ctx_t *ctx,
    int32_t x,
    int32_t y,
    const char *text,
    uint32_t rgb)
{
    int32_t s = ctx->s, i, col, row;
    for (i = 0; text[i]; i ++) {
        int32_t c = (unsigned char)text[i] - BOX_PLOT_FONT_FIRST;
        c = c >= 0 && c < BOX_PLOT_FONT_COUNT ? c : '?' - BOX_PLOT_FONT_FIRST;
        for (col = 0; col < 5; col ++) {
            for (row = 0; row < 7; row ++) {
                if (box_plot_font[c][col] & (1 << row)) {
                    box_plot_rect(ctx->p, x + (i * 6 + col) * s, y + row * s,
                        x + (i * 6 + col + 1) * s, y + (row + 1) * s, rgb);
                }
            }
        }
    }
}

/** Draw a text centered on a column, kept inside the image.
 *
 * @param ctx The plot
 * @param x The column of the center
 * @param y Top row
 * @param text The text
 * @param rgb The color
 */
static void box_plot_text_center(
    const box_plot_ctx_t *ctx,
    int32_t x,
    int32_t y,
    const char *text,
    uint32_t rgb)
{
    int32_t w = box_plot_text_width(ctx, text), left = x - w / 2;
    left = left + w > ctx->p->width - 2 ? ctx->p->width - 2 - w : left;
    left = left < 2 ? 2 : left;
    box_plot_text(ctx, left, y, text, rgb);
}

/** Draw a label of the vertical axis: right aligned left of the data area, centered on a
 * row, with a tick.
 *
 * @param ctx The plot
 * @param y The row
 * @param text The label
 */
static void box_plot_label_y(
    const box_plot_ctx_t *ctx,
    int32_t y,
    const char *text)
{
    box_audio_plot_t *p = ctx->p;
    int32_t tick = 3 * ctx->s;
    box_plot_rect(p, p->x - tick, y, p->x, y + 1, BOX_PLOT_FRAME);
    box_plot_text(ctx, p->x - tick - 3 - box_plot_text_width(ctx, text),
        y - (7 * ctx->s) / 2, text, BOX_PLOT_TEXT);
}

/** Draw a label of the horizontal axis: centered under the data area on a column, with a
 * tick.
 *
 * @param ctx The plot
 * @param x The column
 * @param text The label
 */
static void box_plot_label_x(
    const box_plot_ctx_t *ctx,
    int32_t x,
    const char *text)
{
    box_audio_plot_t *p = ctx->p;
    int32_t tick = 3 * ctx->s, bottom = p->y + p->h;
    box_plot_rect(p, x, bottom, x + 1, bottom + tick, BOX_PLOT_FRAME);
    box_plot_text_center(ctx, x, bottom + tick + 3, text, BOX_PLOT_TEXT);
}

/** Draw the frame of the data area.
 *
 * @param ctx The plot
 */
static void box_plot_frame(
    const box_plot_ctx_t *ctx)
{
    box_audio_plot_t *p = ctx->p;
    box_plot_rect(p, p->x - 1, p->y - 1, p->x + p->w + 1, p->y, BOX_PLOT_FRAME);
    box_plot_rect(p, p->x - 1, p->y + p->h, p->x + p->w + 1, p->y + p->h + 1, BOX_PLOT_FRAME);
    box_plot_rect(p, p->x - 1, p->y, p->x, p->y + p->h, BOX_PLOT_FRAME);
    box_plot_rect(p, p->x + p->w, p->y, p->x + p->w + 1, p->y + p->h, BOX_PLOT_FRAME);
}

/** Get the color of a level on the color scale of a spectrogram.
 *
 * @param db The level in dB
 * @param min_db Bottom of the scale, black
 * @return The color, 0xRRGGBB
 */
static uint32_t box_plot_scale_color(
    float db,
    float min_db)
{
    float t = (db - min_db) / -min_db, f;
    uint32_t result = 0, a, b;
    int32_t i, c;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    i = (int32_t)(t * 4);
    i = i > 3 ? 3 : i;
    f = t * 4 - (float)i;
    a = box_plot_scale[i];
    b = box_plot_scale[i + 1];
    for (c = 16; c >= 0; c -= 8) {
        float va = (float)((a >> c) & 0xFF), vb = (float)((b >> c) & 0xFF);
        result |= (uint32_t)(int32_t)(va + (vb - va) * f + 0.5f) << c;
    }

    return result;
}

/** Get the position of a frequency on a log axis of 20 Hz to 20 kHz.
 *
 * @param hz The frequency
 * @return 0 at 20 Hz, 1 at 20 kHz
 */
static double box_plot_hz_frac(
    double hz)
{
    return log10(hz / BOX_AUDIO_PLOT_LOW_HZ) / 3.0;
}

/** Get the frequency of a position on a log axis of 20 Hz to 20 kHz.
 *
 * @param frac 0 at 20 Hz, 1 at 20 kHz
 * @return The frequency
 */
static double box_plot_frac_hz(
    double frac)
{
    return BOX_AUDIO_PLOT_LOW_HZ * pow(10.0, 3.0 * frac);
}

/** Get the bins of the FFT for the cells of a log frequency axis.
 *
 * @param bins Receives a range per cell, the first cell at 20 Hz
 * @param cells Number of cells
 * @param size Size of the FFT
 * @param rate Frames per second
 */
static void box_plot_bins_init(
    box_plot_bins_t *bins,
    int32_t cells,
    int32_t size,
    float rate)
{
    double df = (double)rate / size;
    int32_t i, half = size / 2;
    for (i = 0; i < cells; i ++) {
        double lo = box_plot_frac_hz((double)i / cells) / df;
        double hi = box_plot_frac_hz((double)(i + 1) / cells) / df;
        double at = sqrt(lo * hi);
        bins[i].lo = (int32_t)ceil(lo);
        bins[i].hi = (int32_t)floor(hi);
        bins[i].hi = bins[i].hi > half ? half : bins[i].hi;
        bins[i].at = (float)at;
        if (at >= half) {
            bins[i].lo = -1;
        }
    }
}

/** Get the level of a cell of a log frequency axis: the loudest bin inside it, else the
 * level between the bins next to its center.
 *
 * @param b The bins of the cell
 * @param db Level per bin in dB
 * @return The level, BOX_AUDIO_ANALYZE_FLOOR above the Nyquist frequency
 */
static float box_plot_bins_db(
    const box_plot_bins_t *b,
    const float *db)
{
    float result = BOX_AUDIO_ANALYZE_FLOOR, f;
    int32_t k;
    if (b->lo < 0) {
        goto done;
    }

    if (b->hi >= b->lo) {
        result = db[b->lo];
        for (k = b->lo + 1; k <= b->hi; k ++) {
            result = db[k] > result ? db[k] : result;
        }
    } else {
        k = (int32_t)b->at;
        f = b->at - (float)k;
        result = db[k] + (db[k + 1] - db[k]) * f;
    }

done:
    return result;
}

/** Convert the power of the bins to dB, a full scale sine at 0 dB.
 *
 * @param power Power per bin summed over count transforms
 * @param db Receives the level per bin
 * @param bins Number of bins
 * @param size Size of the FFT
 * @param count Transforms summed
 */
static void box_plot_power_db(
    const double *power,
    float *db,
    int32_t bins,
    int32_t size,
    int32_t count)
{
    double scale = 16.0 / ((double)size * size * (count > 0 ? count : 1));
    int32_t k;
    for (k = 0; k < bins; k ++) {
        double v = power[k] * scale;
        float d = v > 0 ? (float)(10.0 * log10(v)) : BOX_AUDIO_ANALYZE_FLOOR;
        db[k] = d > BOX_AUDIO_ANALYZE_FLOOR ? d : BOX_AUDIO_ANALYZE_FLOOR;
    }
}

/** Add the power of a Hann windowed frame of every channel. Samples outside the span are 0.
 *
 * @param desc What to plot
 * @param fft The FFT
 * @param window The window
 * @param frame Scratch of fft->size samples
 * @param re Scratch of the bins
 * @param im Scratch of the bins
 * @param start First frame, may be before the span
 * @param power Receives the power per bin, added
 */
static void box_plot_power_add(
    const box_audio_plot_desc_t *desc,
    box_audio_fft_t *fft,
    const float *window,
    float *frame,
    float *re,
    float *im,
    int64_t start,
    double *power)
{
    int32_t size = fft->size, bins = size / 2 + 1, c, n, k;
    for (c = 0; c < desc->channels; c ++) {
        for (n = 0; n < size; n ++) {
            int64_t i = start + n;
            frame[n] = i >= 0 && i < desc->frames ?
                window[n] * desc->data[i * desc->channels + c] : 0.0f;
        }

        box_audio_fft_real(fft, frame, re, im);
        for (k = 0; k < bins; k ++) {
            power[k] += (double)re[k] * re[k] + (double)im[k] * im[k];
        }
    }
}

/** Get the size of the FFT of a plot: about 85 ms.
 *
 * @param rate Frames per second
 * @return The size
 */
static int32_t box_plot_fft_size(
    float rate)
{
    return rate >= 64000 ? 8192 : rate >= 32000 ? 4096 : 2048;
}

/** Get the step of the ticks of the time axis: 1, 2 or 5 times a power of ten seconds.
 *
 * @param ctx The plot
 * @param decimals Receives the decimals of the labels
 * @return The step in seconds
 */
static double box_plot_time_step(
    const box_plot_ctx_t *ctx,
    int32_t *decimals)
{
    static const double m[3] = { 1, 2, 5 };
    double len = ctx->t1 - ctx->t0, step = 1;
    int32_t ticks = ctx->p->w / (9 * ctx->cw), e, i;
    ticks = ticks < 2 ? 2 : ticks;
    for (e = -3; e <= 5; e ++) {
        for (i = 0; i < 3; i ++) {
            step = m[i] * pow(10.0, e);
            if (len / step <= ticks) {
                *decimals = e >= 0 ? 0 : -e;
                goto done;
            }
        }
    }

    *decimals = 0;
done:
    return step;
}

/** Get the column of a time.
 *
 * @param ctx The plot
 * @param t Seconds in the recording
 * @return The column
 */
static int32_t box_plot_time_x(
    const box_plot_ctx_t *ctx,
    double t)
{
    double f = (t - ctx->t0) / (ctx->t1 - ctx->t0);

    return ctx->p->x + (int32_t)floor(f * ctx->p->w);
}

/** Draw the time axis: labeled ticks in seconds and their grid lines.
 *
 * @param ctx The plot
 * @param alpha Weight of the grid lines
 * @param grid Color of the grid lines
 */
static void box_plot_time_axis(
    const box_plot_ctx_t *ctx,
    uint32_t grid,
    int32_t alpha)
{
    box_audio_plot_t *p = ctx->p;
    char label[32];
    int32_t decimals = 0;
    double step = box_plot_time_step(ctx, &decimals);
    int64_t k = (int64_t)ceil(ctx->t0 / step - 1e-6);
    for (; (double)k * step <= ctx->t1 + 1e-9; k ++) {
        double t = (double)k * step;
        int32_t x = box_plot_time_x(ctx, t);
        x = x >= p->x + p->w ? p->x + p->w - 1 : x;
        box_plot_vline(p, x, p->y, p->y + p->h, grid, alpha, 0);
        snprintf(label, sizeof(label), "%.*f s", decimals, t);
        box_plot_label_x(ctx, x, label);
    }
}

/** Draw the bar and beat lines of a capture of a song over the time axis, with the number
 * of the bars above the data area.
 *
 * @param ctx The plot
 */
static void box_plot_beats(
    const box_plot_ctx_t *ctx)
{
    const box_audio_wav_song_t *song = &ctx->desc->song;
    box_audio_plot_t *p = ctx->p;
    double bps = song->tempo / 60.0, bar_beats = song->bar_beats;
    double b0, b1, px_beat, px_bar;
    int64_t beat, bar, every = 1;
    char label[48];
    if (!ctx->desc->has_song || !(bps > 0) || !(bar_beats > 0)) {
        goto done;
    }

    b0 = song->start_beat + ctx->t0 * bps;
    b1 = song->start_beat + ctx->t1 * bps;
    px_beat = p->w / (b1 - b0);
    px_bar = px_beat * bar_beats;
    if (px_beat >= 6 * ctx->s) {
        for (beat = (int64_t)ceil(b0 - 1e-6); (double)beat <= b1 + 1e-9; beat ++) {
            double t = ((double)beat - song->start_beat) / bps;
            box_plot_vline(p, box_plot_time_x(ctx, t), p->y, p->y + p->h, BOX_PLOT_BEAT, 96,
                2 * ctx->s);
        }
    }

    if (px_bar >= 2) {
        while (px_bar * (double)every < box_plot_text_width(ctx, "bar 1000") + 2 * ctx->cw) {
            every *= 2;
        }

        for (bar = (int64_t)ceil(b0 / bar_beats - 1e-6); (double)bar * bar_beats <= b1 + 1e-9;
            bar ++)
        {
            double t = ((double)bar * bar_beats - song->start_beat) / bps;
            int32_t x = box_plot_time_x(ctx, t);
            x = x >= p->x + p->w ? p->x + p->w - 1 : x;
            box_plot_vline(p, x, p->y, p->y + p->h, BOX_PLOT_BAR, 160, 0);
            if (bar % every == 0) {
                snprintf(label, sizeof(label), "bar %lld", (long long)(bar + 1));
                box_plot_rect(p, x, p->y - ctx->ch, x + 1, p->y, BOX_PLOT_BAR);
                box_plot_text(ctx, x + 2 * ctx->s, p->y - ctx->ch, label, BOX_PLOT_BAR);
            }
        }
    }

    snprintf(label, sizeof(label), "%.1f bpm, %.2f beats per bar", (double)song->tempo,
        bar_beats);
    box_plot_text(ctx, p->width - 4 - box_plot_text_width(ctx, label), 4, label,
        BOX_PLOT_BAR);
done:
    return;
}

/** Get the step of the labels of a dB scale: 10, 20 or 50 dB.
 *
 * @param ctx The plot
 * @param range dB of the scale
 * @param rows Rows of the scale
 * @return The step in dB
 */
static int32_t box_plot_db_step(
    const box_plot_ctx_t *ctx,
    float range,
    int32_t rows)
{
    int32_t result = 5;
    while (range / (float)result * (float)(2 * ctx->ch) > (float)rows && result < 1000) {
        result = result == 5 ? 10 : result == 10 ? 20 : result * 5 / 2;
    }

    return result;
}

/** Get the row of a level on the vertical dB scale of the data area.
 *
 * @param p The image
 * @param db The level
 * @return The row
 */
static int32_t box_plot_db_y(
    const box_audio_plot_t *p,
    float db)
{
    float f = db / p->min_db;
    f = f < 0 ? 0 : f > 1 ? 1 : f;

    return p->y + (int32_t)floorf(f * (float)(p->h - 1) + 0.5f);
}

/** Draw the vertical dB or LUFS axis of a spectrum or loudness plot.
 *
 * @param ctx The plot
 * @param unit The unit, "dB" or "LUFS"
 */
static void box_plot_db_axis(
    const box_plot_ctx_t *ctx,
    const char *unit)
{
    box_audio_plot_t *p = ctx->p;
    int32_t step = box_plot_db_step(ctx, -p->min_db, p->h), db;
    char label[32];
    for (db = 0; (float)db >= p->min_db - 0.001f; db -= step) {
        int32_t y = box_plot_db_y(p, (float)db);
        box_plot_hline(p, p->x, p->x + p->w, y, BOX_PLOT_GRID, 256);
        snprintf(label, sizeof(label), "%d %s", db, unit);
        box_plot_label_y(ctx, y, label);
    }
}

float box_audio_plot_hz_row(
    const box_audio_plot_t *plot,
    float hz)
{
    return (float)(plot->y + plot->h - box_plot_hz_frac(hz) * plot->h - 0.5);
}

float box_audio_plot_hz_column(
    const box_audio_plot_t *plot,
    float hz)
{
    return (float)(plot->x + box_plot_hz_frac(hz) * plot->w - 0.5);
}

/** Draw the labels and grid of a log frequency axis, vertical or horizontal.
 *
 * @param ctx The plot
 * @param vertical Whether the frequencies go up the rows
 * @param grid Color of the grid lines
 * @param alpha Weight of the grid lines
 */
static void box_plot_hz_axis(
    const box_plot_ctx_t *ctx,
    bool vertical,
    uint32_t grid,
    int32_t alpha)
{
    box_audio_plot_t *p = ctx->p;
    int32_t i, m, pos, last = 0;
    float decade;
    for (decade = 10; decade <= 10000; decade *= 10) {
        for (m = 2; m <= 9; m ++) {
            float hz = decade * (float)m;
            if (hz < BOX_AUDIO_PLOT_LOW_HZ || hz > BOX_AUDIO_PLOT_HIGH_HZ || m == 2 || m == 5) {
                continue;
            }

            if (vertical) {
                pos = (int32_t)floorf(box_audio_plot_hz_row(p, hz) + 0.5f);
                box_plot_hline(p, p->x, p->x + p->w, pos, grid, alpha / 2);
            } else {
                pos = (int32_t)floorf(box_audio_plot_hz_column(p, hz) + 0.5f);
                box_plot_vline(p, pos, p->y, p->y + p->h, grid, alpha / 2, 0);
            }
        }
    }

    for (i = 0; i < 10; i ++) {
        if (vertical) {
            pos = (int32_t)floorf(box_audio_plot_hz_row(p, box_plot_hz_major[i]) + 0.5f);
            box_plot_hline(p, p->x, p->x + p->w, pos, grid, alpha);
            if (i && last - pos < ctx->ch) {
                continue;
            }

            box_plot_label_y(ctx, pos, box_plot_hz_labels[i]);
        } else {
            int32_t w = box_plot_text_width(ctx, box_plot_hz_labels[i]);
            pos = (int32_t)floorf(box_audio_plot_hz_column(p, box_plot_hz_major[i]) + 0.5f);
            box_plot_vline(p, pos, p->y, p->y + p->h, grid, alpha, 0);
            if (i && pos - w / 2 - last < 2 * ctx->cw) {
                continue;
            }

            box_plot_label_x(ctx, pos, box_plot_hz_labels[i]);
            pos += w - w / 2;
        }

        last = pos;
    }
}

/** Draw a spectrogram: one FFT per column, centered on its time, the loudest bin of each row
 * of the log frequency axis, and the color scale on the right.
 *
 * @param ctx The plot
 * @return 0 on success, -1 when out of memory
 */
static int box_plot_spectrogram(
    box_plot_ctx_t *ctx)
{
    const box_audio_plot_desc_t *d = ctx->desc;
    box_audio_plot_t *p = ctx->p;
    box_audio_fft_t fft = {0};
    int32_t size = box_plot_fft_size(d->sample_rate), bins = size / 2 + 1, x, r, step, db;
    float *window = NULL, *frame = NULL, *re = NULL, *im = NULL, *level = NULL;
    double *power = NULL;
    box_plot_bins_t *rows = NULL;
    int32_t bar_x = p->x + p->w + 4 * ctx->s, bar_w = 6 * ctx->s;
    char label[32];
    int result = -1;
    if (box_audio_fft_init(&fft, size)) {
        goto done;
    }

    window = malloc(sizeof(float) * (size_t)size);
    frame = malloc(sizeof(float) * (size_t)size);
    re = malloc(sizeof(float) * (size_t)bins);
    im = malloc(sizeof(float) * (size_t)bins);
    level = malloc(sizeof(float) * (size_t)bins);
    power = malloc(sizeof(double) * (size_t)bins);
    rows = malloc(sizeof(box_plot_bins_t) * (size_t)p->h);
    if (!window || !frame || !re || !im || !level || !power || !rows) {
        goto done;
    }

    box_audio_hann(window, size);
    box_plot_bins_init(rows, p->h, size, d->sample_rate);
    for (x = 0; x < p->w; x ++) {
        int64_t center = (int64_t)floor((x + 0.5) / p->w * d->frames);
        memset(power, 0, sizeof(double) * (size_t)bins);
        box_plot_power_add(d, &fft, window, frame, re, im, center - size / 2, power);
        box_plot_power_db(power, level, bins, size, d->channels);
        for (r = 0; r < p->h; r ++) {
            float v = box_plot_bins_db(&rows[r], level);
            box_plot_pixel(p, p->x + x, p->y + p->h - 1 - r,
                box_plot_scale_color(v, p->min_db));
        }
    }

    box_plot_hz_axis(ctx, true, 0xFFFFFF, 40);
    box_plot_time_axis(ctx, 0xFFFFFF, 40);
    box_plot_beats(ctx);
    for (r = 0; r < p->h; r ++) {
        float v = p->min_db * ((float)r + 0.5f) / (float)p->h;
        box_plot_rect(p, bar_x, p->y + r, bar_x + bar_w, p->y + r + 1,
            box_plot_scale_color(v, p->min_db));
    }

    step = box_plot_db_step(ctx, -p->min_db, p->h);
    for (db = 0; (float)db >= p->min_db - 0.001f; db -= step) {
        int32_t y = box_plot_db_y(p, (float)db);
        snprintf(label, sizeof(label), "%d dB", db);
        box_plot_rect(p, bar_x + bar_w, y, bar_x + bar_w + 2 * ctx->s, y + 1, BOX_PLOT_FRAME);
        box_plot_text(ctx, bar_x + bar_w + 3 * ctx->s, y - (7 * ctx->s) / 2, label,
            BOX_PLOT_TEXT);
    }

    result = 0;
done:
    box_audio_fft_fini(&fft);
    free(window);
    free(frame);
    free(re);
    free(im);
    free(level);
    free(power);
    free(rows);

    return result;
}

/** Draw a waveform: a lane per channel with the peak of the frames of each column and the
 * RMS of at least 20 ms around it. A column whose peak is under -100 dBFS stays empty.
 *
 * @param ctx The plot
 * @return 0
 */
static int box_plot_waveform(
    box_plot_ctx_t *ctx)
{
    const box_audio_plot_desc_t *d = ctx->desc;
    box_audio_plot_t *p = ctx->p;
    int32_t gap = 2 * ctx->ch, lanes = d->channels, lane_h = (p->h - gap * (lanes - 1)) / lanes;
    int32_t c, x, i;
    int32_t rms_frames = (int32_t)(d->sample_rate * BOX_PLOT_RMS_WINDOW) + 1;
    static const float marks[5] = { 1.0f, 0.5f, 0.0f, -0.5f, -1.0f };
    static const char *mark_labels[5] = { "1.0", "0.5", "0", "-0.5", "-1.0" };
    box_plot_time_axis(ctx, BOX_PLOT_GRID, 256);
    for (c = 0; c < lanes; c ++) {
        int32_t top = p->y + c * (lane_h + gap);
        float half = (float)(lane_h - 1) * 0.5f;
        if (c) {
            box_plot_rect(p, p->x, top - gap, p->x + p->w, top, BOX_PLOT_BG);
        }

        for (i = 0; i < 5; i ++) {
            int32_t y = top + (int32_t)floorf((1.0f - marks[i]) * half + 0.5f);
            if (lane_h < 5 * ctx->ch && (i == 1 || i == 3)) {
                continue;
            }

            box_plot_hline(p, p->x, p->x + p->w, y, BOX_PLOT_GRID, 256);
            box_plot_label_y(ctx, y, mark_labels[i]);
        }

        for (x = 0; x < p->w; x ++) {
            int32_t f0 = (int32_t)((int64_t)x * d->frames / p->w);
            int32_t f1 = (int32_t)((int64_t)(x + 1) * d->frames / p->w), f, y0, y1, r0, r1;
            float lo = 0, hi = 0, rms;
            double sq = 0;
            f1 = f1 > f0 ? f1 : f0 + 1;
            f1 = f1 < d->frames ? f1 : d->frames;
            for (f = f0; f < f1; f ++) {
                float v = d->data[(int64_t)f * lanes + c];
                lo = v < lo ? v : lo;
                hi = v > hi ? v : hi;
            }

            if ((hi > -lo ? hi : -lo) < 1e-5f) {
                continue;
            }

            r0 = f0;
            r1 = f1;
            if (r1 - r0 < rms_frames) {
                r0 = (f0 + f1 - rms_frames) / 2;
                r1 = r0 + rms_frames;
                r0 = r0 > 0 ? r0 : 0;
                r1 = r1 < d->frames ? r1 : d->frames;
            }

            for (f = r0; f < r1; f ++) {
                float v = d->data[(int64_t)f * lanes + c];
                sq += (double)v * v;
            }

            hi = hi > 1 ? 1 : hi;
            lo = lo < -1 ? -1 : lo;
            y0 = top + (int32_t)floorf((1.0f - hi) * half + 0.5f);
            y1 = top + (int32_t)floorf((1.0f - lo) * half + 0.5f);
            box_plot_rect(p, p->x + x, y0, p->x + x + 1, y1 + 1, BOX_AUDIO_PLOT_PEAK);
            rms = r1 > r0 ? (float)sqrt(sq / (r1 - r0)) : 0;
            rms = rms > 1 ? 1 : rms;
            y0 = top + (int32_t)floorf((1.0f - rms) * half + 0.5f);
            y1 = top + (int32_t)floorf((1.0f + rms) * half + 0.5f);
            if (y1 >= y0) {
                box_plot_rect(p, p->x + x, y0, p->x + x + 1, y1 + 1, BOX_AUDIO_PLOT_RMS);
            }
        }

        if (lanes == 2) {
            box_plot_text(ctx, p->x + 4 * ctx->s, top + 3 * ctx->s, c ? "R" : "L",
                BOX_PLOT_TEXT);
        }
    }

    box_plot_beats(ctx);

    return 0;
}

/** Draw a line of a curve from the row of the previous column to the row of a column.
 *
 * @param ctx The plot
 * @param x The column
 * @param prev Row of the previous column, -1 for none
 * @param y Row of the column
 * @param rgb The color
 */
static void box_plot_curve(
    const box_plot_ctx_t *ctx,
    int32_t x,
    int32_t prev,
    int32_t y,
    uint32_t rgb)
{
    int32_t a = prev < 0 ? y : (prev < y ? prev : y);
    int32_t b = prev < 0 ? y : (prev > y ? prev : y);
    int32_t t = ctx->s;
    box_plot_rect(ctx->p, x, a - (t - 1) / 2, x + 1, b + t / 2 + 1, rgb);
}

/** Draw the average spectrum of the span: Hann windowed frames with half overlap, the
 * loudest bin of each column of the log frequency axis, and the bands of the analysis.
 *
 * @param ctx The plot
 * @return 0 on success, -1 when out of memory
 */
static int box_plot_spectrum(
    box_plot_ctx_t *ctx)
{
    const box_audio_plot_desc_t *d = ctx->desc;
    box_audio_plot_t *p = ctx->p;
    box_audio_fft_t fft = {0};
    int32_t size = box_plot_fft_size(d->sample_rate), bins = size / 2 + 1, x, b, prev = -1;
    int32_t count = d->frames > size ? (d->frames - size + size / 2 - 1) / (size / 2) + 1 : 1;
    float *window = NULL, *frame = NULL, *re = NULL, *im = NULL, *level = NULL;
    double *power = NULL;
    box_plot_bins_t *cols = NULL;
    int result = -1, i;
    if (box_audio_fft_init(&fft, size)) {
        goto done;
    }

    window = malloc(sizeof(float) * (size_t)size);
    frame = malloc(sizeof(float) * (size_t)size);
    re = malloc(sizeof(float) * (size_t)bins);
    im = malloc(sizeof(float) * (size_t)bins);
    level = malloc(sizeof(float) * (size_t)bins);
    power = calloc((size_t)bins, sizeof(double));
    cols = malloc(sizeof(box_plot_bins_t) * (size_t)p->w);
    if (!window || !frame || !re || !im || !level || !power || !cols) {
        goto done;
    }

    box_audio_hann(window, size);
    box_plot_bins_init(cols, p->w, size, d->sample_rate);
    for (i = 0; i < count; i ++) {
        box_plot_power_add(d, &fft, window, frame, re, im, (int64_t)i * (size / 2), power);
    }

    box_plot_power_db(power, level, bins, size, count * d->channels);
    box_plot_hz_axis(ctx, false, BOX_PLOT_GRID, 256);
    box_plot_db_axis(ctx, "dB");
    for (b = 0; b < BOX_AUDIO_ANALYZE_BANDS; b ++) {
        int32_t x0 = (int32_t)floorf(
            box_audio_plot_hz_column(p, box_audio_analyze_band_low(b)) + 0.5f);
        int32_t x1 = (int32_t)floorf(
            box_audio_plot_hz_column(p, box_audio_analyze_band_high(b)) + 0.5f);
        const char *name = box_audio_analyze_band_name(b);
        if (b) {
            box_plot_vline(p, x0, p->y - ctx->ch, p->y + p->h, BOX_PLOT_BAND, 200, 3 * ctx->s);
        }

        if (box_plot_text_width(ctx, name) < x1 - x0 - 2 * ctx->s) {
            box_plot_text(ctx, (x0 + x1) / 2 - box_plot_text_width(ctx, name) / 2,
                p->y - ctx->ch, name, BOX_PLOT_BAND);
        }
    }

    for (x = 0; x < p->w; x ++) {
        float v = box_plot_bins_db(&cols[x], level);
        int32_t y;
        if (cols[x].lo < 0 || v < p->min_db) {
            prev = -1;
            continue;
        }

        y = box_plot_db_y(p, v);
        box_plot_vline(p, p->x + x, y, p->y + p->h, BOX_PLOT_FILL, 160, 0);
        box_plot_curve(ctx, p->x + x, prev, y, BOX_PLOT_LINE);
        prev = y;
    }

    result = 0;
done:
    box_audio_fft_fini(&fft);
    free(window);
    free(frame);
    free(re);
    free(im);
    free(level);
    free(power);
    free(cols);

    return result;
}

/** Draw the short-term (3 s) and momentary (400 ms) loudness over time: K-weighted power
 * per hop of 10 ms, summed over the window that ends at each column.
 *
 * @param ctx The plot
 * @return 0 on success, -1 when out of memory
 */
static int box_plot_loudness(
    box_plot_ctx_t *ctx)
{
    const box_audio_plot_desc_t *d = ctx->desc;
    box_audio_plot_t *p = ctx->p;
    box_audio_kweight_t k[2];
    int32_t hop = (int32_t)floorf(d->sample_rate / BOX_PLOT_HOPS_PER_S + 0.5f);
    int32_t hops, h, c, f, x, pass, prev;
    double *sum = NULL;
    int result = -1;
    hop = hop > 0 ? hop : 1;
    hops = (d->frames + hop - 1) / hop;
    sum = malloc(sizeof(double) * (size_t)(hops + 1));
    if (!sum) {
        goto done;
    }

    for (c = 0; c < d->channels; c ++) {
        box_audio_kweight_init(&k[c], d->sample_rate);
    }

    sum[0] = 0;
    for (h = 0; h < hops; h ++) {
        double acc = 0;
        int32_t end = (h + 1) * hop < d->frames ? (h + 1) * hop : d->frames;
        for (f = h * hop; f < end; f ++) {
            for (c = 0; c < d->channels; c ++) {
                double v = box_audio_kweight(&k[c], d->data[(int64_t)f * d->channels + c]);
                acc += v * v;
            }
        }

        sum[h + 1] = sum[h] + acc;
    }

    box_plot_db_axis(ctx, "LUFS");
    box_plot_time_axis(ctx, BOX_PLOT_GRID, 256);
    box_plot_beats(ctx);
    for (pass = 0; pass < 2; pass ++) {
        int32_t window = pass ? BOX_PLOT_SHORT_HOPS : BOX_PLOT_MOMENTARY_HOPS;
        uint32_t rgb = pass ? BOX_PLOT_SHORT : BOX_PLOT_MOMENTARY;
        prev = -1;
        for (x = 0; x < p->w; x ++) {
            int32_t e = (int32_t)floor((x + 1.0) / p->w * hops + 0.5);
            int32_t s, frames;
            float l;
            e = e < 1 ? 1 : e > hops ? hops : e;
            s = e - window > 0 ? e - window : 0;
            frames = (e * hop < d->frames ? e * hop : d->frames) - s * hop;
            l = box_audio_loudness(frames > 0 ? (sum[e] - sum[s]) / frames : 0);
            l = l < p->min_db ? p->min_db : l;
            box_plot_curve(ctx, p->x + x, prev, box_plot_db_y(p, l), rgb);
            prev = box_plot_db_y(p, l);
        }
    }

    box_plot_text(ctx, p->x + p->w - 4 * ctx->s - box_plot_text_width(ctx, "short-term 3 s"),
        p->y + 3 * ctx->s, "short-term 3 s", BOX_PLOT_SHORT);
    box_plot_text(ctx, p->x + p->w - 4 * ctx->s - box_plot_text_width(ctx, "momentary 400 ms"),
        p->y + 3 * ctx->s + ctx->ch, "momentary 400 ms", BOX_PLOT_MOMENTARY);
    result = 0;
done:
    free(sum);

    return result;
}

const char* box_audio_plot_kind_name(
    box_audio_plot_kind_t kind)
{
    const char *result = "spectrogram";
    switch (kind) {
    case BoxAudioPlotSpectrogram: result = "spectrogram"; break;
    case BoxAudioPlotWaveform: result = "waveform"; break;
    case BoxAudioPlotSpectrum: result = "spectrum"; break;
    case BoxAudioPlotLoudness: result = "loudness"; break;
    }

    return result;
}

int box_audio_plot(
    const box_audio_plot_desc_t *desc,
    box_audio_plot_t *out)
{
    box_plot_ctx_t ctx = {0};
    int32_t w, h, left, right, top, bottom;
    int result = -1;
    memset(out, 0, sizeof(*out));
    if (!desc || !desc->data || desc->frames < 1 || desc->channels < 1 ||
        desc->channels > 2 || !(desc->sample_rate > 0))
    {
        goto done;
    }

    w = desc->width ? desc->width : BOX_AUDIO_PLOT_WIDTH;
    h = desc->height ? desc->height : BOX_AUDIO_PLOT_HEIGHT;
    if (w < BOX_AUDIO_PLOT_MIN_WIDTH || h < BOX_AUDIO_PLOT_MIN_HEIGHT ||
        w > BOX_AUDIO_PLOT_MAX_SIZE || h > BOX_AUDIO_PLOT_MAX_SIZE)
    {
        goto done;
    }

    out->pixels = malloc((size_t)w * (size_t)h * 3);
    if (!out->pixels) {
        goto done;
    }

    out->width = w;
    out->height = h;
    out->min_db = desc->min_db < 0 ? desc->min_db :
        desc->kind == BoxAudioPlotLoudness ? BOX_AUDIO_PLOT_MIN_LUFS : BOX_AUDIO_PLOT_MIN_DB;
    ctx.desc = desc;
    ctx.p = out;
    ctx.s = w >= 640 && h >= 320 ? 2 : 1;
    ctx.cw = 6 * ctx.s;
    ctx.ch = 8 * ctx.s + 2;
    ctx.t0 = desc->start;
    ctx.t1 = desc->start + desc->frames / (double)desc->sample_rate;
    left = 9 * ctx.cw + 6 * ctx.s;
    right = desc->kind == BoxAudioPlotSpectrogram ? 13 * ctx.s + 8 * ctx.cw : 6 * ctx.cw;
    top = 2 * ctx.ch + 6;
    bottom = ctx.ch + 4 * ctx.s + 6;
    out->x = left;
    out->y = top;
    out->w = w - left - right;
    out->h = h - top - bottom;
    box_plot_rect(out, 0, 0, w, h, BOX_PLOT_BG);
    box_plot_rect(out, out->x, out->y, out->x + out->w, out->y + out->h,
        desc->kind == BoxAudioPlotSpectrogram ? box_plot_scale[0] : BOX_PLOT_AREA);
    if (desc->title) {
        box_plot_text(&ctx, 4, 4, desc->title, BOX_PLOT_TEXT);
    }

    switch (desc->kind) {
    case BoxAudioPlotSpectrogram: result = box_plot_spectrogram(&ctx); break;
    case BoxAudioPlotWaveform: result = box_plot_waveform(&ctx); break;
    case BoxAudioPlotSpectrum: result = box_plot_spectrum(&ctx); break;
    case BoxAudioPlotLoudness: result = box_plot_loudness(&ctx); break;
    }

    box_plot_frame(&ctx);
done:
    if (result) {
        box_audio_plot_fini(out);
    }

    return result;
}

void box_audio_plot_fini(
    box_audio_plot_t *plot)
{
    free(plot->pixels);
    memset(plot, 0, sizeof(*plot));
}
