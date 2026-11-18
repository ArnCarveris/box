/**
 * @file portal/sensor.c
 * @brief Notices actors that come near: a PortalSensor is near while a PortalActor of its scene
 * is within its radius.
 *
 * The sensor writes near only when it changes, with an OnSet, so scripts that read it (a door
 * template) run again only then. The OnSets are emitted after the sensors were iterated, outside
 * of readonly mode, as the scripts they run may change the entities of the sensors.
 */

#include "portal.h"

/** Actors the sensors notice at most */
#define BOX_PORTAL_ACTORS_MAX (64)

ECS_TAG_DECLARE(PortalActor);

/** The actors of a frame and the query that finds them */
typedef struct box_portal_actors_t {
    ecs_query_t *query;     /**< Entities with PortalActor */
    int32_t count;          /**< Actors found this frame */
    vec3 positions[BOX_PORTAL_ACTORS_MAX];        /**< Their world positions */
    ecs_entity_t scenes[BOX_PORTAL_ACTORS_MAX];   /**< Their scenes */
    ecs_vec_t changed;      /**< Sensors whose near changed this frame (ecs_entity_t) */
} box_portal_actors_t;

/** Find the actors of the frame.
 *
 * @param world The world
 * @param actors Receives their positions and scenes
 */
static void box_portal_actors_find(
    ecs_world_t *world,
    box_portal_actors_t *actors)
{
    ecs_iter_t it = ecs_query_iter(world, actors->query);
    actors->count = 0;

    while (ecs_query_next(&it)) {
        int32_t i;

        for (i = 0; i < it.count && actors->count < BOX_PORTAL_ACTORS_MAX; i ++) {
            WorldTransform buf;
            const WorldTransform *transform = box_transform_world(world, it.entities[i], &buf);

            if (transform) {
                glm_vec3_copy(ECS_CONST_CAST(float*, transform->m[3]),
                    actors->positions[actors->count]);
                actors->scenes[actors->count ++] = ecs_get_target_for(
                    world, it.entities[i], EcsChildOf, Scene);
            }
        }
    }
}

/** Check whether an actor of a scene is within a radius of a point.
 *
 * @param actors The actors of the frame
 * @param scene The scene
 * @param point The point, in world space
 * @param radius The radius
 * @return Whether one is
 */
static bool box_portal_actors_near(
    const box_portal_actors_t *actors,
    ecs_entity_t scene,
    const float *point,
    float radius)
{
    bool result = false;
    int32_t i;

    for (i = 0; i < actors->count && !result; i ++) {
        result = actors->scenes[i] == scene && glm_vec3_distance(
            ECS_CONST_CAST(float*, actors->positions[i]),
            ECS_CONST_CAST(float*, point)) < radius;
    }

    return result;
}

/** Sense the actors near every sensor of the world.
 *
 * @param it The system iterator
 */
static void BoxPortalSensors(
    ecs_iter_t *it)
{
    box_portal_actors_t *actors = it->ctx;
    bool found = false;
    int32_t k, changed;

    ecs_vec_clear(&actors->changed);

    while (ecs_query_next(it)) {
        PortalSensor *sensors = ecs_field(it, PortalSensor, 0);
        int32_t i;

        if (!found) {
            box_portal_actors_find(it->world, actors);
            found = true;
        }

        for (i = 0; i < it->count; i ++) {
            ecs_entity_t e = it->entities[i];
            WorldTransform buf;
            const WorldTransform *transform = box_transform_world(it->world, e, &buf);
            bool near = false;

            if (transform && sensors[i].radius > 0) {
                ecs_entity_t scene = ecs_get_target_for(it->world, e, EcsChildOf, Scene);
                near = box_portal_actors_near(
                    actors, scene, transform->m[3], sensors[i].radius);
            }

            if (near != sensors[i].near) {
                sensors[i].near = near;
                *ecs_vec_append_t(NULL, &actors->changed, ecs_entity_t) = e;
            }
        }
    }

    changed = ecs_vec_count(&actors->changed);

    for (k = 0; k < changed; k ++) {
        ecs_entity_t e = *ecs_vec_get_t(&actors->changed, ecs_entity_t, k);
        ecs_modified(it->world, e, PortalSensor);
    }
}

/** Free the actors of the sensor system.
 *
 * @param ctx The box_portal_actors_t
 */
static void box_portal_actors_free(
    void *ctx)
{
    box_portal_actors_t *actors = ctx;
    ecs_vec_fini_t(NULL, &actors->changed, ecs_entity_t);
    ecs_os_free(actors);
}

void box_portal_sensors_import(
    ecs_world_t *world)
{
    box_portal_actors_t *actors = ecs_os_calloc_t(box_portal_actors_t);

    ECS_TAG_DEFINE(world, PortalActor);
    ecs_vec_init_t(NULL, &actors->changed, ecs_entity_t, 0);

    actors->query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "Actors" }),
        .terms = {{ .id = PortalActor }}
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Sensors" }),
        .phase = EcsPreUpdate,
        .query.terms = {
            { .id = ecs_id(PortalSensor), .inout = EcsInOut }
        },
        .run = BoxPortalSensors,
        .immediate = true,
        .ctx = actors,
        .ctx_free = box_portal_actors_free
    });
}
