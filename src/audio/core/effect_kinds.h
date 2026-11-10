/**
 * @file audio/core/effect_kinds.h
 * @brief Functions of the effect kinds, each implemented in the file of its kind.
 */

#ifndef BOX_AUDIO_CORE_EFFECT_KINDS_H
#define BOX_AUDIO_CORE_EFFECT_KINDS_H

#include "effect_state.h"

/** The reverb kind, in reverb.c: floats of its memory at a rate.
 *
 * @param sample_rate Frames per second of the core
 * @return Floats of memory it needs
 */
int32_t box_audio_reverb_floats(
    float sample_rate);

/** The reverb kind: the smoothing of a core.
 *
 * @param core The core
 */
void box_audio_reverb_init(
    box_audio_core_t *core);

/** The reverb kind: start on clear memory.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param fade_in Whether its input fades in, as after a move
 */
void box_audio_reverb_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The reverb kind: new parameters while it runs.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 */
void box_audio_reverb_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The reverb kind: process a block.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param l Left samples of the block, processed in place
 * @param r Right samples of the block, processed in place
 * @param frames Frames of the block
 * @param silent Whether the input of the block is silent
 * @return Whether it decayed to silence: it cleared itself and is idle
 */
bool box_audio_reverb_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The reverb kind: a block skipped while idle.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param frames Frames of the skipped block
 */
void box_audio_reverb_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames);

/** The delay kind, in delay.c: floats of its memory at a rate.
 *
 * @param sample_rate Frames per second of the core
 * @return Floats of memory it needs
 */
int32_t box_audio_delay_floats(
    float sample_rate);

/** The delay kind: start on its memory.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param fade_in Whether its input fades in, as after a move
 */
void box_audio_delay_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The delay kind: new parameters while it runs.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 */
void box_audio_delay_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The delay kind: process a block.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param l Left samples of the block, processed in place
 * @param r Right samples of the block, processed in place
 * @param frames Frames of the block
 * @param silent Whether the input of the block is silent
 * @return Whether it decayed to silence: it cleared itself and is idle
 */
bool box_audio_delay_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The delay kind: a block skipped while idle.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param frames Frames of the skipped block
 */
void box_audio_delay_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames);

/** The phaser kind, in phaser.c: start at the params of fx.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param fade_in Whether its input fades in, as after a move
 */
void box_audio_phaser_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The phaser kind: new parameters while it runs.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 */
void box_audio_phaser_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The phaser kind: process a block.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param l Left samples of the block, processed in place
 * @param r Right samples of the block, processed in place
 * @param frames Frames of the block
 * @param silent Whether the input of the block is silent
 * @return Whether it decayed to silence: it cleared itself and is idle
 */
bool box_audio_phaser_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The phaser kind: a block skipped while idle.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param frames Frames of the skipped block
 */
void box_audio_phaser_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames);

/** The chorus kind, in chorus.c: floats of its memory at a rate.
 *
 * @param sample_rate Frames per second of the core
 * @return Floats of memory it needs
 */
int32_t box_audio_chorus_floats(
    float sample_rate);

/** The chorus kind: start on its memory.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param fade_in Whether its input fades in, as after a move
 */
void box_audio_chorus_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The chorus kind: new parameters while it runs.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 */
void box_audio_chorus_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The chorus kind: process a block.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param l Left samples of the block, processed in place
 * @param r Right samples of the block, processed in place
 * @param frames Frames of the block
 * @param silent Whether the input of the block is silent
 * @return Whether it decayed to silence: it cleared itself and is idle
 */
bool box_audio_chorus_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The chorus kind: a block skipped while idle.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param frames Frames of the skipped block
 */
void box_audio_chorus_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames);

/** The pitch shifter kind, in pitch_shifter.c: floats of its memory at a rate.
 *
 * @param sample_rate Frames per second of the core
 * @return Floats of memory it needs
 */
int32_t box_audio_pitch_shifter_floats(
    float sample_rate);

/** The pitch shifter kind: start on its memory.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param fade_in Whether its input fades in, as after a move
 */
void box_audio_pitch_shifter_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The pitch shifter kind: new parameters while it runs.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 */
void box_audio_pitch_shifter_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The pitch shifter kind: process a block.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param l Left samples of the block, processed in place
 * @param r Right samples of the block, processed in place
 * @param frames Frames of the block
 * @param silent Whether the input of the block is silent
 * @return Whether it decayed to silence: it cleared itself and is idle
 */
bool box_audio_pitch_shifter_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The pitch shifter kind: a block skipped while idle.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param frames Frames of the skipped block
 */
void box_audio_pitch_shifter_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames);

/** The rotary kind, in rotary.c: floats of its memory at a rate.
 *
 * @param sample_rate Frames per second of the core
 * @return Floats of memory it needs
 */
int32_t box_audio_rotary_floats(
    float sample_rate);

/** The rotary kind: start on its memory.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param fade_in Whether its input fades in, as after a move
 */
void box_audio_rotary_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The rotary kind: new parameters while it runs.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 */
void box_audio_rotary_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The rotary kind: process a block.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param l Left samples of the block, processed in place
 * @param r Right samples of the block, processed in place
 * @param frames Frames of the block
 * @param silent Whether the input of the block is silent
 * @return Whether it decayed to silence: it cleared itself and is idle
 */
bool box_audio_rotary_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The rotary kind: a block skipped while idle.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param frames Frames of the skipped block
 */
void box_audio_rotary_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames);

/** The beat repeat kind, in beat_repeat.c: floats of its memory at a rate.
 *
 * @param sample_rate Frames per second of the core
 * @return Floats of memory it needs
 */
int32_t box_audio_beat_repeat_floats(
    float sample_rate);

/** The beat repeat kind: start on its memory.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param fade_in Whether its input fades in, as after a move
 */
void box_audio_beat_repeat_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The beat repeat kind: new parameters while it runs.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 */
void box_audio_beat_repeat_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The beat repeat kind: process a block.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param l Left samples of the block, processed in place
 * @param r Right samples of the block, processed in place
 * @param frames Frames of the block
 * @param silent Whether the input of the block is silent
 * @return Whether it decayed to silence: it cleared itself and is idle
 */
bool box_audio_beat_repeat_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The beat repeat kind: a block skipped while idle.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param frames Frames of the skipped block
 */
void box_audio_beat_repeat_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames);

/** The tape stop kind, in tape_stop.c: floats of its memory at a rate.
 *
 * @param sample_rate Frames per second of the core
 * @return Floats of memory it needs
 */
int32_t box_audio_tape_stop_floats(
    float sample_rate);

/** The tape stop kind: start on its memory.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param fade_in Whether its input fades in, as after a move
 */
void box_audio_tape_stop_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The tape stop kind: new parameters while it runs.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 */
void box_audio_tape_stop_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The tape stop kind: process a block.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param l Left samples of the block, processed in place
 * @param r Right samples of the block, processed in place
 * @param frames Frames of the block
 * @param silent Whether the input of the block is silent
 * @return Whether it decayed to silence: it cleared itself and is idle
 */
bool box_audio_tape_stop_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The tape stop kind: a block skipped while idle.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param frames Frames of the skipped block
 */
void box_audio_tape_stop_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames);

/** The equalizer kind, in eq.c: start at the params of fx.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param fade_in Whether its input fades in, as after a move
 */
void box_audio_eq_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The equalizer kind: new parameters while it runs.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 */
void box_audio_eq_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The equalizer kind: process a block.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param l Left samples of the block, processed in place
 * @param r Right samples of the block, processed in place
 * @param frames Frames of the block
 * @param silent Whether the input of the block is silent
 * @return Always false: it never goes idle
 */
bool box_audio_eq_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The equalizer kind: a block skipped while idle, which it never is.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param frames Frames of the skipped block
 */
void box_audio_eq_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames);

/** The filter kind, in filter.c: start at the params of fx.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param fade_in Whether its input fades in, as after a move
 */
void box_audio_filter_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The filter kind: new parameters while it runs.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 */
void box_audio_filter_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The filter kind: process a block.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param l Left samples of the block, processed in place
 * @param r Right samples of the block, processed in place
 * @param frames Frames of the block
 * @param silent Whether the input of the block is silent
 * @return Always false: it never goes idle
 */
bool box_audio_filter_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The filter kind: a block skipped while idle, which it never is.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param frames Frames of the skipped block
 */
void box_audio_filter_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames);

/** The compressor kind, in dynamics.c: start at the params of fx.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param fade_in Whether its input fades in, as after a move
 */
void box_audio_compressor_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The compressor kind: new parameters while it runs.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 */
void box_audio_compressor_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The compressor kind: process a block, keyed on the output of its key bus when that came
 * before its bus in the order.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param l Left samples of the block, processed in place
 * @param r Right samples of the block, processed in place
 * @param frames Frames of the block
 * @param silent Whether the input of the block is silent
 * @return Always false: it never goes idle
 */
bool box_audio_compressor_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The gate kind, in dynamics.c: start at the params of fx.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param fade_in Whether its input fades in, as after a move
 */
void box_audio_gate_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The gate kind: new parameters while it runs.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 */
void box_audio_gate_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The gate kind: process a block.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param l Left samples of the block, processed in place
 * @param r Right samples of the block, processed in place
 * @param frames Frames of the block
 * @param silent Whether the input of the block is silent
 * @return Always false: it never goes idle
 */
bool box_audio_gate_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The compressor and the gate: a block skipped while idle, which they never are.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param frames Frames of the skipped block
 */
void box_audio_dynamics_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames);

/** The saturator kind, in saturator.c: start at the params of fx.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param fade_in Whether its input fades in, as after a move
 */
void box_audio_saturator_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The saturator kind: new parameters while it runs.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 */
void box_audio_saturator_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The saturator kind: process a block; true once a silent block left its state silent.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param l Left samples of the block, processed in place
 * @param r Right samples of the block, processed in place
 * @param frames Frames of the block
 * @param silent Whether the input of the block is silent
 * @return Whether it decayed to silence: it cleared itself and is idle
 */
bool box_audio_saturator_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The saturator kind: a block skipped while idle.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param frames Frames of the skipped block
 */
void box_audio_saturator_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames);

/** The transient shaper kind, in dynamics.c: start at the params of fx.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param fade_in Whether its input fades in, as after a move
 */
void box_audio_transient_shaper_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The transient shaper kind: new parameters while it runs.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 */
void box_audio_transient_shaper_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The transient shaper kind: process a block; true once a silent block left its envelopes
 * and its gain at rest.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param l Left samples of the block, processed in place
 * @param r Right samples of the block, processed in place
 * @param frames Frames of the block
 * @param silent Whether the input of the block is silent
 * @return Whether it decayed to silence: it cleared itself and is idle
 */
bool box_audio_transient_shaper_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The transient shaper kind: a block skipped while idle.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param frames Frames of the skipped block
 */
void box_audio_transient_shaper_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames);

/** The vocoder kind: floats of memory, its bands.
 *
 * @param sample_rate Frames per second of the core
 * @return Floats of memory it needs
 */
int32_t box_audio_vocoder_floats(
    float sample_rate);

/** The vocoder kind: start on the memory of its slot.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param fade_in Whether its input fades in, as after a move
 */
void box_audio_vocoder_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The vocoder kind: new parameters while it runs.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 */
void box_audio_vocoder_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The vocoder kind: process a block on the output of its key bus.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param l Left samples of the block, processed in place
 * @param r Right samples of the block, processed in place
 * @param frames Frames of the block
 * @param silent Whether the input of the block is silent
 * @return Always false: it never goes idle
 */
bool box_audio_vocoder_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The vocoder kind: a block skipped while idle.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param frames Frames of the skipped block
 */
void box_audio_vocoder_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames);

/** The frequency shifter kind, in frequency_shifter.c: start at the params of fx.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param fade_in Whether its input fades in, as after a move
 */
void box_audio_frequency_shifter_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The frequency shifter kind: new parameters while it runs.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 */
void box_audio_frequency_shifter_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The frequency shifter kind: process a block; true once a silent block left its allpasses
 * and its feedback decayed.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param l Left samples of the block, processed in place
 * @param r Right samples of the block, processed in place
 * @param frames Frames of the block
 * @param silent Whether the input of the block is silent
 * @return Whether it decayed to silence: it cleared itself and is idle
 */
bool box_audio_frequency_shifter_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The frequency shifter kind: a block skipped while idle.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param frames Frames of the skipped block
 */
void box_audio_frequency_shifter_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames);

/** The pattern gate kind, in pattern_gate.c: start at the params of fx, open as the step of
 * the frame of the core clock is.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param fade_in Whether its input fades in, as after a move
 */
void box_audio_pattern_gate_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The pattern gate kind: new parameters while it runs.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 */
void box_audio_pattern_gate_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The pattern gate kind: process a block; true when the block is silent, which the gain
 * keeps silent.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param l Left samples of the block, processed in place
 * @param r Right samples of the block, processed in place
 * @param frames Frames of the block
 * @param silent Whether the input of the block is silent
 * @return Whether the block is silent
 */
bool box_audio_pattern_gate_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The pattern gate kind: a block skipped while idle, over which its gain moves on as it
 * would have rendered.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param frames Frames of the skipped block
 */
void box_audio_pattern_gate_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames);

#endif
