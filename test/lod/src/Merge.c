/**
 * @file Merge.c
 * @brief Tests for merging the meshes of a subtree into one world space mesh.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

void Merge_one_mesh_lands_in_world_space(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    vec3 verts[3] = { {0, 0, 0}, {1, 0, 0}, {0, 1, 0} };
    box_lod_mesh_source_t sources[1] = {{
        .transform = mat4_translation(vec3_new(1, 2, 3)),
        .vertices = verts,
        .vertex_count = 3,
        .material = 9
    }};
    box_lod_merge_desc_t desc = { .sources = sources, .count = 1 };
    box_lod_mesh_t mesh;

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &desc, &mesh), true);
    test_int(mesh.vertex_count, 3);
    test_int(mesh.index_count, 3);
    test_int(mesh.group_count, 1);
    test_assert(mesh.groups[0].material == 9);
    test_int(mesh.groups[0].first_index, 0);
    test_int(mesh.groups[0].index_count, 3);
    test_flt(mesh.vertices[0].x, 1);
    test_flt(mesh.vertices[0].y, 2);
    test_flt(mesh.vertices[0].z, 3);
    test_flt(mesh.vertices[1].x, 2);
    test_flt(mesh.vertices[2].y, 3);
    test_int(mesh.indices[0], 0);
    test_int(mesh.indices[2], 2);
    test_flt(mesh.error, 0);

    box_lod_mesh_builder_fini(&builder);
}

void Merge_two_meshes_join_one_group(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    vec3 verts[3] = { {0, 0, 0}, {1, 0, 0}, {0, 1, 0} };
    box_lod_mesh_source_t sources[2] = {
        { .transform = mat4_identity(), .vertices = verts,
          .vertex_count = 3, .material = 4 },
        { .transform = mat4_translation(vec3_new(10, 0, 0)), .vertices = verts,
          .vertex_count = 3, .material = 4 }
    };
    box_lod_merge_desc_t desc = { .sources = sources, .count = 2 };
    box_lod_mesh_t mesh;

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &desc, &mesh), true);
    test_int(mesh.group_count, 1);
    test_int(mesh.vertex_count, 6);
    test_int(mesh.index_count, 6);
    test_int(mesh.groups[0].index_count, 6);
    test_flt(mesh.vertices[3].x, 10);

    box_lod_mesh_builder_fini(&builder);
}

void Merge_materials_split_into_groups(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    vec3 verts[3] = { {0, 0, 0}, {1, 0, 0}, {0, 1, 0} };
    box_lod_mesh_source_t sources[2] = {
        { .transform = mat4_identity(), .vertices = verts,
          .vertex_count = 3, .material = 7 },
        { .transform = mat4_translation(vec3_new(10, 0, 0)), .vertices = verts,
          .vertex_count = 3, .material = 3 }
    };
    box_lod_merge_desc_t desc = { .sources = sources, .count = 2 };
    box_lod_mesh_t mesh;

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &desc, &mesh), true);
    test_int(mesh.group_count, 2);
    test_assert(mesh.groups[0].material == 3);
    test_assert(mesh.groups[1].material == 7);
    test_int(mesh.groups[0].first_index, 0);
    test_int(mesh.groups[0].index_count, 3);
    test_int(mesh.groups[1].first_index, 3);
    test_int(mesh.groups[1].index_count, 3);
    test_flt(mesh.vertices[mesh.indices[0]].x, 10);

    box_lod_mesh_builder_fini(&builder);
}

void Merge_a_material_gathers_every_source(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    vec3 verts[3] = { {0, 0, 0}, {1, 0, 0}, {0, 1, 0} };
    box_lod_mesh_source_t sources[4] = {
        { .transform = mat4_translation(vec3_new(0, 0, 0)), .vertices = verts,
          .vertex_count = 3, .material = 5 },
        { .transform = mat4_translation(vec3_new(10, 0, 0)), .vertices = verts,
          .vertex_count = 3, .material = 2 },
        { .transform = mat4_translation(vec3_new(20, 0, 0)), .vertices = verts,
          .vertex_count = 3, .material = 5 },
        { .transform = mat4_translation(vec3_new(30, 0, 0)), .vertices = verts,
          .vertex_count = 3, .material = 2 }
    };
    box_lod_merge_desc_t desc = { .sources = sources, .count = 4 };
    box_lod_mesh_t mesh;

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &desc, &mesh), true);
    test_int(mesh.group_count, 2);
    test_assert(mesh.groups[0].material == 2);
    test_assert(mesh.groups[1].material == 5);
    test_int(mesh.groups[0].index_count, 6);
    test_int(mesh.groups[1].index_count, 6);
    test_int(mesh.index_count, 12);
    test_flt(mesh.vertices[mesh.indices[0]].x, 10);
    test_flt(mesh.vertices[mesh.indices[3]].x, 30);
    test_flt(mesh.vertices[mesh.indices[6]].x, 0);
    test_flt(mesh.vertices[mesh.indices[9]].x, 20);

    box_lod_mesh_builder_fini(&builder);
}

void Merge_welding_joins_a_shared_edge(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    vec3 verts_a[3] = { {0, 0, 0}, {1, 0, 0}, {0, 1, 0} };
    vec3 verts_b[3] = { {0, 0, 0}, {1, 0, 0}, {0, -1, 0} };
    vec3 up[3] = { {0, 1, 0}, {0, 1, 0}, {0, 1, 0} };
    vec3 back[3] = { {0, 0, 1}, {0, 0, 1}, {0, 0, 1} };
    box_lod_mesh_source_t sources[2] = {
        { .transform = mat4_identity(), .vertices = verts_a, .normals = back,
          .vertex_count = 3, .material = 1 },
        { .transform = mat4_identity(), .vertices = verts_b, .normals = up,
          .vertex_count = 3, .material = 1 }
    };
    box_lod_merge_desc_t desc = { .sources = sources, .count = 2, .weld = true };
    box_lod_mesh_t mesh;

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &desc, &mesh), true);
    test_int(mesh.vertex_count, 4);
    test_int(mesh.index_count, 6);
    test_flt(mesh.normals[0].x, 0);
    test_assert(fabs(mesh.normals[0].y - 0.7071f) < 0.001f);
    test_assert(fabs(mesh.normals[0].z - 0.7071f) < 0.001f);
    test_flt(mesh.normals[2].z, 1);
    test_flt(mesh.normals[3].y, 1);

    box_lod_mesh_builder_fini(&builder);
}

void Merge_an_unwelded_seam_keeps_both_vertices(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    vec3 verts_a[3] = { {0, 0, 0}, {1, 0, 0}, {0, 1, 0} };
    vec3 verts_b[3] = { {0, 0, 0}, {1, 0, 0}, {0, -1, 0} };
    vec3 up[3] = { {0, 1, 0}, {0, 1, 0}, {0, 1, 0} };
    vec3 back[3] = { {0, 0, 1}, {0, 0, 1}, {0, 0, 1} };
    box_lod_mesh_source_t sources[2] = {
        { .transform = mat4_identity(), .vertices = verts_a, .normals = back,
          .vertex_count = 3, .material = 1 },
        { .transform = mat4_identity(), .vertices = verts_b, .normals = up,
          .vertex_count = 3, .material = 1 }
    };
    box_lod_merge_desc_t desc = { .sources = sources, .count = 2 };
    box_lod_mesh_t mesh;

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &desc, &mesh), true);
    test_int(mesh.vertex_count, 6);
    test_int(mesh.index_count, 6);
    test_flt(mesh.normals[0].z, 1);
    test_flt(mesh.normals[3].y, 1);

    box_lod_mesh_builder_fini(&builder);
}

void Merge_identical_vertices_are_shared(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    vec3 verts[4] = { {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0} };
    vec3 normals[4] = { {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1} };
    uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };
    box_lod_mesh_source_t sources[1] = {{
        .transform = mat4_identity(), .vertices = verts, .normals = normals,
        .indices = indices, .vertex_count = 4, .index_count = 6, .material = 1
    }};
    box_lod_merge_desc_t desc = { .sources = sources, .count = 1 };
    box_lod_mesh_t mesh;

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &desc, &mesh), true);
    test_int(mesh.vertex_count, 4);
    test_int(mesh.index_count, 6);
    test_int(mesh.indices[3], 0);
    test_int(mesh.indices[4], 2);

    box_lod_mesh_builder_fini(&builder);
}

void Merge_a_mesh_without_normals_gets_them(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    vec3 verts[3] = { {0, 0, 0}, {0, 0, 1}, {1, 0, 0} };
    box_lod_mesh_source_t sources[1] = {{
        .transform = mat4_identity(), .vertices = verts,
        .vertex_count = 3, .material = 1
    }};
    box_lod_merge_desc_t desc = { .sources = sources, .count = 1 };
    box_lod_mesh_t mesh;
    int32_t i;

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &desc, &mesh), true);
    test_int(mesh.vertex_count, 3);

    for (i = 0; i < 3; i ++) {
        test_flt(mesh.normals[i].x, 0);
        test_flt(mesh.normals[i].y, 1);
        test_flt(mesh.normals[i].z, 0);
    }

    box_lod_mesh_builder_fini(&builder);
}

void Merge_missing_uvs_and_colors_get_defaults(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    vec3 verts[3] = { {0, 0, 0}, {1, 0, 0}, {0, 1, 0} };
    box_lod_mesh_source_t sources[1] = {{
        .transform = mat4_identity(), .vertices = verts,
        .vertex_count = 3, .material = 1
    }};
    box_lod_merge_desc_t desc = { .sources = sources, .count = 1 };
    box_lod_mesh_t mesh;
    int32_t i;

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &desc, &mesh), true);

    for (i = 0; i < 3; i ++) {
        test_flt(mesh.uvs[i].x, 0);
        test_flt(mesh.uvs[i].y, 0);
        test_int(mesh.colors[i].r, 255);
        test_int(mesh.colors[i].g, 255);
        test_int(mesh.colors[i].b, 255);
        test_int(mesh.colors[i].a, 255);
    }

    box_lod_mesh_builder_fini(&builder);
}

void Merge_uvs_and_colors_are_kept(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    vec3 verts[3] = { {0, 0, 0}, {1, 0, 0}, {0, 1, 0} };
    vec2 uvs[3] = { {0, 0}, {1, 0}, {0, 1} };
    rgba colors[3] = { {10, 20, 30, 40}, {50, 60, 70, 80}, {90, 100, 110, 120} };
    box_lod_mesh_source_t sources[1] = {{
        .transform = mat4_identity(), .vertices = verts, .uvs = uvs,
        .colors = colors, .vertex_count = 3, .material = 1
    }};
    box_lod_merge_desc_t desc = { .sources = sources, .count = 1 };
    box_lod_mesh_t mesh;

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &desc, &mesh), true);
    test_flt(mesh.uvs[1].x, 1);
    test_flt(mesh.uvs[2].y, 1);
    test_int(mesh.colors[0].r, 10);
    test_int(mesh.colors[2].a, 120);

    box_lod_mesh_builder_fini(&builder);
}

void Merge_the_bounds_cover_every_source(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    vec3 verts[3] = { {0, 0, 0}, {1, 0, 0}, {0, 1, 0} };
    box_lod_mesh_source_t sources[2] = {
        { .transform = mat4_translation(vec3_new(-5, 0, -2)), .vertices = verts,
          .vertex_count = 3, .material = 1 },
        { .transform = mat4_translation(vec3_new(10, 3, 0)), .vertices = verts,
          .vertex_count = 3, .material = 1 }
    };
    box_lod_merge_desc_t desc = { .sources = sources, .count = 2 };
    box_lod_mesh_t mesh;

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &desc, &mesh), true);
    test_flt(mesh.min.x, -5);
    test_flt(mesh.min.y, 0);
    test_flt(mesh.min.z, -2);
    test_flt(mesh.max.x, 11);
    test_flt(mesh.max.y, 4);
    test_flt(mesh.max.z, 0);

    box_lod_mesh_builder_fini(&builder);
}

void Merge_an_empty_subtree_merges_to_nothing(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    box_lod_merge_desc_t desc = { .sources = NULL, .count = 0 };
    box_lod_mesh_t mesh;

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &desc, &mesh), false);
    test_int(mesh.vertex_count, 0);
    test_int(mesh.index_count, 0);
    test_int(mesh.group_count, 0);
    test_flt(mesh.min.x, 0);
    test_flt(mesh.max.x, 0);

    box_lod_mesh_builder_fini(&builder);
}

void Merge_an_index_outside_the_mesh_is_dropped(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    vec3 verts[3] = { {0, 0, 0}, {1, 0, 0}, {0, 1, 0} };
    uint32_t indices[6] = { 0, 1, 2, 0, 1, 9 };
    box_lod_mesh_source_t sources[1] = {{
        .transform = mat4_identity(), .vertices = verts, .indices = indices,
        .vertex_count = 3, .index_count = 6, .material = 1
    }};
    box_lod_merge_desc_t desc = { .sources = sources, .count = 1 };
    box_lod_mesh_t mesh;

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &desc, &mesh), true);
    test_int(mesh.index_count, 3);
    test_int(mesh.group_count, 1);
    test_int(mesh.groups[0].index_count, 3);

    box_lod_mesh_builder_fini(&builder);
}

void Merge_a_degenerate_triangle_is_dropped(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    vec3 verts[3] = { {0, 0, 0}, {1, 0, 0}, {0, 1, 0} };
    uint32_t indices[6] = { 0, 1, 2, 0, 1, 1 };
    box_lod_mesh_source_t sources[1] = {{
        .transform = mat4_identity(), .vertices = verts, .indices = indices,
        .vertex_count = 3, .index_count = 6, .material = 1
    }};
    box_lod_merge_desc_t desc = { .sources = sources, .count = 1 };
    box_lod_mesh_t mesh;

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &desc, &mesh), true);
    test_int(mesh.index_count, 3);
    test_int(mesh.groups[0].index_count, 3);

    box_lod_mesh_builder_fini(&builder);
}

void Merge_a_source_without_vertices_is_skipped(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    vec3 verts[3] = { {0, 0, 0}, {1, 0, 0}, {0, 1, 0} };
    box_lod_mesh_source_t sources[2] = {
        { .transform = mat4_identity(), .vertices = NULL,
          .vertex_count = 0, .material = 2 },
        { .transform = mat4_identity(), .vertices = verts,
          .vertex_count = 3, .material = 8 }
    };
    box_lod_merge_desc_t desc = { .sources = sources, .count = 2 };
    box_lod_mesh_t mesh;

    box_lod_mesh_builder_init(&builder);

    test_bool(box_lod_merge(&builder, &desc, &mesh), true);
    test_int(mesh.group_count, 1);
    test_assert(mesh.groups[0].material == 8);
    test_int(mesh.index_count, 3);

    box_lod_mesh_builder_fini(&builder);
}

void Merge_the_same_input_merges_the_same_mesh(void)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder, fresh;
    vec3 verts[4] = { {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0} };
    uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };
    box_lod_mesh_source_t sources[3] = {
        { .transform = mat4_translation(vec3_new(0, 0, 0)), .vertices = verts,
          .indices = indices, .vertex_count = 4, .index_count = 6, .material = 3 },
        { .transform = mat4_translation(vec3_new(1, 0, 0)), .vertices = verts,
          .indices = indices, .vertex_count = 4, .index_count = 6, .material = 1 },
        { .transform = mat4_translation(vec3_new(2, 0, 0)), .vertices = verts,
          .indices = indices, .vertex_count = 4, .index_count = 6, .material = 3 }
    };
    box_lod_merge_desc_t desc = { .sources = sources, .count = 3, .weld = true };
    box_lod_mesh_t first, second, third;
    int32_t i;

    box_lod_mesh_builder_init(&builder);
    box_lod_mesh_builder_init(&fresh);

    test_bool(box_lod_merge(&builder, &desc, &first), true);
    test_bool(box_lod_merge(&builder, &desc, &second), true);
    test_bool(box_lod_merge(&fresh, &desc, &third), true);
    test_int(first.vertex_count, second.vertex_count);
    test_int(first.vertex_count, third.vertex_count);
    test_int(first.index_count, second.index_count);
    test_int(first.index_count, third.index_count);
    test_int(first.group_count, third.group_count);

    for (i = 0; i < first.index_count; i ++) {
        test_int(second.indices[i], third.indices[i]);
    }

    for (i = 0; i < first.vertex_count; i ++) {
        test_flt(second.vertices[i].x, third.vertices[i].x);
        test_flt(second.vertices[i].y, third.vertices[i].y);
        test_flt(second.normals[i].y, third.normals[i].y);
    }

    box_lod_mesh_builder_fini(&builder);
    box_lod_mesh_builder_fini(&fresh);
}
