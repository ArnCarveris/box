/**
 * @file Material.c
 * @brief Tests that a proxy keeps the material mix of the subtree it replaces.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

void Material_every_material_keeps_a_shape(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[3] = {
        { .transform = mat4_translation(vec3_new(0, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f),
          .material = 1 },
        { .transform = mat4_translation(vec3_new(1, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f),
          .material = 2 },
        { .transform = mat4_translation(vec3_new(2, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f),
          .material = 3 }
    };
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 3, .max_shapes = 2 };
    box_lod_proxy_t proxy;

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_int(proxy.count, 3);
    test_assert(proxy.shapes[0].material == 1);
    test_assert(proxy.shapes[1].material == 2);
    test_assert(proxy.shapes[2].material == 3);

    box_lod_builder_fini(&builder);
}

void Material_a_shape_carries_the_material_of_its_inputs(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[4] = {
        { .transform = mat4_translation(vec3_new(0, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f),
          .material = 10 },
        { .transform = mat4_translation(vec3_new(1, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f),
          .material = 10 },
        { .transform = mat4_translation(vec3_new(10, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f),
          .material = 20 },
        { .transform = mat4_translation(vec3_new(11, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f),
          .material = 20 }
    };
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 4, .max_shapes = 2 };
    box_lod_proxy_t proxy;

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_int(proxy.count, 2);
    test_assert(proxy.shapes[0].material == 10);
    test_assert(proxy.shapes[1].material == 20);
    test_int(proxy.shapes[0].count, 2);
    test_int(proxy.shapes[1].count, 2);
    test_flt(proxy.shapes[0].position.x, 0.5f);
    test_flt(proxy.shapes[1].position.x, 10.5f);

    box_lod_builder_fini(&builder);
}

void Material_the_budget_follows_the_volume(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[8];
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 8, .max_shapes = 4 };
    box_lod_proxy_t proxy;
    int32_t big = 0, small = 0;
    int32_t i;

    for (i = 0; i < 4; i ++) {
        prims[i].transform = mat4_translation(vec3_new((float)i * 10.0f, 0, 0));
        prims[i].min = vec3_new(-1, -1, -1);
        prims[i].max = vec3_new(1, 1, 1);
        prims[i].material = 1;
        prims[i].volume = 0;
        prims[i + 4].transform = mat4_translation(vec3_new((float)i * 10.0f, 5, 0));
        prims[i + 4].min = vec3_new(-0.25f, -0.25f, -0.25f);
        prims[i + 4].max = vec3_new(0.25f, 0.25f, 0.25f);
        prims[i + 4].material = 2;
        prims[i + 4].volume = 0;
    }

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_int(proxy.count, 4);

    for (i = 0; i < proxy.count; i ++) {
        big += proxy.shapes[i].material == 1;
        small += proxy.shapes[i].material == 2;
    }

    test_int(big, 3);
    test_int(small, 1);

    box_lod_builder_fini(&builder);
}

void Material_materials_never_share_a_shape(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[4] = {
        { .transform = mat4_translation(vec3_new(0, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f),
          .material = 1 },
        { .transform = mat4_translation(vec3_new(1, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f),
          .material = 2 },
        { .transform = mat4_translation(vec3_new(2, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f),
          .material = 1 },
        { .transform = mat4_translation(vec3_new(3, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f),
          .material = 2 }
    };
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 4, .max_shapes = 2 };
    box_lod_proxy_t proxy;

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_int(proxy.count, 2);
    test_assert(proxy.shapes[0].material == 1);
    test_assert(proxy.shapes[1].material == 2);
    test_int(proxy.shapes[0].count, 2);
    test_int(proxy.shapes[1].count, 2);
    test_flt(proxy.shapes[0].position.x, 1);
    test_flt(proxy.shapes[1].position.x, 2);
    test_flt(proxy.shapes[0].box.x, 3);
    test_flt(proxy.shapes[1].box.x, 3);

    box_lod_builder_fini(&builder);
}
