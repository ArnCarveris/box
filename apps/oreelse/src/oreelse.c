/**
 * @file oreelse.c
 * @brief The oreelse module: registers the components and the systems of every area.
 *
 * The systems of EcsOnUpdate run in the order the areas register them: the flow (clock, blink,
 * toast), then the player (walking, the camera). Those that only run while playing match
 * (OreGameState, Playing), the game's only pause mechanism.
 */

#define OREELSE_IMPL
#include "game.h"

ECS_TAG_DECLARE(OreRunScript);

void OreelseImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, Oreelse);
    ECS_IMPORT(world, Box);

    ECS_META_COMPONENT(world, OreGameState);
    ECS_META_COMPONENT(world, OreSeed);
    ECS_META_COMPONENT(world, OreRandom);
    ECS_META_COMPONENT(world, OreClock);
    ECS_META_COMPONENT(world, OreBlink);
    ECS_META_COMPONENT(world, OreToast);
    ECS_META_COMPONENT(world, OreZoom);
    ECS_META_COMPONENT(world, OreView);
    ECS_META_COMPONENT(world, OreActions);
    ECS_META_COMPONENT(world, OrePlayer);
    ECS_META_COMPONENT(world, OreLimb);
    ECS_META_COMPONENT(world, OreDeposit);
    ECS_META_COMPONENT(world, OreMap);
    ECS_TAG_DEFINE(world, OreRunScript);

    ecs_add_id(world, ecs_id(OreGameState), EcsExclusive);
    ecs_add_id(world, ecs_id(OreGameState), EcsPairIsTag);

    ore_flow_import(world);
    ore_map_import(world);
    ore_player_import(world);
}
