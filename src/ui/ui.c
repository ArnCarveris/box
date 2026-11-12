/**
 * @file ui/ui.c
 * @brief Registers box.ui: the focus tags, the event types, the module state, its hooks in
 * box.input, box.picking and the flecs focus, the text field, the theme and the widget library.
 */

#define BOX_UI_IMPL
#include "ui.h"

ECS_COMPONENT_DECLARE(UiState);
ECS_TAG_DECLARE(Focusable);
ECS_TAG_DECLARE(Focused);
ECS_TAG_DECLARE(FocusScope);
ECS_TAG_DECLARE(KeyCapture);

/** Release the state of the module with the futures still waiting for an event.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_ui_state_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    UiState *us = ptr;
    int32_t i, w;
    (void)ti;
    for (i = 0; i < count; i ++) {
        box_ui_state_t *st = us[i].state;
        if (!st) {
            continue;
        }

        for (w = 0; w < ecs_vec_count(&st->waiters); w ++) {
            ecs_script_future_release(ecs_vec_get_t(&st->waiters, box_ui_waiter_t, w)->future);
        }

        for (w = 0; w < ecs_vec_count(&st->timers); w ++) {
            ecs_script_future_release(ecs_vec_get_t(&st->timers, box_ui_timer_t, w)->future);
        }

        ecs_vec_fini_t(NULL, &st->waiters, box_ui_waiter_t);
        ecs_vec_fini_t(NULL, &st->timers, box_ui_timer_t);
        ecs_vec_fini_t(NULL, &st->order, ecs_entity_t);
        ecs_vec_fini_t(NULL, &st->text, char);
        ecs_vec_fini_t(NULL, &st->paste, char);
        ecs_vec_fini_t(NULL, &st->edit_list, ecs_entity_t);
        ecs_vec_fini_t(NULL, &st->edit_chars, box_ui_char_t);
        ecs_os_free(st);
    }
}

box_ui_state_t* box_ui_state(
    const ecs_world_t *world)
{
    const ecs_world_t *real = ecs_get_world(world);
    const UiState *us = NULL;
    if (ecs_id(UiState) && !ecs_is_fini(real)) {
        us = ecs_singleton_get(real, UiState);
    }

    return us ? us->state : NULL;
}

/** Get the text of a vector of the state, empty when it holds none.
 *
 * @param text The char vector, terminated when it holds text
 * @return The text
 */
static const char* box_ui_vec_text(
    const ecs_vec_t *text)
{
    return ecs_vec_count(text) ? ecs_vec_first_t(text, char) : "";
}

const char* box_ui_text(
    const ecs_world_t *world)
{
    const box_ui_state_t *st = box_ui_state(world);

    return st ? box_ui_vec_text(&st->text) : "";
}

const char* box_ui_paste(
    const ecs_world_t *world)
{
    const box_ui_state_t *st = box_ui_state(world);

    return st ? box_ui_vec_text(&st->paste) : "";
}

void BoxUiImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxUi);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxInput);
    ECS_IMPORT(world, BoxPicking);

    ECS_TAG_DEFINE(world, Focusable);
    ECS_TAG_DEFINE(world, Focused);
    ECS_TAG_DEFINE(world, FocusScope);
    ECS_TAG_DEFINE(world, KeyCapture);
    ECS_META_COMPONENT(world, FocusStatus);
    ECS_META_COMPONENT(world, UiChangeEvent);
    ECS_META_COMPONENT(world, UiTheme);
    ECS_META_COMPONENT(world, TextEdit);
    ECS_META_COMPONENT(world, TextEditStatus);
    ecs_set_hooks(world, FocusStatus, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, UiChangeEvent, { .ctor = flecs_default_ctor });
    ECS_COMPONENT_DEFINE(world, UiState);
    ecs_set_hooks(world, UiState, {
        .ctor = flecs_default_ctor,
        .dtor = box_ui_state_dtor,
        .flags = ECS_TYPE_HOOK_COPY_ILLEGAL | ECS_TYPE_HOOK_MOVE_ILLEGAL
    });

    box_ui_state_t *st = ecs_os_calloc_t(box_ui_state_t);
    int32_t i;
    ecs_vec_init_t(NULL, &st->waiters, box_ui_waiter_t, 0);
    ecs_vec_init_t(NULL, &st->timers, box_ui_timer_t, 0);
    ecs_vec_init_t(NULL, &st->order, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &st->text, char, 0);
    ecs_vec_init_t(NULL, &st->paste, char, 0);
    for (i = 0; i < BoxUiWaitCount; i ++) {
        st->waits[i] = (box_ui_wait_ctx_t){ .st = st, .kind = (box_ui_wait_kind_t)i };
    }

    st->focusables = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "UiFocusables" }),
        .terms = {{ .id = Focusable }},
        .cache_kind = EcsQueryCacheAuto
    });
    st->scopes = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "UiFocusScopes" }),
        .terms = {{ .id = FocusScope }},
        .cache_kind = EcsQueryCacheAuto
    });
    ecs_singleton_ensure(world, UiState)->state = st;

    ecs_entity_t script = ecs_lookup(world, "flecs.script");
    ecs_entity_t on = ecs_lookup(world, "flecs.script.on");
    ecs_entity_t ui = ecs_entity(world, { .name = "ui", .parent = script });
    box_ui_focus_import(world, st);
    box_ui_keys_import(world, st);
    box_ui_events_import(world, st, on, ui);
    box_ui_activation_import(world, ui);
    box_ui_text_edit_import(world, st);
    box_ui_widgets_import(world, ui);
    st->theme = ecs_lookup(world, "box.ui.theme");

    if (box_ui_events_ready(world)) {
        ecs_script_set_focus(world, 0);
    }
}
