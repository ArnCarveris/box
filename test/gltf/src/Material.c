/**
 * @file Material.c
 * @brief Tests for the material components and texture entities a glTF file produces.
 */

#include <box.h>
#include <box/gltf.h>
#include <bake_test.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;
static int32_t material_errors;
static ecs_os_api_log_t material_log_prev;

/** Log callback that counts errors instead of printing them.
 *
 * @param level Log level, -3 for an error
 * @param file Source file of the message
 * @param line Source line of the message
 * @param msg The message
 */
static void material_log(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    (void)msg;
    material_errors += level == -3;
}

void Material_setup(void)
{
    material_errors = 0;
    w = ecs_init();
    material_log_prev = ecs_os_api.log_;
    ecs_os_api.log_ = material_log;
    ECS_IMPORT(w, BoxGeometry);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
}

void Material_teardown(void)
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
    ecs_os_api.log_ = material_log_prev;
}

void Material_base_color_becomes_rgb(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    ecs_entity_t root = ecs_lookup_child(w, e, "root");
    const Rgb *rgb = ecs_get(w, ecs_lookup_child(w, root, "primitive0"), Rgb);
    test_assert(rgb != NULL);
    test_int(rgb->r, 255);
    test_int(rgb->g, 0);
    test_int(rgb->b, 0);
}

void Material_metallic_and_roughness_are_kept(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    ecs_entity_t root = ecs_lookup_child(w, e, "root");
    const PbrMaterial *pbr = ecs_get(w, ecs_lookup_child(w, root, "primitive0"), PbrMaterial);
    test_assert(pbr != NULL);
    test_flt(pbr->metallic, 0.25f);
    test_flt(pbr->roughness, 0.75f);
}

void Material_alpha_becomes_opacity_and_blend_mode(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    ecs_entity_t root = ecs_lookup_child(w, e, "root");
    ecs_entity_t leaf = ecs_lookup_child(w, root, "leaf");
    ecs_entity_t prim = ecs_lookup_child(w, leaf, "primitive0");
    const Opacity *opacity = ecs_get(w, prim, Opacity);
    const BlendMode *blend = ecs_get(w, prim, BlendMode);
    test_assert(opacity != NULL);
    test_flt(opacity->value, 0.5f);
    test_assert(blend != NULL);
    test_int(*blend, AlphaBlend);
}

void Material_double_sided_becomes_a_back_face_mode(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    ecs_entity_t root = ecs_lookup_child(w, e, "root");
    ecs_entity_t leaf = ecs_lookup_child(w, root, "leaf");
    ecs_entity_t prim = ecs_lookup_child(w, leaf, "primitive0");
    const BackFaceMode *faces = ecs_get(w, prim, BackFaceMode);
    test_assert(faces != NULL);
    test_int(*faces, BackFaceTextured);

    ecs_entity_t opaque = ecs_lookup_child(w, root, "primitive0");
    test_assert(ecs_get(w, opaque, BackFaceMode) == NULL);
}

void Material_one_texture_entity_per_image(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    ecs_entity_t textures = ecs_lookup_child(w, e, "textures");
    test_assert(textures != 0);

    ecs_entity_t paint = ecs_lookup_child(w, textures, "paint");
    test_assert(paint != 0);
    test_assert(ecs_has(w, paint, GltfNode));
    test_assert(ecs_lookup_child(w, textures, "image1") == 0);
}

void Material_albedo_slot_binds_the_image(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    ecs_entity_t textures = ecs_lookup_child(w, e, "textures");
    ecs_entity_t paint = ecs_lookup_child(w, textures, "paint");
    ecs_entity_t root = ecs_lookup_child(w, e, "root");
    const PbrTextures *slots = ecs_get(w, ecs_lookup_child(w, root, "primitive1"), PbrTextures);
    test_assert(slots != NULL);
    test_uint(slots->albedo, paint);
    test_uint(slots->normal, 0);
    test_uint(slots->emissive, 0);
}

void Material_embedded_image_is_uploaded(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    ecs_entity_t textures = ecs_lookup_child(w, e, "textures");
    const RenderTexture *rt = ecs_get(w, ecs_lookup_child(w, textures, "paint"), RenderTexture);
    test_assert(rt != NULL);
    test_assert(rt->handle != 0);
    test_int(rt->width, 256);

    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 1);
    test_int(material_errors, 0);
}

void Material_referenced_image_is_loaded(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/external.gltf") });

    ecs_entity_t textures = ecs_lookup_child(w, e, "textures");
    ecs_entity_t image = ecs_lookup_child(w, textures, "image0");
    test_assert(image != 0);

    const Texture *texture = ecs_get(w, image, Texture);
    test_assert(texture != NULL);
    test_str(texture->path, "etc/checker.png");

    const RenderTexture *rt = ecs_get(w, image, RenderTexture);
    test_assert(rt->handle != 0);
    test_int(material_errors, 0);
}

void Material_untextured_primitive_has_no_slots(void)
{
    // meshes/gltf
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Gltf, { .path = ecs_os_strdup("etc/scene.gltf") });

    ecs_entity_t root = ecs_lookup_child(w, e, "root");
    test_assert(ecs_get(w, ecs_lookup_child(w, root, "primitive0"), PbrTextures) == NULL);
    test_assert(ecs_get(w, ecs_lookup_child(w, root, "primitive0"), Emissive) == NULL);
}
