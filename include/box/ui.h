/**
 * @file box/ui.h
 * @brief Ui component: the root of a retained UI tree and the view it draws in.
 *
 * An entity with a Ui component roots a UI tree: the entity and everything
 * below it is drawn by a view with an orthographic camera in logical pixels,
 * composited over the views of the 3D scene. The tree reuses Position, Rotation,
 * Scale and the 2D shapes, so nothing in the geometry path is UI specific.
 *
 * The view shows Ui.height logical pixels from top to bottom and as many across
 * as its aspect ratio asks for, with the origin in the middle of the view, x to
 * the right and y up, which is the orientation the rest of the engine draws in.
 * A height of zero follows the surface: the view then shows the points of the
 * window divided by Ui.scale, so the tree keeps its size in points when the
 * window is resized and when it moves to a display with another resolution
 * scale.
 *
 * Depth inside a tree comes from the order of the nodes in the walk of the tree
 * and not from their positions: every node is drawn over its parent and over
 * the subtrees of its earlier siblings, so a tree needs no authored z and
 * cannot z fight. The bias is a fixed step per node toward the camera, which
 * leaves room for BOX_UI_NODE_MAX nodes in one tree.
 *
 * A root needs no Position: a tree that has none sits at the origin of its
 * view, because the layout pass gives every node of a tree the Position its
 * place asks for and a node that was never moved the origin of its parent. A
 * node only has a world transform once its Position is set, so a node without
 * one would keep the zero matrix it was created with and take its subtree with
 * it.
 *
 * A tree is walked again when it is marked: setting Ui marks it, and so does
 * every change of the tree itself. Reordering the children of a node with
 * ecs_set_child_order() raises no event, so a tree that was reordered is marked
 * by setting Ui on its root again.
 *
 * Every node of a tree joins the layer of its tree, which is what keeps the two
 * apart: the UI view draws the members of that layer only, and every other view
 * hides them. A world whose only views are UI views still draws its active
 * camera, so a scene does not lose its image when it gains a UI.
 *
 * A layout pass runs after the walk and gives every node of a tree a Bounds,
 * the rectangle the node covers in the logical pixels of its view, with its
 * centre in x, y like a Position and a Rect. A node takes its size from the
 * Layout that sizes it, from its Rect, or from its content when it arranges
 * children and no size is given. A Layout places the children of a node in a
 * row or a column in child order, with padding around them, spacing between
 * them and an alignment on both axes, and writes the Position of every child it
 * places; a child with an Anchor is pinned to an edge or the centre of its
 * parent instead and takes no room in the row or the column. The pass runs for
 * a tree that changed and for no other, so a UI that stands still costs
 * nothing.
 *
 * A node with a Text is measured by that pass too: its glyphs are rasterized
 * into the atlas of its Font, the string is wrapped to the width its Layout
 * gives it and the element ends up as large as the lines it holds. The glyphs
 * are emitted as ordinary children of the element, one textured quad each, so
 * text takes the same 2D shape path as every other element and the renderer
 * knows nothing about it. The helpers text.measure, text.wrap and text.label
 * offer the same measuring, wrapping and building to a script.
 */

#ifndef BOX_UI_H
#define BOX_UI_H

#include "types.h"

#ifdef BOX_UI_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Roots a UI tree, which a view with an orthographic camera draws */
ECS_STRUCT(Ui, {
    ecs_entity_t view; /**< View that draws the tree, 0 creates one with a camera */
    float height;      /**< Logical pixels the view shows vertically, 0 follows the surface */
    float scale;       /**< Surface points one logical pixel covers, default 1 */
    int32_t priority;  /**< Order of a created view in the frame, default 1000 */
});

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

/** Arranges the children of an element in a row or a column, in child order.
 *
 * The box of the element is width by height, and a side that is zero takes the
 * matching side of the Rect of the element, or the size of its content when
 * that side is zero too: a row is then as wide as its children plus the spacing
 * between them and the padding around them, and a column as tall. The padding
 * insets the box on every side, the children are placed in the box that is
 * left, and h and v align them: along the axis of the layout the alignment
 * moves the whole group, across it every child on its own. An element that has
 * a Rect has it resized to the box, so the rectangle of a panel is what its
 * layout worked out.
 */
ECS_STRUCT(Layout, {
    Direction direction; /**< Axis the children are placed along */
    float padding;       /**< Logical pixels between the box and its children */
    float spacing;       /**< Logical pixels between two children */
    float width;         /**< Width of the box, 0 takes the Rect or the content */
    float height;        /**< Height of the box, 0 takes the Rect or the content */
    Align h;             /**< Alignment on the x axis */
    VAlign v;            /**< Alignment on the y axis */
});

/** Pins an element to an edge or the centre of its parent, with an offset.
 *
 * The element is placed against the edges h and v name, inside the padding of
 * the layout of its parent when it has one, and x and y move it from there
 * toward the inside of the parent. An element in the middle of an axis is moved
 * along that axis instead, x to the right and y up. An anchored element is not
 * part of the row or the column of its parent and does not add to its content.
 */
ECS_STRUCT(Anchor, {
    Align h;  /**< Edge of the parent the element is pinned to on the x axis */
    VAlign v; /**< Edge of the parent the element is pinned to on the y axis */
    float x;  /**< Offset from that edge on the x axis */
    float y;  /**< Offset from that edge on the y axis */
});

/** Font a text element rasterizes its glyphs with, inherited by its subtree.
 *
 * The path is resolved against the search path of the engine, so
 * `etc/fonts/B612-Regular.ttf` finds the font the framework installs as well as
 * one an application ships. A text element takes the Font of its own entity,
 * else the nearest one above it in the tree, else the font the engine ships at
 * BOX_UI_FONT_SIZE logical pixels. One atlas is rasterized per file and size
 * and is shared by every element that asks for it.
 */
ECS_STRUCT(Font, {
    char *path; /**< Font file, empty for the font the engine ships */
    float size; /**< Em size the glyphs are rasterized at, 0 for the default */
});

/** String drawn as one textured quad per glyph, sized by the layout pass.
 *
 * The element is as wide and as tall as the text it holds, unless its Layout
 * gives it a width or a height, and a Layout width also wraps the string: the
 * words that do not fit move to the next line and a newline in the value always
 * breaks. The lines are aligned inside the box by align, the first baseline
 * sits one ascent below its top and the next ones follow at line_height. The
 * glyphs themselves are children of the element, drawn over it like every other
 * node of the tree, in the Rgb of the element, which the build hands them as
 * the emissive color of an alpha blended quad so a glyph shows the color it was
 * given whatever the view lights it with.
 *
 * A blended quad of a UI view composites plainly: the view hands the renderer
 * the plain source over contribution of every alpha blended fragment instead of
 * the glass model the rest of the engine draws a transparent surface with, so a
 * glyph or an image lies over what is below it without refracting it and the
 * view needs no copy of the scene behind it.
 */
ECS_STRUCT(Text, {
    char *value;       /**< The string, UTF-8 */
    float line_height; /**< Logical pixels between two baselines, 0 for the font default */
    Align align;       /**< Alignment of the lines inside the box of the element */
});

/** Rectangle an element covers, written by the layout pass and read only.
 *
 * The rectangle is in the logical pixels of the view of its tree, with x and y
 * at the centre of the element like a Position and width and height the size
 * the layout gave it, so an element covers x - width / 2 to x + width / 2 and
 * y - height / 2 to y + height / 2.
 */
ECS_STRUCT(Bounds, {
    float x;      /**< X of the centre of the element */
    float y;      /**< Y of the centre of the element */
    float width;  /**< Width of the element */
    float height; /**< Height of the element */
});

/** Order in the frame a created UI view renders at, after the views of a scene */
#define BOX_UI_PRIORITY (1000)

/** Surface points one logical pixel of a tree covers by default */
#define BOX_UI_SCALE (1.0f)

/** Distance between the camera of a created UI view and the plane of the tree */
#define BOX_UI_DEPTH (1000.0f)

/** Near plane of the camera of a created UI view */
#define BOX_UI_NEAR (1.0f)

/** Far plane of the camera of a created UI view */
#define BOX_UI_FAR (2001.0f)

/** World units one node of a tree is drawn in front of the node before it */
#define BOX_UI_STEP (0.01f)

/** Nodes one tree can hold before its deepest node reaches the near plane */
#define BOX_UI_NODE_MAX (99900)

/** Font the engine ships, which a text element without a Font uses */
#define BOX_UI_FONT "etc/fonts/B612-Regular.ttf"

/** Em size a text element rasterizes at when its Font gives none */
#define BOX_UI_FONT_SIZE (16.0f)

/** Edge length of a glyph atlas before it grew, one texture size class */
#define BOX_UI_ATLAS_SIZE (256)

/** Register the box.ui module.
 *
 * @param world The world
 */
void BoxUiImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
