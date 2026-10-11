#include <box.h>
#include <bake_test.h>
#include <math.h>

void StaticParent_single_none(void) {
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

void StaticParent_single_r(void) {
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
            0.936293f, 0.312992f, -0.159345f, 0.0f,
            -0.289629f, 0.944702f, 0.153792f, 0.0f,
            0.198669f, -0.097843f, 0.97517f, 0.0f,
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
            0.895949f, 0.442104f, 0.042661f, 0.0f,
            -0.432793f, 0.847397f, 0.307585f, 0.0f,
            0.099833f, -0.294044f, 0.950564f, 0.0f,
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

void StaticParent_single_s(void) {
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

void StaticParent_single_rs(void) {
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
            1.40444f, 0.469488f, -0.239018f, 0.0f,
            -0.144815f, 0.472351f, 0.076896f, 0.0f,
            0.397339f, -0.195687f, 1.950341f, 0.0f,
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
            1.56791f, 0.773682f, 0.074657f, 0.0f,
            -0.432793f, 0.847397f, 0.307585f, 0.0f,
            0.174708f, -0.514577f, 1.663487f, 0.0f,
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

void StaticParent_depth_1_none(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);

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

void StaticParent_depth_1_r(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Rotation, {0.2f, 0.25f, 0.25f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.936293f, 0.312992f, -0.159345f, 0.0f,
            -0.289629f, 0.944702f, 0.153792f, 0.0f,
            0.198669f, -0.097843f, 0.97517f, 0.0f,
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
            0.758054f, 0.586066f, -0.286147f, 0.0f,
            -0.445791f, 0.785869f, 0.42858f, 0.0f,
            0.476051f, -0.197325f, 0.856994f, 0.0f,
            2.781627f, 3.472843f, 1.310272f, 1.0f
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
            0.895949f, 0.442104f, 0.042661f, 0.0f,
            -0.432793f, 0.847397f, 0.307585f, 0.0f,
            0.099833f, -0.294044f, 0.950564f, 0.0f,
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
            0.69701f, 0.715498f, -0.047319f, 0.0f,
            -0.595281f, 0.614164f, 0.518116f, 0.0f,
            0.399773f, -0.332964f, 0.854001f, 0.0f,
            7.458938f, 0.437562f, 3.843472f, 1.0f
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

void StaticParent_depth_1_s(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);

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

void StaticParent_depth_1_rs(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);

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
            1.40444f, 0.469488f, -0.239018f, 0.0f,
            -0.144815f, 0.472351f, 0.076896f, 0.0f,
            0.397339f, -0.195687f, 1.950341f, 0.0f,
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
            0.601324f, 0.307067f, -0.28221f, 0.0f,
            -0.744193f, 0.561701f, 1.243613f, 0.0f,
            1.128978f, -0.240893f, 2.667158f, 0.0f,
            4.061404f, 3.21564f, 2.049201f, 1.0f
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
            1.56791f, 0.773682f, 0.074657f, 0.0f,
            -0.432793f, 0.847397f, 0.307585f, 0.0f,
            0.174708f, -0.514577f, 1.663487f, 0.0f,
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
            0.656966f, 0.533876f, -0.074865f, 0.0f,
            -1.474907f, 0.958f, 1.380891f, 0.0f,
            0.95568f, -0.690521f, 2.308361f, 0.0f,
            8.877736f, 0.880185f, 4.620386f, 1.0f
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

void StaticParent_depth_1_none_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);

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

void StaticParent_depth_1_r_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);

    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Rotation, {0.2f, 0.25f, 0.25f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.938791f, 0.290096f, -0.185783f, 0.0f,
            -0.239713f, 0.937438f, 0.252482f, 0.0f,
            0.247404f, -0.192493f, 0.949599f, 0.0f,
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
            0.910718f, 0.412278f, 0.02487f, 0.0f,
            -0.385046f, 0.825692f, 0.412278f, 0.0f,
            0.149438f, -0.385046f, 0.910718f, 0.0f,
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

void StaticParent_depth_1_s_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);

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

void StaticParent_depth_1_rs_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);

    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Rotation, {0.2f, 0.25f, 0.25f});
    ecs_set(world, e1, Scale, {0.5f, 2.0f, 1.5f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e1, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.469396f, 0.145048f, -0.092891f, 0.0f,
            -0.479426f, 1.874877f, 0.504964f, 0.0f,
            0.371106f, -0.28874f, 1.424398f, 0.0f,
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
            0.683039f, 0.309209f, 0.018653f, 0.0f,
            -0.962614f, 2.064229f, 1.030696f, 0.0f,
            0.186798f, -0.481307f, 1.138398f, 0.0f,
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

void StaticParent_depth_1_none_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);

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

void StaticParent_depth_1_r_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.936293f, 0.312992f, -0.159345f, 0.0f,
            -0.289629f, 0.944702f, 0.153792f, 0.0f,
            0.198669f, -0.097843f, 0.97517f, 0.0f,
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
            0.895949f, 0.442104f, 0.042661f, 0.0f,
            -0.432793f, 0.847397f, 0.307585f, 0.0f,
            0.099833f, -0.294044f, 0.950564f, 0.0f,
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

void StaticParent_depth_1_s_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);

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

void StaticParent_depth_1_rs_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e0, Scale, {1.5f, 0.5f, 2.0f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            1.40444f, 0.469488f, -0.239018f, 0.0f,
            -0.144815f, 0.472351f, 0.076896f, 0.0f,
            0.397339f, -0.195687f, 1.950341f, 0.0f,
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
            1.56791f, 0.773682f, 0.074657f, 0.0f,
            -0.432793f, 0.847397f, 0.307585f, 0.0f,
            0.174708f, -0.514577f, 1.663487f, 0.0f,
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

void StaticParent_depth_2_none(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);

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

void StaticParent_depth_2_r(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);

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
            0.936293f, 0.312992f, -0.159345f, 0.0f,
            -0.289629f, 0.944702f, 0.153792f, 0.0f,
            0.198669f, -0.097843f, 0.97517f, 0.0f,
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
            0.758054f, 0.586066f, -0.286147f, 0.0f,
            -0.445791f, 0.785869f, 0.42858f, 0.0f,
            0.476051f, -0.197325f, 0.856994f, 0.0f,
            2.781627f, 3.472843f, 1.310272f, 1.0f
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
            0.483225f, 0.808162f, -0.336702f, 0.0f,
            -0.388953f, 0.542717f, 0.744429f, 0.0f,
            0.784353f, -0.228766f, 0.576591f, 0.0f,
            5.769865f, 4.935053f, 1.737321f, 1.0f
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
            0.895949f, 0.442104f, 0.042661f, 0.0f,
            -0.432793f, 0.847397f, 0.307585f, 0.0f,
            0.099833f, -0.294044f, 0.950564f, 0.0f,
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
            0.69701f, 0.715498f, -0.047319f, 0.0f,
            -0.595281f, 0.614164f, 0.518116f, 0.0f,
            0.399773f, -0.332964f, 0.854001f, 0.0f,
            7.458938f, 0.437562f, 3.843472f, 1.0f
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
            0.40153f, 0.91163f, -0.08778f, 0.0f,
            -0.54111f, 0.313472f, 0.780343f, 0.0f,
            0.7389f, -0.265832f, 0.61916f, 0.0f,
            10.149627f, 2.084611f, 4.982515f, 1.0f
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

void StaticParent_depth_2_s(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);

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

void StaticParent_depth_2_rs(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);

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
            1.40444f, 0.469488f, -0.239018f, 0.0f,
            -0.144815f, 0.472351f, 0.076896f, 0.0f,
            0.397339f, -0.195687f, 1.950341f, 0.0f,
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
            0.601324f, 0.307067f, -0.28221f, 0.0f,
            -0.744193f, 0.561701f, 1.243613f, 0.0f,
            1.128978f, -0.240893f, 2.667158f, 0.0f,
            4.061404f, 3.21564f, 2.049201f, 1.0f
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
            0.223952f, 0.989401f, -1.006301f, 0.0f,
            -0.611537f, 0.561916f, 3.177684f, 0.0f,
            0.709093f, -0.143845f, 0.999866f, 0.0f,
            7.558842f, 3.775501f, 5.203307f, 1.0f
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
            1.56791f, 0.773682f, 0.074657f, 0.0f,
            -0.432793f, 0.847397f, 0.307585f, 0.0f,
            0.174708f, -0.514577f, 1.663487f, 0.0f,
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
            0.656966f, 0.533876f, -0.074865f, 0.0f,
            -1.474907f, 0.958f, 1.380891f, 0.0f,
            0.95568f, -0.690521f, 2.308361f, 0.0f,
            8.877736f, 0.880185f, 4.620386f, 1.0f
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
            0.00124f, 1.828415f, -0.385997f, 0.0f,
            -1.724475f, 0.810441f, 3.121818f, 0.0f,
            0.741381f, -0.371455f, 0.847394f, 0.0f,
            12.282152f, 1.446031f, 7.858332f, 1.0f
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

void StaticParent_depth_2_none_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);

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

void StaticParent_depth_2_r_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);

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
            0.938791f, 0.290096f, -0.185783f, 0.0f,
            -0.239713f, 0.937438f, 0.252482f, 0.0f,
            0.247404f, -0.192493f, 0.949599f, 0.0f,
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
            0.75904f, 0.571734f, -0.311413f, 0.0f,
            -0.312929f, 0.739845f, 0.595571f, 0.0f,
            0.570905f, -0.354612f, 0.740485f, 0.0f,
            5.18748f, 1.581548f, 1.867049f, 1.0f
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
            0.910718f, 0.412278f, 0.02487f, 0.0f,
            -0.385046f, 0.825692f, 0.412278f, 0.0f,
            0.149438f, -0.385046f, 0.910718f, 0.0f,
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
            0.714088f, 0.697332f, -0.061699f, 0.0f,
            -0.475022f, 0.547398f, 0.688992f, 0.0f,
            0.51423f, -0.462692f, 0.722138f, 0.0f,
            9.956313f, -1.340734f, 4.440689f, 1.0f
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

void StaticParent_depth_2_s_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);

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

void StaticParent_depth_2_rs_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);

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
            0.469396f, 0.145048f, -0.092891f, 0.0f,
            -0.479426f, 1.874877f, 0.504964f, 0.0f,
            0.371106f, -0.28874f, 1.424398f, 0.0f,
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
            0.453139f, 1.43013f, -0.516815f, 0.0f,
            -0.602034f, 2.39333f, 1.461155f, 0.0f,
            0.306382f, -0.374988f, 0.564994f, 0.0f,
            3.964846f, 1.002034f, 2.857923f, 1.0f
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
            0.683039f, 0.309209f, 0.018653f, 0.0f,
            -0.962614f, 2.064229f, 1.030696f, 0.0f,
            0.186798f, -0.481307f, 1.138398f, 0.0f,
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
            0.667429f, 1.925779f, 0.106308f, 0.0f,
            -1.424469f, 2.507739f, 2.005763f, 0.0f,
            0.322051f, -0.465336f, 0.376752f, 0.0f,
            9.329313f, -1.794334f, 4.763555f, 1.0f
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

void StaticParent_depth_2_none_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);

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

void StaticParent_depth_2_r_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Rotation, {0.3f, 0.3f, 0.2f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.936293f, 0.312992f, -0.159345f, 0.0f,
            -0.289629f, 0.944702f, 0.153792f, 0.0f,
            0.198669f, -0.097843f, 0.97517f, 0.0f,
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
            0.753578f, 0.57454f, -0.319412f, 0.0f,
            -0.375174f, 0.774897f, 0.508703f, 0.0f,
            0.539781f, -0.263513f, 0.799498f, 0.0f,
            4.106884f, 2.79221f, 1.48472f, 1.0f
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
            0.895949f, 0.442104f, 0.042661f, 0.0f,
            -0.432793f, 0.847397f, 0.307585f, 0.0f,
            0.099833f, -0.294044f, 0.950564f, 0.0f,
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
            0.697923f, 0.711398f, -0.082558f, 0.0f,
            -0.533245f, 0.593144f, 0.603183f, 0.0f,
            0.478072f, -0.376952f, 0.793319f, 0.0f,
            8.837596f, -0.114753f, 4.05383f, 1.0f
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

void StaticParent_depth_2_s_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);

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

void StaticParent_depth_2_rs_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);

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
            1.40444f, 0.469488f, -0.239018f, 0.0f,
            -0.144815f, 0.472351f, 0.076896f, 0.0f,
            0.397339f, -0.195687f, 1.950341f, 0.0f,
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
            2.376949f, 1.224628f, -1.25551f, 0.0f,
            -0.3934f, 0.415957f, 1.185443f, 0.0f,
            0.409281f, -0.086604f, 0.843835f, 0.0f,
            5.809328f, 3.114933f, 2.708458f, 1.0f
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
            1.56791f, 0.773682f, 0.074657f, 0.0f,
            -0.432793f, 0.847397f, 0.307585f, 0.0f,
            0.174708f, -0.514577f, 1.663487f, 0.0f,
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
            2.621509f, 2.13985f, -0.41601f, 0.0f,
            -0.952342f, 0.680954f, 1.265371f, 0.0f,
            0.372493f, -0.240119f, 0.726718f, 0.0f,
            10.965793f, 0.549182f, 5.219202f, 1.0f
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

void StaticParent_depth_2_none_skip_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);

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

void StaticParent_depth_2_r_skip_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e1, Position, {2.0f, 1.0f, 1.0f});
    ecs_set(world, e1, Rotation, {0.2f, 0.25f, 0.25f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.936293f, 0.312992f, -0.159345f, 0.0f,
            -0.289629f, 0.944702f, 0.153792f, 0.0f,
            0.198669f, -0.097843f, 0.97517f, 0.0f,
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
            0.758054f, 0.586066f, -0.286147f, 0.0f,
            -0.445791f, 0.785869f, 0.42858f, 0.0f,
            0.476051f, -0.197325f, 0.856994f, 0.0f,
            2.781627f, 3.472843f, 1.310272f, 1.0f
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
            0.895949f, 0.442104f, 0.042661f, 0.0f,
            -0.432793f, 0.847397f, 0.307585f, 0.0f,
            0.099833f, -0.294044f, 0.950564f, 0.0f,
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
            0.69701f, 0.715498f, -0.047319f, 0.0f,
            -0.595281f, 0.614164f, 0.518116f, 0.0f,
            0.399773f, -0.332964f, 0.854001f, 0.0f,
            7.458938f, 0.437562f, 3.843472f, 1.0f
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

void StaticParent_depth_2_s_skip_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);

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

void StaticParent_depth_2_rs_skip_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);

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
            1.40444f, 0.469488f, -0.239018f, 0.0f,
            -0.144815f, 0.472351f, 0.076896f, 0.0f,
            0.397339f, -0.195687f, 1.950341f, 0.0f,
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
            0.601324f, 0.307067f, -0.28221f, 0.0f,
            -0.744193f, 0.561701f, 1.243613f, 0.0f,
            1.128978f, -0.240893f, 2.667158f, 0.0f,
            4.061404f, 3.21564f, 2.049201f, 1.0f
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
            1.56791f, 0.773682f, 0.074657f, 0.0f,
            -0.432793f, 0.847397f, 0.307585f, 0.0f,
            0.174708f, -0.514577f, 1.663487f, 0.0f,
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
            0.656966f, 0.533876f, -0.074865f, 0.0f,
            -1.474907f, 0.958f, 1.380891f, 0.0f,
            0.95568f, -0.690521f, 2.308361f, 0.0f,
            8.877736f, 0.880185f, 4.620386f, 1.0f
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

void StaticParent_depth_2_none_skip_0_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);

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

void StaticParent_depth_2_r_skip_0_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);

    ecs_set(world, e2, Position, {3.0f, 0.0f, 1.5f});
    ecs_set(world, e2, Rotation, {0.3f, 0.3f, 0.2f});

    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e0, WorldTransform));
    test_assert(!ecs_has(world, e1, WorldTransform));
    {
        const WorldTransform *t = ecs_get(world, e2, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.936293f, 0.275387f, -0.217983f, 0.0f,
            -0.189796f, 0.918943f, 0.345718f, 0.0f,
            0.29552f, -0.282321f, 0.912668f, 0.0f,
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
            0.920648f, 0.390394f, 0.000616f, 0.0f,
            -0.336063f, 0.791717f, 0.510143f, 0.0f,
            0.198669f, -0.469869f, 0.860089f, 0.0f,
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

void StaticParent_depth_2_s_skip_0_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);

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

void StaticParent_depth_2_rs_skip_0_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);

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
            1.872587f, 0.550775f, -0.435966f, 0.0f,
            -0.284694f, 1.378415f, 0.518577f, 0.0f,
            0.14776f, -0.141161f, 0.456334f, 0.0f,
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
            2.071458f, 0.878386f, 0.001385f, 0.0f,
            -0.672125f, 1.583434f, 1.020286f, 0.0f,
            0.049667f, -0.117467f, 0.215022f, 0.0f,
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

void StaticParent_depth_3_none(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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

void StaticParent_depth_3_r(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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
            0.936293f, 0.312992f, -0.159345f, 0.0f,
            -0.289629f, 0.944702f, 0.153792f, 0.0f,
            0.198669f, -0.097843f, 0.97517f, 0.0f,
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
            0.758054f, 0.586066f, -0.286147f, 0.0f,
            -0.445791f, 0.785869f, 0.42858f, 0.0f,
            0.476051f, -0.197325f, 0.856994f, 0.0f,
            2.781627f, 3.472843f, 1.310272f, 1.0f
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
            0.483225f, 0.808162f, -0.336702f, 0.0f,
            -0.388953f, 0.542717f, 0.744429f, 0.0f,
            0.784353f, -0.228766f, 0.576591f, 0.0f,
            5.769865f, 4.935053f, 1.737321f, 1.0f
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
            0.144646f, 0.955123f, -0.258491f, 0.0f,
            -0.075269f, 0.271102f, 0.959603f, 0.0f,
            0.986617f, -0.119346f, 0.111105f, 0.0f,
            9.660424f, 7.167451f, 0.799266f, 1.0f
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
            0.895949f, 0.442104f, 0.042661f, 0.0f,
            -0.432793f, 0.847397f, 0.307585f, 0.0f,
            0.099833f, -0.294044f, 0.950564f, 0.0f,
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
            0.69701f, 0.715498f, -0.047319f, 0.0f,
            -0.595281f, 0.614164f, 0.518116f, 0.0f,
            0.399773f, -0.332964f, 0.854001f, 0.0f,
            7.458938f, 0.437562f, 3.843472f, 1.0f
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
            0.40153f, 0.91163f, -0.08778f, 0.0f,
            -0.54111f, 0.313472f, 0.780343f, 0.0f,
            0.7389f, -0.265832f, 0.61916f, 0.0f,
            10.149627f, 2.084611f, 4.982515f, 1.0f
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
            0.039281f, 0.998824f, -0.028417f, 0.0f,
            -0.218983f, 0.036352f, 0.975051f, 0.0f,
            0.974938f, -0.032078f, 0.220154f, 0.0f,
            13.774656f, 4.885994f, 5.089374f, 1.0f
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

void StaticParent_depth_3_s(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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

void StaticParent_depth_3_rs(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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
            1.40444f, 0.469488f, -0.239018f, 0.0f,
            -0.144815f, 0.472351f, 0.076896f, 0.0f,
            0.397339f, -0.195687f, 1.950341f, 0.0f,
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
            0.601324f, 0.307067f, -0.28221f, 0.0f,
            -0.744193f, 0.561701f, 1.243613f, 0.0f,
            1.128978f, -0.240893f, 2.667158f, 0.0f,
            4.061404f, 3.21564f, 2.049201f, 1.0f
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
            0.223952f, 0.989401f, -1.006301f, 0.0f,
            -0.611537f, 0.561916f, 3.177684f, 0.0f,
            0.709093f, -0.143845f, 0.999866f, 0.0f,
            7.558842f, 3.775501f, 5.203307f, 1.0f
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
            -0.171345f, 1.383829f, -0.414747f, 0.0f,
            -0.202255f, 0.224601f, 2.55301f, 0.0f,
            1.37103f, 0.013878f, -0.963571f, 0.0f,
            10.484375f, 6.883498f, 0.000149f, 1.0f
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
            1.56791f, 0.773682f, 0.074657f, 0.0f,
            -0.432793f, 0.847397f, 0.307585f, 0.0f,
            0.174708f, -0.514577f, 1.663487f, 0.0f,
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
            0.656966f, 0.533876f, -0.074865f, 0.0f,
            -1.474907f, 0.958f, 1.380891f, 0.0f,
            0.95568f, -0.690521f, 2.308361f, 0.0f,
            8.877736f, 0.880185f, 4.620386f, 1.0f
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
            0.00124f, 1.828415f, -0.385997f, 0.0f,
            -1.724475f, 0.810441f, 3.121818f, 0.0f,
            0.741381f, -0.371455f, 0.847394f, 0.0f,
            12.282152f, 1.446031f, 7.858332f, 1.0f
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
            -0.815337f, 2.514017f, 0.33504f, 0.0f,
            -0.911863f, 0.228513f, 2.400951f, 0.0f,
            1.909066f, 0.013655f, -0.811747f, 0.0f,
            15.494348f, 7.206342f, 4.887313f, 1.0f
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

void StaticParent_depth_3_none_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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

void StaticParent_depth_3_r_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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
            0.938791f, 0.290096f, -0.185783f, 0.0f,
            -0.239713f, 0.937438f, 0.252482f, 0.0f,
            0.247404f, -0.192493f, 0.949599f, 0.0f,
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
            0.75904f, 0.571734f, -0.311413f, 0.0f,
            -0.312929f, 0.739845f, 0.595571f, 0.0f,
            0.570905f, -0.354612f, 0.740485f, 0.0f,
            5.18748f, 1.581548f, 1.867049f, 1.0f
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
            0.475566f, 0.82066f, -0.316788f, 0.0f,
            -0.138529f, 0.42549f, 0.894297f, 0.0f,
            0.868704f, -0.381413f, 0.316034f, 0.0f,
            9.678381f, 2.419414f, 1.506795f, 1.0f
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
            0.910718f, 0.412278f, 0.02487f, 0.0f,
            -0.385046f, 0.825692f, 0.412278f, 0.0f,
            0.149438f, -0.385046f, 0.910718f, 0.0f,
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
            0.714088f, 0.697332f, -0.061699f, 0.0f,
            -0.475022f, 0.547398f, 0.688992f, 0.0f,
            0.51423f, -0.462692f, 0.722138f, 0.0f,
            9.956313f, -1.340734f, 4.440689f, 1.0f
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
            0.404502f, 0.912882f, -0.054993f, 0.0f,
            -0.301103f, 0.189717f, 0.934529f, 0.0f,
            0.863548f, -0.36146f, 0.351612f, 0.0f,
            14.316146f, -0.024189f, 4.949178f, 1.0f
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

void StaticParent_depth_3_s_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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

void StaticParent_depth_3_rs_skip_0(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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
            0.469396f, 0.145048f, -0.092891f, 0.0f,
            -0.479426f, 1.874877f, 0.504964f, 0.0f,
            0.371106f, -0.28874f, 1.424398f, 0.0f,
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
            0.453139f, 1.43013f, -0.516815f, 0.0f,
            -0.602034f, 2.39333f, 1.461155f, 0.0f,
            0.306382f, -0.374988f, 0.564994f, 0.0f,
            3.964846f, 1.002034f, 2.857923f, 1.0f
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
            0.225857f, 2.586296f, -0.286944f, 0.0f,
            -0.350587f, 1.326785f, 1.213731f, 0.0f,
            0.961047f, -1.06434f, -0.334311f, 0.0f,
            6.992201f, 3.57925f, 0.459498f, 1.0f
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
            0.683039f, 0.309209f, 0.018653f, 0.0f,
            -0.962614f, 2.064229f, 1.030696f, 0.0f,
            0.186798f, -0.481307f, 1.138398f, 0.0f,
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
            0.667429f, 1.925779f, 0.106308f, 0.0f,
            -1.424469f, 2.507739f, 2.005763f, 0.0f,
            0.322051f, -0.465336f, 0.376752f, 0.0f,
            9.329313f, -1.794334f, 4.763555f, 1.0f
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
            0.192442f, 3.229021f, 0.67989f, 0.0f,
            -0.917516f, 1.321746f, 1.45094f, 0.0f,
            1.542883f, -0.989439f, -0.55695f, 0.0f,
            14.067599f, 2.47037f, 3.936527f, 1.0f
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

void StaticParent_depth_3_none_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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

void StaticParent_depth_3_r_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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
            0.936293f, 0.312992f, -0.159345f, 0.0f,
            -0.289629f, 0.944702f, 0.153792f, 0.0f,
            0.198669f, -0.097843f, 0.97517f, 0.0f,
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
            0.753578f, 0.57454f, -0.319412f, 0.0f,
            -0.375174f, 0.774897f, 0.508703f, 0.0f,
            0.539781f, -0.263513f, 0.799498f, 0.0f,
            4.106884f, 2.79221f, 1.48472f, 1.0f
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
            0.461615f, 0.809571f, -0.362638f, 0.0f,
            -0.206661f, 0.495696f, 0.84355f, 0.0f,
            0.862672f, -0.314452f, 0.396128f, 0.0f,
            8.575935f, 3.788446f, 1.297366f, 1.0f
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
            0.895949f, 0.442104f, 0.042661f, 0.0f,
            -0.432793f, 0.847397f, 0.307585f, 0.0f,
            0.099833f, -0.294044f, 0.950564f, 0.0f,
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
            0.697923f, 0.711398f, -0.082558f, 0.0f,
            -0.533245f, 0.593144f, 0.603183f, 0.0f,
            0.478072f, -0.376952f, 0.793319f, 0.0f,
            8.837596f, -0.114753f, 4.05383f, 1.0f
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
            0.382973f, 0.916499f, -0.115594f, 0.0f,
            -0.366325f, 0.265552f, 0.891789f, 0.0f,
            0.84802f, -0.299187f, 0.437436f, 0.0f,
            13.118678f, 1.383792f, 4.707052f, 1.0f
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

void StaticParent_depth_3_s_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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

void StaticParent_depth_3_rs_skip_1(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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
            1.40444f, 0.469488f, -0.239018f, 0.0f,
            -0.144815f, 0.472351f, 0.076896f, 0.0f,
            0.397339f, -0.195687f, 1.950341f, 0.0f,
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
            2.376949f, 1.224628f, -1.25551f, 0.0f,
            -0.3934f, 0.415957f, 1.185443f, 0.0f,
            0.409281f, -0.086604f, 0.843835f, 0.0f,
            5.809328f, 3.114933f, 2.708458f, 1.0f
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
            2.497107f, 1.589553f, -1.326095f, 0.0f,
            -0.380392f, 0.120881f, 1.197703f, 0.0f,
            1.969617f, 0.289245f, -0.201083f, 0.0f,
            16.529089f, 7.424281f, -1.811357f, 1.0f
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
            1.56791f, 0.773682f, 0.074657f, 0.0f,
            -0.432793f, 0.847397f, 0.307585f, 0.0f,
            0.174708f, -0.514577f, 1.663487f, 0.0f,
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
            2.621509f, 2.13985f, -0.41601f, 0.0f,
            -0.952342f, 0.680954f, 1.265371f, 0.0f,
            0.372493f, -0.240119f, 0.726718f, 0.0f,
            10.965793f, 0.549182f, 5.219202f, 1.0f
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
            2.604319f, 2.79024f, -0.28727f, 0.0f,
            -0.791481f, 0.151794f, 1.124748f, 0.0f,
            2.354361f, 0.415343f, 0.034856f, 0.0f,
            23.14916f, 7.94739f, 3.743226f, 1.0f
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

void StaticParent_depth_3_none_skip_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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

void StaticParent_depth_3_r_skip_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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
            0.936293f, 0.312992f, -0.159345f, 0.0f,
            -0.289629f, 0.944702f, 0.153792f, 0.0f,
            0.198669f, -0.097843f, 0.97517f, 0.0f,
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
            0.758054f, 0.586066f, -0.286147f, 0.0f,
            -0.445791f, 0.785869f, 0.42858f, 0.0f,
            0.476051f, -0.197325f, 0.856994f, 0.0f,
            2.781627f, 3.472843f, 1.310272f, 1.0f
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
            0.462922f, 0.806418f, -0.367957f, 0.0f,
            -0.297739f, 0.53246f, 0.792362f, 0.0f,
            0.834898f, -0.257247f, 0.48659f, 0.0f,
            7.211736f, 4.636587f, 1.45109f, 1.0f
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
            0.895949f, 0.442104f, 0.042661f, 0.0f,
            -0.432793f, 0.847397f, 0.307585f, 0.0f,
            0.099833f, -0.294044f, 0.950564f, 0.0f,
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
            0.69701f, 0.715498f, -0.047319f, 0.0f,
            -0.595281f, 0.614164f, 0.518116f, 0.0f,
            0.399773f, -0.332964f, 0.854001f, 0.0f,
            7.458938f, 0.437562f, 3.843472f, 1.0f
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
            0.385291f, 0.914798f, -0.121222f, 0.0f,
            -0.455301f, 0.302714f, 0.837297f, 0.0f,
            0.802653f, -0.26741f, 0.533141f, 0.0f,
            11.641804f, 2.019464f, 4.844081f, 1.0f
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

void StaticParent_depth_3_s_skip_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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

void StaticParent_depth_3_rs_skip_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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
            1.40444f, 0.469488f, -0.239018f, 0.0f,
            -0.144815f, 0.472351f, 0.076896f, 0.0f,
            0.397339f, -0.195687f, 1.950341f, 0.0f,
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
            0.601324f, 0.307067f, -0.28221f, 0.0f,
            -0.744193f, 0.561701f, 1.243613f, 0.0f,
            1.128978f, -0.240893f, 2.667158f, 0.0f,
            4.061404f, 3.21564f, 2.049201f, 1.0f
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
            0.088718f, 0.622369f, -0.755564f, 0.0f,
            -0.19449f, 0.264835f, 1.72518f, 0.0f,
            2.182858f, -0.462912f, 2.633974f, 0.0f,
            9.468847f, 3.40042f, 5.011063f, 1.0f
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
            1.56791f, 0.773682f, 0.074657f, 0.0f,
            -0.432793f, 0.847397f, 0.307585f, 0.0f,
            0.174708f, -0.514577f, 1.663487f, 0.0f,
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
            0.656966f, 0.533876f, -0.074865f, 0.0f,
            -1.474907f, 0.958f, 1.380891f, 0.0f,
            0.95568f, -0.690521f, 2.308361f, 0.0f,
            8.877736f, 0.880185f, 4.620386f, 1.0f
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
            -0.037956f, 1.162098f, -0.354597f, 0.0f,
            -0.744699f, 0.359951f, 1.678747f, 0.0f,
            2.387519f, -1.147249f, 2.199638f, 0.0f,
            14.891865f, 0.676647f, 7.556756f, 1.0f
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

void StaticParent_depth_3_none_skip_3(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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

void StaticParent_depth_3_r_skip_3(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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
            0.936293f, 0.312992f, -0.159345f, 0.0f,
            -0.289629f, 0.944702f, 0.153792f, 0.0f,
            0.198669f, -0.097843f, 0.97517f, 0.0f,
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
            0.758054f, 0.586066f, -0.286147f, 0.0f,
            -0.445791f, 0.785869f, 0.42858f, 0.0f,
            0.476051f, -0.197325f, 0.856994f, 0.0f,
            2.781627f, 3.472843f, 1.310272f, 1.0f
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
            0.483225f, 0.808162f, -0.336702f, 0.0f,
            -0.388953f, 0.542717f, 0.744429f, 0.0f,
            0.784353f, -0.228766f, 0.576591f, 0.0f,
            5.769865f, 4.935053f, 1.737321f, 1.0f
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
            0.895949f, 0.442104f, 0.042661f, 0.0f,
            -0.432793f, 0.847397f, 0.307585f, 0.0f,
            0.099833f, -0.294044f, 0.950564f, 0.0f,
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
            0.69701f, 0.715498f, -0.047319f, 0.0f,
            -0.595281f, 0.614164f, 0.518116f, 0.0f,
            0.399773f, -0.332964f, 0.854001f, 0.0f,
            7.458938f, 0.437562f, 3.843472f, 1.0f
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
            0.40153f, 0.91163f, -0.08778f, 0.0f,
            -0.54111f, 0.313472f, 0.780343f, 0.0f,
            0.7389f, -0.265832f, 0.61916f, 0.0f,
            10.149627f, 2.084611f, 4.982515f, 1.0f
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

void StaticParent_depth_3_s_skip_3(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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

void StaticParent_depth_3_rs_skip_3(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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
            1.40444f, 0.469488f, -0.239018f, 0.0f,
            -0.144815f, 0.472351f, 0.076896f, 0.0f,
            0.397339f, -0.195687f, 1.950341f, 0.0f,
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
            0.601324f, 0.307067f, -0.28221f, 0.0f,
            -0.744193f, 0.561701f, 1.243613f, 0.0f,
            1.128978f, -0.240893f, 2.667158f, 0.0f,
            4.061404f, 3.21564f, 2.049201f, 1.0f
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
            0.223952f, 0.989401f, -1.006301f, 0.0f,
            -0.611537f, 0.561916f, 3.177684f, 0.0f,
            0.709093f, -0.143845f, 0.999866f, 0.0f,
            7.558842f, 3.775501f, 5.203307f, 1.0f
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
            1.56791f, 0.773682f, 0.074657f, 0.0f,
            -0.432793f, 0.847397f, 0.307585f, 0.0f,
            0.174708f, -0.514577f, 1.663487f, 0.0f,
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
            0.656966f, 0.533876f, -0.074865f, 0.0f,
            -1.474907f, 0.958f, 1.380891f, 0.0f,
            0.95568f, -0.690521f, 2.308361f, 0.0f,
            8.877736f, 0.880185f, 4.620386f, 1.0f
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
            0.00124f, 1.828415f, -0.385997f, 0.0f,
            -1.724475f, 0.810441f, 3.121818f, 0.0f,
            0.741381f, -0.371455f, 0.847394f, 0.0f,
            12.282152f, 1.446031f, 7.858332f, 1.0f
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

void StaticParent_depth_3_none_skip_1_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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

void StaticParent_depth_3_r_skip_1_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

    ecs_set(world, e0, Position, {1.0f, 2.0f, 0.5f});
    ecs_set(world, e0, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e3, Position, {4.0f, -1.0f, 2.0f});
    ecs_set(world, e3, Rotation, {0.4f, 0.35f, 0.15f});

    ecs_progress(world, 0);

    {
        const WorldTransform *t = ecs_get(world, e0, WorldTransform);
        test_assert(t != NULL);
        const float expect[16] = {
            0.936293f, 0.312992f, -0.159345f, 0.0f,
            -0.289629f, 0.944702f, 0.153792f, 0.0f,
            0.198669f, -0.097843f, 0.97517f, 0.0f,
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
            0.741067f, 0.570336f, -0.354311f, 0.0f,
            -0.303553f, 0.755278f, 0.580871f, 0.0f,
            0.598895f, -0.322912f, 0.732839f, 0.0f,
            5.432142f, 2.111578f, 1.659168f, 1.0f
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
            0.895949f, 0.442104f, 0.042661f, 0.0f,
            -0.432793f, 0.847397f, 0.307585f, 0.0f,
            0.099833f, -0.294044f, 0.950564f, 0.0f,
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
            0.6901f, 0.713871f, -0.118956f, 0.0f,
            -0.468135f, 0.565671f, 0.678871f, 0.0f,
            0.551916f, -0.412801f, 0.724558f, 0.0f,
            10.216254f, -0.667068f, 4.264188f, 1.0f
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

void StaticParent_depth_3_s_skip_1_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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

void StaticParent_depth_3_rs_skip_1_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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
            1.40444f, 0.469488f, -0.239018f, 0.0f,
            -0.144815f, 0.472351f, 0.076896f, 0.0f,
            0.397339f, -0.195687f, 1.950341f, 0.0f,
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
            1.455583f, 0.766468f, -0.871037f, 0.0f,
            -0.115801f, 0.202698f, 0.708802f, 0.0f,
            1.317509f, -0.271674f, 2.366078f, 0.0f,
            7.557252f, 3.014226f, 3.367715f, 1.0f
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
            1.56791f, 0.773682f, 0.074657f, 0.0f,
            -0.432793f, 0.847397f, 0.307585f, 0.0f,
            0.174708f, -0.514577f, 1.663487f, 0.0f,
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
            1.619012f, 1.347355f, -0.337979f, 0.0f,
            -0.397575f, 0.317851f, 0.736901f, 0.0f,
            1.27067f, -0.73487f, 2.028546f, 0.0f,
            13.05385f, 0.218178f, 5.818018f, 1.0f
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

void StaticParent_depth_3_none_skip_0_1_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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

void StaticParent_depth_3_r_skip_0_1_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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
            0.928825f, 0.269673f, -0.254089f, 0.0f,
            -0.140378f, 0.890764f, 0.432243f, 0.0f,
            0.342898f, -0.365809f, 0.86522f, 0.0f,
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
            0.925637f, 0.377359f, -0.028208f, 0.0f,
            -0.286333f, 0.747191f, 0.599766f, 0.0f,
            0.247404f, -0.547089f, 0.799678f, 0.0f,
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

void StaticParent_depth_3_s_skip_0_1_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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

void StaticParent_depth_3_rs_skip_0_1_2(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e0 = ecs_new(world);
    ecs_entity_t e1 = ecs_new_w_parent(world, e0, NULL);
    ecs_entity_t e2 = ecs_new_w_parent(world, e1, NULL);
    ecs_entity_t e3 = ecs_new_w_parent(world, e2, NULL);

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
            1.161031f, 0.337091f, -0.317612f, 0.0f,
            -0.105284f, 0.668073f, 0.324182f, 0.0f,
            0.514347f, -0.548713f, 1.297829f, 0.0f,
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
            1.388456f, 0.566038f, -0.042312f, 0.0f,
            -0.357916f, 0.933988f, 0.749708f, 0.0f,
            0.309255f, -0.683861f, 0.999597f, 0.0f,
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
