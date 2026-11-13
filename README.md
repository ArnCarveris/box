# Box

A data-driven 2D/3D game framework built on [Flecs](https://github.com/SanderMertens/flecs)
and WebGPU.

## Prerequisites

- [bake3](https://github.com/SanderMertens/bake3)
- [flecs](https://github.com/SanderMertens/flecs)
- A Rust toolchain

## Build

```sh
bake3 --local-env
```

## Run

```sh
bake3 run apps/boxrun --local-env -- etc/scenes/town.flecs                      # open a scene in a window
bake3 run apps/boxrun --local-env -- --headless --frames 10 etc/scenes/town.flecs  # run 10 frames without a window and exit
bake3 run apps/boxrun --local-env -- --headless --frames 3 --delta-time 1/60 --out /tmp/town.png etc/scenes/town.flecs  # render a frame to a PNG
bake3 run apps/boxrun --local-env -- --headless --timeout 60 --port 27760 etc/scenes/town.flecs  # take shell commands on a REST port
bake3 run apps/boxrun --local-env -- --headless --unguarded --frames 10 etc/scenes/town.flecs  # start even when the machine is oversubscribed
bake3 run apps/boxrun --local-env -- --headless --async-loads --frames 600 etc/scenes/town.flecs  # don't wait for loads, to time frames like a window
bake3 run apps/boxrun --local-env -- --headless --fail-on-error --frames 20 etc/scenes/town.flecs  # exit with 1 when an error was logged
bake3 run apps/boxrun --local-env=agent_a -- etc/scenes/town.flecs              # a separate build environment per agent
bake3 --target em --local-env apps/boxrun                                       # build for the browser
```

## Test

```sh
bake3 runall test/cpu --local-env -- -j 8                     # every CPU test project
bake3 run test/cpu/core --local-env -- -j 8                   # one test project
bake3 run test/cpu/core --local-env -- Args -j 8              # one suite
bake3 run test/cpu/core --local-env -- Args.defaults          # one case
bake3 run test/cpu/core --local-env --test-report-failure-only -- -j 8 --json /tmp/core.json  # record the failures
bake3 run test/cpu/core --local-env -- -j 8 --ignore /tmp/core.json                          # fail only on new failures
bake3 run test/gpu/render --local-env -- -j 4                 # a GPU test project, needs a GPU
python3 tools/integration.py visual                           # render the visual scenes and compare with the baselines
python3 tools/integration.py visual --update-baseline <scene> # store the render of a scene as its baseline
```

## Docs

- [Audio](docs/audio.md): the guide to sound and music in box, from a first sound to a mixed
  song on the timeline, the web rules, captures and the reference of the audio core, the mixer
  and the sequencer.
- [Curves](docs/curves.md): the guide to authoring curves for particles, lights, emission,
  animation, paths and eases, with recipes, the errors and the cost.

## Commands

A running app takes shell commands on its REST port (`--port`, `PUT /shell/session`), in the
in game console (the grave key) and, in the browser, through
`Module.cwrap('box_command_web_run', 'string', ['string'])`. `help` lists them all. Besides the
builtins of the flecs shell (`ls`, `get`, `set`, `query`, ...):

| Command | What it does |
|---------|--------------|
| `capture [camera\|surface]` | Write a PNG of the active camera or a camera, as its view shows it, or of a surface with all its views |
| `render <subject>` | Write a PNG of an entity, prefab or template on its own |
| `fit [camera]` | Move the active camera or a camera so that it sees the whole scene |
| `audio` | Print the audio state, rate, voices, load and the buses in processing order |
| `audio.volume <volume>` | Set `Settings.master_volume`, 0 to 4 |
| `audio.mute [state]` | Set (`on`, `off`) or toggle `Settings.mute` |
| `audio.solo <bus> [state]` | Set or toggle `Bus.solo`; `audio.solo none` clears every solo |
| `audio.mute_bus <bus> [state]` | Set or toggle `Bus.mute` of a bus |
| `audio.bypass <effect> [state]` | Bypass an effect, or stop bypassing it, with `Disabled` |
| `audio.note <subject> <note> [velocity] [duration]` | Play one note (C2, F#3 or a MIDI number) of a sound or instrument now, velocity 0 to 1, for `duration` seconds (default 0.5) |
| `audio.play`, `audio.stop`, `audio.seek <beats>`, `audio.tempo <bpm>` | Drive the transport |
| `audio.song [file]` | Show the song, load a song file or unload it with `none` |
| `audio.capture <seconds>` | Record the master output to a 16 bit stereo WAV resource |
| `audio.render <subject> [options...]` | Render a sound, instrument, bus stem or song section on its own, faster than real time, to a WAV resource |
| `audio.analyze <wav> [from] [to]` | Print loudness, peaks, bands, stereo, onsets, tail, clicks and pitch of a WAV |
| `audio.diff <a> <b> [from] [to]` | Compare two WAVs: bit identity, length, loudness, true peak, bands, stereo, and the null test residual after alignment with its loudest spans and the bars that differ most |
| `audio.plot <wav> [options...]` | Draw a PNG resource of a WAV: `spectrogram` (default), `waveform`, `spectrum` or `loudness`, `size=WxH`, `from=s`, `to=s`, `min_db=dB`, with labeled axes and the bar lines of a capture of a song |
| `audio.meter [options...]` | Measure peak, RMS, integrated and short-term LUFS per bus (`[bus...] [seconds\|bars=a..b]`) and the gain reduction of its dynamics, PatternGate and Saturator inserts over a span of the live output, with the headroom; run `audio.meter` again for the table |
| `audio.notes <track> [options...]` | Print the notes of a track per bar (position, pitch or drum sound, length, velocity), or with `roll` as a text piano roll of one column per sixteenth; `bars=a..b` limits the bars |
| `audio.chords [options...]` | Print the chords of a section (`[track...] [bars=a..b]`) per bar or per beat, its key, the key of the song and the notes outside the key, from the note data |
| `audio.check [options...]` | Lint the song, or the tracks of the scene (`[static] [bars=a..b]`): one line per problem (note range, overlaps, clips past the end, routing, effects, lanes, and from one offline play voice steals, refused effects, clipping buses, the limiter, loudness and silent tracks) with its place and a hint |
| `fps [rate]`, `frames` | Toggle the fps panel or cap the rate, print the frame timing |
| `edit [on\|off]` | Open or close the editor |
| `logs [level] [count]` | Show the last log messages |
| `key_*`, `mouse_*`, `action_*` | Send input, see `help` |

`capture` of a camera that a view renders shows it as on screen, with the debug drawing of the
view. `capture`, `audio.capture`, `audio.render` and `audio.plot` return a resource
(`flecs.script.shell.resource`, its name and content type) at once and fill it when the frames are
in; fetch it with `GET /shell/session/<id>/resource/<name>`, which is empty until then. In the
browser, which has no REST port, `audio.capture` writes `/tmp/audio_capture_<n>.wav`,
`audio.render` `/tmp/audio_render_<n>.wav` and `audio.plot` `/tmp/audio_plot_<n>.png` in the file
system of the page and return that path.
Headless with a fixed `--delta-time`, two runs capture identical audio, so a render can be listened
to and compared. See [docs/audio.md](docs/audio.md).

## Screenshots
<img width="1600" height="973" alt="image" src="https://github.com/user-attachments/assets/f79c8f68-4813-4564-88e7-5a14485cdaf8" />
<img width="800" height="504" alt="Screenshot 2026-11-02 at 1 45 28 PM" src="https://github.com/user-attachments/assets/bb85ef3b-0f08-4391-a2cc-d6f01fe4db93" />
<img width="1600" height="997" alt="image" src="https://github.com/user-attachments/assets/5c39170a-b799-4e6f-beda-3de1e1baca12" />
<img width="1600" height="898" alt="image" src="https://github.com/user-attachments/assets/d98127e7-e923-4d96-a2f4-b92f8a3a8113" />
<img width="1600" height="1010" alt="image" src="https://github.com/user-attachments/assets/465b29c5-e6ba-406c-b95b-9787aa8d485b" />
<img width="3966" height="2502" alt="image" src="https://github.com/user-attachments/assets/9d370b09-d7be-4b59-b65e-0c6c4b898a2b" />
<img width="1980" height="1241" alt="Screenshot 2026-10-20 at 12 24 00 AM" src="https://github.com/user-attachments/assets/7384e4e7-15fd-430f-bd1d-d8a79324d61d" />
<img width="1600" height="959" alt="image" src="https://github.com/user-attachments/assets/2b8abc1f-643f-4ac7-a116-97c59f167c65" />
