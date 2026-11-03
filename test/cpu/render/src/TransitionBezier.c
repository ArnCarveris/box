/**
 * @file TransitionBezier.c
 * @brief Cases of the Transition suite for the CubicBezier timing function: the handles in the
 * records, the values along a custom curve, overshoot, invalid handles and interruption.
 *
 * The cases render into the null backend and step the world with a fixed delta, never the wall
 * clock, as the other cases of the suite.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"
#include "transition_test.h"

/** Handles of the custom curve of the CubicBezier cases, a CSS "emphasized decelerate" */
static const TransitionBezier tr_bezier = { 0.2f, 0.0f, 0.0f, 1.0f };

/** Eased progress of a CubicBezier.
 *
 * @param bezier The handles
 * @param t Linear progress
 * @return The eased progress
 */
static float tr_ease(
    const TransitionBezier *bezier,
    float t)
{
    return box_transition_ease_bezier(CubicBezier, bezier, t);
}

/** Get the last start record of the last packet.
 *
 * @param null The backend
 * @return The record, NULL when the packet has none
 */
static const box_render_transition_record_t* tr_last_start(
    const box_render_null_t *null)
{
    const box_render_transition_record_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.transitions); i ++) {
        const box_render_transition_record_t *r = ecs_vec_get_t(
            &null->last.transitions, box_render_transition_record_t, i);
        if (r->op == BOX_RENDER_TRANSITION_START) {
            result = r;
        }
    }

    return result;
}

/* A move along {0.2, 0, 0, 1} sends its handles in the start record and renders the start, the
 * eased values at a quarter, half and three quarters, which are ahead of linear, and the end */
void Transition_bezier_values(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){
        .duration = 1.0f, .timing_function = CubicBezier, .bezier = tr_bezier });
    tr_frame(world);

    ecs_set(world, e, Position, {4, -2, 0});
    tr_frame(world);
    const box_render_transition_record_t *r = tr_last_start(null);
    test_assert(r != NULL);
    test_int(r->timing_function, CubicBezier);
    test_flt(r->bezier.x1, 0.2f);
    test_flt(r->bezier.y1, 0);
    test_flt(r->bezier.x2, 0);
    test_flt(r->bezier.y2, 1);
    Position v = tr_position(world, e);
    test_flt(v.x, 0);
    test_flt(v.y, 0);

    int32_t i;
    for (i = 1; i < 4; i ++) {
        tr_frame(world);
        float t = tr_ease(&tr_bezier, (float)i / 4.0f);
        v = tr_position(world, e);
        test_flt(v.x, 4.0f * t);
        test_flt(v.y, -2.0f * t);
        test_flt(v.z, 0);
    }

    test_assert(tr_ease(&tr_bezier, 0.5f) > 0.5f);
    test_assert(tr_ease(&tr_bezier, 0.5f) < 1.0f);

    tr_frame(world);
    v = tr_position(world, e);
    test_flt(v.x, 4);
    test_flt(v.y, -2);
    test_int(box_render_transition_test_active(world), 0);

    tr_fini(world, null);
}

/* The script of the docs parses the handles into the spec, and Opacity renders 1 at the start,
 * the eased value in the middle and 0 at the end */
void Transition_bezier_script(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    test_assert(ecs_script_run(world, "panel",
        "using box.material\n"
        "using box.transition\n"
        "panel {\n"
        "  Opacity: {1}\n"
        "  Transition: [Opacity: {duration: 0.3, timing_function: CubicBezier, "
            "bezier: {0.2, 0, 0, 1}}]\n"
        "}\n", NULL) == 0);
    ecs_entity_t e = ecs_lookup(world, "panel");
    test_assert(e != 0);
    const TransitionSpec *spec = box_transition_get(world, e, ecs_id(Opacity));
    test_assert(spec != NULL);
    test_int(spec->timing_function, CubicBezier);
    test_flt(spec->duration, 0.3f);
    test_flt(spec->bezier.x1, 0.2f);
    test_flt(spec->bezier.y1, 0);
    test_flt(spec->bezier.x2, 0);
    test_flt(spec->bezier.y2, 1);
    test_bool(box_transition_valid(world, ecs_id(Opacity), spec), true);
    tr_frame(world);

    ecs_set(world, e, Opacity, {0.0f});
    tr_frame(world);
    Opacity o = {0};
    test_assert(box_render_transition_test_value(world, e, ecs_id(Opacity), &o));
    test_flt(o.value, 1);

    tr_frame(world);
    test_assert(box_render_transition_test_value(world, e, ecs_id(Opacity), &o));
    float t = tr_ease(&tr_bezier, (float)(0.25 / (double)0.3f));
    test_assert(fabsf(o.value - (1.0f - t)) < 1e-5f);
    test_assert(o.value > 0.0f);

    tr_frame(world);
    test_assert(box_render_transition_test_value(world, e, ecs_id(Opacity), &o));
    test_flt(o.value, 0);
    test_int(box_render_transition_test_active(world), 0);

    tr_fini(world, null);
}

/* A CubicBezier with the handles of EaseInOut renders the same Position and Rgb every frame as
 * the preset, and the ease functions agree for every preset */
void Transition_bezier_matches_preset(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t preset = tr_entity(world, &(TransitionSpec){
        .duration = 1.0f, .timing_function = EaseInOut });
    ecs_entity_t custom = tr_entity(world, &(TransitionSpec){
        .duration = 1.0f, .timing_function = CubicBezier,
        .bezier = { 0.42f, 0.0f, 0.58f, 1.0f } });
    ecs_entity_t rgb[2];
    int32_t i;
    for (i = 0; i < 2; i ++) {
        rgb[i] = ecs_new(world);
        ecs_set(world, rgb[i], Rgb, {0, 200, 10});
    }

    box_transition_set(world, rgb[0], ecs_id(Rgb), &(TransitionSpec){
        .duration = 1.0f, .timing_function = EaseInOut });
    box_transition_set(world, rgb[1], ecs_id(Rgb), &(TransitionSpec){
        .duration = 1.0f, .timing_function = CubicBezier,
        .bezier = { 0.42f, 0.0f, 0.58f, 1.0f } });
    tr_frame(world);

    ecs_set(world, preset, Position, {4, -2, 1});
    ecs_set(world, custom, Position, {4, -2, 1});
    ecs_set(world, rgb[0], Rgb, {255, 100, 10});
    ecs_set(world, rgb[1], Rgb, {255, 100, 10});
    for (i = 0; i < 6; i ++) {
        tr_frame(world);
        Position a = tr_position(world, preset);
        Position b = tr_position(world, custom);
        test_flt(a.x, b.x);
        test_flt(a.y, b.y);
        test_flt(a.z, b.z);
        Rgb ca = tr_rgb(world, rgb[0]);
        Rgb cb = tr_rgb(world, rgb[1]);
        test_int(ca.r, cb.r);
        test_int(ca.g, cb.g);
        test_int(ca.b, cb.b);
    }

    for (i = 0; i <= 16; i ++) {
        float t = (float)i / 16.0f;
        test_flt(box_transition_ease(EaseInOut, t),
            tr_ease(&(TransitionBezier){ 0.42f, 0.0f, 0.58f, 1.0f }, t));
        test_flt(box_transition_ease(EaseIn, t),
            tr_ease(&(TransitionBezier){ 0.42f, 0.0f, 1.0f, 1.0f }, t));
        test_flt(box_transition_ease(EaseOut, t),
            tr_ease(&(TransitionBezier){ 0.0f, 0.0f, 0.58f, 1.0f }, t));
        test_flt(box_transition_ease_bezier(EaseInOut, &tr_bezier, t),
            box_transition_ease(EaseInOut, t));
    }

    test_flt(box_transition_ease_bezier(CubicBezier, NULL, 0.25f), 0.25f);
    test_int(box_render_transition_test_active(world), 0);
    tr_fini(world, null);
}

/* A y above 1 moves Position past its target and a y below 0 back past its start, and both end
 * on the target; the resolver extrapolates for progress outside 0..1 */
void Transition_bezier_overshoot_float(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    TransitionBezier over = { 0.3f, 1.8f, 0.7f, 1.8f };
    TransitionBezier under = { 0.3f, -0.8f, 0.7f, -0.8f };
    ecs_entity_t e[2];
    e[0] = tr_entity(world, &(TransitionSpec){
        .duration = 1.0f, .timing_function = CubicBezier, .bezier = over });
    e[1] = tr_entity(world, &(TransitionSpec){
        .duration = 1.0f, .timing_function = CubicBezier, .bezier = under });
    test_bool(box_transition_valid(world, ecs_id(Position),
        box_transition_get(world, e[0], ecs_id(Position))), true);
    test_bool(box_transition_valid(world, ecs_id(Position),
        box_transition_get(world, e[1], ecs_id(Position))), true);
    tr_frame(world);

    ecs_set(world, e[0], Position, {4, 0, 0});
    ecs_set(world, e[1], Position, {4, 0, 0});
    tr_frame(world);
    test_flt(tr_position(world, e[0]).x, 0);
    test_flt(tr_position(world, e[1]).x, 0);

    tr_frame(world);
    tr_frame(world);
    float to = tr_ease(&over, 0.5f);
    float tu = tr_ease(&under, 0.5f);
    test_assert(to > 1.0f);
    test_assert(tu < 0.0f);
    Position a = tr_position(world, e[0]);
    Position b = tr_position(world, e[1]);
    test_flt(a.x, 4.0f * to);
    test_assert(a.x > 4.0f);
    test_flt(b.x, 4.0f * tu);
    test_assert(b.x < 0.0f);
    test_int(box_render_transition_test_active(world), 2);

    tr_frames(world, 2);
    test_flt(tr_position(world, e[0]).x, 4);
    test_flt(tr_position(world, e[1]).x, 4);
    test_int(box_render_transition_test_active(world), 0);

    uint8_t from[BOX_RENDER_TRANSITION_VALUE] = {0}, target[BOX_RENDER_TRANSITION_VALUE] = {0};
    Position out = {0};
    box_render_transition_pack(BOX_RENDER_TRANSITION_POSITION, &(Position){0, 0, 0}, from);
    box_render_transition_pack(BOX_RENDER_TRANSITION_POSITION, &(Position){4, 2, 0}, target);
    uint8_t mixed[BOX_RENDER_TRANSITION_VALUE];
    box_render_transition_mix(BOX_RENDER_TRANSITION_POSITION, from, target, 1.5f, mixed);
    ecs_os_memcpy(&out, mixed, ECS_SIZEOF(Position));
    test_flt(out.x, 6);
    test_flt(out.y, 3);
    box_render_transition_mix(BOX_RENDER_TRANSITION_POSITION, from, target, -0.5f, mixed);
    ecs_os_memcpy(&out, mixed, ECS_SIZEOF(Position));
    test_flt(out.x, -2);
    test_flt(out.y, -1);

    tr_fini(world, null);
}

/* An overshooting Rgb clamps a field at 255 or 0 when it is rounded, while a field with room
 * passes its target or goes back past its start */
void Transition_bezier_overshoot_rgb(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    TransitionBezier beziers[2] = {
        { 0.3f, 1.8f, 0.7f, 1.8f },
        { 0.3f, -0.8f, 0.7f, -0.8f }
    };
    ecs_entity_t e[2];
    int32_t i, f;
    for (i = 0; i < 2; i ++) {
        e[i] = ecs_new(world);
        ecs_set(world, e[i], Rgb, {0, 200, 10});
        box_transition_set(world, e[i], ecs_id(Rgb), &(TransitionSpec){
            .duration = 1.0f, .timing_function = CubicBezier, .bezier = beziers[i] });
    }

    tr_frame(world);
    for (i = 0; i < 2; i ++) {
        ecs_set(world, e[i], Rgb, {255, 100, 10});
    }

    tr_frame(world);
    for (f = 1; f < 4; f ++) {
        tr_frame(world);
        for (i = 0; i < 2; i ++) {
            float t = tr_ease(&beziers[i], (float)f / 4.0f);
            float r = glm_clamp(255.0f * t, 0.0f, 255.0f);
            float g = glm_clamp(200.0f - 100.0f * t, 0.0f, 255.0f);
            Rgb v = tr_rgb(world, e[i]);
            test_int(v.r, (uint8_t)(r + 0.5f));
            test_int(v.g, (uint8_t)(g + 0.5f));
            test_int(v.b, 10);
        }

        if (f == 2) {
            test_assert(tr_ease(&beziers[0], 0.5f) > 1.0f);
            test_assert(tr_ease(&beziers[1], 0.5f) < 0.0f);
            Rgb over = tr_rgb(world, e[0]);
            Rgb under = tr_rgb(world, e[1]);
            test_int(over.r, 255);
            test_assert(over.g < 100);
            test_int(under.r, 0);
            test_assert(under.g > 200);
        }
    }

    tr_frames(world, 2);
    for (i = 0; i < 2; i ++) {
        Rgb v = tr_rgb(world, e[i]);
        test_int(v.r, 255);
        test_int(v.g, 100);
        test_int(v.b, 10);
    }

    test_int(box_render_transition_test_active(world), 0);
    tr_fini(world, null);
}

/* Handles with an x outside 0..1 or that are not finite make an entry invalid, from C and from
 * script: the change snaps without a start record. The other timing functions ignore the handles,
 * and a valid curve set afterwards animates again */
void Transition_bezier_invalid_x(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    TransitionBezier invalid[] = {
        { 1.5f, 0.0f, 0.5f, 1.0f },
        { 0.5f, 0.0f, -0.1f, 1.0f },
        { -0.01f, 0.0f, 1.0f, 1.0f },
        { 0.5f, 0.0f, NAN, 1.0f },
        { 0.5f, INFINITY, 0.5f, 1.0f }
    };
    int32_t i;
    for (i = 0; i < (int32_t)(sizeof(invalid) / sizeof(invalid[0])); i ++) {
        test_bool(box_transition_bezier_valid(&invalid[i]), false);
        test_bool(box_transition_valid(world, ecs_id(Position), &(TransitionSpec){
            .duration = 1.0f, .timing_function = CubicBezier, .bezier = invalid[i] }), false);
        test_bool(box_transition_valid(world, ecs_id(Position), &(TransitionSpec){
            .duration = 1.0f, .timing_function = EaseOut, .bezier = invalid[i] }), true);
    }

    test_bool(box_transition_bezier_valid(&(TransitionBezier){ 0, -3, 1, 4 }), true);
    test_bool(box_transition_bezier_valid(&(TransitionBezier){ 0, 0, 0, 0 }), true);
    test_bool(box_transition_bezier_valid(NULL), false);

    ecs_entity_t e = tr_entity(world, &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    ecs_set(world, e, Position, {4, 0, 0});
    tr_frames(world, 2);
    test_flt(tr_position(world, e).x, 1);

    int level = ecs_log_set_level(-4);
    box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){
        .duration = 1.0f, .timing_function = CubicBezier, .bezier = invalid[0] });
    ecs_log_set_level(level);
    tr_frame(world);
    test_flt(tr_position(world, e).x, 4);
    test_int(box_render_transition_test_active(world), 0);

    ecs_set(world, e, Position, {0, 0, 0});
    tr_frame(world);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_START), 0);
    test_int(box_render_transition_test_active(world), 0);

    level = ecs_log_set_level(-4);
    test_assert(ecs_script_run(world, "invalid",
        "using box.transform\n"
        "using box.transition\n"
        "bad {\n"
        "  Position: {0, 0, 0}\n"
        "  Transition: [Position: {duration: 1, timing_function: CubicBezier, "
            "bezier: {0.2, 0, 1.2, 1}}]\n"
        "}\n", NULL) == 0);
    ecs_log_set_level(level);
    ecs_entity_t bad = ecs_lookup(world, "bad");
    test_assert(bad != 0);
    test_bool(box_transition_valid(world, ecs_id(Position),
        box_transition_get(world, bad, ecs_id(Position))), false);
    tr_frame(world);
    ecs_set(world, bad, Position, {4, 0, 0});
    tr_frame(world);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_START), 0);
    test_int(box_render_transition_test_active(world), 0);

    box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){
        .duration = 1.0f, .timing_function = CubicBezier, .bezier = tr_bezier });
    tr_frame(world);
    test_flt(tr_position(world, e).x, 0);
    ecs_set(world, e, Position, {4, 0, 0});
    tr_frames(world, 3);
    test_flt(tr_position(world, e).x, 4.0f * tr_ease(&tr_bezier, 0.5f));

    tr_fini(world, null);
}

/* A custom transition interrupted halfway starts from the value it renders and runs the whole
 * curve again to the new target */
void Transition_bezier_interrupt(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    ecs_entity_t e = tr_entity(world, &(TransitionSpec){
        .duration = 1.0f, .timing_function = CubicBezier, .bezier = tr_bezier });
    tr_frame(world);

    ecs_set(world, e, Position, {4, 0, 0});
    tr_frames(world, 3);
    float mid = 4.0f * tr_ease(&tr_bezier, 0.5f);
    test_flt(tr_position(world, e).x, mid);

    ecs_set(world, e, Position, {0, 0, 0});
    tr_frame(world);
    test_flt(tr_position(world, e).x, 4.0f * tr_ease(&tr_bezier, 0.75f));
    float from = 4.0f * tr_ease(&tr_bezier, 0.75f);
    int32_t i;
    for (i = 1; i < 4; i ++) {
        tr_frame(world);
        float t = tr_ease(&tr_bezier, (float)i / 4.0f);
        test_flt(tr_position(world, e).x, from - from * t);
        test_int(box_render_transition_test_active(world), 1);
    }

    tr_frame(world);
    test_flt(tr_position(world, e).x, 0);
    test_int(box_render_transition_test_active(world), 0);

    tr_fini(world, null);
}
