/**
 * @file box/shader.h
 * @brief Shader component: a WGSL file with includes and macros that the renderer compiles.
 *
 * The file is preprocessed on the main thread and compiled on the render thread; a change of
 * the component, or of a file it includes as the FileWatch of box.app (box/app.h) sees it,
 * recompiles it. Engine shaders live in etc/shaders.
 */

#ifndef BOX_SHADER_H
#define BOX_SHADER_H

#include "types.h"

#ifdef BOX_SHADER_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** A WGSL file the renderer compiles, preprocessed with its defines.
 *
 * A relative path is looked up in the working directory, then in its ancestors, then in the
 * folder of the executable and its ancestors.
 *
 * The preprocessor resolves #include "file" (relative to the including file, then like a
 * path), #define NAME value, #undef, #ifdef, #ifndef, #else, #endif and #pragma once.
 */
ECS_STRUCT(Shader, {
    char *path;     /**< WGSL file, found from the working directory or the executable up */
    char *defines;  /**< Macros defined first, NAME or NAME=value separated by commas */
});

/** Outcome of the last preprocessing and GPU compile of a Shader, written by the engine.
 *
 * A GPU error stays until the compile outcome of newer code arrives, which in the browser
 * can take longer than a frame.
 */
ECS_STRUCT(ShaderStatus, {
    int32_t revision;   /**< Times preprocessing produced new code */
    bool ok;            /**< The last preprocessing succeeded and no GPU error is reported */
    char *error;        /**< First error as file:line: message, NULL when ok */
});

/** Register the box.shader module.
 *
 * @param world The world
 */
void BoxShaderImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
