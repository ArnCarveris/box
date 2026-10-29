/**
 * @file stresstest.c
 * @brief The stress test mode (--stresstest[=N]): every run starts as a late game base, built by
 * placement's own rules from the layout of OreStressKit, with the clock and the waves advanced.
 *
 * OreStressTestApply builds it once per run, after the scripts of the run loaded: on the first
 * frame and after every restart, in OnLoad after the purge of the tile grid.
 */

#include "game.h"
#include <math.h>
#include <stdlib.h>

/** A layout in progress: the watts of the buildings it placed */
typedef struct ore_stress_t {
    ecs_world_t *world; /**< The world, not a stage */
    ore_grid_t grid;    /**< The grid of cfg */
    float demand;       /**< Watts of the consumers placed */
    float production;   /**< Watts of the producers placed */
} ore_stress_t;

/** Place a building for free and count its watts.
 *
 * @param s The layout
 * @param prefab The building, 0 places nothing
 * @param row Row of the top left tile
 * @param col Column of the top left tile
 * @return Whether it was placed
 */
static bool ore_stress_place(
    ore_stress_t *s,
    ecs_entity_t prefab,
    int32_t row,
    int32_t col)
{
    bool result = prefab && ore_placement_build(s->world, prefab, row, col) != 0;
    const OrePowerConsumer *consumer;
    const OrePowerProducer *producer;
    if (result) {
        consumer = ecs_get(s->world, prefab, OrePowerConsumer);
        producer = ecs_get(s->world, prefab, OrePowerProducer);
        s->demand += consumer ? consumer->watts : 0;
        s->production += producer ? producer->watts : 0;
    }

    return result;
}

/** Place a building at the first free cell of the square shells around a tile, the shell at
 * distance d before the one at d + 1, each in row major order.
 *
 * @param s The layout
 * @param site The building and the tile
 * @param radius Distance of the last shell
 * @return Whether it was placed
 */
static bool ore_stress_place_near(
    ore_stress_t *s,
    const OreStressSite *site,
    int32_t radius)
{
    bool result = false;
    int32_t d, r, c;
    for (d = 0; d <= radius && !result; d ++) {
        for (r = site->row - d; r <= site->row + d && !result; r ++) {
            for (c = site->col - d; c <= site->col + d && !result; c ++) {
                if (abs(r - site->row) == d || abs(c - site->col) == d) {
                    result = ore_stress_place(s, site->building, r, c);
                }
            }
        }
    }

    return result;
}

/** Place a drill on every deposit of the map.
 *
 * @param s The layout
 * @param drill The drill prefab
 */
static void ore_stress_drills(
    ore_stress_t *s,
    ecs_entity_t drill)
{
    OreMap *map = ecs_singleton_get_mut(s->world, OreMap);
    ecs_entity_t *deposit;
    int32_t r, c;
    for (r = 0; map && r < s->grid.rows; r ++) {
        for (c = 0; c < s->grid.cols; c ++) {
            deposit = ore_map_deposit(map, r, c);
            if (deposit && *deposit) {
                ore_stress_place(s, drill, r, c);
            }
        }
    }
}

/** Place a ring of buildings on the perimeter of the square of rows and columns lo to hi, the
 * original's oreStressRing: step i places on the top row from the left, the bottom row from the
 * right, the left column from the bottom and the right column from the top, cycling prefabs.
 *
 * @param s The layout
 * @param lo First row and column of the square
 * @param hi Last row and column of the square
 * @param prefabs The prefabs the ring cycles
 * @param count Number of prefabs
 */
static void ore_stress_ring(
    ore_stress_t *s,
    int32_t lo,
    int32_t hi,
    const ecs_entity_t *prefabs,
    int32_t count)
{
    int32_t i, n;
    for (i = lo, n = 0; count > 0 && i <= hi; i ++, n ++) {
        ore_stress_place(s, prefabs[n % count], lo, i);
        ore_stress_place(s, prefabs[(n + 1) % count], hi, hi - (i - lo));
        ore_stress_place(s, prefabs[(n + 2) % count], hi - (i - lo), lo);
        ore_stress_place(s, prefabs[(n + 3) % count], i, hi);
    }
}

/** Place solar panels on the free tiles of successive rings, cfg.stressSolarInset in from the
 * edge of the map and further in, four at a time, until the production is at least
 * cfg.stressPowerMargin times the watts of the consumers.
 *
 * @param s The layout
 * @param solar The solar panel prefab
 */
static void ore_stress_solar(
    ore_stress_t *s,
    ecs_entity_t solar)
{
    float margin = ORE_CFG(s->world, "stressPowerMargin");
    int32_t inset = ORE_CFG_I32(s->world, "stressSolarInset");
    int32_t lo, hi, i;
    bool done = false;
    for (; !done && inset <= s->grid.rows - 1 - inset; inset ++) {
        lo = inset;
        hi = s->grid.rows - 1 - inset;
        for (i = lo; !done && i <= hi; i ++) {
            done = s->production >= s->demand * margin;
            if (!done) {
                ore_stress_place(s, solar, lo, i);
                ore_stress_place(s, solar, hi, i);
                ore_stress_place(s, solar, i, lo);
                ore_stress_place(s, solar, i, hi);
            }
        }
    }
}

/** Build the base: a drill on every deposit, the two wall rings, the turret ring, the
 * buildings near their tiles and the solar farm sized to the demand.
 *
 * @param s The layout
 * @param kit The layout's buildings
 */
static void ore_stress_base(
    ore_stress_t *s,
    const OreStressKit *kit)
{
    int32_t edge = ORE_CFG_I32(s->world, "stressEdge");
    int32_t far = ORE_CFG_I32(s->world, "stressFar");
    int32_t radius = ORE_CFG_I32(s->world, "stressSearch");
    ecs_entity_t turrets[4];
    int32_t i, count = 0;
    for (i = 0; i < 4; i ++) {
        if (kit->turrets[i]) {
            turrets[count ++] = kit->turrets[i];
        }
    }

    ore_stress_drills(s, kit->drill);
    ore_stress_ring(s, edge, far, &kit->wall, kit->wall ? 1 : 0);
    ore_stress_ring(s, edge + 1, far - 1, &kit->wall, kit->wall ? 1 : 0);
    ore_stress_ring(s, edge + 2, far - 2, turrets, count);
    for (i = 0; i < 8; i ++) {
        if (kit->sites[i].building) {
            ore_stress_place_near(s, &kit->sites[i], radius);
        }
    }

    ore_stress_solar(s, kit->solar);
}

/** Move the astronaut to the nearest tile without a deposit or a building from
 * cfg.stressCenter, the first of the square shells around it in row major order.
 *
 * @param s The layout
 */
static void ore_stress_player(
    ore_stress_t *s)
{
    OreMap *map = ecs_singleton_get_mut(s->world, OreMap);
    int32_t center = ORE_CFG_I32(s->world, "stressCenter");
    ecs_iter_t it = ecs_each_id(s->world, ecs_id(OrePlayer));
    ecs_entity_t player = 0, *building, *deposit;
    int32_t d, r, c;
    bool found = false;
    while (ecs_each_next(&it)) {
        player = !player && it.count ? it.entities[0] : player;
    }

    for (d = 0; map && player && !found && d < s->grid.rows; d ++) {
        for (r = center - d; r <= center + d && !found; r ++) {
            for (c = center - d; c <= center + d && !found; c ++) {
                building = ore_map_building(map, r, c);
                deposit = ore_map_deposit(map, r, c);
                found = (abs(r - center) == d || abs(c - center) == d) &&
                    building && !*building && deposit && !*deposit;
                if (found) {
                    ecs_set(s->world, player, Position, {
                        ore_tile_x(&s->grid, c), 0, ore_tile_z(&s->grid, r)
                    });
                }
            }
        }
    }
}

/** Advance the run to a play time: the clock at it, the wave counter at the waves that would
 * have started by then, floor((seconds - cfg.firstWaveAt) / cfg.waveGap), and the next wave
 * cfg.stressWaveLead seconds away.
 *
 * @param world The world
 * @param seconds The play time
 */
static void ore_stress_clock(
    ecs_world_t *world,
    float seconds)
{
    float first = ORE_CFG(world, "firstWaveAt");
    float gap = ORE_CFG(world, "waveGap");
    float lead = ORE_CFG(world, "stressWaveLead");
    OreWaveState *w = ecs_singleton_get_mut(world, OreWaveState);
    OreClock *clock = ecs_singleton_get_mut(world, OreClock);
    int32_t wave = gap > 0 ? (int32_t)floorf((seconds - first) / gap) : 0;
    if (w) {
        w->wave = wave > 0 ? wave : 0;
        w->timer = lead;
        w->gap = lead;
        w->frac = 0;
        w->left = 0;
        ecs_os_memset_n(w->spawn, 0, int32_t, 4);
        ecs_singleton_modified(world, OreWaveState);
    }

    if (clock) {
        clock->elapsed = seconds;
        ecs_singleton_modified(world, OreClock);
    }
}

/** Build the late game base of the stress test into the current run and play it.
 *
 * @param world The world, not a stage
 * @param kit The layout
 * @param minutes Minutes of play the run starts at, 0 or less for cfg.stressMinutes
 */
static void ore_stress_build(
    ecs_world_t *world,
    const OreStressKit *kit,
    float minutes)
{
    ore_stress_t s = { .world = world, .grid = ore_grid_get(world) };
    int32_t i;
    minutes = minutes > 0 ? minutes : ORE_CFG(world, "stressMinutes");
    ore_stress_base(&s, kit);
    ore_stress_player(&s);
    for (i = 0; i < 16; i ++) {
        if (kit->stock[i].item && kit->stock[i].amount > 0) {
            ore_inventory_add(world, kit->stock[i].item, kit->stock[i].amount);
        }
    }

    ore_stress_clock(world, minutes * 60);
    ore_state_set(world, OreGameStatePlaying);
    ecs_trace("stresstest: %.0f minutes, power %.0f / %.0f W", (double)minutes,
        (double)s.production, (double)s.demand);
}

/** Build the layout once per run: when the run of OreSeed is not the one it was built for.
 * Runs immediate on the world, so every building is in the tile grid before the next one
 * checks its tiles.
 *
 * @param it Iterator with the OreStressTest, OreStressKit and OreSeed singletons
 */
static void OreStressTestApply(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    OreStressTest *st = ecs_field(it, OreStressTest, 0);
    const OreStressKit *kit = ecs_field(it, OreStressKit, 1);
    const OreSeed *seed = ecs_field(it, OreSeed, 2);
    const OrePlacement *pl = ecs_singleton_get(world, OrePlacement);
    if (st->run != seed->run && pl && ecs_is_alive(world, pl->root)) {
        st->run = seed->run;
        ore_stress_build(world, kit, st->minutes);
    }
}

void ore_stresstest_import(
    ecs_world_t *world)
{
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreStressTestApply" }),
        .phase = EcsOnLoad,
        .query.terms = {
            { .id = ecs_id(OreStressTest), .src.id = ecs_id(OreStressTest), .inout = EcsInOut },
            { .id = ecs_id(OreStressKit), .src.id = ecs_id(OreStressKit), .inout = EcsIn },
            { .id = ecs_id(OreSeed), .src.id = ecs_id(OreSeed), .inout = EcsIn }
        },
        .callback = OreStressTestApply,
        .immediate = true
    });
}
