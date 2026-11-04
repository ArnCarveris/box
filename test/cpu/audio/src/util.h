/**
 * @file util.h
 * @brief Helpers of the audio core tests: offline renders, sounds and measurements.
 */

#ifndef AUDIO_TEST_UTIL_H
#define AUDIO_TEST_UTIL_H

#include <bake_test.h>
#include <core.h>
#include <math.h>
#include <string.h>

/** Sample rate of the tests */
#define AUDIO_RATE 48000

/** Frames of the longest render of a case, one second */
#define AUDIO_FRAMES 48000

/** Frames of a ramp at AUDIO_RATE */
#define AUDIO_RAMP ((int32_t)(BOX_AUDIO_CORE_RAMP * AUDIO_RATE))

/** A route of a bus to an output with send 0 to a bus, the other sends unused */
#define AUDIO_ROUTE(bus, output, send)\
    { (bus), (output), { (send), BOX_AUDIO_BUS_NONE, BOX_AUDIO_BUS_NONE, BOX_AUDIO_BUS_NONE },\
        0, 0 }

/** A route of a bus to an output without sends */
#define AUDIO_OUT(bus, output) AUDIO_ROUTE(bus, output, BOX_AUDIO_BUS_NONE)

/** The route of the master, last in an order */
#define AUDIO_MASTER AUDIO_OUT(0, BOX_AUDIO_BUS_NONE)

/** Pi, which math.h of a strict C11 build does not define */
#define AUDIO_PI 3.14159265358979323846

/** Output of the last render */
extern float audio_l[AUDIO_FRAMES];
extern float audio_r[AUDIO_FRAMES];

/** Room for sounds of the tests */
extern float audio_sound_a[AUDIO_FRAMES * 2];
extern float audio_sound_b[AUDIO_FRAMES * 2];

/** Create a core at AUDIO_RATE.
 *
 * @return The core
 */
box_audio_core_t* audio_new(void);

/** Render frames into audio_l and audio_r from a frame on, in quanta of the web.
 *
 * @param core The core
 * @param at First frame
 * @param frames Number of frames
 */
void audio_render(
    box_audio_core_t *core,
    int32_t at,
    int32_t frames);

/** Fill a mono buffer with a sine.
 *
 * @param data The buffer
 * @param frames Frames
 * @param frequency Frequency in Hz at AUDIO_RATE
 * @param level Peak level
 */
void audio_sine(
    float *data,
    int32_t frames,
    double frequency,
    float level);

/** Fill a buffer with one value.
 *
 * @param data The buffer
 * @param count Values
 * @param value The value
 */
void audio_fill(
    float *data,
    int32_t count,
    float value);

/** Play a sound on a bus with a single command.
 *
 * @param core The core
 * @param voice Handle
 * @param sound Sound id
 * @param bus Bus
 * @param volume Volume
 * @param pan Pan
 * @param pitch Pitch ratio
 * @param flags Flags
 */
void audio_play(
    box_audio_core_t *core,
    uint32_t voice,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pan,
    float pitch,
    uint32_t flags);

/** Set a parameter of a bus with a single command.
 *
 * @param core The core
 * @param bus Bus
 * @param param Parameter
 * @param value Value
 */
void audio_bus_set(
    box_audio_core_t *core,
    uint32_t bus,
    box_audio_param_t param,
    float value);

/** Set a parameter of a voice with a single command.
 *
 * @param core The core
 * @param voice Handle
 * @param param Parameter
 * @param value Value
 */
void audio_voice_set(
    box_audio_core_t *core,
    uint32_t voice,
    box_audio_param_t param,
    float value);

/** Replace the order with a single command.
 *
 * @param core The core
 * @param routes Routes
 * @param count Number of routes
 */
void audio_order(
    box_audio_core_t *core,
    const box_audio_route_t *routes,
    int32_t count);

/** Largest change between two neighbouring samples of a range; the sample before from counts
 * as 0 when from is 0.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @return The largest change
 */
float audio_max_step(
    const float *x,
    int32_t from,
    int32_t to);

/** Largest magnitude of a range.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @return The peak
 */
float audio_peak(
    const float *x,
    int32_t from,
    int32_t to);

/** Frequency of a range from its upward zero crossings.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @return Frequency in Hz
 */
double audio_frequency(
    const float *x,
    int32_t from,
    int32_t to);

/** Count events of a type, and keep the last of them.
 *
 * @param core The core
 * @param type Event type
 * @param last Receives the last event of the type, may be NULL
 * @return Number of events of the type
 */
int32_t audio_events(
    box_audio_core_t *core,
    int32_t type,
    box_audio_event_t *last);

/** Render a fixed sequence of commands at fixed block boundaries: plays, steals, stops, pitch,
 * pan, mute, a stereo sound at another rate and an order with sends. Registers sounds 1 and 2
 * from audio_sound_a and audio_sound_b.
 *
 * @param core The core
 * @param left Receives frames samples of the left channel
 * @param right Receives frames samples of the right channel
 * @param frames Number of frames
 */
void audio_workload(
    box_audio_core_t *core,
    float *left,
    float *right,
    int32_t frames);

/** FNV-1a hash of the bits of samples.
 *
 * @param x Samples
 * @param n Number of samples
 * @return The hash
 */
uint64_t audio_hash(
    const float *x,
    int32_t n);

#endif
