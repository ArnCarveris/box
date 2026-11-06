window.BOX_AUDIO_PARAMS = {
 "AudioBus": {
  "effects": {
   "doc": "Effect entities of the BOX_AUDIO_EFFECT_SLOTS insert slots, in the order they run, 0 for an empty slot"
  },
  "kind": {
   "doc": "What the channel is, default AudioChannelGroup"
  },
  "mute": {
   "doc": "Silence the bus, default false"
  },
  "output": {
   "doc": "Bus it feeds, 0 (or a deleted bus) for the master"
  },
  "pan": {
   "def": 0.0,
   "doc": "-1 left to 1 right, default 0",
   "max": 1.0,
   "min": -1.0,
   "soft": true,
   "unit": ""
  },
  "sends": {
   "doc": "Up to BOX_AUDIO_SENDS sends to other buses"
  },
  "solo": {
   "doc": "Solo the channel, default false"
  },
  "volume": {
   "def": 1.0,
   "doc": "Linear gain, default 1",
   "max": 4.0,
   "min": 0.0,
   "soft": true,
   "unit": "gain"
  },
  "width": {
   "doc": "Stereo width, 0 mono, 1 as is (default), 2 doubles the side; a mid/side gain after the inserts and before the pan",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  }
 },
 "AudioClip": {
  "length": {
   "def": 4.0,
   "doc": "Beats, default 4: the length of a pass when a placement loops the clip, and of a placement of duration 0",
   "max": 16.0,
   "min": 0.0,
   "soft": true,
   "unit": "beats"
  },
  "offset": {
   "doc": "Seconds into the sound at which the clip starts",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "sound": {
   "doc": "The sound, an entity with AudioPcm or SoundFile"
  }
 },
 "AudioEmitter": {
  "extents": {
   "doc": "Half size of the box, default {0, 0, 0}; their absolute values"
  },
  "interval_max": {
   "def": 4.0,
   "doc": "Seconds between plays, upper bound, default 4; swapped with interval_min when below it",
   "log": true,
   "max": 16.0,
   "min": 0.008,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "interval_min": {
   "def": 1.0,
   "doc": "Seconds between plays, lower bound, default 1; at least 0.01",
   "log": true,
   "max": 4.0,
   "min": 0.002,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "max_active": {
   "doc": "Plays of this emitter that may sound at once, 0 for no limit, default 0"
  },
  "pitch_max": {
   "def": 1.0,
   "doc": "Playback ratio, upper bound, default 1",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "pitch_min": {
   "def": 1.0,
   "doc": "Playback ratio, lower bound, default 1",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "playing": {
   "doc": "Whether it plays, default true"
  },
  "radius": {
   "def": 0.0,
   "doc": "Radius of disc, sphere and hemisphere, default 0; its absolute value",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "seed": {
   "doc": "Seed of the randomness, default 0"
  },
  "shape": {
   "doc": "Volume plays are placed in, default AudioEmitterPoint"
  },
  "sound": {
   "doc": "AudioPcm, SoundFile or SynthSound to play"
  },
  "volume_max": {
   "def": 1.0,
   "doc": "Linear gain, upper bound, default 1",
   "max": 4.0,
   "min": 0.0,
   "soft": true,
   "unit": "gain"
  },
  "volume_min": {
   "def": 1.0,
   "doc": "Linear gain, lower bound, default 1; at least 0",
   "max": 4.0,
   "min": 0.0,
   "soft": true,
   "unit": "gain"
  }
 },
 "AudioEqBand": {
  "frequency": {
   "doc": "Hz, the center of a peak or the corner of a shelf or a cut, 10 to 0.49 of the sample rate",
   "log": true,
   "max": 20000.0,
   "min": 10.0,
   "unit": "Hz"
  },
  "gain": {
   "def": 0.0,
   "doc": "dB, -24 to 24, of a peak or a shelf, default 0; a peak or a shelf at 0 dB is flat and costs nothing",
   "max": 24.0,
   "min": -24.0,
   "unit": "dB"
  },
  "kind": {
   "doc": "What the band does, default AudioEqOff"
  },
  "q": {
   "def": 0.7071,
   "doc": "0.1 to 18, default 0.7071: the width of a peak (higher is narrower), the resonance of a shelf or a cut at its corner",
   "max": 18.0,
   "min": 0.1,
   "unit": ""
  }
 },
 "AudioFilter": {
  "cutoff": {
   "def": 1000.0,
   "doc": "Hz, default 1000, 10 to 0.49 of the sample rate",
   "log": true,
   "max": 20000.0,
   "min": 10.0,
   "unit": "Hz"
  },
  "drive": {
   "def": 0.0,
   "doc": "Saturation, 0 to 1, default 0 (linear), as the drive of SynthFilter; moves over 20 ms",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "kind": {
   "doc": "The kind, default SynthLowPass; SynthNotch for a notch"
  },
  "mix": {
   "def": 1.0,
   "doc": "Wet level, 0 to 1, default 1; the dry signal passes at 1 - mix",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "resonance": {
   "doc": "0 (a Butterworth response, the default) to 1 (rings the longest)",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "slope": {
   "doc": "Steepness, default SynthSlope12; at SynthSlope24 the resonance applies to the second stage only"
  }
 },
 "AudioMacro": {
  "value": {
   "def": 0.0,
   "doc": "0..1, default 0, clamped",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  }
 },
 "AudioMacroMap": {
  "component": {
   "doc": "The component: AudioBus or an effect component"
  },
  "curve": {
   "doc": "-1 logarithmic .. 0 linear (default) .. 1 exponential",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "max": {
   "def": 1.0,
   "doc": "Value at a macro of 1, default 1; below min inverts",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "min": {
   "def": 0.0,
   "doc": "Value at a macro of 0, in the units of the member, default 0",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "scale": {
   "doc": "AudioModLinear (default) or AudioModOctaves, for a frequency, with min and max above 0"
  },
  "target": {
   "doc": "The entity with the AudioBus or the effect component"
  }
 },
 "AudioMeter": {
  "peak_left": {
   "doc": "Linear, the largest absolute left sample since the last frame",
   "max": 4.0,
   "min": 0.0,
   "soft": true,
   "unit": "gain"
  },
  "peak_right": {
   "doc": "Linear, the largest absolute right sample since the last frame",
   "max": 4.0,
   "min": 0.0,
   "soft": true,
   "unit": "gain"
  },
  "reduction": {
   "doc": "dB, on an effect entity: the largest gain reduction since the last frame, for dynamics effects and a PatternGate, and how hard the clipper of a Saturator squashes; 0 for others",
   "max": 12.0,
   "min": -60.0,
   "soft": true,
   "unit": "dB"
  },
  "rms_left": {
   "doc": "Linear, the RMS of the left channel over the last BOX_AUDIO_METER_WINDOW seconds of output",
   "max": 4.0,
   "min": 0.0,
   "soft": true,
   "unit": "gain"
  },
  "rms_right": {
   "doc": "Linear, the RMS of the right channel over the window",
   "max": 4.0,
   "min": 0.0,
   "soft": true,
   "unit": "gain"
  }
 },
 "AudioModulator": {
  "attack": {
   "def": 0.01,
   "doc": "Seconds of the rise of a follower, default 0.01",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "component": {
   "doc": "The component: AudioBus or an effect component"
  },
  "depth": {
   "doc": "Swing either way at a source of 1, in the units of the member, or in octaves; negative turns it upside down. Default 0",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "key": {
   "doc": "Bus whose level a follower follows, measured before its inserts and volume; 0 for the bus that holds the target effect, or the target bus"
  },
  "phase": {
   "doc": "Phase in turns, 0..1: of a cycle of an LFO, of the whole sequence of steps",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "rate": {
   "def": 1.0,
   "doc": "Hz of an LFO, or steps per second, when sync is 0, 0..100, default 1",
   "log": true,
   "max": 100.0,
   "min": 0.05,
   "unit": "Hz",
   "zero": true
  },
  "release": {
   "def": 0.1,
   "doc": "Seconds of the fall of a follower, default 0.1",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "scale": {
   "doc": "Linear (default) or in octaves, for a frequency"
  },
  "smooth": {
   "doc": "Share of a step the value glides from the step before over, 0 for hard steps (default) .. 1",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "source": {
   "doc": "What moves it, default AudioModLfo"
  },
  "step_count": {
   "doc": "Steps that play, 1..16, default 16"
  },
  "steps": {
   "doc": "Values of the steps, -1..1",
   "max": 1.0,
   "min": -1.0,
   "unit": ""
  },
  "sync": {
   "def": 0.0,
   "doc": "Beats of a cycle of an LFO, or of a step, 0..256, default 0 for rate; when positive it wins over rate, like SynthLfo.sync and Delay.sync: 0.5 is an eighth, 0.25 a sixteenth",
   "max": 256.0,
   "min": 0.0,
   "unit": "beats"
  },
  "target": {
   "doc": "The entity with the AudioBus or the effect component"
  },
  "unipolar": {
   "doc": "An LFO moves 0..1 instead of -1..1, for a tremolo that only dips: depth -1 on a volume of 1"
  },
  "wave": {
   "doc": "Wave of an LFO, default AudioModSine"
  }
 },
 "AudioOutput": {
  "bus": {
   "doc": "Bus entity, 0 for the master"
  }
 },
 "AudioPcm": {
  "channels": {
   "doc": "1 or 2, 0 for 1"
  },
  "sample_rate": {
   "doc": "Frames per second, 0 for the rate of the output",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "samples": {
   "doc": "Frames, interleaved when stereo"
  }
 },
 "AudioSend": {
  "bus": {
   "doc": "Bus that receives the send, 0 (or a deleted bus) for none"
  },
  "level": {
   "doc": "Linear gain of the send, 0 to 4",
   "max": 4.0,
   "min": 0.0,
   "unit": "gain"
  },
  "pre_fader": {
   "doc": "Tap before the volume and pan of the channel instead of after them; the mute and the solo still silence it"
  }
 },
 "AudioSettings": {
  "master_volume": {
   "def": 1.0,
   "doc": "Linear gain of the master bus, default 1",
   "max": 4.0,
   "min": 0.0,
   "soft": true,
   "unit": "gain"
  },
  "mute": {
   "doc": "Silence everything, default false"
  }
 },
 "AudioSong": {},
 "AudioSongStatus": {
  "foreign": {
   "doc": "Entities the song changed that it did not create, each logged with a warning; their changes stay after the song unloads"
  },
  "state": {
   "doc": "Progress of the load"
  }
 },
 "AudioSource": {
  "loop": {
   "doc": "Play the sound over and over, default false"
  },
  "pitch": {
   "def": 1.0,
   "doc": "Ratio of the playback speed, default 1",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "playing": {
   "doc": "Whether it plays, default true"
  },
  "sound": {
   "doc": "The sound to play, an entity with AudioPcm, SoundFile or SynthSound; a looping synth starts over at the end of its envelopes"
  },
  "volume": {
   "def": 1.0,
   "doc": "Linear gain, default 1",
   "max": 4.0,
   "min": 0.0,
   "soft": true,
   "unit": "gain"
  }
 },
 "AudioSpatial": {
  "cutoff": {
   "def": 0.0,
   "doc": "Low-pass cutoff in Hz at max_distance, 0 for no filter, default 0",
   "log": true,
   "max": 20000.0,
   "min": 10.0,
   "soft": true,
   "unit": "Hz",
   "zero": true
  },
  "max_distance": {
   "def": 50.0,
   "doc": "Silent from this distance, default 50",
   "max": 200.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "min_distance": {
   "def": 1.0,
   "doc": "Full volume within this distance, default 1",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "rolloff": {
   "def": 1.0,
   "doc": "How fast the volume falls beyond min_distance, default 1",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  }
 },
 "AudioStatus": {
  "buses": {
   "doc": "Buses in the processing order, the master included"
  },
  "clock": {
   "doc": "Frame of the core that renders now, as far as the main thread can tell; stops while the output does not run, 0 for a new output"
  },
  "load": {
   "doc": "Share of real time the render takes, 0 where it is not measured (the web)",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "sample_rate": {
   "doc": "Frames per second of the output, 0 while not known"
  },
  "state": {
   "doc": "What the output does"
  },
  "voices": {
   "doc": "Voices that play, without the ones that fade out"
  },
  "voices_refused": {
   "doc": "Plays refused so far at the voice cap because every voice that played had a higher priority"
  },
  "voices_stolen": {
   "doc": "Voices the voice cap of 64 stopped so far"
  }
 },
 "AudioStream": {
  "loop": {
   "doc": "Start over at the end without a gap, default true"
  },
  "playing": {
   "doc": "Whether it plays, default true; false fades it out and true resumes it where it stopped"
  },
  "volume": {
   "def": 1.0,
   "doc": "Linear gain, default 1",
   "max": 4.0,
   "min": 0.0,
   "soft": true,
   "unit": "gain"
  }
 },
 "AudioStreamStatus": {
  "buffered": {
   "doc": "Seconds decoded ahead of what plays",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "channels": {
   "doc": "1 or 2 after the mix down, 0 until it opened"
  },
  "decode_max_ms": {
   "doc": "Largest decode_ms so far",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "decode_ms": {
   "doc": "Milliseconds the last decode took; on the web the main thread spends it in its frame",
   "log": true,
   "max": 50.0,
   "min": 0.025,
   "soft": true,
   "unit": "ms",
   "zero": true
  },
  "frames": {
   "doc": "Frames of the file, 0 until it opened"
  },
  "position": {
   "doc": "Seconds into the file of what plays",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "sample_rate": {
   "doc": "Frames per second of the file, 0 until it opened",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "state": {
   "doc": "What it does"
  },
  "underruns": {
   "doc": "Times the output ran out of decoded frames and faded to silence"
  }
 },
 "AutomationClip": {
  "component": {
   "doc": "The component: AudioBus or an effect component"
  },
  "curve": {
   "doc": "The values: time in beats from the start of the clip, value in the units of the member"
  },
  "length": {
   "def": 4.0,
   "doc": "Beats, default 4: the length of a pass when a placement loops the clip, and of a placement of duration 0",
   "max": 16.0,
   "min": 0.0,
   "soft": true,
   "unit": "beats"
  },
  "target": {
   "doc": "The entity with the AudioBus or the effect component"
  }
 },
 "BeatRepeat": {
  "active": {
   "def": 0.0,
   "doc": "0 to 1, default 0: 0.5 and above repeats",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "decay": {
   "def": 1.0,
   "doc": "Gain of a repeat against the one before it, 0 to 1, default 1: 0.5 drops each repeat by 6 dB",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "gate": {
   "def": 1.0,
   "doc": "Share of each repeat that sounds, 0.1 to 1, default 1",
   "max": 1.0,
   "min": 0.1,
   "unit": ""
  },
  "grid": {
   "def": -1.0,
   "doc": "Beats of the grid the capture waits for, 0 to 4; 0 captures at once; default -1, any negative grid, is the slice",
   "max": 4.0,
   "min": -1.0,
   "unit": "beats"
  },
  "mix": {
   "def": 1.0,
   "doc": "Level of the repeats while it repeats, 0 to 1, default 1: the input is replaced; the dry signal passes at 1 - mix",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "pitch": {
   "def": 0.0,
   "doc": "Semitones of a repeat against the one before it, -12 to 12, default 0; the repeats add up to at most 48 either way",
   "max": 12.0,
   "min": -12.0,
   "unit": "st"
  },
  "reverse": {
   "def": 0.0,
   "doc": "0 to 1, default 0: 0.5 and above plays the repeats after the first reversed",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "slice": {
   "def": 0.25,
   "doc": "Beats of the slice, 1/32 to 4, default 0.25",
   "max": 4.0,
   "min": 0.03125,
   "unit": "beats"
  }
 },
 "Chorus": {
  "delay": {
   "def": 12.0,
   "doc": "Milliseconds of the taps at the middle, default 12, 0 to 50; a tap reads at most 50 ms and at least a frame back, so a flanger goes down to about 0.1 ms",
   "log": true,
   "max": 50.0,
   "min": 0.025,
   "unit": "ms",
   "zero": true
  },
  "depth": {
   "def": 3.0,
   "doc": "Milliseconds the taps move either way, default 3, 0 to 25",
   "log": true,
   "max": 25.0,
   "min": 0.0125,
   "unit": "ms",
   "zero": true
  },
  "feedback": {
   "def": 0.0,
   "doc": "Share of the first tap fed back into the line, -0.95 to 0.95, default 0; 0 renders exactly as without it",
   "max": 0.95,
   "min": -0.95,
   "unit": ""
  },
  "mix": {
   "def": 0.5,
   "doc": "Wet level, 0 to 1, default 0.5; the dry signal passes at 1 - mix",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "rate": {
   "def": 0.8,
   "doc": "Hz of the LFOs, default 0.8, 0 to 20",
   "log": true,
   "max": 20.0,
   "min": 0.01,
   "unit": "Hz",
   "zero": true
  },
  "spread": {
   "def": 0.5,
   "doc": "0 to 1, default 0.5: 0 keeps the voices in the center, so a mono input stays mono, 1 pans the first voice left and the last right",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "voices": {
   "doc": "Taps, 1 to 4, default 2"
  }
 },
 "ClipPlacement": {
  "clip": {
   "doc": "The entity with NoteClip, AudioClip or AutomationClip"
  },
  "duration": {
   "doc": "Beats, 0 for the length of the clip minus the offset",
   "max": 16.0,
   "min": 0.0,
   "soft": true,
   "unit": "beats"
  },
  "gain": {
   "def": 1.0,
   "doc": "Linear gain, default 1, times the gain of the velocity",
   "max": 4.0,
   "min": 0.0,
   "soft": true,
   "unit": "gain"
  },
  "loop": {
   "doc": "Repeat the clip every length beats until the duration ends"
  },
  "offset": {
   "doc": "Beats into the clip at which the placement starts; with a loop the passes go on from there and wrap at length",
   "max": 16.0,
   "min": 0.0,
   "soft": true,
   "unit": "beats"
  },
  "start": {
   "doc": "Beats on the timeline where the placement starts",
   "max": 16.0,
   "min": 0.0,
   "soft": true,
   "unit": "beats"
  },
  "transpose": {
   "doc": "Semitones added to every pitch; of an audio clip the pitch ratio 2^(transpose / 12)",
   "max": 24.0,
   "min": -24.0,
   "soft": true,
   "unit": "st"
  }
 },
 "Compressor": {
  "attack": {
   "def": 0.01,
   "doc": "Seconds in which the detected level rises 63% of a step, default 0.01, 0.0001 to 1",
   "log": true,
   "max": 1.0,
   "min": 0.0001,
   "unit": "s"
  },
  "knee": {
   "def": 6.0,
   "doc": "Width of the soft knee in dB, default 6, 0 to 24",
   "max": 24.0,
   "min": 0.0,
   "unit": "dB"
  },
  "makeup": {
   "def": 0.0,
   "doc": "Gain after the compression in dB, default 0, 0 to 24",
   "max": 24.0,
   "min": 0.0,
   "unit": "dB"
  },
  "mix": {
   "def": 1.0,
   "doc": "Wet level, 0 to 1, default 1; lower for parallel compression",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "ratio": {
   "def": 4.0,
   "doc": "1 to 20, default 4",
   "max": 20.0,
   "min": 1.0,
   "unit": ""
  },
  "release": {
   "def": 0.15,
   "doc": "Seconds in which it falls 63% of a step, default 0.15, 0.001 to 5",
   "log": true,
   "max": 5.0,
   "min": 0.001,
   "unit": "s"
  },
  "sidechain": {
   "doc": "Channel (an AudioBus) whose output is the key, 0 for its own input"
  },
  "threshold": {
   "def": -18.0,
   "doc": "dB, default -18, -80 to 0",
   "max": 0.0,
   "min": -80.0,
   "unit": "dB"
  }
 },
 "Delay": {
  "damping": {
   "def": 6000.0,
   "doc": "Cutoff in Hz of a one pole lowpass in the feedback path, default 6000, at least 20; at 0.49 of the sample rate or above the repeats stay bright",
   "log": true,
   "max": 20000.0,
   "min": 20.0,
   "soft": true,
   "unit": "Hz"
  },
  "feedback": {
   "def": 0.35,
   "doc": "Share of a repeat in the next, 0 to 0.95, default 0.35",
   "max": 0.95,
   "min": 0.0,
   "unit": ""
  },
  "mix": {
   "def": 0.3,
   "doc": "Wet level, 0 to 1, default 0.3; the dry signal passes at 1 - mix",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "ping_pong": {
   "doc": "Bounce the repeats between left and right, default false: the input, summed to mono, repeats on the left first"
  },
  "sync": {
   "def": 0.0,
   "doc": "Beats between the repeats, 0 (the default) to use time; when positive it wins over time, which becomes sync 60 / tempo at the tempo of the Transport (120 without one): 0.5 is an eighth, 0.75 a dotted eighth",
   "max": 16.0,
   "min": 0.0,
   "soft": true,
   "unit": "beats"
  },
  "time": {
   "def": 0.25,
   "doc": "Seconds between the repeats, default 0.25, 0 to BOX_AUDIO_DELAY_MAX (4)",
   "log": true,
   "max": 4.0,
   "min": 0.002,
   "unit": "s",
   "zero": true
  }
 },
 "Equalizer": {
  "bands": {
   "doc": "The BOX_AUDIO_EQ_BANDS bands, in the order they run"
  }
 },
 "FrequencyShifter": {
  "feedback": {
   "def": 0.0,
   "doc": "Share of the shifted signal fed back into the input, -0.95 to 0.95, default 0",
   "max": 0.95,
   "min": -0.95,
   "unit": ""
  },
  "mix": {
   "def": 1.0,
   "doc": "Wet level, 0 to 1, default 1; the dry signal passes at 1 - mix",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "mode": {
   "doc": "FrequencyShift (default) or FrequencyRing"
  },
  "shift": {
   "def": 0.0,
   "doc": "Hz, -5000 to 5000, default 0; in the ring mode the frequency of the sine is its magnitude",
   "max": 5000.0,
   "min": -5000.0,
   "unit": "Hz"
  },
  "stereo": {
   "def": 0.0,
   "doc": "0 to 1, default 0: the right channel shifts by shift times 1 - 2 stereo, so at 1 the opposite way of the left",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  }
 },
 "Gate": {
  "attack": {
   "def": 0.001,
   "doc": "Seconds in which it opens 63% of the way, default 0.001",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "hold": {
   "def": 0.05,
   "doc": "Seconds it stays open after the level fell, default 0.05, 0 to 5",
   "log": true,
   "max": 5.0,
   "min": 0.0025,
   "unit": "s",
   "zero": true
  },
  "range": {
   "def": -80.0,
   "doc": "Gain when closed in dB, default -80, -100 to 0",
   "max": 0.0,
   "min": -100.0,
   "unit": "dB"
  },
  "release": {
   "def": 0.1,
   "doc": "Seconds in which it closes 63% of the way, default 0.1",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "threshold": {
   "def": -50.0,
   "doc": "dB, default -50, -100 to 0",
   "max": 0.0,
   "min": -100.0,
   "unit": "dB"
  }
 },
 "Instrument": {
  "gate": {
   "doc": "The duration of a note ends it, default true; false plays the sound as authored, like a drum hit"
  },
  "release": {
   "def": 0.05,
   "doc": "Seconds a sampler note fades out over after its end, default 0.05; a synth uses the release of its envelopes",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "root": {
   "def": 69.0,
   "doc": "MIDI note at which the sound plays as authored, default 69 (A4, 440 Hz)",
   "max": 127.0,
   "min": 0.0,
   "soft": true,
   "unit": "note"
  },
  "sound": {
   "doc": "The sound that plays: SynthSound for a synth, AudioPcm or SoundFile for a sampler"
  },
  "spread": {
   "def": 0.0,
   "doc": "0..1, default 0: how far the notes spread over the stereo field, an offset added to the pan of each note",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "spread_mode": {
   "doc": "How the notes spread, default InstrumentSpreadPitch"
  },
  "velocity_curve": {
   "def": 2.0,
   "doc": "Exponent of the velocity in the gain, default 2",
   "max": 8.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  }
 },
 "Note": {
  "bend": {
   "doc": "Pitch bend in semitones, -48..48, over beats of the note: the pitch of the voice times 2^(bend / 12), its glide, oscillators, pluck strings and key tracking included; synth and sample voices. Without keys 0"
  },
  "duration": {
   "doc": "Beats",
   "max": 16.0,
   "min": 0.0,
   "soft": true,
   "unit": "beats"
  },
  "mod": {
   "doc": "0..1 over beats of the note: scales the vibrato of a synth voice, the pitch depth of its LFOs. Without keys 1, so a sound keeps its vibrato"
  },
  "pan": {
   "doc": "Pan of the voice, -1 left to 1 right",
   "max": 1.0,
   "min": -1.0,
   "soft": true,
   "unit": ""
  },
  "pitch": {
   "doc": "MIDI note number, a float so microtones work",
   "max": 127.0,
   "min": 0.0,
   "soft": true,
   "unit": "note"
  },
  "pressure": {
   "doc": "0..1 over beats of the note: moves the cutoff of a synth voice up by pressure times 2 octaves (BOX_AUDIO_SYNTH_PRESSURE_OCTAVES). Without keys 0"
  },
  "start": {
   "doc": "Beats from the start of the clip",
   "max": 16.0,
   "min": 0.0,
   "soft": true,
   "unit": "beats"
  },
  "velocity": {
   "def": 1.0,
   "doc": "0..1, default 1",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  }
 },
 "NoteClip": {
  "length": {
   "def": 4.0,
   "doc": "Beats, default 4: the length of a pass when a placement loops the clip, and of a placement of duration 0",
   "max": 16.0,
   "min": 0.0,
   "soft": true,
   "unit": "beats"
  },
  "notes": {
   "doc": "The notes, in any order; the clip owns the keys of their curves, copying the clip copies them"
  }
 },
 "PatternGate": {
  "attack": {
   "def": 0.002,
   "doc": "Seconds of an edge from closed to open, default 0.002, 0.0005 to 2",
   "log": true,
   "max": 2.0,
   "min": 0.0005,
   "unit": "s"
  },
  "floor": {
   "def": -80.0,
   "doc": "Gain when closed in dB, -80 (default) to 0; a higher floor pumps instead of chopping",
   "max": 12.0,
   "min": -80.0,
   "soft": true,
   "unit": "dB"
  },
  "gate": {
   "def": 0.5,
   "doc": "Share of each step that is open, 0.05 to 1, default 0.5; at 1 neighbouring open steps run together",
   "max": 1.0,
   "min": 0.05,
   "unit": ""
  },
  "length": {
   "doc": "Steps in the pattern, 1 to 16, default 16"
  },
  "mix": {
   "def": 1.0,
   "doc": "Wet level, 0 to 1, default 1; the dry signal passes at 1 - mix",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "rate": {
   "def": 0.25,
   "doc": "Beats per step, default 0.25 (sixteenths); 0.375 is a dotted sixteenth, 1 / 6 a sixteenth triplet, 1/64 to 16",
   "max": 16.0,
   "min": 0.015625,
   "unit": "beats"
  },
  "release": {
   "def": 0.03,
   "doc": "Seconds of an edge from open to closed, default 0.03, 0.0005 to 2; a longer one gives a softer pulse",
   "log": true,
   "max": 2.0,
   "min": 0.0005,
   "unit": "s"
  },
  "steps": {
   "def": 1.0,
   "doc": "Level of each step, 0 (closed) to 1, default all 1; an accent is a level under 1",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "swing": {
   "def": 0.0,
   "doc": "0 to 0.75, default 0: the second step of every pair starts later by swing steps, as Track.swing",
   "max": 0.75,
   "min": 0.0,
   "unit": ""
  }
 },
 "Phaser": {
  "center": {
   "def": 800.0,
   "doc": "Hz at the middle of the sweep, default 800, 20 to 20000",
   "log": true,
   "max": 20000.0,
   "min": 20.0,
   "unit": "Hz"
  },
  "depth": {
   "def": 0.7,
   "doc": "0 to 1, default 0.7: the sweep reaches 2 depth octaves either way of the center",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "feedback": {
   "def": 0.0,
   "doc": "Share of the phased signal fed back into the chain, -0.9 to 0.9, default 0; more sharpens the peaks between the notches",
   "max": 0.9,
   "min": -0.9,
   "unit": ""
  },
  "mix": {
   "def": 0.5,
   "doc": "Wet level, 0 to 1, default 0.5, which gives the deepest notches; the dry signal passes at 1 - mix",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "rate": {
   "def": 0.5,
   "doc": "Hz of the LFO, default 0.5, 0 to 20",
   "log": true,
   "max": 20.0,
   "min": 0.01,
   "unit": "Hz",
   "zero": true
  },
  "stages": {
   "doc": "Allpass stages, 2 to 12 and even, default 4: stages / 2 notches"
  },
  "stereo": {
   "def": 0.5,
   "doc": "Phase offset of the LFO of the right channel in half turns, 0 to 1, default 0.5 (a quarter of a period)",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  }
 },
 "PitchShifter": {
  "cents": {
   "def": 0.0,
   "doc": "Detune on top of the semitones, -100 to 100, default 0",
   "max": 100.0,
   "min": -100.0,
   "unit": ""
  },
  "feedback": {
   "def": 0.0,
   "doc": "Share of the shifted signal fed back into the line, 0 to 0.9, default 0",
   "max": 0.9,
   "min": 0.0,
   "unit": ""
  },
  "mix": {
   "def": 0.5,
   "doc": "Wet level, 0 to 1, default 0.5; the dry signal passes at 1 - mix",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "semitones": {
   "def": 0.0,
   "doc": "Shift, -24 to 24, default 0",
   "max": 24.0,
   "min": -24.0,
   "unit": ""
  },
  "stereo": {
   "def": 0.0,
   "doc": "0 to 1, default 0: the cents of the right channel go from cents at 0 to -cents at 1",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "window": {
   "def": 40.0,
   "doc": "Milliseconds of the window, 10 to 100, default 40",
   "max": 100.0,
   "min": 10.0,
   "unit": "ms"
  }
 },
 "Reverb": {
  "damping": {
   "def": 0.5,
   "doc": "High frequency loss, 0 to 1, default 0.5",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "decay": {
   "def": 1.5,
   "doc": "Seconds until -60 dB, default 1.5",
   "log": true,
   "max": 6.0,
   "min": 0.003,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "mix": {
   "def": 1.0,
   "doc": "Wet level, 0 to 1, default 1 on a send bus; the dry signal passes at 1 - mix",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "predelay": {
   "def": 0.02,
   "doc": "Seconds before the first reflection, default 0.02",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "shimmer": {
   "def": 0.0,
   "doc": "Share of the feedback that is pitch shifted, 0 to 1, default 0 (off)",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "shimmer_pitch": {
   "def": 12.0,
   "doc": "Semitones each pass of the shimmer shifts, -24 to 24, default 12 (an octave up)",
   "max": 24.0,
   "min": -24.0,
   "unit": "st"
  },
  "size": {
   "def": 0.5,
   "doc": "Room scale, 0 to 1, default 0.5",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  }
 },
 "Rotary": {
  "acceleration": {
   "def": 1.0,
   "doc": "Seconds of the horn to a new speed, default 1, 0.01 to 20; the drum takes three times as long",
   "log": true,
   "max": 20.0,
   "min": 0.01,
   "unit": "s"
  },
  "balance": {
   "def": 0.0,
   "doc": "-1 to 1, default 0: -1 is the drum alone, 1 the horn alone",
   "max": 1.0,
   "min": -1.0,
   "unit": ""
  },
  "drive": {
   "def": 0.0,
   "doc": "0 to 1, default 0: the share of the tube preamp, its gain up to +24 dB",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "drum_fast": {
   "def": 5.9,
   "doc": "Hz of the drum at speed 1, default 5.9, 0 to 20",
   "log": true,
   "max": 20.0,
   "min": 0.01,
   "unit": "Hz",
   "zero": true
  },
  "drum_slow": {
   "def": 0.7,
   "doc": "Hz of the drum at speed 0, default 0.7, 0 to 20",
   "log": true,
   "max": 20.0,
   "min": 0.01,
   "unit": "Hz",
   "zero": true
  },
  "horn_fast": {
   "def": 6.7,
   "doc": "Hz of the horn at speed 1, default 6.7, 0 to 20",
   "log": true,
   "max": 20.0,
   "min": 0.01,
   "unit": "Hz",
   "zero": true
  },
  "horn_slow": {
   "def": 0.8,
   "doc": "Hz of the horn at speed 0, default 0.8, 0 to 20",
   "log": true,
   "max": 20.0,
   "min": 0.01,
   "unit": "Hz",
   "zero": true
  },
  "mix": {
   "def": 1.0,
   "doc": "Wet level, 0 to 1, default 1; the dry signal passes at 1 - mix",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "speed": {
   "def": 0.0,
   "doc": "0 to 1, default 0: 0 is slow (chorale), 1 fast (tremolo)",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "spread": {
   "def": 1.0,
   "doc": "0 to 1, default 1: the stereo width of the mics; 0 is mono, 1 puts them half a turn apart",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  }
 },
 "Saturator": {
  "asymmetry": {
   "def": 0.0,
   "doc": "0 to 1, default 0: a bias before the clipper, shared by both channels, that adds even harmonics; in SaturatorRectify the blend from half wave (0) to full wave (1)",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "bits": {
   "def": 16.0,
   "doc": "Bits of the bit crusher, 1 to 16, default 16, which is off; bits b round the output to 2^b levels",
   "max": 16.0,
   "min": 1.0,
   "unit": ""
  },
  "drive": {
   "def": 0.3,
   "doc": "0 to 1, default 0.3: a gain of 0 to +30 dB into the clipper",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "high_cut": {
   "def": 0.0,
   "doc": "Hz of a 12 dB lowpass after the clipper, 0 to 20000, default 0, which is off; held under 0.45 times the sample rate",
   "log": true,
   "max": 20000.0,
   "min": 10.0,
   "unit": "Hz",
   "zero": true
  },
  "low_cut": {
   "def": 0.0,
   "doc": "Hz of a 12 dB highpass before the clipper, 0 to 20000, default 0, which is off",
   "log": true,
   "max": 20000.0,
   "min": 10.0,
   "unit": "Hz",
   "zero": true
  },
  "mix": {
   "def": 1.0,
   "doc": "Wet level, 0 to 1, default 1; the dry signal passes at 1 - mix",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "mode": {
   "doc": "Curve of the clipper, default SaturatorTanh"
  },
  "rate": {
   "def": 0.0,
   "doc": "Hz of a sample and hold after the clipper, 0 or 100 to the sample rate, default 0, which is off",
   "log": true,
   "max": 20000.0,
   "min": 10.0,
   "soft": true,
   "unit": "Hz",
   "zero": true
  },
  "tone": {
   "def": 0.0,
   "doc": "-1 to 1, default 0: a tilt of up to 9 dB either way at the ends of the spectrum around 1 kHz before the clipper, the inverse after it; above 0 the highs distort most, below 0 the lows",
   "max": 1.0,
   "min": -1.0,
   "unit": ""
  }
 },
 "Song": {
  "beat_unit": {
   "doc": "Bottom of the time signature, default 4"
  },
  "beats": {
   "doc": "Length of the song in beats, 0 for unknown",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "beats_per_bar": {
   "doc": "Top of the time signature, default 4"
  },
  "lanes": {
   "doc": "The channels a host can meter, in the order to show them"
  },
  "loop": {
   "doc": "Loop the Transport over 0..beats, default true"
  },
  "tempo": {
   "def": 120.0,
   "doc": "Quarter notes per minute, default 120",
   "max": 480.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  }
 },
 "SongLane": {
  "bus": {
   "doc": "The bus of the channel, an entity with AudioBus of the song"
  }
 },
 "SoundFile": {},
 "SoundStatus": {
  "channels": {
   "doc": "1 or 2 after the mix down, 0 unless ready"
  },
  "frames": {
   "doc": "Frames of the decoded sound, 0 unless ready"
  },
  "sample_rate": {
   "doc": "Frames per second of the file, 0 unless ready",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "state": {
   "doc": "Progress of the load"
  }
 },
 "SynthDrive": {
  "asymmetry": {
   "def": 0.0,
   "doc": "0..1, default 0: an offset before the clipper that adds even harmonics; its DC is blocked",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "drive": {
   "def": 0.3,
   "doc": "0..1, default 0.3: a gain of -6 to +24 dB before the clipper",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  }
 },
 "SynthEnvelope": {
  "attack": {
   "doc": "Seconds from 0 to 1, at least 0.5 ms so it does not click",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "attack_curve": {
   "doc": "Curve of the attack, 0 linear (default), -16..16",
   "max": 16.0,
   "min": -16.0,
   "unit": ""
  },
  "curve": {
   "doc": "Curve of the decay and the release, 0 linear (default), -16..16",
   "max": 16.0,
   "min": -16.0,
   "unit": ""
  },
  "decay": {
   "doc": "Seconds from 1 to the sustain level",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "hold": {
   "doc": "Seconds at the sustain level before the release",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "key_scale": {
   "def": 0.0,
   "doc": "Key scaling 0..1, default 0: the decay and the release are times 2^(-key_scale key / 12) at the key of the note (see SynthFilter), fixed at its start, so at 1 they halve per octave above the root and double per octave below; clamped to the times of an envelope",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "release": {
   "doc": "Seconds from the sustain level to 0",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "sustain": {
   "def": 0.0,
   "doc": "Level after the decay, 0..1, default 0",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  }
 },
 "SynthFilter": {
  "cutoff": {
   "def": 1000.0,
   "doc": "Hz, default 1000, clamped under Nyquist",
   "log": true,
   "max": 20000.0,
   "min": 20.0,
   "soft": true,
   "unit": "Hz"
  },
  "drive": {
   "def": 0.0,
   "doc": "Saturation, 0 to 1, default 0 (linear): soft clips the input and the resonance of each stage, so a high resonance compresses and growls instead of whistling; 1 drives it hard",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "feedback": {
   "def": 0.5,
   "doc": "SynthComb: gain of the delayed output fed back to the input, -0.98..0.98, default 0.5: positive peaks at the harmonics of the comb, negative at the odd harmonics of half of it, hollow like a closed pipe; near 1 either way rings long",
   "max": 0.98,
   "min": -0.98,
   "unit": ""
  },
  "keytrack": {
   "def": 0.0,
   "doc": "Key tracking 0..1, default 0: the cutoff is times the pitch ratio of the voice to the keytrack, so 1 moves it 1:1 with the pitch; it follows the pitch of the voice, glides included",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "kind": {
   "doc": "The kind, default SynthLowPass"
  },
  "model": {
   "doc": "The model, default SynthFilterSvf. SynthFilterLadder is a transistor ladder: SynthSlope24 is its 4-pole lowpass and SynthSlope12 the 2-pole tap; the highpass, bandpass and notch mix the poles as a multimode ladder. Its resonance oscillates at 1 as a near sine at the cutoff, its lowpass keeps its passband within 2 dB as the resonance rises, and drive is the gain into it, from -6 dB to +12 dB, the output scaled back"
  },
  "resonance": {
   "doc": "0 (a Butterworth response) to 1 (rings the longest)",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "slope": {
   "doc": "Steepness, default SynthSlope12; at SynthSlope24 the resonance applies to the second stage only"
  },
  "tune": {
   "def": 1.0,
   "doc": "SynthComb: ratio of the frequency of the comb to the note, 0.125..8, default 1; 2 an octave up, 1.5 a fifth",
   "max": 8.0,
   "min": 0.125,
   "unit": ""
  },
  "velocity": {
   "def": 0.0,
   "doc": "Octaves of the cutoff at velocity 1 over velocity 0, -10..10, default 0: the cutoff applies as authored at velocity 1 and is cutoff 2^(velocity (v - 1)) at the velocity v of a note",
   "max": 10.0,
   "min": -10.0,
   "unit": "oct"
  },
  "vowel": {
   "def": 0.0,
   "doc": "SynthFormant: the vowel, 0 a, 1 e, 2 i, 3 o, 4 u, default 0; a fraction interpolates the frequencies, levels and widths of the formants of the two nearest vowels. SynthLfo.vowel and SynthFilterEnvelope.vowel move it, clamped to 0..4",
   "max": 4.0,
   "min": 0.0,
   "unit": ""
  }
 },
 "SynthFilterEnvelope": {
  "amount": {
   "def": 0.0,
   "doc": "Octaves the cutoff moves at level 1, -10..10, negative closes the filter, default 0",
   "max": 10.0,
   "min": -10.0,
   "unit": "oct"
  },
  "attack": {
   "doc": "Seconds from 0 to 1, at least 0.5 ms",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "attack_curve": {
   "doc": "Curve of the attack, 0 linear (default), -16..16",
   "max": 16.0,
   "min": -16.0,
   "unit": ""
  },
  "curve": {
   "doc": "Curve of the decay and the release, 0 linear (default), -16..16",
   "max": 16.0,
   "min": -16.0,
   "unit": ""
  },
  "decay": {
   "doc": "Seconds from 1 to the sustain level",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "hold": {
   "doc": "Seconds at the sustain level before the release",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "release": {
   "doc": "Seconds from the sustain level to 0",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "sustain": {
   "def": 0.0,
   "doc": "Level after the decay, 0..1, default 0",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "velocity": {
   "def": 0.0,
   "doc": "Share 0..1 of the amount that the velocity of a note scales, amount (1 - share + share v), default 0; it scales vowel too",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "vowel": {
   "def": 0.0,
   "doc": "SynthFormant: vowels the vowel moves by at level 1, -4..4, default 0; the moved vowel is clamped to 0..4",
   "max": 4.0,
   "min": -4.0,
   "unit": ""
  }
 },
 "SynthGlide": {
  "channel": {
   "doc": "Mono voice of the one shots of legato sounds, default 0: legato sounds of the same channel share one voice"
  },
  "legato": {
   "doc": "Play as a note of a mono voice, default false"
  },
  "time": {
   "def": 0.0,
   "doc": "Seconds of a glide, 0 to 10, default 0",
   "log": true,
   "max": 10.0,
   "min": 0.005,
   "unit": "s",
   "zero": true
  }
 },
 "SynthLfo": {
  "amplitude": {
   "doc": "Tremolo: share of the level it takes at the bottom of the wave, 0..1",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "cutoff": {
   "doc": "Octaves of the cutoff of the SynthFilter at the top of the wave, -10..10; nothing on a SynthFormant filter",
   "max": 10.0,
   "min": -10.0,
   "unit": "oct"
  },
  "delay": {
   "doc": "Seconds after the start of the note before it moves anything",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "fade": {
   "doc": "Seconds it fades in over after its delay, 0 at once",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "hard_sync": {
   "def": 0.0,
   "doc": "Semitones the ratio of SynthOscillator.sync moves by at the top of the wave, -48..48, default 0; the moved ratio is clamped to 1..16. On a layer it syncs that layer, from a ratio of 1 when it has none; on the root it moves every layer with a ratio over 1",
   "max": 48.0,
   "min": -48.0,
   "unit": "st"
  },
  "phase": {
   "doc": "Phase it starts at in turns, 0..1, 0.25 is the top of a sine",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "pitch": {
   "doc": "Cents of vibrato at the top of the wave, -2400..2400",
   "max": 2400.0,
   "min": -2400.0,
   "unit": "ct"
  },
  "position": {
   "def": 0.0,
   "doc": "Frames the position of SynthWavetable layers moves by at the top of the wave, -256..256, default 0; the moved position is clamped to the frames of the table",
   "max": 256.0,
   "min": -256.0,
   "unit": ""
  },
  "pulse_width": {
   "doc": "Share of the period the pulse width of squares moves by at the top of the wave, -0.49..0.49",
   "max": 0.49,
   "min": -0.49,
   "unit": ""
  },
  "random_phase": {
   "doc": "Start at a random phase per play instead, seeded from SynthSound.seed and the count of plays"
  },
  "rate": {
   "def": 5.0,
   "doc": "Hz, 0..100, default 5",
   "log": true,
   "max": 100.0,
   "min": 0.05,
   "unit": "Hz",
   "zero": true
  },
  "sync": {
   "def": 0.0,
   "doc": "Cycle in beats of the Transport, 0..256, default 0 for rate. When positive it wins over rate, like Delay.sync: a beat is a quarter note at the tempo of the Transport, 120 without one, so 0.25 is a sixteenth and 1.5 a dotted quarter. The LFO starts at phase + fract(beat / sync), beat being the position of the Transport at the start of the note, so every note sees the same phase of the grid. A voice keeps the tempo it started at: a tempo change applies to the next notes. Delay and fade stay in seconds; SynthLfoRandom steps once per cycle",
   "max": 256.0,
   "min": 0.0,
   "unit": ""
  },
  "vowel": {
   "def": 0.0,
   "doc": "Vowels of the SynthFormant filter at the top of the wave, -4..4, default 0; the moved vowel is clamped to 0..4. Of the sound on a layer too, as the cutoff",
   "max": 4.0,
   "min": -4.0,
   "unit": ""
  },
  "wave": {
   "doc": "The wave, default SynthLfoSine"
  }
 },
 "SynthModEnvelope": {
  "attack": {
   "doc": "Seconds from 0 to 1, at least 0.5 ms",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "attack_curve": {
   "doc": "Curve of the attack, 0 linear (default), -16..16",
   "max": 16.0,
   "min": -16.0,
   "unit": ""
  },
  "curve": {
   "doc": "Curve of the decay and the release, 0 linear (default), -16..16",
   "max": 16.0,
   "min": -16.0,
   "unit": ""
  },
  "decay": {
   "doc": "Seconds from 1 to the sustain level",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "hold": {
   "doc": "Seconds at the sustain level before the release",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "key_scale": {
   "def": 0.0,
   "doc": "Key scaling 0..1, default 0, as SynthEnvelope.key_scale",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "release": {
   "doc": "Seconds from the sustain level to 0",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "sustain": {
   "def": 0.0,
   "doc": "Level after the decay, 0..1, default 0",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  }
 },
 "SynthModRoute": {
  "amount": {
   "def": 0.0,
   "doc": "Amount at a source value of 1, in the unit of the target, default 0",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "source": {
   "doc": "The source, default SynthSourceAmpEnvelope"
  },
  "target": {
   "doc": "The target, default SynthTargetCutoff"
  }
 },
 "SynthOscillator": {
  "audible": {
   "doc": "Whether a modulator is heard as well, at its level"
  },
  "color": {
   "doc": "SynthNoise: color of the noise, default SynthNoiseWhite. The white noise it filters is seeded as white noise is, so a colored layer is as deterministic; each unison voice filters its own. Ignored by the other waves"
  },
  "feedback": {
   "doc": "Radians of its own output on its phase, 0..1.5, a sawlike tone",
   "max": 1.5,
   "min": 0.0,
   "unit": ""
  },
  "frequency": {
   "def": 440.0,
   "doc": "Hz, default 440, ignored by noise",
   "log": true,
   "max": 20000.0,
   "min": 20.0,
   "soft": true,
   "unit": "Hz"
  },
  "frequency_end": {
   "doc": "Hz the pitch sweeps to, 0 for no sweep",
   "log": true,
   "max": 20000.0,
   "min": 20.0,
   "soft": true,
   "unit": "Hz"
  },
  "index": {
   "doc": "SynthPhase: peak index in radians at the top of its envelope, 0..32, limited against the frequency so FM aliases little; SynthRing: depth 0..1. Default 1",
   "max": 32.0,
   "min": 0.0,
   "unit": ""
  },
  "level": {
   "def": 1.0,
   "doc": "Linear gain, default 1",
   "max": 4.0,
   "min": 0.0,
   "soft": true,
   "unit": "gain"
  },
  "modulation": {
   "doc": "What it does with its signal, default SynthMix"
  },
  "pulse_width": {
   "def": 0.5,
   "doc": "SynthSquare: share of the period it is high, 0.01..0.99, default 0.5; band limited at every width, without DC",
   "max": 0.99,
   "min": 0.01,
   "unit": ""
  },
  "sub": {
   "def": 0.0,
   "doc": "Level of the sub oscillator, 0..1, default 0 (off): a band limited square sub_octave octaves under the layer, added to its signal before its level and envelope. It is derived from the phase of the layer, so it follows its pitch, sweep, glide, vibrato and unison, but not its phase modulation or feedback; a modulator modulates with the sub included. Ignored by SynthNoise and SynthPluck",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "sub_octave": {
   "doc": "Octaves the sub is under the layer, 1 or 2, default 1; 0 counts as 1 and other values are clamped"
  },
  "sweep": {
   "doc": "Seconds of the sweep, exponential in frequency",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "sync": {
   "def": 0.0,
   "doc": "Hard sync: ratio 1..16 of the wave heard to the note, default 0, which counts as 1 (off). The note frequency of the layer, its sweep, glide, vibrato, unison detune and the pitch of the voice included, is the master: the wave runs at the master times the ratio and restarts at phase 0 on every cycle of the master, band limited at the restart, for sync leads and, with SynthLfo.hard_sync, sync sweeps. Each unison voice syncs to its own master; the sub stays at the master and phase modulation adds to the synced phase. Ignored by SynthNoise and SynthPluck",
   "max": 16.0,
   "min": 0.0,
   "unit": ""
  },
  "velocity": {
   "def": 0.0,
   "doc": "SynthPhase, SynthRing: share 0..1 of the index that the velocity of a note scales, index (1 - share + share v), so soft notes have fewer sidebands; default 0",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "wave": {
   "doc": "The wave, default SynthSine"
  }
 },
 "SynthPitchEnvelope": {
  "amount": {
   "def": 0.0,
   "doc": "Semitones at the start of a note, -48..48, default 0",
   "max": 48.0,
   "min": -48.0,
   "unit": "st"
  },
  "curve": {
   "doc": "Curve of the decay and the release, 0 linear (default), -16..16, as SynthEnvelope.curve: positive moves fast at first",
   "max": 16.0,
   "min": -16.0,
   "unit": ""
  },
  "decay": {
   "def": 0.0,
   "doc": "Seconds back to the note, default 0",
   "log": true,
   "max": 2.0,
   "min": 0.001,
   "soft": true,
   "unit": "s",
   "zero": true
  },
  "release_amount": {
   "def": 0.0,
   "doc": "Semitones the pitch moves to over the release, -48..48, default 0",
   "max": 48.0,
   "min": -48.0,
   "unit": "st"
  },
  "sync": {
   "def": 0.0,
   "doc": "Semitones the ratio of SynthOscillator.sync moves by at the start of a note, -48..48, default 0, returning to 0 with the decay: the sweep of a sync lead. Only on layers with a ratio over 1; the moved ratio is clamped to 1..16",
   "max": 48.0,
   "min": -48.0,
   "unit": "st"
  }
 },
 "SynthSound": {
  "drift": {
   "def": 0.0,
   "doc": "Cents of analog drift, 0..50, default 0: the pitch of each voice and of each unison voice of each layer wanders smoothly within this, over about 0.2 to 2 seconds, and the cutoff of its SynthFilter by a quarter of it; seeded from seed and the count of plays",
   "max": 50.0,
   "min": 0.0,
   "unit": "ct"
  },
  "free_phase": {
   "doc": "Start the oscillators of each play, and their subs, at a random phase instead of 0, seeded as drift; default false. Unison voices always start at random phases"
  },
  "level_variance": {
   "def": 0.0,
   "doc": "Random share of the level per play, 0..1, default 0",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "pitch_variance": {
   "def": 0.0,
   "doc": "Random share of the pitch per play, 0..1, default 0",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "seed": {
   "doc": "Seed of the noise and the variances, with a count of plays"
  }
 },
 "SynthString": {
  "brightness": {
   "def": 0.5,
   "doc": "Lowpass of the pluck and in the loop: 0 dark, the harmonics start soft and die fast, to 1 bright, a metallic ring; default 0.5",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "decay": {
   "def": 2.0,
   "doc": "Seconds the fundamental takes to fall by 60 dB, 0.01..60, default 2",
   "log": true,
   "max": 60.0,
   "min": 0.01,
   "unit": "s"
  },
  "hardness": {
   "def": 0.8,
   "doc": "Lowpass of the noise that plucks: 0 a soft finger, 1 a hard pick; default 0.8",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "velocity": {
   "def": 0.0,
   "doc": "Share 0..1 of the hardness that the velocity of a note scales, hardness (1 - share + share v), so soft notes are plucked softer; default 0",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  }
 },
 "SynthTable": {
  "envelope": {
   "def": 0.0,
   "doc": "Frames the position moves by at level 1 of the envelope of the layer, either way, default 0",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "position": {
   "def": 0.0,
   "doc": "Frame it plays, 0..frames - 1, default 0",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "table": {
   "doc": "The entity with the Wavetable"
  }
 },
 "SynthUnison": {
  "detune": {
   "def": 10.0,
   "doc": "Cents from the lowest copy to the highest, 0 to 100, default 10",
   "max": 100.0,
   "min": 0.0,
   "unit": "ct"
  },
  "voices": {
   "doc": "Copies of the oscillator, 1 to 7, default 3"
  },
  "width": {
   "def": 0.0,
   "doc": "0 (every copy in the center, mono) to 1 (the lowest copy left, the highest right), default 0",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  }
 },
 "TapeStop": {
  "active": {
   "def": 0.0,
   "doc": "0 to 1, default 0: 0.5 and above stops, under it starts",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "curve": {
   "def": 0.0,
   "doc": "Shape of the ramps, -1 to 1, default 0 (linear)",
   "max": 1.0,
   "min": -1.0,
   "unit": ""
  },
  "start_time": {
   "def": 0.2,
   "doc": "Seconds from the halt to full speed, 0.01 to 4, default 0.2",
   "log": true,
   "max": 4.0,
   "min": 0.01,
   "unit": "s"
  },
  "stop_time": {
   "def": 0.5,
   "doc": "Seconds from full speed to the halt, 0.01 to 4, default 0.5",
   "log": true,
   "max": 4.0,
   "min": 0.01,
   "unit": "s"
  }
 },
 "Track": {
  "humanize": {
   "doc": "Random timing and velocity of the notes, default none"
  },
  "instrument": {
   "doc": "The entity with Instrument that plays the notes of its note clips; a track of only audio clips and automation needs none"
  },
  "priority": {
   "doc": "Priority of the voices of its notes and audio clips, 0..255, default 1: at the voice cap of 64 a play steals only a voice of at most its priority, so one shots (0) cannot steal the voices of a track; streams play at 255"
  },
  "swing": {
   "def": 0.0,
   "doc": "0..0.75, default 0: the off-beat of every pair of steps of swing_grid moves later by swing step, the downbeats stay put; notes keep their duration, audio clips do not swing",
   "max": 0.75,
   "min": 0.0,
   "unit": ""
  },
  "swing_grid": {
   "doc": "The step of the swing, default TrackSwing8"
  }
 },
 "TrackHumanize": {
  "timing": {
   "def": 0.0,
   "doc": "Milliseconds, 0..50, default 0: the most a note start moves either way",
   "log": true,
   "max": 50.0,
   "min": 0.025,
   "unit": "ms",
   "zero": true
  },
  "velocity": {
   "def": 0.0,
   "doc": "Share, 0..1, default 0: the most the velocity changes either way, relative, clamped to 0..1",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  }
 },
 "TransientShaper": {
  "attack": {
   "def": 0.0,
   "doc": "-1 to 1, default 0: above 0 boosts the transients, below 0 softens them",
   "max": 1.0,
   "min": -1.0,
   "unit": ""
  },
  "sustain": {
   "def": 0.0,
   "doc": "-1 to 1, default 0: above 0 boosts the tail, below 0 shortens it",
   "max": 1.0,
   "min": -1.0,
   "unit": ""
  }
 },
 "Transport": {
  "beat_unit": {
   "doc": "Bottom of the time signature, 1..64, default 4"
  },
  "beats_per_bar": {
   "doc": "Top of the time signature, 1..64, default 4"
  },
  "lookahead": {
   "def": 0.15,
   "doc": "Seconds scheduled ahead of the clock, 0.05..0.5, default 0.15",
   "max": 0.5,
   "min": 0.05,
   "unit": "s"
  },
  "loop": {
   "doc": "Wrap from loop_end to loop_start, default false"
  },
  "loop_end": {
   "doc": "Beat the loop wraps at, after loop_start",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "loop_start": {
   "doc": "First beat of the loop",
   "max": 1.0,
   "min": 0.0,
   "soft": true,
   "unit": ""
  },
  "metronome": {
   "doc": "Click on every beat while playing, accented on the first of a bar, default false"
  },
  "playing": {
   "doc": "Whether the timeline plays, default false: false stops at the position, true plays on from it"
  },
  "position": {
   "doc": "Beats; written by box.audio while it plays, a write seeks",
   "max": 16.0,
   "min": 0.0,
   "soft": true,
   "unit": "beats"
  },
  "tempo": {
   "def": 120.0,
   "doc": "Quarter notes per minute, 20..999, default 120",
   "max": 999.0,
   "min": 20.0,
   "unit": ""
  }
 },
 "Vocoder": {
  "attack": {
   "def": 0.005,
   "doc": "Seconds in which a band envelope rises 63% of a step, default 0.005, 0.0001 to 1",
   "log": true,
   "max": 1.0,
   "min": 0.0001,
   "unit": "s"
  },
  "bands": {
   "doc": "Bands, 8 to 32, default 16"
  },
  "high": {
   "def": 8000.0,
   "doc": "Center of the highest band in Hz, default 8000, at least an octave over low and under 0.45 of the sample rate",
   "log": true,
   "max": 20000.0,
   "min": 20.0,
   "soft": true,
   "unit": "Hz"
  },
  "low": {
   "def": 100.0,
   "doc": "Center of the lowest band in Hz, default 100, at least 20",
   "log": true,
   "max": 20000.0,
   "min": 20.0,
   "soft": true,
   "unit": "Hz"
  },
  "mix": {
   "def": 1.0,
   "doc": "Wet level, 0 to 1, default 1; the dry signal passes at 1 - mix",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  },
  "modulator": {
   "doc": "Channel (an AudioBus) whose output is the modulator, the key"
  },
  "q": {
   "def": 1.0,
   "doc": "Sharpness of the bands, 0.25 to 8, default 1: at 1 neighboring bands cross at -3 dB, above 1 they are narrower",
   "max": 8.0,
   "min": 0.25,
   "unit": ""
  },
  "release": {
   "def": 0.05,
   "doc": "Seconds in which it falls 63% of a step, default 0.05, 0.001 to 5",
   "log": true,
   "max": 5.0,
   "min": 0.001,
   "unit": "s"
  },
  "shift": {
   "def": 0.0,
   "doc": "-1 to 1, default 0: moves the carrier bands up to an octave up or down against the modulator bands, for a formant shift",
   "max": 1.0,
   "min": -1.0,
   "unit": ""
  },
  "unvoiced": {
   "def": 0.0,
   "doc": "0 to 1, default 0: white noise added to the carrier of the high bands, so consonants come through",
   "max": 1.0,
   "min": 0.0,
   "unit": ""
  }
 },
 "Wavetable": {
  "frame_size": {
   "doc": "Samples per frame of the sound, 64..4096, default 2048; 0 is the default"
  },
  "kind": {
   "doc": "The table, default WavetableBasic; ignored with a sound"
  },
  "sound": {
   "doc": "An entity with AudioPcm or SoundFile whose frames make the table in place of kind, 0 for none, the default"
  }
 }
};
window.BOX_AUDIO_DOCS = {
 "AudioBus": "A mixer bus, the component of a mixer channel: a group, a return, an SFX bus, the bus of a track and the master.",
 "AudioClip": "A clip of recorded audio, reusable as a NoteClip is: a part of a sound that a ClipPlacement plays on the bus of its track, a vocal, a loop or a stem.",
 "AudioEmitter": "Plays a sound once at random intervals, at a random point of a shape, with a random volume and pitch, seeded from seed, the entity and the count of plays: insects, drips and rain.",
 "AudioEqBand": "A band of an Equalizer: an RBJ cookbook biquad.",
 "AudioFilter": "A filter, an effect in an insert slot of an AudioBus, as a Reverb is: the state variable filter of SynthFilter on the signal of a bus, for a sweep on a music group, a muffled SFX bus or a telephone voice.",
 "AudioMacro": "A macro, on an entity of its own: one value of 0..1 that its AudioMacroMap children turn into several members of buses and effects at once, each over its own range and curve, like a knob of a Combinator.",
 "AudioMacroMap": "A map of a macro, a child of the AudioMacro entity: one member of a bus or an effect that the macro moves from min at a value of 0 to max at 1.",
 "AudioMeter": "The levels of a mixer channel, read only: box.audio writes it each frame on every channel entity that has it, the master (box.audio.master) included, and on effect entities.",
 "AudioModulator": "A modulator, on an entity of its own: moves a float member of a bus or an effect on its own, in time with the song, for a filter that wobbles in eighths, a send that pulses, a tremolo, an auto-pan or an auto-wah: wobble { AudioModulator: {target: bass_filter, component: AudioFilter, member: \"cutoff\", scale: AudioModOctaves, depth: 2, sync: 0.5} }.",
 "AudioOutput": "The bus a sound or a source plays on.",
 "AudioPcm": "Samples of a sound, the sound kind of this module.",
 "AudioSend": "A send of a mixer channel to another bus.",
 "AudioSettings": "Settings of the audio of the world, a singleton.",
 "AudioSong": "The song to play: one .flecs file, a singleton.",
 "AudioSongStatus": "Outcome of the load of an AudioSong, a singleton written by box.audio and read only for applications.",
 "AudioSource": "A sound that plays while the entity exists and playing is true.",
 "AudioSpatial": "Places an AudioSource in the world: it follows the WorldTransform of the entity, parents included, gets quieter with its distance from the listener and pans with its direction.",
 "AudioStatus": "What the audio output does, a singleton written by box.audio and read only for apps.",
 "AudioStream": "Music: one Ogg Vorbis file that streams while the entity exists and playing is true.",
 "AudioStreamStatus": "What an AudioStream does, written by box.audio and read only for applications.",
 "AutomationClip": "A clip of automation, reusable as a NoteClip is: a Curve over beats that a ClipPlacement on a Track plays into one float member of a bus or an effect, for a filter sweep, a send that opens for a chorus or the fade of a track: sweep { AutomationClip: {target: synth, component: AudioFilter, member: \"cutoff\", curve: {keys: [{0, 200}, {4, 5000}]}} }.",
 "BeatRepeat": "A beat repeat, an effect in an insert slot of an AudioBus, as a Reverb is: the stutter of a fill, which captures a slice of the beat and repeats it, each repeat quieter, pitched or reversed, while active is up.",
 "Chorus": "A chorus, an effect in an insert slot of an AudioBus, as a Reverb is: up to 4 voices, each a tap of a delay line that a sine LFO moves around delay by depth, their LFOs spread evenly in phase and the voices panned across the stereo field by spread: pad { AudioBus: {effects: [pad]}; Chorus: {voices: 3, spread: 1, mix: 0.4} }.",
 "ClipPlacement": "A clip on the timeline, on a child of a Track: a NoteClip, an AudioClip or an AutomationClip.",
 "Compressor": "A compressor, an effect in an insert slot of an AudioBus, as a Reverb is: feed-forward and stereo linked, it detects the peak of the louder channel, smooths it with its attack and release in dB and reduces the gain over its threshold by its ratio, with a soft knee.",
 "Delay": "A delay, an effect in an insert slot of an AudioBus, as a Reverb is: echoes of its input every time seconds, each feedback times the last and darker by a lowpass at damping Hz in the feedback path.",
 "Equalizer": "An equalizer, an effect in an insert slot of an AudioBus, as a Reverb is: up to BOX_AUDIO_EQ_BANDS bands in a row.",
 "FrequencyShifter": "A frequency shifter, an effect in an insert slot of an AudioBus, as a Reverb is: every partial moves by the same number of Hz, so harmonic sounds turn inharmonic.",
 "Gate": "A gate, an effect in an insert slot of an AudioBus, as a Reverb is: it opens when the peak of the louder channel reaches its threshold, stays open while the level is above the threshold less 4 dB of hysteresis and for its hold after, and then closes to its range.",
 "Instrument": "An instrument: a synth or a sampler that plays notes with a pitch, a velocity, a pan and a duration, ad hoc with box_audio_play_note, on the bus of its AudioOutput, else of its sound.",
 "Note": "A note of a NoteClip, in beats from the start of the clip.",
 "NoteClip": "A clip of notes, reusable: usually a prefab or a plain entity that any number of ClipPlacement on any tracks name.",
 "PatternGate": "A pattern gate, an effect in an insert slot of an AudioBus, as a Reverb is: a tempo-synced level pattern that chops a sustained sound into rhythm, the trance gate.",
 "Phaser": "A phaser, an effect in an insert slot of an AudioBus, as a Reverb is: a chain of first order allpasses whose break frequency a sine LFO sweeps around a center, mixed with the dry signal, so its notches move up and down at the rate: keys { AudioBus: {effects: [keys]}; Phaser: {stages: 6, rate: 0.3, feedback: 0.5} }.",
 "PitchShifter": "A pitch shifter, an effect in an insert slot of an AudioBus, as a Reverb is: thickens a lead by a few cents either way, adds octaves to basses and leads and makes harmonies, without changing the speed of what plays through it.",
 "Reverb": "A reverb, an effect: it runs in the insert slot of an AudioBus that names its entity in effects, and does nothing while no slot names it.",
 "Rotary": "A rotary, an effect in an insert slot of an AudioBus, as a Reverb is: a rotating speaker cabinet, the swirl of an organ, also on guitars, electric pianos and vocals.",
 "Saturator": "A saturator, an effect in an insert slot of an AudioBus, as a Reverb is: warmth, grit or lo-fi for a drum bus, a synth bus or the master.",
 "Song": "What a host reads of the song that is loaded, a singleton that always exists, with its default value while no song is loaded.",
 "SongLane": "A channel of a song that a host can meter: a name to show and the bus to read.",
 "SoundFile": "A sound decoded from a WAV or FLAC file on the job system, found as a TexturePath is.",
 "SoundStatus": "Outcome of the load of a SoundFile, written by box.audio and read only for applications.",
 "SynthDrive": "A drive stage of a synth sound, on its root: the mix of the layers of each voice runs through a tanh soft clipper before the SynthFilter, or before the amp without one, where an analog synth gets most of its thickness.",
 "SynthEnvelope": "An envelope: from 0 to 1 over attack, to sustain over decay, hold seconds there, then to 0 over release.",
 "SynthFilter": "The filter of a synth sound, on its root: a state variable filter on the mix of the layers, stable at any cutoff and resonance.",
 "SynthFilterEnvelope": "An envelope of the cutoff of the SynthFilter of the same root, with the segments of a SynthEnvelope: the cutoff is cutoff times 2 to the amount times the level of the envelope, so it rises over the attack, settles at the sustain and returns over the release.",
 "SynthGlide": "Portamento and mono legato of a synth sound, on its root.",
 "SynthLfo": "An LFO of a synth sound: a wave at a rate that moves the pitch, the level, the cutoff, the pulse width of squares, the ratio of hard sync and the position of wavetables of the sound, each by its own depth, after a delay and a fade in.",
 "SynthModEnvelope": "A third envelope of a synth sound, on its root, with the segments of a SynthEnvelope: a source of SynthModRoute, SynthSourceModEnvelope.",
 "SynthModRoute": "A route of the modulation matrix of a synth sound, on an entity of its own: the value of a source times an amount moves a target, on top of the fixed routes (the depths of SynthLfo, SynthFilterEnvelope.amount, the velocity and keytrack fields), once per control block of the LFOs.",
 "SynthOscillator": "An oscillator of a synth sound, a layer: on the root of the sound, on a child of it, or on a child of a layer.",
 "SynthPitchEnvelope": "A pitch envelope of a synth sound, relative to the note so it fits every pitch: the pitch starts amount semitones off the note and returns to it over the decay with a curve, the drop of a kick or a tom, the blip of a zap or the scoop of brass.",
 "SynthSound": "A sound made by the synth from the components of this entity and its children, without a file: SynthOscillator layers, each with an optional SynthUnison, a SynthTable, a SynthEnvelope, a SynthDrive, a SynthFilter, a SynthFilterEnvelope, a SynthPitchEnvelope, a SynthGlide, SynthLfo, a SynthModEnvelope and SynthModRoute children.",
 "SynthString": "The string of a SynthPluck layer, on the layer: a delay line a period long, plucked with a burst of seeded noise, whose loop damps it.",
 "SynthTable": "The wavetable of a SynthWavetable layer, on the entity of its SynthOscillator.",
 "SynthUnison": "Unison of a synth layer, on the entity of its SynthOscillator: the layer plays as several copies of its oscillator, detuned evenly over a spread in cents and placed from left to right over the stereo width, each starting at a seeded random phase.",
 "TapeStop": "A tape stop, an effect in an insert slot of an AudioBus, as a Reverb is: the drop of a transition, which slows what plays through it down to a halt, pitch and time together, and spins it back up.",
 "Track": "A track of the timeline: a mixer channel whose notes play on an instrument.",
 "TrackHumanize": "How far the notes of a Track move at random from where they are programmed.",
 "TransientShaper": "A transient shaper, an effect in an insert slot of an AudioBus, as a Reverb is: more or less punch and tail on drums and plucks, without a threshold, at most 12 dB either way: drums { AudioBus: {effects: [drums]}; TransientShaper: {attack: 0.6, sustain: -0.4} }.",
 "Transport": "The transport of the timeline in beats, a singleton: the tempo, the time signature, the position, play and stop, the loop and the metronome.",
 "Vocoder": "A vocoder, an effect in an insert slot of an AudioBus, as a Reverb is: the spectrum of its modulator, a channel keyed as the sidechain of a Compressor, shapes the input of its own channel, the carrier.",
 "Wavetable": "A wavetable, on an entity of its own, usually a prefab, that the SynthTable of wavetable layers names: frames of one period each that a layer morphs through."
};
