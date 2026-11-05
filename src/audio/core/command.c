/**
 * @file audio/core/command.c
 * @brief Writes commands of the audio core into a byte buffer, as a transport sends them.
 */

#include "core.h"

void box_audio_writer_init(
    box_audio_writer_t *writer,
    void *data,
    int32_t capacity)
{
    writer->data = data;
    writer->size = 0;
    writer->capacity = data && capacity > 0 ? capacity : 0;
}

bool box_audio_write_bytes(
    box_audio_writer_t *writer,
    const void *data,
    int32_t size)
{
    const uint8_t *bytes = data;
    bool result = size >= 0 && writer->size + size <= writer->capacity;
    int32_t i;
    if (result) {
        for (i = 0; i < size; i ++) {
            writer->data[writer->size + i] = bytes[i];
        }

        writer->size += size;
    }

    return result;
}

/** Fill the header of a command.
 *
 * @param header The header
 * @param type A box_audio_cmd_type_t
 * @param size Bytes of the command
 */
static void box_audio_write_header(
    box_audio_cmd_header_t *header,
    box_audio_cmd_type_t type,
    size_t size)
{
    header->type = (uint16_t)type;
    header->size = (uint16_t)size;
}

bool box_audio_write_play(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pan,
    float pitch,
    uint32_t flags,
    uint64_t when)
{
    return box_audio_write_play_priority(writer, voice, sound, bus, volume, pan, pitch, 1.0f,
        flags, 0, when);
}

bool box_audio_write_play_priority(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pan,
    float pitch,
    float velocity,
    uint32_t flags,
    uint32_t priority,
    uint64_t when)
{
    return box_audio_write_play_note(writer, voice, sound, bus, volume, pan, pitch, velocity,
        0.0f, 0.0f, flags, priority, when);
}

bool box_audio_write_play_note(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pan,
    float pitch,
    float velocity,
    float beat,
    float tempo,
    uint32_t flags,
    uint32_t priority,
    uint64_t when)
{
    box_audio_cmd_play_t cmd;
    box_audio_write_header(&cmd.header, BOX_AUDIO_CMD_PLAY, sizeof(cmd));
    cmd.when_lo = (uint32_t)when;
    cmd.when_hi = (uint32_t)(when >> 32);
    cmd.voice = voice;
    cmd.sound = sound;
    cmd.bus = bus;
    cmd.volume = volume;
    cmd.pan = pan;
    cmd.pitch = pitch;
    cmd.flags = flags;
    cmd.priority = priority;
    cmd.velocity = velocity;
    cmd.beat = beat;
    cmd.tempo = tempo;

    return box_audio_write_bytes(writer, &cmd, (int32_t)sizeof(cmd));
}

bool box_audio_write_legato(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint32_t from,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pan,
    float pitch,
    uint32_t flags)
{
    return box_audio_write_legato_at(writer, voice, from, sound, bus, volume, pan, pitch, flags,
        0);
}

bool box_audio_write_legato_at(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint32_t from,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pan,
    float pitch,
    uint32_t flags,
    uint64_t when)
{
    return box_audio_write_legato_priority(writer, voice, from, sound, bus, volume, pan, pitch,
        1.0f, flags, 0, when);
}

bool box_audio_write_legato_priority(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint32_t from,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pan,
    float pitch,
    float velocity,
    uint32_t flags,
    uint32_t priority,
    uint64_t when)
{
    return box_audio_write_legato_note(writer, voice, from, sound, bus, volume, pan, pitch,
        velocity, 0.0f, 0.0f, flags, priority, when);
}

bool box_audio_write_legato_note(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint32_t from,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pan,
    float pitch,
    float velocity,
    float beat,
    float tempo,
    uint32_t flags,
    uint32_t priority,
    uint64_t when)
{
    box_audio_cmd_legato_t cmd;
    box_audio_write_header(&cmd.header, BOX_AUDIO_CMD_LEGATO, sizeof(cmd));
    cmd.when_lo = (uint32_t)when;
    cmd.when_hi = (uint32_t)(when >> 32);
    cmd.voice = voice;
    cmd.from = from;
    cmd.sound = sound;
    cmd.bus = bus;
    cmd.volume = volume;
    cmd.pan = pan;
    cmd.pitch = pitch;
    cmd.flags = flags;
    cmd.priority = priority;
    cmd.velocity = velocity;
    cmd.beat = beat;
    cmd.tempo = tempo;

    return box_audio_write_bytes(writer, &cmd, (int32_t)sizeof(cmd));
}

bool box_audio_write_stop(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint64_t when)
{
    box_audio_cmd_stop_t cmd;
    box_audio_write_header(&cmd.header, BOX_AUDIO_CMD_STOP, sizeof(cmd));
    cmd.when_lo = (uint32_t)when;
    cmd.when_hi = (uint32_t)(when >> 32);
    cmd.voice = voice;

    return box_audio_write_bytes(writer, &cmd, (int32_t)sizeof(cmd));
}

bool box_audio_write_voice_release(
    box_audio_writer_t *writer,
    uint32_t voice,
    float fade,
    uint64_t when)
{
    box_audio_cmd_voice_release_t cmd;
    box_audio_write_header(&cmd.header, BOX_AUDIO_CMD_RELEASE, sizeof(cmd));
    cmd.when_lo = (uint32_t)when;
    cmd.when_hi = (uint32_t)(when >> 32);
    cmd.voice = voice;
    cmd.fade = fade;

    return box_audio_write_bytes(writer, &cmd, (int32_t)sizeof(cmd));
}

/** Write a set command of a voice or a bus.
 *
 * @param writer The writer
 * @param type BOX_AUDIO_CMD_VOICE_SET or BOX_AUDIO_CMD_BUS_SET
 * @param target Handle of the voice or index of the bus
 * @param param A box_audio_param_t
 * @param value The value
 * @param when Frame of the core clock it applies at, 0 for now
 * @return Whether the command fit
 */
static bool box_audio_write_set(
    box_audio_writer_t *writer,
    box_audio_cmd_type_t type,
    uint32_t target,
    box_audio_param_t param,
    float value,
    uint64_t when)
{
    box_audio_cmd_set_t cmd;
    box_audio_write_header(&cmd.header, type, sizeof(cmd));
    cmd.when_lo = (uint32_t)when;
    cmd.when_hi = (uint32_t)(when >> 32);
    cmd.target = target;
    cmd.param = (uint32_t)param;
    cmd.value = value;

    return box_audio_write_bytes(writer, &cmd, (int32_t)sizeof(cmd));
}

bool box_audio_write_voice_set(
    box_audio_writer_t *writer,
    uint32_t voice,
    box_audio_param_t param,
    float value,
    uint64_t when)
{
    return box_audio_write_set(writer, BOX_AUDIO_CMD_VOICE_SET, voice, param, value, when);
}

bool box_audio_write_bus_set(
    box_audio_writer_t *writer,
    uint32_t bus,
    box_audio_param_t param,
    float value,
    uint64_t when)
{
    return box_audio_write_set(writer, BOX_AUDIO_CMD_BUS_SET, bus, param, value, when);
}

bool box_audio_write_order(
    box_audio_writer_t *writer,
    const box_audio_route_t *routes,
    int32_t count)
{
    box_audio_cmd_order_t cmd;
    int32_t bytes = count * (int32_t)sizeof(box_audio_route_t);
    bool result = false;
    if (count < 0 || count > BOX_AUDIO_BUS_MAX || (count && !routes) ||
        writer->size + (int32_t)sizeof(cmd) + bytes > writer->capacity)
    {
        goto done;
    }

    box_audio_write_header(&cmd.header, BOX_AUDIO_CMD_ORDER, sizeof(cmd) + (size_t)bytes);
    cmd.count = (uint32_t)count;
    result = box_audio_write_bytes(writer, &cmd, (int32_t)sizeof(cmd)) &&
        box_audio_write_bytes(writer, routes, bytes);

done:
    return result;
}

bool box_audio_write_stream(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint32_t stream,
    uint32_t bus,
    float volume,
    uint32_t channels,
    float sample_rate,
    uint32_t priority)
{
    box_audio_cmd_stream_t cmd;
    box_audio_write_header(&cmd.header, BOX_AUDIO_CMD_STREAM, sizeof(cmd));
    cmd.voice = voice;
    cmd.stream = stream;
    cmd.bus = bus;
    cmd.volume = volume;
    cmd.channels = channels;
    cmd.sample_rate = sample_rate;
    cmd.priority = priority;

    return box_audio_write_bytes(writer, &cmd, (int32_t)sizeof(cmd));
}

bool box_audio_write_release(
    box_audio_writer_t *writer,
    uint32_t sound)
{
    box_audio_cmd_release_t cmd;
    box_audio_write_header(&cmd.header, BOX_AUDIO_CMD_SOUND_RELEASE, sizeof(cmd));
    cmd.sound = sound;

    return box_audio_write_bytes(writer, &cmd, (int32_t)sizeof(cmd));
}

bool box_audio_write_effect(
    box_audio_writer_t *writer,
    uint32_t slot,
    uint32_t kind,
    uint32_t bus,
    uint32_t index,
    uint32_t key,
    const float *params)
{
    box_audio_cmd_effect_t cmd;
    int32_t i;
    box_audio_write_header(&cmd.header, BOX_AUDIO_CMD_EFFECT, sizeof(cmd));
    cmd.slot = slot;
    cmd.kind = kind;
    cmd.bus = bus;
    cmd.index = index;
    cmd.key = key;
    for (i = 0; i < BOX_AUDIO_EFFECT_PARAMS; i ++) {
        cmd.params[i] = params ? params[i] : 0.0f;
    }

    return box_audio_write_bytes(writer, &cmd, (int32_t)sizeof(cmd));
}

bool box_audio_write_effect_params(
    box_audio_writer_t *writer,
    uint32_t slot,
    uint32_t kind,
    const float *params,
    uint64_t when)
{
    box_audio_cmd_effect_params_t cmd;
    int32_t i;
    box_audio_write_header(&cmd.header, BOX_AUDIO_CMD_EFFECT_PARAMS, sizeof(cmd));
    cmd.when_lo = (uint32_t)when;
    cmd.when_hi = (uint32_t)(when >> 32);
    cmd.slot = slot;
    cmd.kind = kind;
    for (i = 0; i < BOX_AUDIO_EFFECT_PARAMS; i ++) {
        cmd.params[i] = params ? params[i] : 0.0f;
    }

    return box_audio_write_bytes(writer, &cmd, (int32_t)sizeof(cmd));
}

bool box_audio_write_voice_ramp(
    box_audio_writer_t *writer,
    uint32_t voice,
    box_audio_param_t param,
    float value,
    uint32_t frames,
    uint64_t when)
{
    box_audio_cmd_ramp_t cmd;
    box_audio_write_header(&cmd.header, BOX_AUDIO_CMD_VOICE_RAMP, sizeof(cmd));
    cmd.when_lo = (uint32_t)when;
    cmd.when_hi = (uint32_t)(when >> 32);
    cmd.voice = voice;
    cmd.param = (uint32_t)param;
    cmd.value = value;
    cmd.frames = frames;

    return box_audio_write_bytes(writer, &cmd, (int32_t)sizeof(cmd));
}

bool box_audio_write_cancel(
    box_audio_writer_t *writer,
    uint32_t type,
    uint32_t target,
    uint32_t param)
{
    box_audio_cmd_cancel_t cmd;
    box_audio_write_header(&cmd.header, BOX_AUDIO_CMD_CANCEL, sizeof(cmd));
    cmd.type = type;
    cmd.target = target;
    cmd.param = param;

    return box_audio_write_bytes(writer, &cmd, (int32_t)sizeof(cmd));
}

bool box_audio_write_synth(
    box_audio_writer_t *writer,
    uint32_t sound,
    const box_audio_synth_patch_t *patch)
{
    box_audio_cmd_synth_t cmd;
    uint32_t layers = patch->layers < BOX_AUDIO_SYNTH_LAYERS ? patch->layers :
        BOX_AUDIO_SYNTH_LAYERS;
    int32_t size = BOX_AUDIO_CMD_SYNTH_SIZE(layers);
    box_audio_write_header(&cmd.header, BOX_AUDIO_CMD_SYNTH, (size_t)size);
    cmd.sound = sound;
    cmd.patch = *patch;

    return box_audio_write_bytes(writer, &cmd, size);
}
