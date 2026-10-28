/**
 * @file render/shape2d.c
 * @brief Instance data of the 2D shapes, which etc/shaders/shape2d.wgsl shades with a distance.
 */

#include "draw.h"
#include <math.h>

/** Half float bits of the linear light of every display encoded channel value */
static uint16_t box_shape2d_halves[256];

/** Whether box_shape2d_halves is filled */
static bool box_shape2d_halves_ready;

/** Convert a value from 0 to 1 to a half precision float, flushing tiny values to zero.
 *
 * @param value The value
 * @return The bits of the half float
 */
static uint16_t box_shape2d_half(
    float value)
{
    uint32_t bits, result = 0;
    float v = glm_clamp(value, 0.0f, 1.0f);
    ecs_os_memcpy(&bits, &v, 4);
    int32_t exponent = (int32_t)((bits >> 23) & 0xFF) - 127 + 15;
    if (exponent > 0) {
        result = ((uint32_t)exponent << 10) | ((bits >> 13) & 0x3FF);
        if (bits & 0x1000) {
            result ++;
        }
    }

    return (uint16_t)result;
}

/** Get the half float bits of the linear light of a display encoded channel.
 *
 * @param value The channel
 * @return The bits of the half float
 */
static uint32_t box_shape2d_linear(
    uint8_t value)
{
    int32_t i;
    if (!box_shape2d_halves_ready) {
        for (i = 0; i < 256; i ++) {
            float c = (float)i / 255.0f;
            box_shape2d_halves[i] = box_shape2d_half(
                c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f));
        }

        box_shape2d_halves_ready = true;
    }

    return box_shape2d_halves[value];
}

/** Store two half floats in the bits of a float.
 *
 * @param out The float
 * @param lo The bits of the low half
 * @param hi The bits of the high half
 */
static void box_shape2d_pack(
    float *out,
    uint32_t lo,
    uint32_t hi)
{
    uint32_t bits = lo | (hi << 16);
    ecs_os_memcpy(out, &bits, 4);
}

/** Write the corner radius, border and aspect of a rectangle.
 *
 * Lengths are relative to half the smaller side, the unit the shader measures distances in.
 *
 * @param out The shading
 * @param s The shape
 * @param border Linear border color, NULL for the color of the shape
 */
static void box_shape2d_rect(
    box_render_shading_t *out,
    const Shape2D *s,
    const float *border)
{
    float w = s->size[0] > 0.0f ? s->size[0] : 1.0f;
    float h = s->size[1] > 0.0f ? s->size[1] : 1.0f;
    float half = (w < h ? w : h) * 0.5f;
    const rgba *c = &s->border_color;
    out->material[0] = glm_clamp(s->radius / half, 0.0f, 1.0f);
    out->material[1] = s->border / half;
    uint32_t r = border ? box_shape2d_half(border[0]) : box_shape2d_linear(c->r);
    uint32_t g = border ? box_shape2d_half(border[1]) : box_shape2d_linear(c->g);
    uint32_t b = border ? box_shape2d_half(border[2]) : box_shape2d_linear(c->b);
    box_shape2d_pack(&out->material[2], r, g);
    box_shape2d_pack(&out->material[3], b, box_shape2d_half((float)c->a / 255.0f));
    out->emissive[3] = w / h;
}

/** Write the end points and width of a line.
 *
 * @param out The shading
 * @param s The shape
 */
static void box_shape2d_line(
    box_render_shading_t *out,
    const Shape2D *s)
{
    out->material[0] = s->line[0];
    out->material[1] = s->line[1];
    out->material[2] = s->line[2];
    out->material[3] = s->line[3];
    out->emissive[3] = -s->width;
}

void box_draws_shape2d(
    box_render_shading_t *out,
    const box_draw_material_t *material,
    const float *border)
{
    const Shape2D *s = material->shape2d;
    if (s->kind == BOX_SHAPE2D_LINE) {
        box_shape2d_line(out, s);
    } else {
        box_shape2d_rect(out, s, border);
    }

    out->uv[2] = s->uv[2] * out->uv[0] + out->uv[2];
    out->uv[3] = s->uv[3] * out->uv[1] + out->uv[3];
    out->uv[0] *= s->uv[0];
    out->uv[1] *= s->uv[1];
}
