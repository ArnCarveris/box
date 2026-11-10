/**
 * @file render/batch_material.c
 * @brief Shading, material, transform and surface records of batch instances.
 *
 * Converts display encoded colors to linear light, applies tints and packs the material, mesh
 * shading, transform and surface records that batches upload per instance.
 */

#include "draw.h"
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
    const Shape2D *shape2d,
    uint32_t features)
{
    return box_draws_cutout(cutoff, shape2d) && (features & BOX_RENDER_FEATURE_ALPHA_CUTOUT);
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

void box_draws_material_pack(
    box_render_material_t *out,
    const box_draw_material_t *material)
{
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
        material->mapping->mode == MappingWorld))
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
