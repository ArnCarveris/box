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

/** The node is drawn as a billboard of the views its impostor atlas holds */
#define BOX_LOD_LEVEL_IMPOSTOR (3)

/** The node is drawn as the box over its bounds, the cheapest of the four */
#define BOX_LOD_LEVEL_BOX (4)

/** Views one impostor captures around the Y axis of its subtree */
#define BOX_LOD_IMPOSTOR_VIEWS (8)

/** Texels one tile of an impostor atlas is wide and high */
#define BOX_LOD_IMPOSTOR_TILE (128)

/** Impostors the extract captures into their atlas in one frame */
#define BOX_LOD_IMPOSTOR_CAPTURES (2)

/** Atlases one world keeps before a node stops asking for an impostor */
#define BOX_LOD_IMPOSTOR_MAX (64)

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
    vec3 local_min;    /**< Minimum corner of the subtree in the space of the node */
    vec3 local_max;    /**< Maximum corner of the subtree in the space of the node */
    uint32_t mesh;     /**< Handle of the merged mesh, 0 when it has none */
    uint32_t atlas;    /**< Render target the impostor is captured into, 0 for none */
    float impostor_error; /**< Error of the billboard of the impostor in world units */
    bool captured;     /**< The atlas holds a capture of this version of the entry */
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
    int32_t captures;     /**< Impostor atlases the cache ever captured */
    int32_t atlases;      /**< Impostor atlases the cache holds */
    uint32_t tiles[BOX_LOD_IMPOSTOR_VIEWS]; /**< Quad mesh of every tile of an atlas */
    bool tiled;           /**< The tile meshes were uploaded */
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

/** One billboard of this frame, drawn in the sorted pass of every view.
 *
 * The billboard stands at the bounds of the node it replaces and turns around
 * the world Y axis to face the camera, which is the axis the views of its
 * atlas were captured around.
 */
typedef struct box_lod_billboard_t {
    vec3 center;    /**< Center of the world bounds of the node */
    vec3 axis;      /**< Direction of the first captured view, in world space */
    float radius;   /**< Half the diagonal of those bounds, in world units */
    float size[2];  /**< Width and height one tile of the atlas stands for */
    uint32_t atlas; /**< Render target the billboard composites */
    float color[4]; /**< Linear albedo and opacity the billboard is weighted by */
} box_lod_billboard_t;

/** Width and height one tile of an impostor atlas stands for, in world units.
 *
 * @param min Minimum corner of the bounds of the subtree
 * @param max Maximum corner of the bounds of the subtree
 * @param out Receives the width and the height
 */
void box_lod_impostor_size(
    vec3 min,
    vec3 max,
    float *out);

/** Error of the billboard of a subtree in world units.
 *
 * The billboard gets the size of one texel of its tile wrong and, between two
 * captured directions, the depth that half a step of the turn moves its
 * surface by, so a subtree is only ever replaced by one when both are small.
 *
 * @param min Minimum corner of the bounds of the subtree
 * @param max Maximum corner of the bounds of the subtree
 * @return The error in world units
 */
float box_lod_impostor_error(
    vec3 min,
    vec3 max);

/** Give an entry the atlas its impostor is captured into.
 *
 * @param world The world
 * @param cache The cache
 * @param p The packet the target is created on
 * @param entry The entry
 * @return True when the entry holds an atlas afterwards
 */
bool box_lod_impostor_atlas(
    ecs_world_t *world,
    box_lod_cache_t *cache,
    box_packet_t *p,
    box_lod_entry_t *entry);

/** Release the atlas of an entry.
 *
 * @param world The world
 * @param cache The cache
 * @param entry The entry
 */
void box_lod_impostor_drop(
    ecs_world_t *world,
    box_lod_cache_t *cache,
    box_lod_entry_t *entry);

/** The quad mesh one tile of an impostor atlas is drawn with.
 *
 * The mesh covers the tile inside the bounds of the whole atlas, so the
 * composite of the renderer samples that tile without a uv transform of its
 * own; the instance of a billboard scales it back to the quad it draws.
 *
 * @param world The world
 * @param cache The cache the meshes are kept in
 * @param tile Index of the tile
 * @return The mesh handle, 0 when the meshes cannot be uploaded
 */
uint32_t box_lod_impostor_tile(
    ecs_world_t *world,
    box_lod_cache_t *cache,
    int32_t tile);

/** Render the views of the impostor of an entry into its atlas.
 *
 * @param p The packet the batches and the view are added to
 * @param entry The entry, which must hold an atlas and a representation
 * @param lit The view the sun and the environment are taken from
 */
void box_lod_impostor_capture(
    box_packet_t *p,
    box_lod_entry_t *entry,
    const box_view_t *lit);

/** Tile of an atlas that stands closest to where the eye is.
 *
 * @param bb The billboard
 * @param eye Position of the eye in world space
 * @return Index of the tile
 */
int32_t box_lod_impostor_pick(
    const box_lod_billboard_t *bb,
    vec3 eye);

/** Fill the sorted entry one billboard is drawn as.
 *
 * @param bb The billboard
 * @param mesh Quad mesh of the tile it shows
 * @param tile Index of that tile
 * @param eye Position of the eye in world space
 * @param out Receives the entry
 */
void box_lod_impostor_billboard(
    const box_lod_billboard_t *bb,
    uint32_t mesh,
    int32_t tile,
    vec3 eye,
    box_transparent_t *out);

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
