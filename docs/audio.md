# Audio

This page is the guide to audio in box: how sound is put together, the components a scene uses, a
worked example, the rules of the web, notes for native apps, the limits of this version and how
to capture and inspect what an app plays. After the guide comes the [reference](#reference): the
audio core, the effects and the reverb, the synth, the transports, the native and the web outputs and the
`box.audio` module in detail.

## The model

A scene makes sound with components, from a script, the REST API or C; nothing needs to be
written in C to play a sound. The `box.audio` module turns them into commands of the **audio
core**, a small mixer in plain C that renders on its own thread, once per frame and in one batch
(see [The box.audio module](#the-boxaudio-module)):

- **Sounds** are what can play: samples in memory (`AudioPcm`), a WAV or FLAC file
  (`SoundFile`), or a synth patch made in script (`SynthSound`). A sound plays once with
  `box.audio.play` or `box.audio.play_at`, or for as long as an entity with an `AudioSource`
  exists. Music is not a sound: an `AudioStream` streams one Ogg Vorbis file.
- **Voices** play the sounds, 64 at once; a play past that steals the quietest voice of at most
  its priority: one shots rank lowest, the notes and clips of a track above them and music
  highest. A voice has a volume, a pan and a pitch, and plays on a bus.
- **Buses** are the channels of the mixer and mix voices and other buses: each has a volume, a
  pan, a stereo width, a mute, a solo, a kind (group, return or SFX), an output and up to 4
  sends, pre or post fader. Bus 0 is the master, `box.audio.master` in script; every other bus
  ends there. Each bus has 4 insert slots that run **effects**, entities such as a `Reverb`,
  an `Equalizer`, an `AudioFilter`, a `Compressor`, a `Gate` or a `Delay`, in order on its
  signal; a bus with a reverb or a delay in a slot is a return, which other buses reach through
  a send. A compressor may key on another bus, to duck one channel under another, and a delay
  may follow the tempo of the `Transport`.
- **Space**: a source with `AudioSpatial` gets its volume and pan every frame from its distance
  to the listener and its direction, the camera of the main view or the entity with
  `AudioListener`.
- **The master** has a limiter that keeps the peak under 0.95, and `AudioSettings` sets its volume
  and mute.

The core never waits for the main thread: a slow frame delays the next commands, never the
sound that plays. Everything ramps over 5 ms, so nothing clicks.

## Components

| Component | Use it for |
|-----------|------------|
| `AudioBus` | A channel of the mixer: `output`, `volume`, `pan`, `width`, `mute`, `solo`, `kind`, up to 4 `sends` and 4 insert slots, `effects`, see [Mixer channels](#mixer-channels) |
| `Reverb` | An effect: the reverb of a room, which runs in the insert slot of a bus that names its entity, see [Effects](#effects) and [Reverb](#reverb) |
| `Equalizer` | An effect: 4 bands of peaks, shelves and cuts, such as a low cut on a vocal or SFX bus, see [Equalizer](#equalizer) |
| `AudioFilter` | An effect: the synth's filter on a bus, a lowpass, highpass, bandpass or notch of 12 or 24 dB, such as a filter sweep on a music group, see [Filter effect](#filter-effect) |
| `Compressor` | An effect: a compressor with a soft knee, keyed on its own input or on another channel (`sidechain`) to duck music under dialog or explosions, see [Compressor](#compressor) |
| `Gate` | An effect: a gate with a hold, a range and hysteresis, see [Gate](#gate) |
| `Delay` | An effect: echoes with feedback, damping and ping pong, in seconds or synced to the tempo in beats, such as a dotted eighth on a return, see [Delay](#delay) |
| `AudioOutput` | The bus a source, a stream or a sound plays on (the master without one) |
| `AudioPcm` | A sound from samples, from script or C |
| `SoundFile` | A short sound from a WAV or FLAC file, decoded into memory, see [Sound files](#sound-files) |
| `SoundStatus` | Read only: whether a `SoundFile` loads, is ready or failed |
| `SynthSound` | A sound made by the synth from layers of `SynthOscillator`, `SynthEnvelope`, `SynthFilter` and `SynthFilterEnvelope`, layers that may modulate each other, see [Synth sounds](#synth-sounds) |
| `SynthUnison` | Unison voices of a synth layer, detuned and spread in stereo, see [Unison and width](#unison-and-width) |
| `SynthGlide` | Portamento and mono legato of a synth sound, see [Glide and legato](#glide-and-legato) |
| `SynthLfo` | An LFO of a synth sound: vibrato, tremolo, a moving cutoff or pulse width, see [LFOs](#lfos) |
| `AudioSource` | A sound that plays while its entity exists, once or in a loop, with a volume and a pitch |
| `AudioSpatial` | Places a source in the world: `min_distance`, `max_distance`, `rolloff`, see [Spatial audio](#spatial-audio) |
| `AudioListener` | The entity that hears the spatial sources, the camera without one |
| `AudioStream` | Music: one Ogg Vorbis file that streams, see [Music](#music) |
| `AudioStreamStatus` | Read only: the state, position, buffer, underruns and decode time of a stream |
| `AudioSettings` | Singleton: the master volume and mute |
| `AudioStatus` | Singleton, read only: the state of the output, its rate, the voices, steals, refused plays, load and the clock of its core |
| `AudioMeter` | Read only, opt-in: the peak and the RMS of each channel of a mixer channel, the master included, and the gain reduction of an effect, see [Meters](#meters) |
| `Transport` | Singleton: the timeline in beats, its tempo, time signature, position, play and stop, loop and metronome, see [Transport](#transport) |
| `AudioQuantize` | Enum of `box.audio.play_quantized`: `AudioQuantizeNone`, `AudioQuantizeBeat`, `AudioQuantizeBar`, see [Quantized plays](#quantized-plays) |
| `Instrument` | A synth or a sampler that plays notes with a pitch, a velocity and a duration: `sound`, `root`, `velocity_curve`, `gate`, `release`, see [Instruments](#instruments) |
| `NoteClip` | A reusable clip of notes: `length` and `notes`, a vector of `Note` (`start`, `duration`, `pitch`, `velocity`, `pan`), see [Clips and tracks](#clips-and-tracks) |
| `AudioClip` | A reusable clip of recorded audio: `sound`, `offset` (seconds into it) and `length` (beats), see [Audio clips](#audio-clips) |
| `Track` | A mixer channel of the timeline whose notes play on an `instrument` and whose audio clips play on its bus, at a voice `priority` (1) above the one shots; it brings an `AudioBus`, see [Clips and tracks](#clips-and-tracks) |
| `ClipPlacement` | On a child of a track, a note or audio clip on the timeline: `clip`, `start`, `duration`, `loop`, `offset`, `gain`, `transpose`, see [Clips and tracks](#clips-and-tracks) |

The functions are `box.audio.play(sound, volume)`, `box.audio.play_at(sound, {x, y, z},
volume)`, `box.audio.play_quantized(sound, volume, quantize)`,
`box.audio.play_note(instrument, pitch, velocity, duration)` and
`box.audio.capture(seconds, path)`, the same as `box_audio_play`, `box_audio_play_at`,
`box_audio_play_quantized`, `box_audio_play_note` and `box_audio_capture` in C. The members and their defaults are in the
table of [The box.audio module](#the-boxaudio-module).

## A worked example: the dust merchant

`etc/scenes/dust_merchant/audio.flecs` gives the cabin of `etc/scenes/dust_merchant.flecs` its
sound, in script only. Run it with `bake3 run apps/boxrun --local-env --
etc/scenes/dust_merchant.flecs`. It has four buses, all into the master:

```
cabin_verb {
  AudioBus: {output: master, kind: AudioChannelReturn, effects: [cabin_verb]}
  Reverb: {decay: 0.75, damping: 0.6, predelay: 0.008, size: 0.25}
}
world { AudioBus: {output: master, sends: [{bus: cabin_verb, level: 0.28}]} }
ui { AudioBus: {output: master, volume: 0.55} }
music { AudioBus: {output: master, volume: 0.45} }
```

A small metal cabin rings briefly: a short decay, a small size and little predelay. The bus
runs its own reverb in its first insert slot. `world`, the bus of the engine, sends to it; the controls play on `ui`, dry and close.

**The controls** click with synth sounds, prefabs that need no file. Each has a pitch and a level
variance, so a row of switches flipped in a row does not sound like one sample repeated:

```
prefab SfxToggle {
  AudioOutput: {bus: ui}
  SynthSound: {seed: 21, pitch_variance: 0.08, level_variance: 0.12}
  SynthEnvelope: {attack: 0.0005, decay: 0.04}
  SynthFilter: {kind: SynthHighPass, cutoff: 600, resonance: 0.15}
  snap {
    SynthOscillator: {wave: SynthNoise, level: 0.5}
    SynthEnvelope: {attack: 0.0005, decay: 0.012}
  }
  tick {
    SynthOscillator: {wave: SynthTriangle, frequency: 2600, frequency_end: 1400, sweep: 0.02, level: 0.45}
    SynthEnvelope: {attack: 0.0005, decay: 0.025}
  }
  knock { SynthOscillator: {wave: SynthSine, frequency: 1100, frequency_end: 700, sweep: 0.04, level: 0.35} }
}
```

The widgets of `etc/scenes/dust_merchant/widgets.flecs` play them where they handle their input,
in the same async blocks that flip and turn them, so a sound is never out of step with what the
control does:

```
async {
  while true {
    await on.click(this)
    sw[sig] = !sw[sig]
    const snap = box.audio.play(SfxToggle, 0.8)
    emit Flipped(sig: sig)
  }
}
```

A knob or a lever ticks at each of its 24 detents while it is dragged: the handler compares the
detent before and after the move (`floor(kn[sig] * DETENTS)`). The guard of the main switch, the
breakers, the cap buttons and the radio have sounds of their own. `audio.flecs` is included after
`state.flecs`, whose state the engine reads, and before `widgets.flecs`, whose controls name the
sounds.

**The engine** is a looping synth drone at a fixed pitch, placed on the starboard engine pod
behind the cabin. `eye`, the camera of the main view, is the listener, so the drone pans and
changes its level as the camera moves between the stations (keys 1 to 6) and as it looks around.
The scene tags it: its screens render their own views before the main one, and without a tag the
camera of the first view would listen, far from the engine:

```
eye { AudioListener }

engine_sound {
  Position: {3.1, 0.6, 3.8}
  AudioSource: {sound: EngineHum, loop: true, volume: 0.3 + 0.2 * bool_f(bus) + 0.3 * bool_f(reactor_on)}
  AudioOutput: {bus: world}
  AudioSpatial: {min_distance: 1.5, max_distance: 30}
}
```

Its volume follows two booleans of the scene, the main bus and the reactor, which change a few
times in a session; a script that changes the volume or the pitch of a source every frame works
too, but re-evaluates the entity every frame.

**The music** is one looping track, `etc/scenes/dust_merchant/assets/dust_merchant_loop.ogg`, on
the bus `music`:

```
music_track {
  AudioStream: {path: "etc/scenes/dust_merchant/assets/dust_merchant_loop.ogg"}
  AudioOutput: {bus: music}
}
```

It is made in this repository with the synth of box, see
[Making music with the synth](#making-music-with-the-synth). A track from elsewhere replaces it by
taking its path.

## Making music with the synth

`etc/scenes/dust_merchant/assets/music_score.flecs` is a score: synth instruments as templates
(a pad, a bass, a lead) and prefabs (a kick, a rim, a hat), and one async block that plays 16
bars at 120 bpm, a step of an eighth note every 15 frames. `make_music.py` next to it renders and
encodes it:

```sh
python3 etc/scenes/dust_merchant/assets/make_music.py <boxrun binary>
```

It runs the score headless at a fixed delta of 1/60 s with `box.audio.capture` at load, so the
render is the same in every run, adds the tail after the 32 seconds (the release of the last
notes and the reverb) to the start, which makes a seamless loop, normalizes it and encodes it
with `tools/ogg_vorbis.py`, a small mono Vorbis encoder of this repository, at the finest
quantizer that keeps the file under 1 MB. The track is checked in; run the script again after a
change of the score.

## The web rules

The web build plays the same scene as native, with a few rules of the browser (details in
[Starting and failing](#starting-and-failing) and [Two threads on the web](#two-threads-on-the-web)):

- **A gesture starts the audio.** Until the first key or mouse button, `AudioStatus.state` is
  `AudioWaitingForGesture` and the app runs silently. Plays that do not loop are dropped in that
  time; looping sources and streams start and are heard from the gesture on, see
  [Before the gesture](#before-the-gesture). Do not count on a sound at load.
- **A hidden page is silent.** It fades out over 50 ms and suspends the context
  (`AudioSuspended`); shown again it resumes. Native apps keep playing without focus.
- **Music files.** A track under `etc` is embedded in the download of the page; that is fine for
  a short loop such as the dust merchant's (well under 1 MB), not for an album. A longer track is
  kept out of the embedded folders and copied next to the page under the path the scene names;
  the stream fetches it once and decodes it in chunks, see [Music](#music).
- **Stalls.** The core runs in an AudioWorklet, so a stall of the main thread does not stop the
  sound: what plays keeps playing and only new commands wait. A stream keeps more than 1.5 s
  decoded in the worklet, so a stall of 500 ms does not run it short.
- **Decode cost.** The page has no worker threads, so decoding runs on the main thread. A
  `SoundFile` decodes in the frame that loads it (a few ms for a second of FLAC), so load sounds
  with the scene; a stream decodes at most 2 ms per frame, about a sixtieth of a second of music
  once its buffer is full.
- **Rates.** The context runs at 44100 or 48000 Hz, as the browser chooses; sounds and streams
  play at their own rate on either.

Open the dust merchant in the browser with the scene embedded:

```sh
bake3 --target em --local-env=audio apps/boxrun
python3 -m http.server 8080 -d "$(tools/integration.py local-env --env audio)/wasm32-Emscripten/debug/bin"
```

then `http://localhost:8080/boxrun.html#/etc/scenes/dust_merchant.flecs` in Chrome or Safari
(`localhost` is a secure context, which the AudioWorklet requires), and click the canvas.

## Native notes

- macOS and Linux play on the default device through miniaudio, at its rate, on a real time
  audio thread, see [Native output](#native-output).
- No device is not an error: the app logs one warning and renders on the main thread with the null
  transport (`AudioNoDevice`). `--headless` always uses the null transport, `--no-audio` opens
  nothing.
- Unplugging a device does not stop the app: it renders on the main thread and reopens the
  default device every 2 seconds.
- Sound files and music decode on the job system, never on the audio thread.

## Limits of this version

- **Space**: distance and pan only. No Doppler, no distance filter, no occlusion, no HRTF, and
  front and back sound alike.
- **Effects**: `BOX_AUDIO_EFFECT_MAX` (64) effects at once in all channels together, 4 insert
  slots per channel, and six kinds: an algorithmic reverb, an equalizer of 4 bands, a filter, a
  compressor, a gate and a delay; one more effect logs one error and does nothing. No reverb
  zones and no rooms from geometry. A delay is at most 4 seconds (`BOX_AUDIO_DELAY_MAX`), with
  one read position per channel (no multitap), no modulation of its time (no chorus or tape
  wow) and a lowpass in its feedback but no highpass; its sync is a number of beats, not a grid,
  and a tempo ramp is a series of crossfades, one per frame of a change. An equalizer has 4 bands of 12 dB per octave at most (no
  steeper cuts, no linear phase, no dynamic EQ); a filter is one SVF of 12 or 24 dB, with no
  envelope follower or LFO of its own: a script moves its cutoff. The compressor is a single band
  peak compressor without lookahead, RMS detection or a filter on its key; one channel is its
  key, not a sum of several. The gate keys on its own input only.
- **Voices**: 64. A play past that steals the quietest voice of equal or lower priority and is
  refused when every voice ranks higher; one shots, `play_note`, quantized plays and
  `AudioSource` play at 0, the notes and audio clips of a `Track` at its `priority` (1), streams
  at 255. There is no reserved voice count, and a priority does not change within a voice. A one
  shot cannot be controlled after it started.
- **Synth**: up to 8 oscillators, one filter and two LFOs per sound, up to 7 unison voices per
  oscillator, linear or curved envelopes and no node graph; an LFO has fixed destinations (pitch,
  level, cutoff, pulse width) and no LFO modulates another; an oscillator may phase or ring
  modulate another, but FM is not band limited. Plucked strings go down to 40 Hz and 32 of them ring
  at once; a unison pluck takes one per voice, so a chord of 6 notes of 4 voices takes 24; a pitch
  LFO gives them vibrato. Legato is mono per channel; a note of an
  [instrument](#instruments) ends it with its gate, a `box.audio.play` of a legato sound lasts
  its hold after the last note. A looping synth sound starts a new pass of its envelopes and
  oscillators at the end of the last, at most every 60 seconds; a drone keeps that gap short with
  edges of a few ms, as the dust merchant's engine does with 2 ms.
- **Files**: WAV and FLAC for sounds, Ogg Vorbis for music, one stream at once (a change of path
  fades over 5 ms, there is no crossfade).
- **Mixer**: 63 buses besides the master and 4 sends per channel; more buses are silent and
  more sends are ignored, both with one error.
- **Meters**: a peak and an RMS per channel and the gain reduction of an effect, no other
  measure (no LUFS, no true peak between samples, no spectrum). The RMS is over the last 300 ms
  of output, from the frames of the last 64 frames of the app at most, so above 200 frames per
  second its window is shorter.
- **Tools**: no mixer UI, and the UI theme of box has no sounds; a scene adds them in its own
  widgets, as the dust merchant does.
- **Web**: no shared memory, decoding on the main thread, and no load measurement in the
  worklet (it reports 0).
- **Scheduling**: `BOX_AUDIO_SCHEDULE_MAX` (4096) commands wait in the core for their frame at
  once; one more applies at once and reports `BOX_AUDIO_EVENT_LATE`. Only `PLAY`, `STOP`,
  `VOICE_SET`, `BUS_SET`, `RELEASE` and `LEGATO` have a `when`.
- **Instruments**: one sound per instrument at every pitch, so a sampler has no key zones and no
  velocity layers and plays faster for higher notes; a pitch is fixed for the note (no bend or
  aftertouch); a sampler note of a sound that does not loop ends at the end of the sound even
  while its gate holds. `box.audio.play_note` notes have the pan 0.
- **Transport**: one timeline per world, a tempo of 20 to 999 that changes in steps (no ramps),
  time signatures up to 64/64 that only accent the metronome, one loop, and a lookahead of 0.05
  to 0.5 seconds. On native the first beat after a start or a seek plays as soon as the batch
  arrives, a few ms late; the beats after it are exact. The metronome click is fixed.
- **Tracks**: a placement plays its clip at one gain and transpose, without automation, swing
  or quantize. An audio clip does not stretch with the tempo: its transpose changes its pitch and
  speed together, as a sampler does, and a tempo change moves where its passes start and stop, not
  how fast its sound plays. Its passes cut with the 5 ms fade of a `STOP`, so the seam of a loop
  is a short crossfade, not a splice; a pass that a stall skipped does not play. A note is scheduled when its start falls
  in the lookahead window, so a change applies to what is not scheduled yet, up to the lookahead
  later. A looping placement repeats at least every 1/64 beat. Only gated notes are tracked to
  their end: a note of an instrument without a gate plays as authored and a stop does not
  release it once it started. A note whose start the window skipped after a stall longer than
  the lookahead does not play.
- **Quantized plays**: a beat or a bar, no other grid (no eighths, no triplets) and no offset.
  A quantized play of a legato synth sound starts a new voice instead of gliding the one that
  holds. On native a play whose boundary is the clock itself plays when its batch arrives.

## Captures and the audio command

The audio a box app plays can be inspected without listening:

- `audio` prints the state, the rate, the voices, the steals, the refused plays, the load, the clock of the core
  (frames and seconds), the transport and every bus with its kind, width, solo, routing, sends,
  the effects of its insert slots, with whether each runs, and its peak and RMS in dB, the
  instruments and the [tracks](#clips-and-tracks) with their placements and sounding notes;
  `audio volume <v>` and
  `audio mute` set the master, `audio play`, `audio stop`, `audio seek <beats>` and
  `audio tempo <bpm>` drive the [transport](#transport).
- `audio capture <seconds>` records the master to a 16 bit stereo WAV resource of the shell
  session, `box.audio.capture(seconds, path)` to a file from a script, at load if it should
  start with the scene. In the browser `audio capture` writes `/tmp/audio_capture_<n>.wav` in
  the file system of the page.
- Headless with a fixed `--delta-time` the null transport renders the same frames in every run,
  so two runs capture bit identical audio: a render can be compared, measured and listened to.

A capture of the running dust merchant over the REST port:

```sh
boxrun --headless --delta-time 0.016666667 --timeout 120 --port 8000 etc/scenes/dust_merchant.flecs &
S=$(curl -s -X PUT http://127.0.0.1:8000/shell/session | python3 -c 'import json,sys; print(json.load(sys.stdin)["session"])')
curl -s -X PUT --data 'audio capture 20' http://127.0.0.1:8000/shell/session/$S
curl -s -o dust_merchant.wav http://127.0.0.1:8000/shell/session/$S/resource/<name it returned>
```

The resource is empty until the capture is complete. Input commands (`key_press space`,
`mouse_click`) drive the scene meanwhile, so a capture can hear the switches and the reactor.
`box.perf` reports the largest load of a run as `PerfStatus.audio_load`.

The tests that use all of it together are the `all_features` case of
`test/integration/e2e/audio.py` and `test/integration/web/audio_all_features.mjs`, both on
`test/assets/audio/all_features.flecs`. Its timeline is a list of `SetAt` entities, which change
the scene at known frames: they start the loops and give the `Play` and `PlayAt` templates a new
count, whose async block then calls `box.audio.play` or `box.audio.play_at`.

## Reference

The rest of this page describes the parts of audio in box in detail. Box renders its sound in the
audio core, `src/audio/core`: plain C without flecs, box or platform code. The core is a mixer of
voices and buses that renders blocks of float samples into a left and a right channel. It is
driven by a byte stream of commands and reports back with events. Rendering does not allocate,
lock, log or wait. On macOS and Linux the core plays on a miniaudio device, see
[Native output](#native-output); on the web in an AudioWorklet, the platform where audio is the
hardest to get right.

## The core

```c
box_audio_core_t* box_audio_core_new(const box_audio_core_desc_t *desc);
void box_audio_core_free(box_audio_core_t *core);
bool box_audio_core_sound(box_audio_core_t *core, uint32_t id, const float *data,
    int32_t frames, int32_t channels, float sample_rate);
void box_audio_core_apply(box_audio_core_t *core, const void *commands, int32_t size);
void box_audio_core_render(box_audio_core_t *core, float *left, float *right, int32_t frames);
int32_t box_audio_core_read_events(box_audio_core_t *core, box_audio_event_t *events, int32_t max);
uint64_t box_audio_core_clock(const box_audio_core_t *core);
```

`box_audio_core_new` is the only allocation, through the `alloc` and `free` hooks of the desc
(`malloc` and `free` on native when they are NULL). Every other call runs on the thread that
renders, between renders; `render` takes any frame count and mixes in blocks of
`BOX_AUDIO_CORE_BLOCK` (128) frames.

**Commands** are plain bytes without pointers, the same on every transport. Each command starts
with a header of a 16 bit type and a 16 bit size in bytes, header included, followed by 32 bit
fields in the byte order of the target (little endian everywhere box runs): the structs
`box_audio_cmd_*_t` of `core.h`. The `box_audio_write_*` functions write them into a buffer.
A command of an unknown type is skipped by its size and a too short one is ignored, both with
`BOX_AUDIO_EVENT_IGNORED`; a size that runs past the batch ends it. Commands take effect at the
start of the next render block, but those with a `when` in the future (see
[The clock and the schedule](#the-clock-and-the-schedule)). They are:

| Command | Fields |
|---------|--------|
| `PLAY` | when, voice handle, sound, bus, volume, pan, pitch, flags (`BOX_AUDIO_PLAY_LOOP`, `BOX_AUDIO_PLAY_GATE`, `BOX_AUDIO_PLAY_ATTACK`), priority (0..255): the first sample of the voice is at `when` |
| `LEGATO` (12) | when, new voice handle, handle of the voice it follows, then the fields of `PLAY` but `when`: a note of a mono voice at `when`, see [Synth](#synth) |
| `RELEASE` (10) | when, voice handle, fade (seconds): the end of a note at `when`; a synth voice moves every envelope into its release from its level, a sample voice fades out linearly over the fade (0.005..10 s) and ends; a voice that ended is ignored |
| `STOP` | when, voice handle: the voice fades out over the ramp from `when` and ends; one that applies now also drops the scheduled commands of its handle |
| `VOICE_SET` | when, voice handle, `VOLUME`, `PAN`, `PITCH`, `BUS` or `POSITION`, value: the ramp starts at `when` |
| `BUS_SET` | when, bus, `VOLUME`, `PAN`, `MUTE`, `WIDTH` (7), `SEND` (5, the level of send 0) or `SEND_1..3` (8..10, the levels of sends 1 to 3), value: the ramp starts at `when` |
| `ORDER` | count, then per bus in processing order: bus, output, the buses of its 4 sends (or none) and a mask of the pre-fader ones |
| `SOUND_RELEASE` | sound: freed once no voice plays it |
| `STREAM` | voice handle, stream, bus, volume, channels, sample rate, priority: opens a stream and starts a voice that reads it |
| `EFFECT` (11) | slot, kind (`BOX_AUDIO_EFFECT_NONE` takes it off), bus (or none), index (0..3 within the bus), key (a sidechain bus, none for now), 16 params the kind reads: see [Effects](#effects) |
| `SYNTH` | sound, then a patch up to its last layer: makes a free sound id a synth or replaces its patch, see [Synth](#synth) |

The flags of a `PLAY`: `BOX_AUDIO_PLAY_LOOP` (1) loops the sound; `BOX_AUDIO_PLAY_GATE`
(1 << 1) makes a synth voice sustain at the sustain level of its envelopes, their hold ignored,
until a `RELEASE` (an envelope with a sustain of 0 still ends after its decay; sample voices
ignore it); `BOX_AUDIO_PLAY_ATTACK` (1 << 2) starts a sample voice at its gain instead of the
ramp of 5 ms, so a sampled drum keeps its transient (synth voices start at their gain anyway).
A play without the gate and the attack renders as before, bit identical.

The sender picks the voice handles, so it addresses a voice without waiting for a reply. Values
are clamped (volumes and send levels to 0..4, pans to -1..1, widths to 0..2, pitches to
1/64..16) and a NaN
becomes the low end. `POSITION` moves the read position of a voice at once, in frames of its
sound: a looping sound wraps it and one that does not loop clamps it to its end, and a NaN, a
negative or a huge position is 0. Sent in the batch of the `PLAY`, it starts the voice there,
which is how `box.audio` brings a looping spatial source back in time.

**Sound memory belongs to the side that runs the core.** `box_audio_core_sound` gives the core
an id, a pointer in the core's own address space, a frame count, the channels (1, or 2
interleaved) and the sample rate of the sound. The transport fills it: on native the main thread
will allocate the frames and hand them to the render thread, on the web the worklet copies them
into the heap of its core. After a release, `BOX_AUDIO_EVENT_SOUND_FREE` tells the transport
when no voice uses the sound any more, so the side that owns it frees it, never the render path.

**Events** are `{ type, id, value }`: `VOICE_END` (id the handle, for an end, a stop or a
steal, value 0; for a play that the priorities refused, value the refusals so far), `VOICE_STOLEN` (id the handle, value the steals so far), `SOUND_FREE`,
`ORDER_REFUSED`, `IGNORED` (id the command type), `LOST` (value the events lost) and `LATE`
(id the type of a command that found the schedule full and applied at once). The core
holds `BOX_AUDIO_EVENT_MAX` (256) until `read_events`. The last slot is kept for `LOST`: when
the other 255 are full a newer event is dropped and counted in a single `LOST` event in that
slot, whose value grows with each further loss until it is read; reading it resets the count,
which `box_audio_core_events_lost` returns. `SOUND_FREE` is never lost, since the owner frees a
sound's memory only when it sees it. When it finds the ring full, the core marks the sound as
free pending (a state of its slot, so the id stays taken and no memory is needed: a sound is
pending at most once) and `read_events` turns the pending frees into events as soon as the ring
has room, in the same call or a later one. A burst of hundreds of plays at the cap in one batch,
each with a steal and an end, can therefore lose `VOICE_END` and `VOICE_STOLEN` events but not
the free of a sound released after it.

**Streams** are the source of music, `BOX_AUDIO_STREAM_MAX` (2) of them, so a stream can fade
out while the next one starts. Each is a fixed ring of `BOX_AUDIO_STREAM_CHUNKS` (32) chunk
descriptors: `box_audio_core_stream_chunk(core, stream, data, frames, last)` queues a chunk of at
most `BOX_AUDIO_STREAM_CHUNK` (4096) frames, interleaved like a sound, whose memory belongs to
the side that runs the core as a sound's does. `box_audio_core_stream_status` reports the voice
that opened the stream, whether it is still open, whether it ended, the chunks consumed (read to
their end, so the owner frees the oldest ones), the frames read of the next, the underruns and
whether it starves. A closed stream holds no chunk. The render path never waits for a chunk:
when the ring holds fewer frames than a fade of `BOX_AUDIO_STREAM_FADE` (5 ms) takes, the gain
fades to 0 on the frames it still has, the stream counts an underrun and plays silence; when
chunks arrive it fades back in from the frame where it stopped, so neither side clicks. A stream
reads its frames with the linear interpolation of a sample at the rate of its chunks; the voice
ends after the last frame of the chunk marked `last`.

**Voices** are a fixed pool of `BOX_AUDIO_VOICE_MAX` (64) voices, plus 16 slots for voices that
fade out. A voice has a source, a bus, a volume, a pan and a pitch ratio. The source is a sample
buffer, mono or stereo, looped or not, read with linear interpolation, or a synth patch (see
[Synth](#synth)); it is a tagged union in `internal.h` with its functions in `source.c`, so the
music source adds a kind there.
A play when 64 voices play steals the one with the lowest level, the larger of its two gain
targets (the first of equals), of those whose priority is at most its own, with a fade of 2 ms.
This is the voice cap. The priority is a field of `PLAY`, `LEGATO` and `STREAM`, 0..255
(`BOX_AUDIO_PRIORITY_MAX`, a larger value is clamped), and stays with the voice; a legato that
retargets a voice gives it the priority of the note. When every playing voice has a higher
priority the play is refused: the core reports `VOICE_END` for its handle with the refusals so
far as value, so the sender forgets the handle, and `box_audio_core_refused` counts them. A synth
play that needs parts takes them only from fading voices and voices of at most its priority, and
is refused when only higher ones hold them. Plays of priority 0 everywhere, or of any one equal
priority everywhere, render bit identically to the cap without priorities. A priority protects
the music without leaving voices idle while it is quiet, which a reserved voice count would.

**Buses** are a fixed pool of `BOX_AUDIO_BUS_MAX` (64); bus 0 is the master. A bus that is not
in the order costs nothing. Each has a volume, a pan, a width, a mute, an output and
`BOX_AUDIO_SENDS` (4) sends with a level each. A bus renders its block in this order: its
inserts (see [Effects](#effects)), its width, its pre-fader sends, its volume, pan and mute, its
output, then its post-fader sends; the master renders its limiter last. The width is a gain of the side signal: with `mid = (l + r) / 2` and
`side = (l - r) / 2`, the bus plays `mid + width side` on the left and `mid - width side` on the
right, so 0 is mono, 1 passes as is and 2 doubles the side; at 1 the bus skips it, bit
identical. A post-fader send taps the bus after its volume and pan; a pre-fader send taps it
before them, and its gain follows the mute so that a muted bus sends nothing. A send without a
bus does no work. An `ORDER` command replaces the processing order and the routing as a whole,
the pre-fader sends included. The core checks it: the master comes last and outputs and sends
nowhere, every other bus outputs, and sends, only to buses after it, and no bus appears twice.
An invalid order is ignored with `ORDER_REFUSED`, so a cycle cannot hang the render. A bus not in
the order is silent and its ramps wait. The order starts as the master alone.

**Ramps.** A voice and a bus each hold a gain per channel that combines volume, pan, mute and
fades; every change moves it linearly over `BOX_AUDIO_CORE_RAMP` (5 ms), and a voice starts at
0, so nothing clicks, a start or a stop included. A change of the bus of a voice or of the order
is immediate. The pan is equal power with unity at the center: the gains are
`sqrt(2) cos(a)` and `sqrt(2) sin(a)` for `a` from 0 to pi/2, so a centered mono voice plays
its samples as they are. A stereo voice, a stereo sample or a wide synth, takes the same two
gains on its two channels, so its pan, and the pan of a bus, is a balance: at the center both
channels pass as they are, toward a side the far channel fades and the near one rises by up to
3 dB. For two channels that are not correlated, a wide synth, the power stays the same at every
pan; a post-fader send takes both channels of its bus, after its volume and pan.

**The limiter** on the master keeps the peak at `BOX_AUDIO_LIMITER_THRESHOLD` (0.95): its gain,
one for both channels so the image does not shift, drops at once to the threshold over the peak
and rises back linearly over 100 ms. A signal under the threshold passes unchanged, and a final
clamp keeps the output within -1..1.

**Bad input.** Every sample a source reads and every sample a bus outputs that is NaN,
infinite, larger than 1e30 or smaller than 1e-30 becomes 0, so neither a NaN nor a denormal
reaches the output or stays in a bus, and a silent tail costs no more than sound.

**Determinism.** The ramps and the limiter advance per frame and the sums run in a fixed order,
so the same commands at the same block boundaries give bit identical output on the same build,
whether the frames are rendered one at a time or in one large block.

### The clock and the schedule

The core counts the frames it rendered since it was created in 64 bits:
`box_audio_core_clock(core)` returns the count, 0 for a new core. `PLAY`, `STOP`, `VOICE_SET`,
`BUS_SET`, `RELEASE` and `LEGATO` carry a `when` right after their header, the frame of that
clock at which they take effect, as two 32 bit words `when_lo` and `when_hi`, so that every
field stays 32 bits. The writers `box_audio_write_play`, `_stop`, `_voice_set`, `_bus_set`,
`_voice_release` and `_legato_at` take it as their last argument, a `uint64_t`;
`box_audio_write_legato` writes a `LEGATO` for now. `box_audio_write_play_priority` and
`box_audio_write_legato_priority` take a priority before it; the others write priority 0.

- A `when` of 0, or of a frame that was already rendered, applies at the start of the next
  block, as a command without a `when` would.
- A later `when` waits in the schedule of the core, a fixed heap of `BOX_AUDIO_SCHEDULE_MAX`
  (4096) commands ordered by frame; commands for the same frame keep the order in which they
  arrived, but the releases of a frame apply after its other commands, so a legato note on the
  frame where the note it follows ends glides on. The render splits its block at the frame of the next due command, so the command
  takes effect on exactly its frame: the first sample of a play is at `when`, the ramp of a set
  starts at `when` and the fade of a stop starts at `when`. The output stays bit identical
  whether the core renders one frame at a time, blocks of 128 or one large block. The schedule
  does not allocate, and an empty one costs a test of its count per block.
- A command that finds the schedule full applies at once and the core reports
  `BOX_AUDIO_EVENT_LATE` (7), the command type as its id: a late command is better than a lost
  one, since a lost release would leave a note hanging.
- A `STOP` that applies now (a `when` of 0 or in the past) also drops every scheduled `PLAY`,
  `STOP`, `VOICE_SET`, `RELEASE` and `LEGATO` (that would give the voice the handle) of its
  voice handle, so a scheduled play that has not started never
  starts. A `STOP` with a future `when` only stops. This is how a sender cancels what it
  scheduled ahead, on a transport stop or a seek.

`box_audio_core_scheduled(core)` returns the commands that wait. On the main thread, the
`clock` function of a transport tells the frame of the core that renders now, see
[Transports](#transports).

### Meters of the core

Every bus in the order is measured per block at its output, after its inserts, width, pan,
volume and mute, the signal it sends on: the largest absolute sample of each channel and the
sum of the squares of each with the frames they hold. That is a compare and a multiply-add per
sample and channel of a bus in the order, about 5% of `bench/audio` `Mix` with 64 voices
through 8 buses; a bus out of the order costs nothing. The master is
measured after its volume and before the limiter, so its meter reads over 1 while the limiter
reduces the gain, and shows how far the mix is over.

```c
box_audio_meter_t meters[BOX_AUDIO_BUS_MAX];       // peak_left, peak_right, square_left,
box_audio_core_meters(core, meters, BOX_AUDIO_BUS_MAX); // square_right, frames, by bus index
box_audio_effect_meter_t fx[BOX_AUDIO_EFFECT_MAX]; // reduction in dB, by effect slot
box_audio_core_effect_meters(core, fx, BOX_AUDIO_EFFECT_MAX);
```

Each read returns the values since the last read and starts them over; a bus that did not
render reads 0 frames. The per effect values are filled by the kinds that have them with
`box_audio_effect_reduction` (`internal.h`), the largest gain reduction since the last read; the
reverb has none and reads 0. The meters do not change the output, which stays bit identical
whatever the render sizes; the frames and peaks are the same too, the sums round differently.

The tests are `test/cpu/audio`, which render offline; `bench/audio` mixes 64 voices through 8
buses and reports the time per frame.

## Effects

`src/audio/core/effect.c` holds a pool of `BOX_AUDIO_EFFECT_MAX` (64) effect instances, which
the `EFFECT` command puts into the insert slots of the buses. An effect is addressed by its slot
of the pool; the command gives it a kind, a bus, an index within the bus and the parameters of
its kind.

- **Slots and order.** Each bus has `BOX_AUDIO_EFFECT_SLOTS` (4) insert slots. The effects of a
  bus run in the order of their index on its block, in place, before its width, pan and volume:
  they are pre-fader inserts, so a pre-fader send hears them too. The inserts of the master run
  before the limiter. An index without an effect is skipped; two effects on the same index run
  in the order of their slots.
- **Kinds.** A kind is one file of `src/audio/core` and one row of the table of `effect.c`:
  `floats` (the memory it needs at a rate), `init` (constants of a new core), `start` (on clear
  memory at its parameters), `command` (new parameters while it runs), `render` (a block in
  place) and `rest` (a block it skips). The kinds are the reverb, `BOX_AUDIO_EFFECT_REVERB`,
  the [equalizer](#equalizer), `BOX_AUDIO_EFFECT_EQ`, the [filter](#filter-effect),
  `BOX_AUDIO_EFFECT_FILTER`, the [compressor](#compressor), `BOX_AUDIO_EFFECT_COMPRESSOR`, the
  [gate](#gate), `BOX_AUDIO_EFFECT_GATE`, and the [delay](#delay), `BOX_AUDIO_EFFECT_DELAY`; the
  parameters of a command are 16 floats (`BOX_AUDIO_EFFECT_PARAMS`) that each kind reads in its
  own way, the reverb its first five (`box_audio_reverb_params_t`), the equalizer all 16 (4
  `box_audio_eq_band_t`), the filter its first five (`box_audio_filter_params_t`), the
  compressor its first seven (`box_audio_compressor_params_t`), the gate its first five
  (`box_audio_gate_params_t`) and the delay its first five (`box_audio_delay_params_t`). The
  core clamps them, a NaN to the low end.
- **Key.** The `key` of the command is a sidechain bus, `BOX_AUDIO_BUS_NONE` for none; only the
  compressor reads it. Since the inserts of a bus run in the order of the slots, the order of a
  compressor and an equalizer matters: a compressor before a boost of 12 dB hears the signal
  without it.
- **Changes.** A change of the parameters is smoothed by the kind. A change of kind, bus or index
  blends the effect out of its bus over 50 ms, so the bus passes its dry signal, applies the new
  place on clear memory and blends it into the new one over 50 ms (`BOX_AUDIO_EFFECT_FADE`,
  100 ms in all); a reverb also fades its input into its clear lines over the second half.
  An effect put on a bus from off, such as an enabled one or one put live into a slot, starts
  at its first block: it blends in from the dry signal over 50 ms the same way, unless that
  block is silent, when it starts at once since its output would be silent either way.
- **Taking it off.** Kind `BOX_AUDIO_EFFECT_NONE` (or bus `BOX_AUDIO_BUS_NONE`) takes the effect
  off: on a bus that stays in the order it blends out to the dry signal over 50 ms, so its tail
  fades out where it was; when its bus left the order, as a deleted bus does in the same batch,
  its tail gets no more input and fades out into the master over 50 ms instead, so deleting a
  reverb bus does not cut it.
- **Memory.** The memory of an effect belongs to the side that runs the core, like sound memory:
  `box_audio_effect_floats(kind, rate)` floats, 0 for a kind that needs none (about 200 KB for
  a reverb and 1.5 MB for a delay at 48 kHz). `box_audio_core_effect_memory(core, slot, data, floats)` gives a slot its
  memory once, or larger memory for a kind that needs more, on which the effect starts over; the
  render path never allocates. An effect whose kind needs memory runs once its slot has enough,
  another at once. The transports allocate it on the `effect_memory` call of the transport
  interface: the null transport on the main thread, the native one on the main thread and
  hands it to the audio thread as a message of the command ring, the web one with an
  `{ type: 'effect', slot, floats }` message, on which the worklet allocates it in the heap of
  its core. Each frees it after its core, the memory a slot outgrew included.
- **Tails.** A kind with a tail follows the silence rule: once its input has been silent long
  enough and its state has decayed under -140 dB, it clears itself and stops; while it is stopped
  and no signal comes in, a block costs a scan of its input for a sample over 1e-9.
  `box_audio_core_effect_running` tells whether an effect runs.
- **Cost.** A bus without effects pays one check of a mask per block and renders bit identical
  to a core without effects; an effect that does not move costs its kind, and one that blends
  copies its input once per block.

The tests are the `Effect` suite of `test/cpu/audio` (several reverbs at once, an empty slot,
the fades of a removal, a move and a disabled effect, the pool of 64, a channel without effects,
an entity in two slots), the `Equalizer`, `EffectFilter`, `Dynamics` and `Delay` suites and the
`effects`, `eq_filter`, `dynamics` and `delay` cases of `NoAlloc`.

## Reverb

`src/audio/core/reverb.c` is the reverb kind of effect, an algorithmic reverb: a feedback delay
network of `BOX_AUDIO_REVERB_LINES` (8) delay lines. A return bus with a reverb in its insert
slot is the usual setup, so sources reach it through a send of their bus.

- **The network.** The input, the mean of both channels, goes through a predelay line, then into
  every line with a sign pattern that is no row of the matrix, so the first reflection already
  spreads over all lines. The lengths are distinct primes, so mutually prime, from 39 to 98 ms
  at size 1, scaled down to a fifth at size 0. Each line has a one pole lowpass, the damping,
  and a gain `10^(-3 L / (decay x rate))`, so every line loses 60 dB in `decay` seconds whatever
  its length `L`. The feedback matrix is the 8 x 8 Hadamard matrix over `sqrt(8)`, orthogonal.
  The left and the right outputs take the lines with two orthogonal sign patterns, which keeps
  the channels decorrelated.
- **It cannot blow up.** The matrix keeps the energy, the lowpass has a gain of at most 1 and
  each gain is at most `0.9999`, so the loop gain stays under unity. Its parameters are
  clamped: decay to `BOX_AUDIO_REVERB_DECAY_MIN..MAX` (0.05..60 s), damping, size and mix to
  0..1 and predelay to 0..`BOX_AUDIO_REVERB_PREDELAY_MAX` (0.5 s), a NaN to the low end. Every
  value a line keeps is flushed like a bus sample, so no denormal forms in a tail.
- **Mix.** The bus outputs its signal at `1 - mix` and the reverb at `mix`; 1, the default, is
  the fully wet send bus.
- **Changes.** Decay and damping move to a new value over about 50 ms, a block at a time, and
  mix ramps over 20 ms. A change of size or predelay would change the lengths of the lines,
  which clicks: the wet signal fades out over 50 ms, the lines are cleared and take their new
  lengths, and it fades back in over 50 ms (`BOX_AUDIO_REVERB_FADE`, 100 ms in all), while the
  input fades into the clear lines over the same 50 ms, so a steady sound does not enter them
  with a step. A change of the bus or the slot blends it out and in, see [Effects](#effects).
- **Silence costs nothing.** The tail rings out after its sources stop. Once the input has been
  silent for longer than the predelay plus the longest line, and every line has read out under
  -140 dB for a whole length of the longest line, so nothing louder is left in them, the reverb
  clears its lines and stops; while it is stopped and no signal comes in, a block costs a scan
  of the input for a sample over 1e-9. `box_audio_core_effect_running` tells whether it runs.
- **Memory.** `box_audio_effect_floats(BOX_AUDIO_EFFECT_REVERB, rate)` floats per slot (about
  200 KB at 48 kHz), sized for the largest size and predelay at the rate. Until its slot has
  memory a reverb does not run and its bus stays dry.

Every reverb runs with its own memory, up to the 64 effects of the pool; more only cost their
memory and their time. Convolution is not used, since it needs recorded impulse responses.

The tests are the `Reverb` suite of `test/cpu/audio`, from an impulse in an offline render:
the decay time at 44.1 and 48 kHz, damping, predelay, stereo correlation, density, stability at
extreme values, silence, changes without clicks, sends, a second reverb that runs, a delete and
no allocation. `bench/audio` has a `Reverb` suite in ns per frame, and
`test/integration/web/audio_reverb.mjs` measures a tail and the load of the worklet in Chrome.

## Equalizer

`src/audio/core/eq.c` is the equalizer kind of effect, `BOX_AUDIO_EFFECT_EQ`: 4 bands
(`BOX_AUDIO_EQ_BANDS`) in a row, each an RBJ cookbook biquad in transposed direct form II, one
per channel. Its parameters are the 16 floats of the command, 4 `box_audio_eq_band_t` of `kind`,
`frequency`, `gain` and `q`; the component is `Equalizer`, whose `bands` are 4 `AudioEqBand`.

- **Bands.** `AudioEqOff` (the default) passes its input. `AudioEqPeak` boosts or cuts `gain`
  dB around `frequency`; `q` sets its width, so the frequencies where it has half its gain in dB
  are `f / w` and `f w` with `w - 1/w = 1/q` (0.7071 is about two octaves wide, 4 about a third
  of an octave), and its gain at the center is `gain`. `AudioEqLowShelf` and `AudioEqHighShelf`
  apply `gain` under or above their corner `frequency`, half of it at the corner; a `q` over
  0.7071 makes the shelf overshoot near its corner. `AudioEqLowCut` and `AudioEqHighCut` are a
  highpass and a lowpass of 12 dB per octave: at a `q` of 0.7071, the default, they are -3 dB at
  `frequency` and -12 dB an octave past it, and a higher `q` gives a resonant bump at the corner.
  The cuts ignore `gain`.
- **Ranges.** `frequency` is clamped to 10 Hz..0.49 of the rate, `gain` to -24..24 dB and `q`
  to 0.1..18, a NaN to the low end; an unknown kind is off.
- **Changes.** A change of a band moves its coefficients linearly to the new ones over 20 ms
  (`BOX_AUDIO_EQ_RAMP`), frame by frame, so a script may automate a band every frame without
  zipper noise or clicks. A biquad is stable when its `a1` and `a2` lie in the triangle
  `|a2| < 1`, `|a1| < 1 + a2`, which is convex, so every set of coefficients on the way between
  two stable ones is stable too.
- **Flat costs nothing.** A band that is off, or a peak or a shelf at 0 dB, has the
  coefficients of a wire, and once its state drained (two frames) it is skipped: an equalizer
  whose bands are all off or flat renders bit identical to none, and each band that does work
  costs about 5 multiplies per sample and channel. It has no tail and needs no memory.

A low cut on a vocal bus, which removes the rumble under 120 Hz, with a small presence peak:

```
vocals {
  AudioBus: {output: master, effects: [vocals]}
  Equalizer: {bands: [
    {kind: AudioEqLowCut, frequency: 120},
    {kind: AudioEqPeak, frequency: 3000, gain: 3, q: 1.2}
  ]}
}
```

The bands a script does not give keep their defaults: off, at 100, 400, 2000 and 8000 Hz, 0 dB
and a `q` of 0.7071. On an SFX bus the same low cut keeps footsteps and impacts from muddying
the music: `sfx { AudioBus: {kind: AudioChannelSfx, effects: [sfx]}; Equalizer: {bands:
[{kind: AudioEqLowCut, frequency: 80}]} }`.

The tests are the `Equalizer` suite of `test/cpu/audio`, which measures sines in offline
renders: the gain of a peak at its center within 0.1 dB and its half gain frequencies for 3
`q`, the shelves well past their corner, the cuts at their corner and an octave past it, two
bands in a row, stability at extreme values and at Nyquist, a step of gain and automation every
frame without a click, flat bands and a band back to flat bit identical to no equalizer, render
sizes and the component in a world; and the `eq_filter` cases of `NoAlloc` and `Script`.

## Filter effect

`src/audio/core/filter.c` is the filter kind of effect, `BOX_AUDIO_EFFECT_FILTER`: the
trapezoidal state variable filter of the [synth](#synth) on both channels of a bus. Both share
it through `src/audio/core/svf.h`, so the stability argument holds for both: each stage is
stable at any cutoff and resonance and while they move, and the 24 dB filter is the synth's
cascade of two stages, each stable on its own. Its parameters are a
`box_audio_filter_params_t`; the component is `AudioFilter`.

- **Kinds.** `kind` is a `SynthFilterKind`: `SynthLowPass` (the default), `SynthHighPass`,
  `SynthBandPass`, unity at the cutoff, and `SynthNotch`, silent at the cutoff. `slope` is
  `SynthSlope12` or `SynthSlope24`: at resonance 0 the filter is a Butterworth one, -3 dB at its
  cutoff and -12 or -24 dB an octave past it, and at `SynthSlope24` the resonance applies to the
  second stage only, as in the synth.
- **Ranges.** `cutoff` is clamped to 10 Hz..0.49 of the rate, `resonance` and `mix` to 0..1, a
  NaN to the low end, and `kind` to the four kinds.
- **Mix.** The bus outputs its signal at `1 - mix` and the filter at `mix`; 1 is the default.
  A mix of 0 passes the input bit for bit.
- **Changes.** The cutoff moves exponentially in frequency toward a new value, and the
  resonance toward its own, about 63% of the way in 10 ms (`BOX_AUDIO_FILTER_SMOOTH`), with new
  coefficients every 8 frames as the filter envelope of the synth does, so a sweep driven from a
  script is smooth; the mix ramps over 20 ms. A change of the kind or the slope fades the
  filtered signal out to the dry one over 20 ms, swaps, and fades it back in over 20 ms.
- **Cost.** About 10 multiplies per sample and channel at 12 dB, twice that at 24, plus the
  coefficients every 8 frames while the cutoff moves. It has no tail and needs no memory.

A filter sweep on a music group: the music bus closes to 400 Hz when the player is underwater,
and a system moves its cutoff every frame:

```
music {
  AudioBus: {output: master, effects: [music]}
  AudioFilter: {cutoff: 400, resonance: 0.3, slope: SynthSlope24}
}
```

```c
void Underwater(ecs_iter_t *it) {
    AudioFilter *f = ecs_get_mut(it->world, music, AudioFilter);
    float target = player_underwater ? 400.0f : 20000.0f;
    f->cutoff += (target - f->cutoff) * 0.1f;  // a slow sweep; the core smooths each step
    ecs_modified(it->world, music, AudioFilter);
}
```

A `SynthHighPass` at 300 Hz with `SynthSlope24` and a `SynthLowPass` at 3 kHz in two slots of
one bus make a telephone voice.

The tests are the `EffectFilter` suite of `test/cpu/audio`, which measures sines in offline
renders: the lowpass and the highpass at both slopes at their cutoff and an octave past it, the
bandpass, the depth of the notch, stability at extreme values and at Nyquist, a step of the
cutoff, a sweep every frame and a change of kind without a click, the mix, render sizes and the
component in a world; and the `eq_filter` cases of `NoAlloc` and `Script`.

## Compressor

`src/audio/core/dynamics.c` holds the dynamics kinds of effect. The compressor,
`BOX_AUDIO_EFFECT_COMPRESSOR`, is feed-forward and stereo linked: one gain for both channels.
Its parameters are a `box_audio_compressor_params_t`; the component is `Compressor`.

- **Detector.** It reads the peak of the louder channel, held over the current and the last
  window of 5 ms (`BOX_AUDIO_DYNAMICS_WINDOW`), so a sine of 100 Hz or more reads its peak
  without ripple; a drop of the input reaches the detector 5 to 10 ms later. Silence reads -120
  dB (`BOX_AUDIO_DYNAMICS_FLOOR`).
- **Ballistics.** The detected level is smoothed in dB: it moves 63% of the way up in `attack`
  seconds and 63% of the way down in `release` seconds, exponentially. It counts from the start
  of the knee up, where the reduction begins, so after a loud key that falls silent the
  reduction goes 63% of the way back to none in `release`, not in a few milliseconds toward
  the silence floor.
- **Gain computer.** Over the threshold `T` by `x` dB the level is reduced to `T + x / ratio`;
  with a `knee` of `W` dB the curve bends smoothly from `T - W/2` to `T + W/2` (a quadratic,
  `(1/ratio - 1) (x + W/2)^2 / 2W`), a hard knee at 0. A -10 dBFS sine at a threshold of -20,
  ratio 4 and no knee comes out at -17.5 dB. Then `makeup` dB are added and the output is
  `mix` of the compressed signal and `1 - mix` of the input, for parallel compression.
- **Ranges.** `threshold` -80..0 dB, `ratio` 1..20, `attack` 0.0001..1 s, `release` 0.001..5 s,
  `knee` 0..24 dB, `makeup` 0..24 dB and `mix` 0..1, a NaN to the low end. The defaults of the
  component are -18 dB, 4, 0.01, 0.15, 6 dB, 0 and 1.
- **Changes.** The threshold, ratio, knee, makeup and mix ramp over 20 ms
  (`BOX_AUDIO_COMPRESSOR_RAMP`); the attack and release apply at once.
- **Sidechain.** With a key bus the detector reads the output of that bus in the same block,
  after its inserts, volume and pan, instead of the input of the compressor. The key bus must
  run first: the core keys on the own input when the key comes after the bus of the compressor
  in the order, is the bus itself or is not in the order, as a deleted bus is not. The module
  adds an edge from the key to the compressed channel when it compiles the order, so the key
  runs first (see [Mixer channels](#mixer-channels)).
- **Reduction.** It reports its largest gain reduction in each block, without the makeup,
  through the meters of the effects, so an `AudioMeter` on its entity reads it.
- **Cost.** A logarithm and a power of 2 of a few multiplies each per frame, for both channels
  together. It has no tail and needs no memory.

Ducking: the music group ducks under the SFX bus, so an explosion pushes the music down by up
to about 15 dB and the music comes back over 300 ms after it:

```
sfx { AudioBus: {output: master, kind: AudioChannelSfx} }
music_duck {
  Compressor: {threshold: -30, ratio: 8, attack: 0.005, release: 0.3, sidechain: sfx}
  AudioMeter: {}
}
music {
  AudioBus: {output: master, effects: [music_duck]}
}
```

A script resolves a name when it reads it, so `sfx` and `music_duck` come before the entities
that name them. `music_duck` is an entity of its own, so its `AudioMeter` reads the reduction of the
compressor (`reduction`, in dB) for a ducking UI, while an `AudioMeter` on `music` reads its
levels.

## Gate

The gate, `BOX_AUDIO_EFFECT_GATE` in `src/audio/core/dynamics.c`, silences a channel under its
threshold, such as the noise of a dialog bus between lines. Its parameters are a
`box_audio_gate_params_t`; the component is `Gate`.

- **Open and closed.** It reads the peak of the louder channel with the detector of the
  compressor. It opens when the peak reaches `threshold`, and stays open while the peak stays
  over the threshold less 4 dB (`BOX_AUDIO_GATE_HYSTERESIS`) and for `hold` seconds after, so
  a signal that wanders around its threshold does not open and close it over and over. Then it
  closes.
- **Gain.** Its linear gain moves to 1 63% of the way in `attack` seconds when it opens, and to
  `range` dB 63% of the way in `release` seconds when it closes; a range of 0 never reduces.
- **Ranges.** `threshold` -100..0 dB, `attack` 0.0001..1 s, `hold` 0..5 s, `release`
  0.001..5 s and `range` -100..0 dB, a NaN to the low end. The defaults of the component are
  -50 dB, 0.001, 0.05, 0.1 and -80 dB.
- **Reduction.** It reports its largest gain reduction in each block, like the compressor.
- **Cost.** A comparison and a multiply per frame, no logarithm. It has no tail and needs no
  memory.

```
dialog_gate { Gate: {threshold: -45, hold: 0.08} }
dialog { AudioBus: {output: master, effects: [dialog_gate]} }
```

The tests are the `Dynamics` suite of `test/cpu/audio`: the static curve, the knee, the attack
and release times, a key burst that ducks a music bus and its release, a key after the bus and
a key that left the order, the gate closing after its hold and release and opening on its
attack, a signal at the threshold of the gate without chatter, the reduction, the order of a
compressor and an equalizer in the slots, render sizes, the components, ducking in a world, a
sidechain cycle and `test/assets/audio/dynamics.flecs`; and the `dynamics` case of `NoAlloc`.

## Delay

`src/audio/core/delay.c` is the delay kind of effect, `BOX_AUDIO_EFFECT_DELAY`: a line per
channel on the memory of its slot, read `time` seconds back. Its parameters are a
`box_audio_delay_params_t` of `time`, `feedback`, `damping`, `ping_pong` and `mix`; the component
is `Delay`.

- **Echo.** The output of the line is the echo; the input plus `feedback` times the echo goes
  back into the line, so each repeat is `feedback` times the last. A time of `time * rate`
  frames that is not whole is read with linear interpolation between the two frames around it,
  so an impulse echoes exactly `time * rate` frames later.
- **Damping.** A one pole lowpass at `damping` Hz sits in the feedback path, so every repeat is
  darker than the last while the sum of its samples is still `feedback` times the last; the
  first echo is not filtered. At 0.49 of the rate or above it is off.
- **Ping pong.** With `ping_pong` the input, summed to mono, enters the left line, and the
  feedback of each channel enters the other: the first echo is on the left, the second on the
  right, and so on. A change of `ping_pong` blends the two routings over 20 ms.
- **Ranges.** `time` 0..`BOX_AUDIO_DELAY_MAX` (4 s, which sizes the memory) and at least a
  frame, `feedback` 0..0.95 (`BOX_AUDIO_DELAY_FEEDBACK_MAX`), `damping` from 20 Hz, `mix` 0..1
  and `ping_pong` 0 or 1, a NaN to the low end. The bus outputs its signal at `1 - mix` and the
  echoes at `mix`, so at a mix of 0 it passes its input bit for bit. The defaults of the
  component are 0.25 s, 0.35, a mix of 0.3, no ping pong and 6000 Hz.
- **Time changes.** A new time does not move the read position, which would glide the pitch:
  the delay reads at the old and the new position at once and crossfades from the one to the
  other over 50 ms (`BOX_AUDIO_DELAY_CROSSFADE`), so nothing clicks. A time that arrives while a
  crossfade runs waits for it, and the next crossfade goes to the last time that arrived. The
  feedback, the mix and the ping pong ramp over 20 ms (`BOX_AUDIO_DELAY_RAMP`), the damping
  applies at once.
- **Tempo sync.** `sync`, in beats of the `Transport` (a quarter note), wins over `time` when it
  is positive: the module sends `sync * 60 / tempo` seconds at the tempo of the `Transport`, 120
  without one, clamped to 4 s. 0.25 is a sixteenth, 0.5 an eighth, 0.75 a dotted eighth and 1/3
  an eighth triplet. A change of the tempo changes the time the module sends, so the echo moves
  with the crossfade of any time change, on the next frame.
- **Silence costs nothing.** The delay has a tail and follows the silence rule of the effects:
  once its input has been silent for the time and its echoes have read under -140 dB for the
  time, so nothing louder is left in the line, it stops; while it is stopped and no signal comes
  in, a block costs a scan of its input. It never clears its memory: it counts the frames it
  wrote since its line was last silent and reads the older ones as silence, so neither a start
  nor a stop costs more than a few stores.
- **Memory.** `box_audio_effect_floats(BOX_AUDIO_EFFECT_DELAY, rate)` floats per slot, 4 seconds
  and two frames of both channels (about 1.5 MB at 48 kHz).
- **Cost.** One or, while a crossfade runs, two interpolated reads per channel, the lowpass and
  a write per frame.

A synced dotted eighth on a return, which the world bus reaches through a send:

```
using box.audio

$ { Transport: {tempo: 100} }
echo {
  AudioBus: {output: master, kind: AudioChannelReturn, effects: [echo]}
  Delay: {sync: 0.75, feedback: 0.4, mix: 1, damping: 4000, ping_pong: true}
}
world { AudioBus: {output: master, sends: [{bus: echo, level: 0.3}]} }
```

At 100 bpm its echo comes 0.45 s after the sound, and a change of the tempo moves it. On a
return the delay is fully wet (`mix: 1`), and the send sets how much of the bus it hears; as an
insert on the bus itself, a mix of 0.2 to 0.4 leaves the dry signal in front.

The tests are the `Delay` suite of `test/cpu/audio`, from an impulse in an offline render: the
echo time, feedback and damping, ping pong, tempo sync and tempo changes without clicks, the
clamp, silence, a dry mix and bit identical renders of any size; `NoAlloc` covers it as well.
`bench/audio` has a `Delay` suite in ns per frame.

The tests are the `Delay` suite of `test/cpu/audio`: the frame of the echo, whole and
fractional, the repeats at the feedback and darker with damping, ping pong, a synced eighth at
120 bpm, a tempo change of the `Transport` that moves the echo without a click, a time change
that crossfades without a glide, the clamp of the time, a tail that stops when silent, a mix of
0 that is dry, render sizes, the component and `test/assets/audio/delay.flecs`; and the
`delay` case of `NoAlloc`.

## Synth

`src/audio/core/synth.c` renders procedural sounds live, a new source kind of the voices, so
synth voices share the pool, the cap, the buses, the ramps and the limiter. A sound id becomes a
synth with a `SYNTH` command, whose patch (`box_audio_synth_patch_t` of `core.h`) is plain
values in seconds and Hz, the same at every rate: a fixed topology of up to
`BOX_AUDIO_SYNTH_LAYERS` (8) oscillators, each with an envelope, summed into one filter and an
amp; an oscillator may modulate an earlier one instead of being heard, and up to
`BOX_AUDIO_SYNTH_LFOS` (2) LFOs move the pitch, the level, the cutoff and the pulse width. There
is no node graph.
The core keeps up to `BOX_AUDIO_SYNTH_MAX` (256) patches; a play copies the patch into its
voice, so a play allocates nothing, a new patch for the same id reaches the next play while the
voices that play keep theirs, and a release frees the id at once with `SOUND_FREE`.

A patch costs only the layers it has. The `SYNTH` command ends after its last layer,
`BOX_AUDIO_CMD_SYNTH_SIZE(layers)` bytes: 196 bytes and 104 per layer, 404 for 2 layers, 1028 for 8;
a shorter one than its layers need is ignored. The LFOs come before the layers, so every command
carries them. A voice holds the patch without its layers and
takes, for each layer, a part from a pool the core allocates with itself: the layer, its oscillator,
its own envelope and the state of its unison voices, 512 bytes. The pool has 4 parts per voice slot
(320), so patches of up to 4 layers never run out of them and a voice of 8 takes the room of two of
4. A play that finds too few free parts takes them like one at the voice cap takes a voice: the core
ends the quietest synth voices that fade out, then the quietest that play, which it reports as
`VOICE_STOLEN` and counts as steals, until the parts are free, and only then starts the play. It
first checks that the play can start at all, so a play that is ignored fades or ends no voice. A
voice renders and advances only its parts and gives them back when it ends. The 80 voice slots
(808 bytes each) and the pool take 223 KB. The pluck strings are a second pool of
`BOX_AUDIO_SYNTH_STRINGS` (32), each with a delay line of `BOX_AUDIO_SYNTH_STRING_FRAMES` (2048)
floats, 8 KB, 259 KB in all; a voice takes one for each unison voice of each pluck layer, and a
play that finds too few free is ignored, without fading or ending a voice. A pluck layer still
takes one part: its strings hang off it in a chain, so a part does not grow for the layers that
are not plucks.
The 256 patches the core keeps are full size, 1020 bytes each. The core clamps every value of a
patch, a NaN to the low
end.

- **Oscillators.** Sine (`box_audio_core_sin`), saw, square, triangle and white noise. Saw and
  square are band limited with polyBLEP, the corners of the triangle with polyBLAMP. Frequencies are clamped to 20 kHz and to 0.48 of the
  rate, so the top of the audible range plays at 44.1 and 48 kHz. A sweep moves the frequency
  exponentially to its end over its time, and lands on it exactly. The pitch of the voice, the
  play's and a `VOICE_SET`, multiplies every frequency but noise.
- **Pulse width.** A square has a `pulse_width`, the share of its period it is high,
  `BOX_AUDIO_SYNTH_PULSE_MIN` (0.01) to 0.99; 0 in a patch is the default of 0.5. A width other
  than 0.5 renders as a pulse: a step up at phase 0 and one down at the width, each with its
  polyBLEP, so it is band limited at every width, and without its DC (`2 w - 1`), so a moving
  width does not thump. The width keeps a frame from either end, so both steps never fall in one
  frame. A width of exactly 0.5 takes the square as before, bit identical.
- **LFOs.** An LFO of a patch (`box_audio_synth_lfo_t`) has a wave (sine, triangle, square,
  rising saw and sample and hold of random values), a `rate` (0..100 Hz), a `delay` and a
  `fade` (s), a start `phase` in turns or a `random_phase`, a `layer` (1 + the layer it moves, 0
  for all) and a depth per destination: `pitch` in cents (up to 2400 either way), `amplitude`
  (0..1), `cutoff` in octaves (up to 10) and `pulse_width` (a share of the period, up to 0.49).
  Every wave starts at 0 rising at phase 0, the square at its top. It is at depth 0 during its
  delay, then its phase runs from its start and its depth rises linearly over the fade (at
  least 5 ms after a delay, so it does not jump in). The
  pitch is multiplied by `2^(cents x v / 1200)` for the value `v` of the wave times the fade
  (the sum of both LFOs when both move it); the level dips from 1 at the top of the wave to
  `1 - amplitude` at its bottom, `1 - amplitude x fade x (1 - v) / 2`, so an LFO in its delay
  leaves it alone and the two LFOs multiply; the cutoff is the cutoff of the filter envelope
  times `2^(octaves x v)`; the pulse width of a square is its own plus `pulse_width x v`,
  clamped. The cutoff is of the patch, whichever layer the LFO names; the level of an LFO of
  every layer is a gain after the filter, that of a layer a gain of the layer. Random phases and
  the random wave use a generator of their own, seeded from the seed of the patch and the count
  of plays apart from the noise, so a play of a fixed seed and count is the same every time and
  the noise and variances stay as without LFOs. An LFO restarts with the envelopes of a looping
  voice and goes on over a legato retarget.
- **Smoothness of the LFOs.** The LFOs run per control block of 8 frames: each block advances
  their phases and computes the aim of each destination at the end of the block, one
  `box_audio_core_sin` per sine LFO and one `exp2` of the series per oscillator whose pitch
  moves; per frame the pitch factor, the level and the width of each oscillator step linearly
  toward that aim, so a slow LFO moves them in steps far under a cent and a thousandth of a
  level, without zipper noise. The cutoff takes new coefficients per block, as under the filter
  envelope. The waves that jump, square, saw and random, move to their new value through a one
  pole smoother of 2 ms at the control rate, so they do not click. The oscillators take the
  vibrato into their turns per frame, under the top frequency, the unison voices of a layer
  with their detune; a pluck string tunes itself to it once per control block while it moves,
  as in a glide.
- **Modulation.** A layer whose `modulates` is 1 + an earlier layer is a modulator: its wave
  times its own envelope, its signal, changes that carrier and is not heard unless `audible`.
  `BOX_AUDIO_SYNTH_PHASE` adds the signal times `index` (radians, at most
  `BOX_AUDIO_SYNTH_INDEX_MAX`, 32) to the phase of the carrier, phase modulation as DX synths
  do their FM, so the index follows the envelope of the modulator and the pitch of the carrier
  never drifts; a sine carrier at fc with a sine at fm makes sidebands at fc ± k fm with
  amplitudes J_k(index). `BOX_AUDIO_SYNTH_RING` multiplies the carrier by `1 - index + index x
  signal` (`index` a depth of 0..1; at 1 two sines give their sum and difference alone). Several
  modulators of one carrier add their phases and multiply their rings; a modulator may have
  modulators of its own, after it. `feedback` (radians, at most `BOX_AUDIO_SYNTH_FEEDBACK_MAX`,
  1.5) adds the mean of the last two outputs of an oscillator to its own phase, which turns a
  sine sawlike; it works on carriers and modulators alike. The layers of a modulated patch
  render from the last, so every modulator is ready before its carrier; a patch in which no
  layer modulates or feeds back takes the plain loop and renders as before, at no cost.
- **Aliasing of FM.** Phase modulation is not band limited: its sidebands go on past Nyquist and
  fold back as inharmonic aliases. The core limits each index at the start of a pass and at a
  change of pitch so that, by Carson's rule, the carrier plus (index + 1) times the modulator
  stays under the top frequency of the oscillators (20 kHz, or 0.48 of the rate), the higher
  ends of their sweeps counted: a 1 kHz pair at 48 kHz keeps up to 18, a 7 kHz pair 0.86. The
  sidebands past that rule are weak but not 0, so a high index on a high carrier still aliases
  a little; a modulator that is itself modulated, feedback and several modulators of one
  carrier widen the spectrum past what the limit counts. Saw, square and triangle carriers
  smooth their edges, the polyBLEP and polyBLAMP, with the speed their modulated phase moves at
  in each frame, how far it moved since the last one, wrapped so that a phase running backwards
  counts as a negative speed, and under the top; so FM on them is about as clean as the plain
  waves. Unison layers keep the speed of their unmodulated rate.
- **Noise** is xorshift32, seeded per play from the seed of the patch and the count of its
  plays, so a fixed run gives bit identical output and every play differs. The same generator
  picks the pitch and level variances of the play, and plucks the strings.
- **Pluck strings.** A layer of the wave `BOX_AUDIO_SYNTH_PLUCK` is a Karplus-Strong string
  instead of an oscillator, one for each of its unison voices. At the start of a pass each string
  takes its own burst of the noise of the play, one after the other, so a fixed run is bit
  identical and the strings of a unison do not sound alike. The burst is one period long,
  through a one pole lowpass of its `hardness` (0 a soft finger, 1 a pick) and one of its
  `brightness` (a coefficient of 0.1 at 0 to 1, no filter, at 1), without its mean so it carries
  no DC, scaled to a peak of 1, into its delay line. Per frame the frame out of the line goes through a lowpass `(b, 1 - 2b, b)` with `b` from 0.25 at
  `brightness` 0 to 0.01 at 1, a first order allpass and the gain of the loop back into it; the
  frame out of the line is the sample. The lowpass is symmetric, a delay of exactly 1 frame at
  every frequency, so a period of n frames is a line of `d = floor(n - 1.5)` frames, the lowpass
  and an allpass of `f = n - 1 - d`, 0.5 to 1.5 frames, whose coefficient
  `c = sin(w (1 - f) / 2) / sin(w (1 + f) / 2)` gives it a phase delay of exactly f at the
  fundamental w. The pitch is exact to a small share of a cent at any note, where a line of whole
  frames is up to 70 cents off at 2 kHz. The gain of the loop is `10^(-3 n / (decay rate))`
  over the gain of the lowpass at the fundamental, so the fundamental falls 60 dB over `decay`
  seconds (0.01 to 60) and the harmonics faster, the more so the darker the string; it is at most
  0.99999, so every string decays, and a high, bright, long string rings shorter than its
  `decay`. The string follows the frequency of its layer: the pitch of the voice, sweeps, glides
  and legato notes and the vibrato of an LFO retune it while it moves, without a new pluck: every
  frame cheaply (the period from the last one by a series, the sines of the allpass coefficient
  from two terms of their series, within a few millionths, and one division) and exactly, with
  the gain, every `BOX_AUDIO_SYNTH_STRING_RETUNE`, 8, frames, the control block of the LFOs, and
  every frame under a period of 16 frames; a steady pitch never retunes. When a retune moves the
  delay line by whole frames,
  which a vibrato does all the time, the allpass fraction jumps from one end of its range to the
  other and the read point by a frame: the old read point and its allpass play on and crossfade
  into the new one over `BOX_AUDIO_SYNTH_STRING_FADE` (8) frames, and the new allpass starts from
  4 frames of the past of the line, so the crossing does not click; a crossing during a
  crossfade ends it and starts the next from the read point that was new. A string that does not move
  pays one compare per frame for it. Its period is 4 frames at the shortest, a quarter of the rate,
  and at the longest the rate over `BOX_AUDIO_SYNTH_PLUCK_MIN` (40 Hz) and 2046 frames, which
  limits the lowest note to the rate over 2046 above 81.8 kHz. Once the peak of a period is under
  -80 dB of the pluck it is silent and costs nothing more. A layer that uses no envelope at all
  (`rings_free`) plays at level 1 and the voice plays on while such a string rings; a voice of
  only strings ends when they are all silent, those of every unison voice, envelopes or not; a
  looping voice plucks again. A string ignores the phase modulation and the feedback of its layer, it may ring modulate or be
  ring modulated, and a patch without strings renders as before, bit identical, and costs one
  compare per frame.
- **Envelopes** are sample accurate: attack from 0 to 1, decay to the sustain level, hold,
  release to 0. Each segment is linear, or curved with `curve` (decay and release) and
  `attack_curve`: a segment of curve c over n frames follows `v(k) = a + (v0 - a) r^k` with
  `r = exp(-c / n)` and an asymptote `a` past the target, chosen so that `v(n)` is the target.
  A positive curve is fast at first and slow toward the target, as an analog envelope; a
  negative one is slow at first. Unlike an analog envelope the segment ends on time: the last
  frame takes the target exactly, so a curved release still ends the voice at the end of its
  release. The level at the middle of a segment is `1 / (1 + exp(-c / 2))` of the way, 0.5
  for a linear one and 0.88 for a curve of 4. `exp` runs once per segment, in the series of
  `synth.c`; per frame a curved segment costs one multiply and one add in double, a linear one
  its step as before. Curves are clamped to plus or minus `BOX_AUDIO_SYNTH_CURVE_MAX` (16),
  and one under `BOX_AUDIO_SYNTH_CURVE_MIN` (0.001) is linear, so a patch with curves of 0
  renders bit identical to the linear envelopes. An attack, and a decay or a release that changes the level, takes at
  least `BOX_AUDIO_SYNTH_EDGE` (0.5 ms), so neither end clicks; with a sustain of 0 the envelope
  ends after its decay. A pass of an envelope is at most `BOX_AUDIO_SYNTH_LENGTH_MAX` (60 s); a
  longer one loses its hold first. A voice ends, and frees its slot, once every envelope it
  uses reached silence; a looping voice starts a new pass instead, with the state of its
  filter. Synth voices start at their gain, without the ramp of the core, since the envelope
  starts at 0: an attack of 1 ms takes 1 ms.
- **The filter** is the trapezoidal state variable filter (Simper) of `src/audio/core/svf.h`,
  which the [filter effect](#filter-effect) shares: lowpass, highpass, a bandpass with unity
  gain at the cutoff and a notch. At a slope of 12 dB per octave it is one stage:
  resonance 0 is a Butterworth response (Q 0.707), 1 a Q of 50. At 24 dB per octave it is two
  cascaded stages of the same kind and cutoff: the first at a fixed Q of 0.54, the second
  resonant from Q 1.31 at resonance 0, so that resonance 0 is a fourth order Butterworth
  response (-3 dB at the cutoff, -24 dB an octave past it), to a Q of 50 at 1. Its
  coefficients come from
  `tan(pi cutoff / rate)` with the cutoff clamped to 10 Hz..0.49 of the rate, and every stage
  is stable on its own for any coefficient, so the cascade stays stable at every cutoff and
  resonance, a cutoff at and above Nyquist and a fast envelope included. Its state is flushed
  like a bus sample.
- **The filter envelope** has the segments of the other envelopes and an amount in octaves,
  -10..10. The cutoff is `cutoff * 2^(amount * level)`: exponential in frequency, so a linear
  segment of the envelope moves the cutoff by the same musical interval per second. It updates
  the coefficients every 8 frames while it runs and once at its end; it does not keep the voice
  playing, and a looping voice restarts it with its pass. A patch with an amount of 0 runs no
  envelope and computes no coefficients per frame, and a 12 dB filter runs one stage, so a
  patch that uses neither costs what it did before.
- **Unison.** A layer plays as `voices` copies of its oscillator, 1 to
  `BOX_AUDIO_SYNTH_UNISON_MAX` (7), detuned evenly over `detune` cents from the lowest to the
  highest (0 to `BOX_AUDIO_SYNTH_DETUNE_MAX`, 100): voice k of n plays at the frequency of the
  layer times `2^(detune (k / (n - 1) - 1/2) / 1200)`, so 3 voices over 20 cents play 10 cents
  under, on and 10 cents over the pitch. Each voice starts every pass at a random phase from the
  generator of the noise, seeded per play, so the voices do not cancel on the attack and a fixed
  run is bit identical. The voices follow the sweep, the pitch and the glide of the layer. Each
  plays at `1 / sqrt(n)` of the level of the layer: voices at different frequencies add their
  power, so the level of the layer stays the same as `voices` changes, while its peaks may rise
  over those of one voice. `width`, 0 to 1, places voice k at the pan `width (2k / (n - 1) - 1)`
  with the equal power gains of the core, from the lowest voice on the left to the highest on
  the right; each channel keeps the level of the layer. The correlation of the two channels is
  the mean of `cos(pi pan / 2)` over the voices: 0 for 2 voices at width 1, 1/3 for 3, 0.48 for
  5, and 1 at width 0. A layer with `voices` and `width` makes the voice stereo: the mix of its
  layers is rendered on two channels, a layer without unison the same on both, through two sets
  of the stages of the filter with the same coefficients, and the voice reaches its bus as a
  stereo sample does. A unison layer that modulates or feeds back does so with the mean of its
  two channels; a modulator of a unison layer moves the phase of each of its voices. A pluck
  layer with unison rings a string per voice, each at the detune and the pan of its voice, and
  takes no random phases; a pluck layer of one voice is its string in the center, bit identical
  to a pluck without unison.
- **Glide.** The `glide` of a patch, 0 to `BOX_AUDIO_SYNTH_GLIDE_MAX` (10) seconds, makes a
  change of the pitch of a voice (`VOICE_SET` `PITCH`) move exponentially in frequency, linear
  in cents, over that time: each oscillator multiplies its turns per frame by
  `(to / from)^(1 / frames)` per frame, so halfway the frequency is the geometric mean of the two.
  A change during a glide starts a new glide from where the pitch stands. A running sweep
  moves with the glide. A loop that starts a new pass during a glide starts at the new pitch.
  A glide of 0 jumps as before, bit identical, and a voice that does not glide costs one
  compare per frame.
- **Legato.** A `LEGATO` command of a synth sound looks for the synth voice of the handle it
  follows. While one of the envelopes of that voice has not reached its release, it retargets
  the voice and gives it the new handle instead of starting a voice: layer i glides from its frequency to the frequency
  of layer i of the new patch times the pitch of the play, over the glide of the new patch (a
  glide of 0 jumps), and stops its sweep. Its envelopes go on without a restart; one in its
  hold starts the hold over, so a held note lasts while notes follow. The voice keeps its
  waves, levels, envelopes, filter and variances, takes the volume, pan and bus of the play
  with the ramp, and a loop restarts on the new note. Layers past the count of the new patch
  follow the pitch. When every envelope of the voice releases or ended, or the voice is not a
  synth, the command takes the handle from it and starts a new voice with the new handle: a
  synth voice stops looping, rings out its release and reports its `VOICE_END` with handle 0
  (so a looping voice does not start another pass without a handle), any other fades
  out as after a `STOP`. Without a voice of the handle it follows it is a plain `PLAY` of the
  new handle. Since every note has a new handle, the `VOICE_END` of a voice that ended before
  the sender saw it never names the voice of the next note. A sound that is not ready leaves
  the voice as it was, with the new handle. A retarget allocates nothing. A `LEGATO` has a
  `when` and waits in the schedule like a `PLAY`. With `BOX_AUDIO_PLAY_GATE` in its flags the
  voice it retargets sustains until a `RELEASE`, its hold ignored; without it a gated voice
  holds for the hold of its envelopes from there, as a voice of `box.audio.play` does.
- **Gate and release.** A voice played with `BOX_AUDIO_PLAY_GATE` enters its hold after the
  decay and stays there, at the sustain level of each envelope, until a `RELEASE`, which moves
  every envelope the voice has, the amp, the own envelopes of its layers and the filter
  envelope, into its release from the level where it stands: the release starts from that
  level and takes the release time of the envelope with its curve, so it neither jumps nor
  lasts longer. A release during the attack or the decay releases from there, and an envelope
  with a sustain of 0 that is in its decay releases over its release time too. A released voice
  stops looping. A gated voice costs a test per segment change and per curved frame, and a
  voice without the gate renders bit identical.
- **Cost.** Render work only: per frame of a voice an envelope step per envelope (a multiply
  and an add when curved), an oscillator per layer and the filter, one stage or two; a modulated
  patch adds a phase offset and a wrap per layer; during a glide a multiply per oscillator. A
  unison layer costs an oscillator per voice; a stereo patch runs its filter twice. A patch
  in which no layer has more than one voice takes the loop it took before, bit identical, and
  costs one compare per block; a layer of one voice with a width is such a layer. A string
  costs a read and a write of its line, 6 multiplies and its peak per frame while it rings, and
  a pluck writes its line of 2048 floats once; a unison pluck layer costs that per voice. A
  patch with LFOs adds three adds and two multiplies per oscillator per frame and the control
  block every 8 frames, and a string with a vibrato retunes cheaply every frame and exactly
  every 8 frames and, for 8 frames after each crossing of a whole frame, runs a second read
  point through its lowpass and allpass; a patch without LFOs takes the loops it
  took before and renders bit identical, and a square at a width of 0.5 costs a compare.
  `bench/audio` has a `Synth` suite of 64 voices in ns per sample, with a filter envelope, with a
  24 dB filter, `voices_64_lfo`, two LFOs on the pitch, the level, the cutoff and the pulse
  width (about twice the plain patch, most of it the coefficients of the moving cutoff, as with
  a filter envelope), `voices_64_fm`, two FM pairs per voice, which measures the modulated
  path, `voices_32_pluck`, 32 ringing strings, one per voice, `voices_32_pluck_vibrato`, the
  same strings under a vibrato of 25 cents, `voices_8_pluck_unison`, the same 32 strings as 8
  voices of a 4 voice unison in stereo (about half the time of `voices_32_pluck`: a voice, its
  filter and its mix cost more than a string), and `voices_64_curved`, which is
  `voices_64` with a curve of 4 and an attack curve of 1.5 on the patch envelope and on the
  envelope of the square. A frame inside a linear segment is an inlined add behind one test;
  entering a segment, the last frame of one and a curved frame take an out of line step, so a
  patch without curves pays nothing for them. Measured on a shared 4 core arm64
  machine, release build, one core, 12 interleaved runs (min / median in ns per sample):
  `voices_64` 546 / 617, `voices_64_curved` 551 / 656, about 1 to 6 % more, near the noise.
  The curved segments are some 15 % of a pass of these voices, which hold for 2 s at a flat
  level, so a patch that is curved all the time costs more. Before the linear step was split
  out, the test of curved in every step made linear patches 8 to 10 % slower than without
  curves (`voices_64` 590 / 702 against 546 / 634, `voices_64_filter_envelope` 886 / 1080
  against 830 / 993); now they are back at 546 / 617 and 778 / 898. On the same machine a
  quantum of `voices_32_pluck` takes about 35 µs (9 ns per string and frame) and of
  `voices_32_pluck_vibrato` about 80 µs (20 ns), of which some 60 µs is the LFO path without
  any retuning; an exact retune every frame, as strings did before, took about 145 µs.

The core needs no libm: the exponential sweeps, the filter envelope and the LFOs use series for
`log2` and `exp2` in `synth.c`.

### Synth sounds

A sound effect is a prefab in script, with no file. `SynthSound` marks the root; the layers are
the root and its children that have a `SynthOscillator`, as particle effect layers are, and the
children of layers that have one, each after its parent. The `SynthEnvelope`, the `SynthFilter`
and the `SynthFilterEnvelope` of the root apply to the mix of the layers; a layer may have its
own `SynthEnvelope`, and one without uses the root's. A `SynthGlide` on the root makes notes
slide, see [Glide and legato](#glide-and-legato), and a `SynthLfo` moves the sound, see
[LFOs](#lfos).

A layer with the `modulation` `SynthPhase` or `SynthRing` is a modulator of its parent layer
(see [Synth](#synth)): its wave times its envelope moves the phase of its parent by `index`
radians, or multiplies it at a depth of `index`. Give a modulator its own, shorter
`SynthEnvelope` and the brightness of the sound decays with it. A modulator is not heard unless
it is `audible`; its `level` is the level it is heard at. `frequency` sets the ratio: the same
as the carrier for a harmonic spectrum, a ratio such as 3.5 or 1.41 for an inharmonic one.

| Component | What it is |
|-----------|------------|
| `SynthSound` | The root: `seed`, `pitch_variance` and `level_variance` (random shares per play, 0) |
| `SynthOscillator` | A layer: `wave` (`SynthSine`, `SynthSaw`, `SynthSquare`, `SynthTriangle`, `SynthNoise`, `SynthPluck`), `frequency` (440), `frequency_end` (0, no sweep), `sweep` (s), `level` (1), `modulation` (`SynthMix`, heard; `SynthPhase` or `SynthRing`, modulates its parent), `index` (1: radians for `SynthPhase` up to 32, limited against the frequency; a depth of 0..1 for `SynthRing`), `feedback` (0, radians on its own phase up to 1.5), `audible` (false: whether a modulator is heard as well), `pulse_width` (0.5: the share of the period a `SynthSquare` is high, 0.01..0.99) |
| `SynthString` | On a `SynthPluck` layer: `decay` (2, seconds of the fundamental to -60 dB, 0.01..60), `brightness` (0.5, the lowpass of the pluck and in the loop: 0 dark, 1 metallic) and `hardness` (0.8, the lowpass of the pluck: 0 a soft finger, 1 a hard pick); a `SynthPluck` layer without one takes these defaults |
| `SynthEnvelope` | `attack`, `decay`, `sustain`, `release`, `hold`: seconds and a sustain level, all 0 by default; `curve` (decay and release) and `attack_curve`: 0 linear (default), positive fast at first, -16..16 |
| `SynthFilter` | `kind` (`SynthLowPass`, `SynthHighPass`, `SynthBandPass`, `SynthNotch`), `cutoff` (1000), `resonance` (0), `slope` (`SynthSlope12`, `SynthSlope24`) |
| `SynthFilterEnvelope` | The envelope of the cutoff: `attack`, `decay`, `sustain`, `release`, `hold`, `curve` and `attack_curve` as in `SynthEnvelope`, and `amount`, the octaves the cutoff moves at level 1 (0, negative closes the filter); nothing without a `SynthFilter` |
| `SynthUnison` | On a layer: `voices` (3, 1 to 7), `detune` (10 cents from the lowest voice to the highest, 0 to 100) and `width` (0 mono to 1, from left to right), see [Unison and width](#unison-and-width) |
| `SynthGlide` | On the root: `time` of a glide (s, 0 jumps), `legato` (false) and `channel` (0), see [Glide and legato](#glide-and-legato) |
| `SynthLfo` | On the root, a layer or another child of the root: `wave` (`SynthLfoSine`), `rate` (5 Hz), `delay`, `fade`, `phase`, `random_phase` and the depths `pitch` (cents), `amplitude`, `cutoff` (octaves) and `pulse_width`, all 0, see [LFOs](#lfos) |

The module (`src/audio/synth_sound.c`) compiles a sound once, and again when a component of it
or of a layer changes, into a patch that the sync system sends with a `SYNTH` command before the
plays of the frame. `box.audio.play(UiClick, 0.8)` plays it like any sound, and an `AudioSource`
with `loop: true` starts it over at the end of its envelopes. A play allocates nothing.

- More than 8 layers log one error that names the sound and their count; the first 8 play. A
  layer that is left out takes its modulators with it.
- A modulator whose parent is not a layer, such as a child of a root without a
  `SynthOscillator`, logs one error that names the sound and is left out.
- More than 2 `SynthLfo` log one error that names the sound; the first 2 play, see
  [LFOs](#lfos).
- A layer without any envelope (no `SynthEnvelope` on it or on the root) would never end: it
  logs one error and the sound plays for `BOX_AUDIO_SYNTH_ENDLESS` (10) seconds. A `SynthPluck`
  layer is the exception: it rings out on its own and the sound ends once its string is silent.
- A `SynthPluck` layer under `BOX_AUDIO_SYNTH_PLUCK_LOWEST` (40 Hz), its `frequency` or its
  `frequency_end`, logs one warning that names the sound and plays at 40 Hz.
- An envelope that is not a time of 0 to 60 seconds logs one error and is cut.
- The filter envelope has its own times: it does not follow the `SynthEnvelope`, so a cutoff
  that closes with the note takes the same attack, decay, hold and release. One that should stay
  open while the sound plays takes a sustain of 1 and a hold that lasts the sound.
- Deleting the sound releases it and fades its one shots, as for `AudioPcm`.

#### Unison and width

A `SynthUnison` next to the `SynthOscillator` of a layer thickens it into several detuned copies
of itself, the chorus of an ensemble or a supersaw, from one layer instead of one layer per copy:

| Field | What it is |
|-------|------------|
| `voices` | Copies of the oscillator, 1 to 7 (3); 1 is the layer without unison |
| `detune` | Cents from the lowest copy to the highest, 0 to 100 (10), spread evenly around the pitch |
| `width` | 0 (0, every copy in the center, mono) to 1 (the lowest copy left, the highest right) |

The copies share the level of the layer, so adding voices thickens the sound without making it
louder, and each starts at a random phase of the `seed` of the sound and the count of its plays,
so the attack does not cancel and the same seed renders the same. A sound in which a layer has
voices and width plays in stereo; layers without unison stay in the center. A detune of 5 to 15
cents is a gentle chorus, 20 to 40 a supersaw; a width of 0.5 to 0.8 keeps a pad wide without
leaving a hole in the middle.

```
using box.audio

music { AudioBus: {volume: 1} }

// A wide pad: a saw of five voices over 20 cents across most of the stereo field and a fifth
// of three voices across all of it, over a sine sub in the center, 2.1 s
prefab WidePad {
  AudioOutput: {bus: music}
  SynthSound: {seed: 3}
  SynthEnvelope: {attack: 0.4, decay: 0.3, sustain: 0.7, hold: 0.6, release: 0.8, curve: 2.5, attack_curve: 1.5}
  SynthFilter: {kind: SynthLowPass, cutoff: 1400, resonance: 0.1, slope: SynthSlope24}
  saw {
    SynthOscillator: {wave: SynthSaw, frequency: 220, level: 0.2}
    SynthUnison: {voices: 5, detune: 20, width: 0.8}
  }
  fifth {
    SynthOscillator: {wave: SynthSaw, frequency: 330, level: 0.12}
    SynthUnison: {voices: 3, detune: 14, width: 1}
  }
  sub { SynthOscillator: {wave: SynthSine, frequency: 110, level: 0.15} }
}
```

The pad takes three layers, where the hand detuned `Strings` below take six saws for one
narrower, mono ensemble. Its render costs nine oscillators and two filters per frame.

**In the mixer and in space** a wide sound is a stereo voice: the pan of the voice, of
`box.audio.play` or of `AudioSpatial`, and the pan of its bus are a balance of its two channels
(see [The core](#the-core)), and a send carries both. A spatial source keeps its width while it
is in front of the listener, and as it moves to a side the far channel fades and the near one
rises by 3 dB, so a wide sound at the side is heard from that side alone, at the same power,
with the voices of that side. It does not narrow with distance. A sound that should be a point
in space keeps `width` at 0: its unison voices still thicken it, in mono.

#### LFOs

A `SynthLfo` moves a held note: vibrato on a flute, tremolo on an organ, a pad whose cutoff and
pulse width drift. On the root of a sound, or on a child of the root without a
`SynthOscillator`, it moves every layer; on a layer it moves that layer alone. A sound has up
to two: the root's first, then those of its layers in their order, then those of the other
children; a third logs one error that names the sound and is left out.

| Field | What it is |
|-------|------------|
| `wave` | `SynthLfoSine` (default), `SynthLfoTriangle`, `SynthLfoSquare`, `SynthLfoSaw` (rising) or `SynthLfoRandom`, a random value each period seeded per play from `SynthSound.seed`; the square, the saw and the random wave are smoothed over 2 ms so they do not click |
| `rate` | Hz, 0..100 (5) |
| `delay` | Seconds from the start of the note before it moves anything (0) |
| `fade` | Seconds its depth rises over after the delay (0, at once) |
| `phase` | Phase it starts at in turns, 0..1 (0): every wave starts at 0 rising, the square at its top, and 0.25 is the top of a sine |
| `random_phase` | Start at a random phase per play instead (false), the same for the same seed and count of plays |
| `pitch` | Cents of vibrato either way at the top of the wave, -2400..2400 (0) |
| `amplitude` | Tremolo: the level dips from 1 at the top of the wave to `1 - amplitude` at its bottom, 0..1 (0) |
| `cutoff` | Octaves the cutoff of the `SynthFilter` moves either way, -10..10 (0); of the whole sound even on a layer, nothing without a filter |
| `pulse_width` | Share of the period the pulse width of the squares it moves changes by either way, -0.49..0.49 (0) |

A delay and a fade bring a vibrato in after the attack, as players do; a fade on its own makes
it swell from the start. The LFO restarts with every note and with each pass of a looping
sound; a legato note keeps it running. The level only dips, so a tremolo never makes a sound
louder than it is without it. A vibrato moves every unison voice of a layer and the pitch of a
`SynthPluck` string with it, each string of a unison pluck.

```
using box.audio

music { AudioBus: {volume: 1} }

// The Mossvale flute: a vibrato of 14 cents at 5.2 Hz with a slight tremolo, which comes in
// 0.2 s after the start of the note and fades in over 0.3 s, at a random phase per note
template Flute {
  prop f: f32 = 440
  AudioOutput: {bus: music}
  SynthSound: {level_variance: 0.05}
  SynthEnvelope: {attack: 0.06, decay: 0.25, sustain: 0.55, hold: 0.22, release: 0.5, curve: 2.5, attack_curve: 1.5}
  SynthFilter: {kind: SynthLowPass, cutoff: 2600, resonance: 0.1}
  SynthLfo: {rate: 5.2, delay: 0.2, fade: 0.3, random_phase: true, pitch: 14, amplitude: 0.08}
  tone { SynthOscillator: {wave: SynthSine, frequency: f, level: 0.42} }
  reed { SynthOscillator: {wave: SynthTriangle, frequency: f * 1.002, level: 0.14} }
  breath { SynthOscillator: {wave: SynthNoise, level: 0.025}; SynthEnvelope: {attack: 0.02, decay: 0.12} }
  octave { SynthOscillator: {wave: SynthSine, frequency: f * 2, level: 0.05} }
}

Flute flute_a4(f: 440)

// A pad: two squares whose pulse widths a slow triangle moves with the cutoff, and the upper
// square alone drifting a few cents against the lower
prefab PwmPad {
  AudioOutput: {bus: music}
  SynthSound: {seed: 3}
  SynthEnvelope: {attack: 1.2, decay: 0.5, sustain: 0.8, hold: 1.5, release: 1.5, curve: 2, attack_curve: 1}
  SynthFilter: {kind: SynthLowPass, cutoff: 1200, resonance: 0.2}
  SynthLfo: {wave: SynthLfoTriangle, rate: 0.3, pulse_width: 0.3, cutoff: 0.5}
  low { SynthOscillator: {wave: SynthSquare, frequency: 220, level: 0.15} }
  high {
    SynthOscillator: {wave: SynthSquare, frequency: 330, level: 0.12, pulse_width: 0.3}
    SynthLfo: {rate: 0.4, phase: 0.5, fade: 1, pitch: 6}
  }
}
```

The flute plays its first 0.2 s straight; then its pitch swings 14 cents up and down 5.2 times
a second, at full depth from 0.5 s, and its level dips by up to 8 % with it. The pad opens and
closes its cutoff by half an octave over 3.3 s while the widths of its squares go from 0.2 to
0.8 and back (the upper one from 0.01 to 0.6, clamped), and its fifth beats slowly against the
root as the upper square drifts 6 cents either way.

#### Glide and legato

Leads and basses slide between notes with a `SynthGlide` on the root:

| Field | What it is |
|-------|------------|
| `time` | Seconds a change of pitch takes, exponential in frequency (linear in cents), 0 to 10; 0 (default) jumps as without the component |
| `legato` | Play the sound as a note of a mono voice (false) |
| `channel` | The mono voice of the one shots of legato sounds (0): legato sounds of the same channel share one voice |

How a note follows the previous one:

- **Pitch.** With a `time`, a change of `AudioSource.pitch` while the source plays glides from
  the pitch that sounds to the new one over the time. Halfway the frequency is the geometric
  mean of the two.
- **Notes from script.** Each note is its own sound, as in the Mossvale score. A
  `box.audio.play` (or `play_at`) of a legato sound while the last one shot of its `channel`
  still holds does not start a voice: that voice glides to the frequencies of the new sound
  over the `time` of the new sound, without a new attack, and takes the volume and the bus of
  the new play. A note that comes while the voice holds its sustain holds it for its `hold`
  again, so a phrase of quick notes is one held voice that releases after the last note. A
  note after the voice reached its release, or after it ended, starts a new voice with its
  attack; the voice that released rings out, and a looping one stops looping and ends.
- **Notes of an instrument.** An [instrument](#instruments) is the note off of a legato
  voice. A gated note of a legato sound follows the voice of its `channel` as `box.audio.play`
  does, with its gate: the voice sustains, its hold ignored, until the release at the end of
  the note. A note that follows before that release glides into the voice and cancels the
  release, so overlapping notes and notes back to back (a release and the next note on the same
  frame) are one held phrase; the release of the last note releases the voice with its
  envelopes. A note after the voice released starts a new voice with its attack.
- **Notes on a source.** A new legato `sound` on an `AudioSource` does the same with the voice
  of the source, when `loop` does not change (the voice it follows may be of any synth sound); the source keeps playing and
  ends with the voice.
- The voice keeps the waves, levels, envelopes, filter and variances of the sound that
  started it; the following notes give it only their frequencies, layer by layer, and their
  glide time. Notes of a legato voice share its layers in order, so they should come from one
  template, as in the example. A sound without `legato` always starts its own voice, and its
  `time` only glides changes of the pitch of its source.

```
using box.audio

music { AudioBus: {volume: 1} }

// A lead: a mono saw an octave over a sine whose notes slide into each other over 80 ms
template Lead {
  prop f: f32 = 220
  AudioOutput: {bus: music}
  SynthSound: {}
  SynthGlide: {time: 0.08, legato: true, channel: 1}
  SynthEnvelope: {attack: 0.01, decay: 0.2, sustain: 0.6, hold: 0.3, release: 0.25, curve: 2}
  SynthFilter: {kind: SynthLowPass, cutoff: 1800, resonance: 0.2}
  saw { SynthOscillator: {wave: SynthSaw, frequency: f, level: 0.3} }
  sub { SynthOscillator: {wave: SynthSine, frequency: f * 0.5, level: 0.2} }
}

Lead lead_a3(f: 220)
Lead lead_c4(f: 261.63)
Lead lead_e4(f: 329.63)
```

`box.audio.play(lead_a3, 0.8)`, then `lead_c4` and `lead_e4` a few frames apart, play one voice
that slides up a minor third and a major third and releases 0.3 s after the last note.

Which curves to use: plucks, the harp, mallets and drums want a `curve` of 4 to 6 so they fall
off fast at first and ring out softly; pads, strings and winds want 2 to 3 on the decay and the
release, and an `attack_curve` of 1 to 2 for a swell that settles. A sound that keeps its
linear envelope (curves of 0) renders exactly as before. A curve makes a segment quieter
sooner, so a pluck usually wants a longer decay than its linear version.

Nine examples, six that are plain layers and three that modulate:

```
using box.audio

ui { AudioBus: {volume: 1} }

// A click: a burst of noise and a falling tone, 31 ms
prefab UiClick {
  AudioOutput: {bus: ui}
  SynthSound: {pitch_variance: 0.05}
  SynthEnvelope: {attack: 0.001, decay: 0.03}
  noise { SynthOscillator: {wave: SynthNoise, level: 0.6} }
  tone { SynthOscillator: {wave: SynthSine, frequency: 1800, frequency_end: 900, sweep: 0.03, level: 0.4} }
}

// A UI blip: a soft square that steps up a fifth, through a lowpass
prefab UiBlip {
  AudioOutput: {bus: ui}
  SynthSound: {level_variance: 0.1}
  SynthEnvelope: {attack: 0.004, decay: 0.06, sustain: 0.4, hold: 0.04, release: 0.05}
  SynthFilter: {kind: SynthLowPass, cutoff: 3000, resonance: 0.2}
  low { SynthOscillator: {wave: SynthSquare, frequency: 660, level: 0.25} }
  high {
    SynthOscillator: {wave: SynthTriangle, frequency: 990, level: 0.3}
    SynthEnvelope: {attack: 0.04, decay: 0.08}
  }
}

// A whoosh: noise through a resonant bandpass that a filter envelope opens as it fades in
prefab Whoosh {
  SynthSound: {seed: 7, level_variance: 0.1}
  SynthEnvelope: {attack: 0.15, decay: 0.25, sustain: 0.3, hold: 0.1, release: 0.3}
  SynthFilter: {kind: SynthBandPass, cutoff: 300, resonance: 0.6}
  SynthFilterEnvelope: {attack: 0.5, sustain: 1, hold: 0.31, amount: 3.737}
  SynthOscillator: {wave: SynthNoise, level: 0.8}
}

// A bell: a modulator at 3.5 times the pitch makes an inharmonic spectrum, and its envelope
// is shorter than the carrier's, so the bell starts bright and rings out pure, 2.5 s
prefab Bell {
  SynthSound: {level_variance: 0.05}
  SynthEnvelope: {attack: 0.002, decay: 2.5}
  SynthOscillator: {frequency: 660, level: 0.4}
  bright {
    SynthOscillator: {frequency: 2310, modulation: SynthPhase, index: 4}
    SynthEnvelope: {attack: 0.001, decay: 1.2}
  }
}

// An electric piano: a 1:1 pair for the body and a short tine, a modulator at 14 times the
// pitch that fades in 80 ms, 1.6 s
prefab ElectricPiano {
  SynthSound: {}
  body {
    SynthOscillator: {frequency: 220, level: 0.35}
    SynthEnvelope: {attack: 0.002, decay: 1.6}
    warmth {
      SynthOscillator: {frequency: 220, modulation: SynthPhase, index: 1.8}
      SynthEnvelope: {attack: 0.002, decay: 0.6}
    }
  }
  tine {
    SynthOscillator: {frequency: 220, level: 0.12}
    SynthEnvelope: {attack: 0.001, decay: 0.3}
    strike {
      SynthOscillator: {frequency: 3080, modulation: SynthPhase, index: 2.5}
      SynthEnvelope: {attack: 0.001, decay: 0.08}
    }
  }
}

// A metallic hit: two partials that do not share harmonics, ring modulated, the modulator
// gritty with feedback, and a short clang of FM on top, through a highpass, 0.6 s
prefab MetalHit {
  SynthSound: {pitch_variance: 0.03}
  SynthEnvelope: {attack: 0.001, decay: 0.6}
  SynthFilter: {kind: SynthHighPass, cutoff: 400}
  SynthOscillator: {frequency: 587, level: 0.4}
  ring { SynthOscillator: {frequency: 1371, modulation: SynthRing, index: 1, feedback: 0.6} }
  clang {
    SynthOscillator: {frequency: 1960, modulation: SynthPhase, index: 3}
    SynthEnvelope: {attack: 0.001, decay: 0.15}
  }
}

// A pluck: a saw through a 24 dB lowpass that its filter envelope opens 4 octaves on the attack
// and closes with the note
prefab Pluck {
  SynthSound: {}
  SynthEnvelope: {attack: 0.002, decay: 0.4, sustain: 0.3, hold: 0.1, release: 0.2}
  SynthFilter: {kind: SynthLowPass, cutoff: 300, resonance: 0.3, slope: SynthSlope24}
  SynthFilterEnvelope: {attack: 0.002, decay: 0.25, sustain: 0.2, hold: 0.25, release: 0.2, amount: 4}
  SynthOscillator: {wave: SynthSaw, frequency: 220, level: 0.5}
}

// A ring: a curved decay that falls fast and rings out, no sustain, its lowpass closing from
// 4 kHz to 1.2 kHz over 0.3 s
prefab Ring {
  AudioOutput: {bus: ui}
  SynthSound: {level_variance: 0.1}
  SynthEnvelope: {attack: 0.002, decay: 0.6, curve: 5}
  SynthFilter: {kind: SynthLowPass, cutoff: 1200, resonance: 0.1}
  SynthFilterEnvelope: {decay: 0.3, amount: 1.737}
  string { SynthOscillator: {wave: SynthTriangle, frequency: 330, level: 0.4} }
  body { SynthOscillator: {wave: SynthSine, frequency: 330, level: 0.3} }
}

// Strings: an ensemble of five saws detuned around 220 Hz and one an octave up, a sine an
// octave down and a breath of noise, eight layers, through a 24 dB lowpass that opens with the
// bow and swells in, 1.6 s
prefab Strings {
  SynthSound: {}
  SynthEnvelope: {attack: 0.3, decay: 0.2, sustain: 0.7, hold: 0.5, release: 0.6, curve: 2.5, attack_curve: 1.5}
  SynthFilter: {kind: SynthLowPass, cutoff: 900, resonance: 0.15, slope: SynthSlope24}
  SynthFilterEnvelope: {attack: 0.4, sustain: 1, hold: 0.7, release: 0.5, amount: 1.5}
  SynthOscillator: {wave: SynthSaw, frequency: 220, level: 0.08}
  a { SynthOscillator: {wave: SynthSaw, frequency: 219.2, level: 0.08} }
  b { SynthOscillator: {wave: SynthSaw, frequency: 220.9, level: 0.08} }
  c { SynthOscillator: {wave: SynthSaw, frequency: 218.5, level: 0.07} }
  d { SynthOscillator: {wave: SynthSaw, frequency: 221.6, level: 0.07} }
  e { SynthOscillator: {wave: SynthSaw, frequency: 440.6, level: 0.04} }
  sub { SynthOscillator: {wave: SynthSine, frequency: 110, level: 0.12} }
  air { SynthOscillator: {wave: SynthNoise, level: 0.01} }
}
```

The strings use all 8 layers: the detuned saws beat slowly against each other, the chorus of
an ensemble, which takes more oscillators than a lead does. A sound of 8 layers costs twice the
render work of one of 4 and nothing more; the other sounds keep their cost.

The whoosh opens from 300 Hz to `300 * 2^3.737`, 4 kHz, over half a second and stays there. A
sweep from one cutoff to another over a time is an attack of that time with a sustain of 1 when
it rises, and a decay of that time from the higher cutoff to the lower one, the `cutoff`, when
it falls: `SynthFilter: {cutoff: 300}` with `SynthFilterEnvelope: {decay: 0.2, amount: 3}`
falls from 2400 Hz to 300 Hz in 0.2 s.

#### Plucked strings

Oscillators make a pluck that sounds like a synth: a triangle or a saw under a closing filter
keeps the same harmonics while it fades. A real string loses its high harmonics first and its
pitch lives on in a ringing tone. A `SynthPluck` layer is such a string, a physical model
(Karplus-Strong, see [Synth](#synth)) that costs about what an oscillator does. It needs no
envelope: it rings out on its own and the sound ends when it is silent. Its `SynthString` shapes
it:

| Field | What it is |
|-------|------------|
| `decay` | Seconds the fundamental takes to fall by 60 dB, 0.01 to 60 (2); the harmonics fall faster |
| `brightness` | The lowpass of the pluck and in the loop, 0 to 1 (0.5): 0 a dark gut or nylon string that starts soft and whose harmonics die in a moment, 1 a bright, metallic ring |
| `hardness` | The lowpass of the noise that plucks it, 0 to 1 (0.8): 0 a soft finger, round from the start, 1 a hard pick with a sharp attack |

The `frequency`, `frequency_end`, `sweep` and `level` of the `SynthOscillator` apply as to any
layer; `index` and `feedback` do not. Every play plucks with other noise, from the `seed` of the
`SynthSound` and the count of plays, so repeated notes differ a little, as on a real string, and a
fixed run renders bit identical. The lowest note is `BOX_AUDIO_SYNTH_PLUCK_LOWEST`, 40 Hz: a lower
one plays at 40 Hz with one warning. The string follows the pitch of its voice: a change of
`AudioSource.pitch`, a sweep, a `SynthGlide`, legato notes and the `pitch` of a `SynthLfo` bend
the ringing string without a new pluck, so a pitch LFO gives a string its vibrato, as the guitar
below has on a held note. Glides and vibrato stay clean: while the pitch moves the string
retunes cheaply every frame and exactly once per control block, and when its delay line moves by
a whole frame it crossfades from the old read point to the new one. A string may ring modulate
or be ring modulated, but it ignores phase modulation and feedback.

A `SynthUnison` on a `SynthPluck` layer rings a string for each of its `voices`, each detuned and
panned as the voices of an oscillator (see [Unison and width](#unison-and-width)) and plucked with
its own noise, so the strings beat against each other and do not sound alike: the pairs of a 12
string guitar, or a harp ensemble, in one layer and wide. The strings share the level of the
layer at `1 / sqrt(voices)` each, and the sound ends once the last of them is silent.

A core rings 32 strings at once (`BOX_AUDIO_SYNTH_STRINGS`), each with its delay line of 8 KB,
and a pluck takes one string per unison voice: a single string takes 1, the 12 string guitar
below 3 per note, a 4 voice unison 4, so a chord of 6 notes of a 4 voice unison takes 24 and a
second chord while the first rings finds too few. A play that does not find a string for each
voice is ignored, as one without a voice slot, and its strings come back when its voice ends.
Keep the unison of a pluck at 2 or 3 voices for parts of many ringing notes; a long `decay` keeps
the strings of a note taken for longer.

```
using box.audio

music { AudioBus: {volume: 1} }

// A guitar: a steel string on G3 plucked with a pick, through the lowpass of its body, 3 s,
// with a vibrato of 10 cents at 5.5 Hz that the finger brings in once the note is held
prefab Guitar {
  AudioOutput: {bus: music}
  SynthSound: {level_variance: 0.05}
  SynthFilter: {kind: SynthLowPass, cutoff: 4500}
  string {
    SynthOscillator: {wave: SynthPluck, frequency: 196, level: 0.6}
    SynthString: {decay: 3, brightness: 0.6, hardness: 0.9}
    SynthLfo: {rate: 5.5, delay: 0.4, fade: 0.5, pitch: 10}
  }
}

// A 12 string guitar: G3 strung as a pair of steel strings 6 cents apart across half of the
// stereo field, with a quieter octave string in the center, 3 strings, 3 s
prefab TwelveString {
  AudioOutput: {bus: music}
  SynthSound: {level_variance: 0.05}
  SynthFilter: {kind: SynthLowPass, cutoff: 5000}
  string {
    SynthOscillator: {wave: SynthPluck, frequency: 196, level: 0.6}
    SynthUnison: {voices: 2, detune: 6, width: 0.5}
    SynthString: {decay: 3, brightness: 0.6, hardness: 0.9}
  }
  octave {
    SynthOscillator: {wave: SynthPluck, frequency: 392, level: 0.2}
    SynthString: {decay: 2, brightness: 0.7, hardness: 0.9}
  }
}

// A koto: a bright silk string on D4 plucked with a hard pick, pressed up a semitone after
// the pluck, and a quieter string an octave up that dies sooner, 1.5 s
prefab Koto {
  AudioOutput: {bus: music}
  SynthSound: {}
  string {
    SynthOscillator: {wave: SynthPluck, frequency: 293.66, frequency_end: 311.13, sweep: 0.25, level: 0.5}
    SynthString: {decay: 1.5, brightness: 0.8, hardness: 1}
  }
  octave {
    SynthOscillator: {wave: SynthPluck, frequency: 587.33, frequency_end: 622.25, sweep: 0.25, level: 0.12}
    SynthString: {decay: 0.6, brightness: 0.7, hardness: 1}
  }
}
```

The guitar wants a `brightness` around 0.6 and a long `decay`, a nylon string 0.3 and a softer
`hardness`; the 12 string guitar is the same string as a detuned pair, which shimmers as the two
beat, with its octave; the koto rings bright and short. The harp of the Mossvale score
(`etc/scenes/mossvale/music.flecs`) is a gut string, `SynthString: {decay: 2.2, brightness: 0.35,
hardness: 0.45}`, over a soft sine with its own short envelope for the body; its notes ring out
over each other as a harp's do, and each ends when its string is silent.

The tests are the `Synth` suite of `test/cpu/audio`, on offline renders: frequencies of every wave
at 44.1 and 48 kHz, sweeps, aliasing of a 5 kHz saw, square and triangle, flat and seeded noise,
the times and edges of envelopes, curved envelopes (a linear one bit identical to the linear
recurrence, a curved decay at its midpoint and on its sustain, a curved release that ends the voice
on time), the end of a voice, the filter one octave past its cutoff at both slopes, the cutoff
following a filter envelope up and down, stability under fast filter envelopes at full resonance
and at Nyquist for both slopes and extreme values, layers, phase modulation at 1:1 and 6:1 against
Bessel amplitudes and without drift, an index that follows the envelope of its modulator and fades
the high sidebands, the limit of the index, ring modulation at the sum and difference alone,
audible modulators, feedback, 8 layers each found at its own frequency with a Goertzel filter, a
9th left out, the size of the command, the pool of parts, renders of patches of 4 layers bit
identical whatever the unused layers hold, scripts from `test/assets/audio/synth.flecs` and
`test/assets/audio/synth_fm.flecs` and `test/assets/audio/synth_layers.flecs`, the nine examples
above from `test/assets/audio/synth_examples.flecs`, glides (the frequency halfway through a glide
at the geometric mean, a glide of 0 bit identical to the jump, the pitch of a source) and legato
notes (no new attack, a held sustain that holds again, a new voice after the release, also of a
looping voice that then ends, and after the end, from script and from a source, the lead above
from `test/assets/audio/synth_glide.flecs`), unison (peaks at the detuned frequencies, width 0
identical on both channels, the correlation of the channels at width 1 and 0.5, the level as the
count of voices changes, one voice bit identical to a layer without unison, the same seed bit
identical, a wide voice through the pan of the voice and of its bus and a send, and the pad above
from `test/assets/audio/synth_unison.flecs` from script, panned in space), pluck strings (the
pitch within 2 cents from 80 Hz to 2 kHz at 44.1 and 48 kHz, a -60 dB time that scales with the
decay, more upper harmonics when brighter, bit identical for a seed and different for another,
the end of a voice once its string is silent with and without an envelope, a jump and a glide of
the pitch, the pool of strings, a string of one voice bit identical to one without unison and
next to a wide layer, unison plucks (peaks at the detuned frequencies, width 0 identical on both
channels and width 1 decorrelated, one string per voice from the pool, given back when the voice
ends, and the same seed bit identical), the defaults and the warning of a low note, and the
guitar, the 12 string guitar, the koto and the Mossvale harp from
`test/assets/audio/synth_pluck.flecs`), LFOs (a vibrato that moves the pitch by its cents at its
rate only after its delay, a tremolo that moves the envelope at its rate by its depth, a cutoff
and a pulse width that move the spectrum at the rate, a pulse band limited at every width, a
seeded random phase the same for the same seed and count of plays, a square at a width of 0.5
and a patch without LFOs bit identical, also in a voice slot that played a cutoff LFO, smooth
square LFOs, a delay without a fade that does not click, a tremolo and a vibrato of unison
voices in stereo, a vibrato of a pluck string at its depth and rate, a glide of an octave in 20 ms and a wide
fast vibrato with less energy above 8 kHz than without the crossfade, a string under an LFO at
no depth bit identical to one without, the cost of 32 strings under vibrato, the examples of [LFOs](#lfos) from
`test/assets/audio/synth_lfo.flecs`) and errors; `NoAlloc` renders 64 synth voices. The browser
check is `test/integration/web/audio_synth.mjs`.

## Transports

The main thread reaches the core through a transport, one small interface in
`src/audio/transport.h` that every output implements:

```c
bool commands(t, data, size);                      // send a command batch, copied
bool sound(t, id, data, frames, channels, rate);   // hand over a sound, copied
bool stream_chunk(t, stream, voice, data, frames, channels, last); // a chunk, copied
void stream_status(t, stream, status);             // a stream's status, frees consumed chunks
int32_t read_events(t, events, max);               // the events of the core, oldest first
void meters(t, buses, effects);                    // the meters since the last call
box_audio_state_t state(t);                        // what the output does
void update(t, delta);                             // once per frame
void stats(t, stats);                              // voices and load of the core
uint64_t clock(t);                                 // the frame of the core that renders now
bool capture(t, frames);                           // record the next frames the core renders
float* capture_take(t, &frames);                   // a finished capture, NULL while it records
```

| Transport | Where the core runs |
|-----------|---------------------|
| native | on the audio thread of a miniaudio device, macOS and Linux |
| web | in the AudioWorklet of the page, over its `MessagePort` |
| null | on the main thread, which renders the frames of each frame's delta |

The state is `OFF` (no output), `NO_DEVICE` (the null transport renders), `STARTING` (the web
loads the worklet), `WAITING` (the page waits for a gesture), `RUNNING` (a device plays),
`LOST` (the device disappeared and the main thread renders until it reopens), `FAILED` (the page
could not start its audio) or `SUSPENDED` (the page is hidden).

**Captures** tap the core where it renders, so they work with every transport: the null
transport copies the frames it renders on the main thread; the native transport hands the audio
thread a buffer through the command ring, which the audio thread fills after each block and
reports full with an atomic flag, so the main thread takes it back without a lock; the worklet
records its quanta and posts the frames back over the port. The load is the time a render takes
over the time it renders, smoothed; the native audio thread and the null transport measure it,
the worklet does not (it has no clock finer than a millisecond) and reports 0. Every transport frees the frames of
a sound itself after `SOUND_FREE`; the main thread keeps the last 4096 events until
`read_events`.

**Meters** travel back from where the core renders without a lock, and each call of `meters`
takes what was measured since the last one (see [Meters of the core](#meters-of-the-core)):

- null: the main thread reads the core directly;
- native: after each callback the audio thread reads the meters of its core and adds them to
  atomic words per bus, the peaks raised with a compare and swap of their bits, the sums of
  squares added the same way and the frames counted up; the main thread swaps each word with 0.
  Every value is consistent on its own, and nothing is lost between two reads;
- web: every 4 quanta the worklet reads the meters of its core and, while any bus had a signal
  within the last 350 ms, posts `{ type: 'meters', peaks, squares, frames, reduction }`; the page
  accumulates them in `Module.boxAudio.meters` until the transport takes them, and counts them in
  `Module.boxAudio.metersReceived`.

They follow the clock of the output: what does not render reports nothing, and `box.audio`
shows silence while the page waits for a gesture, is suspended or failed.

**The clock** of a transport is the frame of the core that is being rendered now, as far as the
main thread can tell, the frame a command with a `when` is scheduled against:

- null: the frames rendered so far, `box_audio_core_clock` of its core;
- native: a 64 bit count that the audio thread publishes atomically after each callback (and
  the main thread while the device is lost);
- web: every 16 quanta the worklet posts the clock of its core with the `currentFrame` of its
  global scope after the quantum, and the page extrapolates it from `AudioContext.currentTime`,
  at most 32 quanta ahead of the report and never backward, so that it moves between messages.
  `Module.boxAudio.clock()` returns it, and the `stats` of the worklet include `clock`.

The clock stops while the output does not run: waiting for a gesture, suspended or off. A new
output (a new serial) has a new core, whose clock starts at 0; on the web the core of the
worklet lives as long as the page, so a second world of the same page starts at its clock.
`AudioStatus.clock` holds it once per frame, and `box_audio_module_clock` (`module.h`) reads it
for the parts of `box.audio` that schedule ahead.

**Where the output opens.** A world that imports box opens nothing: thousands of tests import
it and must not touch audio hardware. `box_app_run` opens the output of its world
(`src/audio/output.c`), as the singleton `box.app.AudioTransport`, which the system
`AudioTransportUpdate` updates in `OnStore` with the delta of the frame. Every open gets a new
serial, which tells `box.audio` to send its sounds and state again:

- a real app opens the default device, and the null transport when there is none;
- `--headless` opens no device and runs the null transport;
- `--no-audio` opens nothing, and the state is `OFF`.

**No device is not an error.** The pm containers and CI have no audio device. A failed open
logs one warning, `box.audio: no audio device (...), the app runs without sound`, never an
error, because the e2e runs fail on a logged error, and the app goes on with the null
transport and the state `NO_DEVICE`.

**The null transport** renders `floor(time x rate)` frames in total, where time is the sum of
the frame deltas, at most one second per frame. With a fixed `--delta-time` every run renders
the same frames at the same block boundaries, so the audio is bit identical in every run, which
the capture of the audio module depends on. Commands apply at once and take effect at the
start of the next render.

## Native output

`src/audio/native.c` is the only file that includes the device part of miniaudio, bundled
header only in `project.json` (tag `0.11.26`); `src/audio/decode.c` includes its decoders
alone, on every platform. It uses the device API alone, built with
`MA_NO_ENGINE`, `MA_NO_NODE_GRAPH`, `MA_NO_RESOURCE_MANAGER`, `MA_NO_DECODING`,
`MA_NO_ENCODING`, `MA_NO_GENERATION` and `MA_NO_NULL` (so a machine without audio fails the
open instead of playing into the silent null backend of miniaudio). Mixing, panning and
resampling are the core's. The device is opened for 32 bit float stereo at the native rate of
the device, which becomes the rate of the core, with `noFixedSizedCallback`, so the callback
renders any frame count the device asks for, in blocks of 512 frames.

**Two rings.** The main thread and the device callback share two single producer single
consumer rings of bytes (`src/audio/ring.c`) with C11 atomics: commands and sounds to the audio
thread (64 KB), events back (4096 events). Each side owns one index, loads the other with
acquire and stores its own with release; a write is all or nothing. `src/render/ring.c` is not
a model for them: it takes a lock.

- A command batch is split at command boundaries into messages of at most 4 KB. A sound is a
  message with a pointer to frames the main thread copied and owns.
- The messages first go to a queue of the main thread, which writes as many as fit into the
  ring. A full ring keeps the rest on the main thread for the next frame; neither side blocks.
- The callback takes every message the ring holds, applies it, renders, and moves the events
  of the core into the event ring as far as it has room; the rest wait in the core. A sound the
  core refuses comes back as an internal event, so the main thread frees it. A sound waits in
  the ring while the event ring has no room for that refusal.
- The callback never allocates, locks, logs or waits.

**Thread priority.** The audio thread gets a real time priority through miniaudio
(`ma_context_config.threadPriority`) and nowhere else. It counts against
`BOX_JOBS_RESERVED_CORES`, which keeps 3 cores free: the main thread, the render thread and the
audio thread; the job pool takes the rest.

**A device that disappears**, such as unplugged headphones, does not stop the app. miniaudio
follows a change of the default device itself where the backend supports it (reported as
`rerouted`). A stop the main thread did not ask for marks the device lost: the next frame
closes it, which joins its thread, logs one warning, and the main thread takes over the rings
and renders the deltas like the null transport, so voices end and sounds are freed. Every 2
seconds it tries to reopen the default device at the rate of the core; miniaudio converts when
the new device has another rate.

**Linking.** Runtime linking stays on: miniaudio loads the libraries of its backends
(PulseAudio, ALSA, JACK on Linux; CoreAudio on macOS) at run time, so there are no new link
flags. A notarized macOS build, whose hardened runtime may refuse to load them, builds
`native.c` with `-DMA_NO_RUNTIME_LINKING` and links `-framework CoreFoundation -framework
CoreAudio -framework AudioToolbox`, or keeps runtime linking and signs with the entitlements
the miniaudio documentation lists for macOS.

The web does not compile the device part: `native.c` compiles to a stub there, and only the
decoders of `decode.c` use the bundle.

The tests are `Ring`, `Transport` and `NoDevice` in `test/cpu/audio`, which need no device.

## Two threads on the web

The web build has no threads: there is no render thread and jobs run inline, so the main thread
runs every frame. Audio rendered on the main thread would glitch on every frame stall. The core
therefore runs in an AudioWorklet, on the audio rendering thread of the browser:

- **Main thread**: the main wasm module (box, flecs, the app). `src/audio/platform_web.c`
  creates the `AudioContext`, loads the worklet, starts the core with `AudioContext.sampleRate`
  (44100 and 48000 both occur, it never assumes one) and sends commands to it.
- **Audio thread**: an `AudioWorkletProcessor` that owns a second, small wasm instance of the
  core. Every `process` call renders one quantum of 128 frames into static buffers of the
  core's memory and copies them to the outputs.

The two talk only over the `MessagePort` of the `AudioWorkletNode`, with plain objects:

- `{ type: 'commands', data }`: a batch of commands of the core as an `ArrayBuffer`, which is
  transferred. The worklet copies it into the heap of its core and applies it between quanta.
- `{ type: 'sound', id, frames, channels, sampleRate, data }`: the frames of a sound as an
  `ArrayBuffer`, transferred. The worklet copies them into the heap of its core and keeps the
  pointer; the reply `{ type: 'sound', id, ok }` says whether the core took it.
- `{ type: 'effect', slot, floats }`: the memory of an effect slot. The worklet allocates
  `floats` cleared floats in the heap of its core and gives them to the slot, or larger memory
  for a slot that has less, after which it frees the old one, which the core let go of at once;
  the reply `{ type: 'effect', slot, ok }` says whether the slot has the memory, and
  `Module.boxAudio.effects` keeps the last reply of each slot.
- `{ type: 'events', events }` from the worklet: the events of the core after a quantum that
  had any. The worklet reads until the core has none left, 64 per call, so no backlog builds
  up across quanta. On `SOUND_FREE` the worklet has already freed the frames in its heap.
- `{ type: 'chunk', stream, frames, last, data }`: a chunk of a stream as a transferred
  `ArrayBuffer`. The worklet copies it into the heap of its core, keeps the pointers of each
  stream in order and frees the consumed ones after each quantum, all of them when the stream
  closed.
- `{ type: 'stream', status }` from the worklet: the status of a stream when it changed, and
  every 8 quanta while it plays; `Module.boxAudio.streams` keeps the last of each.
- `{ type: 'stats', id }` asks the worklet for its quantum count, clock, voices, sounds,
  memory, the events the core lost since the last `LOST` was read, the chunks it holds and the
  streams, which the browser checks use.

The web transport of `transport.c` sends with these functions and reads the events from
`Module.boxAudio.queue`, which keeps up to 4096 events until the main module takes them.

In the page, `Module.boxAudio.commands(bytes)` sends a batch, `Module.boxAudio.sound(id,
samples, channels, rate)` a sound (a promise of whether the core took it),
`Module.boxAudio.events` keeps the last events and `Module.boxAudio.onevents` is called with
each new list; `Module.boxAudio.stats()` returns a promise of the stats. From C,
`box_audio_web_apply` and `box_audio_web_sound` of `src/audio/platform.h` copy the bytes out of
the main module and send them. Messages sent before the worklet has loaded wait in the page and
are sent once the node exists.

The heap of the core grows: allocating a batch or a sound can grow the wasm memory, which
detaches the `ArrayBuffer` of the old memory, so the worklet recreates its views of the channel
and event buffers after every allocation.

A stall of the main thread does not stop the worklet: the browser keeps calling `process`, and
only the commands of the stalled frames arrive late.

## Why no shared memory

Emscripten can run a worklet on the shared memory of the main module (`-sAUDIO_WORKLET
-sWASM_WORKERS`), but that does not link with this repository today. With emcc 6.0.8 against the
wasm `libresvg.a`:

- without `-pthread`: `undefined symbol: pthread_mutex_destroy` and more, from the Rust `std` in
  `libresvg.a`;
- with `-pthread`: `--shared-memory is disallowed by resvg-...rcgu.o because it was not compiled
  with 'atomics' or 'bulk-memory' features.`

Fixing that takes a nightly Rust with `-Zbuild-std` in bake3 and on every runner, and shared
memory requires COOP/COEP headers on every host that serves a box page. A separate module needs
neither: the page works with `crossOriginIsolated` false and without `SharedArrayBuffer`.

The device part of miniaudio is not used on the web: its web backend forbids `-std=c*` flags,
while box sets `c11`, and its worklet mode needs `-sASYNCIFY` on the whole binary. Its decoders
are, see [Sound files](#sound-files).

## How the core is built and delivered

The `rules` of `project.json` run `tools/audio_core.py` for `src/audio/core/core.c` before box
compiles, on every build of box. For a target other than Emscripten the script does nothing. For
`--target em` it compiles the C files of `src/audio/core` with the clang of Emscripten (the
`LLVM_ROOT` of `em-config`), every `.c` of the folder:

```sh
clang --target=wasm32-unknown-unknown -O2 -msimd128 -std=c11 -ffreestanding -nostdlib \
    -DBOX_AUDIO_CORE_WASM -Wl,--no-entry -Wl,-z,stack-size=65536 \
    -Wl,--initial-memory=1048576 -Wl,--max-memory=268435456 -Wl,--strip-all \
    -Wl,--export=box_audio_core_wasm_init ... command.c core.c dynamics.c effect.c eq.c filter.c reverb.c source.c synth.c synth_patch.c wasm.c \
    -o box_audio_core.wasm
```

into `box_audio_core.wasm` in the generated folder of the build: a freestanding module of a
few KB with its own memory, which starts at 1 MB and grows up to 256 MB, no imports (the core
needs no libm) and the exports of `src/audio/core/wasm.c`: `box_audio_core_wasm_init`, a first
fit heap with `_alloc` and `_free` that grows the memory, `_sound`,
`_effect`, `_apply`, `_render`, `_left`, `_right`, `_read_events`, `_events`, `_events_lost` and `_voices`. It is not built
with `emcc`: a standalone wasm of `emcc` links start code and libc from the Emscripten cache,
which the core does not use, and building those for a new configuration fails when the cache
folder is a symlink. The script writes the bytes as
a C array to `.bake/generated/box_audio_core_wasm.h` in the box folder, which
`src/audio/platform_web.c` includes; bake3 puts the generated folder of a build on the include
path of test harnesses only, so the header needs a fixed path. The core is built with `-O2` in
every configuration and the header is rewritten only when its bytes change, so the build is the
same for every local env; a stamp skips the compile while the sources are older than it.

So the core travels inside the main module and the page needs no extra file: every app that
uses box gets it without setup, whatever its shell. At start, `platform_web.c` copies the bytes
into an `ArrayBuffer`, makes the worklet script from a Blob URL and creates the node with the
bytes and the sample rate in `processorOptions`. The processor compiles the bytes synchronously
in its constructor (`new WebAssembly.Module`, then `new WebAssembly.Instance`), before any sound
plays. Bytes rather than a compiled `WebAssembly.Module` are passed because they clone into a
worklet in every browser, Safari included.

Nothing is committed in binary form: a clean tree builds the page with

```sh
bake3 --target em --local-env=<name> apps/boxrun
```

## Starting and failing

Browsers only start audio after a user gesture. The context is created suspended when the
app opens its audio output, before the first frame, so that the sounds and the commands of that
frame reach the worklet (they wait in the page while it loads). The key and mouse button
handlers of the canvas
(`box_web_callbacks` in `src/input/platform_web.c`) resume it on the first key or button event.
Until then the app runs normally, silently, and logs nothing; `AudioStatus.state` is
`AudioWaitingForGesture`, and `box.audio` drops the plays that do not loop (see
[Before the gesture](#before-the-gesture)).

**A hidden page** gets no animation frames, so no commands arrive and looping sounds would never
end. On `visibilitychange` to hidden the page ramps a gain node after the worklet to 0 over
50 ms and then suspends the context (`AudioSuspended`); when it shows again it resumes the
context, if it was running, and ramps the gain back to 1. `Module.boxAudio.suspends` and
`visibilityResumes` count both. Native apps keep playing when they lose the focus.

A failure to create audio, a browser without Web Audio or without `AudioWorklet`, a worklet that
does not load or a core that does not start in it, logs one warning
(`box.audio: ..., the app runs without sound`) and the app keeps running without sound.

## Playing a sound by hand

Apps play through the `box.audio` module. To hear the core alone, build the page, serve its
folder and open it with a scene that renders, then click the canvas:

```sh
bake3 --target em --local-env=audio apps/boxrun
BIN="$(tools/integration.py local-env --env audio)/wasm32-Emscripten/debug/bin"
mkdir -p "$BIN/test/integration/web"
cp test/integration/web/audio_core.flecs "$BIN/test/integration/web/"
python3 -m http.server 8080 -d "$BIN"
```

Open
`http://localhost:8080/boxrun.html?preload=/test/integration/web/audio_core.flecs#/test/integration/web/audio_core.flecs`
(`localhost` is a secure context, which `AudioWorklet` requires) and click the canvas. In the
developer console, a sound and a play command make a 440 Hz tone:

```js
const a = Module.boxAudio, rate = a.ctx.sampleRate, s = new Float32Array(rate);
for (let i = 0; i < rate; i++) s[i] = 0.25 * Math.sin(2 * Math.PI * 440 * i / rate);
await a.sound(1, s, 1, rate);
const v = new DataView(new ArrayBuffer(40));
v.setUint16(0, 1, true); v.setUint16(2, 40, true); v.setUint32(12, 1, true);
v.setUint32(16, 1, true); v.setFloat32(24, 1, true); v.setFloat32(32, 1, true);
v.setUint32(36, 1, true);
a.commands(v.buffer);
```

The bytes are a `PLAY`: the header at 0, `when_lo` and `when_hi` at 4 and 8 (0, so it plays
now), the voice at 12, the sound at 16, the bus at 20, the volume at 24, the pan at 28, the
pitch at 32 and the flags at 36 (looping). `a.clock()` is the frame of the core that renders
now; a `when` of `a.clock() + rate` in the two words starts the tone a second later.

The automated check of Chrome is `test/integration/web/audio_core.mjs`, and of Safari
`test/integration/web/audio_core_safari.mjs`, which drives real Safari through `safaridriver`
on macOS (see `test/integration/web/README.md`):

```sh
node test/integration/web/audio_core_safari.mjs "$BIN"
```

Safari automation has to be enabled once on the host, by an admin, with
`sudo safaridriver --enable` and "Allow remote automation" in the Developer section of Safari
Settings (Advanced > "Show features for web developers" shows it), and needs a logged-in GUI
session; without it the check stops with exit status 2 and says so. Where it cannot run, check
Safari by hand: open the same address in Safari on macOS, click the canvas, run the snippet and
expect the tone and no `box.audio` warning in the console.

`Module.boxAudio.gestures` counts the key and mouse button events that reached the audio and
`Module.boxAudio.resumes` the calls of `ctx.resume()` they made, which tells an input that never
arrives from a resume the browser refuses.

## The box.audio module

`include/box/audio.h`, in `src/audio/audio.c` and `src/audio/sync.c`, makes audio data driven:
components that a script, the REST API or the `set` command write, sent to the core once per
frame, the same on every platform.

| Component | What it is |
|-----------|------------|
| `AudioPcm` | The samples of a sound (`samples`, `channels`, `sample_rate`, 0 for the output rate); `box_audio_sound_from_pcm` sets it from C |
| `SoundFile` | A sound decoded from a WAV or FLAC file (`path`), see [Sound files](#sound-files) |
| `SoundStatus` | Read only: `state` (`SoundLoading`, `SoundReady`, `SoundFailed`), `frames`, `channels`, `sample_rate` and `error` of a `SoundFile` |
| `AudioBus` | A mixer channel: `output` (0 or `master` for the master), `volume` (1), `pan`, `width` (1), `mute`, `solo`, `kind` (`AudioChannelGroup`, `AudioChannelReturn` or `AudioChannelSfx`), `sends`, up to 4 `AudioSend` of `bus`, `level` and `pre_fader`, and `effects`, the effect entities of its 4 insert slots (0 for empty); see [Mixer channels](#mixer-channels) |
| `Reverb` | An effect, a reverb in the insert slot of a bus that names its entity: `decay` (1.5 s), `damping` (0.5), `predelay` (0.02 s), `size` (0.5), `mix` (1) |
| `Equalizer` | An effect, an equalizer in the insert slot of a bus that names its entity: `bands`, 4 `AudioEqBand` of `kind` (`AudioEqOff`, `AudioEqPeak`, `AudioEqLowShelf`, `AudioEqHighShelf`, `AudioEqLowCut`, `AudioEqHighCut`; off), `frequency` (100, 400, 2000 and 8000 Hz; 10 Hz..0.49 of the rate), `gain` (0 dB, -24..24) and `q` (0.7071, 0.1..18); see [Equalizer](#equalizer) |
| `AudioFilter` | An effect, a filter in the insert slot of a bus that names its entity: `kind` (`SynthLowPass`, `SynthHighPass`, `SynthBandPass`, `SynthNotch`), `cutoff` (1000 Hz, 10 Hz..0.49 of the rate), `resonance` (0, 0..1), `slope` (`SynthSlope12`, `SynthSlope24`) and `mix` (1); see [Filter effect](#filter-effect) |
| `Compressor` | An effect, a compressor in the insert slot of a bus that names its entity: `threshold` (-18 dB, -80..0), `ratio` (4, 1..20), `attack` (0.01 s), `release` (0.15 s), `knee` (6 dB, 0..24), `makeup` (0 dB, 0..24), `mix` (1) and `sidechain` (0 for its own input, or the channel it ducks under); see [Compressor](#compressor) |
| `Gate` | An effect, a gate in the insert slot of a bus that names its entity: `threshold` (-50 dB, -100..0), `attack` (0.001 s), `hold` (0.05 s, 0..5), `release` (0.1 s) and `range` (-80 dB, -100..0, the gain when closed); see [Gate](#gate) |
| `Delay` | An effect, a delay in the insert slot of a bus that names its entity: `time` (0.25 s, 0..4), `sync` (0 beats; when positive the time is `sync * 60 / tempo` of the `Transport`), `feedback` (0.35, 0..0.95), `mix` (0.3), `ping_pong` (false) and `damping` (6000 Hz); see [Delay](#delay) |
| `AudioOutput` | The bus a source, or a sound by default, plays on |
| `AudioSource` | A sound that plays while the entity exists: `sound`, `volume` (1), `pitch` (1), `loop`, `playing` (true) |
| `AudioStream` | Music: an Ogg Vorbis file (`path`) that streams, `volume` (1), `loop` (true), `playing` (true), see [Music](#music) |
| `AudioStreamStatus` | Read only: `state` (`AudioStreamLoading`, `AudioStreamPlaying`, `AudioStreamStopped`, `AudioStreamEnded`, `AudioStreamFailed`), `frames`, `channels`, `sample_rate`, `position`, `buffered`, `underruns`, `decode_ms`, `decode_max_ms` and `error` |
| `SynthSound` | A sound made by the synth from its layers, see [Synth sounds](#synth-sounds) |
| `SynthUnison` | Unison voices of a synth layer: `voices` (3), `detune` (10 cents), `width` (0), see [Unison and width](#unison-and-width) |
| `SynthGlide` | Portamento and mono legato of a synth sound, see [Glide and legato](#glide-and-legato) |
| `SynthLfo` | An LFO of a synth sound, see [LFOs](#lfos) |
| `AudioSpatial` | Places a source in the world: `min_distance` (1), `max_distance` (50), `rolloff` (1), see [Spatial audio](#spatial-audio) |
| `AudioListener` | Tag of the entity whose `WorldTransform` hears the spatial sources |
| `AudioSettings` | Singleton: `master_volume` (1) and `mute` |
| `AudioStatus` | Singleton, read only: `state`, `sample_rate`, `voices`, `voices_stolen`, `voices_refused` (plays the voice cap refused because every voice ranked higher), `load`, `buses`, `clock` (the frame of the core that renders now) |
| `AudioMeter` | Read only, opt-in, on a channel, the master or an effect: `peak_left` and `peak_right` (linear, the largest absolute sample since the last frame), `rms_left` and `rms_right` (linear, over the last 300 ms of output) and `reduction` (dB, the gain reduction of a dynamics effect, 0 for others); see [Meters](#meters) |
| `Transport` | Singleton: `tempo` (120, 20..999), `beats_per_bar` (4), `beat_unit` (4), `position` (beats, written while it plays, clamped to ±2^31 like `loop_start` and `loop_end`), `playing` (false), `loop` (false), `loop_start`, `loop_end`, `lookahead` (0.15 s, 0.05..0.5), `metronome` (false), see [Transport](#transport) |
| `AudioQuantize` | Enum of the `quantize` of `box.audio.play_quantized`: `AudioQuantizeNone` (at once), `AudioQuantizeBeat`, `AudioQuantizeBar`; `None`, `Beat` and `Bar` in a script, see [Quantized plays](#quantized-plays) |
| `Instrument` | A synth or a sampler that plays notes: `sound`, `root` (69, A4), `velocity_curve` (2), `gate` (true), `release` (0.05 s, a sampler's fade), see [Instruments](#instruments) |
| `Note` | A note of a `NoteClip`: `start` (beats from the start of the clip), `duration` (beats), `pitch` (a MIDI note number), `velocity` (1, 0..1), `pan` (0, -1..1) |
| `NoteClip` | A reusable clip: `length` (4 beats, the length of a pass of a loop), `notes` (a vector of `Note`, in any order), see [Clips and tracks](#clips-and-tracks) |
| `AudioClip` | A reusable clip of audio: `sound` (an entity with `AudioPcm` or `SoundFile`), `offset` (0 seconds into the sound), `length` (4 beats, the length of a pass), see [Audio clips](#audio-clips) |
| `Track` | A track: `instrument` (the entity with `Instrument` of its note clips, none for a track of audio clips), `priority` (1, 0..255: the priority of the voices of its notes and audio clips at the voice cap, see [Clips and tracks](#clips-and-tracks)); brings an `AudioBus` with the `With` trait, so it has a volume, a pan, a mute, an output and sends |
| `ClipPlacement` | On a child of a `Track`: `clip` (a `NoteClip` or an `AudioClip`), `start` (beats on the timeline), `duration` (0, the length of the clip minus the offset), `loop` (false), `offset` (beats into the clip), `gain` (1), `transpose` (0 semitones; the pitch ratio `2^(transpose / 12)` of an audio clip) |

Constructors give the defaults in parentheses, so `AudioSource: {sound: click}` in a script plays
at unity volume and pitch. From C, `ecs_set` writes every member; use `ecs_ensure` to keep the
defaults.

```
music { AudioBus: {volume: 0.5} }
sfx { AudioBus: {output: music} }
hum { AudioSource: {sound: hum_pcm, loop: true}  AudioOutput: {bus: sfx} }
const played: bool = box.audio.play(click, 0.8)
```

`box_audio_play(world, sound, volume)`, in scripts `box.audio.play(sound, volume)`, plays a
sound once on the bus of its `AudioOutput`, without an entity per play; there is no control of a
voice after it started. `box_audio_play_at(world, sound, position, volume)`, in scripts
`box.audio.play_at(sound, {x, y, z}, volume)`, plays any of them once at a place in the world
(see [Spatial audio](#spatial-audio)). `box_audio_play_quantized(world, sound, volume,
quantize)`, in scripts `box.audio.play_quantized(sound, volume, quantize)`, plays it once on the
next beat or bar of the transport (see [Quantized plays](#quantized-plays)).
`box_audio_play_note(world, instrument, pitch, velocity, duration)`, in scripts
`box.audio.play_note(instrument, pitch, velocity, duration)`, plays a note of an
[instrument](#instruments). No ad-hoc play, quantized or not, nor note is affected by the
transport: a stop or a seek never stops or moves a one shot
that started or is scheduled. `box_audio_capture(world, seconds, path)`, in scripts
`box.audio.capture(seconds, path)`, records the master output to a WAV file.

**One batch per frame.** The system `AudioSync` runs in `PreStore`, after the transforms and
before the output renders in `OnStore`. It remembers what it sent and writes a command only for
what differs, all into one batch, and sends nothing in a frame without changes. Its order:
the events of the output (ended voices, steals, freed sounds), the listener, the buses, the
order, the master settings, new sounds, the sources, the spatial sources and the one shots of
`play_at` when the listener moved, then the plays, the stops of deleted sources and the
releases of sounds, the notes of `play_note`, then the [transport](#transport): its window and the metronome, and last
the quantized plays, which need the timeline of the frame. A world without audio components pays the empty queries.

**Buses** get an index of the core, up to 63 besides the master; more log one error and are
silent. The order is compiled whenever a bus appears, disappears or changes its output or sends:
a depth first walk computes the longest path of each bus to the master and finds cycles; the
deepest buses come first, the master last. A cycle through outputs and sends logs one error that
names the buses on it and keeps the last valid order, which the core keeps playing. A deleted
bus, or an output or send that names no bus, routes to the master: its sources and one shots are
moved to the master and the index is reused a frame later. A Disabled bus is muted.

### Mixer channels

`AudioBus` is the component of every channel of a mix: a group, a return, an SFX bus, the bus of
a track and the master. `kind` tells the solo what a channel is: `AudioChannelGroup` (the
default) for groups and other buses, `AudioChannelReturn` for returns such as a reverb bus and
`AudioChannelSfx` for the buses of the game sounds. A track is identified by its `Track` and the
master by `box.audio.master`. A script cannot declare an entity by a path, so it sets the master's
`AudioBus` in the scopes `box { audio { master { ... } } }`. The module keeps the master out of
the script, so a reload of the scene does not delete it.

```
using box.audio

box { audio { master { AudioBus: {width: 0.9} } } }
hall { AudioBus: {output: master, kind: AudioChannelReturn, effects: [hall]}; Reverb: {decay: 2} }
sfx { AudioBus: {output: master, kind: AudioChannelSfx} }
band { AudioBus: {output: master, volume: 0.6} }
lead { AudioBus: {output: band, solo: true, sends: [{bus: hall, level: 0.3}]} }
pad {
  AudioBus: {output: band, width: 1.5, sends: [{bus: hall, level: 0.5, pre_fader: true}]}
}
```

- **Width** (0..2, default 1) is a mid/side gain after the inserts of the channel and before
  its pan: 0 is mono, 1 as is, 2 doubles the side.
- **Sends**: up to `BOX_AUDIO_SENDS` (4) per channel, each with a `bus`, a `level` (0..4) and
  `pre_fader`. A post-fader send taps after the volume and pan, a pre-fader one before them; the
  mute and the solo silence both. More than 4 log one error that names the channel and the first
  4 play. A send to no bus, or to a deleted one, does nothing, and a cycle through any send logs
  the error of a cycle through outputs.
- **Solo** is computed by the module and sent as part of the mute, so it ramps. When any channel
  is soloed, a channel is heard when it is soloed, is on the output path of a soloed channel,
  feeds a soloed channel (the tracks of a soloed group), is a return (solo safe, so the reverb of
  a soloed track is heard), is an SFX bus (solo is a tool of the music mix and never silences the
  game), or is downstream of one of these. `mute` always wins, and a Disabled channel is muted
  and does not solo.
- **The master** may have an `AudioBus` for its pan and width. Its volume multiplies
  `AudioSettings.master_volume` and its mute adds to `AudioSettings.mute`; its output, sends,
  solo and kind are ignored.

A width of 1 and unused sends add no work per frame, and a world without a solo pays one check
of a mask.

**Effects.** An effect is an entity with an effect component: `Reverb`, `Equalizer`,
`AudioFilter`, `Compressor`, `Gate` or `Delay` (the first of them in that order when it has several). It runs in an insert slot
of a channel that names it in `effects`, an array of 4 entities that run in order; an empty slot
(0) is skipped. An effect that no slot names does nothing. The channel may name
itself, which is the usual reverb return, and unloading the scene deletes it, which fades its
tail out:

```
hall { AudioBus: {output: master, kind: AudioChannelReturn, effects: [hall]}; Reverb: {decay: 2.4, damping: 0.4, predelay: 0.02} }
world { AudioBus: {output: master, sends: [{bus: hall, level: 0.3}]} }
```

An effect may also be an entity of its own, which a channel names in any slot, so several of a
kind run at once: `band { AudioBus: {effects: [0, 0, band_verb]} }`. Components on one line are
separated by `;` (or a newline): flecs script does not take a comma there. `master` is
`box.audio.master`, an entity without components that names the master; its `AudioBus` may name
effects too, which run before the limiter.

- An effect gets a slot of the pool of the core and the module asks the transport for the
  memory of its kind before the first `EFFECT` command; it sends the command again when the kind,
  the channel, the slot or a parameter changes, so a move between slots or channels fades.
- Deleting an effect entity, taking it out of its slot or removing its component takes it off
  with its tail. A Disabled effect entity keeps its slot and is bypassed, with the fade of a
  removal; enabling it fades it back in.
- An entity in two slots logs one error that names it and both channels (`box.audio: the effect
  '...' is in an insert slot of '...' and of '...', the slot of '...' stays empty`), and the
  second slot stays empty.
- More than `BOX_AUDIO_EFFECT_MAX` (64) effects in all channels log one error that names the
  effect (`box.audio: more than 64 effects, '...' does nothing`), which does nothing until a slot
  of the pool is free.

- A `Compressor` with a `sidechain` keys on the output of that channel. When the module compiles
  the order it adds an edge from the sidechain to the compressed channel, so the sidechain runs
  first even where its depth alone would put it later. A sidechain that would form a cycle, such
  as a channel that outputs into its own sidechain, logs one error that names the compressor,
  its channel and the sidechain (`box.audio: the compressor '...' on '...' has the sidechain
  '...', which runs after '...', a cycle: '...' keys on its own input`); that sidechain is
  ignored and the compressor keys on its own input. So does a compressor whose sidechain is the
  master, a deleted channel or an entity that is no channel, the latter two without an error.
  A change of the sidechain recompiles the order. Without a `Compressor` the module pays one
  count of its component per frame.

Reverb zones, rooms from geometry and a delay are later work.

#### Meters

A mix is seen as well as heard: a channel with `AudioMeter` gets its levels every frame, for a
mixer UI, a test or an agent that checks a scene without listening.

```
using box.audio

music { AudioBus: {volume: 0.5}; AudioMeter: {} }
box { audio { master { AudioMeter: {} } } }
```

- `peak_left` and `peak_right` are the largest absolute samples since the last frame, linear;
  `rms_left` and `rms_right` the RMS over the last `BOX_AUDIO_METER_WINDOW` (0.3) seconds of
  output, which the module computes from the sums of squares of the core. A sine of peak 0.5
  reads a peak of 0.5 and an RMS of 0.354.
- A channel is measured at its output, after its inserts, width, pan, volume and mute: what it
  sends on to its output. A group reads the sum of its inputs. A muted channel reads 0 once its
  ramp ends, and its RMS 300 ms later.
- The master reads its signal after its volume (`AudioSettings.master_volume` times the volume
  of its `AudioBus`) and **before the limiter**, so it reads over 1 when the mix is too loud,
  while the output stays under 0.95.
- An effect entity with `AudioMeter` gets `reduction`, the largest gain reduction in dB since
  the last frame, of the dynamics effects (`Compressor` and `Gate`); the other kinds read 0. A
  channel that names itself as an effect gets both.
- Metering is opt-in: the module reads the meters of the transport every frame, but writes the
  component only on the entities that have it, so a scene that never looks pays no component
  writes and no change detection fires. `audio` prints the levels of every channel anyway.
- The levels follow the clock of the output: a page that waits for a gesture, is suspended or
  failed reads silence, as does a world without output. The sync reads what rendered before it,
  so the levels lag the commands of a frame: with the null transport, which renders after the
  sync, a change shows in the meter two frames later.

**Sources** start a voice when `playing` is true and their sound is known, and stop it when
`playing` goes false, the sound or `loop` changes or the entity is deleted, which fades it out
over the ramp of the core. A sound that does not loop sets `playing` to false when it ends. A
Disabled source keeps its voice at volume 0. Prefabs do not play; their instances get their own
copy of `AudioSource` and play on their own, as `ParticleEmitter` instances do.

**Sounds** are sent to the output once, with an id of the core; a change of `AudioPcm` releases
the old id and sends the new samples, and a deleted sound is released, freed by the side that
owns the frames once no voice plays it.

### Spatial audio

`src/audio/spatial.c` places sounds in the world with a simple model: a sound gets quieter with
its distance from the listener and pans with its direction. There is no Doppler, no distance
filter, no occlusion and no HRTF yet. All of it runs in `AudioSync` on the main thread, which
sends the result as the volume and the pan of the voice, so the core knows nothing of it and the
web plays as native does.

```
ears { Position: {0, 1.7, 0}  AudioListener }
engine {
  Position: {4, 0, -10}
  AudioSource: {sound: hum, loop: true}
  AudioSpatial: {min_distance: 2, max_distance: 40}
}
const shot: bool = box.audio.play_at(bang, {-3, 0, -5}, 1.0)
```

**The listener** is the entity with `AudioListener` and a `WorldTransform`. Without one it is the
camera of the first enabled `View` that has a camera with a `WorldTransform`, first in render
order (`box_view_compare`), so a game view listens before the overlays of the editor and the
console. Several `AudioListener` entities use the one with the lowest id and log one warning,
`box.audio: <n> entities have AudioListener, '<path>' listens`, again only after the count
dropped to one. To make a character listen while the camera follows it, tag the character,
or a child at the height of its head; to make the camera listen, give it nothing, unless the
scene renders to textures: their views come first in render order, so tag the camera of the main
view, as the dust merchant does. Without a listener a spatial source plays centered at full
volume, beyond `max_distance` too, and nothing is logged.

**A source** is an `AudioSource` with `AudioSpatial`. With a `WorldTransform` it is heard at the
translation of that transform, which is the world position of the entity: a child of a moving
parent follows the parent, ChildOf or `Parent` alike, its own `Position` an offset in the space
of the parent; the rotation and scale of the source do not matter. A source with `AudioSpatial`
and no `WorldTransform` plays as one without it. On a sound entity, `AudioSpatial` gives the
distances of the plays of `box_audio_play_at`, the defaults without one. A one shot of
`play_at` stays at the position it started at, while the listener moves.

**Distance.** Within `min_distance` the gain is 1. Beyond it the gain is the inverse distance
`g(d) = min / (min + rolloff (d - min))`, faded so that it reaches exactly 0 at `max_distance`:
`(g(d) - g(max)) / (1 - g(max))`. A `rolloff` of 0 keeps full volume up to `max_distance` and is
silent from there. A `min_distance` of 0 counts as 0.001. The volume of the voice is the
`volume` of the source times this gain.

**Pan.** The pan is the component of the direction to the source along the right axis of the
listener (the first column of its `WorldTransform`, its +X), over the distance: 1 straight to the
right, -1 to the left, 0 in front, behind, above and below. The core turns it into equal power
gains; a stereo voice, such as a synth sound with unison width, takes them as a balance of its
two channels, see [Unison and width](#unison-and-width). **Front and back sound alike** in this version: a source behind the listener is as loud as
the same source in front. Within `min_distance` the pan narrows to the center (the component is
divided by `min_distance` instead of the distance), so a source at the listener's position is
centered, with no division by zero, and one that passes through the listener sweeps across
instead of jumping from side to side.

**Out of range.** A looping source at or beyond `max_distance` uses no voice: the module stops
it, with the fade of a stop, and keeps the time of the sound running on the main thread from the
time it started, at its pitch. Back in range it starts a new voice with a `POSITION` where the
sound would be had it kept playing. A source that does not loop and a `play_at` beyond
`max_distance` do not start (the source's `playing` goes false); a source that does not loop and
leaves the range while it plays keeps its voice, silent, until it ends.

**Smoothing.** The gain and the pan are computed once per frame, after the transforms, and the
ramps of the core move the voice to them over 5 ms, so a fast source or a camera that jumps does
not click. During a stall on the web no command arrives and the voice holds the last target and
keeps playing.

**Cost.** The spatial sources have a query of their own with change detection: a frame updates
the sources of the tables that changed (a new source, a changed `AudioSource` or `AudioSpatial`,
a dynamic transform, which its systems write every frame) and the sources whose static
transform moved, which box.transform reports (`box_transform_on_moved`). When the listener moves,
or the output, the sounds, the buses or the gesture state change, every spatial source and every
`play_at` one shot that still plays is updated. A frame allocates nothing once its vectors have
grown. `bench/audio` (`Spatial`) runs 1000 looping sources, 32 of them in range, with a still
listener, a moving listener and 10 moving sources, against the same world without sources.

**Disabled** works as for other sources: a Disabled spatial source keeps its voice at volume 0,
or none when it is out of range.

### Sound files

```
using box.audio
click { SoundFile: {path: "etc/assets/sounds/click.wav"} }
hum { SoundFile: {path: "etc/assets/sounds/hum.flac"} }
hum_source { AudioSource: {sound: hum, loop: true} }
```

`SoundFile` (`src/audio/sample.c`) plays short sounds decoded fully into memory; long music
streams with `AudioStream`, see [Music](#music). The path is found as a `TexturePath` is: absolute, relative to the
working directory or an ancestor of it, or to an ancestor of the folder of the executable.

- **Decoding** runs on the job system (`box_job_submit`), as textures and fonts do, with the
  WAV and FLAC decoders of miniaudio in `src/audio/decode.c`, to float frames at the sample rate
  of the file. The voice of the core plays them at the rate of the output with the linear
  interpolation of the pitch, so a 44100 Hz file plays in its time and at its pitch on a 48000
  Hz output. Mono and stereo stay as they are; more channels are mixed down to stereo: each side
  is the mean of the channels on it, by the channel map of the file (FL FR FC LFE BL BR for
  5.1), a center counts on both sides and the LFE on none. A file is read up to 256 MB and
  decodes to at most 512 MB of frames.
- **Status.** `SoundStatus` is `SoundLoading` until the decode completes, then `SoundReady` with
  the frames, the channels after the mix down and the rate, or `SoundFailed` with an `error`.
  A failed load (a missing, empty or corrupt file, or another format) logs one error that names
  the file, and the other sounds keep playing.
- **Not ready yet.** A source of a sound that still loads starts once it is ready if it loops;
  otherwise it is dropped and its `playing` goes false, and so is a `box.audio.play` of it.
- **One copy.** The decoded frames are shared by path: every `SoundFile` with the same path,
  prefab instances included, uses one sample with one id of the core, and any number of sources
  play it. Deleting the last of them releases the sound: the sources and one shots that play it
  fade out over the ramp of the core, the main thread frees its frames at once and the side that
  runs the core frees its own after `SOUND_FREE`. Deleting an entity with `AudioPcm` fades its
  one shots the same way. Changing the path takes the sample of the new path.
- **Handing over.** The sync system sends a ready sample like an `AudioPcm`, with the `sound` of
  the transport: on native a copy whose pointer goes to the audio thread over the ring, on the
  web a transferred `ArrayBuffer` that the worklet copies into the heap of its core. A new
  output gets every sample again.
- **Headless** frames wait for the decodes in flight, at most 10 seconds, as they wait for
  textures, so a fixed delta time hears the same sounds in every run.

miniaudio is built in `decode.c` with `MA_NO_DEVICE_IO`, `MA_NO_ENGINE`, `MA_NO_NODE_GRAPH`,
`MA_NO_RESOURCE_MANAGER`, `MA_NO_ENCODING`, `MA_NO_GENERATION`, `MA_NO_MP3`, `MA_NO_VORBIS` and
`MA_NO_THREADING`, its functions static (`MA_API static`) so they do not clash with the device
part of `native.c`. Its warning that the web build cannot use `-std=c*` is about the Web Audio
backend; without device IO the decoders compile with `-std=c11` under emcc 6.0.8, so
`project.json` fetches the bundle for Emscripten too and the web uses the same decoders.

**The web stalls while it decodes.** Jobs run inline in the browser, at the end of the frame
that submitted them, so decoding a sound stalls that frame for the whole decode. Load sounds
at the start of a scene, behind a loading screen, rather than in the middle of play; the audio
itself does not stall, since the worklet keeps rendering. `test/integration/web/audio_sample.mjs`
measures the longest animation frame while 2 seconds of 48000 Hz stereo (384 KB) decode. In
headless Chrome on an Apple M-series Mac the frame grows from about 0.5 ms to under 1 ms for
the 16 bit WAV and to 2 to 3 ms for the FLAC in a release build (about 1 and 4.5 ms in a debug
build), well within a frame at 60 Hz; the decode time grows with the length of the sound.
A file under `etc` reaches the page through the `embed` of the app (`apps/boxrun/project.json`
embeds `etc`); a test file through `?preload=`.

The tests are `Sample` in `test/cpu/audio`, with files in `test/assets/audio` that
`make_sounds.py` next to them writes, and the browser check `audio_sample.mjs`.

### Music

```
using box.audio
music { AudioStream: {path: "etc/assets/music/theme.ogg"}  AudioOutput: {bus: music_bus} }
```

`AudioStream` (`src/audio/music.c`) plays one Ogg Vorbis file that is too long to decode into
memory. The file is found as a `SoundFile` is, read into memory in its compressed form and
decoded with the pull API of `stb_vorbis` (the `stb` bundle, compiled once in
`src/audio/vorbis.c`) in chunks of 4096 frames, at most 2 seconds
(`BOX_AUDIO_MUSIC_AHEAD`) ahead of what plays, never as a whole track. Mono and stereo stay as
they are; more channels mix down to stereo as for sound files, by the channel order of Vorbis.

- **Into the core.** The sync system starts the stream with a `STREAM` command once half a
  second is decoded (or the whole of a shorter file) and hands the chunks over with
  `stream_chunk` after the batch of the frame: on native a copy whose pointer goes to the audio
  thread over the command ring, on the web a transferred `ArrayBuffer`. The voice of a stream
  plays at priority 255, so no play steals the music. The core reads them from
  the ring of the stream. The transport frees a chunk when the status of the stream says the
  core consumed it; the native audio thread publishes that status after each render in one
  atomic word (voice, open, ended, consumed), so the main thread frees by a consistent view.
- **Loops.** The decoder goes on with the first frame after the last within the same chunk, so
  the seam is like any other pair of frames: no gap, no click, the length of each loop exact. A
  stream that does not loop marks its last chunk; the voice ends after it, `playing` goes false
  and the state is `AudioStreamEnded`. Setting `playing` again starts over.
- **Native** decodes on a job (`box_job_submit`) whenever less than 1.5 s is ahead, never on the
  audio thread; headless frames wait for these jobs as for sound files.
- **One stream at once.** A second `AudioStream` logs one error that names it, its state is
  `AudioStreamFailed` and it stays silent. A change of `path` stops the old stream with the fade
  of a stop (5 ms) and starts the new one on the other stream of the core; a delete fades out,
  then frees the decoder and the chunks once the core closed the stream. `playing: false` fades
  out and keeps the position, `true` resumes there.
- **Status.** `AudioStreamStatus` says `AudioStreamLoading` while the file is fetched, opened or
  decoded ahead of the start, then `AudioStreamPlaying`; a missing, empty or corrupt file, or a
  failed fetch, logs one error and fails without a crash. `underruns` counts the times the
  output ran short, `buffered` the seconds decoded ahead, `decode_ms` the time of the last
  decode and `decode_max_ms` the largest.
- **Before the gesture** a stream that loops starts and is heard from the gesture on; one that
  does not loop is dropped, as a source is (see [Before the gesture](#before-the-gesture)).

**The web.** The page has no worker threads and jobs run inline at the end of the frame, so
decoding runs on the main thread. A decode there stops after `BOX_AUDIO_MUSIC_WEB_BUDGET`
(2 ms) and goes on in the next frame, every frame that has less than 2 seconds ahead, which
keeps more than 1.5 s of chunks in the worklet: a 500 ms stall of the main thread does not run it
short. Once full, a frame decodes only what played since, about a sixtieth of a second, which
`decode_ms` reports; `test/integration/web/audio_stream.mjs` prints it with the longest frame.

**The web does not embed long music.** `embed` puts all of `etc` into the download of the page, so a
track there adds its whole size to the start of every visit; a short loop, such as the 32 seconds
of the dust merchant under 1 MB, can stay in `etc`. A stream looks in the page's file
system first (a file of `etc` an app embeds, or one that `?preload=` fetched), then fetches the
path relative to the page with `fetch` into memory; the status says `AudioStreamLoading` until it
arrived, and a failed fetch logs one error and stays silent. To deploy a track, keep it out of
the folders the app embeds and copy it next to the page under the same relative path the scene
names:

```sh
BIN=<folder that holds boxrun.html>
mkdir -p "$BIN/assets/music"
cp assets/music/theme.ogg "$BIN/assets/music/"
```

with `AudioStream: {path: "assets/music/theme.ogg"}`; a native build finds the same path
relative to its working directory. The server sends the file as it is, `audio/ogg` or any type,
and the page reads it once, compressed; it is never decoded whole.

The tests are `Stream` in `test/cpu/audio`, with `tone.ogg`, `tone_960.ogg` and `corrupt.ogg` in
`test/assets/audio`, which `make_ogg.py` next to them writes with an encoder of its own from
known tones, and the browser check `audio_stream.mjs`:

```sh
node test/integration/web/audio_stream.mjs \
    "$(tools/integration.py local-env --env audio)/wasm32-Emscripten/debug/bin"
```

### Transport

`src/audio/tempo.c` keeps one timeline in beats for the world, which the metronome, and later
tracks, quantized plays and tempo synced delays, schedule against. The `Transport` singleton
holds it:

```
using box.audio
$ { Transport: {tempo: 96, beats_per_bar: 6, beat_unit: 8, playing: true, metronome: true} }
```

**Beats.** A beat is a quarter note in every time signature, and every time of the timeline is
in these beats. A bar is `beats_per_bar * 4 / beat_unit` beats: 4 in 4/4, 3 in 6/8, 3.5 in 7/8.
Bars start at beat 0. `tempo` is quarter notes per minute, clamped to 20..999 (a NaN to 20).

**Position.** While `playing` is true the module writes `position` every frame from the clock
of the output: the beat that the core renders now. It follows the audio, not the frame delta,
so it stops while a page is suspended and while the output waits for its device. A write of
`position` by the application is a seek: the module notices a value that differs from the one
it wrote last, so a seek works from a script, the REST API and the `set` command alike.
`playing: false` stops at the position and `playing: true` plays on from it; a position written
while stopped is where the next start begins. `position`, `loop_start` and `loop_end` are
clamped to -2^31..2^31 beats (a NaN to 0), so a bad value cannot overflow the frames.

**Beats to frames.** The timeline is a list of anchors, each a frame of the core clock and the
beat at that frame. A beat lasts `rate * 60 / tempo` frames, and the frame of a beat on an
anchor is `anchor frame + round((beat - anchor beat) * rate * 60 / tempo)`. An anchor is set
when the transport starts, on a seek, on a new output and on a change of the tempo or the loop.
A change is anchored at the end of what was scheduled already, at the current position on that
line: the position does not jump, and the new tempo applies to what has not been scheduled.
`box_audio_timeline_beat` and `box_audio_timeline_frame` in `src/audio/module.h` convert both
ways; `box_audio_anchor_frame` and `box_audio_anchor_beat` do it on one anchor.

**Loop.** With `loop` on and `loop_end > loop_start`, the timeline wraps from `loop_end` to
`loop_start` on the frame of `loop_end`, `anchor frame + round((loop_end - anchor beat) * rate *
60 / tempo)`, where a new anchor starts at `loop_start`. A position before `loop_start` plays
into the loop; a position at or past `loop_end` plays on without wrapping until a seek. A loop
shorter than a frame does not wrap.

**The lookahead window.** Each frame the module covers the frames from the end of the last
window to `clock + lookahead`. The window is a list of segments, each with a beat range
`[start, end)`, its frames `[frame, frame_end)` and its anchor, split at a loop wrap. The frames
and the beats of consecutive segments and windows follow each other without a gap or an
overlap, so an event whose beat is in a segment is scheduled once, as a command with the frame
of its beat as its `when`. When the clock overtook the window, after a stall longer than the
lookahead, the window starts at the clock and the beats in between are skipped rather than
played late. The internal API in `src/audio/module.h` is for the parts that schedule:
`box_audio_timeline_hook` adds a hook whose `window` callback gets the segments every frame the
transport plays, and whose `reset` callback runs on a stop, a seek and a new output;
`box_audio_timeline_play` writes a `PLAY` with a `when` and remembers its handle. On a stop and a
seek the module cancels what it scheduled ahead that has not started with an immediate `STOP` of
each handle, which also drops its scheduled play in the core; a new output has none of them.

**Metronome.** With `metronome: true` and the transport playing, a click plays on the master on
every beat counted from the start of each bar, accented on the first beat of a bar: volume 0.8
and pitch 1.5 against 0.4 and 1. The click is a synth patch built in code, a 1600 Hz sine that
decays in 30 ms, sent once to the output when the metronome first plays. Each click is a
scheduled `PLAY` with its exact frame, so its first sample is at
`round(beat * rate * 60 / tempo)` frames from the start, whatever the frame rate.

A transport that does not play costs a read of the singleton per frame and sends nothing. The
tests are the `Timeline` suite of `test/cpu/audio`, with the null transport at a fixed delta:
the position, seeks, the loop, tempo changes, the coverage of the window, the frames and
accents of the clicks in 4/4, 6/8 and a loop, the cancel on a stop, a new output, a script
(`test/assets/audio/transport.flecs`), the command and two runs that capture identical audio.

### Quantized plays

A game often wants a stinger or a hit to land on the grid of the music.
`box.audio.play_quantized(sound, volume, quantize)` (`box_audio_play_quantized` in C) plays a
sound once, as `box.audio.play` does, on the bus of its `AudioOutput`, but starts it on the
next beat (`AudioQuantizeBeat`) or the next bar (`AudioQuantizeBar`) of the transport. A
script names the constants without the prefix of the enum, `None`, `Beat` and `Bar`:

```
using box.audio
const hit: bool = box.audio.play_quantized(stinger, 0.9, Bar)
```

The play starts on the first boundary at or after the clock of the output, the frame the core
renders now, and goes out as a `PLAY` with the frame of that boundary as its `when`, so its first
sample is exact. Bars follow the time signature of the transport, `beats_per_bar * 4 /
beat_unit` beats counted from beat 0, so a bar of 7/8 is 3.5 beats. The boundary is found on the
pieces of the timeline and goes on past the window along the last one, through the loop: a
boundary at or past `loop_end` is not played, the next one after the wrap is, counted on the
timeline from `loop_start`. A loop with no boundary in it starts the play at its next wrap. A
boundary further away than the lookahead, a bar at a slow tempo, is sent at once with its frame
and waits in the schedule of the core.

With the transport stopped, or without a `Transport`, a quantized play starts at once, as
`box.audio.play`, and `AudioQuantizeNone` is `box.audio.play`. The rules of `box.audio.play`
apply: a play before the gesture is dropped, and so is a play of a sound file that still loads.

**The transport does not own ad-hoc plays.** `box.audio.play`, `box.audio.play_at` and
`box.audio.play_quantized` are one shots of the game, not parts of the timeline. The module
does not remember a quantized play among what the timeline scheduled, so a stop or a seek of the
transport neither cancels it before it starts nor moves it, and no transport change stops a one
shot that plays. They play on any bus their `AudioOutput` names: a group, a return, an SFX bus
or the bus of a track.

The quantized plays of a frame are sent after the timeline followed the transport, so a play
in the frame the transport starts lands on the grid of that start. A scene without quantized
plays pays a loop over an empty vector. The tests are the `Quantize` suite of `test/cpu/audio`:
beats and bars at 120 and 97 bpm in 4/4 and 7/8, a play on a boundary, a boundary past
`loop_end`, a stopped transport, a stop and a seek after a play was queued, a `box.audio.play`
through a stop, and a script (`test/assets/audio/quantize.flecs`).

### Instruments

Without an instrument, a note of a score is a sound of its own whose pitch is baked into its
patch, one template per pitch, and it cannot last a duration. An `Instrument` plays one sound at
any pitch, with a velocity, a pan and a duration, a pitch being a MIDI note number:

| Field | What it is |
|-------|------------|
| `sound` | The sound that plays: with `SynthSound` the instrument is a synth, with `AudioPcm` or `SoundFile` a sampler |
| `root` | The pitch at which the sound plays as authored (69, A4 at 440 Hz), so a synth whose oscillators are at 440 Hz plays in tune |
| `velocity_curve` | The gain of a note is `velocity ^ velocity_curve` (2) |
| `gate` | The duration of a note ends it (true); false plays the sound as authored, like a drum hit |
| `release` | Seconds a sampler note fades out over after its end (0.05); a synth uses the release of its envelopes |

```
using box.audio

music { AudioBus: {volume: 1} }

prefab piano_tone {
  SynthSound: {}
  SynthEnvelope: {attack: 0.005, decay: 0.4, sustain: 0.5, release: 0.3, curve: 4}
  SynthOscillator: {wave: SynthTriangle, frequency: 440, level: 0.4}
}

piano { Instrument: {sound: piano_tone}  AudioOutput: {bus: music} }

// C4 for a beat, then a quarter tone over E4 for half a beat
const c: bool = box.audio.play_note(piano, 60, 0.8, 1)
const q: bool = box.audio.play_note(piano, 64.5, 0.5, 0.5)
```

- **Pitch.** A pitch is a float, so microtones work: the voice plays at the ratio
  `2^((pitch - root) / 12)`. A pitch past the limits of the core (a ratio of 1/64..16, six
  octaves down and four up) is clamped, with one warning per instrument. A synth moves every
  oscillator by the ratio, a sampler reads its frames at the ratio.
- **Velocity and pan.** The velocity is 0..1 and the gain `velocity ^ velocity_curve`, so 0.5
  plays at 0.25 at the default curve; a curve of 1 is linear. The pan of a note, -1..1, is the
  pan of its voice; `play_note` plays at 0.
- **Gate.** With `gate`, a synth note is a `PLAY` with `BOX_AUDIO_PLAY_GATE` and a `RELEASE` at
  its end: the voice sustains at the sustain level of its envelopes, their hold ignored, and
  releases over the release of its envelopes from where they stand (see
  [Synth](#synth), Gate and release). A sampler note fades out linearly over `release` from its
  end. Without `gate` the note plays the sound as authored, whatever its duration.
- **Attack.** A sampler note plays with `BOX_AUDIO_PLAY_ATTACK`: it starts at its gain, without
  the 5 ms ramp of a play, so a sampled drum keeps its transient.
- **Legato.** A gated note of a legato synth sound follows the voice of its channel, see
  [Glide and legato](#glide-and-legato).

`box.audio.play_note(instrument, pitch, velocity, duration)` (`box_audio_play_note` in C) plays
a note ad hoc. It starts at the clock of the output, so it applies at the next block as a play
does, and its duration is in beats at the tempo of the `Transport` (120 without one), also while
the transport is stopped: with a gate, its `RELEASE` waits in the schedule of the core for the
start frame plus the duration. It plays on the bus of the `AudioOutput` of the instrument, else
of its sound, else the master. The transport does not affect it, like the other ad-hoc plays,
and it is dropped before the gesture and while its sound file loads, as `box.audio.play` is.

Later parts of box.audio play their notes through the same function,
`box_audio_instrument_note` of `src/audio/instrument.c`: an instrument, a pitch, a velocity, a
pan, a start frame, an end frame, a bus and a voice handle. The [tracks](#clips-and-tracks) use
`box_audio_instrument_voice`, the same with a gain and without the `RELEASE`, which they send
once the end of the note falls in a window. A note costs a `PLAY` and a
`RELEASE` in the batch of its frame, and a scene without instruments pays a loop over an empty
vector. The tests are the `Instrument` suite of `test/cpu/audio`: the pitch of a synth and a
sampler, the gain of a velocity, the sustain and the release of a gated synth (linear, curved and
its filter envelope), the fade of a sampler, a note without a gate, the exact frame of a
release, a script (`test/assets/audio/instrument.flecs`) and legato notes.

### Clips and tracks

An `Instrument` plays a note when the game asks for it. A score is notes on the timeline of the
[transport](#transport): `src/audio/sequencer.c` plays clips of notes that tracks place on it.

- **A clip** is a `NoteClip`: its `length` in beats (4) and its `notes`, each a `Note` with a
  `start` in beats from the start of the clip, a `duration` in beats, a `pitch` (a MIDI note
  number, a float), a `velocity` (1) and a `pan`. A clip is reusable, usually a prefab or a
  plain entity: any number of placements on any tracks name it.
- **A track** is a `Track` with the `instrument` its notes play on. `Track` brings an `AudioBus`
  with the `With` trait, so a track is a mixer channel with a volume, a pan, a mute, an output,
  sends and inserts, and its notes play on its own bus. Its notes and audio clips play at its
  `priority` (1), above the one shots, `play_note`, quantized plays and `AudioSource` at 0: at
  the voice cap of 64 a burst of one shots steals only one shots, and a note of a track steals
  the quietest voice of at most its priority. When every voice ranks higher a play is refused
  and counted in `AudioStatus.voices_refused`. A track of priority 0 ranks with the one shots.
- **A placement** is a `ClipPlacement` on a child of a track, so placements are added and removed
  as entities, like the layers of a synth sound: the `clip`, its `start` on the timeline in
  beats, its `duration` (0 for the length of the clip minus the offset), `loop` to repeat the clip
  every `length` beats until the duration ends, an `offset` into the clip at which it starts (with
  a loop the passes go on from there and wrap at `length`), a linear `gain` (1) that multiplies the
  gain of the velocity, and a `transpose` in semitones added to every pitch.

```
using box.audio

music { AudioBus: {volume: 0.8} }

prefab bass_tone {
  SynthSound: {}
  SynthEnvelope: {attack: 0.005, decay: 0.1, sustain: 0.7, release: 0.08, curve: 3}
  SynthOscillator: {wave: SynthSaw, frequency: 440, level: 0.3}
}

bass_synth { Instrument: {sound: bass_tone} }

// One bar: A2, then C3 and E3 an eighth each, then G2 for the last two beats
prefab bass_line {
  NoteClip: {length: 4, notes: [
    {start: 0, duration: 1, pitch: 45, velocity: 0.9},
    {start: 1, duration: 0.5, pitch: 48},
    {start: 1.5, duration: 0.5, pitch: 52, pan: 0.2},
    {start: 2, duration: 2, pitch: 43, velocity: 0.7}
  ]}
}

bass {
  Track: {instrument: bass_synth}
  AudioBus: {volume: 0.9, output: music}

  // Four bars of the line, then the second half of it a fourth up and softer
  verse { ClipPlacement: {clip: bass_line, start: 0, duration: 16, loop: true} }
  turn { ClipPlacement: {clip: bass_line, start: 16, offset: 2, transpose: 5, gain: 0.6} }
}

$ { Transport: {tempo: 110, loop: true, loop_start: 0, loop_end: 18, playing: true} }
```

**Scheduling.** Each frame the transport plays, for every segment of the lookahead window, every
enabled track and placement schedules the notes whose start falls in the segment: a note of the
instrument of the track, a `PLAY` with the frame of its start as its `when`, on the bus of the
track, at the pitch plus the transpose, the gain `velocity ^ velocity_curve` times the gain of the
placement and the pan of the note. A loop of the transport wraps the notes on the frame of the
wrap, as it does the metronome. The module keeps its own copy of the notes of each clip sorted by
start, rebuilt when the clip is set, so a window finds its notes with a binary search, and an
index of the placements sorted by start, rebuilt when a placement, a track or a clip changes, so
a window visits only the placements that overlap it.

**Ends.** The module keeps the notes that sound with the beat at which each ends, and sends their
`RELEASE` when that beat falls in a window, mapped to a frame on the piece of that window. A tempo
change therefore moves the end of a long note. A note ends at the earliest of its own end, the end
of its pass of the clip (with a loop), the end of its placement and, while the transport loops,
`loop_end`. A note of an instrument without a gate plays as authored and is not tracked.

**Stop, seek and a new output.** A stop of the transport releases the notes that sound at once
and cancels the notes that were scheduled but have not started, with an immediate `STOP` of their
handles. A seek does the same and goes on at the new position, so a note that started before the
new position does not start in the middle; a new output does the same as a seek.

**Changes while playing.** A change of a clip, a placement, a track or an instrument applies to
what has not been scheduled yet, so to the notes that start more than the lookahead later.
Deleting or disabling a track or a placement releases its notes, cancels the ones that have not
started and schedules nothing more for it. A track whose instrument has no `Instrument`, or whose
sound the output does not have yet (a sound file that loads), drops its notes and logs one warning
until it plays again.

**What the transport does not touch.** The transport only drives tracks. `AudioSource`,
`AudioStream`, `box.audio.play`, `box.audio.play_at`, `box.audio.play_quantized` and
`box.audio.play_note` are never stopped or moved by it.

**Cost.** A world whose transport does not play pays nothing for its tracks: the hooks of the
timeline do not run, and the observers only mark the index for a rebuild. A playing transport
visits the placements that overlap its window and the notes that sound. The `Sequencer` suite of
`bench/audio` runs 16 tracks of 4 placements of 64 notes, playing and stopped, and a playing
transport without tracks.

The `audio` command lists every track with its instrument, each placement with its clip (its
notes and length, or `audio <sound> from <offset> s, <length> beats` for an audio clip), start,
duration, loop, offset, gain and transpose, the notes that sound, each with its pitch and the
beat it ends at (`released` once its `RELEASE` went out), and the voices of audio clips that
play (`playing: audio until beat <end>`, `stopped` once its `STOP` went out). The tests
are the `Sequencer` suite of `test/cpu/audio`: the start frames at two tempos, the release frames,
the transpose, gain and pan, a loop with an offset, durations that cut a clip, two placements of a
clip, a loop of the transport, a stop, a seek, a tempo change in a note, an edit of a clip, the
delete of a track, a disabled placement, one shots through a stop, an instrument that cannot
play, the cost, a script (`test/assets/audio/sequencer.flecs`), two runs that capture identical
audio and the command; the `audio_` cases play audio clips, see [Audio clips](#audio-clips).

#### Audio clips

Tracks also play recorded audio in time with the notes: a vocal, a loop or a stem. An `AudioClip`
is a part of a sound that a `ClipPlacement` names like a `NoteClip`:

- `sound`: an entity with `AudioPcm` or `SoundFile`;
- `offset`: seconds into the sound at which the clip starts;
- `length`: beats (4), the length of a pass, and of a placement of duration 0.

The offset is in seconds, not beats: a sound does not stretch with the tempo, so an offset in
beats would land on another sample after a tempo change. Every other time of the timeline is in
beats. One track may hold note and audio placements; its `instrument` only plays the note clips,
so a track of audio clips needs none.

```
using box.audio

vocal { SoundFile: {path: "etc/sounds/vocal.wav"} }

// The chorus of the take: from 12.5 seconds in, 8 beats long
chorus { AudioClip: {sound: vocal, offset: 12.5, length: 8} }

vox {
  Track: {}
  AudioBus: {volume: 0.9}
  first { ClipPlacement: {clip: chorus, start: 16} }
  // The second half of the chorus twice, a whole tone up
  again { ClipPlacement: {clip: chorus, start: 32, duration: 8, offset: 4, loop: true, transpose: 2} }
}
```

An audio placement plays its sound on the bus of its track, at its `gain`, with the pitch ratio
`2^(transpose / 12)`, which changes the speed too, as a sampler does. Each pass of the clip starts
with a `PLAY` scheduled on its frame, at its gain at once (without the 5 ms ramp of a play), so it
starts on its exact sample. A pass that starts in the middle, with the `offset` of the placement
or after a seek, gets a `POSITION` scheduled on the same frame as its play: the offset of the clip
plus the beats of the pass before its start, converted at the tempo and the pitch ratio.

A pass ends at the earliest of the end of its pass (`length` beats), the end of the sound, the end
of its placement and, while the transport loops, `loop_end`, with a `STOP` on that frame. The end
beat is mapped to a frame once it falls in a window, as the release of a note is, so a tempo
change moves it. A looping placement starts a new voice at the offset of the clip on every pass,
on the frame where the voice before stops.

A stop of the transport stops the voices at once and cancels the passes that have not started. A
seek, a loop wrap and a start in the middle of a placement go on in the middle of the pass that
covers the new beat, at its sample. Deleting or disabling the placement or the track stops its
voices. A sound that is not ready when a pass starts (a sound file that still loads, an entity
without samples) skips that pass and logs one warning per track until a pass plays again.

The tests are the `audio_` cases of the `Sequencer` suite of `test/cpu/audio`, which play ramp
sounds whose every frame tells its position: a start on its frame from the offset of the clip, a
placement offset at two tempos, a loop without a gap, the end of a pass and of a sound, a seek, a
loop of the transport, a transpose of 12, the gain, a track of notes and audio, a stop, a delete
and a disable, a sound that is not ready, two runs that capture identical audio and the command.

### Before the gesture

A scene plays the same on native, on the web and headless. On the web, plays requested while the
page waits for a gesture are dropped unless they loop: a `box.audio.play` and the start of a
source that does not loop are skipped (the source's `playing` goes false), while a looping source
starts and is heard from the gesture on. So a page does not burst out with every click sound of
its first seconds at the first key press. Native and headless runs have no gesture and drop
nothing.

### The audio command and captures

The shell command `audio` (`src/command/cmd_audio.c`) prints the state, the rate, the voices, the
steals, the refused plays and the load, the clock, the transport (`transport: 120.0 bpm 4/4, position 2.1 (4.250
beats), playing, loop off`, the position as bar.beat counted from 1), then the master (with the
volume, pan and width of its `AudioBus` when it has one) and the buses in processing order, each
with its kind, volume, pan, width, `solo`, `muted`, its output, its sends (`send pre` for a
pre-fader one) and its insert slots, each as `insert <index> <entity>:` with its kind and values
(`reverb decay 2.40 damping 0.40 ...`, `eq [low cut 120 Hz 0.0 dB q 0.71] [peak 3000 Hz 3.0 dB
q 1.20] [off] [off]`, `filter lowpass 24 dB cutoff 400 resonance 0.30 mix 1.00`,
`compressor threshold -30.0 dB ratio 8.0 ... sidechain sfx`, `gate threshold -45.0 dB ...`, the
two dynamics kinds with `reduction <dB>` of the last frame, `delay sync 0.750 beats feedback 0.40
damping 4000 Hz mix 1.00 ping pong`, or `delay time 0.250 ...` without sync) and whether it
runs: `(runs)`, `(bypassed)` for a Disabled effect, or `(does not run: ...)` with
the reason (no output, in another slot, every effect of the pool taken, waits for its memory);
the master lists its own after its `AudioBus`. Each channel in the order ends with its levels of
the last frame, `peak <left>/<right> dB, rms <left>/<right> dB` (`-inf` for silence), the master
before the limiter. The instruments follow, then the tracks, each as
`track <entity>: instrument <entity>, priority <n>` with its placements and sounding notes (see
[Clips and tracks](#clips-and-tracks)). `audio volume <v>` and `audio mute [on|off]` write
`AudioSettings`; `audio play`, `audio stop`, `audio seek <beats>` and `audio tempo <bpm>` (20 to
999) write `Transport`. `audio capture <seconds>` records the master output
to a 16 bit stereo WAV resource of the session, fetched like a `capture` PNG with
`GET /shell/session/<id>/resource/<name>`, empty until the file is written in one rename. In the
browser it writes `/tmp/audio_capture_<n>.wav` in the file system of the page and returns the
path; the page runs commands with `Module.cwrap('box_command_web_run', 'string', ['string'])`.
Headless with a fixed `--delta-time`, two runs write identical captures, which is how agents and
tests inspect audio and how a render can be listened to. `box.perf` reports the largest load of
a run as `PerfStatus.audio_load` and `audio_load` in its report.

The tests are `Module`, `Mixer`, `Meter`, `Effect`, `Source`, `Script`, `Capture` and `Spatial` in
`test/cpu/audio`,
through the null transport; `test/integration/e2e/audio.py` drives the command on a headless
app, `test/integration/web/audio_spatial.mjs` moves a source from the left of the listener to
its right in Chrome and measures both channels with `test/integration/web/audio_spatial.flecs`,
and
`test/integration/web/audio_module.mjs` checks the gesture, the levels, the meters messages and
`AudioMeter`, a hidden page, a capture
and a stall in Chrome with `test/integration/web/audio_module.flecs`. The `all_features` case of
`audio.py` and `test/integration/web/audio_all_features.mjs` run
`test/assets/audio/all_features.flecs`, which uses every part together: levels on both channels,
a reverb tail, bit identical captures, a stall and a hidden page.

```sh
node test/integration/web/audio_module.mjs \
    "$(tools/integration.py local-env --env audio)/wasm32-Emscripten/debug/bin"
```
