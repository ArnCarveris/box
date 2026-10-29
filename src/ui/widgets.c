/**
 * @file ui/widgets.c
 * @brief The UiTheme component and the box.ui.theme entity, and ui.sibling, which the widget
 * library etc/scenes/ui/widgets.flecs uses.
 */

#include "ui.h"

/** The dark theme, box.widgets.DarkTheme, in static storage so its padding is zero */
static const UiTheme box_ui_dark_theme = {
    .background = { 32, 35, 41 },
    .surface = { 46, 50, 58 },
    .surface_alt = { 38, 41, 48 },
    .border = { 70, 76, 87 },
    .text = { 214, 218, 224 },
    .text_muted = { 142, 149, 160 },
    .accent = { 98, 168, 122 },
    .accent_text = { 22, 26, 30 },
    .hover = { 60, 65, 75 },
    .pressed = { 36, 39, 46 },
    .focus = { 120, 172, 240 },
    .danger = { 210, 104, 91 },
    .selection = { 60, 104, 84 },
    .caret = { 214, 218, 224 },
    .disabled_opacity = 0.45f,
    .font_size = 14,
    .small_font_size = 12,
    .heading_font_size = 18,
    .radius = 4,
    .padding = 8,
    .spacing = 8,
    .control_height = 28,
    .focus_width = 2,
    .caret_blink = 0.5f
};

/** Fill themes with the dark theme, box.widgets.DarkTheme.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_ui_theme_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    UiTheme *themes = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        ecs_os_memcpy_t(&themes[i], &box_ui_dark_theme, UiTheme);
    }
}

/** Find the sibling of an entity a number of places away in the child order of its parent, among
 * the siblings with an id, wrapping around: ui.sibling(entity, offset, with).
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The entity, the offset (negative steps back) and the id, 0 for any sibling
 * @param result Receives the sibling, the entity itself when no other sibling has the id
 */
static void box_ui_sibling(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    const ecs_world_t *world = ctx->world;
    ecs_entity_t e = *(ecs_entity_t*)argv[0].ptr;
    int32_t offset = *(int32_t*)argv[1].ptr;
    ecs_id_t with = *(ecs_id_t*)argv[2].ptr;
    ecs_entity_t parent = e && ecs_is_alive(world, e) ? ecs_get_parent(world, e) : 0;
    ecs_entity_t found = e;
    ecs_vec_t children;
    int32_t i, n, count, at = -1, step = offset < 0 ? -1 : 1, left = offset < 0 ? -offset : offset;
    (void)argc;
    ecs_vec_init_t(NULL, &children, ecs_entity_t, 0);
    if (parent) {
        ecs_iter_t it = ecs_children(world, parent);
        while (ecs_children_next(&it)) {
            for (i = 0; i < it.count; i ++) {
                ecs_vec_append_t(NULL, &children, ecs_entity_t)[0] = it.entities[i];
            }
        }
    }

    count = ecs_vec_count(&children);
    for (i = 0; i < count && at < 0; i ++) {
        at = *ecs_vec_get_t(&children, ecs_entity_t, i) == e ? i : -1;
    }

    while (at >= 0 && left > 0) {
        int32_t next = at;
        for (n = 1; n < count && next == at; n ++) {
            int32_t c = ((at + step * n) % count + count) % count;
            ecs_entity_t sibling = *ecs_vec_get_t(&children, ecs_entity_t, c);
            next = !with || ecs_has_id(world, sibling, with) ? c : at;
        }

        left = next == at ? 0 : left - 1;
        at = next;
    }

    found = at >= 0 ? *ecs_vec_get_t(&children, ecs_entity_t, at) : e;
    ecs_vec_fini_t(NULL, &children, ecs_entity_t);
    *(ecs_entity_t*)result->ptr = found;
}

void box_ui_widgets_import(
    ecs_world_t *world,
    ecs_entity_t ui)
{
    ecs_set_hooks(world, UiTheme, { .ctor = box_ui_theme_ctor });
    ecs_entity_t theme = ecs_entity(world, { .name = "theme" });
    ecs_add(world, theme, UiTheme);
    ecs_add_pair(world, theme, ecs_id(EcsScript), ecs_get_parent(world, theme));
    ecs_function(world, {
        .name = "sibling",
        .parent = ui,
        .return_type = ecs_id(ecs_entity_t),
        .params = {
            { "entity", ecs_id(ecs_entity_t) },
            { "offset", ecs_id(ecs_i32_t) },
            { "with", ecs_id(ecs_id_t) }
        },
        .callback = box_ui_sibling
    });
}
