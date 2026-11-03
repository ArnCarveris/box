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
 * take rows of values as the scalar curves of ParticleCurves do.
 */

#include "particle_rows.h"
#include "../particle/simulate.h"
#include "../particle/curves.h"

/** Floats of the largest content of an entry, the rows of a gradient */
#define BOX_PARTICLE_ROWS_MAX_FLOATS (BOX_PARTICLE_GRADIENT_ROWS * BOX_PARTICLE_CURVE_SAMPLES)

/** What the rows of an entry hold */
typedef enum box_particle_rows_kind_t {
    BOX_PARTICLE_ROWS_SCALAR,   /**< A row of values */
    BOX_PARTICLE_ROWS_SPIN,     /**< A row of values and a row of their accumulated area */
    BOX_PARTICLE_ROWS_GRADIENT  /**< BOX_PARTICLE_GRADIENT_ROWS rows of linear colors */
} box_particle_rows_kind_t;

/** Rows baked from one curve, shared by the pools with that curve */
typedef struct box_particle_rows_entry_t {
    box_particle_rows_kind_t kind; /**< What the rows hold */
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

/** Write the content of an entry to its rows and mark them for the send.
 *
 * @param t The table
 * @param entry The entry, its rows handed out
 * @param samples The content, BOX_PARTICLE_CURVE_SAMPLES floats per row of the entry
 */
static void box_particle_rows_write(
    box_particle_rows_t *t,
    const box_particle_rows_entry_t *entry,
    const float *samples)
{
    float *out = ecs_vec_get_t(&t->data, float,
        (int32_t)(entry->row * BOX_PARTICLE_CURVE_SAMPLES));
    ecs_os_memcpy(out, samples,
        (ecs_size_t)(entry->rows * BOX_PARTICLE_CURVE_SAMPLES * sizeof(float)));
    if (entry->row < t->dirty_first) {
        t->dirty_first = entry->row;
    }

    if (entry->row + entry->rows > t->dirty_end) {
        t->dirty_end = entry->row + entry->rows;
    }

    t->bakes ++;
}

/** Get the rows of a baked curve: those of an entry of the same kind and content, else rows
 * nothing uses or new rows, written. The old rows of a pool are released after its new ones are
 * acquired, so its unchanged curves keep theirs.
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
    int32_t i, count = ecs_vec_count(&t->entries), result = -1, unused = -1;
    box_particle_rows_entry_t *entry;
    for (i = 0; i < count; i ++) {
        entry = ecs_vec_get_t(&t->entries, box_particle_rows_entry_t, i);
        if (entry->kind == kind && !memcmp(ecs_vec_get_t(&t->data, float,
            (int32_t)(entry->row * BOX_PARTICLE_CURVE_SAMPLES)), samples, bytes))
        {
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
    entry->refs = 0;
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
            ecs_vec_get_t(&t->entries, box_particle_rows_entry_t, entries[i] - 1)->refs --;
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

/** Add bytes to an FNV-1a hash.
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
    size_t i;
    for (i = 0; i < size; i ++) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }

    return hash;
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
    hash = box_particle_rows_hash_bytes(hash, &count, sizeof(count));
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
    hash = box_particle_rows_hash_bytes(hash, &count, sizeof(count));
    for (i = 0; i < count; i ++) {
        uint8_t color[3] = { k[i].color.r, k[i].color.g, k[i].color.b };
        hash = box_particle_rows_hash_bytes(hash, &k[i].time, sizeof(float));
        hash = box_particle_rows_hash_bytes(hash, color, sizeof(color));
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
        hash = box_particle_rows_hash_bytes(hash, &scalars[i]->kind, sizeof(CurveKind));
        hash = box_particle_rows_hash_keys(hash, &scalars[i]->keys);
        hash = box_particle_rows_hash_keys(hash, &scalars[i]->max_keys);
    }

    hash = box_particle_rows_hash_bytes(hash, &curves->color.kind, sizeof(CurveKind));
    hash = box_particle_rows_hash_colors(hash, &curves->color.keys);
    hash = box_particle_rows_hash_colors(hash, &curves->color.max_keys);

    return hash;
}

void box_particle_rows_update(
    box_particle_rows_t *t,
    box_particle_rows_use_t *use,
    const ParticleCurves *curves)
{
    static const float defaults[4] = {1.0f, 1.0f, 0.0f, 1.0f};
    uint64_t hash = curves ? box_particle_rows_hash(curves) : 0;
    int32_t i, old[BOX_PARTICLE_ROWS_ENTRIES];
    if (use->baked && use->had == (curves != NULL) && (!curves || hash == use->hash)) {
        goto done;
    }

    ecs_os_memcpy(old, use->entries, ECS_SIZEOF(old));
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
    }

    box_particle_rows_unref(t, old, BOX_PARTICLE_ROWS_ENTRIES);
    use->hash = hash;
    use->had = curves != NULL;
    use->baked = true;
done:
    return;
}

/** Get the curves of a ParticleMotion in the order of their rows.
 *
 * @param motion The ParticleMotion
 * @param out Receives the BOX_PARTICLE_MOTION_CURVES curves
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
    const ParticleCurve *curves[BOX_PARTICLE_MOTION_CURVES];
    uint64_t hash = 14695981039346656037ull;
    int32_t i;
    box_particle_rows_motion_curves(motion, curves);
    for (i = 0; i < BOX_PARTICLE_MOTION_CURVES; i ++) {
        hash = box_particle_rows_hash_bytes(hash, &curves[i]->kind, sizeof(CurveKind));
        hash = box_particle_rows_hash_keys(hash, &curves[i]->keys);
        hash = box_particle_rows_hash_keys(hash, &curves[i]->max_keys);
    }

    return hash;
}

void box_particle_rows_motion_update(
    box_particle_rows_t *t,
    box_particle_rows_motion_t *use,
    const ParticleMotion *motion)
{
    static const float defaults[BOX_PARTICLE_MOTION_CURVES] = {0, 0, 0, 1, 0, 0, 0, 1};
    const ParticleCurve *curves[BOX_PARTICLE_MOTION_CURVES];
    uint64_t hash = motion ? box_particle_rows_motion_hash(motion) : 0;
    int32_t i, old[2 * BOX_PARTICLE_MOTION_CURVES];
    if (use->baked && use->had == (motion != NULL) && (!motion || hash == use->hash)) {
        goto done;
    }

    ecs_os_memcpy(old, use->entries, ECS_SIZEOF(old));
    ecs_os_memset(use->entries, 0, ECS_SIZEOF(use->entries));
    for (i = 0; i < BOX_PARTICLE_MOTION_CURVES; i ++) {
        use->rows.rows[i] = BOX_PARTICLE_CURVE_CONSTANT;
        use->rows.max_rows[i] = BOX_PARTICLE_CURVE_CONSTANT;
        use->rows.values[i] = defaults[i];
    }

    use->active = false;
    if (motion) {
        box_particle_rows_motion_curves(motion, curves);
        for (i = 0; i < BOX_PARTICLE_MOTION_CURVES; i ++) {
            box_particle_rows_slot_t slot = {
                .entry = &use->entries[i],
                .max_entry = &use->entries[i + BOX_PARTICLE_MOTION_CURVES],
                .row = &use->rows.rows[i],
                .max_row = &use->rows.max_rows[i],
                .value = &use->rows.values[i]
            };
            box_particle_rows_curve(t, &slot, curves[i], BOX_PARTICLE_ROWS_SCALAR);
            use->active = use->active ||
                use->rows.rows[i] != BOX_PARTICLE_CURVE_CONSTANT ||
                use->rows.values[i] != defaults[i];
        }
    }

    box_particle_rows_unref(t, old, 2 * BOX_PARTICLE_MOTION_CURVES);
    use->hash = hash;
    use->had = motion != NULL;
    use->baked = true;
done:
    return;
}

void box_particle_rows_motion_release(
    box_particle_rows_t *t,
    box_particle_rows_motion_t *use)
{
    box_particle_rows_unref(t, use->entries, 2 * BOX_PARTICLE_MOTION_CURVES);
    use->baked = false;
    use->active = false;
}

bool box_particle_rows_motion_used(
    const box_particle_rows_motion_t *use)
{
    bool result = false;
    int32_t i;
    for (i = 0; i < 2 * BOX_PARTICLE_MOTION_CURVES; i ++) {
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
