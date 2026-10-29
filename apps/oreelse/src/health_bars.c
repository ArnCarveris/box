/**
 * @file health_bars.c
 * @brief The health bars over the damaged buildings and the damaged astronaut: a HealthBar
 * instance per damaged building or astronaut, its fraction in step with the health, and the view
 * of the bars shown while playing.
 *
 * box.projection anchors a bar to the model box (OreModelBox) of its building or astronaut and
 * sizes it every frame, so the astronaut's bar follows it as it moves; the game only creates,
 * updates and deletes bars when the health changes, and (OreBarOf, owner) deletes a bar with its
 * building or astronaut.
 */

#include "game.h"

/** Set the props of the HealthBar template on a bar.
 *
 * @param world The world
 * @param bars The health bars singleton
 * @param bar The bar
 * @param building The building or astronaut of the bar
 * @param box Its model box, NULL for none
 * @param frac Fraction of the health left
 */
static void ore_health_bar_set(
    ecs_world_t *world,
    const OreHealthBars *bars,
    ecs_entity_t bar,
    ecs_entity_t building,
    const OreModelBox *box,
    float frac)
{
    const ecs_world_t *real = ecs_get_world(world);
    const ecs_type_info_t *ti = ecs_get_type_info(real, bars->bar);
    ecs_value_t value;
    ecs_meta_cursor_t cur;
    if (!ti) {
        goto done;
    }

    value = ecs_value_new(real, bars->bar);
    cur = ecs_meta_cursor(real, bars->bar, value.ptr);
    ecs_meta_push(&cur);
    ecs_meta_member(&cur, "building");
    ecs_meta_set_entity(&cur, building);
    ecs_meta_member(&cur, "view");
    ecs_meta_set_entity(&cur, bars->world);
    ecs_meta_member(&cur, "w");
    ecs_meta_set_float(&cur, box ? box->w : 0);
    ecs_meta_member(&cur, "d");
    ecs_meta_set_float(&cur, box ? box->d : 0);
    ecs_meta_member(&cur, "h");
    ecs_meta_set_float(&cur, box ? box->h : 0);
    ecs_meta_member(&cur, "frac");
    ecs_meta_set_float(&cur, frac);
    ecs_meta_pop(&cur);
    ecs_set_id(world, bar, bars->bar, (size_t)ti->size, value.ptr);
    ecs_value_fini(real, &value);
done:
    return;
}

/** Keep the bar of a building or the astronaut in step with its health: create it when it is
 * damaged, set its fraction while it is, delete it at full health or at 0 (the death of the
 * astronaut), and while the astronaut is aboard the rocket, hidden.
 *
 * The link on the building is written in place, so a second change of the same health in one
 * merge finds the bar the first one created.
 *
 * @param it Iterator over the buildings or astronauts whose health or registration was set
 */
static void OreHealthBarSync(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    const ecs_world_t *real = ecs_get_world(world);
    const OreHealth *health = ecs_field(it, OreHealth, 0);
    const OreHealthBars *bars = ecs_singleton_get(real, OreHealthBars);
    ecs_entity_t root = bars && bars->root && ecs_is_alive(real, bars->root) ? bars->root : 0;
    bool ready = root && bars->bar && ecs_is_alive(real, bars->bar);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        OreHealthBar *link = ecs_get_mut(world, e, OreHealthBar);
        ecs_entity_t bar = link && link->bar && ecs_is_alive(real, link->bar) ? link->bar : 0;
        float max = health[i].max, value = health[i].value;
        bool damaged = max > 0 && value > 0 && value < max &&
            !(ecs_has(real, e, OrePlayer) && ore_rocket_aboard(real));
        if (!damaged || !ready) {
            if (bar) {
                ecs_delete(world, bar);
            }

            if (link) {
                link->bar = 0;
            }

            continue;
        }

        if (!bar) {
            bar = ecs_new_w_pair(world, EcsChildOf, root);
            ecs_add_pair(world, bar, OreBarOf, e);
            if (link) {
                link->bar = bar;
            }
        }

        ore_health_bar_set(world, bars, bar, e, ecs_get(real, e, OreModelBox), value / max);
    }
}

/** Show the view of the health bars while playing and hide it in every other state, by giving
 * it its camera or none. Runs every frame in every state and writes only a change.
 *
 * @param it Iterator with the OreHealthBars singleton
 */
static void OreHealthBarsShow(
    ecs_iter_t *it)
{
    const OreHealthBars *bars = ecs_field(it, OreHealthBars, 0);
    ecs_entity_t camera = ore_state_is(it->world, OreGameStatePlaying) ? bars->camera : 0;
    View *view = NULL;
    if (bars->view && ecs_is_alive(it->world, bars->view)) {
        view = ecs_get_mut(it->world, bars->view, View);
    }

    if (view && view->camera != camera) {
        view->camera = camera;
        ecs_modified(it->stage, bars->view, View);
    }
}

void ore_health_bars_import(
    ecs_world_t *world)
{
    ecs_add_pair(world, ecs_id(OreModelBox), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(OreBuilding), EcsWith, ecs_id(OreHealthBar));
    ecs_add_pair(world, ecs_id(OrePlayer), EcsWith, ecs_id(OreHealthBar));
    ecs_add_pair(world, OreBarOf, EcsOnDeleteTarget, EcsDelete);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OreHealthBarSync" }),
        .query.terms = {
            { .id = ecs_id(OreHealth), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(OreBuilding), .src.id = EcsSelf, .inout = EcsInOutNone,
              .oper = EcsOr },
            { .id = ecs_id(OrePlayer), .src.id = EcsSelf, .inout = EcsInOutNone }
        },
        .events = { EcsOnSet },
        .callback = OreHealthBarSync
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreHealthBarsShow" }),
        .phase = EcsPreStore,
        .query.terms = {
            { .id = ecs_id(OreHealthBars), .src.id = ecs_id(OreHealthBars), .inout = EcsIn }
        },
        .callback = OreHealthBarsShow
    });
}
