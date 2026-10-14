#include <box.h>
#include <bake_test.h>
#include <math.h>

void Manual_root_not_written(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ManualTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, e, WorldTransform, {{{2.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 2.0f, 0.0f}, {7.0f, 8.0f, 9.0f, 1.0f}}});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            7.0f, 8.0f, 9.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e, Position, {4.0f, 5.0f, 6.0f});
    ecs_set(world, e, Rotation, {0.0f, 0.0f, 1.5707963f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            7.0f, 8.0f, 9.0f, 1.0f
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

void Manual_root_manual_dynamic_not_written(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, DynamicTransform);
    ecs_add(world, e, ManualTransform);
    ecs_set(world, e, Position, {1.0f, 2.0f, 3.0f});
    ecs_set(world, e, WorldTransform, {{{2.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 2.0f, 0.0f}, {7.0f, 8.0f, 9.0f, 1.0f}}});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            7.0f, 8.0f, 9.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    {
        Position *p = ecs_ensure(world, e, Position);
        p->x = 4.0f;
    }

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            7.0f, 8.0f, 9.0f, 1.0f
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

void Manual_static_childof_manual_child_not_written(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {1.0f, 0.0f, 0.0f});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_add(world, child, ManualTransform);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, child, WorldTransform, {{{2.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 2.0f, 0.0f}, {7.0f, 8.0f, 9.0f, 1.0f}}});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, parent, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            7.0f, 8.0f, 9.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, parent, Position, {5.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, parent, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            7.0f, 8.0f, 9.0f, 1.0f
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

void Manual_dynamic_childof_manual_child_not_written(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, DynamicTransform);
    ecs_set(world, parent, Position, {1.0f, 0.0f, 0.0f});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_add(world, child, ManualTransform);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, child, WorldTransform, {{{2.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 2.0f, 0.0f}, {7.0f, 8.0f, 9.0f, 1.0f}}});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, parent, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            7.0f, 8.0f, 9.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, parent, Position, {5.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, parent, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            7.0f, 8.0f, 9.0f, 1.0f
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

void Manual_dynamic_childof_manual_dynamic_child_not_written(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, DynamicTransform);
    ecs_set(world, parent, Position, {1.0f, 0.0f, 0.0f});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_add(world, child, DynamicTransform);
    ecs_add(world, child, ManualTransform);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, child, WorldTransform, {{{2.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 2.0f, 0.0f}, {7.0f, 8.0f, 9.0f, 1.0f}}});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, parent, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            7.0f, 8.0f, 9.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    {
        Position *p = ecs_ensure(world, parent, Position);
        p->x = 5.0f;
    }
    {
        Position *p = ecs_ensure(world, child, Position);
        p->x = 3.0f;
    }

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, parent, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            7.0f, 8.0f, 9.0f, 1.0f
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

void Manual_manual_childof_static_child_composes(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t manual = ecs_new(world);
    ecs_add(world, manual, ManualTransform);
    ecs_set(world, manual, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, manual, WorldTransform, {{{0.0f, 1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {10.0f, 0.0f, 0.0f, 1.0f}}});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, manual);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, manual, WorldTransform);
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

    ecs_set(world, child, Position, {2.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, manual, WorldTransform);
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 2.0f, 0.0f, 1.0f
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

void Manual_manual_childof_dynamic_child_composes(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t manual = ecs_new(world);
    ecs_add(world, manual, ManualTransform);
    ecs_set(world, manual, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, manual, WorldTransform, {{{0.0f, 1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {10.0f, 0.0f, 0.0f, 1.0f}}});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, manual);
    ecs_add(world, child, DynamicTransform);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, manual, WorldTransform);
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
        Position *p = ecs_ensure(world, child, Position);
        p->x = 2.0f;
    }

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, manual, WorldTransform);
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 2.0f, 0.0f, 1.0f
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

void Manual_manual_childof_dynamic_child_follows_manual_change(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t manual = ecs_new(world);
    ecs_add(world, manual, ManualTransform);
    ecs_set(world, manual, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, manual, WorldTransform, {{{0.0f, 1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {10.0f, 0.0f, 0.0f, 1.0f}}});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, manual);
    ecs_add(world, child, DynamicTransform);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
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

    ecs_set(world, manual, WorldTransform, {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {20.0f, 0.0f, 0.0f, 1.0f}}});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, manual, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            20.0f, 0.0f, 0.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            21.0f, 0.0f, 0.0f, 1.0f
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

void Manual_static_childof_manual_child_static_grandchild_composes(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {1.0f, 0.0f, 0.0f});
    ecs_entity_t manual = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_add(world, manual, ManualTransform);
    ecs_set(world, manual, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, manual, WorldTransform, {{{0.0f, 1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {10.0f, 0.0f, 0.0f, 1.0f}}});
    ecs_entity_t grandchild = ecs_new_w_pair(world, EcsChildOf, manual);
    ecs_set(world, grandchild, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, parent, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, manual, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, grandchild, WorldTransform);
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

    ecs_set(world, parent, Position, {5.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, parent, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, manual, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, grandchild, WorldTransform);
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

    ecs_fini(world);
}

void Manual_static_parent_manual_child_not_written(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {1.0f, 0.0f, 0.0f});
    ecs_entity_t child = ecs_new_w_parent(world, parent, NULL);
    ecs_add(world, child, ManualTransform);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, child, WorldTransform, {{{2.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 2.0f, 0.0f}, {7.0f, 8.0f, 9.0f, 1.0f}}});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, parent, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            7.0f, 8.0f, 9.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, parent, Position, {5.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, parent, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            7.0f, 8.0f, 9.0f, 1.0f
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

void Manual_dynamic_parent_manual_child_not_written(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, DynamicTransform);
    ecs_set(world, parent, Position, {1.0f, 0.0f, 0.0f});
    ecs_entity_t child = ecs_new_w_parent(world, parent, NULL);
    ecs_add(world, child, ManualTransform);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, child, WorldTransform, {{{2.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 2.0f, 0.0f}, {7.0f, 8.0f, 9.0f, 1.0f}}});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, parent, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            7.0f, 8.0f, 9.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, parent, Position, {5.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, parent, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            7.0f, 8.0f, 9.0f, 1.0f
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

void Manual_dynamic_parent_manual_dynamic_child_not_written(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, DynamicTransform);
    ecs_set(world, parent, Position, {1.0f, 0.0f, 0.0f});
    ecs_entity_t child = ecs_new_w_parent(world, parent, NULL);
    ecs_add(world, child, DynamicTransform);
    ecs_add(world, child, ManualTransform);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, child, WorldTransform, {{{2.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 2.0f, 0.0f}, {7.0f, 8.0f, 9.0f, 1.0f}}});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, parent, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            7.0f, 8.0f, 9.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    {
        Position *p = ecs_ensure(world, parent, Position);
        p->x = 5.0f;
    }
    {
        Position *p = ecs_ensure(world, child, Position);
        p->x = 3.0f;
    }

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, parent, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            7.0f, 8.0f, 9.0f, 1.0f
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

void Manual_manual_parent_static_child_composes(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t manual = ecs_new(world);
    ecs_add(world, manual, ManualTransform);
    ecs_set(world, manual, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, manual, WorldTransform, {{{0.0f, 1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {10.0f, 0.0f, 0.0f, 1.0f}}});
    ecs_entity_t child = ecs_new_w_parent(world, manual, NULL);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, manual, WorldTransform);
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

    ecs_set(world, child, Position, {2.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, manual, WorldTransform);
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 2.0f, 0.0f, 1.0f
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

void Manual_manual_parent_dynamic_child_composes(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t manual = ecs_new(world);
    ecs_add(world, manual, ManualTransform);
    ecs_set(world, manual, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, manual, WorldTransform, {{{0.0f, 1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {10.0f, 0.0f, 0.0f, 1.0f}}});
    ecs_entity_t child = ecs_new_w_parent(world, manual, NULL);
    ecs_add(world, child, DynamicTransform);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, manual, WorldTransform);
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
        Position *p = ecs_ensure(world, child, Position);
        p->x = 2.0f;
    }

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, manual, WorldTransform);
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
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 2.0f, 0.0f, 1.0f
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

void Manual_manual_parent_dynamic_child_follows_manual_change(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t manual = ecs_new(world);
    ecs_add(world, manual, ManualTransform);
    ecs_set(world, manual, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, manual, WorldTransform, {{{0.0f, 1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {10.0f, 0.0f, 0.0f, 1.0f}}});
    ecs_entity_t child = ecs_new_w_parent(world, manual, NULL);
    ecs_add(world, child, DynamicTransform);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
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

    ecs_set(world, manual, WorldTransform, {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {20.0f, 0.0f, 0.0f, 1.0f}}});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, manual, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            20.0f, 0.0f, 0.0f, 1.0f
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
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            21.0f, 0.0f, 0.0f, 1.0f
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

void Manual_static_parent_manual_child_static_grandchild_composes(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {1.0f, 0.0f, 0.0f});
    ecs_entity_t manual = ecs_new_w_parent(world, parent, NULL);
    ecs_add(world, manual, ManualTransform);
    ecs_set(world, manual, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, manual, WorldTransform, {{{0.0f, 1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {10.0f, 0.0f, 0.0f, 1.0f}}});
    ecs_entity_t grandchild = ecs_new_w_parent(world, manual, NULL);
    ecs_set(world, grandchild, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, parent, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, manual, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, grandchild, WorldTransform);
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

    ecs_set(world, parent, Position, {5.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, parent, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, manual, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, grandchild, WorldTransform);
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

    ecs_fini(world);
}

void Manual_manual_childof_static_child_follows_manual_change(void) {
    // core_fixes/manual_static_children
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t manual = ecs_new(world);
    ecs_add(world, manual, ManualTransform);
    ecs_set(world, manual, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, manual, WorldTransform, {{{0.0f, 1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {10.0f, 0.0f, 0.0f, 1.0f}}});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, manual);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});
    ecs_entity_t grandchild = ecs_new_w_pair(world, EcsChildOf, child);
    ecs_set(world, grandchild, Position, {0.0f, 1.0f, 0.0f});

    ecs_progress(world, 0);

    test_flt(ecs_get(world, child, WorldTransform)->m[3][0], 10.0f);
    test_flt(ecs_get(world, child, WorldTransform)->m[3][1], 1.0f);

    ecs_set(world, manual, WorldTransform, {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {20.0f, 0.0f, 0.0f, 1.0f}}});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            21.0f, 0.0f, 0.0f, 1.0f
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
        test_flt(t->m[3][0], 21.0f);
        test_flt(t->m[3][1], 1.0f);
    }

    ecs_fini(world);
}

void Manual_manual_parent_static_child_follows_manual_change(void) {
    // core_fixes/manual_static_children
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t manual = ecs_new(world);
    ecs_add(world, manual, ManualTransform);
    ecs_set(world, manual, Position, {1.0f, 0.0f, 0.0f});
    ecs_set(world, manual, WorldTransform, {{{0.0f, 1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {10.0f, 0.0f, 0.0f, 1.0f}}});
    ecs_entity_t child = ecs_new_w_parent(world, manual, NULL);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    test_flt(ecs_get(world, child, WorldTransform)->m[3][0], 10.0f);
    test_flt(ecs_get(world, child, WorldTransform)->m[3][1], 1.0f);

    ecs_set(world, manual, WorldTransform, {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {20.0f, 0.0f, 0.0f, 1.0f}}});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, child, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            21.0f, 0.0f, 0.0f, 1.0f
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

void Manual_manual_childof_static_child_follows_modified(void) {
    // core_fixes/manual_static_children
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t manual = ecs_new(world);
    ecs_add(world, manual, ManualTransform);
    ecs_set(world, manual, WorldTransform, {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {10.0f, 0.0f, 0.0f, 1.0f}}});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, manual);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    test_flt(ecs_get(world, child, WorldTransform)->m[3][0], 11.0f);

    WorldTransform *t = ecs_get_mut(world, manual, WorldTransform);
    t->m[3][0] = 30.0f;
    ecs_modified(world, manual, WorldTransform);

    ecs_progress(world, 0);

    test_flt(ecs_get(world, child, WorldTransform)->m[3][0], 31.0f);

    ecs_fini(world);
}
