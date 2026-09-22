#include "render_internal.h"

/** Channel of the instance a transitioned component feeds.
 *
 * @param component The component entity
 * @return The channel, BOX_DISPLAY_CHANNELS when the component feeds none
 */
static uint32_t box_render_channel(
    ecs_entity_t component)
{
    uint32_t result = BOX_DISPLAY_CHANNELS;
    if (component == ecs_id(Position)) {
        result = BOX_DISPLAY_POSITION;
    } else if (component == ecs_id(Rotation)) {
        result = BOX_DISPLAY_ROTATION;
    } else if (component == ecs_id(Scale)) {
        result = BOX_DISPLAY_SCALE;
    } else if (component == ecs_id(Rgb)) {
        result = BOX_DISPLAY_RGB;
    } else if (component == ecs_id(ActualTint)) {
        result = BOX_DISPLAY_TINT;
    } else if (component == ecs_id(Opacity)) {
        result = BOX_DISPLAY_OPACITY;
    } else if (component == ecs_id(Emissive)) {
        result = BOX_DISPLAY_EMISSIVE;
    }

    return result;
}

/** Copy the values the author assigned into the channels of a command.
 *
 * @param world The world
 * @param e The entity
 * @param out The command
 */
static void box_render_values(
    ecs_world_t *world,
    ecs_entity_t e,
    box_transition_cmd_t *out)
{
    const Position *p = ecs_get(world, e, Position);
    const Rotation *r = ecs_get(world, e, Rotation);
    const Scale *s = ecs_get(world, e, Scale);
    const Orientation *o = ecs_get(world, e, Orientation);
    const Rgb *rgb = ecs_get(world, e, Rgb);
    const ActualTint *tint = ecs_get(world, e, ActualTint);
    const Opacity *opacity = ecs_get(world, e, Opacity);
    const Emissive *emissive = ecs_get(world, e, Emissive);
    float *value;

    if (p) {
        value = out->values[BOX_DISPLAY_POSITION];
        value[0] = p->x;
        value[1] = p->y;
        value[2] = p->z;
        out->present |= 1u << BOX_DISPLAY_POSITION;
    }

    if (o || r) {
        value = out->values[BOX_DISPLAY_ROTATION];
        value[0] = o ? o->pitch : r->x;
        value[1] = o ? o->yaw : r->y;
        value[2] = o ? o->roll : r->z;
        out->present |= 1u << BOX_DISPLAY_ROTATION;
        out->oriented = o != NULL;
    }

    if (s) {
        value = out->values[BOX_DISPLAY_SCALE];
        value[0] = s->x;
        value[1] = s->y;
        value[2] = s->z;
        out->present |= 1u << BOX_DISPLAY_SCALE;
    }

    if (rgb) {
        value = out->values[BOX_DISPLAY_RGB];
        value[0] = (float)rgb->r;
        value[1] = (float)rgb->g;
        value[2] = (float)rgb->b;
        out->present |= 1u << BOX_DISPLAY_RGB;
    }

    if (tint) {
        value = out->values[BOX_DISPLAY_TINT];
        value[0] = (float)tint->r;
        value[1] = (float)tint->g;
        value[2] = (float)tint->b;
        value[3] = (float)tint->a;
        out->present |= 1u << BOX_DISPLAY_TINT;
    }

    if (opacity) {
        out->values[BOX_DISPLAY_OPACITY][0] = opacity->value;
        out->present |= 1u << BOX_DISPLAY_OPACITY;
    }

    if (emissive) {
        value = out->values[BOX_DISPLAY_EMISSIVE];
        value[0] = emissive->strength;
        value[1] = (float)emissive->color.r;
        value[2] = (float)emissive->color.g;
        value[3] = (float)emissive->color.b;
        out->present |= 1u << BOX_DISPLAY_EMISSIVE;
    }
}

/** Copy the context the renderer composes the instance from into a command.
 *
 * @param world The world
 * @param e The entity
 * @param mesh Its render mesh
 * @param depth Its depth offset
 * @param out The command
 */
static void box_render_context(
    ecs_world_t *world,
    ecs_entity_t e,
    const RenderMesh *mesh,
    float depth,
    box_transition_cmd_t *out)
{
    mat4 parent = mat4_identity();
    ecs_entity_t ancestor = box_hierarchy_parent(world, e);
    const WorldTransform *above = ancestor ?
        ecs_get(world, ancestor, WorldTransform) : NULL;
    if (above) {
        parent = above->m;
    }

    memcpy(out->parent, parent.v, sizeof(out->parent));
    out->mesh_scale[0] = mesh->scale.x;
    out->mesh_scale[1] = mesh->scale.y;
    out->mesh_scale[2] = mesh->scale.z;
    out->bounds_min[0] = mesh->bounds_min.x;
    out->bounds_min[1] = mesh->bounds_min.y;
    out->bounds_min[2] = mesh->bounds_min.z;
    out->bounds_max[0] = mesh->bounds_max.x;
    out->bounds_max[1] = mesh->bounds_max.y;
    out->bounds_max[2] = mesh->bounds_max.z;
    out->depth = depth;
}

/** Collect the records of an entity that are still moving a channel.
 *
 * @param transition The transitions of the entity
 * @param time The time of the frame
 * @param oriented The rotation of the entity comes from an Orientation
 * @param out Receives one record per channel
 * @param mask Receives a bit per channel that has a record
 * @param slow Receives true when a record moves a component without a channel
 * @return Number of records, -1 when two records move one channel
 */
static int32_t box_render_records(
    const TransitionState *transition,
    double time,
    bool oriented,
    box_transition_rec_t *out,
    uint32_t *mask,
    bool *slow)
{
    const box_transition_t *records = ecs_vec_first_t(
        &transition->records, box_transition_t);
    int32_t result = 0;
    int32_t i, count = ecs_vec_count(&transition->records);
    for (i = 0; i < count && result >= 0; i ++) {
        uint32_t channel = box_render_channel(records[i].component);
        double end = records[i].start + (double)records[i].delay +
            (double)records[i].duration;
        if (end <= time) {
            continue;
        }

        if (channel == BOX_DISPLAY_CHANNELS) {
            slow[0] = true;
            continue;
        }

        if (channel == BOX_DISPLAY_ROTATION && oriented) {
            continue;
        }

        if (*mask & (1u << channel)) {
            result = -1;
            continue;
        }

        box_transition_rec_t *rec = &out[result];

        memset(rec, 0, sizeof(box_transition_rec_t));
        rec->start = records[i].start;
        rec->duration = records[i].duration;
        rec->delay = records[i].delay;
        rec->timing = (uint32_t)records[i].timing;
        rec->channel = channel;
        rec->first = records[i].index;
        rec->count = records[i].count;
        memcpy(rec->from, records[i].from, sizeof(rec->from));
        memcpy(rec->to, records[i].to, sizeof(rec->to));
        *mask |= 1u << channel;
        result ++;
    }

    return result;
}

uint32_t box_render_extract_transition(
    ecs_world_t *world,
    ecs_entity_t e,
    const RenderMesh *mesh,
    uint32_t slot,
    const TransitionState *transition,
    double time,
    float depth,
    box_packet_t *p)
{
    uint32_t result = BOX_UPDATE_STILL;
    box_transition_cmd_t cmd;
    box_transition_rec_t records[BOX_DISPLAY_CHANNELS];
    uint32_t mask = 0;

    memset(&cmd, 0, sizeof(cmd));
    box_render_values(world, e, &cmd);

    bool slow = false;
    int32_t count = box_render_records(transition, time, cmd.oriented != 0, records,
        &mask, &slow);
    if (count < 0) {
        result = BOX_UPDATE_SLOW;
        goto done;
    }

    if (!count) {
        result = slow ? BOX_UPDATE_SLOW : result;
        goto done;
    }


    uint32_t pose = (1u << BOX_DISPLAY_POSITION) | (1u << BOX_DISPLAY_ROTATION) |
        (1u << BOX_DISPLAY_SCALE);
    cmd.posed = (mask & pose) && !ecs_has_id(world, e, ManualTransform);
    if (!cmd.posed && !(mask & ~pose)) {
        goto done;
    }

    box_render_context(world, e, mesh, depth, &cmd);
    cmd.mesh = mesh->handle;
    cmd.slot = slot;
    cmd.first = p->transition_records.count;
    cmd.count = (uint32_t)count;
    cmd.until = 0;

    int32_t i;
    for (i = 0; i < count; i ++) {
        double end = records[i].start + (double)records[i].delay +
            (double)records[i].duration;

        cmd.until = end > cmd.until ? end : cmd.until;
        *(box_transition_rec_t*)box_vec_push(&p->transition_records) = records[i];
    }

    result = p->transitions.count + 1;
    *(box_transition_cmd_t*)box_vec_push(&p->transitions) = cmd;
done:
    return result;
}

void box_render_extract_finished(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p)
{
    uint32_t i;
    for (i = 0; i < p->transitions_done.count; i ++) {
        box_transition_done_t *done = box_vec_at(&p->transitions_done, i);
        if ((int32_t)done->mesh >= ecs_vec_count(&state->batches)) {
            continue;
        }

        box_batch_t *batch = ecs_vec_get_t(&state->batches, box_batch_t, (int32_t)done->mesh);
        if ((int32_t)done->slot >= ecs_vec_count(&batch->entities)) {
            continue;
        }

        ecs_entity_t e = ecs_vec_get_t(&batch->entities, ecs_entity_t, (int32_t)done->slot)[0];
        TransitionState *transition = ecs_is_alive(world, e) ?
            ecs_get_mut(world, e, TransitionState) : NULL;
        if (transition) {
            transition->dirty = false;
        }
    }

    box_vec_clear(&p->transitions_done);
}
