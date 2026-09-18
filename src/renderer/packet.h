/**
 * @file renderer/packet.h
 * @brief Frame packet exchanged between the main thread and the renderer.
 *
 * The packet is the only data the renderer sees; it never touches ECS state.
 */

#ifndef BOX_RENDERER_PACKET_H
#define BOX_RENDERER_PACKET_H

#include <stdint.h>
#include <stdbool.h>
#include "vec.h"

/** Longest capture path stored in a packet */
#define BOX_CAPTURE_PATH_MAX (512)

/** Floats per vertex: position, normal and uv */
#define BOX_VERTEX_FLOATS (8)

/** Per instance data as laid out in GPU instance buffers */
typedef struct box_instance_t {
    float transform[12];   /**< Rows of the 3x4 world matrix, each x y z w */
    float color[4];        /**< Linear albedo rgb and opacity */
    float emissive[4];     /**< Linear emissive rgb, w unused */
    float params[4];       /**< Metallic, roughness, unused, unused */
} box_instance_t;

/** Camera and lighting of one view */
typedef struct box_view_t {
    float view[16];         /**< World to view matrix */
    float proj[16];         /**< View to clip matrix */
    float eye[3];           /**< Camera position */
    float ambient;          /**< Ambient light intensity */
    float sun_dir[3];       /**< Direction the sun light travels */
    float sun_intensity;    /**< Sun intensity */
    float sun_color[3];     /**< Sun color */
    float gamma_encode;     /**< 1 when the target is not an sRGB format */
} box_view_t;

/** Mesh to upload, the renderer takes ownership of the arrays */
typedef struct box_mesh_cmd_t {
    uint32_t handle;        /**< Mesh handle */
    uint32_t vertex_count;  /**< Vertices in the array */
    uint32_t index_count;   /**< Indices in the array */
    float *vertices;        /**< Interleaved BOX_VERTEX_FLOATS per vertex */
    uint32_t *indices;      /**< Triangle indices */
} box_mesh_cmd_t;

/** Instance data written to a slot of a mesh's static buffer */
typedef struct box_static_update_t {
    uint32_t mesh;          /**< Mesh handle */
    uint32_t slot;          /**< Instance slot */
    box_instance_t data;    /**< New instance data */
} box_static_update_t;

/** New static instance count of a mesh */
typedef struct box_static_count_t {
    uint32_t mesh;          /**< Mesh handle */
    uint32_t count;         /**< Instances in use */
} box_static_count_t;

/** Range of dynamic instances sharing a mesh */
typedef struct box_dynamic_batch_t {
    uint32_t mesh;          /**< Mesh handle */
    uint32_t first;         /**< First instance in the dynamic array */
    uint32_t count;         /**< Instances in the batch */
} box_dynamic_batch_t;

/** All work the renderer performs for one frame */
typedef struct box_packet_t {
    uint32_t frame;                          /**< Frame number */
    double time;                             /**< Seconds since start */
    uint32_t width;                          /**< Target width in pixels */
    uint32_t height;                         /**< Target height in pixels */
    float clear[4];                          /**< Clear color, linear RGBA */
    char capture_path[BOX_CAPTURE_PATH_MAX]; /**< PNG to write, empty for none */
    box_view_t view;                         /**< Main view */
    bool has_view;                           /**< A camera was extracted */
    box_vec_t mesh_create;                   /**< box_mesh_cmd_t */
    box_vec_t mesh_destroy;                  /**< uint32_t handles */
    box_vec_t static_updates;                /**< box_static_update_t */
    box_vec_t static_counts;                 /**< box_static_count_t */
    box_vec_t dynamic_batches;               /**< box_dynamic_batch_t */
    box_vec_t dynamic_instances;             /**< box_instance_t */
} box_packet_t;

/** Initialize the vectors of a packet.
 *
 * @param p The packet
 */
static inline void box_packet_init(
    box_packet_t *p)
{
    memset(p, 0, sizeof(box_packet_t));
    box_vec_init(&p->mesh_create, sizeof(box_mesh_cmd_t));
    box_vec_init(&p->mesh_destroy, sizeof(uint32_t));
    box_vec_init(&p->static_updates, sizeof(box_static_update_t));
    box_vec_init(&p->static_counts, sizeof(box_static_count_t));
    box_vec_init(&p->dynamic_batches, sizeof(box_dynamic_batch_t));
    box_vec_init(&p->dynamic_instances, sizeof(box_instance_t));
}

/** Reset per frame fields while keeping vector storage.
 *
 * @param p The packet
 */
static inline void box_packet_reset(
    box_packet_t *p)
{
    p->frame = 0;
    p->time = 0;
    p->width = 0;
    p->height = 0;
    p->capture_path[0] = '\0';
    p->has_view = false;
    box_vec_clear(&p->mesh_create);
    box_vec_clear(&p->mesh_destroy);
    box_vec_clear(&p->static_updates);
    box_vec_clear(&p->static_counts);
    box_vec_clear(&p->dynamic_batches);
    box_vec_clear(&p->dynamic_instances);
}

/** Release the vectors of a packet.
 *
 * @param p The packet
 */
static inline void box_packet_fini(
    box_packet_t *p)
{
    box_vec_fini(&p->mesh_create);
    box_vec_fini(&p->mesh_destroy);
    box_vec_fini(&p->static_updates);
    box_vec_fini(&p->static_counts);
    box_vec_fini(&p->dynamic_batches);
    box_vec_fini(&p->dynamic_instances);
}

#endif
