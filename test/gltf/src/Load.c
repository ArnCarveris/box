/**
 * @file Load.c
 * @brief Tests for the entity tree a Gltf builds, replaces and removes.
 */

#include <box.h>
#include <box/gltf.h>
#include <bake_test.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;
static int32_t load_errors;
static ecs_os_api_log_t load_log_prev;

/** Log callback that counts errors instead of printing them.
 *
 * @param level Log level, -3 for an error
 * @param file Source file of the message
 * @param line Source line of the message
 * @param msg The message
 */
static void load_log(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    (void)msg;
    load_errors += level == -3;
}

void Load_setup(void)
{
    load_errors = 0;
    w = ecs_init();
    load_log_prev = ecs_os_api.log_;
    ecs_os_api.log_ = load_log;
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxGltf);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
}

void Load_teardown(void)
{
    uint32_t i;
    for (i = 0; i < packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, i);

        free(cmd->vertices);
        free(cmd->indices);
    }

    for (i = 0; i < packet.texture_create.count; i ++) {
        free(((box_texture_cmd_t*)box_vec_at(&packet.texture_create, i))->pixels);
    }

    box_packet_fini(&packet);
    ecs_fini(w);
    ecs_os_api.log_ = load_log_prev;
}

void Load_loads_the_default_scene(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    ecs_entity_t root = ecs_lookup_child(w, e, "root");
    test_assert(root != 0);
    test_assert(ecs_has(w, root, GltfNode));
    test_int(load_errors, 0);
}

void Load_node_transform_is_mirrored_in_z(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    ecs_entity_t root = ecs_lookup_child(w, e, "root");
    const Position *p = ecs_get(w, root, Position);
    const Scale *s = ecs_get(w, root, Scale);
    test_assert(p != NULL);
    test_flt(p->x, 1);
    test_flt(p->y, 2);
    test_flt(p->z, -3);
    test_assert(s != NULL);
    test_flt(s->x, 2);
    test_flt(s->y, 2);
    test_flt(s->z, 2);
}

void Load_rotation_is_mirrored_in_z(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    ecs_entity_t leaf = ecs_lookup_child(w, ecs_lookup_child(w, e, "root"), "leaf");
    const Rotation *r = ecs_get(w, leaf, Rotation);
    test_assert(r != NULL);
    test_flt(r->x, 0);
    test_flt(r->y, -1.5707963f);
    test_flt(r->z, 0);
}

void Load_child_nodes_become_child_entities(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    ecs_entity_t root = ecs_lookup_child(w, e, "root");
    ecs_entity_t leaf = ecs_lookup_child(w, root, "leaf");
    test_assert(leaf != 0);
    test_assert(ecs_has_pair(w, leaf, EcsChildOf, root));

    const Position *p = ecs_get(w, leaf, Position);
    test_flt(p->y, 1);
}

void Load_one_entity_per_primitive(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    ecs_entity_t root = ecs_lookup_child(w, e, "root");
    ecs_entity_t leaf = ecs_lookup_child(w, root, "leaf");
    test_assert(ecs_lookup_child(w, root, "primitive0") != 0);
    test_assert(ecs_lookup_child(w, root, "primitive1") != 0);
    test_assert(ecs_lookup_child(w, root, "primitive2") == 0);
    test_assert(ecs_lookup_child(w, leaf, "primitive0") != 0);
    test_assert(ecs_lookup_child(w, leaf, "primitive1") == 0);
}

void Load_named_scene_is_loaded(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, {
        .path = ecs_os_strdup("etc/scene.gltf"),
        .scene = ecs_os_strdup("other")
    });

    test_assert(ecs_lookup_child(w, e, "lonely") != 0);
    test_assert(ecs_lookup_child(w, e, "root") == 0);
    test_int(load_errors, 0);
}

void Load_unknown_scene_is_reported(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, {
        .path = ecs_os_strdup("etc/scene.gltf"),
        .scene = ecs_os_strdup("nope")
    });

    test_int(load_errors, 1);
    test_assert(ecs_lookup_child(w, e, "root") == 0);
}

void Load_missing_file_is_reported(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/absent.gltf") });

    test_int(load_errors, 1);
    test_assert(ecs_lookup_child(w, e, "root") == 0);
}

void Load_scale_scales_the_root_nodes(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf"), .scale = 0.5f });

    ecs_entity_t root = ecs_lookup_child(w, e, "root");
    const Position *p = ecs_get(w, root, Position);
    const Scale *s = ecs_get(w, root, Scale);
    test_flt(p->x, 0.5f);
    test_flt(p->z, -1.5f);
    test_flt(s->x, 1);

    const Position *leaf = ecs_get(w, ecs_lookup_child(w, root, "leaf"), Position);
    test_flt(leaf->y, 1);
}

void Load_setting_another_path_replaces_the_tree(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    ecs_entity_t root = ecs_lookup_child(w, e, "root");
    test_assert(root != 0);

    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/external.gltf") });
    test_assert(!ecs_is_alive(w, root));
    test_assert(ecs_lookup_child(w, e, "root") == 0);
    test_assert(ecs_lookup_child(w, e, "plate") != 0);
    test_int(load_errors, 0);
}

void Load_removing_the_component_removes_the_tree(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    ecs_entity_t root = ecs_lookup_child(w, e, "root");
    ecs_entity_t textures = ecs_lookup_child(w, e, "textures");
    test_assert(root != 0);
    test_assert(textures != 0);

    ecs_remove(w, e, Gltf);
    test_assert(ecs_is_alive(w, e));
    test_assert(!ecs_is_alive(w, root));
    test_assert(!ecs_is_alive(w, textures));
}

void Load_deleting_the_entity_removes_the_tree(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    ecs_entity_t root = ecs_lookup_child(w, e, "root");
    ecs_entity_t leaf = ecs_lookup_child(w, root, "leaf");
    test_assert(leaf != 0);

    ecs_delete(w, e);
    test_assert(!ecs_is_alive(w, root));
    test_assert(!ecs_is_alive(w, leaf));
}

void Load_application_children_are_left_alone(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_entity_t mine = ecs_entity(w, { .name = "mine", .parent = e });
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });
    test_assert(ecs_is_alive(w, mine));

    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/external.gltf") });
    test_assert(ecs_is_alive(w, mine));

    ecs_remove(w, e, Gltf);
    test_assert(ecs_is_alive(w, mine));
    test_assert(ecs_lookup_child(w, e, "mine") == mine);
}
