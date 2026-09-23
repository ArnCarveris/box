/**
 * @file box/shader.h
 * @brief Shader components: a WGSL file with includes and macros, compiled by the renderer.
 *
 * Setting a Shader preprocesses its file and hands it to the renderer; a failure
 * keeps the previous shader and reports file and line through ShaderStatus.
 * Builtin shaders live under `box.shader.builtin`.
 *
 * The preprocessor supports `#include`, `#define`, `#if`, `#ifdef`, `#ifndef`,
 * `#else`, `#endif` and `#pragma once`; an undefined macro is 0. Shared engine
 * constants such as SHADOW_CASCADE_MAX and ENV_MIPS are predefined.
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
