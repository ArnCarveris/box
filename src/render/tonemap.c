/**
 * @file render/tonemap.c
 * @brief The Tony McMapface LUT and the C twins of the tone map operators of tonemap.wgsl.
 *
 * The LUT is read from its DDS file: a DX10 header that declares a 48^3 RGB9E5 volume, then the
 * texels. The twins map one color as the shader does, for tests and tools.
 */

#include "render.h"
#include "preprocess.h"
#include <math.h>
#include <string.h>

/** Bytes of the DDS header, its DX10 extension included */
#define BOX_TONY_HEADER (148)

/** DXGI_FORMAT_R9G9B9E5_SHAREDEXP */
#define BOX_TONY_DXGI_RGB9E5 (67)

/** D3D10_RESOURCE_DIMENSION_TEXTURE3D */
#define BOX_TONY_DIMENSION_3D (4)

/** Read a little endian 32 bit value.
 *
 * @param p The bytes
 * @return The value
 */
static uint32_t box_tony_u32(
    const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
}

/** Check that a DDS header declares the volume of the LUT.
 *
 * @param h The header, BOX_TONY_HEADER bytes
 * @return Whether it is a DX10 header of a 48^3 RGB9E5 3D texture with one mip
 */
static bool box_tony_header_valid(
    const uint8_t *h)
{
    uint32_t mips = box_tony_u32(h + 28);
    bool result = !memcmp(h, "DDS ", 4) && box_tony_u32(h + 4) == 124 &&
        box_tony_u32(h + 12) == BOX_RENDER_TONY_SIZE &&
        box_tony_u32(h + 16) == BOX_RENDER_TONY_SIZE &&
        box_tony_u32(h + 24) == BOX_RENDER_TONY_SIZE && mips <= 1 &&
        !memcmp(h + 84, "DX10", 4) && box_tony_u32(h + 128) == BOX_TONY_DXGI_RGB9E5 &&
        box_tony_u32(h + 132) == BOX_TONY_DIMENSION_3D;

    return result;
}

uint32_t* box_render_tony_load(
    const char *path,
    char **error)
{
    uint32_t *result = NULL;
    uint8_t header[BOX_TONY_HEADER];
    uint8_t *bytes = NULL;
    size_t size = (size_t)BOX_RENDER_TONY_TEXELS * sizeof(uint32_t);
    char *file = path ? box_shader_locate(path) : NULL;
    FILE *f = file ? fopen(file, "rb") : NULL;
    int32_t i;
    *error = NULL;
    if (!f) {
        *error = flecs_asprintf("cannot read LUT '%s'", path ? path : "");
        goto done;
    }

    if (fread(header, 1, sizeof(header), f) != sizeof(header) ||
        !box_tony_header_valid(header))
    {
        *error = flecs_asprintf("'%s' is not a 48^3 RGB9E5 DDS volume", path);
        goto done;
    }

    bytes = ecs_os_malloc((ecs_size_t)size);
    if (fread(bytes, 1, size, f) != size) {
        *error = flecs_asprintf("'%s' ends before its texels do", path);
        goto done;
    }

    result = ecs_os_malloc((ecs_size_t)size);
    for (i = 0; i < BOX_RENDER_TONY_TEXELS; i ++) {
        result[i] = box_tony_u32(bytes + (size_t)i * 4);
    }

done:
    if (f) {
        fclose(f);
    }

    ecs_os_free(bytes);
    ecs_os_free(file);

    return result;
}

void box_render_rgb9e5_decode(
    uint32_t texel,
    float *out)
{
    int32_t exponent = (int32_t)(texel >> 27) - 15 - 9;
    float scale = ldexpf(1.0f, exponent);
    out[0] = (float)(texel & 0x1FFu) * scale;
    out[1] = (float)((texel >> 9) & 0x1FFu) * scale;
    out[2] = (float)((texel >> 18) & 0x1FFu) * scale;
}

void box_render_tonemap_pbr_neutral(
    const float *in,
    float *out)
{
    const float start = 0.8f - 0.04f, desaturation = 0.15f;
    float c[3], low, offset, peak, d, new_peak, g;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        c[i] = glm_max(in[i], 0.0f);
    }

    low = glm_min(c[0], glm_min(c[1], c[2]));
    offset = low < 0.08f ? low - 6.25f * low * low : 0.04f;
    for (i = 0; i < 3; i ++) {
        c[i] -= offset;
    }

    peak = glm_max(c[0], glm_max(c[1], c[2]));
    if (peak >= start) {
        d = 1.0f - start;
        new_peak = 1.0f - d * d / (peak + d - start);
        g = 1.0f - 1.0f / (desaturation * (peak - new_peak) + 1.0f);
        for (i = 0; i < 3; i ++) {
            c[i] *= new_peak / peak;
            c[i] += (new_peak - c[i]) * g;
        }
    }

    for (i = 0; i < 3; i ++) {
        out[i] = c[i];
    }
}

void box_render_tonemap_tony(
    const uint32_t *lut,
    const float *in,
    float *out)
{
    const int32_t n = BOX_RENDER_TONY_SIZE;
    int32_t lo[3], hi[3], i, corner;
    float f[3];
    for (i = 0; i < 3; i ++) {
        float c = glm_max(in[i], 0.0f);
        float p = glm_clamp(c / (c + 1.0f), 0.0f, 1.0f) * (float)(n - 1);
        lo[i] = (int32_t)floorf(p);
        hi[i] = lo[i] + 1 < n ? lo[i] + 1 : n - 1;
        f[i] = p - (float)lo[i];
        out[i] = 0.0f;
    }

    for (corner = 0; corner < 8; corner ++) {
        int32_t x = corner & 1 ? hi[0] : lo[0];
        int32_t y = corner & 2 ? hi[1] : lo[1];
        int32_t z = corner & 4 ? hi[2] : lo[2];
        float w = (corner & 1 ? f[0] : 1.0f - f[0]) * (corner & 2 ? f[1] : 1.0f - f[1]) *
            (corner & 4 ? f[2] : 1.0f - f[2]);
        float texel[3];
        box_render_rgb9e5_decode(lut[(z * n + y) * n + x], texel);
        for (i = 0; i < 3; i ++) {
            out[i] += texel[i] * w;
        }
    }
}
