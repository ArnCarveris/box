/**
 * @file Error.c
 * @brief Tests for the approximation error a generated proxy reports.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

void Error_an_exact_box_has_no_error(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[1] = {{
        .transform = mat4_translation(vec3_new(0, 0, 0)),
        .min = vec3_new(-2, -1, -3),
        .max = vec3_new(2, 1, 3)
    }};
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 1, .max_shapes = 4 };
    box_lod_proxy_t proxy;

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_flt(proxy.error, 0);
    test_flt(proxy.shapes[0].error, 0);

    box_lod_builder_fini(&builder);
}

void Error_a_gap_between_inputs_raises_the_error(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[2] = {
        { .transform = mat4_translation(vec3_new(-2, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f) },
        { .transform = mat4_translation(vec3_new(2, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f) }
    };
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 2, .max_shapes = 1 };
    box_lod_proxy_t proxy;
    float near_error;

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_int(proxy.count, 1);
    near_error = proxy.error;
    test_assert(near_error > 0);

    prims[0].transform = mat4_translation(vec3_new(-5, 0, 0));
    prims[1].transform = mat4_translation(vec3_new(5, 0, 0));
    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_assert(proxy.error > near_error);

    box_lod_builder_fini(&builder);
}

void Error_splitting_a_gap_removes_the_error(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[2] = {
        { .transform = mat4_translation(vec3_new(-5, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f) },
        { .transform = mat4_translation(vec3_new(5, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f) }
    };
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 2, .max_shapes = 2 };
    box_lod_proxy_t proxy;

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_int(proxy.count, 2);
    test_flt(proxy.error, 0);

    box_lod_builder_fini(&builder);
}

void Error_the_error_bound_buys_more_shapes(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[2] = {
        { .transform = mat4_translation(vec3_new(-5, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f) },
        { .transform = mat4_translation(vec3_new(5, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f) }
    };
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 2, .max_shapes = 1 };
    box_lod_proxy_t proxy;

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_int(proxy.count, 1);
    test_assert(proxy.error > 1.0f);

    desc.max_error = 1.0f;
    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_int(proxy.count, 2);
    test_assert(proxy.error <= 1.0f);

    box_lod_builder_fini(&builder);
}

void Error_the_error_is_the_edge_of_the_missed_volume(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[2] = {
        { .transform = mat4_translation(vec3_new(-5, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f) },
        { .transform = mat4_translation(vec3_new(5, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f) }
    };
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 2, .max_shapes = 1 };
    box_lod_proxy_t proxy;

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_int(proxy.count, 1);
    test_assert(fabs(proxy.error - cbrt(9.0)) < 0.001);
    test_assert(fabs(proxy.shapes[0].error - proxy.error) < 0.001);

    box_lod_builder_fini(&builder);
}

void Error_the_error_scales_with_the_subtree(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[2] = {
        { .transform = mat4_translation(vec3_new(-5, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f) },
        { .transform = mat4_translation(vec3_new(5, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f) }
    };
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 2, .max_shapes = 1 };
    box_lod_proxy_t proxy;
    float small;

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    small = proxy.error;

    prims[0].transform = mat4_translation(vec3_new(-10, 0, 0));
    prims[0].min = vec3_new(-1, -1, -1);
    prims[0].max = vec3_new(1, 1, 1);
    prims[1].transform = mat4_translation(vec3_new(10, 0, 0));
    prims[1].min = vec3_new(-1, -1, -1);
    prims[1].max = vec3_new(1, 1, 1);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_assert(fabs(proxy.error - small * 2.0f) < 0.001f);

    box_lod_builder_fini(&builder);
}
