/**
 * @file render/tiling.c
 * @brief Derives the uv transform of an instance from its material and world size.
 */

#include "render_internal.h"

/** Projection a tiling shape selects.
 *
 * TilingMeters keeps the mesh uv as the base of the transform, the other three
 * shapes project the local position in the fragment stage.
 *
 * @param shape The shape
 * @return One of the BOX_TILE constants
 */
static uint32_t box_render_projection(
    TilingShape shape)
{
    uint32_t result = BOX_TILE_MESH;
    if (shape == TilingBox) {
        result = BOX_TILE_BOX;
    } else if (shape == TilingCylinder) {
        result = BOX_TILE_CYLINDER;
    } else if (shape == TilingRadial) {
        result = BOX_TILE_RADIAL;
    }

    return result;
}

/** Size of an instance in world meters along each of its local axes.
 *
 * An axis spans the mesh bounds of that axis times the length of the matching
 * column of the instance matrix, which already holds the shape scale. The
 * vertex stage derives the local position of a fragment from the same lengths.
 *
 * @param mesh The render mesh
 * @param m The instance matrix, the world transform times the shape scale
 * @param out Three floats receiving the size
 */
static void box_render_world_size(
    const RenderMesh *mesh,
    const mat4 *m,
    float *out)
{
    vec3 extent = vec3_sub(mesh->bounds_max, mesh->bounds_min);
    float span[3] = { extent.x, extent.y, extent.z };
    int32_t axis;
    for (axis = 0; axis < 3; axis ++) {
        vec3 col = vec3_new(m->v[axis * 4], m->v[axis * 4 + 1], m->v[axis * 4 + 2]);

        out[axis] = vec3_length(col) * span[axis];
    }
}

/** Map the uv transform of an instance into the texture region of its mesh.
 *
 * The transform is applied first and the region last, so tiling repeats
 * inside the region. A region without extent leaves the transform alone.
 *
 * @param mesh The render mesh
 * @param uv The uv scale x, scale y, offset x and offset y, updated
 */
static void box_render_fill_region(
    const RenderMesh *mesh,
    float *uv)
{
    const vec4 *r = &mesh->uv_region;
    float span_u = r->z - r->x;
    float span_v = r->w - r->y;
    if (span_u <= 0.0f || span_v <= 0.0f) {
        goto done;
    }

    uv[2] = r->x + span_u * uv[2];
    uv[3] = r->y + span_v * uv[3];
    uv[0] *= span_u;
    uv[1] *= span_v;
done:
    return;
}

void box_render_fill_uv(
    ecs_world_t *world,
    ecs_entity_t e,
    const RenderMesh *mesh,
    const mat4 *m,
    bool tiled,
    box_instance_t *out)
{
    out->uv_transform[0] = 1.0f;
    out->uv_transform[1] = 1.0f;
    out->uv_transform[2] = 0.0f;
    out->uv_transform[3] = 0.0f;
    out->uv_mode = 0;
    if (!tiled) {
        goto done;
    }

    const TextureTransform *xform = ecs_get(world, e, TextureTransform);
    const TextureTiling *tiling = ecs_get(world, e, TextureTiling);
    const TextureWrap *wrap = ecs_get(world, e, TextureWrap);
    float base[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
    uint32_t projection = BOX_TILE_MESH;
    uint32_t mode = BOX_WRAP_REPEAT;

    if (xform) {
        base[0] = xform->scale_x;
        base[1] = xform->scale_y;
        base[2] = xform->offset_x;
        base[3] = xform->offset_y;
    }

    if (tiling) {
        float size[3];
        float meters[2] = { tiling->meters_x, tiling->meters_y };

        box_render_world_size(mesh, m, size);
        projection = box_render_projection(tiling->shape);
        box_tiling_transform(projection, size, meters, tiling->fit, tiling->allow_partial,
            tiling->min_tiles, base, out->uv_transform);
    } else {
        memcpy(out->uv_transform, base, sizeof(base));
    }

    if (wrap) {
        mode = wrap[0] == WrapClamp ? BOX_WRAP_CLAMP : mode;
        mode = wrap[0] == WrapMirror ? BOX_WRAP_MIRROR : mode;
    }

    out->uv_mode = box_tiling_mode(projection, mode);
done:
    box_render_fill_region(mesh, out->uv_transform);
}
