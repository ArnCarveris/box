/**
 * @file Simplify.c
 * @brief Tests for the error bounded simplification of a merged mesh.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

/** Quads along one side of the grid these tests simplify */
#define TEST_SIDE (20)

/** Vertices of that grid */
#define TEST_VERTS (441)

/** Indices of that grid */
#define TEST_INDICES (2400)

void Simplify_a_flat_grid_loses_triangles(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    vec3 verts[TEST_VERTS];
    uint32_t indices[TEST_INDICES];
    box_lod_mesh_source_t sources[1];
    box_lod_merge_desc_t merge = { .sources = sources, .count = 1 };
    box_lod_simplify_desc_t desc = { .ratio = 0.25f };
    box_lod_mesh_t mesh;
    int32_t x, z, n = 0;

    for (z = 0; z <= TEST_SIDE; z ++) {
        for (x = 0; x <= TEST_SIDE; x ++) {
            verts[z * (TEST_SIDE + 1) + x] = vec3_new((float)x, 0, (float)z);
        }
    }

    for (z = 0; z < TEST_SIDE; z ++) {
        for (x = 0; x < TEST_SIDE; x ++) {
            uint32_t a = (uint32_t)(z * (TEST_SIDE + 1) + x);
            indices[n ++] = a;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 1;
            indices[n ++] = a + 1;
            indices[n ++] = a + 1;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 1;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 2;
        }
    }

    sources[0] = (box_lod_mesh_source_t){ .transform = mat4_identity(),
        .vertices = verts, .indices = indices, .vertex_count = TEST_VERTS,
        .index_count = TEST_INDICES, .material = 1 };

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &merge, &mesh), true);
    test_int(mesh.index_count, TEST_INDICES);
    test_bool(box_lod_simplify(&builder, &desc, &mesh), true);
    test_assert(mesh.index_count <= TEST_INDICES / 4);
    test_assert(mesh.index_count >= 3);
    test_assert(mesh.error < 0.001f);
    test_int(mesh.group_count, 1);
    test_assert(mesh.groups[0].material == 1);

    box_lod_mesh_builder_fini(&builder);
}

void Simplify_the_ratio_caps_the_triangles(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    vec3 verts[TEST_VERTS];
    uint32_t indices[TEST_INDICES];
    box_lod_mesh_source_t sources[1];
    box_lod_merge_desc_t merge = { .sources = sources, .count = 1 };
    box_lod_simplify_desc_t desc = { .ratio = 0.5f };
    box_lod_mesh_t mesh;
    int32_t x, z, n = 0;

    for (z = 0; z <= TEST_SIDE; z ++) {
        for (x = 0; x <= TEST_SIDE; x ++) {
            verts[z * (TEST_SIDE + 1) + x] = vec3_new((float)x, 0, (float)z);
        }
    }

    for (z = 0; z < TEST_SIDE; z ++) {
        for (x = 0; x < TEST_SIDE; x ++) {
            uint32_t a = (uint32_t)(z * (TEST_SIDE + 1) + x);
            indices[n ++] = a;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 1;
            indices[n ++] = a + 1;
            indices[n ++] = a + 1;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 1;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 2;
        }
    }

    sources[0] = (box_lod_mesh_source_t){ .transform = mat4_identity(),
        .vertices = verts, .indices = indices, .vertex_count = TEST_VERTS,
        .index_count = TEST_INDICES, .material = 1 };

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &merge, &mesh), true);
    test_bool(box_lod_simplify(&builder, &desc, &mesh), true);
    test_assert(mesh.index_count <= TEST_INDICES / 2);
    test_int(mesh.index_count % 3, 0);

    box_lod_mesh_builder_fini(&builder);
}

void Simplify_a_ratio_of_one_keeps_the_mesh(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    vec3 verts[TEST_VERTS];
    uint32_t indices[TEST_INDICES];
    box_lod_mesh_source_t sources[1];
    box_lod_merge_desc_t merge = { .sources = sources, .count = 1 };
    box_lod_simplify_desc_t desc = { .ratio = 1.0f };
    box_lod_mesh_t mesh;
    int32_t x, z, n = 0;

    for (z = 0; z <= TEST_SIDE; z ++) {
        for (x = 0; x <= TEST_SIDE; x ++) {
            verts[z * (TEST_SIDE + 1) + x] = vec3_new((float)x, 0, (float)z);
        }
    }

    for (z = 0; z < TEST_SIDE; z ++) {
        for (x = 0; x < TEST_SIDE; x ++) {
            uint32_t a = (uint32_t)(z * (TEST_SIDE + 1) + x);
            indices[n ++] = a;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 1;
            indices[n ++] = a + 1;
            indices[n ++] = a + 1;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 1;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 2;
        }
    }

    sources[0] = (box_lod_mesh_source_t){ .transform = mat4_identity(),
        .vertices = verts, .indices = indices, .vertex_count = TEST_VERTS,
        .index_count = TEST_INDICES, .material = 1 };

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &merge, &mesh), true);
    test_bool(box_lod_simplify(&builder, &desc, &mesh), true);
    test_int(mesh.index_count, TEST_INDICES);
    test_int(mesh.vertex_count, TEST_VERTS);
    test_flt(mesh.error, 0);

    box_lod_mesh_builder_fini(&builder);
}

void Simplify_the_error_bound_limits_the_loss(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    vec3 verts[TEST_VERTS];
    uint32_t indices[TEST_INDICES];
    box_lod_mesh_source_t sources[1];
    box_lod_merge_desc_t merge = { .sources = sources, .count = 1 };
    box_lod_simplify_desc_t desc = { .ratio = 0.01f, .max_error = 0.05f };
    box_lod_mesh_t mesh;
    int32_t x, z, n = 0;

    for (z = 0; z <= TEST_SIDE; z ++) {
        for (x = 0; x <= TEST_SIDE; x ++) {
            verts[z * (TEST_SIDE + 1) + x] = vec3_new(
                (float)x, (float)((x * 7 + z * 13) % 5) * 0.02f, (float)z);
        }
    }

    for (z = 0; z < TEST_SIDE; z ++) {
        for (x = 0; x < TEST_SIDE; x ++) {
            uint32_t a = (uint32_t)(z * (TEST_SIDE + 1) + x);
            indices[n ++] = a;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 1;
            indices[n ++] = a + 1;
            indices[n ++] = a + 1;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 1;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 2;
        }
    }

    sources[0] = (box_lod_mesh_source_t){ .transform = mat4_identity(),
        .vertices = verts, .indices = indices, .vertex_count = TEST_VERTS,
        .index_count = TEST_INDICES, .material = 1 };

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &merge, &mesh), true);
    test_bool(box_lod_simplify(&builder, &desc, &mesh), true);
    test_assert(mesh.error <= 0.0501f);
    test_assert(mesh.error > 0);
    test_assert(mesh.index_count > TEST_INDICES / 100);
    test_assert(mesh.groups[0].error <= 0.0501f);

    box_lod_mesh_builder_fini(&builder);
}

void Simplify_the_error_is_measured_in_world_units(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    vec3 verts[TEST_VERTS];
    uint32_t indices[TEST_INDICES];
    box_lod_mesh_source_t sources[1];
    box_lod_merge_desc_t merge = { .sources = sources, .count = 1 };
    box_lod_simplify_desc_t desc = { .ratio = 0.05f };
    box_lod_mesh_t mesh;
    float small, large;
    int32_t x, z, n = 0;

    for (z = 0; z <= TEST_SIDE; z ++) {
        for (x = 0; x <= TEST_SIDE; x ++) {
            verts[z * (TEST_SIDE + 1) + x] = vec3_new(
                (float)x, (float)((x * 7 + z * 13) % 5) * 0.02f, (float)z);
        }
    }

    for (z = 0; z < TEST_SIDE; z ++) {
        for (x = 0; x < TEST_SIDE; x ++) {
            uint32_t a = (uint32_t)(z * (TEST_SIDE + 1) + x);
            indices[n ++] = a;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 1;
            indices[n ++] = a + 1;
            indices[n ++] = a + 1;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 1;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 2;
        }
    }

    sources[0] = (box_lod_mesh_source_t){ .transform = mat4_identity(),
        .vertices = verts, .indices = indices, .vertex_count = TEST_VERTS,
        .index_count = TEST_INDICES, .material = 1 };

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &merge, &mesh), true);
    test_bool(box_lod_simplify(&builder, &desc, &mesh), true);
    small = mesh.error;
    test_assert(small > 0);

    sources[0].transform = mat4_scaling(vec3_new(10, 10, 10));

    test_bool(box_lod_merge(&builder, &merge, &mesh), true);
    test_bool(box_lod_simplify(&builder, &desc, &mesh), true);
    large = mesh.error;

    test_assert(fabs(large - small * 10.0f) < small);

    box_lod_mesh_builder_fini(&builder);
}

void Simplify_a_group_keeps_its_own_triangles(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    vec3 verts[TEST_VERTS];
    uint32_t indices[TEST_INDICES];
    box_lod_mesh_source_t sources[2];
    box_lod_merge_desc_t merge = { .sources = sources, .count = 2 };
    box_lod_simplify_desc_t desc = { .ratio = 0.25f };
    box_lod_mesh_t mesh;
    int32_t x, z, i, n = 0;

    for (z = 0; z <= TEST_SIDE; z ++) {
        for (x = 0; x <= TEST_SIDE; x ++) {
            verts[z * (TEST_SIDE + 1) + x] = vec3_new((float)x, 0, (float)z);
        }
    }

    for (z = 0; z < TEST_SIDE; z ++) {
        for (x = 0; x < TEST_SIDE; x ++) {
            uint32_t a = (uint32_t)(z * (TEST_SIDE + 1) + x);
            indices[n ++] = a;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 1;
            indices[n ++] = a + 1;
            indices[n ++] = a + 1;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 1;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 2;
        }
    }

    sources[0] = (box_lod_mesh_source_t){ .transform = mat4_identity(),
        .vertices = verts, .indices = indices, .vertex_count = TEST_VERTS,
        .index_count = TEST_INDICES, .material = 2 };
    sources[1] = sources[0];
    sources[1].transform = mat4_translation(vec3_new(100, 0, 0));
    sources[1].material = 1;

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &merge, &mesh), true);
    test_int(mesh.group_count, 2);
    test_bool(box_lod_simplify(&builder, &desc, &mesh), true);
    test_int(mesh.group_count, 2);
    test_assert(mesh.groups[0].material == 1);
    test_assert(mesh.groups[1].material == 2);
    test_int(mesh.groups[0].first_index, 0);
    test_int(mesh.groups[1].first_index, mesh.groups[0].index_count);
    test_int(mesh.index_count,
        mesh.groups[0].index_count + mesh.groups[1].index_count);

    for (i = 0; i < mesh.groups[0].index_count; i ++) {
        test_assert(mesh.vertices[mesh.indices[i]].x >= 100);
    }

    for (i = 0; i < mesh.groups[1].index_count; i ++) {
        test_assert(mesh.vertices[mesh.indices[
            mesh.groups[1].first_index + i]].x <= (float)TEST_SIDE);
    }

    box_lod_mesh_builder_fini(&builder);
}

void Simplify_unused_vertices_are_dropped(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    vec3 verts[TEST_VERTS];
    uint32_t indices[TEST_INDICES];
    box_lod_mesh_source_t sources[1];
    box_lod_merge_desc_t merge = { .sources = sources, .count = 1 };
    box_lod_simplify_desc_t desc = { .ratio = 0.1f };
    box_lod_mesh_t mesh;
    int32_t x, z, i, n = 0;

    for (z = 0; z <= TEST_SIDE; z ++) {
        for (x = 0; x <= TEST_SIDE; x ++) {
            verts[z * (TEST_SIDE + 1) + x] = vec3_new((float)x, 0, (float)z);
        }
    }

    for (z = 0; z < TEST_SIDE; z ++) {
        for (x = 0; x < TEST_SIDE; x ++) {
            uint32_t a = (uint32_t)(z * (TEST_SIDE + 1) + x);
            indices[n ++] = a;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 1;
            indices[n ++] = a + 1;
            indices[n ++] = a + 1;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 1;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 2;
        }
    }

    sources[0] = (box_lod_mesh_source_t){ .transform = mat4_identity(),
        .vertices = verts, .indices = indices, .vertex_count = TEST_VERTS,
        .index_count = TEST_INDICES, .material = 1 };

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &merge, &mesh), true);
    test_bool(box_lod_simplify(&builder, &desc, &mesh), true);
    test_assert(mesh.vertex_count < TEST_VERTS);
    test_assert(mesh.vertex_count > 0);

    for (i = 0; i < mesh.index_count; i ++) {
        test_assert(mesh.indices[i] < (uint32_t)mesh.vertex_count);
    }

    test_assert(mesh.min.x >= 0);
    test_assert(mesh.max.x <= (float)TEST_SIDE);

    box_lod_mesh_builder_fini(&builder);
}

void Simplify_the_same_mesh_simplifies_the_same_way(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder, fresh;
    vec3 verts[TEST_VERTS];
    uint32_t indices[TEST_INDICES];
    box_lod_mesh_source_t sources[1];
    box_lod_merge_desc_t merge = { .sources = sources, .count = 1 };
    box_lod_simplify_desc_t desc = { .ratio = 0.2f };
    box_lod_mesh_t first, second;
    int32_t x, z, i, n = 0;

    for (z = 0; z <= TEST_SIDE; z ++) {
        for (x = 0; x <= TEST_SIDE; x ++) {
            verts[z * (TEST_SIDE + 1) + x] = vec3_new(
                (float)x, (float)((x * 7 + z * 13) % 5) * 0.02f, (float)z);
        }
    }

    for (z = 0; z < TEST_SIDE; z ++) {
        for (x = 0; x < TEST_SIDE; x ++) {
            uint32_t a = (uint32_t)(z * (TEST_SIDE + 1) + x);
            indices[n ++] = a;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 1;
            indices[n ++] = a + 1;
            indices[n ++] = a + 1;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 1;
            indices[n ++] = a + (uint32_t)TEST_SIDE + 2;
        }
    }

    sources[0] = (box_lod_mesh_source_t){ .transform = mat4_identity(),
        .vertices = verts, .indices = indices, .vertex_count = TEST_VERTS,
        .index_count = TEST_INDICES, .material = 1 };

    box_lod_mesh_builder_init(&builder);
    box_lod_mesh_builder_init(&fresh);

    test_bool(box_lod_merge(&builder, &merge, &first), true);
    test_bool(box_lod_simplify(&builder, &desc, &first), true);
    test_bool(box_lod_merge(&fresh, &merge, &second), true);
    test_bool(box_lod_simplify(&fresh, &desc, &second), true);
    test_int(first.index_count, second.index_count);
    test_int(first.vertex_count, second.vertex_count);
    test_flt(first.error, second.error);

    for (i = 0; i < first.index_count; i ++) {
        test_int(first.indices[i], second.indices[i]);
    }

    for (i = 0; i < first.vertex_count; i ++) {
        test_flt(first.vertices[i].x, second.vertices[i].x);
        test_flt(first.vertices[i].y, second.vertices[i].y);
        test_flt(first.vertices[i].z, second.vertices[i].z);
    }

    box_lod_mesh_builder_fini(&builder);
    box_lod_mesh_builder_fini(&fresh);
}

void Simplify_an_empty_mesh_has_nothing_to_lose(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    box_lod_merge_desc_t merge = { .sources = NULL, .count = 0 };
    box_lod_simplify_desc_t desc = { .ratio = 0.25f };
    box_lod_mesh_t mesh;

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &merge, &mesh), false);
    test_bool(box_lod_simplify(&builder, &desc, &mesh), false);
    test_int(mesh.index_count, 0);
    test_int(mesh.vertex_count, 0);
    test_flt(mesh.error, 0);

    box_lod_mesh_builder_fini(&builder);
}

void Simplify_a_single_triangle_survives(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    vec3 verts[3] = { {0, 0, 0}, {0, 0, 1}, {1, 0, 0} };
    box_lod_mesh_source_t sources[1] = {{
        .transform = mat4_identity(), .vertices = verts,
        .vertex_count = 3, .material = 6
    }};
    box_lod_merge_desc_t merge = { .sources = sources, .count = 1 };
    box_lod_simplify_desc_t desc = { .ratio = 0.1f };
    box_lod_mesh_t mesh;

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &merge, &mesh), true);
    test_bool(box_lod_simplify(&builder, &desc, &mesh), true);
    test_int(mesh.index_count, 3);
    test_int(mesh.vertex_count, 3);
    test_int(mesh.group_count, 1);
    test_assert(mesh.groups[0].material == 6);

    box_lod_mesh_builder_fini(&builder);
}
