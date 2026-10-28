/**
 * @file render/preprocess.h
 * @brief WGSL preprocessor of the box.shader module: includes, macros and conditionals.
 *
 * Plain C without GPU access; it reads files from disk or through a callback.
 */

#ifndef BOX_RENDER_PREPROCESS_H
#define BOX_RENDER_PREPROCESS_H

#include <box.h>

/** Deepest nesting of included files */
#define BOX_SHADER_INCLUDE_DEPTH (32)

/** Deepest nesting of macros expanded inside macros */
#define BOX_SHADER_EXPAND_DEPTH (16)

/** Reads a file for the preprocessor.
 *
 * @param ctx The context of the preprocessing
 * @param path The file
 * @return The contents allocated with ecs_os_malloc, NULL when the file does not exist
 */
typedef char* (*box_shader_read_t)(
    void *ctx,
    const char *path);

/** Modification time and size of a file, to see whether it changed */
typedef struct box_shader_stamp_t {
    int64_t sec;    /**< Seconds of the modification time, -1 when the file is missing */
    int64_t nsec;   /**< Nanoseconds of the modification time */
    int64_t size;   /**< Bytes of the file */
} box_shader_stamp_t;

/** A file a preprocessing read or looked for */
typedef struct box_shader_file_t {
    char *path;                 /**< The file as it was opened */
    box_shader_stamp_t stamp;   /**< Stamp taken before reading, unset with a read callback */
} box_shader_file_t;

/** What to preprocess */
typedef struct box_shader_desc_t {
    const char *path;           /**< Main file */
    const char *defines;        /**< NAME or NAME=value separated by commas, NULL for none */
    box_shader_read_t read;     /**< Reads files, NULL reads them from disk */
    void *ctx;                  /**< Passed to read */
} box_shader_desc_t;

/** Source position of a line of preprocessed code */
typedef struct box_shader_loc_t {
    int32_t file;       /**< Index in the sources of the output */
    int32_t line;       /**< Line in that file, starting at 1 */
} box_shader_loc_t;

/** Result of a preprocessing */
typedef struct box_shader_output_t {
    char *code;         /**< Preprocessed code, NULL on error */
    char *error;        /**< First error as file:line: message, NULL on success */
    ecs_vec_t files;    /**< box_shader_file_t, the main file first */
    ecs_vec_t sources;  /**< char*, the files lines of the code come from */
    ecs_vec_t lines;    /**< box_shader_loc_t, source position of every line of the code */
} box_shader_output_t;

/** Preprocess a shader.
 *
 * @param desc What to preprocess
 * @param out The result, released with box_shader_output_fini
 * @return 0 on success, nonzero when out holds an error
 */
int box_shader_preprocess(
    const box_shader_desc_t *desc,
    box_shader_output_t *out);

/** Release the result of a preprocessing.
 *
 * @param out The result
 */
void box_shader_output_fini(
    box_shader_output_t *out);

/** Find a file: absolute, or relative to the working directory or one of its ancestors.
 *
 * @param path The file
 * @return The path the file was found at, NULL when it was not found
 */
char* box_shader_locate_cwd(
    const char *path);

/** Find a file: absolute, relative to the working directory or one of its ancestors, or
 * relative to an ancestor of the folder of the executable.
 *
 * @param path The file
 * @return The path the file was found at, NULL when it was not found
 */
char* box_shader_locate(
    const char *path);

/** Read a file into a terminated string.
 *
 * @param path The file
 * @return The contents allocated with ecs_os_malloc, NULL when the file cannot be read
 */
char* box_shader_read_file(
    const char *path);

/** Take the stamp of a file.
 *
 * @param path The file
 * @param stamp The stamp, with sec -1 when the file is missing
 */
void box_shader_stamp(
    const char *path,
    box_shader_stamp_t *stamp);

/** Compare two stamps.
 *
 * @param a The first stamp
 * @param b The second stamp
 * @return Whether the stamps are equal
 */
bool box_shader_stamp_equals(
    const box_shader_stamp_t *a,
    const box_shader_stamp_t *b);

#endif
