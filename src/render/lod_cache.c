/**
 * @file render/lod_cache.c
 * @brief Builds the proxies of the LOD traversal on the job system and caches them.
 *
 * The traversal collects the primitives of a subtree in the local space of its
 * node and hands them over; a worker clusters them into a handful of shapes and,
 * when those shapes miss more than the node asked for, merges and simplifies
 * their meshes into one. The main thread only resolves mesh handles, so nothing
 * of the generation runs where the frame is built.
 */

#include "lod_cache.h"
#include "../geometry/geometry_internal.h"
#include "../job/job_internal.h"
#include <math.h>

/** Offset of the 64 bit FNV hash the merged meshes are shared by */
#define BOX_LOD_HASH_OFFSET (14695981039346656037ull)

/** Prime of the 64 bit FNV hash the merged meshes are shared by */
#define BOX_LOD_HASH_PRIME (1099511628211ull)

/** Step the cylinder length of a generated capsule is rounded to */
#define BOX_LOD_CAPSULE_STEP (0.25f)

/** Cylinder length of a generated capsule, relative to its radius, at most */
#define BOX_LOD_CAPSULE_MAX (16.0f)

/** What one job builds and what it produced.
 *
 * The context is owned by the cache, which orphans it when the entry it
 * belongs to goes away, so a result that nobody wants any more is freed
 * without ever being looked at.
 */
typedef struct box_lod_gen_t {
    box_lod_cache_t *cache;          /**< Cache the result belongs to */
    uint64_t id;                     /**< Node the proxy stands for */
    uint64_t root;                   /**< Root of that node */
    uint32_t version;                /**< Version of the entry the inputs came from */
    float need;                      /**< Error in world units the node asked for */
    box_lod_primitive_t *primitives; /**< Inputs in the local space of the node */
    int32_t count;                   /**< Inputs the subtree holds */
    box_lod_shape_t *shapes;         /**< Shapes the primitive set holds */
    int32_t shape_count;             /**< Number of shapes */
    float shape_error;               /**< Error of the primitive set in world units */
    box_mesh_data_t mesh;            /**< Merged and simplified mesh, empty for none */
    float mesh_error;                /**< Error of that mesh in world units */
    uint64_t hash;                   /**< Content hash of that mesh */
    float metallic;                  /**< Metallic value the merged mesh is drawn with */
    uint64_t job;                    /**< Handle of the job */
    bool orphaned;                   /**< The entry went away while the job ran */
} box_lod_gen_t;

/** Hold a value inside a range.
 *
 * @param value The value
 * @param low Lowest value it may take
 * @param high Highest value it may take
 * @return The value inside the range
 */
static float box_lod_clamp(
    float value,
    float low,
    float high)
{
    return value < low ? low : (value > high ? high : value);
}

uint64_t box_lod_cache_material(
    rgba color,
    float metallic,
    float roughness)
{
    uint64_t m = (uint64_t)(box_lod_clamp(metallic, 0.0f, 1.0f) * 255.0f + 0.5f);
    uint64_t r = (uint64_t)(box_lod_clamp(roughness, 0.0f, 1.0f) * 255.0f + 0.5f);

    return (uint64_t)color.r | ((uint64_t)color.g << 8) | ((uint64_t)color.b << 16) |
        ((uint64_t)color.a << 24) | (m << 32) | (r << 40);
}

/** Unpack the display encoded color a material key holds.
 *
 * @param material The material key
 * @return The color
 */
static rgba box_lod_cache_color(
    uint64_t material)
{
    rgba result = {
        (uint8_t)(material & 0xFF), (uint8_t)((material >> 8) & 0xFF),
        (uint8_t)((material >> 16) & 0xFF), (uint8_t)((material >> 24) & 0xFF)
    };

    return result;
}

/** Metallic value a material key holds.
 *
 * @param material The material key
 * @return The metallic value
 */
static float box_lod_cache_metallic(
    uint64_t material)
{
    return (float)((material >> 32) & 0xFF) / 255.0f;
}

/** Roughness value a material key holds.
 *
 * @param material The material key
 * @return The roughness value
 */
static float box_lod_cache_roughness(
    uint64_t material)
{
    return (float)((material >> 40) & 0xFF) / 255.0f;
}

/** Add bytes to a running FNV hash.
 *
 * @param hash The hash so far
 * @param data The bytes
 * @param size Number of bytes
 * @return The hash
 */
static uint64_t box_lod_cache_hash(
    uint64_t hash,
    const void *data,
    size_t size)
{
    const uint8_t *bytes = data;
    size_t i;
    for (i = 0; i < size; i ++) {
        hash = (hash ^ bytes[i]) * BOX_LOD_HASH_PRIME;
    }

    return hash;
}

/** The error of a proxy as the length the screen space metric projects.
 *
 * The generator reports the edge of the cube of the volume its shapes get
 * wrong, while a node reports the thickness that volume spreads to over its
 * surface; this turns the first into the second, so the two are comparable.
 *
 * @param error Edge of the missed volume the generator reported
 * @param min Minimum corner of the bounds of the proxy
 * @param max Maximum corner of the bounds of the proxy
 * @return The thickness in world units
 */
static float box_lod_gen_shell(
    float error,
    vec3 min,
    vec3 max)
{
    vec3 size = vec3_sub(max, min);
    float area = 2.0f * (size.x * size.y + size.y * size.z + size.z * size.x);

    return area > 0.0f ? error * error * error / area : error;
}

/** Release the working memory and the result of a job context.
 *
 * @param gen The context
 */
static void box_lod_gen_free(
    box_lod_gen_t *gen)
{
    ecs_os_free(gen->primitives);
    ecs_os_free(gen->shapes);
    box_mesh_data_fini(&gen->mesh);
    ecs_os_free(gen);
}

/** Take a job context out of a list of them.
 *
 * @param list The list
 * @param gen The context
 * @return True when the list held it
 */
static bool box_lod_gen_take(
    ecs_vec_t *list,
    const box_lod_gen_t *gen)
{
    bool result = false;
    int32_t i, count = ecs_vec_count(list);
    for (i = 0; i < count && !result; i ++) {
        box_lod_gen_t **slot = ecs_vec_get_t(list, box_lod_gen_t*, i);
        if (slot[0] != gen) {
            continue;
        }

        slot[0] = ecs_vec_get_t(list, box_lod_gen_t*, count - 1)[0];
        ecs_vec_remove_last(list);
        result = true;
    }

    return result;
}

/** The mesh key and the scale one generated shape is drawn with.
 *
 * @param shape The shape
 * @param key Receives the mesh key
 * @return The scale of the instance
 */
static vec3 box_lod_gen_shape(
    const box_lod_shape_t *shape,
    box_mesh_key_t *key)
{
    vec3 result = vec3_new(1, 1, 1);

    memset(key, 0, sizeof(box_mesh_key_t));
    key->i0 = BOX_LOD_CACHE_SEGMENTS;
    key->smooth = true;
    if (shape->kind == BoxLodShapeSphere) {
        float r = shape->sphere.radius;

        key->kind = BoxShapeSphere;
        result = vec3_new(r, r, r);
    } else if (shape->kind == BoxLodShapeCapsule) {
        float r = shape->capsule.radius > 0.0f ? shape->capsule.radius : 1.0f;
        float length = shape->capsule.length / r;

        length = box_lod_clamp(length, 0.0f, BOX_LOD_CAPSULE_MAX);
        key->kind = BoxShapeCapsule;
        key->f0 = ceilf(length / BOX_LOD_CAPSULE_STEP) * BOX_LOD_CAPSULE_STEP;
        result = vec3_new(r, r, r);
    } else {
        key->kind = BoxShapeBox;
        key->i0 = 0;
        key->smooth = false;
        result = vec3_new(shape->box.x, shape->box.y, shape->box.z);
    }

    return result;
}

/** Generate the mesh of one shape and describe it as a source of the merge.
 *
 * @param shape The shape
 * @param data Receives the mesh of the shape
 * @param colors Receives the vertex color of every vertex of that mesh
 * @param out Receives the source
 */
static void box_lod_gen_source(
    const box_lod_shape_t *shape,
    box_mesh_data_t *data,
    ecs_vec_t *colors,
    box_lod_mesh_source_t *out)
{
    box_mesh_key_t key;
    vec3 scale = box_lod_gen_shape(shape, &key);
    mat4 translation = mat4_translation(shape->position);
    mat4 scaling = mat4_scaling(scale);
    rgba color = box_lod_cache_color(shape->material);
    uint32_t i;

    if (key.kind == BoxShapeSphere) {
        box_mesh_sphere(data, key.i0, key.smooth);
    } else if (key.kind == BoxShapeCapsule) {
        box_mesh_capsule(data, key.i0, key.smooth, key.f0);
    } else {
        box_mesh_box(data);
    }

    color.a = (uint8_t)(box_lod_clamp(box_lod_cache_roughness(shape->material),
        0.0f, 1.0f) * 255.0f + 0.5f);
    ecs_vec_set_count_t(NULL, colors, rgba, (int32_t)data->vertex_count);
    for (i = 0; i < data->vertex_count; i ++) {
        ecs_vec_get_t(colors, rgba, (int32_t)i)[0] = color;
    }

    memset(out, 0, sizeof(box_lod_mesh_source_t));
    out->transform = mat4_mul(&translation, &scaling);
    out->vertices = (const vec3*)data->positions;
    out->normals = (const vec3*)data->normals;
    out->uvs = (const vec2*)data->uvs;
    out->colors = ecs_vec_first_t(colors, rgba);
    out->indices = data->indices;
    out->vertex_count = (int32_t)data->vertex_count;
    out->index_count = (int32_t)data->index_count;
    out->material = shape->material;
}

/** Copy a merged mesh into the arrays the upload reads.
 *
 * @param mesh The merged mesh
 * @param out Receives the mesh data, which the caller frees
 */
static void box_lod_gen_copy(
    const box_lod_mesh_t *mesh,
    box_mesh_data_t *out)
{
    int32_t i;

    out->vertex_count = (uint32_t)mesh->vertex_count;
    out->index_count = (uint32_t)mesh->index_count;
    out->positions = ecs_os_malloc_n(float, mesh->vertex_count * 3);
    out->normals = ecs_os_malloc_n(float, mesh->vertex_count * 3);
    out->uvs = ecs_os_malloc_n(float, mesh->vertex_count * 2);
    out->colors = ecs_os_malloc_n(uint32_t, mesh->vertex_count);
    out->indices = ecs_os_malloc_n(uint32_t, mesh->index_count);
    memcpy(out->positions, mesh->vertices, (size_t)mesh->vertex_count * 3 * sizeof(float));
    memcpy(out->normals, mesh->normals, (size_t)mesh->vertex_count * 3 * sizeof(float));
    memcpy(out->uvs, mesh->uvs, (size_t)mesh->vertex_count * 2 * sizeof(float));
    memcpy(out->indices, mesh->indices, (size_t)mesh->index_count * sizeof(uint32_t));
    for (i = 0; i < mesh->vertex_count; i ++) {
        rgba c = mesh->colors[i];

        out->colors[i] = (uint32_t)c.r | ((uint32_t)c.g << 8) |
            ((uint32_t)c.b << 16) | ((uint32_t)c.a << 24);
    }

    out->bounds_min = mesh->min;
    out->bounds_max = mesh->max;
}

/** Build the merged and simplified mesh of a proxy from a finer shape set.
 *
 * @param gen The context
 * @param builder The proxy builder, reused for the finer set
 */
static void box_lod_gen_merge(
    box_lod_gen_t *gen,
    box_lod_builder_t *builder)
{
    int32_t fine = gen->count < BOX_LOD_CACHE_FINE ? gen->count : BOX_LOD_CACHE_FINE;
    box_lod_proxy_desc_t desc = {
        .primitives = gen->primitives, .count = gen->count,
        .max_shapes = fine, .max_error = gen->need * 0.25f
    };
    box_lod_simplify_desc_t simplify = {
        .ratio = BOX_LOD_CACHE_RATIO, .max_error = gen->need * 0.5f
    };
    box_lod_mesh_builder_t meshes;
    box_lod_merge_desc_t merge = {0};
    box_mesh_data_t *data = NULL;
    ecs_vec_t *colors = NULL;
    box_lod_mesh_source_t *sources = NULL;
    box_lod_proxy_t proxy;
    box_lod_mesh_t mesh;
    int32_t i;

    if (!box_lod_proxy_build(builder, &desc, &proxy) || proxy.count <= 0) {
        goto done;
    }

    data = ecs_os_calloc_n(box_mesh_data_t, proxy.count);
    colors = ecs_os_calloc_n(ecs_vec_t, proxy.count);
    sources = ecs_os_calloc_n(box_lod_mesh_source_t, proxy.count);
    merge.sources = sources;
    merge.count = proxy.count;
    merge.weld = false;
    for (i = 0; i < proxy.count; i ++) {
        ecs_vec_init_t(NULL, &colors[i], rgba, 0);
        box_lod_gen_source(&proxy.shapes[i], &data[i], &colors[i], &sources[i]);
    }

    box_lod_mesh_builder_init(&meshes);
    if (box_lod_merge(&meshes, &merge, &mesh) &&
        box_lod_simplify(&meshes, &simplify, &mesh))
    {
        box_lod_gen_copy(&mesh, &gen->mesh);
        gen->mesh_error = box_lod_gen_shell(proxy.error, proxy.min, proxy.max) +
            mesh.error;
    }

    box_lod_mesh_builder_fini(&meshes);
    for (i = 0; i < proxy.count; i ++) {
        box_mesh_data_fini(&data[i]);
        ecs_vec_fini_t(NULL, &colors[i], rgba);
    }

    ecs_os_free(data);
    ecs_os_free(colors);
    ecs_os_free(sources);
done:
    return;
}

/** Give the merged mesh of a context the hash its content shares.
 *
 * @param gen The context
 */
static void box_lod_gen_hash(
    box_lod_gen_t *gen)
{
    uint64_t hash = BOX_LOD_HASH_OFFSET;

    hash = box_lod_cache_hash(hash, gen->mesh.positions,
        (size_t)gen->mesh.vertex_count * 3 * sizeof(float));
    hash = box_lod_cache_hash(hash, gen->mesh.normals,
        (size_t)gen->mesh.vertex_count * 3 * sizeof(float));
    hash = box_lod_cache_hash(hash, gen->mesh.colors,
        (size_t)gen->mesh.vertex_count * sizeof(uint32_t));
    hash = box_lod_cache_hash(hash, gen->mesh.indices,
        (size_t)gen->mesh.index_count * sizeof(uint32_t));
    gen->hash = hash;
}

/** Build the representations of one subtree, on a worker.
 *
 * @param ctx The context
 */
static void box_lod_gen_run(
    void *ctx)
{
    box_lod_gen_t *gen = ctx;
    box_lod_builder_t builder;
    box_lod_proxy_desc_t desc = {
        .primitives = gen->primitives, .count = gen->count,
        .max_shapes = BOX_LOD_CACHE_SHAPES
    };
    box_lod_proxy_t proxy;
    float volume = 0.0f;
    int32_t i;

    for (i = 0; i < gen->count; i ++) {
        float weight = gen->primitives[i].volume;

        gen->metallic += box_lod_cache_metallic(gen->primitives[i].material) * weight;
        volume += weight;
    }

    gen->metallic = volume > 0.0f ? gen->metallic / volume : 0.0f;
    box_lod_builder_init(&builder);
    if (box_lod_proxy_build(&builder, &desc, &proxy) && proxy.count > 0) {
        gen->shape_count = proxy.count;
        gen->shapes = ecs_os_malloc_n(box_lod_shape_t, proxy.count);
        gen->shape_error = box_lod_gen_shell(proxy.error, proxy.min, proxy.max);
        memcpy(gen->shapes, proxy.shapes, (size_t)proxy.count * sizeof(box_lod_shape_t));
    }

    if (gen->shape_count > BOX_LOD_CACHE_SHAPES || gen->shape_error > gen->need) {
        box_lod_gen_merge(gen, &builder);
    }

    box_lod_builder_fini(&builder);
    if (gen->mesh.vertex_count) {
        box_lod_gen_hash(gen);
    }
}

/** Give an entry the primitive set a job produced.
 *
 * @param world The world, which the shape meshes come from
 * @param gen The context
 * @param entry The entry
 */
static void box_lod_gen_shapes(
    ecs_world_t *world,
    const box_lod_gen_t *gen,
    box_lod_entry_t *entry)
{
    int32_t i;
    for (i = 0; i < gen->shape_count; i ++) {
        const box_lod_shape_t *shape = &gen->shapes[i];
        box_mesh_key_t key;
        vec3 scale = box_lod_gen_shape(shape, &key);
        mat4 translation = mat4_translation(shape->position);
        mat4 scaling = mat4_scaling(scale);
        rgba color = box_lod_cache_color(shape->material);
        box_lod_part_t *part = ecs_vec_append_t(NULL, &entry->parts, box_lod_part_t);

        part->transform = mat4_mul(&translation, &scaling);
        part->mesh = box_mesh_cache_get(world, &key, &part->min, &part->max);
        part->color[0] = box_srgb_to_linear(color.r);
        part->color[1] = box_srgb_to_linear(color.g);
        part->color[2] = box_srgb_to_linear(color.b);
        part->color[3] = (float)color.a / 255.0f;
        part->params[0] = box_lod_cache_metallic(shape->material);
        part->params[1] = box_lod_cache_roughness(shape->material);
    }

    entry->shape_error = gen->shape_error;
    entry->shape_count = gen->shape_count;
}

/** Give an entry the merged mesh a job produced, sharing an identical one.
 *
 * @param world The world, which the mesh is uploaded to
 * @param gen The context
 * @param entry The entry
 */
static void box_lod_gen_mesh(
    ecs_world_t *world,
    const box_lod_gen_t *gen,
    box_lod_entry_t *entry)
{
    vec3 min, max;
    if (!box_mesh_cache_acquire(world, gen->hash, &entry->mesh, &min, &max)) {
        entry->mesh = box_mesh_cache_upload(world, gen->hash, &gen->mesh);
        min = gen->mesh.bounds_min;
        max = gen->mesh.bounds_max;
    }

    entry->transform = mat4_identity();
    entry->min = min;
    entry->max = max;
    entry->color[0] = 1.0f;
    entry->color[1] = 1.0f;
    entry->color[2] = 1.0f;
    entry->color[3] = 1.0f;
    entry->params[0] = gen->metallic;
    entry->params[1] = 1.0f;
    entry->mesh_error = gen->mesh_error;
}

/** Hand the result of a job to its entry, on the main thread.
 *
 * @param world The world
 * @param ctx The context
 */
static void box_lod_gen_complete(
    ecs_world_t *world,
    void *ctx)
{
    box_lod_gen_t *gen = ctx;
    box_lod_cache_t *cache = gen->cache;
    box_lod_entry_t *entry;
    if (gen->orphaned) {
        goto done;
    }

    box_lod_gen_take(&cache->jobs->live, gen);
    cache->queued --;
    entry = box_lod_cache_find(cache, gen->id);
    if (entry && entry->version == gen->version) {
        entry->job = 0;
        entry->ready = true;
        box_lod_gen_shapes(world, gen, entry);
        if (gen->mesh.vertex_count) {
            box_lod_gen_mesh(world, gen, entry);
        }
    }

    box_lod_gen_free(gen);
done:
    return;
}

void box_lod_cache_init(
    box_lod_cache_t *cache)
{
    memset(cache, 0, sizeof(box_lod_cache_t));
    ecs_map_init(&cache->entries, NULL);
}

/** Free the contexts a world left behind, whatever is left of its pool.
 *
 * The teardown actions of a world run before its components are destroyed, so
 * this may be the first or the second of the two to look at the contexts; the
 * cache and this list let go of each other so that neither frees them twice.
 *
 * @param world The world
 * @param ctx The job list
 */
static void box_lod_jobs_fini(
    ecs_world_t *world,
    void *ctx)
{
    box_lod_jobs_t *jobs = ctx;
    bool alive = ecs_lookup(world, "box.job.JobPool") != 0;
    int32_t i;

    if (alive && jobs->cache) {
        box_job_flush(world);
    }

    for (i = 0; i < ecs_vec_count(&jobs->live); i ++) {
        box_lod_gen_t *gen = ecs_vec_get_t(&jobs->live, box_lod_gen_t*, i)[0];

        if (alive) {
            box_job_cancel(world, gen->job);
        }

        box_lod_gen_free(gen);
    }

    for (i = 0; i < ecs_vec_count(&jobs->garbage); i ++) {
        box_lod_gen_free(ecs_vec_get_t(&jobs->garbage, box_lod_gen_t*, i)[0]);
    }

    if (jobs->cache) {
        jobs->cache->jobs = NULL;
    }

    ecs_vec_fini_t(NULL, &jobs->live, box_lod_gen_t*);
    ecs_vec_fini_t(NULL, &jobs->garbage, box_lod_gen_t*);
    ecs_os_free(jobs);
}

/** Release the parts and the merged mesh an entry holds.
 *
 * @param world The world, NULL when the meshes cannot be released any more
 * @param entry The entry
 */
static void box_lod_entry_clear(
    ecs_world_t *world,
    box_lod_entry_t *entry)
{
    if (entry->mesh && world) {
        box_mesh_cache_release(world, entry->mesh);
    }

    ecs_vec_clear(&entry->parts);
    entry->mesh = 0;
    entry->mesh_error = 0.0f;
    entry->shape_error = 0.0f;
    entry->ready = false;
}

void box_lod_cache_fini(
    box_lod_cache_t *cache)
{
    ecs_map_iter_t it = ecs_map_iter(&cache->entries);
    while (ecs_map_next(&it)) {
        box_lod_entry_t *entry = ecs_map_ptr(&it);

        ecs_vec_fini_t(NULL, &entry->parts, box_lod_part_t);
        ecs_os_free(entry);
    }

    ecs_map_fini(&cache->entries);
    if (!cache->jobs) {
        goto done;
    }

    int32_t i, count = ecs_vec_count(&cache->jobs->live);
    for (i = 0; i < count; i ++) {
        box_lod_gen_t *gen = ecs_vec_get_t(&cache->jobs->live, box_lod_gen_t*, i)[0];

        gen->orphaned = true;
        ecs_vec_append_t(NULL, &cache->jobs->garbage, box_lod_gen_t*)[0] = gen;
    }

    ecs_vec_clear(&cache->jobs->live);
    cache->jobs->cache = NULL;
    cache->jobs = NULL;
done:
    return;
}

box_lod_entry_t* box_lod_cache_find(
    const box_lod_cache_t *cache,
    uint64_t id)
{
    return ecs_map_get_ptr(&cache->entries, id);
}

void box_lod_cache_sweep(
    ecs_world_t *world,
    box_lod_cache_t *cache)
{
    if (!cache->jobs) {
        goto done;
    }

    int32_t i = 0;
    while (i < ecs_vec_count(&cache->jobs->garbage)) {
        box_lod_gen_t *gen = ecs_vec_get_t(&cache->jobs->garbage, box_lod_gen_t*, i)[0];
        if (cache->pooled && box_job_cancel(world, gen->job)) {
            i ++;
            continue;
        }

        box_lod_gen_take(&cache->jobs->garbage, gen);
        box_lod_gen_free(gen);
    }

done:
    return;
}

/** Give up the job an entry is waiting for, if it has one.
 *
 * @param world The world
 * @param cache The cache
 * @param entry The entry
 */
static void box_lod_cache_orphan(
    ecs_world_t *world,
    box_lod_cache_t *cache,
    box_lod_entry_t *entry)
{
    if (!entry->job || !cache->jobs) {
        goto done;
    }

    int32_t i, count = ecs_vec_count(&cache->jobs->live);
    for (i = 0; i < count; i ++) {
        box_lod_gen_t *gen = ecs_vec_get_t(&cache->jobs->live, box_lod_gen_t*, i)[0];
        if (gen->job != entry->job) {
            continue;
        }

        gen->orphaned = true;
        ecs_vec_append_t(NULL, &cache->jobs->garbage, box_lod_gen_t*)[0] = gen;
        box_lod_gen_take(&cache->jobs->live, gen);
        break;
    }

    if (cache->pooled) {
        box_job_cancel(world, entry->job);
    }

    cache->queued --;
    entry->job = 0;
done:
    return;
}

bool box_lod_cache_invalidate(
    box_lod_cache_t *cache,
    uint64_t id)
{
    box_lod_entry_t *entry = box_lod_cache_find(cache, id);
    bool result = entry && entry->ready;
    if (entry) {
        entry->stale = true;
    }

    return result;
}

void box_lod_cache_drop(
    ecs_world_t *world,
    box_lod_cache_t *cache,
    uint64_t id)
{
    box_lod_entry_t *entry = box_lod_cache_find(cache, id);
    if (!entry) {
        goto done;
    }

    box_lod_cache_orphan(world, cache, entry);
    box_lod_entry_clear(world, entry);
    ecs_vec_fini_t(NULL, &entry->parts, box_lod_part_t);
    ecs_map_remove(&cache->entries, id);
    ecs_os_free(entry);
done:
    return;
}

void box_lod_cache_clear(
    ecs_world_t *world,
    box_lod_cache_t *cache)
{
    ecs_vec_t dead;
    ecs_vec_init_t(NULL, &dead, uint64_t, 0);

    ecs_map_iter_t it = ecs_map_iter(&cache->entries);
    while (ecs_map_next(&it)) {
        ecs_vec_append_t(NULL, &dead, uint64_t)[0] = ecs_map_key(&it);
    }

    int32_t i, count = ecs_vec_count(&dead);
    for (i = 0; i < count; i ++) {
        box_lod_cache_drop(world, cache, ecs_vec_get_t(&dead, uint64_t, i)[0]);
    }

    ecs_vec_fini_t(NULL, &dead, uint64_t);
}

void box_lod_cache_drop_root(
    ecs_world_t *world,
    box_lod_cache_t *cache,
    uint64_t root)
{
    ecs_vec_t dead;
    ecs_vec_init_t(NULL, &dead, uint64_t, 0);

    ecs_map_iter_t it = ecs_map_iter(&cache->entries);
    while (ecs_map_next(&it)) {
        box_lod_entry_t *entry = ecs_map_ptr(&it);
        if (entry->root == root || entry->id == root) {
            ecs_vec_append_t(NULL, &dead, uint64_t)[0] = entry->id;
        }
    }

    int32_t i, count = ecs_vec_count(&dead);
    for (i = 0; i < count; i ++) {
        box_lod_cache_drop(world, cache, ecs_vec_get_t(&dead, uint64_t, i)[0]);
    }

    ecs_vec_fini_t(NULL, &dead, uint64_t);
}

/** The entry of a node, creating an empty one when it has none.
 *
 * @param cache The cache
 * @param id Identity of the node
 * @param root Identity of its root
 * @return The entry
 */
static box_lod_entry_t* box_lod_cache_ensure(
    box_lod_cache_t *cache,
    uint64_t id,
    uint64_t root)
{
    box_lod_entry_t *result = box_lod_cache_find(cache, id);
    if (result) {
        goto done;
    }

    result = ecs_os_calloc_t(box_lod_entry_t);
    result->id = id;
    result->root = root;
    ecs_vec_init_t(NULL, &result->parts, box_lod_part_t, 0);
    ecs_map_insert_ptr(&cache->entries, id, result);
done:
    return result;
}

/** Look the world up for a worker pool, once.
 *
 * @param world The world
 * @param cache The cache
 * @return True when box.job is imported and generation runs on workers
 */
static bool box_lod_cache_pooled(
    ecs_world_t *world,
    box_lod_cache_t *cache)
{
    if (!cache->checked) {
        cache->pooled = ecs_lookup(world, "box.job.JobPool") != 0;
        cache->checked = true;
    }

    return cache->pooled;
}

/** Hash of the primitives of a collection, which is what a rebuild compares.
 *
 * @param collect The collection
 * @return The hash
 */
static uint64_t box_lod_cache_content(
    const box_lod_collect_t *collect)
{
    return box_lod_cache_hash(BOX_LOD_HASH_OFFSET, collect->primitives,
        (size_t)collect->count * sizeof(box_lod_primitive_t));
}

void box_lod_cache_submit(
    ecs_world_t *world,
    box_lod_cache_t *cache,
    uint64_t id,
    uint64_t root,
    const box_lod_collect_t *collect,
    float need,
    int32_t priority,
    vec3 position)
{
    box_lod_entry_t *entry = box_lod_cache_ensure(cache, id, root);
    uint64_t content = box_lod_cache_content(collect);
    if (entry->job || collect->count <= 0 || collect->full) {
        goto done;
    }

    if (entry->ready && entry->content == content) {
        entry->stale = false;
        goto done;
    }

    if (entry->ready) {
        box_lod_entry_clear(world, entry);
    }

    entry->content = content;
    entry->stale = false;
    entry->version ++;

    box_lod_gen_t *gen = ecs_os_calloc_t(box_lod_gen_t);
    gen->cache = cache;
    gen->id = id;
    gen->root = root;
    gen->version = entry->version;
    gen->need = need > 0.0f ? need : 0.0f;
    gen->count = collect->count;
    gen->primitives = ecs_os_malloc_n(box_lod_primitive_t, collect->count);
    memcpy(gen->primitives, collect->primitives,
        (size_t)collect->count * sizeof(box_lod_primitive_t));

    if (!cache->jobs) {
        cache->jobs = ecs_os_calloc_t(box_lod_jobs_t);
        cache->jobs->cache = cache;
        ecs_vec_init_t(NULL, &cache->jobs->live, box_lod_gen_t*, 0);
        ecs_vec_init_t(NULL, &cache->jobs->garbage, box_lod_gen_t*, 0);
        ecs_atfini(world, box_lod_jobs_fini, cache->jobs);
    }

    ecs_vec_append_t(NULL, &cache->jobs->live, box_lod_gen_t*)[0] = gen;
    cache->queued ++;
    cache->builds ++;
    if (!box_lod_cache_pooled(world, cache)) {
        box_lod_gen_run(gen);
        box_lod_gen_complete(world, gen);
        goto done;
    }

    box_job_desc_t desc = {
        .run = box_lod_gen_run, .complete = box_lod_gen_complete, .ctx = gen,
        .priority = priority, .position = position, .has_position = true, .group = id
    };

    gen->job = box_job_submit(world, &desc);
    entry->job = gen->job;
done:
    return;
}
