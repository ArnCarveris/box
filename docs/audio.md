# Audio

`box.audio` plays samples, synth sounds, streamed music and a beat timeline from components set in
script, C or REST; synth components live in `box.audio.synth`. Scripts drop the C `Audio`/`Synth`
prefix (`AudioBus` is `Bus`). Reference: `include/box/audio*.h`. [audio.flecs](audio.flecs) is a
playable song file that uses every audio feature (`audio.song docs/audio.flecs`, `audio.play`);
`test/assets/audio/` has a scene per feature. C also has `box_audio_sound_from_pcm`.

## First sound ([first_sound.flecs](../test/assets/audio/first_sound.flecs))

```flecs
using box.audio
using box.audio.synth

sfx { Bus: {output: master, kind: ChannelSfx} }

prefab Beep {
  Output: {bus: sfx}
  Sound: {}
  Envelope: {attack: 0.002, decay: 0.25}
  Oscillator: {wave: Sine, frequency: 660, level: 0.5}
}

const played: bool = box.audio.play(Beep, 0.8)
```

| Script (C `box_audio_*`, `world` first) | Does |
|---|---|
| `box.audio.play(sound, volume)` | One shot on the sound's `Output` bus, else master; returns whether queued |
| `box.audio.play_at(sound, {x,y,z}, volume)` | One shot at a fixed position, with the sound's `Spatial` |
| `box.audio.play_quantized(sound, volume, None\|Beat\|Bar)` | Starts on the next beat/bar of a playing `Transport` |
| `box.audio.play_note(instrument, pitch, velocity, beats)` | One note of an `Instrument` |
| `box.audio.capture(seconds, path)` | Master to a 16-bit WAV, at most 60 s |

## Rules

- A sound is an entity with `Pcm`, `SoundFile` or `synth.Sound`; prefabs never play. At the 64-voice
  cap a play steals the quietest voice of equal or lower priority, else is refused (one shots and
  sources 0, `Track.priority` 1, streams 255).
- The core renders off the main thread in 128-frame blocks; gains ramp over 5 ms; the limiter holds
  peaks under 0.95. One shots can't be stopped and ignore transport stop/seek.
- C `ecs_set` with a literal zeroes omitted members (use `ecs_ensure` + `ecs_modified`). Limits: 63
  buses, 8 inserts and 4 sends per bus, 64 effects, 64 modulators, 32 macros, 256 synth sounds, 1023
  core sounds, one transport and one stream per world.
- Declare buses and effects before naming them; with both `using`s, write `box.audio.Filter` for the bus filter.

## Sounds, sources, streams

| Component | Fields (defaults) | Notes |
|---|---|---|
| `SoundFile` / `SoundStatus` | `path` / `state` (`SoundLoading/Ready/Failed`), `frames`, `error` | WAV/FLAC decoded whole, shared by path; plays while loading drop (looping sources wait) |
| `Pcm` | `samples`, `channels` 1, `sample_rate` (0 = output rate) | Generated samples |
| `Source` | `sound`, `volume` 1, `pitch` 1, `loop` false, `playing` true | Non-looping sets `playing: false` at end |
| `Output` | `bus` (0 = master) | On a sound or source; the source's wins |
| `Stream` / `StreamStatus` | `path` (.ogg), `volume`, `loop` true, `playing` / `state`, `position`, `underruns` | One plays at a time |
| `Settings` / `Status` | `master_volume`, `mute` / `state`, `voices`, `voices_stolen`, `load` | Singletons |

## Mixer

```flecs
hall { Bus: {output: master, kind: ChannelReturn, effects: [hall]}; Reverb: {decay: 2.4} }
lead { Bus: {output: master, sends: [{bus: hall, level: 0.3}]} }
box { audio { master { Bus: {width: 0.9}; Meter: {} } } }
```

`Bus`: `output` (0 = master), `volume` 1 (0..4), `pan`, `width` 1 (0..2), `mute`, `solo`, `kind`
(`ChannelGroup/Return/Sfx`), `sends` (≤4 `{bus, level, pre_fader}`), `effects` (8 slots). Order:
inserts, width, pre-fader sends, volume/pan/mute, output, post-fader sends. Solo keeps returns and
SFX audible; mute wins; `Disabled` mutes. 63 buses + `box.audio.master`. `Meter: {}` on a bus or
effect gives `peak_*`, `rms_*` (300 ms), `reduction` dB; master meters before the limiter.

## Effects

An effect is a component on an entity named in a `Bus.effects` slot (often the bus itself). 64 in
total, one slot each; `Disabled` bypasses; changes are smoothed.

| Effect | Main fields (defaults) |
|---|---|
| `Reverb` | `decay` 1.5, `damping` .5, `predelay` .02, `size` .5, `mix` 1, `shimmer` 0, `shimmer_pitch` 12 |
| `Equalizer` | 4 `bands {kind: EqOff\|EqPeak\|EqLowShelf\|EqHighShelf\|EqLowCut\|EqHighCut, frequency, gain, q}` |
| `Filter` | `kind` LowPass/HighPass/BandPass/Notch, `cutoff` 1000, `resonance`, `slope` Slope12/24, `mix`, `drive` |
| `Compressor` / `Gate` | `threshold` -18, `ratio` 4, `attack`, `release`, `knee`, `makeup`, `sidechain` (bus) / `threshold` -50, `hold`, `range` |
| `Delay` | `time` .25 s, `sync` beats (wins if >0), `feedback` .35, `mix` .3, `ping_pong`, `damping` |
| `Phaser` / `Chorus` | `stages` 4, `rate`, `depth`, `center` / `voices` 2, `depth` ms, `delay` ms, `feedback` (flanger) |
| `Saturator`, `TransientShaper` | `drive` .3, `mode` SaturatorTanh/Tube/Hard/Fold/Rectify, `asymmetry`, `bits`, `low_cut`, `high_cut`, `tone`, `rate`; `attack`, `sustain` |
| `FrequencyShifter` / `PitchShifter` | `shift` Hz, `mode` FrequencyShift/FrequencyRing / `semitones`, `cents`, `stereo`, `window` 40 ms |
| `PatternGate` | `steps[16]`, `length` 16, `rate` .25 beats, `swing`, `gate` .5, `floor` -80 dB |
| `Rotary` | `speed` 0 slow..1 fast, `acceleration` 1 s, `balance`, `spread`, `drive` |
| `BeatRepeat` / `TapeStop` | `active` (≥0.5 on; automate it), `slice`, `grid`, `decay`, `pitch` / `stop_time`, `start_time` |
| `Vocoder` | `modulator` (bus), `bands` 16 (8..32), `low`, `high`, `q`, `shift`, `unvoiced` |

Sidechain and vocoder keys render before the keyed bus; a cycle or missing key logs once. Beat
effects follow the Transport (120 bpm while stopped). Use `mix: 1` on returns.

### Modulators

`Modulator` (own entity) adds to one float member without writing the component: `target`,
`component` (`Bus`, an effect or `Macro`), `member` (`"cutoff"`, `"sends[0].level"`, `"bands[2].gain"`),
`depth`, `scale` `ModLinear/ModOctaves`, `source` `ModLfo/ModSteps/ModFollower`, `wave` `ModSine/
Triangle/Square/SawUp/SawDown/Random`, `rate` Hz or `sync` beats (Transport-locked), `unipolar`,
`steps`/`step_count`/`smooth`, follower `key`/`attack`/`release`. 64 total, 4 per member. Example:
`wobble { Modulator: {target: bass_filter, component: Filter, member: "cutoff", scale: ModOctaves, depth: 2, sync: 0.5} }`.

### Macros

`Macro: {value}` (0..1) drives children with `MacroMap: {target, component, member, min, max, curve
(-1 log..1 exp), scale}`. A map replaces the member's value (modulators add on top). Automate it as
`component: Macro, member: "value"`; from C `ecs_set(world, m, AudioMacro, {.value = x})`. 32 macros
of 8 maps; newest map wins a member. Example: `test/assets/audio/macros.flecs`.

## Spatial

`Listener` + `WorldTransform` (else the first view's camera). `Spatial`: `min_distance` 1,
`max_distance` 50, `rolloff` 1, `cutoff` Hz at max (0 off). Pan follows the listener's right axis;
no Doppler/HRTF. `Emitter` plays a sound at random times and places: `sound`, `interval_min/max`,
`shape` `EmitterPoint/Disc/Sphere/Hemisphere/Box`, `radius`, `extents`, `volume_*`, `pitch_*`, `max_active`.

## Synth

A root with `synth.Sound` (`seed`, `pitch_variance`, `level_variance`, `drift`, `free_phase`); it and
each descendant with `synth.Oscillator` is a layer (≤8). Layers use their own `synth.Envelope` or the
root's; the root `synth.Filter` filters the mix. Patches recompile on change, for later plays.

| Component | Key fields (defaults) |
|---|---|
| `synth.Oscillator` | `wave` Sine/Saw/Square/Triangle/Noise/Pluck/Wavetable/Bowed, `frequency` 440, `frequency_end`+`sweep`, `level` 1, `pulse_width`, `sub`, `sync`, `color` NoiseWhite/Pink/Brown, `modulation` Mix/Phase/Ring + `index`, `feedback`, `audible` |
| `synth.Envelope` | `attack decay sustain hold release curve attack_curve key_scale`, all 0 |
| `synth.Filter` | `kind` LowPass/HighPass/BandPass/Notch/Formant/Comb, `cutoff` 1000, `resonance`, `slope`, `drive`, `velocity`, `keytrack`, `model` FilterSvf/FilterLadder, `vowel`, `feedback`, `tune` |
| `synth.FilterEnvelope`, `synth.ModEnvelope` | Envelope fields; filter one adds `amount` octaves, `velocity`, `vowel` |
| `synth.PitchEnvelope` | `amount` semitones, `decay`, `curve`, `release_amount`, `sync` |
| `synth.Unison` / `synth.Glide` / `synth.Drive` | `voices` 3, `detune` 10, `width` / `time`, `legato`, `channel` / `drive` .3, `asymmetry` |
| `synth.String` / `synth.Bow` | `decay` 2, `brightness`, `hardness` / `pressure`, `speed`, `position` .12, `noise` |
| `synth.Table` / `synth.Wavetable` | `table`, `position`, `envelope` / `kind` WavetableBasic/Pulse/Harmonics or `sound` + `frame_size` |

A layer without an envelope logs an error and plays 10 s (not `Pluck`). `Phase`/`Ring` children
modulate their parent and are silent unless `audible`. Plucks and bows share 32 strings, combs 32
lines; plays past that are ignored. Legato retargets a held voice without restarting envelopes.

### LFOs

≤2 `synth.Lfo` per sound, ordered root, then layers, then other root children (= `SourceLfo1/2`).
On the root or a non-oscillator child it moves all layers, on a layer only that one. `wave` `LfoSine/
Triangle/Square/Saw/Random`, `rate` 5 Hz or `sync` beats (wins; Transport-locked), `delay`, `fade`,
`phase`, `random_phase`; depths `pitch` cents, `amplitude`, `cutoff` octaves, `pulse_width`, `hard_sync`, `vowel`, `position`.

### Modulation matrix

`synth.ModRoute: {source, target, amount}`, a child of the root (all layers) or a layer; ≤8 per sound.
Sources `Source` + `AmpEnvelope FilterEnvelope ModEnvelope Lfo1 Lfo2 Velocity Key Random Mod Pressure`.
Targets `Target` + `Cutoff Resonance Index Pitch Pan Detune PulseWidth Level HardSync Position Vowel
BowPressure BowSpeed BowPosition`. A route from `SourceMod`/`SourcePressure` disables that hardwiring
(mod → vibrato, pressure → +2 octaves cutoff). Non-applicable targets do nothing. Example:
`sweep { synth.ModRoute: {source: SourceModEnvelope, target: TargetCutoff, amount: 4} }`.

## Timeline

`Transport` singleton: `tempo` 120, `beats_per_bar`/`beat_unit`, `position` (write seeks),
`playing`, `loop`, `loop_start`, `loop_end`, `lookahead` .15 s, `metronome`. A beat is a quarter.
`Instrument` {`sound`, `root` 69, `velocity_curve` 2, `gate` true, `release`, `spread`}; `NoteClip`
{`length` 4, `notes [{start, duration, pitch, velocity, pan, bend, mod, pressure}]`}; `Clip` {`sound`,
`offset` s, `length` beats}; `AutomationClip` {`target`, `component`, `member`, `length`, `curve`}.
A `Track` {`instrument`, `priority`, `swing`, `humanize`} brings its own `Bus`; its children place
clips: `ClipPlacement: {clip, start, duration, loop, offset, gain, transpose}`, e.g.
`bass { Track: {instrument: bass_synth}; verse { ClipPlacement: {clip: bass_line, start: 0, duration: 16, loop: true} } }`.
Automation never writes the component. Stop/seek releases gated notes and cancels scheduled ones.

### Song files

One song `.flecs` loads at a time as a managed script; unload deletes all it made. Load via `$ { SongFile:
{file: "..."} }`, `box_audio_song_load` or `audio.song`; watch `SongStatus` (`SongLoading/Ready/Failed/None`).
The file sets `$ { Song: {title, tempo, beats_per_bar, beat_unit, beats, loop, lanes: [{name, bus}]} }`
last, which sets the Transport (beat 0, loop over `beats`). Prefix names, change only entities the song
creates (it may name `master`, give `box.audio.Song` an `Rgb`), never write `Transport`/`Settings`.

## Web build and transports

- Transports (`src/audio/transport.h`): native (miniaudio device thread, lock-free rings), web
  (AudioWorklet), null (main thread renders each frame's delta: `--headless`, no or lost device).
  `--no-audio` opens nothing; only `box_app_run` opens one. Headless + fixed `--delta-time` is bit-identical.
- Web: `tools/audio_core.py` compiles the core with clang (`--target=wasm32-unknown-unknown -O2 -msimd128
  -std=c11 -ffreestanding -nostdlib`) to its own wasm, embedded via `.bake/generated/box_audio_core_wasm.h`.
  The worklet loads from a Blob URL and talks over its port: no shared memory, COOP/COEP or extra link
  flags. Needs a secure context; `Status` is `WaitingForGesture` until a key/click, `Suspended` if hidden.
- `decode.c` (miniaudio WAV/FLAC decoders only, all static) also compiles on the web, as C11; file
  decodes stall the web main thread, so load sounds early.
- Native: miniaudio runtime-links its backends (no audio link flags). Notarized macOS builds need
  `-DMA_NO_RUNTIME_LINKING` and `-framework CoreFoundation -framework CoreAudio -framework AudioToolbox`.

## Shell commands

| Command | Does |
|---|---|
| `audio` | Status, voices, transport, routing, effects, meters, tracks |
| `audio.volume <v>`, `audio.mute [on\|off]` | Master volume 0..4, mute (toggles without state) |
| `audio.play`, `.stop`, `.seek <beats>`, `.tempo <bpm>` | Transport |
| `audio.solo <bus\|none>`, `.mute_bus <bus>`, `.bypass <effect>` `[on\|off]` | Mix checks |
| `audio.note <subject> <C4\|60> [vel] [secs]`, `audio.song [file\|none]` | Audition a note; load a song |
| `audio.capture <s>`, `audio.render <subject> [note= vel= dur= tail= bars=a..b solo= mute=]` | WAV resource |
| `audio.analyze <wav> [from to]`, `audio.diff <a> <b>`, `audio.plot <wav> [kind] [size=]` | Inspect WAVs |
| `audio.meter [bus...] [seconds\|bars=a..b]`, `audio.notes <track> [roll]`, `audio.chords`, `audio.check [static]` | Levels, notes, chords, lint |
