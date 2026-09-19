/**
 * @file Text.c
 * @brief Tests for Text: measuring, wrapping, alignment and the glyph quads.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/ui/ui_internal.h"

static ecs_world_t *w;

void Text_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxUi);
}

void Text_teardown(void)
{
    ecs_fini(w);
}

void Text_element_is_as_wide_as_its_string(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("Hello")});
    box_ui_update(w);

    const Bounds *b = ecs_get(w, label, Bounds);
    test_assert(b != NULL);
    test_assert(b->width > 10);
    test_assert(b->height > 5);
}

void Text_longer_string_is_wider(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t one = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, one, Text, {.value = ecs_os_strdup("i")});
    ecs_entity_t two = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, two, Text, {.value = ecs_os_strdup("iiiii")});
    box_ui_update(w);

    test_assert(ecs_get(w, two, Bounds)->width >
        ecs_get(w, one, Bounds)->width);
    test_flt(ecs_get(w, two, Bounds)->height,
        ecs_get(w, one, Bounds)->height);
}

void Text_empty_string_has_no_glyphs(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("")});
    box_ui_update(w);

    test_int(ecs_count_id(w, UiGlyph), 0);
    test_flt(ecs_get(w, label, Bounds)->width, 0);
}

void Text_one_glyph_per_visible_character(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("ab cd")});
    box_ui_update(w);

    test_int(ecs_count_id(w, UiGlyph), 4);
}

void Text_glyph_shows_a_region_of_the_atlas(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("A")});
    box_ui_update(w);

    ecs_entity_t glyph = 0;
    ecs_iter_t it = ecs_each_id(w, UiGlyph);
    while (ecs_each_next(&it)) {
        glyph = it.count ? it.entities[0] : glyph;
    }

    test_assert(glyph != 0);

    const Image *img = ecs_get(w, glyph, Image);
    test_assert(img != NULL);
    test_assert(img->u1 > img->u0);
    test_assert(img->v1 > img->v0);
    test_assert(ecs_get(w, glyph, Rect)->width > 0);
    test_assert(ecs_get(w, glyph, RenderMesh) != NULL);
}

void Text_glyph_binds_the_atlas_texture(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("A")});
    box_ui_update(w);

    ecs_entity_t glyph = 0;
    ecs_iter_t it = ecs_each_id(w, UiGlyph);
    while (ecs_each_next(&it)) {
        glyph = it.count ? it.entities[0] : glyph;
    }

    const PbrTextures *slots = ecs_get(w, glyph, PbrTextures);
    test_assert(slots != NULL);
    test_assert(slots->albedo != 0);
    test_assert(ecs_get(w, slots->albedo, RenderTexture) != NULL);
    test_int(ecs_get(w, slots->albedo, RenderTexture)->width, BOX_UI_ATLAS_SIZE);
}

void Text_glyph_is_drawn_over_its_element(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("A")});
    box_ui_update(w);

    ecs_entity_t glyph = 0;
    ecs_iter_t it = ecs_each_id(w, UiGlyph);
    while (ecs_each_next(&it)) {
        glyph = it.count ? it.entities[0] : glyph;
    }

    test_assert(ecs_has(w, glyph, UiNode));
    test_int(ecs_get(w, glyph, UiNode)->root, root);
    test_assert(ecs_get(w, glyph, UiNode)->order >
        ecs_get(w, label, UiNode)->order);
}

void Text_newline_starts_a_second_line(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t one = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, one, Text, {.value = ecs_os_strdup("ab")});
    ecs_entity_t two = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, two, Text, {.value = ecs_os_strdup("a\nb")});
    box_ui_update(w);

    test_flt(ecs_get(w, two, Bounds)->height,
        ecs_get(w, one, Bounds)->height * 2);
    test_assert(ecs_get(w, two, Bounds)->width <
        ecs_get(w, one, Bounds)->width);
}

void Text_line_height_sets_the_distance_between_lines(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("a\nb\nc"), .line_height = 30});
    box_ui_update(w);

    test_flt(ecs_get(w, label, Bounds)->height, 90);
}

void Text_layout_width_wraps_the_string(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("aaa bbb ccc ddd"),
        .line_height = 20});
    ecs_set(w, label, Layout, {.width = 40});
    box_ui_update(w);

    const Bounds *b = ecs_get(w, label, Bounds);
    test_flt(b->width, 40);
    test_assert(b->height > 20);
    test_assert(b->height <= 80);
}

void Text_wrapped_lines_stay_inside_the_width(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("one two three four five six"),
        .line_height = 20});
    ecs_set(w, label, Layout, {.width = 90});
    box_ui_update(w);

    test_assert(ecs_count_id(w, UiGlyph) > 0);
    ecs_iter_t it = ecs_each_id(w, UiGlyph);
    while (ecs_each_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const Bounds *b = ecs_get(w, it.entities[i], Bounds);

            test_assert(b->x - b->width * 0.5f >= -46);
            test_assert(b->x + b->width * 0.5f <= 46);
        }
    }
}

void Text_align_center_centres_the_lines(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("ab"), .align = AlignCenter});
    ecs_set(w, label, Layout, {.width = 200});
    box_ui_update(w);

    float left = 1e9f;
    ecs_iter_t it = ecs_each_id(w, UiGlyph);
    while (ecs_each_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const Bounds *b = ecs_get(w, it.entities[i], Bounds);

            left = b->x - b->width * 0.5f < left ? b->x - b->width * 0.5f : left;
        }
    }

    test_assert(left > -40);
    test_assert(left < 0);
}

void Text_align_right_puts_the_lines_against_the_edge(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("ab"), .align = AlignRight});
    ecs_set(w, label, Layout, {.width = 200});
    box_ui_update(w);

    float right = -1e9f;
    ecs_iter_t it = ecs_each_id(w, UiGlyph);
    while (ecs_each_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const Bounds *b = ecs_get(w, it.entities[i], Bounds);

            right = b->x + b->width * 0.5f > right ? b->x + b->width * 0.5f : right;
        }
    }

    test_assert(right > 95);
    test_assert(right <= 101);
}

void Text_changed_value_rebuilds_the_glyphs(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("ab")});
    box_ui_update(w);
    test_int(ecs_count_id(w, UiGlyph), 2);

    ecs_set(w, label, Text, {.value = ecs_os_strdup("abcde")});
    box_ui_update(w);

    test_int(ecs_count_id(w, UiGlyph), 5);
}

void Text_shorter_string_drops_the_glyphs_it_lost(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("abcde")});
    box_ui_update(w);
    test_int(ecs_count_id(w, UiGlyph), 5);

    ecs_set(w, label, Text, {.value = ecs_os_strdup("ab")});
    box_ui_update(w);

    test_int(ecs_count_id(w, UiGlyph), 2);
}

void Text_unchanged_text_keeps_its_glyphs(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("abc")});
    box_ui_update(w);

    ecs_entity_t glyph = 0;
    ecs_iter_t it = ecs_each_id(w, UiGlyph);
    while (ecs_each_next(&it)) {
        glyph = it.count ? it.entities[0] : glyph;
    }

    ecs_get_mut(w, root, UiImpl)->relayout = true;
    box_ui_update(w);

    test_int(ecs_count_id(w, UiGlyph), 3);
    test_assert(ecs_is_alive(w, glyph));
}

void Text_removed_text_drops_its_glyphs(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("abc")});
    box_ui_update(w);
    test_int(ecs_count_id(w, UiGlyph), 3);

    ecs_remove(w, label, Text);
    box_ui_update(w);

    test_int(ecs_count_id(w, UiGlyph), 0);
}

void Text_text_sizes_the_row_that_holds_it(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionRow, .padding = 4});
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("Save")});
    box_ui_update(w);

    const Bounds *row = ecs_get(w, root, Bounds);
    const Bounds *b = ecs_get(w, label, Bounds);
    test_flt(row->width, b->width + 8);
    test_flt(row->height, b->height + 8);
}

void Text_layout_height_overrides_the_measured_height(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("Save")});
    ecs_set(w, label, Layout, {.width = 120, .height = 40});
    box_ui_update(w);

    test_flt(ecs_get(w, label, Bounds)->width, 120);
    test_flt(ecs_get(w, label, Bounds)->height, 40);
}

void Text_glyphs_take_the_color_of_the_element(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("A")});
    ecs_set(w, label, Rgb, {10, 20, 30});
    box_ui_update(w);

    ecs_entity_t glyph = 0;
    ecs_iter_t it = ecs_each_id(w, UiGlyph);
    while (ecs_each_next(&it)) {
        glyph = it.count ? it.entities[0] : glyph;
    }

    const Emissive *lit = ecs_get(w, glyph, Emissive);
    test_assert(lit != NULL);
    test_flt(lit->strength, 1);
    test_int(lit->color.r, 10);
    test_int(lit->color.g, 20);
    test_int(lit->color.b, 30);
}

void Text_glyphs_composite_over_what_is_behind_them(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("A")});
    box_ui_update(w);

    ecs_entity_t glyph = 0;
    ecs_iter_t it = ecs_each_id(w, UiGlyph);
    while (ecs_each_next(&it)) {
        glyph = it.count ? it.entities[0] : glyph;
    }

    test_assert(ecs_has(w, glyph, BlendMode));
    test_int(*ecs_get(w, glyph, BlendMode), AlphaBlend);
    test_assert(ecs_get(w, glyph, Rgb)->r <= 1);
    test_flt(ecs_get(w, glyph, PbrMaterial)->metallic, 1);
    test_flt(ecs_get(w, glyph, PbrMaterial)->roughness, 0);
}

void Text_rect_of_the_element_wraps_the_string(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Rect, {60, 10});
    ecs_set(w, label, Text, {.value = ecs_os_strdup("aaa bbb ccc"),
        .line_height = 20});
    box_ui_update(w);

    test_assert(ecs_get(w, label, Bounds)->height > 20);
    test_assert(ecs_get(w, label, Bounds)->width <= 60);
}

void Text_content_sized_row_wraps_around_its_label(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionColumn, .width = 600,
        .padding = 16, .spacing = 12, .h = AlignCenter});
    ecs_set(w, root, Border, {.radius = 16, .width = 2});
    ecs_entity_t button = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, button, Layout, {.direction = DirectionRow, .padding = 12,
        .h = AlignCenter, .v = VAlignMiddle});
    ecs_set(w, button, Border, {.radius = 18});
    ecs_entity_t caption = ecs_new_w_pair(w, EcsChildOf, button);
    ecs_set(w, caption, Text, {.value = ecs_os_strdup("Save changes")});
    box_ui_update(w);

    const Bounds *label = ecs_get(w, caption, Bounds);
    test_assert(label != NULL);
    test_assert(label->width > 10);
    test_flt(ecs_get(w, button, Bounds)->width, label->width + 24);
    test_flt(ecs_get(w, button, Bounds)->height, label->height + 24);
    test_flt(ecs_get(w, root, Bounds)->width, 600);
    test_flt(ecs_get(w, root, Bounds)->height, label->height + 24 + 32);
    test_flt(ecs_get(w, root, Rect)->width, 600);
}

void Text_glyphs_composite_with_plain_alpha_in_the_ui_view(void)
{
    // ui/root_position
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 960;
    packet.height = 540;

    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_get_mut(w, root, Ui)->height = 540;
    ecs_modified(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("Hi")});
    ecs_set(w, label, Rgb, {230, 230, 255});
    box_ui_update(w);
    box_render_extract(w, &packet);

    test_int(packet.views.count, 2);
    box_view_t *scene = box_vec_at(&packet.views, 0);
    box_view_t *view = box_vec_at(&packet.views, 1);
    test_uint(scene->plain_blend, 0);
    test_uint(view->plain_blend, 1);
    test_uint(view->translucent_count, 0);
    test_assert(view->transparent_count > 0);

    uint32_t i;
    for (i = 0; i < view->transparent_count; i ++) {
        box_transparent_t *draw = box_vec_at(&packet.transparent,
            view->transparent_first + i);
        test_uint(draw->blend, BOX_BLEND_ALPHA);
    }

    box_packet_fini(&packet);
}
