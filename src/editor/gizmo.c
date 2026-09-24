/**
 * @file editor/gizmo.c
 * @brief The transform gizmos: drawing them over the selection and the math of a drag.
 *
 * The gizmo of the tool is drawn through box.draw every frame at the world
 * position of the selection, sized by its distance to the camera, and every
 * tool draws a box around the bounds of the selection.
 *
 * A handle that is pressed records where the drag started; every drag maps the
 * cursor ray back onto the axis or the plane of the handle and moves the selection.
 */

#include "editor_internal.h"
#include "../render/render_internal.h"
#include <string.h>

/** Radians one unit of normalized cursor movement turns a free rotation */
#define BOX_EDITOR_TURN_RATE (3.0f)

/** Smallest scale a drag leaves */
#define BOX_EDITOR_SCALE_MIN (0.01f)

/** Smallest cosine between the cursor ray and a rotation plane that reads the angle from it */
#define BOX_EDITOR_GRAZING (0.25f)

/** Rotation about an arbitrary axis.
 *
 * @param axis The axis, normalized
 * @param angle The angle in radians
 * @return The matrix
 */
static mat4 box_editor_axis_rotation(
    vec3 axis,
    float angle)
{
    float c = cosf(angle), s = sinf(angle), t = 1.0f - c;
    mat4 m = mat4_identity();
    m.v[0] = t * axis.x * axis.x + c;
    m.v[1] = t * axis.x * axis.y + s * axis.z;
    m.v[2] = t * axis.x * axis.z - s * axis.y;
    m.v[4] = t * axis.x * axis.y - s * axis.z;
    m.v[5] = t * axis.y * axis.y + c;
    m.v[6] = t * axis.y * axis.z + s * axis.x;
    m.v[8] = t * axis.x * axis.z + s * axis.y;
    m.v[9] = t * axis.y * axis.z - s * axis.x;
    m.v[10] = t * axis.z * axis.z + c;

    return m;
}

/** Rotation part of a transform, with the scale divided out.
 *
 * @param m The transform
 * @return The rotation
 */
static mat4 box_editor_rotation_of(
    const mat4 *m)
{
    mat4 r = mat4_identity();
    int32_t col;
    for (col = 0; col < 3; col ++) {
        vec3 axis = vec3_normalize(vec3_new(m->v[col * 4], m->v[col * 4 + 1], m->v[col * 4 + 2]));
        r.v[col * 4] = axis.x;
        r.v[col * 4 + 1] = axis.y;
        r.v[col * 4 + 2] = axis.z;
    }

    return r;
}

/** World transform of an entity, or the identity when it has none.
 *
 * @param world The world
 * @param e The entity, 0 for the identity
 * @return The transform
 */
static mat4 box_editor_world_of(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const WorldTransform *wt = e ? ecs_get(world, e, WorldTransform) : NULL;

    return wt ? wt->m : mat4_identity();
}

/** Eye and forward direction of the camera a pick came from, or of the scene.
 *
 * @param world The world
 * @param view The view of the pick, 0 for the primary view of the scene
 * @param eye Receives the position of the camera
 * @param forward Receives the direction the camera looks in
 * @return true when the world has a camera
 */
static bool box_editor_camera(
    ecs_world_t *world,
    ecs_entity_t view,
    vec3 *eye,
    vec3 *forward)
{
    if (!view) {
        RenderState *render = box_render_state(world);
        view = box_render_primary_view(world, render);
    }

    return box_picking_ray(world, view, (vec2){0, 0}, eye, forward);
}

/** Parameter of the point of a line nearest to a ray.
 *
 * @param origin Origin of the ray
 * @param dir Direction of the ray, normalized
 * @param point A point of the line
 * @param axis Direction of the line, normalized
 * @return Distance along the axis from the point to the nearest point of the line
 */
static float box_editor_axis_param(
    vec3 origin,
    vec3 dir,
    vec3 point,
    vec3 axis)
{
    vec3 w = vec3_sub(origin, point);
    float b = vec3_dot(dir, axis);
    float d = vec3_dot(dir, w);
    float e = vec3_dot(axis, w);
    float denom = 1.0f - b * b;
    float result = 0;
    if (denom > 1e-5f) {
        result = (e - b * d) / denom;
    }

    return result;
}

/** Point where a ray meets the plane through a point with a normal.
 *
 * @param origin Origin of the ray
 * @param dir Direction of the ray
 * @param point A point of the plane
 * @param normal Normal of the plane
 * @param out Receives the intersection, the point itself when the ray misses
 * @return true when the ray met the plane
 */
static bool box_editor_plane_point(
    vec3 origin,
    vec3 dir,
    vec3 point,
    vec3 normal,
    vec3 *out)
{
    bool result = box_ray_plane(origin, dir, normal, vec3_dot(normal, point), out);
    if (!result) {
        *out = point;
    }

    return result;
}

/** Angle of a point of a plane about its normal, from the first of the other axes.
 *
 * @param grab The drag, whose axes span the plane
 * @param index Index of the normal among the axes
 * @param point The point
 * @return The angle in radians
 */
static float box_editor_plane_angle(
    const box_editor_grab_t *grab,
    int32_t index,
    vec3 point)
{
    vec3 d = vec3_sub(point, grab->origin);
    vec3 u = grab->axis[(index + 1) % 3];
    vec3 v = grab->axis[(index + 2) % 3];

    return atan2f(vec3_dot(d, v), vec3_dot(d, u));
}

/** Index of the single axis of a handle, -1 when the handle has more than one.
 *
 * @param axes The axes bits
 * @return 0, 1 or 2 for a single axis, -1 otherwise
 */
static int32_t box_editor_single_axis(
    int32_t axes)
{
    int32_t result = -1;
    if (axes == 1) {
        result = 0;
    } else if (axes == 2) {
        result = 1;
    } else if (axes == 4) {
        result = 2;
    }

    return result;
}

/** Index of the axis a two axis handle leaves out, -1 when it is not such a handle.
 *
 * @param axes The axes bits
 * @return The missing axis
 */
static int32_t box_editor_missing_axis(
    int32_t axes)
{
    int32_t result = -1;
    if (axes == 6) {
        result = 0;
    } else if (axes == 5) {
        result = 1;
    } else if (axes == 3) {
        result = 2;
    }

    return result;
}

/** World position of the selection, or of the grabbed control point.
 *
 * @param world The world
 * @param target The selected entity
 * @param mode What is dragged
 * @param index Control point of a spline handle
 * @return The position in world space
 */
static vec3 box_editor_anchor(
    const ecs_world_t *world,
    ecs_entity_t target,
    HandleMode mode,
    int32_t index)
{
    mat4 wt = box_editor_world_of(world, target);
    vec3 result = mat4_get_translation(&wt);
    const Spline *spline = mode == HandlePoint ? ecs_get(world, target, Spline) : NULL;
    if (spline && index >= 0 && index < ecs_vec_count(&spline->points)) {
        result = mat4_transform_point(&wt, ecs_vec_get_t(&spline->points, vec3, index)[0]);
    } else if (!ecs_has(world, target, WorldTransform)) {
        const Position *p = ecs_get(world, target, Position);
        result = p ? vec3_new(p->x, p->y, p->z) : vec3_new(0, 0, 0);
    }

    return result;
}

/** Set the axes of a drag: the world axes, or the local axes of the target for a scale.
 *
 * @param world The world
 * @param grab The drag
 */
static void box_editor_grab_axes(
    const ecs_world_t *world,
    box_editor_grab_t *grab)
{
    grab->axis[0] = vec3_new(1, 0, 0);
    grab->axis[1] = vec3_new(0, 1, 0);
    grab->axis[2] = vec3_new(0, 0, 1);
    if (grab->mode == HandleScale) {
        mat4 wt = box_editor_world_of(world, grab->target);
        mat4 r = box_editor_rotation_of(&wt);
        grab->axis[0] = mat4_transform_dir(&r, grab->axis[0]);
        grab->axis[1] = mat4_transform_dir(&r, grab->axis[1]);
        grab->axis[2] = mat4_transform_dir(&r, grab->axis[2]);
    }
}

/** Record the components of the target the drag starts from.
 *
 * @param world The world
 * @param grab The drag
 */
static void box_editor_grab_components(
    const ecs_world_t *world,
    box_editor_grab_t *grab)
{
    const Position *p = ecs_get(world, grab->target, Position);
    const Rotation *r = ecs_get(world, grab->target, Rotation);
    const Scale *s = ecs_get(world, grab->target, Scale);
    grab->p0 = p ? *p : (Position){0, 0, 0};
    grab->r0 = r ? *r : (Rotation){0, 0, 0};
    grab->s0 = s ? *s : (Scale){1, 1, 1};

    ecs_entity_t parent = ecs_get_parent(world, grab->target);
    mat4 parent_world = box_editor_world_of(world, parent);
    grab->parent_inverse = mat4_inverse(&parent_world);
    grab->parent_rotation = box_editor_rotation_of(&parent_world);
    if (grab->mode == HandlePoint) {
        const Spline *spline = ecs_get(world, grab->target, Spline);
        mat4 wt = box_editor_world_of(world, grab->target);
        grab->parent_inverse = mat4_inverse(&wt);
        grab->point0 = spline && grab->index < ecs_vec_count(&spline->points) ?
            ecs_vec_get_t(&spline->points, vec3, grab->index)[0] : vec3_new(0, 0, 0);
    }
}

bool box_editor_grab(
    ecs_world_t *world,
    ecs_entity_t handle,
    const PickHit *hit)
{
    EditorState *state = box_editor_state(world);
    const Editor *editor = ecs_singleton_get(world, Editor);
    const GizmoHandle *h = handle && ecs_is_alive(world, handle) ?
        ecs_get(world, handle, GizmoHandle) : NULL;
    bool result = false;
    vec3 origin, dir, eye, forward;
    if (!h || !editor->selected || !ecs_is_alive(world, editor->selected) ||
        !box_picking_ray(world, hit->view, hit->cursor, &origin, &dir) ||
        !box_editor_camera(world, hit->view, &eye, &forward))
    {
        goto done;
    }

    box_editor_grab_t *grab = &state->grab;
    memset(grab, 0, sizeof(box_editor_grab_t));
    grab->mode = h->mode;
    grab->axes = h->axes ? h->axes : 7;
    grab->index = h->index;
    grab->target = editor->selected;
    grab->cursor = hit->cursor;
    grab->origin = box_editor_anchor(world, grab->target, grab->mode, grab->index);
    grab->size = vec3_length(vec3_sub(grab->origin, eye)) * BOX_EDITOR_GIZMO_SIZE;
    box_editor_grab_axes(world, grab);
    box_editor_grab_components(world, grab);

    int32_t single = box_editor_single_axis(grab->axes);
    int32_t missing = box_editor_missing_axis(grab->axes);
    if (grab->mode == HandleRotate && single >= 0) {
        grab->normal = grab->axis[single];
        vec3 point;
        box_editor_plane_point(origin, dir, grab->origin, grab->normal, &point);
        grab->angle = box_editor_plane_angle(grab, single, point);
    } else if (grab->mode == HandleRotate) {
        grab->normal = vec3_scale(forward, -1.0f);
    } else if (single >= 0) {
        grab->normal = grab->axis[single];
        float t = box_editor_axis_param(origin, dir, grab->origin, grab->normal);
        grab->start = vec3_add(grab->origin, vec3_scale(grab->normal, t));
    } else {
        grab->normal = missing >= 0 ? grab->axis[missing] : vec3_scale(forward, -1.0f);
        box_editor_plane_point(origin, dir, grab->origin, grab->normal, &grab->start);
    }

    grab->active = true;
    box_editor_get(world)->handle = handle;
    box_editor_modified(world);
    result = true;
done:
    return result;
}

/** Apply a world space offset of the anchor to the target.
 *
 * @param world The world
 * @param grab The drag
 * @param delta The offset in world space
 */
static void box_editor_apply_move(
    ecs_world_t *world,
    const box_editor_grab_t *grab,
    vec3 delta)
{
    vec3 target = vec3_add(grab->origin, delta);
    vec3 local = mat4_transform_point(&grab->parent_inverse, target);
    if (grab->mode == HandlePoint) {
        Spline *spline = ecs_get_mut(world, grab->target, Spline);
        if (spline && grab->index >= 0 && grab->index < ecs_vec_count(&spline->points)) {
            ecs_vec_get_t(&spline->points, vec3, grab->index)[0] = local;
            ecs_modified(world, grab->target, Spline);
        }
    } else {
        ecs_set(world, grab->target, Position, { local.x, local.y, local.z });
    }
}

/** Apply a rotation of the target about a world axis.
 *
 * @param world The world
 * @param grab The drag
 * @param axis The axis in world space
 * @param angle The angle in radians
 */
static void box_editor_apply_rotation(
    ecs_world_t *world,
    const box_editor_grab_t *grab,
    vec3 axis,
    float angle)
{
    mat4 parent_inverse = mat4_inverse(&grab->parent_rotation);
    vec3 local_axis = vec3_normalize(mat4_transform_dir(&parent_inverse, axis));
    mat4 turn = box_editor_axis_rotation(local_axis, angle);
    mat4 r0 = mat4_rotation_xyz(vec3_new(grab->r0.x, grab->r0.y, grab->r0.z));
    mat4 r = mat4_mul(&turn, &r0);
    vec3 euler = mat4_to_euler_xyz(&r);
    ecs_set(world, grab->target, Rotation, { euler.x, euler.y, euler.z });
}

/** Apply a scale factor per axis to the target.
 *
 * @param world The world
 * @param grab The drag
 * @param factor The factors of the local axes
 */
static void box_editor_apply_scale(
    ecs_world_t *world,
    const box_editor_grab_t *grab,
    vec3 factor)
{
    Scale s = {
        fmaxf(grab->s0.x * factor.x, BOX_EDITOR_SCALE_MIN),
        fmaxf(grab->s0.y * factor.y, BOX_EDITOR_SCALE_MIN),
        fmaxf(grab->s0.z * factor.z, BOX_EDITOR_SCALE_MIN)
    };

    ecs_set(world, grab->target, Scale, { s.x, s.y, s.z });
}

/** Displacement of the cursor along the axis or on the plane of a drag.
 *
 * @param grab The drag
 * @param origin Origin of the cursor ray
 * @param dir Direction of the cursor ray
 * @return The displacement in world space since the grab
 */
static vec3 box_editor_displacement(
    const box_editor_grab_t *grab,
    vec3 origin,
    vec3 dir)
{
    vec3 result;
    if (box_editor_single_axis(grab->axes) >= 0) {
        float t = box_editor_axis_param(origin, dir, grab->origin, grab->normal);
        vec3 now = vec3_add(grab->origin, vec3_scale(grab->normal, t));
        result = vec3_sub(now, grab->start);
    } else {
        vec3 now;
        box_editor_plane_point(origin, dir, grab->origin, grab->normal, &now);
        result = vec3_sub(now, grab->start);
    }

    return result;
}

bool box_editor_pull(
    ecs_world_t *world,
    ecs_entity_t handle,
    const PickHit *hit)
{
    EditorState *state = box_editor_state(world);
    box_editor_grab_t *grab = &state->grab;
    bool result = false;
    vec3 origin, dir, eye, forward;
    (void)handle;
    if (!grab->active || !ecs_is_alive(world, grab->target) ||
        !box_picking_ray(world, hit->view, hit->cursor, &origin, &dir) ||
        !box_editor_camera(world, hit->view, &eye, &forward))
    {
        goto done;
    }

    int32_t single = box_editor_single_axis(grab->axes);
    vec2 dc = { hit->cursor.x - grab->cursor.x, hit->cursor.y - grab->cursor.y };
    if (grab->mode == HandleMove || grab->mode == HandlePoint) {
        box_editor_apply_move(world, grab, box_editor_displacement(grab, origin, dir));
    } else if (grab->mode == HandleRotate && single >= 0) {
        vec3 point;
        float angle;
        if (box_editor_plane_point(origin, dir, grab->origin, grab->normal, &point) &&
            fabsf(vec3_dot(dir, grab->normal)) > BOX_EDITOR_GRAZING)
        {
            angle = box_editor_plane_angle(grab, single, point) - grab->angle;
        } else {
            angle = (dc.x + dc.y) * BOX_EDITOR_TURN_RATE;
        }

        box_editor_apply_rotation(world, grab, grab->normal, angle);
    } else if (grab->mode == HandleRotate) {
        vec3 right = vec3_normalize(vec3_cross(vec3_new(0, 1, 0), forward));
        vec3 up = vec3_normalize(vec3_cross(forward, right));
        mat4 parent_inverse = mat4_inverse(&grab->parent_rotation);
        vec3 a = vec3_normalize(mat4_transform_dir(&parent_inverse, up));
        vec3 b = vec3_normalize(mat4_transform_dir(&parent_inverse, right));
        mat4 yaw = box_editor_axis_rotation(a, -dc.x * BOX_EDITOR_TURN_RATE);
        mat4 pitch = box_editor_axis_rotation(b, dc.y * BOX_EDITOR_TURN_RATE);
        mat4 turn = mat4_mul(&yaw, &pitch);
        mat4 r0 = mat4_rotation_xyz(vec3_new(grab->r0.x, grab->r0.y, grab->r0.z));
        mat4 r = mat4_mul(&turn, &r0);
        vec3 euler = mat4_to_euler_xyz(&r);
        ecs_set(world, grab->target, Rotation, { euler.x, euler.y, euler.z });
    } else if (grab->mode == HandleScale) {
        vec3 factor = vec3_new(1, 1, 1);
        float size = grab->size > 1e-4f ? grab->size : 1.0f;
        if (grab->axes == 7) {
            float f = 1.0f + (dc.x + dc.y) * 2.0f;
            factor = vec3_new(f, f, f);
        } else {
            vec3 d = box_editor_displacement(grab, origin, dir);
            if (grab->axes & 1) {
                factor.x = 1.0f + vec3_dot(d, grab->axis[0]) / size;
            }

            if (grab->axes & 2) {
                factor.y = 1.0f + vec3_dot(d, grab->axis[1]) / size;
            }

            if (grab->axes & 4) {
                factor.z = 1.0f + vec3_dot(d, grab->axis[2]) / size;
            }
        }

        box_editor_apply_scale(world, grab, factor);
    }

    result = true;
done:
    return result;
}

bool box_editor_drop(
    ecs_world_t *world)
{
    EditorState *state = box_editor_state(world);
    box_editor_grab_t *grab = &state->grab;
    bool result = grab->active;
    if (!result) {
        goto done;
    }

    grab->active = false;
    box_editor_get(world)->handle = 0;
    box_editor_modified(world);
    if (!ecs_is_alive(world, grab->target)) {
        goto done;
    }

    if (grab->mode == HandleMove) {
        box_editor_write_through(world, grab->target, ecs_id(Position));
    } else if (grab->mode == HandleRotate) {
        box_editor_write_through(world, grab->target, ecs_id(Rotation));
    } else if (grab->mode == HandleScale) {
        box_editor_write_through(world, grab->target, ecs_id(Scale));
    } else {
        box_editor_write_through(world, grab->target, ecs_id(Spline));
    }

done:
    return result;
}

/** World bounds of the selection: of its instance, its AABB or its collider.
 *
 * @param world The world
 * @param e The selected entity
 * @param min Receives the minimum corner
 * @param max Receives the maximum corner
 * @return true when the selection has bounds with some extent
 */
static bool box_editor_bounds(
    const ecs_world_t *world,
    ecs_entity_t e,
    vec3 *min,
    vec3 *max)
{
    const RootBounds *rb = ecs_get(world, e, RootBounds);
    const AABB *aabb = ecs_get(world, e, AABB);
    const Collider *collider = ecs_get(world, e, Collider);
    bool result = false;
    if (rb && rb->count) {
        *min = rb->min;
        *max = rb->max;
        result = true;
    } else if (aabb && (aabb->max.x > aabb->min.x || aabb->max.y > aabb->min.y)) {
        *min = aabb->min;
        *max = aabb->max;
        result = true;
    } else if (collider) {
        box_collider_bounds(collider, min, max);
        result = true;
    }

    return result && (max->x - min->x > 0 || max->y - min->y > 0 || max->z - min->z > 0);
}

/** Draw the twelve edges of the box around the bounds of the selection.
 *
 * @param world The world
 * @param editor The editor singleton
 * @param eye Position of the camera
 */
static void box_editor_draw_selection(
    ecs_world_t *world,
    const Editor *editor,
    vec3 eye)
{
    ecs_entity_t asset = ecs_lookup(world, "box.editor.gizmos.SelectionEdge");
    vec3 min, max;
    if (!asset || !box_editor_bounds(world, editor->selected, &min, &max)) {
        goto done;
    }

    vec3 center = vec3_scale(vec3_add(min, max), 0.5f);
    vec3 size = vec3_sub(max, min);
    float t = vec3_length(vec3_sub(center, eye)) * BOX_EDITOR_GIZMO_SIZE * BOX_EDITOR_EDGE_SIZE;
    float half = 1.5707963f;
    int32_t i;
    for (i = 0; i < 12; i ++) {
        int32_t axis = i / 4;
        float a = (i & 1) ? 0.5f : -0.5f;
        float b = (i & 2) ? 0.5f : -0.5f;
        vec3 at = center;
        vec3 scale = vec3_new(t, t, t);
        vec3 rotation = vec3_new(0, 0, 0);
        if (axis == 0) {
            at.y += a * size.y;
            at.z += b * size.z;
            scale.x = size.x + t;
        } else if (axis == 1) {
            at.x += a * size.x;
            at.z += b * size.z;
            scale.x = size.y + t;
            rotation.z = half;
        } else {
            at.x += a * size.x;
            at.y += b * size.y;
            scale.x = size.z + t;
            rotation.y = half;
        }

        box_draw(world, &(box_draw_desc_t){ .asset = asset, .key = 100 + (uint64_t)i,
            .position = at, .rotation = rotation, .scale = scale, .overlay = true });
    }

done:
    return;
}

/** Draw the gizmo of the tool at the selection.
 *
 * @param world The world
 * @param editor The editor singleton
 * @param eye Position of the camera
 */
static void box_editor_draw_tool(
    ecs_world_t *world,
    const Editor *editor,
    vec3 eye)
{
    const char *names[] = { NULL, "MoveGizmo", "RotateGizmo", "ScaleGizmo" };
    ecs_entity_t e = editor->selected;
    box_editor_draw_selection(world, editor, eye);
    if (editor->tool == ToolSelect) {
        goto done;
    }

    if (!editor->transform || !ecs_has(world, e, Position)) {
        goto done;
    }

    char path[64];
    snprintf(path, sizeof(path), "box.editor.gizmos.%s", names[editor->tool]);
    ecs_entity_t asset = ecs_lookup(world, path);
    if (!asset) {
        goto done;
    }

    vec3 origin = box_editor_anchor(world, e, HandleMove, -1);
    float size = vec3_length(vec3_sub(origin, eye)) * BOX_EDITOR_GIZMO_SIZE;
    vec3 rotation = vec3_new(0, 0, 0);
    if (editor->tool == ToolScale) {
        mat4 wt = box_editor_world_of(world, e);
        mat4 r = box_editor_rotation_of(&wt);
        rotation = mat4_to_euler_xyz(&r);
    }

    box_draw(world, &(box_draw_desc_t){
        .asset = asset, .key = 1, .position = origin, .rotation = rotation,
        .scale = { size, size, size }, .overlay = true, .on_top = true });
done:
    return;
}

/** Draw a handle on every control point of the selected spline and its guide.
 *
 * @param world The world
 * @param editor The editor singleton
 * @param eye Position of the camera
 */
static void box_editor_draw_spline(
    ecs_world_t *world,
    const Editor *editor,
    vec3 eye)
{
    ecs_entity_t e = editor->selected;
    ecs_entity_t asset = ecs_lookup(world, "box.editor.gizmos.PointHandle");
    const Spline *spline = ecs_get(world, e, Spline);
    if (!asset || !spline) {
        goto done;
    }

    mat4 wt = box_editor_world_of(world, e);
    int32_t i, count = ecs_vec_count(&spline->points);
    for (i = 0; i < count; i ++) {
        vec3 p = mat4_transform_point(&wt, ecs_vec_get_t(&spline->points, vec3, i)[0]);
        float size = vec3_length(vec3_sub(p, eye)) * BOX_EDITOR_GIZMO_SIZE * 0.3f;
        ecs_entity_t handle = box_draw(world, &(box_draw_desc_t){
            .asset = asset, .key = 1000 + (uint64_t)i, .position = p,
            .scale = { size, size, size }, .overlay = true, .on_top = true });
        const GizmoHandle *h = ecs_get(world, handle, GizmoHandle);
        if (!h || h->index != i || h->mode != HandlePoint) {
            ecs_set(world, handle, GizmoHandle, { .mode = HandlePoint, .axes = 7, .index = i });
        }
    }

    box_editor_spline_guide(world, e, &wt);
done:
    return;
}

void box_editor_gizmo_draw(
    ecs_world_t *world,
    EditorState *state)
{
    const Editor *editor = ecs_singleton_get(world, Editor);
    vec3 eye, forward;
    (void)state;
    if (!editor->selected || !ecs_is_alive(world, editor->selected) ||
        !box_editor_camera(world, 0, &eye, &forward))
    {
        box_editor_spline_guide(world, 0, NULL);
        goto done;
    }

    box_editor_draw_tool(world, editor, eye);
    if (editor->spline) {
        box_editor_draw_spline(world, editor, eye);
    } else {
        box_editor_spline_guide(world, 0, NULL);
    }

done:
    return;
}
