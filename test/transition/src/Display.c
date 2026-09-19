/**
 * @file Display.c
 * @brief Tests for the values a transition puts into the instance data of a packet.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void Display_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
}

void Display_teardown(void)
{
    box_packet_fini(&packet);
    ecs_fini(w);
}

void Display_transitioning_color_reaches_the_instance(void)
{
    // transitions/transition_params
    test_int(ecs_script_run(w, "test",
        "using box.geometry\n"
        "using box.material\n"
        "using box.transition\n"
        "e {\n"
        "  Box: {1, 1, 1}\n"
        "  Rgb: {0, 0, 0}\n"
        "  Transition: [Rgb: {duration: 1, timing_function: Linear}]\n"
        "}\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(w, "e");

    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_packet_reset(&packet);

    ecs_set(w, e, Rgb, {200, 0, 0});
    ecs_progress(w, 0.5);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    box_static_update_t *update = box_vec_at(&packet.static_updates, 0);
    test_flt(update->data.color[0], box_srgb_to_linear(100));
}

void Display_transitioning_position_moves_the_instance(void)
{
    // transitions/transition_params
    test_int(ecs_script_run(w, "test",
        "using box.geometry\n"
        "using box.transform\n"
        "using box.transition\n"
        "e {\n"
        "  Box: {1, 1, 1}\n"
        "  Position: {0, 0, 0}\n"
        "  Transition: [Position: {duration: 1, timing_function: Linear}]\n"
        "}\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(w, "e");

    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_packet_reset(&packet);

    ecs_set(w, e, Position, {100, 0, 0});
    ecs_progress(w, 0.25);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    box_static_update_t *update = box_vec_at(&packet.static_updates, 0);
    test_flt(update->data.transform[3], 25);
}

void Display_transitioning_child_keeps_the_parent_transform(void)
{
    // transitions/transition_params
    test_int(ecs_script_run(w, "test",
        "using box.geometry\n"
        "using box.transform\n"
        "using box.transition\n"
        "parent {\n"
        "  Position: {100, 0, 0}\n"
        "  child {\n"
        "    Box: {1, 1, 1}\n"
        "    Position: {0, 0, 0}\n"
        "    Transition: [Position: {duration: 1, timing_function: Linear}]\n"
        "  }\n"
        "}\n", NULL), 0);
    ecs_entity_t child = ecs_lookup(w, "parent.child");
    test_assert(child != 0);

    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_packet_reset(&packet);

    ecs_set(w, child, Position, {40, 0, 0});
    ecs_progress(w, 0.5);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    box_static_update_t *update = box_vec_at(&packet.static_updates, 0);
    test_flt(update->data.transform[3], 120);
}

void Display_emissive_transitions_the_instance(void)
{
    // transitions/transition_params
    test_int(ecs_script_run(w, "test",
        "using box.geometry\n"
        "using box.material\n"
        "using box.transition\n"
        "e {\n"
        "  Box: {1, 1, 1}\n"
        "  Emissive: {strength: 0, color: {255, 255, 255}}\n"
        "  Transition: [Emissive: {duration: 1, timing_function: Linear}]\n"
        "}\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(w, "e");

    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_packet_reset(&packet);

    ecs_set(w, e, Emissive, {10, {255, 255, 255}});
    ecs_progress(w, 0.5);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    box_static_update_t *update = box_vec_at(&packet.static_updates, 0);
    test_flt(update->data.emissive[3], 5);
}

void Display_tint_transitions_through_the_composed_tint(void)
{
    // transitions/transition_params
    test_int(ecs_script_run(w, "test",
        "using box.geometry\n"
        "using box.material\n"
        "using box.transition\n"
        "e {\n"
        "  Box: {1, 1, 1}\n"
        "  Rgb: {255, 255, 255}\n"
        "  Tint: {255, 0, 0, 0}\n"
        "  Transition: [Tint: {duration: 1, timing_function: Linear}]\n"
        "}\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(w, "e");

    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_packet_reset(&packet);

    ecs_set(w, e, Tint, {255, 0, 0, 200});
    ecs_progress(w, 0.5);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    box_static_update_t *update = box_vec_at(&packet.static_updates, 0);
    test_flt(update->data.color[2], 1.0f - 100.0f / 255.0f);
}

void Display_transitioning_entity_is_extracted_once(void)
{
    // transitions/renderer_eval
    test_int(ecs_script_run(w, "test",
        "using box.geometry\n"
        "using box.transform\n"
        "using box.transition\n"
        "e {\n"
        "  Box: {1, 1, 1}\n"
        "  Position: {0, 0, 0}\n"
        "  Transition: [Position: {duration: 1, timing_function: Linear}]\n"
        "}\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(w, "e");

    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_packet_reset(&packet);

    ecs_set(w, e, Position, {100, 0, 0});
    ecs_progress(w, 0.25);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    test_int(packet.transitions.count, 1);

    int32_t frame;
    for (frame = 0; frame < 3; frame ++) {
        box_packet_reset(&packet);
        ecs_progress(w, 0.25);
        box_render_extract(w, &packet);
        test_int(packet.static_updates.count, 0);
        test_int(packet.transitions.count, 0);
    }
}

void Display_finished_transition_stops_reextracting(void)
{
    // transitions/transition_params
    test_int(ecs_script_run(w, "test",
        "using box.geometry\n"
        "using box.transform\n"
        "using box.transition\n"
        "e {\n"
        "  Box: {1, 1, 1}\n"
        "  Position: {0, 0, 0}\n"
        "  Transition: [Position: {duration: 1, timing_function: Linear}]\n"
        "}\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(w, "e");

    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_packet_reset(&packet);

    ecs_set(w, e, Position, {100, 0, 0});
    ecs_progress(w, 1.0);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    box_static_update_t *update = box_vec_at(&packet.static_updates, 0);
    test_flt(update->data.transform[3], 100);
    box_packet_reset(&packet);

    ecs_progress(w, 0.25);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 0);
}

void Display_entity_without_a_transition_is_not_reextracted(void)
{
    // transitions/transition_params
    test_int(ecs_script_run(w, "test",
        "using box.geometry\n"
        "using box.transform\n"
        "using box.transition\n"
        "moving {\n"
        "  Box: {1, 1, 1}\n"
        "  Position: {0, 0, 0}\n"
        "  Transition: [Position: {duration: 1, timing_function: Linear}]\n"
        "}\n"
        "still {\n"
        "  Box: {1, 1, 1}\n"
        "  Position: {5, 0, 0}\n"
        "}\n", NULL), 0);
    ecs_entity_t moving = ecs_lookup(w, "moving");

    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_packet_reset(&packet);

    ecs_set(w, moving, Position, {100, 0, 0});
    ecs_progress(w, 0.25);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    box_static_update_t *update = box_vec_at(&packet.static_updates, 0);
    test_flt(update->data.transform[3], 25);
}
