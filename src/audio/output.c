/**
 * @file audio/output.c
 * @brief The audio output of a world, which box_app_run opens and every frame updates.
 */

#include "output.h"

/** The output of a world, a singleton that owns its transport */
typedef struct AudioTransport {
    box_audio_transport_t *transport;  /**< The transport */
    uint32_t serial;                   /**< Changes with every open */
} AudioTransport;

/** Serial of the last output opened in the process */
static uint32_t box_audio_output_serial_last;

static ECS_COMPONENT_DECLARE(AudioTransport);

/** Free the transport of an output.
 *
 * @param ptr The outputs
 * @param count Number of outputs
 * @param ti Type info
 */
static void box_audio_output_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    AudioTransport *outputs = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        box_audio_transport_free(outputs[i].transport);
        outputs[i].transport = NULL;
    }
}

/** Move the transport of an output, which has one owner.
 *
 * @param dst_ptr The outputs moved to
 * @param src_ptr The outputs moved from
 * @param count Number of outputs
 * @param ti Type info
 */
static void box_audio_output_move(
    void *dst_ptr,
    void *src_ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    AudioTransport *dst = dst_ptr, *src = src_ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        box_audio_transport_free(dst[i].transport);
        dst[i].transport = src[i].transport;
        dst[i].serial = src[i].serial;
        src[i].transport = NULL;
    }
}

/** Update the transport once per frame with the delta of the frame.
 *
 * @param it The iterator, matched only while the world has an output
 */
static void AudioTransportUpdate(
    ecs_iter_t *it)
{
    AudioTransport *output = ecs_field(it, AudioTransport, 0);
    if (output->transport) {
        output->transport->update(output->transport, it->delta_time);
    }
}

void box_audio_output_init(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, AudioTransport);
    ecs_set_hooks(world, AudioTransport, {
        .ctor = flecs_default_ctor,
        .dtor = box_audio_output_dtor,
        .move = box_audio_output_move
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "AudioTransportUpdate" }),
        .phase = EcsOnStore,
        .query.terms = {{ .id = ecs_id(AudioTransport), .src.id = ecs_id(AudioTransport) }},
        .callback = AudioTransportUpdate
    });
}

void box_audio_output_open(
    ecs_world_t *world,
    const box_audio_output_desc_t *desc)
{
    box_audio_transport_t *transport = NULL;
    box_audio_output_close(world);
    if (desc->disabled) {
        goto done;
    }

#ifdef __EMSCRIPTEN__
    transport = box_audio_transport_web_new();
#else
    if (!desc->headless) {
        transport = box_audio_transport_native_new(&(box_audio_native_desc_t){
            .backend = desc->backend });
    }

    if (!transport) {
        transport = box_audio_transport_null_new(&(box_audio_null_desc_t){
            .output = desc->output, .ctx = desc->ctx });
    }
#endif

    box_audio_output_use(world, transport);
done:
    return;
}

void box_audio_output_use(
    ecs_world_t *world,
    box_audio_transport_t *transport)
{
    box_audio_output_close(world);
    if (transport) {
        AudioTransport *output = ecs_singleton_ensure(world, AudioTransport);
        output->transport = transport;
        output->serial = ++ box_audio_output_serial_last;
        ecs_singleton_modified(world, AudioTransport);
    }
}

void box_audio_output_close(
    ecs_world_t *world)
{
    if (ecs_id(AudioTransport) &&
        ecs_has_id(world, ecs_id(AudioTransport), ecs_id(AudioTransport)))
    {
        ecs_singleton_remove(world, AudioTransport);
    }
}

box_audio_transport_t* box_audio_output_transport(
    const ecs_world_t *world)
{
    const AudioTransport *output = ecs_id(AudioTransport) ?
        ecs_singleton_get(world, AudioTransport) : NULL;

    return output ? output->transport : NULL;
}

uint32_t box_audio_output_serial(
    const ecs_world_t *world)
{
    const AudioTransport *output = ecs_id(AudioTransport) ?
        ecs_singleton_get(world, AudioTransport) : NULL;

    return output && output->transport ? output->serial : 0;
}

box_audio_state_t box_audio_output_state(
    const ecs_world_t *world)
{
    box_audio_transport_t *transport = box_audio_output_transport(world);

    return transport ? transport->state(transport) : BOX_AUDIO_STATE_OFF;
}
