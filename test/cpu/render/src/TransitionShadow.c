/**
 * @file TransitionShadow.c
 * @brief Tests for the shadow regions of shadow casters a transform transition moves: each
 * frame records the bounds of every pose the resolver can render a caster at, instead of a
 * region over the whole scene.
 *
 * The cases render into the null backend and step the world with a fixed delta, never the wall
 * clock. Rendered transforms are read from the patched copy of the last packet, and the
 * regions are read from the draws, with the light extract that takes them stopped.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"
#include "draw.h"
#include "mesh_cache.h"
#include "transition_test.h"

/** Stop the light extract, which takes and clears the shadow regions every frame, and have
 * the draws record them, so a case reads the regions of each frame.
 *
 * @param world The world
 * @return box_render_shadow_region_t, the regions recorded since the case cleared them
 */
static ecs_vec_t* tr_regions(
    ecs_world_t *world)
{
    ecs_entity_t lights = ecs_lookup(world, "box.render.RenderExtractLights");
    test_assert(lights != 0);
    ecs_enable(world, lights, false);
    box_render_draws_shadow_record(world, true);
    ecs_vec_t *result = box_render_draws_shadow_regions(world);
    test_assert(result != NULL);

    return result;
}

/** Get the world bounds of the shape of an entity at the transform the backend got in the last
 * packet, with the region math of the shadow regions.
 *
 * @param world The world
 * @param null The backend
 * @param e The entity
 * @param out Receives the bounds
 */
static void tr_rendered_bounds(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e,
    box_render_shadow_region_t *out)
{
    box_render_transform_t t;
    box_draw_batch_t *b = tr_batch(world, e);
    int32_t slot = ecs_get(world, e, DrawSlot)->slot;
    test_assert(tr_rendered(world, null, e, &t));
    test_assert(box_draws_caster(b));
    test_assert(box_draws_shadow_bounds(world, b, &t, &t, box_draws_slot_surface(b, slot), out));
}

/** Check that the regions recorded since they were cleared are each within an x range, which
 * no region over the whole scene is, and that one of them holds bounds.
 *
 * @param regions The regions
 * @param bounds The bounds
 * @param min_x Least x a region may reach
 * @param max_x Largest x a region may reach
 */
static void tr_test_regions(
    const ecs_vec_t *regions,
    const box_render_shadow_region_t *bounds,
    float min_x,
    float max_x)
{
    bool found = false;
    int32_t i, k;
    test_assert(ecs_vec_count(regions) > 0);
    for (i = 0; i < ecs_vec_count(regions); i ++) {
        const box_render_shadow_region_t *r = ecs_vec_get_t(
            regions, box_render_shadow_region_t, i);
        bool inside = r->scene == bounds->scene;
        test_assert(r->min[0] >= min_x);
        test_assert(r->max[0] <= max_x);
        for (k = 0; k < 3; k ++) {
            test_assert(isfinite(r->min[k]));
            test_assert(isfinite(r->max[k]));
            inside = inside && r->min[k] <= bounds->min[k] + 1e-4f &&
                r->max[k] >= bounds->max[k] - 1e-4f;
        }

        found = found || inside;
    }

    test_assert(found);
}

/* A box 1 wide moves from x = 0 to 4 and is sent back to -4 at x = 2, which the game thread
 * never sees: the regions of each frame hold the pose it renders and stay within -4.5 to 4.5,
 * the frame it ends in holds the pose of the frame before, and a start once it is idle covers
 * only its new range */
void TransitionShadow_interrupted(void) {
    // engine_core/transition
    box_render_null_t *null;
    box_render_shadow_region_t bounds, last;
    ecs_world_t *world = tr_scene(&null);
    ecs_vec_t *regions = tr_regions(world);
    ecs_entity_t e = tr_shape(world, 0, 0);
    box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);

    ecs_set(world, e, Position, {4, 0, 0});
    int32_t i;
    for (i = 0; i < 2; i ++) {
        ecs_vec_clear(regions);
        tr_frame(world);
        tr_rendered_bounds(world, null, e, &bounds);
        tr_test_regions(regions, &bounds, -0.6f, 4.6f);
    }

    ecs_set(world, e, Position, {-4, 0, 0});
    for (i = 0; i < 4; i ++) {
        ecs_vec_clear(regions);
        tr_frame(world);
        tr_rendered_bounds(world, null, e, &bounds);
        tr_test_regions(regions, &bounds, -4.6f, 4.6f);
        last = bounds;
    }

    test_assert(last.min[0] < 2.0f);
    test_assert(last.max[0] > -4.0f);
    ecs_vec_clear(regions);
    tr_frame(world);
    test_assert(!ecs_has_id(world, e, TransitionTransform));
    tr_test_regions(regions, &last, -4.6f, 4.6f);

    ecs_set(world, e, Position, {-2, 0, 0});
    for (i = 0; i < 4; i ++) {
        ecs_vec_clear(regions);
        tr_frame(world);
        tr_rendered_bounds(world, null, e, &bounds);
        tr_test_regions(regions, &bounds, -4.6f, -1.4f);
    }

    tr_frame(world);
    tr_test_static(world, null, e);
    tr_fini(world, null);
}

/** Check the regions of a child 3 units out along x of a parent that turns about y and is sent
 * back halfway, while the child moves up on its own, for a ChildOf or a Parent hierarchy.
 *
 * @param non_fragmenting Whether the child has a Parent component instead of a ChildOf pair
 */
static void tr_shadow_child(
    bool non_fragmenting)
{
    box_render_null_t *null;
    box_render_shadow_region_t bounds;
    ecs_world_t *world = tr_scene(&null);
    ecs_vec_t *regions = tr_regions(world);
    ecs_entity_t parent = tr_shape(world, 0, 0);
    ecs_entity_t child;
    int32_t i;
    if (non_fragmenting) {
        child = ecs_new(world);
        ecs_set(world, child, EcsParent, {parent});
        ecs_set(world, child, Box, {1, 1, 1});
    } else {
        child = tr_shape(world, parent, 0);
    }

    ecs_set(world, child, Position, {3, 0, 0});
    ecs_set(world, parent, Rotation, {0, 0, 0});
    box_transition_set(world, parent, ecs_id(Rotation), &(TransitionSpec){
        .duration = 1.0f, .timing_function = EaseInOut });
    box_transition_set(world, child, ecs_id(Position), &(TransitionSpec){ .duration = 0.5f });
    tr_frame(world);

    ecs_set(world, parent, Rotation, {0, (float)GLM_PI, 0});
    ecs_set(world, child, Position, {3, 2, 0});
    for (i = 0; i < 2; i ++) {
        ecs_vec_clear(regions);
        tr_frame(world);
        tr_rendered_bounds(world, null, child, &bounds);
        tr_test_regions(regions, &bounds, -5.5f, 5.5f);
        tr_rendered_bounds(world, null, parent, &bounds);
        tr_test_regions(regions, &bounds, -5.5f, 5.5f);
    }

    ecs_set(world, parent, Rotation, {0, -0.5f, 0});
    for (i = 0; i < 4; i ++) {
        ecs_vec_clear(regions);
        tr_frame(world);
        tr_rendered_bounds(world, null, child, &bounds);
        tr_test_regions(regions, &bounds, -5.5f, 5.5f);
    }

    tr_frame(world);
    tr_test_static(world, null, parent);
    tr_test_static(world, null, child);
    tr_fini(world, null);
}

void TransitionShadow_nested_childof(void) {
    // engine_core/transition
    tr_shadow_child(false);
}

void TransitionShadow_nested_parent(void) {
    // engine_core/transition
    tr_shadow_child(true);
}

/** Check that the regions recorded since they were cleared are each finite and within an x
 * range, without asking for any of them, as on a frame that may record none.
 *
 * @param regions The regions
 * @param min_x Least x a region may reach
 * @param max_x Largest x a region may reach
 */
static void tr_test_within(
    const ecs_vec_t *regions,
    float min_x,
    float max_x)
{
    int32_t i, k;
    for (i = 0; i < ecs_vec_count(regions); i ++) {
        const box_render_shadow_region_t *r = ecs_vec_get_t(
            regions, box_render_shadow_region_t, i);
        test_assert(r->min[0] >= min_x);
        test_assert(r->max[0] <= max_x);
        for (k = 0; k < 3; k ++) {
            test_assert(isfinite(r->min[k]));
            test_assert(isfinite(r->max[k]));
        }
    }
}

/* A plain box 3 units out along x of a parent that turns about y and is sent back halfway has
 * no Transition of its own: its regions are those of the node it moves with, which hold the
 * pose it renders on every frame and stay within -5.5 to 5.5 */
void TransitionShadow_plain_child(void) {
    // engine_core/transition
    box_render_null_t *null;
    box_render_shadow_region_t bounds, last;
    ecs_world_t *world = tr_scene(&null);
    ecs_vec_t *regions = tr_regions(world);
    ecs_entity_t parent = tr_shape(world, 0, 0);
    ecs_entity_t child = tr_shape(world, parent, 3);
    int32_t i;
    ecs_set(world, parent, Rotation, {0, 0, 0});
    box_transition_set(world, parent, ecs_id(Rotation), &(TransitionSpec){
        .duration = 1.0f, .timing_function = EaseInOut });
    tr_frame(world);
    test_assert(!ecs_has(world, child, Transition));

    ecs_set(world, parent, Rotation, {0, (float)GLM_PI, 0});
    for (i = 0; i < 2; i ++) {
        ecs_vec_clear(regions);
        tr_frame(world);
        test_assert(ecs_has_id(world, child, TransitionTransform));
        tr_rendered_bounds(world, null, child, &bounds);
        tr_test_regions(regions, &bounds, -5.5f, 5.5f);
        tr_rendered_bounds(world, null, parent, &bounds);
        tr_test_regions(regions, &bounds, -5.5f, 5.5f);
    }

    ecs_set(world, parent, Rotation, {0, -0.5f, 0});
    for (i = 0; i < 4; i ++) {
        ecs_vec_clear(regions);
        tr_frame(world);
        tr_rendered_bounds(world, null, child, &bounds);
        tr_test_regions(regions, &bounds, -5.5f, 5.5f);
        last = bounds;
    }

    ecs_vec_clear(regions);
    tr_frame(world);
    test_assert(!ecs_has_id(world, child, TransitionTransform));
    tr_test_regions(regions, &last, -5.5f, 5.5f);

    ecs_vec_clear(regions);
    tr_frame(world);
    tr_test_within(regions, -5.5f, 5.5f);
    tr_test_static(world, null, parent);
    tr_test_static(world, null, child);
    tr_fini(world, null);
}

/** Get the world bounds of the local bounds of the ShapeMesh of an entity at the transform the
 * backend got in the last packet, without the region math of the draws.
 *
 * @param world The world
 * @param null The backend
 * @param e The entity
 * @param out Receives the bounds
 */
static void tr_shape_bounds(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e,
    box_render_shadow_region_t *out)
{
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    box_render_transform_t t;
    int32_t c, r, k;
    test_assert(tr_rendered(world, null, e, &t));
    out->scene = tr_batch(world, e)->key.scene;
    for (r = 0; r < 3; r ++) {
        out->min[r] = INFINITY;
        out->max[r] = -INFINITY;
    }

    for (c = 0; c < 8; c ++) {
        for (r = 0; r < 3; r ++) {
            float v = t.rows[r][3];
            for (k = 0; k < 3; k ++) {
                v += t.rows[r][k] * ((c & (1 << k)) ? shape->max[k] : shape->min[k]);
            }

            out->min[r] = fminf(out->min[r], v);
            out->max[r] = fmaxf(out->max[r], v);
        }
    }
}

/* Half a torus shell on the -x side, of radius 3 and tube 1, moves from x = 0 to 4. Its
 * surface record bounds it by -4 to 4 along x around its position, while its mesh reaches no
 * further than x = 0: the regions of each frame hold the surface bounds and stay within -4.1
 * to 8.1 */
void TransitionShadow_surface_instance(void) {
    // engine_core/transition
    box_render_null_t *null;
    box_render_shadow_region_t bounds, expected, last;
    ecs_world_t *world = tr_scene(&null);
    ecs_vec_t *regions = tr_regions(world);
    ecs_entity_t e = ecs_new(world);
    int32_t i, k;
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, TorusShell, {
        .radius = 3, .tube = 1,
        .ring_start = (float)GLM_PI_2, .ring_end = 3 * (float)GLM_PI_2,
        .tube_start = 0, .tube_end = 2 * (float)GLM_PI });
    ecs_set(world, e, Rgb, {20, 100, 200});
    box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);

    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    box_draw_batch_t *b = tr_batch(world, e);
    const box_mesh_t *mesh = box_mesh_cache_get(box_mesh_cache_of(world), b->key.mesh);
    test_assert(shape->surface.p[0][0] != 0.0f);
    test_assert(box_draws_slot_surface(b, ecs_get(world, e, DrawSlot)->slot) != NULL);
    test_assert(mesh != NULL);
    test_flt(shape->min[0], -4);
    test_flt(shape->max[0], 4);
    test_assert(mesh->max[0] < shape->max[0] - 3.0f);

    ecs_set(world, e, Position, {4, 0, 0});
    for (i = 0; i < 4; i ++) {
        ecs_vec_clear(regions);
        tr_frame(world);
        test_assert(ecs_has_id(world, e, TransitionTransform));
        tr_rendered_bounds(world, null, e, &bounds);
        tr_shape_bounds(world, null, e, &expected);
        for (k = 0; k < 3; k ++) {
            test_assert(fabsf(bounds.min[k] - expected.min[k]) < 1e-4f);
            test_assert(fabsf(bounds.max[k] - expected.max[k]) < 1e-4f);
        }

        test_assert(expected.max[0] - expected.min[0] > 7.9f);
        tr_test_regions(regions, &expected, -4.1f, 8.1f);
        last = expected;
    }

    ecs_vec_clear(regions);
    tr_frame(world);
    test_assert(!ecs_has_id(world, e, TransitionTransform));
    tr_test_regions(regions, &last, -4.1f, 8.1f);

    ecs_vec_clear(regions);
    tr_frame(world);
    tr_test_within(regions, -4.1f, 8.1f);
    tr_test_static(world, null, e);
    tr_fini(world, null);
}
