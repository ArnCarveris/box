/**
 * @file Gpu.c
 * @brief Tests for GPU swept spline meshes and their sample buffers.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/render/render_internal.h"

static ecs_entity_t gpu_curve(
    ecs_world_t *w)
{
    test_int(ecs_script_run(w, "gpu",
        "using box.*\n"
        "curve { Spline: {points: [{0, 0, 0}, {0, 3, 0}, {2, 5, 1}, {6, 4, 2}]} }\n"
        "cable { SplineMesh: {spline: curve, gpu: true, segments: 24, closed: true, smooth: true,"
        "profile: [{0.2, 0}, {0.1, -0.17}, {-0.1, -0.17}, {-0.2, 0}, {-0.1, 0.17}, "
        "{0.1, 0.17}]} }\n", NULL), 0);

    return ecs_lookup(w, "cable");
}

void Gpu_sweep_matches_cpu_geometry(void)
{
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    ecs_entity_t cable = gpu_curve(w);
    const Mesh3 *m = ecs_get(w, cable, Mesh3);
    test_int(ecs_vec_count(&m->sweeps), ecs_vec_count(&m->vertices));
    for (int32_t i = 0; i < ecs_vec_count(&m->sweeps); i ++) {
        const SweepVertex *s = ecs_vec_get_t(&m->sweeps, SweepVertex, i);
        vec3 tangent = vec3_normalize(s->tangent);
        vec3 right = vec3_cross(vec3_new(0, 1, 0), tangent);
        if (vec3_length(right) < 1e-6f) {
            right = vec3_cross(vec3_new(0, 0, 1), tangent);
        }

        right = vec3_normalize(right);
        vec3 up = vec3_normalize(vec3_cross(tangent, right));
        vec3 p = vec3_add(s->center, vec3_add(vec3_scale(right, s->profile.x),
            vec3_scale(up, s->profile.y)));
        vec3 n = vec3_normalize(vec3_add(vec3_scale(right, s->profile.z),
            vec3_scale(up, s->profile.w)));
        test_assert(vec3_length(vec3_sub(p, ecs_vec_get_t(&m->vertices, vec3, i)[0])) < 1e-5f);
        test_assert(vec3_length(vec3_sub(n, ecs_vec_get_t(&m->normals, vec3, i)[0])) < 1e-5f);
    }

    RenderState *state = ecs_singleton_get_mut(w, RenderState);
    const RenderMesh *render = ecs_get(w, cable, RenderMesh);
    bool found = false;
    for (int32_t i = 0; i < ecs_vec_count(&state->pending_meshes); i ++) {
        const box_mesh_cmd_t *cmd = ecs_vec_get_t(&state->pending_meshes, box_mesh_cmd_t, i);
        if (cmd->handle == render->handle) {
            const SweepVertex *s = ecs_vec_get_t(&m->sweeps, SweepVertex, 12);
            const float *v = cmd->vertices + 12 * BOX_VERTEX_FLOATS +
                BOX_VERTEX_SWEEP_OFFSET / sizeof(float);
            test_flt(v[0], s->center.x);
            test_flt(v[4], s->tangent.y);
            test_flt(v[6], s->profile.x);
            found = true;
        }
    }

    test_assert(found);
    ecs_fini(w);
}

void Gpu_curve_edits_refresh_samples(void)
{
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    ecs_entity_t cable = gpu_curve(w);
    ecs_entity_t curve = ecs_lookup(w, "curve");
    Spline *s = ecs_get_mut(w, curve, Spline);
    ecs_vec_get_t(&s->points, vec3, 3)->x = 12;
    ecs_modified(w, curve, Spline);
    const Mesh3 *m = ecs_get(w, cable, Mesh3);
    const SweepVertex *last = ecs_vec_get_t(&m->sweeps, SweepVertex, ecs_vec_count(&m->sweeps) - 1);
    test_flt(last->center.x, 12);
    ecs_remove(w, curve, Spline);
    test_assert(!ecs_has(w, cable, Mesh3));
    ecs_fini(w);
}

void Gpu_disabling_clears_samples(void)
{
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    ecs_entity_t cable = gpu_curve(w);
    uint32_t gpu_handle = ecs_get(w, cable, RenderMesh)->handle;
    ecs_get_mut(w, cable, SplineMesh)->gpu = false;
    ecs_modified(w, cable, SplineMesh);
    const Mesh3 *m = ecs_get(w, cable, Mesh3);
    test_int(ecs_vec_count(&m->sweeps), 0);
    test_assert(ecs_get(w, cable, RenderMesh)->handle != gpu_handle);
    ecs_fini(w);
}

void Gpu_clones_own_samples(void)
{
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    ecs_entity_t cable = gpu_curve(w);
    ecs_entity_t copy = ecs_new(w);
    ecs_set_ptr(w, copy, Mesh3, ecs_get(w, cable, Mesh3));
    const Mesh3 *source = ecs_get(w, cable, Mesh3);
    const Mesh3 *target = ecs_get(w, copy, Mesh3);
    test_assert(ecs_vec_first(&source->sweeps) != ecs_vec_first(&target->sweeps));
    test_int(ecs_vec_count(&source->sweeps), ecs_vec_count(&target->sweeps));
    ecs_delete(w, cable);
    test_assert(ecs_vec_count(&ecs_get(w, copy, Mesh3)->sweeps) > 0);
    ecs_fini(w);
}

void Gpu_component_copy_keeps_gpu_mode(void)
{
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    ecs_entity_t cable = gpu_curve(w);
    ecs_entity_t copy = ecs_new(w);
    ecs_set_ptr(w, copy, SplineMesh, ecs_get(w, cable, SplineMesh));
    test_assert(ecs_get(w, copy, SplineMesh)->gpu);
    const Mesh3 *m = ecs_get(w, copy, Mesh3);
    test_assert(m != NULL);
    test_int(ecs_vec_count(&m->sweeps), ecs_vec_count(&m->vertices));
    ecs_fini(w);
}
