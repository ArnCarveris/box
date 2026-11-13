/**
 * @file texture/texture.h
 * @brief Registry of the loaded textures, shared by box.texture and the render extract.
 *
 * box.texture decodes or generates images on the job system into the registry; box.render sends the
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

/** A texture entity with a TexturePath, a TextureGen or a TextureGraph */
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
    bool waiting;           /**< Its TextureGen waits for its source to get a TextureGen, it
                             *   fails at the next frame when none arrived */
    char *source_name;      /**< Path of the source it waits for whose TextureGen was removed,
                             *   or which was deleted, so the failure names it */
    struct box_texture_graph_t *graph; /**< Last graph its TextureGraph compiled to, NULL
                             *   before the first build that succeeded */
    ecs_vec_t reached;      /**< ecs_entity_t, the entities the last build of its TextureGraph
                             *   reached, whose changes build it again */
    bool graph_dirty;       /**< Its TextureGraph builds at the next graph build */
    bool graph_waiting;     /**< The last build waited for a node without a node component,
                             *   the next build fails it unless the node got one */
    bool graph_held;        /**< The last build waited for a texture it samples to get an
                             *   image, it builds again every frame until it has one */
    bool graph_queued;      /**< The entity is in the list of graphs to send */
    bool graph_sent;        /**< The current backend has the graph under handle */
    uint32_t graph_serial;  /**< Graphs taken for the backend, the serial of the last one */
    bool graph_pending;     /**< The backend evaluates the graphs, and the outcome of the graph
                             *   of graph_serial did not arrive: Texture and TextureStatus wait
                             *   for it */
} box_texture_t;

/** A node kind of the registry, see box_texture_node_register */
typedef struct box_texture_node_kind_t {
    int32_t id;                 /**< Id of the kind, from 1 */
    ecs_entity_t component;     /**< Component of its nodes */
    TextureValueType output;    /**< Type of its value, TexAny when it follows the inputs */
    box_texture_node_type_t output_type; /**< Type of a node when output is TexAny, NULL for
                                          *   the default */
    int32_t input_count;        /**< Inputs in inputs */
    box_texture_node_input_t inputs[BOX_TEXTURE_NODE_INPUTS]; /**< Inputs, names owned */
    int32_t param_count;        /**< Parameters in params */
    box_texture_node_param_t params[BOX_TEXTURE_NODE_PARAMS]; /**< Parameters, names owned */
    int32_t param_offsets[BOX_TEXTURE_NODE_PARAMS]; /**< Offset of each parameter in the
                                 *   parameter bytes of a node */
    box_texture_node_array_t array; /**< Array parameter, name owned, NULL for none */
    int32_t array_header;       /**< Offset of the header of the array in the parameters */
    int32_t param_size;         /**< Bytes of the parameters with the header of the array, a
                                 *   multiple of 16 */
    int32_t types_offset;       /**< Offset of the types word of the inputs in the parameter
                                 *   bytes of a node, -1 without a TexAny input */
    int32_t node_size;          /**< Parameter bytes of a node: the parameters, then a vec4 per
                                 *   input with the constant of an input without a node, then
                                 *   the max elements of the array */
    char *function;             /**< Name of the WGSL function */
    char *source;               /**< WGSL source of the function */
    uint32_t flags;             /**< BOX_TEXTURE_NODE_NEIGHBOURHOOD, BOX_TEXTURE_NODE_ANIMATED,
                                 *   BOX_TEXTURE_NODE_PLANE, BOX_TEXTURE_NODE_SAMPLES */
    int32_t passes;             /**< Passes of the function, from 1 */
    char *texture;              /**< Member of the texture entity a node samples, NULL for
                                 *   none */
    ecs_size_t texture_offset;  /**< Offset of that member */
    char *linear;               /**< Member that reads the texture as stored, NULL for none */
    ecs_size_t linear_offset;   /**< Offset of that member */
    bool sent;                  /**< The current backend has the kind */
} box_texture_node_kind_t;

/** A node of a compiled graph */
typedef struct box_texture_graph_node_t {
    ecs_entity_t entity;        /**< The node entity */
    int32_t kind;               /**< Id of its kind */
    TextureValueType output;    /**< Type of its value */
    int32_t inputs[BOX_TEXTURE_NODE_INPUTS]; /**< Index of the earlier node of each input, -1
                                 *   for none: the constant in its parameters */
    int32_t params;             /**< Offset of its parameter bytes in the graph parameters */
    int32_t param_size;         /**< Its parameter bytes, the node_size of its kind */
    int32_t texture;            /**< Index of the texture it samples in the textures of the
                                 *   graph, -1 for none */
} box_texture_graph_node_t;

/** A texture a compiled graph samples, one per node of a sampling kind */
typedef struct box_texture_graph_texture_t {
    ecs_entity_t entity;        /**< The texture entity */
    bool linear;                /**< Its color channels read as stored */
    uint32_t handle;            /**< Backend handle the graph was last sent with, 0 before */
} box_texture_graph_texture_t;

/** A TextureGraph compiled: the nodes from its output in topological order, the output last */
typedef struct box_texture_graph_t {
    int32_t width;              /**< Width of the image, capped */
    int32_t height;             /**< Height of the image, capped */
    TextureGraphDomain domain;  /**< Coordinates of the texels */
    TextureGraphFormat format;  /**< What the image stores */
    float rate;                 /**< Updates per second of an animated graph, 0 every frame */
    int32_t node_count;         /**< Nodes in nodes */
    box_texture_graph_node_t *nodes; /**< The nodes, inputs first */
    int32_t param_size;         /**< Bytes in params */
    uint8_t *params;            /**< Parameter bytes of the nodes */
    int32_t texture_count;      /**< Textures in textures */
    box_texture_graph_texture_t *textures; /**< The textures its nodes sample, in node order */
    uint64_t structure_hash;    /**< Hash of the kinds and edges of the nodes */
    uint64_t param_hash;        /**< Hash of the parameter bytes */
} box_texture_graph_t;

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
    ecs_entity_t removed;       /**< Source whose TextureGen is being removed, which the
                                 *   textures derived from it see as gone */
    int32_t loads;              /**< File decodes submitted whose completion did not run */
    int32_t gens;               /**< Generations submitted whose completion did not run */
    box_task_track_t load_task; /**< Report of loads */
    box_task_track_t gen_task;  /**< Report of gens */
    ecs_vec_t node_kinds;       /**< box_texture_node_kind_t*, the kind of id i + 1 at i */
    ecs_vec_t graph_queued;     /**< ecs_entity_t, textures with a compiled graph to send */
    ecs_vec_t graph_released;   /**< uint32_t, handles of sent graphs to destroy */
    ecs_vec_t reload;           /**< ecs_entity_t, textures to load at the next graph build,
                                 *   once a component being removed is gone */
} box_textures_t;

/** Singleton that holds the registry of a world */
typedef struct TextureRegistry {
    box_textures_t *textures;   /**< The registry */
} TextureRegistry;

/** Component id of TextureRegistry */
extern ECS_COMPONENT_DECLARE(TextureRegistry);

/** Image of a texture entity made by the engine, such as the grid of a TileTerrain, sent as it
 * is: no decode, scaling or file watch. A TexturePath or a TextureGen on the entity wins. */
typedef struct TexturePixels {
    int32_t width;          /**< Width in pixels */
    int32_t height;         /**< Height in pixels */
    bool single_channel;    /**< The rows hold one R8 byte per pixel, else RGBA8 */
    uint8_t *pixels;        /**< Rows of the image, the first row is the top, owned */
} TexturePixels;

/** Component id of TexturePixels */
extern ECS_COMPONENT_DECLARE(TexturePixels);

/** What a generation job makes: a TextureGen with its source resolved and its size capped */
typedef struct box_texture_gen_t {
    int32_t width;              /**< Width of the result */
    int32_t height;             /**< Height of the result */
    int32_t seed;               /**< Seed of the height field */
    TextureGenLayer *layers;    /**< Layers of the height field */
    int32_t layer_count;        /**< Number of layers */
    TextureGenOutput output;    /**< What is stored */
    TextureGenKey *colors;      /**< Color ramp of GenAlbedo */
    int32_t color_count;        /**< Number of keys of the ramp */
    float strength;             /**< Steepness of GenNormal */
    rgb *palette;               /**< Colors of the tones of GenAlbedo, NULL without */
    int32_t palette_count;      /**< Number of colors of the palette */
    int32_t palette_layer;      /**< Layer whose tone picks the palette color */
} box_texture_gen_t;

/** A decode that runs on a worker: what to load and what came out */
typedef struct box_texture_job_t {
    box_texture_sync_t *sync;   /**< Counter of the registry */
    ecs_entity_t entity;        /**< Texture entity */
    uint32_t generation;        /**< Load request the result belongs to */
    char *path;                 /**< Image file */
    box_texture_gen_t *gen;     /**< What to generate instead of decoding a file, or NULL */
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
    bool generated;             /**< The job generates, it counts among the gens of the
                                 *   registry instead of its loads */
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

/** Generate the image of a job with a gen on a worker thread: RGBA8 pixels for GenAlbedo and
 * GenNormal, R8 pixels for GenHeight.
 *
 * @param job The job, receives the pixels
 */
void box_texture_generate(
    box_texture_job_t *job);

/** Fill the raw value, id and tone of one layer, before its amount, for every pixel.
 *
 * @param layer The layer
 * @param seed Seed of its generator
 * @param width Width in pixels
 * @param height Height in pixels
 * @param value Receives the value of the pattern of each pixel row by row, 0..1
 * @param id Receives the random block or cell id of each pixel, 0..1, 0 for GenNoise
 * @param tone Receives the tone of the block or cell of each pixel, 0..tones - 1, 0 without
 * tones and for GenNoise; may be NULL
 */
void box_texture_gen_pattern(
    const TextureGenLayer *layer,
    int32_t seed,
    int32_t width,
    int32_t height,
    float *value,
    float *id,
    int32_t *tone);

/** Fill the height field of a generator: its layers combined and scaled to span 0..1, 0.5
 * where they are flat.
 *
 * @param gen The generator
 * @param height Receives width * height values row by row from the top
 * @param tone Receives the tone of each pixel in the layer palette_layer, which takes one tone
 * per palette color when it has no tones; 0 everywhere when that layer does not exist. May be
 * NULL
 */
void box_texture_gen_height(
    const box_texture_gen_t *gen,
    float *height,
    int32_t *tone);

/** Map a height field through a color ramp into opaque RGBA8 pixels, linearly between the
 * display encoded keys; gray without keys. With a palette the ramp color, white without keys,
 * is multiplied per channel by the palette color of the tone of the pixel, a * b / 255.
 *
 * @param height The heights, 0..1
 * @param count Number of pixels
 * @param keys The keys in order of position
 * @param key_count Number of keys
 * @param tone The tone of each pixel, not negative; may be NULL without a palette
 * @param palette Display encoded color per tone, palette[tone % palette_count]
 * @param palette_count Number of colors of the palette, 0 for none
 * @param rgba Receives four bytes per pixel
 */
void box_texture_gen_albedo(
    const float *height,
    int32_t count,
    const TextureGenKey *keys,
    int32_t key_count,
    const int32_t *tone,
    const rgb *palette,
    int32_t palette_count,
    uint8_t *rgba);

/** Make a tangent space normal map from a height field by central differences across the
 * wrap, green up the image: a field rising down the image tilts the normals up it.
 *
 * @param height The heights row by row from the top
 * @param width Width in pixels
 * @param h Height in pixels
 * @param strength Steepness, see TextureGen.strength; 0 for 1
 * @param rgba Receives four bytes per pixel, (128, 128, 255, 255) where flat
 */
void box_texture_gen_normal(
    const float *height,
    int32_t width,
    int32_t h,
    float strength,
    uint8_t *rgba);

/** Free a generation description.
 *
 * @param gen The description, may be NULL
 */
void box_texture_gen_free(
    box_texture_gen_t *gen);

/** Get the registry entry of a texture entity, creating it on first use.
 *
 * @param t The registry
 * @param e The texture entity
 * @return The entry
 */
box_texture_t* box_texture_entry(
    box_textures_t *t,
    ecs_entity_t e);

/** Write the status of a texture.
 *
 * @param world The world or a stage
 * @param e The texture entity
 * @param state Progress of the load
 * @param revision Loads that succeeded
 * @param error Why the load failed, NULL unless it failed
 */
void box_texture_status(
    ecs_world_t *world,
    ecs_entity_t e,
    TextureState state,
    int32_t revision,
    const char *error);

/** Get the loads of a texture that succeeded.
 *
 * @param world The world or a stage
 * @param e The texture entity
 * @return The revision of its status, 0 without one
 */
int32_t box_texture_revision(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Fail the load of a texture without a job, dropping the result of the load in flight.
 *
 * @param world The world or a stage
 * @param t The registry
 * @param e The texture entity
 * @param error Why it failed
 */
void box_texture_fail(
    ecs_world_t *world,
    box_textures_t *t,
    ecs_entity_t e,
    const char *error);

/** Load the image of a texture entity from its TexturePixels, TexturePath, TextureGen or
 * TextureGraph; two of the last three on one entity is an error.
 *
 * @param world The world or a stage
 * @param t The registry
 * @param e The texture entity
 */
void box_texture_load(
    ecs_world_t *world,
    box_textures_t *t,
    ecs_entity_t e);

/** Compute the size of an image capped by the texture quality, keeping its aspect ratio.
 *
 * @param world The world or a stage
 * @param t The registry
 * @param width Requested width, 0 for 512
 * @param height Requested height, 0 for the width
 * @param width_out Receives the width, at least 1
 * @param height_out Receives the height, at least 1
 */
void box_texture_capped_size(
    const ecs_world_t *world,
    box_textures_t *t,
    int32_t width,
    int32_t height,
    int32_t *width_out,
    int32_t *height_out);

/** Create the node kind registry, register the built in kinds and the build of the graphs.
 *
 * @param world The world
 * @param t The registry
 */
void box_texture_graphs_init(
    ecs_world_t *world,
    box_textures_t *t);

/** Register the built in node kinds.
 *
 * @param world The world
 */
void box_texture_nodes_register(
    ecs_world_t *world);

/** Get the type that follows the inputs a node reads: a Vector when one of them has a Vector
 * node, else a Scalar when one has a Scalar node, else a Vector.
 *
 * @param inputs Type of the node of each input, TexAny for none
 * @param used Bit j set when input j is read
 * @return The type
 */
TextureValueType box_texture_graph_follow(
    const TextureValueType *inputs,
    uint32_t used);

/** Free the node kinds and the graph lists of a registry.
 *
 * @param t The registry
 */
void box_texture_graphs_fini(
    box_textures_t *t);

/** Free the compiled graph and the reached entities of a registry entry.
 *
 * @param tex The entry
 */
void box_texture_graph_clear(
    box_texture_t *tex);

/** Mark every graph dirty that reached an entity in its last build, such as a node or a
 * texture it samples, or every graph.
 *
 * @param t The registry
 * @param e The entity, 0 for every graph
 */
void box_texture_graphs_touch(
    box_textures_t *t,
    ecs_entity_t e);

/** Build the TextureGraph of a texture at the next graph build, showing it as loading.
 *
 * @param world The world or a stage
 * @param t The registry
 * @param e The texture entity
 */
void box_texture_graph_request(
    ecs_world_t *world,
    box_textures_t *t,
    ecs_entity_t e);

/** Get a node kind by id.
 *
 * @param t The registry
 * @param id The id, from 1
 * @return The kind, NULL when no kind has the id
 */
box_texture_node_kind_t* box_texture_node_kind(
    const box_textures_t *t,
    int32_t id);

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
