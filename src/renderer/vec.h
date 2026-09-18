/**
 * @file renderer/vec.h
 * @brief Minimal growable array used inside frame packets and the renderer.
 */

#ifndef BOX_RENDERER_VEC_H
#define BOX_RENDERER_VEC_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/** Growable array of fixed size elements */
typedef struct box_vec_t {
    void *data;          /**< Element storage */
    uint32_t count;      /**< Elements in use */
    uint32_t capacity;   /**< Elements allocated */
    uint32_t size;       /**< Element size in bytes */
} box_vec_t;

/** Initialize a vector for elements of a given size.
 *
 * @param v The vector
 * @param size Element size in bytes
 */
static inline void box_vec_init(
    box_vec_t *v,
    uint32_t size)
{
    v->data = NULL;
    v->count = 0;
    v->capacity = 0;
    v->size = size;
}

/** Release the storage of a vector.
 *
 * @param v The vector
 */
static inline void box_vec_fini(
    box_vec_t *v)
{
    free(v->data);
    v->data = NULL;
    v->count = 0;
    v->capacity = 0;
}

/** Make room for at least a number of elements.
 *
 * @param v The vector
 * @param capacity Required capacity
 */
static inline void box_vec_reserve(
    box_vec_t *v,
    uint32_t capacity)
{
    if (capacity > v->capacity) {
        uint32_t grown = v->capacity ? v->capacity * 2 : 16;
        if (grown < capacity) {
            grown = capacity;
        }

        v->data = realloc(v->data, (size_t)grown * v->size);
        v->capacity = grown;
    }
}

/** Append an uninitialized element.
 *
 * @param v The vector
 * @return Pointer to the new element
 */
static inline void* box_vec_push(
    box_vec_t *v)
{
    box_vec_reserve(v, v->count + 1);

    return (char*)v->data + (size_t)v->count ++ * v->size;
}

/** Element accessor.
 *
 * @param v The vector
 * @param index Element index
 * @return Pointer to the element
 */
static inline void* box_vec_at(
    const box_vec_t *v,
    uint32_t index)
{
    return (char*)v->data + (size_t)index * v->size;
}

/** Drop all elements but keep the storage.
 *
 * @param v The vector
 */
static inline void box_vec_clear(
    box_vec_t *v)
{
    v->count = 0;
}

#endif
