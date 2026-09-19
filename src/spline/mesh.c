/**
 * @file spline/mesh.c
 * @brief Sweeps the cross section of a SplineMesh along its curve into a Mesh3.
 *
 * The cross section is laid out once as a list of columns, a point with the
 * normal of the surface at it, and copied along the curve at even arc length.
 * The mesh is rebuilt when the component or the curve changes, never per frame,
 * so the entity stays a static instance.
 */

#include "spline_internal.h"
#include <math.h>

/** Slices a sweep takes per curve segment when the component names none */
#define BOX_SPLINE_MESH_SLICES (8)

/** Shortest vector that still gives a direction */
#define BOX_SPLINE_MESH_EPSILON (1e-6f)

/** Cross section of a sweep without a profile: a flat strip one unit wide */
static const vec2 box_spline_mesh_strip[2] = {
    {-0.5f, 0.0f}, {0.5f, 0.0f}
};

/** One column of a sweep: a cross section point with the normal at it */
typedef struct box_spline_column_t {
    vec2 point;  /**< Scaled cross section point, x across the curve and y up */
    vec2 normal; /**< Unit normal of the surface at the point */
    float u;     /**< Distance along the cross section, the u of the mesh */
} box_spline_column_t;

/** The layout of one sweep, worked out before its mesh is filled */
typedef struct box_spline_sweep_t {
    box_spline_column_t *columns; /**< Columns of the cross section */
    int32_t column_count;         /**< Number of columns */
    int32_t edges;                /**< Cross section edges, one quad strip each */
    int32_t slices;               /**< Quad strips along the curve */
    int32_t rings;                /**< Copies of the cross section along the curve */
    bool smooth;                  /**< Columns are shared by neighbouring edges */
} box_spline_sweep_t;

/** Scale a cross section direction to unit length.
 *
 * @param v The direction
 * @return The unit direction, up when the direction is too short to tell
 */
static vec2 box_spline_mesh_unit(
    vec2 v)
{
    float length = sqrtf(v.x * v.x + v.y * v.y);
    vec2 result = {0.0f, 1.0f};
    if (length > BOX_SPLINE_MESH_EPSILON) {
        result.x = v.x / length;
        result.y = v.y / length;
    }

    return result;
}

/** Normal of a cross section edge, a quarter turn counter clockwise from it.
 *
 * @param from Start of the edge, x across the curve and y up
 * @param to End of the edge
 * @return The unit normal of the surface the edge sweeps
 */
static vec2 box_spline_mesh_edge_normal(
    vec3 from,
    vec3 to)
{
    vec2 normal = {from.y - to.y, to.x - from.x};

    return box_spline_mesh_unit(normal);
}

/** Scale the cross section of a component and measure it along its length.
 *
 * @param sm The component
 * @param profile The cross section points
 * @param points Number of points
 * @param out Receives the scaled points and their distance, one past the end
 */
static void box_spline_mesh_scale(
    const SplineMesh *sm,
    const vec2 *profile,
    int32_t points,
    vec3 *out)
{
    float distance = 0;
    float dx, dy;
    int32_t i;
    for (i = 0; i < points; i ++) {
        out[i].x = profile[i].x * sm->width;
        out[i].y = profile[i].y * sm->height;
        if (i) {
            dx = out[i].x - out[i - 1].x;
            dy = out[i].y - out[i - 1].y;
            distance += sqrtf(dx * dx + dy * dy);
        }

        out[i].z = distance;
    }

    dx = out[0].x - out[points - 1].x;
    dy = out[0].y - out[points - 1].y;
    out[points].x = out[0].x;
    out[points].y = out[0].y;
    out[points].z = distance + sqrtf(dx * dx + dy * dy);
}

/** Fill the columns of a sweep from its scaled cross section.
 *
 * A sweep with shared normals holds one column per point and adds the normal of
 * an edge to the two columns it runs between; a faceted sweep holds two columns
 * per edge, which is what keeps the normals of the edges apart.
 *
 * @param sweep The layout, its columns are allocated
 * @param scaled The scaled cross section, one point past the end for the wrap
 * @param points Number of cross section points
 */
static void box_spline_mesh_columns(
    box_spline_sweep_t *sweep,
    const vec3 *scaled,
    int32_t points)
{
    box_spline_column_t *columns = sweep->columns;
    int32_t j;

    if (sweep->smooth) {
        for (j = 0; j < points; j ++) {
            columns[j].point.x = scaled[j].x;
            columns[j].point.y = scaled[j].y;
            columns[j].normal.x = 0;
            columns[j].normal.y = 0;
            columns[j].u = scaled[j].z;
        }
    }

    for (j = 0; j < sweep->edges; j ++) {
        vec2 normal = box_spline_mesh_edge_normal(scaled[j], scaled[j + 1]);

        if (sweep->smooth) {
            int32_t b = (j + 1) % points;

            columns[j].normal.x += normal.x;
            columns[j].normal.y += normal.y;
            columns[b].normal.x += normal.x;
            columns[b].normal.y += normal.y;
        } else {
            box_spline_column_t *lo = &columns[j * 2];
            box_spline_column_t *hi = &columns[j * 2 + 1];

            lo->point.x = scaled[j].x;
            lo->point.y = scaled[j].y;
            lo->normal = normal;
            lo->u = scaled[j].z;
            hi->point.x = scaled[j + 1].x;
            hi->point.y = scaled[j + 1].y;
            hi->normal = normal;
            hi->u = scaled[j + 1].z;
        }
    }

    for (j = 0; sweep->smooth && j < points; j ++) {
        columns[j].normal = box_spline_mesh_unit(columns[j].normal);
    }
}

/** Work out the layout of a sweep and build its columns.
 *
 * @param curve The curve to sweep along, NULL when the entity holds none
 * @param table The arc length table of the curve, NULL with the curve
 * @param sm The component
 * @param sweep Receives the layout, its columns are allocated on success
 * @return true when the sweep has geometry, false when there is none to build
 */
static bool box_spline_mesh_plan(
    const Spline *curve,
    const SplineTable *table,
    const SplineMesh *sm,
    box_spline_sweep_t *sweep)
{
    const vec2 *profile = ecs_vec_first_t(&sm->profile, vec2);
    int32_t points = ecs_vec_count(&sm->profile);
    bool result = false;
    if (!curve || !table || table->length <= 0) {
        goto done;
    }

    if (points < 2) {
        profile = box_spline_mesh_strip;
        points = 2;
    }

    sweep->smooth = sm->smooth;
    sweep->slices = sm->segments > 0 ?
        sm->segments : table->segments * BOX_SPLINE_MESH_SLICES;
    if (sweep->slices < 1) {
        goto done;
    }

    bool closed = sm->closed && points > 2;
    vec3 *scaled = ecs_os_malloc_n(vec3, points + 1);

    sweep->rings = curve->loop ? sweep->slices : sweep->slices + 1;
    sweep->edges = closed ? points : points - 1;
    sweep->column_count = sweep->smooth ? points : sweep->edges * 2;
    sweep->columns = ecs_os_malloc_n(box_spline_column_t, sweep->column_count);
    box_spline_mesh_scale(sm, profile, points, scaled);
    box_spline_mesh_columns(sweep, scaled, points);
    ecs_os_free(scaled);
    result = true;
done:
    return result;
}

/** Write one ring of a sweep: the cross section placed on a point of the curve.
 *
 * @param sweep The layout
 * @param sample The point of the curve the ring sits on
 * @param out The mesh being filled
 * @param first Index of the first vertex of the ring
 * @param v Texture coordinate along the curve, the distance of the ring
 */
static void box_spline_mesh_ring(
    const box_spline_sweep_t *sweep,
    const box_spline_sample_t *sample,
    Mesh3 *out,
    int32_t first,
    float v)
{
    vec3 *vertices = ecs_vec_first_t(&out->vertices, vec3);
    vec3 *normals = ecs_vec_first_t(&out->normals, vec3);
    vec2 *uvs = ecs_vec_first_t(&out->uvs, vec2);
    vec3 right = vec3_normalize(vec3_cross(sample->normal, sample->tangent));
    int32_t c;
    for (c = 0; c < sweep->column_count; c ++) {
        const box_spline_column_t *column = &sweep->columns[c];
        vec3 across = vec3_scale(right, column->point.x);
        vec3 up = vec3_scale(sample->normal, column->point.y);
        vec3 normal = vec3_add(vec3_scale(right, column->normal.x),
            vec3_scale(sample->normal, column->normal.y));

        vertices[first + c] = vec3_add(sample->position, vec3_add(across, up));
        normals[first + c] = vec3_normalize(normal);
        uvs[first + c].x = column->u;
        uvs[first + c].y = v;
    }
}

/** Write the triangles that stitch the rings of a sweep together.
 *
 * @param sweep The layout
 * @param out The mesh being filled
 */
static void box_spline_mesh_stitch(
    const box_spline_sweep_t *sweep,
    Mesh3 *out)
{
    uint32_t *indices = ecs_vec_first_t(&out->indices, uint32_t);
    int32_t columns = sweep->column_count;
    int32_t next = 0;
    int32_t i, j;
    for (i = 0; i < sweep->slices; i ++) {
        int32_t lo = i * columns;
        int32_t hi = ((i + 1) % sweep->rings) * columns;

        for (j = 0; j < sweep->edges; j ++) {
            int32_t a = sweep->smooth ? j : j * 2;
            int32_t b = sweep->smooth ? (j + 1) % columns : j * 2 + 1;

            indices[next ++] = (uint32_t)(lo + a);
            indices[next ++] = (uint32_t)(hi + b);
            indices[next ++] = (uint32_t)(lo + b);
            indices[next ++] = (uint32_t)(lo + a);
            indices[next ++] = (uint32_t)(hi + a);
            indices[next ++] = (uint32_t)(hi + b);
        }
    }
}

/** Build the mesh of a sweep into the streams of a Mesh3.
 *
 * @param curve The curve to sweep along
 * @param table The arc length table of the curve
 * @param sweep The layout
 * @param out The mesh to overwrite
 */
static void box_spline_mesh_fill(
    const Spline *curve,
    const SplineTable *table,
    const box_spline_sweep_t *sweep,
    Mesh3 *out)
{
    int32_t vertices = sweep->rings * sweep->column_count;
    int32_t indices = sweep->slices * sweep->edges * 6;
    box_spline_sample_t sample;
    int32_t i;

    ecs_vec_init_if_t(&out->vertices, vec3);
    ecs_vec_init_if_t(&out->normals, vec3);
    ecs_vec_init_if_t(&out->uvs, vec2);
    ecs_vec_init_if_t(&out->indices, uint32_t);
    ecs_vec_set_count_t(NULL, &out->vertices, vec3, vertices);
    ecs_vec_set_count_t(NULL, &out->normals, vec3, vertices);
    ecs_vec_set_count_t(NULL, &out->uvs, vec2, vertices);
    ecs_vec_set_count_t(NULL, &out->indices, uint32_t, indices);

    for (i = 0; i < sweep->rings; i ++) {
        float distance = table->length * (float)i / (float)sweep->slices;

        box_spline_eval(curve, box_spline_table_param(table, distance), &sample);
        box_spline_mesh_ring(sweep, &sample, out, i * sweep->column_count, distance);
    }

    box_spline_mesh_stitch(sweep, out);
}

/** Release the streams a sweep filled.
 *
 * @param mesh The mesh
 */
static void box_spline_mesh_streams_fini(
    Mesh3 *mesh)
{
    ecs_vec_fini_t(NULL, &mesh->vertices, vec3);
    ecs_vec_fini_t(NULL, &mesh->normals, vec3);
    ecs_vec_fini_t(NULL, &mesh->uvs, vec2);
    ecs_vec_fini_t(NULL, &mesh->indices, uint32_t);
}

/** Rebuild the mesh of one sweep and hand it to its entity.
 *
 * A sweep that has nothing to build loses its mesh, which is what makes a road
 * disappear with the curve it was swept along.
 *
 * @param world The world
 * @param stage The stage a mesh the entity does not hold yet is added through
 * @param e The entity holding the sweep
 * @param sm The sweep
 * @param curve The curve to sweep along, NULL when there is none
 * @param table The arc length table of the curve, NULL with the curve
 */
static void box_spline_mesh_apply(
    ecs_world_t *world,
    ecs_world_t *stage,
    ecs_entity_t e,
    const SplineMesh *sm,
    const Spline *curve,
    const SplineTable *table)
{
    box_spline_sweep_t sweep = {0};
    Mesh3 *existing = NULL;
    if (!box_spline_mesh_plan(curve, table, sm, &sweep)) {
        if (ecs_has(world, e, Mesh3)) {
            ecs_remove(stage, e, Mesh3);
        }

        goto done;
    }

    existing = ecs_get_mut(world, e, Mesh3);
    if (existing) {
        box_spline_mesh_fill(curve, table, &sweep, existing);
        ecs_modified(world, e, Mesh3);
    } else {
        Mesh3 mesh = {0};

        box_spline_mesh_fill(curve, table, &sweep, &mesh);
        ecs_set_ptr(stage, e, Mesh3, &mesh);
        box_spline_mesh_streams_fini(&mesh);
    }

done:
    ecs_os_free(sweep.columns);

    return;
}

/** SplineMesh observer: the sweep changed, so its mesh is built again.
 *
 * @param it Iterator over the changed sweeps
 */
static void box_spline_mesh_on_set(
    ecs_iter_t *it)
{
    const SplineMesh *sm = ecs_field(it, SplineMesh, 0);
    bool self = ecs_field_is_self(it, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        const SplineMesh *mesh = self ? &sm[i] : sm;
        const SplineTable *table = NULL;
        const Spline *curve = box_spline_get(it->world,
            mesh->spline ? mesh->spline : e, &table);

        box_spline_mesh_apply(it->world, it->stage, e, mesh, curve, table);
    }
}

/** An entity lost its SplineMesh: the mesh that was swept goes with it.
 *
 * @param it Iterator over the entities that lost the component
 */
static void box_spline_mesh_on_remove(
    ecs_iter_t *it)
{
    bool deleting = !it->other_table || !ecs_table_get_type(it->other_table)->count ||
        ecs_is_fini(it->world) || ecs_should_quit(it->world);
    int32_t i;
    for (i = 0; !deleting && i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];

        if (ecs_has(it->world, e, Mesh3)) {
            ecs_remove(it->stage, e, Mesh3);
        }
    }
}

void box_spline_mesh_invalidate(
    ecs_world_t *world,
    ecs_world_t *stage,
    ecs_entity_t spline,
    const Spline *curve,
    const SplineTable *table)
{
    ecs_iter_t it = ecs_each(world, SplineMesh);
    ecs_vec_t users;
    int32_t i;

    ecs_vec_init_t(NULL, &users, ecs_entity_t, 0);
    while (ecs_each_next(&it)) {
        const SplineMesh *sm = ecs_field(&it, SplineMesh, 0);

        for (i = 0; i < it.count; i ++) {
            ecs_entity_t bound = sm[i].spline ? sm[i].spline : it.entities[i];

            if (bound == spline) {
                ecs_vec_append_t(NULL, &users, ecs_entity_t)[0] = it.entities[i];
            }
        }
    }

    for (i = 0; i < ecs_vec_count(&users); i ++) {
        ecs_entity_t e = ecs_vec_get_t(&users, ecs_entity_t, i)[0];
        const SplineMesh *sm = ecs_get(world, e, SplineMesh);

        if (sm) {
            box_spline_mesh_apply(world, stage, e, sm, curve, table);
        }
    }

    ecs_vec_fini_t(NULL, &users, ecs_entity_t);
}

void box_spline_mesh_init(
    ecs_world_t *world)
{
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(SplineMesh), .inout = EcsIn }},
        .events = { EcsOnSet },
        .callback = box_spline_mesh_on_set
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(SplineMesh), .inout = EcsIn }},
        .events = { EcsOnRemove },
        .callback = box_spline_mesh_on_remove
    });
}
