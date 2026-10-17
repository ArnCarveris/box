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
    SizeModeGrow = 2   /**< Take a share of the free space of the parent, weighted by value */
});

/** Size of an element on one axis.
 *
 * A growing element adds its fit size to the fit of its parent, takes a weighted share of the
 * free space along the main axis and stretches to the content box across or in DirectionNone.
 * A fitting element narrows to that box down to its content minimum, as Clay compresses.
 *
 * The content minimum is the widest word of a text that wraps, else the fit size. With
 * content_min a share or stretch never goes below it, as CSS flex items. A weight of 0 takes no
 * share and keeps the minimum. Fixed sizes never change.
 */
ECS_STRUCT(Sizing, {
    SizeMode mode;    /**< How the size is computed */
    float value;      /**< Size for SizeModeFixed, weight for SizeModeGrow, 1 in a new LayoutItem */
    float min;        /**< Smallest size, 0 for no limit */
    float max;        /**< Largest size, 0 for no limit */
    bool content_min; /**< A Grow size keeps the content minimum, true in a new LayoutItem */
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

/** Shifts the children of an element with a Layout to show another part of its content, like
 * CSS overflow: auto. A scroll view is a ClipChildren element with a LayoutScroll.
 *
 * The children are placed shifted by -x and -y logical pixels: a positive y shows content
 * further down, a positive x content further right. The layout applies the offset clamped to
 * 0..LayoutContent.max_x and 0..max_y and never writes the clamped value back, so a script owns
 * the offset. A change of the offset alone moves the children without measuring the tree again.
 *
 * Along an axis whose flag is set the children are not narrowed to the element: they keep their
 * fit size, grow children included, which only grow across that axis, and the element no
 * longer needs its content as its minimum. An axis whose flag is clear lays out as without a
 * LayoutScroll, so its offset only applies when the content minimum overflows the element.
 * Children of a DirectionNone element without a LayoutItem keep their position and do not
 * scroll; floating children (LayoutFloat) follow their anchor instead.
 */
ECS_STRUCT(LayoutScroll, {
    float x;         /**< Offset of the content to the left, in logical pixels */
    float y;         /**< Offset of the content upward, in logical pixels */
    bool horizontal; /**< The children keep their fit width */
    bool vertical;   /**< The children keep their fit height */
});

/** Size of the content of an element with a LayoutScroll, written by the layout.
 *
 * The content is the box the children take, measured as the fit size of the element from the
 * sizes the children got, including padding. The largest offsets keep the end of the content
 * in view and are 0 when the content fits.
 */
ECS_STRUCT(LayoutContent, {
    float width;  /**< Width of the content including padding */
    float height; /**< Height of the content including padding */
    float max_x;  /**< Largest LayoutScroll.x that keeps content in view */
    float max_y;  /**< Largest LayoutScroll.y that keeps content in view */
    float x;      /**< LayoutScroll.x the layout applied, clamped to 0..max_x */
    float y;      /**< LayoutScroll.y the layout applied, clamped to 0..max_y */
});

/** Places an element next to an anchor outside the flow of its parent, for dropdowns, popup
 * menus and tooltips.
 *
 * The element is left out of the fit size and the flow of its parent, which must have a Layout.
 * Its own h/v point sits on the anchor_h/anchor_v point of the anchor plus the x/y offset (y up,
 * as LayoutItem.y). A dropdown list has h: Left, v: Top, anchor_h: Left, anchor_v: Bottom.
 * The anchor is found through the Position of it and its ancestors, rotation and scale left out.
 *
 * It sizes itself as any element from its LayoutItem: fit, fixed, and a grow axis takes the size
 * of the anchor within min and max, so a list can be as wide as its button.
 *
 * With flip, an element that does not fit inside the root of its layout tree (the surface for a
 * LayoutFill root) along y is placed on the other side of the anchor when that fits better, its
 * y offset mirrored; along x it is shifted to stay inside.
 *
 * It draws above every non floating element of its scene, and picking agrees; floating elements
 * draw among each other in hierarchy order. ClipChildren ancestors of its parent do not clip it
 * or its descendants, a ClipChildren on the element itself clips its children. It follows its
 * anchor when the anchor moves, scrolls or resizes, in the same frame. A LayoutFloat on the root
 * of a layout tree does nothing.
 */
ECS_STRUCT(LayoutFloat, {
    ecs_entity_t anchor; /**< Element the float is placed against, 0 for its parent */
    Align h;             /**< Point of the float on the x axis that sits on the anchor point */
    VAlign v;            /**< Point of the float on the y axis that sits on the anchor point */
    Align anchor_h;      /**< Point of the anchor on the x axis */
    VAlign anchor_v;     /**< Point of the anchor on the y axis */
    float x;             /**< Offset from the anchor point on the x axis */
    float y;             /**< Offset from the anchor point on the y axis, up */
    bool flip;           /**< Keep the float inside the root of its layout tree */
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

/** Clips the Rect, Circle, Line, Border, Image and Text of the descendants of an element to its
 * area, like CSS overflow: hidden; the element itself is not clipped.
 *
 * The area is its LayoutBounds, else its Rect, centered in its local XY plane and rounded by its
 * Border.radius. Nested clips intersect, anti-aliased per pixel in screen, world and texture UI,
 * and picking misses outside the area. 3D shapes below the element are not clipped.
 */
extern ECS_TAG_DECLARE(ClipChildren);

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
