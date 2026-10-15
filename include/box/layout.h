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

/** Space between the edges of an element and its content, in logical pixels */
ECS_STRUCT(Padding, {
    float top;    /**< Space below the top edge */
    float bottom; /**< Space above the bottom edge */
    float left;   /**< Space right of the left edge */
    float right;  /**< Space left of the right edge */
});

/** Arranges the children of an element in a row or a column. */
ECS_STRUCT(Layout, {
    Direction direction;  /**< Axis the children are placed along */
    Padding padding;      /**< Logical pixels between the box and its children */
    float spacing;        /**< Logical pixels between two children */
    Align h;              /**< Alignment on the x axis */
    VAlign v;             /**< Alignment on the y axis */
});

/** How an element sizes itself on one axis */
ECS_ENUM(SizeMode, {
    SizeModeFit = 0,   /**< Fit the content plus padding, or the shape */
    SizeModeFixed = 1, /**< Take the size in value */
    SizeModeGrow = 2   /**< Share the free space of the parent by weight, at least the fit size */
});

/** Size of an element on one axis */
ECS_STRUCT(Sizing, {
    SizeMode mode; /**< How the size is computed */
    float value;   /**< Size for SizeModeFixed, weight for SizeModeGrow, 1 in a new LayoutItem */
    float min;     /**< Smallest size, 0 for no limit */
    float max;     /**< Largest size, 0 for no limit */
});

/** Sizing and placement of an element inside the layout of its parent */
ECS_STRUCT(LayoutItem, {
    Sizing width;  /**< Sizing on the x axis */
    Sizing height; /**< Sizing on the y axis */
    Align h;       /**< Anchor on the x axis in a DirectionNone parent */
    VAlign v;      /**< Anchor on the y axis in a DirectionNone parent */
    float x;       /**< Offset from the anchor on the x axis */
    float y;       /**< Offset from the anchor on the y axis */
});

/** Size the layout computed for an element, in logical pixels */
ECS_STRUCT(LayoutBounds, {
    float width;  /**< Computed width */
    float height; /**< Computed height */
});

/** Makes an element take the size of a surface in points, the root of a layout that fills the
 * screen.
 *
 * Before the layout runs, the LayoutItem of the element is set to a fixed Surface.width by
 * Surface.height, so the tree follows a resized surface. A missing surface gives size zero.
 */
ECS_STRUCT(LayoutFill, {
    ecs_entity_t surface; /**< Surface whose size in points the element takes */
});

/** Register the box.layout module.
 *
 * @param world The world
 */
void BoxLayoutImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
