#include <box.h>
#include <bake_test.h>
#include <math.h>

void StaticChildOf_single_none(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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

void StaticChildOf_single_r(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.942155f, 0.294044f, -0.160881f, 0.0f,
            -0.270681f, 0.950564f, 0.152184f, 0.0f,
            0.197677f, -0.099833f, 0.97517f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Rotation, {0.3f, 0.1f, 0.45f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.908781f, 0.415538f, 0.038004f, 0.0f,
            -0.406227f, 0.86023f, 0.308195f, 0.0f,
            0.095375f, -0.29552f, 0.950564f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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

void StaticChildOf_single_s(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Scale, {1.5f, 0.5f, 2.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Scale, {1.75f, 1.0f, 1.75f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.75f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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

void StaticChildOf_single_rs(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e0, Scale, {1.5f, 0.5f, 2.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.413232f, 0.441066f, -0.241322f, 0.0f,
            -0.135341f, 0.475282f, 0.076092f, 0.0f,
            0.395354f, -0.199667f, 1.950341f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Rotation, {0.3f, 0.1f, 0.45f});
    ecs_set(world, e0, Scale, {1.75f, 1.0f, 1.75f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.590367f, 0.727192f, 0.066507f, 0.0f,
            -0.406227f, 0.86023f, 0.308195f, 0.0f,
            0.166905f, -0.51716f, 1.663487f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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

void StaticChildOf_depth_1_none(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            3.0f, 3.0f, 1.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            8.0f, 0.0f, 3.5f, 1.0f
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

void StaticChildOf_depth_1_r(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Rotation, {0.2f, 0.25f, 0.25f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.942155f, 0.294044f, -0.160881f, 0.0f,
            -0.270681f, 0.950564f, 0.152184f, 0.0f,
            0.197677f, -0.099833f, 0.97517f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            0.792339f, 0.529284f, -0.30341f, 0.0f,
            -0.389048f, 0.821441f, 0.416984f, 0.0f,
            0.469936f, -0.212352f, 0.856777f, 0.0f,
            2.811305f, 3.438818f, 1.305592f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Rotation, {0.3f, 0.1f, 0.45f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.908781f, 0.415538f, 0.038004f, 0.0f,
            -0.406227f, 0.86023f, 0.308195f, 0.0f,
            0.095375f, -0.29552f, 0.950564f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            0.747388f, 0.660505f, -0.071724f, 0.0f,
            -0.536694f, 0.663847f, 0.520833f, 0.0f,
            0.391627f, -0.35077f, 0.85064f, 0.0f,
            7.50671f, 0.395787f, 3.834767f, 1.0f
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

void StaticChildOf_depth_1_s(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Scale, {1.5f, 0.5f, 2.0f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Scale, {0.5f, 2.0f, 1.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            0.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 3.0f, 0.0f,
            4.0f, 2.5f, 2.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Scale, {1.75f, 1.0f, 1.75f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.75f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            0.875f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.625f, 0.0f,
            9.5f, 0.0f, 4.25f, 1.0f
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

void StaticChildOf_depth_1_rs(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e0, Scale, {1.5f, 0.5f, 2.0f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Rotation, {0.2f, 0.25f, 0.25f});
    ecs_set(world, e1, Scale, {0.5f, 2.0f, 1.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.413232f, 0.441066f, -0.241322f, 0.0f,
            -0.135341f, 0.475282f, 0.076092f, 0.0f,
            0.395354f, -0.199667f, 1.950341f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            0.617578f, 0.286514f, -0.292837f, 0.0f,
            -0.6041f, 0.634284f, 1.203493f, 0.0f,
            1.117478f, -0.265622f, 2.667615f, 0.0f,
            4.086477f, 3.157747f, 2.043789f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Rotation, {0.3f, 0.1f, 0.45f});
    ecs_set(world, e0, Scale, {1.75f, 1.0f, 1.75f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.590367f, 0.727192f, 0.066507f, 0.0f,
            -0.406227f, 0.86023f, 0.308195f, 0.0f,
            0.166905f, -0.51716f, 1.663487f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            0.690901f, 0.499724f, -0.090782f, 0.0f,
            -1.299799f, 1.098155f, 1.383923f, 0.0f,
            0.937227f, -0.728508f, 2.301813f, 0.0f,
            8.941413f, 0.797454f, 4.604696f, 1.0f
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

void StaticChildOf_depth_1_none_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);

    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
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

    ecs_set(world, e1, Position, {7.0f, -2.0f, 3.0f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            7.0f, -2.0f, 3.0f, 1.0f
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

void StaticChildOf_depth_1_r_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);

    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Rotation, {0.2f, 0.25f, 0.25f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.950952f, 0.242472f, -0.192089f, 0.0f,
            -0.192089f, 0.949599f, 0.247718f, 0.0f,
            0.242472f, -0.198669f, 0.949599f, 0.0f,
            2.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e1, Position, {7.0f, -2.0f, 3.0f});
    ecs_set(world, e1, Rotation, {0.4f, 0.15f, 0.4f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.93338f, 0.358678f, 0.012302f, 0.0f,
            -0.331445f, 0.848353f, 0.412844f, 0.0f,
            0.137642f, -0.389418f, 0.910718f, 0.0f,
            7.0f, -2.0f, 3.0f, 1.0f
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

void StaticChildOf_depth_1_s_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);

    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Scale, {0.5f, 2.0f, 1.5f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.5f, 0.0f,
            2.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e1, Position, {7.0f, -2.0f, 3.0f});
    ecs_set(world, e1, Scale, {0.75f, 2.5f, 1.25f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.25f, 0.0f,
            7.0f, -2.0f, 3.0f, 1.0f
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

void StaticChildOf_depth_1_rs_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);

    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Rotation, {0.2f, 0.25f, 0.25f});
    ecs_set(world, e1, Scale, {0.5f, 2.0f, 1.5f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.475476f, 0.121236f, -0.096045f, 0.0f,
            -0.384178f, 1.899197f, 0.495436f, 0.0f,
            0.363709f, -0.298004f, 1.424398f, 0.0f,
            2.0f, 1.0f, 1.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e1, Position, {7.0f, -2.0f, 3.0f});
    ecs_set(world, e1, Rotation, {0.4f, 0.15f, 0.4f});
    ecs_set(world, e1, Scale, {0.75f, 2.5f, 1.25f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.700035f, 0.269009f, 0.009227f, 0.0f,
            -0.828614f, 2.120883f, 1.032111f, 0.0f,
            0.172052f, -0.486773f, 1.138398f, 0.0f,
            7.0f, -2.0f, 3.0f, 1.0f
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

void StaticChildOf_depth_1_none_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));

    ecs_fini(world);
}

void StaticChildOf_depth_1_r_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.942155f, 0.294044f, -0.160881f, 0.0f,
            -0.270681f, 0.950564f, 0.152184f, 0.0f,
            0.197677f, -0.099833f, 0.97517f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Rotation, {0.3f, 0.1f, 0.45f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.908781f, 0.415538f, 0.038004f, 0.0f,
            -0.406227f, 0.86023f, 0.308195f, 0.0f,
            0.095375f, -0.29552f, 0.950564f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));

    ecs_fini(world);
}

void StaticChildOf_depth_1_s_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Scale, {1.5f, 0.5f, 2.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Scale, {1.75f, 1.0f, 1.75f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.75f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));

    ecs_fini(world);
}

void StaticChildOf_depth_1_rs_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e0, Scale, {1.5f, 0.5f, 2.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.413232f, 0.441066f, -0.241322f, 0.0f,
            -0.135341f, 0.475282f, 0.076092f, 0.0f,
            0.395354f, -0.199667f, 1.950341f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Rotation, {0.3f, 0.1f, 0.45f});
    ecs_set(world, e0, Scale, {1.75f, 1.0f, 1.75f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.590367f, 0.727192f, 0.066507f, 0.0f,
            -0.406227f, 0.86023f, 0.308195f, 0.0f,
            0.166905f, -0.51716f, 1.663487f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));

    ecs_fini(world);
}

void StaticChildOf_depth_2_none(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            3.0f, 3.0f, 1.5f, 1.0f
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
            6.0f, 3.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            8.0f, 0.0f, 3.5f, 1.0f
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
            11.0f, 0.0f, 5.0f, 1.0f
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

void StaticChildOf_depth_2_r(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Rotation, {0.2f, 0.25f, 0.25f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Rotation, {0.3f, 0.3f, 0.2f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.942155f, 0.294044f, -0.160881f, 0.0f,
            -0.270681f, 0.950564f, 0.152184f, 0.0f,
            0.197677f, -0.099833f, 0.97517f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            0.792339f, 0.529284f, -0.30341f, 0.0f,
            -0.389048f, 0.821441f, 0.416984f, 0.0f,
            0.469936f, -0.212352f, 0.856777f, 0.0f,
            2.811305f, 3.438818f, 1.305592f, 1.0f
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
            0.57202f, 0.710247f, -0.410295f, 0.0f,
            -0.28921f, 0.642732f, 0.709403f, 0.0f,
            0.767561f, -0.287131f, 0.573066f, 0.0f,
            5.893226f, 4.708141f, 1.680528f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Rotation, {0.3f, 0.1f, 0.45f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.908781f, 0.415538f, 0.038004f, 0.0f,
            -0.406227f, 0.86023f, 0.308195f, 0.0f,
            0.095375f, -0.29552f, 0.950564f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            0.747388f, 0.660505f, -0.071724f, 0.0f,
            -0.536694f, 0.663847f, 0.520833f, 0.0f,
            0.391627f, -0.35077f, 0.85064f, 0.0f,
            7.50671f, 0.395787f, 3.834767f, 1.0f
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
            0.519418f, 0.837801f, -0.168206f, 0.0f,
            -0.449031f, 0.435078f, 0.780435f, 0.0f,
            0.727032f, -0.329842f, 0.602186f, 0.0f,
            10.336314f, 1.851147f, 4.895555f, 1.0f
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

void StaticChildOf_depth_2_s(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Scale, {1.5f, 0.5f, 2.0f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Scale, {0.5f, 2.0f, 1.5f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Scale, {2.0f, 1.5f, 0.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            0.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 3.0f, 0.0f,
            4.0f, 2.5f, 2.5f, 1.0f
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
            1.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.5f, 0.0f,
            6.25f, 2.5f, 7.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Scale, {1.75f, 1.0f, 1.75f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.75f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            0.875f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.625f, 0.0f,
            9.5f, 0.0f, 4.25f, 1.0f
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
            1.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.3125f, 0.0f,
            12.125f, 0.0f, 8.1875f, 1.0f
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

void StaticChildOf_depth_2_rs(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e0, Scale, {1.5f, 0.5f, 2.0f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Rotation, {0.2f, 0.25f, 0.25f});
    ecs_set(world, e1, Scale, {0.5f, 2.0f, 1.5f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Rotation, {0.3f, 0.3f, 0.2f});
    ecs_set(world, e2, Scale, {2.0f, 1.5f, 0.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.413232f, 0.441066f, -0.241322f, 0.0f,
            -0.135341f, 0.475282f, 0.076092f, 0.0f,
            0.395354f, -0.199667f, 1.950341f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            0.617578f, 0.286514f, -0.292837f, 0.0f,
            -0.6041f, 0.634284f, 1.203493f, 0.0f,
            1.117478f, -0.265622f, 2.667615f, 0.0f,
            4.086477f, 3.157747f, 2.043789f, 1.0f
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
            0.426633f, 0.911301f, -1.347683f, 0.0f,
            -0.382743f, 0.712394f, 3.0781f, 0.0f,
            0.686382f, -0.174489f, 0.998158f, 0.0f,
            7.615427f, 3.618857f, 5.166699f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Rotation, {0.3f, 0.1f, 0.45f});
    ecs_set(world, e0, Scale, {1.75f, 1.0f, 1.75f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.590367f, 0.727192f, 0.066507f, 0.0f,
            -0.406227f, 0.86023f, 0.308195f, 0.0f,
            0.166905f, -0.51716f, 1.663487f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            0.690901f, 0.499724f, -0.090782f, 0.0f,
            -1.299799f, 1.098155f, 1.383923f, 0.0f,
            0.937227f, -0.728508f, 2.301813f, 0.0f,
            8.941413f, 0.797454f, 4.604696f, 1.0f
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
            0.386592f, 1.710241f, -0.722956f, 0.0f,
            -1.461957f, 1.097666f, 3.115884f, 0.0f,
            0.717275f, -0.424165f, 0.833092f, 0.0f,
            12.419958f, 1.203863f, 7.78507f, 1.0f
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

void StaticChildOf_depth_2_none_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);

    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            5.0f, 1.0f, 2.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e1, Position, {7.0f, -2.0f, 3.0f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            7.0f, -2.0f, 3.0f, 1.0f
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
            10.0f, -2.0f, 4.5f, 1.0f
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

void StaticChildOf_depth_2_r_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);

    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Rotation, {0.2f, 0.25f, 0.25f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Rotation, {0.3f, 0.3f, 0.2f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.950952f, 0.242472f, -0.192089f, 0.0f,
            -0.192089f, 0.949599f, 0.247718f, 0.0f,
            0.242472f, -0.198669f, 0.949599f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.813784f, 0.45786f, -0.357939f, 0.0f,
            -0.197619f, 0.797202f, 0.570453f, 0.0f,
            0.546537f, -0.39349f, 0.739232f, 0.0f,
            5.216563f, 1.429413f, 1.84813f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e1, Position, {7.0f, -2.0f, 3.0f});
    ecs_set(world, e1, Rotation, {0.4f, 0.15f, 0.4f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.93338f, 0.358678f, 0.012302f, 0.0f,
            -0.331445f, 0.848353f, 0.412844f, 0.0f,
            0.137642f, -0.389418f, 0.910718f, 0.0f,
            7.0f, -2.0f, 3.0f, 1.0f
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
            0.79506f, 0.59401f, -0.122602f, 0.0f,
            -0.361427f, 0.626319f, 0.690721f, 0.0f,
            0.487083f, -0.504853f, 0.712653f, 0.0f,
            10.006603f, -1.508093f, 4.402984f, 1.0f
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

void StaticChildOf_depth_2_s_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);

    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Scale, {0.5f, 2.0f, 1.5f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Scale, {2.0f, 1.5f, 0.5f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.5f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.75f, 0.0f,
            3.5f, 1.0f, 3.25f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e1, Position, {7.0f, -2.0f, 3.0f});
    ecs_set(world, e1, Scale, {0.75f, 2.5f, 1.25f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.25f, 0.0f,
            7.0f, -2.0f, 3.0f, 1.0f
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
            1.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 3.75f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.625f, 0.0f,
            9.25f, -2.0f, 4.875f, 1.0f
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

void StaticChildOf_depth_2_rs_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);

    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Rotation, {0.2f, 0.25f, 0.25f});
    ecs_set(world, e1, Scale, {0.5f, 2.0f, 1.5f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Rotation, {0.3f, 0.3f, 0.2f});
    ecs_set(world, e2, Scale, {2.0f, 1.5f, 0.5f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.475476f, 0.121236f, -0.096045f, 0.0f,
            -0.384178f, 1.899197f, 0.495436f, 0.0f,
            0.363709f, -0.298004f, 1.424398f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.591156f, 1.091345f, -0.660432f, 0.0f,
            -0.430892f, 2.498431f, 1.427446f, 0.0f,
            0.289857f, -0.399501f, 0.563238f, 0.0f,
            3.97199f, 0.916703f, 2.848463f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e1, Position, {7.0f, -2.0f, 3.0f});
    ecs_set(world, e1, Rotation, {0.4f, 0.15f, 0.4f});
    ecs_set(world, e1, Scale, {0.75f, 2.5f, 1.25f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.700035f, 0.269009f, 0.009227f, 0.0f,
            -0.828614f, 2.120883f, 1.032111f, 0.0f,
            0.172052f, -0.486773f, 1.138398f, 0.0f,
            7.0f, -2.0f, 3.0f, 1.0f
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
            0.940271f, 1.54551f, -0.122346f, 0.0f,
            -1.186598f, 2.691707f, 2.020832f, 0.0f,
            0.299767f, -0.49754f, 0.368287f, 0.0f,
            9.358184f, -1.923134f, 4.735277f, 1.0f
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

void StaticChildOf_depth_2_none_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 2.0f, 2.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, -1.0f, 4.0f, 1.0f
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

void StaticChildOf_depth_2_r_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Rotation, {0.3f, 0.3f, 0.2f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.942155f, 0.294044f, -0.160881f, 0.0f,
            -0.270681f, 0.950564f, 0.152184f, 0.0f,
            0.197677f, -0.099833f, 0.97517f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.80094f, 0.484141f, -0.352282f, 0.0f,
            -0.285313f, 0.825881f, 0.48633f, 0.0f,
            0.526395f, -0.289011f, 0.799613f, 0.0f,
            4.122979f, 2.732381f, 1.480111f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Rotation, {0.3f, 0.1f, 0.45f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.908781f, 0.415538f, 0.038004f, 0.0f,
            -0.406227f, 0.86023f, 0.308195f, 0.0f,
            0.095375f, -0.29552f, 0.950564f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.767279f, 0.62856f, -0.127259f, 0.0f,
            -0.443058f, 0.663008f, 0.603424f, 0.0f,
            0.463662f, -0.406612f, 0.787201f, 0.0f,
            8.869406f, -0.196665f, 4.039858f, 1.0f
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

void StaticChildOf_depth_2_s_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Scale, {1.5f, 0.5f, 2.0f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Scale, {2.0f, 1.5f, 0.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            3.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.75f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            5.5f, 2.0f, 3.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Scale, {1.75f, 1.0f, 1.75f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.75f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            3.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.875f, 0.0f,
            11.25f, -1.0f, 5.125f, 1.0f
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

void StaticChildOf_depth_2_rs_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e0, Scale, {1.5f, 0.5f, 2.0f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Rotation, {0.3f, 0.3f, 0.2f});
    ecs_set(world, e2, Scale, {2.0f, 1.5f, 0.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.413232f, 0.441066f, -0.241322f, 0.0f,
            -0.135341f, 0.475282f, 0.076092f, 0.0f,
            0.395354f, -0.199667f, 1.950341f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.459403f, 1.114913f, -1.342355f, 0.0f,
            -0.212071f, 0.49811f, 1.125816f, 0.0f,
            0.399904f, -0.099081f, 0.844698f, 0.0f,
            5.832726f, 3.023697f, 2.701545f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Rotation, {0.3f, 0.1f, 0.45f});
    ecs_set(world, e0, Scale, {1.75f, 1.0f, 1.75f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.590367f, 0.727192f, 0.066507f, 0.0f,
            -0.406227f, 0.86023f, 0.308195f, 0.0f,
            0.166905f, -0.51716f, 1.663487f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.801128f, 1.955057f, -0.533147f, 0.0f,
            -0.735136f, 0.834289f, 1.259357f, 0.0f,
            0.360686f, -0.260455f, 0.722955f, 0.0f,
            11.02146f, 0.405836f, 5.194752f, 1.0f
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

void StaticChildOf_depth_2_none_skip_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            3.0f, 3.0f, 1.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e2, WorldTransform));

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            8.0f, 0.0f, 3.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e2, WorldTransform));

    ecs_fini(world);
}

void StaticChildOf_depth_2_r_skip_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Rotation, {0.2f, 0.25f, 0.25f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.942155f, 0.294044f, -0.160881f, 0.0f,
            -0.270681f, 0.950564f, 0.152184f, 0.0f,
            0.197677f, -0.099833f, 0.97517f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            0.792339f, 0.529284f, -0.30341f, 0.0f,
            -0.389048f, 0.821441f, 0.416984f, 0.0f,
            0.469936f, -0.212352f, 0.856777f, 0.0f,
            2.811305f, 3.438818f, 1.305592f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e2, WorldTransform));

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Rotation, {0.3f, 0.1f, 0.45f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.908781f, 0.415538f, 0.038004f, 0.0f,
            -0.406227f, 0.86023f, 0.308195f, 0.0f,
            0.095375f, -0.29552f, 0.950564f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            0.747388f, 0.660505f, -0.071724f, 0.0f,
            -0.536694f, 0.663847f, 0.520833f, 0.0f,
            0.391627f, -0.35077f, 0.85064f, 0.0f,
            7.50671f, 0.395787f, 3.834767f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e2, WorldTransform));

    ecs_fini(world);
}

void StaticChildOf_depth_2_s_skip_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Scale, {1.5f, 0.5f, 2.0f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Scale, {0.5f, 2.0f, 1.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            0.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 3.0f, 0.0f,
            4.0f, 2.5f, 2.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e2, WorldTransform));

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Scale, {1.75f, 1.0f, 1.75f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.75f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            0.875f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.625f, 0.0f,
            9.5f, 0.0f, 4.25f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e2, WorldTransform));

    ecs_fini(world);
}

void StaticChildOf_depth_2_rs_skip_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e0, Scale, {1.5f, 0.5f, 2.0f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Rotation, {0.2f, 0.25f, 0.25f});
    ecs_set(world, e1, Scale, {0.5f, 2.0f, 1.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.413232f, 0.441066f, -0.241322f, 0.0f,
            -0.135341f, 0.475282f, 0.076092f, 0.0f,
            0.395354f, -0.199667f, 1.950341f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            0.617578f, 0.286514f, -0.292837f, 0.0f,
            -0.6041f, 0.634284f, 1.203493f, 0.0f,
            1.117478f, -0.265622f, 2.667615f, 0.0f,
            4.086477f, 3.157747f, 2.043789f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e2, WorldTransform));

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Rotation, {0.3f, 0.1f, 0.45f});
    ecs_set(world, e0, Scale, {1.75f, 1.0f, 1.75f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.590367f, 0.727192f, 0.066507f, 0.0f,
            -0.406227f, 0.86023f, 0.308195f, 0.0f,
            0.166905f, -0.51716f, 1.663487f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            0.690901f, 0.499724f, -0.090782f, 0.0f,
            -1.299799f, 1.098155f, 1.383923f, 0.0f,
            0.937227f, -0.728508f, 2.301813f, 0.0f,
            8.941413f, 0.797454f, 4.604696f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e2, WorldTransform));

    ecs_fini(world);
}

void StaticChildOf_depth_2_none_skip_0_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);

    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            3.0f, 0.0f, 1.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e2, Position, {8.0f, -3.0f, 3.5f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            8.0f, -3.0f, 3.5f, 1.0f
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

void StaticChildOf_depth_2_r_skip_0_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);

    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Rotation, {0.3f, 0.3f, 0.2f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.953644f, 0.189796f, -0.233541f, 0.0f,
            -0.104205f, 0.936293f, 0.335404f, 0.0f,
            0.282321f, -0.29552f, 0.912668f, 0.0f,
            3.0f, 0.0f, 1.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e2, Position, {8.0f, -3.0f, 3.5f});
    ecs_set(world, e2, Rotation, {0.5f, 0.2f, 0.35f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.953308f, 0.300921f, -0.025508f, 0.0f,
            -0.24659f, 0.824377f, 0.509505f, 0.0f,
            0.174349f, -0.479426f, 0.860089f, 0.0f,
            8.0f, -3.0f, 3.5f, 1.0f
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

void StaticChildOf_depth_2_s_skip_0_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);

    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Scale, {2.0f, 1.5f, 0.5f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.5f, 0.0f,
            3.0f, 0.0f, 1.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e2, Position, {8.0f, -3.0f, 3.5f});
    ecs_set(world, e2, Scale, {2.25f, 2.0f, 0.25f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.25f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.25f, 0.0f,
            8.0f, -3.0f, 3.5f, 1.0f
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

void StaticChildOf_depth_2_rs_skip_0_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);

    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Rotation, {0.3f, 0.3f, 0.2f});
    ecs_set(world, e2, Scale, {2.0f, 1.5f, 0.5f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.907287f, 0.379592f, -0.467082f, 0.0f,
            -0.156307f, 1.40444f, 0.503107f, 0.0f,
            0.141161f, -0.14776f, 0.456334f, 0.0f,
            3.0f, 0.0f, 1.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e2, Position, {8.0f, -3.0f, 3.5f});
    ecs_set(world, e2, Rotation, {0.5f, 0.2f, 0.35f});
    ecs_set(world, e2, Scale, {2.25f, 2.0f, 0.25f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.144943f, 0.677073f, -0.057392f, 0.0f,
            -0.49318f, 1.648754f, 1.019011f, 0.0f,
            0.043587f, -0.119856f, 0.215022f, 0.0f,
            8.0f, -3.0f, 3.5f, 1.0f
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

void StaticChildOf_depth_3_none(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            3.0f, 3.0f, 1.5f, 1.0f
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
            6.0f, 3.0f, 3.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 2.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            8.0f, 0.0f, 3.5f, 1.0f
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
            11.0f, 0.0f, 5.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            15.0f, -1.0f, 7.0f, 1.0f
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

void StaticChildOf_depth_3_r(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Rotation, {0.2f, 0.25f, 0.25f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Rotation, {0.3f, 0.3f, 0.2f});
    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});
    ecs_set(world, e3, Rotation, {0.4f, 0.35f, 0.15f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.942155f, 0.294044f, -0.160881f, 0.0f,
            -0.270681f, 0.950564f, 0.152184f, 0.0f,
            0.197677f, -0.099833f, 0.97517f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            0.792339f, 0.529284f, -0.30341f, 0.0f,
            -0.389048f, 0.821441f, 0.416984f, 0.0f,
            0.469936f, -0.212352f, 0.856777f, 0.0f,
            2.811305f, 3.438818f, 1.305592f, 1.0f
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
            0.57202f, 0.710247f, -0.410295f, 0.0f,
            -0.28921f, 0.642732f, 0.709403f, 0.0f,
            0.767561f, -0.287131f, 0.573066f, 0.0f,
            5.893226f, 4.708141f, 1.680528f, 1.0f
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
            0.284633f, 0.843989f, -0.454606f, 0.0f,
            0.048796f, 0.460851f, 0.886135f, 0.0f,
            0.957394f, -0.274406f, 0.08999f, 0.0f,
            10.005639f, 6.332136f, 0.476077f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Rotation, {0.3f, 0.1f, 0.45f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.908781f, 0.415538f, 0.038004f, 0.0f,
            -0.406227f, 0.86023f, 0.308195f, 0.0f,
            0.095375f, -0.29552f, 0.950564f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            0.747388f, 0.660505f, -0.071724f, 0.0f,
            -0.536694f, 0.663847f, 0.520833f, 0.0f,
            0.391627f, -0.35077f, 0.85064f, 0.0f,
            7.50671f, 0.395787f, 3.834767f, 1.0f
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
            0.519418f, 0.837801f, -0.168206f, 0.0f,
            -0.449031f, 0.435078f, 0.780435f, 0.0f,
            0.727032f, -0.329842f, 0.602186f, 0.0f,
            10.336314f, 1.851147f, 4.895555f, 1.0f
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
            0.224253f, 0.948574f, -0.223421f, 0.0f,
            -0.113053f, 0.253034f, 0.960829f, 0.0f,
            0.967951f, -0.190211f, 0.163983f, 0.0f,
            14.317083f, 4.107589f, 4.646667f, 1.0f
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

void StaticChildOf_depth_3_s(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Scale, {1.5f, 0.5f, 2.0f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Scale, {0.5f, 2.0f, 1.5f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Scale, {2.0f, 1.5f, 0.5f});
    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});
    ecs_set(world, e3, Scale, {1.25f, 0.75f, 1.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            0.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 3.0f, 0.0f,
            4.0f, 2.5f, 2.5f, 1.0f
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
            1.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.5f, 0.0f,
            6.25f, 2.5f, 7.0f, 1.0f
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
            1.875f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.125f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.25f, 0.0f,
            12.25f, 1.0f, 10.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Scale, {1.75f, 1.0f, 1.75f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.75f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            0.875f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.625f, 0.0f,
            9.5f, 0.0f, 4.25f, 1.0f
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
            1.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.3125f, 0.0f,
            12.125f, 0.0f, 8.1875f, 1.0f
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
            2.1875f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.25f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.96875f, 0.0f,
            19.125f, -3.0f, 10.8125f, 1.0f
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

void StaticChildOf_depth_3_rs(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e0, Scale, {1.5f, 0.5f, 2.0f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Rotation, {0.2f, 0.25f, 0.25f});
    ecs_set(world, e1, Scale, {0.5f, 2.0f, 1.5f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Rotation, {0.3f, 0.3f, 0.2f});
    ecs_set(world, e2, Scale, {2.0f, 1.5f, 0.5f});
    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});
    ecs_set(world, e3, Rotation, {0.4f, 0.35f, 0.15f});
    ecs_set(world, e3, Scale, {1.25f, 0.75f, 1.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.413232f, 0.441066f, -0.241322f, 0.0f,
            -0.135341f, 0.475282f, 0.076092f, 0.0f,
            0.395354f, -0.199667f, 1.950341f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            0.617578f, 0.286514f, -0.292837f, 0.0f,
            -0.6041f, 0.634284f, 1.203493f, 0.0f,
            1.117478f, -0.265622f, 2.667615f, 0.0f,
            4.086477f, 3.157747f, 2.043789f, 1.0f
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
            0.426633f, 0.911301f, -1.347683f, 0.0f,
            -0.382743f, 0.712394f, 3.0781f, 0.0f,
            0.686382f, -0.174489f, 0.998158f, 0.0f,
            7.615427f, 3.618857f, 5.166699f, 1.0f
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
            0.196131f, 1.265376f, -1.423546f, 0.0f,
            -0.051522f, 0.426847f, 2.420036f, 0.0f,
            1.316493f, -0.210862f, -1.141022f, 0.0f,
            11.077468f, 6.20269f, -1.305818f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Rotation, {0.3f, 0.1f, 0.45f});
    ecs_set(world, e0, Scale, {1.75f, 1.0f, 1.75f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.590367f, 0.727192f, 0.066507f, 0.0f,
            -0.406227f, 0.86023f, 0.308195f, 0.0f,
            0.166905f, -0.51716f, 1.663487f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            0.690901f, 0.499724f, -0.090782f, 0.0f,
            -1.299799f, 1.098155f, 1.383923f, 0.0f,
            0.937227f, -0.728508f, 2.301813f, 0.0f,
            8.941413f, 0.797454f, 4.604696f, 1.0f
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
            0.386592f, 1.710241f, -0.722956f, 0.0f,
            -1.461957f, 1.097666f, 3.115884f, 0.0f,
            0.717275f, -0.424165f, 0.833092f, 0.0f,
            12.419958f, 1.203863f, 7.78507f, 1.0f
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
            -0.048019f, 2.367938f, -0.617458f, 0.0f,
            -0.778848f, 0.607675f, 2.39081f, 0.0f,
            1.968016f, -0.381453f, -1.081359f, 0.0f,
            16.862834f, 6.09883f, 3.443547f, 1.0f
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

void StaticChildOf_depth_3_none_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
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
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            5.0f, 1.0f, 2.5f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, 0.0f, 4.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e1, Position, {7.0f, -2.0f, 3.0f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            7.0f, -2.0f, 3.0f, 1.0f
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
            10.0f, -2.0f, 4.5f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            14.0f, -3.0f, 6.5f, 1.0f
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

void StaticChildOf_depth_3_r_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Rotation, {0.2f, 0.25f, 0.25f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Rotation, {0.3f, 0.3f, 0.2f});
    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});
    ecs_set(world, e3, Rotation, {0.4f, 0.35f, 0.15f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.950952f, 0.242472f, -0.192089f, 0.0f,
            -0.192089f, 0.949599f, 0.247718f, 0.0f,
            0.242472f, -0.198669f, 0.949599f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.813784f, 0.45786f, -0.357939f, 0.0f,
            -0.197619f, 0.797202f, 0.570453f, 0.0f,
            0.546537f, -0.39349f, 0.739232f, 0.0f,
            5.216563f, 1.429413f, 1.84813f, 1.0f
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
            0.589476f, 0.656037f, -0.471311f, 0.0f,
            0.038921f, 0.559716f, 0.82777f, 0.0f,
            0.806848f, -0.506294f, 0.304405f, 0.0f,
            9.762392f, 1.676671f, 1.324385f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e1, Position, {7.0f, -2.0f, 3.0f});
    ecs_set(world, e1, Rotation, {0.4f, 0.15f, 0.4f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.93338f, 0.358678f, 0.012302f, 0.0f,
            -0.331445f, 0.848353f, 0.412844f, 0.0f,
            0.137642f, -0.389418f, 0.910718f, 0.0f,
            7.0f, -2.0f, 3.0f, 1.0f
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
            0.79506f, 0.59401f, -0.122602f, 0.0f,
            -0.361427f, 0.626319f, 0.690721f, 0.0f,
            0.487083f, -0.504853f, 0.712653f, 0.0f,
            10.006603f, -1.508093f, 4.402984f, 1.0f
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
            0.566072f, 0.793363f, -0.223916f, 0.0f,
            -0.134657f, 0.356967f, 0.92436f, 0.0f,
            0.813284f, -0.493102f, 0.308901f, 0.0f,
            14.522437f, -0.768077f, 4.647162f, 1.0f
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

void StaticChildOf_depth_3_s_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Scale, {0.5f, 2.0f, 1.5f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Scale, {2.0f, 1.5f, 0.5f});
    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});
    ecs_set(world, e3, Scale, {1.25f, 0.75f, 1.5f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.5f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.75f, 0.0f,
            3.5f, 1.0f, 3.25f, 1.0f
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
            1.25f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.25f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.125f, 0.0f,
            7.5f, -2.0f, 4.75f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e1, Position, {7.0f, -2.0f, 3.0f});
    ecs_set(world, e1, Scale, {0.75f, 2.5f, 1.25f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.25f, 0.0f,
            7.0f, -2.0f, 3.0f, 1.0f
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
            1.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 3.75f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.625f, 0.0f,
            9.25f, -2.0f, 4.875f, 1.0f
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
            1.875f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.8125f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.9375f, 0.0f,
            15.25f, -5.75f, 6.125f, 1.0f
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

void StaticChildOf_depth_3_rs_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Rotation, {0.2f, 0.25f, 0.25f});
    ecs_set(world, e1, Scale, {0.5f, 2.0f, 1.5f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Rotation, {0.3f, 0.3f, 0.2f});
    ecs_set(world, e2, Scale, {2.0f, 1.5f, 0.5f});
    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});
    ecs_set(world, e3, Rotation, {0.4f, 0.35f, 0.15f});
    ecs_set(world, e3, Scale, {1.25f, 0.75f, 1.5f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.475476f, 0.121236f, -0.096045f, 0.0f,
            -0.384178f, 1.899197f, 0.495436f, 0.0f,
            0.363709f, -0.298004f, 1.424398f, 0.0f,
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
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.591156f, 1.091345f, -0.660432f, 0.0f,
            -0.430892f, 2.498431f, 1.427446f, 0.0f,
            0.289857f, -0.399501f, 0.563238f, 0.0f,
            3.97199f, 0.916703f, 2.848463f, 1.0f
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
            0.523922f, 1.86618f, -0.737878f, 0.0f,
            -0.208245f, 1.575965f, 1.153574f, 0.0f,
            0.907938f, -1.460868f, -0.4157f, 0.0f,
            7.347221f, 1.984648f, -0.094234f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e1, Position, {7.0f, -2.0f, 3.0f});
    ecs_set(world, e1, Rotation, {0.4f, 0.15f, 0.4f});
    ecs_set(world, e1, Scale, {0.75f, 2.5f, 1.25f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.700035f, 0.269009f, 0.009227f, 0.0f,
            -0.828614f, 2.120883f, 1.032111f, 0.0f,
            0.172052f, -0.486773f, 1.138398f, 0.0f,
            7.0f, -2.0f, 3.0f, 1.0f
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
            0.940271f, 1.54551f, -0.122346f, 0.0f,
            -1.186598f, 2.691707f, 2.020832f, 0.0f,
            0.299767f, -0.49754f, 0.368287f, 0.0f,
            9.358184f, -1.923134f, 4.735277f, 1.0f
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
            0.80442f, 2.472912f, 0.071671f, 0.0f,
            -0.723539f, 1.674774f, 1.495134f, 0.0f,
            1.527619f, -1.485844f, -0.76041f, 0.0f,
            14.905397f, 0.57212f, 2.961636f, 1.0f
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

void StaticChildOf_depth_3_none_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, 2.0f, 2.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            8.0f, 1.0f, 4.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, -1.0f, 4.0f, 1.0f
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
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            13.0f, -2.0f, 6.0f, 1.0f
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

void StaticChildOf_depth_3_r_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Rotation, {0.3f, 0.3f, 0.2f});
    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});
    ecs_set(world, e3, Rotation, {0.4f, 0.35f, 0.15f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.942155f, 0.294044f, -0.160881f, 0.0f,
            -0.270681f, 0.950564f, 0.152184f, 0.0f,
            0.197677f, -0.099833f, 0.97517f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.80094f, 0.484141f, -0.352282f, 0.0f,
            -0.285313f, 0.825881f, 0.48633f, 0.0f,
            0.526395f, -0.289011f, 0.799613f, 0.0f,
            4.122979f, 2.732381f, 1.480111f, 1.0f
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
            0.570947f, 0.655208f, -0.494693f, 0.0f,
            -0.049153f, 0.628759f, 0.776045f, 0.0f,
            0.819514f, -0.418765f, 0.391194f, 0.0f,
            8.664842f, 3.265045f, 1.18388f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Rotation, {0.3f, 0.1f, 0.45f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.908781f, 0.415538f, 0.038004f, 0.0f,
            -0.406227f, 0.86023f, 0.308195f, 0.0f,
            0.095375f, -0.29552f, 0.950564f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.767279f, 0.62856f, -0.127259f, 0.0f,
            -0.443058f, 0.663008f, 0.603424f, 0.0f,
            0.463662f, -0.406612f, 0.787201f, 0.0f,
            8.869406f, -0.196665f, 4.039858f, 1.0f
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
            0.535139f, 0.803255f, -0.26155f, 0.0f,
            -0.218439f, 0.430659f, 0.875681f, 0.0f,
            0.816034f, -0.411478f, 0.405925f, 0.0f,
            13.308904f, 0.841343f, 4.5018f, 1.0f
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

void StaticChildOf_depth_3_s_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Scale, {1.5f, 0.5f, 2.0f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Scale, {2.0f, 1.5f, 0.5f});
    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});
    ecs_set(world, e3, Scale, {1.25f, 0.75f, 1.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            3.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.75f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            5.5f, 2.0f, 3.5f, 1.0f
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
            3.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.5625f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.5f, 0.0f,
            17.5f, 1.25f, 5.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Scale, {1.75f, 1.0f, 1.75f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.75f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            3.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.875f, 0.0f,
            11.25f, -1.0f, 5.125f, 1.0f
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
            4.375f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.125f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.3125f, 0.0f,
            25.25f, -2.5f, 6.875f, 1.0f
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

void StaticChildOf_depth_3_rs_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e0, Scale, {1.5f, 0.5f, 2.0f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Rotation, {0.3f, 0.3f, 0.2f});
    ecs_set(world, e2, Scale, {2.0f, 1.5f, 0.5f});
    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});
    ecs_set(world, e3, Rotation, {0.4f, 0.35f, 0.15f});
    ecs_set(world, e3, Scale, {1.25f, 0.75f, 1.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.413232f, 0.441066f, -0.241322f, 0.0f,
            -0.135341f, 0.475282f, 0.076092f, 0.0f,
            0.395354f, -0.199667f, 1.950341f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.459403f, 1.114913f, -1.342355f, 0.0f,
            -0.212071f, 0.49811f, 1.125816f, 0.0f,
            0.399904f, -0.099081f, 0.844698f, 0.0f,
            5.832726f, 3.023697f, 2.701545f, 1.0f
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
            2.738143f, 1.44318f, -1.69857f, 0.0f,
            -0.036396f, 0.302562f, 1.038989f, 0.0f,
            1.808012f, 0.108634f, -0.19728f, 0.0f,
            16.682215f, 6.787077f, -2.104296f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Rotation, {0.3f, 0.1f, 0.45f});
    ecs_set(world, e0, Scale, {1.75f, 1.0f, 1.75f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.590367f, 0.727192f, 0.066507f, 0.0f,
            -0.406227f, 0.86023f, 0.308195f, 0.0f,
            0.166905f, -0.51716f, 1.663487f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.801128f, 1.955057f, -0.533147f, 0.0f,
            -0.735136f, 0.834289f, 1.259357f, 0.0f,
            0.360686f, -0.260455f, 0.722955f, 0.0f,
            11.02146f, 0.405836f, 5.194752f, 1.0f
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
            3.067368f, 2.554774f, -0.672618f, 0.0f,
            -0.407954f, 0.476948f, 1.087432f, 0.0f,
            2.224541f, 0.100841f, -0.049929f, 0.0f,
            23.68248f, 6.870867f, 3.248715f, 1.0f
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

void StaticChildOf_depth_3_none_skip_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            3.0f, 3.0f, 1.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            7.0f, 2.0f, 3.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            8.0f, 0.0f, 3.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            12.0f, -1.0f, 5.5f, 1.0f
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

void StaticChildOf_depth_3_r_skip_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Rotation, {0.2f, 0.25f, 0.25f});
    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});
    ecs_set(world, e3, Rotation, {0.4f, 0.35f, 0.15f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.942155f, 0.294044f, -0.160881f, 0.0f,
            -0.270681f, 0.950564f, 0.152184f, 0.0f,
            0.197677f, -0.099833f, 0.97517f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            0.792339f, 0.529284f, -0.30341f, 0.0f,
            -0.389048f, 0.821441f, 0.416984f, 0.0f,
            0.469936f, -0.212352f, 0.856777f, 0.0f,
            2.811305f, 3.438818f, 1.305592f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.564564f, 0.675627f, -0.474126f, 0.0f,
            -0.16687f, 0.655994f, 0.736088f, 0.0f,
            0.808345f, -0.336452f, 0.483093f, 0.0f,
            7.309582f, 4.309808f, 1.388523f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Rotation, {0.3f, 0.1f, 0.45f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.908781f, 0.415538f, 0.038004f, 0.0f,
            -0.406227f, 0.86023f, 0.308195f, 0.0f,
            0.095375f, -0.29552f, 0.950564f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            0.747388f, 0.660505f, -0.071724f, 0.0f,
            -0.536694f, 0.663847f, 0.520833f, 0.0f,
            0.391627f, -0.35077f, 0.85064f, 0.0f,
            7.50671f, 0.395787f, 3.834767f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.523863f, 0.817799f, -0.238268f, 0.0f,
            -0.333295f, 0.454217f, 0.826197f, 0.0f,
            0.783889f, -0.3534f, 0.510516f, 0.0f,
            11.816209f, 1.672419f, 4.728318f, 1.0f
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

void StaticChildOf_depth_3_s_skip_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Scale, {1.5f, 0.5f, 2.0f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Scale, {0.5f, 2.0f, 1.5f});
    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});
    ecs_set(world, e3, Scale, {1.25f, 0.75f, 1.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            0.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 3.0f, 0.0f,
            4.0f, 2.5f, 2.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.9375f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.75f, 0.0f, 0.0f,
            0.0f, 0.0f, 4.5f, 0.0f,
            7.0f, 1.5f, 8.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Scale, {1.75f, 1.0f, 1.75f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.75f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            0.875f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.625f, 0.0f,
            9.5f, 0.0f, 4.25f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.09375f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 3.9375f, 0.0f,
            13.0f, -2.0f, 9.5f, 1.0f
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

void StaticChildOf_depth_3_rs_skip_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e0, Scale, {1.5f, 0.5f, 2.0f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Rotation, {0.2f, 0.25f, 0.25f});
    ecs_set(world, e1, Scale, {0.5f, 2.0f, 1.5f});
    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});
    ecs_set(world, e3, Rotation, {0.4f, 0.35f, 0.15f});
    ecs_set(world, e3, Scale, {1.25f, 0.75f, 1.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.413232f, 0.441066f, -0.241322f, 0.0f,
            -0.135341f, 0.475282f, 0.076092f, 0.0f,
            0.395354f, -0.199667f, 1.950341f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            0.617578f, 0.286514f, -0.292837f, 0.0f,
            -0.6041f, 0.634284f, 1.203493f, 0.0f,
            1.117478f, -0.265622f, 2.667615f, 0.0f,
            4.086477f, 3.157747f, 2.043789f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.231257f, 0.543351f, -1.08851f, 0.0f,
            -0.070399f, 0.349182f, 1.650046f, 0.0f,
            2.095741f, -0.5795f, 2.620385f, 0.0f,
            9.395843f, 3.138277f, 5.004176f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Rotation, {0.3f, 0.1f, 0.45f});
    ecs_set(world, e0, Scale, {1.75f, 1.0f, 1.75f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.590367f, 0.727192f, 0.066507f, 0.0f,
            -0.406227f, 0.86023f, 0.308195f, 0.0f,
            0.166905f, -0.51716f, 1.663487f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            0.690901f, 0.499724f, -0.090782f, 0.0f,
            -1.299799f, 1.098155f, 1.383923f, 0.0f,
            0.937227f, -0.728508f, 2.301813f, 0.0f,
            8.941413f, 0.797454f, 4.604696f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.262595f, 1.040567f, -0.6878f, 0.0f,
            -0.601872f, 0.52133f, 1.65873f, 0.0f,
            2.30292f, -1.3502f, 2.135966f, 0.0f,
            14.879272f, 0.241178f, 7.461272f, 1.0f
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

void StaticChildOf_depth_3_none_skip_3(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            3.0f, 3.0f, 1.5f, 1.0f
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
            6.0f, 3.0f, 3.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e3, WorldTransform));

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            8.0f, 0.0f, 3.5f, 1.0f
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
            11.0f, 0.0f, 5.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e3, WorldTransform));

    ecs_fini(world);
}

void StaticChildOf_depth_3_r_skip_3(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Rotation, {0.2f, 0.25f, 0.25f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Rotation, {0.3f, 0.3f, 0.2f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.942155f, 0.294044f, -0.160881f, 0.0f,
            -0.270681f, 0.950564f, 0.152184f, 0.0f,
            0.197677f, -0.099833f, 0.97517f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            0.792339f, 0.529284f, -0.30341f, 0.0f,
            -0.389048f, 0.821441f, 0.416984f, 0.0f,
            0.469936f, -0.212352f, 0.856777f, 0.0f,
            2.811305f, 3.438818f, 1.305592f, 1.0f
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
            0.57202f, 0.710247f, -0.410295f, 0.0f,
            -0.28921f, 0.642732f, 0.709403f, 0.0f,
            0.767561f, -0.287131f, 0.573066f, 0.0f,
            5.893226f, 4.708141f, 1.680528f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e3, WorldTransform));

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Rotation, {0.3f, 0.1f, 0.45f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.908781f, 0.415538f, 0.038004f, 0.0f,
            -0.406227f, 0.86023f, 0.308195f, 0.0f,
            0.095375f, -0.29552f, 0.950564f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            0.747388f, 0.660505f, -0.071724f, 0.0f,
            -0.536694f, 0.663847f, 0.520833f, 0.0f,
            0.391627f, -0.35077f, 0.85064f, 0.0f,
            7.50671f, 0.395787f, 3.834767f, 1.0f
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
            0.519418f, 0.837801f, -0.168206f, 0.0f,
            -0.449031f, 0.435078f, 0.780435f, 0.0f,
            0.727032f, -0.329842f, 0.602186f, 0.0f,
            10.336314f, 1.851147f, 4.895555f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e3, WorldTransform));

    ecs_fini(world);
}

void StaticChildOf_depth_3_s_skip_3(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Scale, {1.5f, 0.5f, 2.0f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Scale, {0.5f, 2.0f, 1.5f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Scale, {2.0f, 1.5f, 0.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            0.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 3.0f, 0.0f,
            4.0f, 2.5f, 2.5f, 1.0f
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
            1.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.5f, 0.0f,
            6.25f, 2.5f, 7.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e3, WorldTransform));

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Scale, {1.75f, 1.0f, 1.75f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.75f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            0.875f, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.625f, 0.0f,
            9.5f, 0.0f, 4.25f, 1.0f
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
            1.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 3.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.3125f, 0.0f,
            12.125f, 0.0f, 8.1875f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e3, WorldTransform));

    ecs_fini(world);
}

void StaticChildOf_depth_3_rs_skip_3(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e0, Scale, {1.5f, 0.5f, 2.0f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Rotation, {0.2f, 0.25f, 0.25f});
    ecs_set(world, e1, Scale, {0.5f, 2.0f, 1.5f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Rotation, {0.3f, 0.3f, 0.2f});
    ecs_set(world, e2, Scale, {2.0f, 1.5f, 0.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.413232f, 0.441066f, -0.241322f, 0.0f,
            -0.135341f, 0.475282f, 0.076092f, 0.0f,
            0.395354f, -0.199667f, 1.950341f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
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
            0.617578f, 0.286514f, -0.292837f, 0.0f,
            -0.6041f, 0.634284f, 1.203493f, 0.0f,
            1.117478f, -0.265622f, 2.667615f, 0.0f,
            4.086477f, 3.157747f, 2.043789f, 1.0f
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
            0.426633f, 0.911301f, -1.347683f, 0.0f,
            -0.382743f, 0.712394f, 3.0781f, 0.0f,
            0.686382f, -0.174489f, 0.998158f, 0.0f,
            7.615427f, 3.618857f, 5.166699f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e3, WorldTransform));

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Rotation, {0.3f, 0.1f, 0.45f});
    ecs_set(world, e0, Scale, {1.75f, 1.0f, 1.75f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.590367f, 0.727192f, 0.066507f, 0.0f,
            -0.406227f, 0.86023f, 0.308195f, 0.0f,
            0.166905f, -0.51716f, 1.663487f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
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
            0.690901f, 0.499724f, -0.090782f, 0.0f,
            -1.299799f, 1.098155f, 1.383923f, 0.0f,
            0.937227f, -0.728508f, 2.301813f, 0.0f,
            8.941413f, 0.797454f, 4.604696f, 1.0f
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
            0.386592f, 1.710241f, -0.722956f, 0.0f,
            -1.461957f, 1.097666f, 3.115884f, 0.0f,
            0.717275f, -0.424165f, 0.833092f, 0.0f,
            12.419958f, 1.203863f, 7.78507f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e3, WorldTransform));

    ecs_fini(world);
}

void StaticChildOf_depth_3_none_skip_1_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            5.0f, 1.0f, 2.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, -2.0f, 4.5f, 1.0f
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

void StaticChildOf_depth_3_r_skip_1_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});
    ecs_set(world, e3, Rotation, {0.4f, 0.35f, 0.15f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.942155f, 0.294044f, -0.160881f, 0.0f,
            -0.270681f, 0.950564f, 0.152184f, 0.0f,
            0.197677f, -0.099833f, 0.97517f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.800424f, 0.438211f, -0.409015f, 0.0f,
            -0.172749f, 0.822016f, 0.54263f, 0.0f,
            0.574003f, -0.363677f, 0.733662f, 0.0f,
            5.434654f, 2.025945f, 1.654631f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Rotation, {0.3f, 0.1f, 0.45f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.908781f, 0.415538f, 0.038004f, 0.0f,
            -0.406227f, 0.86023f, 0.308195f, 0.0f,
            0.095375f, -0.29552f, 0.950564f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.779196f, 0.596698f, -0.191845f, 0.0f,
            -0.338159f, 0.657926f, 0.672891f, 0.0f,
            0.527732f, -0.45944f, 0.714432f, 0.0f,
            10.232101f, -0.789117f, 4.244949f, 1.0f
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

void StaticChildOf_depth_3_s_skip_1_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Scale, {1.5f, 0.5f, 2.0f});
    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});
    ecs_set(world, e3, Scale, {1.25f, 0.75f, 1.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.0f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.875f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.375f, 0.0f, 0.0f,
            0.0f, 0.0f, 3.0f, 0.0f,
            7.0f, 1.5f, 4.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Scale, {1.75f, 1.0f, 1.75f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.75f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.75f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            2.1875f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.75f, 0.0f, 0.0f,
            0.0f, 0.0f, 2.625f, 0.0f,
            13.0f, -2.0f, 6.0f, 1.0f
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

void StaticChildOf_depth_3_rs_skip_1_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e0, Scale, {1.5f, 0.5f, 2.0f});
    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});
    ecs_set(world, e3, Rotation, {0.4f, 0.35f, 0.15f});
    ecs_set(world, e3, Scale, {1.25f, 0.75f, 1.5f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.413232f, 0.441066f, -0.241322f, 0.0f,
            -0.135341f, 0.475282f, 0.076092f, 0.0f,
            0.395354f, -0.199667f, 1.950341f, 0.0f,
            1.0f, 2.0f, 0.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.512232f, 0.675843f, -0.966411f, 0.0f,
            0.021154f, 0.260037f, 0.65752f, 0.0f,
            1.261669f, -0.327806f, 2.372437f, 0.0f,
            7.578976f, 2.889647f, 3.359301f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e0, Position, {6.0f, -1.0f, 2.5f});
    ecs_set(world, e0, Rotation, {0.3f, 0.1f, 0.45f});
    ecs_set(world, e0, Scale, {1.75f, 1.0f, 1.75f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.590367f, 0.727192f, 0.066507f, 0.0f,
            -0.406227f, 0.86023f, 0.308195f, 0.0f,
            0.166905f, -0.51716f, 1.663487f, 0.0f,
            6.0f, -1.0f, 2.5f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }
    test_assert(!ecs_has(world, e1, WorldTransform));
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.756911f, 1.194274f, -0.45943f, 0.0f,
            -0.235733f, 0.42285f, 0.725287f, 0.0f,
            1.207331f, -0.829166f, 2.010404f, 0.0f,
            13.101507f, 0.014218f, 5.784808f, 1.0f
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

void StaticChildOf_depth_3_none_skip_0_1_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    test_assert(!ecs_has(world, e1, WorldTransform));
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, -1.0f, 2.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e3, Position, {9.0f, -4.0f, 4.0f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    test_assert(!ecs_has(world, e1, WorldTransform));
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            9.0f, -4.0f, 4.0f, 1.0f
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

void StaticChildOf_depth_3_r_skip_0_1_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});
    ecs_set(world, e3, Rotation, {0.4f, 0.35f, 0.15f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    test_assert(!ecs_has(world, e1, WorldTransform));
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.948779f, 0.137642f, -0.284382f, 0.0f,
            -0.008347f, 0.910718f, 0.412943f, 0.0f,
            0.31583f, -0.389418f, 0.86522f, 0.0f,
            4.0f, -1.0f, 2.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e3, Position, {9.0f, -4.0f, 4.0f});
    ecs_set(world, e3, Rotation, {0.6f, 0.25f, 0.3f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    test_assert(!ecs_has(world, e1, WorldTransform));
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.96692f, 0.243903f, -0.074678f, 0.0f,
            -0.152878f, 0.788473f, 0.595767f, 0.0f,
            0.204191f, -0.564642f, 0.799678f, 0.0f,
            9.0f, -4.0f, 4.0f, 1.0f
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

void StaticChildOf_depth_3_s_skip_0_1_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});
    ecs_set(world, e3, Scale, {1.25f, 0.75f, 1.5f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    test_assert(!ecs_has(world, e1, WorldTransform));
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.25f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.75f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.5f, 0.0f,
            4.0f, -1.0f, 2.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e3, Position, {9.0f, -4.0f, 4.0f});
    ecs_set(world, e3, Scale, {1.5f, 1.25f, 1.25f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    test_assert(!ecs_has(world, e1, WorldTransform));
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.25f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.25f, 0.0f,
            9.0f, -4.0f, 4.0f, 1.0f
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

void StaticChildOf_depth_3_rs_skip_0_1_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_pair(world, EcsChildOf, e0);
    ecs_entity_t e2 = ecs_new_w_pair(world, EcsChildOf, e1);
    ecs_entity_t e3 = ecs_new_w_pair(world, EcsChildOf, e2);

    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});
    ecs_set(world, e3, Rotation, {0.4f, 0.35f, 0.15f});
    ecs_set(world, e3, Scale, {1.25f, 0.75f, 1.5f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    test_assert(!ecs_has(world, e1, WorldTransform));
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.185974f, 0.172052f, -0.355477f, 0.0f,
            -0.00626f, 0.683039f, 0.309707f, 0.0f,
            0.473745f, -0.584128f, 1.297829f, 0.0f,
            4.0f, -1.0f, 2.0f, 1.0f
        };
        const float *m = &t->m[0][0];
        for (int i = 0; i < 16; i ++) {
            if (fabsf(m[i] - expect[i]) > 1e-3f) {
                test_flt(m[i], expect[i]);
            }
        }
    }

    ecs_set(world, e3, Position, {9.0f, -4.0f, 4.0f});
    ecs_set(world, e3, Rotation, {0.6f, 0.25f, 0.3f});
    ecs_set(world, e3, Scale, {1.5f, 1.25f, 1.25f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    test_assert(!ecs_has(world, e1, WorldTransform));
    test_assert(!ecs_has(world, e2, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e3, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.45038f, 0.365855f, -0.112017f, 0.0f,
            -0.191097f, 0.985592f, 0.744709f, 0.0f,
            0.255239f, -0.705803f, 0.999597f, 0.0f,
            9.0f, -4.0f, 4.0f, 1.0f
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
