/**
 * @file Actions.c
 * @brief Benchmarks for the OnLoad system that resolves the bindings of the maps.
 */

#include <box.h>
#include <bake_bench.h>

/** Bindings the map holds while the action system resolves it */
#define BENCH_ACTION_BINDINGS (100)

/** Maps the layered benchmark spreads its bindings over */
#define BENCH_ACTION_LAYERS (4)

void Actions_resolve_100_bindings(bench_t *b)
{
    // input/action_layer
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxInput);
    ecs_entity_t map = ecs_lookup(world, "box.input.map");
    int32_t i;
    for (i = 0; i < BENCH_ACTION_BINDINGS; i ++) {
        ecs_entity_t action = ecs_new_w_pair(world, EcsChildOf, map);
        ecs_add(world, action, InputAction);
        ecs_entity_t bind = ecs_new_w_pair(world, EcsChildOf, map);
        uint64_t mask = (uint64_t)(BoxKeyA + (i % 26));
        if (i % 3 == 1) {
            mask |= BOX_KEY_MOD_CTRL;
        } else if (i % 3 == 2) {
            mask |= BOX_KEY_MOD_SHIFT_LEFT;
        }

        ecs_set(world, bind, Binding, { .mask = mask, .action = action });
    }

    box_input_inject(world, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyW });
    box_input_inject(world, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyCtrlLeft });
    ecs_progress(world, 0.016f);

    bench_set_items(b, BENCH_ACTION_BINDINGS);

    while (bench_iter(b)) {
        ecs_progress(world, 0.016f);
        bench_keep(ecs_singleton_get(world, Input)->keys[BoxKeyW].state);
    }

    ecs_fini(world);
}

void Actions_resolve_100_bindings_in_4_layers(bench_t *b)
{
    // ore_engine/input_layers
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxInput);
    ecs_entity_t maps[BENCH_ACTION_LAYERS];
    int32_t i;
    for (i = 0; i < BENCH_ACTION_LAYERS; i ++) {
        maps[i] = ecs_new(world);
        ecs_set(world, maps[i], InputMap, { .enabled = true, .priority = i });
    }

    for (i = 0; i < BENCH_ACTION_BINDINGS; i ++) {
        ecs_entity_t map = maps[i % BENCH_ACTION_LAYERS];
        ecs_entity_t action = ecs_new_w_pair(world, EcsChildOf, map);
        ecs_add(world, action, InputAction);
        ecs_entity_t bind = ecs_new_w_pair(world, EcsChildOf, map);
        uint64_t mask = (uint64_t)(BoxKeyA + (i % 26));
        if (i % 3 == 1) {
            mask |= BOX_KEY_MOD_CTRL;
        } else if (i % 3 == 2) {
            mask |= BOX_KEY_MOD_SHIFT_LEFT;
        }

        ecs_set(world, bind, Binding, { .mask = mask, .action = action });
    }

    box_input_inject(world, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyW });
    box_input_inject(world, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyCtrlLeft });
    ecs_progress(world, 0.016f);

    bench_set_items(b, BENCH_ACTION_BINDINGS);

    while (bench_iter(b)) {
        ecs_progress(world, 0.016f);
        bench_keep(ecs_singleton_get(world, Input)->keys[BoxKeyW].state);
    }

    ecs_fini(world);
}
