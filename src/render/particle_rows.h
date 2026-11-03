/**
 * @file render/particle_rows.h
 * @brief The curve table of the particle extract: the curves of the emitters baked into rows of
 * BOX_PARTICLE_CURVE_SAMPLES floats of one storage buffer, which the particle shaders read.
 *
 * A curve is baked when it changes, into the rows of an entry with the same baked content when
 * there is one, so the instances of an effect share them. Constant curves take no row, and a
 * curve that is not valid, or whose samples are not finite, is the default of its field.
 *
 * A curve with max_keys takes rows for both of its curves, also a flat one, unless the two are
 * the same constant; the curve without max_keys takes no second row.
 */

#ifndef BOX_RENDER_PARTICLE_ROWS_H
#define BOX_RENDER_PARTICLE_ROWS_H

#include "render.h"
#include "../particle/simulate.h"

/** Curves of an emitter: the size, opacity, rotation and spin curves and the gradient */
#define BOX_PARTICLE_ROWS_CURVES (5)

/** Entries a pool may use: those of its curves, then those of their max curves */
#define BOX_PARTICLE_ROWS_ENTRIES (2 * BOX_PARTICLE_ROWS_CURVES)

/** The curve table: the entries that hand out rows and the rows as they were sent */
typedef struct box_particle_rows_t {
    ecs_vec_t entries;      /**< box_particle_rows_entry_t of particle_rows.c */
    ecs_vec_t data;         /**< float, BOX_PARTICLE_CURVE_SAMPLES per row handed out */
    uint32_t capacity;      /**< Rows of the buffer on the backend, 0 before it exists */
    uint32_t dirty_first;   /**< First row baked since the last send, UINT32_MAX for none */
    uint32_t dirty_end;     /**< Row after the last row baked since the last send */
    int64_t bakes;          /**< Curves baked since the table was made */
} box_particle_rows_t;

/** The rows the curves of one pool use and what its block reads */
typedef struct box_particle_rows_use_t {
    bool baked;             /**< The members below follow hash */
    bool had;               /**< The emitter had ParticleCurves when they were baked */
    uint64_t hash;          /**< Hash of the kinds and keys of the ParticleCurves they were
                             *   baked from */
    int32_t entries[BOX_PARTICLE_ROWS_ENTRIES]; /**< Index plus 1 of the entries it uses, 0
                             *   for a constant curve: the curves, then their max curves */
    uint32_t rows[4];       /**< Rows of the size, opacity, rotation and spin curves,
                             *   BOX_PARTICLE_CURVE_CONSTANT for a constant */
    uint32_t max_rows[4];   /**< Rows of their max curves, BOX_PARTICLE_CURVE_CONSTANT for a
                             *   curve without max_keys or one whose two curves are the same
                             *   constant */
    float values[4];        /**< Values of those curves while constant */
    uint32_t gradient_row;  /**< First row of the gradient, BOX_PARTICLE_CURVE_CONSTANT for a
                             *   constant */
    uint32_t gradient_max_row; /**< First row of the max gradient, BOX_PARTICLE_CURVE_CONSTANT
                             *   for none */
    float gradient[3];      /**< Linear color of a constant gradient */
} box_particle_rows_use_t;

/** The rows the curves of the ParticleMotion of one pool use and what its block reads */
typedef struct box_particle_rows_motion_t {
    bool baked;             /**< The members below follow hash */
    bool had;               /**< The emitter had ParticleMotion when they were baked */
    bool active;            /**< A curve is not the default of its field, so the passes read
                             *   the rows */
    uint64_t hash;          /**< Hash of the kinds and keys of the ParticleMotion they were
                             *   baked from */
    int32_t entries[2 * BOX_PARTICLE_MOTION_CURVES]; /**< Index plus 1 of the entries it uses,
                             *   0 for a constant curve: the curves, then their max curves */
    box_particle_motion_rows_t rows; /**< Rows and constants of the curves */
} box_particle_rows_motion_t;

/** Initialize an empty curve table.
 *
 * @param t The table
 */
void box_particle_rows_init(
    box_particle_rows_t *t);

/** Free a curve table.
 *
 * @param t The table
 */
void box_particle_rows_fini(
    box_particle_rows_t *t);

/** Forget every row, for a backend that does not have the table; the next send creates it.
 * The uses of the pools must be released first.
 *
 * @param t The table
 */
void box_particle_rows_reset(
    box_particle_rows_t *t);

/** Give a pool the rows of the curves of its emitter, baked only when they changed since the
 * last call. A constant curve takes no row, nor an emitter without curves.
 *
 * @param t The table
 * @param use The rows of the pool
 * @param curves The ParticleCurves of the emitter, NULL for none
 */
void box_particle_rows_update(
    box_particle_rows_t *t,
    box_particle_rows_use_t *use,
    const ParticleCurves *curves);

/** Give a pool the rows of the curves of the ParticleMotion of its emitter, baked only when
 * they changed since the last call, as box_particle_rows_update.
 *
 * @param t The table
 * @param use The rows of the pool
 * @param motion The ParticleMotion of the emitter, NULL for none
 */
void box_particle_rows_motion_update(
    box_particle_rows_t *t,
    box_particle_rows_motion_t *use,
    const ParticleMotion *motion);

/** Stop using the rows of the ParticleMotion of a pool.
 *
 * @param t The table
 * @param use The rows of the pool
 */
void box_particle_rows_motion_release(
    box_particle_rows_t *t,
    box_particle_rows_motion_t *use);

/** Check whether the ParticleMotion of a pool reads rows of the table.
 *
 * @param use The rows of the pool
 * @return Whether one of its curves has rows
 */
bool box_particle_rows_motion_used(
    const box_particle_rows_motion_t *use);

/** Stop using the rows of a pool, which are handed out again to other curves.
 *
 * @param t The table
 * @param use The rows of the pool
 */
void box_particle_rows_release(
    box_particle_rows_t *t,
    box_particle_rows_use_t *use);

/** Check whether a pool reads rows of the table.
 *
 * @param use The rows of the pool
 * @return Whether one of its curves has rows
 */
bool box_particle_rows_used(
    const box_particle_rows_use_t *use);

/** Send the rows baked since the last send, creating the buffer again with every row when it
 * has to grow; the backend binds the new buffer with the groups of the packet.
 *
 * @param t The table
 * @param p The packet
 */
void box_particle_rows_send(
    box_particle_rows_t *t,
    box_render_packet_t *p);

/** Get the counts of a table.
 *
 * @param t The table
 * @param out Receives the counts
 */
void box_particle_rows_stats(
    const box_particle_rows_t *t,
    box_render_particle_curve_stats_t *out);

/** Get the rows of a table.
 *
 * @param t The table
 * @param rows Receives the rows handed out, NULL to ignore
 * @return The rows, NULL without rows
 */
const float* box_particle_rows_data(
    const box_particle_rows_t *t,
    int32_t *rows);

#endif
