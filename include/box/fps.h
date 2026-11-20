/**
 * @file box/fps.h
 * @brief The fps panel: a chart of the recent frame times and the mean, worst and rate of the
 * last second, at the top right of the surface. The load panel: a chart per thread of the time
 * it worked in the recent frames.
 *
 * FpsPanel opens and closes it, from C, a script, the REST API or the fps command. A scene under
 * BOX_FPS_UI drawn by BOX_FPS_VIEW at BOX_VIEW_ORDER_FPS, over the editor and under the console,
 * holds a panel at the top right of the surface; closing deletes the view and the content.
 *
 * The panel is the template BOX_FPS_TEMPLATE of BOX_FPS_SCRIPT_PATH, reloaded when it changes.
 * It reads FpsSummary, what box.fps writes to BOX_FPS_BAR_MUT and BOX_FPS_BARS_MUT, and the
 * colors and limits below, exported as box.fps.green, yellow, red, fast_ms and slow_ms.
 */

#ifndef BOX_FPS_H
#define BOX_FPS_H

#include "types.h"
#include "app.h"

#ifdef BOX_FPS_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Name of the root entity of the panel content and its view */
#define BOX_FPS_SCOPE "fps"

/** Path of the scene root of the panel content while it is open */
#define BOX_FPS_UI "fps.ui"

/** Path of the view of the panel content while it is open */
#define BOX_FPS_VIEW "fps.view"

/** Path of the layout root that fills the surface, a column of the open panels at its top
 * right: the fps panel above the load panel */
#define BOX_FPS_SCREEN "fps.ui.screen"

/** Path of the panel, anchored to the top right of the surface, the instance of
 * BOX_FPS_TEMPLATE */
#define BOX_FPS_PANEL "fps.ui.screen.panel"

/** Path of the heading of the panel, a Text above the chart */
#define BOX_FPS_HEADING BOX_FPS_PANEL ".heading"

/** Path of the chart. Its children are BOX_FPS_CHART_BARS, the parent of the bars, and the
 * reference lines line_60 and line_30 over them. It has no Layout, so a bar that changes lays
 * nothing out. */
#define BOX_FPS_CHART BOX_FPS_PANEL ".chart"

/** Path of the parent of the bars, an instance of BOX_FPS_BARS_TEMPLATE without a Layout. Its
 * children are the bars bar_0 to bar_N, N BOX_FPS_BARS - 1, instances of BOX_FPS_BAR that show
 * the oldest sample on the left to the newest on the right.
 *
 * A bar keeps its name and entity but not its place: the bars rotate, so bar_N is not always
 * the newest sample. */
#define BOX_FPS_CHART_BARS BOX_FPS_CHART ".bars"

/** Path of the summary row under the chart. Its children are the Texts times, the mean and
 * worst frame time on the left, and rate, the fps on the right. */
#define BOX_FPS_SUMMARY BOX_FPS_PANEL ".summary"

/** The script of the panel content, found in the working directory or an ancestor of it */
#define BOX_FPS_SCRIPT_PATH "etc/fps/fps.flecs"

/** Path of the managed script entity of BOX_FPS_SCRIPT_PATH */
#define BOX_FPS_SCRIPT "box.fps.script"

/** Path of the template of the script that the panel instantiates */
#define BOX_FPS_TEMPLATE "box.fps.FpsWidget"

/** Path of the template of the script of a chart: the bars in bars and the reference lines
 * line_60 and line_30 over them, which the fps panel and every row of the load panel have */
#define BOX_FPS_CHART_TEMPLATE "box.fps.FpsChart"

/** Path of the template of the script that every bar of the chart instantiates */
#define BOX_FPS_BAR "box.fps.FpsBar"

/** Path of the mut variables of BOX_FPS_BAR, set as a whole: ms, an f64, the frame time in
 * milliseconds of the sample the bar shows, 0 without a sample, and col, an i32, its column. The
 * script draws the bar from them, col - first slots from the left, first of BOX_FPS_BARS_MUT.
 *
 * box.fps writes them while the panel is open, only where they changed. A new sample goes to the
 * bar of the oldest sample in the column right of the newest, so it evaluates one bar and the
 * parent.
 *
 * Every BOX_FPS_BARS samples, and when the samples changed otherwise, bar_i gets the sample of
 * age BOX_FPS_BARS - 1 - i in column i again. */
#define BOX_FPS_BAR_MUT BOX_FPS_BAR ".mut"

/** Path of the template of the script that the parent of the bars, BOX_FPS_CHART_BARS,
 * instantiates */
#define BOX_FPS_BARS_TEMPLATE "box.fps.FpsBars"

/** Path of the mut variables of BOX_FPS_BARS_TEMPLATE, which box.fps sets as a whole: first, an
 * i32, the column of the bar at the left of the chart. The script moves the bars first slots
 * left, so a new sample moves one entity instead of every bar. */
#define BOX_FPS_BARS_MUT BOX_FPS_BARS_TEMPLATE ".mut"

/** Points between the panel and the top and right edges of the surface */
#define BOX_FPS_MARGIN (10.0f)

/** Bars of the chart, one per frame sample, the newest on the right */
#define BOX_FPS_BARS (120)

/** Width of a bar in points */
#define BOX_FPS_BAR_WIDTH (3.0f)

/** Width of the slot of a bar in points: the bar and the gap to the next one */
#define BOX_FPS_BAR_SLOT (4.0f)

/** Width of the chart in points */
#define BOX_FPS_CHART_WIDTH (BOX_FPS_BARS * BOX_FPS_BAR_SLOT)

/** Height of the chart in points */
#define BOX_FPS_CHART_HEIGHT (60.0f)

/** Frame time at the top of the chart in milliseconds. The vertical axis is linear in frame
 * time, from 0 at the bottom; a bar is a whole number of points high, at least BOX_FPS_BAR_WIDTH
 * and at most the chart. */
#define BOX_FPS_CHART_MS (50.0)

/** Milliseconds of samples the summary covers, the last second */
#define BOX_FPS_SUMMARY_MS (1000.0)

/** Milliseconds a frame may exceed 1000 / 60 and stay green, for the jitter of vsync */
#define BOX_FPS_TOLERANCE_MS (0.5)

/** Longest frame time of a green bar in milliseconds, exported as box.fps.fast_ms */
#define BOX_FPS_FAST_MS (1000.0 / 60.0 + BOX_FPS_TOLERANCE_MS)

/** Longest frame time of a yellow bar in milliseconds, exported as box.fps.slow_ms */
#define BOX_FPS_SLOW_MS (1000.0 / 30.0)

/** Color of a bar of a frame at 60 fps or faster, exported as box.fps.green */
#define BOX_FPS_GREEN ((rgb){ 70, 200, 90 })

/** Color of a bar of a frame under 60 fps, down to 30 fps, exported as box.fps.yellow */
#define BOX_FPS_YELLOW ((rgb){ 235, 200, 50 })

/** Color of a bar of a frame under 30 fps, exported as box.fps.red */
#define BOX_FPS_RED ((rgb){ 230, 65, 55 })

/** Path of the load panel, anchored to the top right of the surface below the fps panel, the
 * instance of BOX_LOAD_TEMPLATE */
#define BOX_LOAD_PANEL "fps.ui.screen.load"

/** Path of the heading of the load panel, a Text above the rows */
#define BOX_LOAD_HEADING BOX_LOAD_PANEL ".heading"

/** Path of the template of the script that the load panel instantiates */
#define BOX_LOAD_TEMPLATE "box.fps.LoadWidget"

/** Rows of the load panel, one per member of LoadSamples */
#define BOX_LOAD_ROWS (5)

/** Paths of the rows of the load panel in the order of LoadSamples. A row has a head, the Texts
 * name, times and percent, over a chart like BOX_FPS_CHART, whose bars are in chart.bars. */
#define BOX_LOAD_MAIN BOX_LOAD_PANEL ".main"

/** Path of the row of the time the renderer worked */
#define BOX_LOAD_RENDER BOX_LOAD_PANEL ".render"

/** Path of the row of the time the renderer waited on the GPU */
#define BOX_LOAD_GPU BOX_LOAD_PANEL ".gpu"

/** Path of the row of the time the audio thread rendered */
#define BOX_LOAD_AUDIO BOX_LOAD_PANEL ".audio"

/** Path of the row of the time the busiest job worker worked */
#define BOX_LOAD_JOBS BOX_LOAD_PANEL ".jobs"

/** Name of the phase of box.fps that ends the frame of the main thread for LoadSamples, after
 * TaskPublish */
#define BOX_LOAD_END_PHASE "box.fps.LoadEnd"

/** Path of the GPU stats panel, below the load panel in the column at the top right of the
 * surface, the instance of BOX_GPU_TEMPLATE */
#define BOX_GPU_PANEL "fps.ui.screen.gpu"

/** Path of the heading of the GPU stats panel, a Text above its columns */
#define BOX_GPU_HEADING BOX_GPU_PANEL ".heading"

/** Path of the template of the script that the GPU stats panel instantiates */
#define BOX_GPU_TEMPLATE "box.fps.GpuWidget"

/** Seconds GpuStats averages over by default (GpuPanel.interval) */
#define BOX_GPU_INTERVAL (0.5f)

/** The fps panel singleton: whether it is shown.
 *
 * A change applies in the next frame. The panel shows FrameSamples (box/app.h), takes neither
 * the pointer nor the keyboard and takes its background, text and border from UiTheme.
 */
ECS_STRUCT(FpsPanel, {
    bool open;          /**< Shown, the fps command flips it */
});

/** The frames of the last second of FrameSamples, written by box.fps while the panel is open
 * whenever FrameSamples is set or notified, read by the summary of the panel.
 *
 * The last second is the newest samples up to the first one that brings their sum to
 * BOX_FPS_SUMMARY_MS, or all of them when they sum to less.
 */
ECS_STRUCT(FpsSummary, {
    int32_t frames;     /**< Samples of the last second, 0 without samples */
    double mean_ms;     /**< Mean frame time in milliseconds, 0 without samples */
    double worst_ms;    /**< Longest frame time in milliseconds, 0 without samples */
    double fps;         /**< Frames per second, 1000 / mean_ms, 0 without samples */
});

/** The load panel singleton: whether it is shown.
 *
 * A change applies in the next frame. The panel shows LoadSamples, takes neither the pointer
 * nor the keyboard and takes its background, text and border from UiTheme.
 */
ECS_STRUCT(LoadPanel, {
    bool open;          /**< Shown, the load command flips it */
});

/** Milliseconds the threads of box worked in the recent frames, one sample per frame, written by
 * box.fps at the start of every frame into each member whose hold is false.
 *
 * A sample is the time a thread worked between the starts of the frame before and this one, so
 * it lines up with FrameSamples. A thread that waits for another or for the GPU does not work.
 */
ECS_STRUCT(LoadSamples, {
    FrameSamples main;      /**< The main thread, without its waits for a free render packet
                             *   or a drain of the renderer, its sleeps and the frame pacing */
    FrameSamples render;    /**< The renderer, the render thread or the main thread that renders
                             *   for it, without its waits on the GPU */
    FrameSamples gpu;       /**< The renderer waiting on the GPU: for the window image, which
                             *   includes vsync, the oldest frame in flight and GPU reads */
    FrameSamples audio;     /**< The audio thread rendering, 0 without a device */
    FrameSamples jobs;      /**< The job worker that worked the longest, 0 without workers */
});

/** Time a thread worked per frame over the last second of LoadSamples */
ECS_STRUCT(LoadStat, {
    double mean_ms;     /**< Mean milliseconds per frame, 0 without samples */
    double worst_ms;    /**< Longest milliseconds of a frame, 0 without samples */
    double share;       /**< Percent of the frame time the thread worked, 0 without samples */
});

/** The last second of LoadSamples, written by box.fps while the load panel is open whenever
 * LoadSamples is set or notified, read by the heads of the rows of the panel.
 *
 * The last second is the frames of FpsSummary: the newest frame samples up to the first one that
 * brings their sum to BOX_FPS_SUMMARY_MS, or all of them when they sum to less.
 */
ECS_STRUCT(LoadSummary, {
    int32_t frames;     /**< Frames of the last second, 0 without frame samples */
    int32_t workers;    /**< Job workers, 0 before the first job or without workers */
    LoadStat main;      /**< The main thread */
    LoadStat render;    /**< The renderer working */
    LoadStat gpu;       /**< The renderer waiting on the GPU */
    LoadStat audio;     /**< The audio thread */
    LoadStat jobs;      /**< The busiest job worker */
});

/** The GPU stats panel singleton: whether it is shown and how often its stats update.
 *
 * A change applies in the next frame. While it is open box.fps writes GpuStats and the GPU
 * backend reads back the triangles its indirect draws drew (box_render_gpu_stats_detail).
 */
ECS_STRUCT(GpuPanel, {
    bool open;          /**< Shown, the gpu_stats command flips it */
    float interval;     /**< Seconds between two updates of GpuStats, which averages over
                         *   them; 0 or less updates every frame. BOX_GPU_INTERVAL by default */
});

/** What the scene asks the renderer to draw, and what the views drew of it */
ECS_STRUCT(GpuSceneStats, {
    int32_t batches;            /**< Instanced batches, impostor batches included */
    int32_t instances;          /**< Instances in batches, static and dynamic */
    int32_t dynamic;            /**< Dynamic instances among them */
    int32_t impostors;          /**< Instances drawn as impostor cards */
    int32_t materials;          /**< Slots of the material table */
    int32_t views;              /**< Views with a CullStatus, the sums below are over them */
    int32_t tested;             /**< Instances the views tested */
    int32_t visible;            /**< Instances the views drew */
    int32_t frustum_culled;     /**< Instances outside the frustums */
    int32_t occlusion_culled;   /**< Instances hidden behind closer geometry */
    int32_t range_culled;       /**< Instances outside their DrawRange */
    double triangles;           /**< Triangles the views drew per frame after culling */
    double shadow_triangles;    /**< Triangles the shadow cascades drew per frame */
    double other_triangles;     /**< Triangles of the direct draws per frame: effects,
                                 *   particles, sky, fullscreen passes */
});

/** Commands the renderer recorded per frame through the webgpu API */
ECS_STRUCT(GpuCommandStats, {
    double total;               /**< Every command below, bytes excluded */
    double render_passes;       /**< Render passes */
    double compute_passes;      /**< Compute passes */
    double draws;               /**< Draw calls, direct and indirect */
    double dispatches;          /**< Compute dispatches */
    double workgroups;          /**< Workgroups of the direct dispatches */
    double pipelines;           /**< Pipeline switches: pipelines set in passes */
    double bind_groups;         /**< Bind groups set */
    double buffer_binds;        /**< Vertex and index buffers set */
    double copies;              /**< Copies between buffers and textures */
    double copy_bytes;          /**< Bytes copied between buffers */
    double writes;              /**< Buffer and texture writes on the queue */
    double write_bytes;         /**< Bytes of those writes */
    double submits;             /**< Queue submits */
    double command_buffers;     /**< Command buffers submitted */
    int32_t pipelines_cached;   /**< Pipelines in the pipeline cache */
    int32_t pipelines_created;  /**< Pipelines created since the device was created */
    int32_t pipelines_building; /**< Pipelines whose creation is in progress */
});

/** What the main thread sent the renderer per frame in its render packets */
ECS_STRUCT(GpuPacketStats, {
    double resources;           /**< Resource creates, updates and destroys */
    double uploads;             /**< Uploads */
    double scatters;            /**< Scatters of changed instance slots */
    double copies;              /**< Copies between buffers */
    double bytes;               /**< Bytes of data the resources and uploads point into */
    double processed;           /**< Changed entities the draw extract processed */
    double instance_bytes;      /**< Bytes the draw extract uploaded into instance, clip and
                                 *   material buffers */
});

/** Shadow work per frame */
ECS_STRUCT(GpuShadowStats, {
    double draws;               /**< Draw calls of the cascades and atlas tiles */
    double tiles;               /**< Atlas tiles rendered */
    double tiles_kept;          /**< Atlas tiles kept from an earlier frame */
    double cascades;            /**< Cascades rendered */
    double cascades_kept;       /**< Cascades kept from an earlier frame */
});

/** Texture graph work per frame and the state of their cache */
ECS_STRUCT(GpuTextureGraphStats, {
    int32_t graphs;             /**< Texture graphs alive */
    int32_t programs;           /**< Generated stage programs alive */
    double evaluations;         /**< Evaluations, animated updates included */
    double dispatches;          /**< Stage dispatches */
    double texels;              /**< Texels the dispatches computed */
    double hits;                /**< Stages the cache had */
    double misses;              /**< Stages dispatched into a new cache entry */
    double evictions;           /**< Cached values evicted */
    int32_t cached;             /**< Cached values */
    int64_t cached_bytes;       /**< Bytes of the cached values */
    int64_t pool_bytes;         /**< Bytes of the pooled intermediates and key images */
});

/** Mesh graph work per frame and the state of their value cache */
ECS_STRUCT(GpuMeshGraphStats, {
    double jobs;                /**< Jobs submitted */
    double evaluated;           /**< Node values computed */
    double hits;                /**< Node values found in the cache */
    double evicted;             /**< Values evicted from the cache */
    int32_t values;             /**< Values in the cache */
    int64_t bytes;              /**< Bytes of those values */
});

/** Resources of the GPU backend and the memory the renderer counts */
ECS_STRUCT(GpuMemoryStats, {
    int32_t buffers;            /**< Buffers alive */
    int32_t meshes;             /**< Meshes alive */
    int32_t targets;            /**< Targets alive */
    int32_t shaders;            /**< Shader modules alive */
    int64_t instance_bytes;     /**< Bytes of the instance pages */
    int64_t blend_bytes;        /**< Bytes of the texture arrays of the TextureBlend layers */
    int64_t graph_bytes;        /**< Bytes of the texture graph values, pool and key images */
});

/** GPU time per frame, from timestamp queries; 0 without them */
ECS_STRUCT(GpuTimeStats, {
    bool measured;              /**< The device has timestamp queries */
    double cull_ms;             /**< Cull passes of the views */
    double texture_graph_ms;    /**< Texture graph evaluations */
    double atmosphere_ms;       /**< Atmosphere passes */
});

/** Renderer statistics, written by box.fps while GpuPanel is open, every GpuPanel.interval.
 *
 * Counts per frame are averages over the interval: the work of the renderer over the packets
 * it rendered, the work of the main thread over its frames. Other values are the latest.
 */
ECS_STRUCT(GpuStats, {
    bool gpu;                   /**< The world renders with the GPU backend; without it every
                                 *   value the renderer counts is 0 */
    int32_t frames;             /**< Main thread frames the averages cover */
    int32_t packets;            /**< Packets the renderer rendered in them */
    GpuSceneStats scene;        /**< Instances, culling and triangles */
    GpuCommandStats commands;   /**< webgpu commands */
    GpuPacketStats packet;      /**< Render packets */
    GpuShadowStats shadows;     /**< Shadow work */
    GpuTextureGraphStats texture_graphs; /**< Texture graphs */
    GpuMeshGraphStats mesh_graphs; /**< Mesh graphs */
    GpuMemoryStats memory;      /**< Resources and memory */
    GpuTimeStats time;          /**< GPU time */
});

/** Compute the summary of the last second of frame samples.
 *
 * @param samples The samples, NULL for none
 * @param out Receives the summary
 */
void box_fps_summarize(
    const FrameSamples *samples,
    FpsSummary *out);

/** Compute the summary of the last second of thread samples.
 *
 * @param frames The frame samples that set the last second, NULL for none
 * @param samples The thread samples, NULL for none
 * @param workers The number of job workers
 * @param out Receives the summary
 */
void box_load_summarize(
    const FrameSamples *frames,
    const LoadSamples *samples,
    int32_t workers,
    LoadSummary *out);

/** Register the box.fps module.
 *
 * Creates the FpsPanel and LoadPanel singletons, closed, and the FpsSummary, LoadSamples and
 * LoadSummary singletons. The content, its view and the script are created when a panel first
 * opens.
 *
 * @param world The world
 */
void BoxFpsImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
