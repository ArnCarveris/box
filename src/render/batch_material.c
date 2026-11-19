/**
 * @file render/batch_material.c
 * @brief Shading, material, transform and surface records of batch instances.
 *
 * Converts display encoded colors to linear light, applies tints and packs the material, mesh
 * shading, transform and surface records that batches upload per instance.
 */

#include "draw.h"
#include "../terrain/terrain.h"
#include <math.h>

/** Color of a shape without Rgb, display encoded */
static const uint8_t box_draws_default_color[3] = { 204, 204, 204 };

/** Color of a 2D shape with an Image and without Rgb, which shows the texture as is */
static const uint8_t box_draws_image_color[3] = { 255, 255, 255 };

/** Keep a function out of line where the compiler supports it */
#if defined(__GNUC__) || defined(__clang__)
#define BOX_DRAWS_NOINLINE __attribute__((noinline))
#else
#define BOX_DRAWS_NOINLINE
#endif

/** Linear light of every display encoded channel value */
static float box_draws_srgb[256];

void box_draws_tables_init(void)
{
    int32_t i;
    for (i = 0; i < 256; i ++) {
        float c = (float)i / 255.0f;
        box_draws_srgb[i] = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
    }
}

/** Convert a display encoded channel to linear light.
 *
 * @param value The channel
 * @return The linear value
 */
static float box_draws_linear(
    uint8_t value)
{
    return box_draws_srgb[value];
}

/** Convert a display encoded channel from 0 to 1 to linear light.
 *
 * Interpolates the table of the byte values, which stays within 1e-5 of the exact curve.
 *
 * @param value The channel
 * @return The linear value
 */
static float box_draws_linear_f(
    float value)
{
    float x = glm_clamp(value, 0.0f, 1.0f) * 255.0f;
    int32_t i = (int32_t)x;
    if (i > 254) {
        i = 254;
    }

    float f = x - (float)i;

    return box_draws_srgb[i] + (box_draws_srgb[i + 1] - box_draws_srgb[i]) * f;
}

void box_draws_tinted(
    float *out,
    const uint8_t *color,
    const box_draw_material_t *material)
{
    const box_draw_tint_t *inherited = material->inherited;
    const Tint *tint = material->tint;
    const uint8_t *own = tint ? &tint->r : NULL;
    float a = tint ? (float)tint->a / 255.0f : 0.0f;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        float c = (float)color[i] / 255.0f;
        if (inherited) {
            c = c * inherited->keep + inherited->add[i];
        }

        if (own) {
            c += ((float)own[i] / 255.0f - c) * a;
        }

        out[i] = box_draws_linear_f(c);
    }
}

float box_draws_alpha(
    const box_draw_material_t *material)
{
    float alpha = material->opacity ? material->opacity->value : 1.0f;

    return glm_clamp(alpha, 0.0f, 1.0f) * material->fade;
}

bool box_draws_cutout(
    const AlphaCutoff *cutoff,
    const Shape2D *shape2d)
{
    return cutoff != NULL && shape2d == NULL;
}

uint8_t box_draws_blend(
    const BlendMode *mode,
    float alpha,
    bool cutout)
{
    uint8_t result = mode ? (uint8_t)*mode : (uint8_t)Opaque;
    if (result == Opaque && alpha < 1.0f && !cutout) {
        result = AlphaBlend;
    }

    return result;
}

bool box_draws_scene_cutout(
    const AlphaCutoff *cutoff,
    const VertexReveal *reveal,
    const Shape2D *shape2d,
    uint32_t features)
{
    return (box_draws_cutout(cutoff, shape2d) || (reveal && !shape2d)) &&
        (features & BOX_RENDER_FEATURE_ALPHA_CUTOUT);
}

uint8_t box_draws_scene_blend(
    const BlendMode *mode,
    float alpha,
    bool cutout,
    const Shape2D *shape2d,
    uint32_t features)
{
    uint8_t result = box_draws_blend(mode, alpha, cutout);
    if (!shape2d && !(features & BOX_RENDER_FEATURE_BLEND_MODES)) {
        result = Opaque;
    }

    return result;
}

/** Convert a value from 0 to 1 to unorm16.
 *
 * @param value The value
 * @return The unorm16 bits
 */
static uint32_t box_draws_unorm16(
    float value)
{
    return (uint32_t)(glm_clamp(value, 0.0f, 1.0f) * 65535.0f + 0.5f);
}

void box_draws_color_pack(
    uint32_t *out,
    const float *color)
{
    out[0] = box_draws_unorm16(color[0]) | (box_draws_unorm16(color[1]) << 16);
    out[1] = box_draws_unorm16(color[2]) | (box_draws_unorm16(color[3]) << 16);
}

void box_draws_color_unpack(
    float *out,
    const uint32_t *packed)
{
    int32_t i;
    for (i = 0; i < 4; i ++) {
        out[i] = (float)((packed[i / 2] >> (16 * (i % 2))) & 0xFFFFu) / 65535.0f;
    }
}

/** Mix the color of a shape toward its tints and write the shape of a 2D shape.
 *
 * Kept out of line so the plain material path does not pay for the registers it needs.
 *
 * @param out The shading, with the plain material written
 * @param color The linear base color, receives the tinted one
 * @param material Material components of the entity
 * @param base The base color, display encoded
 */
static BOX_DRAWS_NOINLINE void box_draws_material_mixed(
    box_render_shading_t *out,
    float *color,
    const box_draw_material_t *material,
    const uint8_t *base)
{
    float border[3];
    bool tinted = material->inherited || material->tint;
    if (tinted) {
        box_draws_tinted(color, base, material);
    }

    if (material->shape2d && tinted) {
        box_draws_tinted(border, &material->shape2d->border_color.r, material);
    }

    if (material->shape2d) {
        box_draws_shape2d(out, material, tinted ? border : NULL);
    }
}

void box_draws_material(
    box_render_shading_t *out,
    const box_draw_material_t *material)
{
    const Rgb *color = material->color;
    const Emissive *emissive = material->emissive;
    const PbrMaterial *pbr = material->pbr;
    const TextureTransform *uv = material->uv;
    const uint8_t *base = color ? &color->r : box_draws_default_color;
    if (!color && material->shape2d && material->shape2d->texture) {
        base = box_draws_image_color;
    }

    float linear[4] = {
        box_draws_linear(base[0]),
        box_draws_linear(base[1]),
        box_draws_linear(base[2]),
        box_draws_alpha(material)
    };
    float strength = emissive && emissive->strength > 0.0f ? emissive->strength : 0.0f;
    out->emissive[0] = emissive ? box_draws_linear(emissive->color.r) * strength : 0.0f;
    out->emissive[1] = emissive ? box_draws_linear(emissive->color.g) * strength : 0.0f;
    out->emissive[2] = emissive ? box_draws_linear(emissive->color.b) * strength : 0.0f;
    out->emissive[3] = 0.0f;
    out->material[0] = pbr ? glm_clamp(pbr->metallic, 0.0f, 1.0f) : BOX_MATERIAL_METALLIC;
    out->material[1] = pbr ? glm_clamp(pbr->roughness, 0.0f, 1.0f) : BOX_MATERIAL_ROUGHNESS;
    out->material[2] = 0.0f;
    out->material[3] = 0.0f;
    out->uv[0] = uv ? uv->scale_x : 1.0f;
    out->uv[1] = uv ? uv->scale_y : 1.0f;
    out->uv[2] = uv ? uv->offset_x : 0.0f;
    out->uv[3] = uv ? uv->offset_y : 0.0f;
    out->ids[0] = material->clip > 0 ? (uint32_t)material->clip : 0u;
    out->ids[1] = material->rank;
    if (material->inherited || material->tint || material->shape2d) {
        box_draws_material_mixed(out, linear, material, base);
    }

    box_draws_color_pack(out->color, linear);
}

/** Pack a SpecularAnisotropy into the anisotropy word of a material record: the strength clamped
 * to 0..1 and the rotation wrapped to 0..2 pi over 2 pi, as two unorm16.
 *
 * @param anisotropy The component, NULL for none
 * @return strength | rotation << 16, 0 without one or with a strength of 0
 */
static uint32_t box_draws_anisotropy_pack(
    const SpecularAnisotropy *anisotropy)
{
    uint32_t result = 0u;
    if (anisotropy && box_draws_unorm16(anisotropy->strength)) {
        float turn = 2.0f * GLM_PIf;
        float rotation = anisotropy->rotation;
        rotation = isfinite(rotation) ? fmodf(rotation, turn) : 0.0f;
        if (rotation < 0.0f) {
            rotation += turn;
        }

        result = box_draws_unorm16(anisotropy->strength) |
            (box_draws_unorm16(rotation / turn) << 16);
    }

    return result;
}

/** Pack a Sheen into the sheen word of a material record: the display encoded color and the
 * roughness clamped to 0..1 as a unorm8.
 *
 * @param sheen The component, NULL for none
 * @return r | g << 8 | b << 16 | roughness << 24, 0 without one or with a black color
 */
static uint32_t box_draws_sheen_pack(
    const Sheen *sheen)
{
    uint32_t result = 0u;
    if (sheen && (sheen->color.r || sheen->color.g || sheen->color.b)) {
        float roughness = isfinite(sheen->roughness) ? sheen->roughness : 0.0f;
        uint32_t r = (uint32_t)(glm_clamp(roughness, 0.0f, 1.0f) * 255.0f + 0.5f);
        result = (uint32_t)sheen->color.r | ((uint32_t)sheen->color.g << 8) |
            ((uint32_t)sheen->color.b << 16) | (r << 24);
    }

    return result;
}

/** Pack a DiffuseTransmission into the diffuse_transmission word of a material record: the
 * display encoded color and the factor clamped to 0..1 as a unorm8.
 *
 * @param diffuse The component, NULL for none
 * @return r | g << 8 | b << 16 | factor << 24, 0 without one
 */
static uint32_t box_draws_diffuse_transmission_pack(
    const DiffuseTransmission *diffuse)
{
    uint32_t result = 0u;
    if (diffuse) {
        float factor = isfinite(diffuse->factor) ? diffuse->factor : 0.0f;
        uint32_t f = (uint32_t)(glm_clamp(factor, 0.0f, 1.0f) * 255.0f + 0.5f);
        result = (uint32_t)diffuse->color.r | ((uint32_t)diffuse->color.g << 8) |
            ((uint32_t)diffuse->color.b << 16) | (f << 24);
    }

    return result;
}

/** Pack a Fresnel into the fresnel words of a material record: the color in linear light as
 * unorm16, the strength and power when positive and finite, the bias when finite and the
 * opacity clamped to 0..1; all 0 without one.
 *
 * @param out The record
 * @param fresnel The component, NULL for none
 */
static void box_draws_fresnel_pack(
    box_render_material_t *out,
    const Fresnel *fresnel)
{
    out->fresnel_color[0] = 0u;
    out->fresnel_color[1] = 0u;
    out->fresnel_strength = 0.0f;
    out->fresnel_power = 0.0f;
    out->fresnel_bias = 0.0f;
    out->fresnel_opacity = 0.0f;
    if (fresnel) {
        out->fresnel_color[0] = box_draws_unorm16(box_draws_linear(fresnel->color.r)) |
            (box_draws_unorm16(box_draws_linear(fresnel->color.g)) << 16);
        out->fresnel_color[1] = box_draws_unorm16(box_draws_linear(fresnel->color.b));
        out->fresnel_strength = isfinite(fresnel->strength) && fresnel->strength > 0.0f
            ? fresnel->strength : 0.0f;
        out->fresnel_power = isfinite(fresnel->power) && fresnel->power > 0.0f
            ? fresnel->power : 0.0f;
        out->fresnel_bias = isfinite(fresnel->bias) ? fresnel->bias : 0.0f;
        out->fresnel_opacity = isfinite(fresnel->opacity)
            ? glm_clamp(fresnel->opacity, 0.0f, 1.0f) : 0.0f;
    }
}

float box_draws_fresnel_factor(
    float ndv,
    float power,
    float bias)
{
    float x = glm_max(1.0f - glm_clamp(ndv, 0.0f, 1.0f), 1e-6f);

    return glm_clamp(bias + powf(x, power), 0.0f, 1.0f);
}

float box_draws_water_depth_mix(
    float d,
    float depth)
{
    float result = 1.0f;
    if (isfinite(d)) {
        result = 1.0f - expf(-3.0f * glm_max(d, 0.0f) / glm_max(depth, BOX_RENDER_WATER_MIN));
    }

    return glm_clamp(result, 0.0f, 1.0f);
}

float box_draws_water_transmittance(
    float ray_length,
    float visibility)
{
    float result = 0.0f;
    if (isfinite(ray_length)) {
        result = expf(-3.0f * glm_max(ray_length, 0.0f) /
            glm_max(visibility, BOX_RENDER_WATER_MIN));
    }

    return glm_clamp(result, 0.0f, 1.0f);
}

float box_draws_water_absorption(
    float absorption,
    float ray_length,
    float visibility)
{
    float result = 1.0f;
    if (isfinite(ray_length)) {
        float a = glm_clamp(absorption, 1e-6f, 1.0f);
        float e = glm_max(ray_length, 0.0f) / glm_max(visibility, BOX_RENDER_WATER_MIN);
        result = exp2f(e * log2f(a));
    }

    return glm_clamp(result, 0.0f, 1.0f);
}

float box_draws_water_foam(
    float d,
    float foam_width,
    bool textured,
    float tex)
{
    float result = 0.0f;
    if (foam_width > 0.0f && isfinite(d)) {
        result = glm_clamp(1.0f - glm_max(d, 0.0f) / foam_width, 0.0f, 1.0f);
    }

    if (textured) {
        float t = 1.0f - result;
        float x = glm_clamp((tex - t) / 0.1f, 0.0f, 1.0f);
        result = result > 0.0f ? x * x * (3.0f - 2.0f * x) : 0.0f;
    }

    return result;
}

/** Pack a Water into the water words of a material record: the colors display encoded, the
 * depth and visibility at least BOX_RENDER_WATER_MIN, the other floats when finite (foam width
 * and ripple scale when positive), the ripple offset wrapped at the time; 0 without one.
 *
 * The set of its WaterWaves in the wave table goes into the flags.
 *
 * @param out The record
 * @param water The component, NULL for none
 * @param waves Set of its WaterWaves in the wave table, 0 for none
 * @param time Total render time the ripple offset is computed at
 */
static void box_draws_water_pack(
    box_render_material_t *out,
    const Water *water,
    uint32_t waves,
    double time)
{
    out->water_ripple_offset[0] = 0.0f;
    out->water_ripple_offset[1] = 0.0f;
    out->water_shallow = 0u;
    out->water_deep = 0u;
    out->water_foam = 0u;
    out->water_depth = 0.0f;
    out->water_visibility = 0.0f;
    out->water_refraction = 0.0f;
    out->water_foam_width = 0.0f;
    out->water_foam_scale = 0.0f;
    out->water_ripple_scale = 0.0f;
    out->water_flags = 0u;
    if (!water) {
        goto done;
    }

    out->water_shallow = (uint32_t)water->shallow_color.r |
        ((uint32_t)water->shallow_color.g << 8) | ((uint32_t)water->shallow_color.b << 16);
    out->water_deep = (uint32_t)water->deep_color.r | ((uint32_t)water->deep_color.g << 8) |
        ((uint32_t)water->deep_color.b << 16);
    out->water_foam = (uint32_t)water->foam_color.r | ((uint32_t)water->foam_color.g << 8) |
        ((uint32_t)water->foam_color.b << 16);
    out->water_depth = isfinite(water->depth) && water->depth > BOX_RENDER_WATER_MIN
        ? water->depth : BOX_RENDER_WATER_MIN;
    out->water_visibility = isfinite(water->visibility) &&
        water->visibility > BOX_RENDER_WATER_MIN ? water->visibility : BOX_RENDER_WATER_MIN;
    out->water_refraction = isfinite(water->refraction) ? water->refraction : 0.0f;
    out->water_foam_width = isfinite(water->foam_width) && water->foam_width > 0.0f
        ? water->foam_width : 0.0f;
    out->water_foam_scale = isfinite(water->foam_scale) ? water->foam_scale : 0.0f;
    out->water_ripple_scale = isfinite(water->ripple_scale) && water->ripple_scale > 0.0f
        ? water->ripple_scale : 0.0f;
    out->water_flags = water->foam_texture ? BOX_RENDER_WATER_FOAM_TEXTURE : 0u;
    out->water_flags |= ((uint32_t)water->absorption.r | ((uint32_t)water->absorption.g << 8) |
        ((uint32_t)water->absorption.b << 16)) << BOX_RENDER_WATER_ABSORPTION_SHIFT;
    out->water_flags |= (waves & BOX_RENDER_WATER_WAVES_MASK) << BOX_RENDER_WATER_WAVES_SHIFT;
    if (out->water_ripple_scale > 0.0f) {
        TextureScroll ripple = {
            .velocity = { water->ripple_velocity[0], water->ripple_velocity[1] }
        };
        float offsets[4];
        box_draws_texture_scroll(&ripple, time, offsets);
        out->water_ripple_offset[0] = offsets[0];
        out->water_ripple_offset[1] = offsets[1];
    }

done:
    return;
}

float box_draws_water_flow_phase(
    double time)
{
    float result = 0.0f;
    if (isfinite(time)) {
        double v = fmod(time / BOX_RENDER_WATER_FLOW_PERIOD, 1.0);
        if (v < 0.0) {
            v += 1.0;
        }

        result = (float)v;
        if (result >= 1.0f) {
            result = 0.0f;
        }
    }

    return result;
}

/** Pack the WaterFlow of a Water into the flow words of its record, which hold the Distortion of
 * a record without Water.
 *
 * With WaterFlow.textures they are its velocity (0 when not finite), the phase of the flow
 * mapping at the time and BOX_RENDER_WATER_FLOW_TEXTURES; 0 without one or without textures.
 *
 * @param out The record
 * @param flow The WaterFlow of the Water, NULL for none
 * @param time Total render time the phase is computed at
 */
static void box_draws_water_flow_pack(
    box_render_material_t *out,
    const WaterFlow *flow,
    double time)
{
    out->water_flow[0] = 0.0f;
    out->water_flow[1] = 0.0f;
    out->water_flow_phase = 0.0f;
    out->water_flow_flags = 0u;
    if (!flow || !flow->textures) {
        goto done;
    }

    out->water_flow[0] = isfinite(flow->velocity[0]) ? flow->velocity[0] : 0.0f;
    out->water_flow[1] = isfinite(flow->velocity[1]) ? flow->velocity[1] : 0.0f;
    out->water_flow_phase = box_draws_water_flow_phase(time);
    out->water_flow_flags = BOX_RENDER_WATER_FLOW_TEXTURES;
done:
    return;
}

/** Pack a WaterCaustics into the caustics words of a Water record, which hold the Fresnel of a
 * record without Water: its strength when positive and finite, the inverse of its scale and its
 * depth (at least BOX_RENDER_CAUSTICS_MIN), its water frame and its phase at the time.
 *
 * Without one or without a strength the words are 0, which draws as without one.
 *
 * @param out The record
 * @param caustics The component, NULL for none
 * @param frame Offset of its water frame in cells, wrapped
 * @param time Total render time the phase is computed at
 */
static void box_draws_water_caustics_pack(
    box_render_material_t *out,
    const WaterCaustics *caustics,
    const float *frame,
    double time)
{
    out->caustics_offset[0] = 0.0f;
    out->caustics_offset[1] = 0.0f;
    out->caustics_strength = 0.0f;
    out->caustics_frequency = 0.0f;
    out->caustics_depth = 0.0f;
    out->caustics_phase = 0.0f;
    if (!caustics || !isfinite(caustics->strength) || caustics->strength <= 0.0f) {
        goto done;
    }

    out->caustics_strength = caustics->strength;
    out->caustics_frequency = box_draws_water_caustics_frequency(caustics);
    out->caustics_depth = isfinite(caustics->depth) && caustics->depth > BOX_RENDER_CAUSTICS_MIN
        ? caustics->depth : BOX_RENDER_CAUSTICS_MIN;
    out->caustics_offset[0] = isfinite(frame[0]) ? frame[0] : 0.0f;
    out->caustics_offset[1] = isfinite(frame[1]) ? frame[1] : 0.0f;
    out->caustics_phase = box_draws_water_caustics_phase(caustics->speed, time);
done:
    return;
}

float box_draws_distortion_phase(
    float speed,
    double time)
{
    float result = 0.0f;
    if (isfinite(speed) && isfinite(time)) {
        double v = fmod((double)speed * time, 1.0);
        if (v < 0.0) {
            v += 1.0;
        }

        result = (float)v;
        if (result >= 1.0f) {
            result = 0.0f;
        }
    }

    return result;
}

/** Pack a Distortion into the distortion words of a material record: its strength when
 * positive and finite, the inverse of its scale and its phase at the time; 0 without one, and
 * 0 with Transmission and no strength, which then looks as without one.
 *
 * A shape with a Distortion and no Transmission shows the scene behind as a transmission
 * factor of 1 without refraction or absorption does, and samples the sharp snapshot
 * (BOX_RENDER_DISTORTION_SHARP).
 *
 * @param out The record, its transmission words already packed
 * @param distortion The component, NULL for none
 * @param transmission The Transmission of the material, NULL for none
 * @param time Total render time the phase is computed at
 */
static void box_draws_distortion_pack(
    box_render_material_t *out,
    const Distortion *distortion,
    const Transmission *transmission,
    double time)
{
    out->distortion_strength = 0.0f;
    out->distortion_frequency = 0.0f;
    out->distortion_phase = 0.0f;
    out->distortion_flags = 0u;
    if (!distortion) {
        goto done;
    }

    float strength = isfinite(distortion->strength) && distortion->strength > 0.0f
        ? distortion->strength : 0.0f;
    if (transmission && strength == 0.0f) {
        goto done;
    }

    float scale = isfinite(distortion->scale) && distortion->scale > BOX_RENDER_DISTORTION_MIN
        ? distortion->scale : BOX_RENDER_DISTORTION_MIN;
    out->distortion_strength = strength;
    out->distortion_frequency = 1.0f / scale;
    out->distortion_phase = box_draws_distortion_phase(distortion->speed, time);
    out->distortion_flags = BOX_RENDER_DISTORTION_PRESENT;
    if (!transmission) {
        out->distortion_flags |= BOX_RENDER_DISTORTION_SHARP;
        out->transmission_factor = 1.0f;
        out->ior = 1.0f;
        out->thickness_factor = 0.0f;
        out->attenuation_distance = BOX_MATERIAL_ATTENUATION_DISTANCE;
        out->attenuation_color = 0xFFFFFFu;
    }

done:
    return;
}

/** Wrap one axis of a scroll offset to 0..2.
 *
 * @param velocity Texture widths per second
 * @param time Total render time
 * @return The offset, 0 for a velocity that is not finite
 */
static float box_draws_scroll_axis(
    float velocity,
    double time)
{
    float result = 0.0f;
    if (isfinite(velocity) && isfinite(time)) {
        double v = fmod((double)velocity * time, 2.0);
        if (v < 0.0) {
            v += 2.0;
        }

        result = (float)v;
        if (result >= 2.0f) {
            result = 0.0f;
        }
    }

    return result;
}

void box_draws_texture_scroll(
    const TextureScroll *scroll,
    double time,
    float *out)
{
    out[0] = scroll ? box_draws_scroll_axis(scroll->velocity[0], time) : 0.0f;
    out[1] = scroll ? box_draws_scroll_axis(scroll->velocity[1], time) : 0.0f;
    out[2] = scroll ? box_draws_scroll_axis(scroll->normal_velocity[0], time) : 0.0f;
    out[3] = scroll ? box_draws_scroll_axis(scroll->normal_velocity[1], time) : 0.0f;
}

/** Convert a positive float to a half float, rounded toward zero, the largest half for one
 * above its range and 0 for one below it.
 *
 * @param value The value, positive and finite
 * @return The bits of the half float
 */
static uint32_t box_draws_half(
    float value)
{
    uint32_t bits, result = 0u;
    ecs_os_memcpy(&bits, &value, ECS_SIZEOF(bits));
    int32_t exponent = (int32_t)((bits >> 23) & 0xFFu) - 127 + 15;
    uint32_t mantissa = (bits & 0x7FFFFFu) | 0x800000u;
    if (exponent >= 31) {
        result = 0x7BFFu;
    } else if (exponent > 0) {
        result = ((uint32_t)exponent << 10) | ((bits >> 13) & 0x3FFu);
    } else if (exponent > -10) {
        result = mantissa >> (14 - exponent);
    }

    return result;
}

/** Pack a VertexReveal into the reveal bits of the flags and the threshold of a material record,
 * after the wind fields, which set the flags.
 *
 * @param out The record
 * @param reveal The VertexReveal, NULL for none
 */
static void box_draws_reveal_pack(
    box_render_material_t *out,
    const VertexReveal *reveal)
{
    out->reveal_threshold = 0.0f;
    if (!reveal || reveal->channel < 0 || reveal->channel > 7) {
        goto done;
    }

    float softness = isfinite(reveal->softness) && reveal->softness > 0.0f
        ? reveal->softness : 0.0f;
    out->reveal_threshold = isfinite(reveal->threshold) ? reveal->threshold : 0.0f;
    out->flags |= BOX_RENDER_REVEAL_PRESENT |
        ((uint32_t)reveal->channel << BOX_RENDER_REVEAL_CHANNEL_SHIFT) |
        (reveal->invert ? BOX_RENDER_REVEAL_INVERT : 0u) |
        (box_draws_half(softness) << BOX_RENDER_REVEAL_SOFTNESS_SHIFT);
done:
    return;
}

void box_draws_material_pack(
    box_render_material_t *out,
    const box_draw_material_t *material)
{
    float scroll[4];
    const Rgb *color = material->color;
    const Emissive *emissive = material->emissive;
    const PbrMaterial *pbr = material->pbr;
    const TextureTransform *uv = material->uv;
    const Transmission *transmission = material->transmission;
    const ClearCoat *coat = material->clear_coat;
    const uint8_t *base = color ? &color->r : box_draws_default_color;
    float strength = emissive && emissive->strength > 0.0f ? emissive->strength : 0.0f;
    out->color = (uint32_t)base[0] | ((uint32_t)base[1] << 8) | ((uint32_t)base[2] << 16);
    out->metallic = pbr ? glm_clamp(pbr->metallic, 0.0f, 1.0f) : BOX_MATERIAL_METALLIC;
    out->roughness = pbr ? glm_clamp(pbr->roughness, 0.0f, 1.0f) : BOX_MATERIAL_ROUGHNESS;
    out->cutoff = material->cutoff ? glm_clamp(material->cutoff->value, 0.0f, 1.0f) : 0.0f;
    out->emissive[0] = emissive ? box_draws_linear(emissive->color.r) * strength : 0.0f;
    out->emissive[1] = emissive ? box_draws_linear(emissive->color.g) * strength : 0.0f;
    out->emissive[2] = emissive ? box_draws_linear(emissive->color.b) * strength : 0.0f;
    out->attenuation_color = transmission
        ? (uint32_t)transmission->attenuation_color.r |
            ((uint32_t)transmission->attenuation_color.g << 8) |
            ((uint32_t)transmission->attenuation_color.b << 16)
        : 0xFFFFFFu;
    out->uv[0] = uv ? uv->scale_x : 1.0f;
    out->uv[1] = uv ? uv->scale_y : 1.0f;
    out->uv[2] = uv ? uv->offset_x : 0.0f;
    out->uv[3] = uv ? uv->offset_y : 0.0f;
    if (material->scroll) {
        box_draws_texture_scroll(material->scroll, material->time, scroll);
        out->uv[2] += scroll[0];
        out->uv[3] += scroll[1];
        out->normal_offset[0] = scroll[2];
        out->normal_offset[1] = scroll[3];
    } else {
        out->normal_offset[0] = 0.0f;
        out->normal_offset[1] = 0.0f;
    }

    box_draws_water_pack(out, material->water, material->water_waves, material->time);
    if (material->water) {
        box_draws_water_caustics_pack(out, material->caustics, material->caustics_frame,
            material->time);
    } else {
        box_draws_fresnel_pack(out, material->fresnel);
    }

    box_render_wind_pack(out, material->sway, &material->wind);
    box_draws_reveal_pack(out, material->shape2d ? NULL : material->reveal);
    out->transmission_factor = transmission ? transmission->transmission_factor : 0.0f;
    out->ior = transmission ? transmission->ior : BOX_MATERIAL_IOR;
    out->thickness_factor = transmission ? glm_max(transmission->thickness_factor, 0.0f) : 0.0f;
    out->attenuation_distance = transmission
        ? transmission->attenuation_distance : BOX_MATERIAL_ATTENUATION_DISTANCE;
    out->clear_coat = coat && box_draws_unorm16(coat->strength)
        ? box_draws_unorm16(coat->strength) | (box_draws_unorm16(coat->roughness) << 16) : 0u;
    out->anisotropy = box_draws_anisotropy_pack(material->anisotropy);
    out->sheen = box_draws_sheen_pack(material->sheen);
    out->diffuse_transmission = box_draws_diffuse_transmission_pack(
        material->diffuse_transmission);
    if (material->water) {
        box_draws_water_flow_pack(out, material->flow, material->time);
    } else {
        box_draws_distortion_pack(out, material->distortion, transmission, material->time);
    }

    out->thickness = material->diffuse_transmission &&
        isfinite(material->diffuse_transmission->thickness) &&
        material->diffuse_transmission->thickness > 0.0f
        ? material->diffuse_transmission->thickness : 0.0f;
    out->diffuse_blur = material->diffuse_transmission &&
        isfinite(material->diffuse_transmission->blur) &&
        material->diffuse_transmission->blur > 0.0f
        ? material->diffuse_transmission->blur : 0.0f;
    out->mapping = (uint32_t)MappingUv;
    out->mapping_size = 0.0f;
    if (material->mapping && (material->mapping->mode == MappingObject ||
        material->mapping->mode == MappingWorld || material->mapping->mode == MappingFrame))
    {
        float size = material->mapping->size;
        float blend = material->mapping->blend;
        out->mapping = (uint32_t)material->mapping->mode |
            (box_draws_unorm16(isfinite(blend) ? blend : 0.0f) << 16);
        out->mapping_size = isfinite(size) && size > 0.0f ? size : 1.0f;
    }
}

void box_draws_mesh_shading(
    box_render_mesh_shading_t *out,
    uint32_t id,
    const box_draw_material_t *material)
{
    const box_draw_tint_t *inherited = material->inherited;
    const Tint *tint = material->tint;
    float keep = inherited ? inherited->keep : 1.0f;
    float add[3] = { 0.0f, 0.0f, 0.0f };
    int32_t i;
    if (inherited) {
        glm_vec3_copy(ECS_CONST_CAST(float*, inherited->add), add);
    }

    if (tint) {
        float a = (float)tint->a / 255.0f;
        const uint8_t *own = &tint->r;
        for (i = 0; i < 3; i ++) {
            add[i] = add[i] * (1.0f - a) + (float)own[i] / 255.0f * a;
        }

        keep *= 1.0f - a;
    }

    out->material = id;
    out->tint[0] = box_draws_unorm16(add[0]) | (box_draws_unorm16(add[1]) << 16);
    out->tint[1] = box_draws_unorm16(add[2]) | (box_draws_unorm16(keep) << 16);
    out->alpha = box_draws_alpha(material);
}

void box_draws_transform(
    box_render_transform_t *out,
    const WorldTransform *transform,
    const ShapeMesh *shape)
{
    int32_t r, c;
    for (r = 0; r < 3; r ++) {
        for (c = 0; c < 3; c ++) {
            out->rows[r][c] = transform->m[c][r] * shape->scale[c];
        }

        out->rows[r][3] = transform->m[3][r];
    }
}

box_render_transform_t* box_draws_slot_frame(
    box_draw_batch_t *b,
    int32_t slot)
{
    return b->key.frame ? ecs_vec_get_t(&b->frames, box_render_transform_t, slot) : NULL;
}

ecs_entity_t box_render_texture_frame(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = 0;
    if (!ecs_id_in_use(world, TextureFrame)) {
        goto done;
    }

    for (; e && !result; e = ecs_get_parent(world, e)) {
        if (ecs_has_id(world, e, TextureFrame)) {
            result = e;
        }
    }

done:
    return result;
}

void box_draws_frame(
    box_render_transform_t *out,
    const box_render_transform_t *model,
    const WorldTransform *frame)
{
    mat4 inv;
    mat3 axes;
    int32_t r, c, k;
    if (!frame) {
        *out = *model;
        goto done;
    }

    glm_mat4_pick3(ECS_CONST_CAST(vec4*, frame->m), axes);
    if (!(fabsf(glm_mat3_det(axes)) > 1e-30f)) {
        *out = *model;
        goto done;
    }

    glm_mat4_inv(ECS_CONST_CAST(vec4*, frame->m), inv);
    for (k = 0; k < 3; k ++) {
        for (c = 0; c < 4; c ++) {
            float sum = c == 3 ? inv[3][k] : 0.0f;
            for (r = 0; r < 3; r ++) {
                sum += inv[r][k] * model->rows[r][c];
            }

            out->rows[k][c] = sum;
        }
    }

done:
    return;
}

void box_draws_frame_scaled(
    box_render_transform_t *out,
    const box_render_transform_t *model,
    const WorldTransform *frame)
{
    float scale = frame ? box_transform_world_scale(frame) : 1.0f;
    float inv = scale > 1e-20f ? 1.0f / scale : 1.0f;
    int32_t r, c;
    *out = *model;
    for (r = 0; r < 3; r ++) {
        for (c = 0; c < 3; c ++) {
            out->rows[r][c] *= inv;
        }
    }
}

void box_draws_frame_src(
    const ecs_world_t *world,
    ecs_entity_t e,
    box_draw_frame_src_t *out)
{
    const TextureMapping *mapping = ecs_get(world, e, TextureMapping);
    out->frame = box_render_texture_frame(world, e);
    out->mode = mapping ? (uint8_t)mapping->mode : (uint8_t)MappingUv;
    out->live = false;
}

const box_draw_frame_pose_t* box_draws_frame_pose(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t frame)
{
    ecs_map_val_t *v = ecs_map_get(&d->frame_poses, frame);
    box_draw_frame_pose_t *result;
    mat3 axes;
    if (v) {
        result = ecs_vec_get_t(&d->frame_cache, box_draw_frame_pose_t, (int32_t)*v - 1);
    } else {
        result = ecs_vec_append_t(NULL, &d->frame_cache, box_draw_frame_pose_t);
        ecs_os_memset_t(result, 0, box_draw_frame_pose_t);
        result->frame = frame;
        result->tick = d->tick - 1;
        ecs_map_insert(&d->frame_poses, frame, (ecs_map_val_t)ecs_vec_count(&d->frame_cache));
    }

    if (result->tick == d->tick) {
        goto done;
    }

    result->tick = d->tick;
    result->valid = false;
    result->scale = 1.0f;
    WorldTransform t_buf;
    const WorldTransform *t = ecs_is_alive(world, frame)
        ? box_transform_world(world, frame, &t_buf) : NULL;
    if (!t) {
        goto done;
    }

    result->scale = box_transform_world_scale(t);
    glm_mat4_pick3(ECS_CONST_CAST(vec4*, t->m), axes);
    if (fabsf(glm_mat3_det(axes)) > 1e-30f) {
        glm_mat4_inv(ECS_CONST_CAST(vec4*, t->m), result->inv);
        result->valid = true;
    }

done:
    return result;
}

void box_draws_frame_record(
    const ecs_world_t *world,
    box_render_draws_t *d,
    const box_draw_frame_src_t *src,
    box_render_transform_t *out,
    const box_render_transform_t *model)
{
    const box_draw_frame_pose_t *pose = src->frame
        ? box_draws_frame_pose(world, d, src->frame) : NULL;
    int32_t r, c, k;
    if (src->mode == MappingObject) {
        float scale = pose ? pose->scale : 1.0f;
        float inv = scale > 1e-20f ? 1.0f / scale : 1.0f;
        *out = *model;
        for (r = 0; r < 3; r ++) {
            for (c = 0; c < 3; c ++) {
                out->rows[r][c] *= inv;
            }
        }

        goto done;
    }

    if (!pose || !pose->valid) {
        *out = *model;
        goto done;
    }

    for (k = 0; k < 3; k ++) {
        for (c = 0; c < 4; c ++) {
            float sum = c == 3 ? pose->inv[3][k] : 0.0f;
            for (r = 0; r < 3; r ++) {
                sum += pose->inv[r][k] * model->rows[r][c];
            }

            out->rows[k][c] = sum;
        }
    }

done:
    return;
}

box_draw_frame_src_t* box_draws_slot_frame_src(
    box_draw_batch_t *b,
    int32_t slot)
{
    return b->key.frame ? ecs_vec_get_t(&b->frame_srcs, box_draw_frame_src_t, slot) : NULL;
}

void box_draws_surface(
    box_render_surface_t *out,
    const ShapeMesh *shape)
{
    ecs_os_memcpy(out->p, shape->surface.p, ECS_SIZEOF(out->p));
    out->p[19][0] = shape->render_mesh != 0;
    if (shape->surface.p[0][0]) {
        for (int j = 0; j < 3; j ++) {
            out->p[22][j] = (shape->min[j] + shape->max[j]) * 0.5f;
            out->p[23][j] = (shape->max[j] - shape->min[j]) * 0.5f;
        }
    }
}

uint32_t box_draws_texture_handle(
    const box_render_draws_t *d,
    ecs_entity_t e)
{
    uint32_t result = e ? box_textures_handle(d->textures, e) : 0;
    if (e && !result && d->world) {
        result = box_render_texture_target_handle(d->world, e);
    }

    return result;
}

void box_draws_batch_tiles(
    const box_render_draws_t *d,
    ecs_entity_t e,
    const TileTerrain *terrain,
    box_render_texture_blend_t *r)
{
    const TileTerrainGrid *grid = ecs_get(d->world, e, TileTerrainGrid);
    const TileKind *kinds = ecs_vec_first(&terrain->kinds);
    int32_t k, count = ecs_vec_count(&terrain->kinds);
    count = count < BOX_RENDER_BLEND_SLICES ? count : BOX_RENDER_BLEND_SLICES;
    r->tiles = 1;
    r->mask = grid ? box_draws_texture_handle(d, grid->texture) : 0;
    r->count = count > 0 ? (uint32_t)count : 1;
    for (k = 0; k < count; k ++) {
        ecs_entity_t kind = kinds[k].textures;
        const PbrTextures *textures = kind && ecs_is_alive(d->world, kind)
            ? ecs_get(d->world, kind, PbrTextures) : NULL;
        if (textures) {
            r->layers[k][BOX_RENDER_BLEND_ALBEDO] = box_draws_texture_handle(d, textures->albedo);
            r->layers[k][BOX_RENDER_BLEND_ORM] = box_draws_texture_handle(d, textures->orm);
            r->layers[k][BOX_RENDER_BLEND_NORMAL] = box_draws_texture_handle(d, textures->normal);
        }
    }
}

/** Check whether a 3D shape fades out a removed material lobe its scene enables: Sheen,
 * ClearCoat, SpecularAnisotropy, DiffuseTransmission or Fresnel with on_remove.
 *
 * @param world The world
 * @param e The entity
 * @param features The scene features of its key
 * @return Whether one of those leaves
 */
static bool box_draws_lobe_leaving(
    const ecs_world_t *world,
    ecs_entity_t e,
    uint32_t features)
{
    static const struct {
        uint32_t feature;
        box_render_transition_component_t component;
    } lobes[] = {
        { BOX_RENDER_FEATURE_SHEEN, BOX_RENDER_TRANSITION_SHEEN },
        { BOX_RENDER_FEATURE_CLEAR_COAT, BOX_RENDER_TRANSITION_CLEAR_COAT },
        { BOX_RENDER_FEATURE_ANISOTROPY, BOX_RENDER_TRANSITION_ANISOTROPY },
        { BOX_RENDER_FEATURE_DIFFUSE_TRANSMISSION, BOX_RENDER_TRANSITION_DIFFUSE_TRANSMISSION },
        { BOX_RENDER_FEATURE_FRESNEL, BOX_RENDER_TRANSITION_FRESNEL }
    };
    const box_render_transitions_t *s = box_render_transitions_get(world);
    bool result = false;
    int32_t i;
    for (i = 0; s && !result && i < (int32_t)(sizeof(lobes) / sizeof(lobes[0])); i ++) {
        result = (features & lobes[i].feature) &&
            box_render_transitions_leaving(world, s, e, lobes[i].component);
    }

    return result;
}

bool box_draws_distorted(
    const ecs_world_t *world,
    ecs_entity_t e,
    uint32_t features)
{
    bool result = false;
    if (!(features & BOX_RENDER_FEATURE_DISTORTION)) {
        goto done;
    }

    result = ecs_get(world, e, Distortion) != NULL;
    if (!result) {
        const box_render_transitions_t *s = box_render_transitions_get(world);
        result = s && box_render_transitions_leaving(world, s, e,
            BOX_RENDER_TRANSITION_DISTORTION);
    }
done:
    return result;
}

bool box_draws_extended(
    const ecs_world_t *world,
    const box_render_draws_t *d,
    ecs_entity_t e,
    const box_draw_key_t *key)
{
    const uint32_t lobes = BOX_RENDER_FEATURE_SHEEN | BOX_RENDER_FEATURE_CLEAR_COAT |
        BOX_RENDER_FEATURE_ANISOTROPY | BOX_RENDER_FEATURE_DIFFUSE_TRANSMISSION |
        BOX_RENDER_FEATURE_FRESNEL;
    uint32_t f = key->features;
    bool result = key->transmissive || key->water ||
        ((f & BOX_RENDER_FEATURE_PLANAR) && ecs_get(world, e, PlanarReflection) != NULL);
    const TextureMapping *mapping = result || !(f & BOX_RENDER_FEATURE_TRIPLANAR)
        ? NULL : ecs_get(world, e, TextureMapping);
    ecs_iter_t it;
    if (mapping && mapping->mode != MappingUv && isfinite(mapping->blend) &&
        mapping->blend >= 0.5f / 65535.0f)
    {
        result = true;
    }

    if (result || !(f & lobes)) {
        goto done;
    }

    if (ecs_query_has_table(d->extended, ecs_get_table(world, e), &it)) {
        ecs_iter_fini(&it);
        result = (f & lobes) == lobes ||
            ((f & BOX_RENDER_FEATURE_SHEEN) && ecs_get(world, e, Sheen)) ||
            ((f & BOX_RENDER_FEATURE_CLEAR_COAT) && ecs_get(world, e, ClearCoat)) ||
            ((f & BOX_RENDER_FEATURE_ANISOTROPY) && ecs_get(world, e, SpecularAnisotropy)) ||
            ((f & BOX_RENDER_FEATURE_DIFFUSE_TRANSMISSION) &&
                ecs_get(world, e, DiffuseTransmission)) ||
            ((f & BOX_RENDER_FEATURE_FRESNEL) && ecs_get(world, e, Fresnel));
    }

    if (!result) {
        result = box_draws_lobe_leaving(world, e, f);
    }
done:
    return result;
}

/** Clamp a float to a range, a value that is not finite to a fallback.
 *
 * @param v The value
 * @param lo Lower bound
 * @param hi Upper bound
 * @param fallback Value of a v that is not finite
 * @return The clamped value
 */
static float box_draws_layer_clamp(
    float v,
    float lo,
    float hi,
    float fallback)
{
    return isfinite(v) ? glm_clamp(v, lo, hi) : fallback;
}

void box_draws_layer_pack(
    const ecs_world_t *world,
    ecs_entity_t e,
    double time,
    box_render_texture_layer_t *out)
{
    const TextureLayer *l = ecs_get(world, e, TextureLayer);
    const Rgb *rgb = ecs_get(world, e, Rgb);
    const PbrMaterial *pbr = ecs_get(world, e, PbrMaterial);
    const PbrTextures *textures = ecs_get(world, e, PbrTextures);
    const TextureMapping *mapping = ecs_get(world, e, TextureMapping);
    const TextureTransform *uv = ecs_get(world, e, TextureTransform);
    const TextureScroll *scroll = ecs_get(world, e, TextureScroll);
    float scrolled[4];
    ecs_os_memset_t(out, 0, box_render_texture_layer_t);
    if (!l) {
        goto done;
    }

    out->color[0] = rgb ? box_draws_linear(rgb->r) : 1.0f;
    out->color[1] = rgb ? box_draws_linear(rgb->g) : 1.0f;
    out->color[2] = rgb ? box_draws_linear(rgb->b) : 1.0f;
    out->color[3] = box_draws_layer_clamp(l->amount, 0.0f, 1.0f, 0.0f);

    vec3 dir = { l->direction[0], l->direction[1], l->direction[2] };
    float len = glm_vec3_norm(dir);
    if (!isfinite(len) || len < 1e-6f) {
        glm_vec3_copy((vec3){ 0.0f, 1.0f, 0.0f }, dir);
    } else {
        glm_vec3_scale(dir, 1.0f / len, dir);
    }

    out->direction[0] = dir[0];
    out->direction[1] = dir[1];
    out->direction[2] = dir[2];
    out->direction[3] = box_draws_layer_clamp(l->facing, 0.0f, 1.0f, 0.0f);

    float contrast = isfinite(l->contrast) && l->contrast > 0.0f
        ? l->contrast : BOX_MATERIAL_LAYER_CONTRAST;
    out->coverage[0] = box_draws_layer_clamp(l->cavity, -1.0f, 1.0f, 0.0f);
    out->coverage[1] = 1.0f / contrast;
    out->coverage[2] = box_draws_layer_clamp(l->normal, 0.0f, 1.0f, 0.0f);
    out->coverage[3] = box_draws_layer_clamp(l->porosity, 0.0f, 1.0f, 0.0f);

    out->material[0] = pbr
        ? box_draws_layer_clamp(pbr->roughness, 0.0f, 1.0f, BOX_MATERIAL_ROUGHNESS)
        : BOX_MATERIAL_ROUGHNESS;
    out->material[1] = pbr
        ? box_draws_layer_clamp(pbr->metallic, 0.0f, 1.0f, BOX_MATERIAL_METALLIC)
        : BOX_MATERIAL_METALLIC;
    out->material[2] = mapping && isfinite(mapping->size) && mapping->size > 0.0f
        ? mapping->size : 1.0f;
    out->material[3] = mapping ? box_draws_layer_clamp(mapping->blend, 0.0f, 1.0f, 0.0f) : 0.0f;

    box_draws_texture_scroll(scroll, time, scrolled);
    out->transform[0] = uv ? uv->scale_x : 1.0f;
    out->transform[1] = uv ? uv->scale_y : 1.0f;
    out->transform[2] = (uv ? uv->offset_x : 0.0f) + scrolled[0];
    out->transform[3] = (uv ? uv->offset_y : 0.0f) + scrolled[1];

    out->modes[0] = (uint32_t)l->albedo <= LayerMultiply ? (uint32_t)l->albedo : LayerKeep;
    out->modes[1] = (uint32_t)l->roughness <= LayerMultiply ? (uint32_t)l->roughness : LayerKeep;
    out->modes[2] = (uint32_t)l->metallic <= LayerMultiply ? (uint32_t)l->metallic : LayerKeep;
    out->modes[3] = (uint32_t)l->occlusion <= LayerMultiply ? (uint32_t)l->occlusion : LayerKeep;

    out->flags[0] = (textures && textures->albedo ? BOX_RENDER_LAYER_HAS_ALBEDO : 0u) |
        (textures && textures->orm ? BOX_RENDER_LAYER_HAS_ORM : 0u) |
        (textures && textures->normal ? BOX_RENDER_LAYER_HAS_NORMAL : 0u) |
        (l->mask ? BOX_RENDER_LAYER_HAS_MASK : 0u) |
        (mapping && mapping->mode == MappingUv ? BOX_RENDER_LAYER_UV : 0u);
done:
    return;
}

uint64_t box_draws_layers_hash(
    const box_draw_key_t *key)
{
    uint64_t h = 0;
    int32_t k;
    for (k = 0; k < BOX_RENDER_LAYERS; k ++) {
        h = (h ^ (h >> 31)) * 0x9E3779B97F4A7C15ull + key->layers[k];
    }

    return h;
}

bool box_draws_key_layered(
    const box_draw_key_t *key)
{
    bool result = false;
    int32_t k;
    for (k = 0; k < BOX_RENDER_LAYERS; k ++) {
        result |= key->layers[k] != 0;
    }

    return result;
}

void box_draws_batch_layers(
    box_render_packet_t *p,
    const box_render_draws_t *d,
    const box_draw_batch_t *b,
    box_render_batch_t *out)
{
    uint32_t offset = 0;
    int32_t k;
    if (!box_draws_key_layered(&b->key) || !d->world) {
        goto done;
    }

    box_render_texture_layers_t *r = box_render_packet_reserve(p,
        (uint32_t)sizeof(box_render_texture_layers_t), &offset);
    ecs_os_memset_t(r, 0, box_render_texture_layers_t);
    r->count = 1;
    r->quality = d->textures && d->textures->quality
        ? (uint32_t)d->textures->quality : BOX_TEXTURE_QUALITY_DEFAULT;
    for (k = 0; k < BOX_RENDER_LAYERS; k ++) {
        ecs_entity_t layer = b->key.layers[k];
        if (!layer || !ecs_is_alive(d->world, layer) || !ecs_has(d->world, layer, TextureLayer)) {
            continue;
        }

        const PbrTextures *textures = ecs_get(d->world, layer, PbrTextures);
        const TextureLayer *l = ecs_get(d->world, layer, TextureLayer);
        r->entities[k] = layer;
        r->count = (uint32_t)k + 1;
        box_draws_layer_pack(d->world, layer, d->materials.time, &r->values[k]);
        if (textures) {
            r->maps[k][BOX_RENDER_LAYER_ALBEDO] = box_draws_texture_handle(d, textures->albedo);
            r->maps[k][BOX_RENDER_LAYER_ORM] = box_draws_texture_handle(d, textures->orm);
            r->maps[k][BOX_RENDER_LAYER_NORMAL] = box_draws_texture_handle(d, textures->normal);
        }

        r->maps[k][BOX_RENDER_LAYER_MASK] = box_draws_texture_handle(d, l->mask);
    }

    out->texture_layers = true;
    out->layers = offset;
done:
    return;
}

void box_draws_batch_textures(
    const box_render_draws_t *d,
    const box_draw_batch_t *b,
    box_render_batch_t *out)
{
    int32_t i;
    for (i = 0; i < BOX_RENDER_TEXTURE_SLOTS; i ++) {
        ecs_entity_t e = b->key.textures[i];
        out->textures[i] = e ? box_textures_handle(d->textures, e) : 0;
        if (e && !out->textures[i] && d->world) {
            out->textures[i] = box_render_texture_target_handle(d->world, e);
        }
    }

    if (b->key.atlas) {
        out->textures[2] = b->key.atlas;
    }

    out->foam = b->key.foam ? box_textures_handle(d->textures, b->key.foam) : 0;
    if (b->key.foam && !out->foam && d->world) {
        out->foam = box_render_texture_target_handle(d->world, b->key.foam);
    }

    out->wrap = b->key.wrap;
}

bool box_draws_range(
    const ecs_world_t *world,
    ecs_entity_t e,
    float *out)
{
    const DrawRange *range = NULL;
    ecs_entity_t cur = e;
    out[0] = out[1] = out[2] = 0.0f;
    while (cur && !range) {
        range = ecs_get(world, cur, DrawRange);
        cur = ecs_get_parent(world, cur);
    }

    if (range) {
        out[0] = isfinite(range->near_) && range->near_ > 0.0f ? range->near_ : 0.0f;
        out[1] = isfinite(range->far_) && range->far_ > 0.0f ? range->far_ : 0.0f;
        out[2] = isfinite(range->fade) && range->fade > 0.0f ? range->fade : 0.0f;
        if (out[0] == 0.0f && out[1] == 0.0f) {
            out[2] = 0.0f;
        }
    }

    return out[0] > 0.0f || out[1] > 0.0f;
}
