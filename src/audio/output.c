/**
 * @file audio/output.c
 * @brief The audio output of a world, which box_app_run opens and every frame updates.
 */

#include "output.h"

/** The output of a world, a singleton that owns its transport */
typedef struct AudioOutput {
    box_audio_transport_t *transport;  /**< The transport */
} AudioOutput;

static ECS_COMPONENT_DECLARE(AudioOutput);

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
    AudioOutput *outputs = ptr;
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
    AudioOutput *dst = dst_ptr, *src = src_ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        box_audio_transport_free(dst[i].transport);
        dst[i].transport = src[i].transport;
        src[i].transport = NULL;
    }
}

/** Update the transport once per frame with the delta of the frame.
 *
 * @param it The iterator, matched only while the world has an output
 */
static void AudioOutputUpdate(
    ecs_iter_t *it)
{
    AudioOutput *output = ecs_field(it, AudioOutput, 0);
    if (output->transport) {
        output->transport->update(output->transport, it->delta_time);
    }
}

void box_audio_output_init(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, AudioOutput);
    ecs_set_hooks(world, AudioOutput, {
        .ctor = flecs_default_ctor,
        .dtor = box_audio_output_dtor,
        .move = box_audio_output_move
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "AudioOutputUpdate" }),
        .phase = EcsOnStore,
        .query.terms = {{ .id = ecs_id(AudioOutput), .src.id = ecs_id(AudioOutput) }},
        .callback = AudioOutputUpdate
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
        transport = box_audio_transport_null_new(NULL);
    }
#endif

    if (transport) {
        ecs_singleton_ensure(world, AudioOutput)->transport = transport;
        ecs_singleton_modified(world, AudioOutput);
    }

done:
    return;
}

void box_audio_output_close(
    ecs_world_t *world)
{
    if (ecs_id(AudioOutput) && ecs_has_id(world, ecs_id(AudioOutput), ecs_id(AudioOutput))) {
        ecs_singleton_remove(world, AudioOutput);
    }
}

box_audio_transport_t* box_audio_output_transport(
    const ecs_world_t *world)
{
    const AudioOutput *output = ecs_id(AudioOutput) ?
        ecs_singleton_get(world, AudioOutput) : NULL;

    return output ? output->transport : NULL;
}

box_audio_state_t box_audio_output_state(
    const ecs_world_t *world)
{
    box_audio_transport_t *transport = box_audio_output_transport(world);

    return transport ? transport->state(transport) : BOX_AUDIO_STATE_OFF;
}
