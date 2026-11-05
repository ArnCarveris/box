# Audio

`box.audio` plays samples, synth sounds and music from components authored in script,
C or the REST API. This guide covers scene authoring and the main runtime rules.
See [audio.h](../include/box/audio.h) for the complete component and function reference,
and [the example scenes](../test/assets/audio/) for working patches and songs.

## The model

- A **sound** is an `AudioPcm`, `SoundFile` or `SynthSound`. Play it once with
  `box.audio.play`, or attach an `AudioSource` to an entity for controlled playback.
- A **voice** plays a sound on a mixer bus, with volume, pan and pitch. There are
  64 voices. At the cap, a play steals the quietest voice of equal or lower priority;
  it is refused if every voice ranks higher. One shots and sources have priority 0,
  tracks default to 1, and streamed music uses 255.
- An **AudioBus** is a mixer channel: output, volume, pan, width, mute, solo,
  four sends and four insert effects. All buses ultimately feed the master.
- A **Transport** provides a timeline in beats. Tracks place reusable note or audio
  clips on it; instruments turn notes into synth or sampler voices.
- An **AudioStream** plays one Ogg Vorbis file, independently of the timeline.
- **AudioSpatial** derives a source's volume and pan from its position relative to
  the listener. **AudioSettings** controls master volume and mute.

The audio core renders independently of the game thread. A slow game frame delays
new commands; it does not stop voices already playing. Gain changes normally ramp
over 5 ms. The master's limiter keeps output peaks under 0.95.

## A first sound

```flecs
using box.audio

sfx { AudioBus: {output: master, kind: AudioChannelSfx} }

prefab Beep {
  AudioOutput: {bus: sfx}
  SynthSound: {}
  SynthEnvelope: {attack: 0.002, decay: 0.25}
  SynthOscillator: {wave: SynthSine, frequency: 660, level: 0.5}
}

const played: bool = box.audio.play(Beep, 0.8)
```

A prefab defines the sound without playing continuously. `play` returns whether the
request was queued. Call it when the game event occurs; use `AudioSource` for a
sound such as an engine or fire that belongs to an entity.

| Script function | Purpose |
| --- | --- |
| `box.audio.play(sound, volume)` | Play once on the sound's `AudioOutput`, or master |
| `box.audio.play_at(sound, {x, y, z}, volume)` | Play once at a fixed world position |
| `box.audio.play_quantized(sound, volume, quantize)` | Play on the next beat or bar |
| `box.audio.play_note(instrument, pitch, velocity, duration)` | Play a note; duration is in beats |
| `box.audio.capture(seconds, path)` | Record the master to a WAV file |

The C names are `box_audio_play`, `box_audio_play_at`, `box_audio_play_quantized`,
`box_audio_play_note` and `box_audio_capture`, with `world` as the first argument.
One shots cannot be controlled after starting. Transport stop and seek do not affect
these ad hoc plays, including quantized plays that have not started yet.

Component constructors supply the defaults below. In C, `ecs_set` with a compound
literal writes every member, including omitted members as zero; use `ecs_ensure`
and `ecs_modified` when you want to preserve constructor defaults.

## Sources and files

```flecs
using box.audio

click { SoundFile: {path: "etc/assets/sounds/click.wav"} }
hum { SoundFile: {path: "etc/assets/sounds/hum.flac"} }
engine {
  AudioSource: {sound: hum, loop: true, volume: 0.4}
}
```

`AudioSource` has `sound`, `volume` (1), `pitch` (1), `loop` (false) and
`playing` (true). It starts when playing and its sound is ready. Setting playing
false, deleting the source, or changing its sound or loop stops the voice with a
fade; a legato synth sound change can retarget it instead. A nonlooping source sets
playing false when it ends. Disabled sources keep their voices at volume zero.
Prefabs do not play; instances get their own copy of `AudioSource`.

`SoundFile` decodes WAV or FLAC fully into memory. Paths may be absolute, relative
to the working directory or its ancestors, or relative to ancestors of the executable's
folder. Decoding runs on jobs on native and on the main thread on the web.
Load short sounds with the scene to avoid browser frame stalls during play.

- `SoundStatus.state` is `SoundLoading`, `SoundReady` or `SoundFailed`, with
  frames, channels, sample rate and an error on failure.
- Plays requested before a file is ready are dropped. Looping sources wait for it.
  Start a song's transport after its samples are ready.
- Decoded samples are shared by path, including prefab instances. Deleting the last
  owner releases the sound and fades its plays. Changing the path loads the new file.
- Mono and stereo are preserved; multichannel files mix down to stereo. The core
  resamples to the output rate with linear interpolation.
- Input files are limited to 256 MB and decoded frames to 512 MB.

For generated samples, use `AudioPcm` (`samples`, `channels`, `sample_rate`), or
`box_audio_sound_from_pcm` from C. A sample rate of zero means the output rate.

## Mixer channels

`AudioOutput: {bus: channel}` routes a source, stream or sound to a bus. Without it,
plays go to master. A source can override its sound's routing.

| `AudioBus` field | Meaning and default |
| --- | --- |
| `output` | Destination bus; zero or `master` means master |
| `volume` | Linear gain, 1; range 0..4 |
| `pan` | -1 left to 1 right, 0 centered |
| `width` | 0 mono, 1 unchanged, 2 doubles the side signal |
| `mute`, `solo` | Both false |
| `kind` | `AudioChannelGroup`, `AudioChannelReturn` or `AudioChannelSfx`; group by default |
| `sends` | Up to four `{bus, level, pre_fader}` entries |
| `effects` | Four effect entities, run in array order; zero skips a slot |

A channel processes inserts, width, pre-fader sends, volume/pan/mute, output, then
post-fader sends. Mute and solo silence both kinds of send. Stereo pan is a balance:
toward either side, the far channel fades and the near channel rises by up to 3 dB.

```flecs
using box.audio

hall {
  AudioBus: {output: master, kind: AudioChannelReturn, effects: [hall]}
  Reverb: {decay: 2.4, damping: 0.4, predelay: 0.02}
}
sfx { AudioBus: {output: master, kind: AudioChannelSfx} }
music { AudioBus: {output: master, volume: 0.6} }
lead {
  AudioBus: {output: music, sends: [{bus: hall, level: 0.3}]}
}
```

Define effects, destination buses and sidechains before referring to them in script.
A return usually uses a fully wet effect; its incoming send levels set the amount.

Solo keeps the soloed channel, its output path, channels feeding it, returns and
SFX buses audible, along with their downstream paths. Mute always wins; a Disabled
bus is muted and does not solo. Thus soloing music does not silence the game.

The master is `box.audio.master`. Set it through nested scopes in script:

```flecs
box { audio { master { AudioBus: {width: 0.9}; AudioMeter: {} } } }
```

Its bus volume multiplies `AudioSettings.master_volume` (1), and its mute combines
with `AudioSettings.mute`. Master output, sends, solo and kind are ignored.

There are 63 buses besides master. Excess buses are silent and excess sends are
ignored, with errors. Output/send cycles log an error and keep the last valid order.
Sources whose bus disappears route to master; sends to a missing bus do nothing.

### Effects

An effect runs only when a bus names its entity in an insert slot. Each effect can
occupy one slot; a duplicate assignment logs an error and leaves the second empty.
There are 64 effects across all buses. Disabled effects are bypassed.

Parameter changes are smoothed by the effect. Moving or changing an effect blends
out and back in over 100 ms total. Removing it fades it out over 50 ms; deleting a
bus lets its effect tails fade into master. Silent effects stop processing after
their tails end.

| Component | Main controls and defaults |
| --- | --- |
| `Reverb` | `decay: 1.5` s, `damping: 0.5`, `predelay: 0.02` s, `size: 0.5`, `mix: 1` |
| `Equalizer` | Four `bands` of `kind`, `frequency`, `gain` dB and `q`; off by default |
| `AudioFilter` | `kind: SynthLowPass`, `cutoff: 1000` Hz, `resonance: 0`, `slope: SynthSlope12`, `mix: 1`, `drive: 0` |
| `Compressor` | `threshold: -18` dB, `ratio: 4`, `attack: 0.01` s, `release: 0.15` s, `knee: 6` dB, `makeup: 0` dB, `mix: 1`, `sidechain: 0` |
| `Gate` | `threshold: -50` dB, `attack: 0.001` s, `hold: 0.05` s, `release: 0.1` s, `range: -80` dB |
| `Delay` | `time: 0.25` s, `sync: 0` beats, `feedback: 0.35`, `mix: 0.3`, `ping_pong: false`, `damping: 6000` Hz |
| `Phaser` | `stages: 4`, `rate: 0.5` Hz, `depth: 0.7`, `center: 800` Hz, `feedback: 0`, `mix: 0.5`, `stereo: 0.5` |
| `Chorus` | `voices: 2`, `rate: 0.8` Hz, `depth: 3` ms, `delay: 12` ms, `mix: 0.5`, `spread: 0.5` |
| `Saturator` | `drive: 0.3`, `asymmetry: 0`, `bits: 16`, `mix: 1` |
| `TransientShaper` | `attack: 0`, `sustain: 0` |

Equalizer bands use `AudioEqOff`, `AudioEqPeak`, `AudioEqLowShelf`,
`AudioEqHighShelf`, `AudioEqLowCut` or `AudioEqHighCut`. Defaults are 100, 400,
2000 and 8000 Hz, gain 0 dB and Q 0.7071. Gains range from -24 to 24 dB.
Filters also support `SynthHighPass`, `SynthBandPass`, `SynthNotch` and
`SynthSlope24`; cutoff is clamped below Nyquist.

A positive delay `sync` overrides time: `sync * 60 / tempo` seconds, capped at
4 seconds. An eighth is 0.5 beats, a dotted eighth 0.75. Time changes crossfade
between read positions instead of gliding pitch. Use `mix: 1` on a return.
Phaser stages range from 2 to 12, even; chorus supports up to four voices.

A saturator adds warmth, grit or lo-fi to a drum bus, a synth bus or the master. It
soft clips each channel with tanh at twice the sample rate, so it aliases less.
`drive` goes from 0 to +30 dB into the clipper, and the output is level compensated,
so drive mostly changes the tone and not the loudness: a sine around -14 dBFS keeps
its RMS within about 1 dB at any drive. `asymmetry` biases the clipper to add even
harmonics, and the DC it creates is filtered out. Below 16, `bits` runs a bit crusher
after the saturation: 4 bits gives 16 levels. Oversampling delays the bus by 15
frames, and the dry signal is delayed the same amount, so `mix` does not comb.
An `AudioMeter` on the saturator reads how hard the clipper squashes, in dB.

A transient shaper gives drums and plucks more or less punch and more or less tail,
without a threshold. Stereo linked like a compressor, it follows the peak of the
louder channel with a fast envelope (1 ms attack, 20 ms release) and a slow one
(20 ms attack, 200 ms release). Where the fast one is above the slow one the signal
is a transient, otherwise it is sustain, which ends as soon as the next hit comes
up. The gain follows their difference in dB, so
it works the same at any level. `attack` from -1 to 1 softens or boosts transients,
`sustain` from -1 to 1 shortens or boosts the tail, each by up to 12 dB. The gain is
smoothed so it does not distort low notes, is the same on both channels, and 0 and 0
pass the signal unchanged. It has no meter.

For ducking, key a compressor on another channel:

```flecs
music_duck {
  Compressor: {threshold: -30, ratio: 8, attack: 0.005, release: 0.3, sidechain: sfx}
  AudioMeter: {}
}
music { AudioBus: {output: master, effects: [music_duck]} }
```

The sidechain runs before the compressed bus. A sidechain cycle logs an error and
falls back to the compressor's own input; a missing sidechain also uses its own input.
A gate always keys on its own input. See the effect scenes in
[test/assets/audio](../test/assets/audio/) for EQ, filter, delay and modulation setups.

### Meters

Add `AudioMeter: {}` to a bus, master or effect to read it every frame:

- `peak_left` and `peak_right`: largest absolute sample since the last frame, linear.
- `rms_left` and `rms_right`: RMS over the last 300 ms of output, limited to
  64 application frames. A peak-0.5 sine reads an RMS of about 0.354.
- `reduction`: largest gain reduction in dB for compressors and gates, and how hard
  the clipper of a saturator squashes; zero otherwise.

Buses meter their output after inserts, width, pan, volume and mute. Master meters
**before the limiter**, so values above 1 indicate an overdriven mix. Lower the group
or master gain instead of relying on limiting. Meter values lag commands; with the
null transport a change appears two application frames later. Suspended, failed or
waiting outputs read silence. The `audio` command shows levels without `AudioMeter`.

## Spatial audio

```flecs
using box.audio
using box.transform

ears { Position: {0, 1.7, 0}; AudioListener }
engine {
  Position: {4, 0, -10}
  AudioSource: {sound: hum, loop: true}
  AudioSpatial: {min_distance: 2, max_distance: 40}
}
const shot: bool = box.audio.play_at(click, {-3, 0, -5}, 1.0)
```

The listener is an `AudioListener` with `WorldTransform`; otherwise it is the camera
of the first enabled view in render order. Tag the main camera explicitly in scenes
with render-to-texture views. Multiple listeners warn and use the lowest entity ID.
Without a listener, spatial sounds play centered at full volume.

`AudioSpatial` defaults to `min_distance: 1`, `max_distance: 50`, `rolloff: 1`.
A source uses its world position, including parent transforms; without a
`WorldTransform` it plays nonspatially. On a sound entity, `AudioSpatial` supplies
distances for `play_at`, whose position stays fixed while the listener moves.

Gain is 1 within min distance and zero at max distance. Between them it follows
inverse-distance attenuation, normalized to reach zero at max:
`g(d) = min / (min + rolloff * (d - min))`, then
`gain = (g(d) - g(max)) / (1 - g(max))`.
Rolloff zero holds full gain until max distance. Pan follows the listener's right
axis and narrows to center within min distance. Front and back sound alike.
There is no Doppler, occlusion, distance filter or HRTF.

Out-of-range looping sources release their voices but keep time on the game thread;
returning in range resumes where the sound would have been. Nonlooping sources and
`play_at` requests starting out of range are dropped. A nonlooping source that moves
out of range keeps a silent voice until it ends. Spatial gains update after
transforms and use the core's 5 ms ramps.

## Synth sounds

`SynthSound` marks a sound's root. The root and descendants with `SynthOscillator`
are its layers, up to eight. Layers use their own `SynthEnvelope` or the root's;
the root's filter processes the mix. A patch is compiled on change and copied into
voices on play, so edits affect subsequent plays.

| Component | Authoring controls |
| --- | --- |
| `SynthSound` | `seed`, `pitch_variance`, `level_variance`; variances default to zero |
| `SynthOscillator` | `wave`, `frequency` (440 Hz), `frequency_end` (0, no sweep), `sweep` seconds, `level` (1), `pulse_width` (0.5), `sub` (0, 0..1), `sub_octave` (1, 1..2), modulation controls, `velocity` (0) |
| `SynthEnvelope` | `attack`, `decay`, `sustain`, `hold`, `release`, `curve`, `attack_curve`; all zero by default |
| `SynthFilter` | `kind`, `cutoff` (1000 Hz), `resonance` (0), `slope` (`SynthSlope12` or `SynthSlope24`), `drive` (0, 0..1), `velocity` (0 octaves) |
| `SynthFilterEnvelope` | Envelope controls plus `amount` in octaves and `velocity` (0); requires a filter |
| `SynthUnison` | Per layer: `voices` (3, 1..7), `detune` (10 cents, 0..100), `width` (0, 0..1) |
| `SynthGlide` | Root: `time` seconds (0, up to 10), `legato` (false), `channel` (0) |
| `SynthLfo` | `wave`, `rate` (5 Hz), `delay`, `fade`, `phase`, `random_phase`, destination depths |
| `SynthString` | Pluck layer: `decay` (2 s to -60 dB), `brightness` (0.5), `hardness` (0.8), `velocity` (0) |

Waves are `SynthSine`, `SynthSaw`, `SynthSquare`, `SynthTriangle`, `SynthNoise`
and `SynthPluck`. Frequency sweeps are exponential. Positive envelope curves make
decay/release fall quickly then settle; zero is linear, negative curves start slowly.
Curve values range from -16 to 16. A sustain of zero ends after decay.
For gated notes, sustain holds until release, ignoring hold.

`sub` adds a band-limited square one or two octaves (`sub_octave`) under a layer,
for weight under basses and leads without another layer. It is added before the
layer's level and envelope, so it shares them. It is derived from the layer's
phase: it follows sweeps, glides, legato, pitch LFOs and every unison voice, but
not phase modulation or feedback. A modulator modulates with its sub included.
`SynthNoise` and `SynthPluck` ignore it.

A filter envelope uses `cutoff * 2^(amount * envelope_level)`. For example,
cutoff 300 and amount 3 open to 2400 Hz. Its timing is independent of the amplitude
envelope; author both when they should move together.

`drive` soft clips the filter input and its resonance inside each filter stage,
both stages at `SynthSlope24`. Quiet signals pass almost unchanged and loud ones
compress and gain harmonics; a high resonance settles at a bounded level and growls
instead of whistling. Zero is linear and costs nothing; 1 drives the filter hard.
`AudioFilter.drive` does the same on a bus and moves over 20 ms like `mix`.
In the `Synth` bench of [bench/audio](../bench/audio/) (64 voices, 48 kHz, one
128-frame quantum), drive raises a 12 dB filter from about 75 to 115 µs and a 24 dB
filter with an envelope from about 125 to 170 µs: roughly 4 ns per stage and frame of
a voice.

```flecs
using box.audio

prefab Pluck {
  SynthSound: {}
  SynthEnvelope: {attack: 0.002, decay: 0.4, sustain: 0.3, hold: 0.1, release: 0.2}
  SynthFilter: {kind: SynthLowPass, cutoff: 300, resonance: 0.3, slope: SynthSlope24}
  SynthFilterEnvelope: {attack: 0.002, decay: 0.25, sustain: 0.2, hold: 0.25, release: 0.2, amount: 4}
  SynthOscillator: {wave: SynthSaw, frequency: 220, level: 0.5}
}
```

A layer without an envelope logs an error and plays for 10 seconds. `SynthPluck`
is the exception: its physical string model rings out without an envelope. Brightness
controls how quickly harmonics fade; hardness controls the attack. Frequencies below
40 Hz warn and clamp. There are 32 strings total, one per pluck unison voice; long
ringing chords can exhaust them and cause plays to be ignored.

The velocity of a note (an `Instrument` play or a `NoteClip`, clamped to 0..1) reaches
the voice as well as its gain, so harder notes can be brighter, not only louder. Each
`velocity` field defaults to 0, which leaves the sound as it was:

- `SynthFilter.velocity` is the octaves of cutoff at velocity 1 over velocity 0, -10..10.
  The authored cutoff applies at velocity 1; at velocity `v` it is
  `cutoff * 2^(velocity * (v - 1))`.
- `SynthFilterEnvelope.velocity`, a share 0..1, scales the envelope amount:
  `amount * (1 - share + share * v)`.
- `SynthOscillator.velocity` scales the index of a phase or ring modulator the same way,
  so soft notes have fewer sidebands.
- `SynthString.velocity` scales the hardness of a pluck the same way, so soft notes are
  plucked softer.

Plays that are not notes, such as `AudioSource`, `box_audio_play`, quantized plays and
streams, play at velocity 1. Sample voices ignore velocity beyond its gain.

### Modulation, unison and glide

A child oscillator with `modulation: SynthPhase` or `SynthRing` modulates its
parent layer. Phase `index` is radians, up to 32 and limited against frequency;
ring index is a depth from 0 to 1. A modulator is heard separately only with
`audible: true`. `feedback` adds self phase modulation, up to 1.5 radians.
Phase modulation is not fully band limited; high frequencies and indices can alias.
Pluck strings ignore phase modulation and feedback, but support ring modulation.

Unison spreads copies evenly across the detune range and stereo width. Each plays
at `1 / sqrt(voices)` of the layer level, so power stays similar while peaks may
rise. Use width zero for a spatial point source. Wide sounds stay stereo in space;
pan balances their channels rather than narrowing them with distance.

A sound supports two LFOs, on the root, a layer or another root child. Waves include
sine, triangle, square, rising saw and sample-and-hold. Destinations are `pitch`
(cents), `amplitude` (0..1), `cutoff` (octaves) and `pulse_width`.
Delay and fade bring modulation in gradually. Random phase and noise are seeded,
so the same sequence of plays is reproducible.

`SynthGlide.time` moves pitch exponentially in frequency. With `legato: true`,
notes on the same channel retarget a held voice without restarting its envelopes.
They keep the original waves, levels, filters and variances; use patches from the
same template so layer order agrees. A note after release starts a new voice.
Instrument gates end a legato phrase; one shots instead hold for the authored hold
after the last note. A retargeted voice keeps the velocity of the note that started it,
since its envelopes do not restart. Quantized legato plays start new voices.

Working examples include [synth_examples.flecs](../test/assets/audio/synth_examples.flecs),
[synth_unison.flecs](../test/assets/audio/synth_unison.flecs),
[synth_lfo.flecs](../test/assets/audio/synth_lfo.flecs),
[synth_glide.flecs](../test/assets/audio/synth_glide.flecs),
[synth_pluck.flecs](../test/assets/audio/synth_pluck.flecs) and
[synth_sub.flecs](../test/assets/audio/synth_sub.flecs).

## Music on the timeline

A beat is always a quarter note. A bar is `beats_per_bar * 4 / beat_unit` beats:
4 in 4/4, 3 in 6/8. Bars start at beat zero.

| `Transport` field | Meaning and default |
| --- | --- |
| `tempo` | Quarter notes per minute, 120; range 20..999 |
| `beats_per_bar`, `beat_unit` | Time signature, 4/4 |
| `position` | Current beat; writing it seeks |
| `playing` | False; false stops at the current position, true resumes |
| `loop`, `loop_start`, `loop_end` | One optional beat range; end must exceed start |
| `lookahead` | Scheduling window in seconds, 0.15; range 0.05..0.5 |
| `metronome` | False; clicks on beats, accented at bar starts |

Position follows the audio output clock. A suspended output holds it. Notes are
scheduled on exact samples ahead of that clock. Edits apply to events not yet
scheduled, up to the lookahead later. A stall longer than the lookahead skips
missed note starts instead of playing them late. Tempo changes are steps, with
new timing applied after the already scheduled window.

### Instruments, clips and tracks

An `Instrument` names one synth or sample sound. `root` (69, A4) is the MIDI pitch
at which it plays as authored. Pitch can be fractional; playback ratio is
`2^((pitch - root) / 12)`, clamped to 1/64..16. Samplers change speed with pitch.
Velocity gain is `velocity ^ velocity_curve`; the curve defaults to 2.

With `gate: true` (default), duration releases the synth's envelopes or fades a
sampler over `release` seconds (0.05). With gate false, a hit plays as authored,
regardless of duration. A nonlooping sample can end before its gate. Sampler notes
start without the usual 5 ms fade to preserve transients.

A `NoteClip` has a `length` (4 beats) and notes with `start`, `duration`, `pitch`,
`velocity` (1) and `pan` (0). A `Track` supplies the instrument and brings an
`AudioBus`. Its child `ClipPlacement` entities arrange clips:

| Placement field | Meaning and default |
| --- | --- |
| `clip` | Reusable `NoteClip` or `AudioClip` |
| `start` | Beat on the timeline |
| `duration` | 0 uses clip length minus placement offset |
| `loop` | False; true repeats passes until duration ends |
| `offset` | Beats into the clip, 0 |
| `gain` | Linear multiplier, 1 |
| `transpose` | Semitones, 0 |

```flecs
using box.audio

prefab bass_tone {
  SynthSound: {}
  SynthEnvelope: {attack: 0.005, decay: 0.1, sustain: 0.7, release: 0.08, curve: 3}
  SynthOscillator: {wave: SynthSaw, frequency: 440, level: 0.3}
}
bass_synth { Instrument: {sound: bass_tone} }

prefab bass_line {
  NoteClip: {length: 4, notes: [
    {start: 0, duration: 1, pitch: 45, velocity: 0.9},
    {start: 1, duration: 0.5, pitch: 48},
    {start: 1.5, duration: 0.5, pitch: 52},
    {start: 2, duration: 2, pitch: 43, velocity: 0.7}
  ]}
}

bass {
  Track: {instrument: bass_synth}
  AudioBus: {volume: 0.9}
  verse { ClipPlacement: {clip: bass_line, start: 0, duration: 16, loop: true} }
  turn { ClipPlacement: {clip: bass_line, start: 16, offset: 2, transpose: 5, gain: 0.6} }
}

$ { Transport: {tempo: 110, loop: true, loop_start: 0, loop_end: 18, playing: true} }
```

A gated note ends at its own end, its loop pass end, its placement end or transport
loop end, whichever comes first. Tempo changes can move ends not yet scheduled.
Stopping or seeking releases sounding gated notes and cancels scheduled starts.
Seeking does not restart a note that began before the new position. Ungated hits
already playing ring out. Deleting or disabling tracks or placements releases their
tracked notes and cancels future starts.

An `AudioClip` has `sound`, `offset` in **seconds**, and `length` in beats. It uses
`AudioPcm` or `SoundFile`, and needs no instrument. A track can mix note and audio
placements. Placement offsets remain in beats; transpose changes pitch and speed
together. Audio is not stretched to tempo. Each looping pass starts a new voice;
its seam uses the 5 ms stop fade. A seek or start within an audio placement resumes
inside the relevant pass, unlike note clips.

For a complete mixed song with samples, returns, ducking and a quantized game
stinger, see [song.flecs](../test/assets/audio/song.flecs). Wait for its sound files
to decode before setting transport playing. A quantized play uses `None`, `Beat`
or `Bar` in script, or `AudioQuantizeNone`, `AudioQuantizeBeat`, `AudioQuantizeBar`
in C. With no playing transport it starts immediately.

## Streamed and rendered music

```flecs
using box.audio
music_track {
  AudioStream: {path: "etc/scenes/dust_merchant/assets/dust_merchant_loop.ogg"}
}
```

`AudioStream` defaults to volume 1, loop true and playing true. Only one stream can
play at once. Vorbis is decoded in 4096-frame chunks, up to two seconds ahead;
the compressed file stays in memory. A stream starts after half a second is decoded,
or all of a shorter file. Native decoding runs on jobs; web decoding has a 2 ms
budget per frame. Loops continue within chunks without gaps.

Playing false fades out and preserves position; true resumes. A nonlooping stream
ends with playing false, and playing true then restarts it. Changing path fades
out the old stream over 5 ms and starts the new one, without a full crossfade.
`AudioStreamStatus` reports state, position, buffered seconds, underruns, decode
cost and errors. Starvation fades to silence, then back in when chunks arrive.

Use a live timeline when the arrangement or tempo responds to the game. Render a
fixed background loop ahead of time to spend one stream voice instead of voices
and effects for every note. The dust merchant's
[make_music.py](../etc/scenes/dust_merchant/assets/make_music.py) renders its score
headless, folds the tail into the start, normalizes and encodes Vorbis:

```sh
python3 etc/scenes/dust_merchant/assets/make_music.py <boxrun binary>
```

## Platform rules

### Web

- Audio starts on the first key or mouse button. Until then
  `AudioStatus.state` is `AudioWaitingForGesture`. Nonlooping plays are dropped;
  loops and looping streams become audible after the gesture.
- Hidden pages fade out over 50 ms and suspend (`AudioSuspended`), then resume
  when shown. Output sample rate is chosen by the browser, commonly 44100 or 48000 Hz.
- The core runs in an AudioWorklet, so main-thread stalls leave existing sounds
  playing. Sound-file decoding still stalls the main thread; load files early.
- Short files under embedded folders such as `etc` join the page download. Keep
  long music outside those folders and deploy it beside the page at the relative
  path named by `AudioStream`. The stream checks the page filesystem, then fetches.
- AudioWorklet requires a secure context; localhost works for development.

```sh
bake3 --target em --local-env=audio apps/boxrun
python3 -m http.server 8080 -d "$(tools/integration.py local-env --env audio)/wasm32-Emscripten/debug/bin"
```

Open `http://localhost:8080/boxrun.html#/etc/scenes/dust_merchant.flecs` and click
the canvas. A startup failure warns once and the app continues silently.

### Native and headless

macOS and Linux use miniaudio's default device on an audio thread. No device logs
one warning and uses the null transport (`AudioNoDevice`). Device loss falls back
to main-thread rendering and retries the default device every two seconds.
`--headless` always uses the null transport; `--no-audio` opens nothing.
Importing box alone does not open an output: `box_app_run` does.

## Inspecting and capturing audio

| Shell command | Action |
| --- | --- |
| `audio` | State, rate, voices, steals/refusals, load, clock, transport, routing, effects, meters and tracks |
| `audio volume <v>` | Set master volume, 0..4 |
| `audio mute [on\|off]` | Set or toggle master mute |
| `audio play`, `audio stop` | Start or stop the transport |
| `audio seek <beats>` | Seek the transport |
| `audio tempo <bpm>` | Set tempo, 20..999 |
| `audio capture <seconds>` | Record master output as a 16-bit stereo WAV resource |

Fetch a capture with `GET /shell/session/<id>/resource/<name>`; it stays empty
until recording finishes. In the browser it writes `/tmp/audio_capture_<n>.wav`
in the page filesystem and returns that path. A script can capture from load
with `box.audio.capture(seconds, path)`.

Headless with fixed `--delta-time`, repeated runs capture bit-identical audio on
the same build. Use this to render music, compare changes or inspect scenes offline.
`PerfStatus.audio_load` records the largest load of the run; worklet load reports
zero because it has no suitable high-resolution timer.

If a track is silent, check sample readiness, its instrument and overlapping
placements. If a return is silent, check its send and insert assignment. `audio`
reports whether each effect runs, is bypassed or waits for resources.

## Implementation and limits

`AudioSync` runs in `PreStore` after transforms, batching changed state and plays
once per frame. The transport updates in `OnStore`. Spatial sources update on
relevant changes; moving the listener updates all of them.

The [audio core](../src/audio/core/) is plain C without Flecs or platform code.
Rendering does not allocate, lock, log or wait. It mixes blocks of 128 float frames.
Native uses command/event rings; web uses a separate core wasm instance and
transferred messages over an AudioWorklet port. There is no shared memory or
COOP/COEP requirement. [audio_core.py](../tools/audio_core.py) builds the wasm and
embeds it in the main module during Emscripten builds; no extra deployment file is needed.

Play and legato commands carry the velocity of a note, 0..1, after their priority;
`box_audio_write_play_priority` and `box_audio_write_legato_priority` take it, and the
other writers send 1. The velocity makes a play command 48 bytes and a legato command 52, so
a writer buffer of a given size holds fewer of them than before.

The core owns a 64-bit rendered-frame clock. Scheduled play, stop, voice/bus set,
release and legato commands carry a frame time. Future commands wait in a fixed
4096-entry schedule; a full schedule applies the command immediately and reports
`BOX_AUDIO_EVENT_LATE`. Immediate stops also cancel scheduled commands for their
voice handle. Sound memory is freed by its owner after `SOUND_FREE`, which is
retained even when other events overflow. See [core.h](../src/audio/core/core.h)
and [transport.h](../src/audio/transport.h) for the low-level APIs.

The main limits are 64 voices, 64 buses, 64 effects, four inserts and sends per
bus, eight synth layers, two synth LFOs, seven unison copies per layer and
32 pluck strings. Effects are algorithmic: no convolution or geometry-derived rooms.
There is one transport and one music stream per world. There are no automation
lanes, tempo ramps, swing, time stretching, MIDI I/O, plugins, mixer UI or piano roll.
Scripts can change channel and effect values each frame.

Offline coverage lives in [test/cpu/audio](../test/cpu/audio/); browser and end-to-end
checks are under [test/integration/web](../test/integration/web/) and
[test/integration/e2e/audio.py](../test/integration/e2e/audio.py).
[all_features.flecs](../test/assets/audio/all_features.flecs) combines the subsystems.
Use [bench/audio](../bench/audio/) for mixer, effects, spatial and sequencer costs.
