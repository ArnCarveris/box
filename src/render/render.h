/**
 * @file render/render.h
 * @brief Render packets, the backend interface and the packet ring of the box.render sources.
 *
 * Main thread modules append to the packet of the frame during the RenderExtract phase; the
 * backend reads a submitted packet on the render thread, or on the main thread in a browser.
 */

#ifndef BOX_RENDER_INTERNAL_H
#define BOX_RENDER_INTERNAL_H

#include <box.h>

/** Byte alignment of every block in the data of a packet */
#define BOX_RENDER_DATA_ALIGN (16)

/** Phase after EcsOnStore in which modules append what a frame renders to its packet */
extern ECS_DECLARE(RenderExtract);

/** Phase after RenderExtract that hands the packet of the frame to the renderer */
extern ECS_DECLARE(RenderSubmit);

/** Kind of backend object a resource change applies to */
typedef enum box_render_resource_kind_t {
    BOX_RENDER_BUFFER,   /**< GPU buffer, the target of uploads */
    BOX_RENDER_MESH,     /**< Vertex and index data */
    BOX_RENDER_TEXTURE,  /**< Sampled texture */
    BOX_RENDER_TARGET,   /**< Surface or offscreen target that views render into */
    BOX_RENDER_SHADER    /**< Shader module or pipeline */
} box_render_resource_kind_t;

/** What a resource change does to its object */
typedef enum box_render_resource_op_t {
    BOX_RENDER_CREATE,   /**< Create the object, or recreate it when the handle exists */
    BOX_RENDER_UPDATE,   /**< Change the object in place */
    BOX_RENDER_DESTROY   /**< Release the object and its handle */
} box_render_resource_op_t;

/** Creation, change or release of a backend object, applied before the views render */
typedef struct box_render_resource_t {
    box_render_resource_kind_t kind; /**< Kind of object */
    box_render_resource_op_t op;     /**< What happens to it */
    uint32_t handle;                 /**< Handle the main thread assigned to the object */
    uint32_t size;                   /**< Bytes of the description, 0 for none */
    uint32_t data;                   /**< Offset of the description in the packet data */
} box_render_resource_t;

/** Bytes written into a buffer before the views render */
typedef struct box_render_upload_t {
    uint32_t buffer;    /**< Handle of the destination buffer */
    uint32_t offset;    /**< Byte offset in the destination buffer */
    uint32_t size;      /**< Bytes to write */
    uint32_t data;      /**< Offset of the bytes in the packet data */
} box_render_upload_t;

/** A view of the frame: the camera it renders and how it clears */
typedef struct box_render_view_t {
    ecs_entity_t view;          /**< Entity with the View component */
    ecs_entity_t camera;        /**< Entity with the Camera component */
    mat4 camera_transform;      /**< World transform of the camera */
    Camera camera_data;         /**< Settings of the camera */
    ClearSettings clear;        /**< Clear settings of the view */
} box_render_view_t;

/** Instanced draw of one mesh with one pipeline */
typedef struct box_render_batch_t {
    int32_t view;               /**< Index of the view that draws it, -1 for every view */
    uint32_t pipeline;          /**< Shader handle of the pipeline */
    uint32_t mesh;              /**< Mesh handle */
    uint32_t instances;         /**< Buffer handle holding the instance data */
    uint32_t first_instance;    /**< First instance in the instance buffer */
    uint32_t instance_count;    /**< Instances to draw */
} box_render_batch_t;

/** Everything the renderer needs of one frame, filled on the main thread.
 *
 * The vectors keep their storage when the packet is reused, so a frame that needs no more
 * than an earlier one does not allocate. Resources apply first, then uploads, then the views
 * draw their batches.
 */
typedef struct box_render_packet_t {
    int64_t frame;          /**< Frame the packet was extracted in */
    double time;            /**< World time at the end of the frame, in seconds */
    float delta;            /**< Seconds the frame advanced the world */
    ecs_vec_t resources;    /**< box_render_resource_t in the order they were made */
    ecs_vec_t uploads;      /**< box_render_upload_t in the order they were made */
    ecs_vec_t views;        /**< box_render_view_t in render order */
    ecs_vec_t batches;      /**< box_render_batch_t */
    ecs_vec_t data;         /**< Bytes the resources and uploads point into */
} box_render_packet_t;

/** A renderer: the only code that receives packets, always called from one thread.
 *
 * init runs before the first packet, render once per packet in frame order and fini after
 * the last; all three run on the render thread, or on the main thread when it renders.
 */
typedef struct box_render_backend_t {
    void *ctx;                  /**< State of the backend, passed to every callback */
    int (*init)(void*);         /**< Create the backend state, nonzero drops every packet */
    void (*render)(void*, const box_render_packet_t*); /**< Render a packet, valid until return */
    void (*fini)(void*);        /**< Release what init created */
    void (*free)(void*);        /**< Free ctx on the main thread, NULL when the caller owns it */
} box_render_backend_t;

/** Backend that renders nothing and records what it received, the default backend */
typedef struct box_render_null_t {
    int32_t inits;              /**< Calls to init */
    int32_t finis;              /**< Calls to fini */
    int32_t packets;            /**< Packets received */
    int32_t gaps;               /**< Packets whose frame did not follow the previous one */
    int32_t wrong_thread;       /**< Calls on another thread than the one that ran init */
    ecs_os_thread_id_t thread;  /**< Thread that ran init */
    int64_t frame;              /**< Frame of the last packet */
    double time;                /**< Time of the last packet */
    float delta;                /**< Delta of the last packet */
    int64_t views;              /**< Views received in total */
    int64_t batches;            /**< Batches received in total */
    int64_t uploads;            /**< Uploads received in total */
    int64_t resources;          /**< Resource changes received in total */
    int64_t bytes;              /**< Upload bytes received in total */
    box_render_packet_t last;   /**< Copy of the last packet */
} box_render_null_t;

/** Packet ring between the main thread and the consumer of the packets */
typedef struct box_render_ring_t box_render_ring_t;

/** Create a null backend.
 *
 * @return The backend state, freed by box_render_null_free
 */
box_render_null_t* box_render_null_new(void);

/** Free a null backend.
 *
 * @param ctx The backend state
 */
void box_render_null_free(
    void *ctx);

/** Get the backend interface of a null backend that the caller keeps ownership of.
 *
 * @param null The backend state
 * @return The backend interface, with a NULL free
 */
box_render_backend_t box_render_null_backend(
    box_render_null_t *null);

/** Initialize the vectors of a packet.
 *
 * @param p The packet
 */
void box_render_packet_init(
    box_render_packet_t *p);

/** Free the storage of a packet.
 *
 * @param p The packet
 */
void box_render_packet_fini(
    box_render_packet_t *p);

/** Empty a packet and keep its storage.
 *
 * @param p The packet
 */
void box_render_packet_reset(
    box_render_packet_t *p);

/** Copy the contents of a packet into another one, reusing its storage.
 *
 * @param dst The destination packet
 * @param src The source packet
 */
void box_render_packet_copy(
    box_render_packet_t *dst,
    const box_render_packet_t *src);

/** Get the packet of the current frame, acquiring it on first use in the frame.
 *
 * Only call this from the main thread during the RenderExtract phase. It waits when every
 * packet is in flight.
 *
 * @param world The world
 * @return The packet
 */
box_render_packet_t* box_render_packet(
    ecs_world_t *world);

/** Append a view to a packet.
 *
 * @param p The packet
 * @return The zeroed view
 */
box_render_view_t* box_render_packet_view(
    box_render_packet_t *p);

/** Append a batch to a packet.
 *
 * @param p The packet
 * @return The zeroed batch
 */
box_render_batch_t* box_render_packet_batch(
    box_render_packet_t *p);

/** Append an upload to a packet.
 *
 * @param p The packet
 * @param buffer Handle of the destination buffer
 * @param offset Byte offset in the destination buffer
 * @param size Bytes to write
 * @return Where the caller writes the bytes, valid until the next append to the packet
 */
void* box_render_packet_upload(
    box_render_packet_t *p,
    uint32_t buffer,
    uint32_t offset,
    uint32_t size);

/** Append a resource change to a packet.
 *
 * @param p The packet
 * @param kind Kind of object
 * @param op What happens to it
 * @param handle Handle of the object
 * @param size Bytes of the description, 0 for none
 * @return Where the caller writes the description, valid until the next append, or NULL
 */
void* box_render_packet_resource(
    box_render_packet_t *p,
    box_render_resource_kind_t kind,
    box_render_resource_op_t op,
    uint32_t handle,
    uint32_t size);

/** Get the bytes an upload or a resource change points to.
 *
 * @param p The packet
 * @param offset The data offset of the upload or the resource change
 * @return The bytes
 */
const void* box_render_packet_data(
    const box_render_packet_t *p,
    uint32_t offset);

/** Replace the backend, after the packets in flight rendered with the current one.
 *
 * The current backend runs its fini and is freed; the new one runs init before its first
 * packet. Renderer modules call this when they are imported.
 *
 * @param world The world
 * @param backend The backend, copied
 */
void box_render_set_backend(
    ecs_world_t *world,
    const box_render_backend_t *backend);

/** Hold or release the packets on their way to the backend.
 *
 * A held ring still accepts packets until all of them are in flight. Shutting down renders
 * the packets in flight even while they are held.
 *
 * @param world The world
 * @param paused Whether the backend stops receiving packets
 */
void box_render_pause(
    ecs_world_t *world,
    bool paused);

/** Wait until the backend rendered every submitted packet, also while the ring is held,
 * and bring RenderStatus up to date.
 *
 * @param world The world
 */
void box_render_sync(
    ecs_world_t *world);

/** Create a packet ring with the null backend.
 *
 * @return The ring, freed by box_render_ring_free
 */
box_render_ring_t* box_render_ring_new(void);

/** Stop the consumer after it rendered every packet in flight and free the ring.
 *
 * @param ring The ring
 */
void box_render_ring_free(
    box_render_ring_t *ring);

/** Change the settings of a ring, applied before the next packet is acquired.
 *
 * @param ring The ring
 * @param settings The settings
 */
void box_render_ring_configure(
    box_render_ring_t *ring,
    const RenderSettings *settings);

/** Take the next free packet, waiting while every packet is in flight.
 *
 * @param ring The ring
 * @return The empty packet
 */
box_render_packet_t* box_render_ring_acquire(
    box_render_ring_t *ring);

/** Get the packet that was acquired and not yet submitted.
 *
 * @param ring The ring
 * @return The packet, or NULL when none is acquired
 */
box_render_packet_t* box_render_ring_current(
    box_render_ring_t *ring);

/** Hand the acquired packet to the consumer, or render it when the main thread renders.
 *
 * @param ring The ring
 */
void box_render_ring_submit(
    box_render_ring_t *ring);

/** Replace the backend of a ring.
 *
 * @param ring The ring
 * @param backend The backend, copied
 */
void box_render_ring_set_backend(
    box_render_ring_t *ring,
    const box_render_backend_t *backend);

/** Hold or release the packets of a ring.
 *
 * @param ring The ring
 * @param paused Whether the consumer stops taking packets
 */
void box_render_ring_pause(
    box_render_ring_t *ring,
    bool paused);

/** Wait until every submitted packet of a ring was rendered.
 *
 * @param ring The ring
 */
void box_render_ring_sync(
    box_render_ring_t *ring);

/** Read the counters of a ring.
 *
 * @param ring The ring
 * @param status The counters
 */
void box_render_ring_status(
    box_render_ring_t *ring,
    RenderStatus *status);

#endif
