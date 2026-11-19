/**
 * @file geometry/validate.c
 * @brief On-validate hooks that report invalid paths, profiles, surfaces, and meshes.
 *
 * Flecs calls a hook with a NULL value when a prefab child is instantiated and the instance
 * child inherits the component instead of owning it. The hooks skip that call: the value is
 * validated, and reported, on the prefab child that owns it.
 */

#include "paths.h"
#include <math.h>
#include <stdio.h>

/** Check that an entity holds a usable 2D or 3D path.
 *
 * @param world The world
 * @param entity The path entity
 * @param three True to check Path3D, false to check Path2D
 * @param closed 1 if the path must be closed, 0 if it must be open, -1 if either
 * @param steps Steps of each curve segment, 0 for the default
 * @return NULL if the path is valid, otherwise a reason string
 */
static const char* box_validate_path(
    const ecs_world_t *world,
    ecs_entity_t entity,
    bool three,
    int32_t closed,
    int32_t steps)
{
    const char *reason = NULL;
    ecs_vec_t points = {0};
    if (!entity || !ecs_is_alive(world, entity) ||
        !(three ? ecs_has(world, entity, Path3D) : ecs_has(world, entity, Path2D)))
    {
        reason = three ? "missing Path3D with at least one segment" :
            "missing Path2D with at least one segment";
        goto out;
    }

    bool ok = three ? box_path3_sample_steps(world, entity, steps, &points) :
        box_path2_sample_steps(world, entity, steps, &points);
    if (!ok) {
        reason = "path has no usable segments or contains invalid coordinates";
        goto done;
    }

    bool path_closed = three ? ecs_get(world, entity, Path3D)->closed :
        ecs_get(world, entity, Path2D)->closed;
    if (closed >= 0 && path_closed != (bool)closed) {
        reason = closed ? "profile path must be closed" : "contour path must be open";
        goto done;
    }

    int32_t count = ecs_vec_count(&points);
    const float *data = ecs_vec_first(&points);
    int32_t dimensions = three ? 3 : 2;
    bool span = false;
    for (int32_t i = 1; i < count && !span; i ++) {
        for (int32_t j = 0; j < dimensions; j ++) {
            span |= data[i * dimensions + j] != data[j];
        }
    }

    if (!span) {
        reason = "path is degenerate; move a point away from the start";
        goto done;
    }

    if (closed == 1 && !three) {
        if (count < 3) {
            reason = "closed path needs at least three distinct points";
            goto done;
        }

        double area = 0;
        for (int32_t i = 0; i < count; i ++) {
            int32_t next = (i + 1) % count;
            area += (double)data[2 * i] * data[2 * next + 1] -
                (double)data[2 * next] * data[2 * i + 1];
        }

        if (fabs(area) < 1e-8) {
            reason = "closed path has zero area; correct its footprint";
        }
    }
done:
    if (three) {
        ecs_vec_fini_t(NULL, &points, vec3);
    } else {
        ecs_vec_fini_t(NULL, &points, vec2);
    }
out:
    return reason;
}

/** Check that an entity holds a Profile2D whose outer path and holes are valid.
 *
 * @param world The world
 * @param entity The profile entity
 * @param steps Steps of each curve segment, 0 for the default
 * @return NULL if the profile is valid, otherwise a reason string
 */
static const char* box_validate_profile(
    const ecs_world_t *world,
    ecs_entity_t entity,
    int32_t steps)
{
    const Profile2D *profile = entity && ecs_is_alive(world, entity) ?
        ecs_get(world, entity, Profile2D) : NULL;
    const char *reason = NULL;
    if (!profile) {
        reason = "missing Profile2D with a closed outer Path2D";
    } else {
        reason = box_validate_path(world, profile->outer, false, true, steps);
        const ecs_entity_t *holes = ecs_vec_first(&profile->holes);
        for (int32_t i = 0; !reason && i < ecs_vec_count(&profile->holes); i ++) {
            reason = box_validate_path(world, holes[i], false, true, steps);
        }
    }

    return reason;
}

/** Log a validation error for an entity if a reason is set.
 *
 * @param world The world
 * @param entity The entity being validated
 * @param status Status word printed before the component name
 * @param component Name of the validated component
 * @param reason The failure reason, or NULL to report nothing
 */
static void box_validate_report_status(
    const ecs_world_t *world,
    ecs_entity_t entity,
    const char *status,
    const char *component,
    const char *reason)
{
    if (reason) {
        char *path = ecs_get_path(world, entity);
        ecs_err("%s %s on %s: %s", status, component, path, reason);
        ecs_os_free(path);
    }
}

/** Log an "invalid" validation error for an entity if a reason is set.
 *
 * @param world The world
 * @param entity The entity being validated
 * @param component Name of the validated component
 * @param reason The failure reason, or NULL to report nothing
 */
static void box_validate_report(
    const ecs_world_t *world,
    ecs_entity_t entity,
    const char *component,
    const char *reason)
{
    box_validate_report_status(world, entity, "invalid", component, reason);
}

/** Validate hook for Path2D.
 *
 * @param world The world
 * @param entity The entity being validated
 * @param ptr Pointer to the component value
 * @return Always true; problems are only logged
 */
static bool box_validate_path2_hook(
    ecs_world_t *world,
    ecs_entity_t entity,
    void *ptr)
{
    const Path2D *path = ptr;
    if (!path) {
        goto done;
    }

    box_validate_report(world, entity, "Path2D",
        box_validate_path(world, entity, false, path->closed, 0));
done:
    return true;
}

/** Validate hook for Path3D.
 *
 * @param world The world
 * @param entity The entity being validated
 * @param ptr Pointer to the component value
 * @return Always true; problems are only logged
 */
static bool box_validate_path3_hook(
    ecs_world_t *world,
    ecs_entity_t entity,
    void *ptr)
{
    const Path3D *path = ptr;
    if (!path) {
        goto done;
    }

    box_validate_report(world, entity, "Path3D",
        box_validate_path(world, entity, true, path->closed, 0));
done:
    return true;
}

/** Validate hook for Profile2D.
 *
 * @param world The world
 * @param entity The entity being validated
 * @param ptr Pointer to the component value
 * @return Always true; problems are only logged
 */
static bool box_validate_profile_hook(
    ecs_world_t *world,
    ecs_entity_t entity,
    void *ptr)
{
    if (ptr) {
        box_validate_report(world, entity, "Profile2D", box_validate_profile(world, entity, 0));
    }

    return true;
}

/** Validate hook for Sweep.
 *
 * @param world The world
 * @param entity The entity being validated
 * @param ptr Pointer to the component value
 * @return Always true; problems are only logged
 */
static bool box_validate_sweep_hook(
    ecs_world_t *world,
    ecs_entity_t entity,
    void *ptr)
{
    const Sweep *sweep = ptr;
    if (!sweep) {
        goto done;
    }

    const char *reason = NULL;
    if (!!sweep->profile == !!sweep->contour) {
        reason = "set exactly one of profile or contour";
    } else {
        reason = box_validate_path(world, sweep->path, true, -1, 0);
        if (!reason) {
            reason = sweep->profile ?
                box_validate_profile(world, sweep->profile, sweep->segments) :
                box_validate_path(world, sweep->contour, false, false, sweep->segments);
        }
    }

    if (!reason && (!isfinite(sweep->scale[0]) || !isfinite(sweep->scale[1]) ||
        !isfinite(sweep->taper) || !isfinite(sweep->roll)))
    {
        reason = "scale, taper, and roll must be finite";
    }

    box_validate_report(world, entity, "Sweep", reason);
done:
    return true;
}

/** Validate hook for Loft.
 *
 * @param world The world
 * @param entity The entity being validated
 * @param ptr Pointer to the component value
 * @return Always true; problems are only logged
 */
static bool box_validate_loft_hook(
    ecs_world_t *world,
    ecs_entity_t entity,
    void *ptr)
{
    const Loft *loft = ptr;
    if (!loft) {
        goto done;
    }

    int32_t count = ecs_vec_count(&loft->sections);
    const char *reason = count < 2 ? "at least two sections are required" : NULL;
    const LoftSection *sections = ecs_vec_first(&loft->sections);
    for (int32_t i = 0; !reason && i < count; i ++) {
        reason = box_validate_profile(world, sections[i].profile, loft->segments);
        if (!reason) {
            for (int32_t j = 0; j < 3; j ++) {
                if (!isfinite(sections[i].offset[j]) || !isfinite(sections[i].rotation[j])) {
                    reason = "section offsets and rotations must be finite";
                }
            }

            if (!isfinite(sections[i].scale[0]) || !isfinite(sections[i].scale[1])) {
                reason = "section scale must be finite";
            }
        }
    }

    box_validate_report(world, entity, "Loft", reason);
done:
    return true;
}

/** Validate hook for Lathe.
 *
 * @param world The world
 * @param entity The entity being validated
 * @param ptr Pointer to the component value
 * @return Always true; problems are only logged
 */
static bool box_validate_lathe_hook(
    ecs_world_t *world,
    ecs_entity_t entity,
    void *ptr)
{
    const Lathe *lathe = ptr;
    if (!lathe) {
        goto done;
    }

    const char *reason = box_validate_path(world, lathe->profile, false, -1, 0);
    if (!reason && lathe->inner) {
        reason = box_validate_path(world, lathe->inner, false, -1, 0);
    }

    box_validate_report(world, entity, "Lathe", reason);
done:
    return true;
}

/** Validate hook for ProfileExtrusion.
 *
 * @param world The world
 * @param entity The entity being validated
 * @param ptr Pointer to the component value
 * @return Always true; problems are only logged
 */
static bool box_validate_extrusion_hook(
    ecs_world_t *world,
    ecs_entity_t entity,
    void *ptr)
{
    const ProfileExtrusion *extrusion = ptr;
    if (!extrusion) {
        goto done;
    }

    const char *reason = box_validate_profile(world, extrusion->profile, 0);
    if (!reason && !isfinite(extrusion->depth)) {
        reason = "depth must be finite";
    }

    box_validate_report(world, entity, "ProfileExtrusion", reason);
done:
    return true;
}

/** Validate hook for BeveledPrism.
 *
 * @param world The world
 * @param entity The entity being validated
 * @param ptr Pointer to the component value
 * @return Always true; problems are only logged
 */
static bool box_validate_prism_hook(
    ecs_world_t *world,
    ecs_entity_t entity,
    void *ptr)
{
    const BeveledPrism *prism = ptr;
    if (!prism) {
        goto done;
    }

    int32_t count = ecs_vec_count(&prism->footprint);
    const char *reason = NULL;
    if (count != 3 && count != 4) {
        reason = "footprint needs 3 or 4 vertices";
    } else {
        const vec2 *points = ecs_vec_first(&prism->footprint);
        float sign = 0;
        for (int32_t i = 0; i < count; i ++) {
            const float *a = points[i], *b = points[(i + 1) % count];
            const float *c = points[(i + 2) % count];
            float cross = (b[0] - a[0]) * (c[1] - b[1]) -
                (b[1] - a[1]) * (c[0] - b[0]);
            if (!isfinite(cross) || fabsf(cross) < 0.000001f || (i && sign * cross < 0)) {
                reason = "footprint must be finite, convex, and nondegenerate";
                break;
            }

            sign = cross;
        }
    }

    if (!reason && (!isfinite(prism->depth) || !isfinite(prism->inset) ||
        !isfinite(prism->bevel) || !isfinite(prism->offset[0]) ||
        !isfinite(prism->offset[1])))
    {
        reason = "depth, inset, bevel, and offset must be finite";
    }

    box_validate_report(world, entity, "BeveledPrism", reason);
done:
    return true;
}

/** Validate hook for Bevel that reports radii that are negative or not finite.
 *
 * A Bevel without a shape it rounds is not reported: a script can set the Bevel before the
 * shape.
 *
 * @param world The world
 * @param entity The entity being validated
 * @param ptr Pointer to the component value
 * @return Always true; problems are only logged
 */
static bool box_validate_bevel_hook(
    ecs_world_t *world,
    ecs_entity_t entity,
    void *ptr)
{
    const Bevel *bevel = ptr;
    if (!bevel) {
        goto done;
    }

    const float radii[] = {
        bevel->radius,
        bevel->top_front, bevel->top_back, bevel->top_left, bevel->top_right,
        bevel->bottom_front, bevel->bottom_back, bevel->bottom_left, bevel->bottom_right,
        bevel->front_left, bevel->front_right, bevel->back_left, bevel->back_right,
        bevel->top, bevel->bottom
    };
    const char *reason = NULL;
    for (size_t i = 0; !reason && i < sizeof(radii) / sizeof(radii[0]); i ++) {
        if (!isfinite(radii[i])) {
            reason = "radii must be finite";
        } else if (radii[i] < 0.0f) {
            reason = "radii must not be negative";
        }
    }

    box_validate_report(world, entity, "Bevel", reason);
done:
    return true;
}

/** Validate hook for ShapeMesh that reports empty or invalid meshes.
 *
 * @param world The world
 * @param entity The entity being validated
 * @param ptr Pointer to the component value
 * @return Always true; problems are only logged
 */
static bool box_validate_mesh_hook(
    ecs_world_t *world,
    ecs_entity_t entity,
    void *ptr)
{
    const ShapeMesh *shape = ptr;
    if (!shape) {
        goto done;
    }

    const box_mesh_cache_t *cache = box_mesh_cache_of(world);
    const box_mesh_t *mesh = shape->mesh ? box_mesh_cache_get(cache, shape->mesh) : NULL;
    if (!mesh) {
        goto done;
    }

    if (!mesh->vertex_count || mesh->index_count < 3) {
        box_validate_report_status(world, entity, "empty", "ShapeMesh",
            "mesh has no triangles");
        goto done;
    }

    const box_mesh_t *render = shape->render_mesh ?
        box_mesh_cache_get(cache, shape->render_mesh) : mesh;
    if (!render || render->index_count < 3) {
        box_validate_report_status(world, entity, "empty", "ShapeMesh",
            "resolved mesh has no triangles");
        goto done;
    }

    for (int32_t j = 0; j < 3; j ++) {
        if (!isfinite(shape->scale[j])) {
            box_validate_report(world, entity, "ShapeMesh", "scale must be finite");
            goto done;
        }
    }

    int32_t visible = 0;
    for (int32_t i = 0; i + 2 < render->index_count; i += 3) {
        uint32_t a = render->indices[i], b = render->indices[i + 1];
        uint32_t c = render->indices[i + 2];
        if (a >= (uint32_t)render->vertex_count || b >= (uint32_t)render->vertex_count ||
            c >= (uint32_t)render->vertex_count)
        {
            box_validate_report(world, entity, "ShapeMesh", "mesh index is out of range");
            goto done;
        }

        vec3 ab, ac, normal;
        for (int32_t j = 0; j < 3; j ++) {
            ab[j] = (render->vertices[b].position[j] -
                render->vertices[a].position[j]) * shape->scale[j];
            ac[j] = (render->vertices[c].position[j] -
                render->vertices[a].position[j]) * shape->scale[j];
        }

        glm_vec3_cross(ab, ac, normal);
        float area = glm_vec3_norm2(normal);
        if (!isfinite(area)) {
            box_validate_report(world, entity, "ShapeMesh", "mesh vertices must be finite");
            goto done;
        }

        visible += area > 1e-20f;
    }

    if (!visible) {
        box_validate_report_status(world, entity, "empty", "ShapeMesh",
            "all triangles collapse after shape scale; check dimensions and paths");
    }
done:
    return true;
}

/** Check that a per vertex member of a Mesh3 is empty or has one value per vertex.
 *
 * @param member The member
 * @param vertices Number of vertices
 * @param name Name of the member
 * @param reason Buffer the reason is written to
 * @param size Bytes of the buffer
 * @return The reason, NULL when the member is valid
 */
static const char* box_validate_mesh3_member(
    const ecs_vec_t *member,
    int32_t vertices,
    const char *name,
    char *reason,
    size_t size)
{
    const char *result = NULL;
    int32_t count = ecs_vec_count(member);
    if (count && count != vertices) {
        snprintf(reason, size, "%s has %d values for %d vertices; give one per vertex or none, "
            "it is ignored", name, count, vertices);
        result = reason;
    }

    return result;
}

/** Validate hook for Mesh3: the colors, data0 and data1 are empty or have one value per vertex.
 *
 * @param world The world
 * @param entity The entity being validated
 * @param ptr Pointer to the component value
 * @return Always true; problems are only logged
 */
static bool box_validate_mesh3_hook(
    ecs_world_t *world,
    ecs_entity_t entity,
    void *ptr)
{
    const Mesh3 *mesh = ptr;
    char buf[160];
    if (!mesh) {
        goto done;
    }

    int32_t vertices = ecs_vec_count(&mesh->vertices);
    const char *reason = box_validate_mesh3_member(&mesh->colors, vertices, "colors",
        buf, sizeof(buf));
    if (!reason) {
        reason = box_validate_mesh3_member(&mesh->data0, vertices, "data0", buf, sizeof(buf));
    }

    if (!reason) {
        reason = box_validate_mesh3_member(&mesh->data1, vertices, "data1", buf, sizeof(buf));
    }

    box_validate_report(world, entity, "Mesh3", reason);
done:
    return true;
}

void box_geometry_validation_init(
    ecs_world_t *world)
{
    ecs_set_hooks(world, Path2D, {.on_validate = box_validate_path2_hook});
    ecs_set_hooks(world, Path3D, {.on_validate = box_validate_path3_hook});
    ecs_set_hooks(world, Profile2D, {.on_validate = box_validate_profile_hook});
    ecs_set_hooks(world, Sweep, {.on_validate = box_validate_sweep_hook});
    ecs_set_hooks(world, Loft, {.on_validate = box_validate_loft_hook});
    ecs_set_hooks(world, Lathe, {.on_validate = box_validate_lathe_hook});
    ecs_set_hooks(world, ProfileExtrusion, {.on_validate = box_validate_extrusion_hook});
    ecs_set_hooks(world, BeveledPrism, {.on_validate = box_validate_prism_hook});
    ecs_set_hooks(world, Bevel, {.on_validate = box_validate_bevel_hook});
    ecs_set_hooks(world, ShapeMesh, {.on_validate = box_validate_mesh_hook});
    ecs_set_hooks(world, Mesh3, {.on_validate = box_validate_mesh3_hook});
}
