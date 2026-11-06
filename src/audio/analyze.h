/**
 * @file audio/analyze.h
 * @brief Analysis of recordings in plain C: a real FFT, K-weighting, loudness (EBU R128 /
 * ITU-R BS.1770), true peak, band energies, stereo, onsets, tails, clicks and pitch, and a
 * reader of WAV files. No Flecs and no platform code; the per sample loops do not allocate.
 *
 * The audio analyze command prints the result; audio plot, meter, diff and render reuse it.
 */

#ifndef BOX_AUDIO_ANALYZE_H
#define BOX_AUDIO_ANALYZE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bands of the spectrum: sub, low, low-mid, mid, presence, brilliance and air */
#define BOX_AUDIO_ANALYZE_BANDS (7)

/** Onsets kept in a result; more are counted only */
#define BOX_AUDIO_ANALYZE_ONSETS (16)

/** Discontinuities kept in a result; more are counted only */
#define BOX_AUDIO_ANALYZE_JUMPS (8)

/** Floor of every level in dB: a level at or below it is silence, -inf in a report */
#define BOX_AUDIO_ANALYZE_FLOOR (-150.0f)

/** Size of the FFT of the spectrum, 8192 samples */
#define BOX_AUDIO_ANALYZE_FFT (8192)

/** Level a first or last sample must reach to be a click, in dBFS */
#define BOX_AUDIO_ANALYZE_CLICK_DB (-40.0f)

/** Level below which a sample is silence for the leading and trailing silence, in dBFS */
#define BOX_AUDIO_ANALYZE_SILENCE_DB (-60.0f)

/** Id of the WAV chunk of a capture of a song: its start beat, tempo and bar length */
#define BOX_AUDIO_WAV_SONG_CHUNK "bxsg"

/** Bytes of the data of the song chunk: a double and two floats, little endian */
#define BOX_AUDIO_WAV_SONG_BYTES (16)

/** Where a capture of a song started, as it is stored in its WAV file */
typedef struct box_audio_wav_song_t {
    double start_beat;      /**< Beat of the transport at the first frame */
    float tempo;            /**< Quarter notes per minute at the first frame */
    float bar_beats;        /**< Beats of a bar, beats_per_bar * 4 / beat_unit */
} box_audio_wav_song_t;

/** A WAV file read into float frames */
typedef struct box_audio_wav_t {
    float *data;            /**< Interleaved frames, freed with box_audio_wav_fini */
    int32_t frames;         /**< Number of frames */
    int32_t channels;       /**< 1 or 2 */
    float sample_rate;      /**< Frames per second */
    bool has_song;          /**< Whether the file has a song chunk */
    box_audio_wav_song_t song; /**< The song chunk, when has_song */
} box_audio_wav_t;

/** A real FFT of a power of two size, with its tables */
typedef struct box_audio_fft_t {
    int32_t size;           /**< Real samples in, size / 2 + 1 bins out */
    float *cos;             /**< Twiddles of the half size complex FFT, then of the split */
    float *sin;             /**< Sines that go with cos */
    int32_t *rev;           /**< Bit reversed index of the half size complex FFT */
    float *re;              /**< Scratch of the half size complex FFT */
    float *im;              /**< Scratch of the half size complex FFT */
} box_audio_fft_t;

/** A biquad in direct form 1, in double */
typedef struct box_audio_biquad_t {
    double b0;              /**< Feed forward coefficient of x[n] */
    double b1;              /**< Feed forward coefficient of x[n-1] */
    double b2;              /**< Feed forward coefficient of x[n-2] */
    double a1;              /**< Feedback coefficient of y[n-1] */
    double a2;              /**< Feedback coefficient of y[n-2] */
    double x1;              /**< x[n-1] */
    double x2;              /**< x[n-2] */
    double y1;              /**< y[n-1] */
    double y2;              /**< y[n-2] */
} box_audio_biquad_t;

/** The K-weighting of BS.1770 of one channel: a high shelf and a high pass */
typedef struct box_audio_kweight_t {
    box_audio_biquad_t shelf; /**< The pre-filter, +4 dB above about 1.5 kHz */
    box_audio_biquad_t high_pass; /**< The RLB high pass at about 38 Hz */
} box_audio_kweight_t;

/** Taps of each phase of the true peak interpolator */
#define BOX_AUDIO_TRUE_PEAK_TAPS (16)

/** The 4x oversampling interpolator of the true peak: windowed sinc taps of the 3 points
 * between two samples */
typedef struct box_audio_true_peak_t {
    float taps[3][BOX_AUDIO_TRUE_PEAK_TAPS]; /**< Taps of the points at 1/4, 2/4 and 3/4 */
} box_audio_true_peak_t;

/** An onset: where a sound starts */
typedef struct box_audio_onset_t {
    float time;             /**< Seconds from the start of the span */
    float strength;         /**< Rise of the level in dB */
} box_audio_onset_t;

/** A discontinuity: a sample step far above the local slope */
typedef struct box_audio_jump_t {
    float time;             /**< Seconds from the start of the span */
    float size;             /**< Size of the step in dBFS */
} box_audio_jump_t;

/** The analysis of a recording. Levels are in dB, at least BOX_AUDIO_ANALYZE_FLOOR. */
typedef struct box_audio_analysis_t {
    int32_t frames;         /**< Frames analyzed */
    int32_t channels;       /**< 1 or 2 */
    float sample_rate;      /**< Frames per second */
    float integrated;       /**< Integrated loudness in LUFS */
    float range;            /**< Loudness range in LU */
    float short_term_max;   /**< Highest short-term (3 s) loudness in LUFS */
    float momentary_max;    /**< Highest momentary (400 ms) loudness in LUFS */
    float sample_peak;      /**< Highest absolute sample in dBFS */
    float true_peak;        /**< Highest absolute sample, 4x oversampled, in dBFS */
    float rms;              /**< RMS of every sample in dBFS */
    float crest;            /**< Sample peak over RMS in dB */
    int32_t clipped;        /**< Samples at full scale next to another one, or above it */
    float dc[2];            /**< Mean of each channel, full scale is 1 */
    float bands[BOX_AUDIO_ANALYZE_BANDS]; /**< Energy per band in dB relative to the total */
    float centroid;         /**< Spectral centroid in Hz */
    float rolloff;          /**< Frequency below which 85% of the energy is, in Hz */
    float correlation;      /**< Correlation of left and right, -1..1; 1 for mono */
    float side_mid;         /**< Energy of the side over the mid in dB */
    float mono_loss;        /**< Loudness lost when summed to mono in dB, at most 100 */
    int32_t onset_count;    /**< Onsets found */
    box_audio_onset_t onsets[BOX_AUDIO_ANALYZE_ONSETS]; /**< The first onsets */
    float attack;           /**< 10% to 90% rise of the first onset in seconds, -1 for none */
    float tail;             /**< Seconds from the peak after the last onset to -60 dB below
                             *   it, -1 without onsets */
    bool tail_cut;          /**< The span ends before the tail falls 60 dB */
    float tail_drop;        /**< dB the tail fell by at the end of the span when cut */
    float tail_onset;       /**< Time of the last onset in seconds */
    float leading;          /**< Seconds before the first sample over -60 dBFS */
    float trailing;         /**< Seconds after the last sample over -60 dBFS */
    float first;            /**< Largest absolute first sample of the channels */
    float last;             /**< Largest absolute last sample of the channels */
    int32_t jump_count;     /**< Discontinuities found */
    box_audio_jump_t jumps[BOX_AUDIO_ANALYZE_JUMPS]; /**< The first discontinuities */
    bool pitched;           /**< Whether there is a stable pitch */
    float pitch;            /**< The fundamental in Hz, when pitched */
    float pitch_spread;     /**< Standard deviation of the pitch over the span in cents */
    float hnr;              /**< Harmonic to noise ratio in dB */
    float voiced;           /**< Fraction of the loud frames with a pitch, 0..1 */
} box_audio_analysis_t;

/** Integrated loudness measured as the frames come, for a recording that is not kept: the
 * gating blocks of 400 ms, a step of 100 ms apart, as box_audio_analyze gates them */
typedef struct box_audio_loudness_meter_t {
    box_audio_kweight_t k[2]; /**< K-weighting of the left and the right channel */
    int32_t channels;       /**< 1 or 2 */
    int32_t step;           /**< Frames of a step of 100 ms */
    int32_t filled;         /**< Frames in the step that fills */
    double sum;             /**< Sum of the squares of the weighted samples of that step */
    double steps[4];        /**< Sums of the last four steps, step i at i % 4 */
    int64_t done;           /**< Steps done */
    double total;           /**< Sum of the squares of every weighted sample */
    int64_t frames;         /**< Frames measured */
    double *blocks;         /**< Power of each gating block */
    int32_t block_count;    /**< Blocks in blocks */
    int32_t block_size;     /**< Blocks blocks holds room for */
} box_audio_loudness_meter_t;

/** A part of a recording, such as a bar, with its loudness and bands */
typedef struct box_audio_analyze_segment_t {
    int32_t start;          /**< First frame */
    int32_t end;            /**< Frame after the last one */
    float loudness;         /**< Loudness of the part in LUFS, ungated */
    float bands[BOX_AUDIO_ANALYZE_BANDS]; /**< Energy per band in dB relative to the total */
} box_audio_analyze_segment_t;

/** Get the lowest frequency of a band.
 *
 * @param band The band, 0..BOX_AUDIO_ANALYZE_BANDS - 1
 * @return Its lowest frequency in Hz
 */
float box_audio_analyze_band_low(
    int32_t band);

/** Get the highest frequency of a band.
 *
 * @param band The band, 0..BOX_AUDIO_ANALYZE_BANDS - 1
 * @return Its highest frequency in Hz
 */
float box_audio_analyze_band_high(
    int32_t band);

/** Get the name of a band, such as "low-mid".
 *
 * @param band The band, 0..BOX_AUDIO_ANALYZE_BANDS - 1
 * @return Its name
 */
const char* box_audio_analyze_band_name(
    int32_t band);

/** Fill a periodic Hann window, the window of the spectrum of the analysis.
 *
 * @param window Receives size values
 * @param size Size of the window
 */
void box_audio_hann(
    float *window,
    int32_t size);

/** Set up a real FFT.
 *
 * @param fft The FFT
 * @param size Real samples, a power of two of at least 4
 * @return 0 on success, -1 for a bad size or out of memory
 */
int box_audio_fft_init(
    box_audio_fft_t *fft,
    int32_t size);

/** Free the tables of a real FFT.
 *
 * @param fft The FFT
 */
void box_audio_fft_fini(
    box_audio_fft_t *fft);

/** Transform real samples. Does not allocate.
 *
 * @param fft The FFT
 * @param in fft->size real samples
 * @param re Receives the real parts of the size / 2 + 1 bins
 * @param im Receives the imaginary parts of the size / 2 + 1 bins
 */
void box_audio_fft_real(
    box_audio_fft_t *fft,
    const float *in,
    float *re,
    float *im);

/** Set up the K-weighting of BS.1770 for a sample rate.
 *
 * @param k The filter
 * @param sample_rate Frames per second
 */
void box_audio_kweight_init(
    box_audio_kweight_t *k,
    double sample_rate);

/** Filter a sample with the K-weighting.
 *
 * @param k The filter
 * @param x The sample
 * @return The weighted sample
 */
double box_audio_kweight(
    box_audio_kweight_t *k,
    double x);

/** Get the loudness of a mean square of K-weighted samples summed over the channels.
 *
 * @param power The sum over the channels of the mean squares
 * @return The loudness in LUFS, at least BOX_AUDIO_ANALYZE_FLOOR
 */
float box_audio_loudness(
    double power);

/** Set up a loudness meter.
 *
 * @param m The meter
 * @param channels 1 or 2
 * @param sample_rate Frames per second
 */
void box_audio_loudness_meter_init(
    box_audio_loudness_meter_t *m,
    int32_t channels,
    float sample_rate);

/** Measure frames. Grows the blocks once per 100 ms of frames at most, never per sample.
 *
 * @param m The meter
 * @param left The left channel, or the only one
 * @param right The right channel, NULL for mono
 * @param frames Number of frames
 */
void box_audio_loudness_meter_add(
    box_audio_loudness_meter_t *m,
    const float *left,
    const float *right,
    int32_t frames);

/** Get the integrated loudness of the frames measured: gated at -70 LUFS and at 10 LU under
 * the mean of the blocks over it. Frames shorter than a block are one block.
 *
 * @param m The meter
 * @return The loudness in LUFS, BOX_AUDIO_ANALYZE_FLOOR for silence or no frames
 */
float box_audio_loudness_meter_integrated(
    const box_audio_loudness_meter_t *m);

/** Free the blocks of a loudness meter.
 *
 * @param m The meter
 */
void box_audio_loudness_meter_fini(
    box_audio_loudness_meter_t *m);

/** Set up the interpolator of the true peak.
 *
 * @param tp The interpolator
 */
void box_audio_true_peak_init(
    box_audio_true_peak_t *tp);

/** Get the oversampled peak between two samples of a channel: the largest absolute value of
 * the 3 points between frame i and frame i + 1, 4x oversampled. Does not allocate.
 *
 * @param tp The interpolator
 * @param data Interleaved frames
 * @param frames Number of frames
 * @param channels Number of channels
 * @param channel The channel
 * @param i The frame
 * @return The largest absolute value of the points between frame i and i + 1
 */
float box_audio_true_peak_between(
    const box_audio_true_peak_t *tp,
    const float *data,
    int32_t frames,
    int32_t channels,
    int32_t channel,
    int32_t i);

/** Analyze interleaved frames.
 *
 * @param data Interleaved frames
 * @param frames Number of frames, at least 1
 * @param channels 1 or 2
 * @param sample_rate Frames per second
 * @param out Receives the analysis
 * @return 0 on success, -1 for bad arguments or out of memory
 */
int box_audio_analyze(
    const float *data,
    int32_t frames,
    int32_t channels,
    float sample_rate,
    box_audio_analysis_t *out);

/** Measure the loudness and the bands of parts of a recording.
 *
 * @param data Interleaved frames
 * @param frames Number of frames
 * @param channels 1 or 2
 * @param sample_rate Frames per second
 * @param segments The parts, start and end set by the caller, the rest receives the measures
 * @param count Number of parts
 * @return 0 on success, -1 for bad arguments or out of memory
 */
int box_audio_analyze_segments(
    const float *data,
    int32_t frames,
    int32_t channels,
    float sample_rate,
    box_audio_analyze_segment_t *segments,
    int32_t count);

/** Get the name and the cents of the note nearest to a frequency, A4 at 440 Hz.
 *
 * @param hz The frequency
 * @param name Receives the name and octave, such as "A4", at least 8 bytes
 * @return The cents from the note, -50..50
 */
float box_audio_note_name(
    float hz,
    char *name);

/** Read a WAV file in memory: PCM of 8, 16, 24 or 32 bits or float of 32 or 64 bits, with
 * 1 or 2 channels, and the song chunk of a capture.
 *
 * @param bytes The file
 * @param size Bytes of the file
 * @param out Receives the frames
 * @return NULL on success, else why it cannot be read: "not a WAV file", "unsupported WAV
 * format: ..." or "no samples"
 */
const char* box_audio_wav_read(
    const uint8_t *bytes,
    size_t size,
    box_audio_wav_t *out);

/** Free the frames of a WAV file.
 *
 * @param wav The file
 */
void box_audio_wav_fini(
    box_audio_wav_t *wav);

#ifdef __cplusplus
}
#endif

#endif
