/**
 * @file editor/spline.c
 * @brief The spline editor: the guide curve, the selected point and adding or removing points.
 *
 * The guide is one entity of the module with a SplineMesh over the selected
 * spline, drawn in the overlay layer on top and hidden when no spline is selected.
 */

#include "editor_internal.h"

/** Width and height of the guide tube */
#define BOX_EDITOR_GUIDE_SIZE (0.05f)

/** Get the guide entity, creating it on first use.
 *
 * @param world The world
 * @return The guide entity
 */
static ecs_entity_t box_editor_guide(
    ecs_world_t *world)
{
    ecs_entity_t module = ecs_lookup(world, "box.editor");
    ecs_entity_t guide = ecs_lookup_child(world, module, "guide");
    if (guide) {
        goto done;
    }

    guide = ecs_entity(world, { .name = "guide", .parent = module });
    ecs_add_id(world, guide, DynamicTransform);
    ecs_add_id(world, guide, Hidden);
    ecs_add_pair(world, guide, Layer, box_draw_top_layer(world));
    ecs_set(world, guide, Rgb, { 255, 200, 60 });
    ecs_set(world, guide, Position, { 0, 0, 0 });
done:
    return guide;
}

void box_editor_spline_guide(
    ecs_world_t *world,
    ecs_entity_t spline,
    const mat4 *transform)
{
    ecs_entity_t guide = box_editor_guide(world);
    if (!spline) {
        if (!ecs_has_id(world, guide, Hidden)) {
            ecs_add_id(world, guide, Hidden);
        }

        goto done;
    }

    const SplineMesh *mesh = ecs_get(world, guide, SplineMesh);
    if (!mesh || mesh->spline != spline) {
        SplineMesh *m = ecs_ensure(world, guide, SplineMesh);
        m->spline = spline;
        m->width = BOX_EDITOR_GUIDE_SIZE;
        m->height = BOX_EDITOR_GUIDE_SIZE;
        m->closed = true;
        ecs_vec_clear(&m->profile);
        ecs_vec_append_t(NULL, &m->profile, vec2)[0] = (vec2){ 0, 1 };
        ecs_vec_append_t(NULL, &m->profile, vec2)[0] = (vec2){ 1, 0 };
        ecs_vec_append_t(NULL, &m->profile, vec2)[0] = (vec2){ 0, -1 };
        ecs_vec_append_t(NULL, &m->profile, vec2)[0] = (vec2){ -1, 0 };
        ecs_modified(world, guide, SplineMesh);
    }

    vec3 t = mat4_get_translation(transform);
    vec3 sx = vec3_new(transform->v[0], transform->v[1], transform->v[2]);
    vec3 sy = vec3_new(transform->v[4], transform->v[5], transform->v[6]);
    vec3 sz = vec3_new(transform->v[8], transform->v[9], transform->v[10]);
    mat4 r = mat4_identity();
    vec3 nx = vec3_normalize(sx), ny = vec3_normalize(sy), nz = vec3_normalize(sz);
    r.v[0] = nx.x; r.v[1] = nx.y; r.v[2] = nx.z;
    r.v[4] = ny.x; r.v[5] = ny.y; r.v[6] = ny.z;
    r.v[8] = nz.x; r.v[9] = nz.y; r.v[10] = nz.z;
    vec3 e = mat4_to_euler_xyz(&r);
    ecs_set(world, guide, Position, { t.x, t.y, t.z });
    ecs_set(world, guide, Rotation, { e.x, e.y, e.z });
    ecs_set(world, guide, Scale, { vec3_length(sx), vec3_length(sy), vec3_length(sz) });
    if (ecs_has_id(world, guide, Hidden)) {
        ecs_remove_id(world, guide, Hidden);
    }

done:
    return;
}

bool box_editor_point(
    ecs_world_t *world,
    int32_t index)
{
    Editor *editor = box_editor_get(world);
    const Spline *spline = editor->selected && ecs_is_alive(world, editor->selected) ?
        ecs_get(world, editor->selected, Spline) : NULL;
    bool result = false;
    if (!spline || index >= ecs_vec_count(&spline->points)) {
        goto done;
    }

    editor->point = index < 0 ? -1 : index;
    box_editor_modified(world);
    result = true;
done:
    return result;
}

bool box_editor_point_add(
    ecs_world_t *world)
{
    Editor *editor = box_editor_get(world);
    Spline *spline = editor->selected && ecs_is_alive(world, editor->selected) ?
        ecs_get_mut(world, editor->selected, Spline) : NULL;
    bool result = false;
    if (!spline || !box_editor_editable(world, editor->selected, ecs_id(Spline))) {
        goto done;
    }

    int32_t count = ecs_vec_count(&spline->points);
    int32_t at = editor->point >= 0 && editor->point < count ? editor->point : count - 1;
    vec3 point = vec3_new(0, 0, 0);
    if (count && at >= 0) {
        vec3 a = ecs_vec_get_t(&spline->points, vec3, at)[0];
        if (at + 1 < count) {
            vec3 b = ecs_vec_get_t(&spline->points, vec3, at + 1)[0];
            point = vec3_scale(vec3_add(a, b), 0.5f);
        } else if (at > 0) {
            vec3 b = ecs_vec_get_t(&spline->points, vec3, at - 1)[0];
            point = vec3_add(a, vec3_sub(a, b));
        } else {
            point = vec3_add(a, vec3_new(1, 0, 0));
        }
    }

    ecs_vec_append_t(NULL, &spline->points, vec3);
    int32_t i, insert = at + 1;
    for (i = count; i > insert; i --) {
        ecs_vec_get_t(&spline->points, vec3, i)[0] =
            ecs_vec_get_t(&spline->points, vec3, i - 1)[0];
    }

    ecs_vec_get_t(&spline->points, vec3, insert)[0] = point;
    ecs_modified(world, editor->selected, Spline);
    editor = box_editor_get(world);
    editor->point = insert;
    box_editor_modified(world);
    box_editor_write_through(world, editor->selected, ecs_id(Spline));
    result = true;
done:
    return result;
}

bool box_editor_point_remove(
    ecs_world_t *world)
{
    Editor *editor = box_editor_get(world);
    Spline *spline = editor->selected && ecs_is_alive(world, editor->selected) ?
        ecs_get_mut(world, editor->selected, Spline) : NULL;
    bool result = false;
    if (!spline || editor->point < 0 || editor->point >= ecs_vec_count(&spline->points) ||
        !box_editor_editable(world, editor->selected, ecs_id(Spline)))
    {
        goto done;
    }

    ecs_vec_remove_ordered_t(&spline->points, vec3, editor->point);
    ecs_modified(world, editor->selected, Spline);
    editor = box_editor_get(world);
    int32_t count = ecs_vec_count(&spline->points);
    editor->point = count ? (editor->point < count ? editor->point : count - 1) : -1;
    box_editor_modified(world);
    box_editor_write_through(world, editor->selected, ecs_id(Spline));
    result = true;
done:
    return result;
}
