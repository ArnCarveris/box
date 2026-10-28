#ifndef FLECS_ENGINE_INPUT_H
#define FLECS_ENGINE_INPUT_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_INPUT_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(flecs_engine_key_state_t, {
    bool pressed;
    bool state;
    bool current;
});

ECS_STRUCT(flecs_engine_mouse_coord_t, {
    float x;
    float y;
});

ECS_STRUCT(flecs_engine_mouse_state_t, {
    flecs_engine_key_state_t left;
    flecs_engine_key_state_t right;
    flecs_engine_mouse_coord_t wnd;
    flecs_engine_mouse_coord_t rel;
    flecs_engine_mouse_coord_t view;
    flecs_engine_mouse_coord_t view_norm;
    flecs_engine_mouse_coord_t scroll;
});

ECS_STRUCT(FlecsInput, {
    flecs_engine_key_state_t keys[128];
    flecs_engine_mouse_state_t mouse;
});

#define FLECS_KEY_UNKNOWN          ((int)0)
#define FLECS_KEY_RETURN           ((int)'\r')
#define FLECS_KEY_ESCAPE           ((int)'\033')
#define FLECS_KEY_BACKSPACE        ((int)'\b')
#define FLECS_KEY_TAB              ((int)'\t')
#define FLECS_KEY_SPACE            ((int)' ')
#define FLECS_KEY_EXCLAIM          ((int)'!')
#define FLECS_KEY_QUOTEDBL         ((int)'"')
#define FLECS_KEY_HASH             ((int)'#')
#define FLECS_KEY_PERCENT          ((int)'%')
#define FLECS_KEY_DOLLAR           ((int)'$')
#define FLECS_KEY_AMPERSAND        ((int)'&')
#define FLECS_KEY_QUOTE            ((int)'\'')
#define FLECS_KEY_LEFT_PAREN       ((int)'(')
#define FLECS_KEY_RIGHT_PAREN      ((int)')')
#define FLECS_KEY_ASTERISK         ((int)'*')
#define FLECS_KEY_PLUS             ((int)'+')
#define FLECS_KEY_COMMA            ((int)',')
#define FLECS_KEY_MINUS            ((int)'-')
#define FLECS_KEY_PERIOD           ((int)'.')
#define FLECS_KEY_SLASH            ((int)'/')

#define FLECS_KEY_0                ((int)'0')
#define FLECS_KEY_1                ((int)'1')
#define FLECS_KEY_2                ((int)'2')
#define FLECS_KEY_3                ((int)'3')
#define FLECS_KEY_4                ((int)'4')
#define FLECS_KEY_5                ((int)'5')
#define FLECS_KEY_6                ((int)'6')
#define FLECS_KEY_7                ((int)'7')
#define FLECS_KEY_8                ((int)'8')
#define FLECS_KEY_9                ((int)'9')

#define FLECS_KEY_COLON            ((int)':')
#define FLECS_KEY_SEMICOLON        ((int)';')
#define FLECS_KEY_LESS             ((int)'<')
#define FLECS_KEY_EQUAL            ((int)'=')
#define FLECS_KEY_GREATER          ((int)'>')
#define FLECS_KEY_QUESTION         ((int)'?')
#define FLECS_KEY_AT               ((int)'@')
#define FLECS_KEY_LEFT_BRACKET     ((int)'[')
#define FLECS_KEY_RIGHT_BRACKET    ((int)']')
#define FLECS_KEY_BACKSLASH        ((int)'\\')
#define FLECS_KEY_CARET            ((int)'^')
#define FLECS_KEY_UNDERSCORE       ((int)'_')
#define FLECS_KEY_GRAVE_ACCENT     ((int)'`')
#define FLECS_KEY_APOSTROPHE       ((int)'\'')

#define FLECS_KEY_A                ((int)'a')
#define FLECS_KEY_B                ((int)'b')
#define FLECS_KEY_C                ((int)'c')
#define FLECS_KEY_D                ((int)'d')
#define FLECS_KEY_E                ((int)'e')
#define FLECS_KEY_F                ((int)'f')
#define FLECS_KEY_G                ((int)'g')
#define FLECS_KEY_H                ((int)'h')
#define FLECS_KEY_I                ((int)'i')
#define FLECS_KEY_J                ((int)'j')
#define FLECS_KEY_K                ((int)'k')
#define FLECS_KEY_L                ((int)'l')
#define FLECS_KEY_M                ((int)'m')
#define FLECS_KEY_N                ((int)'n')
#define FLECS_KEY_O                ((int)'o')
#define FLECS_KEY_P                ((int)'p')
#define FLECS_KEY_Q                ((int)'q')
#define FLECS_KEY_R                ((int)'r')
#define FLECS_KEY_S                ((int)'s')
#define FLECS_KEY_T                ((int)'t')
#define FLECS_KEY_U                ((int)'u')
#define FLECS_KEY_V                ((int)'v')
#define FLECS_KEY_W                ((int)'w')
#define FLECS_KEY_X                ((int)'x')
#define FLECS_KEY_Y                ((int)'y')
#define FLECS_KEY_Z                ((int)'z')
#define FLECS_KEY_DELETE           ((int)127)

#define FLECS_KEY_RIGHT            ((int)'R')
#define FLECS_KEY_LEFT             ((int)'L')
#define FLECS_KEY_DOWN             ((int)'D')
#define FLECS_KEY_UP               ((int)'U')
#define FLECS_KEY_LEFT_CTRL        ((int)'C')
#define FLECS_KEY_LEFT_ALT         ((int)'A')
#define FLECS_KEY_LEFT_SHIFT       ((int)'S')
#define FLECS_KEY_RIGHT_CTRL       ((int)'T')
#define FLECS_KEY_RIGHT_ALT        ((int)'Z')
#define FLECS_KEY_RIGHT_SHIFT      ((int)'H')
#define FLECS_KEY_INSERT           ((int)'I')
#define FLECS_KEY_HOME             ((int)'B')
#define FLECS_KEY_END              ((int)'E')
#define FLECS_KEY_PAGE_UP          ((int)'O')
#define FLECS_KEY_PAGE_DOWN        ((int)'P')
#define FLECS_KEY_LEFT_SUPER       ((int)'M')
#define FLECS_KEY_RIGHT_SUPER      ((int)'N')

#define FLECS_KEY_MOUSE_LEFT       ((int)128)
#define FLECS_KEY_MOUSE_RIGHT      ((int)129)
#define FLECS_INPUT_KEY_COUNT      (130)

typedef uint64_t flecs_input_keymask_t;

#define FLECS_INPUT_KEY_MASK       ((flecs_input_keymask_t)0xFFFFFFFFull)
#define FLECS_MOD_SHIFT_LEFT       ((flecs_input_keymask_t)1 << 32)
#define FLECS_MOD_SHIFT_RIGHT      ((flecs_input_keymask_t)1 << 33)
#define FLECS_MOD_CTRL_LEFT        ((flecs_input_keymask_t)1 << 34)
#define FLECS_MOD_CTRL_RIGHT       ((flecs_input_keymask_t)1 << 35)
#define FLECS_MOD_ALT_LEFT         ((flecs_input_keymask_t)1 << 36)
#define FLECS_MOD_ALT_RIGHT        ((flecs_input_keymask_t)1 << 37)
#define FLECS_MOD_SUPER_LEFT       ((flecs_input_keymask_t)1 << 38)
#define FLECS_MOD_SUPER_RIGHT      ((flecs_input_keymask_t)1 << 39)
#define FLECS_MOD_SHIFT            (FLECS_MOD_SHIFT_LEFT | FLECS_MOD_SHIFT_RIGHT)
#define FLECS_MOD_CTRL             (FLECS_MOD_CTRL_LEFT | FLECS_MOD_CTRL_RIGHT)
#define FLECS_MOD_ALT              (FLECS_MOD_ALT_LEFT | FLECS_MOD_ALT_RIGHT)
#define FLECS_MOD_SUPER            (FLECS_MOD_SUPER_LEFT | FLECS_MOD_SUPER_RIGHT)
#define FLECS_MOD_MASK             ((flecs_input_keymask_t)0xFFull << 32)

#define flecs_input_keymask(key, mods)\
    ((flecs_input_keymask_t)(((flecs_input_keymask_t)(key) & FLECS_INPUT_KEY_MASK) |\
        ((flecs_input_keymask_t)(mods) & FLECS_MOD_MASK)))
#define flecs_input_keymask_key(mask) ((int)((mask) & FLECS_INPUT_KEY_MASK))
#define flecs_input_keymask_mods(mask) ((flecs_input_keymask_t)((mask) & FLECS_MOD_MASK))

ECS_STRUCT(flecs_input_binding_t, {
    ecs_entity_t action;
});

typedef ecs_map_t FlecsInputMap;

extern ECS_COMPONENT_DECLARE(FlecsInputMap);

ECS_STRUCT(FlecsInputAction, {
    char *description;
});

ECS_ENUM(flecs_input_event_kind_t, {
    FlecsInputPressed,
    FlecsInputHeld,
    FlecsInputReleased
});

ECS_STRUCT(FlecsInputEvent, {
    ecs_entity_t action;
    flecs_input_event_kind_t kind;
    uint64_t keymask;
    flecs_engine_mouse_state_t mouse;
    flecs_engine_mouse_state_t start;
    bool moved;
});

ECS_STRUCT(FlecsInputPriority, {
    int32_t value;
});

typedef enum flecs_input_wait_kind_t {
    FlecsInputWaitPressed,
    FlecsInputWaitReleased,
    FlecsInputWaitDragged
} flecs_input_wait_kind_t;

typedef struct flecs_input_waiter_info_t {
    ecs_entity_t owner;
    int32_t priority;
    flecs_input_wait_kind_t kind;
    bool armed;
} flecs_input_waiter_info_t;

typedef struct flecs_input_event_t {
    flecs_input_event_kind_t kind;
    flecs_input_keymask_t keymask;
    flecs_engine_mouse_state_t mouse;
} flecs_input_event_t;

typedef void (*flecs_input_handler_t)(
    ecs_world_t *world,
    ecs_entity_t action,
    const flecs_input_event_t *event,
    void *ctx);

typedef struct FlecsInputHandler {
    flecs_input_handler_t callback;
    void *ctx;
} FlecsInputHandler;

extern ECS_COMPONENT_DECLARE(FlecsInputHandler);

/* Error entity a pending input wait is rejected with, so a script can abort a
 * sequence with catch(Cancelled). Named "Cancelled" in flecs.engine.input. */
extern ecs_entity_t FlecsInputCancelled;

/* Cursor travel (in normalized view coordinates) above which a drag counts as
 * moved rather than as a click in place. */
#define FLECS_INPUT_DRAG_THRESHOLD (0.01f)

void flecsEngine_inputEmit(
    ecs_world_t *world,
    ecs_entity_t action,
    flecs_input_event_kind_t kind);

/* Reject every pending pressed/released/dragged wait on an action. The waits
 * fail with the error entity, or with Cancelled when error is 0. Returns the
 * number of waits that were rejected. */
int32_t flecsEngine_inputReject(
    ecs_world_t *world,
    ecs_entity_t action,
    ecs_entity_t error);

/* True while a dragged() wait on the action has seen its press and is waiting
 * for the release, with the mouse state at press time. */
bool flecsEngine_inputDrag(
    ecs_world_t *world,
    ecs_entity_t action,
    flecs_engine_mouse_state_t *start_out);

bool flecsEngine_inputHeld(
    ecs_world_t *world,
    ecs_entity_t action);

int32_t flecsEngine_inputWaiters(
    ecs_world_t *world,
    ecs_entity_t action,
    flecs_input_waiter_info_t *out,
    int32_t max);

int flecsEngine_input_keyFromName(
    const char *name);

const char* flecsEngine_input_keyToName(
    int key_code);

void flecsEngine_inputMapBind(
    FlecsInputMap *map,
    flecs_input_keymask_t mask,
    ecs_entity_t action);

ecs_entity_t flecsEngine_inputMapGet(
    const FlecsInputMap *map,
    flecs_input_keymask_t mask);

ecs_entity_t flecsEngine_inputMapResolve(
    const FlecsInputMap *map,
    int key,
    flecs_input_keymask_t mods);

int32_t flecsEngine_inputMapBindings(
    const FlecsInputMap *map,
    ecs_entity_t action,
    flecs_input_keymask_t *out,
    int32_t max);

const char* flecsEngine_inputKeyName(
    int key);

int32_t flecsEngine_inputModifierNames(
    flecs_input_keymask_t mods,
    const char **out,
    int32_t max);

bool flecsEngine_inputKeymaskName(
    flecs_input_keymask_t mask,
    char *buf,
    int32_t size);

int flecsEngine_inputModifierKey(
    flecs_input_keymask_t bit);

int32_t flecsEngine_inputKeyConstCount(void);

const char* flecsEngine_inputKeyConst(
    int32_t index,
    flecs_input_keymask_t *value_out);

#endif
