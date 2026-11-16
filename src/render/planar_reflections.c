/**
 * @file render/planar_reflections.c
 * @brief Gives every lit perspective view the PlanarReflections of its scene it renders a
 * mirrored image of, for the planar reflection pass of the backend (gpu_planar.c).
 *
 * A view of a scene with Scene.planar_reflections takes the planes of its scene that cross its
 * frustum with the camera on their front side, the nearest BOX_PLANAR_REFLECTION_MAX; a record
 * holds the mirrored camera and the instance of the shape of the plane, which samples the image.
 */

#include "render.h"
#include <math.h>

/** Aspect ratio the frustum of a camera that follows its surface is assumed to have at most
 * when its planes are picked, so a plane it may see is never left out */
#define BOX_PLANAR_SEES_ASPECT (4.0f)

/** Smallest PlanarReflection.resolution */
#define BOX_PLANAR_RESOLUTION_MIN (0.125f)

/** A PlanarReflection of this frame */
typedef struct box_planar_entry_t {
    ecs_entity_t entity;    /**< Entity with the PlanarReflection */
    ecs_entity_t scene;     /**< Scene root of the entity, 0 for the default scene */
    box_render_planar_plane_t plane; /**< Its plane */
    float clip_offset;      /**< PlanarReflection.clip_offset */
    float resolution;       /**< PlanarReflection.resolution, clamped */
    float distortion;       /**< PlanarReflection.distortion, 0 when not finite */
} box_planar_entry_t;

/** The state of the planar reflection extract */
typedef struct box_render_planar_state_t {
    ecs_query_t *query;     /**< Entities with PlanarReflection, their transform and scene */
    ecs_vec_t entries;      /**< box_planar_entry_t, the planes of this frame */
    ecs_vec_t planes;       /**< box_render_planar_plane_t, scratch: the planes of a scene */
    ecs_vec_t indices;      /**< int32_t, scratch: the entry of each of those planes */
} box_render_planar_state_t;

void box_render_planar_matrix(
    const float *plane,
    mat4 out)
{
    int32_t i, j;
    glm_mat4_identity(out);
    for (j = 0; j < 3; j ++) {
        for (i = 0; i < 3; i ++) {
            out[j][i] -= 2.0f * plane[i] * plane[j];
        }

        out[3][j] = -2.0f * plane[3] * plane[j];
    }
}

bool box_render_planar_oblique(
    mat4 proj,
    const float *plane,
    mat4 out)
{
    mat4 inv, p;
    float most = 0.0f;
    bool result = false;
    int32_t i, j;
    glm_mat4_copy(proj, p);
    glm_mat4_inv(p, inv);
    for (i = 0; i < 8; i ++) {
        vec4 corner = {
            (i & 1) ? 1.0f : -1.0f,
            (i & 2) ? 1.0f : -1.0f,
            (i & 4) ? 1.0f : 0.0f,
            1.0f
        };
        vec4 q;
        glm_mat4_mulv(inv, corner, q);
        float along = plane[0] * q[0] + plane[1] * q[1] + plane[2] * q[2] + plane[3] * q[3];
        if (i == 0 || along > most) {
            most = along;
        }
    }

    glm_mat4_copy(p, out);
    if (!(most > 1e-12f) || !isfinite(most)) {
        goto done;
    }

    float a = 1.0f / most;
    for (j = 0; j < 4; j ++) {
        out[j][2] = p[j][3] - a * plane[j];
    }

    result = true;
done:
    return result;
}

/** Check whether an unbounded plane crosses the frustum of a perspective camera that is on its
 * front side: whether a corner ray of the frustum reaches the back side before the far plane.
 *
 * @param v The view
 * @param plane The plane
 * @param front Signed distance of the camera from the plane, above 0
 * @return Whether the frustum reaches the back side of the plane
 */
static bool box_planar_crosses(
    const box_render_view_t *v,
    const float *plane,
    float front)
{
    const Camera *c = &v->camera_data;
    float aspect = c->aspect_ratio > 0 ? c->aspect_ratio : BOX_PLANAR_SEES_ASPECT;
    float h = tanf(glm_rad(c->fov) * 0.5f), w = h * aspect;
    bool infinite = !(c->far_ > c->near_), result = false;
    vec3 axes[3];
    int32_t k;
    for (k = 0; k < 3; k ++) {
        glm_vec3_copy(ECS_CONST_CAST(float*, v->camera_transform[k]), axes[k]);
        if (glm_vec3_norm(axes[k]) > 1e-12f) {
            glm_vec3_normalize(axes[k]);
        }
    }

    for (k = 0; k < 4 && !result; k ++) {
        vec3 dir;
        glm_vec3_scale(axes[0], (k & 1) ? w : -w, dir);
        glm_vec3_muladds(axes[1], (k & 2) ? h : -h, dir);
        glm_vec3_sub(dir, axes[2], dir);
        float along = plane[0] * dir[0] + plane[1] * dir[1] + plane[2] * dir[2];
        result = along < 0 && (infinite || front + c->far_ * along <= 0);
    }

    return result;
}

int32_t box_render_planar_select(
    const box_render_view_t *v,
    const box_render_planar_plane_t *planes,
    int32_t count,
    int32_t *out)
{
    const Camera *c = &v->camera_data;
    const float *eye = v->camera_transform[3];
    float distance[BOX_PLANAR_REFLECTION_MAX];
    int32_t result = 0, i, k;
    if (c->ortho || !box_camera_has_depth(c)) {
        goto done;
    }

    for (i = 0; i < count; i ++) {
        const float *p = planes[i].plane;
        float front = p[0] * eye[0] + p[1] * eye[1] + p[2] * eye[2] + p[3];
        if (!(front > 0) || !box_planar_crosses(v, p, front)) {
            continue;
        }

        for (k = result; k > 0 && distance[k - 1] > front; k --) {
            if (k < BOX_PLANAR_REFLECTION_MAX) {
                distance[k] = distance[k - 1];
                out[k] = out[k - 1];
            }
        }

        if (k < BOX_PLANAR_REFLECTION_MAX) {
            distance[k] = front;
            out[k] = i;
            if (result < BOX_PLANAR_REFLECTION_MAX) {
                result ++;
            }
        }
    }

done:
    return result;
}

/** Collect the PlanarReflection of every entity with its plane and scene.
 *
 * @param world The world
 * @param l The state
 */
static void box_planar_collect(
    ecs_world_t *world,
    box_render_planar_state_t *l)
{
    ecs_vec_clear(&l->entries);
    ecs_iter_t it = ecs_query_iter(world, l->query);
    while (ecs_query_next(&it)) {
        const PlanarReflection *planars = ecs_field(&it, PlanarReflection, 0);
        const WorldTransform *transforms = ecs_field(&it, WorldTransform, 1);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            vec3 normal;
            WorldTransform buf;
            const WorldTransform *t = box_transform_world_field(world, &it, transforms, i, &buf);
            glm_vec3_copy(ECS_CONST_CAST(float*, t->m[1]), normal);
            if (!(glm_vec3_norm(normal) > 1e-12f)) {
                continue;
            }

            glm_vec3_normalize(normal);
            box_planar_entry_t *e = ecs_vec_append_t(NULL, &l->entries, box_planar_entry_t);
            e->entity = it.entities[i];
            e->scene = box_render_scene_field(&it, 2, i);
            glm_vec3_copy(normal, e->plane.plane);
            e->plane.plane[3] = -glm_vec3_dot(normal,
                ECS_CONST_CAST(float*, t->m[3]));
            e->clip_offset = planars[i].clip_offset;
            e->resolution = glm_clamp(planars[i].resolution, BOX_PLANAR_RESOLUTION_MIN, 1.0f);
            if (!isfinite(e->resolution)) {
                e->resolution = 0.5f;
            }

            e->distortion = isfinite(planars[i].distortion) ? planars[i].distortion : 0.0f;
        }
    }
}

/** Write the record of a plane a view renders.
 *
 * @param world The world
 * @param v The view
 * @param e The plane
 * @param slot Slot of the plane in the view
 * @param out Receives the record
 */
static void box_planar_record(
    ecs_world_t *world,
    const box_render_view_t *v,
    const box_planar_entry_t *e,
    int32_t slot,
    box_render_planar_t *out)
{
    vec4 eye;
    ecs_os_memset_t(out, 0, box_render_planar_t);
    out->entity = e->entity;
    out->slot = slot;
    glm_vec4_copy(ECS_CONST_CAST(float*, e->plane.plane), out->plane);
    glm_vec4_copy(out->plane, out->clip);
    out->clip[3] += e->clip_offset;
    box_render_planar_matrix(out->plane, out->reflection);
    glm_mat4_mul(ECS_CONST_CAST(vec4*, v->view_matrix), out->reflection, out->view_matrix);
    glm_vec4_copy(ECS_CONST_CAST(float*, v->camera_transform[3]), eye);
    eye[3] = 1.0f;
    glm_mat4_mulv(out->reflection, eye, eye);
    glm_vec3_copy(eye, out->camera);
    out->resolution = e->resolution;
    out->distortion = e->distortion;
    if (!box_render_draws_instance(world, e->entity, &out->exclude_page, &out->exclude_slot) ||
        !box_render_draws_instance_transform(world, e->entity, out->owner))
    {
        out->exclude_page = 0;
        out->exclude_slot = 0;
        ecs_os_memset(out->owner, 0, ECS_SIZEOF(out->owner));
    }
}

/** Give every lit perspective view of a scene with Scene.planar_reflections the planes of the
 * scene of its camera it renders.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractPlanar(
    ecs_iter_t *it)
{
    box_render_planar_state_t *l = it->ctx;
    ecs_world_t *world = it->world;
    box_render_packet_t *p = box_render_packet(world);
    int32_t i, j, count = ecs_vec_count(&p->views);
    box_planar_collect(world, l);
    const box_planar_entry_t *entries = ecs_vec_first_t(&l->entries, box_planar_entry_t);
    int32_t entry_count = ecs_vec_count(&l->entries);
    for (i = 0; i < count; i ++) {
        box_render_view_t *v = ecs_vec_get_t(&p->views, box_render_view_t, i);
        const View *view = v->view && ecs_is_alive(world, v->view)
            ? ecs_get(world, v->view, View)
            : NULL;
        int32_t picked[BOX_PLANAR_REFLECTION_MAX], n;
        v->planar_first = ecs_vec_count(&p->planar_reflections);
        v->planar_count = 0;
        if (!entry_count || !view || !view->lighting.enabled || v->camera_data.ortho ||
            !(v->scene_features & BOX_RENDER_FEATURE_PLANAR) ||
            !box_camera_has_depth(&v->camera_data))
        {
            continue;
        }

        ecs_vec_clear(&l->planes);
        ecs_vec_clear(&l->indices);
        for (j = 0; j < entry_count; j ++) {
            if (entries[j].scene == v->scene) {
                *ecs_vec_append_t(NULL, &l->planes, box_render_planar_plane_t) =
                    entries[j].plane;
                *ecs_vec_append_t(NULL, &l->indices, int32_t) = j;
            }
        }

        n = box_render_planar_select(v, ecs_vec_first(&l->planes), ecs_vec_count(&l->planes),
            picked);
        for (j = 0; j < n; j ++) {
            int32_t entry = *ecs_vec_get_t(&l->indices, int32_t, picked[j]);
            box_planar_record(world, v, &entries[entry], j,
                ecs_vec_append_t(NULL, &p->planar_reflections, box_render_planar_t));
        }

        v->planar_count = n;
    }
}

/** Free the state of the planar reflection extract.
 *
 * @param ctx The state
 */
static void box_planar_free(
    void *ctx)
{
    box_render_planar_state_t *l = ctx;
    ecs_vec_fini_t(NULL, &l->entries, box_planar_entry_t);
    ecs_vec_fini_t(NULL, &l->planes, box_render_planar_plane_t);
    ecs_vec_fini_t(NULL, &l->indices, int32_t);
    ecs_os_free(l);
}

void box_render_planar_init(
    ecs_world_t *world)
{
    box_render_planar_state_t *l = ecs_os_calloc_t(box_render_planar_state_t);
    ecs_vec_init_t(NULL, &l->entries, box_planar_entry_t, 0);
    ecs_vec_init_t(NULL, &l->planes, box_render_planar_plane_t, 0);
    ecs_vec_init_t(NULL, &l->indices, int32_t, 0);
    l->query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "PlanarReflections" }),
        .terms = {
            { .id = ecs_id(PlanarReflection), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn },
            {
                .id = ecs_id(Scene),
                .src.id = EcsSelf | EcsUp,
                .trav = EcsChildOf,
                .inout = EcsInOutNone,
                .oper = EcsOptional
            }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "extract.PlanarReflections" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractPlanar,
        .ctx = l,
        .ctx_free = box_planar_free
    });
}
