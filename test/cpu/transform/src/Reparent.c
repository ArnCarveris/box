#include <box.h>
#include <bake_test.h>
#include <math.h>

void Reparent_static_childof_change_parent(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, p1);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, e, EcsChildOf, p2);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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

void Reparent_static_childof_change_parent_w_scale(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 0.0f, 5.0f});
    ecs_set(world, p2, Scale, {2.0f, 3.0f, 4.0f});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, p1);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, e, Rotation, {1.5707964f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 4.0f, 0.0f,
            0.0f, 0.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, -1.0f, 0.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, e, EcsChildOf, p2);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 4.0f, 0.0f,
            0.0f, 0.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 4.0f, 0.0f,
            0.0f, -3.0f, 0.0f, 0.0f,
            2.0f, 6.0f, 17.0f, 1.0f
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

void Reparent_static_childof_change_parent_w_children(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, p1);
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_entity_t g = ecs_new_w_pair(world, EcsChildOf, c);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, c, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, g, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            12.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, g, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            12.0f, 3.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, e, EcsChildOf, p2);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 22.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, g, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -3.0f, 22.0f, 3.0f, 1.0f
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

void Reparent_static_childof_to_parent(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, p1);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_remove_pair(world, e, EcsChildOf, p1);
    ecs_set(world, e, EcsParent, {p2});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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

void Reparent_static_childof_to_parent_w_children(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, p1);
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, c, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            12.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_remove_pair(world, e, EcsChildOf, p1);
    ecs_set(world, e, EcsParent, {p2});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 22.0f, 3.0f, 1.0f
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

void Reparent_static_childof_to_parent_same_parent(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, p2);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_remove_pair(world, e, EcsChildOf, p2);
    ecs_set(world, e, EcsParent, {p2});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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

void Reparent_static_childof_to_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, p2);
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, c, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 22.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_remove_pair(world, e, EcsChildOf, p2);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 2.0f, 3.0f, 1.0f
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

void Reparent_static_childof_from_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new(world);
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, c, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, e, EcsChildOf, p2);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 22.0f, 3.0f, 1.0f
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

void Reparent_static_childof_change_parent_deeper(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t a = ecs_new(world);
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_entity_t d = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_entity_t r = ecs_new(world);
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, r);
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_set(world, a, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, b, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, b, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_set(world, d, Position, {0.0f, 0.0f, 1.0f});
    ecs_set(world, r, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, e, Position, {1.0f, 1.0f, 1.0f});
    ecs_set(world, c, Position, {2.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, r, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 1.0f, 1.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            13.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, e, EcsChildOf, d);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, r, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 2.0f, 2.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 4.0f, 2.0f, 1.0f
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

void Reparent_static_childof_change_parent_shallower(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t a = ecs_new(world);
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_entity_t d = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_entity_t r = ecs_new(world);
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, d);
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_set(world, a, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, b, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, b, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_set(world, d, Position, {0.0f, 0.0f, 1.0f});
    ecs_set(world, r, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, e, Position, {1.0f, 1.0f, 1.0f});
    ecs_set(world, c, Position, {2.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, r, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 2.0f, 2.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 4.0f, 2.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, e, EcsChildOf, r);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, r, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 1.0f, 1.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            13.0f, 1.0f, 1.0f, 1.0f
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

void Reparent_static_childof_change_parent_deeper_and_back(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t a = ecs_new(world);
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_entity_t d = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_entity_t g = ecs_new_w_pair(world, EcsChildOf, c);
    ecs_set(world, a, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, b, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, d, Position, {0.0f, 0.0f, 1.0f});
    ecs_set(world, d, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_set(world, e, Position, {1.0f, 1.0f, 1.0f});
    ecs_set(world, c, Position, {2.0f, 0.0f, 0.0f});
    ecs_set(world, g, Position, {0.0f, 3.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
    {
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 1.0f, 1.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, g, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 4.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, e, EcsChildOf, d);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
    {
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 2.0f, 2.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 4.0f, 2.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, g, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -3.0f, 4.0f, 2.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, e, EcsChildOf, a);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
    {
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 1.0f, 1.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, g, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 4.0f, 1.0f, 1.0f
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

void Reparent_static_childof_to_non_transformable(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t a = ecs_new(world);
    ecs_entity_t n = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, p1);
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_set(world, a, Position, {0.0f, 10.0f, 0.0f});
    ecs_set(world, a, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_set(world, p1, Position, {5.0f, 0.0f, 0.0f});
    ecs_set(world, e, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, c, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 10.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, 0.0f, 0.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, e, EcsChildOf, n);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 10.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 11.0f, 0.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -1.0f, 11.0f, 0.0f, 1.0f
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

void Reparent_static_childof_from_non_transformable(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t a = ecs_new(world);
    ecs_entity_t n = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, n);
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_set(world, a, Position, {0.0f, 10.0f, 0.0f});
    ecs_set(world, a, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_set(world, p1, Position, {5.0f, 0.0f, 0.0f});
    ecs_set(world, e, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, c, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 10.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 11.0f, 0.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -1.0f, 11.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, e, EcsChildOf, p1);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 10.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, 0.0f, 0.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, 1.0f, 0.0f, 1.0f
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

void Reparent_static_childof_change_parent_of_non_transformable(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t n = ecs_new_w_pair(world, EcsChildOf, p1);
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, n);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, n, EcsChildOf, p2);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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

void Reparent_static_childof_non_transformable_to_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t n = ecs_new_w_pair(world, EcsChildOf, p2);
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, n);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_remove_pair(world, n, EcsChildOf, p2);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 2.0f, 3.0f, 1.0f
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

void Reparent_static_childof_move_old_parent(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, p1);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, e, EcsChildOf, p2);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, p1, Position, {100.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            100.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, p2, Position, {0.0f, 30.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            100.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 30.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 31.0f, 3.0f, 1.0f
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

void Reparent_static_childof_change_parent_and_set_position(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, p1);
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, c, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            12.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, e, EcsChildOf, p2);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 22.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, p1, Position, {100.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            100.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 22.0f, 3.0f, 1.0f
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

void Reparent_static_parent_change_parent(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_parent(world, p1, NULL);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, EcsParent, {p2});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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

void Reparent_static_parent_change_parent_w_scale(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 0.0f, 5.0f});
    ecs_set(world, p2, Scale, {2.0f, 3.0f, 4.0f});
    ecs_entity_t e = ecs_new_w_parent(world, p1, NULL);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, e, Rotation, {1.5707964f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 4.0f, 0.0f,
            0.0f, 0.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, -1.0f, 0.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, EcsParent, {p2});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 4.0f, 0.0f,
            0.0f, 0.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 4.0f, 0.0f,
            0.0f, -3.0f, 0.0f, 0.0f,
            2.0f, 6.0f, 17.0f, 1.0f
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

void Reparent_static_parent_change_parent_w_children(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_parent(world, p1, NULL);
    ecs_entity_t c = ecs_new_w_parent(world, e, NULL);
    ecs_entity_t g = ecs_new_w_parent(world, c, NULL);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, c, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, g, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            12.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, g, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            12.0f, 3.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, EcsParent, {p2});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 22.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, g, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -3.0f, 22.0f, 3.0f, 1.0f
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

void Reparent_static_parent_to_childof(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_parent(world, p1, NULL);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_remove(world, e, EcsParent);
    ecs_add_pair(world, e, EcsChildOf, p2);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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

void Reparent_static_parent_to_childof_w_children(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_parent(world, p1, NULL);
    ecs_entity_t c = ecs_new_w_parent(world, e, NULL);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, c, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            12.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_remove(world, e, EcsParent);
    ecs_add_pair(world, e, EcsChildOf, p2);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 22.0f, 3.0f, 1.0f
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

void Reparent_static_parent_to_childof_same_parent(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_parent(world, p2, NULL);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_remove(world, e, EcsParent);
    ecs_add_pair(world, e, EcsChildOf, p2);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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

void Reparent_static_parent_to_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_parent(world, p2, NULL);
    ecs_entity_t c = ecs_new_w_parent(world, e, NULL);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, c, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 22.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_remove(world, e, EcsParent);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 2.0f, 3.0f, 1.0f
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

void Reparent_static_parent_from_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new(world);
    ecs_entity_t c = ecs_new_w_parent(world, e, NULL);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, c, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, EcsParent, {p2});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 22.0f, 3.0f, 1.0f
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

void Reparent_static_parent_change_parent_deeper(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t a = ecs_new(world);
    ecs_entity_t b = ecs_new_w_parent(world, a, NULL);
    ecs_entity_t d = ecs_new_w_parent(world, b, NULL);
    ecs_entity_t r = ecs_new(world);
    ecs_entity_t e = ecs_new_w_parent(world, r, NULL);
    ecs_entity_t c = ecs_new_w_parent(world, e, NULL);
    ecs_set(world, a, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, b, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, b, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_set(world, d, Position, {0.0f, 0.0f, 1.0f});
    ecs_set(world, r, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, e, Position, {1.0f, 1.0f, 1.0f});
    ecs_set(world, c, Position, {2.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, r, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 1.0f, 1.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            13.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, EcsParent, {d});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, r, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 2.0f, 2.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 4.0f, 2.0f, 1.0f
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

void Reparent_static_parent_change_parent_shallower(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t a = ecs_new(world);
    ecs_entity_t b = ecs_new_w_parent(world, a, NULL);
    ecs_entity_t d = ecs_new_w_parent(world, b, NULL);
    ecs_entity_t r = ecs_new(world);
    ecs_entity_t e = ecs_new_w_parent(world, d, NULL);
    ecs_entity_t c = ecs_new_w_parent(world, e, NULL);
    ecs_set(world, a, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, b, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, b, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_set(world, d, Position, {0.0f, 0.0f, 1.0f});
    ecs_set(world, r, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, e, Position, {1.0f, 1.0f, 1.0f});
    ecs_set(world, c, Position, {2.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, r, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 2.0f, 2.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 4.0f, 2.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, EcsParent, {r});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, r, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 1.0f, 1.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            13.0f, 1.0f, 1.0f, 1.0f
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

void Reparent_static_parent_change_parent_deeper_and_back(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t a = ecs_new(world);
    ecs_entity_t b = ecs_new_w_parent(world, a, NULL);
    ecs_entity_t d = ecs_new_w_parent(world, b, NULL);
    ecs_entity_t e = ecs_new_w_parent(world, a, NULL);
    ecs_entity_t c = ecs_new_w_parent(world, e, NULL);
    ecs_entity_t g = ecs_new_w_parent(world, c, NULL);
    ecs_set(world, a, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, b, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, d, Position, {0.0f, 0.0f, 1.0f});
    ecs_set(world, d, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_set(world, e, Position, {1.0f, 1.0f, 1.0f});
    ecs_set(world, c, Position, {2.0f, 0.0f, 0.0f});
    ecs_set(world, g, Position, {0.0f, 3.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
    {
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 1.0f, 1.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, g, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 4.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, EcsParent, {d});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
    {
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 2.0f, 2.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 4.0f, 2.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, g, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -3.0f, 4.0f, 2.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, EcsParent, {a});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
    {
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 1.0f, 1.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, g, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 4.0f, 1.0f, 1.0f
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

void Reparent_static_parent_to_non_transformable(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t a = ecs_new(world);
    ecs_entity_t n = ecs_new_w_parent(world, a, NULL);
    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t e = ecs_new_w_parent(world, p1, NULL);
    ecs_entity_t c = ecs_new_w_parent(world, e, NULL);
    ecs_set(world, a, Position, {0.0f, 10.0f, 0.0f});
    ecs_set(world, a, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_set(world, p1, Position, {5.0f, 0.0f, 0.0f});
    ecs_set(world, e, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, c, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 10.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, 0.0f, 0.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, EcsParent, {n});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 10.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 11.0f, 0.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -1.0f, 11.0f, 0.0f, 1.0f
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

void Reparent_static_parent_from_non_transformable(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t a = ecs_new(world);
    ecs_entity_t n = ecs_new_w_parent(world, a, NULL);
    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t e = ecs_new_w_parent(world, n, NULL);
    ecs_entity_t c = ecs_new_w_parent(world, e, NULL);
    ecs_set(world, a, Position, {0.0f, 10.0f, 0.0f});
    ecs_set(world, a, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_set(world, p1, Position, {5.0f, 0.0f, 0.0f});
    ecs_set(world, e, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, c, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 10.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 11.0f, 0.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -1.0f, 11.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, EcsParent, {p1});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 10.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, 0.0f, 0.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, 1.0f, 0.0f, 1.0f
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

void Reparent_static_parent_change_parent_of_non_transformable(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t n = ecs_new_w_parent(world, p1, NULL);
    ecs_entity_t e = ecs_new_w_parent(world, n, NULL);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, n, EcsParent, {p2});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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

void Reparent_static_parent_non_transformable_to_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t n = ecs_new_w_parent(world, p2, NULL);
    ecs_entity_t e = ecs_new_w_parent(world, n, NULL);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_remove(world, n, EcsParent);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 2.0f, 3.0f, 1.0f
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

void Reparent_static_parent_move_old_parent(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_parent(world, p1, NULL);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, EcsParent, {p2});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, p1, Position, {100.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            100.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, p2, Position, {0.0f, 30.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            100.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 30.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 31.0f, 3.0f, 1.0f
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

void Reparent_static_parent_change_parent_and_set_position(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_entity_t p2 = ecs_new(world);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_parent(world, p1, NULL);
    ecs_entity_t c = ecs_new_w_parent(world, e, NULL);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, c, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            12.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, EcsParent, {p2});
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 22.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, p1, Position, {100.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            100.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 22.0f, 3.0f, 1.0f
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

void Reparent_dynamic_childof_change_parent(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t p2 = ecs_new(world);
    ecs_add(world, p2, DynamicTransform);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, p1);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, e, EcsChildOf, p2);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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

void Reparent_dynamic_childof_change_parent_w_scale(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t p2 = ecs_new(world);
    ecs_add(world, p2, DynamicTransform);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 0.0f, 5.0f});
    ecs_set(world, p2, Scale, {2.0f, 3.0f, 4.0f});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, p1);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, e, Rotation, {1.5707964f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 4.0f, 0.0f,
            0.0f, 0.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, -1.0f, 0.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, e, EcsChildOf, p2);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 4.0f, 0.0f,
            0.0f, 0.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 4.0f, 0.0f,
            0.0f, -3.0f, 0.0f, 0.0f,
            2.0f, 6.0f, 17.0f, 1.0f
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

void Reparent_dynamic_childof_change_parent_w_children(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t p2 = ecs_new(world);
    ecs_add(world, p2, DynamicTransform);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, p1);
    ecs_add(world, e, DynamicTransform);
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_add(world, c, DynamicTransform);
    ecs_entity_t g = ecs_new_w_pair(world, EcsChildOf, c);
    ecs_add(world, g, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, c, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, g, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            12.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, g, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            12.0f, 3.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, e, EcsChildOf, p2);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 22.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, g, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -3.0f, 22.0f, 3.0f, 1.0f
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

void Reparent_dynamic_childof_to_parent(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t p2 = ecs_new(world);
    ecs_add(world, p2, DynamicTransform);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, p1);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_remove_pair(world, e, EcsChildOf, p1);
    ecs_set(world, e, EcsParent, {p2});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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

void Reparent_dynamic_childof_to_parent_w_children(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t p2 = ecs_new(world);
    ecs_add(world, p2, DynamicTransform);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, p1);
    ecs_add(world, e, DynamicTransform);
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_add(world, c, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, c, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            12.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_remove_pair(world, e, EcsChildOf, p1);
    ecs_set(world, e, EcsParent, {p2});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 22.0f, 3.0f, 1.0f
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

void Reparent_dynamic_childof_to_parent_same_parent(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t p2 = ecs_new(world);
    ecs_add(world, p2, DynamicTransform);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, p2);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_remove_pair(world, e, EcsChildOf, p2);
    ecs_set(world, e, EcsParent, {p2});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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

void Reparent_dynamic_childof_to_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t p2 = ecs_new(world);
    ecs_add(world, p2, DynamicTransform);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, p2);
    ecs_add(world, e, DynamicTransform);
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_add(world, c, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, c, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 22.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_remove_pair(world, e, EcsChildOf, p2);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 2.0f, 3.0f, 1.0f
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

void Reparent_dynamic_childof_from_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t p2 = ecs_new(world);
    ecs_add(world, p2, DynamicTransform);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, DynamicTransform);
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_add(world, c, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, c, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, e, EcsChildOf, p2);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 22.0f, 3.0f, 1.0f
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

void Reparent_dynamic_childof_change_parent_deeper(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t a = ecs_new(world);
    ecs_add(world, a, DynamicTransform);
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_add(world, b, DynamicTransform);
    ecs_entity_t d = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_add(world, d, DynamicTransform);
    ecs_entity_t r = ecs_new(world);
    ecs_add(world, r, DynamicTransform);
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, r);
    ecs_add(world, e, DynamicTransform);
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_add(world, c, DynamicTransform);
    ecs_set(world, a, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, b, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, b, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_set(world, d, Position, {0.0f, 0.0f, 1.0f});
    ecs_set(world, r, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, e, Position, {1.0f, 1.0f, 1.0f});
    ecs_set(world, c, Position, {2.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, r, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 1.0f, 1.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            13.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, e, EcsChildOf, d);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, r, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 2.0f, 2.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 4.0f, 2.0f, 1.0f
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

void Reparent_dynamic_childof_change_parent_shallower(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t a = ecs_new(world);
    ecs_add(world, a, DynamicTransform);
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_add(world, b, DynamicTransform);
    ecs_entity_t d = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_add(world, d, DynamicTransform);
    ecs_entity_t r = ecs_new(world);
    ecs_add(world, r, DynamicTransform);
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, d);
    ecs_add(world, e, DynamicTransform);
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_add(world, c, DynamicTransform);
    ecs_set(world, a, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, b, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, b, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_set(world, d, Position, {0.0f, 0.0f, 1.0f});
    ecs_set(world, r, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, e, Position, {1.0f, 1.0f, 1.0f});
    ecs_set(world, c, Position, {2.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, r, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 2.0f, 2.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 4.0f, 2.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, e, EcsChildOf, r);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, r, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 1.0f, 1.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            13.0f, 1.0f, 1.0f, 1.0f
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

void Reparent_dynamic_childof_change_parent_deeper_and_back(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t a = ecs_new(world);
    ecs_add(world, a, DynamicTransform);
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_add(world, b, DynamicTransform);
    ecs_entity_t d = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_add(world, d, DynamicTransform);
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_add(world, e, DynamicTransform);
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_add(world, c, DynamicTransform);
    ecs_entity_t g = ecs_new_w_pair(world, EcsChildOf, c);
    ecs_add(world, g, DynamicTransform);
    ecs_set(world, a, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, b, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, d, Position, {0.0f, 0.0f, 1.0f});
    ecs_set(world, d, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_set(world, e, Position, {1.0f, 1.0f, 1.0f});
    ecs_set(world, c, Position, {2.0f, 0.0f, 0.0f});
    ecs_set(world, g, Position, {0.0f, 3.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
    {
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 1.0f, 1.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, g, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 4.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, e, EcsChildOf, d);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
    {
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 2.0f, 2.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 4.0f, 2.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, g, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -3.0f, 4.0f, 2.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, e, EcsChildOf, a);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
    {
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 1.0f, 1.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, g, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 4.0f, 1.0f, 1.0f
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

void Reparent_dynamic_childof_to_non_transformable(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t a = ecs_new(world);
    ecs_add(world, a, DynamicTransform);
    ecs_entity_t n = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_add(world, n, DynamicTransform);
    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, p1);
    ecs_add(world, e, DynamicTransform);
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_add(world, c, DynamicTransform);
    ecs_set(world, a, Position, {0.0f, 10.0f, 0.0f});
    ecs_set(world, a, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_set(world, p1, Position, {5.0f, 0.0f, 0.0f});
    ecs_set(world, e, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, c, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 10.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, 0.0f, 0.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, e, EcsChildOf, n);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 10.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 11.0f, 0.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -1.0f, 11.0f, 0.0f, 1.0f
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

void Reparent_dynamic_childof_from_non_transformable(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t a = ecs_new(world);
    ecs_add(world, a, DynamicTransform);
    ecs_entity_t n = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_add(world, n, DynamicTransform);
    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, n);
    ecs_add(world, e, DynamicTransform);
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_add(world, c, DynamicTransform);
    ecs_set(world, a, Position, {0.0f, 10.0f, 0.0f});
    ecs_set(world, a, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_set(world, p1, Position, {5.0f, 0.0f, 0.0f});
    ecs_set(world, e, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, c, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 10.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 11.0f, 0.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -1.0f, 11.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, e, EcsChildOf, p1);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 10.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, 0.0f, 0.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, 1.0f, 0.0f, 1.0f
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

void Reparent_dynamic_childof_change_parent_of_non_transformable(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t p2 = ecs_new(world);
    ecs_add(world, p2, DynamicTransform);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t n = ecs_new_w_pair(world, EcsChildOf, p1);
    ecs_add(world, n, DynamicTransform);
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, n);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, n, EcsChildOf, p2);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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

void Reparent_dynamic_childof_non_transformable_to_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t p2 = ecs_new(world);
    ecs_add(world, p2, DynamicTransform);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t n = ecs_new_w_pair(world, EcsChildOf, p2);
    ecs_add(world, n, DynamicTransform);
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, n);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_remove_pair(world, n, EcsChildOf, p2);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 2.0f, 3.0f, 1.0f
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

void Reparent_dynamic_childof_move_old_parent(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t p2 = ecs_new(world);
    ecs_add(world, p2, DynamicTransform);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, p1);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_add_pair(world, e, EcsChildOf, p2);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, p1, Position, {100.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            100.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, p2, Position, {0.0f, 30.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            100.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 30.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 31.0f, 3.0f, 1.0f
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

void Reparent_dynamic_parent_change_parent(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t p2 = ecs_new(world);
    ecs_add(world, p2, DynamicTransform);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_parent(world, p1, NULL);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, EcsParent, {p2});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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

void Reparent_dynamic_parent_change_parent_w_scale(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t p2 = ecs_new(world);
    ecs_add(world, p2, DynamicTransform);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 0.0f, 5.0f});
    ecs_set(world, p2, Scale, {2.0f, 3.0f, 4.0f});
    ecs_entity_t e = ecs_new_w_parent(world, p1, NULL);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, e, Rotation, {1.5707964f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 4.0f, 0.0f,
            0.0f, 0.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, -1.0f, 0.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, EcsParent, {p2});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 4.0f, 0.0f,
            0.0f, 0.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 4.0f, 0.0f,
            0.0f, -3.0f, 0.0f, 0.0f,
            2.0f, 6.0f, 17.0f, 1.0f
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

void Reparent_dynamic_parent_change_parent_w_children(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t p2 = ecs_new(world);
    ecs_add(world, p2, DynamicTransform);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_parent(world, p1, NULL);
    ecs_add(world, e, DynamicTransform);
    ecs_entity_t c = ecs_new_w_parent(world, e, NULL);
    ecs_add(world, c, DynamicTransform);
    ecs_entity_t g = ecs_new_w_parent(world, c, NULL);
    ecs_add(world, g, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, c, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, g, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            12.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, g, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            12.0f, 3.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, EcsParent, {p2});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 22.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, g, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -3.0f, 22.0f, 3.0f, 1.0f
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

void Reparent_dynamic_parent_to_childof(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t p2 = ecs_new(world);
    ecs_add(world, p2, DynamicTransform);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_parent(world, p1, NULL);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_remove(world, e, EcsParent);
    ecs_add_pair(world, e, EcsChildOf, p2);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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

void Reparent_dynamic_parent_to_childof_w_children(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t p2 = ecs_new(world);
    ecs_add(world, p2, DynamicTransform);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_parent(world, p1, NULL);
    ecs_add(world, e, DynamicTransform);
    ecs_entity_t c = ecs_new_w_parent(world, e, NULL);
    ecs_add(world, c, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, c, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            12.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_remove(world, e, EcsParent);
    ecs_add_pair(world, e, EcsChildOf, p2);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 22.0f, 3.0f, 1.0f
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

void Reparent_dynamic_parent_to_childof_same_parent(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t p2 = ecs_new(world);
    ecs_add(world, p2, DynamicTransform);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_parent(world, p2, NULL);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_remove(world, e, EcsParent);
    ecs_add_pair(world, e, EcsChildOf, p2);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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

void Reparent_dynamic_parent_to_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t p2 = ecs_new(world);
    ecs_add(world, p2, DynamicTransform);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_parent(world, p2, NULL);
    ecs_add(world, e, DynamicTransform);
    ecs_entity_t c = ecs_new_w_parent(world, e, NULL);
    ecs_add(world, c, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, c, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 22.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_remove(world, e, EcsParent);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 2.0f, 3.0f, 1.0f
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

void Reparent_dynamic_parent_from_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t p2 = ecs_new(world);
    ecs_add(world, p2, DynamicTransform);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, DynamicTransform);
    ecs_entity_t c = ecs_new_w_parent(world, e, NULL);
    ecs_add(world, c, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, c, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, EcsParent, {p2});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 22.0f, 3.0f, 1.0f
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

void Reparent_dynamic_parent_change_parent_deeper(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t a = ecs_new(world);
    ecs_add(world, a, DynamicTransform);
    ecs_entity_t b = ecs_new_w_parent(world, a, NULL);
    ecs_add(world, b, DynamicTransform);
    ecs_entity_t d = ecs_new_w_parent(world, b, NULL);
    ecs_add(world, d, DynamicTransform);
    ecs_entity_t r = ecs_new(world);
    ecs_add(world, r, DynamicTransform);
    ecs_entity_t e = ecs_new_w_parent(world, r, NULL);
    ecs_add(world, e, DynamicTransform);
    ecs_entity_t c = ecs_new_w_parent(world, e, NULL);
    ecs_add(world, c, DynamicTransform);
    ecs_set(world, a, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, b, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, b, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_set(world, d, Position, {0.0f, 0.0f, 1.0f});
    ecs_set(world, r, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, e, Position, {1.0f, 1.0f, 1.0f});
    ecs_set(world, c, Position, {2.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, r, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 1.0f, 1.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            13.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, EcsParent, {d});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, r, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 2.0f, 2.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 4.0f, 2.0f, 1.0f
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

void Reparent_dynamic_parent_change_parent_shallower(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t a = ecs_new(world);
    ecs_add(world, a, DynamicTransform);
    ecs_entity_t b = ecs_new_w_parent(world, a, NULL);
    ecs_add(world, b, DynamicTransform);
    ecs_entity_t d = ecs_new_w_parent(world, b, NULL);
    ecs_add(world, d, DynamicTransform);
    ecs_entity_t r = ecs_new(world);
    ecs_add(world, r, DynamicTransform);
    ecs_entity_t e = ecs_new_w_parent(world, d, NULL);
    ecs_add(world, e, DynamicTransform);
    ecs_entity_t c = ecs_new_w_parent(world, e, NULL);
    ecs_add(world, c, DynamicTransform);
    ecs_set(world, a, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, b, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, b, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_set(world, d, Position, {0.0f, 0.0f, 1.0f});
    ecs_set(world, r, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, e, Position, {1.0f, 1.0f, 1.0f});
    ecs_set(world, c, Position, {2.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, r, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 2.0f, 2.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 4.0f, 2.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, EcsParent, {r});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, r, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 1.0f, 1.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            13.0f, 1.0f, 1.0f, 1.0f
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

void Reparent_dynamic_parent_change_parent_deeper_and_back(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t a = ecs_new(world);
    ecs_add(world, a, DynamicTransform);
    ecs_entity_t b = ecs_new_w_parent(world, a, NULL);
    ecs_add(world, b, DynamicTransform);
    ecs_entity_t d = ecs_new_w_parent(world, b, NULL);
    ecs_add(world, d, DynamicTransform);
    ecs_entity_t e = ecs_new_w_parent(world, a, NULL);
    ecs_add(world, e, DynamicTransform);
    ecs_entity_t c = ecs_new_w_parent(world, e, NULL);
    ecs_add(world, c, DynamicTransform);
    ecs_entity_t g = ecs_new_w_parent(world, c, NULL);
    ecs_add(world, g, DynamicTransform);
    ecs_set(world, a, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, b, Position, {0.0f, 1.0f, 0.0f});
    ecs_set(world, d, Position, {0.0f, 0.0f, 1.0f});
    ecs_set(world, d, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_set(world, e, Position, {1.0f, 1.0f, 1.0f});
    ecs_set(world, c, Position, {2.0f, 0.0f, 0.0f});
    ecs_set(world, g, Position, {0.0f, 3.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
    {
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 1.0f, 1.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, g, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 4.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, EcsParent, {d});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
    {
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 2.0f, 2.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 4.0f, 2.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, g, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -3.0f, 4.0f, 2.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, EcsParent, {a});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, b, WorldTransform);
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
    {
        const WorldTransform *t = ecs_get(world, d, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            2.0f, 1.0f, 1.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, g, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 4.0f, 1.0f, 1.0f
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

void Reparent_dynamic_parent_to_non_transformable(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t a = ecs_new(world);
    ecs_add(world, a, DynamicTransform);
    ecs_entity_t n = ecs_new_w_parent(world, a, NULL);
    ecs_add(world, n, DynamicTransform);
    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t e = ecs_new_w_parent(world, p1, NULL);
    ecs_add(world, e, DynamicTransform);
    ecs_entity_t c = ecs_new_w_parent(world, e, NULL);
    ecs_add(world, c, DynamicTransform);
    ecs_set(world, a, Position, {0.0f, 10.0f, 0.0f});
    ecs_set(world, a, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_set(world, p1, Position, {5.0f, 0.0f, 0.0f});
    ecs_set(world, e, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, c, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 10.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, 0.0f, 0.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, 1.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, EcsParent, {n});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 10.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 11.0f, 0.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -1.0f, 11.0f, 0.0f, 1.0f
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

void Reparent_dynamic_parent_from_non_transformable(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t a = ecs_new(world);
    ecs_add(world, a, DynamicTransform);
    ecs_entity_t n = ecs_new_w_parent(world, a, NULL);
    ecs_add(world, n, DynamicTransform);
    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t e = ecs_new_w_parent(world, n, NULL);
    ecs_add(world, e, DynamicTransform);
    ecs_entity_t c = ecs_new_w_parent(world, e, NULL);
    ecs_add(world, c, DynamicTransform);
    ecs_set(world, a, Position, {0.0f, 10.0f, 0.0f});
    ecs_set(world, a, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_set(world, p1, Position, {5.0f, 0.0f, 0.0f});
    ecs_set(world, e, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, c, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 10.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 11.0f, 0.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -1.0f, 11.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, EcsParent, {p1});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, a, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 10.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, 0.0f, 0.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, 1.0f, 0.0f, 1.0f
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

void Reparent_dynamic_parent_change_parent_of_non_transformable(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t p2 = ecs_new(world);
    ecs_add(world, p2, DynamicTransform);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t n = ecs_new_w_parent(world, p1, NULL);
    ecs_add(world, n, DynamicTransform);
    ecs_entity_t e = ecs_new_w_parent(world, n, NULL);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, n, EcsParent, {p2});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
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

void Reparent_dynamic_parent_non_transformable_to_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t p2 = ecs_new(world);
    ecs_add(world, p2, DynamicTransform);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t n = ecs_new_w_parent(world, p2, NULL);
    ecs_add(world, n, DynamicTransform);
    ecs_entity_t e = ecs_new_w_parent(world, n, NULL);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_remove(world, n, EcsParent);

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, n, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 2.0f, 3.0f, 1.0f
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

void Reparent_dynamic_parent_move_old_parent(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t p1 = ecs_new(world);
    ecs_add(world, p1, DynamicTransform);
    ecs_entity_t p2 = ecs_new(world);
    ecs_add(world, p2, DynamicTransform);
    ecs_set(world, p1, Position, {10.0f, 0.0f, 0.0f});
    ecs_set(world, p2, Position, {0.0f, 20.0f, 0.0f});
    ecs_set(world, p2, Rotation, {0.0f, 0.0f, 1.5707964f});
    ecs_entity_t e = ecs_new_w_parent(world, p1, NULL);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            11.0f, 2.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, EcsParent, {p2});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, p1, Position, {100.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            100.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 20.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 21.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, p2, Position, {0.0f, 30.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, p1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            100.0f, 0.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, p2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 30.0f, 0.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f, 31.0f, 3.0f, 1.0f
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
