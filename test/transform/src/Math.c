/**
 * @file Math.c
 * @brief Tests for the matrix helpers the transform module composes with.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

void Math_identity_is_neutral(void)
{
    // scene_core/transform
    mat4 id = mat4_identity();
    mat4 t = mat4_translation(vec3_new(3, 4, 5));
    mat4 left = mat4_mul(&id, &t);
    mat4 right = mat4_mul(&t, &id);
    vec3 p = mat4_transform_point(&id, vec3_new(1, 2, 3));
    int i;
    for (i = 0; i < 16; i ++) {
        test_assert(fabsf(left.v[i] - t.v[i]) < 1e-4f);
        test_assert(fabsf(right.v[i] - t.v[i]) < 1e-4f);
    }

    test_assert(fabsf(p.x - 1) < 1e-4f);
    test_assert(fabsf(p.y - 2) < 1e-4f);
    test_assert(fabsf(p.z - 3) < 1e-4f);
}

void Math_translation_matrix(void)
{
    // scene_core/transform
    mat4 m = mat4_translation(vec3_new(1, -2, 3));
    vec3 p = mat4_transform_point(&m, vec3_new(10, 10, 10));
    vec3 d = mat4_transform_dir(&m, vec3_new(10, 10, 10));
    test_assert(fabsf(m.v[12] - 1) < 1e-4f);
    test_assert(fabsf(m.v[13] + 2) < 1e-4f);
    test_assert(fabsf(m.v[14] - 3) < 1e-4f);
    test_assert(fabsf(p.x - 11) < 1e-4f);
    test_assert(fabsf(p.y - 8) < 1e-4f);
    test_assert(fabsf(p.z - 13) < 1e-4f);
    test_assert(fabsf(d.x - 10) < 1e-4f);
    test_assert(fabsf(d.y - 10) < 1e-4f);
    test_assert(fabsf(d.z - 10) < 1e-4f);
}

void Math_scale_matrix(void)
{
    // scene_core/transform
    mat4 m = mat4_scaling(vec3_new(2, 3, 4));
    vec3 p = mat4_transform_point(&m, vec3_new(1, 1, 1));
    test_assert(fabsf(p.x - 2) < 1e-4f);
    test_assert(fabsf(p.y - 3) < 1e-4f);
    test_assert(fabsf(p.z - 4) < 1e-4f);
}

void Math_rotation_x_hand_computed(void)
{
    // scene_core/transform
    float a = 0.37f;
    float c = cosf(a), s = sinf(a);
    mat4 m = mat4_rotation_x(a);
    vec3 y = mat4_transform_dir(&m, vec3_new(0, 1, 0));
    vec3 z = mat4_transform_dir(&m, vec3_new(0, 0, 1));
    test_assert(fabsf(mat4_at(&m, 1, 1) - c) < 1e-4f);
    test_assert(fabsf(mat4_at(&m, 2, 1) - s) < 1e-4f);
    test_assert(fabsf(mat4_at(&m, 1, 2) + s) < 1e-4f);
    test_assert(fabsf(mat4_at(&m, 2, 2) - c) < 1e-4f);
    test_assert(fabsf(y.y - c) < 1e-4f);
    test_assert(fabsf(y.z - s) < 1e-4f);
    test_assert(fabsf(z.y + s) < 1e-4f);
    test_assert(fabsf(z.z - c) < 1e-4f);
}

void Math_rotation_xyz_hand_computed(void)
{
    // scene_core/transform
    float x = 0.3f, y = -0.7f, z = 1.1f;
    float cx = cosf(x), sx = sinf(x);
    float cy = cosf(y), sy = sinf(y);
    float cz = cosf(z), sz = sinf(z);
    mat4 m = mat4_rotation_xyz(vec3_new(x, y, z));
    test_assert(fabsf(mat4_at(&m, 0, 0) - cy * cz) < 1e-4f);
    test_assert(fabsf(mat4_at(&m, 0, 1) + cy * sz) < 1e-4f);
    test_assert(fabsf(mat4_at(&m, 0, 2) - sy) < 1e-4f);
    test_assert(fabsf(mat4_at(&m, 1, 0) - (cx * sz + sx * sy * cz)) < 1e-4f);
    test_assert(fabsf(mat4_at(&m, 1, 1) - (cx * cz - sx * sy * sz)) < 1e-4f);
    test_assert(fabsf(mat4_at(&m, 1, 2) + sx * cy) < 1e-4f);
    test_assert(fabsf(mat4_at(&m, 2, 0) - (sx * sz - cx * sy * cz)) < 1e-4f);
    test_assert(fabsf(mat4_at(&m, 2, 1) - (sx * cz + cx * sy * sz)) < 1e-4f);
    test_assert(fabsf(mat4_at(&m, 2, 2) - cx * cy) < 1e-4f);
}

void Math_rotation_yxz_hand_computed(void)
{
    // scene_core/transform
    float x = -0.45f, y = 0.9f, z = 0.2f;
    float cx = cosf(x), sx = sinf(x);
    float cy = cosf(y), sy = sinf(y);
    float cz = cosf(z), sz = sinf(z);
    mat4 m = mat4_rotation_yxz(x, y, z);
    test_assert(fabsf(mat4_at(&m, 0, 0) - (cy * cz + sy * sx * sz)) < 1e-4f);
    test_assert(fabsf(mat4_at(&m, 0, 1) - (sy * sx * cz - cy * sz)) < 1e-4f);
    test_assert(fabsf(mat4_at(&m, 0, 2) - sy * cx) < 1e-4f);
    test_assert(fabsf(mat4_at(&m, 1, 0) - cx * sz) < 1e-4f);
    test_assert(fabsf(mat4_at(&m, 1, 1) - cx * cz) < 1e-4f);
    test_assert(fabsf(mat4_at(&m, 1, 2) + sx) < 1e-4f);
    test_assert(fabsf(mat4_at(&m, 2, 0) - (cy * sx * sz - sy * cz)) < 1e-4f);
    test_assert(fabsf(mat4_at(&m, 2, 1) - (sy * sz + cy * sx * cz)) < 1e-4f);
    test_assert(fabsf(mat4_at(&m, 2, 2) - cy * cx) < 1e-4f);
}

void Math_trs_composition(void)
{
    // scene_core/transform
    vec3 t = vec3_new(5, -1, 2);
    vec3 s = vec3_new(2, 3, 4);
    mat4 r = mat4_rotation_xyz(vec3_new(0.2f, 0.4f, -0.6f));
    mat4 trs = mat4_trs(t, &r, s);
    mat4 tm = mat4_translation(t);
    mat4 sm = mat4_scaling(s);
    mat4 rs = mat4_mul(&r, &sm);
    mat4 expect = mat4_mul(&tm, &rs);
    int i;
    for (i = 0; i < 16; i ++) {
        test_assert(fabsf(trs.v[i] - expect.v[i]) < 1e-4f);
    }
}

void Math_parent_child_multiplication(void)
{
    // scene_core/transform
    mat4 rot = mat4_rotation_y(1.5707963f);
    mat4 parent = mat4_trs(vec3_new(10, 0, 0), &rot, vec3_new(1, 1, 1));
    mat4 id = mat4_identity();
    mat4 child = mat4_trs(vec3_new(0, 0, 2), &id, vec3_new(1, 1, 1));
    mat4 world = mat4_mul(&parent, &child);
    vec3 p = mat4_get_translation(&world);
    test_assert(fabsf(p.x - 12) < 1e-4f);
    test_assert(fabsf(p.y) < 1e-4f);
    test_assert(fabsf(p.z) < 1e-4f);
}

void Math_facing_z_axis_matches_direction(void)
{
    // scene_core/transform
    vec3 dir = vec3_normalize(vec3_new(1, 2, -3));
    mat4 m = mat4_facing(dir, vec3_new(0, 1, 0));
    vec3 f = mat4_transform_dir(&m, vec3_new(0, 0, 1));
    vec3 r = mat4_transform_dir(&m, vec3_new(1, 0, 0));
    test_assert(fabsf(f.x - dir.x) < 1e-4f);
    test_assert(fabsf(f.y - dir.y) < 1e-4f);
    test_assert(fabsf(f.z - dir.z) < 1e-4f);
    test_assert(fabsf(vec3_dot(r, f)) < 1e-4f);
    test_assert(fabsf(r.y) < 1e-4f);
}

void Math_facing_euler_round_trip(void)
{
    // scene_core/transform
    vec3 dir = vec3_normalize(vec3_new(-2, 1, 4));
    mat4 m = mat4_facing(dir, vec3_new(0, 1, 0));
    vec3 e = vec3_new(
        atan2f(-mat4_at(&m, 1, 2), mat4_at(&m, 2, 2)),
        asinf(mat4_at(&m, 0, 2)),
        atan2f(-mat4_at(&m, 0, 1), mat4_at(&m, 0, 0)));
    mat4 back = mat4_rotation_xyz(e);
    int i;
    for (i = 0; i < 16; i ++) {
        test_assert(fabsf(back.v[i] - m.v[i]) < 1e-4f);
    }
}
