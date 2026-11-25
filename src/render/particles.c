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
#include "../particle/curves.h"
#include "../particle/timeline.h"
#include "particle_rows.h"
#include "../geometry/mesh_cache.h"
#include "../texture/texture.h"
#include <math.h>
#include <stdlib.h>

/** Singleton with the system of the extract, so its state is found without a lookup */
typedef struct RenderParticlesSystem {
    ecs_entity_t system;        /**< The system, whose context is the state */
} RenderParticlesSystem;

static ECS_COMPONENT_DECLARE(RenderParticlesSystem);

/** Get the system of the particle extract of a world.
 *
 * @param world The world
 * @return The system, NULL before box.render was imported or after the system was deleted
 */
static const ecs_system_t* box_particles_system_ctx(
    const ecs_world_t *world)
{
    ecs_entity_t id = ecs_id(RenderParticlesSystem);
    const RenderParticlesSystem *c = id && ecs_is_alive(world, id)
        ? ecs_get_id(world, id, id) : NULL;

    return c && c->system && ecs_is_alive(world, c->system)
        ? ecs_system_get(world, c->system) : NULL;
}

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

/** A range of the arena that holds pools of one size */
typedef struct box_particle_page_t {
    uint32_t base;          /**< First slot of the page in the arena */
    uint32_t slots;         /**< Slots of the page */
    uint32_t capacity;      /**< Slots of each of its pools */
    uint32_t pools;         /**< Pools it holds */
    uint64_t occupied;      /**< Bit per pool in use */
} box_particle_page_t;

/** The pool of an emitter and the accounting of its particles */
typedef struct box_particle_pool_t {
    ecs_entity_t entity;    /**< Entity with the ParticleEmitter */
    ecs_entity_t scene;     /**< Scene of the emitter in this frame */
    int32_t max;            /**< Clamped ParticleEmitter.max the pool was made for */
    uint32_t page;          /**< Index of its page plus one, 0 for none */
    uint32_t base;          /**< First slot of the pool in the arena */
    uint32_t capacity;      /**< Particle slots of the pool */
    bool card;              /**< The pool keeps the card words of ParticleCard after its
                             *   particle slots */
    bool fresh;             /**< The range was not cleared on the backend yet */
    bool seen;              /**< The emitter is enabled in this frame */
    bool kept;              /**< The emitter of an effect instance was disabled, so the pool
                             *   was kept, emptied when the emitter is enabled again */
    uint64_t clock;         /**< Microseconds the pool has run */
    double carry;           /**< Fraction of a particle owed to the next frame */
    uint64_t time;          /**< Microseconds the emitter has played, ParticlePlayback.time */
    bool started;           /**< A frame played since the playback clock was zeroed */
    bool stopped;           /**< ParticleStopped since the last frame it played */
    bool curved;            /**< The emitter had ParticleCurves or ParticleMotion in the last
                             *   frame; else the extract reads neither curves nor motion */
    uint32_t next;          /**< Spawn index of the next particle */
    int32_t live;           /**< Particles alive */
    uint32_t used;          /**< Slots from the start that were handed out */
    ecs_vec_t deaths;       /**< box_particle_death_t, a heap by clock and slot */
    ecs_vec_t free_slots;   /**< uint32_t, slots below used whose particle died */
    uint32_t table;         /**< Index of its shared mesh table plus one, 0 for none */
    int32_t points;         /**< Points of the mesh table */
    bool table_built;       /**< The table matches the key below */
    ecs_entity_t table_source; /**< Entity the table was sampled from */
    uint32_t table_mesh;    /**< Mesh handle the table was sampled from */
    vec3 table_scale;       /**< Shape scale the table was sampled with */
    uint32_t table_seed;    /**< Seed the table was sampled with */
    dvec3 last_position;    /**< World position of the emitter in the last frame */
    dvec3 origin;           /**< Render origin its world space particles are relative to on
                             *   the GPU, which they move by when it moves */
    bool has_last;          /**< last_position holds a position */
    bool linger;            /**< ParticleEmitter.linger of the last frame the emitter was seen */
    bool local;             /**< ParticleEmitter.local its particles were spawned with */
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
    float scale;            /**< Uniform world scale of the emitter in that frame */
    bool has_own;           /**< The emitter entity had ParticleForces in that frame */
    box_render_particle_pool_t entry; /**< The pool of the last frame of a lingering emitter
                             *   in the packet, which a lingering pool runs again */
    box_particle_rows_use_t curves; /**< Rows of the curve table its curves use */
    box_particle_rows_motion_t motion; /**< Rows of the curve table its ParticleMotion uses */
} box_particle_pool_t;

/** A mesh table that the EmitterMesh pools of the same shape and seed share, a slot of
 * BOX_PARTICLE_MESH_POINTS points of the table buffer */
typedef struct box_particle_table_t {
    uint32_t mesh;          /**< Mesh handle it was sampled from */
    vec3 scale;             /**< Shape scale it was sampled with */
    uint32_t seed;          /**< Seed it was sampled with */
    int32_t points;         /**< Points sampled */
    int32_t refs;           /**< Pools that use it, 0 for a free slot */
} box_particle_table_t;

/** A burst of the frame and the emitter whose pool it spawns into */
typedef struct box_particle_burst_t {
    ecs_entity_t emitter;   /**< Nearest ParticleEmitter at or above the burst */
    ParticleBurst burst;    /**< The burst values */
} box_particle_burst_t;

/** Main thread state of the particle extract */
typedef struct box_render_particles_t {
    ecs_query_t *emitters;  /**< ParticleEmitter, WorldTransform, Scene(self|up), Playback,
                             *   Curves, Appearance, ParticleLight, Point and SpotLight(self)
                             *   and ParticleTimeline, NULL without box.particle */
    ecs_query_t *burst_query; /**< ParticleBurst and ParticleEmitter(self|up), NULL without
                             *   box.particle */
    ecs_vec_t bursts;       /**< box_particle_burst_t of the frame, sorted by emitter */
    ecs_vec_t pools;        /**< box_particle_pool_t* */
    ecs_map_t by_entity;    /**< Entity to box_particle_pool_t* */
    ecs_vec_t pages;        /**< box_particle_page_t, kept when empty to be reused */
    uint32_t arena_slots;   /**< Slots of the arena the pages take */
    uint32_t arena_capacity; /**< Slots of the arena buffer sent to the backend */
    ecs_vec_t tables;       /**< box_particle_table_t, a slot of the table buffer each */
    uint32_t table_capacity; /**< Tables the table buffer sent to the backend holds */
    ecs_vec_t points;       /**< box_particle_point_t, the table being sampled */
    const void *backend;    /**< Context of the backend the pages were sent to */
    void (*render)(void*, const box_render_packet_t*); /**< Render callback of that backend */
    int32_t generation;     /**< Generation of that backend, see box_render_backend_t */
    box_particle_rows_t curves; /**< The curve table */
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

/** Make the arena buffer hold its pages, created again twice as large with its particles kept
 * when they outgrow it.
 *
 * @param s The state
 * @param p The packet, receives the arena when it grows
 * @return Whether the pages fit the largest arena
 */
static bool box_particles_arena_fit(
    box_render_particles_t *s,
    box_render_packet_t *p)
{
    uint32_t capacity = s->arena_capacity ? s->arena_capacity : BOX_PARTICLE_PAGE_SLOTS;
    bool result = s->arena_slots <= BOX_RENDER_PARTICLE_ARENA_MAX;
    if (!result) {
        ecs_err("particle pools need %u slots, more than the %u of the arena",
            s->arena_slots, BOX_RENDER_PARTICLE_ARENA_MAX);
        goto done;
    }

    if (s->arena_slots <= s->arena_capacity) {
        goto done;
    }

    while (capacity < s->arena_slots) {
        capacity *= 2;
    }

    capacity = capacity < BOX_RENDER_PARTICLE_ARENA_MAX ? capacity : BOX_RENDER_PARTICLE_ARENA_MAX;
    box_render_buffer_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_BUFFER,
        BOX_RENDER_CREATE, BOX_RENDER_BUFFER_PARTICLE_FIRST, sizeof(box_render_buffer_desc_t));
    desc->size = (uint64_t)capacity * BOX_RENDER_PARTICLE_SIZE;
    desc->usage = BOX_RENDER_BUFFER_STORAGE | BOX_RENDER_BUFFER_KEEP;
    s->arena_capacity = capacity;
done:
    return result;
}

/** Destroy the arena once no pool holds a range of it and the mesh tables once no pool reads
 * one, so a world without emitters keeps no particle memory.
 *
 * @param s The state
 * @param p The packet, receives the destroys
 */
static void box_particles_arena_release(
    box_render_particles_t *s,
    box_render_packet_t *p)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&s->pages); i ++) {
        if (ecs_vec_get_t(&s->pages, box_particle_page_t, i)->occupied) {
            break;
        }
    }

    if (s->arena_capacity && i == ecs_vec_count(&s->pages)) {
        box_render_packet_resource(p, BOX_RENDER_BUFFER, BOX_RENDER_DESTROY,
            BOX_RENDER_BUFFER_PARTICLE_FIRST, 0);
        ecs_vec_clear(&s->pages);
        s->arena_slots = 0;
        s->arena_capacity = 0;
    }

    for (i = 0; i < ecs_vec_count(&s->tables); i ++) {
        if (ecs_vec_get_t(&s->tables, box_particle_table_t, i)->refs) {
            break;
        }
    }

    if (s->table_capacity && i == ecs_vec_count(&s->tables)) {
        box_render_packet_resource(p, BOX_RENDER_BUFFER, BOX_RENDER_DESTROY,
            BOX_RENDER_BUFFER_PARTICLE_TABLE_FIRST, 0);
        ecs_vec_clear(&s->tables);
        s->table_capacity = 0;
    }
}

/** Get the slots of the range of a pool: its particle slots, and for a ParticleCard pool the
 * slots of its card words after them.
 *
 * @param pool The pool, its capacity and card set
 * @return The slots
 */
static uint32_t box_particles_range_slots(
    const box_particle_pool_t *pool)
{
    return pool->capacity + (pool->card ? box_particle_card_slots(pool->capacity) : 0u);
}

/** Give a pool a range of slots in a page of its size: a page of that size with a free range,
 * else an empty page of as many slots, else a new page at the end of the arena.
 *
 * @param s The state
 * @param p The packet, receives the arena when it grows
 * @param pool The pool, its capacity and card set
 */
static void box_particles_range_alloc(
    box_render_particles_t *s,
    box_render_packet_t *p,
    box_particle_pool_t *pool)
{
    box_particle_page_t *page = NULL;
    uint32_t range = box_particles_range_slots(pool);
    uint32_t pools = BOX_PARTICLE_PAGE_SLOTS / range, bit;
    int32_t i, index = -1;
    pools = pools < 1 ? 1 : pools > BOX_PARTICLE_PAGE_POOLS ? BOX_PARTICLE_PAGE_POOLS : pools;
    for (i = 0; i < ecs_vec_count(&s->pages); i ++) {
        box_particle_page_t *candidate = ecs_vec_get_t(&s->pages, box_particle_page_t, i);
        uint64_t full = candidate->pools >= 64 ? UINT64_MAX
            : (((uint64_t)1 << candidate->pools) - 1);
        if (candidate->capacity == range && candidate->occupied != full) {
            index = i;
            break;
        }
    }

    for (i = 0; index < 0 && i < ecs_vec_count(&s->pages); i ++) {
        box_particle_page_t *candidate = ecs_vec_get_t(&s->pages, box_particle_page_t, i);
        if (!candidate->occupied && candidate->slots == pools * range) {
            candidate->capacity = range;
            candidate->pools = pools;
            index = i;
        }
    }

    if (index < 0) {
        page = ecs_vec_append_t(NULL, &s->pages, box_particle_page_t);
        ecs_os_memset_t(page, 0, box_particle_page_t);
        page->base = s->arena_slots;
        page->slots = pools * range;
        page->capacity = range;
        page->pools = pools;
        s->arena_slots += page->slots;
        index = ecs_vec_count(&s->pages) - 1;
        box_particles_arena_fit(s, p);
    }

    page = ecs_vec_get_t(&s->pages, box_particle_page_t, index);
    for (bit = 0; bit < page->pools; bit ++) {
        if (!(page->occupied & ((uint64_t)1 << bit))) {
            break;
        }
    }

    page->occupied |= (uint64_t)1 << bit;
    pool->page = (uint32_t)index + 1;
    pool->base = page->base + bit * page->capacity;
    pool->fresh = true;
}

/** Return the range of a pool to its page, which stays in the arena when it empties.
 *
 * @param s The state
 * @param p The packet
 * @param pool The pool
 */
static void box_particles_range_free(
    box_render_particles_t *s,
    box_render_packet_t *p,
    box_particle_pool_t *pool)
{
    (void)p;
    if (pool->page && pool->page <= (uint32_t)ecs_vec_count(&s->pages)) {
        box_particle_page_t *page = ecs_vec_get_t(
            &s->pages, box_particle_page_t, (int32_t)pool->page - 1);
        page->occupied &= ~((uint64_t)1 << ((pool->base - page->base) / page->capacity));
    }

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

/** Release the shared mesh table of a pool.
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
    (void)p;
    if (pool->table && pool->table <= (uint32_t)ecs_vec_count(&s->tables)) {
        ecs_vec_get_t(&s->tables, box_particle_table_t, (int32_t)pool->table - 1)->refs --;
    }

    pool->table = 0;
    pool->points = 0;
    pool->table_built = false;
}

/** Make the pool of an emitter.
 *
 * @param s The state
 * @param p The packet
 * @param e The emitter
 * @param max Clamped ParticleEmitter.max, at least 1
 * @param card The emitter draws ParticleCard, which keeps card words
 * @return The pool
 */
static box_particle_pool_t* box_particles_pool_new(
    box_render_particles_t *s,
    box_render_packet_t *p,
    ecs_entity_t e,
    int32_t max,
    bool card)
{
    box_particle_pool_t *result = ecs_os_calloc_t(box_particle_pool_t);
    result->entity = e;
    result->max = max;
    result->capacity = box_particles_class(max);
    result->card = card;
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
    }

    box_particle_rows_release(&s->curves, &pool->curves);
    box_particle_rows_motion_release(&s->curves, &pool->motion);

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
    ecs_vec_clear(&s->tables);
    box_particle_rows_reset(&s->curves);
    s->arena_slots = 0;
    s->arena_capacity = 0;
    s->table_capacity = 0;
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

    box_particles_table_free(s, p, pool);
    int32_t i, free_slot = -1;
    for (i = 0; i < ecs_vec_count(&s->tables); i ++) {
        box_particle_table_t *t = ecs_vec_get_t(&s->tables, box_particle_table_t, i);
        if (t->refs && t->mesh == mesh && t->seed == emitter->seed &&
            glm_vec3_eqv(t->scale, scale))
        {
            break;
        }

        if (!t->refs && free_slot < 0) {
            free_slot = i;
        }
    }

    if (i == ecs_vec_count(&s->tables)) {
        const box_mesh_t *m = mesh && cache ? box_mesh_cache_get(cache, mesh) : NULL;
        ecs_vec_set_count_t(NULL, &s->points, box_particle_point_t, BOX_PARTICLE_MESH_POINTS);
        int32_t points = box_particle_mesh_table(m, scale, emitter->seed,
            ecs_vec_first(&s->points), BOX_PARTICLE_MESH_POINTS);
        if (!points) {
            goto built;
        }

        i = free_slot >= 0 ? free_slot : ecs_vec_count(&s->tables);
        if (i == ecs_vec_count(&s->tables)) {
            ecs_vec_append_t(NULL, &s->tables, box_particle_table_t);
        }

        box_particle_table_t *t = ecs_vec_get_t(&s->tables, box_particle_table_t, i);
        t->mesh = mesh;
        t->seed = emitter->seed;
        glm_vec3_copy(scale, t->scale);
        t->points = points;
        t->refs = 0;
        if ((uint32_t)ecs_vec_count(&s->tables) > s->table_capacity) {
            uint32_t capacity = s->table_capacity ? s->table_capacity : 4;
            while (capacity < (uint32_t)ecs_vec_count(&s->tables)) {
                capacity *= 2;
            }

            box_render_buffer_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_BUFFER,
                BOX_RENDER_CREATE, BOX_RENDER_BUFFER_PARTICLE_TABLE_FIRST,
                sizeof(box_render_buffer_desc_t));
            desc->size = (uint64_t)capacity * BOX_PARTICLE_MESH_POINTS *
                sizeof(box_particle_point_t);
            desc->usage = BOX_RENDER_BUFFER_STORAGE | BOX_RENDER_BUFFER_KEEP;
            s->table_capacity = capacity;
        }

        uint32_t size = (uint32_t)points * (uint32_t)sizeof(box_particle_point_t);
        ecs_os_memcpy(box_render_packet_upload(p, BOX_RENDER_BUFFER_PARTICLE_TABLE_FIRST,
            (uint32_t)i * BOX_PARTICLE_MESH_POINTS * (uint32_t)sizeof(box_particle_point_t),
            size), ecs_vec_first(&s->points), (ecs_size_t)size);
    }

    box_particle_table_t *t = ecs_vec_get_t(&s->tables, box_particle_table_t, i);
    t->refs ++;
    pool->table = (uint32_t)i + 1;
    pool->points = t->points;
built:
    pool->table_built = true;
    pool->table_source = source;
    pool->table_mesh = mesh;
    pool->table_seed = emitter->seed;
    glm_vec3_copy(scale, pool->table_scale);
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
 * @param seconds Seconds of emission of the frame at the rate, see
 *                box_particle_timeline_seconds
 * @param burst Particles the bursts of the frame add
 * @return Particles to spawn, at most the free slots of the pool
 */
static uint32_t box_particles_owed(
    box_particle_pool_t *pool,
    const ParticleEmitter *emitter,
    double seconds,
    double burst)
{
    uint32_t free = (uint32_t)(pool->max - pool->live);
    double whole;
    pool->carry += (emitter->rate > 0 ? (double)emitter->rate : 0.0) * seconds;
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
        sum->global_wind += f->global_wind;
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

/** Write the global wind of a frame into the block of a pool: the velocity of the Wind times
 * the share into global_wind, and its gust field, the values the WindSway records pack
 * (box_render_wind_frame), into the w of turbulence, wander and inherit.
 *
 * Without a share, a Wind or a speed every one of them is 0 and the integrate pass leaves the
 * wind of the particles as it is.
 *
 * @param world The world
 * @param share ParticleForces.global_wind summed over the sources, times the world scale
 * @param out The block
 */
static void box_particles_block_wind(
    const ecs_world_t *world,
    float share,
    box_render_particle_block_t *out)
{
    box_render_wind_t wind;
    glm_vec3_zero(out->global_wind);
    out->turbulence[3] = 0.0f;
    out->wander[3] = 0.0f;
    out->inherit[3] = 0.0f;
    if (!isfinite(share) || share == 0.0f) {
        goto done;
    }

    box_render_wind_frame(world, &wind);
    if (!(wind.speed > 0.0f)) {
        goto done;
    }

    out->global_wind[0] = wind.direction[0] * wind.speed * share;
    out->global_wind[1] = wind.direction[1] * wind.speed * share;
    out->global_wind[2] = wind.gust;
    out->turbulence[3] = wind.frequency;
    out->wander[3] = wind.offset[0];
    out->inherit[3] = wind.offset[1];
done:
    return;
}

/** Write the forces of a frame into the block of a pool and move the frame of reference of the
 * pool by the drift of the frame, so the wander field moves with the particles.
 *
 * The field is offset by the displacement before this frame's drift, where the integrate pass
 * samples it. A frame with no particles to move puts the frame back at the world origin, so a
 * burst starts in the field it has without drift.
 *
 * @param pool The pool, its frame of reference advanced
 * @param acceleration ParticleEmitter.acceleration, already scaled
 * @param forces The sum of the forces on its particles
 * @param scale The uniform scale of the emitter, box_transform_world_scale: multiplier of the
 *              gravity, wind, turbulence and drift and divisor of the frequency of the wander
 *              field, so a scaled emitter moves a scaled copy of its particles; the drag is a
 *              rate and stays; turbulence z hands it to the curves of ParticleMotion
 * @param moves Whether the integrate pass moves particles in this frame
 * @param world The world, whose Wind singleton the global wind reads
 * @param out The block, its seed and its seconds of the frame written
 */
static void box_particles_block_forces(
    box_particle_pool_t *pool,
    const vec3 acceleration,
    const ParticleForces *forces,
    float scale,
    bool moves,
    const ecs_world_t *world,
    box_render_particle_block_t *out)
{
    float phase[3], frequency = scale > 0 ? forces->turbulence_scale / scale : 0.0f;
    vec3 gravity;
    int32_t c;
    if (!moves) {
        pool->frame[0] = pool->frame[1] = pool->frame[2] = 0;
    }

    glm_vec3_scale(ECS_CONST_CAST(float*, forces->gravity), scale, gravity);
    glm_vec3_add(ECS_CONST_CAST(float*, acceleration), gravity, out->acceleration);
    glm_vec3_scale(ECS_CONST_CAST(float*, forces->wind), scale, out->wind);
    out->wind[3] = forces->drag > 0 ? forces->drag : 0.0f;
    out->turbulence[0] = forces->turbulence * scale;
    out->turbulence[1] = frequency;
    out->turbulence[2] = scale;
    double origin[3], frame[3];
    box_render_origin(world, origin);
    for (c = 0; c < 3; c ++) {
        frame[c] = pool->frame[c] - origin[c];
    }

    box_particle_phase(out->seed, phase);
    box_particle_frame_phase(phase, frame, frequency, out->wander);
    glm_vec3_scale(ECS_CONST_CAST(float*, forces->drift), scale, out->drift);
    for (c = 0; c < 3; c ++) {
        pool->frame[c] += moves ? (double)out->drift[c] * (double)out->acceleration[3] : 0.0;
    }

    box_particles_block_wind(world, forces->global_wind * scale, out);
}

/** Write the parameters of an emitter for this frame but its forces.
 *
 * @param emitter The emitter values
 * @param transform World transform of the emitter
 * @param origin Render origin of the frame
 * @param collision Its ground collision, NULL for none
 * @param inherit World velocity its spawns get
 * @param delta Seconds of the frame
 * @param scale Multiplier of the speeds, radial speeds and sizes, the uniform scale of the
 *              transform, which holds the scale of the effect it plays in
 * @param out Receives the block, every padding member zero
 */
static void box_particles_block(
    const ParticleEmitter *emitter,
    const WorldTransform *transform,
    const double *origin,
    const ParticleCollision *collision,
    const vec3 inherit,
    float delta,
    float scale,
    box_render_particle_block_t *out)
{
    float spread = glm_clamp(emitter->spread, 0.0f, 180.0f);
    mat4 model;
    ecs_os_memset_t(out, 0, box_render_particle_block_t);
    box_transform_mat4(transform, origin, model);
    ecs_os_memcpy_t(out->model, model, mat4);

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
    out->speed[0] = emitter->speed.min * scale;
    out->speed[1] = emitter->speed.max * scale;
    out->radial[0] = emitter->radial.min * scale;
    out->radial[1] = emitter->radial.max * scale;
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
        out->ground[0] = (float)((double)collision->height - origin[1]);
        out->ground[1] = glm_clamp(collision->bounce, 0.0f, 1.0f);
        out->ground[2] = glm_clamp(collision->friction, 0.0f, 1.0f);
        out->collide = collision->die ? BOX_PARTICLE_GROUND_DIE : BOX_PARTICLE_GROUND_BOUNCE;
    }
}

/** Measure the world velocity of an emitter from its position in the last frame.
 *
 * @param pool The pool of the emitter, keeps the position
 * @param transform World transform of the emitter
 * @param delta Seconds of the frame
 * @param out Receives the velocity, zero in the first frame and for a frame without time
 */
static void box_particles_velocity(
    box_particle_pool_t *pool,
    const WorldTransform *transform,
    float delta,
    vec3 out)
{
    int32_t i;
    glm_vec3_zero(out);
    if (pool->has_last && delta > 0) {
        for (i = 0; i < 3; i ++) {
            out[i] = (float)((transform->position[i] - pool->last_position[i]) / (double)delta);
        }
    }

    box_dvec3_copy(transform->position, pool->last_position);
    pool->has_last = true;
}

_Static_assert(offsetof(box_render_particle_block_t, global_wind) ==
    offsetof(box_render_particle_block_t, gradient_row) + sizeof(uint32_t),
    "the curves of a block end at gradient_row");
_Static_assert(offsetof(box_render_particle_block_t, sampled) ==
    offsetof(box_render_particle_block_t, motion_force) + 4 * sizeof(float),
    "the motion ends the curves and motion of a block");

/** Write the move of the render origin since the last frame of a pool into the w lanes of its
 * block, which its world space particles move by before they move, and take the render origin
 * of this frame as theirs.
 *
 * @param pool The pool
 * @param origin Render origin of the frame
 * @param out The block
 */
static void box_particles_block_shift(
    box_particle_pool_t *pool,
    const double *origin,
    box_render_particle_block_t *out)
{
    vec3 shift = { 0, 0, 0 };
    if (!pool->local) {
        box_dvec3_rel(pool->origin, origin, shift);
    }

    out->color[3] = shift[0];
    out->variance[3] = shift[1];
    out->ground[3] = shift[2];
    box_dvec3_copy(origin, pool->origin);
}

/** The curves and motion of the block of an emitter without ParticleCurves and ParticleMotion,
 * which change nothing */
static const box_render_particle_block_t box_particles_still = {
    .curve_rows = {BOX_PARTICLE_CURVE_CONSTANT, BOX_PARTICLE_CURVE_CONSTANT,
        BOX_PARTICLE_CURVE_CONSTANT, BOX_PARTICLE_CURVE_CONSTANT},
    .curve_values = {1.0f, 1.0f, 0.0f, 1.0f},
    .gradient = {1.0f, 1.0f, 1.0f},
    .gradient_row = BOX_PARTICLE_CURVE_CONSTANT,
    .curve_max_rows = {BOX_PARTICLE_CURVE_CONSTANT, BOX_PARTICLE_CURVE_CONSTANT,
        BOX_PARTICLE_CURVE_CONSTANT, BOX_PARTICLE_CURVE_CONSTANT},
    .gradient_max_row = BOX_PARTICLE_CURVE_CONSTANT,
    .motion_rows = {BOX_PARTICLE_CURVE_CONSTANT, BOX_PARTICLE_CURVE_CONSTANT,
        BOX_PARTICLE_CURVE_CONSTANT, BOX_PARTICLE_CURVE_CONSTANT},
    .motion_velocity = {0.0f, 0.0f, 0.0f, 1.0f},
    .motion_force = {0.0f, 0.0f, 0.0f, 1.0f}
};

/** Write the curves and motion of an emitter without ParticleCurves and ParticleMotion into its
 * block: two copies of the ranges of box_particles_still, where member by member writes
 * through the pool were a tenth of the extract of an emitter.
 *
 * @param out The block
 */
static void box_particles_block_still(
    box_render_particle_block_t *out)
{
    size_t first = offsetof(box_render_particle_block_t, curve_rows);
    size_t end = offsetof(box_render_particle_block_t, global_wind);
    ecs_os_memcpy(ECS_OFFSET(out, first), ECS_OFFSET(&box_particles_still, first),
        (ecs_size_t)(end - first));
    first = offsetof(box_render_particle_block_t, curve_max_rows);
    end = offsetof(box_render_particle_block_t, sampled);
    ecs_os_memcpy(ECS_OFFSET(out, first), ECS_OFFSET(&box_particles_still, first),
        (ecs_size_t)(end - first));
}

/** Write the changes over the life and the appearance of an emitter into its block.
 *
 * Without ParticleCurves the curves change nothing; without ParticleAppearance the particles
 * are soft alpha blended circles.
 *
 * @param look The ParticleAppearance of the emitter, NULL for none
 * @param pool The pool, with the rows of its curves and of its ParticleMotion
 * @param scale Multiplier of the softness and divisor of the stretch per unit of speed, the
 *              uniform scale of the emitter, so a scaled emitter draws a scaled copy
 * @param out The block, its other members written, its color variance among them
 */
static void box_particles_block_look(
    const ParticleAppearance *look,
    const box_particle_pool_t *pool,
    float scale,
    box_render_particle_block_t *out)
{
    const box_particle_motion_rows_t *motion = &pool->motion.rows;
    int32_t i;
    if (!pool->curved) {
        box_particles_block_still(out);
    } else {
        for (i = 0; i < 4; i ++) {
            out->curve_rows[i] = pool->curves.rows[i];
            out->curve_max_rows[i] = pool->curves.max_rows[i];
            out->curve_values[i] = pool->curves.values[i];
            out->motion_velocity[i] = motion->velocity[i];
            out->motion_force[i] = motion->force[i];
        }

        for (i = 0; i < 3; i ++) {
            out->gradient[i] = pool->curves.gradient[i];
        }

        out->gradient_row = pool->curves.gradient_row;
        out->gradient_max_row = pool->curves.gradient_max_row;
        out->motion = pool->motion.active ? 1u : 0u;
        out->motion_rows[0] = motion->velocity_row;
        out->motion_rows[1] = motion->velocity_max_row;
        out->motion_rows[2] = motion->force_row;
        out->motion_rows[3] = motion->force_max_row;
    }

    out->look = ParticleCircle;
    out->blend = ParticleAlpha;
    out->columns = 1;
    out->rows = 1;
    if (look) {
        out->look = (uint32_t)look->shape;
        out->blend = look->blend == ParticleAdditive ? ParticleAdditive : ParticleAlpha;
        out->columns = look->columns > 1 ? (uint32_t)look->columns : 1;
        out->rows = look->rows > 1 ? (uint32_t)look->rows : 1;
        out->fps = look->fps > 0 ? look->fps : 0.0f;
        out->softness = look->softness > 0 ? look->softness * scale : 0.0f;
        out->stretch = look->stretch > 0 && scale > 0 ? look->stretch / scale : 0.0f;
        out->lit = glm_clamp(look->lit, 0.0f, 1.0f);
        if (look->shape != ParticleSprite && look->shape != ParticleCard) {
            out->look = ParticleCircle;
        }
    }

    out->styled = look ? 1u : 0u;
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
    if (look && (look->shape == ParticleSprite || look->shape == ParticleCard) &&
        look->texture)
    {
        result = box_textures_handle(box_textures_get(world), look->texture);
    }

    return result;
}

/** Evaluate the curve of a ParticleLight with the curve module, exactly at its time. Lights are
 * evaluated here on the CPU, not from the curve table; a curve without keys or a bad one, also
 * one with max_keys, which its validate hook logged, is 1.
 *
 * With loop the curve loops over a period of 1, so a Catmull-Rom curve from 0 to 1 takes its
 * end neighbours across the wrap.
 *
 * @param curve The curve, its times from 0 to 1 of the period
 * @param loop Whether the curve repeats every period
 * @param t The time over the period, 0 to 1
 * @return The value
 */
static float box_particles_light_curve(
    const ParticleCurve *curve,
    bool loop,
    float t)
{
    Curve view = { .kind = curve->kind, .keys = curve->keys };
    float result = 1.0f;
    if (ecs_vec_count(&curve->keys) && box_particle_light_curve_valid(curve)) {
        result = box_curve_loop_value(&view, loop ? 1.0f : 0.0f, t, 1.0f);
        result = isfinite(result) ? result : 1.0f;
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

        result *= box_particles_light_curve(&light->curve, light->loop, t);
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
 * The lights of a ParticleLight follow the clock after the frame. A ParticleTimeline shapes
 * the rate over the clock of the frame and the start speed at the clock after it.
 *
 * @param world The world
 * @param s The state
 * @param p The packet
 * @param e The emitter
 * @param emitter The emitter values
 * @param transform Its world transform
 * @param scene Its scene
 * @param playback Its playback control, NULL to play; receives the clock and a cleared reset
 * @param curves Its ParticleCurves, NULL for none
 * @param timeline Its ParticleTimeline, NULL for none
 * @param motion Its ParticleMotion, NULL for none
 * @param look Its ParticleAppearance, NULL for none
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
    const ParticleTimeline *timeline,
    const ParticleMotion *motion,
    const ParticleAppearance *look,
    const box_particles_light_t *lights,
    ParticleStats *stats)
{
    bool card = look && look->shape == ParticleCard;
    int32_t limit = card ? BOX_PARTICLE_MAX / 2 : BOX_PARTICLE_MAX;
    double origin[3];
    box_render_origin(world, origin);
    int32_t max = emitter->max > limit ? limit : emitter->max;
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
        pool = box_particles_pool_new(s, p, e, max, card);
    } else if (pool->max != max || pool->card != card) {
        box_particles_range_free(s, p, pool);
        box_particles_empty(pool);
        pool->max = max;
        pool->capacity = box_particles_class(max);
        pool->card = card;
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

    if (pool->local != emitter->local) {
        box_particles_empty(pool);
        pool->fresh = true;
        pool->local = emitter->local;
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

    box_particles_velocity(pool, transform, p->delta, velocity);
    age_us = reset || state == ParticlePaused ? 0 : delta_us;
    pool->clock += age_us;
    box_particles_retire(pool);
    moves = pool->live != 0;
    first = pool->next;
    if (!reset && state != ParticlePaused && state != ParticleStopped) {
        uint64_t t0 = pool->time;
        pool->time += delta_us;
        count = box_particles_owed(pool, emitter,
            box_particle_timeline_seconds(timeline, t0, pool->time, p->delta),
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
        box_dvec3_copy(origin, pool->origin);
        goto done;
    }

    box_render_particle_pool_t *entry = ecs_vec_append_t(
        NULL, &p->particles, box_render_particle_pool_t);
    ecs_os_memset_t(entry, 0, box_render_particle_pool_t);
    entry->entity = e;
    entry->scene = scene;
    entry->buffer = BOX_RENDER_BUFFER_PARTICLE_FIRST;
    entry->offset = pool->base * BOX_RENDER_PARTICLE_SIZE;
    entry->capacity = box_particles_range_slots(pool);
    entry->clear = pool->fresh;
    entry->table = pool->table ? BOX_RENDER_BUFFER_PARTICLE_TABLE_FIRST : 0;
    entry->slots = slots;
    entry->texture = box_particles_texture(world, look);
    const ParticleForces *own = e != ecs_id(ParticleForces)
        ? ecs_get(world, e, ParticleForces)
        : NULL;
    ParticleForces forces;
    box_particles_forces(world, e, emitter->forces, own, &forces);
    glm_vec3_scale(velocity, emitter->inherit_velocity, velocity);
    if (emitter->local) {
        glm_vec3_zero(velocity);
    }

    float scale = box_transform_world_scale(transform);
    vec3 acceleration;
    glm_vec3_scale(ECS_CONST_CAST(float*, emitter->acceleration), scale, acceleration);
    ParticleCollision ground;
    box_particles_block(emitter, transform, origin,
        box_render_particles_collision(world, e, transform, &ground), velocity,
        age_us ? p->delta : 0.0f, scale, &entry->block);
    if (timeline) {
        float speed = box_particle_timeline_speed(timeline, pool->time);
        entry->block.speed[0] *= speed;
        entry->block.speed[1] *= speed;
    }

    box_particles_block_forces(pool, acceleration, &forces, scale, moves, world,
        &entry->block);
    if (curves || motion || pool->curved) {
        box_particle_rows_update(&s->curves, &pool->curves, curves);
        box_particle_rows_motion_update(&s->curves, &pool->motion, motion);
        pool->curved = curves || motion;
    }

    box_particles_block_look(look, pool, scale, &entry->block);
    entry->block.local = emitter->local ? 1u : 0u;
    box_render_particles_sampled(world, e, emitter, entry);
    entry->curves = pool->curved && (box_particle_rows_used(&pool->curves) ||
        box_particle_rows_motion_used(&pool->motion))
        ? BOX_RENDER_BUFFER_PARTICLE_CURVES
        : 0;
    entry->block.count = count;
    entry->block.first = first;
    entry->block.used = pool->used;
    entry->block.points = (uint32_t)pool->points;
    entry->block.table_first = pool->table
        ? (pool->table - 1) * BOX_PARTICLE_MESH_POINTS
        : 0;
    entry->block.delta_us = age_us;
    entry->block.life_min = life_min;
    entry->block.life_span = life_max > life_min ? life_max - life_min : 0;
    entry->block.cards = pool->card ? pool->capacity : 0u;
    if (pool->fresh) {
        box_dvec3_copy(origin, pool->origin);
    }

    box_particles_block_shift(pool, origin, &entry->block);
    pool->fresh = false;
    if (emitter->linger) {
        pool->entry = *entry;
        glm_vec3_copy(acceleration, pool->acceleration);
        pool->forces = emitter->forces;
        pool->scale = scale;
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
    double origin[3];
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
    box_render_origin(world, origin);
    box_particles_block_shift(pool, origin, &entry->block);
    entry->clear = false;
    entry->slots = slots;
    entry->block.acceleration[3] = delta_us ? p->delta : 0.0f;
    box_particles_forces(world, pool->entity, pool->forces, pool->has_own ? &pool->own : NULL,
        &forces);
    box_particles_block_forces(pool, pool->acceleration, &forces, pool->scale,
        pool->live != 0, world, &entry->block);
    entry->block.count = 0;
    entry->block.first = pool->next;
    entry->block.used = pool->used;
    entry->block.delta_us = delta_us;
    stats->particles += pool->live;
    stats->simulated ++;
    stats->lingering ++;
    result = true;
done:
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
        const ParticleTimeline *timelines = ecs_field(&qit, ParticleTimeline, 9);
        const ParticleMotion *motions = ecs_field(&qit, ParticleMotion, 10);
        bool lights_shared = lights && !ecs_field_is_self(&qit, 6);
        bool shared = !ecs_field_is_self(&qit, 1);
        bool curves_shared = curves && !ecs_field_is_self(&qit, 4);
        bool looks_shared = looks && !ecs_field_is_self(&qit, 5);
        bool timelines_shared = timelines && !ecs_field_is_self(&qit, 9);
        bool motions_shared = motions && !ecs_field_is_self(&qit, 10);
        for (i = 0; i < qit.count; i ++) {
            WorldTransform buf, followed;
            const WorldTransform *t = shared ? &transforms[0] :
                box_transform_world_field(world, &qit, transforms, i, &buf);
            t = box_render_particles_follow(world, &emitters[i], t, &followed);
            box_particles_light_t light = {
                .light = lights ? &lights[lights_shared ? 0 : i] : NULL,
                .point = points ? &points[i] : NULL,
                .spot = spots ? &spots[i] : NULL
            };
            stats.emitters ++;
            box_particles_emitter(world, s, p, qit.entities[i], &emitters[i], t,
                box_render_scene_field(&qit, 2, i), playbacks ? &playbacks[i] : NULL,
                curves ? &curves[curves_shared ? 0 : i] : NULL,
                timelines ? &timelines[timelines_shared ? 0 : i] : NULL,
                motions ? &motions[motions_shared ? 0 : i] : NULL,
                looks ? &looks[looks_shared ? 0 : i] : NULL,
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

    box_particle_rows_send(&s->curves, p);
    for (i = 0; i < ecs_vec_count(&s->pages); i ++) {
        const box_particle_page_t *page = ecs_vec_get_t(&s->pages, box_particle_page_t, i);
        if (page->occupied) {
            stats.bytes += (int64_t)page->slots * BOX_RENDER_PARTICLE_SIZE;
        }
    }

    box_particles_arena_release(s, p);

    const ParticleStats *prev = ecs_singleton_get(world, ParticleStats);
    if (prev) {
        stats.effects = prev->effects;
        stats.recycled = prev->recycled;
        stats.merged = prev->merged;
        stats.dropped = prev->dropped;
    }

    if (!prev || ecs_os_memcmp_t(prev, &stats, ParticleStats)) {
        ecs_singleton_set_ptr(world, ParticleStats, &stats);
    }

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
    ecs_vec_fini_t(NULL, &s->tables, box_particle_table_t);
    ecs_vec_fini_t(NULL, &s->points, box_particle_point_t);
    ecs_vec_fini_t(NULL, &s->bursts, box_particle_burst_t);
    box_particle_rows_fini(&s->curves);
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
    const ecs_system_t *sys = box_particles_system_ctx(world);

    return sys ? sys->ctx : NULL;
}

void box_render_particles_init(
    ecs_world_t *world)
{
    box_render_particles_t *s = ecs_os_calloc_t(box_render_particles_t);
    ecs_vec_init_t(NULL, &s->pools, box_particle_pool_t*, 0);
    ecs_map_init(&s->by_entity, NULL);
    ecs_vec_init_t(NULL, &s->pages, box_particle_page_t, 0);
    ecs_vec_init_t(NULL, &s->tables, box_particle_table_t, 0);
    ecs_vec_init_t(NULL, &s->points, box_particle_point_t, 0);
    ecs_vec_init_t(NULL, &s->bursts, box_particle_burst_t, 0);
    box_particle_rows_init(&s->curves);

    ecs_set_name_prefix(world, "Render");
    ECS_COMPONENT_DEFINE(world, RenderParticlesSystem);
    ecs_set_name_prefix(world, NULL);
    ecs_entity_t extract = ecs_system(world, {
        .entity = ecs_entity(world, { .name = "extract.Particles" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractParticles,
        .ctx = s,
        .ctx_free = box_particles_free
    });
    ecs_singleton_set(world, RenderParticlesSystem, { extract });
}

void box_render_particles_import(
    ecs_world_t *world)
{
    box_render_particles_t *s = box_particles_state(world);
    if (s && !s->emitters) {
        s->emitters = ecs_query(world, {
            .entity = ecs_entity(world, { .name = "ParticleEmitters" }),
            .terms = {
                { .id = ecs_id(ParticleEmitter), .inout = EcsIn },
                { .id = ecs_id(WorldTransform), .inout = EcsIn },
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
                },
                { .id = ecs_id(ParticleTimeline), .inout = EcsIn, .oper = EcsOptional },
                { .id = ecs_id(ParticleMotion), .inout = EcsIn, .oper = EcsOptional }
            },
            .cache_kind = EcsQueryCacheAuto
        });
        s->burst_query = ecs_query(world, {
            .entity = ecs_entity(world, { .name = "ParticleBursts" }),
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

void box_render_particles_curve_stats(
    const box_render_particles_t *s,
    box_render_particle_curve_stats_t *out)
{
    ecs_os_memset_t(out, 0, box_render_particle_curve_stats_t);
    if (s) {
        box_particle_rows_stats(&s->curves, out);
    }
}

const float* box_render_particles_curve_table(
    const box_render_particles_t *s,
    int32_t *rows)
{
    const float *result = NULL;
    if (rows) {
        *rows = 0;
    }

    if (s) {
        result = box_particle_rows_data(&s->curves, rows);
    }

    return result;
}
