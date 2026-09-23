/**
 * @file box/box.c
 * @brief The box module: imports every module of the framework in one order.
 *
 * Applications and tests import this module and nothing else, so every world
 * registers the modules in the same order and gets the same component ids.
 */

#define BOX_IMPL
#include <box.h>
#include "../ui/ui_internal.h"
#include "../command/command_internal.h"

/** Generate the compare and equals hooks of every component under a scope.
 *
 * Recurses into the children of every entity under the scope.
 *
 * @param world The world
 * @param parent The scope to walk
 */
static void box_generate_hooks(
    ecs_world_t *world,
    ecs_entity_t parent)
{
    ecs_iter_t it = ecs_children(world, parent);
    while (ecs_children_next(&it)) {
        if (ecs_table_has_id(world, it.table, ecs_id(EcsComponent))) {
            for (int i = 0; i < it.count; i ++) {
                ecs_set_rtt_compare(world, it.entities[i]);
                ecs_set_rtt_equals(world, it.entities[i]);
            }
        }

        for (int i = 0; i < it.count; i ++) {
            box_generate_hooks(world, it.entities[i]);
        }
    }
}

void BoxImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, Box);
    ECS_IMPORT(world, FlecsScriptMath);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxCore);
    ECS_IMPORT(world, BoxJob);
    ECS_IMPORT(world, BoxShader);
    ECS_IMPORT(world, BoxSurface);
    ECS_IMPORT(world, BoxInput);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxCamera);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxLight);
    ECS_IMPORT(world, BoxMaterial);
    ECS_IMPORT(world, BoxTransition);
    ECS_IMPORT(world, BoxLod);
    ECS_IMPORT(world, BoxEffects);
    ECS_IMPORT(world, BoxSky);
    ECS_IMPORT(world, BoxRender);
    ECS_IMPORT(world, BoxGeometry);
    ECS_IMPORT(world, BoxTexture);
    ECS_IMPORT(world, BoxGltf);
    ECS_IMPORT(world, BoxMovement);
    ECS_IMPORT(world, BoxCollision);
    ECS_IMPORT(world, BoxPicking);
    ECS_IMPORT(world, BoxSpline);
    ECS_IMPORT(world, BoxDraw);
    ECS_IMPORT(world, BoxParticle);
    ECS_IMPORT(world, BoxController);
    ECS_IMPORT(world, BoxUi);
    ECS_IMPORT(world, BoxMonitor);
    ECS_IMPORT(world, BoxScript);
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxCommand);
    ECS_IMPORT(world, BoxEditor);

    box_generate_hooks(world, ecs_id(Box));

    box_ui_widgets_load(world);
}
