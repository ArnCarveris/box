/**
 * @file spatial/index.c
 * @brief The index of every scene: items, their trees and the commit of waiting items.
 */

#include "index.h"

/** Margin of a dynamic leaf that does not depend on its size */
#define BOX_SPATIAL_MARGIN (0.1f)

/** Margin of a dynamic leaf relative to its largest extent */
#define BOX_SPATIAL_MARGIN_SCALE (0.1f)

box_spatial_scene_t* box_spatial_scene(
    const box_spatial_t *index,
    ecs_entity_t scene)
{
    box_spatial_scene_t *result = NULL;
    if (!scene) {
        result = index->root;
    } else {
        result = ecs_map_get_deref(&index->scenes, box_spatial_scene_t, scene);
    }

    return result;
}

box_spatial_item_t* box_spatial_item(
    const box_spatial_scene_t *s,
    int32_t item)
{
    ecs_assert(item >= 0 && item < ecs_vec_count(&s->items), ECS_INVALID_PARAMETER, NULL);

    return &((box_spatial_item_t*)s->items.array)[item];
}

/** Create the empty index of a scene.
 *
 * @param scene The scene, 0 for the default scene
 * @return The index of the scene
 */
static box_spatial_scene_t* box_spatial_scene_new(
    ecs_entity_t scene)
{
    box_spatial_scene_t *s = ecs_os_calloc_t(box_spatial_scene_t);
    s->entity = scene;
    box_bvh_init(&s->trees[BOX_SPATIAL_STATIC]);
    box_bvh_init(&s->trees[BOX_SPATIAL_DYNAMIC]);
    ecs_vec_init_t(NULL, &s->items, box_spatial_item_t, 0);
    ecs_vec_init_t(NULL, &s->pending, int32_t, 0);
    ecs_vec_init_t(NULL, &s->moved, int32_t, 0);
    s->free_item = -1;

    return s;
}

/** Free the index of a scene.
 *
 * @param s The index of the scene
 */
static void box_spatial_scene_free(
    box_spatial_scene_t *s)
{
    box_bvh_fini(&s->trees[BOX_SPATIAL_STATIC]);
    box_bvh_fini(&s->trees[BOX_SPATIAL_DYNAMIC]);
    ecs_vec_fini_t(NULL, &s->items, box_spatial_item_t);
    ecs_vec_fini_t(NULL, &s->pending, int32_t);
    ecs_vec_fini_t(NULL, &s->moved, int32_t);
    ecs_os_free(s);
}

box_spatial_t* box_spatial_new(void)
{
    box_spatial_t *index = ecs_os_calloc_t(box_spatial_t);
    index->root = box_spatial_scene_new(0);
    ecs_map_init(&index->scenes, NULL);
    ecs_vec_init_t(NULL, &index->dirty, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &index->moved, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &index->rescoped, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &index->walk, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &index->leaves, box_bvh_leaf_t, 0);
    ecs_vec_init_t(NULL, &index->nodes, int32_t, 0);
    ecs_vec_init_t(NULL, &index->empty, ecs_entity_t, 0);

    return index;
}

void box_spatial_free(
    box_spatial_t *index)
{
    ecs_map_iter_t it = ecs_map_iter(&index->scenes);
    while (ecs_map_next(&it)) {
        box_spatial_scene_free(ecs_map_ptr(&it));
    }

    box_spatial_scene_free(index->root);
    ecs_map_fini(&index->scenes);
    ecs_vec_fini_t(NULL, &index->dirty, ecs_entity_t);
    ecs_vec_fini_t(NULL, &index->moved, ecs_entity_t);
    ecs_vec_fini_t(NULL, &index->rescoped, ecs_entity_t);
    ecs_vec_fini_t(NULL, &index->walk, ecs_entity_t);
    ecs_vec_fini_t(NULL, &index->leaves, box_bvh_leaf_t);
    ecs_vec_fini_t(NULL, &index->nodes, int32_t);
    ecs_vec_fini_t(NULL, &index->empty, ecs_entity_t);
    ecs_os_free(index);
}

box_spatial_scene_t* box_spatial_scene_ensure(
    box_spatial_t *index,
    ecs_entity_t scene)
{
    box_spatial_scene_t *result = box_spatial_scene(index, scene);
    if (!result) {
        result = box_spatial_scene_new(scene);
        ecs_map_insert_ptr(&index->scenes, scene, result);
    }

    return result;
}

/** Get the margin of a dynamic leaf.
 *
 * @param min Minimum corner of the bounds
 * @param max Maximum corner of the bounds
 * @return The margin
 */
static float box_spatial_margin(
    const vec3 min,
    const vec3 max)
{
    float x = max[0] - min[0], y = max[1] - min[1], z = max[2] - min[2];
    float extent = x > y ? x : y;
    extent = extent > z ? extent : z;

    return BOX_SPATIAL_MARGIN + BOX_SPATIAL_MARGIN_SCALE * extent;
}

/** Put an item that has no leaf in its tree, or let it wait for the commit when static.
 *
 * @param s The scene
 * @param item The item
 */
static void box_spatial_item_place(
    box_spatial_scene_t *s,
    int32_t item)
{
    box_spatial_item_t *it = box_spatial_item(s, item);
    if (it->tree == BOX_SPATIAL_DYNAMIC) {
        vec3 min, max;
        float margin = box_spatial_margin(it->min, it->max);
        glm_vec3_subs(it->min, margin, min);
        glm_vec3_adds(it->max, margin, max);
        it->node = box_bvh_insert(&s->trees[BOX_SPATIAL_DYNAMIC], min, max, item);
    } else {
        it->node = -1;
        *ecs_vec_append_t(NULL, &s->pending, int32_t) = item;
    }
}

int32_t box_spatial_item_add(
    box_spatial_scene_t *s,
    ecs_entity_t e,
    int32_t tree,
    const vec3 min,
    const vec3 max)
{
    int32_t result = s->free_item;
    box_spatial_item_t *it;
    if (result >= 0) {
        it = box_spatial_item(s, result);
        s->free_item = it->node;
    } else {
        result = ecs_vec_count(&s->items);
        it = ecs_vec_append_t(NULL, &s->items, box_spatial_item_t);
    }

    it->entity = e;
    it->tree = tree;
    it->moved = false;
    glm_vec3_copy(ECS_CONST_CAST(float*, min), it->min);
    glm_vec3_copy(ECS_CONST_CAST(float*, max), it->max);
    s->count ++;
    box_spatial_item_place(s, result);

    return result;
}

bool box_spatial_item_set(
    box_spatial_scene_t *s,
    int32_t item,
    int32_t tree,
    const vec3 min,
    const vec3 max)
{
    box_spatial_item_t *it = box_spatial_item(s, item);
    int32_t node = it->node;
    glm_vec3_copy(ECS_CONST_CAST(float*, min), it->min);
    glm_vec3_copy(ECS_CONST_CAST(float*, max), it->max);
    if (it->tree != tree) {
        if (node >= 0) {
            box_bvh_remove(&s->trees[it->tree], node);
        }

        it->tree = tree;
        it->moved = false;
        box_spatial_item_place(s, item);
    } else if (node >= 0 && tree == BOX_SPATIAL_DYNAMIC) {
        it->node = box_bvh_move(&s->trees[tree], node, min, max, box_spatial_margin(min, max));
    } else if (node >= 0 && !it->moved) {
        it->moved = true;
        *ecs_vec_append_t(NULL, &s->moved, int32_t) = item;
    }

    return it->node != node;
}

void box_spatial_item_remove(
    box_spatial_t *index,
    box_spatial_scene_t *s,
    int32_t item)
{
    box_spatial_item_t *it = box_spatial_item(s, item);
    if (it->node >= 0) {
        box_bvh_remove(&s->trees[it->tree], it->node);
    }

    it->entity = 0;
    it->moved = false;
    it->node = s->free_item;
    s->free_item = item;
    s->count --;
    if (!s->count && s->entity) {
        *ecs_vec_append_t(NULL, &index->empty, ecs_entity_t) = s->entity;
    }
}

/** Test whether an item waits for a leaf in the static tree.
 *
 * @param s The scene
 * @param item The item
 * @return true when the item is in use, static and has no leaf
 */
static bool box_spatial_item_waits(
    const box_spatial_scene_t *s,
    int32_t item)
{
    box_spatial_item_t *it = box_spatial_item(s, item);

    return it->entity && it->tree == BOX_SPATIAL_STATIC && it->node < 0;
}

void box_spatial_scene_rebuild(
    box_spatial_t *index,
    box_spatial_scene_t *s)
{
    int32_t i, count = ecs_vec_count(&s->items), leaves = 0;
    ecs_vec_set_count_t(NULL, &index->leaves, box_bvh_leaf_t, count);
    ecs_vec_set_count_t(NULL, &index->nodes, int32_t, count);
    box_bvh_leaf_t *leaf = ecs_vec_first(&index->leaves);
    int32_t *nodes = ecs_vec_first(&index->nodes);
    for (i = 0; i < count; i ++) {
        box_spatial_item_t *it = box_spatial_item(s, i);
        it->moved = false;
        if (it->entity && it->tree == BOX_SPATIAL_STATIC) {
            glm_vec3_copy(it->min, leaf[leaves].min);
            glm_vec3_copy(it->max, leaf[leaves].max);
            leaf[leaves].item = i;
            leaves ++;
        }
    }

    box_bvh_build(&s->trees[BOX_SPATIAL_STATIC], leaf, leaves, nodes);
    for (i = 0; i < leaves; i ++) {
        box_spatial_item(s, leaf[i].item)->node = nodes[leaf[i].item];
    }

    ecs_vec_clear(&s->pending);
    ecs_vec_clear(&s->moved);
}

/** Give the moved static items of a scene their new bounds in its tree.
 *
 * @param s The scene
 */
static void box_spatial_scene_move(
    box_spatial_scene_t *s)
{
    int32_t i, count = ecs_vec_count(&s->moved);
    int32_t *moved = ecs_vec_first(&s->moved);
    for (i = 0; i < count; i ++) {
        box_spatial_item_t *it = box_spatial_item(s, moved[i]);
        if (it->moved) {
            it->moved = false;
            it->node = box_bvh_move(
                &s->trees[BOX_SPATIAL_STATIC], it->node, it->min, it->max, 0);
        }
    }

    ecs_vec_clear(&s->moved);
}

/** Put the waiting static items of a scene in its tree.
 *
 * @param index The index
 * @param s The scene
 */
static void box_spatial_scene_commit(
    box_spatial_t *index,
    box_spatial_scene_t *s)
{
    int32_t i, waiting = 0, count = ecs_vec_count(&s->pending);
    int32_t *pending = ecs_vec_first(&s->pending);
    if (!count) {
        goto done;
    }

    for (i = 0; i < count; i ++) {
        waiting += box_spatial_item_waits(s, pending[i]);
    }

    if (waiting >= s->trees[BOX_SPATIAL_STATIC].leaves) {
        box_spatial_scene_rebuild(index, s);
        goto done;
    }

    for (i = 0; i < count; i ++) {
        if (box_spatial_item_waits(s, pending[i])) {
            box_spatial_item_t *it = box_spatial_item(s, pending[i]);
            it->node = box_bvh_insert(
                &s->trees[BOX_SPATIAL_STATIC], it->min, it->max, pending[i]);
        }
    }

    ecs_vec_clear(&s->pending);
done:
    return;
}

void box_spatial_move(
    box_spatial_t *index)
{
    box_spatial_scene_move(index->root);
    ecs_map_iter_t it = ecs_map_iter(&index->scenes);
    while (ecs_map_next(&it)) {
        box_spatial_scene_move(ecs_map_ptr(&it));
    }
}

void box_spatial_commit(
    box_spatial_t *index)
{
    int32_t i;
    box_spatial_scene_commit(index, index->root);
    ecs_map_iter_t it = ecs_map_iter(&index->scenes);
    while (ecs_map_next(&it)) {
        box_spatial_scene_commit(index, ecs_map_ptr(&it));
    }

    for (i = 0; i < ecs_vec_count(&index->empty); i ++) {
        ecs_entity_t scene = *ecs_vec_get_t(&index->empty, ecs_entity_t, i);
        box_spatial_scene_t *s = box_spatial_scene(index, scene);
        if (s && !s->count) {
            ecs_map_remove(&index->scenes, scene);
            box_spatial_scene_free(s);
        }
    }

    ecs_vec_clear(&index->empty);
}
