/**
 * @file command/cmd_audio_audition.c
 * @brief The audition actions of the audio command: solo and mute a bus, bypass an effect and
 * play one note of a sound or an instrument.
 *
 * Solo and mute write AudioBus and bypass adds or removes Disabled, as a script or the set
 * command would, so a reload of the song resets them.
 *
 * A note plays at once on the live output through box_audio_play_note, without touching the
 * transport or the song. A sound plays through an instrument of its own in
 * box.command.audition, at the default root of A4.
 */

#include "command.h"
#include "../audio/module.h"
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

/** Path of the scope of the instruments that play the notes of sounds */
#define BOX_CMD_AUDITION_SCOPE "box.command.audition"

/** Tempo a note duration converts at without a Transport, as box_audio_play_note */
#define BOX_CMD_AUDITION_TEMPO 120.0

/** Lowest and highest tempo a note duration converts at, as box_audio_play_note */
#define BOX_CMD_AUDITION_TEMPO_MIN 20.0
#define BOX_CMD_AUDITION_TEMPO_MAX 999.0

/** Default seconds of a note */
#define BOX_CMD_AUDITION_DUR 0.5

/** Most seconds of a note */
#define BOX_CMD_AUDITION_DUR_MAX 60.0

/** Parse on, off or nothing.
 *
 * @param value on or off, NULL or empty to toggle
 * @param current The current state, which a toggle inverts
 * @param out Receives the new state
 * @return Whether the value is valid; logs an error when not
 */
static bool box_cmd_audition_switch(
    const char *value,
    bool current,
    bool *out)
{
    bool result = true;
    if (!value || !value[0]) {
        *out = !current;
    } else if (!ecs_os_strcmp(value, "on")) {
        *out = true;
    } else if (!ecs_os_strcmp(value, "off")) {
        *out = false;
    } else {
        ecs_err("invalid state '%s', expected on or off", value);
        result = false;
    }

    return result;
}

/** Look up an entity by its path relative to the scope of the session.
 *
 * @param world The world
 * @param name The path
 * @return The entity, 0 when there is none
 */
static ecs_entity_t box_cmd_audition_lookup(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t cwd = box_command_scope(world);
    ecs_entity_t result = name && name[0] ?
        ecs_lookup_path_w_sep(world, cwd, name, ".", NULL, true) : 0;
    if (!result && name && !ecs_os_strcmp(name, "master")) {
        result = ecs_lookup(world, "box.audio.master");
    }

    return result;
}

/** Look up a bus.
 *
 * @param world The world
 * @param name The path of the bus, master for the master
 * @return The bus, 0 with an error logged when the name is no bus
 */
static ecs_entity_t box_cmd_audition_bus(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t result = box_cmd_audition_lookup(world, name);
    if (!name || !name[0]) {
        ecs_err("missing bus");
    } else if (!result) {
        ecs_err("unknown bus '%s'", name);
    } else if (!ecs_has(world, result, AudioBus)) {
        ecs_err("'%s' is not a bus", name);
        result = 0;
    }

    return result;
}

/** Clear the solo of every bus.
 *
 * @param world The world
 * @return The text, which the caller frees
 */
static char* box_cmd_audition_solo_none(
    ecs_world_t *world)
{
    ecs_entity_t buses[BOX_AUDIO_BUS_MAX];
    int32_t i, count = 0;
    ecs_iter_t it = ecs_each_id(world, ecs_id(AudioBus));
    while (ecs_each_next(&it)) {
        const AudioBus *bus = ecs_field(&it, AudioBus, 0);
        for (i = 0; i < it.count; i ++) {
            if (bus[i].solo && count < BOX_AUDIO_BUS_MAX) {
                buses[count ++] = it.entities[i];
            }
        }
    }

    for (i = 0; i < count; i ++) {
        ecs_ensure(world, buses[i], AudioBus)->solo = false;
        ecs_modified(world, buses[i], AudioBus);
    }

    return flecs_asprintf("solo none, %d cleared", count);
}

char* box_cmd_audio_solo(
    ecs_world_t *world,
    const char *name,
    const char *value)
{
    char *result = NULL, *path = NULL;
    ecs_entity_t bus;
    AudioBus *b;
    bool solo;
    if (name && !ecs_os_strcmp(name, "none") && !box_cmd_audition_lookup(world, name)) {
        result = box_cmd_audition_solo_none(world);
        goto done;
    }

    bus = box_cmd_audition_bus(world, name);
    if (!bus) {
        goto done;
    }

    if (bus == ecs_lookup(world, "box.audio.master")) {
        ecs_err("the master cannot be soloed");
        goto done;
    }

    if (!box_cmd_audition_switch(value, ecs_get(world, bus, AudioBus)->solo, &solo)) {
        goto done;
    }

    b = ecs_ensure(world, bus, AudioBus);
    b->solo = solo;
    ecs_modified(world, bus, AudioBus);
    path = ecs_get_path(world, bus);
    result = flecs_asprintf("solo %s %s", path, solo ? "on" : "off");
done:
    ecs_os_free(path);

    return result;
}

char* box_cmd_audio_bus_mute(
    ecs_world_t *world,
    const char *name,
    const char *value)
{
    char *result = NULL, *path = NULL;
    ecs_entity_t bus = box_cmd_audition_bus(world, name);
    AudioBus *b;
    bool mute;
    if (!bus) {
        goto done;
    }

    if (!box_cmd_audition_switch(value, ecs_get(world, bus, AudioBus)->mute, &mute)) {
        goto done;
    }

    b = ecs_ensure(world, bus, AudioBus);
    b->mute = mute;
    ecs_modified(world, bus, AudioBus);
    path = ecs_get_path(world, bus);
    result = flecs_asprintf("mute %s %s", path, mute ? "on" : "off");
done:
    ecs_os_free(path);

    return result;
}

char* box_cmd_audio_bypass(
    ecs_world_t *world,
    const char *name,
    const char *value)
{
    char *result = NULL, *path = NULL;
    ecs_entity_t e = box_cmd_audition_lookup(world, name);
    bool bypass;
    if (!name || !name[0]) {
        ecs_err("missing effect");
        goto done;
    }

    if (!e) {
        ecs_err("unknown effect '%s'", name);
        goto done;
    }

    if (box_audio_module_effect_kind(world, e, NULL) == BOX_AUDIO_EFFECT_NONE) {
        ecs_err("'%s' is not an effect", name);
        goto done;
    }

    if (!box_cmd_audition_switch(value, ecs_has_id(world, e, EcsDisabled), &bypass)) {
        goto done;
    }

    if (bypass) {
        ecs_add_id(world, e, EcsDisabled);
    } else {
        ecs_remove_id(world, e, EcsDisabled);
    }

    path = ecs_get_path(world, e);
    result = flecs_asprintf("bypass %s %s", path, bypass ? "on" : "off");
done:
    ecs_os_free(path);

    return result;
}

/** Semitones from C of the note letters A to G */
static const int32_t box_cmd_audition_steps[7] = { 9, 11, 0, 2, 4, 5, 7 };

bool box_cmd_audio_note_parse(
    const char *name,
    float *pitch)
{
    const char *p = name;
    char *end = NULL;
    bool result = false;
    double midi;
    long octave;
    int32_t step;
    if (!name || !name[0]) {
        goto done;
    }

    if (isdigit((unsigned char)p[0])) {
        midi = strtod(p, &end);
        if (end == p || end[0]) {
            goto done;
        }
    } else {
        int32_t letter = toupper((unsigned char)p[0]);
        if (letter < 'A' || letter > 'G') {
            goto done;
        }

        step = box_cmd_audition_steps[letter - 'A'];
        p ++;
        while (p[0] == '#' || p[0] == 'b') {
            step += p[0] == '#' ? 1 : -1;
            p ++;
        }

        if (!isdigit((unsigned char)p[0]) && !(p[0] == '-' &&
            isdigit((unsigned char)p[1])))
        {
            goto done;
        }

        octave = strtol(p, &end, 10);
        if (end == p || end[0] || octave < -1 || octave > 9) {
            goto done;
        }

        midi = (double)((octave + 1) * 12 + step);
    }

    if (!(midi >= 0) || midi > 127) {
        goto done;
    }

    *pitch = (float)midi;
    result = true;
done:
    return result;
}

/** Get the instrument that plays a sound or an instrument: the instrument itself, else an
 * instrument of the sound in BOX_CMD_AUDITION_SCOPE at the default root of A4.
 *
 * @param world The world
 * @param e The sound or the instrument
 * @return The instrument, 0 when the entity is neither
 */
static ecs_entity_t box_cmd_audition_instrument(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = 0, scope;
    char name[32];
    Instrument *inst;
    if (ecs_has(world, e, Instrument)) {
        result = e;
        goto done;
    }

    if (!ecs_has(world, e, AudioPcm) && !ecs_has(world, e, SoundFile) &&
        !ecs_has(world, e, SynthSound))
    {
        goto done;
    }

    scope = ecs_entity(world, { .name = BOX_CMD_AUDITION_SCOPE });
    ecs_os_snprintf(name, sizeof(name), "sound_%u", (uint32_t)e);
    result = ecs_entity(world, { .name = name, .parent = scope });
    inst = ecs_ensure(world, result, Instrument);
    if (inst->sound != e) {
        inst->sound = e;
        ecs_modified(world, result, Instrument);
    }
done:
    return result;
}

char* box_cmd_audio_note(
    ecs_world_t *world,
    const char *name,
    const char *note,
    const char *vel,
    const char *dur)
{
    const Transport *tr = ecs_singleton_get(world, Transport);
    ecs_entity_t e = box_cmd_audition_lookup(world, name), inst;
    double velocity = 1.0, seconds = BOX_CMD_AUDITION_DUR, tempo;
    char *result = NULL, *path = NULL, *end = NULL;
    float pitch = 0;
    if (!name || !name[0]) {
        ecs_err("missing sound or instrument");
        goto done;
    }

    if (!e) {
        ecs_err("unknown sound or instrument '%s'", name);
        goto done;
    }

    inst = box_cmd_audition_instrument(world, e);
    if (!inst) {
        ecs_err("'%s' is not a sound or an instrument", name);
        goto done;
    }

    if (!box_cmd_audio_note_parse(note, &pitch)) {
        ecs_err("invalid note '%s', expected a name such as C4 or F#3, or a MIDI number 0 to "
            "127", note ? note : "");
        goto done;
    }

    if (vel && vel[0]) {
        velocity = strtod(vel, &end);
        if (!end || end[0] || !(velocity >= 0) || velocity > 1) {
            ecs_err("invalid velocity '%s', expected 0 to 1", vel);
            goto done;
        }
    }

    if (dur && dur[0]) {
        seconds = strtod(dur, &end);
        if (!end || end[0] || !(seconds > 0) || seconds > BOX_CMD_AUDITION_DUR_MAX) {
            ecs_err("invalid duration '%s', expected more than 0 and at most %.0f seconds",
                dur, BOX_CMD_AUDITION_DUR_MAX);
            goto done;
        }
    }

    tempo = tr ? (double)tr->tempo : BOX_CMD_AUDITION_TEMPO;
    tempo = tempo >= BOX_CMD_AUDITION_TEMPO_MIN ? (tempo < BOX_CMD_AUDITION_TEMPO_MAX ? tempo :
        BOX_CMD_AUDITION_TEMPO_MAX) : BOX_CMD_AUDITION_TEMPO_MIN;
    if (!box_audio_play_note(world, inst, pitch, (float)velocity,
        (float)(seconds * tempo / 60.0)))
    {
        ecs_err("'%s' has no sound that plays", name);
        goto done;
    }

    path = ecs_get_path(world, e);
    result = flecs_asprintf("note %s %.2f (%.2f Hz) velocity %.2f for %.3f s", path,
        (double)pitch, 440.0 * pow(2.0, ((double)pitch - 69.0) / 12.0), velocity, seconds);
done:
    ecs_os_free(path);

    return result;
}
