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
| `Reverb` | `decay: 1.5` s, `damping: 0.5`, `predelay: 0.02` s, `size: 0.5`, `mix: 1`, `shimmer: 0`, `shimmer_pitch: 12` semitones |
| `Equalizer` | Four `bands` of `kind`, `frequency`, `gain` dB and `q`; off by default |
| `AudioFilter` | `kind: SynthLowPass`, `cutoff: 1000` Hz, `resonance: 0`, `slope: SynthSlope12`, `mix: 1`, `drive: 0` |
| `Compressor` | `threshold: -18` dB, `ratio: 4`, `attack: 0.01` s, `release: 0.15` s, `knee: 6` dB, `makeup: 0` dB, `mix: 1`, `sidechain: 0` |
| `Gate` | `threshold: -50` dB, `attack: 0.001` s, `hold: 0.05` s, `release: 0.1` s, `range: -80` dB |
| `Delay` | `time: 0.25` s, `sync: 0` beats, `feedback: 0.35`, `mix: 0.3`, `ping_pong: false`, `damping: 6000` Hz |
| `Phaser` | `stages: 4`, `rate: 0.5` Hz, `depth: 0.7`, `center: 800` Hz, `feedback: 0`, `mix: 0.5`, `stereo: 0.5` |
| `Chorus` | `voices: 2`, `rate: 0.8` Hz, `depth: 3` ms, `delay: 12` ms, `mix: 0.5`, `spread: 0.5`, `feedback: 0` |
| `Saturator` | `drive: 0.3`, `asymmetry: 0`, `bits: 16`, `mix: 1`, `mode: SaturatorTanh`, `low_cut: 0` Hz, `high_cut: 0` Hz, `tone: 0`, `rate: 0` Hz |
| `TransientShaper` | `attack: 0`, `sustain: 0` |
| `FrequencyShifter` | `shift: 0` Hz, `mode: FrequencyShift`, `stereo: 0`, `feedback: 0`, `mix: 1` |
| `PitchShifter` | `semitones: 0`, `cents: 0`, `stereo: 0`, `window: 40` ms, `feedback: 0`, `mix: 0.5` |
| `PatternGate` | `steps`: 16 levels, all `1`, `length: 16`, `rate: 0.25` beats, `swing: 0`, `attack: 0.002` s, `release: 0.03` s, `gate: 0.5`, `floor: -80` dB, `mix: 1` |
| `Rotary` | `speed: 0`, `horn_slow: 0.8` Hz, `horn_fast: 6.7` Hz, `drum_slow: 0.7` Hz, `drum_fast: 5.9` Hz, `acceleration: 1` s, `balance: 0`, `spread: 1`, `drive: 0`, `mix: 1` |
| `BeatRepeat` | `active: 0`, `slice: 0.25` beats, `grid: -1` (the slice), `decay: 1`, `pitch: 0` semitones, `reverse: 0`, `gate: 1`, `mix: 1` |
| `TapeStop` | `active: 0`, `stop_time: 0.5` s, `start_time: 0.2` s, `curve: 0` |
| `Vocoder` | `modulator: 0`, `bands: 16`, `low: 100` Hz, `high: 8000` Hz, `q: 1`, `attack: 0.005` s, `release: 0.05` s, `shift: 0`, `unvoiced: 0`, `mix: 1` |

Equalizer bands use `AudioEqOff`, `AudioEqPeak`, `AudioEqLowShelf`,
`AudioEqHighShelf`, `AudioEqLowCut` or `AudioEqHighCut`. Defaults are 100, 400,
2000 and 8000 Hz, gain 0 dB and Q 0.7071. Gains range from -24 to 24 dB.
Filters also support `SynthHighPass`, `SynthBandPass`, `SynthNotch` and
`SynthSlope24`; cutoff is clamped below Nyquist.

A positive delay `sync` overrides time: `sync * 60 / tempo` seconds, capped at
4 seconds. An eighth is 0.5 beats, a dotted eighth 0.75. A synth LFO `sync` counts
the same beats at the same tempo, phase locked to the Transport (see Synth sounds).
Time changes crossfade
between read positions instead of gliding pitch. Use `mix: 1` on a return.
Phaser stages range from 2 to 12, even; chorus supports up to four voices.

A chorus with `feedback` is a flanger. The first voice's tap feeds back into the
delay line, so the comb resonates: peaks sit at multiples of `1000 / delay` Hz,
and a negative feedback moves them to the odd multiples of half that, for the
hollow flanger sound. Feedback ranges from -0.95 to 0.95; at 0 the chorus sounds
exactly as without it. For a flanger use one voice, a `delay` of about 0.1 to
10 ms and a `depth` a little under the delay, so the sweep nearly reaches zero:

```flecs
drums {
  AudioBus: {effects: [drums]}
  Chorus: {voices: 1, rate: 0.2, delay: 2, depth: 1.8, feedback: 0.7, mix: 0.5}
}
```

Higher feedback sharpens the jet sweep; a negative one sounds hollow. The tail
of the feedback rings out before the effect goes idle.

A saturator adds warmth, grit or lo-fi to a drum bus, a synth bus or the master. It
clips each channel at twice the sample rate, so it aliases less. `drive` goes from 0
to +30 dB into the clipper, and the output is level compensated per mode, so drive
mostly changes the tone and not the loudness: a sine at -14 dBFS keeps its RMS
within about 1.5 dB at any drive (tanh keeps its older compensation, which holds
that at -12 dBFS and rises up to 2.5 dB at -14 dBFS). `asymmetry` biases the clipper to add even
harmonics, and the DC it creates is filtered out. `mode` picks the curve:

- `SaturatorTanh`, the default: a tanh soft clipper, smooth and symmetric.
- `SaturatorTube`: a softer asymmetric tanh whose bias grows with the level, so it
  adds even harmonics even at `asymmetry: 0`. Warmer.
- `SaturatorHard`: a hard clip with a slightly rounded corner, fuzz.
- `SaturatorFold`: a sine wavefolder. The more drive, the more folds and the
  brighter the sound.
- `SaturatorRectify`: a rectifier that `asymmetry` blends from half wave (0) to full
  wave (1), octave-up grit.

Hard clipping and folding alias much more than tanh, so those two modes add first
order antiderivative anti-aliasing (ADAA) on top of the oversampling, and the folder
runs at four times the rate. On a -6 dBFS 5 kHz sine at 48 kHz, the aliases below
20 kHz are -51 dB (drive 0.8) and -42 dB (drive 1) for hard, against -30 and -21 dB
without ADAA. For fold at drive 1 they are -68 dB, against -23 dB with ADAA at twice
the rate and 0 dB at twice the rate alone. Above 20 kHz, the 25 kHz harmonic folds to
23 kHz through the roll-off of the oversampler, at -10 to -25 dB, which neither
removes.

The filters around the clipper shape the distortion. `low_cut` is a 12 dB highpass
before the clipper, so the bass is not distorted (and with `mix` below 1 the dry bass
stays clean). `tone` (-1 to 1) tilts the input by up to 9 dB either way around 1 kHz
and applies the inverse tilt after the clipper. Above 0 the highs distort most,
below 0 the lows, and a clean signal passes as it was. `high_cut` is a 12 dB lowpass
after the clipper that tames fizz. `rate` holds each sample for 1 / rate seconds,
which gives the aliasing lo-fi sound of an old sampler. A cut, or the rate, of 0 is
off. Below 16, `bits` runs a bit crusher last: 4 bits gives 16 levels. Oversampling
delays the bus by 15 frames (23 for `SaturatorFold`), and the dry signal is delayed the
same amount, so `mix` does not comb. Changes move over 20 ms; a change of `mode` applies at once.
An `AudioMeter` on the saturator reads how hard the clipper squashes, in dB.

Recipes:

| Sound | Saturator |
|---|---|
| Tape warmth on a bus | `{drive: 0.3}` |
| Tube on a synth or vocal | `{mode: SaturatorTube, drive: 0.5, high_cut: 9000}` |
| Fuzz on a guitar or bass | `{mode: SaturatorHard, drive: 0.8, low_cut: 150, high_cut: 5000}` |
| Wavefolded lead | `{mode: SaturatorFold, drive: 0.6, tone: 0.3, high_cut: 8000}` |
| Octave-up grit | `{mode: SaturatorRectify, drive: 0.4, asymmetry: 1, mix: 0.5}` |
| Lo-fi drums | `{drive: 0.4, bits: 8, rate: 8000, high_cut: 6000}` |

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

A pitch shifter thickens a lead with a few cents either way, adds octaves to
basses and leads, and makes harmonies, without changing the speed of what plays
through it. Each channel is a delay line read by two taps half a window apart. The
taps slide through the window at the speed of the shift and wrap around, with
constant power sine crossfades between them. It is a granular shifter: it keeps the
attacks of percussive material tight at short windows and sounds smooth on pads at
long ones, but it does not preserve formants. `semitones` ranges from -24 to 24 and
`cents` from -100 to 100. `stereo` from 0 to 1 moves the cents of the right channel
from `cents` to `-cents`, the classic detune widener. `window` ranges from 10 to
100 ms. `feedback` up to 0.9 sends the shifted signal back into the line, so each
pass shifts again into a cascade; it is soft clipped and stays bounded. The shifted
signal comes up to a window late, about half a window on average, and the dry
signal is not delayed to match. A steady tone lands on its interval exactly only
when the window holds a whole number of its periods; otherwise it is off by up to
half the rate at which the taps wrap, up to 12.5 Hz an octave up at 40 ms. Small
detunes wrap slowly and stay accurate.

`shimmer` on a reverb pitches its tail up inside the reverb, the rising pad sound of
ambient and cinematic tracks. A share of the feedback, from 0 to 1, runs through the
same shifter with a 60 ms window, so each pass of the tail rises by
`shimmer_pitch` semitones. The shifted share replaces part of the feedback, so the
reverb stays bounded at any decay and the damping darkens the shimmer like the rest
of the tail. A shimmer shortens the decay somewhat. At `shimmer: 0` the reverb is
the same as without it, sample for sample.

```flecs
// A widener: the left channel 8 cents up, the right 8 cents down
wide { AudioBus: {effects: [wide]}; PitchShifter: {cents: 8, stereo: 1, window: 30, mix: 0.5} }

// An octave-up lead: the octave under the dry signal
lead { AudioBus: {effects: [lead]}; PitchShifter: {semitones: 12, window: 25, mix: 0.35} }

// A shimmer return that sources reach through a send
shimmer {
  AudioBus: {effects: [shimmer]}
  Reverb: {decay: 6, damping: 0.4, size: 0.8, shimmer: 0.5, mix: 1}
}
```

A frequency shifter moves every partial by the same number of Hz, unlike a pitch
shifter, which multiplies them, so harmonic sounds turn inharmonic. `shift` goes from
-5000 to 5000 Hz. It shifts the analytic signal of an allpass pair Hilbert transformer,
whose image stays about 60 dB under the shift from 20 Hz to 20 Hz under Nyquist at 48 kHz
(about 55 dB at 96 kHz).
The allpasses turn the phase, so even a shift of 0 is not bit exact with the input.
`mode: FrequencyRing` makes it a ring modulator instead: a sine at the magnitude of
`shift` splits each partial into one above and one below at half the level, the
partial itself gone. `stereo: 1` shifts the right channel the opposite way, and in
between by `shift * (1 - 2 * stereo)`. `feedback` from -0.95 to 0.95 shifts the output
again on each pass, through a DC blocker and a soft limit. Changes glide over 20 ms
and a change of mode crossfades.

```flecs
// Thickening: a few Hz either way on each side widens a mono part
pad_wide { FrequencyShifter: {shift: 3, stereo: 1, mix: 0.5} }

// Barber pole: a small shift with feedback rises forever; a negative shift falls
pad_rise { FrequencyShifter: {shift: 0.3, feedback: 0.85, mix: 0.5} }

// Ring-mod bell: a clangorous, inharmonic tone from a plain one
bell { FrequencyShifter: {mode: FrequencyRing, shift: 437, mix: 0.8} }
```

A pattern gate chops a sustained sound into rhythm, the classic trance gate: it
turns pads, chords and noise into rhythmic parts without writing notes. `steps` holds
the level of each of 16 steps from 0 (closed) to 1, so a step at 0.5 is an accent
6 dB down, and `length` (1 to 16) says how many of them play. `rate` is the length of
a step in beats: 0.25 for sixteenths, 0.5 for eighths, 0.375 for dotted sixteenths,
1/6 for sixteenth triplets. The pattern is locked to the Transport: at every frame the
step `floor(beat / rate) mod length` sounds, so the gate stays on the grid after a
seek, a loop wrap or a tempo change. Without a playing Transport it runs at 120 bpm.
`swing` (0 to 0.75) bends the step grid as `Track.swing` bends notes: the second step
of each pair starts later by `swing` steps.

A step is open for the first `gate` share of it (0.05 to 1) and closed after; at 1,
neighbouring open steps run together. The gain rises in `attack` seconds and falls in
`release` seconds, linearly, so the edges do not click, and a longer release gives a
softer pulse. Closed is `floor` dB, -80 to 0: a floor of -6 to -12 dB pumps instead of
chopping. Both channels get the same gain. A change of a step or of the timing moves
the gain at the speed of the edges, and `floor` and `mix` move over 20 ms, so
automating `steps[3]` or modulating a step is smooth. An `AudioMeter` on the gate reads
its gain reduction, and the gate stops while its input is silent.

```flecs
// A trance gate on a pad: sixteenths with accents and a soft edge
pad_gate {
  PatternGate: {steps: [1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0.5, 0, 1, 0.5, 1],
    gate: 0.6, release: 0.05}
}
pad { AudioBus: {effects: [pad_gate]} }

// A gentle pump on chords: eighths with swing that only dip 9 dB
chords_pump { PatternGate: {length: 2, steps: [1, 0], rate: 0.5, swing: 0.2, gate: 0.7,
  release: 0.12, floor: -9} }
```
A rotary is a rotating speaker cabinet: the swirl of an organ, also good on guitars,
electric pianos and vocals. The sum of both channels runs through a tube preamp,
blended in by `drive` (up to +24 dB into the tanh clipper of the saturator), then a
crossover at 800 Hz splits it into a drum below and a horn above. Each rotor shifts
the pitch as it turns (a Doppler shift of a few tenths of a millisecond on the
horn), and gets quieter and duller as it faces away. Two virtual mics hear both
rotors; `spread` sets how far apart they are, from mono at 0 to half a turn at 1,
so left and right swirl in opposite phase. `balance` goes from the drum alone at -1
to the horn alone at 1. `speed` 0 is slow (chorale) and 1 fast (tremolo), with
rates in between for values in between. The rotors never jump to a new speed: the
horn gets there within `acceleration` seconds and the heavier drum takes three
times as long, both when speeding up and when braking. The rotary keeps 4 ms of
memory per effect, and its rotors keep turning while it is idle.

Switch the speed like the half-moon switch of an organ by automating `speed`:

```flecs
using box.audio

organ {
  AudioBus: {output: master, effects: [organ]}
  Rotary: {speed: 0, drive: 0.3}
}

prefab switch {
  AutomationClip: {target: organ, component: Rotary, member: "speed", length: 16,
    curve: {kind: CurveLinear, keys: [{0, 0}, {7.9, 0}, {8, 1}, {15.9, 1}, {16, 0}]}}
}

lanes {
  Track: {}
  leslie { ClipPlacement: {clip: switch, start: 0, duration: 64, loop: true} }
}
```

The step of the curve is enough: the inertia of the rotors makes the ramp up and
the slow down.

`BeatRepeat` and `TapeStop` are performance effects for fills and transitions,
driven by their `active` member from an automation lane or a modulator. Both are
float members, so a lane can switch them at a beat: `active` counts as on from 0.5.

A beat repeat waits for the next `grid` point of the Transport after `active`
rises (a negative grid is the slice, 0 captures at once), then repeats the next
`slice` beats, from 1/32 to 4, held to 4 seconds. The first repeat is the input
itself; each repeat after it is `decay` times as loud as the one before (0.5 drops
6 dB a repeat) and `pitch` semitones from it, from -12 to 12, resampled like a
tape: at -12 the second repeat plays at half speed, the third at a quarter.
`reverse: 1` plays the repeats after the first backwards, `gate` from 0.1 to 1 is
the share of each repeat that sounds, and `mix` blends the repeats with the input
while it repeats. Repeats crossfade over 2 ms. When `active` falls the input
returns at once, over 2 ms, not at a grid point, so the lane sets the end of the
fill. Decay, pitch, reverse and gate apply from the next repeat, slice and grid at
the next rise.

A tape stop slows its input down to a halt over `stop_time` seconds while
`active` is up, pitch and time together, and spins it back up over `start_time`
seconds after it falls, both from 0.01 to 4. Halfway through a linear stop a sine
sounds an octave down. `curve` from -1 to 1 shapes the ramps: above 0 the speed
holds longer and drops at the end, under 0 it drops at once. The output fades out
under a tenth of the speed and is silent at the halt; what comes in meanwhile is
skipped. A start plays the input of its moment and, once up to speed, crossfades
to the input over 20 ms instead of catching up.

Both follow the beat clock of the Transport, at 120 bpm while none plays, and run
on 4 seconds of memory of their effect slot.

```flecs
using box.audio

// A stutter fill: the last beat of a four bar phrase repeats a sixteenth, each
// repeat a little quieter and a semitone down
drums { AudioBus: {effects: [drums]}; BeatRepeat: {slice: 0.25, decay: 0.85, pitch: -1} }

prefab fill {
  AutomationClip: {target: drums, component: BeatRepeat, member: "active", length: 16,
    curve: {keys: [{0, 0}, {14.999, 0}, {15, 1}, {15.999, 1}, {16, 0}]}}
}

// A tape stop drop: the music winds down over the last two beats at 120 bpm and
// spins back up on the downbeat of the next phrase
music { AudioBus: {effects: [music]}; TapeStop: {stop_time: 1, start_time: 0.15, curve: 0.3} }

prefab drop {
  AutomationClip: {target: music, component: TapeStop, member: "active", length: 16,
    curve: {keys: [{0, 0}, {13.999, 0}, {14, 1}, {15.999, 1}, {16, 0}]}}
}

transitions {
  Track: {}
  fill_lane { ClipPlacement: {clip: fill, start: 0} }
  drop_lane { ClipPlacement: {clip: drop, start: 16} }
}
```

A vocoder lets the spectrum of one channel, the `modulator`, shape the input of the
channel it sits on, the carrier: a voice on a saw pad gives a talking synth or a robot
voice, a drum loop on a pad gives a pad that pumps with the beat. Both run through
`bands` matched bandpasses (8 to 32, two cascaded state variable filters each), spaced
logarithmically from `low` to `high`. An envelope follower with `attack` and `release`
on each modulator band sets the level of the carrier band of the same index. `q` sets
how sharp the bands are: at 1 neighboring bands cross at -3 dB, higher is narrower and
more tonal. The level is made up so that a full band modulator around -5 dBFS RMS gives
about the carrier's level; a silent modulator gives silence. `shift` from -1 to 1 moves
the carrier bands up to an octave up or down against the modulator bands, which shifts
the formants. `unvoiced` adds white noise to the carrier of the bands from 3 kHz up, so
the consonants of a voice come through a carrier without highs.

The modulator is keyed like a sidechain: it runs before the vocoded channel, and its
output after its fader is the key, so route it into a muted group when only the
vocoded sound should be heard. Without a modulator, or with one that is deleted, is no
channel or would form a cycle, the vocoder logs one error and passes its input. Each
vocoder takes memory for 32 bands. The band count is not automatable; changing it fades
the vocoded sound out and back in. A drum loop vocodes a saw pad:

```flecs
drum_key { AudioBus: {output: master, volume: 0} }
drums { AudioBus: {output: drum_key} }
pad_vocoder {
  Vocoder: {modulator: drums, bands: 16, attack: 0.002, release: 0.08, unvoiced: 0.2}
}
pad { AudioBus: {output: master, effects: [pad_vocoder]} }
```

Play the drum loop on `drums` and a saw pad (a synth sound with a `SynthSaw` layer) on
`pad`; add `drums` to the master too, or give `drum_key` volume, to hear the loop with it.

The cost grows with the bands: each band runs eight bandpass stages per frame. The
`Modulation` bench of [bench/audio](../bench/audio/) measures it as `vocoder_16` and
`vocoder_32`: about 31 and 61 µs per 128-frame quantum in a release build, roughly
250 and 480 ns per frame, or about 15 ns per band and frame.

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

#### Modulators

An `AudioModulator` on an entity of its own moves one float member of a bus or an
effect on its own, in time with the song: a filter that wobbles in eighths, a send
that pulses, a tremolo, an auto-pan, an auto-wah. `target`, `component` and `member`
name what it moves exactly as an [AutomationClip](#automation) does: `volume`, `pan`,
`width` and `sends[i].level` of an `AudioBus` or of the master, every float member
of an effect component, such as `cutoff` or `bands[2].gain`, and the `value` of an
[AudioMacro](#macros).

The modulator adds to the value the output has, from the component or from an
automation lane, and the sum is clamped to the range of the member. It never writes
the component. `depth` is the swing either way in the units of the member; with
`scale: AudioModOctaves` it is in octaves, for a frequency (`cutoff`, `center`, the
`frequency` of a band, the `damping` of a `Delay`), so a sweep sounds even.

| Field | Meaning and default |
| --- | --- |
| `source` | `AudioModLfo` (default), `AudioModSteps` or `AudioModFollower` |
| `depth`, `scale` | `0`, `AudioModLinear` |
| `wave` | `AudioModSine`, `AudioModTriangle`, `AudioModSquare`, `AudioModSawUp`, `AudioModSawDown` or `AudioModRandom` (sample and hold) |
| `rate`, `sync`, `phase` | `1` Hz, `0` beats, `0` turns; a positive `sync` wins over `rate` |
| `unipolar` | `false`: an LFO moves 0..1 instead of -1..1 |
| `steps`, `step_count`, `smooth` | up to 16 values in -1..1, `16`, `0` (hard steps) .. 1 (fully glided); `sync` is the length of a step |
| `key`, `attack`, `release` | bus a follower measures, `0` for the bus of the target, `0.01` s, `0.1` s |

A synced modulator is phase locked to the Transport: at any frame its phase is
`phase + fract(beat / sync)`, beat being the position of the Transport at that frame,
so a seek, a loop wrap and a tempo change keep it on the grid. A stopped Transport
leaves the beat where it stands and moves it on at 120 bpm, like a synced synth LFO.
A follower measures its bus before the inserts and the volume, so a drum bus at volume
0 still keys it, and maps -60..0 dB to 0..1.

Modulators run in the audio core every 32 frames and at the edges of squares, saws,
steps and samples; a bus ramps to each value, an effect smooths it like any change,
so a sine at 10 Hz on a cutoff stays free of zipper noise. A new modulator fades in
over 50 ms. Disabling, deleting or retargeting one fades it out over 50 ms, and the
member returns to its value without a click. There are 64 modulators and up to four
on one member; each one past a limit, or with an unknown target or member, logs one
error that names it. A modulator has no stereo phase offset: every member it can
reach has one value for both channels. The `audio` command lists the modulators of
each bus and insert.

Recipes, at the tempo of the Transport:

```flecs
// Tremolo: the volume dips to 0 for the first half of every eighth
trem { AudioModulator: {target: keys, component: AudioBus, member: "volume",
  wave: AudioModSquare, unipolar: true, depth: -1, sync: 0.5} }

// Auto-pan: left and right once per bar
autopan { AudioModulator: {target: pad, component: AudioBus, member: "pan",
  depth: 0.8, sync: 4} }

// Wobble: the cutoff of a bass filter two octaves either way, in eighths
bass_filter { AudioFilter: {cutoff: 400, resonance: 0.5} }
bass { AudioBus: {effects: [bass_filter]} }
wobble { AudioModulator: {target: bass_filter, component: AudioFilter,
  member: "cutoff", scale: AudioModOctaves, depth: 2, sync: 0.5} }

// Auto-wah: a bandpass that opens with the level of its own bus
wah_filter { AudioFilter: {kind: SynthBandPass, cutoff: 300, resonance: 0.6} }
guitar { AudioBus: {effects: [wah_filter]} }
wah { AudioModulator: {target: wah_filter, component: AudioFilter, member: "cutoff",
  source: AudioModFollower, scale: AudioModOctaves, depth: 3, attack: 0.005,
  release: 0.15} }
```

#### Macros

An `AudioMacro` on an entity of its own is one value of 0..1 that moves several members
at once, each over its own range and curve, like a knob of a Combinator. Its maps are
children with an `AudioMacroMap`: `target`, `component` and `member` name a member
exactly as an [AutomationClip](#automation) and a modulator do, and the member goes
from `min` at a value of 0 to `max` at 1. A build-up is then one automation lane on one
macro instead of four lanes kept in sync by hand, and game code drives the intensity of
the music by writing one value.

| Field | Meaning and default |
| --- | --- |
| `AudioMacro.value` | `0`, clamped to 0..1 |
| `target`, `component`, `member` | The member, as of an `AutomationClip` |
| `min`, `max` | `0`, `1`, in the units of the member; a `min` above `max` inverts |
| `curve` | `0` linear; `1` exponential, the middle gives 11% of the range; `-1` logarithmic, 89% |
| `scale` | `AudioModLinear`, or `AudioModOctaves` for a frequency, with `min` and `max` above 0 |

The value of a member is `min + (max - min) * shape(value)`, or with
`scale: AudioModOctaves` `min * (max / min)^shape(value)`, even in pitch like a modulator
in octaves: 200..8000 Hz at 0.5 is 1265 Hz. The curve is
`shape(x) = (2^(6 curve x) - 1) / (2^(6 curve) - 1)`.

While a map runs, the macro decides its member: the value of the component, and of an
automation lane of the member itself, is replaced, not added to. Modulators of the member
add to the value of the macro. The macro never writes a component, so game code keeps
reading and writing the authored values. When two maps move one member, the one created
last (the higher entity id) wins; the other logs one warning that names both and does
nothing until the newer one is gone, then takes over with a fade.

The value of a macro is itself a target: an `AutomationClip` or an `AudioModulator` with
`component: AudioMacro, member: "value"` moves it, sample accurately. The macros are
expanded in the audio core, not on the main thread: the module sends each macro and its
maps to the core once, an automation lane schedules `MACRO` commands with the value at
their frame as it schedules any value, and the modulators of a macro add to its value in
the core. Every 32 frames, and at the frame of every scheduled value, the core clamps the
value plus its modulators to 0..1 and shapes it into each map, so neither automation nor
modulation writes a component. A write of `AudioMacro.value` from a script or from C goes
the same way at once, with the usual smoothing of the targets: a bus ramps over at least
5 ms, an effect smooths over its usual 10 to 20 ms. A modulator of a macro is linear; a
follower of a macro needs a `key` bus.

A new map fades in over 50 ms. Disabling or deleting a map or its macro, or retargeting a
map, fades it out over 50 ms, and the member returns to its authored value without a
click. There are 32 macros and 8 maps per macro; a macro past a limit logs one error that
names it. A map whose target, component or member is unknown, whose parent has no
`AudioMacro`, that maps the value of a macro, or in octaves of a member that is no
frequency, logs one error that names it. The `audio` command lists the macros with their
value and each map with its member, range and the value it gives.

A build-up over 16 beats, on one lane, with the music intensity also in reach of game code:

```flecs
using box.audio

synth_filter { AudioFilter: {cutoff: 1000} }
drive { Saturator: {drive: 0} }
echo { Delay: {time: 0.375, feedback: 0.2, mix: 0.3} }
room { AudioBus: {kind: AudioChannelReturn, effects: [room]}
  Reverb: {} }
synth { AudioBus: {effects: [synth_filter, drive, echo],
  sends: [{bus: room, level: 0.1}]} }

build {
  AudioMacro: {}
  open { AudioMacroMap: {target: synth_filter, component: AudioFilter,
    member: "cutoff", min: 300, max: 9000, scale: AudioModOctaves} }
  heat { AudioMacroMap: {target: drive, component: Saturator, member: "drive",
    min: 0, max: 0.8, curve: 0.5} }
  wash { AudioMacroMap: {target: synth, component: AudioBus,
    member: "sends[0].level", min: 0.1, max: 0.6} }
  climb { AudioMacroMap: {target: echo, component: Delay, member: "feedback",
    min: 0.2, max: 0.7, curve: 0.3} }
}

prefab rise {
  AutomationClip: {target: build, component: AudioMacro, member: "value",
    length: 16, curve: {keys: [{0, 0}, {16, 1}]}}
}

lanes {
  Track: {}
  build_up { ClipPlacement: {clip: rise, start: 48} }
}

// A pulse on top of the rise, in eighths
build_pulse { AudioModulator: {target: build, component: AudioMacro,
  member: "value", depth: 0.05, sync: 0.5} }
```

From game code the same macro follows the danger of the scene:
`ecs_set(world, build, AudioMacro, { .value = danger })`.
[macros.flecs](../test/assets/audio/macros.flecs) maps a cutoff and a send, sweeps them
with a lane and inverts two ranges.

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
  AudioSpatial: {min_distance: 2, max_distance: 40, cutoff: 800}
}
const shot: bool = box.audio.play_at(click, {-3, 0, -5}, 1.0)
```

The listener is an `AudioListener` with `WorldTransform`; otherwise it is the camera
of the first enabled view in render order. Tag the main camera explicitly in scenes
with render-to-texture views. Multiple listeners warn and use the lowest entity ID.
Without a listener, spatial sounds play centered at full volume.

`AudioSpatial` defaults to `min_distance: 1`, `max_distance: 50`, `rolloff: 1`,
`cutoff: 0`.
A source uses its world position, including parent transforms; without a
`WorldTransform` it plays nonspatially. On a sound entity, `AudioSpatial` supplies
distances for `play_at`, whose position stays fixed while the listener moves.

Gain is 1 within min distance and zero at max distance. Between them it follows
inverse-distance attenuation, normalized to reach zero at max:
`g(d) = min / (min + rolloff * (d - min))`, then
`gain = (g(d) - g(max)) / (1 - g(max))`.
Rolloff zero holds full gain until max distance. Pan follows the listener's right
axis and narrows to center within min distance. Front and back sound alike.

`cutoff` adds a distance filter, since air dulls distant sounds: a 12 dB per octave
low-pass, open within min distance, reaching `cutoff` Hz at max distance. Between
them it moves exponentially from 20000 Hz, so equal steps in distance sound like
equal steps in brightness: `f(d) = 20000 * (cutoff / 20000)^t` with
`t = (d - min) / (max - min)`. A `cutoff` of 0, the default, a negative one or one
of 20000 Hz or more filters nothing and costs nothing; others are clamped to at
least 10 Hz. The filter applies to sources and `play_at` one shots, each channel of
a stereo sound, after the voice's sound and before its volume and pan, and glides
to a new cutoff over 50 ms, so a fast source or a teleporting listener does not
click. A voice starts at its cutoff. There is no Doppler, occlusion or HRTF.

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
| `SynthSound` | `seed`, `pitch_variance`, `level_variance`, `drift` (0 cents, 0..50), `free_phase` (false); variances default to zero |
| `SynthOscillator` | `wave`, `frequency` (440 Hz), `frequency_end` (0, no sweep), `sweep` seconds, `level` (1), `pulse_width` (0.5), `sub` (0, 0..1), `sub_octave` (1, 1..2), `sync` (0, off, 1..16), `color` (`SynthNoiseWhite`), modulation controls, `velocity` (0) |
| `SynthEnvelope` | `attack`, `decay`, `sustain`, `hold`, `release`, `curve`, `attack_curve`, `key_scale`; all zero by default |
| `SynthDrive` | Root: `drive` (0.3, 0..1), `asymmetry` (0, 0..1) |
| `SynthFilter` | `kind`, `cutoff` (1000 Hz), `resonance` (0), `slope` (`SynthSlope12` or `SynthSlope24`), `drive` (0, 0..1), `velocity` (0 octaves), `keytrack` (0), `model` (`SynthFilterSvf` or `SynthFilterLadder`), `vowel` (0, 0..4, `SynthFormant`), `feedback` (0.5, -0.98..0.98, `SynthComb`), `tune` (1, 0.125..8, `SynthComb`) |
| `SynthFilterEnvelope` | Envelope controls plus `amount` in octaves, `velocity` (0) and `vowel` (0, -4..4, `SynthFormant`); requires a filter |
| `SynthPitchEnvelope` | `amount` (0 semitones, -48..48), `decay` seconds, `curve` (0, -16..16), `release_amount` (0 semitones, -48..48), `sync` (0 semitones, -48..48) |
| `SynthUnison` | Per layer: `voices` (3, 1..7), `detune` (10 cents, 0..100), `width` (0, 0..1) |
| `SynthGlide` | Root: `time` seconds (0, up to 10), `legato` (false), `channel` (0) |
| `SynthLfo` | `wave`, `rate` (5 Hz), `sync` (0 beats, off, 0..256), `delay`, `fade`, `phase`, `random_phase`, destination depths |
| `SynthString` | Pluck layer: `decay` (2 s to -60 dB), `brightness` (0.5), `hardness` (0.8), `velocity` (0) |
| `SynthBow` | Bowed layer: `pressure` (0.5, 0..1), `speed` (0.5, 0..1), `position` (0.12, 0.02..0.5), `brightness` (0.5), `noise` (0.1, 0..1), `velocity` (0) |
| `SynthTable` | Wavetable layer: `table` (an entity with `Wavetable`), `position` (0 frames), `envelope` (0 frames) |
| `Wavetable` | On an entity of its own, usually a prefab: `kind` (`WavetableBasic`), `sound` (0, an entity with `AudioPcm` or `SoundFile` that wins over `kind`), `frame_size` (2048 samples, 64..4096) |

Waves are `SynthSine`, `SynthSaw`, `SynthSquare`, `SynthTriangle`, `SynthNoise`,
`SynthPluck`, `SynthWavetable` and `SynthBowed`. Frequency sweeps are exponential. Positive envelope curves make
decay/release fall quickly then settle; zero is linear, negative curves start slowly.
Curve values range from -16 to 16. A sustain of zero ends after decay.
For gated notes, sustain holds until release, ignoring hold.

`sub` adds a band-limited square one or two octaves (`sub_octave`) under a layer,
for weight under basses and leads without another layer. It is added before the
layer's level and envelope, so it shares them. It is derived from the layer's
phase: it follows sweeps, glides, legato, pitch LFOs and every unison voice, but
not phase modulation or feedback. A modulator modulates with its sub included.
`SynthNoise` and `SynthPluck` ignore it.

`color` sets the color of a `SynthNoise` layer, for wind, surf, hiss and softer
percussion than white noise: `SynthNoiseWhite` (the default), `SynthNoisePink`, which
falls by 3 dB per octave, or `SynthNoiseBrown`, which falls by 6 dB per octave. Pink
filters the white noise through a cascade of poles and zeros two octaves apart, brown
through a leaky integrator and a highpass that removes its DC. The filters are set in
Hz from the sample rate, flat under 5 Hz (pink) or 10 Hz (brown), so the slope holds
at every rate. Each color is at about the RMS level of white noise at the same `level`.
The noise stays seeded per play from `SynthSound.seed`, so colored noise is as
deterministic as white, and each unison voice colors its own noise. Other waves ignore it.

`sync` hard syncs a layer, for sync leads and sync sweeps. The layer's note frequency,
with its sweep, glide, pitch LFOs, unison detune and the voice pitch, is the master;
the wave plays at the master times `sync`, a ratio from 1 to 16, and restarts at
phase 0 on every master cycle, so the sound keeps the master's pitch while the ratio
shapes its timbre. A ratio of 0 or 1 is off. The restart is band limited by a polyBLEP
sized by the jump, and a polyBLAMP for the slope of triangles and sines. Each unison
voice syncs to its own detuned master, the sub stays at the master, and phase
modulation adds to the synced phase. `SynthNoise` and `SynthPluck` ignore it.

`SynthWavetable` plays the frames of a wavetable, single periods that the layer morphs
through by a position. The `SynthTable` of the layer names the entity with the
`Wavetable`, which is shared by every sound that names it. The built-in kinds are
generated by additive synthesis, so they need no file:

- `WavetableBasic`: 4 frames, the sine, triangle, saw and square of the other waves.
- `WavetablePulse`: 16 frames of a pulse without DC, from 50% to 5% width.
- `WavetableHarmonics`: 32 frames; frame k has the first k + 1 harmonics of the saw at
  1/n, so moving the position sweeps like a lowpass.

A table authored in another tool loads from a sound: `Wavetable.sound` names an entity
with `AudioPcm` or `SoundFile` (WAV or FLAC, decoded as other sounds are), and wins over
`kind`. The common format is a WAV of N single cycle frames of 2048 samples one after the
other; `frame_size` (default 2048, clamped to 64..4096, 0 for the default) is the samples
per frame. The sound is cut into floor(frames / frame_size) frames, at most 256, the
rest left out with one warning; the samples that do not fill a last frame are ignored, as
is the rate of the file. Stereo is summed to mono, each frame loses its DC, and the table
is normalized to a peak of 1 over all its frames, so tables saved at different levels
sound alike. A forward FFT of each frame, on the main thread when the sound becomes
ready, finds its harmonics, of which it keeps those under the Nyquist of the frame, at
most 512, so a frame of other than 2048 samples is resampled to the frames of the table;
a frame size that is not a power of 2 is first resampled with a cubic spline to the next
one. The table gets the same mip levels as the built-in ones. Until the sound is ready
the table has no frames and its layers play a sine; when the sound becomes ready or
changes the table is built again, and the next play of the synth sounds that name it
takes the new table. A sound that fails to load, that is not an entity with `AudioPcm` or
`SoundFile`, or that is shorter than one frame logs one error that names the `Wavetable`,
and its layers play a sine.

```flecs
using box.audio

SawSquareFile {
  SoundFile: {path: "test/assets/audio/wavetable_saw_square.wav"}
}

prefab SawSquare {
  Wavetable: {sound: SawSquareFile, frame_size: 2048}
}
```

`position` is a frame, 0 to frames - 1; between two frames the layer plays a linear mix
of them, so 2.5 is half of frame 2 and half of frame 3. `envelope` is the frames the
position moves at level 1 of the layer's envelope, its own `SynthEnvelope` or the root's,
and `SynthLfo.position` the frames it moves at the top of the LFO wave; the moved position
is clamped to the frames of the table. Frequency, sweeps, glide, pitch LFOs, unison, phase
and ring modulation (as carrier and as modulator), `sync` and `sub` work as for the other
waves. A wavetable layer without a `SynthTable`, or whose `table` is not an entity with
`Wavetable`, plays a sine and logs one error that names the sound.

Each frame is stored as mip levels, one per octave. Level 0 holds 512 harmonics in
2048 samples; level k holds 512 / 2^k harmonics in 2048 / 2^k samples, at least 32, so
every level has four samples or more per period of its highest harmonic. The oscillator
picks, from the speed of its phase in turns per frame (the rate of the core, pitch LFOs,
unison detune, sync and phase modulation included), the first level whose highest
harmonic stays under Nyquist, and crossfades into the next level over the upper half
of each octave, so a sweep over the keyboard neither aliases nor steps. It reads with
the 4-point, 3rd-order interpolator for twice oversampled audio of Niemitalo, and the
builder divides each harmonic by the gain of that interpolator at its frequency, so it
plays at its amplitude. A saw frame swept from 50 Hz to 15 kHz at 48 kHz keeps what
folds back from over Nyquist about 68 dB under its fundamental; the tests require 60.

The main thread builds a table when its `Wavetable` is set and hands it to the core as
the frames of a sound, the way sound files go, copied into the worklet on the web; a
patch names it by that sound id. The core never builds or allocates a table. A table
that changes or is deleted is released: voices that play it keep it until they end, and
new plays take the new table, or a sine. A frame takes 4160 floats over its levels,
16.6 KB: a table of 64 frames takes about 1 MB, `WavetableHarmonics` 0.53 MB. A table
has at most 256 frames; tables take sound ids of the core, which holds 1023 sounds,
tables and synth patches together. In the `Synth` bench (`voices_64_wavetable`, 64 voices
of two `WavetableHarmonics` layers into a lowpass, 48 kHz) the voices take about 2100 ns
per sample (268 µs per 128-frame quantum), where `voices_64` takes about 780.

`SynthPitchEnvelope` bends the pitch relative to the note, so one patch fits every
pitch of an instrument, where `frequency_end` sweeps to one absolute frequency: the
drop of a kick or a tom, the blip of a zap, the scoop of brass. A note starts `amount`
semitones off and returns to the note over `decay`, with a `curve` that bends that
return as `SynthEnvelope.curve` bends a decay. From the start of the release the offset
moves from where it is to `release_amount` semitones, over the release of the layer's
amplitude envelope, its own or the root's, with the same curve. `sync` is the semitones
the ratio of hard sync starts off by, returning to the layer's `sync` with the decay:
the classic sync sweep, on layers whose `sync` is over 1 only. On the root it moves
every layer; on a layer it moves that layer in place of the root's. It adds to glides,
sweeps and pitch LFOs, moves `SynthPluck` strings as vibrato does, runs at the control
rate of the LFOs, does not keep a voice playing and is not restarted by a legato
retarget.

```flecs
using box.audio

prefab Harmonics {
  Wavetable: {kind: WavetableHarmonics}
}

prefab Morph {
  SynthSound: {}
  SynthEnvelope: {attack: 0.1, decay: 0.2, sustain: 0.6, hold: 0.2, release: 0.1}
  SynthOscillator: {wave: SynthWavetable, frequency: 220, level: 0.3}
  SynthTable: {table: Harmonics, position: 2, envelope: 20}
  SynthLfo: {wave: SynthLfoTriangle, rate: 0.5, position: 6}
}

prefab Kick {
  SynthSound: {}
  SynthEnvelope: {attack: 0.001, decay: 0.35, sustain: 0, release: 0.05, curve: 4}
  SynthPitchEnvelope: {amount: 36, decay: 0.08, curve: 6}
  SynthOscillator: {frequency: 50, level: 0.8}
}
```

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

`model: SynthFilterLadder` swaps the state variable filter for a transistor ladder:
four one-pole stages with a tanh in each stage and in the feedback loop, solved with
zero delay feedback, so it stays in tune up to the clamp of the cutoff under Nyquist
without oversampling. `SynthSlope24` is the fat 4-pole lowpass and `SynthSlope12` its
2-pole tap; `SynthHighPass`, `SynthBandPass` and `SynthNotch` mix the poles as a
multimode ladder does. Resonance 1 self-oscillates: with no input, after a pluck, it
rings as a near sine at the cutoff at a steady level, so the cutoff envelope, the LFOs
and keytrack can play it as an oscillator. A ladder loses its bass as the resonance
rises; the lowpass adds a share of its input to the feedback sum, which keeps the
passband within about 2 dB of resonance 0. `drive` is the gain into the ladder, from
-6 dB at 0 to +12 dB at 1, the output scaled back, so higher drives saturate the stages
sooner. On the stereo path both channels run their own ladder.
In the `Synth` bench (`voices_64_ladder`, 64 voices, 48 kHz, a 24 dB lowpass with an
envelope) the ladder costs about 245 µs per 128-frame quantum against about 145 µs for
the 24 dB state variable filter in the same run: about 12 ns more per voice and frame.

```
prefab Acid {
  SynthSound: {}
  SynthEnvelope: {attack: 0.002, decay: 0.2, sustain: 0.5, hold: 0.2, release: 0.1}
  SynthFilter: {cutoff: 400, resonance: 0.85, slope: SynthSlope24, model: SynthFilterLadder}
  SynthFilterEnvelope: {decay: 0.2, sustain: 0, amount: 3}
  SynthOscillator: {wave: SynthSaw, frequency: 55, level: 0.6}
}
```

`kind: SynthFormant` runs three resonant bandpasses of the state variable filter in
parallel, at the first three formants of a vowel, each at its own level: vowels,
talking pads and wah-like movement. `vowel` picks it, 0 a, 1 e, 2 i, 3 o and 4 u; a
fraction interpolates the frequencies, the levels and the widths of the two nearest
vowels, so 0.5 sits between a and e. The formants are the tenor table of the Csound
manual (Appendix, "Formant Values"):

| Vowel | Frequencies (Hz) | Levels (dB) | Widths (Hz) |
| --- | --- | --- | --- |
| 0 a | 650, 1080, 2650 | 0, -6, -7 | 80, 90, 120 |
| 1 e | 400, 1700, 2600 | 0, -14, -12 | 70, 80, 100 |
| 2 i | 290, 1870, 2800 | 0, -15, -18 | 40, 90, 100 |
| 3 o | 400, 800, 2600 | 0, -10, -12 | 40, 80, 100 |
| 4 u | 350, 600, 2700 | 0, -20, -17 | 40, 60, 100 |

The formant frequencies are fixed: `cutoff`, `slope`, `model`, `velocity` and
`keytrack` do nothing, and neither the filter envelope `amount` nor `SynthLfo.cutoff`
moves them. `resonance` narrows the widths, from those of the table at 0 to a tenth of
them at 1, and `drive` applies to each bandpass. The vowel is the modulation target
instead: `SynthLfo.vowel` moves it by that many vowels at the top of the wave and
`SynthFilterEnvelope.vowel` at level 1 of the envelope, both -4..4, and the moved
vowel is clamped to 0..4. The filter envelope runs on a formant filter whenever its
`vowel` is not 0, whatever its `amount`; its `velocity` scales the vowel depth.

The level is normalized for a saw at 110 Hz, which comes out within about 3 dB of its
level without a filter on every vowel; a higher note puts more of its power into the
formants and comes out louder. On the stereo path both channels run the same filter.
`AudioFilter` has no formant kind and takes `SynthFormant` as a lowpass.

```
prefab Talk {
  SynthSound: {}
  SynthEnvelope: {attack: 0.05, sustain: 1, hold: 1, release: 0.3}
  SynthFilter: {kind: SynthFormant, vowel: 1, resonance: 0.3}
  SynthLfo: {rate: 1.5, vowel: 1}
  SynthOscillator: {wave: SynthSaw, frequency: 110, level: 0.6}
}
```

`kind: SynthComb` is a feedback comb tuned to the note: `y = x + feedback * y` delayed
by one period of the note times `tune`. Metallic and flanged tones, resonators, and
plucks from short noise bursts come out of it. Positive `feedback` (default 0.5) puts
peaks at the harmonics of the comb frequency; negative feedback puts them at the odd
harmonics of half of it, a hollow, closed-pipe tone an octave lower. Near ±0.98 it
rings long. `tune` (default 1, 0.125..8) is the ratio of the comb frequency to the
note: 2 an octave up, 1.5 a fifth.

The note is the frequency of the first layer times the current pitch of the voice:
its pitch ratio, glide, pitch variance, bend, the `SynthPitchEnvelope` of that layer
and the pitch depth of a `SynthLfo` on every layer. Unison detune, drift and the LFOs
of single layers are left out. The delay line is read with a 3rd order Lagrange
fractional delay, so the comb stays in tune at high notes too, within a few cents at
1760 Hz.

`cutoff` damps the loop with a one-pole lowpass, so the filter envelope `amount`,
`SynthLfo.cutoff`, `velocity` and `keytrack` darken the comb over time; the phase
delay of that lowpass at the fundamental comes off the delay, so a damped comb keeps
its pitch. A cutoff of 0.49 of the sample rate or above leaves it undamped. The
default cutoff of 1000 Hz damps a comb strongly, so set 20000 for a bright one.
`resonance`, `slope`, `drive` and `model` do nothing. The output is scaled by
`1 - |feedback|`, so the peaks stay near the input level at any feedback, and broadband
input comes out quieter as the feedback rises. On the stereo path of unison with width
both channels run the same tuning. Once the envelopes of a voice end, its comb rings on
without input until its output falls under -80 dB, so a short burst of noise rings as a
pluck; a looping sound starts its next pass instead.

The core holds 32 comb delay lines, each long enough for 40 Hz (up to 81.8 kHz sample
rates; at higher rates the lowest comb frequency is the rate over 2045). A voice with a
comb takes one line, or two on the stereo path, and gives them back when it ends; a
play that finds too few free is ignored, as a pluck play without a free string is.
Combs below 40 Hz are clamped to 40 Hz. `AudioFilter` has no comb kind and takes
`SynthComb` as a lowpass.

```
prefab NoisePluck {
  SynthSound: {}
  SynthEnvelope: {attack: 0.001, decay: 0.02, sustain: 0, release: 0.01}
  SynthFilter: {kind: SynthComb, cutoff: 6000, feedback: 0.95}
  SynthOscillator: {wave: SynthNoise, frequency: 220, level: 0.5}
}
```

`SynthDrive` on the root drives the mix of the layers of each voice through a tanh
soft clipper before the filter, or before the amp without a filter: the stage where
analog synths get most of their thickness. Since it works on the mix, plucks, unison
voices and FM pass through it, and layers that play together intermodulate. `drive`
is a gain of -6 to +24 dB before the clipper; the level is compensated so that drive
changes the tone more than the loudness: a sine at level 1 keeps its RMS at every
drive, and a layer at its usual level stays within about 1.5 dB from 0.1 to 1. Quieter
mixes are driven less and get louder with drive; louder ones squash. `asymmetry` adds
an offset before the clipper for even harmonics, and a 5 Hz DC blocker after it takes
off the DC the offset makes. The clipper is the one of `Saturator` (shared in
[saturate.h](../src/audio/core/saturate.h)), run at twice the rate so high, loud
notes alias little; the driven mix is 15 frames (0.3 ms) late. On the stereo path of
unison with width, both channels drive with the same settings and their own state.
Without the component there is no drive and the sound renders as before.
In the `Synth` bench (`voices_64_pre_drive`, 64 voices, 48 kHz, drive 0.6 with an
asymmetry of 0.3) the drive raises the 64 voices from about 610 to 1200 ns per sample
(78 to 154 µs per 128-frame quantum): about 9 ns per voice and frame, which roughly
doubles the cost of a plain voice. The 2x oversampler is most of it and is costly per
voice; it is kept because a clipper at the base rate aliases audibly on high notes.

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
40 Hz warn and clamp. There are 32 strings total, one per pluck or bowed unison voice;
long ringing chords can exhaust them and cause plays to be ignored.

`SynthBowed` is the sustained counterpart of the pluck: a bowed string for violins,
violas, cellos and basses, with the bow noise, the small jitter of pitch and level and
the changing tone a saw through a filter does not have. It is a digital waveguide split
at the bow into a bridge side and a nut side (McIntyre, Schumacher and Woodhouse; Smith;
the STK `Bowed` instrument). The bow is a friction junction: the difference of the bow
velocity to the string velocity at the bow goes through a friction curve that decides
between stick and slip. The nut reflects inverted, the bridge inverted through a
one-pole lowpass that damps the loop, and the output is the wave at the bridge. Both
sides are read at fractional delays with a 3rd order Lagrange interpolation, and the
phase delay of the lowpass at the fundamental comes off the loop, so a note settles in
tune at high notes too.

The `SynthBow` of the layer sets the bow; a bowed layer without one takes the defaults:

- `pressure` (0.5): the force of the bow, the width of the stick region of the friction,
  and how sharp the bow keeps the corner of the Helmholtz motion: it moves the lowpass of
  the loop up to 1.5 octaves either way. Low gives a soft, airy tone, the middle the
  Helmholtz motion, high a harder tone with more upper harmonics.
- `speed` (0.5): the bow velocity, the level; the friction scales with it, so the tone
  stays and only the bow hair noise and the attack change with it. 0 is silent.
- `position` (0.12): the distance of the bow from the bridge as a share of the string,
  0.02..0.5. Near the bridge is brighter (sul ponticello), far darker (sul tasto); it
  notches the harmonics at the multiples of `1 / position`, so 0.2 takes out the 5th and
  the 10th. A side keeps at least 2 frames, so at very high notes the bow moves off the
  bridge a little.
- `brightness` (0.5): the damping of the loop, 0 dark to 1 bright, a lowpass at the
  bridge that follows the note, from its 4th to about its 23rd harmonic.
- `noise` (0.1): the noise of the bow hair, added at the contact point in proportion to
  the bow velocity, which the string filters into its own bow noise and jitter.
- `velocity` (0): the share of the pressure and the speed that the velocity of a note
  scales, as `SynthString.velocity` does the hardness.

The bow speed follows the amplitude envelope of the layer, its own or the root's, in
place of the level: the attack starts the bow, the decay and sustain hold it, the release
lifts it. The bow touches the string while the envelope is over 0, firmly from a level of
0.25 up. Once it lifts, the string rings out through its damping, its fundamental falling
60 dB over about 220 periods (a second at A3, at most 4 seconds), less in a bright string; the voice ends once the envelope ended
and the string is silent, as a pluck ends. A bowed layer without any envelope bows for 10
seconds, as other waves do. Glide, legato, bend, pitch LFOs, `SynthPitchEnvelope` and
drift change the length of the string smoothly without restarting it, as they move a
pluck, so vibrato and legato do not click. Phase modulation, `sync` and `sub` do not apply.
With `SynthUnison` each copy is a bowed string of its own, detuned and panned, on the mono
and the stereo path. The routes `SynthTargetBowPressure`, `SynthTargetBowSpeed` and
`SynthTargetBowPosition` move the bow, smoothed over about 5 ms, so the pressure curve of a
note (`SynthSourcePressure`), the LFOs and the mod envelope can drive it.

Bowed strings take their delay lines, one per unison copy, from the same 32 strings of the
core as plucks, each long enough for 40 Hz (up to 81.8 kHz sample rates); a play that finds
too few free is ignored with an ignored event, as a pluck play is, and a voice gives
its strings back when it ends. A bowed note holds its string for its whole envelope and the
ring out after it, so a part should keep its overlapping notes under 32 strings: a legato
`SynthGlide` plays a whole line on one. Body resonance stays out of the model: it is the
job of the bus EQ, as in the strings song of the jukebox. In the `Synth` bench
(`voices_32_bowed`, 32 bowed voices under a vibrato and two routes to the bow, through a
lowpass, 48 kHz) a bowed voice costs about 40 ns per frame (165 µs per 128-frame
quantum for all 32), against about 25 ns for a pluck under a vibrato.

```flecs
using box.audio

prefab Violin {
  SynthSound: {seed: 11, drift: 3}
  SynthGlide: {time: 0.045, legato: true}
  SynthEnvelope: {attack: 0.09, attack_curve: -1, decay: 0.3, sustain: 0.9, release: 0.25, curve: 2}
  SynthLfo: {rate: 5.8, delay: 0.16, fade: 0.4, random_phase: true, pitch: 20}
  SynthOscillator: {wave: SynthBowed, frequency: 440, level: 0.5}
  SynthBow: {pressure: 0.45, speed: 0.6, position: 0.11, brightness: 0.55, noise: 0.12, velocity: 0.5}
  swell { SynthModRoute: {source: SynthSourcePressure, target: SynthTargetBowPressure, amount: 0.4} }
  push { SynthModRoute: {source: SynthSourcePressure, target: SynthTargetBowSpeed, amount: 0.3} }
}
```

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
- `SynthBow.velocity` scales the pressure and the speed of a bow the same way, so soft notes
  are bowed lighter and quieter.

Plays that are not notes, such as `AudioSource`, `box_audio_play`, quantized plays and
streams, play at velocity 1. Sample voices ignore velocity beyond its gain.

The pitch of a note reaches the voice too, as key tracking on analog synths. The key of
a voice is `12 * log2(ratio)` semitones, where the ratio is the pitch of its play without
the `pitch_variance` of `SynthSound`. For an `Instrument` note the ratio is
`2^((pitch - root) / 12)`, so the key is relative to `Instrument.root`; for any other play
it is relative to a ratio of 1, the pitch of an `AudioSource` or of `box_audio_play`. Both
fields default to 0, which leaves the sound as it was:

- `SynthFilter.keytrack`, 0..1, multiplies the cutoff by `ratio^keytrack`, so 1 moves the
  cutoff 1:1 with the pitch and 0.5 half an octave per octave. It follows the current pitch
  of the voice: a glide, a change of `AudioSource.pitch` or a legato retarget moves the
  cutoff along, at the control rate of the LFOs. It adds to the filter envelope, the
  velocity and the LFOs.
- `SynthEnvelope.key_scale`, 0..1, multiplies the decay and the release by
  `2^(-key_scale * key / 12)`, fixed at the start of the note, so at 1 they halve per
  octave above the root and double per octave below. It applies to the root envelope and
  to the envelopes of layers, clamped to the limits of an envelope; the filter envelope
  has no key scaling.

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
(cents), `amplitude` (0..1), `cutoff` (octaves), `vowel` (vowels of a `SynthFormant`
filter, -4..4), `pulse_width`, `hard_sync`, the
semitones (-48..48) the ratio of hard sync moves at the top of the wave, clamped to
1..16, and `position`, the frames (-256..256) the position of wavetable layers moves.
On a layer `hard_sync` syncs that layer, from a ratio of 1 when it has none; on the root
it moves every layer whose oscillator `sync` is over 1.
Delay and fade bring modulation in gradually. Random phase and noise are seeded,
so the same sequence of plays is reproducible.

A positive LFO `sync` locks it to the tempo, like a delay `sync`: the cycle is `sync`
beats of the Transport, a beat a quarter note, at 120 bpm without one, and it wins over
`rate`. A sixteenth is 0.25, a dotted quarter 1.5, a bar of 4/4 is 4. Each voice starts
the LFO at `phase + fract(beat / sync)`, where beat is the position of the Transport at
the start frame of the voice: of the timeline for notes it schedules, else its position
while stopped and the beat of the play's frame while playing. So every note sees the
same phase of the grid, and wobbles, tremolos and filter pulses stay on the beat. A
voice keeps the tempo it started at: a tempo change while it holds does not reach it,
only the next notes. Delay and fade stay in seconds, and the random wave steps once per
cycle. A retargeted legato voice keeps its LFOs.

`SynthSound.drift` adds analog drift, so digital patches are never perfectly in tune.
The pitch of each layer of a voice, and of each unison copy, wanders on its own smooth
random walk within ±`drift` cents, moving over roughly 0.2 to 2 seconds; the cutoff of
the filter drifts per voice by a quarter of it in cents. Pluck strings follow the drift
as they follow vibrato. It adds to the pitch LFOs and pitch envelopes, at their control
rate. `SynthSound.free_phase` starts the oscillators of each play, and their subs, at a
random phase instead of 0; unison copies always start at random phases. Both are seeded
from `seed` and the count of plays, so the same sequence of plays renders the same; at
their defaults a sound renders exactly as without them.

`SynthGlide.time` moves pitch exponentially in frequency. With `legato: true`,
notes on the same channel retarget a held voice without restarting its envelopes.
They keep the original waves, levels, filters and variances; use patches from the
same template so layer order agrees. A note after release starts a new voice.
Instrument gates end a legato phrase; one shots instead hold for the authored hold
after the last note. A retargeted voice keeps the velocity of the note that started it,
since its envelopes do not restart. Quantized legato plays start new voices.

The expression of a note (`Note.bend`, `mod` and `pressure`, see Music on the timeline)
reaches its synth voice only. The bend multiplies the pitch of the voice by
`2^(bend / 12)` on the same path as its pitch and glide, so oscillators, sweeps, pluck
strings and key tracking follow it, and a glide that runs keeps going. Hardwired,
`mod` scales the pitch depth of the LFOs, the vibrato (1 without a curve, so a
sound keeps its vibrato), and `pressure` moves the cutoff up by `pressure * 2` octaves
(`BOX_AUDIO_SYNTH_PRESSURE_OCTAVES`, 0 without a curve); a formant filter ignores it.
A sound with a `SynthModRoute` from `SynthSourceMod` turns the hardwired vibrato depth
off, and one from `SynthSourcePressure` the hardwired cutoff, so the route alone decides
what they do (see the modulation matrix below). A
retargeted legato voice takes the expression of the new note. The voice takes the values
of the ramps every 8 frames while they move; a note without expression renders exactly
as before.

### Modulation matrix

The fixed routes above (the depths of `SynthLfo`, `SynthFilterEnvelope.amount`, the
`velocity` and `keytrack` fields) cover the usual patches. A `SynthModRoute` adds a free
route on top of them: the value of a `source` times an `amount` moves a `target`. Each
route is an entity of its own, a child of the root, where it moves every layer, or a child
of a layer, where it moves that layer and takes the amp envelope of that layer as its
source. The cutoff, resonance, vowel and pan are of the sound either way. A sound has up to
`BOX_AUDIO_SYNTH_ROUTES` (8) routes: the children of the root first, then those of each
layer in the order of the layers; more log one error that names the sound and are left
out. Routes run once per control block, every 8 frames like the LFOs, and add to what the
fixed routes give (the LFOs, the pitch envelope and the filter envelope); the pan, the
level, the ratio of sync and the position ramp over the block so a step does not click. A
route to a target that does not apply to its layer or to the sound does nothing, without
an error.

```
lead {
  SynthSound: {}
  SynthEnvelope: {attack: 0.01, sustain: 1, release: 0.3}
  SynthFilter: {kind: SynthLowPass, cutoff: 400}
  SynthModEnvelope: {attack: 0.2, decay: 0.5, sustain: 0.2, release: 0.3}
  SynthOscillator: {wave: SynthSaw, frequency: 220, level: 0.4}
  SynthLfo: {rate: 0.3}
  wobble { SynthLfo: {rate: 6} }   // a child of the root: the second LFO
  sweep { SynthModRoute: {source: SynthSourceModEnvelope, target: SynthTargetCutoff, amount: 4} }
  drift { SynthModRoute: {source: SynthSourceLfo1, target: SynthTargetPan, amount: 0.6} }
}
```

| Source | Value |
|---|---|
| `SynthSourceAmpEnvelope` | The amp envelope, 0..1: of the layer for a route on a layer, else the `SynthEnvelope` of the root (of the first layer when the root has none) |
| `SynthSourceFilterEnvelope` | The `SynthFilterEnvelope`, 0..1; it runs for the route even without a `SynthFilter` |
| `SynthSourceModEnvelope` | The `SynthModEnvelope` of the root, 0..1 |
| `SynthSourceLfo1`, `SynthSourceLfo2` | The first and second LFO of the patch, -1..1, after their delay and fade, in the order of the LFOs above: the root, the layers, the other children |
| `SynthSourceVelocity` | The velocity of the note, 0..1, 1 for a play that is not a note |
| `SynthSourceKey` | The key of the note in octaves from the root, the key of `keytrack` / 12 |
| `SynthSourceRandom` | A random value per note, -1..1, seeded from `SynthSound.seed` and the count of plays |
| `SynthSourceMod`, `SynthSourcePressure` | `Note.mod` (1 without a curve) and `Note.pressure` (0 without), 0..1; a route from either turns its hardwiring off |

| Target | Amount at a source value of 1 |
|---|---|
| `SynthTargetCutoff` | Octaves of the cutoff, -10..10; nothing on a `SynthFormant` filter |
| `SynthTargetResonance` | Share of the resonance, -1..1, the result clamped to 0..1; nothing on a `SynthComb` |
| `SynthTargetIndex` | Radians of the index of a `SynthPhase` modulator, -32..32, or the depth of a `SynthRing` one, clamped as `index`; nothing on a layer that does not modulate |
| `SynthTargetPitch` | Semitones, -48..48 |
| `SynthTargetPan` | Pan of the voice, -2..2, on top of its own, the result clamped to -1..1 and smoothed; a sound with a route to the pan renders in stereo |
| `SynthTargetDetune` | Cents of the unison detune, -100..100, the result clamped to 0..100 |
| `SynthTargetPulseWidth` | Share of the period of the pulse width of squares, -0.49..0.49 |
| `SynthTargetLevel` | Share of the level of the layer, -4..4: the level is times `1 + amount * value`, clamped at 0, and smoothed |
| `SynthTargetHardSync` | Semitones of the ratio of `sync`, -48..48, the result clamped to 1..16; nothing on a layer without sync (a ratio over 1 or a `hard_sync` LFO of the layer) |
| `SynthTargetPosition` | Frames of the position of a `SynthWavetable` layer, -256..256, the result clamped to 0..N-1 of its table; nothing on a layer that is not a wavetable |
| `SynthTargetVowel` | Vowels of the `SynthFormant` filter, -4..4, the result clamped to 0..4; of the sound on a layer too; nothing without a `SynthFormant` filter |
| `SynthTargetBowPressure` | Share of `SynthBow.pressure`, -1..1, the result clamped to 0..1; nothing on a layer that is not `SynthBowed` |
| `SynthTargetBowSpeed` | Share of `SynthBow.speed`, -1..1, the result clamped to 0..1; nothing on a layer that is not `SynthBowed` |
| `SynthTargetBowPosition` | Share of the string of `SynthBow.position`, -0.5..0.5, the result clamped to 0.02..0.5; nothing on a layer that is not `SynthBowed` |

`SynthModEnvelope` is a third envelope on the root with the fields of `SynthEnvelope`.
It runs only when a route uses it and does not keep the voice playing, as the filter
envelope. A sound without routes renders exactly as before, whatever its
`SynthModEnvelope` holds. In the `Synth` bench (`voices_64_routes`, 64 voices, 48 kHz,
the patch of `voices_64_lfo` with 8 routes of every target but the index and the detune
and the mod envelope) the matrix raises the 64 voices from about 1350 ns per sample
(`voices_64_lfo`) to 1500, about 11%.

Working examples include [synth_examples.flecs](../test/assets/audio/synth_examples.flecs),
[synth_unison.flecs](../test/assets/audio/synth_unison.flecs),
[synth_lfo.flecs](../test/assets/audio/synth_lfo.flecs),
[synth_glide.flecs](../test/assets/audio/synth_glide.flecs),
[synth_pluck.flecs](../test/assets/audio/synth_pluck.flecs),
[synth_bowed.flecs](../test/assets/audio/synth_bowed.flecs),
[synth_sub.flecs](../test/assets/audio/synth_sub.flecs),
[synth_sync.flecs](../test/assets/audio/synth_sync.flecs),
[synth_wavetable.flecs](../test/assets/audio/synth_wavetable.flecs),
[synth_drive.flecs](../test/assets/audio/synth_drive.flecs),
[synth_drift.flecs](../test/assets/audio/synth_drift.flecs),
[synth_pitch.flecs](../test/assets/audio/synth_pitch.flecs) and
[synth_mod_matrix.flecs](../test/assets/audio/synth_mod_matrix.flecs).

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

`spread` (0..1, default 0) spreads the notes of a synth or a sampler over the
stereo field without a pan per note. Its offset is added to the note's `pan` and
the sum is clamped to -1..1. `spread_mode` picks the offset:

| Spread mode | Offset |
| --- | --- |
| `InstrumentSpreadPitch` (default) | `spread * clamp((pitch - root) / 24, -1, 1)`: low notes left, high notes right, full two octaves from the root |
| `InstrumentSpreadRandom` | `spread` times a random value in -1..1 per note, seeded from the instrument and its note count, so a song renders the same every time |
| `InstrumentSpreadAlternate` | `+spread` and `-spread` on successive notes |

A `NoteClip` has a `length` (4 beats) and notes with `start`, `duration`, `pitch`,
`velocity` (1), `pan` (0) and the expression curves `bend`, `mod` and `pressure` (none).
A `Track` supplies the instrument and brings an `AudioBus`. Its child `ClipPlacement` entities arrange clips:

| Placement field | Meaning and default |
| --- | --- |
| `clip` | Reusable `NoteClip`, `AudioClip` or `AutomationClip` |
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

A note carries expression in the style of MPE, sent to its voice only: `bend` in
semitones (-48..48), `mod` and `pressure` (0..1), each a `Curve` whose key times are in
beats from the start of the note. A curve without keys is no expression on that channel.
The voice starts at the value of each curve at the start of the note, then follows it
with timed ramps: one per linear span, and linear ramps of at most 10 ms for each span of
a Catmull-Rom or Bezier curve, up to the end of the note; past its last key a curve holds.
The ramps are scheduled in the lookahead window like the notes, at the beat at which each
starts, and a stop, a seek, a jump and the deletion of a track or placement cancel them
with the notes. A bend also plays on a sampler; `mod` and `pressure` reach synth voices
only (see Synth sounds). A legato note gives its expression to the voice it shares, and a
note without expression resets it.

```flecs
riff {
  NoteClip: {length: 4, notes: [
    // a slide up a fifth over the first beat, then a held note
    {start: 0, duration: 2, pitch: 57, bend: {keys: [{0, -7}, {1, 0}]}},
    // a vibrato that swells in over the note, and pressure that brightens it
    {start: 2, duration: 2, pitch: 60, mod: {keys: [{0, 0}, {2, 1}]},
      pressure: {kind: CurveCatmullRom, keys: [{0, 0}, {1, 0.8}, {2, 0.3}]}}
  ]}
}
```

A `Track` can groove its notes instead of playing them quantized. `swing` (0..0.75,
default 0) works on `swing_grid`: `TrackSwing8` (default, eighths, a step of 0.5
beat) or `TrackSwing16` (sixteenths, 0.25 beat). Steps are counted from beat 0 of
the timeline, not of the clip, so clips placed anywhere swing alike. Within each
pair of steps a start `p` before `step` goes to `p * (1 + swing)`, and one after it
to `step * (1 + swing) + (p - step) * (1 - swing)`: the off-beat moves later by
`swing * step` and the downbeats stay put. A note keeps its duration.

`humanize` adds jitter: `timing` (0..50 ms) moves each start by a uniform random
offset up to that far either way, and `velocity` (a share, 0..1) scales the
velocity by `1 + r * velocity`, clamped to 0..1. The jitter is seeded from the
track, the placement, the pass of the clip and the note, so a render is
deterministic and each loop pass differs. A note never starts before its pass of
the clip began, or before the frame the transport started or seeked to, and still
ends where the rules above say. A track with humanized timing schedules its notes
as much earlier than others as its `timing`, so a note that moves earlier still
plays on time. Audio clips neither swing nor humanize.

An `AudioClip` has `sound`, `offset` in **seconds**, and `length` in beats. It uses
`AudioPcm` or `SoundFile`, and needs no instrument. A track can mix note and audio
placements. Placement offsets remain in beats; transpose changes pitch and speed
together. Audio is not stretched to tempo. Each looping pass starts a new voice;
its seam uses the 5 ms stop fade. A seek or start within an audio placement resumes
inside the relevant pass, unlike note clips.

### Automation

An `AutomationClip` moves one float member of a bus or an effect over beats: a
filter sweep, a send that opens for a chorus, the fade of a track. It is placed
with a `ClipPlacement` on a `Track` like any clip; `start`, `duration`, `offset`
and `loop` work the same, `gain` and `transpose` are ignored, and a track of only
automation needs no instrument.

| Field | Meaning and default |
| --- | --- |
| `target` | The entity with the `AudioBus` or the effect component |
| `component` | `AudioBus` or an effect component, such as `AudioFilter` |
| `member` | Path of a float member: `cutoff`, `volume`, `sends[1].level`, `bands[2].gain` |
| `length` | Beats of a pass, 4 |
| `curve` | A [Curve](curves.md): time in beats from the clip start, value in member units |

```flecs
using box.audio

synth {
  AudioBus: {effects: [synth]}
  AudioFilter: {cutoff: 1000}
}

prefab sweep {
  AutomationClip: {target: synth, component: AudioFilter, member: "cutoff",
    curve: {kind: CurveCatmullRom, keys: [{0, 200}, {2, 1200}, {4, 5000}]}}
}

lanes {
  Track: {}
  rise { ClipPlacement: {clip: sweep, start: 16, duration: 16, loop: true} }
}
```

An `AudioBus` automates `volume`, `pan`, `width` and the `level` of its sends; the
master automates its `volume`, `pan` and `width`. An effect automates every float
member of its effect component: Reverb, Equalizer bands, AudioFilter, Compressor,
Gate, Delay, Phaser, Chorus, Saturator, Rotary and later effects. An
[AudioMacro](#macros) automates its `value`, which moves all its maps. Kinds, slopes,
stages, voices, entities and bools are not continuous and not automatable. A clip whose
target, component or member is unknown or not automatable logs one error that
names it and does nothing until it changes.

While the transport plays, each lane samples its curve within the lookahead window
every 10 ms of output and at its keys, and schedules every value at its frame. A
bus ramps to each value as it does to any set. An effect gets a copy of its
component with the automated members written, through the same parameter builder as
any change of the component, and smooths the step over its usual 10 to 20 ms.
Several lanes may move members of one effect; on one bus parameter the lane that
starts last wins. At a loop wrap the lanes go on from `loop_start`. A seek, a stop
and a change of the clip, the placement, the track or the component of the target
cancel what was scheduled ahead and schedule again.

The automation owns what the output hears, never the component. Game code reads
and writes the authored value, which the automation does not change. Where no lane
covers a target, and while the transport is stopped, the output has the value of
the component: the end of a placement and a stop send it again. A write of the
component while a lane runs applies at once and holds until the next value of the
lane, at most 10 ms later. Scheduled values travel as `BUS_SET` and
`EFFECT_PARAMS` commands with a frame, and a `CANCEL` command drops them; see
[core.h](../src/audio/core/core.h).
[automation.flecs](../test/assets/audio/automation.flecs) sweeps a filter, fades a
track and opens a send.

For a complete mixed song with samples, returns, ducking and a quantized game
stinger, see [song.flecs](../test/assets/audio/song.flecs). Wait for its sound files
to decode before setting transport playing. A quantized play uses `None`, `Beat`
or `Bar` in script, or `AudioQuantizeNone`, `AudioQuantizeBeat`, `AudioQuantizeBar`
in C. With no playing transport it starts immediately.

### Song files

A song can live in one `.flecs` file that the engine loads, plays and unloads as a
whole. One song is loaded at a time, and a song that is not loaded holds nothing
in the engine: its buses, effects, synth and sample sounds, instruments, clips,
tracks, voices and meters exist only while it is loaded. Scenes and tests that
drive `Transport` and `Track` directly keep working; song files are an addition.

```flecs
using box.audio
using box.material

box { audio { Song { Rgb: {255, 60, 200} } } }

neon { AudioBus: {output: master}; AudioMeter: {} }
nr_bass { Track: {instrument: nr_bass_inst}; AudioBus: {output: neon}; AudioMeter: {} }
nr_lead { Track: {instrument: nr_lead_inst}; AudioBus: {output: neon}; AudioMeter: {} }

$ {
  Song: {
    title: "Neon Run", tempo: 108, beats_per_bar: 4, beat_unit: 4, beats: 64,
    lanes: [{name: "Bass", bus: nr_bass}, {name: "Lead", bus: nr_lead}]
  }
}
```

Play it by setting the `AudioSong` singleton, `$ { AudioSong: {file: "songs/neon_run.flecs"} }`,
or with `box_audio_song_load(world, file)` and `box_audio_song_unload(world)` in C,
or `audio song <file>` in the shell (`audio song` shows it, `audio song none`
unloads it). The path is found as the one of a `SoundFile`. A new file swaps the
song; an empty file or removing `AudioSong` unloads it.

| Component | Meaning |
| --- | --- |
| `Song` | Singleton the song sets: `title`, `tempo`, `beats_per_bar`, `beat_unit`, `beats` (its length), `loop` (true) and `lanes`, the `{name, bus}` channels a host meters. Default value without a song |
| `AudioSong` | Singleton with the `file` of the song to play |
| `AudioSongStatus` | Written by the engine: `SongLoading`, `SongReady`, `SongFailed` with `error`, or `SongNone`; `file`; `foreign` counts the host entities the song changed |

The song entity is `box.audio.Song`, the entity of the singleton. A song file
sets it and puts what describes the song on it, such as its `Rgb`, with
`box { audio { Song { ... } } }`; hosts read the colour there, and it is only
there while a song is loaded. The song does not create the entity, so the
engine resets it: on unload `Song` returns to its default and every component
the song added is removed, before the entities of the song are deleted, so a
host that reads the buses of `Song.lanes` never sees a deleted entity. The
singleton always exists, so host scripts that read it stay alive. No script
can own `box.audio.Song`.

A script resolves a name only after the entity is declared, so a song file sets
`Song` at its end, after the buses its `lanes` name.

On load the engine applies the `Song` to the `Transport`: tempo, time signature,
position 0 and, with `loop` and a positive `beats`, a loop over `0..beats`. Song
files do not write the `Transport`, and every song starts at beat 0. While the
song loads and its sound files decode the status is `SongLoading` and the
transport does not run, even with `playing` true; once ready it plays when
`Transport.playing` is true.

The song is a managed script owned by the engine, `box.audio.songs.<name>`,
evaluated in the root scope like a scene, so it resolves the names of the host:
it may send to host returns and read exported variables such as a reverb level.
It must not change host entities. A song that sets a component on an entity it
did not create, or names an entity that existed before it, is reported with a
warning and in `AudioSongStatus.foreign`, and that change stays after unload.
A song file may `include` other files; they become managed scripts next to it
under `box.audio.songs` and unload with it.

Unloading clears the managed scripts of the song, which deletes every entity
each created, and deletes them. A swap runs over several frames, at
`SongLoading`: the voices of the old song stop with a short fade and its buses
mute, so no tail rings on the master; the song unloads; the engine waits until
every bus, effect and sound slot it held is reusable (a freed bus or effect
slot is reusable from the next frame, a sound id once the core reports its
free), at most five seconds; only then does the new song load. Changing the
file again during a swap goes straight to the newest file. A song that needs
more buses, effects, sounds or synth sounds (the core holds 256 synths) than
are free is unloaded before it allocates and fails with an error that names its
count and the limit; a song that does not
parse or evaluate fails and leaves nothing behind.

Editing a loaded song reloads it the same way: the engine watches the files of
the song and its includes, and follows an evaluation of its script by someone
else, such as an editor, with a staged reload of that code. The position starts
over at beat 0.

The jukebox scene ([jukebox.flecs](../etc/scenes/jukebox.flecs)) keeps one file
per song in `etc/scenes/jukebox/songs/` and a list of their names; adding a song
is adding its file and its name.

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
| `audio solo <bus> [on\|off]` | Set or toggle the solo of a bus |
| `audio solo none` | Clear every solo |
| `audio mute <bus> [on\|off]` | Set or toggle the mute of a bus |
| `audio bypass <effect> [on\|off]` | Set or toggle the bypass of an effect entity |
| `audio note <sound\|instrument> <note> [vel] [dur]` | Play one note now, velocity 0..1 (default 1), for `dur` seconds (default 0.5) |
| `audio capture <seconds>` | Record master output as a 16-bit stereo WAV resource |
| `audio render <subject> [options]` | Render a sound, instrument, bus stem or song section on its own, faster than real time, to a WAV resource |
| `audio song [<file>\|none]` | Show the song, load a song file or unload it |
| `audio analyze <wav> [from] [to]` | Print a text report of a WAV resource or file, optionally of the seconds from..to |
| `audio plot <wav> [kind] [options]` | Draw a PNG resource of a WAV resource or file: spectrogram, waveform, spectrum or loudness |
| `audio meter [bus...] [seconds\|bars=a..b]` | Measure peak, RMS and loudness per bus and the gain reduction of its effects over a span of the live output; run again without arguments for the table |
| `audio notes <track> [bars=a..b] [roll]` | Print the notes of a track per bar, or as a piano roll |
| `audio chords [track...] [bars=a..b]` | Print the chords and the key of a section from the notes |

Solo, mute and bypass judge one element of a mix without editing the song. Solo and
mute write `AudioBus` and bypass adds or removes `Disabled` on the effect entity, as a
script or `set` would; bypass fails for an entity that is not an effect. `audio mute`
without a bus, or with `on` or `off`, is still the master mute. These are runtime
changes like any `set`: a song reload resets them. `audio` marks soloed and muted buses
and bypassed effects.

`audio note` plays one note on the live output, through the bus of the instrument when
it has an `AudioOutput`, else of its sound, and releases it after `dur` seconds. A note is
a name, `C2`, `F#3` or `Bb4` with `C4` at 60, or a MIDI number 0..127. A sound without an
instrument plays through an instrument of its own in `box.command.audition`, with the
default root of A4, so `A4` plays the sound as authored. The note does not touch the
transport or the song; capture it with `audio capture` started in the same frame.

Fetch a capture with `GET /shell/session/<id>/resource/<name>`; it stays empty
until recording finishes. In the browser it writes `/tmp/audio_capture_<n>.wav`
in the page filesystem and returns that path. A script can capture from load
with `box.audio.capture(seconds, path)`.

`audio render` renders one thing on its own, without waiting for real time and
without the rest of the scene:

| Subject | Options | What it renders |
| --- | --- | --- |
| a sound (`SynthSound`, `SoundFile`, `AudioPcm`) | `note`, `vel`, `dur`, `tail` | One note of the sound, on the bus of its `AudioOutput` |
| an instrument | `note`, `vel`, `dur`, `tail` | One note through the instrument and its bus chain |
| a bus | `bars`, `beats`, `solo`, `mute`, `tail`; `dur` without a song | The song with only that bus audible, as its solo makes it, with its returns; without a song the scene from its start, `dur` seconds (10) |
| `song` | `bars`, `beats`, `solo`, `mute`, `tail` | The loaded song, or the `Transport` of the scene with a range |

Options are `key=value`: `note` is a name (`C2`, `F#3`, `Bb4`, C4 is 60) or a
MIDI number, by default the root of the instrument, A4 for a sound, which plays
it as authored; `vel` 0..1, default 1; `dur` the seconds the note is held before
its release, by default the natural length of the sound (no gate); `tail` the
seconds rendered after the release or the end of the section, by default until
the level stays under -90 dB for a note and 0 for a section. `bars=a..b` counts
bars from 1 and includes bar b, `beats=a..b` is the beats of the `Transport`
from 0; without a range the whole song (`Song.beats`). `solo` and `mute` take
buses separated by commas.

```
audio render kick note=C1 vel=0.8
audio render bass_synth note=A1 dur=0.5 tail=1
audio render drums bars=5..8
audio render song bars=9..12 mute=lead
```

The render runs in an offline world: box imported again, the managed scripts of
the scene and the song file evaluated again, rendered by the same box.audio sync,
sequencer and core on a null transport at 48000 Hz and fixed deltas of 1/60 s.
The world that plays is only read: its transport, song, components and output do
not change, and a frame spends at most 8 ms of wall time on a render so the live
sequencer keeps its lookahead. The offline world loads its sound files without an
output, so every render starts from an empty core, and two renders of a subject
with the same options are bit identical. Effects with memory, such as reverbs and
delays, start empty. A section that starts after bar 1 is played from beat 0 and
the frames before it are dropped, so the tails of earlier bars, automation and
modulators are as in playback (at most 600 s of song before a section); a section
matches a headless `audio capture` of it within an RMS difference of about 1e-5,
the rounding to 16 bits. In the browser the render also runs on the main thread,
not in the worklet, and writes `/tmp/audio_render_<n>.wav`. A render
of a note silences the sources, streams, emitters and the timeline of the scene.
Only what scripts and the song file make is rendered again: a subject made in C
is an error, except a sound with `AudioPcm`, whose samples are copied. A render
takes at most `BOX_AUDIO_CAPTURE_MAX` seconds; a longer one, a subject that is
not a sound, instrument or bus, or a section without a song and range is an
error that names why. To analyse a render, pass the resource it returns to the
next command, in two steps.

Headless with fixed `--delta-time`, repeated runs capture bit-identical audio on
the same build. Use this to render music, compare changes or inspect scenes offline.
`PerfStatus.audio_load` records the largest load of the run; worklet load reports
zero because it has no suitable high-resolution timer.

`audio analyze` reads a resource of the session (the name `audio capture` returns), else a
WAV file (PCM of 8 to 32 bits or float, mono or stereo); in the browser, a file of the page such
as a capture. It prints one line per measure, each number with its unit:

| Line | Measures |
| --- | --- |
| `loudness` | Integrated loudness (BS.1770 K-weighting, EBU R128 gating) in LUFS, loudness range in LU, short-term (3 s) and momentary (400 ms) maxima |
| `peak` | Sample peak and true peak (4x oversampled) in dBFS, RMS, crest factor, clipped samples (at full scale next to another one) |
| `dc offset` | Mean of each channel, full scale 1 |
| `bands` | Energy of sub 20-60, low 60-250, low-mid 250-500, mid 500-2k, presence 2k-6k, brilliance 6k-12k and air 12k-20k Hz in dB of the total |
| `spectrum` | Spectral centroid and 85% rolloff in Hz |
| `stereo` | L/R correlation, side over mid in dB, loudness lost when summed to mono |
| `onsets` | Count and the first 16: time and rise in dB |
| `attack`, `tail` | 10% to 90% rise of the first onset; time from the peak after the last onset to 60 dB below it, or how far it fell when the span ends first |
| `silence`, `clicks` | Leading and trailing time under -60 dBFS; a first or last sample over -40 dBFS |
| `discontinuities` | Sample steps far above the local slope: count and the first 8 |
| `pitch` | Fundamental in Hz with note and cents, its spread in cents, harmonic to noise ratio and voiced share, or no stable pitch |

A capture that starts while a song plays stores the beat, tempo and bar length it started at in a
`bxsg` chunk of its WAV. Its report ends with a line per bar (64 at most): the loudness of the
bar in LUFS and the band that changed the most against the whole span, counting a band at
40 dB under the total as at least that. A tempo change or loop wrap during the capture is not
followed. The same WAV and span give the same text. The measures are a plain C library,
[analyze.h](../src/audio/analyze.h).

`audio plot` reads a WAV as `audio analyze` does and returns the name of a PNG resource, as
`audio capture` does; in the browser it writes `/tmp/audio_plot_<n>.png` in the page filesystem
and returns that path. The image is drawn on the CPU, so it works headless and in the browser,
and the same WAV and options give the same PNG bytes. Its first line names the kind, the WAV,
its rate and the span.

| Kind | Image |
| --- | --- |
| `spectrogram` (default) | Level per time and frequency: one Hann windowed FFT of about 85 ms per column, a log axis of 20 Hz to 20 kHz, colors from black at `min_db` to pale yellow at 0 dB (a full scale sine), with the scale on the right |
| `waveform` | A lane per channel with the peak (dark) envelope of the frames of each column and the RMS (light) of at least 20 ms around it, -1 to 1 |
| `spectrum` | Average level over the span per log frequency in dB, with the band edges and names of `audio analyze` |
| `loudness` | Short-term (3 s, bright) and momentary (400 ms, dim) loudness over time in LUFS |

| Option | Meaning |
| --- | --- |
| `size=WxH` | Image size in pixels, 240x160 to 4096x4096, default 1200x600 |
| `from=<s>`, `to=<s>` | Plot the seconds from..to of the WAV, default all of it |
| `min_db=<dB>` | Bottom of the scale, default -100 dB, or -60 LUFS for `loudness`; keep it fixed to compare images |

Axes are labeled in Hz, s, dB and LUFS. A capture of a song (a `bxsg` chunk) also gets bar
lines with the bar numbers above the plot and, when there is room, beat lines, from the beat and
tempo the capture started at. A spectrogram shows masking, muddy low-mids, harsh highs, cut
tails and clicks (thin vertical lines) at a glance.

`audio meter` measures the buses for gain staging. It renders in real time: the first call
starts a measurement of the live output over `seconds` (default 3, at most 600) and returns at
once; `audio meter` without arguments prints the progress, and once the span is measured the
table, which it prints only once. Without buses it measures every bus of the processing order
and the master. With `bars=a..b` and a loaded song it seeks the transport to bar `a`, plays and
measures the length of the bars at the tempo of the transport. A call with arguments replaces a
measurement that runs. It needs an output that runs, the null output of a headless run
included; the AudioMeter of the entities, and which entities have one, stay as they are.

```
> audio meter drums bass 2
meter: measuring 2.000 s of the live output in real time: drums, bass
run audio meter after 2.0 s for the table
> audio meter
meter: live output, 2.000 s at 48000 Hz
drums: peak -4.1 dBFS, rms -15.2 dB, integrated -14.8 LUFS, short-term max -14.6 LUFS
  insert 0 glue compressor: reduction avg 2.3 dB, max 5.1 dB
  insert 1 grit saturator: never engages
bass: peak -9.6 dBFS, rms -17.0 dB, integrated -19.9 LUFS, short-term max -19.7 LUFS
headroom: 4.1 dB, loudest drums at -4.1 dBFS; master limiter max reduction 0.0 dB
```

A bus is measured at its output, after its inserts, volume, pan and mute, and the master
before its limiter. The peak is the sample peak; the RMS is over both channels. The loudness
uses the K-weighting of BS.1770, which the core runs on the measured buses only while a
measurement runs, and the gating of `audio analyze` over 100 ms hops: integrated in LUFS, and
the largest short-term (3 s) loudness, over the whole span when it is shorter. Each Compressor,
Gate, PatternGate and Saturator in the inserts of a bus prints its average and largest gain
reduction in dB, `never engages` under 0.1 dB, or `did not run`. The headroom line is how far
the loudest measured bus is under 0 dBFS, and the largest gain reduction of the limiter of the
master, from the peaks over its threshold. A bus that never rendered says so. Measured through
the null output at a fixed delta time, the same scene gives the same table.

`audio notes` and `audio chords` read only the note data of the tracks, their
placements and clips, so they work without an output and without a device. A
note prints once per placement and pass of a loop, at its programmed position,
before swing and humanize; disabled placements are left out. Bars count from 1
in the time signature of the `Song`, else of the `Transport`, and `bars=a..b`
(or `bars=a`) picks a section; without it the bars run to the end of the song or
of the last note.

```
> audio notes lead
notes of lead: pitched, instrument lead_inst, 4/4 at 120 bpm, bars 1..4, 6 notes; ...
bar 1: 1.1.000 C5 1/4 v0.80, 1.2.000 E5 1/8 v0.60, 1.2.240 G5 1/8 v0.60
bar 2: -
> audio notes duo roll bars=1
roll of duo: pitched, ...; a column per 1/16, x starts, = holds, . silent
   |1
E4 |........x=......|
C4 |x===............|
```

A position is bar.beat.tick, 480 ticks a beat of the time signature; C4 is MIDI
60 and a microtone carries its cents (`A4+25c`). A length that is an exact note
value prints as a share of a whole note (`1/4` a quarter, `3/8` a dotted quarter,
`1/12` an eighth triplet), else in beats; the velocity is 0..1. The roll has one
row per pitch the track plays in the bars, highest first, and one column per
sixteenth, with `|` between bars. A run of bars without notes prints as one line
(`bars 5..8: -`).

A drum track is listed per sound instead of per pitch and left out of the
chords. A track is a drum track when the name of the track, its instrument or
its sound holds a drum word (kick, snare, hat, hihat, clap, tom, ride, crash,
cymbal, perc, drum, rim, shaker, tamb, cowbell, conga, bongo, clave, kit and a
few more, as a word of the name: `dk_kick_sound`, `MvKick`), or when its
instrument has no `gate` and plays a single pitch, a one shot hit. Its sound is
the name of the sound of the instrument, with the pitch after a colon when the
track plays several (`tom_sound:A2`).

`audio chords` names chords from the pitched notes that sound together in the
named tracks, by default every enabled track that has notes. In each span every
pitch class weighs the beats it sounds, and the classes of at least a fifth of
the weight of the strongest make the chord, so passing notes drop out. The bass
is the lowest note of those classes. The chord is the one whose notes are the
set (a seventh chord may drop its fifth); when none fits four or more classes
the weakest one is left out once. Qualities print as `C`, `Cm`, `C7`, `Cmaj7`,
`Cm7`, `Cm7b5`, `Cdim`, `Caug`, `Csus4`, `Csus2`, `Cadd9`, `Cdim7` and `C5`, an
inverted bass as a slash chord (`C/E`), a set that fits no chord as its classes
(`[C D F#]`) and silence as `N.C.`.

The span of a chord is a bar, or a beat when the notes are dense enough to
name a chord per beat, at least three pitch classes per beat on average, and a
bar holds two chords for at least two of its beats each (one in a bar of fewer
than four beats). The header says which. Equal chords in a row print as one
line with their length in beats.

```
> audio chords
chords of bars 1..4 from bass, keys, lead; drums left out: kick; a chord per bar (3.4 pitch classes per beat)
1.1 C 4 beats: C E G
2.1 Am 4 beats: A C E
3.1 F 4 beats: F A C
4.1 G7 4 beats: G B D F
key of the bars: C major (r 0.95), then A minor (r 0.79)
key of the song: C major (r 0.95), then A minor (r 0.79)
outside C major: none
```

The key is the major or minor Krumhansl-Kessler profile that correlates best
(r) with the beats of each pitch class, of the bars and of the whole song, with
the next most likely one. Chords and notes are spelled with flats in a flat key.
The notes outside the key of the bars, its major scale or its natural minor
scale with the raised seventh, are listed per pitch class with their count and
first positions.

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

The core also keeps a beat clock: the transport schedules a `BEAT` command with the
beat and tempo at each start, seek, stop, tempo change and loop wrap, only while
something in the core reads it (a modulator or a pattern gate), and the core derives the
beat of any frame from the last one (`box_audio_core_beat`).

An effect command carries 32 floats of parameters, enough for the 16 steps of a
pattern gate and its other values as plain floats, which automation and the modulators
reach like any other parameter. An effect command is 152 bytes and an effect params
command 148, the largest command of the schedule, so a writer buffer holds fewer of
them than with the 16 floats before.

The core owns a 64-bit rendered-frame clock. Scheduled play, stop, voice/bus set,
release, legato, effect params and macro commands carry a frame time. Future commands
wait in a fixed 4096-entry schedule; a full schedule applies the command immediately and
reports `BOX_AUDIO_EVENT_LATE`. Immediate stops also cancel scheduled commands for their
voice handle. Sound memory is freed by its owner after `SOUND_FREE`, which is
retained even when other events overflow. See [core.h](../src/audio/core/core.h)
and [transport.h](../src/audio/transport.h) for the low-level APIs.

The main limits are 64 voices, 64 buses, 64 effects, 64 modulators, 32 macros of eight
maps, four inserts and sends per bus, eight synth layers, two synth LFOs, seven unison
copies per layer, 32 strings shared by pluck and bowed layers, 32 comb delay lines and 256 frames per wavetable, whose tables share the 1023 sound ids. Effects are algorithmic: no convolution or geometry-derived rooms.
There is one transport and one music stream per world. There are no tempo ramps,
time stretching, MIDI I/O, plugins, mixer UI or piano roll.
Scripts can change channel and effect values each frame.

Offline coverage lives in [test/cpu/audio](../test/cpu/audio/); browser and end-to-end
checks are under [test/integration/web](../test/integration/web/) and
[test/integration/e2e/audio.py](../test/integration/e2e/audio.py).
[all_features.flecs](../test/assets/audio/all_features.flecs) combines the subsystems.
Use [bench/audio](../bench/audio/) for mixer, effects, spatial and sequencer costs.
