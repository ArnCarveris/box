#ifndef BOX_RENDERER_EFFECT_TEXTURE_H
#define BOX_RENDERER_EFFECT_TEXTURE_H

#include "gpu.h"
#include "renderer.h"
#include "packet.h"

void box_effect_texture_init(
    box_renderer_t *r);

void box_effect_texture_apply(
    box_renderer_t *r,
    const box_packet_t *p);

WGPUTextureView box_effect_texture_view(
    box_renderer_t *r,
    uint32_t handle);

void box_effect_texture_fini(
    box_renderer_t *r);

#endif
