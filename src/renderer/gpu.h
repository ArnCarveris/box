/**
 * @file renderer/gpu.h
 * @brief Selects the WebGPU header for the current platform and counts its calls.
 */

#ifndef BOX_RENDERER_GPU_H
#define BOX_RENDERER_GPU_H

#ifdef BOX_PLATFORM_WEB
#include <webgpu/webgpu.h>
#else
#include <webgpu.h>
#include <wgpu.h>
#endif

#include "instrument.h"

#endif
