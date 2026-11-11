/**
 * @file ChromaticAberration.c
 * @brief Tests for the C twin of the chromatic aberration: the offset over the distance from the
 * center and the spectral weights of the taps.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"

/** Get the default chromatic aberration settings.
 *
 * @return The settings a constructed ChromaticAberration has
 */
static ChromaticAberration ca_defaults(void) {
    return (ChromaticAberration){
        .intensity = 0.005f, .samples = 8, .center = {0.5f, 0.5f}, .falloff = 2
    };
}

void ChromaticAberration_center_not_shifted(void) {
    // lighting/effects
    ChromaticAberration ca = ca_defaults();
    test_flt(box_render_chromatic_aberration_offset(&ca, 64, 64, 32, 32), 0);
    ca.falloff = 0;
    test_flt(box_render_chromatic_aberration_offset(&ca, 64, 64, 32, 32), 0);
    ca.falloff = 1;
    ca.center[0] = 0.25f;
    test_flt(box_render_chromatic_aberration_offset(&ca, 64, 64, 16, 32), 0);
}

void ChromaticAberration_corner_is_intensity(void) {
    // lighting/effects
    ChromaticAberration ca = ca_defaults();
    test_flt(box_render_chromatic_aberration_corner(&ca, 64, 64), sqrtf(2.0f) * 32);
    test_flt(box_render_chromatic_aberration_offset(&ca, 64, 64, 0, 0), 0.005f * 64);
    test_flt(box_render_chromatic_aberration_offset(&ca, 64, 64, 64, 64), 0.005f * 64);
    test_flt(box_render_chromatic_aberration_offset(&ca, 128, 72, 128, 0), 0.005f * 128);

    // A moved center reaches the intensity at its farthest corner
    ca.center[0] = 0.25f;
    ca.center[1] = 0.25f;
    test_flt(box_render_chromatic_aberration_offset(&ca, 64, 64, 64, 64), 0.005f * 64);
    test_assert(box_render_chromatic_aberration_offset(&ca, 64, 64, 0, 0) < 0.005f * 64);
}

void ChromaticAberration_falloff(void) {
    // lighting/effects
    ChromaticAberration ca = ca_defaults();
    ca.intensity = 0.1f;
    float half = 0.5f * sqrtf(2.0f) * 32;
    float x = 32 + half / sqrtf(2.0f);
    test_flt(box_render_chromatic_aberration_offset(&ca, 64, 64, x, x), 6.4f * 0.25f);
    ca.falloff = 1;
    test_flt(box_render_chromatic_aberration_offset(&ca, 64, 64, x, x), 6.4f * 0.5f);
    ca.falloff = 0;
    test_flt(box_render_chromatic_aberration_offset(&ca, 64, 64, x, x), 6.4f);
    ca.intensity = 0;
    test_flt(box_render_chromatic_aberration_offset(&ca, 64, 64, x, x), 0);
}

void ChromaticAberration_weights_sum_to_one(void) {
    // lighting/effects
    int32_t n, i, c;
    for (n = 3; n <= 16; n ++) {
        float sum[3] = {0, 0, 0}, w[3];
        for (i = 0; i < n; i ++) {
            box_render_chromatic_aberration_weights(n, i, w);
            for (c = 0; c < 3; c ++) {
                test_assert(w[c] >= 0);
                sum[c] += w[c];
            }
        }

        test_flt(sum[0], 1);
        test_flt(sum[1], 1);
        test_flt(sum[2], 1);
    }
}

void ChromaticAberration_weights_spectral(void) {
    // lighting/effects
    float first[3], middle[3], last[3];
    box_render_chromatic_aberration_weights(9, 0, first);
    box_render_chromatic_aberration_weights(9, 4, middle);
    box_render_chromatic_aberration_weights(9, 8, last);
    // Red at -offset, green in the middle, blue at +offset
    test_assert(first[0] > first[1]);
    test_assert(first[2] == 0);
    test_assert(middle[1] > middle[0]);
    test_assert(middle[1] > middle[2]);
    test_assert(last[2] > last[1]);
    test_assert(last[0] == 0);

    // Mirrored: red and blue have the same weights from opposite ends
    int32_t i;
    for (i = 0; i < 9; i ++) {
        float a[3], b[3];
        box_render_chromatic_aberration_weights(9, i, a);
        box_render_chromatic_aberration_weights(9, 8 - i, b);
        test_flt(a[0], b[2]);
        test_flt(a[1], b[1]);
    }

    // Three taps are three shifted copies
    box_render_chromatic_aberration_weights(3, 0, first);
    test_flt(first[0], 1);
    test_flt(first[1], 0);
    test_flt(first[2], 0);
}
