/**
 * @file Script.c
 * @brief Tests for authoring a curve in a script and for the sampling functions.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/spline/spline_internal.h"

void Script_points_from_a_script(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    test_int(ecs_script_run(w, "test",
        "using box.spline\n"
        "curve {\n"
        "  Spline: {points: [{0, 0, 0}, {2, 0, 0}, {2, 0, 2}, {0, 0, 2}],\n"
        "    type: SplineLinear, loop: true}\n"
        "}\n", NULL), 0);

    ecs_entity_t e = ecs_lookup(w, "curve");
    test_assert(e != 0);

    const Spline *s = ecs_get(w, e, Spline);
    test_assert(s != NULL);
    test_int(ecs_vec_count(&s->points), 4);
    test_flt(s->tension, 0.5f);

    const SplineInfo *info = ecs_get(w, e, SplineInfo);
    test_assert(info != NULL);
    test_int(info->segments, 4);
    test_flt(info->length, 8);

    ecs_fini(w);
}

void Script_position_function(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);
    ECS_IMPORT(w, BoxTransform);

    test_int(ecs_script_run(w, "test",
        "using box\n"
        "using box.spline\n"
        "using box.transform\n"
        "curve {\n"
        "  Spline: {points: [{0, 0, 0}, {4, 0, 0}], type: SplineLinear}\n"
        "}\n"
        "const p = spline.position(curve, 0.5)\n"
        "probe {\n"
        "  Position: {p.x, p.y, p.z}\n"
        "}\n", NULL), 0);

    ecs_entity_t probe = ecs_lookup(w, "probe");
    test_assert(probe != 0);

    const Position *p = ecs_get(w, probe, Position);
    test_assert(p != NULL);
    test_flt(p->x, 2);
    test_flt(p->y, 0);
    test_flt(p->z, 0);

    ecs_fini(w);
}

void Script_point_at_function(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);
    ECS_IMPORT(w, BoxTransform);

    test_int(ecs_script_run(w, "test",
        "using box\n"
        "using box.spline\n"
        "using box.transform\n"
        "curve {\n"
        "  Spline: {points: [{0, 0, 0}, {2, 0, 0}, {2, 0, 4}], type: SplineLinear}\n"
        "}\n"
        "const p = spline.point_at(curve, 3)\n"
        "const d = spline.tangent_at(curve, 3)\n"
        "probe {\n"
        "  Position: {p.x, p.y, p.z}\n"
        "}\n"
        "heading {\n"
        "  Position: {d.x, d.y, d.z}\n"
        "}\n", NULL), 0);

    ecs_entity_t probe = ecs_lookup(w, "probe");
    ecs_entity_t heading = ecs_lookup(w, "heading");
    test_assert(probe != 0);
    test_assert(heading != 0);

    const Position *p = ecs_get(w, probe, Position);
    test_assert(p != NULL);
    test_assert(fabsf(p->x - 2) < 0.001f);
    test_assert(fabsf(p->z - 1) < 0.001f);

    const Position *h = ecs_get(w, heading, Position);
    test_assert(h != NULL);
    test_assert(fabsf(h->z - 1) < 0.001f);

    ecs_fini(w);
}

void Script_length_function(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);
    ECS_IMPORT(w, BoxTransform);

    test_int(ecs_script_run(w, "test",
        "using box\n"
        "using box.spline\n"
        "using box.transform\n"
        "curve {\n"
        "  Spline: {points: [{0, 0, 0}, {2, 0, 0}, {2, 0, 4}], type: SplineLinear}\n"
        "}\n"
        "const d = spline.length(curve)\n"
        "probe {\n"
        "  Position: {d, 0, 0}\n"
        "}\n", NULL), 0);

    ecs_entity_t probe = ecs_lookup(w, "probe");
    test_assert(probe != 0);

    const Position *p = ecs_get(w, probe, Position);
    test_assert(p != NULL);
    test_flt(p->x, 6);

    ecs_fini(w);
}
