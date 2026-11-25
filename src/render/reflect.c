/**
 * @file render/reflect.c
 * @brief Reflection probe registry of every scene with ReflectionBake, its probe buffers and
 * the captures that fill its cubemap array.
 *
 * Each probe of a scene keeps its cubemap slot while it is resident; the resident probes are
 * uploaded to the reflection probe buffer of the scene in blend order when they change. Every
 * frame the uncaptured resident probes are captured as budget_ms allows, the scene probe first.
 *
 * A probe is queued for a new capture when what it shows changes: a static shape recorded by
 * bake_change.c reaches one of its boxes, a light changes in its range, the light bake shows a
 * new pass, or the probe moves. It shows its previous capture until the new one lands.
 */

#include "reflect.h"
#include "environment.h"
#include "../spatial/spatial.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** Singleton with the system of the probe extract, so its state is found without a lookup */
typedef struct RenderReflectSystem {
    ecs_entity_t system;        /**< The system, whose context is the state */
} RenderReflectSystem;

static ECS_COMPONENT_DECLARE(RenderReflectSystem);

/** Get the system of the reflection probe extract of a world.
 *
 * @param world The world
 * @return The system, NULL before box.render was imported or after the system was deleted
 */
static const ecs_system_t* box_reflect_system_ctx(
    const ecs_world_t *world)
{
    ecs_entity_t id = ecs_id(RenderReflectSystem);
    const RenderReflectSystem *c = id && ecs_is_alive(world, id)
        ? ecs_get_id(world, id, id) : NULL;

    return c && c->system && ecs_is_alive(world, c->system)
        ? ecs_system_get(world, c->system) : NULL;
}

/** Face edge of a probe cubemap in texels at least */
#define BOX_REFLECT_RESOLUTION_MIN (8)

/** Face edge of a probe cubemap in texels at most */
#define BOX_REFLECT_RESOLUTION_MAX (512)

/** Error of a ReflectionBake whose resolution is out of range */
#define BOX_REFLECT_RESOLUTION_ERROR "resolution must be 8 to 512"

/** Half size of the influence box of the scene probe, beyond any scene */
#define BOX_REFLECT_SCENE_EXTENT (1e6f)

/** Probes a reflection probe buffer holds at least */
#define BOX_REFLECT_MIN_CAPACITY (4)

/** Cubemap faces of a probe, each a layer of the cubemap array */
#define BOX_REFLECT_FACES (6)

/** GPU milliseconds per probe capture assumed until the backend measured one */
#define BOX_REFLECT_PROBE_MS (1.0f)

/** Weight of a new measurement in the GPU time per probe capture */
#define BOX_REFLECT_SMOOTHING (0.5f)

/** Probes captured per scene and frame at most outside a headless run */
#define BOX_REFLECT_FRAME_MAX (64)

/** A probe ranked for a slot or for the blend order */
typedef struct box_reflect_rank_t {
    int32_t index;          /**< Index of the probe in the probes of its scene */
    int32_t priority;       /**< ReflectionProbe.priority */
    bool scene;             /**< The probe is the scene probe */
    float distance;         /**< Squared distance to the nearest camera of the scene */
    float volume;           /**< Volume of the influence box */
    ecs_entity_t entity;    /**< Entity of the probe, the last tie break */
} box_reflect_rank_t;

/** Main thread state of the reflection probe extract */
typedef struct box_render_reflect_t {
    ecs_query_t *bakes;     /**< Entities with ReflectionBake and their scene */
    ecs_query_t *probes;    /**< Entities with ReflectionProbe, their transform and scene */
    ecs_vec_t scenes;       /**< box_reflect_scene_t */
    ecs_vec_t free_buffers; /**< uint32_t, reflection probe buffer handles to reuse */
    uint32_t next_buffer;   /**< Next reflection probe buffer handle */
    ecs_vec_t ranks;        /**< box_reflect_rank_t, scratch of the rankings */
    int32_t indexed;        /**< Entities of the spatial index at the last extract */
    ecs_vec_t results;      /**< box_render_bake_result_t, scratch of the reported captures */
    float probe_ms;         /**< GPU milliseconds per probe capture */
    bool measured;          /**< probe_ms comes from a measurement */
    int32_t pending;        /**< Probes left in the scenes whose captures can progress */
    const void *backend;    /**< Context of the backend the buffers were sent to */
    void (*render)(void*, const box_render_packet_t*); /**< Render callback of that backend */
    int32_t generation;     /**< Generation of that backend, see box_render_backend_t */
    box_render_bake_t *bake; /**< State of the bake extract, looked up once */
    const void *lights_state; /**< State of the light extract, looked up once */
    ecs_map_t light_map;    /**< Light entity to its index in a light record, scratch */
    box_task_id_t task;     /**< Report of the captures left, 0 while no scene progresses */
    bool shifting;          /**< The world origin shifted since the last extract */
} box_render_reflect_t;

/** Find the probes of a scene.
 *
 * @param r The state
 * @param scene The scene root
 * @return The scene, NULL when it has no ReflectionBake
 */
static box_reflect_scene_t* box_reflect_find(
    const box_render_reflect_t *r,
    ecs_entity_t scene)
{
    box_reflect_scene_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&r->scenes); i ++) {
        box_reflect_scene_t *s = ecs_vec_get_t(&r->scenes, box_reflect_scene_t, i);
        if (s->scene == scene) {
            result = s;
            break;
        }
    }

    return result;
}

/** Find the probes of a scene, adding an entry when it has none.
 *
 * @param r The state
 * @param scene The scene root
 * @return The scene, valid until the next scene is added
 */
static box_reflect_scene_t* box_reflect_ensure(
    box_render_reflect_t *r,
    ecs_entity_t scene)
{
    box_reflect_scene_t *result = box_reflect_find(r, scene);
    if (!result) {
        result = ecs_vec_append_t(NULL, &r->scenes, box_reflect_scene_t);
        ecs_os_memset_t(result, 0, box_reflect_scene_t);
        result->scene = scene;
        result->scene_index = -1;
        result->previous = -1;
        ecs_vec_init_t(NULL, &result->probes, box_reflect_probe_t, 0);
        ecs_vec_init_t(NULL, &result->slots, uint8_t, 0);
        ecs_vec_init_t(NULL, &result->cameras, vec3, 0);
        ecs_vec_init_t(NULL, &result->list, box_render_reflection_probe_t, 0);
        ecs_vec_init_t(NULL, &result->sent_list, box_render_reflection_probe_t, 0);
        ecs_map_init(&result->lookup, NULL);
        box_bake_light_record_init(&result->record);
    }

    return result;
}

/** Free what the entry of a scene owns.
 *
 * @param s The scene
 */
static void box_reflect_scene_fini(
    box_reflect_scene_t *s)
{
    ecs_vec_fini_t(NULL, &s->probes, box_reflect_probe_t);
    ecs_vec_fini_t(NULL, &s->slots, uint8_t);
    ecs_vec_fini_t(NULL, &s->cameras, vec3);
    ecs_vec_fini_t(NULL, &s->list, box_render_reflection_probe_t);
    ecs_vec_fini_t(NULL, &s->sent_list, box_render_reflection_probe_t);
    ecs_map_fini(&s->lookup);
    box_bake_light_record_fini(&s->record);
}

/** Give a scene the handle its reflection probe buffer and its cubemap array share.
 *
 * @param r The state
 * @param s The scene
 */
static void box_reflect_handle(
    box_render_reflect_t *r,
    box_reflect_scene_t *s)
{
    if (!s->buffer && ecs_vec_count(&r->free_buffers)) {
        s->buffer = *ecs_vec_last_t(&r->free_buffers, uint32_t);
        ecs_vec_remove_last(&r->free_buffers);
    } else if (!s->buffer) {
        s->buffer = r->next_buffer ++;
    }
}

/** Destroy the reflection probe buffer and the cubemap array of a scene and free its handle.
 *
 * @param r The state
 * @param p The packet, receives the destroy of the buffer
 * @param s The scene
 */
static void box_reflect_release(
    box_render_reflect_t *r,
    box_render_packet_t *p,
    box_reflect_scene_t *s)
{
    if (s->buffer) {
        if (s->capacity) {
            box_render_packet_resource(p, BOX_RENDER_BUFFER, BOX_RENDER_DESTROY, s->buffer, 0);
        }

        if (s->cubes) {
            box_render_packet_resource(p, BOX_RENDER_REFLECTION, BOX_RENDER_DESTROY,
                s->buffer, 0);
            s->cubes = 0;
        }

        *ecs_vec_append_t(NULL, &r->free_buffers, uint32_t) = s->buffer;
        s->buffer = 0;
        s->capacity = 0;
        s->sent = false;
    }
}

/** Remove the entry of a scene that lost its ReflectionBake, moving the last one into its
 * place.
 *
 * @param r The state
 * @param p The packet
 * @param index Index of the scene
 */
static void box_reflect_remove_scene(
    box_render_reflect_t *r,
    box_render_packet_t *p,
    int32_t index)
{
    box_reflect_scene_t *s = ecs_vec_get_t(&r->scenes, box_reflect_scene_t, index);
    int32_t last = ecs_vec_count(&r->scenes) - 1;
    box_reflect_release(r, p, s);
    box_reflect_scene_fini(s);
    if (index != last) {
        *s = *ecs_vec_get_t(&r->scenes, box_reflect_scene_t, last);
    }

    ecs_vec_remove_last(&r->scenes);
}

/** Add a probe to a scene, without a slot.
 *
 * @param s The scene
 * @param entity Entity of the probe, 0 for the scene probe
 * @return The probe, valid until the next probe is added
 */
static box_reflect_probe_t* box_reflect_add_probe(
    box_reflect_scene_t *s,
    ecs_entity_t entity)
{
    int32_t index = ecs_vec_count(&s->probes);
    box_reflect_probe_t *result = ecs_vec_append_t(NULL, &s->probes, box_reflect_probe_t);
    ecs_os_memset_t(result, 0, box_reflect_probe_t);
    result->entity = entity;
    result->slot = -1;
    if (entity) {
        ecs_map_insert(&s->lookup, entity, (ecs_map_val_t)index);
    } else {
        s->scene_index = index;
    }

    return result;
}

/** Find the probe of an entity in a scene, adding it when the scene lacks it.
 *
 * @param s The scene
 * @param entity Entity with the ReflectionProbe
 * @return The probe, valid until the next probe is added
 */
static box_reflect_probe_t* box_reflect_ensure_probe(
    box_reflect_scene_t *s,
    ecs_entity_t entity)
{
    ecs_map_val_t *index = ecs_map_get(&s->lookup, entity);

    return index
        ? ecs_vec_get_t(&s->probes, box_reflect_probe_t, (int32_t)*index)
        : box_reflect_add_probe(s, entity);
}

/** Free the slot of a probe when it has one, forgetting its capture, and for the scene probe
 * the previous capture too.
 *
 * @param s The scene
 * @param probe The probe
 */
static void box_reflect_evict(
    box_reflect_scene_t *s,
    box_reflect_probe_t *probe)
{
    if (!probe->entity) {
        box_reflect_drop_previous(s);
    }

    if (probe->slot >= 0) {
        *ecs_vec_get_t(&s->slots, uint8_t, probe->slot) = 0;
        probe->slot = -1;
        s->resident --;
    }

    probe->captured = false;
    probe->queued = false;
    probe->packet = 0;
}

/** Queue a probe for a new capture when it has a capture or one in flight; a probe without
 * either waits for its first capture already.
 *
 * @param probe The probe
 */
static void box_reflect_queue(
    box_reflect_probe_t *probe)
{
    probe->queued |= probe->captured || probe->packet;
}

/** Queue every probe of a scene for a new capture.
 *
 * @param s The scene
 */
static void box_reflect_queue_all(
    box_reflect_scene_t *s)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&s->probes); i ++) {
        box_reflect_queue(ecs_vec_get_t(&s->probes, box_reflect_probe_t, i));
    }
}

/** Check whether a probe has a capture of what it shows now: reported, with no change since.
 *
 * @param probe The probe
 * @return Whether the capture is current
 */
static bool box_reflect_current(
    const box_reflect_probe_t *probe)
{
    return probe->captured && !probe->queued && !probe->packet;
}

/** Check whether a probe waits for a capture that is not in a packet yet.
 *
 * @param probe The probe
 * @return Whether its next capture can be scheduled
 */
static bool box_reflect_waiting(
    const box_reflect_probe_t *probe)
{
    return !probe->packet && (!probe->captured || probe->queued);
}

/** Check whether two points differ, or differ by more than the rounding of a shift of the
 * world origin.
 *
 * @param a The first point
 * @param b The second point
 * @param shifting Whether the world origin shifted, which allows the rounding
 * @return Whether the points differ
 */
static bool box_reflect_point_moved(
    const float *a,
    const float *b,
    bool shifting)
{
    bool result = false;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        float tolerance = shifting ? 1e-4f * (1.0f + fabsf(a[i])) : 0.0f;
        result = result || fabsf(a[i] - b[i]) > tolerance || isnan(a[i]) != isnan(b[i]);
    }

    return result;
}

/** Check whether the data of a probe captures something else than before: its boxes or its
 * capture point moved or resized. Blend and priority only change how it is blended.
 *
 * @param a The data before, moved by a shift of the world origin
 * @param b The data now
 * @param shifting Whether the world origin shifted, so the points may differ by its rounding
 * @return Whether the probe needs a new capture
 */
static bool box_reflect_moved(
    const box_render_reflection_probe_t *a,
    const box_render_reflection_probe_t *b,
    bool shifting)
{
    return box_reflect_point_moved(a->center, b->center, shifting) ||
        ecs_os_memcmp(a->axes, b->axes, ECS_SIZEOF(a->axes)) ||
        ecs_os_memcmp(a->extent, b->extent, ECS_SIZEOF(a->extent)) ||
        ecs_os_memcmp(a->projection, b->projection, ECS_SIZEOF(a->projection)) ||
        box_reflect_point_moved(a->capture, b->capture, shifting);
}

/** Check whether a box padded by the half size of another box, then by a radius, reaches the
 * center of that box along the axes of a probe: the boxes of a probe padded by the bounds of a
 * shape, or by the range of a light.
 *
 * @param d The data of the probe
 * @param extent Half size of the box of the probe on its axes
 * @param center World center of the other box
 * @param half World half size of the other box
 * @param radius Distance the padded box reaches beyond itself
 * @return Whether the center is reached
 */
static bool box_reflect_box_reaches(
    const box_render_reflection_probe_t *d,
    const float *extent,
    const vec3 center,
    const vec3 half,
    float radius)
{
    vec3 delta;
    float outside = 0.0f;
    int32_t i;
    glm_vec3_sub(ECS_CONST_CAST(float*, center), ECS_CONST_CAST(float*, d->center), delta);
    for (i = 0; i < 3; i ++) {
        const float *axis = d->axes[i];
        float along = fabsf(glm_vec3_dot(delta, ECS_CONST_CAST(float*, axis)));
        float reach = fabsf(extent[i]) + fabsf(axis[0]) * half[0] + fabsf(axis[1]) * half[1] +
            fabsf(axis[2]) * half[2];
        float out = glm_max(along - reach, 0.0f);
        outside += out * out;
    }

    return outside <= radius * radius;
}

/** Check whether the influence box or the projection box of a probe, padded by the half size
 * of a box and a radius, reaches the center of that box.
 *
 * @param d The data of the probe
 * @param center World center of the box
 * @param half World half size of the box
 * @param radius Distance beyond the box
 * @return Whether either box of the probe is reached
 */
static bool box_reflect_reaches(
    const box_render_reflection_probe_t *d,
    const vec3 center,
    const vec3 half,
    float radius)
{
    return box_reflect_box_reaches(d, d->extent, center, half, radius) ||
        box_reflect_box_reaches(d, d->projection, center, half, radius);
}

/** Give a probe the lowest free slot when it has none.
 *
 * @param s The scene
 * @param probe The probe
 * @return Whether the probe has a slot
 */
static bool box_reflect_admit(
    box_reflect_scene_t *s,
    box_reflect_probe_t *probe)
{
    int32_t i, count = ecs_vec_count(&s->slots);
    uint8_t *slots = ecs_vec_first(&s->slots);
    for (i = 0; probe->slot < 0 && i < count; i ++) {
        if (!slots[i]) {
            slots[i] = 1;
            probe->slot = i;
            s->resident ++;
        }
    }

    return probe->slot >= 0;
}

/** Remove the probes of a scene that no longer exist, freeing their slots; the probes that
 * remain keep theirs.
 *
 * @param s The scene
 */
static void box_reflect_prune(
    box_reflect_scene_t *s)
{
    int32_t i;
    for (i = ecs_vec_count(&s->probes) - 1; i >= 0; i --) {
        box_reflect_probe_t *probe = ecs_vec_get_t(&s->probes, box_reflect_probe_t, i);
        int32_t last = ecs_vec_count(&s->probes) - 1;
        if (probe->seen) {
            continue;
        }

        box_reflect_evict(s, probe);
        if (probe->entity) {
            ecs_map_remove(&s->lookup, probe->entity);
        } else {
            s->scene_index = -1;
        }

        if (i != last) {
            *probe = *ecs_vec_get_t(&s->probes, box_reflect_probe_t, last);
            if (probe->entity) {
                *ecs_map_get(&s->lookup, probe->entity) = (ecs_map_val_t)i;
            } else {
                s->scene_index = i;
            }
        }

        ecs_vec_remove_last(&s->probes);
    }
}

/** Fill the GPU data of a probe from the world transform of its entity, ignoring its scale.
 *
 * @param out Receives the data, all but the slot
 * @param probe The ReflectionProbe
 * @param transform World transform of the entity
 */
static void box_reflect_place(
    box_render_reflection_probe_t *out,
    const ReflectionProbe *probe,
    const WorldTransform *transform)
{
    mat4 m;
    int32_t i, k;
    glm_mat4_copy(ECS_CONST_CAST(vec4*, transform->m), m);

    ecs_os_memset_t(out, 0, box_render_reflection_probe_t);
    glm_vec3_copy(m[3], out->center);
    glm_vec3_copy(m[3], out->capture);
    for (i = 0; i < 3; i ++) {
        float length = glm_vec3_norm(m[i]);
        for (k = 0; k < 3; k ++) {
            out->axes[i][k] = length > 0 && isfinite(length)
                ? m[i][k] / length
                : (float)(i == k);
        }

        for (k = 0; k < 3; k ++) {
            out->capture[k] += out->axes[i][k] * probe->offset[i];
        }
    }

    glm_vec3_copy(ECS_CONST_CAST(float*, probe->extent), out->extent);
    glm_vec3_copy(ECS_CONST_CAST(float*, probe->projection), out->projection);
    out->blend = probe->blend;
    out->priority = probe->priority;
}

/** Place the scene probe of a scene: horizontally centered on its static bounds at height
 * above their bottom, the origin of the bounds without static shapes.
 *
 * @param world The world
 * @param s The scene
 * @param height ReflectionBake.scene_probe_height
 */
static void box_reflect_place_scene(
    const ecs_world_t *world,
    box_reflect_scene_t *s,
    float height)
{
    vec3 min = {0, 0, 0}, max = {0, 0, 0};
    box_spatial_static_bounds(world, s->scene, min, max);
    s->scene_capture[0] = (min[0] + max[0]) * 0.5f;
    s->scene_capture[1] = min[1] + height;
    s->scene_capture[2] = (min[2] + max[2]) * 0.5f;
    s->height = height;
    s->placed = true;
}

/** Fill the GPU data of the scene probe: no box projection, an influence covering the whole
 * scene and the lowest priority.
 *
 * @param s The scene
 * @param out Receives the data, all but the slot
 */
static void box_reflect_scene_data(
    const box_reflect_scene_t *s,
    box_render_reflection_probe_t *out)
{
    int32_t i;
    ecs_os_memset_t(out, 0, box_render_reflection_probe_t);
    glm_vec3_copy(ECS_CONST_CAST(float*, s->scene_capture), out->center);
    glm_vec3_copy(ECS_CONST_CAST(float*, s->scene_capture), out->capture);
    for (i = 0; i < 3; i ++) {
        out->axes[i][i] = 1.0f;
        out->extent[i] = BOX_REFLECT_SCENE_EXTENT;
    }

    out->priority = INT32_MIN;
    out->scene = 1;
}

/** Order probes for the slots: the scene probe first, then higher priority, then nearer to
 * the cameras of the scene.
 *
 * @param a The first rank
 * @param b The second rank
 * @return Negative when a comes first
 */
static int box_reflect_slot_cmp(
    const void *a,
    const void *b)
{
    const box_reflect_rank_t *ra = a, *rb = b;
    int result = (rb->scene > ra->scene) - (rb->scene < ra->scene);
    if (!result) {
        result = (rb->priority > ra->priority) - (rb->priority < ra->priority);
    }

    if (!result) {
        result = (ra->distance > rb->distance) - (ra->distance < rb->distance);
    }

    return result ? result : (ra->entity > rb->entity) - (ra->entity < rb->entity);
}

/** Order probes for blending: higher priority first, then the smaller influence box, the
 * scene probe last.
 *
 * @param a The first rank
 * @param b The second rank
 * @return Negative when a comes first
 */
static int box_reflect_blend_cmp(
    const void *a,
    const void *b)
{
    const box_reflect_rank_t *ra = a, *rb = b;
    int result = (ra->scene > rb->scene) - (ra->scene < rb->scene);
    if (!result) {
        result = (rb->priority > ra->priority) - (rb->priority < ra->priority);
    }

    if (!result) {
        result = (ra->volume > rb->volume) - (ra->volume < rb->volume);
    }

    return result ? result : (ra->entity > rb->entity) - (ra->entity < rb->entity);
}

/** Rank the probes of a scene, the squared distance to the nearest camera included.
 *
 * @param r The state, receives the ranks
 * @param s The scene
 * @param resident Rank only the resident probes
 * @return The ranks, one per ranked probe
 */
static box_reflect_rank_t* box_reflect_rank(
    box_render_reflect_t *r,
    const box_reflect_scene_t *s,
    bool resident)
{
    int32_t i, c, count = ecs_vec_count(&s->probes), cams = ecs_vec_count(&s->cameras);
    const box_reflect_probe_t *probes = ecs_vec_first(&s->probes);
    ecs_vec_clear(&r->ranks);
    for (i = 0; i < count; i ++) {
        if (resident && probes[i].slot < 0) {
            continue;
        }

        box_reflect_rank_t *rank = ecs_vec_append_t(NULL, &r->ranks, box_reflect_rank_t);
        rank->index = i;
        rank->priority = probes[i].data.priority;
        rank->scene = !probes[i].entity;
        rank->entity = probes[i].entity;
        rank->volume = fabsf(probes[i].data.extent[0] * probes[i].data.extent[1] *
            probes[i].data.extent[2]);
        rank->distance = FLT_MAX;
        for (c = 0; c < cams; c ++) {
            float d = glm_vec3_distance2(ECS_CONST_CAST(float*, probes[i].data.center),
                *ecs_vec_get_t(&s->cameras, vec3, c));
            rank->distance = glm_min(rank->distance, d);
        }
    }

    return ecs_vec_first(&r->ranks);
}

/** Give slots to the probes of a scene up to the capacity, in the order of
 * box_reflect_slot_cmp. A probe that stays resident keeps its slot.
 *
 * @param r The state
 * @param s The scene
 * @param capacity Slots of the scene
 */
static void box_reflect_assign(
    box_render_reflect_t *r,
    box_reflect_scene_t *s,
    int32_t capacity)
{
    int32_t i, count = ecs_vec_count(&s->probes), old = ecs_vec_count(&s->slots);
    box_reflect_probe_t *probes = ecs_vec_first(&s->probes);
    if (capacity != old) {
        if (s->previous >= capacity) {
            box_reflect_drop_previous(s);
        }

        for (i = 0; i < count; i ++) {
            if (probes[i].slot >= capacity) {
                box_reflect_evict(s, &probes[i]);
            }
        }

        ecs_vec_set_count_t(NULL, &s->slots, uint8_t, capacity);
        for (i = old; i < capacity; i ++) {
            *ecs_vec_get_t(&s->slots, uint8_t, i) = 0;
        }
    }

    if (count <= capacity) {
        for (i = 0; i < count; i ++) {
            box_reflect_admit(s, &probes[i]);
        }

        goto done;
    }

    box_reflect_rank_t *ranks = box_reflect_rank(r, s, false);
    qsort(ranks, (size_t)count, sizeof(box_reflect_rank_t), box_reflect_slot_cmp);
    for (i = capacity; i < count; i ++) {
        box_reflect_evict(s, &probes[ranks[i].index]);
    }

    for (i = 0; i < capacity; i ++) {
        box_reflect_admit(s, &probes[ranks[i].index]);
    }

done:
    return;
}

/** Collect the resident probes of a scene in blend order, after their captures of this frame
 * are scheduled: a probe is ready once its capture was reported or is in flight, since the
 * captures of a packet run before its targets and a packet renders after the earlier ones.
 *
 * @param r The state
 * @param s The scene, receives the list
 */
static void box_reflect_list(
    box_render_reflect_t *r,
    box_reflect_scene_t *s)
{
    const box_reflect_probe_t *probes = ecs_vec_first(&s->probes);
    box_reflect_rank_t *ranks = box_reflect_rank(r, s, true);
    int32_t i, count = ecs_vec_count(&r->ranks);
    qsort(ranks, (size_t)count, sizeof(box_reflect_rank_t), box_reflect_blend_cmp);
    ecs_vec_set_count_t(NULL, &s->list, box_render_reflection_probe_t, count);
    for (i = 0; i < count; i ++) {
        box_render_reflection_probe_t *out = ecs_vec_get_t(
            &s->list, box_render_reflection_probe_t, i);
        const box_reflect_probe_t *probe = &probes[ranks[i].index];
        *out = probe->data;
        out->slot = (uint32_t)probe->slot;
        out->ready = probe->captured || probe->packet;
        if (!probe->entity && s->previous >= 0 && !out->ready) {
            out->slot = (uint32_t)s->previous;
            out->ready = 1;
        } else if (!probe->entity && s->previous >= 0) {
            out->previous = (uint32_t)s->previous;
            out->fade = s->fade;
        }
    }
}

/** Upload the resident probes of a scene when the backend lacks them or they changed,
 * creating a larger buffer when they do not fit.
 *
 * @param r The state
 * @param p The packet
 * @param s The scene
 */
static void box_reflect_send(
    box_render_reflect_t *r,
    box_render_packet_t *p,
    box_reflect_scene_t *s)
{
    int32_t count = ecs_vec_count(&s->list);
    uint32_t size = (uint32_t)count * (uint32_t)sizeof(box_render_reflection_probe_t);
    if (!count) {
        goto done;
    }

    box_reflect_handle(r, s);
    if (!s->capacity || (uint32_t)count > s->capacity) {
        s->capacity = BOX_REFLECT_MIN_CAPACITY;
        while (s->capacity < (uint32_t)count) {
            s->capacity *= 2;
        }

        box_render_buffer_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_BUFFER,
            BOX_RENDER_CREATE, s->buffer, sizeof(box_render_buffer_desc_t));
        desc->size = s->capacity * (uint64_t)sizeof(box_render_reflection_probe_t);
        desc->usage = BOX_RENDER_BUFFER_STORAGE;
        s->sent = false;
    }

    if (s->sent && count == ecs_vec_count(&s->sent_list) &&
        !ecs_os_memcmp(ecs_vec_first(&s->list), ecs_vec_first(&s->sent_list), (ecs_size_t)size))
    {
        goto done;
    }

    ecs_os_memcpy(box_render_packet_upload(p, s->buffer, 0, size), ecs_vec_first(&s->list),
        (ecs_size_t)size);
    ecs_vec_set_count_t(NULL, &s->sent_list, box_render_reflection_probe_t, count);
    ecs_os_memcpy(ecs_vec_first(&s->sent_list), ecs_vec_first(&s->list), (ecs_size_t)size);
    s->sent = true;
done:
    return;
}

/** Write the probe counts of a scene into the ReflectionBakeStatus of its entity when they
 * changed.
 *
 * @param it The iterator of the system
 * @param s The scene
 */
static void box_reflect_status(
    ecs_iter_t *it,
    const box_reflect_scene_t *s)
{
    const ReflectionBakeStatus *status = s->entity && ecs_is_alive(it->world, s->entity)
        ? ecs_get(it->world, s->entity, ReflectionBakeStatus)
        : NULL;
    int32_t probes = ecs_vec_count(&s->probes), captured = s->captured;
    bool converged = !s->error && captured == s->resident;
    if (!status) {
        goto done;
    }

    bool same_error = s->error
        ? status->error && !strcmp(status->error, s->error)
        : !status->error;
    if (status->probes != probes || status->resident != s->resident ||
        status->captured != captured || status->converged != converged || !same_error)
    {
        ReflectionBakeStatus value = *status;
        value.probes = probes;
        value.resident = s->resident;
        value.captured = captured;
        value.converged = converged;
        value.error = ECS_CONST_CAST(char*, s->error);
        ecs_set_ptr(it->stage, s->entity, ReflectionBakeStatus, &value);
    }

done:
    return;
}

/** Take the ReflectionBake of every scene, adding entries for new scenes and removing those
 * of scenes that lost it.
 *
 * @param world The world
 * @param r The state
 * @param p The packet
 */
static void box_reflect_bakes(
    ecs_world_t *world,
    box_render_reflect_t *r,
    box_render_packet_t *p)
{
    box_spatial_stats_t stats;
    int32_t i;
    box_spatial_stats(world, &stats);
    bool changed = stats.updated != 0 || stats.entities != r->indexed;
    r->indexed = stats.entities;
    for (i = 0; i < ecs_vec_count(&r->scenes); i ++) {
        box_reflect_scene_t *s = ecs_vec_get_t(&r->scenes, box_reflect_scene_t, i);
        s->seen = false;
        s->viewed = false;
        s->volume = 0;
        s->volume_buffer = 0;
        ecs_vec_clear(&s->cameras);
    }

    ecs_iter_t qit = ecs_query_iter(world, r->bakes);
    while (ecs_query_next(&qit)) {
        const ReflectionBake *bakes = ecs_field(&qit, ReflectionBake, 0);
        int32_t row;
        for (row = 0; row < qit.count; row ++) {
            const ReflectionBake *bake = &bakes[row];
            box_reflect_scene_t *s = box_reflect_ensure(r, box_render_scene_field(&qit, 1, row));
            if (s->seen) {
                continue;
            }

            s->seen = true;
            s->entity = qit.entities[row];
            s->error = bake->resolution < BOX_REFLECT_RESOLUTION_MIN ||
                bake->resolution > BOX_REFLECT_RESOLUTION_MAX
                ? BOX_REFLECT_RESOLUTION_ERROR
                : NULL;
            s->resolution = bake->resolution;
            s->budget_ms = bake->budget_ms;
            s->threshold.light = bake->light_threshold;
            s->threshold.color = bake->color_threshold;
            s->env_handle = bake->environment
                ? box_render_environment_handle(world, bake->environment)
                : 0;
            s->env_content = s->env_handle
                ? box_render_environment_captured(world, bake->environment)
                : 0;
            s->env_handle = s->env_content ? s->env_handle : 0;
            if (changed || !s->bounded) {
                glm_vec3_zero(s->min);
                glm_vec3_zero(s->max);
                box_spatial_static_bounds(world, s->scene, s->min, s->max);
                s->bounded = true;
            }

            s->follow_radius = bake->follow_radius;
            s->follow_blend = bake->follow_blend;
            s->follow = false;
            if (!bake->scene_probe) {
                s->anchored = false;
                continue;
            }

            vec3 focus;
            if (bake->follow_focus && box_reflect_focus(world, s->scene, focus)) {
                s->follow = true;
                box_reflect_place_focus(s, focus, bake->scene_probe_height,
                    bake->follow_distance);
            } else if (changed || !s->placed || s->anchored ||
                s->height != bake->scene_probe_height)
            {
                box_reflect_place_scene(world, s, bake->scene_probe_height);
                s->anchored = false;
            }

            box_reflect_probe_t *probe = s->scene_index >= 0
                ? ecs_vec_get_t(&s->probes, box_reflect_probe_t, s->scene_index)
                : box_reflect_add_probe(s, 0);
            box_render_reflection_probe_t was = probe->data;
            box_reflect_scene_data(s, &probe->data);
            if (box_reflect_moved(&was, &probe->data, r->shifting) &&
                !(s->follow && box_reflect_hold(s, probe)))
            {
                box_reflect_queue(probe);
            }

            probe->seen = true;
        }
    }

    for (i = ecs_vec_count(&r->scenes) - 1; i >= 0; i --) {
        if (!ecs_vec_get_t(&r->scenes, box_reflect_scene_t, i)->seen) {
            box_reflect_remove_scene(r, p, i);
        }
    }
}

/** Take the ReflectionProbe of every entity in a scene with ReflectionBake, queueing the
 * probes that moved or resized.
 *
 * @param world The world
 * @param r The state
 */
static void box_reflect_probes(
    ecs_world_t *world,
    box_render_reflect_t *r)
{
    ecs_iter_t qit = ecs_query_iter(world, r->probes);
    while (ecs_query_next(&qit)) {
        const ReflectionProbe *probes = ecs_field(&qit, ReflectionProbe, 0);
        const WorldTransform *transforms = ecs_field(&qit, WorldTransform, 1);
        int32_t row;
        for (row = 0; row < qit.count; row ++) {
            box_reflect_scene_t *s = box_reflect_find(r, box_render_scene_field(&qit, 2, row));
            if (!s) {
                continue;
            }

            box_reflect_probe_t *probe = box_reflect_ensure_probe(s, qit.entities[row]);
            box_render_reflection_probe_t was = probe->data;
            WorldTransform buf;
            box_reflect_place(&probe->data, &probes[row],
                box_transform_world_field(world, &qit, transforms, row, &buf));
            if (box_reflect_moved(&was, &probe->data, r->shifting)) {
                box_reflect_queue(probe);
            }

            probe->seen = true;
        }
    }
}

/** Collect the cameras of the views of every scene, the lights of the first one and the probe
 * volume buffer they show, or point the lit views that sample reflections at the reflection
 * probe buffer of their scene.
 *
 * @param world The world
 * @param r The state
 * @param p The packet
 * @param cameras Collect the cameras, else point the views at the buffers
 */
static void box_reflect_views(
    const ecs_world_t *world,
    const box_render_reflect_t *r,
    box_render_packet_t *p,
    bool cameras)
{
    int32_t i, count = ecs_vec_count(&p->views);
    for (i = 0; i < count; i ++) {
        box_render_view_t *rv = ecs_vec_get_t(&p->views, box_render_view_t, i);
        box_reflect_scene_t *s = box_reflect_find(r, rv->scene);
        if (!s) {
            continue;
        }

        if (cameras) {
            glm_vec3_copy(rv->camera_transform[3], *ecs_vec_append_t(NULL, &s->cameras, vec3));
            if (!s->viewed) {
                s->lights = rv->lights;
                s->viewed = true;
            }

            if (!s->volume && rv->volume) {
                s->volume = rv->volume;
                s->volume_buffer = rv->volume_buffer;
            }

            continue;
        }

        if (s->sent && ecs_vec_count(&s->list) && rv->lights.enabled &&
            (rv->features & BOX_RENDER_FEATURE_REFLECTIONS))
        {
            const box_render_reflection_probe_t *last = ecs_vec_last_t(
                &s->list, box_render_reflection_probe_t);
            rv->reflections = s->buffer;
            rv->reflection_count = (uint32_t)ecs_vec_count(&s->list);
            rv->reflection_listed = rv->reflection_count - last->scene;
        }
    }
}

/** Get the cubemap slots a scene has on the backend of a world.
 *
 * @param world The world
 * @return maxTextureArrayLayers of the device divided by the faces of a cubemap
 */
static int32_t box_reflect_capacity(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats;
    int32_t layers = BOX_RENDER_REFLECT_LAYERS;
    if (box_render_gpu_stats(world, &stats) && stats.ready && stats.texture_layers > 0) {
        layers = stats.texture_layers;
    }

    return layers / BOX_REFLECT_FACES;
}

/** Forget the captures of every probe of a scene, those in flight included.
 *
 * @param s The scene
 */
static void box_reflect_uncapture(
    box_reflect_scene_t *s)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&s->probes); i ++) {
        box_reflect_probe_t *probe = ecs_vec_get_t(&s->probes, box_reflect_probe_t, i);
        probe->captured = false;
        probe->queued = false;
        probe->packet = 0;
    }

    box_reflect_drop_previous(s);
    s->relit = false;
    s->settled = false;
}

/** Make the cubemap array of a scene hold the slots in use at its resolution: a power of two
 * of at least BOX_REFLECT_MIN_CAPACITY slots above the highest one a probe has, none without
 * resident probes or with an error. A new array keeps the captures at the same resolution.
 *
 * @param r The state
 * @param p The packet
 * @param s The scene, with its slots assigned
 * @param capacity Slots of the scene
 */
static void box_reflect_texture(
    box_render_reflect_t *r,
    box_render_packet_t *p,
    box_reflect_scene_t *s,
    int32_t capacity)
{
    int32_t i, used = 0;
    uint32_t cubes = 0;
    for (i = 0; i < ecs_vec_count(&s->probes); i ++) {
        used = glm_max(used, ecs_vec_get_t(&s->probes, box_reflect_probe_t, i)->slot + 1);
    }

    used = used ? glm_max(used, s->previous + 1) : 0;

    if (used && !s->error) {
        cubes = BOX_REFLECT_MIN_CAPACITY;
        while (cubes < (uint32_t)used) {
            cubes *= 2;
        }

        cubes = glm_min(cubes, (uint32_t)capacity);
    }

    if (cubes == s->cubes && (!cubes || s->made == s->resolution)) {
        goto done;
    }

    if (!cubes || s->made != s->resolution) {
        box_reflect_uncapture(s);
    }

    if (!cubes) {
        box_render_packet_resource(p, BOX_RENDER_REFLECTION, BOX_RENDER_DESTROY, s->buffer, 0);
        s->cubes = 0;
        goto done;
    }

    box_reflect_handle(r, s);
    box_render_reflection_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_REFLECTION,
        BOX_RENDER_CREATE, s->buffer, sizeof(box_render_reflection_desc_t));
    desc->resolution = (uint32_t)s->resolution;
    desc->cubes = cubes;
    s->cubes = cubes;
    s->made = s->resolution;
done:
    return;
}

/** Find the probe of a scene in flight with a capture into a slot.
 *
 * @param s The scene
 * @param slot The slot
 * @param ticket Ticket of the capture
 * @return The probe, NULL when no probe waits for that capture
 */
static box_reflect_probe_t* box_reflect_flight(
    box_reflect_scene_t *s,
    uint32_t slot,
    uint32_t ticket)
{
    box_reflect_probe_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&s->probes); i ++) {
        box_reflect_probe_t *probe = ecs_vec_get_t(&s->probes, box_reflect_probe_t, i);
        if (probe->packet && probe->ticket == ticket && probe->slot == (int32_t)slot) {
            result = probe;
            break;
        }
    }

    return result;
}

/** Mark the probes the backend reported captured.
 *
 * @param world The world
 * @param r The state
 */
static void box_reflect_captured(
    ecs_world_t *world,
    box_render_reflect_t *r)
{
    int32_t i, j;
    box_render_gpu_reflect_captured(world, &r->results);
    for (i = 0; i < ecs_vec_count(&r->results); i ++) {
        const box_render_bake_result_t *res = ecs_vec_get_t(
            &r->results, box_render_bake_result_t, i);
        for (j = 0; j < ecs_vec_count(&r->scenes); j ++) {
            box_reflect_scene_t *s = ecs_vec_get_t(&r->scenes, box_reflect_scene_t, j);
            box_reflect_probe_t *probe = s->buffer == res->volume && s->cubes
                ? box_reflect_flight(s, res->probe, res->generation)
                : NULL;
            if (probe) {
                probe->captured = true;
                probe->packet = 0;
                break;
            }
        }
    }
}

/** Queue the probes of a scene again whose packet the backend released without reporting
 * their capture, and count the probes whose capture is current.
 *
 * @param s The scene
 * @param rendered Packets the backend released, as RenderStatus.rendered
 */
static void box_reflect_requeue(
    box_reflect_scene_t *s,
    int32_t rendered)
{
    int32_t i;
    s->captured = 0;
    s->landed = 0;
    for (i = 0; i < ecs_vec_count(&s->probes); i ++) {
        box_reflect_probe_t *probe = ecs_vec_get_t(&s->probes, box_reflect_probe_t, i);
        if (probe->packet && probe->packet <= rendered) {
            probe->packet = 0;
            probe->queued |= probe->captured;
        }

        s->captured += probe->slot >= 0 && box_reflect_current(probe);
        s->landed += probe->slot >= 0 && probe->captured;
    }
}

/** Queue the probes of a scene that a light reaches: within its range, or every probe for a
 * directional light or a change of the whole scene. A light reaches a scene probe that follows
 * the focus within the follow radius of it.
 *
 * @param ctx The scene
 * @param l The light, NULL for the whole scene
 */
static void box_reflect_mark_light(
    void *ctx,
    const box_render_light_t *l)
{
    box_reflect_scene_t *s = ctx;
    vec3 zero = {0, 0, 0};
    int32_t i;
    for (i = 0; i < ecs_vec_count(&s->probes); i ++) {
        box_reflect_probe_t *probe = ecs_vec_get_t(&s->probes, box_reflect_probe_t, i);
        bool reaches = !probe->entity && s->follow
            ? box_reflect_within(probe->data.capture, l ? l->position : zero, zero,
                l ? s->follow_radius + l->range : 0.0f)
            : box_reflect_reaches(&probe->data, l ? l->position : zero, zero,
                l ? l->range : 0.0f);
        if (!l || !(l->range > 0) || reaches) {
            box_reflect_queue(probe);
        }
    }

    if (!l || !(l->range > 0)) {
        s->relit = true;
    }
}

/** Queue the probes of a scene that changed what they show since the last frame: those the
 * regions of the static shapes that changed reach, those the lights that changed reach and
 * every probe when the light bake of the scene showed a new pass, converged, started or ended.
 *
 * Another source of the environment the captures see queues every probe, and so does a new
 * version of it unless a directional light is held within the thresholds of its captured
 * state, which the source then follows.
 *
 * Content that only moved, such as drifting clouds, is no new version here.
 *
 * @param r The state
 * @param s The scene
 */
static void box_reflect_invalidate(
    box_render_reflect_t *r,
    box_reflect_scene_t *s)
{
    const box_render_bake_t *b = r->bake;
    const box_bake_volume_t *v = b ? box_bake_find(b, s->scene) : NULL;
    uint32_t handle = v ? v->handle : 0;
    int32_t i, j, switches = v ? v->switches : 0;
    for (i = 0; b && i < ecs_vec_count(&b->regions); i ++) {
        const box_bake_region_t *region = ecs_vec_get_t(&b->regions, box_bake_region_t, i);
        vec3 center, half;
        if (region->scene != s->scene) {
            continue;
        }

        glm_vec3_center(ECS_CONST_CAST(float*, region->min), ECS_CONST_CAST(float*, region->max),
            center);
        glm_vec3_sub(ECS_CONST_CAST(float*, region->max), center, half);
        for (j = 0; j < ecs_vec_count(&s->probes); j ++) {
            box_reflect_probe_t *probe = ecs_vec_get_t(&s->probes, box_reflect_probe_t, j);
            bool reaches = !probe->entity && s->follow
                ? box_reflect_within(probe->data.capture, center, half, s->follow_radius)
                : box_reflect_reaches(&probe->data, center, half, 0.0f);
            if (reaches) {
                box_reflect_queue(probe);
            }
        }
    }

    if (s->viewed) {
        box_bake_light_set_t lights;
        box_bake_view_lights(r->lights_state, s->scene, &lights);
        box_bake_lights_compare(&r->light_map, &s->record, &s->lights, &lights, true,
            &s->threshold, box_reflect_mark_light, s);
    }

    if (s->bake_known && (handle != s->bake_handle || switches != s->bake_switches)) {
        box_reflect_queue_all(s);
        s->relit |= s->bake_settled;
    }

    if (s->env_known && (s->env_handle != s->env_was_handle ||
        (s->env_content != s->env_was_content && !s->record.held)))
    {
        box_reflect_queue_all(s);
        s->relit = true;
    }

    s->env_known = true;
    s->env_was_handle = s->env_handle;
    s->env_was_content = s->env_content;

    s->bake_known = true;
    s->bake_handle = handle;
    s->bake_switches = switches;
    s->bake_settled = !v || v->settled || !v->compute;
}

/** Order probes for their captures: the scene probe first, then nearer to the cameras of the
 * scene.
 *
 * @param a The first rank
 * @param b The second rank
 * @return Negative when a comes first
 */
static int box_reflect_capture_cmp(
    const void *a,
    const void *b)
{
    const box_reflect_rank_t *ra = a, *rb = b;
    int result = (rb->scene > ra->scene) - (rb->scene < ra->scene);
    if (!result) {
        result = (ra->distance > rb->distance) - (ra->distance < rb->distance);
    }

    return result ? result : (ra->entity > rb->entity) - (ra->entity < rb->entity);
}

/** Get the probes a scene captures in a frame of its budget.
 *
 * @param r The state
 * @param s The scene
 * @return Probes of the budget at the GPU time per probe, at least one
 */
static int32_t box_reflect_budget(
    const box_render_reflect_t *r,
    const box_reflect_scene_t *s)
{
    float probes = floorf(s->budget_ms / r->probe_ms);
    int32_t result = BOX_REFLECT_FRAME_MAX;
    if (probes < (float)BOX_REFLECT_FRAME_MAX) {
        result = probes < 1.0f ? 1 : (int32_t)probes;
    }

    return result;
}

/** Add the captures of the next resident probes of a scene that wait for one to the packet,
 * every one of them in a headless run.
 *
 * @param r The state
 * @param p The packet
 * @param s The scene
 * @param headless Whether the run is headless
 * @param packet Number of the packet, as RenderStatus.submitted after it
 */
static void box_reflect_schedule(
    box_render_reflect_t *r,
    box_render_packet_t *p,
    box_reflect_scene_t *s,
    bool headless,
    int32_t packet)
{
    box_reflect_probe_t *probes = ecs_vec_first(&s->probes);
    box_reflect_rank_t *ranks = box_reflect_rank(r, s, true);
    int32_t i, n = 0, count = ecs_vec_count(&r->ranks);
    for (i = 0; i < count; i ++) {
        if (box_reflect_waiting(&probes[ranks[i].index])) {
            ranks[n ++] = ranks[i];
        }
    }

    count = headless ? n : glm_min(n, box_reflect_budget(r, s));
    if (!count) {
        goto done;
    }

    qsort(ranks, (size_t)n, sizeof(box_reflect_rank_t), box_reflect_capture_cmp);
    box_render_reflect_batch_t *batch = ecs_vec_append_t(
        NULL, &p->reflections, box_render_reflect_batch_t);
    ecs_os_memset_t(batch, 0, box_render_reflect_batch_t);
    batch->texture = s->buffer;
    batch->ticket = s->ticket;
    batch->scene = s->scene;
    batch->size = s->resolution;
    batch->lights = s->lights;
    batch->lights.enabled = true;
    batch->volume = s->volume;
    batch->volume_buffer = s->volume_buffer;
    batch->environment = s->env_handle;
    glm_vec3_copy(s->min, batch->bounds_min);
    glm_vec3_copy(s->max, batch->bounds_max);
    batch->first = ecs_vec_count(&p->probes);
    batch->count = count;
    for (i = 0; i < count; i ++) {
        box_reflect_probe_t *probe = &probes[ranks[i].index];
        box_render_probe_capture_t *out = ecs_vec_append_t(
            NULL, &p->probes, box_render_probe_capture_t);
        glm_vec3_copy(probe->data.capture, out->position);
        out->index = (uint32_t)probe->slot;
        probe->ticket = s->ticket ++;
        probe->packet = packet;
        probe->queued = false;
    }

done:
    return;
}

/** Take the GPU time of the captures the backend measured into the time per probe.
 *
 * @param world The world
 * @param r The state
 */
static void box_reflect_timing(
    ecs_world_t *world,
    box_render_reflect_t *r)
{
    box_render_bake_timing_t timing;
    if (!box_render_gpu_reflect_timing(world, &timing) || timing.probes <= 0) {
        goto done;
    }

    float per = (float)(timing.ms / (double)timing.probes);
    r->probe_ms = r->measured
        ? r->probe_ms + (per - r->probe_ms) * BOX_REFLECT_SMOOTHING
        : per;
    r->probe_ms = glm_max(r->probe_ms, 1e-3f);
    r->measured = true;
done:
    return;
}

/** Make the cubemap array of a scene hold its resident probes and capture the next ones.
 *
 * @param r The state
 * @param p The packet
 * @param s The scene, with its slots assigned
 * @param capacity Slots of the scene
 * @param gpu Whether the GPU backend renders
 * @param headless Whether the run is headless
 * @param status The render status, NULL before the first packet
 */
static void box_reflect_progress(
    box_render_reflect_t *r,
    box_render_packet_t *p,
    box_reflect_scene_t *s,
    int32_t capacity,
    bool gpu,
    bool headless,
    const RenderStatus *status)
{
    box_reflect_texture(r, p, s, capacity);
    box_reflect_requeue(s, status ? status->rendered : 0);
    bool progress = gpu && s->cubes && !s->error && s->budget_ms > 0 && s->viewed;
    if (progress && s->bake_settled) {
        s->settled |= s->captured == s->resident || (s->relit && s->landed == s->resident);
    }

    s->pending = progress && s->captured < s->resident;
    if (s->pending) {
        box_reflect_schedule(r, p, s, headless, (status ? status->submitted : 0) + 1);
        r->pending += s->settled ? 0 : s->resident - s->captured;
    }
}

/** Report the captures left of the scenes that progress as BOX_TASK_REFLECTIONS, not
 * blocking: the resident probes and those captured.
 *
 * @param world The world
 * @param r The state
 */
static void box_reflect_report(
    ecs_world_t *world,
    box_render_reflect_t *r)
{
    int32_t i, done = 0, total = 0;
    bool busy = false;
    for (i = 0; i < ecs_vec_count(&r->scenes); i ++) {
        const box_reflect_scene_t *s = ecs_vec_get_t(&r->scenes, box_reflect_scene_t, i);
        if (s->pending) {
            done += s->captured;
            total += s->resident;
            busy = true;
        }
    }

    if (busy && !r->task) {
        r->task = box_task_start(world, BOX_TASK_REFLECTIONS, false, total);
    }

    if (busy) {
        box_task_set(world, r->task, done, total);
    } else if (r->task) {
        box_task_end(world, r->task);
        r->task = 0;
    }
}

/** Keep the probes of every scene with ReflectionBake, give them slots, upload them and
 * capture them.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractReflect(
    ecs_iter_t *it)
{
    box_render_reflect_t *r = it->ctx;
    ecs_world_t *world = it->world;
    box_render_packet_t *p = box_render_packet(world);
    const box_render_backend_t *backend = box_render_get_backend(world);
    const App *app = ecs_singleton_get(world, App);
    const RenderStatus *status = ecs_singleton_get(world, RenderStatus);
    box_render_gpu_stats_t stats;
    bool gpu = box_render_gpu_stats(world, &stats) && !stats.failed;
    int32_t i, j, capacity = box_reflect_capacity(world);
    if (backend->ctx != r->backend || backend->render != r->render ||
        backend->generation != r->generation)
    {
        r->backend = backend->ctx;
        r->render = backend->render;
        r->generation = backend->generation;
        for (i = 0; i < ecs_vec_count(&r->scenes); i ++) {
            box_reflect_scene_t *s = ecs_vec_get_t(&r->scenes, box_reflect_scene_t, i);
            s->sent = false;
            s->capacity = 0;
            s->cubes = 0;
            box_reflect_uncapture(s);
        }
    }

    for (i = 0; i < ecs_vec_count(&r->scenes); i ++) {
        box_reflect_scene_t *s = ecs_vec_get_t(&r->scenes, box_reflect_scene_t, i);
        for (j = 0; j < ecs_vec_count(&s->probes); j ++) {
            ecs_vec_get_t(&s->probes, box_reflect_probe_t, j)->seen = false;
        }
    }

    if (!r->bake) {
        r->bake = box_bake_state(world);
    }

    if (!r->lights_state) {
        r->lights_state = box_render_lights_state(world);
    }

    box_reflect_bakes(world, r, p);
    if (r->bake) {
        r->bake->reflecting = ecs_vec_count(&r->scenes) != 0;
    }

    box_reflect_probes(world, r);
    r->shifting = false;
    box_reflect_views(world, r, p, true);
    box_reflect_timing(world, r);
    box_reflect_captured(world, r);
    r->pending = 0;
    for (i = 0; i < ecs_vec_count(&r->scenes); i ++) {
        box_reflect_scene_t *s = ecs_vec_get_t(&r->scenes, box_reflect_scene_t, i);
        box_reflect_prune(s);
        box_reflect_assign(r, s, capacity);
        box_reflect_invalidate(r, s);
        box_reflect_progress(r, p, s, capacity, gpu, app && app->headless, status);
        box_reflect_fade(s, it->delta_time);
        box_reflect_list(r, s);
        box_reflect_send(r, p, s);
        box_reflect_status(it, s);
    }

    box_reflect_report(world, r);
    box_reflect_views(world, r, p, false);
}

/** Free the state of the reflection probe extract.
 *
 * @param ctx The state
 */
static void box_reflect_free(
    void *ctx)
{
    box_render_reflect_t *r = ctx;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&r->scenes); i ++) {
        box_reflect_scene_fini(ecs_vec_get_t(&r->scenes, box_reflect_scene_t, i));
    }

    ecs_vec_fini_t(NULL, &r->scenes, box_reflect_scene_t);
    ecs_vec_fini_t(NULL, &r->free_buffers, uint32_t);
    ecs_vec_fini_t(NULL, &r->ranks, box_reflect_rank_t);
    ecs_vec_fini_t(NULL, &r->results, box_render_bake_result_t);
    ecs_map_fini(&r->light_map);
    ecs_os_free(r);
}

/** Create a query of a component, the WorldTransform of its entity when asked, and the optional
 * Scene of its entity or an ancestor.
 *
 * @param world The world
 * @param component The component
 * @param transform Whether the WorldTransform comes between them, which the entity needs
 * @param name The name of the query entity
 * @return The query
 */
static ecs_query_t* box_reflect_query(
    ecs_world_t *world,
    ecs_entity_t component,
    bool transform,
    const char *name)
{
    ecs_query_desc_t desc = {
        .entity = ecs_entity(world, { .name = name }),
        .terms = {{ .id = component, .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto
    };
    int32_t scene = 1;
    if (transform) {
        desc.terms[1] = (ecs_term_t){
            .id = ecs_id(WorldTransform), .inout = EcsIn
        };
        scene = 2;
    }

    desc.terms[scene] = (ecs_term_t){
        .id = ecs_id(Scene),
        .src.id = EcsSelf | EcsUp,
        .trav = EcsChildOf,
        .inout = EcsInOutNone,
        .oper = EcsOptional
    };

    return ecs_query_init(world, &desc);
}

void box_render_reflect_init(
    ecs_world_t *world)
{
    box_render_reflect_t *r = ecs_os_calloc_t(box_render_reflect_t);
    ecs_vec_init_t(NULL, &r->scenes, box_reflect_scene_t, 0);
    ecs_vec_init_t(NULL, &r->free_buffers, uint32_t, 0);
    ecs_vec_init_t(NULL, &r->ranks, box_reflect_rank_t, 0);
    ecs_vec_init_t(NULL, &r->results, box_render_bake_result_t, 0);
    ecs_map_init(&r->light_map, NULL);
    r->next_buffer = BOX_RENDER_BUFFER_REFLECT_FIRST;
    r->probe_ms = BOX_REFLECT_PROBE_MS;
    r->bakes = box_reflect_query(world, ecs_id(ReflectionBake), false, "ReflectionBakes");
    r->probes = box_reflect_query(world, ecs_id(ReflectionProbe), true, "ReflectionProbes");

    ecs_set_name_prefix(world, "Render");
    ECS_COMPONENT_DEFINE(world, RenderReflectSystem);
    ecs_set_name_prefix(world, NULL);
    ecs_entity_t extract = ecs_system(world, {
        .entity = ecs_entity(world, { .name = "extract.Reflect" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractReflect,
        .ctx = r,
        .ctx_free = box_reflect_free
    });
    ecs_singleton_set(world, RenderReflectSystem, { extract });
}

/** Get the state of the reflection probe extract of a world.
 *
 * @param world The world
 * @return The state, NULL without the extract
 */
static const box_render_reflect_t* box_reflect_state(
    const ecs_world_t *world)
{
    const ecs_system_t *sys = box_reflect_system_ctx(world);

    return sys ? sys->ctx : NULL;
}

void box_render_reflect_shift(
    ecs_world_t *world,
    ecs_entity_t scene,
    const vec3 offset)
{
    box_render_reflect_t *r = ECS_CONST_CAST(box_render_reflect_t*, box_reflect_state(world));
    box_reflect_scene_t *s = r ? box_reflect_find(r, scene) : NULL;
    int32_t i;
    if (r) {
        r->shifting = true;
    }

    if (!s) {
        goto done;
    }

    glm_vec3_add(s->scene_capture, ECS_CONST_CAST(float*, offset), s->scene_capture);
    glm_vec3_add(s->anchor, ECS_CONST_CAST(float*, offset), s->anchor);
    glm_vec3_add(s->min, ECS_CONST_CAST(float*, offset), s->min);
    glm_vec3_add(s->max, ECS_CONST_CAST(float*, offset), s->max);
    for (i = 0; i < ecs_vec_count(&s->cameras); i ++) {
        float *camera = *ecs_vec_get_t(&s->cameras, vec3, i);
        glm_vec3_add(camera, ECS_CONST_CAST(float*, offset), camera);
    }

    for (i = 0; i < ecs_vec_count(&s->probes); i ++) {
        box_reflect_probe_t *probe = ecs_vec_get_t(&s->probes, box_reflect_probe_t, i);
        glm_vec3_add(probe->data.center, ECS_CONST_CAST(float*, offset), probe->data.center);
        glm_vec3_add(probe->data.capture, ECS_CONST_CAST(float*, offset), probe->data.capture);
    }

done:
    return;
}

const box_render_reflection_probe_t* box_render_reflect_probes(
    const ecs_world_t *world,
    ecs_entity_t scene,
    int32_t *count)
{
    const box_render_reflect_t *r = box_reflect_state(world);
    const box_reflect_scene_t *s = r ? box_reflect_find(r, scene) : NULL;
    *count = s ? ecs_vec_count(&s->list) : 0;

    return s ? ecs_vec_first(&s->list) : NULL;
}

uint32_t box_render_reflect_texture(
    const ecs_world_t *world,
    ecs_entity_t scene)
{
    const box_render_reflect_t *r = box_reflect_state(world);
    const box_reflect_scene_t *s = r ? box_reflect_find(r, scene) : NULL;

    return s && s->cubes ? s->buffer : 0;
}

int32_t box_render_reflect_pending(
    const ecs_world_t *world)
{
    const box_render_reflect_t *r = box_reflect_state(world);

    return r ? r->pending : 0;
}

int32_t box_render_reflect_queue(
    const ecs_world_t *world,
    ecs_entity_t scene,
    ecs_entity_t *out,
    int32_t max)
{
    box_render_reflect_t *r = ECS_CONST_CAST(box_render_reflect_t*, box_reflect_state(world));
    const box_reflect_scene_t *s = r ? box_reflect_find(r, scene) : NULL;
    int32_t i, n = 0;
    if (!s) {
        goto done;
    }

    const box_reflect_probe_t *probes = ecs_vec_first(&s->probes);
    box_reflect_rank_t *ranks = box_reflect_rank(r, s, true);
    for (i = 0; i < ecs_vec_count(&r->ranks); i ++) {
        const box_reflect_probe_t *probe = &probes[ranks[i].index];
        if (probe->queued || (!probe->captured && !probe->packet)) {
            ranks[n ++] = ranks[i];
        }
    }

    qsort(ranks, (size_t)n, sizeof(box_reflect_rank_t), box_reflect_capture_cmp);
    for (i = 0; out && i < n && i < max; i ++) {
        out[i] = ranks[i].entity;
    }

done:
    return n;
}

void box_render_reflect_settle(
    ecs_world_t *world,
    ecs_entity_t scene)
{
    box_render_reflect_t *r = ECS_CONST_CAST(box_render_reflect_t*, box_reflect_state(world));
    box_reflect_scene_t *s = r ? box_reflect_find(r, scene) : NULL;
    int32_t i;
    for (i = 0; s && i < ecs_vec_count(&s->probes); i ++) {
        box_reflect_probe_t *probe = ecs_vec_get_t(&s->probes, box_reflect_probe_t, i);
        if (probe->slot >= 0) {
            probe->captured = true;
            probe->queued = false;
            probe->packet = 0;
        }
    }
}
