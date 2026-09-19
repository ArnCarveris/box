/**
 * @file Cluster.c
 * @brief Tests for the clustering of a subtree into a handful of proxy shapes.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

void Cluster_one_primitive_is_one_box(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[1] = {{
        .transform = mat4_translation(vec3_new(3, 1, 2)),
        .min = vec3_new(-1, -2, -3),
        .max = vec3_new(1, 2, 3),
        .material = 7
    }};
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 1, .max_shapes = 4 };
    box_lod_proxy_t proxy;

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_int(proxy.count, 1);
    test_int(proxy.shapes[0].kind, BoxLodShapeBox);
    test_int(proxy.shapes[0].count, 1);
    test_assert(proxy.shapes[0].material == 7);
    test_flt(proxy.shapes[0].position.x, 3);
    test_flt(proxy.shapes[0].position.y, 1);
    test_flt(proxy.shapes[0].position.z, 2);
    test_flt(proxy.shapes[0].box.x, 2);
    test_flt(proxy.shapes[0].box.y, 4);
    test_flt(proxy.shapes[0].box.z, 6);
    test_flt(proxy.error, 0);

    box_lod_builder_fini(&builder);
}

void Cluster_two_groups_split_by_distance(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[4] = {
        { .transform = mat4_translation(vec3_new(-10.5f, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f) },
        { .transform = mat4_translation(vec3_new(-9.5f, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f) },
        { .transform = mat4_translation(vec3_new(9.5f, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f) },
        { .transform = mat4_translation(vec3_new(10.5f, 0, 0)),
          .min = vec3_new(-0.5f, -0.5f, -0.5f), .max = vec3_new(0.5f, 0.5f, 0.5f) }
    };
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 4, .max_shapes = 2 };
    box_lod_proxy_t proxy;

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_int(proxy.count, 2);
    test_int(proxy.shapes[0].count, 2);
    test_int(proxy.shapes[1].count, 2);
    test_flt(fabs(proxy.shapes[0].position.x), 10);
    test_flt(fabs(proxy.shapes[1].position.x), 10);
    test_flt(proxy.shapes[0].box.x, 2);
    test_flt(proxy.shapes[1].box.x, 2);
    test_assert(proxy.shapes[0].position.x * proxy.shapes[1].position.x < 0);

    box_lod_builder_fini(&builder);
}

void Cluster_the_budget_caps_the_shape_count(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[8];
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 8, .max_shapes = 3 };
    box_lod_proxy_t proxy;
    int32_t i;

    for (i = 0; i < 8; i ++) {
        prims[i].transform = mat4_translation(vec3_new((float)i * 4.0f, 0, 0));
        prims[i].min = vec3_new(-0.5f, -0.5f, -0.5f);
        prims[i].max = vec3_new(0.5f, 0.5f, 0.5f);
        prims[i].material = 1;
        prims[i].volume = 0;
    }

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_int(proxy.count, 3);

    desc.max_shapes = 8;
    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_int(proxy.count, 8);
    test_flt(proxy.error, 0);

    box_lod_builder_fini(&builder);
}

void Cluster_more_shapes_lower_the_error(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[8];
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 8, .max_shapes = 1 };
    box_lod_proxy_t proxy;
    float coarse, fine;
    int32_t i;

    for (i = 0; i < 8; i ++) {
        prims[i].transform = mat4_translation(vec3_new((float)(i % 4) * 6.0f,
            0, (float)(i / 4) * 6.0f));
        prims[i].min = vec3_new(-0.5f, -0.5f, -0.5f);
        prims[i].max = vec3_new(0.5f, 0.5f, 0.5f);
        prims[i].material = 1;
        prims[i].volume = 0;
    }

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    coarse = proxy.error;

    desc.max_shapes = 4;
    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    fine = proxy.error;

    test_assert(coarse > fine);
    test_assert(fine > 0);

    box_lod_builder_fini(&builder);
}

void Cluster_a_shape_covers_the_bounds_of_its_inputs(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[3] = {
        { .transform = mat4_translation(vec3_new(0, 0, 0)),
          .min = vec3_new(-1, -1, -1), .max = vec3_new(1, 1, 1) },
        { .transform = mat4_translation(vec3_new(8, 0, 0)),
          .min = vec3_new(-1, -1, -1), .max = vec3_new(1, 1, 1) },
        { .transform = mat4_translation(vec3_new(4, 1, 0)),
          .min = vec3_new(-1, -1, -1), .max = vec3_new(1, 1, 1) }
    };
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 3, .max_shapes = 1 };
    box_lod_proxy_t proxy;
    vec3 lo, hi;

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_int(proxy.count, 1);
    test_int(proxy.shapes[0].kind, BoxLodShapeBox);
    test_int(proxy.shapes[0].count, 3);

    lo = vec3_sub(proxy.shapes[0].position, vec3_scale(vec3_new(
        proxy.shapes[0].box.x, proxy.shapes[0].box.y, proxy.shapes[0].box.z), 0.5f));
    hi = vec3_add(proxy.shapes[0].position, vec3_scale(vec3_new(
        proxy.shapes[0].box.x, proxy.shapes[0].box.y, proxy.shapes[0].box.z), 0.5f));

    test_flt(lo.x, -1);
    test_flt(lo.y, -1);
    test_flt(lo.z, -1);
    test_flt(hi.x, 9);
    test_flt(hi.y, 2);
    test_flt(hi.z, 1);

    box_lod_builder_fini(&builder);
}

void Cluster_a_round_cluster_becomes_a_sphere(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[8];
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 8, .max_shapes = 1 };
    box_lod_proxy_t proxy;
    int32_t i;

    for (i = 0; i < 8; i ++) {
        prims[i].transform = mat4_translation(vec3_new(
            (i & 1) ? 2.0f : -2.0f, (i & 2) ? 2.0f : -2.0f, (i & 4) ? 2.0f : -2.0f));
        prims[i].min = vec3_new(-0.1f, -0.1f, -0.1f);
        prims[i].max = vec3_new(0.1f, 0.1f, 0.1f);
        prims[i].material = 1;
        prims[i].volume = 0;
    }

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_int(proxy.count, 1);
    test_int(proxy.shapes[0].kind, BoxLodShapeSphere);
    test_int(proxy.shapes[0].count, 8);
    test_assert(fabs(proxy.shapes[0].sphere.radius - 2.1f) < 0.001f);
    test_int(proxy.shapes[0].sphere.segments, BOX_LOD_SEGMENTS);
    test_bool(proxy.shapes[0].sphere.smooth, true);

    box_lod_builder_fini(&builder);
}

void Cluster_a_tall_cluster_becomes_a_capsule(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[5];
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 5, .max_shapes = 1 };
    box_lod_proxy_t proxy;
    int32_t i;

    for (i = 0; i < 5; i ++) {
        prims[i].transform = mat4_translation(vec3_new(0, (float)i, 0));
        prims[i].min = vec3_new(-0.2f, -0.2f, -0.2f);
        prims[i].max = vec3_new(0.2f, 0.2f, 0.2f);
        prims[i].material = 1;
        prims[i].volume = 0;
    }

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_int(proxy.count, 1);
    test_int(proxy.shapes[0].kind, BoxLodShapeCapsule);
    test_int(proxy.shapes[0].count, 5);
    test_assert(fabs(proxy.shapes[0].capsule.radius - 0.2f) < 0.001f);
    test_assert(fabs(proxy.shapes[0].capsule.length - 4.0f) < 0.001f);
    test_assert(fabs(proxy.shapes[0].position.y - 2.0f) < 0.001f);
    test_int(proxy.shapes[0].capsule.segments, BOX_LOD_SEGMENTS);

    box_lod_builder_fini(&builder);
}

void Cluster_the_proxy_bounds_cover_the_subtree(void)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t prims[2] = {
        { .transform = mat4_translation(vec3_new(-3, 0, 1)),
          .min = vec3_new(-1, -2, -1), .max = vec3_new(1, 2, 1) },
        { .transform = mat4_translation(vec3_new(6, 1, -2)),
          .min = vec3_new(-2, -1, -3), .max = vec3_new(2, 1, 3) }
    };
    box_lod_proxy_desc_t desc = { .primitives = prims, .count = 2, .max_shapes = 4 };
    box_lod_proxy_t proxy;

    box_lod_builder_init(&builder);

    test_bool(box_lod_proxy_build(&builder, &desc, &proxy), true);
    test_flt(proxy.min.x, -4);
    test_flt(proxy.min.y, -2);
    test_flt(proxy.min.z, -5);
    test_flt(proxy.max.x, 8);
    test_flt(proxy.max.y, 2);
    test_flt(proxy.max.z, 2);

    box_lod_builder_fini(&builder);
}
