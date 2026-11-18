/**
 * @file controllers/near.c
 * @brief Shapes near a body, for the queries that are not a move: ladders and water.
 */

#include "controllers.h"
#include "../spatial/spatial.h"

int32_t box_controller_near(
    const ecs_world_t *world,
    ecs_entity_t scene,
    const vec3 min,
    const vec3 max,
    ecs_vec_t *out)
{
    ecs_vec_t hits;
    int32_t i, count, before = ecs_vec_count(out);
    ecs_vec_init_t(NULL, &hits, ecs_entity_t, 0);
    count = box_spatial_overlap_box(world, scene, min, max, &hits);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&hits, ecs_entity_t, i);
        box_spatial_expand_box(world, e, min, max, out);
    }

    ecs_vec_fini_t(NULL, &hits, ecs_entity_t);

    return ecs_vec_count(out) - before;
}
