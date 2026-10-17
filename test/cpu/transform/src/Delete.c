#include <box.h>
#include <bake_test.h>
#include <math.h>

void Delete_static_childof_set_delete_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new(world);

    ecs_set(world, e1, Position, {3.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});

    ecs_delete(world, e0);

    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e0));

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}

void Delete_static_childof_set_delete_child(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e0);

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e1, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e2, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e1, Position, {5.0f, 0.0f, 0.0f});

    ecs_delete(world, e1);

    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e1));

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {2.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}

void Delete_static_childof_set_parent_delete_child(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e0);

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e1, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e2, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {2.0f, 0.0f, 0.0f});

    ecs_delete(world, e1);

    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e1));

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}

void Delete_static_childof_set_child_delete_parent(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e1, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e2, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e2, Position, {2.0f, 0.0f, 0.0f});

    ecs_delete(world, e1);

    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e1));
    test_assert(!ecs_is_alive(world, e2));

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {3.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}

void Delete_static_childof_delete_mid_move_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e0);

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e0, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, e1, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e2, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e3, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 2.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_delete(world, e1);

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e1));
    test_assert(!ecs_is_alive(world, e2));

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {5.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            5.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            5.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}

void Delete_static_childof_set_prs_delete(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new(world);

    ecs_set(world, e1, Position, {3.0f, 0.0f, 0.0f});
    ecs_set(world, e1, Rotation, {0.0f, 0.0f, 1.5707963f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e0, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, e0, Scale, {2.0f, 3.0f, 4.0f});

    ecs_delete(world, e0);

    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e0));

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}

void Delete_static_parent_set_delete_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new(world);

    ecs_set(world, e1, Position, {3.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});

    ecs_delete(world, e0);

    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e0));

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}

void Delete_static_parent_set_delete_child(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e0, NULL);

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e1, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e2, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e1, Position, {5.0f, 0.0f, 0.0f});

    ecs_delete(world, e1);

    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e1));

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {2.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}

void Delete_static_parent_set_parent_delete_child(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e0, NULL);

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e1, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e2, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {2.0f, 0.0f, 0.0f});

    ecs_delete(world, e1);

    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e1));

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}

void Delete_static_parent_set_child_delete_parent(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e1, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e2, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e2, Position, {2.0f, 0.0f, 0.0f});

    ecs_delete(world, e1);

    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e1));
    test_assert(!ecs_is_alive(world, e2));

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {3.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}

void Delete_static_parent_delete_mid_move_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e0, NULL);

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e0, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, e1, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e2, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e3, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 2.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_delete(world, e1);

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e1));
    test_assert(!ecs_is_alive(world, e2));

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {5.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            5.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            5.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}

void Delete_static_parent_set_prs_delete(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new(world);

    ecs_set(world, e1, Position, {3.0f, 0.0f, 0.0f});
    ecs_set(world, e1, Rotation, {0.0f, 0.0f, 1.5707963f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e0, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, e0, Scale, {2.0f, 3.0f, 4.0f});

    ecs_delete(world, e0);

    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e0));

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}

void Delete_dynamic_childof_set_delete_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_add(world, e0, DynamicTransform);
    ecs_entity_t e1 = ecs_new(world);
    ecs_add(world, e1, DynamicTransform);

    ecs_set(world, e1, Position, {3.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});

    ecs_delete(world, e0);

    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e0));

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}

void Delete_dynamic_childof_set_delete_child(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_add(world, e0, DynamicTransform);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_add(world, e1, DynamicTransform);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_add(world, e2, DynamicTransform);

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e1, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e2, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e1, Position, {5.0f, 0.0f, 0.0f});

    ecs_delete(world, e1);

    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e1));

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {2.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}

void Delete_dynamic_childof_set_parent_delete_child(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_add(world, e0, DynamicTransform);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_add(world, e1, DynamicTransform);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_add(world, e2, DynamicTransform);

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e1, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e2, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {2.0f, 0.0f, 0.0f});

    ecs_delete(world, e1);

    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e1));

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}

void Delete_dynamic_childof_set_child_delete_parent(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_add(world, e0, DynamicTransform);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_add(world, e1, DynamicTransform);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_add(world, e2, DynamicTransform);

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e1, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e2, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e2, Position, {2.0f, 0.0f, 0.0f});

    ecs_delete(world, e1);

    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e1));
    test_assert(!ecs_is_alive(world, e2));

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {3.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}

void Delete_dynamic_childof_delete_mid_move_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_add(world, e0, DynamicTransform);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_add(world, e1, DynamicTransform);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_add(world, e2, DynamicTransform);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_add(world, e3, DynamicTransform);

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e0, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, e1, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e2, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e3, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 2.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_delete(world, e1);

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e1));
    test_assert(!ecs_is_alive(world, e2));

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {5.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            5.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            5.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}

void Delete_dynamic_childof_set_prs_delete(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_add(world, e0, DynamicTransform);
    ecs_entity_t e1 = ecs_new(world);
    ecs_add(world, e1, DynamicTransform);

    ecs_set(world, e1, Position, {3.0f, 0.0f, 0.0f});
    ecs_set(world, e1, Rotation, {0.0f, 0.0f, 1.5707963f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e0, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, e0, Scale, {2.0f, 3.0f, 4.0f});

    ecs_delete(world, e0);

    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e0));

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}

void Delete_dynamic_parent_set_delete_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_add(world, e0, DynamicTransform);
    ecs_entity_t e1 = ecs_new(world);
    ecs_add(world, e1, DynamicTransform);

    ecs_set(world, e1, Position, {3.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});

    ecs_delete(world, e0);

    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e0));

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}

void Delete_dynamic_parent_set_delete_child(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_add(world, e0, DynamicTransform);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_add(world, e1, DynamicTransform);
    ecs_entity_t e2 = ecs_new_w_parent(world, e0, NULL);
    ecs_add(world, e2, DynamicTransform);

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e1, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e2, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e1, Position, {5.0f, 0.0f, 0.0f});

    ecs_delete(world, e1);

    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e1));

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {2.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}

void Delete_dynamic_parent_set_parent_delete_child(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_add(world, e0, DynamicTransform);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_add(world, e1, DynamicTransform);
    ecs_entity_t e2 = ecs_new_w_parent(world, e0, NULL);
    ecs_add(world, e2, DynamicTransform);

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e1, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e2, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {2.0f, 0.0f, 0.0f});

    ecs_delete(world, e1);

    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e1));

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}

void Delete_dynamic_parent_set_child_delete_parent(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_add(world, e0, DynamicTransform);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_add(world, e1, DynamicTransform);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_add(world, e2, DynamicTransform);

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e1, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e2, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e2, Position, {2.0f, 0.0f, 0.0f});

    ecs_delete(world, e1);

    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e1));
    test_assert(!ecs_is_alive(world, e2));

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {3.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}

void Delete_dynamic_parent_delete_mid_move_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_add(world, e0, DynamicTransform);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_add(world, e1, DynamicTransform);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_add(world, e2, DynamicTransform);
    ecs_entity_t e3 = ecs_new_w_parent(world, e0, NULL);
    ecs_add(world, e3, DynamicTransform);

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e0, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, e1, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e2, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e3, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 2.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_delete(world, e1);

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e1));
    test_assert(!ecs_is_alive(world, e2));

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {5.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            5.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            5.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}

void Delete_dynamic_parent_set_prs_delete(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_add(world, e0, DynamicTransform);
    ecs_entity_t e1 = ecs_new(world);
    ecs_add(world, e1, DynamicTransform);

    ecs_set(world, e1, Position, {3.0f, 0.0f, 0.0f});
    ecs_set(world, e1, Rotation, {0.0f, 0.0f, 1.5707963f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, e0, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, e0, Scale, {2.0f, 3.0f, 4.0f});

    ecs_delete(world, e0);

    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, e0));

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_fini(world);
}
