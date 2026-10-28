/**
 * @file render/particles.c
 * @brief Keeps a particle pool for every enabled ParticleEmitter and counts its particles.
 *
 * A pool is a range of a particle page, a storage buffer of pools of one size. Lifetimes are
 * whole microseconds drawn from the seed, so the extract knows when each particle dies: it
 * frees and picks slots and counts the live particles without reading the GPU back.
 *
 * A deleted linger emitter leaves its pool running its last block without spawns, under the
 * forces of every frame, until the GPU aged its last particle by the same whole microseconds.
 * ParticleLight lights the frame it is written.
 */

#include "render.h"
#include "../particle/simulate.h"
#include "../geometry/mesh_cache.h"
#include "../texture/texture.h"
#include <math.h>
#include <stdlib.h>

/** Slots of the smallest pool */
#define BOX_PARTICLE_CLASS_MIN (1024u)

/** Slots of a particle page at least, unless one pool is larger */
#define BOX_PARTICLE_PAGE_SLOTS (16384u)

/** Pools of a particle page at most, a bit each in its mask */
#define BOX_PARTICLE_PAGE_POOLS (64u)

/** When a particle dies and the slot it frees */
typedef struct box_particle_death_t {
    uint64_t clock;         /**< Clock of the pool at which its age reaches its lifetime */
    uint32_t slot;          /**< Slot of the particle */
    uint32_t pad;           /**< Unused */
} box_particle_death_t;

/** A storage buffer that holds pools of one size */
typedef struct box_particle_page_t {
    uint32_t handle;        /**< Buffer handle */
    uint32_t capacity;      /**< Slots of each of its pools */
    uint32_t pools;         /**< Pools it holds */
    uint64_t occupied;      /**< Bit per pool in use */
} box_particle_page_t;

/** The pool of an emitter and the accounting of its particles */
typedef struct box_particle_pool_t {
    ecs_entity_t entity;    /**< Entity with the ParticleEmitter */
    ecs_entity_t scene;     /**< Scene of the emitter in this frame */
    int32_t max;            /**< Clamped ParticleEmitter.max the pool was made for */
    uint32_t page;          /**< Handle of its page */
    uint32_t base;          /**< First slot of the pool in its page */
    uint32_t capacity;      /**< Slots of the pool */
    bool fresh;             /**< The range was not cleared on the backend yet */
    bool seen;              /**< The emitter is enabled in this frame */
    bool kept;              /**< The emitter of an effect instance was disabled, so the pool
                             *   was kept, emptied when the emitter is enabled again */
    uint64_t clock;         /**< Microseconds the pool has run */
    double carry;           /**< Fraction of a particle owed to the next frame */
    uint64_t time;          /**< Microseconds the emitter has played, ParticlePlayback.time */
    bool started;           /**< A frame played since the playback clock was zeroed */
    bool stopped;           /**< ParticleStopped since the last frame it played */
    uint32_t next;          /**< Spawn index of the next particle */
    int32_t live;           /**< Particles alive */
    uint32_t used;          /**< Slots from the start that were handed out */
    ecs_vec_t deaths;       /**< box_particle_death_t, a heap by clock and slot */
    ecs_vec_t free_slots;   /**< uint32_t, slots below used whose particle died */
    uint32_t table;         /**< Buffer handle of the mesh table, 0 for none */
    int32_t points;         /**< Points of the mesh table */
    bool table_built;       /**< The table matches the key below */
    ecs_entity_t table_source; /**< Entity the table was sampled from */
    uint32_t table_mesh;    /**< Mesh handle the table was sampled from */
    vec3 table_scale;       /**< Shape scale the table was sampled with */
    uint32_t table_seed;    /**< Seed the table was sampled with */
    vec3 last_position;     /**< World position of the emitter in the last frame */
    bool has_last;          /**< last_position holds a position */
    uint32_t mesh;          /**< Buffer handle of the corners of a billboard, 0 for none */
    uint32_t corners;       /**< Corners in the billboard buffer */
    bool mesh_built;        /**< The corners match the key below */
    ecs_entity_t mesh_entity; /**< Entity the corners were built from */
    uint32_t mesh_handle;   /**< Mesh handle the corners were built from */
    vec3 mesh_scale;        /**< Shape scale the corners were built with */
    bool linger;            /**< ParticleEmitter.linger of the last frame the emitter was seen */
    bool lingering;         /**< The emitter was deleted and the pool runs out its particles,
                             *   no longer in by_entity */
    uint64_t faded;         /**< Microseconds of stopped frames since the emitter stopped, the
                             *   fade of its ParticleLight */
    double frame[3];        /**< World displacement of the frame of reference since the last
                             *   frame without particles to move: the drift times the seconds
                             *   of every frame they moved */
    vec3 acceleration;      /**< ParticleEmitter.acceleration of the last frame of a linger
                             *   emitter */
    ecs_entity_t forces;    /**< ParticleEmitter.forces of that frame */
    ParticleForces own;     /**< ParticleForces of the emitter entity in that frame */
    bool has_own;           /**< The emitter entity had ParticleForces in that frame */
    box_render_particle_pool_t entry; /**< The pool of the last frame of a lingering emitter
                             *   in the packet, which a lingering pool runs again */
} box_particle_pool_t;

/** A burst of the frame and the emitter whose pool it spawns into */
typedef struct box_particle_burst_t {
    ecs_entity_t emitter;   /**< Nearest ParticleEmitter at or above the burst */
    ParticleBurst burst;    /**< The burst values */
} box_particle_burst_t;

/** Main thread state of the particle extract */
typedef struct box_render_particles_t {
    ecs_query_t *emitters;  /**< ParticleEmitter, WorldTransform, Scene(self|up), Playback,
                             *   Curves, Appearance, ParticleLight, PointLight(self) and
                             *   SpotLight(self), NULL without box.particle */
    ecs_query_t *burst_query; /**< ParticleBurst and ParticleEmitter(self|up), NULL without
                             *   box.particle */
    ecs_vec_t bursts;       /**< box_particle_burst_t of the frame, sorted by emitter */
    ecs_vec_t pools;        /**< box_particle_pool_t* */
    ecs_map_t by_entity;    /**< Entity to box_particle_pool_t* */
    ecs_vec_t pages;        /**< box_particle_page_t */
    ecs_vec_t page_handles; /**< uint32_t, page handles to reuse */
    uint32_t next_page;     /**< Next page handle */
    ecs_vec_t table_handles; /**< uint32_t, table handles to reuse */
    uint32_t next_table;    /**< Next table handle */
    ecs_vec_t points;       /**< box_particle_point_t, the table being sampled */
    ecs_vec_t mesh_handles; /**< uint32_t, billboard corner buffer handles to reuse */
    uint32_t next_mesh;     /**< Next billboard corner buffer handle */
    ecs_vec_t corners;      /**< box_render_particle_corner_t, the corners being built */
    const void *backend;    /**< Context of the backend the pages were sent to */
    void (*render)(void*, const box_render_packet_t*); /**< Render callback of that backend */
    int32_t generation;     /**< Generation of that backend, see box_render_backend_t */
} box_render_particles_t;

/** Check whether a death comes before another, by clock and then by slot.
 *
 * @param a The first death
 * @param b The second death
 * @return Whether a comes first
 */
static bool box_particles_before(
    const box_particle_death_t *a,
    const box_particle_death_t *b)
{
    return a->clock < b->clock || (a->clock == b->clock && a->slot < b->slot);
}

/** Add a death to the heap of a pool.
 *
 * @param pool The pool
 * @param clock Clock at which the particle dies
 * @param slot Slot of the particle
 */
static void box_particles_heap_push(
    box_particle_pool_t *pool,
    uint64_t clock,
    uint32_t slot)
{
    int32_t i = ecs_vec_count(&pool->deaths);
    box_particle_death_t *heap;
    ecs_vec_append_t(NULL, &pool->deaths, box_particle_death_t);
    heap = ecs_vec_first_t(&pool->deaths, box_particle_death_t);
    box_particle_death_t d = { .clock = clock, .slot = slot };
    while (i > 0) {
        int32_t parent = (i - 1) / 2;
        if (!box_particles_before(&d, &heap[parent])) {
            break;
        }

        heap[i] = heap[parent];
        i = parent;
    }

    heap[i] = d;
}

/** Remove the first death from the heap of a pool.
 *
 * @param pool The pool, with at least one death
 * @return The death
 */
static box_particle_death_t box_particles_heap_pop(
    box_particle_pool_t *pool)
{
    box_particle_death_t *heap = ecs_vec_first_t(&pool->deaths, box_particle_death_t);
    int32_t count = ecs_vec_count(&pool->deaths) - 1, i = 0;
    box_particle_death_t result = heap[0], last = heap[count];
    ecs_vec_remove_last(&pool->deaths);
    while (count) {
        int32_t child = 2 * i + 1;
        if (child >= count) {
            break;
        }

        if (child + 1 < count && box_particles_before(&heap[child + 1], &heap[child])) {
            child ++;
        }

        if (!box_particles_before(&heap[child], &last)) {
            break;
        }

        heap[i] = heap[child];
        i = child;
    }

    if (count) {
        heap[i] = last;
    }

    return result;
}

/** Get the slots of the pool of a number of particles: a power of two, at least
 * BOX_PARTICLE_CLASS_MIN.
 *
 * @param max Particles alive at once, 1 to BOX_PARTICLE_MAX
 * @return The slots
 */
static uint32_t box_particles_class(
    int32_t max)
{
    uint32_t result = BOX_PARTICLE_CLASS_MIN;
    while (result < (uint32_t)max) {
        result *= 2;
    }

    return result;
}

/** Find the page of a handle.
 *
 * @param s The state
 * @param handle The handle
 * @return Index of the page, -1 when there is none
 */
static int32_t box_particles_page_find(
    const box_render_particles_t *s,
    uint32_t handle)
{
    int32_t i, result = -1;
    for (i = 0; i < ecs_vec_count(&s->pages); i ++) {
        if (ecs_vec_get_t(&s->pages, box_particle_page_t, i)->handle == handle) {
            result = i;
            break;
        }
    }

    return result;
}

/** Give a pool a range of slots in a page of its size, creating a page when every page of that
 * size is full.
 *
 * @param s The state
 * @param p The packet, receives a new page
 * @param pool The pool, its capacity set
 */
static void box_particles_range_alloc(
    box_render_particles_t *s,
    box_render_packet_t *p,
    box_particle_pool_t *pool)
{
    box_particle_page_t *page = NULL;
    uint32_t bit;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&s->pages); i ++) {
        box_particle_page_t *candidate = ecs_vec_get_t(&s->pages, box_particle_page_t, i);
        uint64_t full = candidate->pools >= 64 ? UINT64_MAX
            : (((uint64_t)1 << candidate->pools) - 1);
        if (candidate->capacity == pool->capacity && candidate->occupied != full) {
            page = candidate;
            break;
        }
    }

    if (!page) {
        uint32_t pools = BOX_PARTICLE_PAGE_SLOTS / pool->capacity;
        pools = pools < 1 ? 1 : pools > BOX_PARTICLE_PAGE_POOLS ? BOX_PARTICLE_PAGE_POOLS : pools;
        page = ecs_vec_append_t(NULL, &s->pages, box_particle_page_t);
        ecs_os_memset_t(page, 0, box_particle_page_t);
        page->capacity = pool->capacity;
        page->pools = pools;
        if (ecs_vec_count(&s->page_handles)) {
            page->handle = *ecs_vec_last_t(&s->page_handles, uint32_t);
            ecs_vec_remove_last(&s->page_handles);
        } else {
            page->handle = s->next_page ++;
        }

        box_render_buffer_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_BUFFER,
            BOX_RENDER_CREATE, page->handle, sizeof(box_render_buffer_desc_t));
        desc->size = (uint64_t)pools * pool->capacity * BOX_RENDER_PARTICLE_SIZE;
        desc->usage = BOX_RENDER_BUFFER_STORAGE;
    }

    for (bit = 0; bit < page->pools; bit ++) {
        if (!(page->occupied & ((uint64_t)1 << bit))) {
            break;
        }
    }

    page->occupied |= (uint64_t)1 << bit;
    pool->page = page->handle;
    pool->base = bit * page->capacity;
    pool->fresh = true;
}

/** Return the range of a pool to its page, destroying the page when it is empty.
 *
 * @param s The state
 * @param p The packet, receives the destroy of an empty page
 * @param pool The pool
 */
static void box_particles_range_free(
    box_render_particles_t *s,
    box_render_packet_t *p,
    box_particle_pool_t *pool)
{
    int32_t index = box_particles_page_find(s, pool->page);
    if (index < 0) {
        goto done;
    }

    box_particle_page_t *page = ecs_vec_get_t(&s->pages, box_particle_page_t, index);
    page->occupied &= ~((uint64_t)1 << (pool->base / page->capacity));
    if (!page->occupied) {
        box_render_packet_resource(p, BOX_RENDER_BUFFER, BOX_RENDER_DESTROY, page->handle, 0);
        *ecs_vec_append_t(NULL, &s->page_handles, uint32_t) = page->handle;
        ecs_vec_remove_t(&s->pages, box_particle_page_t, index);
    }

done:
    pool->page = 0;
}

/** Forget every particle of a pool, as if it was just made.
 *
 * @param pool The pool
 */
static void box_particles_empty(
    box_particle_pool_t *pool)
{
    pool->clock = 0;
    pool->carry = 0;
    pool->next = 0;
    pool->live = 0;
    pool->used = 0;
    pool->frame[0] = pool->frame[1] = pool->frame[2] = 0;
    ecs_vec_clear(&pool->deaths);
    ecs_vec_clear(&pool->free_slots);
}

/** Destroy the mesh table of a pool on the backend.
 *
 * @param s The state
 * @param p The packet
 * @param pool The pool
 */
static void box_particles_table_free(
    box_render_particles_t *s,
    box_render_packet_t *p,
    box_particle_pool_t *pool)
{
    if (pool->table) {
        box_render_packet_resource(p, BOX_RENDER_BUFFER, BOX_RENDER_DESTROY, pool->table, 0);
        *ecs_vec_append_t(NULL, &s->table_handles, uint32_t) = pool->table;
    }

    pool->table = 0;
    pool->points = 0;
    pool->table_built = false;
}

/** Destroy the billboard corners of a pool on the backend.
 *
 * @param s The state
 * @param p The packet
 * @param pool The pool
 */
static void box_particles_mesh_free(
    box_render_particles_t *s,
    box_render_packet_t *p,
    box_particle_pool_t *pool)
{
    if (pool->mesh) {
        box_render_packet_resource(p, BOX_RENDER_BUFFER, BOX_RENDER_DESTROY, pool->mesh, 0);
        *ecs_vec_append_t(NULL, &s->mesh_handles, uint32_t) = pool->mesh;
    }

    pool->mesh = 0;
    pool->corners = 0;
    pool->mesh_built = false;
}

/** Make the pool of an emitter.
 *
 * @param s The state
 * @param p The packet
 * @param e The emitter
 * @param max Clamped ParticleEmitter.max, at least 1
 * @return The pool
 */
static box_particle_pool_t* box_particles_pool_new(
    box_render_particles_t *s,
    box_render_packet_t *p,
    ecs_entity_t e,
    int32_t max)
{
    box_particle_pool_t *result = ecs_os_calloc_t(box_particle_pool_t);
    result->entity = e;
    result->max = max;
    result->capacity = box_particles_class(max);
    ecs_vec_init_t(NULL, &result->deaths, box_particle_death_t, 0);
    ecs_vec_init_t(NULL, &result->free_slots, uint32_t, 0);
    box_particles_range_alloc(s, p, result);
    *ecs_vec_append_t(NULL, &s->pools, box_particle_pool_t*) = result;
    ecs_map_insert_ptr(&s->by_entity, e, result);

    return result;
}

/** Free a pool, its range and its table.
 *
 * @param s The state
 * @param p The packet, NULL when the backend lost them anyway
 * @param index Index of the pool
 */
static void box_particles_pool_free(
    box_render_particles_t *s,
    box_render_packet_t *p,
    int32_t index)
{
    box_particle_pool_t *pool = *ecs_vec_get_t(&s->pools, box_particle_pool_t*, index);
    if (p) {
        box_particles_range_free(s, p, pool);
        box_particles_table_free(s, p, pool);
        box_particles_mesh_free(s, p, pool);
    }

    if (!pool->lingering) {
        ecs_map_remove(&s->by_entity, pool->entity);
    }

    ecs_vec_remove_t(&s->pools, box_particle_pool_t*, index);
    ecs_vec_fini_t(NULL, &pool->deaths, box_particle_death_t);
    ecs_vec_fini_t(NULL, &pool->free_slots, uint32_t);
    ecs_os_free(pool);
}

/** Forget every pool, page and table, for a backend that does not have them.
 *
 * @param s The state
 */
static void box_particles_reset_backend(
    box_render_particles_t *s)
{
    while (ecs_vec_count(&s->pools)) {
        box_particles_pool_free(s, NULL, ecs_vec_count(&s->pools) - 1);
    }

    ecs_vec_clear(&s->pages);
    ecs_vec_clear(&s->page_handles);
    ecs_vec_clear(&s->table_handles);
    ecs_vec_clear(&s->mesh_handles);
    s->next_page = BOX_RENDER_BUFFER_PARTICLE_FIRST;
    s->next_table = BOX_RENDER_BUFFER_PARTICLE_TABLE_FIRST;
    s->next_mesh = BOX_RENDER_BUFFER_PARTICLE_MESH_FIRST;
}

/** Sample the mesh table of an EmitterMesh again when its source, its shape or its seed
 * changed, and send it.
 *
 * @param world The world
 * @param s The state
 * @param p The packet
 * @param pool The pool
 * @param e The emitter
 * @param emitter The emitter values
 */
static void box_particles_table_update(
    ecs_world_t *world,
    box_render_particles_t *s,
    box_render_packet_t *p,
    box_particle_pool_t *pool,
    ecs_entity_t e,
    const ParticleEmitter *emitter)
{
    ecs_entity_t source = emitter->source ? emitter->source : e;
    const ShapeMesh *shape = source && ecs_is_alive(world, source)
        ? ecs_get(world, source, ShapeMesh)
        : NULL;
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    uint32_t mesh = shape ? shape->mesh : 0;
    vec3 scale = {0, 0, 0};
    if (shape) {
        glm_vec3_copy(ECS_CONST_CAST(float*, shape->scale), scale);
    }

    if (pool->table_built && pool->table_source == source && pool->table_mesh == mesh &&
        pool->table_seed == emitter->seed && glm_vec3_eqv(scale, pool->table_scale))
    {
        goto done;
    }

    const box_mesh_t *m = mesh && cache ? box_mesh_cache_get(cache, mesh) : NULL;
    ecs_vec_set_count_t(NULL, &s->points, box_particle_point_t, BOX_PARTICLE_MESH_POINTS);
    int32_t points = box_particle_mesh_table(m, scale, emitter->seed,
        ecs_vec_first(&s->points), BOX_PARTICLE_MESH_POINTS);
    box_particles_table_free(s, p, pool);
    if (points) {
        uint32_t size = (uint32_t)points * (uint32_t)sizeof(box_particle_point_t);
        if (ecs_vec_count(&s->table_handles)) {
            pool->table = *ecs_vec_last_t(&s->table_handles, uint32_t);
            ecs_vec_remove_last(&s->table_handles);
        } else {
            pool->table = s->next_table ++;
        }

        box_render_buffer_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_BUFFER,
            BOX_RENDER_CREATE, pool->table, sizeof(box_render_buffer_desc_t));
        desc->size = size;
        desc->usage = BOX_RENDER_BUFFER_STORAGE;
        ecs_os_memcpy(box_render_packet_upload(p, pool->table, 0, size),
            ecs_vec_first(&s->points), (ecs_size_t)size);
        pool->points = points;
    }

    pool->table_built = true;
    pool->table_source = source;
    pool->table_mesh = mesh;
    pool->table_seed = emitter->seed;
    glm_vec3_copy(scale, pool->table_scale);
done:
    return;
}

/** Write the corners of the triangles of a mesh, scaled by the shape, as a billboard draws them.
 *
 * @param m The mesh
 * @param scale Scale of the shape
 * @param out Receives a corner per index
 */
static void box_particles_corners(
    const box_mesh_t *m,
    const float *scale,
    box_render_particle_corner_t *out)
{
    int32_t i, c;
    for (i = 0; i < m->index_count; i ++) {
        uint32_t v = m->indices[i];
        const box_vertex_t *vertex = (int32_t)v < m->vertex_count ? &m->vertices[v] : NULL;
        vec3 n = {0, 0, 1};
        ecs_os_memset_t(&out[i], 0, box_render_particle_corner_t);
        if (!vertex) {
            continue;
        }

        for (c = 0; c < 3; c ++) {
            out[i].position[c] = vertex->position[c] * scale[c];
            n[c] = scale[c] != 0 ? vertex->normal[c] / scale[c] : 0.0f;
        }

        if (glm_vec3_norm2(n) > 0) {
            glm_vec3_normalize(n);
        }

        glm_vec3_copy(n, out[i].normal);
        out[i].position[3] = vertex->uv[0];
        out[i].normal[3] = vertex->uv[1];
    }
}

/** Build the corners of the mesh a billboard draws again when its entity, its shape or its
 * scale changed, and send them.
 *
 * @param world The world
 * @param s The state
 * @param p The packet
 * @param pool The pool
 * @param source Entity of the mesh, 0 for none
 */
static void box_particles_mesh_update(
    ecs_world_t *world,
    box_render_particles_t *s,
    box_render_packet_t *p,
    box_particle_pool_t *pool,
    ecs_entity_t source)
{
    const ShapeMesh *shape = source && ecs_is_alive(world, source)
        ? ecs_get(world, source, ShapeMesh)
        : NULL;
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    uint32_t mesh = shape ? shape->mesh : 0;
    vec3 scale = {0, 0, 0};
    if (shape) {
        glm_vec3_copy(ECS_CONST_CAST(float*, shape->scale), scale);
    }

    if (pool->mesh_built && pool->mesh_entity == source && pool->mesh_handle == mesh &&
        glm_vec3_eqv(scale, pool->mesh_scale))
    {
        goto done;
    }

    const box_mesh_t *m = mesh && cache ? box_mesh_cache_get(cache, mesh) : NULL;
    box_particles_mesh_free(s, p, pool);
    if (m && m->index_count >= 3 && m->vertex_count) {
        int32_t count = m->index_count - m->index_count % 3;
        uint32_t size = (uint32_t)count * (uint32_t)sizeof(box_render_particle_corner_t);
        ecs_vec_set_count_t(NULL, &s->corners, box_render_particle_corner_t, m->index_count);
        box_particles_corners(m, scale, ecs_vec_first(&s->corners));
        if (ecs_vec_count(&s->mesh_handles)) {
            pool->mesh = *ecs_vec_last_t(&s->mesh_handles, uint32_t);
            ecs_vec_remove_last(&s->mesh_handles);
        } else {
            pool->mesh = s->next_mesh ++;
        }

        box_render_buffer_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_BUFFER,
            BOX_RENDER_CREATE, pool->mesh, sizeof(box_render_buffer_desc_t));
        desc->size = size;
        desc->usage = BOX_RENDER_BUFFER_STORAGE;
        ecs_os_memcpy(box_render_packet_upload(p, pool->mesh, 0, size),
            ecs_vec_first(&s->corners), (ecs_size_t)size);
        pool->corners = (uint32_t)count;
    }

    pool->mesh_built = m != NULL || !mesh;
    pool->mesh_entity = source;
    pool->mesh_handle = mesh;
    glm_vec3_copy(scale, pool->mesh_scale);
done:
    return;
}

/** Free the slots of the particles of a pool whose age reached their lifetime by the clock.
 *
 * @param pool The pool
 */
static void box_particles_retire(
    box_particle_pool_t *pool)
{
    while (ecs_vec_count(&pool->deaths) &&
        ecs_vec_first_t(&pool->deaths, box_particle_death_t)->clock <= pool->clock)
    {
        box_particle_death_t d = box_particles_heap_pop(pool);
        *ecs_vec_append_t(NULL, &pool->free_slots, uint32_t) = d.slot;
        pool->live --;
    }

    if (!pool->live) {
        pool->used = 0;
        ecs_vec_clear(&pool->free_slots);
    }
}

/** Add what an emitter owes for a frame to its pool and take the whole particles, carrying the
 * fraction; what a full pool has no slots for is dropped, not owed.
 *
 * @param pool The pool
 * @param emitter The emitter values
 * @param delta Seconds of the frame
 * @param burst Particles the bursts of the frame add
 * @return Particles to spawn, at most the free slots of the pool
 */
static uint32_t box_particles_owed(
    box_particle_pool_t *pool,
    const ParticleEmitter *emitter,
    double delta,
    double burst)
{
    uint32_t free = (uint32_t)(pool->max - pool->live);
    double whole;
    pool->carry += (emitter->rate > 0 ? (double)emitter->rate : 0.0) * delta;
    whole = floor(pool->carry);
    pool->carry -= whole;
    whole += burst;

    return whole < (double)free ? (uint32_t)whole : free;
}

/** Count the times a burst goes off in a frame that moves the playback clock from t0 to t1.
 *
 * Firing k, from 1, is at k times its period, so the firings up to a time are the time over the
 * period, capped at repeat + 1 for a burst that does not repeat forever.
 *
 * @param burst The burst
 * @param t0 Playback microseconds before the frame
 * @param t1 Playback microseconds after the frame
 * @param first Whether this is the first frame since the clock was zeroed
 * @return The times it goes off
 */
static uint64_t box_particles_burst_fires(
    const ParticleBurst *burst,
    uint64_t t0,
    uint64_t t1,
    bool first)
{
    uint64_t period = box_particle_us(burst->at), n0, n1, result = 0;
    if (!period) {
        result = first ? 1 : 0;
        goto done;
    }

    n0 = t0 / period;
    n1 = t1 / period;
    if (burst->repeat >= 0) {
        uint64_t total = (uint64_t)burst->repeat + 1;
        n0 = n0 < total ? n0 : total;
        n1 = n1 < total ? n1 : total;
    }

    result = n1 - n0;
done:
    return result;
}

/** Order bursts by their emitter.
 *
 * @param a The first burst
 * @param b The second burst
 * @return Below, at or above zero as a sorts before, with or after b
 */
static int box_particles_burst_compare(
    const void *a,
    const void *b)
{
    ecs_entity_t ea = ((const box_particle_burst_t*)a)->emitter;
    ecs_entity_t eb = ((const box_particle_burst_t*)b)->emitter;

    return (ea > eb) - (ea < eb);
}

/** Collect the bursts of the frame with the emitter of each, sorted by emitter.
 *
 * @param world The world
 * @param s The state
 */
static void box_particles_bursts_collect(
    ecs_world_t *world,
    box_render_particles_t *s)
{
    int32_t i;
    ecs_vec_clear(&s->bursts);
    ecs_iter_t it = ecs_query_iter(world, s->burst_query);
    while (ecs_query_next(&it)) {
        const ParticleBurst *bursts = ecs_field(&it, ParticleBurst, 0);
        ecs_entity_t src = ecs_field_src(&it, 1);
        for (i = 0; i < it.count; i ++) {
            box_particle_burst_t *b = ecs_vec_append_t(NULL, &s->bursts, box_particle_burst_t);
            b->emitter = src ? src : it.entities[i];
            b->burst = bursts[i];
        }
    }

    if (ecs_vec_count(&s->bursts) > 1) {
        qsort(ecs_vec_first(&s->bursts), (size_t)ecs_vec_count(&s->bursts),
            sizeof(box_particle_burst_t), box_particles_burst_compare);
    }
}

/** Count the particles the bursts of an emitter spawn in a frame that moves its playback clock
 * from t0 to t1.
 *
 * @param s The state, with the bursts of the frame
 * @param e The emitter
 * @param t0 Playback microseconds before the frame
 * @param t1 Playback microseconds after the frame
 * @param first Whether this is the first frame since the clock was zeroed
 * @return The particles, not capped by the free slots
 */
static double box_particles_bursts_owed(
    const box_render_particles_t *s,
    ecs_entity_t e,
    uint64_t t0,
    uint64_t t1,
    bool first)
{
    const box_particle_burst_t *bursts = ecs_vec_first_t(&s->bursts, box_particle_burst_t);
    int32_t lo = 0, hi = ecs_vec_count(&s->bursts);
    double result = 0;
    while (lo < hi) {
        int32_t mid = (lo + hi) / 2;
        if (bursts[mid].emitter < e) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    for (; lo < ecs_vec_count(&s->bursts) && bursts[lo].emitter == e; lo ++) {
        const ParticleBurst *b = &bursts[lo].burst;
        if (b->count > 0) {
            result += (double)b->count * (double)box_particles_burst_fires(b, t0, t1, first);
        }
    }

    return result;
}

/** Spawn particles into free slots of a pool, the slots of particles that died first.
 *
 * @param pool The pool
 * @param emitter The emitter values
 * @param count Particles to spawn, at most the free slots of the pool
 * @param life_min Shortest lifetime in microseconds
 * @param life_span Microseconds the longest lifetime is above life_min
 * @param out Receives the slot of each spawn
 */
static void box_particles_spawn(
    box_particle_pool_t *pool,
    const ParticleEmitter *emitter,
    uint32_t count,
    uint32_t life_min,
    uint32_t life_span,
    uint32_t *out)
{
    uint32_t k;
    for (k = 0; k < count; k ++) {
        uint32_t life = box_particle_life(emitter->seed, pool->next + k, life_min, life_span);
        uint32_t slot = BOX_RENDER_PARTICLE_NO_SLOT;
        if (life) {
            if (ecs_vec_count(&pool->free_slots)) {
                slot = *ecs_vec_last_t(&pool->free_slots, uint32_t);
                ecs_vec_remove_last(&pool->free_slots);
            } else {
                slot = pool->used ++;
            }

            box_particles_heap_push(pool, pool->clock + life, slot);
            pool->live ++;
        }

        out[k] = slot;
    }
}

/** Add the forces of a source to the sum of the forces of an emitter, multiplying the
 * turbulence scale.
 *
 * @param f The forces of the source, NULL for none
 * @param sum The sum
 */
static void box_particles_forces_add(
    const ParticleForces *f,
    ParticleForces *sum)
{
    if (f) {
        glm_vec3_add(ECS_CONST_CAST(float*, f->gravity), sum->gravity, sum->gravity);
        glm_vec3_add(ECS_CONST_CAST(float*, f->wind), sum->wind, sum->wind);
        glm_vec3_add(ECS_CONST_CAST(float*, f->drift), sum->drift, sum->drift);
        sum->drag += f->drag;
        sum->turbulence += f->turbulence;
        sum->turbulence_scale *= f->turbulence_scale;
    }
}

/** Sum the forces on the particles of an emitter: those of the world singleton, of the entity
 * the emitter names and of the emitter entity, each source counted once.
 *
 * @param world The world
 * @param e The emitter
 * @param named The entity ParticleEmitter.forces names, 0 for none
 * @param own The ParticleForces of the emitter entity, NULL for none or when the emitter is
 *            the singleton
 * @param out Receives the sum
 */
static void box_particles_forces(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t named,
    const ParticleForces *own,
    ParticleForces *out)
{
    ecs_entity_t singleton = ecs_id(ParticleForces);
    ecs_os_memset_t(out, 0, ParticleForces);
    out->turbulence_scale = 1.0f;
    box_particles_forces_add(ecs_get(world, singleton, ParticleForces), out);
    if (named && named != singleton && named != e && ecs_is_alive(world, named)) {
        box_particles_forces_add(ecs_get(world, named, ParticleForces), out);
    }

    box_particles_forces_add(own, out);
}

/** Write the forces of a frame into the block of a pool and move the frame of reference of the
 * pool by the drift of the frame, so the wander field moves with the particles.
 *
 * The field is offset by the displacement before this frame's drift, where the integrate pass
 * samples it. A frame with no particles to move puts the frame back at the world origin, so a
 * burst starts in the field it has without drift.
 *
 * @param pool The pool, its frame of reference advanced
 * @param acceleration ParticleEmitter.acceleration
 * @param forces The sum of the forces on its particles
 * @param moves Whether the integrate pass moves particles in this frame
 * @param out The block, its seed and its seconds of the frame written
 */
static void box_particles_block_forces(
    box_particle_pool_t *pool,
    const vec3 acceleration,
    const ParticleForces *forces,
    bool moves,
    box_render_particle_block_t *out)
{
    float phase[3];
    int32_t c;
    if (!moves) {
        pool->frame[0] = pool->frame[1] = pool->frame[2] = 0;
    }

    glm_vec3_add(ECS_CONST_CAST(float*, acceleration),
        ECS_CONST_CAST(float*, forces->gravity), out->acceleration);
    glm_vec3_copy(ECS_CONST_CAST(float*, forces->wind), out->wind);
    out->wind[3] = forces->drag > 0 ? forces->drag : 0.0f;
    out->turbulence[0] = forces->turbulence;
    out->turbulence[1] = forces->turbulence_scale;
    box_particle_phase(out->seed, phase);
    box_particle_frame_phase(phase, pool->frame, forces->turbulence_scale, out->wander);
    glm_vec3_copy(ECS_CONST_CAST(float*, forces->drift), out->drift);
    for (c = 0; c < 3; c ++) {
        pool->frame[c] += moves ? (double)out->drift[c] * (double)out->acceleration[3] : 0.0;
    }
}

/** Write the parameters of an emitter for this frame but its forces.
 *
 * @param emitter The emitter values
 * @param transform World transform of the emitter, NULL for the identity
 * @param collision Its ground collision, NULL for none
 * @param inherit World velocity its spawns get
 * @param delta Seconds of the frame
 * @param scale Multiplier of the speeds and sizes, the scale of the effect it plays in
 * @param out Receives the block, every padding member zero
 */
static void box_particles_block(
    const ParticleEmitter *emitter,
    const WorldTransform *transform,
    const ParticleCollision *collision,
    const vec3 inherit,
    float delta,
    float scale,
    box_render_particle_block_t *out)
{
    float spread = glm_clamp(emitter->spread, 0.0f, 180.0f);
    ecs_os_memset_t(out, 0, box_render_particle_block_t);
    if (transform) {
        ecs_os_memcpy_t(out->model, transform->m, mat4);
    } else {
        ecs_os_memcpy_t(out->model, GLM_MAT4_IDENTITY, mat4);
    }

    glm_vec3_copy(ECS_CONST_CAST(float*, emitter->direction), out->direction);
    if (glm_vec3_norm2(out->direction) > 0) {
        glm_vec3_normalize(out->direction);
    } else {
        glm_vec3_zero(out->direction);
    }

    out->direction[3] = cosf(glm_rad(spread));
    glm_vec3_copy(ECS_CONST_CAST(float*, emitter->extents), out->extents);
    out->extents[3] = emitter->radius;
    out->acceleration[3] = delta;
    out->color[0] = (float)emitter->color.r / 255.0f;
    out->color[1] = (float)emitter->color.g / 255.0f;
    out->color[2] = (float)emitter->color.b / 255.0f;
    out->color[3] = 1.0f;
    out->speed[0] = emitter->speed.min * scale;
    out->speed[1] = emitter->speed.max * scale;
    out->size[0] = emitter->size.min * scale;
    out->size[1] = emitter->size.max * scale;
    out->rotation[0] = emitter->rotation.min;
    out->rotation[1] = emitter->rotation.max;
    out->spin[0] = emitter->spin.min;
    out->spin[1] = emitter->spin.max;
    out->opacity[0] = emitter->opacity.min;
    out->opacity[1] = emitter->opacity.max;
    out->shape = (uint32_t)emitter->shape;
    out->seed = emitter->seed;
    out->variance[0] = (float)emitter->color_variance.r / 255.0f;
    out->variance[1] = (float)emitter->color_variance.g / 255.0f;
    out->variance[2] = (float)emitter->color_variance.b / 255.0f;
    glm_vec3_copy(ECS_CONST_CAST(float*, inherit), out->inherit);
    if (collision && collision->ground) {
        out->ground[0] = collision->height;
        out->ground[1] = glm_clamp(collision->bounce, 0.0f, 1.0f);
        out->ground[2] = glm_clamp(collision->friction, 0.0f, 1.0f);
        out->collide = collision->die ? BOX_PARTICLE_GROUND_DIE : BOX_PARTICLE_GROUND_BOUNCE;
    }
}

/** Measure the world velocity of an emitter from its position in the last frame.
 *
 * @param pool The pool of the emitter, keeps the position
 * @param transform World transform of the emitter, NULL for the identity
 * @param delta Seconds of the frame
 * @param out Receives the velocity, zero in the first frame and for a frame without time
 */
static void box_particles_velocity(
    box_particle_pool_t *pool,
    const WorldTransform *transform,
    float delta,
    vec3 out)
{
    vec3 position = {0, 0, 0};
    if (transform) {
        glm_vec3_copy(ECS_CONST_CAST(float*, transform->m[3]), position);
    }

    glm_vec3_zero(out);
    if (pool->has_last && delta > 0) {
        glm_vec3_sub(position, pool->last_position, out);
        glm_vec3_scale(out, 1.0f / delta, out);
    }

    glm_vec3_copy(position, pool->last_position);
    pool->has_last = true;
}

/** Get the keys the sort of a pool sorts: the used slots rounded up to a power of two, at least
 * the keys of a workgroup.
 *
 * @param used Slots that may hold a particle
 * @param capacity Slots of the pool, a power of two
 * @return The keys
 */
static uint32_t box_particles_sort_count(
    uint32_t used,
    uint32_t capacity)
{
    uint32_t result = BOX_RENDER_PARTICLE_SORT_MIN;
    while (result < used) {
        result *= 2;
    }

    return result < capacity ? result : capacity;
}

/** Write the changes over the life and the appearance of an emitter into its block.
 *
 * Without ParticleCurves the curves change nothing; without ParticleAppearance the particles
 * are soft alpha blended circles, unsorted because their one color blends the same in any
 * order.
 *
 * @param curves The ParticleCurves of the emitter, NULL for none
 * @param look The ParticleAppearance of the emitter, NULL for none
 * @param pool The pool, with its billboard corners
 * @param out The block, its other members written
 */
static void box_particles_block_look(
    const ParticleCurves *curves,
    const ParticleAppearance *look,
    const box_particle_pool_t *pool,
    box_render_particle_block_t *out)
{
    const rgb *colors = NULL;
    int32_t i;
    for (i = 0; i < 4; i ++) {
        out->curve_size[i] = curves ? (&curves->size.start)[i] : 1.0f;
        out->curve_opacity[i] = curves ? (&curves->opacity.start)[i] : 1.0f;
        out->curve_rotation[i] = curves ? (&curves->rotation.start)[i] : 0.0f;
        out->curve_spin[i] = curves ? (&curves->spin.start)[i] : 1.0f;
        colors = curves ? &(&curves->color.start)[i] : NULL;
        out->gradient[i][0] = colors ? box_particle_linear(colors->r) : 1.0f;
        out->gradient[i][1] = colors ? box_particle_linear(colors->g) : 1.0f;
        out->gradient[i][2] = colors ? box_particle_linear(colors->b) : 1.0f;
        out->gradient[i][3] = 1.0f;
    }

    out->look = ParticleCircle;
    out->blend = ParticleAlpha;
    out->columns = 1;
    out->rows = 1;
    out->corners = 6;
    if (look) {
        out->look = (uint32_t)look->shape;
        out->blend = look->blend == ParticleAdditive ? ParticleAdditive : ParticleAlpha;
        out->columns = look->columns > 1 ? (uint32_t)look->columns : 1;
        out->rows = look->rows > 1 ? (uint32_t)look->rows : 1;
        out->fps = look->fps > 0 ? look->fps : 0.0f;
        out->softness = look->softness > 0 ? look->softness : 0.0f;
        out->stretch = look->stretch > 0 ? look->stretch : 0.0f;
        out->lit = glm_clamp(look->lit, 0.0f, 1.0f);
        if (look->shape == ParticleBillboard && pool->corners) {
            out->corners = pool->corners;
        } else if (look->shape != ParticleSprite) {
            out->look = ParticleCircle;
        }
    }

    out->styled = look ? 1u : 0u;
    out->sorted = out->blend == ParticleAlpha && (curves || look) ? 1u : 0u;
    out->sort_count = out->sorted ? box_particles_sort_count(pool->used, pool->capacity) : 0;
}

/** Get the texture handle of the sprite of an appearance.
 *
 * @param world The world
 * @param look The ParticleAppearance, NULL for none
 * @return The handle, 0 for white
 */
static uint32_t box_particles_texture(
    ecs_world_t *world,
    const ParticleAppearance *look)
{
    uint32_t result = 0;
    if (look && look->shape == ParticleSprite && look->texture) {
        result = box_textures_handle(box_textures_get(world), look->texture);
    }

    return result;
}

/** Get the intensity a ParticleLight gives its light.
 *
 * @param light The ParticleLight
 * @param time Microseconds of the playback clock
 * @param stopped Whether the emitter stopped since it last played
 * @param faded Microseconds of stopped frames since it stopped
 * @return The intensity
 */
static float box_particles_light_value(
    const ParticleLight *light,
    uint64_t time,
    bool stopped,
    uint64_t faded)
{
    uint64_t period = box_particle_us(light->period);
    uint64_t fade = box_particle_us(light->fade);
    float result = light->intensity, t;
    if (period) {
        if (light->loop) {
            t = (float)((double)(time % period) / (double)period);
        } else {
            t = time >= period ? 1.0f : (float)((double)time / (double)period);
        }

        result *= box_particle_curve(&light->curve.start, t);
    }

    if (stopped) {
        result = faded < fade
            ? result * (float)(1.0 - (double)faded / (double)fade)
            : 0.0f;
    }

    return result;
}

/** The lights a ParticleLight of an emitter drives in a frame */
typedef struct box_particles_light_t {
    const ParticleLight *light; /**< The ParticleLight, NULL for none */
    PointLight *point;      /**< The PointLight of the emitter itself, NULL for none */
    SpotLight *spot;        /**< The SpotLight of the emitter itself, NULL for none */
} box_particles_light_t;

/** Write the intensity of the lights a ParticleLight drives.
 *
 * @param lights The lights
 * @param intensity The intensity
 */
static void box_particles_light_write(
    const box_particles_light_t *lights,
    float intensity)
{
    if (lights->point) {
        lights->point->intensity = intensity;
    }

    if (lights->spot) {
        lights->spot->intensity = intensity;
    }
}

/** Account the frame of one emitter and add its pool to the packet when it has particles.
 *
 * A paused emitter ages and moves nothing and spawns nothing, a stopped one ages its particles
 * and spawns nothing, and a reset empties the pool and zeroes the playback clock in a frame that
 * does nothing else. Only a playing emitter advances the playback clock its bursts go off by.
 *
 * The lights of a ParticleLight follow the clock after the frame.
 *
 * @param world The world
 * @param s The state
 * @param p The packet
 * @param e The emitter
 * @param emitter The emitter values
 * @param transform Its world transform, NULL for the identity
 * @param scene Its scene
 * @param playback Its playback control, NULL to play; receives the clock and a cleared reset
 * @param curves Its ParticleCurves, NULL for none
 * @param look Its ParticleAppearance, NULL for none
 * @param effect The ParticleEffect of the instance it plays in, NULL for none
 * @param lights The lights its ParticleLight drives
 * @param stats Receives its counts
 */
static void box_particles_emitter(
    ecs_world_t *world,
    box_render_particles_t *s,
    box_render_packet_t *p,
    ecs_entity_t e,
    const ParticleEmitter *emitter,
    const WorldTransform *transform,
    ecs_entity_t scene,
    ParticlePlayback *playback,
    const ParticleCurves *curves,
    const ParticleAppearance *look,
    const ParticleEffect *effect,
    const box_particles_light_t *lights,
    ParticleStats *stats)
{
    int32_t max = emitter->max > BOX_PARTICLE_MAX ? BOX_PARTICLE_MAX : emitter->max;
    box_particle_pool_t *pool = ecs_map_get_deref(&s->by_entity, box_particle_pool_t, e);
    uint32_t life_min = box_particle_us(emitter->lifetime.min);
    uint32_t life_max = box_particle_us(emitter->lifetime.max);
    uint32_t first, count = 0, slots = 0, delta_us = box_particle_us(p->delta), age_us;
    PlaybackState state = playback ? playback->state : ParticlePlaying;
    bool reset = playback && playback->reset;
    uint64_t time = 0;
    bool stopped = state == ParticleStopped;
    uint64_t faded = UINT64_MAX;
    bool moves;
    vec3 velocity;
    if (max <= 0) {
        goto done;
    }

    if (!pool) {
        pool = box_particles_pool_new(s, p, e, max);
    } else if (pool->max != max) {
        box_particles_range_free(s, p, pool);
        box_particles_empty(pool);
        pool->max = max;
        pool->capacity = box_particles_class(max);
        box_particles_range_alloc(s, p, pool);
    }

    if (pool->kept) {
        box_particles_empty(pool);
        pool->time = 0;
        pool->started = false;
        pool->stopped = false;
        pool->has_last = false;
        pool->fresh = true;
        pool->kept = false;
        pool->faded = 0;
    }

    pool->seen = true;
    pool->scene = scene;
    pool->linger = emitter->linger;
    if (emitter->shape == EmitterMesh) {
        box_particles_table_update(world, s, p, pool, e, emitter);
    } else if (pool->table_built) {
        box_particles_table_free(s, p, pool);
    }

    if (reset) {
        box_particles_empty(pool);
        pool->time = 0;
        pool->started = false;
        pool->fresh = true;
    }

    if (state == ParticleStopped) {
        pool->stopped = true;
        pool->faded += delta_us;
    } else if (state != ParticlePaused && pool->stopped) {
        pool->time = 0;
        pool->started = false;
        pool->carry = 0;
        pool->stopped = false;
        pool->has_last = false;
        pool->faded = 0;
    }

    if (look && look->shape == ParticleBillboard) {
        box_particles_mesh_update(world, s, p, pool, look->mesh);
    } else if (pool->mesh_built) {
        box_particles_mesh_free(s, p, pool);
    }


    box_particles_velocity(pool, transform, p->delta, velocity);
    age_us = reset || state == ParticlePaused ? 0 : delta_us;
    pool->clock += age_us;
    box_particles_retire(pool);
    moves = pool->live != 0;
    first = pool->next;
    if (!reset && state != ParticlePaused && state != ParticleStopped) {
        uint64_t t0 = pool->time;
        pool->time += delta_us;
        count = box_particles_owed(pool, emitter, p->delta,
            box_particles_bursts_owed(s, e, t0, pool->time, !pool->started));
        pool->started = true;
    }

    time = pool->time;
    stopped = pool->stopped;
    faded = pool->faded;
    box_particles_spawn(pool, emitter, count, life_min,
        life_max > life_min ? life_max - life_min : 0,
        box_render_packet_reserve(p, count * (uint32_t)sizeof(uint32_t), &slots));
    pool->next += count;
    stats->spawned += (int32_t)count;
    stats->particles += pool->live;
    if (!pool->used) {
        goto done;
    }

    box_render_particle_pool_t *entry = ecs_vec_append_t(
        NULL, &p->particles, box_render_particle_pool_t);
    ecs_os_memset_t(entry, 0, box_render_particle_pool_t);
    entry->entity = e;
    entry->scene = scene;
    entry->buffer = pool->page;
    entry->offset = pool->base * BOX_RENDER_PARTICLE_SIZE;
    entry->capacity = pool->capacity;
    entry->clear = pool->fresh;
    entry->table = pool->table;
    entry->slots = slots;
    entry->texture = box_particles_texture(world, look);
    entry->mesh = pool->mesh;
    const ParticleForces *own = e != ecs_id(ParticleForces)
        ? ecs_get(world, e, ParticleForces)
        : NULL;
    ParticleForces forces;
    box_particles_forces(world, e, emitter->forces, own, &forces);
    glm_vec3_scale(velocity, emitter->inherit_velocity, velocity);
    box_particles_block(emitter, transform, ecs_get(world, e, ParticleCollision), velocity,
        age_us ? p->delta : 0.0f, effect && effect->scale > 0 ? effect->scale : 1.0f,
        &entry->block);
    box_particles_block_forces(pool, emitter->acceleration, &forces, moves, &entry->block);
    box_particles_block_look(curves, look, pool, &entry->block);
    entry->block.count = count;
    entry->block.first = first;
    entry->block.used = pool->used;
    entry->block.points = (uint32_t)pool->points;
    entry->block.delta_us = age_us;
    entry->block.life_min = life_min;
    entry->block.life_span = life_max > life_min ? life_max - life_min : 0;
    pool->fresh = false;
    if (emitter->linger) {
        pool->entry = *entry;
        glm_vec3_copy(ECS_CONST_CAST(float*, emitter->acceleration), pool->acceleration);
        pool->forces = emitter->forces;
        pool->has_own = own != NULL;
        if (own) {
            pool->own = *own;
        }
    }

    stats->simulated ++;
done:
    if (playback) {
        playback->time = (float)((double)time * 1e-6);
        playback->reset = false;
    }

    if (lights->light) {
        box_particles_light_write(lights,
            box_particles_light_value(lights->light, time, stopped, faded));
    }
}

/** Run a frame of a lingering pool: age and move its particles with the block of the last frame
 * of its emitter, spawning nothing, under the forces of this frame: those of the world and of
 * the entity the emitter named, and the last ones of the emitter itself.
 *
 * @param world The world
 * @param p The packet
 * @param pool The lingering pool
 * @param stats Receives its counts
 * @return Whether particles live on, else the pool is to be freed
 */
static bool box_particles_linger(
    const ecs_world_t *world,
    box_render_packet_t *p,
    box_particle_pool_t *pool,
    ParticleStats *stats)
{
    ParticleForces forces;
    uint32_t delta_us = box_particle_us(p->delta), slots = 0;
    bool result = false;
    pool->clock += delta_us;
    box_particles_retire(pool);
    if (!pool->used) {
        goto done;
    }

    box_render_packet_reserve(p, 0, &slots);
    box_render_particle_pool_t *entry = ecs_vec_append_t(
        NULL, &p->particles, box_render_particle_pool_t);
    *entry = pool->entry;
    entry->clear = false;
    entry->slots = slots;
    entry->block.acceleration[3] = delta_us ? p->delta : 0.0f;
    box_particles_forces(world, pool->entity, pool->forces, pool->has_own ? &pool->own : NULL,
        &forces);
    box_particles_block_forces(pool, pool->acceleration, &forces, pool->live != 0,
        &entry->block);
    entry->block.count = 0;
    entry->block.first = pool->next;
    entry->block.used = pool->used;
    entry->block.delta_us = delta_us;
    entry->block.sort_count = entry->block.sorted
        ? box_particles_sort_count(pool->used, pool->capacity)
        : 0;
    stats->particles += pool->live;
    stats->simulated ++;
    stats->lingering ++;
    result = true;
done:
    return result;
}

/** Find the ParticleEffect of the instance the emitters of a table play in, on the emitters
 * themselves or on the nearest ancestor that has one.
 *
 * Searched per table instead of matched by the query, whose up traversal allocates. The rows of
 * a table with a Parent component have parents of their own, so their ancestor is searched per
 * emitter.
 *
 * @param world The world
 * @param table The table of the emitters
 * @param e The emitter whose ancestor is searched, 0 to search the ancestor of the table
 * @param src Receives the ancestor that has it, 0 when the emitters have it or none does
 * @return Whether the emitters have it themselves
 */
static bool box_particles_effect_of(
    const ecs_world_t *world,
    ecs_table_t *table,
    ecs_entity_t e,
    ecs_entity_t *src)
{
    bool result = false;
    *src = 0;
    if (ecs_table_has_id(world, table, ecs_id(ParticleEffect))) {
        result = true;
    } else if (e) {
        *src = ecs_get_target_for_id(world, e, EcsChildOf, ecs_id(ParticleEffect));
    } else if (!ecs_table_has_id(world, table, ecs_id(EcsParent)) &&
        ecs_search_relation(world, table, 0, ecs_id(ParticleEffect), EcsChildOf, EcsUp, src,
            NULL, NULL) == -1)
    {
        *src = 0;
    }

    return result;
}

/** Check whether the pool of an emitter that is not enabled in this frame is kept: the emitter
 * is a disabled layer of an effect instance, which plays again from the pool of its template.
 *
 * @param world The world
 * @param e The emitter
 * @return Whether the pool is kept
 */
static bool box_particles_keeps(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    bool result = false;
    if (ecs_is_alive(world, e) && ecs_has_id(world, e, EcsDisabled) &&
        ecs_has(world, e, ParticleEmitter))
    {
        result = ecs_get_target_for_id(world, e, EcsChildOf, ecs_id(ParticleEffect)) != 0;
    }

    return result;
}

/** Account the particles of every enabled emitter, free the pools of the emitters that are
 * gone, keep those of disabled effect layers, run out those of deleted lingering emitters and
 * write ParticleStats.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractParticles(
    ecs_iter_t *it)
{
    box_render_particles_t *s = it->ctx;
    ecs_world_t *world = it->world;
    const box_render_backend_t *backend = box_render_get_backend(world);
    box_render_packet_t *p = NULL;
    ParticleStats stats = {0};
    int32_t i;
    if (!s->emitters) {
        goto done;
    }

    p = box_render_packet(world);
    if (backend->ctx != s->backend || backend->render != s->render ||
        backend->generation != s->generation)
    {
        box_particles_reset_backend(s);
        s->backend = backend->ctx;
        s->render = backend->render;
        s->generation = backend->generation;
    }

    for (i = 0; i < ecs_vec_count(&s->pools); i ++) {
        (*ecs_vec_get_t(&s->pools, box_particle_pool_t*, i))->seen = false;
    }

    box_particles_bursts_collect(world, s);
    ecs_iter_t qit = ecs_query_iter(world, s->emitters);
    while (ecs_query_next(&qit)) {
        const ParticleEmitter *emitters = ecs_field(&qit, ParticleEmitter, 0);
        const WorldTransform *transforms = ecs_field(&qit, WorldTransform, 1);
        ParticlePlayback *playbacks = ecs_field(&qit, ParticlePlayback, 3);
        const ParticleCurves *curves = ecs_field(&qit, ParticleCurves, 4);
        const ParticleAppearance *looks = ecs_field(&qit, ParticleAppearance, 5);
        const ParticleLight *lights = ecs_field(&qit, ParticleLight, 6);
        PointLight *points = ecs_field(&qit, PointLight, 7);
        SpotLight *spots = ecs_field(&qit, SpotLight, 8);
        bool lights_shared = lights && !ecs_field_is_self(&qit, 6);
        bool shared = ecs_field_is_set(&qit, 1) && !ecs_field_is_self(&qit, 1);
        bool curves_shared = curves && !ecs_field_is_self(&qit, 4);
        bool looks_shared = looks && !ecs_field_is_self(&qit, 5);
        ecs_entity_t effect_src = 0;
        bool effect_self = box_particles_effect_of(world, qit.table, 0, &effect_src);
        bool effect_rows = !effect_self && ecs_table_has_id(world, qit.table, ecs_id(EcsParent));
        const ParticleEffect *shared_effect = effect_src
            ? ecs_get(world, effect_src, ParticleEffect)
            : NULL;
        for (i = 0; i < qit.count; i ++) {
            if (effect_rows) {
                box_particles_effect_of(world, qit.table, qit.entities[i], &effect_src);
                shared_effect = effect_src ? ecs_get(world, effect_src, ParticleEffect) : NULL;
            }

            const WorldTransform *t = transforms ? &transforms[shared ? 0 : i] : NULL;
            box_particles_light_t light = {
                .light = lights ? &lights[lights_shared ? 0 : i] : NULL,
                .point = points ? &points[i] : NULL,
                .spot = spots ? &spots[i] : NULL
            };
            stats.emitters ++;
            box_particles_emitter(world, s, p, qit.entities[i], &emitters[i], t,
                box_render_scene_field(&qit, 2, i), playbacks ? &playbacks[i] : NULL,
                curves ? &curves[curves_shared ? 0 : i] : NULL,
                looks ? &looks[looks_shared ? 0 : i] : NULL,
                effect_self ? ecs_get(world, qit.entities[i], ParticleEffect) : shared_effect,
                &light, &stats);
        }
    }

    for (i = ecs_vec_count(&s->pools) - 1; i >= 0; i --) {
        box_particle_pool_t *pool = *ecs_vec_get_t(&s->pools, box_particle_pool_t*, i);
        if (pool->seen) {
            continue;
        }

        if (!pool->lingering) {
            if (box_particles_keeps(world, pool->entity)) {
                pool->kept = true;
                continue;
            }

            if (!pool->linger || pool->kept || !pool->live ||
                ecs_is_alive(world, pool->entity))
            {
                box_particles_pool_free(s, p, i);
                continue;
            }

            ecs_map_remove(&s->by_entity, pool->entity);
            pool->lingering = true;
        }

        if (!box_particles_linger(world, p, pool, &stats)) {
            box_particles_pool_free(s, p, i);
        }
    }

    for (i = 0; i < ecs_vec_count(&s->pages); i ++) {
        const box_particle_page_t *page = ecs_vec_get_t(&s->pages, box_particle_page_t, i);
        stats.bytes += (int64_t)page->pools * page->capacity * BOX_RENDER_PARTICLE_SIZE;
    }

    const ParticleStats *prev = ecs_singleton_get(world, ParticleStats);
    if (prev) {
        stats.effects = prev->effects;
        stats.recycled = prev->recycled;
        stats.merged = prev->merged;
        stats.dropped = prev->dropped;
    }

    ecs_singleton_set_ptr(world, ParticleStats, &stats);
done:
    return;
}

/** Free the state of the particle extract.
 *
 * @param ctx The state
 */
static void box_particles_free(
    void *ctx)
{
    box_render_particles_t *s = ctx;
    box_particles_reset_backend(s);
    ecs_vec_fini_t(NULL, &s->pools, box_particle_pool_t*);
    ecs_map_fini(&s->by_entity);
    ecs_vec_fini_t(NULL, &s->pages, box_particle_page_t);
    ecs_vec_fini_t(NULL, &s->page_handles, uint32_t);
    ecs_vec_fini_t(NULL, &s->table_handles, uint32_t);
    ecs_vec_fini_t(NULL, &s->points, box_particle_point_t);
    ecs_vec_fini_t(NULL, &s->bursts, box_particle_burst_t);
    ecs_vec_fini_t(NULL, &s->mesh_handles, uint32_t);
    ecs_vec_fini_t(NULL, &s->corners, box_render_particle_corner_t);
    ecs_os_free(s);
}

/** Get the state of the particle extract of a world.
 *
 * @param world The world
 * @return The state, NULL without the extract
 */
static box_render_particles_t* box_particles_state(
    const ecs_world_t *world)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractParticles");
    const ecs_system_t *sys = system ? ecs_system_get(world, system) : NULL;

    return sys ? sys->ctx : NULL;
}

void box_render_particles_init(
    ecs_world_t *world)
{
    box_render_particles_t *s = ecs_os_calloc_t(box_render_particles_t);
    ecs_vec_init_t(NULL, &s->pools, box_particle_pool_t*, 0);
    ecs_map_init(&s->by_entity, NULL);
    ecs_vec_init_t(NULL, &s->pages, box_particle_page_t, 0);
    ecs_vec_init_t(NULL, &s->page_handles, uint32_t, 0);
    ecs_vec_init_t(NULL, &s->table_handles, uint32_t, 0);
    ecs_vec_init_t(NULL, &s->points, box_particle_point_t, 0);
    ecs_vec_init_t(NULL, &s->bursts, box_particle_burst_t, 0);
    ecs_vec_init_t(NULL, &s->mesh_handles, uint32_t, 0);
    ecs_vec_init_t(NULL, &s->corners, box_render_particle_corner_t, 0);
    s->next_mesh = BOX_RENDER_BUFFER_PARTICLE_MESH_FIRST;
    s->next_page = BOX_RENDER_BUFFER_PARTICLE_FIRST;
    s->next_table = BOX_RENDER_BUFFER_PARTICLE_TABLE_FIRST;

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RenderExtractParticles" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractParticles,
        .ctx = s,
        .ctx_free = box_particles_free
    });
}

void box_render_particles_import(
    ecs_world_t *world)
{
    box_render_particles_t *s = box_particles_state(world);
    if (s && !s->emitters) {
        s->emitters = ecs_query(world, {
            .terms = {
                { .id = ecs_id(ParticleEmitter), .inout = EcsIn },
                { .id = ecs_id(WorldTransform), .inout = EcsIn, .oper = EcsOptional },
                {
                    .id = ecs_id(Scene),
                    .src.id = EcsSelf | EcsUp,
                    .trav = EcsChildOf,
                    .inout = EcsInOutNone,
                    .oper = EcsOptional
                },
                {
                    .id = ecs_id(ParticlePlayback),
                    .src.id = EcsSelf,
                    .inout = EcsInOut,
                    .oper = EcsOptional
                },
                { .id = ecs_id(ParticleCurves), .inout = EcsIn, .oper = EcsOptional },
                { .id = ecs_id(ParticleAppearance), .inout = EcsIn, .oper = EcsOptional },
                { .id = ecs_id(ParticleLight), .inout = EcsIn, .oper = EcsOptional },
                {
                    .id = ecs_id(PointLight),
                    .src.id = EcsSelf,
                    .inout = EcsInOut,
                    .oper = EcsOptional
                },
                {
                    .id = ecs_id(SpotLight),
                    .src.id = EcsSelf,
                    .inout = EcsInOut,
                    .oper = EcsOptional
                }
            },
            .cache_kind = EcsQueryCacheAuto
        });
        s->burst_query = ecs_query(world, {
            .terms = {
                { .id = ecs_id(ParticleBurst), .inout = EcsIn },
                {
                    .id = ecs_id(ParticleEmitter),
                    .src.id = EcsSelf | EcsUp,
                    .trav = EcsChildOf,
                    .inout = EcsInOutNone
                }
            },
            .cache_kind = EcsQueryCacheAuto
        });
    }
}

box_render_particles_t* box_render_particles_get(
    const ecs_world_t *world)
{
    box_render_particles_t *result = box_particles_state(world);

    return result && result->emitters ? result : NULL;
}

int32_t box_render_particles_live(
    const box_render_particles_t *s,
    ecs_entity_t emitter)
{
    const box_particle_pool_t *pool = s
        ? ecs_map_get_deref(&s->by_entity, box_particle_pool_t, emitter)
        : NULL;

    return pool && !pool->kept ? pool->live : 0;
}

bool box_render_particles_use(
    const box_render_packet_t *p,
    uint32_t handle)
{
    bool result = false;
    if (handle == BOX_RENDER_SHADER_PARTICLE_SIM || handle == BOX_RENDER_SHADER_PARTICLE_DRAW) {
        result = ecs_vec_count(&p->particles) != 0;
    }

    return result;
}
