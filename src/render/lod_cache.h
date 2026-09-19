/**
 * @file render/lod_cache.h
 * @brief Generated proxies of the LOD traversal, cached per node and built on jobs.
 *
 * A node that the traversal replaces asks the cache for a proxy. The cache
 * collects the primitives of the subtree in the local space of the node, hands
 * them to the job system and keeps the result until something below the node
 * changes, so a rigid move of the node costs nothing and a changed descendant
 * invalidates only the proxies that contain it.
 */

#ifndef BOX_RENDER_LOD_CACHE_H
#define BOX_RENDER_LOD_CACHE_H

#include "render_internal.h"

/** Shapes the primitive set of a proxy aims for */
#define BOX_LOD_CACHE_SHAPES (8)

/** Shapes the merged mesh of a proxy aims for */
#define BOX_LOD_CACHE_FINE (48)

/** Primitives one subtree hands the generator at most */
#define BOX_LOD_CACHE_INPUTS (2048)

/** Subtrees the extract collects and submits in one frame */
#define BOX_LOD_CACHE_STARTS (4)

/** Part of the triangles the simplification of a merged proxy keeps */
#define BOX_LOD_CACHE_RATIO (0.4f)

/** Segments a generated sphere or capsule of a merged proxy is drawn with */
#define BOX_LOD_CACHE_SEGMENTS (8)

/** The node is drawn as the merged and simplified mesh, its finest proxy */
#define BOX_LOD_LEVEL_MESH (1)

/** The node is drawn as the generated set of primitives */
#define BOX_LOD_LEVEL_SHAPES (2)

/** The node is drawn as the box over its bounds, the cheapest of the three */
#define BOX_LOD_LEVEL_BOX (3)

/** Part of the height of a view a node may cover before a proxy may replace it */
#define BOX_LOD_CACHE_SIZE (0.10f)

/** One drawn part of a generated proxy, in the local space of its node */
typedef struct box_lod_part_t {
    mat4 transform;  /**< Local transform of the part */
    float color[4];  /**< Linear albedo and opacity of the part */
    float params[2]; /**< Metallic and roughness of the part */
    vec3 min;        /**< Minimum corner of the local bounds of its mesh */
    vec3 max;        /**< Maximum corner of the local bounds of its mesh */
    uint32_t mesh;   /**< Mesh handle the part is drawn with */
} box_lod_part_t;

/** The representations one node was given, keyed by the identity of the node.
 *
 * Everything is in the local space of the node, so the entity the node stands
 * for moves, turns and scales without the proxy being built again. The errors
 * are lengths in world units the screen space metric projects, and a
 * representation the generator did not produce leaves its error at zero.
 */
typedef struct box_lod_entry_t {
    uint64_t id;       /**< Node the proxy stands for, an entity or a cluster */
    uint64_t root;     /**< Root of the node, which drops the entry with it */
    ecs_vec_t parts;   /**< box_lod_part_t of the primitive set */
    int32_t shape_count; /**< Shapes the primitive set holds, which may exceed its budget */
    float shape_error; /**< Error of the primitive set in world units */
    float mesh_error;  /**< Error of the merged mesh in world units */
    mat4 transform;    /**< Local transform of the merged mesh */
    float color[4];    /**< Linear albedo and opacity of the merged mesh */
    float params[2];   /**< Metallic and roughness of the merged mesh */
    vec3 min;          /**< Minimum corner of the local bounds of the merged mesh */
    vec3 max;          /**< Maximum corner of the local bounds of the merged mesh */
    uint32_t mesh;     /**< Handle of the merged mesh, 0 when it has none */
    uint64_t job;      /**< Job that is building it, 0 for none */
    uint64_t content;  /**< Hash of the primitives the proxy was built from */
    uint32_t version;  /**< Times the entry was built again */
    bool ready;        /**< A generation finished for this version */
    bool stale;        /**< Something below the node changed and it must be checked */
} box_lod_entry_t;

/** The contexts of the jobs of a world, which outlive the cache.
 *
 * The render registry is destroyed with the rest of the world, before the
 * teardown actions run, so a context that is still on a worker at that point
 * is handed to this struct instead, which a teardown action frees once the
 * pool joined its workers.
 */
typedef struct box_lod_jobs_t {
    ecs_vec_t live;    /**< box_lod_gen_t* of the jobs that may still complete */
    ecs_vec_t garbage; /**< box_lod_gen_t* of the jobs whose result is not wanted */
    struct box_lod_cache_t *cache; /**< Cache they belong to, NULL once it is gone */
} box_lod_jobs_t;

/** The proxies of a world, held by the traversal */
struct box_lod_cache_t {
    ecs_map_t entries;    /**< Node identity to box_lod_entry_t* */
    box_lod_jobs_t *jobs; /**< Contexts of the jobs in flight, NULL until the first */
    int32_t queued;       /**< Generations that were submitted and did not finish */
    int32_t builds;       /**< Generations the cache ever submitted */
    bool checked;         /**< The world was looked at for a worker pool */
    bool pooled;          /**< box.job is imported, so generation runs on workers */
};

/** One subtree the cache is asked to build a proxy for.
 *
 * The primitives are filled in by the traversal, which is the only part that
 * may read the world, and the look of every primitive is packed into its
 * material key, so the generator needs no table to draw its result.
 */
typedef struct box_lod_collect_t {
    box_lod_primitive_t *primitives; /**< Inputs in the local space of the node */
    int32_t count;                   /**< Inputs the subtree holds */
    int32_t max;                     /**< Inputs the array can hold */
    bool full;                       /**< The subtree holds more than the array */
} box_lod_collect_t;

/** Initialise an empty cache.
 *
 * @param cache The cache
 */
void box_lod_cache_init(
    box_lod_cache_t *cache);

/** Release the cache, orphaning the jobs that are still in flight.
 *
 * @param cache The cache
 */
void box_lod_cache_fini(
    box_lod_cache_t *cache);

/** The entry of a node, without creating one.
 *
 * @param cache The cache
 * @param id Identity of the node
 * @return The entry, NULL when the node has none
 */
box_lod_entry_t* box_lod_cache_find(
    const box_lod_cache_t *cache,
    uint64_t id);

/** Drop what a node generated and forget the node.
 *
 * @param world The world, which the meshes are released through
 * @param cache The cache
 * @param id Identity of the node
 */
void box_lod_cache_drop(
    ecs_world_t *world,
    box_lod_cache_t *cache,
    uint64_t id);

/** Drop every entry of a root, the root itself included.
 *
 * @param world The world
 * @param cache The cache
 * @param root Identity of the root
 */
void box_lod_cache_drop_root(
    ecs_world_t *world,
    box_lod_cache_t *cache,
    uint64_t root);

/** Drop every entry of the cache.
 *
 * @param world The world
 * @param cache The cache
 */
void box_lod_cache_clear(
    ecs_world_t *world,
    box_lod_cache_t *cache);

/** Mark what a node generated as out of date.
 *
 * The proxy stays drawn until the generator gets to the node again, which
 * compares the primitives of the subtree with the ones the proxy was built
 * from and only builds again when they really differ.
 *
 * @param cache The cache
 * @param id Identity of the node
 * @return True when the node holds a proxy that is now out of date
 */
bool box_lod_cache_invalidate(
    box_lod_cache_t *cache,
    uint64_t id);

/** Queue the generation of the proxy of a node.
 *
 * @param world The world
 * @param cache The cache
 * @param id Identity of the node
 * @param root Identity of its root
 * @param collect The primitives of its subtree
 * @param need Error in world units the node asks the proxy for
 * @param priority Higher priority is generated first
 * @param position World position the subtree sits at
 */
void box_lod_cache_submit(
    ecs_world_t *world,
    box_lod_cache_t *cache,
    uint64_t id,
    uint64_t root,
    const box_lod_collect_t *collect,
    float need,
    int32_t priority,
    vec3 position);

/** Free the contexts of the jobs the pool is done with.
 *
 * @param world The world
 * @param cache The cache
 */
void box_lod_cache_sweep(
    ecs_world_t *world,
    box_lod_cache_t *cache);

/** Pack the look of a primitive into the material key the generator clusters by.
 *
 * @param color Display encoded albedo and opacity of the primitive
 * @param metallic Metallic value
 * @param roughness Roughness value
 * @return The material key
 */
uint64_t box_lod_cache_material(
    rgba color,
    float metallic,
    float roughness);

#endif
