/**
 * @file oreelse.c
 * @brief The oreelse module: registers the components and the systems of every area.
 *
 * The systems of EcsOnUpdate run in the order the areas register them: the flow, the map, the
 * player, mining, then crafting (the workers, the order book, OreCanOrder and OreNeeds). Those
 * that only run while playing match (OreGameState, Playing), the only pause mechanism.
 *
 * The look applies the render quality tier in every state, in EcsPreStore.
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
    ECS_META_COMPONENT(world, OreMining);
    ECS_META_COMPONENT(world, OreMineMarker);
    ECS_META_COMPONENT(world, OreStack);
    ECS_META_COMPONENT(world, OreItem);
    ECS_META_COMPONENT(world, OreMined);
    ECS_META_COMPONENT(world, OreRecipe);
    ECS_META_COMPONENT(world, OreInventory);
    ECS_META_COMPONENT(world, OreCrafter);
    ECS_META_COMPONENT(world, OreCraftBag);
    ECS_META_COMPONENT(world, OreCraftEntry);
    ECS_META_COMPONENT(world, OreCraftOrder);
    ECS_META_COMPONENT(world, OreCraftRow);
    ECS_META_COMPONENT(world, OreCraftBook);
    ECS_META_COMPONENT(world, OreCanOrder);
    ECS_META_COMPONENT(world, OreNeed);
    ECS_META_COMPONENT(world, OreNeeds);
    ECS_META_COMPONENT(world, OreHover);
    ECS_META_COMPONENT(world, OreDeposit);
    ECS_META_COMPONENT(world, OreMap);
    ECS_META_COMPONENT(world, OreRenderQuality);
    ECS_META_COMPONENT(world, OreQuality);
    ECS_META_COMPONENT(world, OreQualityTiers);
    ECS_META_COMPONENT(world, OreLook);
    ECS_TAG_DEFINE(world, OreRunScript);

    ecs_add_id(world, ecs_id(OreGameState), EcsExclusive);
    ecs_add_id(world, ecs_id(OreGameState), EcsPairIsTag);

    ore_flow_import(world);
    ore_map_import(world);
    ore_player_import(world);
    ore_inventory_import(world);
    ore_mining_import(world);
    ore_craft_import(world);
    ore_look_import(world);
}
