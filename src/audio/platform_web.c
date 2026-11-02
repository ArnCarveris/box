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
 * and calls render once per quantum. Messages sent before the node exists wait in the page.
 *
 * @param core Bytes of the wasm of the core
 * @param size Size of the wasm in bytes
 * @param quantum Frames the core renders per call at most
 * @return 0 when the load started, else the reason code of the failure
 */
EM_JS(int, box_audio_web_create, (const unsigned char *core, int size, int quantum), {
    var worklet = function() {
        var EVENTS = 64;
        var SOUND_FREE = 3;
        var STATUS_QUANTA = 32;
        class BoxAudioProcessor extends AudioWorkletProcessor {
            constructor(options) {
                super();
                var o = options.processorOptions;
                var core = new WebAssembly.Instance(new WebAssembly.Module(o.core), {}).exports;
                if (!core.box_audio_core_wasm_init(o.sampleRate)) {
                    throw new Error('the audio core did not start');
                }
                this.core = core;
                this.quantum = o.quantum;
                this.buffer = null;
                this.sounds = new Map();
                this.quanta = 0;
                this.batches = 0;
                this.capture = null;
                this.voices = -1;
                this.views();
                var self = this;
                this.port.onmessage = function(e) { self.command(e.data); };
            }

            // The memory of the core grows when it allocates, which detaches the views
            views() {
                var buffer = this.core.memory.buffer;
                if (buffer !== this.buffer) {
                    this.buffer = buffer;
                    this.left = new Float32Array(buffer, this.core.box_audio_core_wasm_left(),
                        this.quantum);
                    this.right = new Float32Array(buffer, this.core.box_audio_core_wasm_right(),
                        this.quantum);
                    this.events = new Int32Array(buffer, this.core.box_audio_core_wasm_events(),
                        EVENTS * 3);
                }
            }

            copy(bytes) {
                var at = this.core.box_audio_core_wasm_alloc(bytes.length);
                if (at) {
                    this.views();
                    new Uint8Array(this.buffer, at, bytes.length).set(bytes);
                }
                return at;
            }

            command(m) {
                var core = this.core, at, bytes, ok;
                if (m.type === 'commands') {
                    bytes = new Uint8Array(m.data);
                    at = this.copy(bytes);
                    if (at) {
                        core.box_audio_core_wasm_apply(at, bytes.length);
                        core.box_audio_core_wasm_free(at);
                        this.batches++;
                    }
                } else if (m.type === 'sound') {
                    bytes = new Uint8Array(m.data);
                    at = this.sounds.has(m.id) ? 0 : this.copy(bytes);
                    ok = !!at && !!core.box_audio_core_wasm_sound(m.id, at, m.frames, m.channels,
                        m.sampleRate);
                    if (ok) {
                        this.sounds.set(m.id, at);
                    } else if (at) {
                        core.box_audio_core_wasm_free(at);
                    }
                    this.port.postMessage({ type: 'sound', id: m.id, ok: ok });
                } else if (m.type === 'capture') {
                    this.capture = { data: new Float32Array(m.frames * 2), frames: m.frames,
                        at: 0 };
                } else if (m.type === 'stats') {
                    this.port.postMessage({ type: 'stats', id: m.id, quanta: this.quanta,
                        frame: currentFrame, time: currentTime, sampleRate: sampleRate,
                        voices: core.box_audio_core_wasm_voices(), batches: this.batches,
                        sounds: this.sounds.size, memory: this.buffer.byteLength });
                }
            }

            // Sounds the core no longer uses are freed here, on the side that owns them
            receive() {
                var n = this.core.box_audio_core_wasm_read_events();
                if (!n) {
                    return;
                }
                this.views();
                var list = [];
                for (var i = 0; i < n; i++) {
                    var e = { type: this.events[i * 3], id: this.events[i * 3 + 1] >>> 0,
                        value: this.events[i * 3 + 2] };
                    if (e.type === SOUND_FREE && this.sounds.has(e.id)) {
                        this.core.box_audio_core_wasm_free(this.sounds.get(e.id));
                        this.sounds.delete(e.id);
                    }
                    list.push(e);
                }
                this.port.postMessage({ type: 'events', events: list });
            }

            // Interleaves the frames of a quantum into the capture that records, if any
            record(n) {
                var c = this.capture;
                if (!c) {
                    return;
                }
                var count = Math.min(n, c.frames - c.at);
                for (var i = 0; i < count; i++) {
                    c.data[(c.at + i) * 2] = this.left[i];
                    c.data[(c.at + i) * 2 + 1] = this.right[i];
                }
                c.at += count;
                if (c.at >= c.frames) {
                    this.capture = null;
                    this.port.postMessage({ type: 'capture', data: c.data.buffer },
                        [c.data.buffer]);
                }
            }

            // The voices go to the main module every few quanta, when they changed
            status() {
                var voices = this.core.box_audio_core_wasm_voices();
                if (voices !== this.voices) {
                    this.voices = voices;
                    this.port.postMessage({ type: 'status', voices: voices });
                }
            }

            process(inputs, outputs) {
                var out = outputs[0];
                var frames = out.length ? out[0].length : 0;
                this.views();
                for (var at = 0; at < frames; at += this.quantum) {
                    var n = Math.min(this.quantum, frames - at);
                    this.core.box_audio_core_wasm_render(n);
                    this.record(n);
                    out[0].set(n === this.quantum ? this.left : this.left.subarray(0, n), at);
                    if (out.length > 1) {
                        out[1].set(n === this.quantum ? this.right : this.right.subarray(0, n),
                            at);
                    }
                }
                this.receive();
                this.quanta++;
                if (this.quanta % STATUS_QUANTA === 0) {
                    this.status();
                }
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

    var audio = { ctx: ctx, node: null, pending: [], requests: {}, sounds: {}, id: 0,
        failed: 0, coreSize: size, events: [], queue: [], onevents: null, gestures: 0,
        resumes: 0, voices: 0, gain: null, hidden: false, wasRunning: false, suspends: 0,
        visibilityResumes: 0, capturing: false, captured: null };
    audio.post = function(m, transfer) {
        if (audio.node) {
            audio.node.port.postMessage(m, transfer || []);
        } else if (audio.pending) {
            audio.pending.push([m, transfer || []]);
        }
    };
    audio.stats = function() {
        return new Promise(function(resolve) {
            var id = ++audio.id;
            audio.requests[id] = resolve;
            audio.post({ type: 'stats', id: id });
        });
    };
    // A batch of commands of the core, an ArrayBuffer that is transferred or a typed array
    // that is copied
    audio.commands = function(bytes) {
        var data = bytes instanceof ArrayBuffer ? bytes :
            bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
        audio.post({ type: 'commands', data: data }, [data]);
    };
    // A sound of interleaved float frames, copied; the promise resolves with whether the core
    // took it
    audio.sound = function(id, samples, channels, rate) {
        var data = Float32Array.from(samples).buffer;
        return new Promise(function(resolve) {
            audio.sounds[id] = resolve;
            audio.post({ type: 'sound', id: id, channels: channels,
                frames: Math.floor(data.byteLength / 4 / channels),
                sampleRate: rate || ctx.sampleRate, data: data }, [data]);
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
    var receive = function(m) {
        var resolve;
        if (m.type === 'stats') {
            resolve = audio.requests[m.id];
            delete audio.requests[m.id];
        } else if (m.type === 'sound') {
            resolve = audio.sounds[m.id];
            delete audio.sounds[m.id];
            m = m.ok;
        } else if (m.type === 'status') {
            audio.voices = m.voices;
        } else if (m.type === 'capture') {
            audio.captured = new Float32Array(m.data);
        } else if (m.type === 'events') {
            Array.prototype.push.apply(audio.events, m.events);
            if (audio.events.length > 4096) {
                audio.events.splice(0, audio.events.length - 4096);
            }
            // The queue the transport of the main module takes from (transport.c)
            Array.prototype.push.apply(audio.queue, m.events);
            if (audio.queue.length > 4096) {
                audio.queue.splice(0, audio.queue.length - 4096);
            }
            if (audio.onevents) {
                audio.onevents(m.events);
            }
        }
        if (resolve) {
            resolve(m);
        }
    };

    // A hidden page gets no animation frames, so no commands would end its loops: fade out
    // and suspend, and resume where the page was running when it shows again
    var FADE = 0.05;
    var ramp = function(to) {
        if (!audio.gain) {
            return;
        }
        var g = audio.gain.gain, t = ctx.currentTime;
        g.cancelScheduledValues(t);
        g.setValueAtTime(g.value, t);
        g.linearRampToValueAtTime(to, t + FADE);
    };
    audio.visibility = function(hidden) {
        if (audio.failed || hidden === audio.hidden) {
            return;
        }
        audio.hidden = hidden;
        clearTimeout(audio.suspendTimer);
        if (hidden) {
            audio.wasRunning = ctx.state === 'running';
            if (!audio.wasRunning) {
                return;
            }
            ramp(0);
            audio.suspendTimer = setTimeout(function() {
                if (audio.hidden && !audio.failed) {
                    audio.suspends++;
                    ctx.suspend().catch(function() { });
                }
            }, FADE * 1000 + 20);
        } else if (audio.wasRunning) {
            audio.wasRunning = false;
            audio.visibilityResumes++;
            ctx.resume().then(function() { ramp(1); }).catch(function() { });
        }
    };
    if (typeof document !== 'undefined') {
        document.addEventListener('visibilitychange', function() {
            audio.visibility(document.visibilityState === 'hidden');
        });
    }

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
        node.port.onmessage = function(e) { receive(e.data); };
        var gain = ctx.createGain();
        gain.gain.value = audio.hidden ? 0 : 1;
        gain.connect(ctx.destination);
        node.connect(gain);
        audio.gain = gain;
        audio.node = node;
        var pending = audio.pending || [];
        audio.pending = null;
        pending.forEach(function(p) { node.port.postMessage(p[0], p[1]); });
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

/** Post a copy of a command batch to the worklet.
 *
 * @param commands The bytes
 * @param size Number of bytes
 */
EM_JS(void, box_audio_web_post_commands, (const void *commands, int size), {
    var audio = Module.boxAudio;
    if (audio && !audio.failed) {
        audio.commands(HEAPU8.slice(commands, commands + size).buffer);
    }
});

/** Post a copy of a sound to the worklet.
 *
 * @param id Id of the sound
 * @param data Frames, interleaved when stereo
 * @param count Number of floats, frames times channels
 * @param channels 1 or 2
 * @param rate Frames per second of the sound
 */
EM_JS(void, box_audio_web_post_sound, (unsigned id, const float *data, int count, int channels,
    float rate), {
    var audio = Module.boxAudio;
    if (audio && !audio.failed) {
        audio.sound(id >>> 0, new Float32Array(HEAPU8.slice(data, data + count * 4).buffer),
            channels, rate);
    }
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

EMSCRIPTEN_KEEPALIVE void box_audio_web_apply(
    const void *commands,
    int32_t size)
{
    if (box_audio_web_started && commands && size > 0) {
        box_audio_web_post_commands(commands, size);
    }
}

EMSCRIPTEN_KEEPALIVE void box_audio_web_sound(
    uint32_t id,
    const float *data,
    int32_t frames,
    int32_t channels,
    float sample_rate)
{
    if (box_audio_web_started && data && frames > 0 && (channels == 1 || channels == 2)) {
        box_audio_web_post_sound(id, data, frames * channels, channels, sample_rate);
    }
}

void box_audio_web_start(void)
{
    int reason;
    if (box_audio_web_started) {
        goto done;
    }

    box_audio_web_started = true;
    reason = box_audio_web_create(box_audio_core_wasm, (int)sizeof(box_audio_core_wasm),
        BOX_AUDIO_CORE_QUANTUM);
    if (reason) {
        box_audio_web_failed(reason);
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

void box_audio_web_apply(
    const void *commands,
    int32_t size)
{
    (void)commands;
    (void)size;
}

void box_audio_web_sound(
    uint32_t id,
    const float *data,
    int32_t frames,
    int32_t channels,
    float sample_rate)
{
    (void)id;
    (void)data;
    (void)frames;
    (void)channels;
    (void)sample_rate;
}

#endif
