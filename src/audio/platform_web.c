/**
 * @file audio/platform_web.c
 * @brief The browser audio output: the core runs from its own wasm in an AudioWorklet.
 *
 * The bytes of the core come from tools/audio_core.py; the worklet script is made from a Blob
 * URL. The main module and the worklet share no memory and talk over the port of the node.
 */

#include "platform.h"

#ifdef __EMSCRIPTEN__

#include <emscripten/emscripten.h>
#include <flecs.h>
#include "core/core.h"
#include "../../.bake/generated/box_audio_core_wasm.h"

/** Whether the page has created its audio, once per page whatever the number of worlds */
static bool box_audio_web_started;

/** Why the audio of the page failed, by the reason code of the JavaScript side */
static const char *box_audio_web_reasons[] = {
    "",
    "the browser has no Web Audio",
    "the browser has no AudioWorklet",
    "the worklet of the audio core did not load",
    "the audio core did not start in the worklet"
};

/** Create the AudioContext, suspended until a gesture, and load the worklet from a Blob URL.
 *
 * The worklet compiles the bytes of the core synchronously in the constructor of its processor
 * and calls render once per quantum. Commands sent before the node exists wait in the page.
 *
 * @param core Bytes of the wasm of the core
 * @param size Size of the wasm in bytes
 * @param quantum Frames the core renders per call at most
 * @return 0 when the load started, else the reason code of the failure
 */
EM_JS(int, box_audio_web_create, (const unsigned char *core, int size, int quantum), {
    var worklet = function() {
        class BoxAudioProcessor extends AudioWorkletProcessor {
            constructor(options) {
                super();
                var o = options.processorOptions;
                var core = new WebAssembly.Instance(new WebAssembly.Module(o.core), {}).exports;
                core.box_audio_core_wasm_init(o.sampleRate);
                this.core = core;
                this.quantum = o.quantum;
                this.left = new Float32Array(core.memory.buffer,
                    core.box_audio_core_wasm_left(), o.quantum);
                this.right = new Float32Array(core.memory.buffer,
                    core.box_audio_core_wasm_right(), o.quantum);
                this.quanta = 0;
                this.frequency = 0;
                this.level = 0;
                var self = this;
                this.port.onmessage = function(e) { self.command(e.data); };
            }

            command(m) {
                if (m.type === 'tone') {
                    this.frequency = m.frequency;
                    this.level = m.level;
                    this.core.box_audio_core_wasm_set_frequency(m.frequency);
                    this.core.box_audio_core_wasm_set_level(m.level);
                } else if (m.type === 'stats') {
                    this.port.postMessage({ type: 'stats', id: m.id, quanta: this.quanta,
                        frame: currentFrame, time: currentTime, sampleRate: sampleRate,
                        frequency: this.frequency, level: this.level });
                }
            }

            process(inputs, outputs) {
                var out = outputs[0];
                var frames = out.length ? out[0].length : 0;
                for (var at = 0; at < frames; at += this.quantum) {
                    var n = Math.min(this.quantum, frames - at);
                    this.core.box_audio_core_wasm_render(n);
                    out[0].set(n === this.quantum ? this.left : this.left.subarray(0, n), at);
                    if (out.length > 1) {
                        out[1].set(n === this.quantum ? this.right : this.right.subarray(0, n),
                            at);
                    }
                }
                this.quanta++;
                return true;
            }
        }
        registerProcessor('box-audio-core', BoxAudioProcessor);
    };

    if (typeof AudioContext === 'undefined') {
        return 1;
    }

    var ctx;
    try {
        ctx = new AudioContext({ latencyHint: 'interactive' });
    } catch (e) {
        return 1;
    }

    if (!ctx.audioWorklet || typeof AudioWorkletNode === 'undefined') {
        ctx.close().catch(function() { });
        return 2;
    }

    var audio = { ctx: ctx, node: null, pending: [], requests: {}, id: 0, failed: 0,
        coreSize: size, gestures: 0, resumes: 0 };
    audio.post = function(m) {
        if (audio.node) {
            audio.node.port.postMessage(m);
        } else if (audio.pending) {
            audio.pending.push(m);
        }
    };
    audio.stats = function() {
        return new Promise(function(resolve) {
            var id = ++audio.id;
            audio.requests[id] = resolve;
            audio.post({ type: 'stats', id: id });
        });
    };
    var fail = function(code) {
        if (audio.failed) {
            return;
        }
        audio.failed = code;
        audio.pending = null;
        if (audio.node) {
            audio.node.disconnect();
        }
        ctx.close().catch(function() { });
        var report = Module['_box_audio_web_failed'] ||
            (typeof _box_audio_web_failed !== 'undefined' ? _box_audio_web_failed : null);
        if (report) {
            report(code);
        } else {
            console.warn('box.audio: the audio failed (' + code + '), the app runs without sound');
        }
    };

    var bytes = HEAPU8.slice(core, core + size).buffer;
    var url = URL.createObjectURL(new Blob(['(' + worklet.toString() + ')();'],
        { type: 'application/javascript' }));
    Module.boxAudio = audio;
    ctx.audioWorklet.addModule(url).then(function() {
        URL.revokeObjectURL(url);
        var node = new AudioWorkletNode(ctx, 'box-audio-core', {
            numberOfInputs: 0,
            numberOfOutputs: 1,
            outputChannelCount: [2],
            processorOptions: { core: bytes, sampleRate: ctx.sampleRate, quantum: quantum }
        });
        node.onprocessorerror = function() { fail(4); };
        node.port.onmessage = function(e) {
            var resolve = audio.requests[e.data.id];
            if (resolve) {
                delete audio.requests[e.data.id];
                resolve(e.data);
            }
        };
        node.connect(ctx.destination);
        audio.node = node;
        var pending = audio.pending || [];
        audio.pending = null;
        pending.forEach(function(m) { node.port.postMessage(m); });
    }).catch(function() {
        URL.revokeObjectURL(url);
        fail(3);
    });

    return 0;
});

/** Resume the AudioContext when it is suspended. The promise of resume is not waited on; the
 * browser refuses it outside a gesture, which is not an error.
 *
 * Module.boxAudio counts the gestures that reach it and the calls of resume, which tells an
 * input that never arrives from a resume the browser refuses.
 */
EM_JS(void, box_audio_web_resume, (void), {
    var audio = Module.boxAudio;
    if (audio) {
        audio.gestures++;
    }
    if (audio && !audio.failed && audio.ctx.state === 'suspended') {
        audio.resumes++;
        audio.ctx.resume().catch(function() { });
    }
});

/** Post the tone command to the core, or keep it in the page until the node exists.
 *
 * @param frequency Frequency in Hz
 * @param level Peak level, 0..1
 */
EM_JS(void, box_audio_web_post_tone, (float frequency, float level), {
    var audio = Module.boxAudio;
    if (audio && !audio.failed) {
        audio.post({ type: 'tone', frequency: frequency, level: level });
    }
});

/** Read the temporary ?tone=<hz> parameter of the address of the page.
 *
 * @return The frequency in Hz, 0 without a valid parameter
 */
EM_JS(double, box_audio_web_query_tone, (void), {
    if (typeof location === 'undefined' || typeof URLSearchParams === 'undefined') {
        return 0;
    }
    var hz = parseFloat(new URLSearchParams(location.search).get('tone'));
    return isFinite(hz) && hz > 0 ? hz : 0;
});

/** Log the one warning of a failed audio, which the page reports when the worklet or the core
 * fails after box_audio_web_create returned.
 *
 * @param reason The reason code of the JavaScript side
 */
EMSCRIPTEN_KEEPALIVE void box_audio_web_failed(
    int reason)
{
    int32_t count = (int32_t)(sizeof(box_audio_web_reasons) / sizeof(box_audio_web_reasons[0]));
    const char *text = reason > 0 && reason < count ?
        box_audio_web_reasons[reason] : "the audio failed";
    ecs_warn("box.audio: %s, the app runs without sound", text);
}

EMSCRIPTEN_KEEPALIVE void box_audio_web_tone(
    float frequency,
    float level)
{
    box_audio_web_post_tone(frequency, level);
}

void box_audio_web_start(void)
{
    int reason;
    double tone;
    if (box_audio_web_started) {
        goto done;
    }

    box_audio_web_started = true;
    reason = box_audio_web_create(box_audio_core_wasm, (int)sizeof(box_audio_core_wasm),
        BOX_AUDIO_CORE_QUANTUM);
    if (reason) {
        box_audio_web_failed(reason);
        goto done;
    }

    tone = box_audio_web_query_tone();
    if (tone > 0) {
        box_audio_web_tone((float)tone, BOX_AUDIO_WEB_TONE_LEVEL);
    }

done:
    return;
}

void box_audio_web_gesture(void)
{
    if (box_audio_web_started) {
        box_audio_web_resume();
    }
}

#else

void box_audio_web_start(void)
{
}

void box_audio_web_gesture(void)
{
}

void box_audio_web_tone(
    float frequency,
    float level)
{
    (void)frequency;
    (void)level;
}

#endif
