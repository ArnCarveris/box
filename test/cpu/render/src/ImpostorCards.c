/**
 * @file ImpostorCards.c
 * @brief Tests for the cards of the impostors (ImpostorDraw): once a bake is ready every instance
 * of the prefab gets a card beyond the distance and a DrawRange that ends there, all cards of a
 * prefab share one batch that binds the atlases, and the ranges follow the Impostor.
 *
 * Every case renders with the null backend, which bakes the atlases without drawing them, and
 * reads the entities, batches and packets of the extract.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "draw.h"

/** Frames a case runs at most for a bake */
#define CARDS_FRAMES_MAX (200)

/** Create a world that renders with a null backend and has a view.
 *
 * @param null Receives the backend
 * @return The world
 */
static ecs_world_t* cards_world(
    box_render_null_t **null)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});

    return world;
}

/** Run a frame and wait until the backend has its packet.
 *
 * @param world The world
 */
static void cards_frame(
    ecs_world_t *world)
{
    ecs_progress(world, 0);
    box_render_sync(world);
}

/** Create a prefab of a box with a child box and an Impostor.
 *
 * @param world The world
 * @param distance Impostor.distance
 * @param fade Impostor.fade
 * @return The prefab
 */
static ecs_entity_t cards_prefab(
    ecs_world_t *world,
    float distance,
    float fade)
{
    ecs_entity_t result = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, result, Box, {2, 2, 2});
    ecs_set(world, result, Rgb, {200, 100, 50});
    ecs_entity_t child = ecs_new_w_id(world, EcsPrefab);
    ecs_add_pair(world, child, EcsChildOf, result);
    ecs_set_name(world, child, "top");
    ecs_set(world, child, Position, {0, 1.5f, 0});
    ecs_set(world, child, Box, {1, 1, 1});
    ecs_set(world, result, Impostor, {.frames = 4, .resolution = 16, .hemisphere = true,
        .distance = distance, .fade = fade});

    return result;
}

/** Create an instance of a prefab.
 *
 * @param world The world
 * @param prefab The prefab
 * @param x Its x position
 * @return The instance
 */
static ecs_entity_t cards_instance(
    ecs_world_t *world,
    ecs_entity_t prefab,
    float x)
{
    ecs_entity_t result = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, result, Position, {x, 0, -100});

    return result;
}

/** Run frames until an impostor has a ready bake, then two more, so the cards exist and are in
 * their batch.
 *
 * @param world The world
 * @param prefab The prefab with Impostor
 */
static void cards_wait(
    ecs_world_t *world,
    ecs_entity_t prefab)
{
    const ImpostorStatus *s = NULL;
    int32_t i;
    for (i = 0; i < CARDS_FRAMES_MAX; i ++) {
        cards_frame(world);
        s = ecs_get(world, prefab, ImpostorStatus);
        if (s && s->ready) {
            break;
        }
    }

    test_assert(s != NULL);
    test_bool(s->ready, true);
    cards_frame(world);
    cards_frame(world);
}

/** Get the card of an instance.
 *
 * @param world The world
 * @param inst The instance
 * @return The card, 0 for none
 */
static ecs_entity_t cards_card_of(
    ecs_world_t *world,
    ecs_entity_t inst)
{
    ecs_entity_t result = 0;
    int32_t found = 0;
    ecs_iter_t it = ecs_children(world, inst);
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            if (ecs_has(world, it.entities[i], ImpostorCard)) {
                result = it.entities[i];
                found ++;
            }
        }
    }

    test_assert(found <= 1);

    return result;
}

/** Get the batch of a shape.
 *
 * @param world The world
 * @param e The shape
 * @return Its batch
 */
static box_draw_batch_t* cards_batch_of(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const DrawSlot *slot = ecs_get(world, e, DrawSlot);
    test_assert(slot != NULL && slot->batch >= 0);

    return box_draws_batch(box_draws_state(world), slot->batch);
}

/** Check the owned DrawRange of an entity.
 *
 * @param world The world
 * @param e The entity
 * @param near Expected near
 * @param far Expected far
 * @param fade Expected fade
 */
static void cards_range(
    ecs_world_t *world,
    ecs_entity_t e,
    float near,
    float far,
    float fade)
{
    const DrawRange *r = ecs_get(world, e, DrawRange);
    test_assert(r != NULL);
    test_flt(r->near_, near);
    test_flt(r->far_, far);
    test_flt(r->fade, fade);
}

void ImpostorCards_none_before_ready(void) {
    box_render_null_t *null;
    ecs_world_t *world = cards_world(&null);
    ecs_entity_t prefab = cards_prefab(world, 50, 5);
    ecs_entity_t inst = cards_instance(world, prefab, 0);
    cards_frame(world);
    const ImpostorStatus *s = ecs_get(world, prefab, ImpostorStatus);
    test_assert(s != NULL);
    test_bool(s->ready, false);
    test_assert(!ecs_owns(world, inst, DrawRange));
    test_assert(cards_card_of(world, inst) == 0);
    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(stats.impostors, 0);
    ecs_fini(world);
    box_render_null_free(null);
}

void ImpostorCards_card_per_instance(void) {
    box_render_null_t *null;
    ecs_world_t *world = cards_world(&null);
    ecs_entity_t prefab = cards_prefab(world, 50, 5);
    ecs_entity_t a = cards_instance(world, prefab, -5);
    ecs_entity_t b = cards_instance(world, prefab, 0);
    ecs_entity_t c = cards_instance(world, prefab, 5);
    cards_wait(world, prefab);
    ecs_entity_t insts[3] = {a, b, c};
    for (int32_t i = 0; i < 3; i ++) {
        ecs_entity_t card = cards_card_of(world, insts[i]);
        test_assert(card != 0);
        test_assert(ecs_get(world, card, ImpostorCard)->impostor == prefab);
        test_assert(ecs_owns(world, insts[i], DrawRange));
        cards_range(world, insts[i], 0, 50, 5);
        cards_range(world, card, 50, 0, 5);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void ImpostorCards_children_fade_out(void) {
    box_render_null_t *null;
    ecs_world_t *world = cards_world(&null);
    ecs_entity_t prefab = cards_prefab(world, 50, 5);
    ecs_entity_t inst = cards_instance(world, prefab, 0);
    cards_wait(world, prefab);
    ecs_entity_t top = ecs_lookup_child(world, inst, "top");
    test_assert(top != 0);
    float range[3];
    test_bool(box_draws_range(world, top, range), true);
    test_flt(range[0], 0);
    test_flt(range[1], 50);
    test_flt(range[2], 5);
    box_draw_batch_t *b = cards_batch_of(world, top);
    test_flt(b->key.range[1], 50);
    test_assert(b->key.impostor == 0);
    b = cards_batch_of(world, inst);
    test_flt(b->key.range[1], 50);
    test_assert(b->key.impostor == 0);
    ecs_fini(world);
    box_render_null_free(null);
}

void ImpostorCards_one_batch_per_prefab(void) {
    box_render_null_t *null;
    ecs_world_t *world = cards_world(&null);
    ecs_entity_t prefab = cards_prefab(world, 50, 5);
    ecs_entity_t other = cards_prefab(world, 60, 5);
    ecs_entity_t insts[4];
    for (int32_t i = 0; i < 4; i ++) {
        insts[i] = cards_instance(world, prefab, (float)i * 3);
    }

    ecs_entity_t lone = cards_instance(world, other, 20);
    cards_wait(world, prefab);
    cards_wait(world, other);
    box_draw_batch_t *first = cards_batch_of(world, cards_card_of(world, insts[0]));
    test_assert(first->key.impostor == prefab);
    test_bool(first->key.cutout, true);
    test_int(first->key.blend, Opaque);
    test_flt(first->key.range[0], 50);
    for (int32_t i = 1; i < 4; i ++) {
        test_assert(cards_batch_of(world, cards_card_of(world, insts[i])) == first);
    }

    box_draw_batch_t *second = cards_batch_of(world, cards_card_of(world, lone));
    test_assert(second != first);
    test_assert(second->key.impostor == other);
    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(stats.impostors, 5);
    test_int(stats.impostor_batches, 2);
    ecs_fini(world);
    box_render_null_free(null);
}

void ImpostorCards_packet_batch_binds_atlases(void) {
    box_render_null_t *null;
    ecs_world_t *world = cards_world(&null);
    ecs_entity_t prefab = cards_prefab(world, 50, 5);
    cards_instance(world, prefab, 0);
    cards_instance(world, prefab, 4);
    cards_wait(world, prefab);
    const box_render_batch_t *found = NULL;
    int32_t i, count = 0;
    for (i = 0; i < ecs_vec_count(&null->last.batches); i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&null->last.batches, box_render_batch_t, i);
        if (b->impostor) {
            found = b;
            count ++;
        }
    }

    test_int(count, 1);
    test_assert(found != NULL);
    test_int(found->instance_count, 2);
    test_int(found->pipeline, BOX_RENDER_SHADER_MESH);
    test_bool(found->cutout, true);
    test_int(found->blend, Opaque);
    test_flt(found->range[0], 50);
    test_flt(found->range[2], 5);
    test_uint(found->textures[0], box_render_impostor_atlas(world, prefab, ImpostorAlbedo));
    test_uint(found->textures[1], 0);
    test_uint(found->textures[2], box_render_impostor_atlas(world, prefab, ImpostorDepth));
    test_uint(found->textures[3], box_render_impostor_atlas(world, prefab, ImpostorNormal));
    test_assert(found->textures[0] != 0);
    for (i = 1; i < BOX_LOD_LEVELS_MAX; i ++) {
        test_uint(found->lods[i], 0);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void ImpostorCards_mesh_holds_grid(void) {
    box_render_null_t *null;
    ecs_world_t *world = cards_world(&null);
    ecs_entity_t prefab = cards_prefab(world, 50, 5);
    ecs_entity_t inst = cards_instance(world, prefab, 0);
    cards_wait(world, prefab);
    const ImpostorStatus *s = ecs_get(world, prefab, ImpostorStatus);
    const Mesh3 *m = ecs_get(world, cards_card_of(world, inst), Mesh3);
    test_assert(m != NULL);
    test_int(ecs_vec_count(&m->vertices), 4);
    test_int(ecs_vec_count(&m->indices), 6);
    test_int(ecs_vec_count(&m->data0), 4);
    test_int(ecs_vec_count(&m->data1), 4);
    const float *d0 = *ecs_vec_get_t(&m->data0, vec4, 0);
    const float *d1 = *ecs_vec_get_t(&m->data1, vec4, 3);
    float half = d0[3];
    test_flt(d0[0], (float)s->frames);
    test_flt(d0[1], 1);
    test_assert(half > s->radius);
    test_flt(d0[2], s->radius / half);
    test_flt(d1[0], s->center[0]);
    test_flt(d1[1], s->center[1]);
    test_flt(d1[2], s->center[2]);

    /* The positions span the box of the bake, whose half diagonal is its radius */
    const float *lo = *ecs_vec_get_t(&m->vertices, vec3, 0);
    const float *hi = *ecs_vec_get_t(&m->vertices, vec3, 3);
    for (int32_t k = 0; k < 3; k ++) {
        test_assert(lo[k] < hi[k]);
        test_flt((lo[k] + hi[k]) * 0.5f, s->center[k]);
    }

    test_flt(glm_vec3_distance(ECS_CONST_CAST(float*, lo), ECS_CONST_CAST(float*, hi)) * 0.5f,
        s->radius);

    ecs_fini(world);
    box_render_null_free(null);
}

void ImpostorCards_distance_change_updates_ranges(void) {
    box_render_null_t *null;
    ecs_world_t *world = cards_world(&null);
    ecs_entity_t prefab = cards_prefab(world, 50, 5);
    ecs_entity_t inst = cards_instance(world, prefab, 0);
    cards_wait(world, prefab);
    int32_t revision = ecs_get(world, prefab, ImpostorStatus)->revision;
    ecs_set(world, prefab, Impostor, {.frames = 4, .resolution = 16, .hemisphere = true,
        .distance = 80, .fade = 2});
    cards_frame(world);
    cards_frame(world);
    cards_range(world, inst, 0, 80, 2);
    ecs_entity_t card = cards_card_of(world, inst);
    cards_range(world, card, 80, 0, 2);
    test_flt(cards_batch_of(world, card)->key.range[0], 80);
    test_int(ecs_get(world, prefab, ImpostorStatus)->revision, revision);
    ecs_fini(world);
    box_render_null_free(null);
}

void ImpostorCards_owned_range_kept(void) {
    box_render_null_t *null;
    ecs_world_t *world = cards_world(&null);
    ecs_entity_t prefab = cards_prefab(world, 50, 5);
    ecs_entity_t inst = cards_instance(world, prefab, 0);
    ecs_set(world, inst, DrawRange, {.near_ = 1, .far_ = 200, .fade = 2});
    cards_wait(world, prefab);
    cards_range(world, inst, 1, 50, 5);
    ecs_remove(world, prefab, Impostor);
    cards_frame(world);
    cards_range(world, inst, 1, 200, 2);
    test_assert(cards_card_of(world, inst) == 0);
    ecs_fini(world);
    box_render_null_free(null);
}

void ImpostorCards_remove_impostor_removes_cards(void) {
    box_render_null_t *null;
    ecs_world_t *world = cards_world(&null);
    ecs_entity_t prefab = cards_prefab(world, 50, 5);
    ecs_entity_t inst = cards_instance(world, prefab, 0);
    cards_wait(world, prefab);
    test_assert(cards_card_of(world, inst) != 0);
    ecs_remove(world, prefab, Impostor);
    cards_frame(world);
    cards_frame(world);
    test_assert(cards_card_of(world, inst) == 0);
    test_assert(!ecs_owns(world, inst, DrawRange));
    float range[3];
    test_bool(box_draws_range(world, inst, range), false);
    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(stats.impostors, 0);
    test_int(stats.impostor_batches, 0);
    ecs_fini(world);
    box_render_null_free(null);
}

void ImpostorCards_zero_distance_draws_full(void) {
    box_render_null_t *null;
    ecs_world_t *world = cards_world(&null);
    ecs_entity_t prefab = cards_prefab(world, 50, 5);
    ecs_entity_t inst = cards_instance(world, prefab, 0);
    cards_wait(world, prefab);
    test_assert(cards_card_of(world, inst) != 0);
    ecs_set(world, prefab, Impostor, {.frames = 4, .resolution = 16, .hemisphere = true,
        .distance = 0, .fade = 5});
    cards_frame(world);
    test_assert(cards_card_of(world, inst) == 0);
    test_assert(!ecs_owns(world, inst, DrawRange));
    ecs_fini(world);
    box_render_null_free(null);
}

void ImpostorCards_follow_instances(void) {
    box_render_null_t *null;
    ecs_world_t *world = cards_world(&null);
    ecs_entity_t prefab = cards_prefab(world, 50, 5);
    ecs_entity_t a = cards_instance(world, prefab, 0);
    ecs_entity_t b = cards_instance(world, prefab, 4);
    cards_wait(world, prefab);
    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(stats.impostors, 2);

    ecs_entity_t c = cards_instance(world, prefab, 8);
    cards_frame(world);
    cards_frame(world);
    test_assert(cards_card_of(world, c) != 0);
    cards_range(world, c, 0, 50, 5);
    box_render_draws_stats(world, &stats);
    test_int(stats.impostors, 3);

    ecs_delete(world, b);
    cards_frame(world);
    cards_frame(world);
    box_render_draws_stats(world, &stats);
    test_int(stats.impostors, 2);
    test_assert(cards_card_of(world, a) != 0);
    ecs_fini(world);
    box_render_null_free(null);
}

void ImpostorCards_bake_copy_has_no_card(void) {
    box_render_null_t *null;
    ecs_world_t *world = cards_world(&null);
    ecs_entity_t prefab = cards_prefab(world, 50, 5);
    cards_instance(world, prefab, 0);
    cards_wait(world, prefab);
    int32_t cards = 0;
    ecs_iter_t it = ecs_each_id(world, ecs_pair(EcsIsA, prefab));
    while (ecs_each_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            cards += cards_card_of(world, it.entities[i]) != 0;
        }
    }

    test_int(cards, 1);
    test_assert(ecs_count_id(world, ecs_pair(EcsIsA, prefab)) >= 2);
    ecs_fini(world);
    box_render_null_free(null);
}
