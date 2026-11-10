/**
 * @file Vignette.c
 * @brief Tests for the C twin of the vignette factor: at the center, the radius and the corner.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"

/** Get the default vignette settings.
 *
 * @return The settings a constructed Vignette has
 */
static Vignette vignette_defaults(void) {
    return (Vignette){
        .intensity = 0.3f, .radius = 0.75f, .smoothness = 0.45f, .roundness = 1,
        .center = {0.5f, 0.5f}
    };
}

/** Smoothstep of a value between two edges.
 *
 * @param e0 The lower edge
 * @param e1 The upper edge
 * @param x The value
 * @return The smoothstep
 */
static float vignette_smoothstep(
    float e0,
    float e1,
    float x)
{
    float t = glm_clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);

    return t * t * (3.0f - 2.0f * t);
}

void Vignette_center_is_zero(void) {
    // lighting/effects
    Vignette v = vignette_defaults();
    test_flt(box_render_vignette_factor(&v, 1, 0.5f, 0.5f), 0);
    test_flt(box_render_vignette_factor(&v, 16.0f / 9.0f, 0.5f, 0.5f), 0);

    v.radius = 0;
    test_flt(box_render_vignette_factor(&v, 1, 0.5f, 0.5f), 0);

    float c[3] = {0.8f, 0.5f, 0.2f}, out[3];
    v = vignette_defaults();
    v.intensity = 1;
    box_render_vignette_apply(&v, 1, 0.5f, 0.5f, c, out);
    test_flt(out[0], 0.8f);
    test_flt(out[1], 0.5f);
    test_flt(out[2], 0.2f);
}

void Vignette_radius_starts_falloff(void) {
    // lighting/effects
    Vignette v = vignette_defaults();
    v.radius = 0.5f;
    v.smoothness = 0.4f;
    // Half a diagonal of a square image is sqrt(0.5) in uv
    float half = sqrtf(0.5f);
    test_flt(box_render_vignette_factor(&v, 1, 0.5f + 0.5f * half, 0.5f), 0);
    test_flt(box_render_vignette_factor(&v, 1, 0.5f, 0.5f - 0.5f * half), 0);
    test_assert(box_render_vignette_factor(&v, 1, 0.5f + 0.55f * half, 0.5f) > 0);
    test_flt(box_render_vignette_factor(&v, 1, 0.5f + 0.7f * half, 0.5f), 0.5f);
    test_flt(box_render_vignette_factor(&v, 1, 0.5f + 0.9f * half, 0.5f), 1);
}

void Vignette_corner(void) {
    // lighting/effects
    Vignette v = vignette_defaults();
    float expected = vignette_smoothstep(0.75f, 1.2f, 1.0f);
    test_flt(box_render_vignette_factor(&v, 1, 0, 0), expected);
    test_flt(box_render_vignette_factor(&v, 1, 1, 1), expected);
    // The corner is a half diagonal away whatever the aspect and roundness
    test_flt(box_render_vignette_factor(&v, 16.0f / 9.0f, 1, 0), expected);
    v.roundness = 0;
    test_flt(box_render_vignette_factor(&v, 16.0f / 9.0f, 0, 1), expected);

    // Past the falloff the corner darkens by the intensity
    v = vignette_defaults();
    v.radius = 0.5f;
    v.smoothness = 0.3f;
    test_flt(box_render_vignette_factor(&v, 1, 0, 0), 1);
    float c[3] = {1, 1, 1}, out[3];
    box_render_vignette_apply(&v, 1, 0, 0, c, out);
    test_flt(out[0], 0.7f);
    test_flt(out[1], 0.7f);
    test_flt(out[2], 0.7f);
}

void Vignette_hard_edge(void) {
    // lighting/effects
    Vignette v = vignette_defaults();
    v.radius = 0.5f;
    v.smoothness = 0;
    float half = sqrtf(0.5f);
    test_flt(box_render_vignette_factor(&v, 1, 0.5f + 0.49f * half, 0.5f), 0);
    test_flt(box_render_vignette_factor(&v, 1, 0.5f + 0.51f * half, 0.5f), 1);

    v.radius = 0;
    test_flt(box_render_vignette_factor(&v, 1, 0.5f, 0.5f), 1);
    test_flt(box_render_vignette_factor(&v, 1, 0, 0), 1);
}

void Vignette_roundness(void) {
    // lighting/effects
    Vignette v = vignette_defaults();
    v.radius = 0.1f;
    v.smoothness = 0.8f;
    float aspect = 16.0f / 9.0f;
    // A circle: the same distance in pixels right and down of the center
    float h = box_render_vignette_factor(&v, aspect, 0.5f + 0.2f / aspect, 0.5f);
    float d = box_render_vignette_factor(&v, aspect, 0.5f, 0.5f + 0.2f);
    test_assert(h > 0);
    test_flt(h, d);

    // Following the rectangle: the same fraction of the image right and down
    v.roundness = 0;
    h = box_render_vignette_factor(&v, aspect, 0.5f + 0.2f, 0.5f);
    d = box_render_vignette_factor(&v, aspect, 0.5f, 0.5f + 0.2f);
    test_assert(h > 0);
    test_flt(h, d);
}

void Vignette_color_tints(void) {
    // lighting/effects
    Vignette v = vignette_defaults();
    v.intensity = 1;
    v.radius = 0.2f;
    v.smoothness = 0.2f;
    v.color = (rgb){255, 0, 255};
    float c[3] = {0.5f, 0.5f, 0.5f}, out[3];
    box_render_vignette_apply(&v, 1, 0, 0, c, out);
    test_flt(out[0], 0.5f);
    test_flt(out[1], 0);
    test_flt(out[2], 0.5f);

    v.intensity = 0.5f;
    box_render_vignette_apply(&v, 1, 0, 0, c, out);
    test_flt(out[1], 0.25f);
}
