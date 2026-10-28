#ifndef FLECS_ENGINE_UI_H
#define FLECS_ENGINE_UI_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_UI_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_ENUM(FlecsUiAlign, {
    FlecsUiLeft,
    FlecsUiCenter,
    FlecsUiRight,
});

ECS_ENUM(FlecsUiVAlign, {
    FlecsUiTop,
    FlecsUiMiddle,
    FlecsUiBottom,
});

ECS_ENUM(FlecsUiDirection, {
    FlecsUiNone,
    FlecsUiRow,
    FlecsUiColumn,
});

ECS_STRUCT(FlecsUiRect, {
    float width;
    float height;
});

ECS_STRUCT(FlecsUiBorder, {
    /* Shorthand values are inherited by zero-valued corner/edge fields. */
    float radius;
    float radius_top_left;
    float radius_top_right;
    float radius_bottom_left;
    float radius_bottom_right;
    float width;
    float top_width;
    float bottom_width;
    float left_width;
    float right_width;
    /* A zero-alpha color inherits the entity's FlecsRgba. */
    FlecsRgba color;
});

ECS_STRUCT(FlecsUiLine, {
    float x1;
    float y1;
    float x2;
    float y2;
    float thickness;
});

ECS_STRUCT(FlecsUiText, {
    char *text;
    float size;
    FlecsUiAlign align;
});

ECS_ENUM(FlecsUiImageFit, {
    FlecsUiStretch,
    FlecsUiContain,
    FlecsUiCover,
});

ECS_STRUCT(FlecsUiImage, {
    ecs_entity_t texture;
    FlecsUiImageFit fit;
    float u0;
    float v0;
    float u1;
    float v1;
    float rotation;
});

ECS_STRUCT(FlecsUiPosition, {
    float x;
    float y;
});

ECS_STRUCT(FlecsUiLayout, {
    FlecsUiDirection direction;
    float padding;
    float spacing;
    float width;
    float height;
});

ECS_STRUCT(FlecsUiAnchor, {
    FlecsUiAlign h;
    FlecsUiVAlign v;
    float x;
    float y;
});

/* Absolute screen rectangle of an element, written by the layout pass
 * every frame. Read-only for applications. */
ECS_STRUCT(FlecsUiBounds, {
    float x;
    float y;
    float width;
    float height;
});

ECS_STRUCT(FlecsUiHotkey, {
    char *key;
});

/* Input action raised (as FlecsInputPressed) when the widget is clicked or
 * its hotkey is pressed. Declaring it makes the element a widget. */
ECS_STRUCT(FlecsUiAction, {
    ecs_entity_t action;
});

ECS_STRUCT(FlecsUiWidgetState, {
    bool hover;
    bool drag;
    bool lmb_down;
    bool rmb_down;
    float mouse_x;
    float mouse_y;
    float drag_anchor_x;
    float drag_anchor_y;
});

/* Horizontal slider. The element's FlecsUiRect is the track and its FlecsRgba
 * the track colour, so a slider is chromed like any other element; the fields
 * below add the filled part, the handle and the value the handle stands for.
 * Declaring it makes the element a widget: pressing or dragging anywhere on
 * the track writes `value` and emits OnSet, which is the only thing a game
 * has to observe. `value` is authoritative in both directions - write it and
 * the handle moves. */
ECS_STRUCT(FlecsUiSlider, {
    float value;
    float min;
    float max;
    float step;             /* value quantum; 0 is continuous */
    float knob;             /* handle width in pixels; 0 = track height */
    /* A zero-alpha colour inherits the entity's FlecsRgba. */
    FlecsRgba fill;
    FlecsRgba handle;
});

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*flecs_ui_event_callback_t)(
    ecs_world_t *world,
    ecs_entity_t widget,
    void *ctx);

/* Mouse callbacks invoked by the input pass; setting this component with
 * at least one callback makes the element hit-testable. */
typedef struct FlecsUiEventListener {
    flecs_ui_event_callback_t on_enter;
    flecs_ui_event_callback_t on_leave;
    flecs_ui_event_callback_t on_move;
    flecs_ui_event_callback_t on_lmb_down;
    flecs_ui_event_callback_t on_lmb_up;
    flecs_ui_event_callback_t on_rmb_down;
    flecs_ui_event_callback_t on_rmb_up;
    flecs_ui_event_callback_t on_drag;

    void *ctx;
} FlecsUiEventListener;

extern ECS_COMPONENT_DECLARE(FlecsUiEventListener);

/* Emitted on a widget when it is clicked or its hotkey fires, with the
 * widget's own components as event ids. Named "Clicked" in flecs.engine.ui. */
extern ecs_entity_t FlecsUiClicked;

bool flecsEngine_uiMouseCaptured(
    ecs_world_t *world);

/* Widget the pointer is over, or 0 when it is over none. */
ecs_entity_t flecsEngine_uiHovered(
    ecs_world_t *world);

/* The value a slider's handle stands for when the pointer is at `x`, in the
 * screen-space rectangle the layout pass gave the track. Pure: this is the
 * mapping the drag uses, exposed so it can be tested and reused. */
float flecsEngine_uiSliderValueAt(
    const FlecsUiSlider *slider,
    const FlecsUiBounds *bounds,
    float x);

/* Position of the handle in 0..1, clamped. */
float flecsEngine_uiSliderFraction(
    const FlecsUiSlider *slider);

int flecsEngine_uiLoadWidgets(
    ecs_world_t *world,
    const char *filename);

void FlecsEngineUiImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#endif
