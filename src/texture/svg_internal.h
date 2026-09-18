/**
 * @file texture/svg_internal.h
 * @brief State one SVG rasterization shares between the band jobs that render it.
 */

#ifndef BOX_TEXTURE_SVG_INTERNAL_H
#define BOX_TEXTURE_SVG_INTERNAL_H

#include "texture_internal.h"
#include "../job/job_internal.h"
#include <resvg.h>

/** Texels one band of a rasterization renders at most */
#define BOX_SVG_BAND_PIXELS (256 * 1024)

/** Rasterizations of a world that are queued, running or waiting to be freed */
typedef struct box_svg_registry_t box_svg_registry_t;

/** One rasterization, shared by the band jobs that render it.
 *
 * Everything below the lock is written once, by the first band that runs, and
 * read by the others; the bands themselves write disjoint rows of the pixels.
 */
typedef struct box_svg_image_t {
    ecs_os_mutex_t lock;     /**< Guards the parse, the counters and the orphan flags */
    box_svg_registry_t *reg; /**< Registry the image and its tasks belong to */
    ecs_entity_t entity;     /**< Texture entity the image belongs to */
    char *file;              /**< Resolved SVG file, NULL when data is used */
    char *data;              /**< Inline SVG document, NULL when file is used */
    int32_t request[2];      /**< Requested width and height in pixels */
    uint32_t cap;            /**< Largest edge length the surface allows */
    bool keep;               /**< Keep the source pixels of the upload */
    resvg_render_tree *tree; /**< Parsed SVG, parsed by the first band that runs */
    uint8_t *pixels;         /**< Straight alpha RGBA8 image the bands fill */
    int32_t size[2];         /**< Width and height of the image in pixels */
    int32_t rows;            /**< Rows one band covers */
    float scale;             /**< Uniform scale the SVG renders at */
    float offset[2];         /**< Translation that centers the SVG in the image */
    int32_t bands;           /**< Bands the image is split into */
    int32_t left;            /**< Bands that still have to render */
    int32_t refs;            /**< Tasks that hold the image */
    ecs_vec_t tasks;         /**< box_svg_task_t* of the bands the owner still holds */
    box_image_t out;         /**< Mip chain the completion uploads */
    bool measured;           /**< The SVG was parsed and the size is known */
    bool failed;             /**< The SVG did not parse */
    bool ready;              /**< Every band rendered and out holds the chain */
    bool published;          /**< A completion uploaded the image */
} box_svg_image_t;

/** One band of one rasterization, the context of one job */
typedef struct box_svg_task_t {
    box_svg_image_t *image; /**< Rasterization the band belongs to */
    int32_t band;           /**< Band the task renders */
    box_job_t job;          /**< Handle of the job that runs the task */
    bool completed;         /**< The completion of the task ran */
    bool orphaned;          /**< The owner dropped the task */
} box_svg_task_t;

/** Largest pixel size a request can ask for, which sizes the band jobs.
 *
 * A dimension the request leaves at zero follows the SVG, which is only known
 * once it is parsed, so the upper bound of the size class stands in for it and
 * the bands the image does not need do nothing.
 *
 * @param svg The component
 * @param size Receives the upper bound of the width and the height
 */
void box_svg_bound(
    const Svg *svg,
    int32_t *size);

/** Bands an image of a size is split into.
 *
 * @param size The width and the height in pixels
 * @return The number of bands, at least one
 */
int32_t box_svg_band_count(
    const int32_t *size);

/** Parse the SVG of an image and size the buffer its bands render into.
 *
 * @param img The image, the caller holds its lock
 */
void box_svg_image_measure(
    box_svg_image_t *img);

/** Render one band of an image into its pixel buffer.
 *
 * @param img The image, parsed and sized
 * @param band The band to render
 */
void box_svg_image_band(
    box_svg_image_t *img,
    int32_t band);

/** Defringe the assembled image and scale it into the mip chain to upload.
 *
 * @param img The image, every band rendered
 */
void box_svg_image_finish(
    box_svg_image_t *img);

/** Register the rasterization job registry, its system and its teardown.
 *
 * @param world The world
 */
void box_svg_jobs_init(
    ecs_world_t *world);

/** Queue the rasterization of an Svg component, superseding the queued one.
 *
 * @param world The world
 * @param e The texture entity
 * @param svg The component
 */
void box_svg_submit(
    ecs_world_t *world,
    ecs_entity_t e,
    const Svg *svg);

/** Cancel the rasterization of a texture entity and drop its tasks.
 *
 * @param world The world
 * @param e The texture entity
 */
void box_svg_drop(
    ecs_world_t *world,
    ecs_entity_t e);

#endif
