/**
 * @file render/impostor.c
 * @brief Bakes the octahedral atlases of the Impostor components and draws them on cards.
 *
 * An Impostor gets a scene of its own with a copy of its hierarchy at the origin. A view of a
 * bake is three captures of that scene into targets that exist for one packet, unlit, one per
 * channel (box_render_view_t.impostor), which the backend copies into the atlases.
 *
 * A bake swaps in its atlases after its last view; a change of the source makes a new copy and
 * bakes again. Every instance of the prefab of a ready bake gets a card beyond Impostor.distance
 * (ImpostorDraw), a slot of the draws that no entity holds, and its shapes end there.
 */

#include "render.h"
#include "../geometry/mesh_cache.h"
#include "../spatial/spatial.h"
#include <float.h>
#include <math.h>

/** Target handles the impostor views cycle through */
#define BOX_IMPOSTOR_TARGETS (0x08000000u)

/** Frames a new copy waits before its bounds are read: its shapes resolve and enter the
 * spatial index in the frames after it is made */
#define BOX_IMPOSTOR_SETTLE (2)

/** Default ImpostorBudget.frames */
#define BOX_IMPOSTOR_BUDGET (8)

/** Smallest Impostor.resolution */
#define BOX_IMPOSTOR_RESOLUTION_MIN (16)

/** Deepest hierarchy or prefab chain the hash walks */
#define BOX_IMPOSTOR_DEPTH_MAX (32)

/** Roughness of the material of the cards, which the atlases do not hold */
#define BOX_IMPOSTOR_ROUGHNESS (0.8f)

/** Clear color of the image of each channel: no albedo, a normal facing nowhere and the far
 * plane, all uncovered */
static const float box_impostor_clear[BOX_RENDER_IMPOSTOR_CHANNELS][4] = {
    {0.0f, 0.0f, 0.0f, 0.0f},
    {0.5f, 0.5f, 0.5f, 0.0f},
    {1.0f, 1.0f, 1.0f, 0.0f}
};

/** Bake state of one Impostor */
typedef struct box_impostor_t {
    ecs_entity_t scene;         /**< Scene root of the copy */
    ecs_entity_t copy;          /**< Root of the copy of the hierarchy */
    uint64_t hash;              /**< Hash of the source when the copy was made */
    bool rebake;                /**< box_impostor_rebake asked for a bake */
    int32_t settle;             /**< Frames until the bounds of the copy are read */
    bool baking;                /**< A bake is in progress */
    int32_t next;               /**< Next view of the bake in progress */
    int32_t frames;             /**< Views per axis of the bake in progress */
    int32_t resolution;         /**< Pixels of a view of the bake in progress */
    bool hemisphere;            /**< The bake in progress covers the upper hemisphere */
    uint32_t front[BOX_RENDER_IMPOSTOR_CHANNELS]; /**< Atlases of the last bake, 0 for none */
    uint32_t back[BOX_RENDER_IMPOSTOR_CHANNELS];  /**< Atlases of the bake in progress */
    vec3 min;                   /**< Bounds of the copy the bake in progress frames */
    vec3 max;                   /**< Bounds of the copy the bake in progress frames */
    ecs_time_t started;         /**< When the first view of the bake in progress was sent */
    ImpostorStatus status;      /**< Status last written */
    bool status_dirty;          /**< The status changed since it was written */
    ecs_entity_t card;          /**< Prefab with the mesh and material of the cards, 0 for none */
    ecs_map_t cards;            /**< Instances of the prefab with a card, mapped to 1 */
    bool dirty;                 /**< Instances of the prefab were added or removed since the
                                 *   cards were last matched to them */
    vec3 extent;                /**< Half extents of the bounds the last bake framed */
    int32_t card_revision;      /**< Bake revision whose grid the card mesh holds */
    float distance;             /**< Impostor.distance the ranges of the cards have, -1 for none */
    float fade;                 /**< Impostor.fade the ranges of the cards have */
} box_impostor_t;

/** An atlas to write to a file */
typedef struct box_impostor_read_t {
    uint32_t atlas;             /**< Texture handle */
    char *path;                 /**< File, owned */
} box_impostor_read_t;

/** Impostor state of a world */
typedef struct box_impostors_t {
    ecs_map_t impostors;        /**< Entity to box_impostor_t* */
    ecs_map_t watched;          /**< Components the hash reads, mapped to 1 */
    ecs_vec_t sent;             /**< uint32_t, targets the previous packet created */
    ecs_vec_t released;         /**< uint32_t, atlases to destroy in the next packet */
    ecs_vec_t reads;            /**< box_impostor_read_t to add to the next packet */
    ecs_vec_t scratch;          /**< ecs_entity_t, instances that the scan of the cards found */
    ecs_query_t *query;         /**< Impostor, prefabs included */
    uint32_t next_texture;      /**< Counter of atlas handles */
    uint32_t next_target;       /**< Counter of target handles */
    int32_t generation;         /**< Backend generation of the last packet */
} box_impostors_t;

/** Singleton that owns the impostor state of a world */
typedef struct RenderImpostors {
    box_impostors_t *impostors; /**< The state */
} RenderImpostors;

static ECS_COMPONENT_DECLARE(RenderImpostors);

/** Free the state of an impostor, queueing its atlases for release.
 *
 * @param s The impostor state of the world
 * @param imp The impostor
 */
static void box_impostor_free(
    box_impostors_t *s,
    box_impostor_t *imp)
{
    int32_t c;
    for (c = 0; c < BOX_RENDER_IMPOSTOR_CHANNELS; c ++) {
        if (imp->front[c]) {
            *ecs_vec_append_t(NULL, &s->released, uint32_t) = imp->front[c];
        }

        if (imp->back[c]) {
            *ecs_vec_append_t(NULL, &s->released, uint32_t) = imp->back[c];
        }
    }

    ecs_map_fini(&imp->cards);
    ecs_os_free(imp);
}

/** Free the impostor states of the singletons.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_render_impostors_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    RenderImpostors *c = ptr;
    int32_t i, j;
    (void)ti;
    for (i = 0; i < count; i ++) {
        box_impostors_t *s = c[i].impostors;
        if (!s) {
            continue;
        }

        ecs_map_iter_t it = ecs_map_iter(&s->impostors);
        while (ecs_map_next(&it)) {
            box_impostor_free(s, ecs_map_ptr(&it));
        }

        for (j = 0; j < ecs_vec_count(&s->reads); j ++) {
            ecs_os_free(ecs_vec_get_t(&s->reads, box_impostor_read_t, j)->path);
        }

        ecs_map_fini(&s->impostors);
        ecs_map_fini(&s->watched);
        ecs_vec_fini_t(NULL, &s->sent, uint32_t);
        ecs_vec_fini_t(NULL, &s->released, uint32_t);
        ecs_vec_fini_t(NULL, &s->reads, box_impostor_read_t);
        ecs_vec_fini_t(NULL, &s->scratch, ecs_entity_t);
        ecs_os_free(s);
        c[i].impostors = NULL;
    }
}

/** Move impostor states to other singletons.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_render_impostors_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    RenderImpostors *d = dst;
    RenderImpostors *s = src;
    int32_t i;
    for (i = 0; i < count; i ++) {
        if (d[i].impostors != s[i].impostors) {
            box_render_impostors_dtor(&d[i], 1, ti);
        }

        d[i].impostors = s[i].impostors;
        s[i].impostors = NULL;
    }
}

/** Get the impostor state of a world.
 *
 * @param world The world
 * @return The state, NULL when box.render is not imported
 */
static box_impostors_t* box_impostors_get(
    const ecs_world_t *world)
{
    const RenderImpostors *c = ecs_id(RenderImpostors)
        ? ecs_singleton_get(world, RenderImpostors)
        : NULL;

    return c ? c->impostors : NULL;
}

/** Set the defaults of Impostor.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_impostor_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Impostor *values = ptr;
    int32_t i;
    ecs_os_memset(ptr, 0, ti->size * count);
    for (i = 0; i < count; i ++) {
        values[i].frames = 8;
        values[i].resolution = 128;
        values[i].hemisphere = true;
        values[i].distance = 100.0f;
        values[i].fade = 10.0f;
    }
}

/** Set the default of ImpostorBudget.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_impostor_budget_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ImpostorBudget *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i].frames = BOX_IMPOSTOR_BUDGET;
    }
}

/** Clamp an integer to a range.
 *
 * @param v The value
 * @param lo Smallest result
 * @param hi Largest result
 * @return The clamped value
 */
static int32_t box_impostor_clamp(
    int32_t v,
    int32_t lo,
    int32_t hi)
{
    return glm_imax(lo, glm_imin(v, hi));
}

/** Sign of a value, 1 for 0.
 *
 * @param v The value
 * @return 1 or -1
 */
static float box_impostor_sign(
    float v)
{
    return v >= 0.0f ? 1.0f : -1.0f;
}

void box_impostor_frame_direction(
    int32_t frames,
    bool hemisphere,
    int32_t x,
    int32_t y,
    vec3 out)
{
    float n = frames > 1 ? (float)(frames - 1) : 1.0f;
    float ex = 2.0f * (float)x / n - 1.0f;
    float ey = 2.0f * (float)y / n - 1.0f;
    if (hemisphere) {
        float tx = (ex + ey) * 0.5f;
        float ty = (ex - ey) * 0.5f;
        out[0] = tx;
        out[1] = 1.0f - fabsf(tx) - fabsf(ty);
        out[2] = ty;
    } else {
        out[0] = ex;
        out[1] = 1.0f - fabsf(ex) - fabsf(ey);
        out[2] = ey;
        if (out[1] < 0.0f) {
            float ox = out[0];
            out[0] = (1.0f - fabsf(out[2])) * box_impostor_sign(ox);
            out[2] = (1.0f - fabsf(ox)) * box_impostor_sign(out[2]);
        }
    }

    glm_vec3_normalize(out);
}

void box_impostor_frame_of(
    int32_t frames,
    bool hemisphere,
    const vec3 dir,
    int32_t *x,
    int32_t *y)
{
    vec3 d = { dir[0], dir[1], dir[2] };
    float ex, ey, n = frames > 1 ? (float)(frames - 1) : 1.0f;
    float l1;
    if (hemisphere && d[1] < 0.0f) {
        d[1] = 0.0f;
    }

    l1 = fabsf(d[0]) + fabsf(d[1]) + fabsf(d[2]);
    if (l1 <= 0.0f) {
        d[1] = 1.0f;
        l1 = 1.0f;
    }

    glm_vec3_scale(d, 1.0f / l1, d);
    if (hemisphere) {
        ex = d[0] + d[2];
        ey = d[0] - d[2];
    } else if (d[1] < 0.0f) {
        ex = (1.0f - fabsf(d[2])) * box_impostor_sign(d[0]);
        ey = (1.0f - fabsf(d[0])) * box_impostor_sign(d[2]);
    } else {
        ex = d[0];
        ey = d[2];
    }

    *x = box_impostor_clamp((int32_t)floorf((ex + 1.0f) * 0.5f * n + 0.5f), 0, frames - 1);
    *y = box_impostor_clamp((int32_t)floorf((ey + 1.0f) * 0.5f * n + 0.5f), 0, frames - 1);
}

/** Add the components of a module to the components the hash reads.
 *
 * @param world The world
 * @param s The impostor state
 * @param path Path of the module
 */
static void box_impostor_watch_module(
    ecs_world_t *world,
    box_impostors_t *s,
    const char *path)
{
    ecs_entity_t module = ecs_lookup(world, path);
    if (!module) {
        goto done;
    }

    ecs_iter_t it = ecs_children(world, module);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            const EcsComponent *c = ecs_get(world, e, EcsComponent);
            if (c && c->size > 0 && e != ecs_id(MaterialId) && e != ecs_id(ShapeMesh)) {
                ecs_map_insert(&s->watched, e, 1);
            }
        }
    }

done:
    return;
}

/** Check whether a component is a pose of an entity.
 *
 * @param id The component
 * @return Whether it is Position, Rotation or Scale
 */
static bool box_impostor_is_pose(
    ecs_id_t id)
{
    return id == ecs_id(Position) || id == ecs_id(Rotation) || id == ecs_id(Scale);
}

/** Hash bytes into a hash, FNV-1a.
 *
 * @param h The hash
 * @param data The bytes
 * @param size Number of bytes
 * @return The new hash
 */
static uint64_t box_impostor_fnv(
    uint64_t h,
    const void *data,
    ecs_size_t size)
{
    const uint8_t *bytes = data;
    ecs_size_t i;
    for (i = 0; i < size; i ++) {
        h ^= bytes[i];
        h *= 1099511628211ull;
    }

    return h;
}

/** Hash the images of the textures a component refers to by its entity members, such as the
 * maps of PbrTextures: the state and revision of their loads and their size, so that a texture
 * that loads or changes bakes again.
 *
 * @param world The world
 * @param id The component
 * @param ptr Its value
 * @param h The hash
 * @return The new hash
 */
static uint64_t box_impostor_hash_textures(
    const ecs_world_t *world,
    ecs_id_t id,
    const void *ptr,
    uint64_t h)
{
    const EcsStruct *st = ecs_get(world, id, EcsStruct);
    int32_t i, k, count = st ? ecs_vec_count(&st->members) : 0;
    for (i = 0; i < count; i ++) {
        const ecs_member_t *m = ecs_vec_get_t(&st->members, ecs_member_t, i);
        if (m->type != ecs_id(ecs_entity_t)) {
            continue;
        }

        for (k = 0; k < glm_imax(m->count, 1); k ++) {
            ecs_entity_t t = ((const ecs_entity_t*)ECS_OFFSET(ptr, m->offset))[k];
            const TextureStatus *status = t && ecs_is_alive(world, t)
                ? ecs_get(world, t, TextureStatus) : NULL;
            const Texture *size = status ? ecs_get(world, t, Texture) : NULL;
            if (status) {
                h = box_impostor_fnv(h, &status->state, ECS_SIZEOF(status->state));
                h = box_impostor_fnv(h, &status->revision, ECS_SIZEOF(status->revision));
            }

            if (size) {
                h = box_impostor_fnv(h, size, ECS_SIZEOF(Texture));
            }
        }
    }

    return h;
}

/** Hash the watched components of an entity and of its prefabs.
 *
 * @param world The world
 * @param s The impostor state
 * @param e The entity
 * @param h The hash
 * @param root Whether the entity is the root of the source, whose pose the bake leaves out
 * @param depth Prefabs above the entity walked
 * @return The new hash
 */
static uint64_t box_impostor_hash_entity(
    const ecs_world_t *world,
    const box_impostors_t *s,
    ecs_entity_t e,
    uint64_t h,
    bool root,
    int32_t depth)
{
    const ecs_type_t *type = ecs_get_type(world, e);
    int32_t i;
    if (!type) {
        goto done;
    }

    for (i = 0; i < type->count; i ++) {
        ecs_id_t id = type->array[i];
        if (ECS_IS_PAIR(id)) {
            if (ECS_PAIR_FIRST(id) == EcsIsA && depth < BOX_IMPOSTOR_DEPTH_MAX) {
                ecs_entity_t base = ecs_pair_second(world, id);
                h = box_impostor_fnv(h, &base, ECS_SIZEOF(base));
                h = box_impostor_hash_entity(world, s, base, h, root, depth + 1);
            }

            continue;
        }

        if (!ecs_map_get(&s->watched, id) || (root && box_impostor_is_pose(id))) {
            continue;
        }

        const ecs_type_info_t *ti = ecs_get_type_info(world, id);
        const void *ptr = ecs_get_id(world, e, id);
        h = box_impostor_fnv(h, &id, ECS_SIZEOF(id));
        if (ti && ptr) {
            h = box_impostor_fnv(h, ptr, ti->size);
            h = box_impostor_hash_textures(world, id, ptr, h);
        }
    }

done:
    return h;
}

/** Hash an entity and the hierarchy under it.
 *
 * @param world The world
 * @param s The impostor state
 * @param e The entity
 * @param h The hash
 * @param root Whether the entity is the root of the source
 * @param depth Depth of the entity in the hierarchy
 * @return The new hash
 */
static uint64_t box_impostor_hash_tree(
    const ecs_world_t *world,
    const box_impostors_t *s,
    ecs_entity_t e,
    uint64_t h,
    bool root,
    int32_t depth)
{
    h = box_impostor_fnv(h, &e, ECS_SIZEOF(e));
    h = box_impostor_hash_entity(world, s, e, h, root, 0);
    if (depth >= BOX_IMPOSTOR_DEPTH_MAX) {
        goto done;
    }

    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            h = box_impostor_hash_tree(world, s, it.entities[i], h, false, depth + 1);
        }
    }

done:
    return h;
}

/** Clone an entity that is not a prefab with the hierarchy under it.
 *
 * @param world The world
 * @param src The entity
 * @param parent Parent of the clone, 0 to keep the one of the entity
 * @param depth Depth of the entity in the hierarchy
 * @return The clone
 */
static ecs_entity_t box_impostor_clone(
    ecs_world_t *world,
    ecs_entity_t src,
    ecs_entity_t parent,
    int32_t depth)
{
    ecs_entity_t result = ecs_clone(world, 0, src, true);
    if (parent) {
        ecs_add_pair(world, result, EcsChildOf, parent);
    }

    if (depth >= BOX_IMPOSTOR_DEPTH_MAX) {
        goto done;
    }

    ecs_iter_t it = ecs_children(world, src);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            box_impostor_clone(world, it.entities[i], result, depth + 1);
        }
    }

done:
    return result;
}

/** Make the scene of an impostor, with the material features of the scene of its source.
 *
 * The scene draws unlit, so the lighting features are off, and without Wind, TextureScroll,
 * Fresnel and the transmissive features, which would make the views differ.
 *
 * @param world The world
 * @param source The entity with Impostor
 * @return The Scene root
 */
static ecs_entity_t box_impostor_scene(
    ecs_world_t *world,
    ecs_entity_t source)
{
    ecs_entity_t scene = box_render_scene_of(world, source);
    const Scene *from = scene ? ecs_get(world, scene, Scene) : ecs_singleton_get(world, Scene);
    Scene features = {0};
    if (from) {
        const ShaderFeatures *m = &from->shader_features.Extended;
        ShaderFeatures *to = &features.shader_features.Extended;
        to->alpha_cutout = m->alpha_cutout;
        to->blend_modes = m->blend_modes;
        to->texture_blend = m->texture_blend;
        to->triplanar = m->triplanar;
        to->texture_layers = m->texture_layers;
        to->sheen = m->sheen;
        to->clear_coat = m->clear_coat;
        to->anisotropy = m->anisotropy;
        to->diffuse_transmission = m->diffuse_transmission;
    }

    ecs_entity_t result = ecs_new(world);
    ecs_set_ptr(world, result, Scene, &features);

    return result;
}

/** Make the copy of the hierarchy of an impostor in its scene, at the origin.
 *
 * @param world The world
 * @param source The entity with Impostor
 * @param scene The scene of the impostor
 * @return The root of the copy
 */
static ecs_entity_t box_impostor_copy(
    ecs_world_t *world,
    ecs_entity_t source,
    ecs_entity_t scene)
{
    ecs_entity_t result;
    if (ecs_has_id(world, source, EcsPrefab)) {
        result = ecs_new_w_pair(world, EcsIsA, source);
        ecs_add_pair(world, result, EcsChildOf, scene);
    } else {
        result = box_impostor_clone(world, source, scene, 0);
        ecs_remove(world, result, Impostor);
        ecs_remove(world, result, ImpostorStatus);
    }

    ecs_set(world, result, Position, {0, 0, 0});
    if (ecs_has(world, source, Rotation)) {
        ecs_set(world, result, Rotation, {0, 0, 0});
    }

    if (ecs_has(world, source, Scale)) {
        ecs_set(world, result, Scale, {1, 1, 1});
    }

    return result;
}

/** Add the bounds the spatial index holds for an entity and the hierarchy under it.
 *
 * @param world The world
 * @param e The entity
 * @param min The minimum corner, grown
 * @param max The maximum corner, grown
 * @param depth Depth of the entity in the hierarchy
 * @return Whether an entity of the hierarchy has bounds
 */
static bool box_impostor_bounds(
    const ecs_world_t *world,
    ecs_entity_t e,
    vec3 min,
    vec3 max,
    int32_t depth)
{
    vec3 lo, hi;
    bool result = false;
    if (box_spatial_bounds(world, e, NULL, lo, hi)) {
        glm_vec3_minv(min, lo, min);
        glm_vec3_maxv(max, hi, max);
        result = true;
    }

    if (depth >= BOX_IMPOSTOR_DEPTH_MAX) {
        goto done;
    }

    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            result |= box_impostor_bounds(world, it.entities[i], min, max, depth + 1);
        }
    }

done:
    return result;
}

/** Clamp the grid of an Impostor to what the bake supports.
 *
 * @param in The component
 * @param frames Receives the views per axis
 * @param resolution Receives the pixels of a view
 */
static void box_impostor_grid(
    const Impostor *in,
    int32_t *frames,
    int32_t *resolution)
{
    *frames = box_impostor_clamp(in->frames, 2, BOX_IMPOSTOR_FRAMES_MAX);
    *resolution = glm_imax(in->resolution, BOX_IMPOSTOR_RESOLUTION_MIN);
    if (*frames * *resolution > BOX_IMPOSTOR_ATLAS_MAX) {
        *resolution = BOX_IMPOSTOR_ATLAS_MAX / *frames;
    }
}

/** Start a bake of an impostor from its first view.
 *
 * @param imp The impostor
 */
static void box_impostor_restart(
    box_impostor_t *imp)
{
    imp->baking = true;
    imp->next = 0;
    imp->status.baking = true;
    imp->status.baked = 0;
    imp->status_dirty = true;
}

/** Follow the changes of an impostor: a source that changed gets a new copy, and a change of
 * the source or of the grid starts a bake.
 *
 * @param world The world
 * @param s The impostor state
 * @param e The entity with Impostor
 * @param value The Impostor
 */
static void box_impostor_update(
    ecs_world_t *world,
    box_impostors_t *s,
    ecs_entity_t e,
    const Impostor *value)
{
    box_impostor_t *imp = ecs_map_get_deref(&s->impostors, box_impostor_t, e);
    int32_t frames, resolution;
    uint64_t hash = box_impostor_hash_tree(world, s, e, 14695981039346656037ull, true, 0);
    if (!imp) {
        imp = ecs_os_calloc_t(box_impostor_t);
        ecs_map_insert_ptr(&s->impostors, e, imp);
        ecs_map_init(&imp->cards, NULL);
        imp->status_dirty = true;
        imp->dirty = true;
        imp->distance = -1.0f;
    }

    if (!imp->scene || !ecs_is_alive(world, imp->scene)) {
        imp->scene = box_impostor_scene(world, e);
        imp->copy = 0;
    }

    if (!imp->copy || !ecs_is_alive(world, imp->copy) || hash != imp->hash || imp->rebake) {
        if (imp->copy && ecs_is_alive(world, imp->copy)) {
            ecs_delete(world, imp->copy);
        }

        imp->copy = box_impostor_copy(world, e, imp->scene);
        imp->hash = hash;
        imp->rebake = false;
        imp->settle = BOX_IMPOSTOR_SETTLE;
        box_impostor_restart(imp);
    }

    box_impostor_grid(value, &frames, &resolution);
    if (frames != imp->frames || resolution != imp->resolution ||
        value->hemisphere != imp->hemisphere)
    {
        imp->frames = frames;
        imp->resolution = resolution;
        imp->hemisphere = value->hemisphere;
        box_impostor_restart(imp);
    }
}

/** Get a new atlas handle.
 *
 * @param s The impostor state
 * @return The handle
 */
static uint32_t box_impostor_texture(
    box_impostors_t *s)
{
    uint32_t result = BOX_RENDER_IMPOSTOR_TEXTURE_FIRST + s->next_texture;
    s->next_texture = (s->next_texture + 1) % (0x80000000u - BOX_RENDER_IMPOSTOR_TEXTURE_FIRST);

    return result;
}

/** Add one view of the bake of an impostor to the packet: a view and a target per channel and
 * the copies of the targets into the atlases.
 *
 * @param world The world
 * @param s The impostor state
 * @param imp The impostor
 * @param p The packet
 * @param index Index of the view, row by row
 */
static void box_impostor_send(
    ecs_world_t *world,
    box_impostors_t *s,
    box_impostor_t *imp,
    box_render_packet_t *p,
    int32_t index)
{
    int32_t res = imp->resolution, frames = imp->frames;
    int32_t x = index % frames, y = index / frames;
    int32_t pad = glm_imax(res / 16, 1);
    vec3 dir, center, eye, up = {0.0f, 1.0f, 0.0f};
    mat4 view_matrix;
    float radius = glm_max(glm_vec3_distance(imp->min, imp->max) * 0.5f, 1e-3f);
    uint32_t c;
    box_impostor_frame_direction(frames, imp->hemisphere, x, y, dir);
    glm_vec3_center(imp->min, imp->max, center);
    glm_vec3_scale(dir, 2.0f * radius, eye);
    glm_vec3_add(center, eye, eye);
    if (fabsf(dir[1]) > 0.999f) {
        glm_vec3_copy((vec3){0.0f, 0.0f, -1.0f}, up);
    }

    glm_lookat(eye, center, up, view_matrix);
    Camera camera = {
        .fov = 60.0f,
        .near_ = radius,
        .far_ = 3.0f * radius,
        .aspect_ratio = 1.0f,
        .ortho = true,
        .ortho_height = 2.0f * radius * (float)res / (float)(res - 2 * pad)
    };

    for (c = 0; c < BOX_RENDER_IMPOSTOR_CHANNELS; c ++) {
        uint32_t handle = BOX_RENDER_IMPOSTOR_TARGET_FIRST + s->next_target;
        s->next_target = (s->next_target + 1) % BOX_IMPOSTOR_TARGETS;
        box_render_target_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_TARGET,
            BOX_RENDER_CREATE, handle, sizeof(box_render_target_desc_t));
        ecs_os_memset_t(desc, 0, box_render_target_desc_t);
        desc->width = desc->render_width = res;
        desc->height = desc->render_height = res;

        box_render_view_t v = {0};
        v.camera_data = camera;
        v.scene = imp->scene;
        v.cull.enabled = true;
        v.cull.bounds = true;
        v.features = box_render_view_features(world, imp->scene, NULL);
        v.scene_features = box_render_scene_features(world, imp->scene);
        glm_mat4_copy(view_matrix, v.view_matrix);
        glm_mat4_inv(view_matrix, v.camera_transform);
        v.target = handle;
        v.impostor = c + 1;
        *box_render_packet_view(p) = v;

        box_render_target_t *target = box_render_packet_target(p, handle, NULL);
        target->exclusive = true;
        ecs_os_memcpy_n(target->clear, box_impostor_clear[c], float, 4);

        box_render_impostor_blit_t *blit = ecs_vec_append_t(
            NULL, &p->impostor_blits, box_render_impostor_blit_t);
        blit->target = handle;
        blit->atlas = imp->back[c];
        blit->channel = c + 1;
        blit->atlas_size = (uint32_t)(frames * res);
        blit->x = (uint32_t)(x * res);
        blit->y = (uint32_t)(y * res);
        blit->dilate = (uint32_t)box_impostor_clamp(res / 8, 2, 16);
        blit->last = index == frames * frames - 1;
        *ecs_vec_append_t(NULL, &s->sent, uint32_t) = handle;
    }
}

/** Finish the bake of an impostor: its atlases become the front ones and its status ready.
 *
 * @param world The world
 * @param s The impostor state
 * @param imp The impostor
 */
static void box_impostor_finish(
    ecs_world_t *world,
    box_impostors_t *s,
    box_impostor_t *imp)
{
    int32_t c;
    (void)world;
    for (c = 0; c < BOX_RENDER_IMPOSTOR_CHANNELS; c ++) {
        if (imp->front[c]) {
            *ecs_vec_append_t(NULL, &s->released, uint32_t) = imp->front[c];
        }

        imp->front[c] = imp->back[c];
        imp->back[c] = 0;
    }

    imp->baking = false;
    imp->status.ready = true;
    imp->status.baking = false;
    imp->status.revision ++;
    imp->status.frames = imp->frames;
    imp->status.resolution = imp->resolution;
    imp->status.atlas_size = imp->frames * imp->resolution;
    imp->status.hemisphere = imp->hemisphere;
    imp->status.bake_time = (float)ecs_time_measure(&imp->started);
    glm_vec3_center(imp->min, imp->max, imp->status.center);
    glm_vec3_sub(imp->max, imp->min, imp->extent);
    glm_vec3_scale(imp->extent, 0.5f, imp->extent);
    imp->status.radius = glm_max(glm_vec3_distance(imp->min, imp->max) * 0.5f, 1e-3f);
    imp->status_dirty = true;
}

/** Send the next views of an impostor that bakes, as many as the budget has left.
 *
 * @param world The world
 * @param s The impostor state
 * @param imp The impostor
 * @param p The packet
 * @param budget Views the frame may still send, lowered by the views sent
 */
static void box_impostor_bake(
    ecs_world_t *world,
    box_impostors_t *s,
    box_impostor_t *imp,
    box_render_packet_t *p,
    int32_t *budget)
{
    vec3 min = {FLT_MAX, FLT_MAX, FLT_MAX}, max = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    int32_t c, views = imp->frames * imp->frames;
    if (imp->settle > 0) {
        imp->settle --;
        goto done;
    }

    if (!imp->copy || !box_impostor_bounds(world, imp->copy, min, max, 0)) {
        goto done;
    }

    if (imp->next && (!glm_vec3_eqv_eps(min, imp->min) || !glm_vec3_eqv_eps(max, imp->max))) {
        box_impostor_restart(imp);
    }

    if (!imp->next) {
        glm_vec3_copy(min, imp->min);
        glm_vec3_copy(max, imp->max);
        ecs_os_get_time(&imp->started);
        for (c = 0; c < BOX_RENDER_IMPOSTOR_CHANNELS; c ++) {
            if (!imp->back[c]) {
                imp->back[c] = box_impostor_texture(s);
            }
        }
    }

    while (*budget > 0 && imp->next < views) {
        box_impostor_send(world, s, imp, p, imp->next);
        imp->next ++;
        (*budget) --;
    }

    imp->status.baked = imp->next;
    imp->status_dirty = true;
    if (imp->next == views) {
        box_impostor_finish(world, s, imp);
    }

done:
    return;
}

/** Get the half size of the card of a bake: the radius of the sphere the views frame, widened
 * by the margin the views leave around it, as box_impostor_send frames them.
 *
 * @param status The status of the bake
 * @return The half size
 */
static float box_impostor_card_half(
    const ImpostorStatus *status)
{
    int32_t res = glm_imax(status->resolution, BOX_IMPOSTOR_RESOLUTION_MIN);
    int32_t pad = glm_imax(res / 16, 1);

    return status->radius * (float)res / (float)(res - 2 * pad);
}

/** Give the card prefab of an impostor the mesh of its last bake.
 *
 * Two triangles whose UVs are the corners of the card and whose per vertex data holds the grid
 * (impostor_grid of impostor_card.wgsl), which the shaders place the corners by.
 *
 * Their positions are corners of the box of the bake, the bounds that the cull pass tests, from
 * which the shaders fit the card to the box as seen along the view (impostor_corner).
 *
 * @param world The world
 * @param imp The impostor
 */
static void box_impostor_card_mesh(
    ecs_world_t *world,
    box_impostor_t *imp)
{
    static const float corners[4][2] = { {0, 0}, {1, 0}, {0, 1}, {1, 1} };
    static const float signs[4][3] = { {-1, -1, -1}, {1, -1, 1}, {-1, 1, -1}, {1, 1, 1} };
    static const uint32_t indices[6] = { 0, 1, 2, 2, 1, 3 };
    const ImpostorStatus *st = &imp->status;
    float half = box_impostor_card_half(st);
    Mesh3 m = {0};
    int32_t i;
    ecs_vec_init_t(NULL, &m.vertices, vec3, 4);
    ecs_vec_init_t(NULL, &m.normals, vec3, 4);
    ecs_vec_init_t(NULL, &m.uvs, vec2, 4);
    ecs_vec_init_t(NULL, &m.data0, vec4, 4);
    ecs_vec_init_t(NULL, &m.data1, vec4, 4);
    ecs_vec_init_t(NULL, &m.indices, uint32_t, 6);
    for (i = 0; i < 4; i ++) {
        float *p = *ecs_vec_append_t(NULL, &m.vertices, vec3);
        p[0] = st->center[0] + signs[i][0] * imp->extent[0];
        p[1] = st->center[1] + signs[i][1] * imp->extent[1];
        p[2] = st->center[2] + signs[i][2] * imp->extent[2];
        glm_vec3_copy((vec3){0.0f, 0.0f, 1.0f}, *ecs_vec_append_t(NULL, &m.normals, vec3));
        float *uv = *ecs_vec_append_t(NULL, &m.uvs, vec2);
        uv[0] = corners[i][0];
        uv[1] = corners[i][1];
        glm_vec4_copy((vec4){(float)st->frames, st->hemisphere ? 1.0f : 0.0f,
            st->radius / half, half}, *ecs_vec_append_t(NULL, &m.data0, vec4));
        glm_vec4_copy((vec4){st->center[0], st->center[1], st->center[2], 0.0f},
            *ecs_vec_append_t(NULL, &m.data1, vec4));
    }

    ecs_os_memcpy(ecs_vec_grow_t(NULL, &m.indices, uint32_t, 6), indices, ECS_SIZEOF(indices));
    ecs_set_ptr(world, imp->card, Mesh3, &m);
    ecs_vec_fini_t(NULL, &m.vertices, vec3);
    ecs_vec_fini_t(NULL, &m.normals, vec3);
    ecs_vec_fini_t(NULL, &m.uvs, vec2);
    ecs_vec_fini_t(NULL, &m.data0, vec4);
    ecs_vec_fini_t(NULL, &m.data1, vec4);
    ecs_vec_fini_t(NULL, &m.indices, uint32_t);
}

/** Make the prefab of the cards of an impostor in its scene, which holds their mesh and
 * material; the cards themselves are slots of the draws.
 *
 * @param world The world
 * @param imp The impostor
 */
static void box_impostor_card_prefab(
    ecs_world_t *world,
    box_impostor_t *imp)
{
    imp->card = ecs_new_w_id(world, EcsPrefab);
    ecs_add_pair(world, imp->card, EcsChildOf, imp->scene);
    ecs_set(world, imp->card, PbrMaterial, {.metallic = 0, .roughness = BOX_IMPOSTOR_ROUGHNESS});
    imp->card_revision = 0;
    imp->distance = -1.0f;
    imp->dirty = true;
}

/** Give an instance of the prefab of an impostor its card.
 *
 * @param world The world
 * @param imp The impostor
 * @param e The entity with Impostor
 * @param inst The instance
 */
static void box_impostor_card_add(
    ecs_world_t *world,
    box_impostor_t *imp,
    ecs_entity_t e,
    ecs_entity_t inst)
{
    ecs_map_insert(&imp->cards, inst, 1);
    box_render_draws_card_add(world, e, inst);
}

/** Remove the card of an instance.
 *
 * @param world The world
 * @param imp The impostor
 * @param inst The instance, which may be gone
 */
static void box_impostor_card_remove(
    ecs_world_t *world,
    box_impostor_t *imp,
    ecs_entity_t inst)
{
    box_render_draws_card_remove(world, inst);
    ecs_map_remove(&imp->cards, inst);
}

/** Remove the cards of every instance of an impostor, or of those that are no longer alive
 * instances of its prefab.
 *
 * @param world The world
 * @param s The impostor state
 * @param imp The impostor
 * @param e The entity with Impostor, 0 to remove every card
 */
static void box_impostor_cards_remove(
    ecs_world_t *world,
    box_impostors_t *s,
    box_impostor_t *imp,
    ecs_entity_t e)
{
    int32_t i;
    ecs_vec_clear(&s->scratch);
    ecs_map_iter_t it = ecs_map_iter(&imp->cards);
    while (ecs_map_next(&it)) {
        ecs_entity_t inst = ecs_map_key(&it);
        if (!e || !ecs_is_alive(world, inst) || !ecs_has_pair(world, inst, EcsIsA, e)) {
            *ecs_vec_append_t(NULL, &s->scratch, ecs_entity_t) = inst;
        }
    }

    for (i = 0; i < ecs_vec_count(&s->scratch); i ++) {
        box_impostor_card_remove(world, imp, *ecs_vec_get_t(&s->scratch, ecs_entity_t, i));
    }

    ecs_vec_clear(&s->scratch);
    imp->dirty = true;
}

/** Check whether an instance is a prefab or lives in the scene of an impostor, such as the copy
 * a bake renders, which get no card.
 *
 * @param world The world
 * @param s The impostor state
 * @param inst The instance
 * @return Whether it gets no card
 */
static bool box_impostor_skipped(
    const ecs_world_t *world,
    const box_impostors_t *s,
    ecs_entity_t inst)
{
    bool result = ecs_has_id(world, inst, EcsPrefab);
    ecs_entity_t cur = inst;
    int32_t depth = 0;
    while (!result && cur && depth < BOX_IMPOSTOR_DEPTH_MAX) {
        ecs_map_iter_t it = ecs_map_iter(&s->impostors);
        while (ecs_map_next(&it)) {
            result |= ((const box_impostor_t*)ecs_map_ptr(&it))->scene == cur;
        }

        cur = ecs_get_parent(world, cur);
        depth ++;
    }

    return result;
}

/** Give the instances of the prefab of an impostor that have none a card, and remove the cards
 * of the instances that are gone.
 *
 * @param world The world
 * @param s The impostor state
 * @param imp The impostor
 * @param e The entity with Impostor
 */
static void box_impostor_cards_scan(
    ecs_world_t *world,
    box_impostors_t *s,
    box_impostor_t *imp,
    ecs_entity_t e)
{
    int32_t i, kept = 0, added = 0;
    ecs_vec_clear(&s->scratch);
    ecs_iter_t it = ecs_each_id(world, ecs_pair(EcsIsA, e));
    while (ecs_each_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            if (ecs_map_get(&imp->cards, it.entities[i])) {
                kept ++;
            } else {
                *ecs_vec_append_t(NULL, &s->scratch, ecs_entity_t) = it.entities[i];
            }
        }
    }

    for (i = 0; i < ecs_vec_count(&s->scratch); i ++) {
        ecs_entity_t inst = *ecs_vec_get_t(&s->scratch, ecs_entity_t, i);
        if (!box_impostor_skipped(world, s, inst)) {
            box_impostor_card_add(world, imp, e, inst);
            added ++;
        }
    }

    ecs_vec_clear(&s->scratch);
    if (ecs_map_count(&imp->cards) > kept + added) {
        box_impostor_cards_remove(world, s, imp, e);
    }

    imp->dirty = false;
}

/** Follow the instances of an impostor with cards while it has a ready bake and a distance, and
 * remove them otherwise, so the instances draw their whole hierarchy until the bake is ready.
 *
 * @param world The world
 * @param s The impostor state
 * @param imp The impostor
 * @param e The entity with Impostor
 */
static void box_impostor_cards(
    ecs_world_t *world,
    box_impostors_t *s,
    box_impostor_t *imp,
    ecs_entity_t e)
{
    const Impostor *value = ecs_get(world, e, Impostor);
    float distance = value && isfinite(value->distance) ? value->distance : 0.0f;
    float fade = value && isfinite(value->fade) ? glm_max(value->fade, 0.0f) : 0.0f;
    if (!imp->status.ready || !imp->front[ImpostorAlbedo] || distance <= 0.0f) {
        if (ecs_map_count(&imp->cards)) {
            box_impostor_cards_remove(world, s, imp, 0);
        }

        if (imp->distance >= 0.0f) {
            box_render_draws_impostor(world, e, 0, 0.0f, 0.0f);
            imp->distance = -1.0f;
        }

        goto done;
    }

    if (!imp->card || !ecs_is_alive(world, imp->card)) {
        box_impostor_cards_remove(world, s, imp, 0);
        box_impostor_card_prefab(world, imp);
    }

    if (imp->card_revision != imp->status.revision) {
        box_impostor_card_mesh(world, imp);
        imp->card_revision = imp->status.revision;
    }

    if (distance != imp->distance || fade != imp->fade) {
        imp->distance = distance;
        imp->fade = fade;
        box_render_draws_impostor(world, e, imp->card, distance, fade);
    }

    if (imp->dirty) {
        box_impostor_cards_scan(world, s, imp, e);
    }

done:
    return;
}

/** Destroy the targets of the previous views and the released atlases, follow the changes of
 * the impostors and send the views of their bakes within the budget.
 *
 * The system is immediate, so the scenes and copies it makes exist at once and the status it
 * sets is visible in the same frame.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractImpostors(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    box_impostors_t *s = box_impostors_get(world);
    const ImpostorBudget *b = ecs_singleton_get(world, ImpostorBudget);
    int32_t i, budget = b ? glm_imax(b->frames, 1) : BOX_IMPOSTOR_BUDGET;
    box_render_packet_t *p = NULL;
    bool any = false;
    ecs_iter_t qit = ecs_query_iter(world, s->query);
    while (ecs_query_next(&qit)) {
        const Impostor *values = ecs_field(&qit, Impostor, 0);
        for (i = 0; i < qit.count; i ++) {
            box_impostor_update(world, s, qit.entities[i], &values[i]);
            any = true;
        }
    }

    if (!any && !ecs_vec_count(&s->sent) && !ecs_vec_count(&s->released) &&
        !ecs_vec_count(&s->reads))
    {
        goto done;
    }

    p = box_render_packet(world);
    if (p->generation != s->generation) {
        ecs_map_iter_t mit = ecs_map_iter(&s->impostors);
        s->generation = p->generation;
        ecs_vec_clear(&s->sent);
        ecs_vec_clear(&s->released);
        while (ecs_map_next(&mit)) {
            box_impostor_t *imp = ecs_map_ptr(&mit);
            ecs_os_memset_n(imp->front, 0, uint32_t, BOX_RENDER_IMPOSTOR_CHANNELS);
            imp->status.ready = false;
            box_impostor_restart(imp);
        }
    }

    for (i = 0; i < ecs_vec_count(&s->sent); i ++) {
        uint32_t handle = *ecs_vec_get_t(&s->sent, uint32_t, i);
        box_render_packet_resource(p, BOX_RENDER_TARGET, BOX_RENDER_DESTROY, handle, 0);
    }

    for (i = 0; i < ecs_vec_count(&s->released); i ++) {
        uint32_t handle = *ecs_vec_get_t(&s->released, uint32_t, i);
        box_render_packet_resource(p, BOX_RENDER_TEXTURE, BOX_RENDER_DESTROY, handle, 0);
    }

    ecs_vec_clear(&s->sent);
    ecs_vec_clear(&s->released);
    for (i = 0; i < ecs_vec_count(&s->reads); i ++) {
        box_impostor_read_t *r = ecs_vec_get_t(&s->reads, box_impostor_read_t, i);
        uint32_t size = (uint32_t)ecs_os_strlen(r->path) + 1, offset = 0;
        char *path = box_render_packet_reserve(p, size, &offset);
        ecs_os_memcpy(path, r->path, (ecs_size_t)size);
        box_render_impostor_read_t *read = ecs_vec_append_t(
            NULL, &p->impostor_reads, box_render_impostor_read_t);
        read->atlas = r->atlas;
        read->path = offset;
        ecs_os_free(r->path);
    }

    ecs_vec_clear(&s->reads);
    ecs_map_iter_t mit = ecs_map_iter(&s->impostors);
    while (ecs_map_next(&mit)) {
        box_impostor_t *imp = ecs_map_ptr(&mit);
        ecs_entity_t e = ecs_map_key(&mit);
        if (imp->baking) {
            box_impostor_bake(world, s, imp, p, &budget);
        }

        if (imp->status_dirty) {
            imp->status_dirty = false;
            ecs_set_ptr(world, e, ImpostorStatus, &imp->status);
        }

        box_impostor_cards(world, s, imp, e);
    }

done:
    return;
}

/** Mark the impostor whose prefab an entity became or stopped being an instance of, so its cards
 * follow the instances. An instance that stops being one, or is deleted, loses its card at once.
 *
 * @param it The iterator of the observer
 */
static void box_impostor_on_instance(
    ecs_iter_t *it)
{
    box_impostors_t *s = box_impostors_get(it->world);
    ecs_id_t id = ecs_field_id(it, 0);
    box_impostor_t *imp = NULL;
    int32_t i;
    if (!s || ecs_is_fini(it->world) || !ECS_IS_PAIR(id)) {
        goto done;
    }

    imp = ecs_map_get_deref(&s->impostors, box_impostor_t, ECS_PAIR_SECOND(id));
    if (imp) {
        imp->dirty = true;
    }

    for (i = 0; imp && it->event == EcsOnRemove && i < it->count; i ++) {
        if (ecs_map_get(&imp->cards, it->entities[i])) {
            box_impostor_card_remove(it->world, imp, it->entities[i]);
        }
    }

done:
    return;
}

/** Free the bake of an impostor whose component is removed, with its scene and copy. A hook,
 * so that it also runs for prefabs.
 *
 * @param it The iterator of the hook
 */
static void box_impostor_on_remove(
    ecs_iter_t *it)
{
    box_impostors_t *s = box_impostors_get(it->world);
    int32_t i;
    if (!s || ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        box_impostor_t *imp = ecs_map_get_deref(&s->impostors, box_impostor_t, it->entities[i]);
        if (!imp) {
            continue;
        }

        box_impostor_cards_remove(it->world, s, imp, 0);
        box_render_draws_impostor(it->world, it->entities[i], 0, 0.0f, 0.0f);
        if (imp->scene && ecs_is_alive(it->world, imp->scene)) {
            ecs_delete(it->world, imp->scene);
        }

        ecs_map_remove(&s->impostors, it->entities[i]);
        box_impostor_free(s, imp);
    }

done:
    return;
}

void box_impostor_rebake(
    ecs_world_t *world,
    ecs_entity_t impostor)
{
    box_impostors_t *s = box_impostors_get(world);
    box_impostor_t *imp = s
        ? ecs_map_get_deref(&s->impostors, box_impostor_t, impostor)
        : NULL;
    if (imp) {
        imp->rebake = true;
    }
}

int box_impostor_write(
    ecs_world_t *world,
    ecs_entity_t impostor,
    ImpostorChannel channel,
    const char *path)
{
    box_impostors_t *s = box_impostors_get(world);
    box_impostor_t *imp = s
        ? ecs_map_get_deref(&s->impostors, box_impostor_t, impostor)
        : NULL;
    int result = -1;
    if (!imp || !imp->status.ready || !path || !path[0] ||
        (uint32_t)channel >= BOX_RENDER_IMPOSTOR_CHANNELS || !imp->front[channel])
    {
        goto done;
    }

    box_impostor_read_t *r = ecs_vec_append_t(NULL, &s->reads, box_impostor_read_t);
    r->atlas = imp->front[channel];
    r->path = ecs_os_strdup(path);
    result = 0;
done:
    return result;
}

uint32_t box_render_impostor_atlas(
    const ecs_world_t *world,
    ecs_entity_t impostor,
    ImpostorChannel channel)
{
    box_impostors_t *s = box_impostors_get(world);
    box_impostor_t *imp = s
        ? ecs_map_get_deref(&s->impostors, box_impostor_t, impostor)
        : NULL;

    return imp && (uint32_t)channel < BOX_RENDER_IMPOSTOR_CHANNELS ? imp->front[channel] : 0;
}

void box_render_impostors_init(
    ecs_world_t *world)
{
    ecs_set_name_prefix(world, "Render");
    ECS_COMPONENT_DEFINE(world, RenderImpostors);
    ecs_set_name_prefix(world, NULL);
    ecs_set_hooks(world, RenderImpostors, {
        .ctor = flecs_default_ctor,
        .dtor = box_render_impostors_dtor,
        .move = box_render_impostors_move
    });

    ecs_set_hooks(world, Impostor, {
        .ctor = box_impostor_ctor,
        .on_remove = box_impostor_on_remove
    });
    ecs_set_hooks(world, ImpostorBudget, { .ctor = box_impostor_budget_ctor });
    ecs_add_pair(world, ecs_id(Impostor), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(ImpostorStatus), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(Impostor), EcsWith, ecs_id(ImpostorStatus));

    box_impostors_t *s = ecs_os_calloc_t(box_impostors_t);
    ecs_map_init(&s->impostors, NULL);
    ecs_map_init(&s->watched, NULL);
    ecs_vec_init_t(NULL, &s->sent, uint32_t, 0);
    ecs_vec_init_t(NULL, &s->released, uint32_t, 0);
    ecs_vec_init_t(NULL, &s->reads, box_impostor_read_t, 0);
    ecs_vec_init_t(NULL, &s->scratch, ecs_entity_t, 0);
    box_impostor_watch_module(world, s, "box.geometry");
    box_impostor_watch_module(world, s, "box.material");
    ecs_map_insert(&s->watched, ecs_id(Position), 1);
    ecs_map_insert(&s->watched, ecs_id(Rotation), 1);
    ecs_map_insert(&s->watched, ecs_id(Scale), 1);
    ecs_map_insert(&s->watched, ecs_id(DrawRange), 1);
    s->query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "ImpostorQuery" }),
        .terms = {
            { .id = ecs_id(Impostor), .inout = EcsIn },
            { .id = EcsPrefab, .oper = EcsOptional }
        }
    });
    ecs_singleton_set(world, RenderImpostors, { .impostors = s });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "ImpostorInstances" }),
        .query.terms = {{ .id = ecs_pair(EcsIsA, EcsWildcard), .src.id = EcsSelf }},
        .query.flags = EcsQueryMatchDisabled,
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = box_impostor_on_instance
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "extract.Impostors" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractImpostors,
        .immediate = true
    });
}
