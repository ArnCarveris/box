/**
 * @file shader/shader_internal.h
 * @brief Private state of the box.shader module: the search path and the preprocessor.
 */

#ifndef BOX_SHADER_INTERNAL_H
#define BOX_SHADER_INTERNAL_H

#include <box.h>
#include "../render/render_internal.h"

/** Directory prefix of the includes the engine generates instead of reading */
#define BOX_SHADER_GENERATED "generated/"

/** Longest shader path the search path builds */
#define BOX_SHADER_PATH_MAX (1024)

/** Renderer side handle of a Shader entity */
typedef struct ShaderImpl {
    uint32_t handle; /**< Renderer shader handle, valid once it was assigned */
    bool assigned;   /**< The handle was taken, which a builtin slot does up front */
} ShaderImpl;

extern ECS_COMPONENT_DECLARE(ShaderImpl);

/** Shader commands waiting for the next frame packet, stored as a singleton */
typedef struct ShaderState {
    ecs_vec_t pending; /**< box_shader_cmd_t waiting for the next packet */
    uint32_t count;    /**< Shader handles handed out, the builtin slots come first */
} ShaderState;

extern ECS_COMPONENT_DECLARE(ShaderState);

/** Queue a preprocessed source for the renderer.
 *
 * A handle that already has a pending source keeps it, so setting a shader more
 * than once between two packets sends the last source only.
 *
 * @param world The world
 * @param handle Handle to replace, BOX_SHADER_NONE to allocate a new one
 * @param source The preprocessed WGSL, copied
 * @return The shader handle
 */
uint32_t box_shader_create(
    ecs_world_t *world,
    uint32_t handle,
    const char *source);

/** Move the pending shader commands into a packet.
 *
 * @param world The world
 * @param p The packet
 */
void box_shader_extract(
    ecs_world_t *world,
    box_packet_t *p);

/** Preprocessed source or the first error of a preprocessor run */
typedef struct box_shader_result_t {
    char *source; /**< Preprocessed WGSL, NULL when the run failed */
    char *error;  /**< "file:line: message" of the first error, NULL when it succeeded */
} box_shader_result_t;

/** Build the search path of the engine from the environment.
 *
 * The module import calls this, so a process that changes `BAKE_HOME` or its
 * working directory picks the change up by calling it again.
 */
void box_shader_path_init(void);

/** Resolve a shader path against the search path of the engine.
 *
 * The entries are the path as written, `<BAKE_HOME>/etc/box` for a path under
 * `etc/`, the nearest ancestor of the working directory with an `etc/shaders`,
 * `apps/box_app` at or above it and the directory of the executable, in that
 * order. An absolute path is used as it is.
 *
 * @param path The path as the component spells it
 * @param out Receives the readable path
 * @param size Bytes of the output buffer
 * @return true when a readable file was found
 */
bool box_shader_resolve(
    const char *path,
    char *out,
    size_t size);

/** Read a file into a newly allocated, zero terminated buffer.
 *
 * @param path The file
 * @return The contents, or NULL when the file could not be read
 */
char* box_shader_read(
    const char *path);

/** Read a file into a newly allocated buffer of its own bytes.
 *
 * @param path The file
 * @param size Receives the bytes that were read, zero when it could not be read
 * @return The contents, or NULL when the file could not be read
 */
void* box_shader_read_bytes(
    const char *path,
    size_t *size);

/** Preprocess a WGSL file into one source.
 *
 * @param world The world, which supplies the generated includes, NULL for none
 * @param path The file as it is spelled, resolved against the search path
 * @param macros Comma separated NAME=value list of the component, NULL for none
 * @param out Receives the source or the error, both owned by the caller
 */
void box_shader_preprocess(
    ecs_world_t *world,
    const char *path,
    const char *macros,
    box_shader_result_t *out);

/** Release the strings of a preprocessor result.
 *
 * @param result The result
 */
void box_shader_result_fini(
    box_shader_result_t *result);

/** Build the generated include a name asks for.
 *
 * @param world The world, NULL when there is none
 * @param name The include name without the generated prefix
 * @return The source, owned by the caller, or NULL when the name is unknown
 */
char* box_shader_generated(
    ecs_world_t *world,
    const char *name);

/** Build the WGSL of the registered material layers of a world.
 *
 * The body of every registration becomes `layer_<index>` and `apply_layers`
 * dispatches the ones the layer mask of an instance names, which is the
 * composition the renderer used to do when it recomposed the mesh shader.
 *
 * @param world The world
 * @return The source, owned by the caller
 */
char* box_shader_layers_wgsl(
    ecs_world_t *world);

/** Preprocess a Shader entity again and send the result to the renderer.
 *
 * @param world The world
 * @param e The shader entity
 */
void box_shader_reload(
    ecs_world_t *world,
    ecs_entity_t e);

/** Reload the builtin mesh shader after the material layer list changed.
 *
 * @param world The world
 */
void box_shader_layers_changed(
    ecs_world_t *world);

/** Build the WGSL of the registered light modulators of a world.
 *
 * Every registration becomes `box_light_modulate_<index>`, spliced from the
 * file of its plugin, and `box_light_modulate_all` multiplies the ones the
 * modulator mask of a light names.
 *
 * @param world The world
 * @return The source, owned by the caller
 */
char* box_shader_modulators_wgsl(
    ecs_world_t *world);

/** Reload the builtin mesh shader after the light modulator list changed.
 *
 * @param world The world
 */
void box_shader_modulators_changed(
    ecs_world_t *world);

#endif
