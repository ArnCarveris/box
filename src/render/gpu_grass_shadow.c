/**
 * @file render/gpu_grass_shadow.c
 * @brief Shadows of the grass blades of the webgpu backend: the placement of the blades that cast
 * within the shadow distance of a Grass and their draws into the cascades and the light tiles.
 *
 * A compute pass of a view places the blades that cast at coarser levels, which a render pass
 * after the casters of each cascade they cast into draws into its layer (grass.wgsl vs_shadow).
 *
 * The atlas of a scene whose Grass casts into the lights places them around the camera of its
 * first view and draws them into each spot and point light tile.
 */

#include "gpu.h"
#include "gpu_grass.h"
#include <math.h>

#ifdef BOX_RENDER_WGPU

uint32_t box_gpu_grass_shadow_capacity(
    const box_render_grass_t *g)
{
    float thin = box_render_grass_shadow_thin(g);
    double r0 = thin > 1.0f ? thin : 1.0;
    double r = g->shadow, rho = g->density, pi = 3.14159265358979, blades;
    uint32_t result = 0;
    if (!(r > 0.0)) {
        goto done;
    }

    blades = r <= r0 ? pi * r * r * rho
        : pi * rho * r0 * r0 * (1.0 + 2.0 * log(r / r0));
    blades = blades * 1.25 + 1024.0;
    result = blades > (double)BOX_GPU_GRASS_CAPACITY_MAX
        ? BOX_GPU_GRASS_CAPACITY_MAX : (uint32_t)blades;
    result = result < BOX_GPU_GRASS_CAPACITY_MIN ? BOX_GPU_GRASS_CAPACITY_MIN : result;
    result = (result + BOX_GPU_GRASS_GROUP - 1u) / BOX_GPU_GRASS_GROUP * BOX_GPU_GRASS_GROUP;
done:
    return result;
}

void box_gpu_grass_entity_shadows(
    box_gpu_t *gpu,
    box_gpu_grass_entity_t *e,
    uint32_t capacity)
{
    if (e->shadow_blades && e->shadow_capacity == capacity) {
        goto done;
    }

    BOX_GPU_GRASS_RELEASE(e->shadow_place_group, wgpuBindGroupRelease);
    BOX_GPU_GRASS_RELEASE(e->shadow_draw_group, wgpuBindGroupRelease);
    BOX_GPU_GRASS_RELEASE(e->shadow_blades, wgpuBufferRelease);
    BOX_GPU_GRASS_RELEASE(e->shadow_counters, wgpuBufferRelease);
    e->shadow_capacity = capacity;
    e->shadow_recorded = 0;
    if (!capacity) {
        goto done;
    }

    e->shadow_blades = box_gpu_grass_buffer(gpu,
        (uint64_t)capacity * BOX_GRASS_LODS * sizeof(box_gpu_grass_blade_t),
        WGPUBufferUsage_Storage);
    e->shadow_counters = box_gpu_grass_buffer(gpu, BOX_GPU_GRASS_COUNTERS,
        WGPUBufferUsage_Storage | WGPUBufferUsage_Indirect);
done:
    return;
}

/** Create a shadow pipeline: the blades that cast as triangle strips, both sides, depth only into
 * a shadow map with reversed Z, as the shadow pipelines of the casters write it.
 *
 * @param gpu The backend
 * @param module The module of the draw shader
 * @param point Whether it draws into an octant of a point light tile, discarding outside it
 * @return The pipeline, NULL while it is created
 */
static WGPURenderPipeline box_gpu_grass_shadow_create(
    box_gpu_t *gpu,
    WGPUShaderModule module,
    bool point)
{
    WGPUFragmentState fragment = {
        .module = module,
        .entryPoint = { .data = "fs_shadow_point", .length = WGPU_STRLEN },
        .targetCount = 0
    };
    WGPUDepthStencilState depth = {
        .format = BOX_GPU_SHADOW_FORMAT,
        .depthWriteEnabled = WGPUOptionalBool_True,
        .depthCompare = WGPUCompareFunction_Greater,
        .stencilFront = { .compare = WGPUCompareFunction_Always },
        .stencilBack = { .compare = WGPUCompareFunction_Always }
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = gpu->grass->shadow_pipeline_layout,
        .vertex = {
            .module = module,
            .entryPoint = {
                .data = point ? "vs_shadow_point" : "vs_shadow",
                .length = WGPU_STRLEN
            }
        },
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleStrip,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = WGPUCullMode_None
        },
        .depthStencil = &depth,
        .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        .fragment = point ? &fragment : NULL
    };

    return box_gpu_render_pipeline(gpu, &desc);
}

/** Get a shadow pipeline, created on first use and again after the draw shader recompiled.
 *
 * @param gpu The backend
 * @param point Whether it draws into an octant of a point light tile
 * @return The pipeline, NULL without a draw shader or while it is created
 */
static WGPURenderPipeline box_gpu_grass_shadow_pipeline(
    box_gpu_t *gpu,
    bool point)
{
    box_gpu_grass_t *s = gpu->grass;
    box_gpu_shader_t *shader = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_GRASS);
    WGPURenderPipeline result = NULL;
    if (!shader || !shader->module) {
        goto done;
    }

    if (s->draw_revision != shader->revision) {
        box_gpu_grass_draw_reset(s);
        s->draw_revision = shader->revision;
    }

    if (point) {
        if (!s->shadow_point) {
            s->shadow_point = box_gpu_grass_shadow_create(gpu, shader->module, true);
        }

        result = s->shadow_point;
    } else {
        if (!s->shadow) {
            s->shadow = box_gpu_grass_shadow_create(gpu, shader->module, false);
        }

        result = s->shadow;
    }

done:
    return result;
}

/** Make params of a Grass place the blades that cast shadows: out to its shadow distance, at
 * the coarser levels of the shadows, thinning and shrinking away from where the shadows start to
 * fade.
 *
 * @param e The Grass
 * @param direction Direction toward the light whose shadows sweep the frustum
 * @param reach How far along the light the shadows sweep it, 0 for none
 * @param out The params of box_gpu_grass_params around the camera out to the shadow distance
 */
static void box_gpu_grass_cast_params(
    const box_gpu_grass_entity_t *e,
    const float direction[3],
    float reach,
    box_gpu_grass_params_t *out)
{
    const box_render_grass_t *g = &e->grass;
    float fade = box_render_grass_shadow_fade(g);
    out->ground[3] = box_render_grass_shadow_thin(g);
    out->lod[0] = 0.0f;
    out->lod[1] = g->shadow * BOX_RENDER_GRASS_SHADOW_LOD;
    out->lod[2] = g->shadow;
    out->lod[3] = fade < g->lod[2] * 0.85f ? fade : g->lod[2] * 0.85f;
    out->light[0] = direction[0];
    out->light[1] = direction[1];
    out->light[2] = direction[2];
    out->light[3] = 0.0f;
    out->caster[0] = g->shadow;
    out->caster[1] = fade;
    out->caster[2] = reach;
    out->caster[3] = 1.0f;
}

/** Begin a render pass that draws the blades that cast shadows into a shadow map, which it
 * loads, timed with the grass passes when there is room for its timestamps.
 *
 * @param gpu The backend
 * @param s The grass passes
 * @param encoder The encoder
 * @param target The layer of the cascade or the atlas
 * @return The pass, with the empty group at 0
 */
static WGPURenderPassEncoder box_gpu_grass_shadow_pass(
    box_gpu_t *gpu,
    box_gpu_grass_t *s,
    WGPUCommandEncoder encoder,
    WGPUTextureView target)
{
    WGPUPassTimestampWrites writes;
    WGPURenderPassDepthStencilAttachment depth = {
        .view = target,
        .depthLoadOp = WGPULoadOp_Load,
        .depthStoreOp = WGPUStoreOp_Store
    };
    WGPURenderPassDescriptor desc = {
        .depthStencilAttachment = &depth,
        .timestampWrites = box_gpu_grass_writes(s, &writes)
    };
    WGPURenderPassEncoder pass = box_gpu_profile_render_pass(gpu, encoder, &desc,
        "grass shadow");
    wgpuRenderPassEncoderSetBindGroup(pass, 0, s->empty_group, 0, NULL);

    return pass;
}

/** Record the indirect draws of the levels of the blades of a Grass that cast shadows, with
 * params of the cascade or the tile in the ring.
 *
 * @param gpu The backend
 * @param s The grass passes
 * @param pass The shadow pass of the grass
 * @param e The Grass
 * @param params Its params with the projection and the light of the cascade or the tile
 * @return Whether its blades were drawn, false when the ring is full
 */
static bool box_gpu_grass_cast(
    box_gpu_t *gpu,
    box_gpu_grass_t *s,
    WGPURenderPassEncoder pass,
    const box_gpu_grass_entity_t *e,
    const box_gpu_grass_params_t *params)
{
    bool result = false;
    uint32_t l;
    if (s->params_used >= s->params_capacity || !e->shadow_draw_group) {
        goto done;
    }

    uint32_t offset = s->params_used * BOX_GPU_GRASS_PARAMS_STRIDE;
    s->params_used ++;
    wgpuQueueWriteBuffer(gpu->queue, s->params, offset, params, sizeof(*params));
    wgpuRenderPassEncoderSetBindGroup(pass, 1, e->shadow_draw_group, 1, &offset);
    for (l = 0; l < BOX_GRASS_LODS; l ++) {
        wgpuRenderPassEncoderDrawIndirect(pass, e->shadow_counters, (uint64_t)l * 16u);
        box_gpu_count(gpu, BOX_GPU_STAT_SHADOW_DRAW);
    }

    result = true;
done:
    return result;
}

void box_gpu_grass_place_shadows(
    box_gpu_t *gpu,
    box_gpu_grass_t *s,
    WGPUComputePassEncoder pass,
    const box_render_packet_t *packet,
    box_gpu_grass_entity_t *e,
    const box_render_view_t *v,
    const box_gpu_view_uniforms_t *u)
{
    box_gpu_grass_params_t params;
    uint32_t offset = 0;
    if (!e->shadow_place_group || !box_render_grass_cascades(&e->grass, &v->shadows)) {
        goto done;
    }

    const float *light = v->shadows.direction;
    float reach = box_render_grass_shadow_reach(&e->grass, light);
    uint32_t n = box_gpu_grass_params(packet, e, u->camera, e->grass.shadow,
        e->shadow_capacity, &params);
    ecs_os_memcpy_n(params.view_proj, u->view_proj, float, 16);
    box_gpu_grass_pixel(u, params.pixel);
    box_gpu_grass_cast_params(e, light, reach, &params);
    if (box_gpu_grass_dispatch(gpu, s, pass, e, &params, n, e->shadow_place_group, &offset)) {
        e->shadow_recorded = s->recording;
        e->shadow_params = params;
    }

done:
    return;
}

uint32_t box_gpu_grass_cascades(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_render_view_t *v)
{
    uint32_t result = 0;
    int32_t i, count = ecs_vec_count(&packet->grasses);
    if (!box_gpu_grass_drawn(gpu, packet, v)) {
        goto done;
    }

    for (i = 0; i < count; i ++) {
        const box_render_grass_t *g = ecs_vec_get_t(&packet->grasses, box_render_grass_t, i);
        if (g->scene == v->scene) {
            result |= box_render_grass_cascades(g, &v->shadows);
        }
    }

done:
    return result;
}

void box_gpu_grass_shadow(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_render_view_t *v,
    int32_t cascade,
    WGPUTextureView layer,
    const float *matrix,
    bool late)
{
    box_gpu_grass_t *s = gpu->grass;
    WGPURenderPassEncoder pass = NULL;
    WGPURenderPipeline pipeline = NULL;
    box_gpu_grass_params_t params;
    if (!s || !s->active || s->view != v || s->shadows_late != late) {
        goto done;
    }

    ecs_map_iter_t it = ecs_map_iter(&s->entities);
    while (ecs_map_next(&it)) {
        box_gpu_grass_entity_t *e = ecs_map_ptr(&it);
        if (e->shadow_recorded != s->recording || !e->shadow_recorded ||
            !(box_render_grass_cascades(&e->grass, &v->shadows) & (1u << cascade)))
        {
            continue;
        }

        if (!pipeline) {
            pipeline = box_gpu_grass_shadow_pipeline(gpu, false);
            if (!pipeline) {
                goto done;
            }

            pass = box_gpu_grass_shadow_pass(gpu, s, encoder, layer);
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        }

        params = e->shadow_params;
        ecs_os_memcpy_n(params.view_proj, matrix, float, 16);
        box_gpu_grass_cast(gpu, s, pass, e, &params);
    }

    if (pass) {
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        gpu->pending.grass_cascades ++;
    }

done:
    return;
}

/** Find the camera the blades of the scene of an atlas cast into its light tiles around: that
 * of the first lit view of the scene with a depth.
 *
 * @param packet The packet
 * @param scene The scene of the atlas
 * @param out Receives the local position of the camera
 * @return Whether the scene has such a view
 */
static bool box_gpu_grass_atlas_camera(
    const box_render_packet_t *packet,
    ecs_entity_t scene,
    float out[3])
{
    bool result = false;
    int32_t i, count = ecs_vec_count(&packet->views);
    for (i = 0; i < count && !result; i ++) {
        const box_render_view_t *v = ecs_vec_get_t(&packet->views, box_render_view_t, i);
        if (v->scene == scene && v->lights.enabled && !v->impostor &&
            box_camera_has_depth(&v->camera_data))
        {
            out[0] = v->camera_transform[3][0];
            out[1] = v->camera_transform[3][1];
            out[2] = v->camera_transform[3][2];
            result = true;
        }
    }

    return result;
}

bool box_gpu_grass_lights(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    ecs_entity_t scene)
{
    bool result = false;
    float camera[3];
    int32_t i, count = ecs_vec_count(&packet->grasses);
    if (!gpu->grass || !box_gpu_grass_atlas_camera(packet, scene, camera)) {
        goto done;
    }

    for (i = 0; i < count && !result; i ++) {
        const box_render_grass_t *g = ecs_vec_get_t(&packet->grasses, box_render_grass_t, i);
        result = g->scene == scene && g->shadow_lights && g->shadow > 0.0f;
    }

done:
    return result;
}

/** Place the blades of the Grass entities of the scene of an atlas that cast into its light
 * tiles around the camera of the first view of the scene, all of them within their shadow
 * distance.
 *
 * @param gpu The backend
 * @param s The grass passes
 * @param encoder The encoder
 * @param packet The packet
 * @param scene The scene of the atlas
 * @return Whether blades were placed
 */
static bool box_gpu_grass_atlas_place(
    box_gpu_t *gpu,
    box_gpu_grass_t *s,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    ecs_entity_t scene)
{
    static const float up[3] = { 0.0f, 1.0f, 0.0f };
    WGPUComputePassEncoder pass = NULL;
    box_gpu_grass_params_t params;
    float camera[3];
    bool result = false;
    uint32_t offset;
    if (!box_gpu_grass_atlas_camera(packet, scene, camera) ||
        !box_gpu_grass_place_pipelines(gpu))
    {
        goto done;
    }

    s->recording ++;
    s->view = NULL;
    ecs_map_iter_t it = ecs_map_iter(&s->entities);
    while (ecs_map_next(&it)) {
        box_gpu_grass_entity_t *e = ecs_map_ptr(&it);
        if (e->frame != packet->frame || e->grass.scene != scene ||
            !e->grass.shadow_lights || !e->shadow_place_group)
        {
            continue;
        }

        if (!pass) {
            pass = box_gpu_grass_pass(gpu, s, encoder);
        }

        uint32_t n = box_gpu_grass_params(packet, e, camera, e->grass.shadow,
            e->shadow_capacity, &params);
        box_gpu_grass_cast_params(e, up, 0.0f, &params);
        if (box_gpu_grass_dispatch(gpu, s, pass, e, &params, n, e->shadow_place_group,
            &offset))
        {
            e->shadow_recorded = s->recording;
            e->shadow_params = params;
            result = true;
        }
    }

    if (pass) {
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
    }

done:
    return result;
}

/** Record the draws of the blades that cast into a light tile, into each octant of a point
 * light tile.
 *
 * @param gpu The backend
 * @param s The grass passes
 * @param pass The shadow pass of the grass over the atlas, its viewport on the tile
 * @param scene The scene of the atlas
 * @param tile The tile
 * @param spot Pipeline of the spot light tiles
 * @param point Pipeline of the point light tiles, NULL while it is created
 * @return Whether blades were drawn
 */
static bool box_gpu_grass_tile(
    box_gpu_t *gpu,
    box_gpu_grass_t *s,
    WGPURenderPassEncoder pass,
    ecs_entity_t scene,
    const box_render_shadow_tile_t *tile,
    WGPURenderPipeline spot,
    WGPURenderPipeline point)
{
    bool octahedral = tile->projection == BOX_RENDER_SHADOW_OCTAHEDRAL, result = false;
    box_gpu_grass_params_t params;
    int32_t o;
    if (octahedral && !point) {
        goto done;
    }

    wgpuRenderPassEncoderSetPipeline(pass, octahedral ? point : spot);
    ecs_map_iter_t it = ecs_map_iter(&s->entities);
    while (ecs_map_next(&it)) {
        box_gpu_grass_entity_t *e = ecs_map_ptr(&it);
        if (e->shadow_recorded != s->recording || !e->shadow_recorded ||
            e->grass.scene != scene || !e->grass.shadow_lights)
        {
            continue;
        }

        params = e->shadow_params;
        ecs_os_memcpy_n(params.light, tile->position, float, 3);
        for (o = 0; o < (octahedral ? BOX_RENDER_SHADOW_OCTANTS : 1); o ++) {
            if (octahedral) {
                ecs_os_memset_n(params.view_proj, 0, float, 16);
                params.light[3] = 2.0f + (float)o;
                params.planes[0] = tile->near_;
                params.planes[1] = tile->far_;
            } else {
                ecs_os_memcpy_n(params.view_proj, tile->matrix, float, 16);
                params.light[3] = 1.0f;
            }

            result = box_gpu_grass_cast(gpu, s, pass, e, &params) || result;
        }
    }

done:
    return result;
}

void box_gpu_grass_atlas(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    const box_render_shadow_atlas_t *a,
    const bool *renders,
    WGPUTextureView target,
    int32_t resolution)
{
    box_gpu_grass_t *s = gpu->grass;
    WGPURenderPassEncoder pass = NULL;
    int32_t i;
    if (!s || !s->active || !box_gpu_grass_lights(gpu, packet, a->scene)) {
        goto done;
    }

    WGPURenderPipeline spot = box_gpu_grass_shadow_pipeline(gpu, false);
    WGPURenderPipeline point = box_gpu_grass_shadow_pipeline(gpu, true);
    if (!spot || !box_gpu_grass_atlas_place(gpu, s, encoder, packet, a->scene)) {
        goto done;
    }

    for (i = 0; i < a->count; i ++) {
        const box_render_shadow_tile_t *tile = ecs_vec_get_t(
            &packet->shadow_tiles, box_render_shadow_tile_t, a->first + i);
        if (!renders[i] || tile->size < 1 || tile->x < 0 || tile->y < 0 ||
            tile->x + tile->size > resolution || tile->y + tile->size > resolution)
        {
            continue;
        }

        if (!pass) {
            pass = box_gpu_grass_shadow_pass(gpu, s, encoder, target);
        }

        wgpuRenderPassEncoderSetViewport(pass, (float)tile->x, (float)tile->y,
            (float)tile->size, (float)tile->size, 0.0f, 1.0f);
        wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)tile->x, (uint32_t)tile->y,
            (uint32_t)tile->size, (uint32_t)tile->size);
        if (box_gpu_grass_tile(gpu, s, pass, a->scene, tile, spot, point)) {
            gpu->pending.grass_tiles ++;
        }
    }

    if (pass) {
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

done:
    return;
}

#endif
