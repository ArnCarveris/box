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
 *
 * A tree answers the pick of its view itself, because a UI element has no
 * collider: the cursor is resolved against the Bounds of the nodes of the view
 * and the node that is drawn over every other one that holds the point wins,
 * which is the deepest node of the topmost subtree. The view of a tree is drawn
 * over the scene, so an element under the cursor consumes the pick and the ray
 * of the scene never runs. A hit raises what box.picking raises for a collider:
 * on.hover, on.press, on.release, on.click and on.drag, bubbling up the tree
 * until a handler consumes them, with Hovered on the element under the cursor
 * and the Action of an element firing through the input action layer on a
 * click. Every widget is written in script from those pieces; the module holds
 * no widget of its own.
 *
 * The in game console is the proof of that: a Console singleton installs the
 * backtick binding and the first open loads the script the framework ships at
 * BOX_UI_CONSOLE, which builds the overlay out of the elements above. The
 * engine only fills the two parts the script marks, the scrollback of a
 * ConsoleLog element and the line of a TextInput, and holds the keyboard for
 * the overlay while it is open.
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

/** Sizes a side of an element as a fraction of the matching side of its parent.
 *
 * A side of zero leaves the matching side of the Layout or the Rect of the
 * element alone, so an element can fill half its parent on one axis and keep
 * an authored size on the other. The parent must already know its own box: an
 * explicit Layout.width or Layout.height, inside its padding, or, for the root
 * of a tree that has neither, the logical size of the view that draws it. A
 * parent with none of those leaves the side at zero too.
 */
ECS_STRUCT(Fill, {
    float width;  /**< Fraction of the width of the parent, 0 leaves it alone */
    float height; /**< Fraction of the height of the parent, 0 leaves it alone */
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
 *
 * An element with a BoxShadow casts one: a second quad behind it that fills the
 * rounded rectangle of its Border, grown by the spread and faded out over the
 * blur, with the same analytic shape a Border draws and no geometry of its own.
 *
 * An Opacity below one on an element that holds children makes its subtree
 * composite as one image: the subtree is rendered at full strength by a view of
 * its own into a target the size of its Bounds and one quad shows that target
 * with the opacity of the element, so two children that overlap no longer show
 * through each other. The path is taken only when the opacity of the element
 * reaches something below it, which is a child without an opacity of its own or
 * a child that composites a subtree of its own, so an element that is the only
 * thing its opacity applies to keeps the single draw it always had. The subtree
 * is clipped to the Bounds of its element, so a child that sticks out of its
 * parent is cut off, and a nested transparent subtree renders before the one
 * that holds it.
 */
ECS_STRUCT(Text, {
    char *value;       /**< The string, UTF-8 */
    float line_height; /**< Logical pixels between two baselines, 0 for the font default */
    Align align;       /**< Alignment of the lines inside the box of the element */
});

/** Blurred shadow drawn behind the rounded rectangle of an element.
 *
 * The shadow is a second quad below the element, offset by x to the right and
 * by y downward, grown by spread on every side and faded out over blur, and it
 * takes the corner radii of the Border of the element so a rounded panel casts
 * a rounded shadow. It is not part of the layout: it takes no room in the row
 * or the column of its parent and leaves the Bounds of its element alone. A
 * shadow with a transparent color draws nothing.
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

/** Line of text an element lets the keyboard edit, with a caret of its own.
 *
 * The element shows value as its Text and carries one more child, a thin quad
 * pinned to the left edge of its box at the width of the text before the
 * caret. A focused input takes the characters the frame collected and the edit
 * keys that go with them: backspace and delete around the caret, the arrows and
 * home and end over the line, and enter, which moves the line to submitted and
 * empties the input. It follows the focus rules of the device layer, so an
 * input takes nothing while the window is not focused.
 *
 * An input with history walks the lines it submitted with the up and down
 * arrows, keeping the line that was being typed until the walk comes back to
 * it, and an input with complete asks box.command for the command name or the
 * entity path the tab key completes: the longest prefix every candidate shares
 * replaces the word the caret is in and a single candidate is followed by a
 * space.
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
 * Adding the singleton installs the binding: an InputAction under the actions
 * scope that the grave key fires through the action layer, which flips open on
 * every press. The first open loads the console script the framework installs
 * at BOX_UI_CONSOLE, which builds the overlay out of ordinary UI elements and
 * marks the two parts the engine fills, the element with a ConsoleLog that
 * holds the scrollback and the element with a TextInput that holds the line.
 * The module keeps no widget of its own: everything the console looks like is
 * in that script and an application is free to install its own.
 *
 * An open console holds the keyboard: every other input map is disabled while
 * it is open and enabled again when it closes, so the keys that drive a game
 * do not drive it while a line is being typed, and its own map keeps working
 * so the same key closes it again. A closed console keeps its tree and its
 * session and only turns the view of the overlay off.
 *
 * The line a console submits is set as TerminalInput on its session, which is
 * the session an agent reaches over HTTP with every command it holds, and the
 * scrollback is the log of that session: every line it ran and the
 * TerminalOutput that came back, the engine log included through the logs
 * command. Submitting a line also fires the submit action, so a script can
 * await on.pressed for it.
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

/** Name of the terminal session the console runs its lines on */
#define BOX_UI_CONSOLE_SESSION "console"

/** Input map that holds the binding of the grave key, the one an open console keeps */
#define BOX_UI_CONSOLE_MAP "console_map"

/** Lines of the session log a ConsoleLog element shows when it names none */
#define BOX_UI_CONSOLE_LINES (14)

/** Width of the caret of a text input in logical pixels */
#define BOX_UI_CARET_WIDTH (2.0f)

/** Lines one text input keeps in its history */
#define BOX_UI_HISTORY_MAX (64)

/** What the UI holds of the pointer this frame, written by the hit test.
 *
 * A singleton every frame carries the answer of the hit test of that frame, so a
 * script reads it as data and a game that steers its world with the cursor knows
 * to stand back while the pointer is on a panel.
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
 * The point is in the logical pixels of the view of the tree, the units its
 * Bounds are given in, and the node that is drawn over every other node holding
 * it is the answer: the deepest node of the topmost subtree. A node without a
 * rectangle holds nothing, and a node of a transparent subtree that is clipped
 * away by the element that composites it is not hit.
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
