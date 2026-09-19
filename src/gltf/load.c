/**
 * @file gltf/load.c
 * @brief Parses a glTF file and builds the entity tree of one of its scenes.
 *
 * A node of the scene becomes a child entity with Position, Rotation and Scale,
 * and every primitive of its mesh becomes a child of that entity with a Mesh3
 * and a material. Everything the loader creates carries GltfNode, which is what
 * a reload or a removed component deletes again.
 */

#include "gltf_internal.h"
#include <string.h>

/** Explain a parse failure in the log.
 *
 * @param result The cgltf result
 * @return A short description of the failure
 */
static const char* box_gltf_reason(
    cgltf_result result)
{
    const char *reasons[] = {
        "success", "the file is too short", "the format is unknown",
        "the json is invalid", "the gltf is invalid", "the options are invalid",
        "the file was not found", "the file could not be read", "out of memory",
        "the gltf version is too old"
    };

    return result < cgltf_result_max_enum ? reasons[result] : "unknown";
}

/** Directory a path lives in, ending in a separator, empty when it has none.
 *
 * @param path The file path
 * @return The directory prefix, owned by the caller
 */
static char* box_gltf_dir(
    const char *path)
{
    const char *slash = strrchr(path, '/');
    ecs_size_t length = slash ? (ecs_size_t)(slash - path) + 1 : 0;
    char *result = ecs_os_malloc(length + 1);

    ecs_os_memcpy(result, path, length);
    result[length] = '\0';

    return result;
}

/** The scene of a file the component asks for.
 *
 * @param data The parsed file
 * @param name Name of the scene, NULL or empty for the default one
 * @return The scene, NULL when the file holds no scene of that name
 */
static const cgltf_scene* box_gltf_scene(
    const cgltf_data *data,
    const char *name)
{
    const cgltf_scene *result = NULL;
    cgltf_size i;

    if (name && name[0]) {
        for (i = 0; i < data->scenes_count && !result; i ++) {
            if (data->scenes[i].name && !strcmp(data->scenes[i].name, name)) {
                result = &data->scenes[i];
            }
        }
    } else if (data->scene) {
        result = data->scene;
    } else if (data->scenes_count) {
        result = &data->scenes[0];
    }

    return result;
}

void box_gltf_name(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    const char *fallback,
    char *out)
{
    int32_t i = 0;

    while (name && name[i] && i < 63) {
        char c = name[i];
        bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_';

        out[i] = keep ? c : '_';
        i ++;
    }

    out[i] = '\0';
    if (!i || (out[0] >= '0' && out[0] <= '9') || ecs_lookup_child(world, parent, out)) {
        ecs_os_strcpy(out, fallback);
    }
}

/** Local transform of a node, mirrored in Z and split into box components.
 *
 * The mirror that turns glTF axes into box axes is the conjugation of the local
 * matrix with diag(1, 1, -1), which negates every element whose row or column
 * is the Z one, but not the one where both are.
 *
 * @param node The glTF node
 * @param position Receives the translation
 * @param rotation Receives the Euler angles the Rotation component holds
 * @param scale Receives the scale
 */
static void box_gltf_transform(
    const cgltf_node *node,
    vec3 *position,
    vec3 *rotation,
    vec3 *scale)
{
    cgltf_float m[16];
    mat4 basis = mat4_identity();
    vec3 axis[3];
    float length[3];
    int32_t c;

    cgltf_node_transform_local(node, m);
    m[2] = -m[2];
    m[6] = -m[6];
    m[14] = -m[14];
    m[8] = -m[8];
    m[9] = -m[9];
    m[11] = -m[11];

    *position = vec3_new(m[12], m[13], m[14]);
    for (c = 0; c < 3; c ++) {
        axis[c] = vec3_new(m[c * 4], m[c * 4 + 1], m[c * 4 + 2]);
        length[c] = vec3_length(axis[c]);
    }

    if (vec3_dot(vec3_cross(axis[0], axis[1]), axis[2]) < 0) {
        length[0] = -length[0];
    }

    *scale = vec3_new(length[0], length[1], length[2]);
    for (c = 0; c < 3; c ++) {
        vec3 n = length[c] != 0 ? vec3_scale(axis[c], 1.0f / length[c]) : axis[c];

        basis.v[c * 4] = n.x;
        basis.v[c * 4 + 1] = n.y;
        basis.v[c * 4 + 2] = n.z;
    }

    *rotation = mat4_to_euler_xyz(&basis);
}

/** Create one entity per primitive of the mesh of a node.
 *
 * @param ctx The load
 * @param parent The entity of the node
 * @param mesh The glTF mesh
 */
static void box_gltf_primitives(
    box_gltf_ctx_t *ctx,
    ecs_entity_t parent,
    const cgltf_mesh *mesh)
{
    cgltf_size i;
    for (i = 0; i < mesh->primitives_count; i ++) {
        const cgltf_primitive *prim = &mesh->primitives[i];
        char name[64];

        ecs_os_snprintf(name, sizeof(name), "primitive%d", (int32_t)i);

        ecs_entity_t e = ecs_entity(ctx->world, { .name = name, .parent = parent });
        ecs_add(ctx->world, e, GltfNode);
        if (box_gltf_mesh(ctx->world, e, prim)) {
            ecs_set(ctx->world, e, Position, {0, 0, 0});
            box_gltf_material(ctx, e, prim->material);
        } else {
            ecs_delete(ctx->world, e);
        }
    }
}

/** Create the entity of a node and of everything below it.
 *
 * @param ctx The load
 * @param node The glTF node
 * @param parent The entity the node becomes a child of
 * @param scale Uniform scale of the root nodes, one below them
 */
static void box_gltf_node(
    box_gltf_ctx_t *ctx,
    const cgltf_node *node,
    ecs_entity_t parent,
    float scale)
{
    vec3 position, rotation, size;
    char fallback[32];
    char name[64];
    cgltf_size i;

    ecs_os_snprintf(fallback, sizeof(fallback), "node%d",
        (int32_t)cgltf_node_index(ctx->data, node));
    box_gltf_name(ctx->world, parent, node->name, fallback, name);

    ecs_entity_t e = ecs_entity(ctx->world, { .name = name, .parent = parent });
    ecs_add(ctx->world, e, GltfNode);
    box_gltf_transform(node, &position, &rotation, &size);
    ecs_set(ctx->world, e, Position,
        { position.x * scale, position.y * scale, position.z * scale });
    ecs_set(ctx->world, e, Rotation, { rotation.x, rotation.y, rotation.z });
    ecs_set(ctx->world, e, Scale, { size.x * scale, size.y * scale, size.z * scale });

    if (node->mesh) {
        box_gltf_primitives(ctx, e, node->mesh);
    }

    for (i = 0; i < node->children_count; i ++) {
        box_gltf_node(ctx, node->children[i], e, 1.0f);
    }
}

void box_gltf_unload(
    ecs_world_t *world,
    ecs_world_t *stage,
    ecs_entity_t e)
{
    ecs_entity_t textures = 0;
    ecs_vec_t doomed;
    int32_t i;

    ecs_vec_init_t(NULL, &doomed, ecs_entity_t, 0);

    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            if (!ecs_has(world, it.entities[i], GltfNode)) {
                continue;
            }

            if (ecs_has(world, it.entities[i], GltfTextures)) {
                textures = it.entities[i];
            } else {
                ecs_vec_append_t(NULL, &doomed, ecs_entity_t)[0] = it.entities[i];
            }
        }
    }

    for (i = 0; i < ecs_vec_count(&doomed); i ++) {
        ecs_delete(stage, ecs_vec_get_t(&doomed, ecs_entity_t, i)[0]);
    }

    ecs_vec_fini_t(NULL, &doomed, ecs_entity_t);
    if (textures) {
        ecs_delete(stage, textures);
    }
}

void box_gltf_load(
    ecs_world_t *world,
    ecs_entity_t e,
    const Gltf *gltf)
{
    cgltf_options options = {0};
    cgltf_data *data = NULL;
    const cgltf_scene *scene = NULL;
    box_gltf_ctx_t ctx = {0};
    cgltf_result result;
    cgltf_size i;

    box_gltf_unload(world, world, e);
    if (!gltf->path || !gltf->path[0]) {
        goto done;
    }

    result = cgltf_parse_file(&options, gltf->path, &data);
    if (result != cgltf_result_success) {
        ecs_err("box.gltf: cannot load '%s': %s", gltf->path, box_gltf_reason(result));
        goto done;
    }

    result = cgltf_load_buffers(&options, data, gltf->path);
    if (result != cgltf_result_success) {
        ecs_err("box.gltf: cannot load the buffers of '%s': %s",
            gltf->path, box_gltf_reason(result));
        goto done;
    }

    scene = box_gltf_scene(data, gltf->scene);
    if (!scene) {
        ecs_err("box.gltf: '%s' holds no scene '%s'", gltf->path, gltf->scene);
        goto done;
    }

    ctx.world = world;
    ctx.owner = e;
    ctx.data = data;
    ctx.dir = box_gltf_dir(gltf->path);
    ctx.scale = gltf->scale > 0 ? gltf->scale : 1.0f;

    box_gltf_images(&ctx);
    for (i = 0; i < scene->nodes_count; i ++) {
        box_gltf_node(&ctx, scene->nodes[i], e, ctx.scale);
    }

    ecs_os_free(ctx.dir);
    ecs_os_free(ctx.images);
done:
    cgltf_free(data);
}
