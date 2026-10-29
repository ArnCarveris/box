/**
 * @file oreelse.c
 * @brief The oreelse module: registers the components and the systems of every area.
 *
 * The systems of EcsOnUpdate run in the order the areas register them: the flow, the map, the
<<<<<<< HEAD
 * player, mining, power, crafting, the critters, their animation and corpses, the weapons, the
 * buildings, then placement and its preview. Gameplay systems match (OreGameState, Playing).
=======
 * player, mining, power, crafting, the critters, their animation and corpses, the buildings,
 * their animation and lights, placement, its preview, then the rocket.
>>>>>>> 7616982bb50f3620e900cd61c37a36609ce3d9f8
 *
 * Gameplay systems match (OreGameState, Playing), the only pause. The look applies the render
 * quality tier in every state, in EcsPreStore. The particles register the helpers that play
 * the effects of etc/particles and the flash of their lights.
 */

#define OREELSE_IMPL
#include "game.h"

ECS_TAG_DECLARE(OreRunScript);
ECS_TAG_DECLARE(OreGhost);
ECS_TAG_DECLARE(OreHoming);

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
    ECS_META_COMPONENT(world, OrePlayerGait);
    ECS_META_COMPONENT(world, OreEffects);
    ECS_META_COMPONENT(world, OreMining);
    ECS_META_COMPONENT(world, OreMineMarker);
    ECS_META_COMPONENT(world, OreStack);
    ECS_META_COMPONENT(world, OreItem);
    ECS_META_COMPONENT(world, OreIcon);
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
    ECS_META_COMPONENT(world, OreFootprint);
    ECS_META_COMPONENT(world, OreBuildLimit);
    ECS_META_COMPONENT(world, OreHealth);
    ECS_META_COMPONENT(world, OrePowerConsumer);
    ECS_META_COMPONENT(world, OrePowerProducer);
    ECS_META_COMPONENT(world, OrePowerKind);
    ECS_META_COMPONENT(world, OrePowerCategory);
    ECS_META_COMPONENT(world, OrePowerAlloc);
    ECS_META_COMPONENT(world, OrePowerState);
    ECS_META_COMPONENT(world, OreDrill);
    ECS_META_COMPONENT(world, OreWorker);
    ECS_META_COMPONENT(world, OreStatusLamp);
    ECS_META_COMPONENT(world, OreLamps);
    ECS_META_COMPONENT(world, OreSpinner);
    ECS_META_COMPONENT(world, OrePiston);
    ECS_META_COMPONENT(world, OreShake);
    ECS_META_COMPONENT(world, OreVent);
    ECS_META_COMPONENT(world, OreWorkEmitter);
    ECS_META_COMPONENT(world, OreWorkLight);
    ECS_META_COMPONENT(world, OreBuilding);
    ECS_META_COMPONENT(world, OreConnectRule);
    ECS_META_COMPONENT(world, OreConnector);
    ECS_META_COMPONENT(world, OrePlacement);
    ECS_META_COMPONENT(world, OrePreview);
    ECS_META_COMPONENT(world, OreSelectKey);
    ECS_META_COMPONENT(world, OreHover);
    ECS_META_COMPONENT(world, OreDeposit);
    ECS_META_COMPONENT(world, OreMap);
    ECS_META_COMPONENT(world, OreRenderQuality);
    ECS_META_COMPONENT(world, OreQuality);
    ECS_META_COMPONENT(world, OreQualityTiers);
    ECS_META_COMPONENT(world, OreLook);
    ECS_META_COMPONENT(world, OreBlastFlash);
    ECS_META_COMPONENT(world, OreLaunchPhase);
    ECS_META_COMPONENT(world, OreLaunchPad);
    ECS_META_COMPONENT(world, OreRocketKit);
    ECS_META_COMPONENT(world, OreRocketParts);
    ECS_META_COMPONENT(world, OreRocketState);
    ECS_META_COMPONENT(world, OreItemKey);
    ECS_META_COMPONENT(world, OreSpeed);
    ECS_META_COMPONENT(world, OreDamage);
    ECS_META_COMPONENT(world, OreRange);
    ECS_META_COMPONENT(world, OreRadius);
    ECS_META_COMPONENT(world, OreWaveTier);
    ECS_META_COMPONENT(world, OreGait);
    ECS_META_COMPONENT(world, OreDeathBurst);
    ECS_META_COMPONENT(world, OreArsenal);
    ECS_META_COMPONENT(world, OreWaveMix);
    ECS_META_COMPONENT(world, OreWaveStyle);
    ECS_META_COMPONENT(world, OreWaveState);
    ECS_META_COMPONENT(world, OreCritter);
    ECS_META_COMPONENT(world, OreTarget);
    ECS_META_COMPONENT(world, OreVelocity);
    ECS_META_COMPONENT(world, OreCorpse);
    ECS_META_COMPONENT(world, OreHurt);
    ECS_META_COMPONENT(world, OreCritterIndex);
    ECS_META_COMPONENT(world, OreArmory);
    ECS_META_COMPONENT(world, OreAttackInterval);
    ECS_META_COMPONENT(world, OreAttackTimer);
    ECS_META_COMPONENT(world, OreMuzzle);
    ECS_META_COMPONENT(world, OreAmmo);
    ECS_META_COMPONENT(world, OreBeam);
    ECS_META_COMPONENT(world, OreSplash);
    ECS_META_COMPONENT(world, OreTurret);
    ECS_META_COMPONENT(world, OreProjectile);
    ECS_TAG_DEFINE(world, OreRunScript);
    ECS_TAG_DEFINE(world, OreGhost);
    ECS_TAG_DEFINE(world, OreHoming);

    ecs_add_id(world, ecs_id(OreGameState), EcsExclusive);
    ecs_add_id(world, ecs_id(OreGameState), EcsPairIsTag);

    ore_flow_import(world);
    ore_map_import(world);
    ore_player_import(world);
    ore_inventory_import(world);
    ore_mining_import(world);
    ore_power_import(world);
    ore_craft_import(world);
    ore_critters_import(world);
    ore_critter_fx_import(world);
    ore_weapons_import(world);
    ore_buildings_import(world);
    ore_building_fx_import(world);
    ore_placement_import(world);
    ore_rocket_import(world);
    ore_look_import(world);
    ore_particles_import(world);
}
