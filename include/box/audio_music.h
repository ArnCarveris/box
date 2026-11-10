/**
 * @file box/audio_music.h
 * @brief Music: the transport, songs, instruments, clips, modulators, macros and tracks.
 */

#ifndef BOX_AUDIO_MUSIC_H
#define BOX_AUDIO_MUSIC_H

#include "types.h"
#include "curve.h"

#ifdef BOX_AUDIO_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** The transport of the timeline in beats, a singleton: the tempo, the time signature, the
 * position, play and stop, the loop and the metronome. A beat is a quarter note in every time
 * signature, so a bar is beats_per_bar * 4 / beat_unit beats.
 *
 * While it plays, box.audio writes position every frame from the clock of the output, the
 * beat the core renders now. A write of another position is a seek. See Transport in
 * docs/audio.md.
 */
ECS_STRUCT(Transport, {
    float tempo;           /**< Quarter notes per minute, 20..999, default 120 */
    int32_t beats_per_bar; /**< Top of the time signature, 1..64, default 4 */
    int32_t beat_unit;     /**< Bottom of the time signature, 1..64, default 4 */
    double position;       /**< Beats; written by box.audio while it plays, a write seeks */
    bool playing;          /**< Whether the timeline plays, default false: false stops at the
                            *   position, true plays on from it */
    bool loop;             /**< Wrap from loop_end to loop_start, default false */
    double loop_start;     /**< First beat of the loop */
    double loop_end;       /**< Beat the loop wraps at, after loop_start */
    float lookahead;       /**< Seconds scheduled ahead of the clock, 0.05..0.5, default
                            *   0.15 */
    bool metronome;        /**< Click on every beat while playing, accented on the first of a
                            *   bar, default false */
});

/** A channel of a song that a host can meter: a name to show and the bus to read */
ECS_STRUCT(SongLane, {
    char *name;            /**< Name of the channel, such as "Bass" */
    ecs_entity_t bus;      /**< The bus of the channel, an entity with AudioBus of the song */
});

/** What a host reads of the song that is loaded, a singleton that always exists, with its
 * default value while no song is loaded. A song file sets it at its end, after the buses of
 * lanes, and puts Rgb on box.audio.Song: box { audio { Song { Rgb: {...} } } }.
 *
 * box.audio resets the entity on unload, before the song's entities are deleted, and applies
 * Song to the Transport on load. See Song files in docs/audio.md.
 */
ECS_STRUCT(Song, {
    char *title;           /**< Title of the song, NULL without a song */
    float tempo;           /**< Quarter notes per minute, default 120 */
    int32_t beats_per_bar; /**< Top of the time signature, default 4 */
    int32_t beat_unit;     /**< Bottom of the time signature, default 4 */
    double beats;          /**< Length of the song in beats, 0 for unknown */
    bool loop;             /**< Loop the Transport over 0..beats, default true */
    ecs_vec(SongLane) lanes; /**< The channels a host can meter, in the order to show them */
});

/** The song to play: one .flecs file, a singleton. A new file fades out, unloads and drains
 * the old song before it loads the new one; an empty file or removing it unloads the song.
 *
 * The path is found as the one of a SoundFile. AudioSongStatus says how the load goes. See
 * box_audio_song_load, the shell command audio.song [file] and Song files in docs/audio.md.
 */
ECS_STRUCT(AudioSong, {
    char *file;            /**< The .flecs file of the song, NULL or empty for none */
});

/** Progress of the load of an AudioSong */
ECS_ENUM(SongState, {
    SongLoading = 0,       /**< The old song unloads, or the new one loads and its sound files
                            *   decode; the Transport does not play */
    SongReady = 1,         /**< The song is loaded and plays when Transport.playing is true */
    SongFailed = 2,        /**< The song did not load, error says why; nothing of it is left */
    SongNone = 3           /**< No song is loaded */
});

/** Outcome of the load of an AudioSong, a singleton written by box.audio and read only for
 * applications */
ECS_STRUCT(AudioSongStatus, {
    SongState state;       /**< Progress of the load */
    char *file;            /**< The file it is about, NULL for none */
    char *error;           /**< Why the load failed, NULL unless state is SongFailed */
    int32_t foreign;       /**< Entities the song changed that it did not create, each logged
                            *   with a warning; their changes stay after the song unloads */
});

/** How an Instrument spreads its notes over the stereo field. Each mode gives an offset that
 * is added to the pan of a note, the sum clamped to -1..1. */
ECS_ENUM(InstrumentSpreadMode, {
    InstrumentSpreadPitch,     /**< spread * clamp((pitch - root) / 24, -1, 1): lower notes to
                                *   the left, higher ones to the right, all the way two octaves
                                *   from the root */
    InstrumentSpreadRandom,    /**< spread * a random value in -1..1 per note, seeded from the
                                *   instrument and the count of its notes, so the same song
                                *   renders the same */
    InstrumentSpreadAlternate  /**< +spread and -spread on successive notes */
});

/** An instrument: a synth or a sampler that plays notes with a pitch, a velocity, a pan and a
 * duration, ad hoc with box_audio_play_note, on the bus of its AudioOutput, else of its sound.
 *
 * A note plays at the ratio 2^((pitch - root) / 12), clamped to 1/64..16 with one warning, at
 * the gain velocity^velocity_curve. A sampler starts without the 5 ms ramp of a play, so a drum
 * keeps its transient.
 *
 * With a gate the duration ends a note: a synth sustains, its holds ignored, and releases with
 * its envelopes, a sampler fades out over release. A gated note of a legato synth glides into
 * the voice of its channel while it holds, so overlapping notes are one phrase.
 */
ECS_STRUCT(Instrument, {
    ecs_entity_t sound;    /**< The sound that plays: SynthSound for a synth, AudioPcm or
                            *   SoundFile for a sampler */
    float root;            /**< MIDI note at which the sound plays as authored, default 69
                            *   (A4, 440 Hz) */
    float velocity_curve;  /**< Exponent of the velocity in the gain, default 2 */
    bool gate;             /**< The duration of a note ends it, default true; false plays the
                            *   sound as authored, like a drum hit */
    float release;         /**< Seconds a sampler note fades out over after its end, default
                            *   0.05; a synth uses the release of its envelopes */
    float spread;          /**< 0..1, default 0: how far the notes spread over the stereo
                            *   field, an offset added to the pan of each note */
    InstrumentSpreadMode spread_mode; /**< How the notes spread, default
                            *   InstrumentSpreadPitch */
});

/** A note of a NoteClip, in beats from the start of the clip.
 *
 * Its expression, in the style of MPE, is three curves over beats of the note sent to its voice
 * only: bend, mod and pressure. A curve without keys is no expression; past its last key a curve
 * holds. A legato note gives its expression to the voice it shares.
 *
 * In script: {start: 0, duration: 2, pitch: 60, bend: {keys: [{0, 0}, {2, 12}]}}.
 */
ECS_STRUCT(Note, {
    float start;           /**< Beats from the start of the clip */
    float duration;        /**< Beats */
    float pitch;           /**< MIDI note number, a float so microtones work */
    float velocity;        /**< 0..1, default 1 */
    float pan;             /**< Pan of the voice, -1 left to 1 right */
    Curve bend;            /**< Pitch bend in semitones, -48..48, over beats of the note: the
                            *   pitch of the voice times 2^(bend / 12), on synth and sample
                            *   voices. Without keys 0 */
    Curve mod;             /**< 0..1 over beats of the note: scales the vibrato of a synth
                            *   voice, the pitch depth of its LFOs. Without keys 1, so a sound
                            *   keeps its vibrato */
    Curve pressure;        /**< 0..1 over beats of the note: moves the cutoff of a synth voice
                            *   up by pressure times 2 octaves
                            *   (BOX_AUDIO_SYNTH_PRESSURE_OCTAVES). Without keys 0 */
});

/** A clip of notes, reusable: usually a prefab or a plain entity that any number of
 * ClipPlacement on any tracks name.
 *
 * box.audio keeps its own copy of the notes sorted by start, rebuilt when the clip is set. A
 * change applies to what has not been scheduled yet. Notes that start at or past length play
 * only in a placement that does not loop.
 */
ECS_STRUCT(NoteClip, {
    float length;          /**< Beats, default 4: the length of a pass when a placement loops
                            *   the clip, and of a placement of duration 0 */
    ecs_vec(Note) notes;   /**< The notes, in any order; the clip owns the keys of their
                            *   curves, copying the clip copies them */
});

/** A clip of recorded audio, reusable as a NoteClip is: a part of a sound that a ClipPlacement
 * plays on the bus of its track, a vocal, a loop or a stem.
 *
 * The offset is in seconds, not beats: a sound does not stretch with the tempo, so an offset in
 * beats would land on another sample after a tempo change. A pass of the clip lasts length
 * beats, or less when the sound ends first.
 */
ECS_STRUCT(AudioClip, {
    ecs_entity_t sound;    /**< The sound, an entity with AudioPcm or SoundFile */
    float offset;          /**< Seconds into the sound at which the clip starts */
    float length;          /**< Beats, default 4: the length of a pass when a placement loops
                            *   the clip, and of a placement of duration 0 */
});

/** A clip of automation, reusable as a NoteClip is: a Curve over beats that a ClipPlacement
 * on a Track plays into one float member of a bus, an effect or an AudioMacro value:
 * sweep { AutomationClip: {target: synth, component: AudioFilter, member: "cutoff", ...} }.
 *
 * While the Transport plays, the curve is sampled in the lookahead window and each value is
 * scheduled sample accurately. A clip whose target, component or member is unknown or not
 * automatable logs one error that names it, and does nothing until it changes.
 *
 * The automation owns the value the output hears, never the component. While no automation
 * covers the target the output has the value of the component again. See Automation in
 * docs/audio.md.
 */
ECS_STRUCT(AutomationClip, {
    ecs_entity_t target;    /**< The entity with the AudioBus or the effect component */
    ecs_entity_t component; /**< The component: AudioBus or an effect component */
    char *member;           /**< Path of a float member, such as "cutoff", "volume" or
                             *   "sends[1].level" */
    float length;           /**< Beats, default 4: the length of a pass when a placement loops
                             *   the clip, and of a placement of duration 0 */
    Curve curve;            /**< The values: time in beats from the start of the clip, value
                             *   in the units of the member */
});

/** Source of an AudioModulator */
ECS_ENUM(AudioModSource, {
    AudioModLfo,           /**< A wave: AudioModulator.wave at rate or sync */
    AudioModSteps,         /**< A sequence of up to 16 values, one per step of sync beats */
    AudioModFollower       /**< The level of a bus, -60..0 dB mapped to 0..1 */
});

/** Wave of an AudioModulator. Its own enum rather than SynthLfoWave, which has no falling saw
 * and whose square and saw smooth their edges in the synth */
ECS_ENUM(AudioModWave, {
    AudioModSine,          /**< Sine, from 0 rising */
    AudioModTriangle,      /**< Triangle, from 0 rising */
    AudioModSquare,        /**< Square, 1 for the first half of a cycle, -1 for the second */
    AudioModSawUp,         /**< Rising saw, -1 to 1 */
    AudioModSawDown,       /**< Falling saw, 1 to -1 */
    AudioModRandom         /**< Sample and hold: a random value per cycle, the same for the
                            *   same cycle of the Transport when synced */
});

/** How the depth of an AudioModulator applies */
ECS_ENUM(AudioModScale, {
    AudioModLinear,        /**< The value plus depth times the source, in the units of the
                            *   member */
    AudioModOctaves        /**< The value times 2^(depth times the source): depth in octaves,
                            *   for a member that is a frequency (cutoff, center, frequency of
                            *   a band, damping of a Delay), whose linear sweep sounds lopsided */
});

/** Modulators the core runs; more log one error that names the modulator and do nothing */
#define BOX_AUDIO_MODULATORS 64

/** Modulators that may move one member; more log one error that names the modulator and do
 * nothing */
#define BOX_AUDIO_MODULATORS_PER_TARGET 4

/** A modulator, on an entity of its own: moves a float member of a bus, an effect or an
 * AudioMacro value on its own, in time with the song, for a wobble, a tremolo or an auto-pan:
 * wobble { AudioModulator: {target: bass_filter, component: AudioFilter, member: "cutoff"} }.
 *
 * Its value adds to the value the output has, clamped to the range of the member, and never
 * writes the component. A positive sync locks it to the beat of the Transport. Up to
 * BOX_AUDIO_MODULATORS_PER_TARGET modulators of one member add up.
 *
 * It fades in and out over 50 ms. One whose target, component or member is unknown logs one
 * error that names it until it changes. See Modulators in docs/audio.md.
 */
ECS_STRUCT(AudioModulator, {
    ecs_entity_t target;    /**< The entity with the AudioBus or the effect component */
    ecs_entity_t component; /**< The component: AudioBus or an effect component */
    char *member;           /**< Path of a float member, such as "cutoff", "volume" or
                             *   "sends[1].level" */
    AudioModSource source;  /**< What moves it, default AudioModLfo */
    float depth;            /**< Swing either way at a source of 1, in the units of the member,
                             *   or in octaves; negative turns it upside down. Default 0 */
    AudioModScale scale;    /**< Linear (default) or in octaves, for a frequency */
    AudioModWave wave;      /**< Wave of an LFO, default AudioModSine */
    float rate;             /**< Hz of an LFO, or steps per second, when sync is 0, 0..100,
                             *   default 1 */
    float sync;             /**< Beats of a cycle of an LFO, or of a step, 0..256, default 0 for
                             *   rate; when positive it wins over rate, like SynthLfo.sync and
                             *   Delay.sync: 0.5 is an eighth, 0.25 a sixteenth */
    float phase;            /**< Phase in turns, 0..1: of a cycle of an LFO, of the whole
                             *   sequence of steps */
    bool unipolar;          /**< An LFO moves 0..1 instead of -1..1, for a tremolo that only
                             *   dips: depth -1 on a volume of 1 */
    float steps[16];        /**< Values of the steps, -1..1 */
    int32_t step_count;     /**< Steps that play, 1..16, default 16 */
    float smooth;           /**< Share of a step the value glides from the step before over, 0
                             *   for hard steps (default) .. 1 */
    ecs_entity_t key;       /**< Bus whose level a follower follows, measured before its
                             *   inserts and volume; 0 for the bus that holds the target effect,
                             *   or the target bus */
    float attack;           /**< Seconds of the rise of a follower, default 0.01 */
    float release;          /**< Seconds of the fall of a follower, default 0.1 */
});

/** Macros the core runs; more log one error that names the macro and do nothing */
#define BOX_AUDIO_MACROS 32

/** Maps of one macro; more log one error that names the macro and do nothing */
#define BOX_AUDIO_MACRO_MAPS 8

/** A macro, on an entity of its own: one value of 0..1 that its AudioMacroMap children turn
 * into several members of buses and effects at once, each over its own range and curve, so game
 * code drives the intensity of the music with one value.
 *
 * While a map runs, the macro replaces the value of the member, and the modulators of the member
 * add to it. The value is a target of an AutomationClip and an AudioModulator (member "value").
 *
 * Up to BOX_AUDIO_MACROS macros run, and up to BOX_AUDIO_MACRO_MAPS maps of each; a macro past
 * a limit logs one error that names it. See Macros in docs/audio.md.
 */
ECS_STRUCT(AudioMacro, {
    float value;            /**< 0..1, default 0, clamped */
});

/** A map of a macro, a child of the AudioMacro entity: one member of a bus or an effect that
 * the macro moves from min at a value of 0 to max at 1, named as an AutomationClip does.
 *
 * The member is min + (max - min) * shape(value), or min * (max / min)^shape(value) with
 * AudioModOctaves, where shape(x) = (2^(6 curve x) - 1) / (2^(6 curve) - 1).
 *
 * A map fades in and out over 50 ms. When two maps move one member the newer one wins; an invalid
 * map logs one error that names it until it changes.
 */
ECS_STRUCT(AudioMacroMap, {
    ecs_entity_t target;    /**< The entity with the AudioBus or the effect component */
    ecs_entity_t component; /**< The component: AudioBus or an effect component */
    char *member;           /**< Path of a float member, such as "cutoff", "volume" or
                             *   "sends[1].level" */
    float min;              /**< Value at a macro of 0, in the units of the member, default 0 */
    float max;              /**< Value at a macro of 1, default 1; below min inverts */
    float curve;            /**< -1 logarithmic .. 0 linear (default) .. 1 exponential */
    AudioModScale scale;    /**< AudioModLinear (default) or AudioModOctaves, for a frequency,
                             *   with min and max above 0 */
});

/** The grid a Track swings */
ECS_ENUM(TrackSwingGrid, {
    TrackSwing8 = 0,        /**< Eighths, a step of 0.5 beat */
    TrackSwing16 = 1        /**< Sixteenths, a step of 0.25 beat */
});

/** How far the notes of a Track move at random from where they are programmed. The jitter is
 * seeded from the track, the placement, the pass of the clip and the note, so a render is
 * deterministic and every pass of a looping clip differs.
 */
ECS_STRUCT(TrackHumanize, {
    float timing;          /**< Milliseconds, 0..50, default 0: the most a note start moves
                            *   either way */
    float velocity;        /**< Share, 0..1, default 0: the most the velocity changes either
                            *   way, relative, clamped to 0..1 */
});

/** A track of the timeline: a mixer channel whose notes play on an instrument. It brings an
 * AudioBus (the With trait), so its notes play on its own bus.
 *
 * Its ClipPlacement children place clips on the timeline. While the Transport plays, every
 * enabled track schedules the notes that start in the lookahead window. See Clips and tracks in
 * docs/audio.md.
 *
 * Swing works on the grid of the timeline from its beat 0, so clips placed anywhere swing alike.
 * Humanize then moves the start by up to timing ms and scales the velocity.
 */
ECS_STRUCT(Track, {
    ecs_entity_t instrument; /**< The entity with Instrument that plays the notes of its note
                              *   clips; a track of only audio clips and automation needs
                              *   none */
    int32_t priority;        /**< Priority of the voices of its notes and audio clips, 0..255,
                              *   default 1: at the voice cap a play steals only a voice of at
                              *   most its priority, so one shots (0) cannot steal them */
    float swing;             /**< 0..0.75, default 0: the off-beat of every pair of steps of
                              *   swing_grid moves later by swing * step, the downbeats stay
                              *   put; notes keep their duration, audio clips do not swing */
    TrackSwingGrid swing_grid; /**< The step of the swing, default TrackSwing8 */
    TrackHumanize humanize;  /**< Random timing and velocity of the notes, default none */
});

/** A clip on the timeline, on a child of a Track: a NoteClip, an AudioClip or an
 * AutomationClip.
 *
 * A note ends at the earliest of its own end, the end of its pass of the clip, the end of the
 * placement and the loop_end of a Transport that loops. A Disabled or deleted placement
 * releases its notes.
 *
 * An AudioClip plays its sound on the bus of the track at the pitch ratio 2^(transpose / 12).
 * An AutomationClip moves its target until the end of the placement, then returns it to the
 * value of its component.
 */
ECS_STRUCT(ClipPlacement, {
    ecs_entity_t clip;     /**< The entity with NoteClip, AudioClip or AutomationClip */
    double start;          /**< Beats on the timeline where the placement starts */
    double duration;       /**< Beats, 0 for the length of the clip minus the offset */
    bool loop;             /**< Repeat the clip every length beats until the duration ends */
    double offset;         /**< Beats into the clip at which the placement starts; with a loop
                            *   the passes go on from there and wrap at length */
    float gain;            /**< Linear gain, default 1, times the gain of the velocity */
    float transpose;       /**< Semitones added to every pitch; of an audio clip the pitch
                            *   ratio 2^(transpose / 12) */
});

/** Where box_audio_play_quantized starts a play on the grid of the Transport */
ECS_ENUM(AudioQuantize, {
    AudioQuantizeNone = 0,  /**< At once, as box_audio_play */
    AudioQuantizeBeat = 1,  /**< On the next beat, a quarter note */
    AudioQuantizeBar = 2    /**< On the next bar of the time signature of the transport */
});

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
