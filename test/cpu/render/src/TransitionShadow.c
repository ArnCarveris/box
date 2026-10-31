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

/* A parent without a shape moves a caster child: the parent has no caster slot of its own but
 * needs its range for the child, whose regions hold the pose it renders */
void TransitionShadow_group_parent(void) {
    // engine_core/transition
    box_render_null_t *null;
    box_render_shadow_region_t bounds;
    ecs_world_t *world = tr_scene(&null);
    ecs_vec_t *regions = tr_regions(world);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_entity_t child = tr_shape(world, parent, 1);
    box_transition_set(world, parent, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);

    ecs_set(world, parent, Position, {4, 0, 0});
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_vec_clear(regions);
        tr_frame(world);
        tr_rendered_bounds(world, null, child, &bounds);
        tr_test_regions(regions, &bounds, 0.4f, 5.6f);
    }

    tr_frame(world);
    tr_frame(world);
    tr_test_static(world, null, child);
    tr_fini(world, null);
}

/** Check the regions of a caster 3 units out along x and a NoShadow box 20 units out along -x,
 * both plain children of a parent without a shape that turns about y and is sent back halfway:
 * the regions of each frame hold the pose the caster renders and stay within -5.5 to 5.5, which
 * no region of the NoShadow child is.
 *
 * @param no_shadow_first Whether the NoShadow child is created before the caster
 */
static void tr_shadow_mixed(
    bool no_shadow_first)
{
    box_render_null_t *null;
    box_render_shadow_region_t bounds, last;
    ecs_world_t *world = tr_scene(&null);
    ecs_vec_t *regions = tr_regions(world);
    ecs_entity_t parent = ecs_new(world);
    ecs_entity_t caster = 0, hidden = 0;
    int32_t i;
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_set(world, parent, Rotation, {0, 0, 0});
    if (no_shadow_first) {
        hidden = tr_shape(world, parent, -20);
        ecs_add(world, hidden, NoShadow);
        caster = tr_shape(world, parent, 3);
    } else {
        caster = tr_shape(world, parent, 3);
        hidden = tr_shape(world, parent, -20);
        ecs_add(world, hidden, NoShadow);
    }

    box_transition_set(world, parent, ecs_id(Rotation), &(TransitionSpec){
        .duration = 1.0f, .timing_function = EaseInOut });
    tr_frame(world);
    test_assert(!ecs_has(world, caster, Transition));
    test_assert(!ecs_has(world, hidden, Transition));

    ecs_set(world, parent, Rotation, {0, (float)GLM_PI, 0});
    for (i = 0; i < 2; i ++) {
        ecs_vec_clear(regions);
        tr_frame(world);
        test_assert(ecs_has_id(world, caster, TransitionTransform));
        tr_rendered_bounds(world, null, caster, &bounds);
        tr_test_regions(regions, &bounds, -5.5f, 5.5f);
        tr_test_within(regions, -5.5f, 5.5f);
    }

    ecs_set(world, parent, Rotation, {0, -0.5f, 0});
    for (i = 0; i < 4; i ++) {
        ecs_vec_clear(regions);
        tr_frame(world);
        tr_rendered_bounds(world, null, caster, &bounds);
        tr_test_regions(regions, &bounds, -5.5f, 5.5f);
        tr_test_within(regions, -5.5f, 5.5f);
        last = bounds;
    }

    ecs_vec_clear(regions);
    tr_frame(world);
    test_assert(!ecs_has_id(world, caster, TransitionTransform));
    tr_test_regions(regions, &last, -5.5f, 5.5f);

    ecs_vec_clear(regions);
    tr_frame(world);
    tr_test_within(regions, -5.5f, 5.5f);
    tr_test_static(world, null, caster);
    tr_test_static(world, null, hidden);
    tr_fini(world, null);
}

/* A turning parent with a caster child and a NoShadow child, created in either order: the mark
 * of the caster reaches the parent whichever sibling the walk meets first, and the NoShadow
 * child adds no region */
void TransitionShadow_mixed_children(void) {
    // engine_core/transition
    tr_shadow_mixed(true);
    tr_shadow_mixed(false);
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

/** Get the range of a product with the interval math of every element, the reference of the
 * plain products box_draws_range_mul takes for a single matrix.
 *
 * @param a The range of the left matrix
 * @param b The range of the right matrix
 * @param out Receives the range of a * b
 */
static void tr_range_reference(
    const box_draw_transition_range_t *a,
    const box_draw_transition_range_t *b,
    box_draw_transition_range_t *out)
{
    int32_t col, row, k;
    for (col = 0; col < 4; col ++) {
        for (row = 0; row < 4; row ++) {
            float lo = 0.0f, hi = 0.0f;
            for (k = 0; k < 4; k ++) {
                float p0 = a->lo[k][row] * b->lo[col][k];
                float p1 = a->lo[k][row] * b->hi[col][k];
                float p2 = a->hi[k][row] * b->lo[col][k];
                float p3 = a->hi[k][row] * b->hi[col][k];
                lo += glm_min(glm_min(p0, p1), glm_min(p2, p3));
                hi += glm_max(glm_max(p0, p1), glm_max(p2, p3));
            }

            out->lo[col][row] = lo;
            out->hi[col][row] = hi;
        }
    }
}

/* The product of ranges with a single matrix on either side, or both, matches the interval
 * math bit for bit, and that of two single matrices is their glm_mul */
void TransitionShadow_range_mul_single(void) {
    // engine_core/transition
    box_draw_transition_range_t single, range, got, expected;
    const box_draw_transition_range_t *pairs[4][2] = {
        {&single, &range}, {&range, &single}, {&single, &single}, {&range, &range}
    };
    mat4 m, product;
    int32_t i, col, row;
    for (col = 0; col < 4; col ++) {
        for (row = 0; row < 4; row ++) {
            float v = (float)((col * 7 + row * 5) % 11) * 0.37f - 1.8f;
            m[col][row] = v;
            range.lo[col][row] = -v * 0.5f - 0.25f;
            range.hi[col][row] = range.lo[col][row] + (float)((col + row) % 3) * 0.6f;
        }
    }

    box_draws_range_point(m, &single);
    for (i = 0; i < 4; i ++) {
        box_draws_range_mul(pairs[i][0], pairs[i][1], &got);
        tr_range_reference(pairs[i][0], pairs[i][1], &expected);
        test_assert(!ecs_os_memcmp_t(&got, &expected, box_draw_transition_range_t));
    }

    box_draws_range_mul(&single, &single, &got);
    glm_mat4_mul(m, m, product);
    for (col = 0; col < 4; col ++) {
        for (row = 0; row < 4; row ++) {
            test_assert(got.lo[col][row] == got.hi[col][row]);
            test_flt(got.lo[col][row], product[col][row]);
        }
    }

    tr_range_reference(&single, &range, &expected);
    box_draws_range_mul(&single, &range, &range);
    test_assert(!ecs_os_memcmp_t(&range, &expected, box_draw_transition_range_t));
}

/* The bounds of a slot at every transform between two transforms are those of the 8 corners of
 * its local bounds, bit for bit, for terms of either sign and transforms that are equal */
void TransitionShadow_bounds_corners(void) {
    // engine_core/transition
    box_render_null_t *null;
    box_render_shadow_region_t got, expected;
    box_render_transform_t lo, hi;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0, 1);
    tr_frame(world);

    box_draw_batch_t *b = tr_batch(world, e);
    const box_mesh_t *mesh = box_mesh_cache_get(box_mesh_cache_of(world), b->key.mesh);
    test_assert(mesh != NULL);
    int32_t i, c, r, k;
    for (i = 0; i < 2; i ++) {
        for (r = 0; r < 3; r ++) {
            for (k = 0; k < 4; k ++) {
                lo.rows[r][k] = (float)((r * 5 + k * 3) % 7) * 0.43f - 1.3f;
                hi.rows[r][k] = i ? lo.rows[r][k] :
                    lo.rows[r][k] + (float)((r + k) % 3) * 0.71f;
            }
        }

        test_assert(box_draws_shadow_bounds(world, b, &lo, &hi, NULL, &got));
        glm_vec3_fill(expected.min, FLT_MAX);
        glm_vec3_fill(expected.max, -FLT_MAX);
        for (c = 0; c < 8; c ++) {
            vec3 local = {
                (c & 1) ? mesh->max[0] : mesh->min[0],
                (c & 2) ? mesh->max[1] : mesh->min[1],
                (c & 4) ? mesh->max[2] : mesh->min[2]
            };
            for (r = 0; r < 3; r ++) {
                float world_lo = 0.0f, world_hi = 0.0f;
                for (k = 0; k < 3; k ++) {
                    float a = lo.rows[r][k] * local[k], z = hi.rows[r][k] * local[k];
                    world_lo += glm_min(a, z);
                    world_hi += glm_max(a, z);
                }

                world_lo += lo.rows[r][3];
                world_hi += hi.rows[r][3];
                expected.min[r] = glm_min(expected.min[r], world_lo);
                expected.max[r] = glm_max(expected.max[r], world_hi);
            }
        }

        test_assert(got.scene == b->key.scene);
        test_assert(!ecs_os_memcmp_t(&got.min, &expected.min, vec3));
        test_assert(!ecs_os_memcmp_t(&got.max, &expected.max, vec3));
    }

    tr_fini(world, null);
}
