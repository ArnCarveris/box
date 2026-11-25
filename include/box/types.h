/**
 * @file box/types.h
 * @brief Reflected math types shared by every box module.
 *
 * The types are registered by the box.types module so scripts can use them.
 */

#ifndef BOX_TYPES_H
#define BOX_TYPES_H

#include <flecs.h>
#include <cglm/cglm.h>

#ifdef BOX_TYPES_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern ECS_COMPONENT_DECLARE(vec2);
extern ECS_COMPONENT_DECLARE(vec3);
extern ECS_COMPONENT_DECLARE(vec4);
extern ECS_COMPONENT_DECLARE(mat3);
extern ECS_COMPONENT_DECLARE(mat4);

/** A point or vector in f64, for positions that keep their precision anywhere in a large
 * world */
typedef double dvec3[3];

extern ECS_COMPONENT_DECLARE(dvec3);

/** Copy a dvec3.
 *
 * @param v The vector
 * @param out Receives the copy
 */
static inline void box_dvec3_copy(
    const dvec3 v,
    dvec3 out)
{
    out[0] = v[0];
    out[1] = v[1];
    out[2] = v[2];
}

/** Get a point relative to an origin in f32: a - origin, subtracted in f64.
 *
 * @param a The point
 * @param origin The origin
 * @param out Receives the relative point
 */
static inline void box_dvec3_rel(
    const dvec3 a,
    const dvec3 origin,
    vec3 out)
{
    out[0] = (float)(a[0] - origin[0]);
    out[1] = (float)(a[1] - origin[1]);
    out[2] = (float)(a[2] - origin[2]);
}

/** Add an f32 offset to a point: a + offset, in f64.
 *
 * @param a The point
 * @param offset The offset
 * @param out Receives the point
 */
static inline void box_dvec3_add(
    const dvec3 a,
    const vec3 offset,
    dvec3 out)
{
    out[0] = a[0] + (double)offset[0];
    out[1] = a[1] + (double)offset[1];
    out[2] = a[2] + (double)offset[2];
}

/** Display encoded color with 8 bit channels */
ECS_STRUCT(rgb, {
    uint8_t r; /**< Red */
    uint8_t g; /**< Green */
    uint8_t b; /**< Blue */
});

/** Display encoded color with 8 bit channels and an 8 bit alpha channel */
ECS_STRUCT(rgba, {
    uint8_t r; /**< Red */
    uint8_t g; /**< Green */
    uint8_t b; /**< Blue */
    uint8_t a; /**< Alpha */
});

/** Member of a component that owns heap memory. */
typedef struct box_owned_t {
    ecs_size_t offset;      /**< Offset of the member in the component */
    ecs_size_t elem_size;   /**< Element size of an ecs_vec_t member, 0 for a char* member */
} box_owned_t;

/** Owned char* member of component T */
#define BOX_OWNED_STR(T, member) { (ecs_size_t)offsetof(T, member), 0 }

/** Owned ecs_vec_t member of component T with trivially copyable elements of type E */
#define BOX_OWNED_VEC(T, member, E) { (ecs_size_t)offsetof(T, member), ECS_SIZEOF(E) }

/** Elements in a literal array of type T */
#define BOX_VEC_LITERAL_COUNT(T, ...) ((int32_t)(sizeof((T[]){ __VA_ARGS__ }) / sizeof(T)))

/** A literal array of type T as an ecs_vec_t that owns nothing, for a value that ecs_set copies
 * into a component with owned hooks; C only:
 *
 *     ecs_set(world, e, Curve, { .keys = BOX_VEC_VIEW(CurveKey, {0, 1}, {1, 2}) });
 */
#ifdef FLECS_SANITIZE
#define BOX_VEC_VIEW(T, ...) ((ecs_vec_t){ .array = (T[]){ __VA_ARGS__ },\
    .count = BOX_VEC_LITERAL_COUNT(T, __VA_ARGS__), .size = BOX_VEC_LITERAL_COUNT(T, __VA_ARGS__),\
    .elem_size = ECS_SIZEOF(T), .type_name = #T })
#else
#define BOX_VEC_VIEW(T, ...) ((ecs_vec_t){ .array = (T[]){ __VA_ARGS__ },\
    .count = BOX_VEC_LITERAL_COUNT(T, __VA_ARGS__), .size = BOX_VEC_LITERAL_COUNT(T, __VA_ARGS__) })
#endif

/** Install the lifecycle hooks of reflected component T from its owned members.
 *
 * Flecs generates no dtor, copy or move for C structs registered with ECS_META_COMPONENT,
 * so components with BOX_OWNED_STR or BOX_OWNED_VEC members call this right after it.
 */
#define box_owned_hooks(world, T, ...)\
    box_types_owned_hooks(world, ecs_id(T), (const box_owned_t[]){ __VA_ARGS__ },\
        (int32_t)(sizeof((const box_owned_t[]){ __VA_ARGS__ }) / sizeof(box_owned_t)))

/** Install ctor, dtor, copy and move hooks that free, deep copy and hand over owned members.
 *
 * Must be called before the component is used.
 *
 * @param world The world
 * @param component The component
 * @param members The members that own heap memory
 * @param count Number of members
 */
void box_types_owned_hooks(
    ecs_world_t *world,
    ecs_entity_t component,
    const box_owned_t *members,
    int32_t count);

/** Most levels of detail of a shape mesh, level 0 (the mesh as authored) included */
#define BOX_LOD_LEVELS_MAX (4)

/** Code point that stands for an ill formed UTF-8 sequence or an invalid code point */
#define BOX_UTF8_REPLACEMENT (0xFFFD)

/** Read the code point of a UTF-8 sequence and move past it.
 *
 * Decodes per table 3-7 of the Unicode standard, as the WHATWG Encoding decoder does: overlong
 * forms, surrogates and code points above U+10FFFF are ill formed.
 *
 * Each maximal subpart of an ill formed sequence reads as one U+FFFD and the next sequence
 * starts at the byte that broke it, so a terminating zero is never passed.
 *
 * @param str The string, advanced past the sequence or the maximal subpart
 * @return The code point, BOX_UTF8_REPLACEMENT for an ill formed sequence
 */
int32_t box_utf8_next(
    const char **str);

/** Encode a code point as UTF-8.
 *
 * Surrogates, negative values and values above U+10FFFF encode as U+FFFD.
 *
 * @param code The code point
 * @param out Receives the bytes, at least 4
 * @return Number of bytes written, 1 to 4
 */
int32_t box_utf8_encode(
    int32_t code,
    char *out);

/** Register the box.types module.
 *
 * @param world The world
 */
void BoxTypesImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
