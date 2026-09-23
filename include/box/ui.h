/**
 * @file box/ui.h
 * @brief Ui component: the root of a retained UI tree and the view it draws in.
 *
 * A Ui entity roots a tree drawn by an orthographic view in logical pixels over
 * the scene, origin centered, y up. Nodes draw in walk order over their parent,
 * and a layout pass writes Position and Bounds of changed trees only.
 *
 * The tree answers the pick of its view against node Bounds and raises the same
 * events as box.picking. Widgets, including the console, are written in script;
 * the engine only fills ConsoleLog and TextInput elements.
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
    bool passthrough;  /**< An event no element took goes on to the scene behind the tree */
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
 * A zero side takes the Rect side, else the content size. Padding insets the
 * box and h and v align the children, as a group along the axis and one by one
 * across it. A Rect on the element is resized to the box.
 *
 * A zero padding side takes padding. The minimum and maximum bound the box
 * however it was sized, a zero maximum bounds nothing, and a text element wraps
 * at its maximum width.
 */
ECS_STRUCT(Layout, {
    Direction direction;  /**< Axis the children are placed along */
    float padding;        /**< Logical pixels between the box and its children */
    float spacing;        /**< Logical pixels between two children */
    float width;          /**< Width of the box, 0 takes the Rect or the content */
    float height;         /**< Height of the box, 0 takes the Rect or the content */
    Align h;              /**< Alignment on the x axis */
    VAlign v;             /**< Alignment on the y axis */
    float padding_left;   /**< Padding of the left side, 0 takes padding */
    float padding_right;  /**< Padding of the right side, 0 takes padding */
    float padding_top;    /**< Padding of the top side, 0 takes padding */
    float padding_bottom; /**< Padding of the bottom side, 0 takes padding */
    float min_width;      /**< Smallest width of the box, 0 for none */
    float max_width;      /**< Largest width of the box, 0 for none */
    float min_height;     /**< Smallest height of the box, 0 for none */
    float max_height;     /**< Largest height of the box, 0 for none */
});

/** Place of an element among the children of a layout.
 *
 * A row or a column places its children in child order; a child with an Order
 * goes before every child with a higher value and after every child with a
 * lower one, children without one counting as zero.
 *
 * Rows a script builds from a list and adds to later keep the order of the list.
 */
ECS_STRUCT(Order, {
    int32_t value; /**< Place among the siblings, lower first */
});

/** Point of the parent an anchored element attaches to on the x axis */
ECS_ENUM(AttachH, {
    AttachHSame = 0,   /**< The edge the element is pinned by, inside the padding */
    AttachHLeft = 1,   /**< The left edge of the box of the parent */
    AttachHCenter = 2, /**< The middle of the box of the parent */
    AttachHRight = 3   /**< The right edge of the box of the parent */
});

/** Point of the parent an anchored element attaches to on the y axis */
ECS_ENUM(AttachV, {
    AttachVSame = 0,   /**< The edge the element is pinned by, inside the padding */
    AttachVTop = 1,    /**< The top edge of the box of the parent */
    AttachVMiddle = 2, /**< The middle of the box of the parent */
    AttachVBottom = 3  /**< The bottom edge of the box of the parent */
});

/** Pins an element to an edge or the centre of its parent, with an offset.
 *
 * The element is pinned to the edges h and v name, inside the parent padding,
 * and x and y move it inward; a centered axis moves x right and y up. It takes
 * no room in the row or column of its parent.
 *
 * With a parent_h or parent_v other than Same, that edge attaches to the named
 * point of the parent box, ignoring padding, and the offset moves x right, y up.
 */
ECS_STRUCT(Anchor, {
    Align h;          /**< Edge of the element pinned on the x axis */
    VAlign v;         /**< Edge of the element pinned on the y axis */
    float x;          /**< Offset from that edge on the x axis */
    float y;          /**< Offset from that edge on the y axis */
    AttachH parent_h; /**< Point of the parent the edge attaches to on the x axis */
    AttachV parent_v; /**< Point of the parent the edge attaches to on the y axis */
});

/** Follows the screen projection of a world entity or point through a view.
 *
 * The element sits at the projected world position plus offset and its tree is
 * laid out again when that moves. Behind the camera or off view it gets Hidden,
 * removed again by the anchor. It takes no room in the row or column of its parent.
 */
ECS_STRUCT(WorldAnchor, {
    ecs_entity_t entity; /**< Entity followed, 0 projects the offset as a world point */
    vec3 offset;         /**< World units added to the position of the entity */
    ecs_entity_t view;   /**< View whose camera projects, 0 takes the active camera */
});

/** Lets the cursor pass through an element to the element behind it.
 *
 * A caption or an indicator inside a widget carries it, so the widget itself
 * is the element under the cursor, holds Hovered and receives the events,
 * instead of the part the cursor happens to be on.
 */
extern ECS_TAG_DECLARE(HitThrough);

/** Sizes a side of an element as a fraction of the matching side of its parent.
 *
 * A zero side leaves the Layout or Rect side alone. The parent needs a known
 * box: an explicit Layout size inside its padding, or the view size for a root;
 * otherwise the side stays zero.
 */
ECS_STRUCT(Fill, {
    float width;  /**< Fraction of the width of the parent, 0 leaves it alone */
    float height; /**< Fraction of the height of the parent, 0 leaves it alone */
});

/** Grows a side of an element into the room its parent leaves, by weight.
 *
 * Along the parent row or column the element takes a share of the free room by
 * weight, up to its Layout maximum. Across that axis, or without one, a weight
 * above zero stretches to the parent box. A content sized parent leaves no room.
 */
ECS_STRUCT(Grow, {
    float width;  /**< Weight of the width, 0 does not grow it */
    float height; /**< Weight of the height, 0 does not grow it */
});

/** What an element does with the content that does not fit its box */
ECS_ENUM(Overflow, {
    OverflowVisible = 0, /**< The content sticks out of the box and is drawn */
    OverflowHidden = 1,  /**< The content is clipped, the wheel does not move it */
    OverflowScroll = 2,  /**< The content is clipped and the wheel always moves it */
    OverflowAuto = 3     /**< The content is clipped and the wheel moves it when it is larger */
});

/** Clips the content of an element to its box and moves it by an offset.
 *
 * A non Visible axis clips the subtree to the Bounds. The offset, in logical
 * pixels with x right and y down, moves the content but not anchored children.
 *
 * The framework clamps the offset to the content, and an offset at the end stays
 * at the end as the content grows. The wheel moves the innermost Scroll or
 * larger Auto axis under the cursor; Hidden clips but ignores the wheel.
 */
ECS_STRUCT(Scroll, {
    Overflow x;   /**< What the element does with content wider than its box */
    Overflow y;   /**< What the element does with content taller than its box */
    vec2 offset;  /**< Logical pixels the content is moved by, right and down */
});

/** Font a text element rasterizes its glyphs with, inherited by its subtree.
 *
 * The path is resolved against the engine search path. A text element takes its
 * own Font, else the nearest ancestor one, else BOX_UI_FONT at BOX_UI_FONT_SIZE.
 * One atlas is shared per file and size.
 */
ECS_STRUCT(Font, {
    char *path; /**< Font file, empty for the font the engine ships */
    float size; /**< Em size the glyphs are rasterized at, 0 for the default */
});

/** String drawn as one textured quad per glyph, sized by the layout pass.
 *
 * The element sizes to its text unless its Layout gives a size; a Layout width,
 * or with wrap the width the parent leaves, wraps the words. Glyphs are child
 * quads in the Rgb of the element, drawn unlit.
 *
 * An Opacity below one on an element with children composites its subtree into
 * one image clipped to its Bounds, so overlapping children do not show through
 * each other.
 */
ECS_STRUCT(Text, {
    char *value;       /**< The string, UTF-8 */
    float line_height; /**< Logical pixels between two baselines, 0 for the font default */
    Align align;       /**< Alignment of the lines inside the box of the element */
    bool wrap;         /**< The lines wrap at the width the parent leaves the element */
});

/** Blurred shadow drawn behind the rounded rectangle of an element.
 *
 * The shadow is a quad below the element, offset x right and y down, grown by
 * spread and faded over blur, with the corner radii of the Border. It takes no
 * layout room and a transparent color draws nothing.
 */
ECS_STRUCT(BoxShadow, {
    float x;      /**< Logical pixels the shadow is offset to the right */
    float y;      /**< Logical pixels the shadow is offset downward */
    float blur;   /**< Logical pixels the edge of the shadow fades out over */
    float spread; /**< Logical pixels the shadow grows by on every side */
    rgba color;   /**< Color of the shadow, its alpha the strength */
});

/** Rectangle an element covers, written by the layout pass and read only.
 *
 * In logical pixels of the tree view, centered at x, y like a Position, so it
 * spans x - width / 2 to x + width / 2 and y - height / 2 to y + height / 2.
 */
ECS_STRUCT(Bounds, {
    float x;      /**< X of the centre of the element */
    float y;      /**< Y of the centre of the element */
    float width;  /**< Width of the element */
    float height; /**< Height of the element */
});

/** Line of text an element lets the keyboard edit, with a caret of its own.
 *
 * The element shows value as its Text with a caret child. A focused input takes
 * the typed characters and edit keys; enter moves the line to submitted and
 * clears it. Nothing is taken while the window is unfocused.
 *
 * With history, up and down walk submitted lines and with complete, tab completes
 * a command name or entity path via box.command. One input holds the focus at a
 * time: focusing one, or calling text.focus(element), blurs every other input.
 */
ECS_STRUCT(TextInput, {
    char *value;     /**< The line being edited, UTF-8 */
    int32_t caret;   /**< Byte of the line the caret sits before */
    bool focused;    /**< The element takes the text and the edit keys of the frame */
    bool history;    /**< Up and down walk the lines that were submitted */
    bool complete;   /**< Tab completes a command name or an entity path */
    char *submitted; /**< Line the last enter submitted, empty when there is none */
});

/** The in game console, an overlay terminal the backtick key opens.
 *
 * Adding the singleton binds the grave key to toggle open. The first open loads
 * BOX_UI_CONSOLE, which builds the overlay and marks the ConsoleLog and
 * TextInput elements the engine fills.
 *
 * An open console disables every other input map. A submitted line is set as
 * TerminalInput on its session, whose log is the scrollback, and fires the
 * submit action.
 */
ECS_STRUCT(Console, {
    bool open;            /**< The overlay is shown and holds the keyboard */
    ecs_entity_t session; /**< Terminal session of the overlay, written by the engine */
    ecs_entity_t root;    /**< Root of the console tree, written by the engine */
});

/** Element of the console tree the engine writes the scrollback of its session into */
ECS_STRUCT(ConsoleLog, {
    int32_t lines; /**< Lines of the session log the element shows, 0 for the default */
});

/** Colors a console log element draws each style of its session output with */
ECS_STRUCT(TerminalColors, {
    Rgb text;   /**< Plain text */
    Rgb muted;  /**< De-emphasized text */
    Rgb accent; /**< Highlighted text */
    Rgb error;  /**< Error messages */
    Rgb path;   /**< Entity paths */
    Rgb type;   /**< Type names */
    Rgb number; /**< Numeric literals */
    Rgb string; /**< String literals */
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

/** Part of a step the shadow of an element sits behind that element */
#define BOX_UI_SHADOW_STEP (0.5f)

/** Pixels the target of a subtree that composites may span on one axis */
#define BOX_UI_GROUP_MAX (4096)

/** Changes one update may leave in a tree before it is laid out as a whole */
#define BOX_UI_MARK_MAX (256)

/** Subtrees one update may lay out before the tree is laid out as a whole */
#define BOX_UI_SUBTREE_MAX (64)

/** Script the framework installs, which the first open of the console loads */
#define BOX_UI_CONSOLE "etc/ui/console.flecs"

/** Script the widget library is loaded from at import, into box.ui.widgets */
#define BOX_UI_WIDGETS "etc/ui/widgets/widgets.flecs"

/** Name of the terminal session the console runs its lines on */
#define BOX_UI_CONSOLE_SESSION "console"

/** Input map that holds the binding of the grave key, the one an open console keeps */
#define BOX_UI_CONSOLE_MAP "console_map"

/** Lines of the session log a ConsoleLog element shows when it names none */
#define BOX_UI_CONSOLE_LINES (14)

/** Width of the caret of a text input in logical pixels */
#define BOX_UI_CARET_WIDTH (2.0f)

/** Logical pixels one unit of wheel movement scrolls a scrollable element by */
#define BOX_UI_SCROLL_STEP (24.0f)

/** Lines one text input keeps in its history */
#define BOX_UI_HISTORY_MAX (64)

/** What the UI holds of the pointer this frame, written by the hit test.
 *
 * A game that steers with the cursor reads it to stand back while the pointer is
 * on a panel.
 */
ECS_STRUCT(UiCapture, {
    bool mouse;           /**< The cursor is over an element of a tree */
    ecs_entity_t element; /**< Element the cursor is over, 0 when there is none */
});

/** Whether the UI holds the pointer this frame.
 *
 * @param world The world
 * @return Whether the cursor is over an element of a tree
 */
bool box_ui_mouse_captured(
    const ecs_world_t *world);

/** Element of a UI tree at a point in the plane the trees are laid out in.
 *
 * The point is in the logical pixels of the tree view. The answer is the deepest
 * node of the topmost subtree; nodes without a rectangle or clipped away by a
 * compositing ancestor are not hit.
 *
 * @param world The world
 * @param view The view whose trees are tested, 0 for the trees of every view
 * @param x X of the point
 * @param y Y of the point
 * @return The element under the point, 0 when no tree of the view covers it
 */
ecs_entity_t box_ui_hit(
    const ecs_world_t *world,
    ecs_entity_t view,
    float x,
    float y);

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
