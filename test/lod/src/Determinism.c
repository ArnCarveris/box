/**
 * @file Determinism.c
 * @brief Tests that the same subtree always produces the same proxy.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>

void Determinism_the_same_input_builds_the_same_proxy(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[24];
    box_lod_shape_t first[8];
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 24, .max_shapes = 5 };
    box_lod_proxy_t proxy;
    uint32_t seed = 12345;
    float error;
    int32_t count;
    int32_t i;

    for (i = 0; i < 24; i ++) {
        seed = seed * 1103515245u + 12345u;
        prims[i].transform = mat4_translation(vec3_new(
            (float)((seed >> 16) & 63), (float)((seed >> 8) & 31),
            (float)((seed >> 4) & 63)));
        prims[i].min = vec3_new(-0.5f, -0.5f, -0.5f);
        prims[i].max = vec3_new(0.5f, 0.5f, 0.5f);
        prims[i].material = (uint64_t)(i % 2) + 1;
        prims[i].volume = 0;
    }

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    count = proxy.count;
    error = proxy.error;
    test_assert(count > 1);
    memcpy(first, proxy.shapes, sizeof(box_lod_shape_t) * (size_t)count);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_int(proxy.count, count);
    test_flt(proxy.error, error);
    test_int(memcmp(first, proxy.shapes, sizeof(box_lod_shape_t) * (size_t)count), 0);

    box_lod_builder_fini(&builder);
}

void Determinism_a_fresh_builder_builds_the_same_proxy(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t first_builder, second_builder;
    box_lod_primitive_t prims[16];
    box_lod_shape_t first[8];
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 16, .max_shapes = 4 };
    box_lod_proxy_t proxy;
    int32_t count;
    int32_t i;

    for (i = 0; i < 16; i ++) {
        prims[i].transform = mat4_translation(vec3_new(
            (float)(i * 7 % 13), (float)(i * 3 % 5), (float)(i * 11 % 17)));
        prims[i].min = vec3_new(-0.5f, -0.5f, -0.5f);
        prims[i].max = vec3_new(0.5f, 0.5f, 0.5f);
        prims[i].material = (uint64_t)(i % 3) + 1;
        prims[i].volume = 0;
    }

    box_lod_builder_init(&first_builder);
    box_lod_builder_init(&second_builder);

    test_bool(box_lod_proxy_build(&first_builder, &desc, &proxy), true);
    count = proxy.count;
    memcpy(first, proxy.shapes, sizeof(box_lod_shape_t) * (size_t)count);

    test_bool(box_lod_proxy_build(&second_builder, &desc, &proxy), true);
    test_int(proxy.count, count);
    test_int(memcmp(first, proxy.shapes, sizeof(box_lod_shape_t) * (size_t)count), 0);

    box_lod_builder_fini(&first_builder);
    box_lod_builder_fini(&second_builder);
}

void Determinism_an_unrelated_build_leaves_no_trace(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[12];
    box_lod_primitive_t other[3] = {
        { .transform = mat4_translation(vec3_new(100, 100, 100)),
          .min = vec3_new(-3, -3, -3), .max = vec3_new(3, 3, 3), .material = 99 },
        { .transform = mat4_translation(vec3_new(-100, 0, 0)),
          .min = vec3_new(-1, -1, -1), .max = vec3_new(1, 1, 1), .material = 98 },
        { .transform = mat4_translation(vec3_new(0, 50, -20)),
          .min = vec3_new(-2, -2, -2), .max = vec3_new(2, 2, 2), .material = 97 }
    };
    box_lod_shape_t first[8];
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 12, .max_shapes = 4 };
    box_lod_proxy_desc_t other_desc = { .primitives = other, .count = 3, .max_shapes = 8 };
    box_lod_proxy_t proxy;
    int32_t count;
    int32_t i;

    for (i = 0; i < 12; i ++) {
        prims[i].transform = mat4_translation(vec3_new(
            (float)(i % 4) * 3.0f, (float)(i / 4) * 2.0f, 0));
        prims[i].min = vec3_new(-0.5f, -0.5f, -0.5f);
        prims[i].max = vec3_new(0.5f, 0.5f, 0.5f);
        prims[i].material = 1;
        prims[i].volume = 0;
    }

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    count = proxy.count;
    memcpy(first, proxy.shapes, sizeof(box_lod_shape_t) * (size_t)count);

    test_bool(box_lod_proxy_build(&builder, &other_desc, &proxy), true);
    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_int(proxy.count, count);
    test_int(memcmp(first, proxy.shapes, sizeof(box_lod_shape_t) * (size_t)count), 0);

    box_lod_builder_fini(&builder);
}
