/**
 * @file audio/platform.h
 * @brief The browser audio output: an AudioWorklet that runs the core from its own wasm.
 */

#ifndef BOX_AUDIO_PLATFORM_H
#define BOX_AUDIO_PLATFORM_H

/** Default peak level of the tone of the ?tone= query parameter */
#define BOX_AUDIO_WEB_TONE_LEVEL 0.25f

/** Create the AudioContext of the page and load the worklet of the core, once per page.
 *
 * The context stays suspended until box_audio_web_gesture. With ?tone=<hz> in the address of
 * the page the core plays a tone of that frequency once it runs. Nothing on native builds.
 */
void box_audio_web_start(void);

/** Resume the AudioContext from an input event handler, a user gesture to the browser. Cheap
 * once the context runs; nothing on native builds.
 */
void box_audio_web_gesture(void);

/** Send the tone command to the core: its frequency and peak level. Nothing on native builds.
 *
 * @param frequency Frequency in Hz
 * @param level Peak level, 0..1
 */
void box_audio_web_tone(
    float frequency,
    float level);

#endif
