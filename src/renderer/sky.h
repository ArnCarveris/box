/**
 * @file renderer/sky.h
 * @brief Tables, pipelines and uniform of the atmosphere the views draw.
 *
 * The transmittance and multiple scattering tables only depend on the medium,
 * the sky view table also on the sun and the camera altitude, and the packet
 * says which of them the frame has to render again. All three live once per
 * renderer because every view of a frame looks at the same sky.
 */

#ifndef BOX_RENDERER_SKY_H
#define BOX_RENDERER_SKY_H

#include <stdint.h>
#include <stdbool.h>
#include "gpu.h"
#include "packet.h"

/** Format of every table of the atmosphere */
#define BOX_SKY_FORMAT (WGPUTextureFormat_RGBA16Float)

/** Target formats the sky pass is compiled for: the view target and the float one */
#define BOX_SKY_FORMATS (2)

/** Bindings the sky pass reads: uniform, the three tables, the sampler and the depth */
#define BOX_SKY_BINDINGS (6)

/** Uniform block the sky shaders read, the view first and the atmosphere after */
typedef struct box_sky_uniform_t {
    float inv_clip[16]; /**< Clip to world of the view, which rebuilds its rays */
    float camera[4];    /**< Eye of the view, w its altitude over the planet in kilometers */
    box_sky_t sky;      /**< Atmosphere of the frame, copied from the packet */
} box_sky_uniform_t;

/** Tables, pipelines and bind groups of the atmosphere of a renderer */
typedef struct box_sky_gpu_t {
    WGPUTexture trans;            /**< Transmittance table */
    WGPUTextureView trans_view;   /**< View of the transmittance table */
    WGPUTexture multi;            /**< Multiple scattering table */
    WGPUTextureView multi_view;   /**< View of the multiple scattering table */
    WGPUTexture view;             /**< Sky view table */
    WGPUTextureView view_view;    /**< View of the sky view table */
    WGPUTexture dummy;            /**< One texel table bound where a pass writes the real one */
    WGPUTextureView dummy_view;   /**< View of that texture */
    WGPUSampler sampler;          /**< Linear clamped sampler the tables are read through */
    WGPUBuffer uniform;           /**< Uniform of the table passes and the environment face */
    WGPUBindGroupLayout lut_layout; /**< Uniform, both tables and the sampler */
    WGPUBindGroup trans_group;    /**< Bind group of the transmittance pass */
    WGPUBindGroup multi_group;    /**< Bind group of the multiple scattering pass */
    WGPUBindGroup view_group;     /**< Bind group of the sky view pass */
    WGPURenderPipeline trans_pipeline; /**< Pass that fills the transmittance table */
    WGPURenderPipeline multi_pipeline; /**< Pass that fills the multiple scattering table */
    WGPURenderPipeline view_pipeline;  /**< Pass that fills the sky view table */
    WGPUBindGroupLayout layout;   /**< Layout of the sky pass, which adds the depth target */
    WGPURenderPipeline pipeline[BOX_SKY_FORMATS]; /**< Sky pass per target format */
    WGPUBindGroupLayout env_layout; /**< Layout of the environment face pass */
    WGPUBindGroup env_group;      /**< Bind group of that pass */
    WGPURenderPipeline env_pipeline; /**< Pass that fills one face of the environment */
    uint32_t lut_shader;          /**< Shader handle the table pipelines were built from */
    uint32_t shader;              /**< Shader handle the sky pipelines were built from */
    uint32_t epoch;               /**< Rises when the tables are recreated, to rebind the views */
    uint32_t generation;          /**< Sky view generation the tables were last rendered for */
    bool tables;                  /**< The tables hold a sky the sky pass can read */
    bool failed;                  /**< A sky shader did not compile and was reported */
} box_sky_gpu_t;

#endif
