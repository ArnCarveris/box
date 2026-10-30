/**
 * @file app/app.c
 * @brief Registers the box.app module, its stop conditions, the CPU budget and the run loop.
 */

#define BOX_APP_IMPL
#include "app.h"
#include "watch.h"
#include "../render/render.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/** Samples of one series of FrameTiming, the most recent BOX_FRAME_WINDOW frames */
typedef struct box_app_series_t {
    double values[BOX_FRAME_WINDOW];    /**< Samples in milliseconds, a ring */
    int32_t count;                      /**< Samples stored, at most BOX_FRAME_WINDOW */
    int32_t next;                       /**< Slot the next sample goes to */
} box_app_series_t;

/** Clock of the frame systems: the CPU budget and the frame timing */
typedef struct box_app_clock_t {
    double start;               /**< Wall seconds when the module was imported */
    double cpu;                 /**< Process CPU seconds at the end of the previous frame */
    double wall;                /**< Wall seconds at the end of the previous frame */
    double pace;                /**< Deadline the CPU budget waited to at the end of the
                                 *   previous frame, its wall time when it did not wait */
    ecs_world_t *world;         /**< World of the frame loop, for the browser's frame callback */
    double begin;               /**< Wall seconds at the start of the previous frame, 0 none */
    int32_t presented;          /**< RenderStatus.presented at the previous frame */
    box_app_series_t delta;     /**< Deltas of the recent frames */
    box_app_series_t interval;  /**< Intervals of the recent frames */
    box_app_series_t error;     /**< Differences of delta and interval of the recent frames */
    box_app_series_t present;   /**< Present intervals of the recent frames */
    box_app_series_t late;      /**< 1 for a recent frame with a late delta, else 0 */
    box_app_series_t dropped;   /**< 1 for a recent dropped frame, else 0 */
} box_app_clock_t;

_Static_assert(sizeof(((FrameSamples*)0)->interval) == BOX_FRAME_WINDOW * sizeof(double),
    "FrameSamples.interval holds BOX_FRAME_WINDOW samples");

/** Name of the system whose context is the clock of the frame loop */
#define BOX_APP_TIMING_SYSTEM "box.app.FrameTimingUpdate"

/** Create the clock of the frame system.
 *
 * @return The clock, freed by box_app_clock_free
 */
static box_app_clock_t* box_app_clock_new(void)
{
    box_app_clock_t *result = ecs_os_calloc_t(box_app_clock_t);
    result->start = box_app_wall_time();

    return result;
}

/** Free the clock of the frame system.
 *
 * @param ctx The clock
 */
static void box_app_clock_free(
    void *ctx)
{
    ecs_os_free(ctx);
}

/** Make App.fps the target fps of flecs, which caps the frames; 0 for no cap. The browser keeps
 * no cap: the animation frames pace it and a flecs sleep would block its main thread.
 *
 * @param world The world
 * @param app The App singleton
 */
static void box_app_target_fps(
    ecs_world_t *world,
    const App *app)
{
#ifdef __EMSCRIPTEN__
    float fps = 0;
    (void)app;
#else
    float fps = app->fps > 0 ? app->fps : 0;
#endif
    ecs_set_target_fps(world, fps);
}

/** Read the delta time the next frame advances from the App singleton, so a change through the
 * command port or a script applies on the next frame.
 *
 * @param world The world
 * @return App.delta_time, 0 when flecs measures the delta
 */
static float box_app_frame_delta(
    const ecs_world_t *world)
{
    const App *app = ecs_singleton_get(world, App);

    return app ? app->delta_time : 0;
}

/** Make a new App true: flecs caps the frames at App.fps and a headless process gets its
 * temporary folder.
 *
 * @param it The iterator of the observer
 */
static void BoxAppOnSet(
    ecs_iter_t *it)
{
    ecs_world_t *world = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(it->world));
    const App *app = ecs_field(it, App, 0);
    AppStatus *status = ecs_singleton_get_mut(world, AppStatus);
    box_app_target_fps(world, app);
    if (!status) {
        goto done;
    }

    if (app->headless && !status->tmp_dir) {
        status->tmp_dir = box_app_tmp_create(world);
    }

done:
    return;
}

/** Count the frame, start the clock on the first frame, stop the world when App says so and
 * sleep when a headless process used more than its CPU budget since the previous frame.
 *
 * The sleep waits to a deadline that advances from the previous one, as the virtual
 * scheduling form of a leaky bucket (GCRA) does: a wait that wakes late shortens the next one,
 * so timer leeway does not lower the CPU share below the budget.
 *
 * A frame under its budget starts the next interval at its own wall time: idle earns no credit.
 *
 * @param it The iterator of the system
 */
static void BoxAppFrame(
    ecs_iter_t *it)
{
    box_app_clock_t *clock = it->ctx;
    const App *app = ecs_singleton_get(it->world, App);
    AppStatus *status = ecs_singleton_get_mut(it->world, AppStatus);
    double cpu = box_app_cpu_time();
    double wall = box_app_wall_time();
    double pace = wall;
    if (!app || !status) {
        goto done;
    }

    if (clock->wall == 0) {
        clock->start = wall;
    }

    status->frame ++;
    status->elapsed = wall - clock->start;
    if (app->frames > 0 && status->frame >= app->frames) {
        ecs_quit(it->world);
    }

    if (app->timeout > 0 && status->elapsed >= app->timeout) {
        ecs_quit(it->world);
    }

    if (app->headless && !app->unpaced && clock->wall > 0) {
        double sleep = box_app_pace(cpu - clock->cpu, wall - clock->pace, app->cpu_budget);
        if (sleep > 0) {
            double now = box_wait_until(wall + sleep);
            status->slept += now - wall;
            pace = wall + sleep;
            wall = now;
        }
    }

done:
    clock->cpu = cpu;
    clock->wall = wall;
    clock->pace = pace;
}

/** Private state of a SetAt entity, added with SetAt through With: the components written to
 * the entity since it has SetAt, the components it had before SetAt was added, and the ones
 * added in the same operation as SetAt.
 */
typedef struct SetAtWritten {
    ecs_vec_t written;  /**< ecs_id_t of the components set since the entity has SetAt */
    ecs_vec_t before;   /**< ecs_id_t of the components the entity had before SetAt */
    ecs_vec_t batched;  /**< ecs_id_t of the components added with SetAt, as by a stage merge */
} SetAtWritten;

static ECS_COMPONENT_DECLARE(SetAtWritten);

/** Destruct SetAtWritten components.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_app_set_at_written_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    SetAtWritten *w = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        ecs_vec_fini_t(NULL, &w[i].written, ecs_id_t);
        ecs_vec_fini_t(NULL, &w[i].before, ecs_id_t);
        ecs_vec_fini_t(NULL, &w[i].batched, ecs_id_t);
    }
}

/** Move SetAtWritten components.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_app_set_at_written_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    box_app_set_at_written_dtor(dst, count, ti);
    ecs_os_memcpy(dst, src, ti->size * count);
    ecs_os_memset(src, 0, ti->size * count);
}

/** Copy SetAtWritten components, as when a prefab with SetAt is instantiated.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_app_set_at_written_copy(
    void *dst,
    const void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    SetAtWritten *d = dst;
    const SetAtWritten *s = src;
    int32_t i;
    box_app_set_at_written_dtor(dst, count, ti);
    for (i = 0; i < count; i ++) {
        d[i].written = ecs_vec_copy_t(NULL, &s[i].written, ecs_id_t);
        d[i].before = ecs_vec_copy_t(NULL, &s[i].before, ecs_id_t);
        d[i].batched = ecs_vec_copy_t(NULL, &s[i].batched, ecs_id_t);
    }
}

/** Check whether a list of ids holds an id.
 *
 * @param ids The list of ecs_id_t
 * @param id The id
 * @return Whether the list holds id
 */
static bool box_app_set_at_has(
    const ecs_vec_t *ids,
    ecs_id_t id)
{
    const ecs_id_t *array = ecs_vec_first_t(ids, ecs_id_t);
    int32_t i, count = ecs_vec_count(ids);
    bool result = false;
    for (i = 0; i < count && !result; i ++) {
        result = array[i] == id;
    }

    return result;
}

/** Record a component written to SetAt entities: OnSet of a component of an entity with
 * SetAt. A component that With adds without a value gets OnAdd but no OnSet, so it is not
 * recorded.
 *
 * @param it The iterator of the observer
 */
static void BoxAppSetAtOnSet(
    ecs_iter_t *it)
{
    ecs_id_t id = it->event_id;
    bool skip = ECS_IS_PAIR(id) || id == ecs_id(SetAt) || id == ecs_id(SetAtWritten);
    int32_t i;
    for (i = 0; i < it->count && !skip; i ++) {
        SetAtWritten *w = ecs_get_mut(it->world, it->entities[i], SetAtWritten);
        if (w && !box_app_set_at_has(&w->written, id)) {
            ecs_vec_append_t(NULL, &w->written, ecs_id_t)[0] = id;
        }
    }
}

/** State of the SetAt observers. The writes to SetAt entities are recorded by one OnSet observer
 * per component that a table with SetAtWritten has, created while the world has SetAt entities.
 *
 * A wildcard OnSet observer would make flecs emit OnSet for every set of every table that
 * exists while it does, even after it is deleted, since flecs never clears the table flags. The
 * tables of a component a SetAt entity wrote keep emitting OnSet for it for the same reason.
 */
typedef struct box_app_set_at_t {
    ecs_entity_t module;    /**< The box.app module */
    ecs_entity_t scope;     /**< AppSetAtOnSet, parent of the observers, 0 without SetAt entities */
    ecs_vec_t ids;          /**< ecs_id_t of the components with an observer */
    int32_t count;          /**< SetAt entities in the world */
} box_app_set_at_t;

/** Free the state of the SetAt observers.
 *
 * @param ptr The state
 */
static void box_app_set_at_free(
    void *ptr)
{
    box_app_set_at_t *state = ptr;
    ecs_vec_fini_t(NULL, &state->ids, ecs_id_t);
    ecs_os_free(state);
}

/** Create the observers that record the writes to the components of a table with SetAtWritten,
 * for the components that have none yet.
 *
 * @param world The world
 * @param state The state of the SetAt observers
 * @param table The table
 */
static void box_app_set_at_observe_table(
    ecs_world_t *world,
    box_app_set_at_t *state,
    ecs_table_t *table)
{
    const ecs_type_t *type = ecs_table_get_type(table);
    int32_t i;
    for (i = 0; i < type->count; i ++) {
        ecs_id_t id = type->array[i];
        if (ECS_IS_PAIR(id) || id == ecs_id(SetAt) || id == ecs_id(SetAtWritten) ||
            !ecs_get_type_info(world, id) || box_app_set_at_has(&state->ids, id))
        {
            continue;
        }

        ecs_vec_append_t(NULL, &state->ids, ecs_id_t)[0] = id;
        ecs_observer(world, {
            .entity = ecs_entity(world, { .parent = state->scope }),
            .query = {
                .terms = {
                    { .id = id },
                    { .id = ecs_id(SetAtWritten), .inout = EcsInOutFilter }
                },
                .flags = EcsQueryMatchDisabled
            },
            .events = { EcsOnSet },
            .callback = BoxAppSetAtOnSet
        });
    }
}

/** Create the observers of the components of every table with SetAtWritten, when the world gets
 * its first SetAt entity.
 *
 * @param world The world
 * @param state The state of the SetAt observers
 */
static void box_app_set_at_observe(
    ecs_world_t *world,
    box_app_set_at_t *state)
{
    state->scope = ecs_entity(world, { .name = "AppSetAtOnSet", .parent = state->module });
    ecs_query_t *q = ecs_query(world, {
        .terms = {{ .id = ecs_id(SetAtWritten) }},
        .flags = EcsQueryMatchDisabled | EcsQueryMatchEmptyTables
    });
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        box_app_set_at_observe_table(world, state, it.table);
    }

    ecs_query_fini(q);
}

/** Delete the observers that record the writes to SetAt entities, when the world has no SetAt
 * entity left.
 *
 * @param world The world
 * @param state The state of the SetAt observers
 */
static void box_app_set_at_unobserve(
    ecs_world_t *world,
    box_app_set_at_t *state)
{
    ecs_entity_t scope = state->scope;
    state->scope = 0;
    ecs_vec_clear(&state->ids);
    if (scope) {
        ecs_delete(world, scope);
    }
}

/** Create the observers of the components of a new table with SetAtWritten, while the world has
 * SetAt entities. A table created before the first SetAt entity is covered when it is added.
 *
 * @param it The iterator of the observer
 */
static void BoxAppSetAtOnTable(
    ecs_iter_t *it)
{
    ecs_world_t *world = (ecs_world_t*)ecs_get_world(it->world);
    box_app_set_at_t *state = it->ctx;
    if (!ecs_is_fini(world) && state->scope) {
        box_app_set_at_observe_table(world, state, it->table);
    }
}

/** Add the non pair ids of a type to a list of ids, except those another type has.
 *
 * @param ids The list of ecs_id_t
 * @param type The type
 * @param except The type whose ids are not added, or NULL
 */
static void box_app_set_at_add_type(
    ecs_vec_t *ids,
    const ecs_type_t *type,
    const ecs_type_t *except)
{
    int32_t i, j;
    for (i = 0; i < type->count; i ++) {
        ecs_id_t id = type->array[i];
        bool skip = ECS_IS_PAIR(id) || box_app_set_at_has(ids, id);
        for (j = 0; except && j < except->count && !skip; j ++) {
            skip = except->array[j] == id;
        }

        if (!skip) {
            ecs_vec_append_t(NULL, ids, ecs_id_t)[0] = id;
        }
    }
}

/** Record the components a SetAt entity had before SetAt was added, as when a script body
 * writes SetAt after the other components, and the ones added with it.
 *
 * Count the SetAt entities: the first one added creates the observers of AppSetAtOnSet, the
 * last one removed or deleted deletes them.
 *
 * A stage merge adds the components the stage set on an entity with SetAt. A component with a
 * low id and no OnSet hook or observer when the stage queued the set (a light) gets no OnSet:
 * it is copied when it no longer holds its constructed value.
 *
 * @param it The iterator of the observer
 */
static void BoxAppSetAtOnAddRemove(
    ecs_iter_t *it)
{
    ecs_world_t *world = (ecs_world_t*)ecs_get_world(it->world);
    box_app_set_at_t *state = it->ctx;
    bool add = !ecs_is_fini(world) && it->event == EcsOnAdd;
    if (!ecs_is_fini(world) && it->event == EcsOnRemove) {
        state->count -= it->count;
        if (state->count <= 0) {
            state->count = 0;
            box_app_set_at_unobserve(world, state);
        }
    } else if (add) {
        state->count += it->count;
        if (!state->scope) {
            box_app_set_at_observe(world, state);
        }
    }

    const ecs_type_t *before = add && it->other_table ? ecs_table_get_type(it->other_table) : NULL;
    int32_t i;
    for (i = 0; i < it->count && add; i ++) {
        SetAtWritten *w = ecs_get_mut(it->world, it->entities[i], SetAtWritten);
        if (w) {
            if (before) {
                box_app_set_at_add_type(&w->before, before, NULL);
            }

            box_app_set_at_add_type(
                &w->batched, ecs_get_type(it->world, it->entities[i]), before);
        }
    }
}

/** Check whether a component holds the value it is constructed with.
 *
 * @param ti The type info of the component
 * @param value The value of the component
 * @return Whether value is the constructed value
 */
static bool box_app_set_at_constructed(
    const ecs_type_info_t *ti,
    const void *value)
{
    void *constructed = ecs_os_calloc(ti->size);
    if (ti->hooks.ctor) {
        ti->hooks.ctor(constructed, 1, ti);
    }

    bool result = !ecs_os_memcmp(constructed, value, ti->size);
    if (ti->hooks.dtor) {
        ti->hooks.dtor(constructed, 1, ti);
    }

    ecs_os_free(constructed);

    return result;
}

/** Check whether a component a SetAt entity had before SetAt was added is one the engine
 * added: another of its components adds it through the With trait (a material component adds
 * MaterialId, Rotation adds Position) and it still holds the value it was constructed with.
 *
 * Only these components are compared by value: they were written, if at all, before the
 * entity had SetAt.
 *
 * @param world The world
 * @param type The type of the entity
 * @param id The component
 * @param ti The type info of the component
 * @param value The value of the component
 * @return Whether the component is not copied
 */
static bool box_app_set_at_implied(
    const ecs_world_t *world,
    const ecs_type_t *type,
    ecs_id_t id,
    const ecs_type_info_t *ti,
    const void *value)
{
    bool result = false;
    int32_t i;
    for (i = 0; i < type->count && !result; i ++) {
        ecs_id_t other = type->array[i];
        result = !ECS_IS_PAIR(other) && other != id && ecs_has_pair(world, other, EcsWith, id);
    }

    return result && box_app_set_at_constructed(ti, value);
}

/** Copy the components written to a SetAt entity to its target entity: the ones set since it
 * has SetAt, the ones it had before, except those the engine added, and the ones added with
 * SetAt that no longer hold their constructed value.
 *
 * Pairs, tags, SetAt and the components only added are not copied.
 *
 * @param world The world
 * @param from The entity with SetAt
 * @param to The entity that receives the values
 */
static void box_app_set_at_copy(
    ecs_world_t *world,
    ecs_entity_t from,
    ecs_entity_t to)
{
    const SetAtWritten *w = ecs_get(world, from, SetAtWritten);
    const ecs_type_t *type = ecs_get_type(world, from);
    int32_t i;
    for (i = 0; i < type->count && w; i ++) {
        ecs_id_t id = type->array[i];
        bool written = box_app_set_at_has(&w->written, id);
        bool before = !written && box_app_set_at_has(&w->before, id);
        bool batched = !written && !before && box_app_set_at_has(&w->batched, id);
        const ecs_type_info_t *ti = (written || before || batched) && !ECS_IS_PAIR(id) &&
            id != ecs_id(SetAt) && id != ecs_id(SetAtWritten)
                ? ecs_get_type_info(world, id) : NULL;
        const void *value = ti ? ecs_get_id(world, from, id) : NULL;
        if (value && (written ||
            (before && !box_app_set_at_implied(world, type, id, ti, value)) ||
            (batched && !box_app_set_at_constructed(ti, value))))
        {
            ecs_set_id(world, to, id, (size_t)ti->size, value);
        }
    }
}

/** Set the values of the SetAt entities whose frame has come, then delete them.
 *
 * The entities are collected first, since setting the values and deleting the entities change
 * the tables the query iterates.
 *
 * @param it The iterator of the system
 */
static void BoxAppSetAt(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    const AppStatus *status = ecs_singleton_get(world, AppStatus);
    int32_t frame = status ? status->frame : 0;
    ecs_vec_t due = {0};
    int32_t i, count;
    while (ecs_query_next(it)) {
        const SetAt *s = ecs_field(it, SetAt, 0);
        for (i = 0; i < it->count; i ++) {
            if (frame >= s[i].frame) {
                ecs_vec_append_t(NULL, &due, ecs_entity_t)[0] = it->entities[i];
            }
        }
    }

    count = ecs_vec_count(&due);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = ecs_vec_get_t(&due, ecs_entity_t, i)[0];
        ecs_entity_t to = ecs_get(world, e, SetAt)->entity;
        if (to && ecs_is_alive(world, to)) {
            box_app_set_at_copy(world, e, to);
        } else {
            char *name = ecs_get_path(world, e);
            ecs_err("SetAt of '%s' has no entity", name);
            ecs_os_free(name);
        }

        ecs_delete(world, e);
    }

    ecs_vec_fini_t(NULL, &due, ecs_entity_t);
}

/** Add a sample to a series of FrameTiming.
 *
 * @param series The series
 * @param value The sample
 */
static void box_app_series_add(
    box_app_series_t *series,
    double value)
{
    series->values[series->next] = value;
    series->next = (series->next + 1) % BOX_FRAME_WINDOW;
    if (series->count < BOX_FRAME_WINDOW) {
        series->count ++;
    }
}

/** Compute the range of a series.
 *
 * @param series The series
 * @param range The range to fill, zero without samples
 * @return The sum of the samples
 */
static double box_app_series_range(
    const box_app_series_t *series,
    FrameRange *range)
{
    double sum = 0;
    int32_t i;
    *range = (FrameRange){0};
    for (i = 0; i < series->count; i ++) {
        double v = series->values[i];
        range->min = !i || v < range->min ? v : range->min;
        range->max = !i || v > range->max ? v : range->max;
        sum += v;
    }

    range->mean = series->count ? sum / (double)series->count : 0;

    return sum;
}

/** Get the slot of a ring of FrameSamples, from a slot that a script may have set out of range.
 *
 * @param slot The slot
 * @return The slot in 0..BOX_FRAME_WINDOW - 1
 */
static int32_t box_frame_slot(
    int32_t slot)
{
    int32_t result = slot % BOX_FRAME_WINDOW;

    return result < 0 ? result + BOX_FRAME_WINDOW : result;
}

void box_frame_samples_add(
    FrameSamples *samples,
    double interval_ms)
{
    int32_t slot = box_frame_slot(samples->next);
    samples->interval[slot] = interval_ms;
    samples->next = box_frame_slot(slot + 1);
    if (samples->count < 0) {
        samples->count = 0;
    }

    if (samples->count < BOX_FRAME_WINDOW) {
        samples->count ++;
    }
}

double box_frame_sample(
    const FrameSamples *samples,
    int32_t age)
{
    int32_t count = samples->count < BOX_FRAME_WINDOW ? samples->count : BOX_FRAME_WINDOW;
    double result = 0;
    if (age >= 0 && age < count) {
        result = samples->interval[box_frame_slot(samples->next - 1 - age)];
    }

    return result;
}

/** Get the clock of the frame loop of a world.
 *
 * @param world The world
 * @return The clock, NULL when box.app is not imported
 */
static box_app_clock_t* box_app_clock(
    const ecs_world_t *world)
{
    ecs_entity_t e = ecs_lookup(world, BOX_APP_TIMING_SYSTEM);
    const ecs_system_t *system = e ? ecs_system_get(world, e) : NULL;

    return system ? system->ctx : NULL;
}

/** Record the timing of the frame that starts: its delta, the wall time since the previous one
 * and the presents, and publish the ranges of the recent frames in FrameTiming.
 *
 * @param it The iterator of the system
 */
static void BoxAppTiming(
    ecs_iter_t *it)
{
    box_app_clock_t *clock = it->ctx;
    const App *app = ecs_singleton_get(it->world, App);
    const RenderStatus *render = ecs_id(RenderStatus)
        ? ecs_singleton_get(it->world, RenderStatus)
        : NULL;
    FrameTiming *timing = ecs_singleton_get_mut(it->world, FrameTiming);
    FrameSamples *samples = ecs_singleton_get_mut(it->world, FrameSamples);
    FrameRange counts;
    double now = box_app_wall_time();
    double delta = (double)ecs_get_world_info(it->world)->delta_time_raw * 1000.0;
    double interval = clock->begin > 0 ? (now - clock->begin) * 1000.0 : 0;
    double target = app && app->fps > 0 ? 1000.0 / (double)app->fps : 0;
    bool late, dropped;
    clock->begin = now;
    if (!timing || interval <= 0) {
        goto done;
    }

    late = fabs(delta - interval) > BOX_FRAME_TOLERANCE_MS;
    dropped = target > 0 && interval > target * BOX_FRAME_DROPPED;
    box_app_series_add(&clock->delta, delta);
    box_app_series_add(&clock->interval, interval);
    if (samples && !samples->hold) {
        box_frame_samples_add(samples, interval);
        ecs_singleton_modified(it->world, FrameSamples);
    }

    box_app_series_add(&clock->error, fabs(delta - interval));
    box_app_series_add(&clock->late, late);
    box_app_series_add(&clock->dropped, dropped);
    if (render && clock->presented > 0 && render->presented != clock->presented) {
        box_app_series_add(&clock->present, render->present_ms);
    }

    clock->presented = render ? render->presented : 0;
    timing->frames = clock->delta.count;
    timing->target_ms = target;
    timing->vsync = render && render->vsync;
    timing->delta_ms = delta;
    timing->interval_ms = interval;
    box_app_series_range(&clock->delta, &timing->delta);
    box_app_series_range(&clock->interval, &timing->interval);
    box_app_series_range(&clock->error, &timing->error);
    box_app_series_range(&clock->present, &timing->present);
    timing->late = (int32_t)box_app_series_range(&clock->late, &counts);
    timing->dropped = (int32_t)box_app_series_range(&clock->dropped, &counts);
    timing->total ++;
    timing->total_late += late;
    timing->total_dropped += dropped;
    ecs_singleton_modified(it->world, FrameTiming);
done:
    return;
}

/** Serve the REST API, which includes the shell endpoints, on a port.
 *
 * The browser cannot listen on a port and has no threads for the server, so the web build
 * serves nothing.
 *
 * @param world The world
 * @param port The port
 */
static void box_app_serve(
    ecs_world_t *world,
    int32_t port)
{
#ifdef __EMSCRIPTEN__
    (void)world;
    (void)port;
#else
    ECS_IMPORT(world, FlecsStats);
    ecs_enable(world, ecs_id(FlecsStats), true);
    ecs_singleton_set(world, EcsRest, { .port = (uint16_t)port });
#endif
}

char* box_app_locate(
    const char *path)
{
    char dir[4096];
    char *result = NULL;
    if (path[0] == '/' || !access(path, R_OK) || !getcwd(dir, sizeof(dir))) {
        goto done;
    }

    char *slash = NULL;
    do {
        result = flecs_asprintf("%s/%s", dir, path);
        if (!access(result, R_OK)) {
            goto done;
        }

        ecs_os_free(result);
        result = NULL;
        slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
        }
    } while (slash && dir[0]);

done:
    return result ? result : ecs_os_strdup(path);
}

/** Check the settings of a run and load its scene. ecs_script returns the entity of a managed
 * script also when it fails to parse or evaluate; both leave EcsScript without a script and
 * set its error, which tell whether the scene loaded.
 *
 * @param world The world
 * @return 0 when the world may run, nonzero when it may not
 */
static int box_app_start(
    ecs_world_t *world)
{
    const App *app = ecs_singleton_get(world, App);
    const AppStatus *status = ecs_singleton_get(world, AppStatus);
    int result = -1;
    if (!app || !status) {
        ecs_err("box.app is not imported");
        goto done;
    }

    if (app->headless && app->frames <= 0 && app->timeout == 0) {
        ecs_err("headless mode requires --frames or --timeout");
        goto done;
    }

    if (app->headless && !status->tmp_dir) {
        ecs_err("headless mode requires its temporary folder in %s", BOX_TMP_ROOT);
        goto done;
    }

    if (app->headless && !app->unguarded && box_app_guard(app->cpu_budget)) {
        goto done;
    }

    if (app->scene && app->scene[0]) {
        char *scene = box_app_locate(app->scene);
        ecs_entity_t script = ecs_script(world, {
            .entity = ecs_new_from_path_w_sep(world, 0, app->scene, "/", "/"),
            .filename = scene
        });
        const EcsScript *loaded = script ? ecs_get(world, script, EcsScript) : NULL;
        ecs_os_free(scene);
        if (!loaded || !loaded->script || loaded->error) {
            ecs_err("failed to load scene '%s'", app->scene);
            goto done;
        }
    }

    if (box_app_views(world)) {
        goto done;
    }

    app = ecs_singleton_get(world, App);
    if (app->port > 0) {
        box_app_serve(world, app->port);
    }

    box_app_target_fps(world, app);
    result = 0;
done:
    return result;
}

#ifdef __EMSCRIPTEN__
/** Run a frame from the animation frame callback of the browser and end the loop when the
 * world quits. The browser paces the frames, so the target fps of flecs stays 0.
 *
 * @param arg The clock of the frame loop
 */
static void box_app_web_frame(
    void *arg)
{
    box_app_clock_t *clock = arg;
    ecs_world_t *world = clock->world;
    const App *app = ecs_singleton_get(world, App);
    if (!ecs_progress(world, app ? app->delta_time : 0)) {
        ecs_singleton_get_mut(world, AppStatus)->running = false;
        emscripten_cancel_main_loop();
    }
}
#endif

int box_app_run(
    ecs_world_t *world)
{
    AppStatus *status;
    int result = box_app_start(world);
    if (result) {
        goto done;
    }

    status = ecs_singleton_get_mut(world, AppStatus);
    status->running = true;
    box_app_clock_t *clock = box_app_clock(world);
    clock->world = world;
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(box_app_web_frame, clock, 0, true);
#else
    while (ecs_progress(world, box_app_frame_delta(world))) { }
#endif

    status = ecs_singleton_get_mut(world, AppStatus);
    status->running = false;
done:
    return result;
}

void BoxAppImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxApp);

    ECS_META_COMPONENT(world, App);
    ECS_META_COMPONENT(world, AppStatus);
    ECS_META_COMPONENT(world, FrameRange);
    ECS_META_COMPONENT(world, FrameTiming);
    ECS_META_COMPONENT(world, FrameSamples);
    ecs_set_hooks(world, FrameSamples, { .ctor = flecs_default_ctor });
    box_owned_hooks(world, App, BOX_OWNED_STR(App, scene), BOX_OWNED_STR(App, camera),
        BOX_OWNED_STR(App, out));
    box_owned_hooks(world, AppStatus, BOX_OWNED_STR(AppStatus, tmp_dir));
    ECS_META_COMPONENT(world, FileWatch);
    ECS_META_COMPONENT(world, FileWatchStatus);
    box_owned_hooks(world, FileWatchStatus, BOX_OWNED_STR(FileWatchStatus, last));
    ECS_META_COMPONENT(world, SetAt);
    ECS_COMPONENT_DEFINE(world, SetAtWritten);
    ecs_set_hooks(world, SetAtWritten, {
        .ctor = flecs_default_ctor,
        .dtor = box_app_set_at_written_dtor,
        .move = box_app_set_at_written_move,
        .copy = box_app_set_at_written_copy
    });
    ecs_add_pair(world, ecs_id(SetAt), EcsWith, EcsDisabled);
    ecs_add_pair(world, ecs_id(SetAt), EcsWith, ecs_id(SetAtWritten));

    ecs_singleton_set(world, AppStatus, {0});
    ecs_singleton_set(world, FrameTiming, {0});
    ecs_singleton_add(world, FrameSamples);
    box_file_watch_init(world);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "AppOnSet" }),
        .query.terms = {{ .id = ecs_id(App) }},
        .events = { EcsOnSet },
        .callback = BoxAppOnSet
    });

    box_app_set_at_t *set_at = ecs_os_calloc_t(box_app_set_at_t);
    set_at->module = ecs_id(BoxApp);
    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "AppSetAtOnAdd" }),
        .query = {
            .terms = {{ .id = ecs_id(SetAt) }},
            .flags = EcsQueryMatchDisabled
        },
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = BoxAppSetAtOnAddRemove,
        .ctx = set_at,
        .ctx_free = box_app_set_at_free
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "AppSetAtOnTable" }),
        .query = {
            .terms = {{ .id = ecs_id(SetAtWritten) }},
            .flags = EcsQueryMatchDisabled
        },
        .events = { EcsOnTableCreate },
        .callback = BoxAppSetAtOnTable,
        .ctx = set_at
    });

    box_app_clock_t *clock = box_app_clock_new();
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "FrameTimingUpdate" }),
        .phase = EcsPreFrame,
        .run = BoxAppTiming,
        .ctx = clock,
        .ctx_free = box_app_clock_free
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "AppSetAt" }),
        .query = {
            .terms = {{ .id = ecs_id(SetAt) }},
            .flags = EcsQueryMatchDisabled
        },
        .phase = EcsPreFrame,
        .run = BoxAppSetAt,
        .immediate = true
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "AppFrame" }),
        .phase = EcsPostFrame,
        .run = BoxAppFrame,
        .ctx = clock
    });

    ecs_singleton_set(world, App, {
        .cpu_budget = 100,
        .port = BOX_APP_PORT
    });
}
