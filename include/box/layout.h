/**
 * @file box/layout.h
 * @brief Layout components.
 */

#ifndef BOX_LAYOUT_H
#define BOX_LAYOUT_H

#include "types.h"

#ifdef BOX_LAYOUT_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Where an element or the children of a layout sit on the x axis */
ECS_ENUM(Align, {
    AlignLeft = 0,   /**< Against the left edge */
    AlignCenter = 1, /**< In the middle */
    AlignRight = 2   /**< Against the right edge */
});

/** Where an element or the children of a layout sit on the y axis */
ECS_ENUM(VAlign, {
    VAlignTop = 0,    /**< Against the top edge */
    VAlignMiddle = 1, /**< In the middle */
    VAlignBottom = 2  /**< Against the bottom edge */
});

/** Axis a layout places the children of an element along */
ECS_ENUM(Direction, {
    DirectionNone = 0,   /**< The children keep the position they were given */
    DirectionRow = 1,    /**< The children follow each other to the right */
    DirectionColumn = 2  /**< The children follow each other downward */
});

/** Padding */
ECS_STRUCT(Padding, {
    float top;
    float bottom;
    float left;
    float right;
});

/** Arranges the children of an element in a row or a column. */
ECS_STRUCT(Layout, {
    Direction direction;  /**< Axis the children are placed along */
    Padding padding;      /**< Logical pixels between the box and its children */
    float spacing;        /**< Logical pixels between two children */
    Align h;              /**< Alignment on the x axis */
    VAlign v;             /**< Alignment on the y axis */
});

ECS_ENUM(SizeMode, {
    SizeModeFit = 0,
    SizeModeFixed = 1,
    SizeModeGrow = 2
});

ECS_STRUCT(Sizing, {
    SizeMode mode;
    float value;
    float min;
    float max;
});

ECS_STRUCT(LayoutItem, {
    Sizing width;
    Sizing height;
    Align h;
    VAlign v;
    float x;
    float y;
});

ECS_STRUCT(LayoutBounds, {
    float width;
    float height;
});

void BoxLayoutImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
