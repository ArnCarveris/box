/**
 * @file renderer/bake.c
 * @brief Probe chunks around static receivers, their light lists and the bake compute pass.
 *
 * Applying a packet updates the chunks, occluders and lights, packs them into one word buffer
 * and queues dirty chunks nearest the eye first. The bake pass then rebakes a budget of probes
 * per frame on the GPU.
 */

#include "renderer_internal.h"
#include <stdlib.h>
#include <assert.h>

/** Hash a chunk coordinate.
 *
 * @param c Chunk coordinate
 * @return The hash
 */
static uint32_t box_bake_hash(
    const int32_t c[3])
{
    return (uint32_t)c[0] * 73856093u ^ (uint32_t)c[1] * 19349663u ^
        (uint32_t)c[2] * 83492791u;
}

/** Copy the elements of one vector into another of the same element size.
 *
 * @param dst Destination vector
 * @param src Source vector
 */
static void box_bake_copy(
    box_vec_t *dst,
    const box_vec_t *src)
{
    box_vec_reserve(dst, src->count);
    if (src->count) memcpy(dst->data, src->data, (size_t)src->count * src->size);
    dst->count = src->count;
}

/** Compare the elements of two vectors of the same element size.
 *
 * @param a First vector
 * @param b Second vector
 * @return True if both hold the same bytes
 */
static bool box_bake_equal(
    const box_vec_t *a,
    const box_vec_t *b)
{
    return a->count == b->count && (!a->count ||
        !memcmp(a->data, b->data, (size_t)a->count * a->size));
}

box_bake_mesh_t* box_bake_state_mesh(
    box_bake_state_t *s,
    uint32_t mesh)
{
    while (s->meshes.count <= mesh) {
        box_bake_mesh_t *m = box_vec_push(&s->meshes);
        memset(m, 0, sizeof(*m));
        box_vec_init(&m->instances, sizeof(box_bake_instance_t));
    }

    return box_vec_at(&s->meshes, mesh);
}

void box_bake_state_init(
    box_bake_state_t *s)
{
    memset(s, 0, sizeof(*s));
    box_vec_init(&s->meshes, sizeof(box_bake_mesh_t));
    box_vec_init(&s->chunks, sizeof(box_bake_chunk_t));
    box_vec_init(&s->hash, sizeof(uint32_t));
    box_vec_init(&s->regions, sizeof(box_bake_region_t));
    box_vec_init(&s->region_hash, sizeof(uint32_t));
    box_vec_init(&s->free_chunks, sizeof(uint32_t));
    box_vec_init(&s->occluders, sizeof(box_bake_occluder_t));
    box_vec_init(&s->free_occluders, sizeof(uint32_t));
    box_vec_init(&s->lights, sizeof(box_light_t));
    box_vec_init(&s->emitters, sizeof(box_emitter_t));
    box_vec_init(&s->dirty, sizeof(box_bake_job_t));
    box_vec_init(&s->jobs, sizeof(uint32_t));
    box_vec_init(&s->data, sizeof(uint32_t));
}

/** Release every chunk and region and reset the chunk tables.
 *
 * @param s The bake state
 */
static void box_bake_chunks_clear(
    box_bake_state_t *s)
{
    for (uint32_t i = 0; i < s->chunks.count; i ++) {
        box_bake_chunk_t *c = box_vec_at(&s->chunks, i);
        box_vec_fini(&c->lights);
        box_vec_fini(&c->emitters);
        box_vec_fini(&c->occluders);
    }

    box_vec_clear(&s->chunks);
    box_vec_clear(&s->hash);
    box_vec_clear(&s->regions);
    box_vec_clear(&s->region_hash);
    box_vec_clear(&s->free_chunks);
    box_vec_clear(&s->dirty);
    s->chunk_count = 0;
    s->probe_count = 0;
    s->data_dirty = true;
    s->lists_dirty = true;
}

void box_bake_state_fini(
    box_bake_state_t *s)
{
    box_bake_chunks_clear(s);
    for (uint32_t i = 0; i < s->meshes.count; i ++) {
        box_bake_mesh_t *m = box_vec_at(&s->meshes, i);
        box_vec_fini(&m->instances);
    }

    box_vec_fini(&s->meshes);
    box_vec_fini(&s->chunks);
    box_vec_fini(&s->hash);
    box_vec_fini(&s->regions);
    box_vec_fini(&s->region_hash);
    box_vec_fini(&s->free_chunks);
    box_vec_fini(&s->occluders);
    box_vec_fini(&s->free_occluders);
    box_vec_fini(&s->lights);
    box_vec_fini(&s->emitters);
    box_vec_fini(&s->dirty);
    box_vec_fini(&s->jobs);
    box_vec_fini(&s->data);
}

/** Rebuild the chunk hash table with a new slot count, skipping free chunk slots.
 *
 * @param s The bake state
 * @param count Slot count, a power of two
 */
static void box_bake_rehash(
    box_bake_state_t *s,
    uint32_t count)
{
    box_vec_reserve(&s->hash, count);
    s->hash.count = count;
    uint32_t *hash = s->hash.data;
    memset(hash, 0, (size_t)count * sizeof(uint32_t));
    for (uint32_t i = 0; i < s->chunks.count; i ++) {
        box_bake_chunk_t *c = box_vec_at(&s->chunks, i);
        if (!c->lights.size) continue;
        uint32_t slot = box_bake_hash(c->coord) & (count - 1u);
        while (hash[slot]) slot = (slot + 1u) & (count - 1u);
        hash[slot] = i + 1u;
    }

    s->data_dirty = true;
}

uint32_t box_bake_find_chunk(
    const box_bake_state_t *s,
    const int32_t coord[3])
{
    if (!s->hash.count) return BOX_BAKE_NONE;
    const uint32_t *hash = s->hash.data;
    uint32_t slot = box_bake_hash(coord) & (s->hash.count - 1u);
    for (uint32_t i = 0; i < s->hash.count; i ++) {
        if (!hash[slot]) break;
        uint32_t index = hash[slot] - 1u;
        const box_bake_chunk_t *c = box_vec_at(&s->chunks, index);
        if (!memcmp(c->coord, coord, sizeof(c->coord))) return index;
        slot = (slot + 1u) & (s->hash.count - 1u);
    }

    return BOX_BAKE_NONE;
}

/** Find the chunk at a coordinate or create it, growing the hash table when needed.
 *
 * @param s The bake state
 * @param coord Chunk coordinate
 * @return The chunk index
 */
static uint32_t box_bake_chunk_add(
    box_bake_state_t *s,
    const int32_t coord[3])
{
    uint32_t index = box_bake_find_chunk(s, coord);
    if (index != BOX_BAKE_NONE) return index;
    if (!s->hash.count || (s->chunk_count + 1u) * 2u > s->hash.count) {
        box_bake_rehash(s, s->hash.count ? s->hash.count * 2u : 64u);
    }

    box_bake_chunk_t *c;
    if (s->free_chunks.count) {
        index = ((uint32_t*)s->free_chunks.data)[--s->free_chunks.count];
        c = box_vec_at(&s->chunks, index);
    } else {
        index = s->chunks.count;
        c = box_vec_push(&s->chunks);
    }

    memset(c, 0, sizeof(*c));
    memcpy(c->coord, coord, sizeof(c->coord));
    box_vec_init(&c->lights, sizeof(uint32_t));
    box_vec_init(&c->emitters, sizeof(uint32_t));
    box_vec_init(&c->occluders, sizeof(uint32_t));
    c->fresh = true;
    c->dirty = true;
    uint32_t *hash = s->hash.data;
    uint32_t slot = box_bake_hash(coord) & (s->hash.count - 1u);
    while (hash[slot]) slot = (slot + 1u) & (s->hash.count - 1u);
    hash[slot] = index + 1u;
    s->chunk_count ++;
    s->lists_dirty = true;
    s->data_dirty = true;

    return index;
}

/** Compute the range of chunk coordinates the padded bounds of an instance cover.
 *
 * @param s The bake state
 * @param v The instance
 * @param pad Distance added to every side of the bounds
 * @param lo Lowest chunk coordinate
 * @param hi Highest chunk coordinate
 */
static void box_bake_range(
    const box_bake_state_t *s,
    const box_bake_instance_t *v,
    float pad,
    int32_t lo[3],
    int32_t hi[3])
{
    float size = s->spacing * BOX_BAKE_CHUNK_DIM;
    for (int a = 0; a < 3; a ++) {
        lo[a] = (int32_t)floorf((v->center[a] - v->extent[a] - pad) / size);
        hi[a] = (int32_t)floorf((v->center[a] + v->extent[a] + pad) / size);
    }
}

/** Test whether a sphere overlaps the box of a chunk.
 *
 * @param s The bake state
 * @param c The chunk
 * @param position Center of the sphere
 * @param radius Radius of the sphere
 * @return True if the sphere reaches the chunk
 */
static bool box_bake_sphere_chunk(
    const box_bake_state_t *s,
    const box_bake_chunk_t *c,
    const float position[3],
    float radius)
{
    float d2 = 0, size = s->spacing * BOX_BAKE_CHUNK_DIM;
    for (int a = 0; a < 3; a ++) {
        float lo = (float)c->coord[a] * size;
        float d = fmaxf(fmaxf(lo - position[a], position[a] - lo - size), 0);
        d2 += d * d;
    }

    return d2 <= radius * radius;
}

/** Find or create the cached region of a chunk coordinate.
 *
 * A new region is lit when a light or emitter range, padded by two probes, reaches it.
 *
 * @param s The bake state
 * @param coord Chunk coordinate
 * @return The region
 */
static box_bake_region_t* box_bake_region(
    box_bake_state_t *s,
    const int32_t coord[3])
{
    if (!s->region_hash.count || (s->regions.count + 1u) * 2u > s->region_hash.count) {
        uint32_t count = s->region_hash.count ? s->region_hash.count * 2u : 64u;
        box_vec_reserve(&s->region_hash, count);
        s->region_hash.count = count;
        uint32_t *hash = s->region_hash.data;
        memset(hash, 0, (size_t)count * 4);
        for (uint32_t i = 0; i < s->regions.count; i ++) {
            const box_bake_region_t *r = box_vec_at(&s->regions, i);
            uint32_t slot = box_bake_hash(r->coord) & (count - 1u);
            while (hash[slot]) slot = (slot + 1u) & (count - 1u);
            hash[slot] = i + 1u;
        }
    }

    uint32_t *hash = s->region_hash.data;
    uint32_t slot = box_bake_hash(coord) & (s->region_hash.count - 1u);
    while (hash[slot]) {
        box_bake_region_t *r = box_vec_at(&s->regions, hash[slot] - 1u);
        if (!memcmp(r->coord, coord, sizeof(r->coord))) return r;
        slot = (slot + 1u) & (s->region_hash.count - 1u);
    }

    box_bake_region_t *r = box_vec_push(&s->regions);
    memset(r, 0, sizeof(*r));
    memcpy(r->coord, coord, sizeof(r->coord));
    hash[slot] = s->regions.count;
    box_bake_chunk_t c = {0};
    memcpy(c.coord, coord, sizeof(c.coord));
    float pad = s->spacing * 2;
    for (uint32_t i = 0; i < s->lights.count && !r->lit; i ++) {
        const box_light_t *l = box_vec_at(&s->lights, i);
        r->lit = box_bake_sphere_chunk(s, &c, l->position, l->range + pad);
    }

    for (uint32_t i = 0; i < s->emitters.count && !r->lit; i ++) {
        const box_emitter_t *e = box_vec_at(&s->emitters, i);
        r->lit = box_bake_sphere_chunk(s, &c, e->position, e->range + pad);
    }

    return r;
}

/** Add or remove one reference of an instance to the chunk at a lit coordinate.
 *
 * A receiver reference counts towards the receivers of the chunk and creates the chunk; an
 * occlusion reference adds the occluder of the instance to the chunk or removes it.
 *
 * @param s The bake state
 * @param v The instance
 * @param coord Chunk coordinate
 * @param add True to add the reference, false to remove it
 * @param occlusion True for an occlusion reference, false for a receiver reference
 */
static void box_bake_reference(
    box_bake_state_t *s,
    const box_bake_instance_t *v,
    const int32_t coord[3],
    bool add,
    bool occlusion)
{
    box_bake_region_t *r = box_bake_region(s, coord);
    if (!r->lit || r->visit == s->region_visit) return;
    r->visit = s->region_visit;
    uint32_t index = add && !occlusion ?
        box_bake_chunk_add(s, coord) : box_bake_find_chunk(s, coord);
    assert(index != BOX_BAKE_NONE);
    box_bake_chunk_t *c = box_vec_at(&s->chunks, index);
    if (!occlusion) {
        if (add) c->receivers ++;
        else { assert(c->receivers); c->receivers --; }
    } else {
        if (add) *(uint32_t*)box_vec_push(&c->occluders) = v->occluder;
        else {
            uint32_t *ids = c->occluders.data;
            for (uint32_t i = 0; i < c->occluders.count; i ++) {
                if (ids[i] == v->occluder) {
                    ids[i] = ids[--c->occluders.count];
                    break;
                }
            }
        }

        c->occlusion_dirty = true;
        s->data_dirty = true;
    }
}

/** Add or remove the references of an instance to every lit chunk its bounds cover.
 *
 * Bounds that cover more than 512 chunks are walked once per light or emitter, clipped to
 * its range, so that a huge instance only visits chunks that can be lit.
 *
 * @param s The bake state
 * @param v The instance
 * @param add True to add the references, false to remove them
 * @param occlusion True for occlusion references, false for receiver references
 */
static void box_bake_references(
    box_bake_state_t *s,
    const box_bake_instance_t *v,
    bool add,
    bool occlusion)
{
    if (!v->alive || (occlusion && v->occluder == BOX_BAKE_NONE) ||
        (!s->lights.count && !s->emitters.count)) return;
    if (++s->region_visit == 0) {
        for (uint32_t i = 0; i < s->regions.count; i ++) {
            ((box_bake_region_t*)s->regions.data)[i].visit = 0;
        }

        s->region_visit = 1;
    }

    int32_t lo[3], hi[3];
    box_bake_range(s, v, occlusion ? 0 : s->spacing, lo, hi);
    double volume = 1;
    for (int a = 0; a < 3; a ++) volume *= (double)hi[a] - lo[a] + 1;
    uint32_t passes = volume <= 512 ? 1 : s->lights.count + s->emitters.count;
    for (uint32_t pass = 0; pass < passes; pass ++) {
        int32_t first[3], last[3];
        memcpy(first, lo, sizeof(first));
        memcpy(last, hi, sizeof(last));
        if (volume > 512) {
            box_bake_instance_t region = {0};
            float radius;
            if (pass < s->lights.count) {
                const box_light_t *l = box_vec_at(&s->lights, pass);
                memcpy(region.center, l->position, sizeof(region.center));
                radius = l->range;
            } else {
                const box_emitter_t *e = box_vec_at(&s->emitters, pass - s->lights.count);
                memcpy(region.center, e->position, sizeof(region.center));
                radius = e->range;
            }

            for (int a = 0; a < 3; a ++) region.extent[a] = radius;
            box_bake_range(s, &region, s->spacing * 2, first, last);
            for (int a = 0; a < 3; a ++) {
                first[a] = first[a] > lo[a] ? first[a] : lo[a];
                last[a] = last[a] < hi[a] ? last[a] : hi[a];
            }
        }

        for (int32_t z = first[2]; z <= last[2]; z ++) {
            for (int32_t y = first[1]; y <= last[1]; y ++) {
                for (int32_t x = first[0]; x <= last[0]; x ++) {
                    int32_t coord[3] = {x, y, z};
                    box_bake_reference(s, v, coord, add, occlusion);
                }
            }
        }
    }
}

/** Add or remove an instance as a light receiver of the chunks around it.
 *
 * @param s The bake state
 * @param v The instance
 * @param add True to add, false to remove
 */
static void box_bake_receiver(
    box_bake_state_t *s,
    const box_bake_instance_t *v,
    bool add)
{
    box_bake_references(s, v, add, false);
}

/** Add or remove the occluder of an instance to the chunks it overlaps.
 *
 * @param s The bake state
 * @param v The instance
 * @param add True to add, false to remove
 */
static void box_bake_occlusion(
    box_bake_state_t *s,
    const box_bake_instance_t *v,
    bool add)
{
    box_bake_references(s, v, add, true);
}

/** Mark a chunk to be rebaked from its first probe.
 *
 * @param c The chunk
 */
static void box_bake_mark(
    box_bake_chunk_t *c)
{
    c->dirty = true;
    c->next_probe = 0;
}

/** Visit the receiving chunks a sphere reaches.
 *
 * @param s The bake state
 * @param position Center of the sphere
 * @param radius Radius of the sphere
 * @param light Light or emitter index to append for kinds 1 and 2
 * @param kind 0 marks the chunks dirty, 1 appends a light, 2 appends an emitter
 */
static void box_bake_sphere(
    box_bake_state_t *s,
    const float position[3],
    float radius,
    uint32_t light,
    int kind)
{
    float size = s->spacing * BOX_BAKE_CHUNK_DIM;
    int32_t lo[3], hi[3];
    double volume = 1;
    for (int a = 0; a < 3; a ++) {
        lo[a] = (int32_t)floorf((position[a] - radius) / size);
        hi[a] = (int32_t)floorf((position[a] + radius) / size);
        volume *= (double)hi[a] - lo[a] + 1;
    }

    uint32_t count = volume < s->chunks.count ? (uint32_t)volume : s->chunks.count;
    for (uint32_t i = 0; i < count; i ++) {
        uint32_t index = i;
        if (volume < s->chunks.count) {
            uint32_t nx = (uint32_t)(hi[0] - lo[0] + 1);
            uint32_t ny = (uint32_t)(hi[1] - lo[1] + 1);
            int32_t coord[3] = {lo[0] + (int32_t)(i % nx),
                lo[1] + (int32_t)((i / nx) % ny), lo[2] + (int32_t)(i / (nx * ny))};
            index = box_bake_find_chunk(s, coord);
            if (index == BOX_BAKE_NONE) continue;
        }

        box_bake_chunk_t *c = box_vec_at(&s->chunks, index);
        if (!c->receivers || !box_bake_sphere_chunk(s, c, position, radius)) continue;
        if (kind == 0) box_bake_mark(c);
        else *(uint32_t*)box_vec_push(kind == 1 ? &c->lights : &c->emitters) = light;
    }
}

/** Compute the world bounds of a static instance and its occluder box.
 *
 * @param m The mesh of the instance
 * @param u The static update of the instance
 * @param min_size Minimum size of two axes of an automatic occluder
 * @param mode Occluder mode: -1 never, 0 automatic, 1 always
 * @param v The instance to fill
 * @param o The occluder to fill
 * @return True if the instance occludes
 */
static bool box_bake_geometry(
    const box_bake_mesh_t *m,
    const box_static_update_t *u,
    float min_size,
    int32_t mode,
    box_bake_instance_t *v,
    box_bake_occluder_t *o)
{
    memset(v, 0, sizeof(*v));
    memset(o, 0, sizeof(*o));
    v->alive = true;
    v->mode = (int8_t)mode;
    v->occluder = BOX_BAKE_NONE;
    float axes[3][3], length[3];
    const float *t = u->data.transform;
    for (int row = 0; row < 3; row ++) {
        v->center[row] = t[row * 4 + 3];
        for (int a = 0; a < 3; a ++) {
            float half = (m->bounds_max[a] - m->bounds_min[a]) * 0.5f;
            float center = (m->bounds_max[a] + m->bounds_min[a]) * 0.5f;
            v->center[row] += t[row * 4 + a] * center;
            axes[a][row] = t[row * 4 + a] * fmaxf(half, 1e-4f);
            v->extent[row] += fabsf(axes[a][row]);
        }

        o->bounds_min[row] = v->center[row] - v->extent[row];
        o->bounds_max[row] = v->center[row] + v->extent[row];
    }

    int large = 0;
    for (int a = 0; a < 3; a ++) {
        length[a] = sqrtf(axes[a][0] * axes[a][0] + axes[a][1] * axes[a][1] +
            axes[a][2] * axes[a][2]);
        large += length[a] * 2 >= min_size;
    }

    if (mode < 0 || (!mode && (large < 2 || !box_instance_caster(&u->data) ||
        box_instance_blend(&u->data) != BOX_BLEND_OPAQUE))) return false;
    float cross[3][3];
    for (int a = 0; a < 3; a ++) {
        const float *b = axes[(a + 1) % 3], *c = axes[(a + 2) % 3];
        cross[a][0] = b[1] * c[2] - b[2] * c[1];
        cross[a][1] = b[2] * c[0] - b[0] * c[2];
        cross[a][2] = b[0] * c[1] - b[1] * c[0];
    }

    float det = axes[0][0] * cross[0][0] + axes[0][1] * cross[0][1] +
        axes[0][2] * cross[0][2];
    if (fabsf(det) < 1e-12f) return false;
    for (int a = 0; a < 3; a ++) {
        for (int row = 0; row < 3; row ++) {
            o->inverse[a][row] = cross[a][row] / det;
            o->inverse[a][3] -= o->inverse[a][row] * v->center[row];
        }
    }

    return true;
}

/** Remove a live instance from the chunks and free its occluder slot.
 *
 * @param s The bake state
 * @param v The instance
 */
static void box_bake_remove(
    box_bake_state_t *s,
    box_bake_instance_t *v)
{
    if (!v->alive) return;
    box_bake_occlusion(s, v, false);
    box_bake_receiver(s, v, false);
    if (v->occluder != BOX_BAKE_NONE) {
        *(uint32_t*)box_vec_push(&s->free_occluders) = v->occluder;
    }

    v->alive = false;
}

/** Free the chunks that no receiver references and update the probe count.
 *
 * @param s The bake state
 */
static void box_bake_prune(
    box_bake_state_t *s)
{
    bool removed = false;
    for (uint32_t i = 0; i < s->chunks.count; i ++) {
        box_bake_chunk_t *c = box_vec_at(&s->chunks, i);
        if (c->receivers || !c->lights.size) continue;
        box_vec_fini(&c->lights);
        box_vec_fini(&c->emitters);
        box_vec_fini(&c->occluders);
        memset(c, 0, sizeof(*c));
        *(uint32_t*)box_vec_push(&s->free_chunks) = i;
        s->chunk_count --;
        removed = true;
    }

    if (removed) box_bake_rehash(s, s->hash.count);
    s->probe_count = s->chunk_count * BOX_BAKE_CHUNK_PROBES;
}

/** Copy the bounds of the meshes a packet creates.
 *
 * @param s The bake state
 * @param p The packet
 */
static void box_bake_apply_meshes(
    box_bake_state_t *s,
    const box_packet_t *p)
{
    for (uint32_t i = 0; i < p->mesh_create.count; i ++) {
        const box_mesh_cmd_t *cmd = box_vec_at(&p->mesh_create, i);
        box_bake_mesh_t *m = box_bake_state_mesh(s, cmd->handle);
        memcpy(m->bounds_min, cmd->bounds_min, sizeof(m->bounds_min));
        memcpy(m->bounds_max, cmd->bounds_max, sizeof(m->bounds_max));
    }
}

/** Disable baking, dropping chunks, occluders, lights and instances if it was enabled.
 *
 * @param s The bake state
 */
static void box_bake_disable(
    box_bake_state_t *s)
{
    if (s->enabled) {
        box_bake_chunks_clear(s);
        box_vec_clear(&s->occluders);
        box_vec_clear(&s->free_occluders);
        box_vec_clear(&s->lights);
        box_vec_clear(&s->emitters);
        for (uint32_t i = 0; i < s->meshes.count; i ++) {
            box_bake_mesh_t *m = box_vec_at(&s->meshes, i);
            box_vec_clear(&m->instances);
        }
    }

    s->enabled = false;
}

/** Test whether the lit regions change, because of spacing or light positions and ranges.
 *
 * @param s The bake state
 * @param p The packet
 * @param respace True if the probe spacing changes
 * @return True if the cached regions must be rebuilt
 */
static bool box_bake_regions_changed(
    const box_bake_state_t *s,
    const box_packet_t *p,
    bool respace)
{
    bool result = respace || s->lights.count != p->bake_lights.count ||
        s->emitters.count != p->emitters.count;
    for (uint32_t i = 0; i < s->lights.count && !result; i ++) {
        const box_light_t *a = box_vec_at(&s->lights, i);
        const box_light_t *b = box_vec_at(&p->bake_lights, i);
        result = memcmp(a->position, b->position, sizeof(a->position)) ||
            a->range != b->range;
    }

    for (uint32_t i = 0; i < s->emitters.count && !result; i ++) {
        const box_emitter_t *a = box_vec_at(&s->emitters, i);
        const box_emitter_t *b = box_vec_at(&p->emitters, i);
        result = memcmp(a->position, b->position, sizeof(a->position)) ||
            a->range != b->range;
    }

    return result;
}

/** Take the eye of the first lit view of a packet, or of its main view.
 *
 * @param s The bake state
 * @param p The packet
 */
static void box_bake_apply_eye(
    box_bake_state_t *s,
    const box_packet_t *p)
{
    const box_view_t *view = &p->view;
    for (uint32_t i = 0; i < p->views.count; i ++) {
        const box_view_t *candidate = box_vec_at(&p->views, i);
        if (!candidate->unlit) {
            view = candidate;
            break;
        }
    }

    memcpy(s->eye, view->eye, sizeof(s->eye));
}

/** Mark the chunks of the lights that changed dirty and copy the new lights.
 *
 * @param s The bake state
 * @param p The packet
 */
static void box_bake_apply_lights(
    box_bake_state_t *s,
    const box_packet_t *p)
{
    for (uint32_t i = 0; i < s->lights.count; i ++) {
        const box_light_t *l = box_vec_at(&s->lights, i);
        bool kept = i < p->bake_lights.count &&
            !memcmp(l, box_vec_at(&p->bake_lights, i), sizeof(*l));
        if (!kept) box_bake_sphere(s, l->position, l->range, 0, 0);
    }

    for (uint32_t i = 0; i < p->bake_lights.count; i ++) {
        const box_light_t *l = box_vec_at(&p->bake_lights, i);
        bool kept = i < s->lights.count &&
            !memcmp(l, box_vec_at(&s->lights, i), sizeof(*l));
        if (!kept) box_bake_sphere(s, l->position, l->range, 0, 0);
    }

    box_bake_copy(&s->lights, &p->bake_lights);
}

/** Mark the chunks of the emitters that changed dirty and copy the new emitters.
 *
 * @param s The bake state
 * @param p The packet
 */
static void box_bake_apply_emitters(
    box_bake_state_t *s,
    const box_packet_t *p)
{
    for (uint32_t i = 0; i < s->emitters.count; i ++) {
        const box_emitter_t *l = box_vec_at(&s->emitters, i);
        bool kept = i < p->emitters.count &&
            !memcmp(l, box_vec_at(&p->emitters, i), sizeof(*l));
        if (!kept) box_bake_sphere(s, l->position, l->range, 0, 0);
    }

    for (uint32_t i = 0; i < p->emitters.count; i ++) {
        const box_emitter_t *l = box_vec_at(&p->emitters, i);
        bool kept = i < s->emitters.count &&
            !memcmp(l, box_vec_at(&s->emitters, i), sizeof(*l));
        if (!kept) box_bake_sphere(s, l->position, l->range, 0, 0);
    }

    box_bake_copy(&s->emitters, &p->emitters);
}

/** Drop the region cache and add the references of every instance again.
 *
 * @param s The bake state
 */
static void box_bake_rebuild_references(
    box_bake_state_t *s)
{
    box_vec_clear(&s->regions);
    box_vec_clear(&s->region_hash);
    for (uint32_t i = 0; i < s->chunks.count; i ++) {
        box_bake_chunk_t *c = box_vec_at(&s->chunks, i);
        c->receivers = 0;
        box_vec_clear(&c->occluders);
    }

    for (uint32_t i = 0; i < s->meshes.count; i ++) {
        box_bake_mesh_t *m = box_vec_at(&s->meshes, i);
        for (uint32_t j = 0; j < m->instances.count; j ++) {
            box_bake_instance_t *v = box_vec_at(&m->instances, j);
            box_bake_receiver(s, v, true);
            box_bake_occlusion(s, v, true);
        }
    }

    for (uint32_t i = 0; i < s->chunks.count; i ++) {
        ((box_bake_chunk_t*)s->chunks.data)[i].occlusion_dirty = false;
    }

    s->lists_dirty = true;
    s->data_dirty = true;
}

/** Resize the instance slots of meshes, removing the instances past the new count.
 *
 * @param s The bake state
 * @param p The packet
 */
static void box_bake_apply_counts(
    box_bake_state_t *s,
    const box_packet_t *p)
{
    for (uint32_t i = 0; i < p->static_counts.count; i ++) {
        const box_static_count_t *n = box_vec_at(&p->static_counts, i);
        box_bake_mesh_t *m = box_bake_state_mesh(s, n->mesh);
        for (uint32_t j = n->count; j < m->instances.count; j ++) {
            box_bake_remove(s, box_vec_at(&m->instances, j));
        }

        while (m->instances.count < n->count) {
            memset(box_vec_push(&m->instances), 0, sizeof(box_bake_instance_t));
        }

        m->instances.count = n->count;
    }
}

/** Pick the occluder slot of an instance: its current one, a free one or a new one.
 *
 * @param s The bake state
 * @param v The instance as it was before the update
 * @return The occluder index
 */
static uint32_t box_bake_occluder_slot(
    box_bake_state_t *s,
    const box_bake_instance_t *v)
{
    uint32_t result;
    if (v->alive && v->occluder != BOX_BAKE_NONE) {
        result = v->occluder;
    } else if (s->free_occluders.count) {
        result = ((uint32_t*)s->free_occluders.data)[--s->free_occluders.count];
    } else {
        result = s->occluders.count;
        box_vec_push(&s->occluders);
    }

    return result;
}

/** Apply one static instance update, moving its references when its bounds or occluder change.
 *
 * @param s The bake state
 * @param u The static update
 */
static void box_bake_apply_update(
    box_bake_state_t *s,
    const box_static_update_t *u)
{
    box_bake_mesh_t *m = box_bake_state_mesh(s, u->mesh);
    while (m->instances.count <= u->slot) {
        memset(box_vec_push(&m->instances), 0, sizeof(box_bake_instance_t));
    }

    box_bake_instance_t next, *v = box_vec_at(&m->instances, u->slot);
    if (u->data.params[2] < 0.5f) {
        box_bake_remove(s, v);
        goto done;
    }

    box_bake_occluder_t o;
    int32_t mode = u->bake_occluder == 2 ? (v->alive ? v->mode : 0) : u->bake_occluder;
    bool occluder = box_bake_geometry(m, u, s->occluder_min_size, mode, &next, &o);
    bool same = v->alive && !memcmp(v->center, next.center, sizeof(next.center)) &&
        !memcmp(v->extent, next.extent, sizeof(next.extent));
    bool same_occluder = (v->occluder != BOX_BAKE_NONE) == occluder;
    if (same && same_occluder && (!occluder || !memcmp(&o,
        box_vec_at(&s->occluders, v->occluder), sizeof(o)))) {
        v->mode = next.mode;
        goto done;
    }

    if (v->alive) box_bake_occlusion(s, v, false);
    if (!same) {
        box_bake_receiver(s, v, false);
        box_bake_receiver(s, &next, true);
    }

    if (occluder) {
        next.occluder = box_bake_occluder_slot(s, v);
        *(box_bake_occluder_t*)box_vec_at(&s->occluders, next.occluder) = o;
    } else if (v->alive && v->occluder != BOX_BAKE_NONE) {
        *(uint32_t*)box_vec_push(&s->free_occluders) = v->occluder;
    }

    *v = next;
    box_bake_occlusion(s, v, true);
done:
    return;
}

/** Mark the chunks near chunks whose occluders changed dirty.
 *
 * @param s The bake state
 * @param refresh True to mark the chunks the largest light range reaches, false to only
 *        reset the occlusion flags
 */
static void box_bake_apply_occlusion(
    box_bake_state_t *s,
    bool refresh)
{
    float spacing = s->spacing;
    float influence = 0;
    for (uint32_t i = 0; i < s->lights.count; i ++) {
        influence = fmaxf(influence, ((box_light_t*)s->lights.data)[i].range);
    }

    for (uint32_t i = 0; i < s->emitters.count; i ++) {
        influence = fmaxf(influence, ((box_emitter_t*)s->emitters.data)[i].range);
    }

    for (uint32_t i = 0; i < s->chunks.count; i ++) {
        box_bake_chunk_t *c = box_vec_at(&s->chunks, i);
        if (!c->occlusion_dirty) continue;
        c->occlusion_dirty = false;
        if (refresh && influence > 0) {
            float center[3];
            for (int a = 0; a < 3; a ++) {
                center[a] = ((float)c->coord[a] + 0.5f) * spacing * BOX_BAKE_CHUNK_DIM;
            }

            box_bake_sphere(s, center, influence + spacing * BOX_BAKE_CHUNK_DIM, 0, 0);
        }
    }
}

/** Rebuild the light and emitter lists of every chunk.
 *
 * @param s The bake state
 */
static void box_bake_assign_lights(
    box_bake_state_t *s)
{
    for (uint32_t i = 0; i < s->chunks.count; i ++) {
        box_bake_chunk_t *c = box_vec_at(&s->chunks, i);
        box_vec_clear(&c->lights);
        box_vec_clear(&c->emitters);
    }

    for (uint32_t i = 0; i < s->lights.count; i ++) {
        box_light_t *l = box_vec_at(&s->lights, i);
        box_bake_sphere(s, l->position, l->range, i, 1);
    }

    for (uint32_t i = 0; i < s->emitters.count; i ++) {
        box_emitter_t *l = box_vec_at(&s->emitters, i);
        box_bake_sphere(s, l->position, l->range, i, 2);
    }

    s->data_dirty = true;
    s->lists_dirty = false;
}

/** Trim an oversized region cache and collect the dirty receiving chunks.
 *
 * @param s The bake state
 */
static void box_bake_queue(
    box_bake_state_t *s)
{
    if (s->regions.count > s->chunk_count * 4u + 4096u) {
        box_vec_clear(&s->regions);
        box_vec_clear(&s->region_hash);
    }

    box_vec_clear(&s->dirty);
    for (uint32_t i = 0; i < s->chunks.count; i ++) {
        box_bake_chunk_t *c = box_vec_at(&s->chunks, i);
        if (c->receivers && c->dirty) {
            box_bake_job_t *job = box_vec_push(&s->dirty);
            job->chunk = i;
            job->distance = 0;
        }
    }
}

/** Apply the bake settings, lights and static instances of a packet with baking enabled.
 *
 * @param s The bake state
 * @param p The packet
 */
static void box_bake_enable(
    box_bake_state_t *s,
    const box_packet_t *p)
{
    bool had_chunks = s->chunk_count != 0;
    float spacing = p->bake.spacing > 0 ? p->bake.spacing : 1;
    bool respace = s->enabled && s->spacing != spacing;
    bool lights_changed = !box_bake_equal(&s->lights, &p->bake_lights);
    bool emitters_changed = !box_bake_equal(&s->emitters, &p->emitters);
    bool regions_changed = box_bake_regions_changed(s, p, respace);
    s->enabled = true;
    s->spacing = spacing;
    s->occluder_min_size = p->bake.occluder_min_size > 0 ? p->bake.occluder_min_size : 0.5f;
    box_bake_apply_eye(s, p);
    if (lights_changed) box_bake_apply_lights(s, p);
    if (emitters_changed) box_bake_apply_emitters(s, p);
    if (respace) box_bake_chunks_clear(s);
    if (regions_changed) box_bake_rebuild_references(s);
    box_bake_apply_counts(s, p);
    for (uint32_t i = 0; i < p->static_updates.count; i ++) {
        box_bake_apply_update(s, box_vec_at(&p->static_updates, i));
    }

    box_bake_apply_occlusion(s, had_chunks && !respace);
    box_bake_prune(s);
    if (s->lists_dirty || lights_changed || emitters_changed) box_bake_assign_lights(s);
    box_bake_queue(s);
}

void box_bake_state_apply(
    box_bake_state_t *s,
    const box_packet_t *p)
{
    box_bake_apply_meshes(s, p);
    if (p->bake.enabled) {
        box_bake_enable(s, p);
    } else {
        box_bake_disable(s);
    }
}

/** Order bake jobs by distance to the eye, then by chunk index.
 *
 * @param a First box_bake_job_t
 * @param b Second box_bake_job_t
 * @return Negative, zero or positive as for qsort
 */
static int box_bake_compare(
    const void *a,
    const void *b)
{
    const box_bake_job_t *x = a, *y = b;
    if (x->distance != y->distance) return x->distance < y->distance ? -1 : 1;

    return x->chunk < y->chunk ? -1 : x->chunk != y->chunk;
}

uint32_t box_bake_state_take(
    box_bake_state_t *s,
    uint32_t budget)
{
    box_vec_clear(&s->jobs);
    for (uint32_t i = 0; i < s->dirty.count; i ++) {
        box_bake_job_t *job = box_vec_at(&s->dirty, i);
        box_bake_chunk_t *c = box_vec_at(&s->chunks, job->chunk);
        job->distance = 0;
        for (int a = 0; a < 3; a ++) {
            float d = ((float)c->coord[a] + 0.5f) * s->spacing * BOX_BAKE_CHUNK_DIM - s->eye[a];
            job->distance += d * d;
        }
    }

    if (s->dirty.count > 1) {
        qsort(s->dirty.data, s->dirty.count, sizeof(box_bake_job_t), box_bake_compare);
    }

    uint32_t kept = 0;
    for (uint32_t i = 0; i < s->dirty.count; i ++) {
        box_bake_job_t job = ((box_bake_job_t*)s->dirty.data)[i];
        box_bake_chunk_t *c = box_vec_at(&s->chunks, job.chunk);
        if (!c->receivers || !c->dirty) continue;
        while (budget && c->next_probe < BOX_BAKE_CHUNK_PROBES) {
            *(uint32_t*)box_vec_push(&s->jobs) =
                job.chunk * BOX_BAKE_CHUNK_PROBES + c->next_probe ++;
            budget --;
        }

        if (c->next_probe == BOX_BAKE_CHUNK_PROBES) c->dirty = false;
        else ((box_bake_job_t*)s->dirty.data)[kept ++] = job;
    }

    s->dirty.count = kept;

    return s->jobs.count;
}

/** Test whether a segment passes through the unit cube of an occluder.
 *
 * @param o The occluder
 * @param from Start of the segment
 * @param to End of the segment
 * @return True if the segment intersects the occluder away from its end points
 */
static bool box_bake_intersect(
    const box_bake_occluder_t *o,
    const float from[3],
    const float to[3])
{
    float near = 0, far = 1;
    for (int a = 0; a < 3; a ++) {
        const float *r = o->inverse[a];
        float p = r[3], d = 0;
        for (int b = 0; b < 3; b ++) { p += r[b] * from[b]; d += r[b] * (to[b] - from[b]); }
        if (fabsf(d) < 1e-8f) { if (fabsf(p) > 1) return false; }
        else {
            float t0 = (-1 - p) / d, t1 = (1 - p) / d;
            near = fmaxf(near, fminf(t0, t1));
            far = fminf(far, fmaxf(t0, t1));
            if (near > far) return false;
        }
    }

    return far > 1e-5f && near < 0.99999f;
}

/** Walk the chunks a segment crosses and test it against their occluders.
 *
 * @param s The bake state
 * @param from Start of the segment
 * @param to End of the segment
 * @param ignore Occluder index to skip, BOX_BAKE_NONE for none
 * @return True if no other occluder intersects the segment
 */
static bool box_bake_visible_ignoring(
    const box_bake_state_t *s,
    const float from[3],
    const float to[3],
    uint32_t ignore)
{
    int32_t coord[3], end[3], step[3];
    float next[3], delta[3], size = s->spacing * BOX_BAKE_CHUNK_DIM;
    uint32_t visits = 1;
    for (int a = 0; a < 3; a ++) {
        coord[a] = (int32_t)floorf(from[a] / size);
        end[a] = (int32_t)floorf(to[a] / size);
        visits += (uint32_t)abs(end[a] - coord[a]);
        float d = to[a] - from[a];
        step[a] = d < 0 ? -1 : 1;
        delta[a] = fabsf(d) > 1e-8f ? size / fabsf(d) : 1e30f;
        next[a] = fabsf(d) > 1e-8f ?
            (((float)coord[a] + (d > 0 ? 1 : 0)) * size - from[a]) / d : 1e30f;
    }

    bool result = true;
    for (uint32_t i = 0; i < visits && result; i ++) {
        uint32_t index = box_bake_find_chunk(s, coord);
        if (index != BOX_BAKE_NONE) {
            const box_bake_chunk_t *c = box_vec_at(&s->chunks, index);
            for (uint32_t j = 0; j < c->occluders.count && result; j ++) {
                uint32_t id = ((uint32_t*)c->occluders.data)[j];
                result = id == ignore ||
                    !box_bake_intersect(box_vec_at(&s->occluders, id), from, to);
            }
        }

        int a = next[0] < next[1] ? 0 : 1;
        a = next[a] < next[2] ? a : 2;
        coord[a] += step[a];
        next[a] += delta[a];
    }

    return result;
}

bool box_bake_visible_sparse(
    const box_bake_state_t *s,
    const float from[3],
    const float to[3])
{
    return box_bake_visible_ignoring(s, from, to, BOX_BAKE_NONE);
}

/** Find the occluder of the instance an emitter belongs to.
 *
 * @param s The bake state
 * @param id Emitter index
 * @return The occluder index, BOX_BAKE_NONE if the emitter has no occluder
 */
static uint32_t box_bake_emitter_occluder(
    const box_bake_state_t *s,
    uint32_t id)
{
    const box_emitter_t *e = box_vec_at(&s->emitters, id);
    if (e->mesh >= s->meshes.count) return BOX_BAKE_NONE;
    const box_bake_mesh_t *m = box_vec_at(&s->meshes, e->mesh);
    if (e->slot >= m->instances.count) return BOX_BAKE_NONE;
    const box_bake_instance_t *v = box_vec_at(&m->instances, e->slot);

    return v->alive ? v->occluder : BOX_BAKE_NONE;
}

void box_bake_sparse_probe(
    const box_bake_state_t *s,
    uint32_t chunk,
    uint32_t probe,
    box_probe_t *out)
{
    memset(out, 0, sizeof(*out));
    const box_bake_chunk_t *c = box_vec_at(&s->chunks, chunk);
    float origin[3];
    for (int a = 0; a < 3; a ++) {
        origin[a] = ((float)c->coord[a] * 4 + (float)((probe >> (a * 2)) & 3u) + 0.5f) *
            s->spacing;
    }

    if (!box_bake_visible_sparse(s, origin, origin)) {
        out->r[0] = -1;
        goto done;
    }

    for (int kind = 0; kind < 2; kind ++) {
        const box_vec_t *list = kind ? &c->emitters : &c->lights;
        for (uint32_t i = 0; i < list->count; i ++) {
            uint32_t id = ((uint32_t*)list->data)[i];
            const box_light_t *l = kind ? NULL : box_vec_at(&s->lights, id);
            const box_emitter_t *e = kind ? box_vec_at(&s->emitters, id) : NULL;
            const float *position = kind ? e->position : l->position;
            float range = kind ? e->range : l->range, dir[3], dist = 0;
            for (int a = 0; a < 3; a ++) {
                dir[a] = position[a] - origin[a];
                dist += dir[a] * dir[a];
            }

            dist = sqrtf(dist);
            if (dist >= range || range <= 0) continue;
            float window = 1 - powf(dist / range, 4), atten;
            for (int a = 0; a < 3; a ++) dir[a] /= fmaxf(dist, 1e-4f);
            if (kind) {
                float reach = fmaxf(dist, e->radius);
                atten = window * window * e->radius * e->radius / fmaxf(reach * reach, 1e-8f);
            } else {
                atten = window * window * l->intensity / (dist * dist + 1);
                if (l->type == BOX_LIGHT_SPOT) {
                    float cd = -dir[0] * l->direction[0] - dir[1] * l->direction[1] -
                        dir[2] * l->direction[2];
                    float cone = fminf(fmaxf((cd - l->cos_outer) /
                        fmaxf(l->cos_inner - l->cos_outer, 1e-4f), 0), 1);
                    atten *= cone * cone;
                }
            }

            uint32_t ignore = kind ? box_bake_emitter_occluder(s, id) : BOX_BAKE_NONE;
            if (!box_bake_visible_ignoring(s, origin, position, ignore)) continue;
            float value[3];
            for (int a = 0; a < 3; a ++) value[a] = (kind ? e->color[a] : l->color[a]) * atten;
            box_sh_add(out, dir, value);
        }
    }

done:
    return;
}

/** Append a dense grid of chunk indices over the receiving chunks to the packed data.
 *
 * Word 6 of the header points at the grid; it is left zero when the grid would exceed the
 * size limit.
 *
 * @param s The bake state
 */
static void box_bake_pack_directory(
    box_bake_state_t *s)
{
    if (!s->chunk_count) return;
    int32_t lo[3] = {INT32_MAX, INT32_MAX, INT32_MAX};
    int32_t hi[3] = {INT32_MIN, INT32_MIN, INT32_MIN};
    for (uint32_t i = 0; i < s->chunks.count; i ++) {
        const box_bake_chunk_t *c = box_vec_at(&s->chunks, i);
        if (!c->receivers) continue;
        for (int a = 0; a < 3; a ++) {
            if (c->coord[a] < lo[a]) lo[a] = c->coord[a];
            if (c->coord[a] > hi[a]) hi[a] = c->coord[a];
        }
    }

    uint64_t limit = (uint64_t)s->hash.count * 32;
    if (limit > 4u * 1024u * 1024u) limit = 4u * 1024u * 1024u;
    uint64_t volume = 1;
    uint32_t dim[3];
    for (int a = 0; a < 3; a ++) {
        uint64_t span = (uint64_t)((int64_t)hi[a] - lo[a] + 1);
        if (span > limit / volume) return;
        dim[a] = (uint32_t)span;
        volume *= span;
    }

    uint32_t base = s->data.count;
    uint32_t count = (uint32_t)volume;
    box_vec_reserve(&s->data, base + 6 + count);
    s->data.count = base + 6 + count;
    uint32_t *data = s->data.data;
    data[6] = base;
    memcpy(data + base, lo, sizeof(lo));
    memcpy(data + base + 3, dim, sizeof(dim));
    memset(data + base + 6, 0xff, (size_t)count * sizeof(uint32_t));
    for (uint32_t i = 0; i < s->chunks.count; i ++) {
        const box_bake_chunk_t *c = box_vec_at(&s->chunks, i);
        if (!c->receivers) continue;
        uint32_t x = (uint32_t)((int64_t)c->coord[0] - lo[0]);
        uint32_t y = (uint32_t)((int64_t)c->coord[1] - lo[1]);
        uint32_t z = (uint32_t)((int64_t)c->coord[2] - lo[2]);
        data[base + 6 + x + dim[0] * (y + dim[1] * z)] = i;
    }
}

void box_bake_pack(
    box_bake_state_t *s)
{
    uint32_t offset = 8 + s->hash.count * 4;
    uint32_t occluders = offset + s->chunks.count * 12;
    uint32_t emitter_map = occluders + s->occluders.count * 12;
    uint32_t neighbors = emitter_map + s->emitters.count;
    uint32_t cells = neighbors + s->chunks.count * 8;
    uint32_t count = cells + s->chunks.count * BOX_BAKE_CHUNK_PROBES * 2;
    for (uint32_t i = 0; i < s->chunks.count; i ++) {
        const box_bake_chunk_t *c = box_vec_at(&s->chunks, i);
        count += c->lights.count + c->emitters.count + c->occluders.count;
    }

    box_vec_reserve(&s->data, count);
    s->data.count = count;
    uint32_t *data = s->data.data;
    memset(data, 0, (size_t)count * sizeof(uint32_t));
    memcpy(data, &s->spacing, sizeof(float));
    data[1] = s->hash.count;
    data[2] = offset;
    data[3] = occluders;
    data[4] = emitter_map;
    data[5] = neighbors;
    for (uint32_t i = 0; i < s->emitters.count; i ++) {
        data[emitter_map + i] = box_bake_emitter_occluder(s, i);
    }

    for (uint32_t i = 0; i < s->hash.count; i ++) {
        uint32_t id = ((uint32_t*)s->hash.data)[i];
        if (!id) continue;
        const box_bake_chunk_t *c = box_vec_at(&s->chunks, id - 1u);
        memcpy(data + 8 + i * 4, c->coord, sizeof(c->coord));
        data[8 + i * 4 + 3] = id;
    }

    for (uint32_t i = 0; i < s->occluders.count; i ++) {
        const box_bake_occluder_t *o = box_vec_at(&s->occluders, i);
        memcpy(data + occluders + i * 12, o->inverse, sizeof(o->inverse));
    }

    uint32_t next = cells + s->chunks.count * BOX_BAKE_CHUNK_PROBES * 2;
    for (uint32_t i = 0; i < s->chunks.count; i ++) {
        const box_bake_chunk_t *c = box_vec_at(&s->chunks, i);
        uint32_t *d = data + offset + i * 12;
        memcpy(d, c->coord, sizeof(c->coord));
        d[3] = i * BOX_BAKE_CHUNK_PROBES;
        d[10] = c->receivers != 0;
        d[11] = cells + i * BOX_BAKE_CHUNK_PROBES * 2;
        for (uint32_t j = 0; j < 8; j ++) {
            int32_t coord[3];
            for (int a = 0; a < 3; a ++) coord[a] = c->coord[a] + ((j >> a) & 1u);
            data[neighbors + i * 8 + j] = box_bake_find_chunk(s, coord);
        }

        const box_vec_t *lists[3] = {&c->lights, &c->emitters, &c->occluders};
        for (int j = 0; j < 3; j ++) {
            d[4 + j * 2] = next;
            d[5 + j * 2] = lists[j]->count;
            if (lists[j]->count) {
                memcpy(data + next, lists[j]->data, (size_t)lists[j]->count * sizeof(uint32_t));
            }

            next += lists[j]->count;
        }
    }

    uint32_t *seen = calloc(s->occluders.count, sizeof(uint32_t));
    box_vec_t local[BOX_BAKE_CHUNK_PROBES];
    for (uint32_t i = 0; i < BOX_BAKE_CHUNK_PROBES; i ++) box_vec_init(&local[i], sizeof(uint32_t));
    for (uint32_t i = 0; i < s->chunks.count; i ++) {
        const box_bake_chunk_t *c = box_vec_at(&s->chunks, i);
        if (!c->receivers) continue;
        for (uint32_t j = 0; j < BOX_BAKE_CHUNK_PROBES; j ++) box_vec_clear(&local[j]);
        for (uint32_t neighbor = 0; neighbor < 8; neighbor ++) {
            int32_t coord[3];
            for (int a = 0; a < 3; a ++) coord[a] = c->coord[a] + ((neighbor >> a) & 1u);
            uint32_t id = box_bake_find_chunk(s, coord);
            if (id == BOX_BAKE_NONE) continue;
            const box_bake_chunk_t *other = box_vec_at(&s->chunks, id);
            for (uint32_t j = 0; j < other->occluders.count; j ++) {
                uint32_t occluder = ((uint32_t*)other->occluders.data)[j];
                if (seen[occluder] == i + 1) continue;
                seen[occluder] = i + 1;
                const box_bake_occluder_t *o = box_vec_at(&s->occluders, occluder);
                int32_t lo[3], hi[3];
                for (int a = 0; a < 3; a ++) {
                    lo[a] = (int32_t)ceilf(o->bounds_min[a] / s->spacing - 1.5f) - c->coord[a] * 4;
                    hi[a] = (int32_t)floorf(o->bounds_max[a] / s->spacing - 0.5f) - c->coord[a] * 4;
                    lo[a] = lo[a] < 0 ? 0 : lo[a];
                    hi[a] = hi[a] > 3 ? 3 : hi[a];
                }

                for (int32_t z = lo[2]; z <= hi[2]; z ++) {
                    for (int32_t y = lo[1]; y <= hi[1]; y ++) {
                        for (int32_t x = lo[0]; x <= hi[0]; x ++) {
                            *(uint32_t*)box_vec_push(&local[x + y * 4 + z * 16]) = occluder;
                        }
                    }
                }
            }
        }

        for (uint32_t j = 0; j < BOX_BAKE_CHUNK_PROBES; j ++) {
            uint32_t first = s->data.count;
            box_vec_reserve(&s->data, first + local[j].count);
            data = s->data.data;
            uint32_t p = cells + (i * BOX_BAKE_CHUNK_PROBES + j) * 2;
            data[p] = first;
            data[p + 1] = local[j].count;
            if (local[j].count) {
                memcpy(data + first, local[j].data, local[j].count * sizeof(uint32_t));
            }

            s->data.count += local[j].count;
        }
    }

    for (uint32_t i = 0; i < BOX_BAKE_CHUNK_PROBES; i ++) box_vec_fini(&local[i]);
    free(seen);
    box_bake_pack_directory(s);
}

/** Create a GPU buffer.
 *
 * @param r The renderer
 * @param usage Buffer usage flags
 * @param size Size in bytes
 * @return The buffer
 */
static WGPUBuffer box_bake_buffer(
    box_renderer_t *r,
    WGPUBufferUsage usage,
    uint64_t size)
{
    WGPUBufferDescriptor desc = {.size = size, .usage = usage};

    return wgpuDeviceCreateBuffer(r->device, &desc);
}

/** Grow a storage buffer to hold at least a number of elements, doubling its capacity.
 *
 * @param r The renderer
 * @param buffer The buffer, replaced when it grows
 * @param capacity Element capacity of the buffer, updated when it grows
 * @param count Elements the buffer must hold
 * @param stride Size of an element in bytes
 * @param preserve True to copy the old contents into the new buffer
 * @return True if a new buffer was created
 */
static bool box_bake_buffer_grow(
    box_renderer_t *r,
    WGPUBuffer *buffer,
    uint32_t *capacity,
    uint32_t count,
    uint32_t stride,
    bool preserve)
{
    if (*buffer && count <= *capacity) return false;
    uint32_t grown = *capacity ? *capacity * 2u : 64u;
    if (grown < count) grown = count;
    WGPUBuffer next = box_bake_buffer(r,
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc,
        (uint64_t)grown * stride);
    if (*buffer) {
        if (preserve) {
            WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(r->device, NULL);
            wgpuCommandEncoderCopyBufferToBuffer(
                encoder, *buffer, 0, next, 0, (uint64_t)*capacity * stride);
            WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, NULL);
            wgpuQueueSubmit(r->gpu_queue, 1, &cmd);
            wgpuCommandBufferRelease(cmd);
            wgpuCommandEncoderRelease(encoder);
        }

        wgpuBufferRelease(*buffer);
    }

    *buffer = next;
    *capacity = grown;

    return true;
}

/** Create a bind group layout.
 *
 * @param r The renderer
 * @param entries Layout entries
 * @param count Number of entries
 * @return The layout
 */
static WGPUBindGroupLayout box_bake_layout(
    box_renderer_t *r,
    WGPUBindGroupLayoutEntry *entries,
    uint32_t count)
{
    WGPUBindGroupLayoutDescriptor d = {.entryCount = count, .entries = entries};

    return wgpuDeviceCreateBindGroupLayout(r->device, &d);
}

/** Recreate the bake, bake work and probe bind groups for the current buffers.
 *
 * @param r The renderer
 */
static void box_bake_groups(
    box_renderer_t *r)
{
    if (r->bake_group) wgpuBindGroupRelease(r->bake_group);
    if (r->bake_work_group) wgpuBindGroupRelease(r->bake_work_group);
    if (r->probe_group) wgpuBindGroupRelease(r->probe_group);
    uint64_t light_size = (uint64_t)r->bake_light_capacity * sizeof(box_light_t);
    uint64_t emitter_size = (uint64_t)r->bake_emitter_capacity * sizeof(box_emitter_t);
    uint64_t probe_size = (uint64_t)r->bake_probe_capacity * sizeof(box_probe_t);
    WGPUBindGroupEntry globals[4] = {
        {.binding = 0, .buffer = r->bake_uniform, .size = sizeof(box_bake_uniform_t)},
        {.binding = 1, .buffer = r->bake_lights, .size = light_size},
        {.binding = 2, .buffer = r->bake_emitters, .size = emitter_size},
        {.binding = 3, .buffer = r->bake_data, .size = (uint64_t)r->bake_data_capacity * 4}
    };
    WGPUBindGroupDescriptor desc = {
        .layout = r->bake_layout, .entryCount = 4, .entries = globals};
    r->bake_group = wgpuDeviceCreateBindGroup(r->device, &desc);
    WGPUBindGroupEntry work[2] = {
        {.binding = 0, .buffer = r->bake_probes, .size = probe_size},
        {.binding = 1, .buffer = r->bake_jobs, .size = (uint64_t)r->bake_job_capacity * 4}
    };
    desc.layout = r->bake_mesh_layout;
    desc.entryCount = 2;
    desc.entries = work;
    r->bake_work_group = wgpuDeviceCreateBindGroup(r->device, &desc);
    work[1].buffer = r->bake_data;
    work[1].size = (uint64_t)r->bake_data_capacity * 4;
    desc.layout = r->probe_layout;
    r->probe_group = wgpuDeviceCreateBindGroup(r->device, &desc);
}

void box_bake_init(
    box_renderer_t *r)
{
    box_bake_state_init(&r->bake);
    WGPUBindGroupLayoutEntry entries[4] = {0};
    for (uint32_t i = 0; i < 4; i ++) {
        entries[i].binding = i;
        entries[i].visibility = WGPUShaderStage_Compute;
        entries[i].buffer.type = i ?
            WGPUBufferBindingType_ReadOnlyStorage : WGPUBufferBindingType_Uniform;
    }

    entries[0].buffer.minBindingSize = sizeof(box_bake_uniform_t);
    r->bake_layout = box_bake_layout(r, entries, 4);
    entries[0].buffer.type = WGPUBufferBindingType_Storage;
    entries[0].buffer.minBindingSize = 0;
    r->bake_mesh_layout = box_bake_layout(r, entries, 2);
    entries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    entries[0].visibility = entries[1].visibility = WGPUShaderStage_Fragment;
    r->probe_layout = box_bake_layout(r, entries, 2);
    r->bake_uniform = box_bake_buffer(r,
        WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, sizeof(box_bake_uniform_t));
    box_bake_buffer_grow(r, &r->bake_lights, &r->bake_light_capacity,
        1, sizeof(box_light_t), false);
    box_bake_buffer_grow(r, &r->bake_emitters, &r->bake_emitter_capacity,
        1, sizeof(box_emitter_t), false);
    box_bake_buffer_grow(r, &r->bake_data, &r->bake_data_capacity, 8, 4, false);
    box_bake_buffer_grow(r, &r->bake_probes, &r->bake_probe_capacity,
        1, sizeof(box_probe_t), false);
    box_bake_buffer_grow(r, &r->bake_jobs, &r->bake_job_capacity, 1, 4, false);
    box_bake_groups(r);
    box_bake_reload(r);
}

void box_bake_reload(
    box_renderer_t *r)
{
    if (r->bake_pipeline) wgpuComputePipelineRelease(r->bake_pipeline);
    WGPUShaderModule shader = box_shader_module(r, BOX_SHADER_BAKE);
    WGPUBindGroupLayout layouts[2] = {r->bake_layout, r->bake_mesh_layout};
    WGPUPipelineLayoutDescriptor ld = {.bindGroupLayoutCount = 2, .bindGroupLayouts = layouts};
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(r->device, &ld);
    WGPUComputePipelineDescriptor pd = {
        .label = BOX_GPU_LABEL("bake"),
        .layout = layout,
        .compute = {.module = shader, .entryPoint = {.data = "cs_bake", .length = WGPU_STRLEN}}
    };
    r->bake_pipeline = wgpuDeviceCreateComputePipeline(r->device, &pd);
    wgpuPipelineLayoutRelease(layout);
    wgpuShaderModuleRelease(shader);
    for (uint32_t i = 0; i < r->bake.chunks.count; i ++) {
        box_bake_mark(box_vec_at(&r->bake.chunks, i));
    }
}

void box_bake_update(
    box_renderer_t *r,
    const box_packet_t *p,
    WGPUCommandEncoder encoder)
{
    box_bake_state_t *s = &r->bake;
    box_bake_state_apply(s, p);
    if (!s->enabled) return;
    uint32_t count = box_bake_state_take(s, p->bake.budget);
    if (s->data_dirty) box_bake_pack(s);
    bool changed = false;
    changed |= box_bake_buffer_grow(r, &r->bake_lights, &r->bake_light_capacity,
        s->lights.count, sizeof(box_light_t), false);
    changed |= box_bake_buffer_grow(r, &r->bake_emitters, &r->bake_emitter_capacity,
        s->emitters.count, sizeof(box_emitter_t), false);
    changed |= box_bake_buffer_grow(r, &r->bake_data, &r->bake_data_capacity,
        s->data.count, 4, false);
    changed |= box_bake_buffer_grow(r, &r->bake_probes, &r->bake_probe_capacity,
        s->chunks.count * BOX_BAKE_CHUNK_PROBES, sizeof(box_probe_t), true);
    changed |= box_bake_buffer_grow(r, &r->bake_jobs, &r->bake_job_capacity, count, 4, false);
    if (changed) box_bake_groups(r);
    if (s->data_dirty) {
        wgpuQueueWriteBuffer(r->gpu_queue, r->bake_data, 0, s->data.data,
            (size_t)s->data.count * 4);
        s->data_dirty = false;
    }

    box_probe_t zero[BOX_BAKE_CHUNK_PROBES] = {0};
    for (uint32_t i = 0; i < s->chunks.count; i ++) {
        box_bake_chunk_t *c = box_vec_at(&s->chunks, i);
        if (!c->fresh) continue;
        wgpuQueueWriteBuffer(r->gpu_queue, r->bake_probes, (uint64_t)i * sizeof(zero),
            zero, sizeof(zero));
        c->fresh = false;
    }

    if (!count) return;
    box_bake_uniform_t u = {.job_count = count};
    wgpuQueueWriteBuffer(r->gpu_queue, r->bake_uniform, 0, &u, sizeof(u));
    if (s->lights.count) {
        wgpuQueueWriteBuffer(r->gpu_queue, r->bake_lights, 0, s->lights.data,
            (size_t)s->lights.count * sizeof(box_light_t));
    }

    if (s->emitters.count) {
        wgpuQueueWriteBuffer(r->gpu_queue, r->bake_emitters, 0, s->emitters.data,
            (size_t)s->emitters.count * sizeof(box_emitter_t));
    }

    wgpuQueueWriteBuffer(r->gpu_queue, r->bake_jobs, 0, s->jobs.data, (size_t)count * 4);
    WGPUComputePassEncoder pass = box_timing_compute(r, encoder, "bake");
    wgpuComputePassEncoderSetPipeline(pass, r->bake_pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, r->bake_group, 0, NULL);
    wgpuComputePassEncoderSetBindGroup(pass, 1, r->bake_work_group, 0, NULL);
    uint32_t groups = (count + BOX_BAKE_GROUP - 1u) / BOX_BAKE_GROUP;
    uint32_t x = groups < 65535u ? groups : 65535u;
    wgpuComputePassEncoderDispatchWorkgroups(pass, x, (groups + x - 1u) / x, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
}

WGPUBindGroup box_bake_group(
    box_renderer_t *r,
    uint32_t mesh,
    bool baked)
{
    (void)mesh;
    (void)baked;

    return r->probe_group;
}

void box_bake_fini(
    box_renderer_t *r)
{
    box_bake_state_fini(&r->bake);
    if (r->bake_group) wgpuBindGroupRelease(r->bake_group);
    if (r->bake_work_group) wgpuBindGroupRelease(r->bake_work_group);
    if (r->probe_group) wgpuBindGroupRelease(r->probe_group);
    if (r->bake_layout) wgpuBindGroupLayoutRelease(r->bake_layout);
    if (r->bake_mesh_layout) wgpuBindGroupLayoutRelease(r->bake_mesh_layout);
    if (r->probe_layout) wgpuBindGroupLayoutRelease(r->probe_layout);
    if (r->bake_pipeline) wgpuComputePipelineRelease(r->bake_pipeline);
    if (r->bake_uniform) wgpuBufferRelease(r->bake_uniform);
    if (r->bake_lights) wgpuBufferRelease(r->bake_lights);
    if (r->bake_emitters) wgpuBufferRelease(r->bake_emitters);
    if (r->bake_data) wgpuBufferRelease(r->bake_data);
    if (r->bake_probes) wgpuBufferRelease(r->bake_probes);
    if (r->bake_jobs) wgpuBufferRelease(r->bake_jobs);
    r->bake_group = NULL;
    r->bake_work_group = NULL;
    r->probe_group = NULL;
    r->bake_layout = NULL;
    r->bake_mesh_layout = NULL;
    r->probe_layout = NULL;
    r->bake_pipeline = NULL;
    r->bake_uniform = NULL;
    r->bake_lights = NULL;
    r->bake_emitters = NULL;
    r->bake_data = NULL;
    r->bake_probes = NULL;
    r->bake_jobs = NULL;
    r->bake_light_capacity = 0;
    r->bake_emitter_capacity = 0;
    r->bake_data_capacity = 0;
    r->bake_probe_capacity = 0;
    r->bake_job_capacity = 0;
}
