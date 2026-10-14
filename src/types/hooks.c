/**
 * @file types/hooks.c
 * @brief Lifecycle hooks for reflected components with members that own heap memory.
 */

#include <box.h>

/** Owned members of a component, stored as the lifecycle context of its hooks */
typedef struct box_owned_hooks_t {
    int32_t count;              /**< Number of owned members */
    box_owned_t members[];      /**< The owned members */
} box_owned_hooks_t;

/** Free the owned members of one value.
 *
 * @param owned The owned members of the type
 * @param value The value
 */
static void box_owned_free(
    const box_owned_hooks_t *owned,
    void *value)
{
    int32_t i;
    for (i = 0; i < owned->count; i ++) {
        const box_owned_t *m = &owned->members[i];
        void *ptr = ECS_OFFSET(value, m->offset);
        if (m->elem_size) {
            ecs_vec_fini(NULL, ptr, m->elem_size);
        } else {
            ecs_os_free(*(char**)ptr);
        }
    }
}

/** Zero the owned members of one value so it no longer owns them.
 *
 * @param owned The owned members of the type
 * @param value The value
 */
static void box_owned_clear(
    const box_owned_hooks_t *owned,
    void *value)
{
    int32_t i;
    for (i = 0; i < owned->count; i ++) {
        const box_owned_t *m = &owned->members[i];
        void *ptr = ECS_OFFSET(value, m->offset);
        if (m->elem_size) {
            ecs_os_memset_t(ptr, 0, ecs_vec_t);
        } else {
            *(char**)ptr = NULL;
        }
    }
}

/** Replace the owned members of a value that was copied bit for bit with copies of their own.
 *
 * @param owned The owned members of the type
 * @param value The value
 */
static void box_owned_dup(
    const box_owned_hooks_t *owned,
    void *value)
{
    int32_t i;
    for (i = 0; i < owned->count; i ++) {
        const box_owned_t *m = &owned->members[i];
        void *ptr = ECS_OFFSET(value, m->offset);
        if (m->elem_size) {
            ecs_vec_t *v = ptr;
            if (v->array) {
                *v = ecs_vec_copy(NULL, v, m->elem_size);
            } else {
                ecs_os_memset_t(v, 0, ecs_vec_t);
            }
        } else {
            *(char**)ptr = ecs_os_strdup(*(char**)ptr);
        }
    }
}

/** Free the owned members of values.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti Type info
 */
static void box_owned_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    const box_owned_hooks_t *owned = ti->hooks.lifecycle_ctx;
    int32_t i;
    for (i = 0; i < count; i ++) {
        box_owned_free(owned, ECS_ELEM(ptr, ti->size, i));
    }
}

/** Copy values, giving the destination copies of the owned members.
 *
 * @param dst_ptr The destination values
 * @param src_ptr The source values
 * @param count Number of values
 * @param ti Type info
 */
static void box_owned_copy(
    void *dst_ptr,
    const void *src_ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    const box_owned_hooks_t *owned = ti->hooks.lifecycle_ctx;
    int32_t i;
    for (i = 0; i < count; i ++) {
        void *dst = ECS_ELEM(dst_ptr, ti->size, i);
        const void *src = ECS_ELEM(src_ptr, ti->size, i);
        if (dst != src) {
            box_owned_free(owned, dst);
            ecs_os_memcpy(dst, src, ti->size);
            box_owned_dup(owned, dst);
        }
    }
}

/** Move values, handing the owned members from the source to the destination.
 *
 * @param dst_ptr The destination values
 * @param src_ptr The source values
 * @param count Number of values
 * @param ti Type info
 */
static void box_owned_move(
    void *dst_ptr,
    void *src_ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    const box_owned_hooks_t *owned = ti->hooks.lifecycle_ctx;
    int32_t i;
    for (i = 0; i < count; i ++) {
        void *dst = ECS_ELEM(dst_ptr, ti->size, i);
        void *src = ECS_ELEM(src_ptr, ti->size, i);
        if (dst != src) {
            box_owned_free(owned, dst);
            ecs_os_memcpy(dst, src, ti->size);
            box_owned_clear(owned, src);
        }
    }
}

/** Free the owned member list when the component's type info goes away.
 *
 * @param ctx The owned member list
 */
static void box_owned_hooks_free(
    void *ctx)
{
    ecs_os_free(ctx);
}

void box_types_owned_hooks(
    ecs_world_t *world,
    ecs_entity_t component,
    const box_owned_t *members,
    int32_t count)
{
    ecs_assert(count > 0, ECS_INVALID_PARAMETER, NULL);
    ecs_size_t size = ECS_SIZEOF(box_owned_t) * count;
    box_owned_hooks_t *owned = ecs_os_malloc(ECS_SIZEOF(box_owned_hooks_t) + size);
    owned->count = count;
    ecs_os_memcpy(owned->members, members, size);

    ecs_set_hooks_id(world, component, &(ecs_type_hooks_t){
        .ctor = flecs_default_ctor,
        .dtor = box_owned_dtor,
        .copy = box_owned_copy,
        .move = box_owned_move,
        .lifecycle_ctx = owned,
        .lifecycle_ctx_free = box_owned_hooks_free
    });
}
