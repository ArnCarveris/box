/**
 * @file render/particle_rows.c
 * @brief The curve table of the particle extract: entries of rows baked from the curves of the
 * emitters, counted by the pools that use them and handed out again when none does.
 *
 * A scalar curve takes a row of values, a spin curve a row of values and a row of their
 * accumulated area, and a gradient BOX_PARTICLE_GRADIENT_ROWS rows of linear colors. The rows
 * are baked by the C twin of the shaders in src/particle/simulate.h.
 */

#include "particle_rows.h"
#include "../particle/simulate.h"

/** What the rows of an entry hold */
typedef enum box_particle_rows_kind_t {
    BOX_PARTICLE_ROWS_SCALAR,   /**< A row of values */
    BOX_PARTICLE_ROWS_SPIN,     /**< A row of values and a row of their accumulated area */
    BOX_PARTICLE_ROWS_GRADIENT  /**< BOX_PARTICLE_GRADIENT_ROWS rows of linear colors */
} box_particle_rows_kind_t;

/** Rows baked from one curve, shared by the pools with that curve */
typedef struct box_particle_rows_entry_t {
    box_particle_rows_kind_t kind; /**< What the rows hold */
    float keys[16];         /**< The four keys of a curve, or the four linear colors of a
                             *   gradient with w 1, the rest zero */
    uint32_t row;           /**< First row */
    uint32_t rows;          /**< Rows it takes */
    int32_t refs;           /**< Pools that use it, 0 for rows that are handed out again */
} box_particle_rows_entry_t;

void box_particle_rows_init(
    box_particle_rows_t *t)
{
    ecs_os_memset_t(t, 0, box_particle_rows_t);
    ecs_vec_init_t(NULL, &t->entries, box_particle_rows_entry_t, 0);
    ecs_vec_init_t(NULL, &t->data, float, 0);
    t->dirty_first = UINT32_MAX;
}

void box_particle_rows_fini(
    box_particle_rows_t *t)
{
    ecs_vec_fini_t(NULL, &t->entries, box_particle_rows_entry_t);
    ecs_vec_fini_t(NULL, &t->data, float);
}

void box_particle_rows_reset(
    box_particle_rows_t *t)
{
    ecs_vec_clear(&t->entries);
    ecs_vec_clear(&t->data);
    t->capacity = 0;
    t->dirty_first = UINT32_MAX;
    t->dirty_end = 0;
}

/** Rows the entries of a kind take.
 *
 * @param kind The kind
 * @return The rows
 */
static uint32_t box_particle_rows_of(
    box_particle_rows_kind_t kind)
{
    uint32_t result = 1;
    if (kind == BOX_PARTICLE_ROWS_SPIN) {
        result = BOX_PARTICLE_SPIN_ROWS;
    } else if (kind == BOX_PARTICLE_ROWS_GRADIENT) {
        result = BOX_PARTICLE_GRADIENT_ROWS;
    }

    return result;
}

/** Bake the rows of an entry and mark them for the send.
 *
 * @param t The table
 * @param entry The entry, its rows handed out
 */
static void box_particle_rows_bake(
    box_particle_rows_t *t,
    const box_particle_rows_entry_t *entry)
{
    float *out = ecs_vec_get_t(&t->data, float,
        (int32_t)(entry->row * BOX_PARTICLE_CURVE_SAMPLES));
    if (entry->kind == BOX_PARTICLE_ROWS_GRADIENT) {
        box_particle_gradient_bake(entry->keys, out);
    } else {
        box_particle_curve_bake(entry->keys, out);
        if (entry->kind == BOX_PARTICLE_ROWS_SPIN) {
            box_particle_curve_bake_area(out, out + BOX_PARTICLE_CURVE_SAMPLES);
        }
    }

    if (entry->row < t->dirty_first) {
        t->dirty_first = entry->row;
    }

    if (entry->row + entry->rows > t->dirty_end) {
        t->dirty_end = entry->row + entry->rows;
    }

    t->bakes ++;
}

/** Get the rows of a curve: those of an entry with the same content, else rows nothing uses
 * or new rows, baked. The old rows of a pool are released after its new ones are acquired, so
 * its unchanged curves keep theirs.
 *
 * @param t The table
 * @param kind What the rows hold
 * @param keys The content, 16 floats
 * @return Index of the entry, its use counted
 */
static int32_t box_particle_rows_acquire(
    box_particle_rows_t *t,
    box_particle_rows_kind_t kind,
    const float *keys)
{
    uint32_t rows = box_particle_rows_of(kind);
    int32_t i, count = ecs_vec_count(&t->entries), result = -1, unused = -1;
    box_particle_rows_entry_t *entry;
    for (i = 0; i < count; i ++) {
        entry = ecs_vec_get_t(&t->entries, box_particle_rows_entry_t, i);
        if (entry->kind == kind && !memcmp(entry->keys, keys, sizeof(entry->keys))) {
            result = i;
            break;
        }

        if (unused < 0 && !entry->refs && entry->rows == rows) {
            unused = i;
        }
    }

    if (result >= 0) {
        goto done;
    }

    if (unused >= 0) {
        result = unused;
        entry = ecs_vec_get_t(&t->entries, box_particle_rows_entry_t, result);
    } else {
        result = count;
        entry = ecs_vec_append_t(NULL, &t->entries, box_particle_rows_entry_t);
        entry->row = (uint32_t)ecs_vec_count(&t->data) / BOX_PARTICLE_CURVE_SAMPLES;
        entry->rows = rows;
        ecs_vec_grow_t(NULL, &t->data, float, (int32_t)(rows * BOX_PARTICLE_CURVE_SAMPLES));
    }

    entry->kind = kind;
    ecs_os_memcpy(entry->keys, keys, ECS_SIZEOF(entry->keys));
    entry->refs = 0;
    box_particle_rows_bake(t, entry);
done:
    ecs_vec_get_t(&t->entries, box_particle_rows_entry_t, result)->refs ++;

    return result;
}

/** Stop using entries.
 *
 * @param t The table
 * @param entries Index plus 1 of the entries, 0 for none, zeroed
 */
static void box_particle_rows_unref(
    box_particle_rows_t *t,
    int32_t *entries)
{
    int32_t i;
    for (i = 0; i < BOX_PARTICLE_ROWS_CURVES; i ++) {
        if (entries[i] > 0 && entries[i] <= ecs_vec_count(&t->entries)) {
            ecs_vec_get_t(&t->entries, box_particle_rows_entry_t, entries[i] - 1)->refs --;
        }

        entries[i] = 0;
    }
}

void box_particle_rows_release(
    box_particle_rows_t *t,
    box_particle_rows_use_t *use)
{
    box_particle_rows_unref(t, use->entries);
    use->baked = false;
}

/** Check whether the four keys of a curve are equal.
 *
 * @param keys The keys
 * @return Whether the curve is constant
 */
static bool box_particle_rows_constant(
    const float *keys)
{
    return keys[0] == keys[1] && keys[1] == keys[2] && keys[2] == keys[3];
}

/** Give a use the rows of the scalar curves of ParticleCurves.
 *
 * @param t The table
 * @param use The use, its constants set to the defaults
 * @param curves The curves
 */
static void box_particle_rows_scalars(
    box_particle_rows_t *t,
    box_particle_rows_use_t *use,
    const ParticleCurves *curves)
{
    const ParticleCurve *scalars[4] = {
        &curves->size, &curves->opacity, &curves->rotation, &curves->spin
    };
    float keys[16];
    int32_t i;
    for (i = 0; i < 4; i ++) {
        const float *k = &scalars[i]->start;
        if (box_particle_rows_constant(k)) {
            use->values[i] = k[0];
            continue;
        }

        ecs_os_memset(keys, 0, ECS_SIZEOF(keys));
        ecs_os_memcpy(keys, k, 4 * ECS_SIZEOF(float));
        int32_t entry = box_particle_rows_acquire(t,
            i == 3 ? BOX_PARTICLE_ROWS_SPIN : BOX_PARTICLE_ROWS_SCALAR, keys);
        use->entries[i] = entry + 1;
        use->rows[i] = ecs_vec_get_t(&t->entries, box_particle_rows_entry_t, entry)->row;
    }
}

/** Give a use the rows of the gradient of ParticleCurves, none for a constant one.
 *
 * @param t The table
 * @param use The use
 * @param curves The curves
 */
static void box_particle_rows_gradient(
    box_particle_rows_t *t,
    box_particle_rows_use_t *use,
    const ParticleCurves *curves)
{
    const rgb *colors = &curves->color.start;
    float keys[16];
    bool constant = true;
    int32_t i, c;
    for (i = 0; i < 4; i ++) {
        keys[i * 4 + 0] = box_particle_linear(colors[i].r);
        keys[i * 4 + 1] = box_particle_linear(colors[i].g);
        keys[i * 4 + 2] = box_particle_linear(colors[i].b);
        keys[i * 4 + 3] = 1.0f;
    }

    for (c = 0; c < 3; c ++) {
        float channel[4] = {keys[c], keys[4 + c], keys[8 + c], keys[12 + c]};
        constant = constant && box_particle_rows_constant(channel);
        use->gradient[c] = keys[c];
    }

    if (!constant) {
        int32_t entry = box_particle_rows_acquire(t, BOX_PARTICLE_ROWS_GRADIENT, keys);
        use->entries[4] = entry + 1;
        use->gradient_row = ecs_vec_get_t(&t->entries, box_particle_rows_entry_t, entry)->row;
    }
}

void box_particle_rows_update(
    box_particle_rows_t *t,
    box_particle_rows_use_t *use,
    const ParticleCurves *curves)
{
    static const float defaults[4] = {1.0f, 1.0f, 0.0f, 1.0f};
    int32_t i, old[BOX_PARTICLE_ROWS_CURVES];
    if (use->baked && use->had == (curves != NULL) &&
        (!curves || !memcmp(curves, &use->key, sizeof(ParticleCurves))))
    {
        goto done;
    }

    ecs_os_memcpy(old, use->entries, ECS_SIZEOF(old));
    ecs_os_memset(use->entries, 0, ECS_SIZEOF(use->entries));
    for (i = 0; i < 4; i ++) {
        use->rows[i] = BOX_PARTICLE_CURVE_CONSTANT;
        use->values[i] = defaults[i];
    }

    use->gradient_row = BOX_PARTICLE_CURVE_CONSTANT;
    use->gradient[0] = use->gradient[1] = use->gradient[2] = 1.0f;
    if (curves) {
        box_particle_rows_scalars(t, use, curves);
        box_particle_rows_gradient(t, use, curves);
        use->key = *curves;
    }

    box_particle_rows_unref(t, old);
    use->had = curves != NULL;
    use->baked = true;
done:
    return;
}

bool box_particle_rows_used(
    const box_particle_rows_use_t *use)
{
    bool result = false;
    int32_t i;
    for (i = 0; i < BOX_PARTICLE_ROWS_CURVES; i ++) {
        result = result || use->entries[i] != 0;
    }

    return result;
}

void box_particle_rows_send(
    box_particle_rows_t *t,
    box_render_packet_t *p)
{
    uint32_t rows = (uint32_t)ecs_vec_count(&t->data) / BOX_PARTICLE_CURVE_SAMPLES;
    uint32_t row_bytes = BOX_PARTICLE_CURVE_SAMPLES * (uint32_t)sizeof(float);
    uint32_t first = t->dirty_first, end = t->dirty_end;
    if (rows > t->capacity) {
        uint32_t capacity = t->capacity ? t->capacity : BOX_RENDER_PARTICLE_CURVE_ROWS;
        while (capacity < rows) {
            capacity *= 2;
        }

        box_render_buffer_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_BUFFER,
            BOX_RENDER_CREATE, BOX_RENDER_BUFFER_PARTICLE_CURVES,
            sizeof(box_render_buffer_desc_t));
        desc->size = (uint64_t)capacity * row_bytes;
        desc->usage = BOX_RENDER_BUFFER_STORAGE;
        t->capacity = capacity;
        first = 0;
        end = rows;
    }

    if (first < end) {
        ecs_os_memcpy(box_render_packet_upload(p, BOX_RENDER_BUFFER_PARTICLE_CURVES,
            first * row_bytes, (end - first) * row_bytes),
            ecs_vec_get_t(&t->data, float, (int32_t)(first * BOX_PARTICLE_CURVE_SAMPLES)),
            (ecs_size_t)((end - first) * row_bytes));
    }

    t->dirty_first = UINT32_MAX;
    t->dirty_end = 0;
}

void box_particle_rows_stats(
    const box_particle_rows_t *t,
    box_render_particle_curve_stats_t *out)
{
    int32_t i;
    ecs_os_memset_t(out, 0, box_render_particle_curve_stats_t);
    for (i = 0; i < ecs_vec_count(&t->entries); i ++) {
        const box_particle_rows_entry_t *entry = ecs_vec_get_t(
            &t->entries, box_particle_rows_entry_t, i);
        if (entry->refs > 0) {
            out->used += (int32_t)entry->rows;
            out->entries ++;
        }
    }

    out->rows = ecs_vec_count(&t->data) / BOX_PARTICLE_CURVE_SAMPLES;
    out->capacity = (int32_t)t->capacity;
    out->bakes = t->bakes;
}

const float* box_particle_rows_data(
    const box_particle_rows_t *t,
    int32_t *rows)
{
    int32_t count = ecs_vec_count(&t->data) / BOX_PARTICLE_CURVE_SAMPLES;
    if (rows) {
        *rows = count;
    }

    return count ? ecs_vec_first_t(&t->data, float) : NULL;
}
