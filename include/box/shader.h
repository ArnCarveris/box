/**
 * @file box/shader.h
 * @brief Shader components: a WGSL file with includes and macros, compiled by the renderer.
 *
 * Setting a Shader reads its file, runs the preprocessor over it and hands the
 * result to the renderer, which compiles a module and rebuilds the pipelines
 * that use the shader. A file that does not preprocess leaves the previous
 * shader in place and reports the file and line through ShaderStatus.
 *
 * Paths are resolved against the shader search path: the working directory,
 * the framework assets bake3 installed at `<BAKE_HOME>/etc/box`, the nearest
 * ancestor of the working directory with an `etc/shaders`, `apps/box_app` at or
 * above it and the directory of the executable, in that order. The shaders of
 * the engine live in the `etc` of the framework, an application keeps its own
 * under its `etc` and a file it ships wins over the engine copy when the
 * application runs from its own folder. On wasm both are embedded under `etc`,
 * which the working directory entry finds.
 *
 * The preprocessor understands `#include "file"` relative to the including file,
 * `#define NAME value` with identifier substitution, `#if NAME`, `#ifdef NAME`,
 * `#ifndef NAME`, `#else`, `#endif` and `#pragma once`. An undefined macro is 0
 * in a condition. The engine predefines the constants the renderer and the
 * shaders share, such as SHADOW_CASCADE_MAX and ENV_MIPS.
 *
 * The builtin shaders of the engine live under `box.shader.builtin`, so
 * reloading the mesh shader is a component write on `box.shader.builtin.mesh`.
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

/** WGSL source file of one shader, preprocessed and compiled when it is set */
ECS_STRUCT(Shader, {
    char *path;   /**< WGSL file, resolved against the shader search path */
    char *macros; /**< Comma separated NAME=value list, empty for none */
});

/** Outcome of the last preprocessing of a Shader, written by the engine */
ECS_STRUCT(ShaderStatus, {
    bool ok;     /**< The file and everything it includes preprocessed */
    char *error; /**< First error with its file and line, empty when ok */
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
