/**
 * @file render/particle_rows.c
 * @brief The curve table of the particle extract: entries of rows baked from the curves of the
 * emitters, counted by the pools that use them and handed out again when none does.
 *
 * A scalar curve takes a row of values, a spin curve a row of values and a row of their
 * accumulated area, and a gradient BOX_PARTICLE_GRADIENT_ROWS rows of linear colors, baked by
 * the C twin of the shaders in src/particle/simulate.h. Entries are shared by baked content.
 *
 * A curve with max_keys takes an entry for each of its two curves. The curves of ParticleMotion
 * are two vectors, each BOX_PARTICLE_VECTOR_ROWS rows of four floats per sample.
 */

#include "particle_rows.h"
#include "../particle/simulate.h"
#include "../particle/curves.h"

/** Floats of the largest content of an entry, the rows of a gradient or a vector */
#define BOX_PARTICLE_ROWS_MAX_FLOATS (BOX_PARTICLE_GRADIENT_ROWS * BOX_PARTICLE_CURVE_SAMPLES)

_Static_assert(BOX_PARTICLE_VECTOR_ROWS <= BOX_PARTICLE_GRADIENT_ROWS,
    "the rows of a vector must fit in the largest content of an entry");

/** What the rows of an entry hold */
typedef enum box_particle_rows_kind_t {
    BOX_PARTICLE_ROWS_SCALAR,   /**< A row of values */
    BOX_PARTICLE_ROWS_SPIN,     /**< A row of values and a row of their accumulated area */
    BOX_PARTICLE_ROWS_GRADIENT, /**< BOX_PARTICLE_GRADIENT_ROWS rows of linear colors */
    BOX_PARTICLE_ROWS_VECTOR    /**< BOX_PARTICLE_VECTOR_ROWS rows of four curves of
                                 *   ParticleMotion */
} box_particle_rows_kind_t;

/** Rows baked from one curve, shared by the pools with that curve */
typedef struct box_particle_rows_entry_t {
    box_particle_rows_kind_t kind; /**< What the rows hold */
    uint32_t row;           /**< First row */
    uint32_t rows;          /**< Rows it takes */
    int32_t refs;           /**< Pools that use it, 0 for rows that are handed out again */
    bool listed;            /**< It is in the unused list of its rows */
    uint32_t generation;    /**< Times its rows were written, so a remembered use can tell
                             *   that they still hold what it baked */
    uint64_t hash;          /**< Hash of its kind and content, its key in by_hash */
} box_particle_rows_entry_t;

/** The rows a ParticleCurves took when it was sampled, remembered by the hash of its keys */
typedef struct box_particle_rows_known_t {
    box_particle_rows_use_t use; /**< The use as it was baked, its entries not counted */
    uint32_t generations[BOX_PARTICLE_ROWS_ENTRIES]; /**< Generations of its entries */
} box_particle_rows_known_t;

/** The rows a ParticleMotion took when it was sampled, remembered by the hash of its keys */
typedef struct box_particle_rows_known_motion_t {
    box_particle_rows_motion_t use; /**< The use as it was baked, its entries not counted */
    uint32_t generations[4]; /**< Generations of its entries */
} box_particle_rows_known_motion_t;

void box_particle_rows_init(
    box_particle_rows_t *t)
{
    int32_t i;
    ecs_os_memset_t(t, 0, box_particle_rows_t);
    ecs_vec_init_t(NULL, &t->entries, box_particle_rows_entry_t, 0);
    ecs_map_init(&t->by_hash, NULL);
    for (i = 0; i <= BOX_PARTICLE_GRADIENT_ROWS; i ++) {
        ecs_vec_init_t(NULL, &t->unused[i], int32_t, 0);
    }

    ecs_vec_init_t(NULL, &t->data, float, 0);
    ecs_vec_init_t(NULL, &t->known, box_particle_rows_known_t, 0);
    ecs_vec_init_t(NULL, &t->known_motion, box_particle_rows_known_motion_t, 0);
    ecs_map_init(&t->known_by_hash, NULL);
    ecs_map_init(&t->known_motion_by_hash, NULL);
    t->dirty_first = UINT32_MAX;
}

void box_particle_rows_fini(
    box_particle_rows_t *t)
{
    int32_t i;
    ecs_vec_fini_t(NULL, &t->entries, box_particle_rows_entry_t);
    ecs_map_fini(&t->by_hash);
    for (i = 0; i <= BOX_PARTICLE_GRADIENT_ROWS; i ++) {
        ecs_vec_fini_t(NULL, &t->unused[i], int32_t);
    }

    ecs_vec_fini_t(NULL, &t->data, float);
    ecs_vec_fini_t(NULL, &t->known, box_particle_rows_known_t);
    ecs_vec_fini_t(NULL, &t->known_motion, box_particle_rows_known_motion_t);
    ecs_map_fini(&t->known_by_hash);
    ecs_map_fini(&t->known_motion_by_hash);
}

/** Forget the rows of the curves sampled before, of both kinds.
 *
 * @param t The table
 */
static void box_particle_rows_forget(
    box_particle_rows_t *t)
{
    ecs_vec_clear(&t->known);
    ecs_vec_clear(&t->known_motion);
    ecs_map_clear(&t->known_by_hash);
    ecs_map_clear(&t->known_motion_by_hash);
}

void box_particle_rows_reset(
    box_particle_rows_t *t)
{
    int32_t i;
    ecs_vec_clear(&t->entries);
    ecs_map_clear(&t->by_hash);
    for (i = 0; i <= BOX_PARTICLE_GRADIENT_ROWS; i ++) {
        ecs_vec_clear(&t->unused[i]);
    }

    ecs_vec_clear(&t->data);
    box_particle_rows_forget(t);
    t->last = NULL;
    t->last_motion = NULL;
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
    } else if (kind == BOX_PARTICLE_ROWS_VECTOR) {
        result = BOX_PARTICLE_VECTOR_ROWS;
    }

    return result;
}

/** Mix a word into a hash.
 *
 * @param hash The hash so far
 * @param word The word
 * @return The hash
 */
static uint64_t box_particle_rows_mix(
    uint64_t hash,
    uint64_t word)
{
    hash = (hash ^ word) * 0x9E3779B97F4A7C15ull;

    return hash ^ (hash >> 29);
}

/** Add bytes to a hash, eight at a time: the curves of every emitter are hashed every frame.
 *
 * @param hash The hash so far
 * @param data The bytes
 * @param size Number of bytes
 * @return The hash
 */
static uint64_t box_particle_rows_hash_bytes(
    uint64_t hash,
    const void *data,
    size_t size)
{
    const uint8_t *bytes = data;
    uint64_t word;
    size_t i;
    for (i = 0; i + sizeof(word) <= size; i += sizeof(word)) {
        ecs_os_memcpy(&word, bytes + i, ECS_SIZEOF(word));
        hash = box_particle_rows_mix(hash, word);
    }

    if (i < size) {
        word = 0;
        ecs_os_memcpy(&word, bytes + i, (ecs_size_t)(size - i));
        hash = box_particle_rows_mix(hash, word);
    }

    return hash;
}

/** Write the content of an entry to its rows and mark them for the send.
 *
 * @param t The table
 * @param entry The entry, its rows handed out
 * @param samples The content, BOX_PARTICLE_CURVE_SAMPLES floats per row of the entry
 */
static void box_particle_rows_write(
    box_particle_rows_t *t,
    box_particle_rows_entry_t *entry,
    const float *samples)
{
    float *out = ecs_vec_get_t(&t->data, float,
        (int32_t)(entry->row * BOX_PARTICLE_CURVE_SAMPLES));
    ecs_os_memcpy(out, samples,
        (ecs_size_t)(entry->rows * BOX_PARTICLE_CURVE_SAMPLES * sizeof(float)));
    entry->generation ++;
    if (entry->row < t->dirty_first) {
        t->dirty_first = entry->row;
    }

    if (entry->row + entry->rows > t->dirty_end) {
        t->dirty_end = entry->row + entry->rows;
    }

    t->bakes ++;
}

/** Take an entry that no pool uses with a number of rows, from the unused list of its rows.
 *
 * @param t The table
 * @param rows Rows of the entry
 * @return Index of the entry, -1 for none
 */
static int32_t box_particle_rows_unused(
    box_particle_rows_t *t,
    uint32_t rows)
{
    ecs_vec_t *list = &t->unused[rows];
    int32_t result = -1;
    while (result < 0 && ecs_vec_count(list)) {
        int32_t i = *ecs_vec_last_t(list, int32_t);
        box_particle_rows_entry_t *entry = ecs_vec_get_t(
            &t->entries, box_particle_rows_entry_t, i);
        ecs_vec_remove_last(list);
        entry->listed = false;
        if (!entry->refs) {
            result = i;
        }
    }

    return result;
}

/** Get the rows of a baked curve: those of an entry of the same kind and content, found by
 * their hash, else the rows of an entry nothing uses or new rows, written. The old rows of a
 * pool are released after its new ones are acquired, so its unchanged curves keep theirs.
 *
 * @param t The table
 * @param kind What the rows hold
 * @param samples The content, BOX_PARTICLE_CURVE_SAMPLES floats per row of the kind
 * @return Index of the entry, its use counted
 */
static int32_t box_particle_rows_acquire(
    box_particle_rows_t *t,
    box_particle_rows_kind_t kind,
    const float *samples)
{
    uint32_t rows = box_particle_rows_of(kind);
    size_t bytes = rows * BOX_PARTICLE_CURVE_SAMPLES * sizeof(float);
    uint64_t hash = box_particle_rows_mix(
        box_particle_rows_hash_bytes(14695981039346656037ull, samples, bytes), (uint64_t)kind);
    ecs_map_val_t *found = ecs_map_get(&t->by_hash, hash);
    box_particle_rows_entry_t *entry;
    int32_t result = -1;
    if (found) {
        entry = ecs_vec_get_t(&t->entries, box_particle_rows_entry_t, (int32_t)*found);
        if (entry->kind == kind && !memcmp(ecs_vec_get_t(&t->data, float,
            (int32_t)(entry->row * BOX_PARTICLE_CURVE_SAMPLES)), samples, bytes))
        {
            result = (int32_t)*found;
            goto done;
        }
    }

    result = box_particle_rows_unused(t, rows);
    if (result >= 0) {
        entry = ecs_vec_get_t(&t->entries, box_particle_rows_entry_t, result);
        found = ecs_map_get(&t->by_hash, entry->hash);
        if (found && *found == (ecs_map_val_t)result) {
            ecs_map_remove(&t->by_hash, entry->hash);
        }
    } else {
        result = ecs_vec_count(&t->entries);
        entry = ecs_vec_append_t(NULL, &t->entries, box_particle_rows_entry_t);
        ecs_os_memset_t(entry, 0, box_particle_rows_entry_t);
        entry->row = (uint32_t)ecs_vec_count(&t->data) / BOX_PARTICLE_CURVE_SAMPLES;
        entry->rows = rows;
        ecs_vec_grow_t(NULL, &t->data, float, (int32_t)(rows * BOX_PARTICLE_CURVE_SAMPLES));
    }

    entry->kind = kind;
    entry->refs = 0;
    entry->hash = hash;
    ecs_map_ensure(&t->by_hash, hash)[0] = (ecs_map_val_t)result;
    box_particle_rows_write(t, entry, samples);
done:
    ecs_vec_get_t(&t->entries, box_particle_rows_entry_t, result)->refs ++;

    return result;
}

/** Stop using entries.
 *
 * @param t The table
 * @param entries Index plus 1 of the entries, 0 for none, zeroed
 * @param count Number of entries
 */
static void box_particle_rows_unref(
    box_particle_rows_t *t,
    int32_t *entries,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        if (entries[i] > 0 && entries[i] <= ecs_vec_count(&t->entries)) {
            box_particle_rows_entry_t *entry = ecs_vec_get_t(
                &t->entries, box_particle_rows_entry_t, entries[i] - 1);
            entry->refs --;
            if (!entry->refs && !entry->listed) {
                entry->listed = true;
                *ecs_vec_append_t(NULL, &t->unused[entry->rows], int32_t) = entries[i] - 1;
            }
        }

        entries[i] = 0;
    }
}

void box_particle_rows_release(
    box_particle_rows_t *t,
    box_particle_rows_use_t *use)
{
    box_particle_rows_unref(t, use->entries, BOX_PARTICLE_ROWS_ENTRIES);
    use->baked = false;
}

/** Check whether samples are finite, so that no NaN reaches the GPU.
 *
 * @param samples The samples
 * @param count Number of samples
 * @return Whether every sample is finite
 */
static bool box_particle_rows_finite(
    const float *samples,
    int32_t count)
{
    bool result = true;
    int32_t i;
    for (i = 0; i < count && result; i ++) {
        result = isfinite(samples[i]);
    }

    return result;
}

/** Check whether the samples of a row, each stride floats apart, are all equal.
 *
 * @param samples The first sample
 * @param stride Floats from one sample to the next
 * @return Whether the row is flat
 */
static bool box_particle_rows_flat(
    const float *samples,
    int32_t stride)
{
    bool result = true;
    int32_t i;
    for (i = 1; i < BOX_PARTICLE_CURVE_SAMPLES && result; i ++) {
        result = samples[i * stride] == samples[0];
    }

    return result;
}

/** Acquire the rows of baked content for one entry of a use.
 *
 * @param t The table
 * @param slot Receives the index plus 1 of the entry
 * @param kind What the rows hold
 * @param samples The content
 * @return First row of the entry
 */
static uint32_t box_particle_rows_take(
    box_particle_rows_t *t,
    int32_t *slot,
    box_particle_rows_kind_t kind,
    const float *samples)
{
    int32_t entry = box_particle_rows_acquire(t, kind, samples);
    *slot = entry + 1;

    return ecs_vec_get_t(&t->entries, box_particle_rows_entry_t, entry)->row;
}

/** Bake one list of keys of a scalar curve, and the area after it for a spin curve.
 *
 * @param kind The kind of the curve
 * @param keys The keys
 * @param rows What the rows hold
 * @param fallback The value without keys
 * @param out Receives the rows
 * @return Whether every sample is finite
 */
static bool box_particle_rows_bake_keys(
    CurveKind kind,
    const ecs_vec_t *keys,
    box_particle_rows_kind_t rows,
    float fallback,
    float *out)
{
    ParticleCurve view = { .kind = kind, .keys = *keys };
    box_particle_curve_bake(&view, fallback, out);
    if (rows == BOX_PARTICLE_ROWS_SPIN) {
        box_particle_curve_bake_area(out, out + BOX_PARTICLE_CURVE_SAMPLES);
    }

    return box_particle_rows_finite(out,
        (int32_t)box_particle_rows_of(rows) * BOX_PARTICLE_CURVE_SAMPLES);
}

/** Where the rows of one scalar curve go: the entries, rows and constant of its use */
typedef struct box_particle_rows_slot_t {
    int32_t *entry;         /**< Index plus 1 of the entry of the curve */
    int32_t *max_entry;     /**< Index plus 1 of the entry of its max curve */
    uint32_t *row;          /**< Row of the curve */
    uint32_t *max_row;      /**< Row of its max curve */
    float *value;           /**< Value of the curve while constant, set to its default */
} box_particle_rows_slot_t;

/** Give a slot the rows of a scalar curve with max_keys: rows for both of its curves, also a
 * flat one, unless both are the same constant.
 *
 * @param t The table
 * @param slot The slot, its constant set to the default
 * @param curve The curve, valid
 * @param kind What its rows hold
 */
static void box_particle_rows_scalar_range(
    box_particle_rows_t *t,
    const box_particle_rows_slot_t *slot,
    const ParticleCurve *curve,
    box_particle_rows_kind_t kind)
{
    float samples[BOX_PARTICLE_SPIN_ROWS * BOX_PARTICLE_CURVE_SAMPLES];
    float max[BOX_PARTICLE_SPIN_ROWS * BOX_PARTICLE_CURVE_SAMPLES];
    if (!box_particle_rows_bake_keys(curve->kind, &curve->keys, kind, *slot->value, samples) ||
        !box_particle_rows_bake_keys(curve->kind, &curve->max_keys, kind, *slot->value, max))
    {
        goto done;
    }

    if (box_particle_rows_flat(samples, 1) && box_particle_rows_flat(max, 1) &&
        samples[0] == max[0])
    {
        *slot->value = samples[0];
        goto done;
    }

    *slot->row = box_particle_rows_take(t, slot->entry, kind, samples);
    *slot->max_row = box_particle_rows_take(t, slot->max_entry, kind, max);
done:
    return;
}

/** Give a slot the row of one scalar curve: none for a curve without keys, a bad one or a flat
 * one, which are constants. A curve with max_keys also gets the row of its max curve.
 *
 * @param t The table
 * @param slot The slot, its constant set to the default
 * @param curve The curve
 * @param kind What its rows hold
 */
static void box_particle_rows_curve(
    box_particle_rows_t *t,
    const box_particle_rows_slot_t *slot,
    const ParticleCurve *curve,
    box_particle_rows_kind_t kind)
{
    float samples[BOX_PARTICLE_SPIN_ROWS * BOX_PARTICLE_CURVE_SAMPLES];
    int32_t count = ecs_vec_count(&curve->keys);
    if (!box_particle_curve_valid(curve)) {
        goto done;
    }

    if (ecs_vec_count(&curve->max_keys)) {
        box_particle_rows_scalar_range(t, slot, curve, kind);
        goto done;
    }

    if (!count) {
        goto done;
    }

    if (count == 1) {
        *slot->value = ecs_vec_first_t(&curve->keys, CurveKey)->value;
        goto done;
    }

    box_particle_curve_bake(curve, *slot->value, samples);
    if (kind == BOX_PARTICLE_ROWS_SPIN) {
        box_particle_curve_bake_area(samples, samples + BOX_PARTICLE_CURVE_SAMPLES);
    }

    if (!box_particle_rows_finite(samples,
        (int32_t)box_particle_rows_of(kind) * BOX_PARTICLE_CURVE_SAMPLES))
    {
        goto done;
    }

    if (box_particle_rows_flat(samples, 1)) {
        *slot->value = samples[0];
        goto done;
    }

    *slot->row = box_particle_rows_take(t, slot->entry, kind, samples);
done:
    return;
}

/** Give a use the row of one scalar curve of ParticleCurves, box_particle_rows_curve.
 *
 * @param t The table
 * @param use The use, its constants set to the defaults
 * @param i Index of the curve: size, opacity, rotation or spin
 * @param curve The curve
 */
static void box_particle_rows_scalar(
    box_particle_rows_t *t,
    box_particle_rows_use_t *use,
    int32_t i,
    const ParticleCurve *curve)
{
    box_particle_rows_slot_t slot = {
        .entry = &use->entries[i],
        .max_entry = &use->entries[i + BOX_PARTICLE_ROWS_CURVES],
        .row = &use->rows[i],
        .max_row = &use->max_rows[i],
        .value = &use->values[i]
    };
    box_particle_rows_curve(t, &slot, curve,
        i == 3 ? BOX_PARTICLE_ROWS_SPIN : BOX_PARTICLE_ROWS_SCALAR);
}

/** Check whether the colors of baked gradient rows are all the same.
 *
 * @param samples The rows
 * @return Whether every channel is flat
 */
static bool box_particle_rows_gradient_flat(
    const float *samples)
{
    bool result = true;
    int32_t c;
    for (c = 0; c < 3; c ++) {
        result = result && box_particle_rows_flat(samples + c, 4);
    }

    return result;
}

/** Give a use the rows of the gradient of ParticleCurves: none for a gradient without keys, a
 * bad one or a flat one, which are constants. A gradient with max_keys gets rows for both of
 * its gradients, also a flat one, unless both are the same constant.
 *
 * @param t The table
 * @param use The use, its constant gradient white
 * @param gradient The gradient
 */
static void box_particle_rows_gradient(
    box_particle_rows_t *t,
    box_particle_rows_use_t *use,
    const ParticleGradient *gradient)
{
    float samples[BOX_PARTICLE_ROWS_MAX_FLOATS], max[BOX_PARTICLE_ROWS_MAX_FLOATS];
    bool ranged = ecs_vec_count(&gradient->max_keys) != 0;
    int32_t c;
    if ((!ecs_vec_count(&gradient->keys) && !ranged) || !box_particle_gradient_valid(gradient)) {
        goto done;
    }

    box_particle_gradient_bake(gradient, samples);
    if (!box_particle_rows_finite(samples, BOX_PARTICLE_ROWS_MAX_FLOATS)) {
        goto done;
    }

    if (ranged) {
        ParticleGradient view = { .kind = gradient->kind, .keys = gradient->max_keys };
        box_particle_gradient_bake(&view, max);
        if (!box_particle_rows_finite(max, BOX_PARTICLE_ROWS_MAX_FLOATS)) {
            goto done;
        }
    }

    if (box_particle_rows_gradient_flat(samples) && (!ranged ||
        (box_particle_rows_gradient_flat(max) && !memcmp(samples, max, 3 * sizeof(float)))))
    {
        for (c = 0; c < 3; c ++) {
            use->gradient[c] = samples[c];
        }

        goto done;
    }

    use->gradient_row = box_particle_rows_take(t, &use->entries[4],
        BOX_PARTICLE_ROWS_GRADIENT, samples);
    if (ranged) {
        use->gradient_max_row = box_particle_rows_take(t,
            &use->entries[4 + BOX_PARTICLE_ROWS_CURVES], BOX_PARTICLE_ROWS_GRADIENT, max);
    }
done:
    return;
}

/** Add the count and the keys of a list of CurveKey to a hash.
 *
 * @param hash The hash so far
 * @param keys The keys
 * @return The hash
 */
static uint64_t box_particle_rows_hash_keys(
    uint64_t hash,
    const ecs_vec_t *keys)
{
    int32_t count = ecs_vec_count(keys);
    hash = box_particle_rows_mix(hash, (uint64_t)(uint32_t)count);
    if (count) {
        hash = box_particle_rows_hash_bytes(hash, ecs_vec_first(keys),
            (size_t)count * sizeof(CurveKey));
    }

    return hash;
}

/** Add the count and the keys of a list of ParticleGradientKey to a hash, without padding.
 *
 * @param hash The hash so far
 * @param keys The keys
 * @return The hash
 */
static uint64_t box_particle_rows_hash_colors(
    uint64_t hash,
    const ecs_vec_t *keys)
{
    const ParticleGradientKey *k = ecs_vec_first_t(keys, ParticleGradientKey);
    int32_t i, count = ecs_vec_count(keys);
    hash = box_particle_rows_mix(hash, (uint64_t)(uint32_t)count);
    for (i = 0; i < count; i ++) {
        uint32_t time;
        ecs_os_memcpy(&time, &k[i].time, ECS_SIZEOF(time));
        hash = box_particle_rows_mix(hash, (uint64_t)time | ((uint64_t)k[i].color.r << 32) |
            ((uint64_t)k[i].color.g << 40) | ((uint64_t)k[i].color.b << 48));
    }

    return hash;
}

/** Hash the kinds and keys of ParticleCurves, which changes when a key does, also one written in
 * place in the vector of its curve.
 *
 * @param curves The curves
 * @return The hash
 */
static uint64_t box_particle_rows_hash(
    const ParticleCurves *curves)
{
    const ParticleCurve *scalars[4] = {
        &curves->size, &curves->opacity, &curves->rotation, &curves->spin
    };
    uint64_t hash = 14695981039346656037ull;
    int32_t i;
    for (i = 0; i < 4; i ++) {
        hash = box_particle_rows_mix(hash, (uint64_t)scalars[i]->kind);
        hash = box_particle_rows_hash_keys(hash, &scalars[i]->keys);
        hash = box_particle_rows_hash_keys(hash, &scalars[i]->max_keys);
    }

    hash = box_particle_rows_mix(hash, (uint64_t)curves->color.kind);
    hash = box_particle_rows_hash_colors(hash, &curves->color.keys);
    hash = box_particle_rows_hash_colors(hash, &curves->color.max_keys);

    return hash;
}

/** Check whether the entries a use took when it was remembered still hold what it baked.
 *
 * @param t The table
 * @param entries Index plus 1 of its entries, 0 for none
 * @param generations Generations of its entries when it was remembered
 * @param count Number of entries
 * @return Whether every entry is as it was
 */
static bool box_particle_rows_current(
    const box_particle_rows_t *t,
    const int32_t *entries,
    const uint32_t *generations,
    int32_t count)
{
    bool result = true;
    int32_t i;
    for (i = 0; i < count && result; i ++) {
        if (entries[i] > 0) {
            result = entries[i] <= ecs_vec_count(&t->entries) && ecs_vec_get_t(&t->entries,
                box_particle_rows_entry_t, entries[i] - 1)->generation == generations[i];
        }
    }

    return result;
}

/** Count the uses of entries.
 *
 * @param t The table
 * @param entries Index plus 1 of the entries, 0 for none
 * @param count Number of entries
 */
static void box_particle_rows_ref(
    box_particle_rows_t *t,
    const int32_t *entries,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        if (entries[i] > 0) {
            ecs_vec_get_t(&t->entries, box_particle_rows_entry_t, entries[i] - 1)->refs ++;
        }
    }
}

/** Record the generations of the entries of a use.
 *
 * @param t The table
 * @param entries Index plus 1 of the entries, 0 for none
 * @param count Number of entries
 * @param out Receives the generations
 */
static void box_particle_rows_generations(
    const box_particle_rows_t *t,
    const int32_t *entries,
    int32_t count,
    uint32_t *out)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        out[i] = entries[i] > 0
            ? ecs_vec_get_t(&t->entries, box_particle_rows_entry_t, entries[i] - 1)->generation
            : 0;
    }
}

/** Get the hash of ParticleCurves, computed once per frame for the curves at one address, which
 * the instances of a template share.
 *
 * @param t The table
 * @param curves The curves
 * @return The hash
 */
static uint64_t box_particle_rows_curves_hash(
    box_particle_rows_t *t,
    const ParticleCurves *curves)
{
    if (curves != t->last) {
        t->last = curves;
        t->last_hash = box_particle_rows_hash(curves);
    }

    return t->last_hash;
}

/** Give a use the rows that curves with the same hash took when they were sampled, while those
 * rows still hold what was baked.
 *
 * @param t The table
 * @param use The use, its old entries saved by the caller
 * @param hash Hash of the curves
 * @return Whether the use took them
 */
static bool box_particle_rows_recall(
    box_particle_rows_t *t,
    box_particle_rows_use_t *use,
    uint64_t hash)
{
    ecs_map_val_t *index = ecs_map_get(&t->known_by_hash, hash);
    bool result = false;
    if (index) {
        const box_particle_rows_known_t *known = ecs_vec_get_t(
            &t->known, box_particle_rows_known_t, (int32_t)*index);
        if (box_particle_rows_current(t, known->use.entries, known->generations,
            BOX_PARTICLE_ROWS_ENTRIES))
        {
            *use = known->use;
            box_particle_rows_ref(t, use->entries, BOX_PARTICLE_ROWS_ENTRIES);
            result = true;
        }
    }

    return result;
}

/** Remember the rows of sampled curves by their hash, forgetting all of them when the table
 * knows BOX_PARTICLE_ROWS_KNOWN.
 *
 * @param t The table
 * @param use The use, baked
 */
static void box_particle_rows_remember(
    box_particle_rows_t *t,
    const box_particle_rows_use_t *use)
{
    ecs_map_val_t *index = ecs_map_get(&t->known_by_hash, use->hash);
    box_particle_rows_known_t *known;
    if (!index && ecs_vec_count(&t->known) >= BOX_PARTICLE_ROWS_KNOWN) {
        ecs_vec_clear(&t->known);
        ecs_map_clear(&t->known_by_hash);
    }

    if (index) {
        known = ecs_vec_get_t(&t->known, box_particle_rows_known_t, (int32_t)*index);
    } else {
        ecs_map_insert(&t->known_by_hash, use->hash, (ecs_map_val_t)ecs_vec_count(&t->known));
        known = ecs_vec_append_t(NULL, &t->known, box_particle_rows_known_t);
    }

    known->use = *use;
    box_particle_rows_generations(t, use->entries, BOX_PARTICLE_ROWS_ENTRIES,
        known->generations);
}

void box_particle_rows_update(
    box_particle_rows_t *t,
    box_particle_rows_use_t *use,
    const ParticleCurves *curves)
{
    static const float defaults[4] = {1.0f, 1.0f, 0.0f, 1.0f};
    uint64_t hash = curves ? box_particle_rows_curves_hash(t, curves) : 0;
    int32_t i, old[BOX_PARTICLE_ROWS_ENTRIES];
    if (use->baked && use->had == (curves != NULL) && (!curves || hash == use->hash)) {
        goto done;
    }

    ecs_os_memcpy(old, use->entries, ECS_SIZEOF(old));
    if (curves && box_particle_rows_recall(t, use, hash)) {
        box_particle_rows_unref(t, old, BOX_PARTICLE_ROWS_ENTRIES);
        goto done;
    }

    ecs_os_memset(use->entries, 0, ECS_SIZEOF(use->entries));
    for (i = 0; i < 4; i ++) {
        use->rows[i] = BOX_PARTICLE_CURVE_CONSTANT;
        use->max_rows[i] = BOX_PARTICLE_CURVE_CONSTANT;
        use->values[i] = defaults[i];
    }

    use->gradient_row = BOX_PARTICLE_CURVE_CONSTANT;
    use->gradient_max_row = BOX_PARTICLE_CURVE_CONSTANT;
    use->gradient[0] = use->gradient[1] = use->gradient[2] = 1.0f;
    if (curves) {
        box_particle_rows_scalar(t, use, 0, &curves->size);
        box_particle_rows_scalar(t, use, 1, &curves->opacity);
        box_particle_rows_scalar(t, use, 2, &curves->rotation);
        box_particle_rows_scalar(t, use, 3, &curves->spin);
        box_particle_rows_gradient(t, use, &curves->color);
        t->samplings ++;
    }

    use->hash = hash;
    use->had = curves != NULL;
    use->baked = true;
    if (curves) {
        box_particle_rows_remember(t, use);
    }

    box_particle_rows_unref(t, old, BOX_PARTICLE_ROWS_ENTRIES);
done:
    return;
}

/** Get the curves of a ParticleMotion in the order of their channels: velocity x, y and z,
 * speed, then force x, y and z, drag.
 *
 * @param motion The ParticleMotion
 * @param out Receives the eight curves
 */
static void box_particle_rows_motion_curves(
    const ParticleMotion *motion,
    const ParticleCurve **out)
{
    out[0] = &motion->velocity_x;
    out[1] = &motion->velocity_y;
    out[2] = &motion->velocity_z;
    out[3] = &motion->speed;
    out[4] = &motion->force_x;
    out[5] = &motion->force_y;
    out[6] = &motion->force_z;
    out[7] = &motion->drag;
}

/** Hash the kinds and keys of a ParticleMotion, as box_particle_rows_hash.
 *
 * @param motion The ParticleMotion
 * @return The hash
 */
static uint64_t box_particle_rows_motion_hash(
    const ParticleMotion *motion)
{
    const ParticleCurve *curves[8];
    uint64_t hash = 14695981039346656037ull;
    int32_t i;
    box_particle_rows_motion_curves(motion, curves);
    for (i = 0; i < 8; i ++) {
        hash = box_particle_rows_mix(hash, (uint64_t)curves[i]->kind);
        hash = box_particle_rows_hash_keys(hash, &curves[i]->keys);
        hash = box_particle_rows_hash_keys(hash, &curves[i]->max_keys);
    }

    return hash;
}

/** Bake one curve of a vector into a channel of its rows and of the rows of its max: a curve
 * without max_keys is the same in both, and one that is not valid or whose samples are not
 * finite is the default of its field.
 *
 * @param curve The curve
 * @param fallback The default of its field
 * @param c The channel, 0 to 3
 * @param out Receives the channel of the rows of the vector
 * @param max Receives the channel of the rows of the max vector
 */
static void box_particle_rows_vector_channel(
    const ParticleCurve *curve,
    float fallback,
    int32_t c,
    float *out,
    float *max)
{
    float samples[BOX_PARTICLE_CURVE_SAMPLES], high[BOX_PARTICLE_CURVE_SAMPLES];
    bool ok = box_particle_curve_valid(curve);
    int32_t i;
    if (ok) {
        ok = box_particle_rows_bake_keys(curve->kind, &curve->keys, BOX_PARTICLE_ROWS_SCALAR,
            fallback, samples);
    }

    if (ok && ecs_vec_count(&curve->max_keys)) {
        ok = box_particle_rows_bake_keys(curve->kind, &curve->max_keys,
            BOX_PARTICLE_ROWS_SCALAR, fallback, high);
    } else if (ok) {
        ecs_os_memcpy(high, samples, ECS_SIZEOF(high));
    }

    for (i = 0; i < BOX_PARTICLE_CURVE_SAMPLES; i ++) {
        out[i * 4 + c] = ok ? samples[i] : fallback;
        max[i * 4 + c] = ok ? high[i] : fallback;
    }
}

/** Give a vector of a ParticleMotion its rows: none while its four curves are constant, which
 * are its values, else rows of the four, and rows of their max curves when one of them has a
 * max curve that differs.
 *
 * @param t The table
 * @param curves The four curves
 * @param defaults Their defaults
 * @param entries Receive the index plus 1 of the entries of the vector and of its max
 * @param row Receives the first row of the vector
 * @param max_row Receives the first row of its max
 * @param values Receive the values while constant
 */
static void box_particle_rows_vector(
    box_particle_rows_t *t,
    const ParticleCurve **curves,
    const float *defaults,
    int32_t *entries,
    uint32_t *row,
    uint32_t *max_row,
    float *values)
{
    float samples[BOX_PARTICLE_ROWS_MAX_FLOATS], max[BOX_PARTICLE_ROWS_MAX_FLOATS];
    size_t bytes = BOX_PARTICLE_VECTOR_ROWS * BOX_PARTICLE_CURVE_SAMPLES * sizeof(float);
    bool flat = true, ranged;
    int32_t c;
    for (c = 0; c < 4; c ++) {
        box_particle_rows_vector_channel(curves[c], defaults[c], c, samples, max);
        flat = flat && box_particle_rows_flat(samples + c, 4);
    }

    ranged = memcmp(samples, max, bytes) != 0;
    if (flat && !ranged) {
        for (c = 0; c < 4; c ++) {
            values[c] = samples[c];
        }

        goto done;
    }

    *row = box_particle_rows_take(t, &entries[0], BOX_PARTICLE_ROWS_VECTOR, samples);
    if (ranged) {
        *max_row = box_particle_rows_take(t, &entries[1], BOX_PARTICLE_ROWS_VECTOR, max);
    }
done:
    return;
}

/** Get the hash of a ParticleMotion, computed once per frame for the motion at one address, as
 * box_particle_rows_curves_hash.
 *
 * @param t The table
 * @param motion The motion
 * @return The hash
 */
static uint64_t box_particle_rows_motion_hash_of(
    box_particle_rows_t *t,
    const ParticleMotion *motion)
{
    if (motion != t->last_motion) {
        t->last_motion = motion;
        t->last_motion_hash = box_particle_rows_motion_hash(motion);
    }

    return t->last_motion_hash;
}

/** Give a use the rows that a ParticleMotion with the same hash took when it was sampled, as
 * box_particle_rows_recall.
 *
 * @param t The table
 * @param use The use, its old entries saved by the caller
 * @param hash Hash of the motion
 * @return Whether the use took them
 */
static bool box_particle_rows_motion_recall(
    box_particle_rows_t *t,
    box_particle_rows_motion_t *use,
    uint64_t hash)
{
    ecs_map_val_t *index = ecs_map_get(&t->known_motion_by_hash, hash);
    bool result = false;
    if (index) {
        const box_particle_rows_known_motion_t *known = ecs_vec_get_t(
            &t->known_motion, box_particle_rows_known_motion_t, (int32_t)*index);
        if (box_particle_rows_current(t, known->use.entries, known->generations, 4)) {
            *use = known->use;
            box_particle_rows_ref(t, use->entries, 4);
            result = true;
        }
    }

    return result;
}

/** Remember the rows of a sampled ParticleMotion by its hash, as box_particle_rows_remember.
 *
 * @param t The table
 * @param use The use, baked
 */
static void box_particle_rows_motion_remember(
    box_particle_rows_t *t,
    const box_particle_rows_motion_t *use)
{
    ecs_map_val_t *index = ecs_map_get(&t->known_motion_by_hash, use->hash);
    box_particle_rows_known_motion_t *known;
    if (!index && ecs_vec_count(&t->known_motion) >= BOX_PARTICLE_ROWS_KNOWN) {
        ecs_vec_clear(&t->known_motion);
        ecs_map_clear(&t->known_motion_by_hash);
    }

    if (index) {
        known = ecs_vec_get_t(&t->known_motion, box_particle_rows_known_motion_t,
            (int32_t)*index);
    } else {
        ecs_map_insert(&t->known_motion_by_hash, use->hash,
            (ecs_map_val_t)ecs_vec_count(&t->known_motion));
        known = ecs_vec_append_t(NULL, &t->known_motion, box_particle_rows_known_motion_t);
    }

    known->use = *use;
    box_particle_rows_generations(t, use->entries, 4, known->generations);
}

void box_particle_rows_motion_update(
    box_particle_rows_t *t,
    box_particle_rows_motion_t *use,
    const ParticleMotion *motion)
{
    static const float defaults[8] = {0, 0, 0, 1, 0, 0, 0, 1};
    const ParticleCurve *curves[8];
    box_particle_motion_rows_t *rows = &use->rows;
    uint64_t hash = motion ? box_particle_rows_motion_hash_of(t, motion) : 0;
    int32_t c, old[4];
    if (use->baked && use->had == (motion != NULL) && (!motion || hash == use->hash)) {
        goto done;
    }

    ecs_os_memcpy(old, use->entries, ECS_SIZEOF(old));
    if (motion && box_particle_rows_motion_recall(t, use, hash)) {
        box_particle_rows_unref(t, old, 4);
        goto done;
    }

    ecs_os_memset(use->entries, 0, ECS_SIZEOF(use->entries));
    rows->velocity_row = rows->velocity_max_row = BOX_PARTICLE_CURVE_CONSTANT;
    rows->force_row = rows->force_max_row = BOX_PARTICLE_CURVE_CONSTANT;
    for (c = 0; c < 4; c ++) {
        rows->velocity[c] = defaults[c];
        rows->force[c] = defaults[4 + c];
    }

    if (motion) {
        box_particle_rows_motion_curves(motion, curves);
        box_particle_rows_vector(t, curves, defaults, &use->entries[0], &rows->velocity_row,
            &rows->velocity_max_row, rows->velocity);
        box_particle_rows_vector(t, curves + 4, defaults + 4, &use->entries[2],
            &rows->force_row, &rows->force_max_row, rows->force);
        t->samplings ++;
    }

    use->active = rows->velocity_row != BOX_PARTICLE_CURVE_CONSTANT ||
        rows->force_row != BOX_PARTICLE_CURVE_CONSTANT ||
        memcmp(rows->velocity, defaults, sizeof(rows->velocity)) ||
        memcmp(rows->force, defaults + 4, sizeof(rows->force));
    use->hash = hash;
    use->had = motion != NULL;
    use->baked = true;
    if (motion) {
        box_particle_rows_motion_remember(t, use);
    }

    box_particle_rows_unref(t, old, 4);
done:
    return;
}

void box_particle_rows_motion_release(
    box_particle_rows_t *t,
    box_particle_rows_motion_t *use)
{
    box_particle_rows_unref(t, use->entries, 4);
    use->baked = false;
    use->active = false;
}

bool box_particle_rows_motion_used(
    const box_particle_rows_motion_t *use)
{
    bool result = false;
    int32_t i;
    for (i = 0; i < 4; i ++) {
        result = result || use->entries[i] != 0;
    }

    return result;
}

bool box_particle_rows_used(
    const box_particle_rows_use_t *use)
{
    bool result = false;
    int32_t i;
    for (i = 0; i < BOX_PARTICLE_ROWS_ENTRIES; i ++) {
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
    t->last = NULL;
    t->last_motion = NULL;
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
    out->samplings = t->samplings;
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
