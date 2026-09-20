/**
 * @file Font.c
 * @brief Tests for Font: the atlas, its growth, font switching and the script helpers.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include "../../../src/ui/ui_internal.h"

static ecs_world_t *w;

void Font_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
}

void Font_teardown(void)
{
    ecs_fini(w);
}

void Font_default_font_is_the_one_the_engine_ships(void)
{
    // ui/text
    box_ui_atlas_t *atlas = box_ui_atlas(w, NULL, 0);

    test_assert(atlas != NULL);
    test_bool(atlas->failed, false);
    test_str(atlas->path, BOX_UI_FONT);
    test_flt(atlas->size, BOX_UI_FONT_SIZE);
    test_assert(atlas->ascent > 0);
    test_assert(atlas->descent > 0);
    test_int(atlas->edge, BOX_UI_ATLAS_SIZE);
}

void Font_one_atlas_per_file_and_size(void)
{
    // ui/text
    box_ui_atlas_t *a = box_ui_atlas(w, BOX_UI_FONT, 16);
    box_ui_atlas_t *b = box_ui_atlas(w, BOX_UI_FONT, 16);
    box_ui_atlas_t *c = box_ui_atlas(w, BOX_UI_FONT, 32);

    test_assert(a == b);
    test_assert(a != c);
    test_int(ecs_vec_count(&box_ui_fonts(w)->atlases), 2);
}

void Font_missing_font_file_fails_once(void)
{
    // ui/text
    ecs_log_set_level(-4);
    box_ui_atlas_t *atlas = box_ui_atlas(w, "etc/fonts/nothing.ttf", 16);
    ecs_log_set_level(-1);

    test_assert(atlas != NULL);
    test_bool(atlas->failed, true);
    test_assert(box_ui_atlas_glyph(w, atlas, 'A') == NULL);
}

void Font_missing_font_draws_nothing(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Font, {.path = ecs_os_strdup("etc/fonts/nothing.ttf")});
    ecs_set(w, label, Text, {.value = ecs_os_strdup("Hello")});
    ecs_log_set_level(-4);
    box_ui_update(w);
    ecs_log_set_level(-1);

    test_int(ecs_count_id(w, UiGlyph), 0);
    test_flt(ecs_get(w, label, Bounds)->width, 0);
}

void Font_size_scales_the_measurement(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t small = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, small, Font, {.size = 12});
    ecs_set(w, small, Text, {.value = ecs_os_strdup("Hello")});
    ecs_entity_t large = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, large, Font, {.size = 48});
    ecs_set(w, large, Text, {.value = ecs_os_strdup("Hello")});
    box_ui_update(w);

    test_assert(ecs_get(w, large, Bounds)->width >
        ecs_get(w, small, Bounds)->width * 3);
    test_assert(ecs_get(w, large, Bounds)->height >
        ecs_get(w, small, Bounds)->height * 3);
}

void Font_font_of_an_ancestor_is_used(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Font, {.size = 48});
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, panel);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("Hello")});
    box_ui_update(w);

    box_ui_atlas_t *atlas = box_ui_atlas_of(w, label);
    test_flt(atlas->size, 48);
    test_assert(ecs_get(w, label, Bounds)->height > 40);
}

void Font_own_font_wins_from_the_one_above_it(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Font, {.size = 48});
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Font, {.size = 12});
    ecs_set(w, label, Text, {.value = ecs_os_strdup("Hello")});
    box_ui_update(w);

    test_flt(box_ui_atlas_of(w, label)->size, 12);
}

void Font_another_font_rebuilds_the_glyphs(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("Hello")});
    box_ui_update(w);
    float before = ecs_get(w, label, Bounds)->width;

    ecs_set(w, label, Font, {.size = 48});
    box_ui_update(w);

    test_assert(ecs_get(w, label, Bounds)->width > before * 2);
    test_int(ecs_count_id(w, UiGlyph), 5);
}

void Font_atlas_grows_when_the_glyphs_do_not_fit(void)
{
    // ui/text
    box_ui_atlas_t *atlas = box_ui_atlas(w, NULL, 160);
    test_int(atlas->edge, BOX_UI_ATLAS_SIZE);

    int32_t cp;
    for (cp = 'A'; cp <= 'Z'; cp ++) {
        box_ui_atlas_glyph(w, atlas, cp);
    }

    test_assert(atlas->edge > BOX_UI_ATLAS_SIZE);
    test_assert(atlas->epoch > 0);
    test_bool(box_ui_fonts(w)->grown, true);
}

void Font_grown_atlas_keeps_every_glyph(void)
{
    // ui/text
    box_ui_atlas_t *atlas = box_ui_atlas(w, NULL, 160);
    const box_ui_glyph_t *first = box_ui_atlas_glyph(w, atlas, 'A');
    int32_t x = first->x, y = first->y;

    int32_t cp;
    for (cp = 'B'; cp <= 'Z'; cp ++) {
        box_ui_atlas_glyph(w, atlas, cp);
    }

    test_assert(atlas->edge > BOX_UI_ATLAS_SIZE);

    const box_ui_glyph_t *again = box_ui_atlas_glyph(w, atlas, 'A');
    test_int(again->x, x);
    test_int(again->y, y);
    test_assert(again->width > 0);
}

void Font_grown_atlas_uploads_at_its_new_size(void)
{
    // ui/text
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Font, {.size = 160});
    ecs_set(w, label, Text, {.value = ecs_os_strdup("ABCDEFGHIJKLMNOPQRSTUVWXYZ")});
    box_ui_update(w);

    box_ui_atlas_t *atlas = box_ui_atlas_of(w, label);
    const RenderTexture *tex = ecs_get(w, atlas->texture, RenderTexture);
    test_assert(tex != NULL);
    test_int(tex->width, atlas->edge);
    test_assert(atlas->edge > BOX_UI_ATLAS_SIZE);
}

void Font_measure_function_returns_the_size(void)
{
    // ui/text
    test_int(ecs_script_run(w, "test",
        "using box.ui\n"
        "using box.geometry\n"
        "root { Ui: {height: 100}\n"
        "  label { Text: {value: \"Hello\"} }\n"
        "}\n"
        "const size = text.measure(root.label, \"Hello\")\n"
        "probe { Rect: {size.x, size.y} }\n", NULL), 0);
    box_ui_update(w);

    ecs_entity_t label = ecs_lookup(w, "root.label");
    ecs_entity_t probe = ecs_lookup(w, "probe");
    test_assert(probe != 0);
    test_assert(ecs_get(w, probe, Rect)->width > 10);
    test_flt(ecs_get(w, probe, Rect)->width, ecs_get(w, label, Bounds)->width);
    test_flt(ecs_get(w, probe, Rect)->height, ecs_get(w, label, Bounds)->height);
}

void Font_wrap_function_breaks_at_the_width(void)
{
    // ui/text
    test_int(ecs_script_run(w, "test",
        "using box.ui\n"
        "root { Ui: {height: 100}\n"
        "  label { Text: {value: \"x\"} }\n"
        "}\n"
        "const wrapped = text.wrap(root.label, \"aaa bbb ccc ddd\", 40)\n"
        "probe { Text: {value: wrapped} }\n", NULL), 0);

    ecs_entity_t probe = ecs_lookup(w, "probe");
    test_assert(probe != 0);
    test_assert(strchr(ecs_get(w, probe, Text)->value, '\n') != NULL);
    test_assert(strstr(ecs_get(w, probe, Text)->value, "aaa") != NULL);
    test_assert(strstr(ecs_get(w, probe, Text)->value, "ddd") != NULL);
}

void Font_label_function_creates_a_text_element(void)
{
    // ui/text
    test_int(ecs_script_run(w, "test",
        "using box.ui\n"
        "panel {\n"
        "  Ui: {height: 100}\n"
        "  Layout: {direction: Row, padding: 6}\n"
        "}\n"
        "const made = text.label(panel, \"caption\", \"Save\")\n", NULL), 0);

    ecs_entity_t panel = ecs_lookup(w, "panel");
    ecs_entity_t caption = ecs_lookup(w, "panel.caption");
    test_assert(caption != 0);
    test_str(ecs_get(w, caption, Text)->value, "Save");

    box_ui_update(w);
    test_int(ecs_count_id(w, UiGlyph), 4);
    test_assert(ecs_get(w, panel, Bounds)->width >
        ecs_get(w, caption, Bounds)->width);
}

void Font_label_function_reuses_the_element_it_made(void)
{
    // ui/text
    test_int(ecs_script_run(w, "test",
        "using box.ui\n"
        "panel { Ui: {height: 100} }\n"
        "const a = text.label(panel, \"caption\", \"One\")\n"
        "const b = text.label(panel, \"caption\", \"Two\")\n", NULL), 0);

    ecs_entity_t caption = ecs_lookup(w, "panel.caption");
    test_assert(caption != 0);
    test_str(ecs_get(w, caption, Text)->value, "Two");
    test_int(ecs_count_id(w, ecs_id(Text)), 1);
}

void Font_raster_size_follows_the_device_scale(void)
{
    // ore_engine/console_text_blurry
    Surface *surface = ecs_singleton_ensure(w, Surface);
    surface->width = 1920;
    surface->height = 1080;
    surface->point_width = 960;
    surface->point_height = 540;

    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("Hello")});
    box_ui_update(w);

    box_ui_atlas_t *atlas = box_ui_atlas(w, NULL, 0);
    test_flt(atlas->device, 2);
    test_flt(atlas->raster, BOX_UI_FONT_SIZE * 2);
    test_flt(atlas->size, BOX_UI_FONT_SIZE);

    const box_ui_glyph_t *glyph = box_ui_atlas_glyph(w, atlas, 'H');
    test_assert(glyph != NULL);
    test_assert((float)glyph->height > BOX_UI_FONT_SIZE);
    test_assert(atlas->ascent < BOX_UI_FONT_SIZE);
}

void Font_atlas_rerasterizes_when_the_device_scale_changes(void)
{
    // ore_engine/console_text_blurry
    Surface *surface = ecs_singleton_ensure(w, Surface);
    surface->width = 1920;
    surface->height = 1080;
    surface->point_width = 960;
    surface->point_height = 540;

    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("Hello")});
    box_ui_update(w);

    box_ui_atlas_t *atlas = box_ui_atlas(w, NULL, 0);
    int32_t tall = box_ui_atlas_glyph(w, atlas, 'H')->height;
    uint32_t epoch = atlas->epoch;
    float width = ecs_get(w, label, Bounds)->width;

    surface = ecs_singleton_ensure(w, Surface);
    surface->height = 540;
    box_ui_update(w);

    test_flt(atlas->device, 1);
    test_flt(atlas->raster, BOX_UI_FONT_SIZE);
    test_assert(atlas->epoch > epoch);
    test_assert(box_ui_atlas_glyph(w, atlas, 'H')->height < tall);
    test_assert(ecs_get(w, label, Bounds)->width > width - 1);
    test_assert(ecs_get(w, label, Bounds)->width < width + 1);
}
