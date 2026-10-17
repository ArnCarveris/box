/**
 * @file render/reflect.c
 * @brief Reflection probe registry of every scene with ReflectionBake and its probe buffers.
 *
 * Each probe of a scene keeps its cubemap slot while it is resident; the resident probes are
 * uploaded to the reflection probe buffer of the scene in blend order when they change.
 */

#include "render.h"
#include "../spatial/spatial.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

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

/** A probe of a scene */
typedef struct box_reflect_probe_t {
    ecs_entity_t entity;    /**< Entity with the ReflectionProbe, 0 for the scene probe */
    box_render_reflection_probe_t data; /**< Boxes and capture point, slot when resident */
    int32_t slot;           /**< Cubemap slot, -1 when the probe is not resident */
    bool seen;              /**< The probe exists in this frame */
} box_reflect_probe_t;

/** The probes of one scene with ReflectionBake */
typedef struct box_reflect_scene_t {
    ecs_entity_t scene;     /**< Scene root, 0 for the default scene */
    ecs_entity_t entity;    /**< Entity with the ReflectionBake of the scene */
    bool seen;              /**< A ReflectionBake of the scene exists in this frame */
    bool placed;            /**< The scene probe was placed at least once */
    bool sent;              /**< The backend has the buffer with the probes of sent */
    float height;           /**< ReflectionBake.scene_probe_height the scene probe was placed at */
    vec3 scene_capture;     /**< Capture point of the scene probe */
    ecs_vec_t probes;       /**< box_reflect_probe_t */
    ecs_map_t lookup;       /**< Entity of a ReflectionProbe to its index in probes */
    int32_t scene_index;    /**< Index of the scene probe in probes, -1 for none */
    ecs_vec_t slots;        /**< uint8_t per slot, 1 while a probe has it */
    int32_t resident;       /**< Probes with a slot */
    ecs_vec_t cameras;      /**< vec3, cameras of the views of the scene in this frame */
    ecs_vec_t list;         /**< box_render_reflection_probe_t, resident probes in blend order */
    ecs_vec_t sent_list;    /**< box_render_reflection_probe_t the backend has */
    uint32_t buffer;        /**< Handle of the reflection probe buffer, 0 for none */
    uint32_t capacity;      /**< Probes the buffer holds, 0 while the backend lacks it */
    const char *error;      /**< Why the scene captures nothing, NULL for none */
} box_reflect_scene_t;

/** A probe ranked for a slot or for the blend order */
typedef struct box_reflect_rank_t {
    int32_t index;          /**< Index of the probe in the probes of its scene */
    int32_t priority;       /**< ReflectionProbe.priority */
    bool scene;             /**< The probe is the scene probe */
    float distance;         /**< Squared distance to the nearest camera of the scene */
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
    const void *backend;    /**< Context of the backend the buffers were sent to */
    void (*render)(void*, const box_render_packet_t*); /**< Render callback of that backend */
    int32_t generation;     /**< Generation of that backend, see box_render_backend_t */
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
        ecs_vec_init_t(NULL, &result->probes, box_reflect_probe_t, 0);
        ecs_vec_init_t(NULL, &result->slots, uint8_t, 0);
        ecs_vec_init_t(NULL, &result->cameras, vec3, 0);
        ecs_vec_init_t(NULL, &result->list, box_render_reflection_probe_t, 0);
        ecs_vec_init_t(NULL, &result->sent_list, box_render_reflection_probe_t, 0);
        ecs_map_init(&result->lookup, NULL);
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
}

/** Destroy the reflection probe buffer of a scene and free its handle.
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

/** Free the slot of a probe when it has one.
 *
 * @param s The scene
 * @param probe The probe
 */
static void box_reflect_evict(
    box_reflect_scene_t *s,
    box_reflect_probe_t *probe)
{
    if (probe->slot >= 0) {
        *ecs_vec_get_t(&s->slots, uint8_t, probe->slot) = 0;
        probe->slot = -1;
        s->resident --;
    }
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
 * @param transform World transform of the entity, NULL for the origin
 */
static void box_reflect_place(
    box_render_reflection_probe_t *out,
    const ReflectionProbe *probe,
    const WorldTransform *transform)
{
    mat4 m = GLM_MAT4_IDENTITY_INIT;
    int32_t i, k;
    if (transform) {
        glm_mat4_copy(ECS_CONST_CAST(vec4*, transform->m), m);
    }

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

/** Order probes for blending: higher priority first, the scene probe last.
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

/** Collect the resident probes of a scene in blend order.
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
        *out = probes[ranks[i].index].data;
        out->slot = (uint32_t)probes[ranks[i].index].slot;
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

    if (!s->buffer) {
        if (ecs_vec_count(&r->free_buffers)) {
            s->buffer = *ecs_vec_last_t(&r->free_buffers, uint32_t);
            ecs_vec_remove_last(&r->free_buffers);
        } else {
            s->buffer = r->next_buffer ++;
        }
    }

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
    int32_t probes = ecs_vec_count(&s->probes), captured = 0;
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
            if (!bake->scene_probe) {
                continue;
            }

            if (changed || !s->placed || s->height != bake->scene_probe_height) {
                box_reflect_place_scene(world, s, bake->scene_probe_height);
            }

            box_reflect_probe_t *probe = s->scene_index >= 0
                ? ecs_vec_get_t(&s->probes, box_reflect_probe_t, s->scene_index)
                : box_reflect_add_probe(s, 0);
            box_reflect_scene_data(s, &probe->data);
            probe->seen = true;
        }
    }

    for (i = ecs_vec_count(&r->scenes) - 1; i >= 0; i --) {
        if (!ecs_vec_get_t(&r->scenes, box_reflect_scene_t, i)->seen) {
            box_reflect_remove_scene(r, p, i);
        }
    }
}

/** Take the ReflectionProbe of every entity in a scene with ReflectionBake.
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
            box_reflect_place(&probe->data, &probes[row], transforms ? &transforms[row] : NULL);
            probe->seen = true;
        }
    }
}

/** Collect the cameras of the views of every scene and point the views at the reflection
 * probe buffer of their scene.
 *
 * @param r The state
 * @param p The packet
 * @param cameras Collect the cameras, else point the views at the buffers
 */
static void box_reflect_views(
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
        } else if (s->sent && ecs_vec_count(&s->list)) {
            rv->reflections = s->buffer;
            rv->reflection_count = (uint32_t)ecs_vec_count(&s->list);
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

/** Keep the probes of every scene with ReflectionBake, give them slots and upload them.
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
        }
    }

    for (i = 0; i < ecs_vec_count(&r->scenes); i ++) {
        box_reflect_scene_t *s = ecs_vec_get_t(&r->scenes, box_reflect_scene_t, i);
        for (j = 0; j < ecs_vec_count(&s->probes); j ++) {
            ecs_vec_get_t(&s->probes, box_reflect_probe_t, j)->seen = false;
        }
    }

    box_reflect_bakes(world, r, p);
    box_reflect_probes(world, r);
    box_reflect_views(r, p, true);
    for (i = 0; i < ecs_vec_count(&r->scenes); i ++) {
        box_reflect_scene_t *s = ecs_vec_get_t(&r->scenes, box_reflect_scene_t, i);
        box_reflect_prune(s);
        box_reflect_assign(r, s, capacity);
        box_reflect_list(r, s);
        box_reflect_send(r, p, s);
        box_reflect_status(it, s);
    }

    box_reflect_views(r, p, false);
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
    ecs_os_free(r);
}

/** Create a query of a component and the optional Scene of its entity or an ancestor.
 *
 * @param world The world
 * @param component The component
 * @param transform Whether the optional WorldTransform comes between them
 * @return The query
 */
static ecs_query_t* box_reflect_query(
    ecs_world_t *world,
    ecs_entity_t component,
    bool transform)
{
    ecs_query_desc_t desc = {
        .terms = {{ .id = component, .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto
    };
    int32_t scene = 1;
    if (transform) {
        desc.terms[1] = (ecs_term_t){
            .id = ecs_id(WorldTransform), .inout = EcsIn, .oper = EcsOptional
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
    r->next_buffer = BOX_RENDER_BUFFER_REFLECT_FIRST;
    r->bakes = box_reflect_query(world, ecs_id(ReflectionBake), false);
    r->probes = box_reflect_query(world, ecs_id(ReflectionProbe), true);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RenderExtractReflect" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractReflect,
        .ctx = r,
        .ctx_free = box_reflect_free
    });
}

const box_render_reflection_probe_t* box_render_reflect_probes(
    const ecs_world_t *world,
    ecs_entity_t scene,
    int32_t *count)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractReflect");
    const ecs_system_t *sys = system ? ecs_system_get(world, system) : NULL;
    const box_reflect_scene_t *s = sys && sys->ctx ? box_reflect_find(sys->ctx, scene) : NULL;
    *count = s ? ecs_vec_count(&s->list) : 0;

    return s ? ecs_vec_first(&s->list) : NULL;
}
