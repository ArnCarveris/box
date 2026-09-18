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

/** Point and spot lights a packet can carry */
#define BOX_LIGHT_MAX (4096)

/** Light type of an omnidirectional light */
#define BOX_LIGHT_POINT (0)

/** Light type of a cone light */
#define BOX_LIGHT_SPOT (1)

/** Per instance data as laid out in GPU instance buffers */
typedef struct box_instance_t {
    float transform[12];   /**< Rows of the 3x4 world matrix, each x y z w */
    float color[4];        /**< Linear albedo rgb and opacity */
    float emissive[4];     /**< Linear emissive rgb, w unused */
    float params[4];       /**< Metallic, roughness, unused, unused */
} box_instance_t;

/** One point or spot light, laid out as the GPU light storage buffer expects it */
typedef struct box_light_t {
    float position[3];   /**< World space position */
    float range;         /**< Distance at which the light reaches zero */
    float color[3];      /**< Linear color */
    float intensity;     /**< Radiance scale */
    float direction[3];  /**< Direction a spot light points in */
    float cos_inner;     /**< Cosine of the full intensity cone half angle */
    float cos_outer;     /**< Cosine of the zero intensity cone half angle */
    uint32_t type;       /**< BOX_LIGHT_POINT or BOX_LIGHT_SPOT */
    uint32_t is_static;  /**< Light and its ancestors have no dynamic tag */
    float pad;           /**< Padding to the 16 byte GPU stride */
} box_light_t;

/** One emissive static instance, treated as a sphere light by the bake */
typedef struct box_emitter_t {
    float position[3];  /**< World space center of the instance bounds */
    float radius;       /**< Radius of the sphere the instance is treated as */
    float color[3];     /**< Linear emissive color times its strength */
    float range;        /**< Distance at which the emitted light reaches zero */
} box_emitter_t;

/** Settings of the baked lighting pass, copied from the BakeSettings singleton */
typedef struct box_bake_t {
    uint32_t enabled; /**< Static instances read a baked probe instead of the static lights */
    uint32_t budget;  /**< Probes rebaked per frame */
    uint32_t x;       /**< Occupancy voxels across the scene */
    uint32_t y;       /**< Occupancy voxels up the scene */
    uint32_t z;       /**< Occupancy voxels through the scene */
    uint32_t steps;   /**< Ray march samples per visibility test */
} box_bake_t;

/** Froxel grid of a view, copied from the ClusterSettings singleton */
typedef struct box_cluster_t {
    uint32_t x;           /**< Clusters across the view */
    uint32_t y;           /**< Clusters down the view */
    uint32_t z;           /**< Depth slices */
    uint32_t max_lights;  /**< Lights stored per cluster */
    float near_;          /**< View depth of the first slice boundary */
    float far_;           /**< View depth of the last slice boundary */
} box_cluster_t;

/** Camera and lighting of one view */
typedef struct box_view_t {
    float view[16];         /**< World to view matrix */
    float proj[16];         /**< View to clip matrix */
    float eye[3];           /**< Camera position */
    float env_intensity;    /**< Radiance scale of the environment colors */
    float sun_dir[3];       /**< Direction the sun light travels */
    float sun_intensity;    /**< Sun intensity */
    float sun_color[3];     /**< Sun color */
    float gamma_encode;     /**< 1 when the target is not an sRGB format */
    float env_sky[4];       /**< Linear color of the upper hemisphere, w unused */
    float env_ground[4];    /**< Linear color of the lower hemisphere, w unused */
    box_cluster_t cluster;  /**< Froxel grid the lights are assigned to */
    uint32_t light_count;   /**< Lights in the packet light array */
    uint32_t env_generation; /**< Changes when the extracted environment changes */
} box_view_t;

/** Mesh to upload, the renderer takes ownership of the arrays */
typedef struct box_mesh_cmd_t {
    uint32_t handle;        /**< Mesh handle */
    uint32_t vertex_count;  /**< Vertices in the array */
    uint32_t index_count;   /**< Indices in the array */
    float *vertices;        /**< Interleaved BOX_VERTEX_FLOATS per vertex */
    uint32_t *indices;      /**< Triangle indices */
    float bounds_min[3];    /**< Local bounds minimum of the unscaled mesh */
    float bounds_max[3];    /**< Local bounds maximum of the unscaled mesh */
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
    box_vec_t lights;                        /**< box_light_t inside the view frustum */
    box_vec_t bake_lights;                   /**< box_light_t of every static light, unculled */
    box_vec_t emitters;                      /**< box_emitter_t of every static emissive instance */
    box_bake_t bake;                         /**< Settings of the baked lighting pass */
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
    box_vec_init(&p->lights, sizeof(box_light_t));
    box_vec_init(&p->bake_lights, sizeof(box_light_t));
    box_vec_init(&p->emitters, sizeof(box_emitter_t));
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
    box_vec_clear(&p->lights);
    box_vec_clear(&p->bake_lights);
    box_vec_clear(&p->emitters);
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
    box_vec_fini(&p->lights);
    box_vec_fini(&p->bake_lights);
    box_vec_fini(&p->emitters);
}

#endif
