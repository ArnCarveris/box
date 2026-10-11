#include <box.h>
#include <bake_test.h>
#include <math.h>

void Update_static_childof_update_middle(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t root = ecs_new(world);
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_entity_t a1 = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_entity_t a2 = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_entity_t a1x = ecs_new_w_pair(world, EcsChildOf, a1);
    ecs_entity_t b1 = ecs_new_w_pair(world, EcsChildOf, b);

    ecs_set(world, root, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, root, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, a, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, b, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, a1, Position, {2.0f, 0.0f, 0.0f});
    ecs_set(world, a1, Scale, {2.0f, 2.0f, 2.0f});
    ecs_set(world, a2, Position, {0.0f, 0.0f, 1.0f});
    ecs_set(world, a1x, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, b1, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            10.0f, 3.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1x, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            10.0f, 5.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, a, Position, {1.0f, 0.0f, 5.0f});
    ecs_set(world, a, Rotation, {1.5707963f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 1.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 3.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f,
            11.0f, 1.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1x, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 5.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, a, Position, {3.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 3.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 5.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f,
            11.0f, 3.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1x, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 7.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 1.0f, 0.0f, 1.0f
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

void Update_static_childof_update_leaf(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t root = ecs_new(world);
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_entity_t leaf = ecs_new_w_pair(world, EcsChildOf, c);
    ecs_entity_t leaf2 = ecs_new_w_pair(world, EcsChildOf, c);

    ecs_set(world, root, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, root, Scale, {2.0f, 2.0f, 2.0f});
    ecs_set(world, c, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, c, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, leaf, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, leaf2, Position, {0.0f, 0.0f, 1.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, c, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, leaf, Position, {3.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, c, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 8.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, leaf, Rotation, {0.0f, 1.5707963f, 0.0f});
    ecs_set(world, leaf, Scale, {1.0f, 2.0f, 1.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, c, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 0.0f, -2.0f, 0.0f,
            -4.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            3.0f, 8.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 5.0f, 1.0f
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

void Update_static_childof_per_entity_rs(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t root = ecs_new(world);
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_entity_t grandchild = ecs_new_w_pair(world, EcsChildOf, child);
    ecs_entity_t leaf = ecs_new_w_pair(world, EcsChildOf, grandchild);

    ecs_set(world, root, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, root, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, child, Position, {2.0f, 0.0f, 0.0f});
    ecs_set(world, child, Scale, {2.0f, 1.0f, 0.5f});
    ecs_set(world, grandchild, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, grandchild, Rotation, {1.5707963f, 0.0f, 0.0f});
    ecs_set(world, grandchild, Scale, {3.0f, 3.0f, 3.0f});
    ecs_set(world, leaf, Position, {1.0f, 1.0f, 1.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.5f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, grandchild, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 6.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.5f, 0.0f,
            3.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 6.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.5f, 0.0f,
            3.0f, 0.0f, 0.0f, 0.0f,
            3.0f, 8.0f, 1.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, child, Scale, {1.0f, 3.0f, 1.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -3.0f, 0.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, grandchild, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 3.0f, 0.0f,
            9.0f, 0.0f, 0.0f, 0.0f,
            -2.0f, 2.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 3.0f, 0.0f,
            9.0f, 0.0f, 0.0f, 0.0f,
            7.0f, 5.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, root, Rotation, {1.5707963f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, -1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 3.0f, 0.0f,
            0.0f, -1.0f, 0.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, grandchild, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            3.0f, 0.0f, 0.0f, 0.0f,
            0.0f, -3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, -9.0f, 0.0f,
            3.0f, 0.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            3.0f, 0.0f, 0.0f, 0.0f,
            0.0f, -3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, -9.0f, 0.0f,
            6.0f, -3.0f, -6.0f, 1.0f
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

void Update_static_parent_update_middle(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t root = ecs_new(world);
    ecs_entity_t a = ecs_new_w_parent(world, root, NULL);
    ecs_entity_t b = ecs_new_w_parent(world, root, NULL);
    ecs_entity_t a1 = ecs_new_w_parent(world, a, NULL);
    ecs_entity_t a2 = ecs_new_w_parent(world, a, NULL);
    ecs_entity_t a1x = ecs_new_w_parent(world, a1, NULL);
    ecs_entity_t b1 = ecs_new_w_parent(world, b, NULL);

    ecs_set(world, root, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, root, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, a, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, b, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, a1, Position, {2.0f, 0.0f, 0.0f});
    ecs_set(world, a1, Scale, {2.0f, 2.0f, 2.0f});
    ecs_set(world, a2, Position, {0.0f, 0.0f, 1.0f});
    ecs_set(world, a1x, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, b1, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            10.0f, 3.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1x, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            10.0f, 5.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, a, Position, {1.0f, 0.0f, 5.0f});
    ecs_set(world, a, Rotation, {1.5707963f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 1.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 3.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f,
            11.0f, 1.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1x, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 5.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, a, Position, {3.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 3.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 5.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f,
            11.0f, 3.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1x, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 7.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 1.0f, 0.0f, 1.0f
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

void Update_static_parent_update_leaf(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t root = ecs_new(world);
    ecs_entity_t c = ecs_new_w_parent(world, root, NULL);
    ecs_entity_t leaf = ecs_new_w_parent(world, c, NULL);
    ecs_entity_t leaf2 = ecs_new_w_parent(world, c, NULL);

    ecs_set(world, root, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, root, Scale, {2.0f, 2.0f, 2.0f});
    ecs_set(world, c, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, c, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, leaf, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, leaf2, Position, {0.0f, 0.0f, 1.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, c, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, leaf, Position, {3.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, c, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 8.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, leaf, Rotation, {0.0f, 1.5707963f, 0.0f});
    ecs_set(world, leaf, Scale, {1.0f, 2.0f, 1.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, c, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 0.0f, -2.0f, 0.0f,
            -4.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            3.0f, 8.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 5.0f, 1.0f
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

void Update_static_parent_per_entity_rs(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t root = ecs_new(world);
    ecs_entity_t child = ecs_new_w_parent(world, root, NULL);
    ecs_entity_t grandchild = ecs_new_w_parent(world, child, NULL);
    ecs_entity_t leaf = ecs_new_w_parent(world, grandchild, NULL);

    ecs_set(world, root, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, root, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, child, Position, {2.0f, 0.0f, 0.0f});
    ecs_set(world, child, Scale, {2.0f, 1.0f, 0.5f});
    ecs_set(world, grandchild, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, grandchild, Rotation, {1.5707963f, 0.0f, 0.0f});
    ecs_set(world, grandchild, Scale, {3.0f, 3.0f, 3.0f});
    ecs_set(world, leaf, Position, {1.0f, 1.0f, 1.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.5f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, grandchild, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 6.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.5f, 0.0f,
            3.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 6.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.5f, 0.0f,
            3.0f, 0.0f, 0.0f, 0.0f,
            3.0f, 8.0f, 1.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, child, Scale, {1.0f, 3.0f, 1.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -3.0f, 0.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, grandchild, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 3.0f, 0.0f,
            9.0f, 0.0f, 0.0f, 0.0f,
            -2.0f, 2.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 3.0f, 0.0f,
            9.0f, 0.0f, 0.0f, 0.0f,
            7.0f, 5.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, root, Rotation, {1.5707963f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, -1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 3.0f, 0.0f,
            0.0f, -1.0f, 0.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, grandchild, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            3.0f, 0.0f, 0.0f, 0.0f,
            0.0f, -3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, -9.0f, 0.0f,
            3.0f, 0.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            3.0f, 0.0f, 0.0f, 0.0f,
            0.0f, -3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, -9.0f, 0.0f,
            6.0f, -3.0f, -6.0f, 1.0f
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

void Update_dynamic_childof_update_middle(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t root = ecs_new(world);
    ecs_add(world, root, DynamicTransform);
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_add(world, a, DynamicTransform);
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_add(world, b, DynamicTransform);
    ecs_entity_t a1 = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_add(world, a1, DynamicTransform);
    ecs_entity_t a2 = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_add(world, a2, DynamicTransform);
    ecs_entity_t a1x = ecs_new_w_pair(world, EcsChildOf, a1);
    ecs_add(world, a1x, DynamicTransform);
    ecs_entity_t b1 = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_add(world, b1, DynamicTransform);

    ecs_set(world, root, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, root, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, a, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, b, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, a1, Position, {2.0f, 0.0f, 0.0f});
    ecs_set(world, a1, Scale, {2.0f, 2.0f, 2.0f});
    ecs_set(world, a2, Position, {0.0f, 0.0f, 1.0f});
    ecs_set(world, a1x, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, b1, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            10.0f, 3.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1x, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            10.0f, 5.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, a, Position, {1.0f, 0.0f, 5.0f});
    ecs_set(world, a, Rotation, {1.5707963f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 1.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 3.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f,
            11.0f, 1.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1x, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 5.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, a, Position, {3.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 3.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 5.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f,
            11.0f, 3.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1x, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 7.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 1.0f, 0.0f, 1.0f
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

void Update_dynamic_childof_update_leaf(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t root = ecs_new(world);
    ecs_add(world, root, DynamicTransform);
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_add(world, c, DynamicTransform);
    ecs_entity_t leaf = ecs_new_w_pair(world, EcsChildOf, c);
    ecs_add(world, leaf, DynamicTransform);
    ecs_entity_t leaf2 = ecs_new_w_pair(world, EcsChildOf, c);
    ecs_add(world, leaf2, DynamicTransform);

    ecs_set(world, root, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, root, Scale, {2.0f, 2.0f, 2.0f});
    ecs_set(world, c, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, c, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, leaf, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, leaf2, Position, {0.0f, 0.0f, 1.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, c, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, leaf, Position, {3.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, c, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 8.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, leaf, Rotation, {0.0f, 1.5707963f, 0.0f});
    ecs_set(world, leaf, Scale, {1.0f, 2.0f, 1.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, c, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 0.0f, -2.0f, 0.0f,
            -4.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            3.0f, 8.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 5.0f, 1.0f
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

void Update_dynamic_childof_per_entity_rs(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t root = ecs_new(world);
    ecs_add(world, root, DynamicTransform);
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_add(world, child, DynamicTransform);
    ecs_entity_t grandchild = ecs_new_w_pair(world, EcsChildOf, child);
    ecs_add(world, grandchild, DynamicTransform);
    ecs_entity_t leaf = ecs_new_w_pair(world, EcsChildOf, grandchild);
    ecs_add(world, leaf, DynamicTransform);

    ecs_set(world, root, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, root, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, child, Position, {2.0f, 0.0f, 0.0f});
    ecs_set(world, child, Scale, {2.0f, 1.0f, 0.5f});
    ecs_set(world, grandchild, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, grandchild, Rotation, {1.5707963f, 0.0f, 0.0f});
    ecs_set(world, grandchild, Scale, {3.0f, 3.0f, 3.0f});
    ecs_set(world, leaf, Position, {1.0f, 1.0f, 1.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.5f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, grandchild, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 6.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.5f, 0.0f,
            3.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 6.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.5f, 0.0f,
            3.0f, 0.0f, 0.0f, 0.0f,
            3.0f, 8.0f, 1.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, child, Scale, {1.0f, 3.0f, 1.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -3.0f, 0.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, grandchild, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 3.0f, 0.0f,
            9.0f, 0.0f, 0.0f, 0.0f,
            -2.0f, 2.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 3.0f, 0.0f,
            9.0f, 0.0f, 0.0f, 0.0f,
            7.0f, 5.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, root, Rotation, {1.5707963f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, -1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 3.0f, 0.0f,
            0.0f, -1.0f, 0.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, grandchild, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            3.0f, 0.0f, 0.0f, 0.0f,
            0.0f, -3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, -9.0f, 0.0f,
            3.0f, 0.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            3.0f, 0.0f, 0.0f, 0.0f,
            0.0f, -3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, -9.0f, 0.0f,
            6.0f, -3.0f, -6.0f, 1.0f
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

void Update_dynamic_parent_update_middle(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t root = ecs_new(world);
    ecs_add(world, root, DynamicTransform);
    ecs_entity_t a = ecs_new_w_parent(world, root, NULL);
    ecs_add(world, a, DynamicTransform);
    ecs_entity_t b = ecs_new_w_parent(world, root, NULL);
    ecs_add(world, b, DynamicTransform);
    ecs_entity_t a1 = ecs_new_w_parent(world, a, NULL);
    ecs_add(world, a1, DynamicTransform);
    ecs_entity_t a2 = ecs_new_w_parent(world, a, NULL);
    ecs_add(world, a2, DynamicTransform);
    ecs_entity_t a1x = ecs_new_w_parent(world, a1, NULL);
    ecs_add(world, a1x, DynamicTransform);
    ecs_entity_t b1 = ecs_new_w_parent(world, b, NULL);
    ecs_add(world, b1, DynamicTransform);

    ecs_set(world, root, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, root, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, a, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, b, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, a1, Position, {2.0f, 0.0f, 0.0f});
    ecs_set(world, a1, Scale, {2.0f, 2.0f, 2.0f});
    ecs_set(world, a2, Position, {0.0f, 0.0f, 1.0f});
    ecs_set(world, a1x, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, b1, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            10.0f, 3.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1x, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            10.0f, 5.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, a, Position, {1.0f, 0.0f, 5.0f});
    ecs_set(world, a, Rotation, {1.5707963f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 1.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 3.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f,
            11.0f, 1.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1x, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 5.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, a, Position, {3.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 3.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 5.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f,
            11.0f, 3.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, a1x, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 0.0f,
            10.0f, 7.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, b1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 1.0f, 0.0f, 1.0f
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

void Update_dynamic_parent_update_leaf(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t root = ecs_new(world);
    ecs_add(world, root, DynamicTransform);
    ecs_entity_t c = ecs_new_w_parent(world, root, NULL);
    ecs_add(world, c, DynamicTransform);
    ecs_entity_t leaf = ecs_new_w_parent(world, c, NULL);
    ecs_add(world, leaf, DynamicTransform);
    ecs_entity_t leaf2 = ecs_new_w_parent(world, c, NULL);
    ecs_add(world, leaf2, DynamicTransform);

    ecs_set(world, root, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, root, Scale, {2.0f, 2.0f, 2.0f});
    ecs_set(world, c, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, c, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, leaf, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, leaf2, Position, {0.0f, 0.0f, 1.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, c, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, leaf, Position, {3.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, c, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 8.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, leaf, Rotation, {0.0f, 1.5707963f, 0.0f});
    ecs_set(world, leaf, Scale, {1.0f, 2.0f, 1.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, c, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 0.0f, -2.0f, 0.0f,
            -4.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            3.0f, 8.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            3.0f, 2.0f, 5.0f, 1.0f
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

void Update_dynamic_parent_per_entity_rs(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t root = ecs_new(world);
    ecs_add(world, root, DynamicTransform);
    ecs_entity_t child = ecs_new_w_parent(world, root, NULL);
    ecs_add(world, child, DynamicTransform);
    ecs_entity_t grandchild = ecs_new_w_parent(world, child, NULL);
    ecs_add(world, grandchild, DynamicTransform);
    ecs_entity_t leaf = ecs_new_w_parent(world, grandchild, NULL);
    ecs_add(world, leaf, DynamicTransform);

    ecs_set(world, root, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, root, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, child, Position, {2.0f, 0.0f, 0.0f});
    ecs_set(world, child, Scale, {2.0f, 1.0f, 0.5f});
    ecs_set(world, grandchild, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, grandchild, Rotation, {1.5707963f, 0.0f, 0.0f});
    ecs_set(world, grandchild, Scale, {3.0f, 3.0f, 3.0f});
    ecs_set(world, leaf, Position, {1.0f, 1.0f, 1.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.5f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, grandchild, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 6.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.5f, 0.0f,
            3.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 6.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.5f, 0.0f,
            3.0f, 0.0f, 0.0f, 0.0f,
            3.0f, 8.0f, 1.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, child, Scale, {1.0f, 3.0f, 1.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -3.0f, 0.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, grandchild, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 3.0f, 0.0f,
            9.0f, 0.0f, 0.0f, 0.0f,
            -2.0f, 2.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 3.0f, 0.0f,
            9.0f, 0.0f, 0.0f, 0.0f,
            7.0f, 5.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, root, Rotation, {1.5707963f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, -1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 3.0f, 0.0f,
            0.0f, -1.0f, 0.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, grandchild, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            3.0f, 0.0f, 0.0f, 0.0f,
            0.0f, -3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, -9.0f, 0.0f,
            3.0f, 0.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            3.0f, 0.0f, 0.0f, 0.0f,
            0.0f, -3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, -9.0f, 0.0f,
            6.0f, -3.0f, -6.0f, 1.0f
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

void Update_static_childof_child_before_parent(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t root = ecs_new(world);
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_entity_t sibling = ecs_new_w_pair(world, EcsChildOf, root);

    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, child, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, sibling, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, root, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, root, Rotation, {0.0f, 0.0f, 1.5707963f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, -1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, sibling, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, child, Position, {2.0f, 0.0f, 0.0f});
    ecs_set(world, root, Position, {0.0f, 10.0f, 0.0f});
    ecs_set(world, root, Scale, {2.0f, 2.0f, 2.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            0.0f, 10.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, -2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            0.0f, 14.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, sibling, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            -2.0f, 10.0f, 0.0f, 1.0f
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

void Update_static_childof_leaf_before_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t root = ecs_new(world);
    ecs_entity_t mid = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_entity_t leaf = ecs_new_w_pair(world, EcsChildOf, mid);

    ecs_set(world, leaf, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, root, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, root, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, mid, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, mid, Scale, {2.0f, 2.0f, 2.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, mid, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            10.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            8.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, leaf, Position, {0.0f, 0.0f, 3.0f});
    ecs_set(world, root, Position, {0.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
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
    {
        const WorldTransform *t = ecs_get(world, mid, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            0.0f, 1.0f, 6.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, leaf, Rotation, {1.5707963f, 0.0f, 0.0f});
    ecs_set(world, root, Rotation, {0.0f, 1.5707963f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 0.0f, -1.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, mid, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 0.0f, -2.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, -1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 0.0f, -2.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, -2.0f, 0.0f, 0.0f,
            6.0f, 0.0f, -1.0f, 1.0f
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

void Update_static_parent_child_before_parent(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t root = ecs_new(world);
    ecs_entity_t child = ecs_new_w_parent(world, root, NULL);
    ecs_entity_t sibling = ecs_new_w_parent(world, root, NULL);

    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, child, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, sibling, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, root, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, root, Rotation, {0.0f, 0.0f, 1.5707963f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, -1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, sibling, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, child, Position, {2.0f, 0.0f, 0.0f});
    ecs_set(world, root, Position, {0.0f, 10.0f, 0.0f});
    ecs_set(world, root, Scale, {2.0f, 2.0f, 2.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            0.0f, 10.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, -2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            0.0f, 14.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, sibling, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            -2.0f, 10.0f, 0.0f, 1.0f
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

void Update_static_parent_leaf_before_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t root = ecs_new(world);
    ecs_entity_t mid = ecs_new_w_parent(world, root, NULL);
    ecs_entity_t leaf = ecs_new_w_parent(world, mid, NULL);

    ecs_set(world, leaf, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, root, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, root, Rotation, {0.0f, 0.0f, 1.5707963f});
    ecs_set(world, mid, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, mid, Scale, {2.0f, 2.0f, 2.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, mid, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            10.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            8.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, leaf, Position, {0.0f, 0.0f, 3.0f});
    ecs_set(world, root, Position, {0.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
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
    {
        const WorldTransform *t = ecs_get(world, mid, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 2.0f, 0.0f, 0.0f,
            -2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            0.0f, 1.0f, 6.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, leaf, Rotation, {1.5707963f, 0.0f, 0.0f});
    ecs_set(world, root, Rotation, {0.0f, 1.5707963f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, root, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 0.0f, -1.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, mid, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 0.0f, -2.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, -1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, leaf, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 0.0f, -2.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, -2.0f, 0.0f, 0.0f,
            6.0f, 0.0f, -1.0f, 1.0f
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
