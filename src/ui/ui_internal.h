/**
 * @file ui/ui_internal.h
 * @brief Private components and helpers of the box.ui module.
 */

#ifndef BOX_UI_INTERNAL_H
#define BOX_UI_INTERNAL_H

#include <box.h>
#include <string.h>
#include "../render/render_internal.h"

/** View, camera and layer of a UI tree, added with Ui */
typedef struct UiImpl {
    ecs_entity_t view;   /**< View that draws the tree */
    ecs_entity_t camera; /**< Camera of the view, 0 while the tree has no view */
    ecs_entity_t layer;  /**< Layer every node of the tree joins */
    int32_t count;       /**< Nodes the last walk of the tree numbered */
    float height;        /**< Logical pixels the view showed after the last update */
    float width;         /**< Logical pixels the view showed across after the last update */
    bool owned;          /**< The module created the view and its camera */
    bool dirty;          /**< The tree must be walked and numbered again */
    bool relayout;       /**< The tree must be laid out again */
} UiImpl;

/** Place of an entity in the UI tree that holds it */
typedef struct UiNode {
    ecs_entity_t root;  /**< Root of the tree, the entity that has Ui */
    ecs_entity_t layer; /**< Layer the node joined, of its tree or of a group above it */
    int32_t order;      /**< Index of the node in the walk of its tree */
} UiNode;

/** Subtree the next layout of a tree has to visit again.
 *
 * A change leaves a mark that names the node it was made on, and the layout
 * resolves that node to the nearest ancestor whose box the change cannot
 * resize, which is the node its pass starts at.
 */
typedef struct UiMark {
    ecs_entity_t root; /**< Root of the tree the node belongs to */
    ecs_entity_t node; /**< Node the change was made on, or the resolved subtree */
    int32_t order;     /**< Place the last walk gave the node, once it is resolved */
} UiMark;

/** Node a walk still has to visit, with the place the walk gave its parent */
typedef struct UiVisit {
    ecs_entity_t e;  /**< The node */
    int32_t parent;  /**< Place of the parent, -1 for the root of the tree */
    bool aux;        /**< The node is a quad the module placed itself */
    bool composite;  /**< The node is the quad that composites the subtree of its parent */
} UiVisit;

/** Place of one node in the walk of its tree and the box the layout gave it.
 *
 * Places are collected in pre-order. The layout measures over them backward and
 * places them forward.
 */
typedef struct UiPlace {
    ecs_entity_t e;   /**< The node */
    int32_t parent;   /**< Place of the parent, -1 for the root of the tree */
    Layout layout;    /**< Layout of the node, zeroed when it has none */
    Anchor anchor;    /**< Anchor of the node, zeroed when it has none */
    WorldAnchor world; /**< World anchor of the node, zeroed when it has none */
    bool has_layout;  /**< The node arranges its children */
    bool has_anchor;  /**< The node is pinned to its parent */
    bool has_world;   /**< The node is pinned to the projection of a world point */
    bool has_rect;    /**< The node has a rectangle the box resizes */
    bool has_text;    /**< The node holds a string the pass draws */
    bool has_shadow;  /**< The node casts a box shadow the pass draws behind it */
    bool aux;         /**< The node is a quad the module placed, not one the pass places */
    bool composite;   /**< The node is the quad that composites the subtree of its parent */
    int32_t kids;     /**< Children without an opacity of their own */
    int32_t heirs;    /**< Children of the node that composite a subtree of their own */
    int32_t depth;    /**< Groups between the node and the root of its tree */
    ecs_entity_t layer; /**< Layer the node joins, written by the numbering */
    float x;          /**< X of the centre of the box in the tree */
    float y;          /**< Y of the centre of the box in the tree */
    float width;      /**< Width of the box */
    float height;     /**< Height of the box */
    float local_x;    /**< X the node was given relative to its parent */
    float local_y;    /**< Y the node was given relative to its parent */
    float sum_width;  /**< Width of the children the layout places */
    float sum_height; /**< Height of the children the layout places */
    float widest_child;  /**< Width of the widest of those children */
    float tallest_child; /**< Height of the tallest of those children */
    float cursor;     /**< Place along the axis the next child takes */
    int32_t flow;     /**< Children the layout places */
    float fill_width;  /**< Width a Fill resolved for the node, 0 when none did */
    float fill_height; /**< Height a Fill resolved for the node, 0 when none did */
    float content_width;  /**< Width the content of the node takes, its box ignored */
    float content_height; /**< Height the content of the node takes, its box ignored */
    Scroll scroll;     /**< Scroll of the node with its clamped offset, zeroed when it has none */
    bool has_scroll;   /**< The node clips what it holds and moves it by an offset */
    float clip_min[2]; /**< Left and bottom edge the node itself is clipped to */
    float clip_max[2]; /**< Right and top edge the node itself is clipped to */
    bool clipped;      /**< An element above the node clips it */
    float pad_left;    /**< Padding of the left side of the box */
    float pad_right;   /**< Padding of the right side of the box */
    float pad_top;     /**< Padding of the top side of the box */
    float pad_bottom;  /**< Padding of the bottom side of the box */
    float grow_width;  /**< Weight the width grows with, 0 when it does not */
    float grow_height; /**< Weight the height grows with, 0 when it does not */
    float wrap;        /**< Width the lines of a text wrap at, 0 when they do not */
    bool fit_width;    /**< The width of the box came from its content */
    bool fit_height;   /**< The height of the box came from its content */
    bool wraps;        /**< The node is a text that wraps at the width its parent leaves */
    bool shrinks;      /**< The width of the node gives way to the width its parent leaves */
    int32_t child;     /**< Place of the first child, -1 when there is none */
    int32_t sibling;   /**< Place of the next sibling, -1 when there is none */
} UiPlace;

/** Last projection of a world anchored element, written by the layout.
 *
 * The pass after the transforms of a frame compares a fresh projection with
 * it, so a tree is only laid out again when an anchored element moved.
 */
typedef struct WorldAnchorImpl {
    float x;      /**< X the projection landed on, in logical pixels */
    float y;      /**< Y the projection landed on, in logical pixels */
    bool visible; /**< The point was in front of the camera and inside the view */
} WorldAnchorImpl;

/** Module wide registry of box.ui, stored as a singleton */
typedef struct UiState {
    ecs_vec_t roots;    /**< ecs_entity_t roots of one update, collected first */
    ecs_vec_t stack;    /**< UiVisit nodes a walk still has to visit */
    ecs_vec_t nodes;    /**< ecs_entity_t children of the node a walk visits */
    ecs_vec_t places;   /**< UiPlace of every node of the tree being walked */
    ecs_vec_t marks;    /**< UiMark of every node changed since the last update */
    ecs_vec_t subtrees; /**< UiMark the layout of one tree visits, in tree order */
    ecs_vec_t scrolled; /**< int32_t places of the walk with a Scroll, found by the measure */
    ecs_query_t *hits;  /**< Nodes with a rectangle the cursor is resolved against */
    int32_t trees;      /**< Trees the world holds, zero skips the observers */
    int32_t texts;      /**< Text elements the world holds, zero skips the text pass */
    int32_t shadows;    /**< Box shadows the world holds, zero skips the shadow pass */
    int32_t groups;     /**< Subtrees that composite, zero skips their bookkeeping */
    int32_t scrolls;    /**< Elements that clip and scroll, zero skips the scroll pass */
    int32_t grows;      /**< Elements with a Grow, zero skips reading it */
    int32_t orders;     /**< Elements with an Order, zero skips sorting the children */
    int32_t opacities;  /**< Opacity components the world holds, zero skips the group pass */
    ecs_entity_t shadow_prefab; /**< Prefab the shadow quads take their material from */
    bool busy;          /**< An update runs, the observers mark no tree */
    bool instanced;     /**< A node gained a shape, its depth bias is missing */
    bool respawn;       /**< A pass made or dropped quads, collect the tree again */
    bool unclip;        /**< A Scroll was removed, the clips it left must be dropped */
    bool anchors_moved; /**< A world anchored element moved after the transforms of the frame */
} UiState;

/** One glyph of a font, rasterized into the atlas of that font */
typedef struct box_ui_glyph_t {
    float advance;   /**< Logical pixels the pen moves after the glyph */
    float bearing_x; /**< Logical pixels from the pen to the left of the bitmap */
    float top;       /**< Logical pixels from the baseline down to the top of the bitmap */
    int32_t x;       /**< Column of the bitmap in the atlas */
    int32_t y;       /**< Row of the bitmap in the atlas, from its top */
    int32_t width;   /**< Width of the bitmap in texels */
    int32_t height;  /**< Height of the bitmap in texels */
} box_ui_glyph_t;

/** Glyph atlas of one font file at one em size, grown as glyphs arrive.
 *
 * Glyphs are packed on shelves. When the last shelf is full the atlas doubles, which
 * changes every uv and raises the epoch so elements rebuild their quads.
 */
typedef struct box_ui_atlas_t {
    char *path;           /**< Font file as the component spells it */
    float size;           /**< Em size of the font in logical pixels */
    float device;         /**< Device pixels one logical pixel of the atlas covers */
    float raster;         /**< Em size in device pixels the glyphs are rasterized at */
    void *font;           /**< stbtt_fontinfo of the parsed file */
    uint8_t *data;        /**< Bytes of the file, which the parsed font points into */
    uint8_t *coverage;    /**< One byte per texel of the atlas, rows from the top */
    ecs_map_t glyphs;     /**< box_ui_glyph_t* keyed by code point */
    ecs_entity_t texture; /**< Entity that holds the uploaded atlas */
    ecs_entity_t prefab;  /**< Prefab the glyph quads inherit their material from */
    int32_t edge;         /**< Edge length of the atlas in texels */
    int32_t pen_x;        /**< Column the next glyph of the shelf is written at */
    int32_t pen_y;        /**< Row the shelf being filled starts at */
    int32_t shelf;        /**< Height of the shelf being filled */
    uint32_t epoch;       /**< Times the atlas grew, part of the fingerprint of a build */
    float scale;          /**< Factor from font units to device pixels */
    float ascent;         /**< Logical pixels from the baseline to the top of the line */
    float descent;        /**< Logical pixels from the baseline to the bottom of the line */
    float line_gap;       /**< Logical pixels between a line and the next top */
    bool dirty;           /**< The atlas gained glyphs and has to be uploaded */
    bool failed;          /**< The font file could not be read or parsed */
} box_ui_atlas_t;

/** One glyph the layout of a string placed in the box of its element */
typedef struct box_ui_quad_t {
    const box_ui_glyph_t *glyph; /**< Glyph of the atlas the quad shows */
    float x;                     /**< Logical pixels from the left of the box to the bitmap */
    float y;                     /**< Logical pixels from the top of the box to the bitmap */
    int32_t byte;                /**< Offset of the glyph in the string of the element */
} box_ui_quad_t;

/** Size a string takes when it is laid out in a box */
typedef struct box_ui_extent_t {
    float width;   /**< Width of the widest line */
    float height;  /**< Height of the lines together */
    int32_t lines; /**< Lines the string was broken into */
} box_ui_extent_t;

/** Glyph quads of a text element, added with Text */
typedef struct TextImpl {
    ecs_vec_t quads; /**< ecs_entity_t of the glyph elements the last build made */
    uint64_t hash;   /**< Fingerprint of what the last build drew */
    float width;     /**< Width the last measure found */
    float height;    /**< Height the last measure found */
} TextImpl;

/** Font atlases and the scratch space of the text pass, stored as a singleton */
typedef struct UiFonts {
    ecs_vec_t atlases; /**< box_ui_atlas_t* of every font file and size in use */
    ecs_vec_t quads;   /**< box_ui_quad_t of the string being laid out */
    float device;      /**< Device pixels one logical pixel covers, 0 until a view said */
    bool grown;        /**< An atlas grew, every tree has to lay out again */
} UiFonts;

/** View, target and quad of a subtree that composites as one image.
 *
 * The view renders the subtree into a target the size of its Bounds, and the quad shows
 * it in the parent layer at the place and depth of the node.
 */
typedef struct GroupImpl {
    ecs_entity_t target; /**< RenderTarget the subtree is rendered into */
    ecs_entity_t view;   /**< View that renders the subtree */
    ecs_entity_t camera; /**< Orthographic camera of that view */
    ecs_entity_t layer;  /**< Layer the subtree joins instead of the layer above it */
    ecs_entity_t quad;   /**< Quad that composites the target in the layer above */
    int32_t width;       /**< Width of the target in pixels */
    int32_t height;      /**< Height of the target in pixels */
} GroupImpl;

/** Content size and offset range of a scrolled element, added with Scroll.
 *
 * Written by the layout. The wheel reads it to find the innermost scrollable ancestor,
 * and it keeps an offset at the end of the content there while the content grows.
 */
typedef struct ScrollImpl {
    float content_width;  /**< Width the content of the element takes */
    float content_height; /**< Height the content of the element takes */
    float max_x;          /**< Largest offset the x axis takes, 0 when it does not scroll */
    float max_y;          /**< Largest offset the y axis takes, 0 when it does not scroll */
    bool measured;        /**< The layout measured the element at least once */
} ScrollImpl;

/** Caret and history of a text input, added with TextInput */
typedef struct TextInputImpl {
    ecs_vec_t history;  /**< char* of the lines the input submitted, oldest first */
    ecs_entity_t caret; /**< Quad that shows the caret, 0 until the input drew one */
    int32_t walk;       /**< Line of the history the up and down arrows stand on */
    char *stash;        /**< Line that was being typed when the walk started */
    float offset;       /**< Pixels from the left of the box to the caret */
    float height;       /**< Height the caret was given */
} TextInputImpl;

/** Binding, session and parts of the in game console, stored as a singleton */
typedef struct ConsoleImpl {
    ecs_entity_t action;  /**< Action the grave key fires, which flips the console */
    ecs_entity_t submit;  /**< Action the console fires when it runs a line */
    ecs_entity_t map;     /**< Input map that holds the binding of the grave key */
    ecs_entity_t input;   /**< Element of the tree that holds the line being typed */
    ecs_entity_t log;     /**< Element of the tree that shows the scrollback */
    ecs_vec_t captured;   /**< ecs_entity_t of the input maps the console disabled */
    int32_t shown;        /**< Lines of the session log the scrollback shows */
    bool loaded;          /**< The console script was loaded */
    bool open;            /**< The console was open after the last update */
    uint8_t *styles;      /**< box_terminal_style_t of every byte of the scrollback text */
    int32_t style_count;  /**< Bytes in styles */
} ConsoleImpl;

/** Quad the shadow pass drew for an element, added with BoxShadow */
typedef struct ShadowImpl {
    ecs_entity_t quad; /**< The quad, 0 until the pass drew one */
    uint64_t hash;     /**< Fingerprint of what the last build drew */
} ShadowImpl;

extern ECS_COMPONENT_DECLARE(UiImpl);
extern ECS_COMPONENT_DECLARE(UiNode);
extern ECS_COMPONENT_DECLARE(UiState);
extern ECS_TAG_DECLARE(UiGlyph);
extern ECS_TAG_DECLARE(UiShadow);
extern ECS_TAG_DECLARE(UiComposite);
extern ECS_TAG_DECLARE(UiQuad);
extern ECS_COMPONENT_DECLARE(ShadowImpl);
extern ECS_COMPONENT_DECLARE(GroupImpl);
extern ECS_COMPONENT_DECLARE(ScrollImpl);
extern ECS_COMPONENT_DECLARE(TextImpl);
extern ECS_COMPONENT_DECLARE(TextInputImpl);
extern ECS_COMPONENT_DECLARE(ConsoleImpl);
extern ECS_COMPONENT_DECLARE(UiFonts);
extern ECS_COMPONENT_DECLARE(WorldAnchorImpl);
extern ECS_TAG_DECLARE(UiWorldHidden);

/** The Rect of the node was written by the layout, so it is not a box of its own */
extern ECS_TAG_DECLARE(UiContentRect);

/** Direct pointer to the registry singleton, never a deferred copy.
 *
 * @param world The world
 * @return The registry
 */
UiState* box_ui_state(
    ecs_world_t *world);

/** Root of the UI tree an entity belongs to.
 *
 * @param world The world
 * @param e The entity
 * @return The root, 0 when the entity is not in a tree
 */
ecs_entity_t box_ui_root(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Mark the tree of an entity so the next update walks it again.
 *
 * @param world The world
 * @param e An entity of the tree, the root or one of its descendants
 */
void box_ui_mark(
    ecs_world_t *world,
    ecs_entity_t e);

/** Mark the tree of an entity so the next update lays the entity out again.
 *
 * The mark names the entity, not its tree, so the update lays out the subtree
 * around it instead of everything the root holds.
 *
 * @param world The world
 * @param e An entity of the tree, the root or one of its descendants
 */
void box_ui_mark_layout(
    ecs_world_t *world,
    ecs_entity_t e);

/** Give a tree its view, its camera and its layer, or bind the authored view.
 *
 * @param world The world
 * @param ui The Ui component of the root
 * @param impl The private state of the root
 */
void box_ui_bind(
    ecs_world_t *world,
    Ui *ui,
    UiImpl *impl);

/** Release the view, camera and layer a tree owns.
 *
 * @param world The world, the stage when this runs in an observer
 * @param impl The private state of the root
 */
void box_ui_unbind(
    ecs_world_t *world,
    UiImpl *impl);

/** Update the views and the node order of every tree of the world.
 *
 * @param world The world, the stage when this runs in a system
 */
void box_ui_update(
    ecs_world_t *world);

/** Register the observers that mark a tree when it changes.
 *
 * @param world The world
 */
void box_ui_tree_init(
    ecs_world_t *world);

/** Device pixels one logical pixel of a view covers.
 *
 * The scene framebuffer of the surface, or its window when the scene is not
 * scaled, over the logical height the view shows; the group targets and the
 * font atlases share the factor with the view.
 *
 * @param surface The surface of the world
 * @param height Logical pixels the view of the tree shows vertically
 * @return The factor, one when the surface says nothing about its pixels
 */
float box_ui_view_device(
    const Surface *surface,
    float height);

/** Point the camera of a tree at its plane and size it in logical pixels.
 *
 * @param world The world
 * @param ui The Ui component of the root
 * @param impl The private state of the root
 */
void box_ui_sync(
    ecs_world_t *world,
    const Ui *ui,
    UiImpl *impl);

/** Drop the node data of an entity and its descendants, which left their tree.
 *
 * @param world The world, the stage when this runs in an observer
 * @param e The entity that left the tree
 */
void box_ui_drop(
    ecs_world_t *world,
    ecs_entity_t e);

/** Walk the tree of a root, number its nodes, join them and lay them out.
 *
 * The private state of the root is copied and written back at the end. The layout runs
 * before the numbering so node instances exist when the bias is set. A tree that only
 * moved is not numbered again and one that only changed shape is not laid out again.
 *
 * A layout-only pass lays out the marked subtrees and numbers nothing. A pass that
 * made or dropped a node falls back to the walk of the whole tree.
 *
 * @param world The world
 * @param e The root of the tree
 */
void box_ui_walk(
    ecs_world_t *world,
    ecs_entity_t e);

/** Give every place of the collected tree its box and its Bounds.
 *
 * Sizes every node, then places layout children and anchored nodes, writing their
 * Position and the Rect of layout-sized nodes. The first place is the tree root or a
 * subtree root, which keeps the Bounds its parent pass gave it.
 *
 * @param world The world
 * @param root The root of the tree being walked
 */
void box_ui_layout(
    ecs_world_t *world,
    ecs_entity_t root);

/** Direct pointer to the font registry singleton, never a deferred copy.
 *
 * @param world The world
 * @return The registry
 */
UiFonts* box_ui_fonts(
    ecs_world_t *world);

/** Atlas of a font file at an em size, parsed and opened the first time.
 *
 * Rasterized at the em size times the device scale of the views. An atlas made for
 * another scale is rasterized again, which raises its epoch and relays every tree.
 *
 * @param world The world
 * @param path The font file, empty or NULL for the font the engine ships
 * @param size The em size in logical pixels, 0 for BOX_UI_FONT_SIZE
 * @return The atlas, which carries its failed flag when the file was unreadable
 */
box_ui_atlas_t* box_ui_atlas(
    ecs_world_t *world,
    const char *path,
    float size);

/** Atlas an element draws with, from its Font, that of an ancestor or the default.
 *
 * @param world The world
 * @param e The element
 * @return The atlas
 */
box_ui_atlas_t* box_ui_atlas_of(
    ecs_world_t *world,
    ecs_entity_t e);

/** Glyph of a code point, rasterized into the atlas the first time it is asked.
 *
 * @param world The world
 * @param atlas The atlas
 * @param codepoint The code point
 * @return The glyph, or NULL when the font has none and no atlas holds room
 */
const box_ui_glyph_t* box_ui_atlas_glyph(
    ecs_world_t *world,
    box_ui_atlas_t *atlas,
    int32_t codepoint);

/** Upload the atlases that gained glyphs and give them their texture entity.
 *
 * @param world The world
 */
void box_ui_atlas_upload(
    ecs_world_t *world);

/** Release the atlases of a world.
 *
 * @param fonts The registry
 */
void box_ui_atlas_fini(
    UiFonts *fonts);

/** Distance between two baselines of an element, from its Text or its font.
 *
 * @param atlas The atlas of the element
 * @param text The Text of the element
 * @return The line height in pixels
 */
float box_ui_text_line_height(
    const box_ui_atlas_t *atlas,
    const Text *text);

/** Lay a string out in a box, wrapping it and filling the quads of the registry.
 *
 * @param world The world
 * @param atlas The atlas the glyphs come from
 * @param text The Text of the element
 * @param wrap Width the lines are broken at, 0 to break on newlines only
 * @param box Width the lines are aligned in, 0 to align them in their own width
 * @param out Receives the size the string takes
 */
void box_ui_text_run(
    ecs_world_t *world,
    box_ui_atlas_t *atlas,
    const Text *text,
    float wrap,
    float box,
    box_ui_extent_t *out);

/** Measure the text of a node into its place and remember it for the build.
 *
 * @param world The world
 * @param p The place of the node
 */
void box_ui_text_measure(
    ecs_world_t *world,
    UiPlace *p);

/** Wrap a text element again at the width the layout gave it.
 *
 * The element takes the width, the lines wrap at that width inside its
 * padding and a height that came from the lines follows them.
 *
 * @param world The world
 * @param p The place of the node
 * @param width Width the layout gave the element
 */
void box_ui_text_rewrap(
    ecs_world_t *world,
    UiPlace *p,
    float width);

/** Give every text node of the collected tree the glyph quads of its string.
 *
 * @param world The world
 */
void box_ui_text_build(
    ecs_world_t *world);

/** Register the components, the hooks and the observers of the text pass.
 *
 * @param world The world
 */
void box_ui_text_init(
    ecs_world_t *world);

/** Give every element of the collected tree the quad of its box shadow.
 *
 * @param world The world
 */
void box_ui_shadow_build(
    ecs_world_t *world);

/** Register the components, the hooks and the observers of the shadow pass.
 *
 * @param world The world
 */
void box_ui_shadow_init(
    ecs_world_t *world);

/** Give every transparent subtree of the collected tree its view and its quad.
 *
 * @param world The world
 * @param root The root of the tree being walked
 */
void box_ui_group_build(
    ecs_world_t *world,
    ecs_entity_t root);

/** Release the view, the target and the quad of a subtree that composited.
 *
 * @param world The world, the stage when this runs in an observer
 * @param group The group state of the node
 */
void box_ui_group_release(
    ecs_world_t *world,
    GroupImpl *group);

/** Project the world point of an anchor into the logical pixels of a tree.
 *
 * @param world The world
 * @param anchor The anchor
 * @param root The root of the tree of the element
 * @param x Receives the x of the projection, in logical pixels of the view
 * @param y Receives the y of the projection, in logical pixels of the view
 * @return Whether the point is in front of the camera and inside the view
 */
bool box_ui_world_project(
    ecs_world_t *world,
    const WorldAnchor *anchor,
    ecs_entity_t root,
    float *x,
    float *y);

/** Place a world anchored node on its projection and hide it when it has none.
 *
 * @param world The world
 * @param root The root of the tree
 * @param p The place of the node, its x and y are written
 */
void box_ui_world_pin(
    ecs_world_t *world,
    ecs_entity_t root,
    UiPlace *p);

/** Register the systems that follow world anchors after the transforms moved.
 *
 * @param world The world
 */
void box_ui_world_init(
    ecs_world_t *world);

/** Load the script widget library of BOX_UI_WIDGETS into box.ui.widgets.
 *
 * @param world The world
 */
void box_ui_widgets_load(
    ecs_world_t *world);

/** Register the components and the observers of the group pass.
 *
 * @param world The world
 */
void box_ui_group_init(
    ecs_world_t *world);

/** Clamp the offset of a scrolled node and read it into its place.
 *
 * The pass runs while the node is measured, after its box and the size of its
 * content are known and before its children are placed, so the offset the
 * children are moved by is the one the frame ends with.
 *
 * @param world The world
 * @param p The place of the node
 */
void box_ui_scroll_resolve(
    ecs_world_t *world,
    UiPlace *p);

/** Give every collected node the clip rectangle the elements above it leave.
 *
 * @param world The world
 */
void box_ui_scroll_clip(
    ecs_world_t *world);

/** Move the innermost scrollable element under the cursor by the wheel.
 *
 * @param world The world
 */
void box_ui_scroll_update(
    ecs_world_t *world);

/** Register the components, the hooks and the observers of the scroll pass.
 *
 * @param world The world
 */
void box_ui_scroll_init(
    ecs_world_t *world);

/** Register the hit test query and install it as the source of a pick.
 *
 * @param world The world
 */
void box_ui_hit_init(
    ecs_world_t *world);

/** Register the text.measure, text.wrap and text.label script functions.
 *
 * @param world The world
 * @param module The box.ui module entity
 */
void box_ui_functions_init(
    ecs_world_t *world,
    ecs_entity_t module);

/** Let every focused text input of the world edit its line with the frame.
 *
 * @param world The world
 */
void box_ui_text_input_update(
    ecs_world_t *world);

/** Register the components, the hooks and the observers of the text input.
 *
 * @param world The world
 */
void box_ui_text_input_init(
    ecs_world_t *world);

/** Open, close and fill the in game console of the world.
 *
 * @param world The world
 */
void box_ui_console_update(
    ecs_world_t *world);

/** Run the line the console submitted and write its scrollback.
 *
 * The pass runs after the text input of the frame, so a line that was typed and
 * submitted in one frame is run in that frame.
 *
 * @param world The world
 */
void box_ui_console_sync(
    ecs_world_t *world);

/** Color a byte of the scrollback is drawn in.
 *
 * @param world The world
 * @param element The element whose text holds the byte
 * @param byte Offset of the byte in the text of the element
 * @param fallback Color returned for an element that is not the console log
 * @return The TerminalColors color of the style of the byte, or fallback
 */
Rgb box_ui_console_color(
    const ecs_world_t *world,
    ecs_entity_t element,
    int32_t byte,
    Rgb fallback);

/** Register the components, the hooks and the observers of the console.
 *
 * @param world The world
 */
void box_ui_console_init(
    ecs_world_t *world);

#endif
