/**
 * @file Degenerate.c
 * @brief Tests for empty, zero sized, flat, reversed and coincident inputs.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

void Degenerate_an_empty_subtree_has_no_proxy(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prim = {
        .transform = mat4_identity(),
        .min = vec3_new(-1, -1, -1),
        .max = vec3_new(1, 1, 1)
    };
    box_lod_proxy_desc_t empty = { .primitives = &prim, .count = 0, .max_shapes = 4 };
    box_lod_proxy_desc_t none = { .primitives = NULL, .count = 4, .max_shapes = 4 };
    box_lod_proxy_t proxy;

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &empty, &proxy), false);
    test_int(proxy.count, 0);
    test_flt(proxy.error, 0);
    test_assert(proxy.shapes == NULL);

    test_bool(box_lod_proxy_build(&builder, &none, &proxy), false);
    test_int(proxy.count, 0);

    box_lod_builder_fini(&builder);
}

void Degenerate_a_zero_sized_primitive_is_finite(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[1] = {{
        .transform = mat4_translation(vec3_new(2, 3, 4)),
        .min = vec3_new(0, 0, 0),
        .max = vec3_new(0, 0, 0),
        .material = 5
    }};
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 1, .max_shapes = 4 };
    box_lod_proxy_t proxy;

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_int(proxy.count, 1);
    test_int(proxy.shapes[0].kind, BoxLodShapeBox);
    test_flt(proxy.shapes[0].box.x, 0);
    test_flt(proxy.shapes[0].position.x, 2);
    test_flt(proxy.shapes[0].position.y, 3);
    test_flt(proxy.shapes[0].position.z, 4);
    test_flt(proxy.error, 0);
    test_assert(proxy.error == proxy.error);

    box_lod_builder_fini(&builder);
}

void Degenerate_overlapping_primitives_become_one_shape(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[5];
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 5, .max_shapes = 4 };
    box_lod_proxy_t proxy;
    int32_t i;

    for (i = 0; i < 5; i ++) {
        prims[i].transform = mat4_identity();
        prims[i].min = vec3_new(-0.5f, -0.5f, -0.5f);
        prims[i].max = vec3_new(0.5f, 0.5f, 0.5f);
        prims[i].material = 1;
        prims[i].volume = 0;
    }

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_int(proxy.count, 1);
    test_int(proxy.shapes[0].count, 5);
    test_flt(proxy.shapes[0].box.x, 1);
    test_flt(proxy.error, 0);

    box_lod_builder_fini(&builder);
}

void Degenerate_more_shapes_than_primitives(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[2] = {
        { .transform = mat4_translation(vec3_new(-8, 0, 0)),
          .min = vec3_new(-1, -1, -1), .max = vec3_new(1, 1, 1), .material = 1 },
        { .transform = mat4_translation(vec3_new(8, 0, 0)),
          .min = vec3_new(-1, -1, -1), .max = vec3_new(1, 1, 1), .material = 1 }
    };
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 2, .max_shapes = 16 };
    box_lod_proxy_t proxy;

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_int(proxy.count, 2);
    test_flt(proxy.error, 0);
    test_int(proxy.shapes[0].count, 1);
    test_int(proxy.shapes[1].count, 1);

    box_lod_builder_fini(&builder);
}

void Degenerate_a_flat_primitive_keeps_its_plane(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[1] = {{
        .transform = mat4_translation(vec3_new(0, 1, 0)),
        .min = vec3_new(-2, 0, -2),
        .max = vec3_new(2, 0, 2),
        .material = 3
    }};
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 1, .max_shapes = 2 };
    box_lod_proxy_t proxy;

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_int(proxy.count, 1);
    test_int(proxy.shapes[0].kind, BoxLodShapeBox);
    test_flt(proxy.shapes[0].box.x, 4);
    test_flt(proxy.shapes[0].box.y, 0);
    test_flt(proxy.shapes[0].box.z, 4);
    test_flt(proxy.shapes[0].position.y, 1);
    test_flt(proxy.error, 0);

    box_lod_builder_fini(&builder);
}

void Degenerate_reversed_bounds_are_normalized(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[1] = {{
        .transform = mat4_translation(vec3_new(5, 0, 0)),
        .min = vec3_new(1, 1, 1),
        .max = vec3_new(-1, -1, -1),
        .material = 1
    }};
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 1, .max_shapes = 2 };
    box_lod_proxy_t proxy;

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_int(proxy.count, 1);
    test_flt(proxy.shapes[0].box.x, 2);
    test_flt(proxy.shapes[0].box.y, 2);
    test_flt(proxy.shapes[0].box.z, 2);
    test_flt(proxy.shapes[0].position.x, 5);
    test_flt(proxy.min.x, 4);
    test_flt(proxy.max.x, 6);

    box_lod_builder_fini(&builder);
}
