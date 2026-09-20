/**
 * @file Renderer.c
 * @brief Tests for the transition records the renderer thread evaluates.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void Renderer_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
}

void Renderer_teardown(void)
{
    box_packet_fini(&packet);
    ecs_fini(w);
}

void Renderer_static_entity_sends_a_transition_command(void)
{
    // transitions/renderer_eval
    test_int(ecs_script_run(w, "test",
        "using box.geometry\n"
        "using box.transform\n"
        "using box.transition\n"
        "still {\n"
        "  Box: {1, 1, 1}\n"
        "  Position: {5, 0, 0}\n"
        "}\n"
        "e {\n"
        "  Box: {1, 1, 1}\n"
        "  Position: {0, 0, 0}\n"
        "  Transition: [Position: {duration: 2, timing_function: Linear}]\n"
        "}\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(w, "e");

    box_render_extract(w, &packet);
    box_mesh_cmd_t *mesh = box_vec_at(&packet.mesh_create, 0);
    free(mesh->vertices);
    free(mesh->indices);
    box_packet_reset(&packet);

    ecs_set(w, e, Position, {100, 0, 0});
    ecs_progress(w, 0.5);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    test_int(packet.transitions.count, 1);
    test_int(packet.transition_records.count, 1);

    box_static_update_t *update = box_vec_at(&packet.static_updates, 0);
    test_assert(update->transition != BOX_UPDATE_STILL);

    box_transition_cmd_t *cmd = box_vec_at(&packet.transitions, 0);
    test_int(cmd->count, 1);
    test_int(cmd->first, 0);
    test_int(cmd->posed, 1);
    test_int(cmd->mesh, update->mesh);
    test_int(cmd->slot, update->slot);
    test_assert(cmd->slot != 0);
    test_flt(cmd->until, 2.0);

    box_transition_rec_t *rec = box_vec_at(&packet.transition_records, 0);
    test_int(rec->channel, BOX_DISPLAY_POSITION);
    test_int(rec->first, 0);
    test_int(rec->count, 3);
    test_flt(rec->from[0], 0);
    test_flt(rec->to[0], 100);
    test_flt(rec->duration, 2);
}

void Renderer_entity_without_a_transition_sends_none(void)
{
    // transitions/renderer_eval
    test_int(ecs_script_run(w, "test",
        "using box.geometry\n"
        "using box.transform\n"
        "e {\n"
        "  Box: {1, 1, 1}\n"
        "  Position: {0, 0, 0}\n"
        "}\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(w, "e");

    box_render_extract(w, &packet);
    box_mesh_cmd_t *mesh = box_vec_at(&packet.mesh_create, 0);
    free(mesh->vertices);
    free(mesh->indices);
    box_packet_reset(&packet);

    ecs_set(w, e, Position, {100, 0, 0});
    ecs_progress(w, 0.5);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    test_int(packet.transitions.count, 0);

    box_static_update_t *update = box_vec_at(&packet.static_updates, 0);
    test_int(update->transition, BOX_UPDATE_STILL);
}

void Renderer_computes_the_position_at_a_time(void)
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
    box_mesh_cmd_t *mesh = box_vec_at(&packet.mesh_create, 0);
    free(mesh->vertices);
    free(mesh->indices);
    box_packet_reset(&packet);

    ecs_set(w, e, Position, {100, 0, 0});
    box_render_extract(w, &packet);

    box_static_update_t *update = box_vec_at(&packet.static_updates, 0);
    box_transition_cmd_t *cmd = box_vec_at(&packet.transitions, 0);
    box_transition_rec_t *rec = box_vec_at(&packet.transition_records, cmd->first);
    box_instance_t inst = update->data;

    test_bool(box_transition_fill(cmd, rec, 0.25, &inst), true);
    test_flt(inst.transform[3], 25);
    test_bool(box_transition_fill(cmd, rec, 0.75, &inst), true);
    test_flt(inst.transform[3], 75);
}

void Renderer_computes_the_color_at_a_time(void)
{
    // transitions/renderer_eval
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
    box_mesh_cmd_t *mesh = box_vec_at(&packet.mesh_create, 0);
    free(mesh->vertices);
    free(mesh->indices);
    box_packet_reset(&packet);

    ecs_set(w, e, Rgb, {200, 0, 0});
    box_render_extract(w, &packet);

    box_static_update_t *update = box_vec_at(&packet.static_updates, 0);
    box_transition_cmd_t *cmd = box_vec_at(&packet.transitions, 0);
    box_transition_rec_t *rec = box_vec_at(&packet.transition_records, cmd->first);
    box_instance_t inst = update->data;

    test_int(cmd->posed, 0);
    test_bool(box_transition_fill(cmd, rec, 0.5, &inst), true);
    test_flt(inst.color[0], box_srgb_to_linear(100));
}

void Renderer_keeps_the_final_value_when_it_ends(void)
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
    box_mesh_cmd_t *mesh = box_vec_at(&packet.mesh_create, 0);
    free(mesh->vertices);
    free(mesh->indices);
    box_packet_reset(&packet);

    ecs_set(w, e, Position, {100, 0, 0});
    box_render_extract(w, &packet);

    box_static_update_t *update = box_vec_at(&packet.static_updates, 0);
    box_transition_cmd_t *cmd = box_vec_at(&packet.transitions, 0);
    box_transition_rec_t *rec = box_vec_at(&packet.transition_records, cmd->first);
    box_instance_t inst = update->data;

    test_bool(box_transition_fill(cmd, rec, 1.0, &inst), false);
    test_flt(inst.transform[3], 100);
    test_bool(box_transition_fill(cmd, rec, 4.0, &inst), false);
    test_flt(inst.transform[3], 100);
}

void Renderer_completion_report_clears_the_bookkeeping(void)
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
    box_mesh_cmd_t *mesh = box_vec_at(&packet.mesh_create, 0);
    free(mesh->vertices);
    free(mesh->indices);
    box_packet_reset(&packet);

    ecs_set(w, e, Position, {100, 0, 0});
    box_render_extract(w, &packet);
    test_bool(ecs_get(w, e, TransitionState)->dirty, true);

    const RenderInstance *inst = ecs_get(w, e, RenderInstance);
    box_transition_done_t *done = box_vec_push(&packet.transitions_done);

    done->mesh = inst->mesh;
    done->slot = inst->slot;
    box_render_extract(w, &packet);
    test_int(packet.transitions_done.count, 0);
    test_bool(ecs_get(w, e, TransitionState)->dirty, false);
}

void Renderer_member_transition_moves_one_float(void)
{
    // transitions/renderer_eval
    test_int(ecs_script_run(w, "test",
        "using box.geometry\n"
        "using box.transform\n"
        "using box.transition\n"
        "e {\n"
        "  Box: {1, 1, 1}\n"
        "  Position: {0, 0, 0}\n"
        "  Transition: [Position.x: {duration: 1, timing_function: Linear}]\n"
        "}\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(w, "e");

    box_render_extract(w, &packet);
    box_mesh_cmd_t *mesh = box_vec_at(&packet.mesh_create, 0);
    free(mesh->vertices);
    free(mesh->indices);
    box_packet_reset(&packet);

    ecs_set(w, e, Position, {100, 40, 0});
    box_render_extract(w, &packet);
    test_int(packet.transitions.count, 1);

    box_static_update_t *update = box_vec_at(&packet.static_updates, 0);
    box_transition_cmd_t *cmd = box_vec_at(&packet.transitions, 0);
    box_transition_rec_t *rec = box_vec_at(&packet.transition_records, cmd->first);

    test_int(cmd->count, 1);
    test_int(rec->channel, BOX_DISPLAY_POSITION);
    test_int(rec->first, 0);
    test_int(rec->count, 1);

    box_instance_t inst = update->data;

    test_bool(box_transition_fill(cmd, rec, 0.5, &inst), true);
    test_flt(inst.transform[3], 50);
    test_flt(inst.transform[7], 40);
}

void Renderer_split_channel_stays_on_the_game_side(void)
{
    // transitions/renderer_eval
    test_int(ecs_script_run(w, "test",
        "using box.geometry\n"
        "using box.transform\n"
        "using box.transition\n"
        "e {\n"
        "  Box: {1, 1, 1}\n"
        "  Position: {0, 0, 0}\n"
        "  Transition: [\n"
        "    Position: {duration: 1, timing_function: Linear},\n"
        "    Position.x: {duration: 2, timing_function: Linear}\n"
        "  ]\n"
        "}\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(w, "e");

    box_render_extract(w, &packet);
    box_mesh_cmd_t *mesh = box_vec_at(&packet.mesh_create, 0);
    free(mesh->vertices);
    free(mesh->indices);
    box_packet_reset(&packet);

    ecs_set(w, e, Position, {100, 100, 0});
    ecs_progress(w, 0.5);
    box_render_extract(w, &packet);
    test_int(packet.transitions.count, 0);
    test_int(packet.static_updates.count, 1);

    box_static_update_t *update = box_vec_at(&packet.static_updates, 0);

    test_int(update->transition, BOX_UPDATE_STILL);
    test_flt(update->data.transform[3], 25);
    test_flt(update->data.transform[7], 50);
    box_packet_reset(&packet);

    ecs_progress(w, 0.5);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    update = box_vec_at(&packet.static_updates, 0);
    test_flt(update->data.transform[3], 50);
    test_flt(update->data.transform[7], 100);
}
