/**
 * @file texture/texture.h
 * @brief Registry of the loaded textures, shared by box.texture and the render extract.
 *
 * box.texture decodes images on the job system into the registry; box.render sends the
 * decoded pixels to the backend and resolves texture entities to backend handles.
 */

#ifndef BOX_TEXTURE_INTERNAL_H
#define BOX_TEXTURE_INTERNAL_H

#include <box.h>
#include "../app/watch.h"

/** Largest dimension of a texture when no surface sets a texture quality */
#define BOX_TEXTURE_QUALITY_DEFAULT (4096)

/** Seconds a headless frame waits at most for the texture loads in flight */
#define BOX_TEXTURE_HEADLESS_WAIT (10.0)

/** Counter of the decodes in flight, shared by the registry and its jobs */
typedef struct box_texture_sync_t {
    ecs_os_mutex_t lock;    /**< Protects working */
    int32_t working;        /**< Jobs whose decode did not finish */
    int32_t refs;           /**< The registry and every job, changed on the main thread */
} box_texture_sync_t;

/** A texture entity with a TexturePath */
typedef struct box_texture_t {
    ecs_entity_t entity;    /**< The texture entity */
    uint32_t generation;    /**< Bumped by every load request, older results are dropped */
    uint32_t handle;        /**< Backend handle, 0 before the first image was sent */
    uint8_t *pixels;        /**< RGBA8 rows of the image, or R8 rows of a single channel one,
                             *   kept after it was sent so a restarted backend gets it again in
                             *   its first packet */
    int32_t width;          /**< Width of the current image */
    int32_t height;         /**< Height of the current image */
    bool single_channel;    /**< The current image has one R8 channel per pixel */
    uint32_t revision;      /**< Images sent to a backend, so a user of the image sees when it
                             *   changed under the same handle */
    bool loaded;            /**< A load succeeded, so the backend should have an image */
    bool sent;              /**< The current backend has the image under handle */
    bool queued;            /**< The entity is in the list of textures to send */
} box_texture_t;

/** Main thread state of the textures of a world */
typedef struct box_textures_t {
    ecs_map_t textures;         /**< Entity to box_texture_t* */
    ecs_vec_t queued;           /**< ecs_entity_t, textures with pixels to send */
    ecs_vec_t released;         /**< uint32_t, handles of removed textures to destroy */
    ecs_vec_t free_handles;     /**< uint32_t, handles to reuse */
    uint32_t next_handle;       /**< Next backend handle */
    int32_t quality;            /**< Largest dimension the current loads are capped to */
    box_texture_sync_t *sync;   /**< Decodes in flight */
    ecs_query_t *surfaces;      /**< Entities with a Surface */
    int32_t watch;              /**< Kind of the textures in the file watch */
} box_textures_t;

/** Singleton that holds the registry of a world */
typedef struct TextureRegistry {
    box_textures_t *textures;   /**< The registry */
} TextureRegistry;

/** Component id of TextureRegistry */
extern ECS_COMPONENT_DECLARE(TextureRegistry);

/** A decode that runs on a worker: what to load and what came out */
typedef struct box_texture_job_t {
    box_texture_sync_t *sync;   /**< Counter of the registry */
    ecs_entity_t entity;        /**< Texture entity */
    uint32_t generation;        /**< Load request the result belongs to */
    char *path;                 /**< Image file */
    char *file;                 /**< The file the decode found, NULL when it was not found */
    box_file_stamp_t stamp;     /**< Stamp of file, taken before it was read */
    int32_t request_width;      /**< Requested width, 0 for the source width */
    int32_t request_height;     /**< Requested height, 0 for the source height */
    int32_t quality;            /**< Largest dimension of the result */
    bool linear;                /**< Encode the color channels of the result from linear light
                                 *   to sRGB (TexturePath.linear) */
    bool single_channel;        /**< Keep only the red channel of the result, as stored
                                 *   (TexturePath.single_channel) */
    uint8_t *pixels;            /**< RGBA8 rows of the result, R8 rows for a single channel
                                 *   one, NULL on failure */
    int32_t width;              /**< Width of the result */
    int32_t height;             /**< Height of the result */
    char *error;                /**< Why the decode failed, NULL on success */
} box_texture_job_t;

/** Create what the decodes of every world share, once per process on the main thread.
 */
void box_texture_decode_init(void);

/** Decode the image of a job into RGBA8 pixels, or R8 pixels of its red channel for a single
 * channel job, on a worker thread.
 *
 * @param job The job, receives the pixels or the error, and the file it found with its stamp
 */
void box_texture_decode(
    box_texture_job_t *job);

/** Keep the red channel of RGBA8 pixels, in place: pixel i becomes byte i.
 *
 * @param pixels The pixels, which receive count R8 values
 * @param count Number of pixels
 */
void box_texture_red_channel(
    uint8_t *pixels,
    int32_t count);

/** Encode the color channels of RGBA8 pixels from linear light to sRGB, keeping alpha.
 *
 * A channel c becomes round(255 * srgb(c / 255)), so decoding it from sRGB gives c / 255 back
 * within half a step of the encoded value.
 *
 * @param pixels The pixels, straight alpha
 * @param count Number of pixels
 */
void box_texture_encode_linear(
    uint8_t *pixels,
    int32_t count);

/** Decode an image file into linear float RGBA texels, on a worker thread.
 *
 * Radiance .hdr files keep their values; other formats are decoded from display encoding by
 * stb_image. An image wider than max_width is scaled down to it, keeping the aspect ratio.
 *
 * @param path The file, located as box_shader_locate does
 * @param max_width Largest width of the result, 0 for no limit
 * @param width Receives the width
 * @param height Receives the height
 * @param error Receives why the decode failed, freed by the caller
 * @param file Receives the file that was found, NULL when none, freed by the caller; may be NULL
 * @param stamp Receives the stamp of that file taken before it was read, may be NULL
 * @return Four floats per texel row by row from the top, freed by the caller, NULL on failure
 */
float* box_texture_decode_float(
    const char *path,
    int32_t max_width,
    int32_t *width,
    int32_t *height,
    char **error,
    char **file,
    box_file_stamp_t *stamp);

/** Get the registry of a world.
 *
 * @param world The world
 * @return The registry, NULL when box.texture is not imported
 */
box_textures_t* box_textures_get(
    const ecs_world_t *world);

/** Get the backend handle of a texture entity whose image the backend has.
 *
 * @param t The registry, may be NULL
 * @param e The texture entity
 * @return The handle, 0 when the backend has no image of the entity
 */
uint32_t box_textures_handle(
    const box_textures_t *t,
    ecs_entity_t e);

/** Get the revision of the image of a texture entity, which changes with every image sent to
 * the backend.
 *
 * @param t The registry, may be NULL
 * @param e The texture entity
 * @return The images of the entity sent, 0 before the first
 */
uint32_t box_textures_revision(
    const box_textures_t *t,
    ecs_entity_t e);

/** Load textures again, for a new backend or a new texture quality.
 *
 * @param world The world
 * @param t The registry
 * @param sent_only Only the textures whose image was sent to the backend
 */
void box_textures_reload(
    ecs_world_t *world,
    box_textures_t *t,
    bool sent_only);

/** Compute the largest dimension of textures from the texture quality of the surfaces.
 *
 * @param world The world
 * @param t The registry
 * @return The largest texture quality of the surfaces, the default without surfaces
 */
int32_t box_textures_quality(
    const ecs_world_t *world,
    const box_textures_t *t);

#endif
