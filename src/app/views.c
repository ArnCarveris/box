#include "app.h"
#include "../render/render.h"
#include "../spatial/spatial.h"
#include "../geometry/mesh_cache.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum {
    BOX_DESIGN_ORTHO_COUNT = 6,
    BOX_DESIGN_VIEW_COUNT = 8
};

static const char *box_design_names[] = {
    "top", "bottom", "left", "right", "front", "back",
    "perspective", "perspective-back"
};

typedef struct box_design_t {
    ecs_entity_t scene;
    ecs_entity_t surface;
    ecs_entity_t camera[BOX_DESIGN_VIEW_COUNT];
    ecs_entity_t view[BOX_DESIGN_VIEW_COUNT];
    ecs_entity_t target[BOX_DESIGN_VIEW_COUNT];
    ecs_entity_t tile[BOX_DESIGN_VIEW_COUNT];
    ecs_entity_t light[BOX_DESIGN_VIEW_COUNT];
    ecs_entity_t label[BOX_DESIGN_VIEW_COUNT];
    ecs_entity_t caption[BOX_DESIGN_VIEW_COUNT];
    ecs_entity_t overview;
    char *capture;
} box_design_t;

static void box_design_free(
    void *ptr)
{
    box_design_t *design = ptr;
    ecs_os_free(design->capture);
    ecs_os_free(design);
}

static ecs_entity_t box_app_child(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return ecs_entity(world, {
        .name = name,
        .parent = parent
    });
}

static ecs_entity_t box_app_surface(
    ecs_world_t *world)
{
    ecs_entity_t result = 0;
    ecs_iter_t it = ecs_each(world, Surface);
    while (ecs_each_next(&it)) {
        const Surface *s = ecs_field(&it, Surface, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            if (!s[i].texture) {
                result = it.entities[i];
                ecs_iter_fini(&it);
                goto done;
            }
        }
    }

    const App *app = ecs_singleton_get(world, App);
    int32_t width = app->surface_width;
    int32_t height = app->surface_height;
    result = ecs_new(world);
    Surface *s = ecs_ensure(world, result, Surface);
    if (width > 0 && height > 0) {
        s->width = width;
        s->height = height;
    }

    s->vsync = true;
    ecs_modified(world, result, Surface);
done:
    return result;
}

static ecs_entity_t box_app_view(
    ecs_world_t *world,
    ecs_entity_t surface,
    ecs_entity_t camera)
{
    const Surface *s = ecs_get(world, surface, Surface);
    ecs_entity_t root = s->view_root;
    ecs_entity_t fallback = 0;
    ecs_iter_t it = ecs_each(world, View);
    while (ecs_each_next(&it)) {
        const View *v = ecs_field(&it, View, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            if (camera && v[i].camera == camera) {
                fallback = e;
                ecs_iter_fini(&it);
                goto done;
            }

            ecs_entity_t parent = ecs_get_parent(world, e);
            bool visible = parent == root;
            if (!root && parent) {
                visible = true;
                ecs_iter_t surfaces = ecs_each(world, Surface);
                while (ecs_each_next(&surfaces)) {
                    const Surface *targets = ecs_field(&surfaces, Surface, 0);
                    for (int32_t t = 0; t < surfaces.count; t ++) {
                        visible &= targets[t].view_root != parent;
                    }
                }
            }

            if (!fallback && visible && ecs_is_alive(world, v[i].camera) &&
                ecs_has(world, v[i].camera, Camera))
            {
                fallback = e;
            }
        }
    }

done:
    return fallback;
}

static void box_design_pose(
    ecs_world_t *world,
    ecs_entity_t entity,
    mat4 matrix)
{
    WorldTransform transform;
    glm_mat4_copy(matrix, transform.m);
    ecs_set_ptr(world, entity, WorldTransform, &transform);
}

static void box_design_fit(
    ecs_world_t *world,
    box_design_t *design,
    int32_t index,
    vec3 min,
    vec3 max,
    float aspect)
{
    vec3 directions[] = {
        {0, 1, 0}, {0, -1, 0}, {-1, 0, 0}, {1, 0, 0},
        {0, 0, 1}, {0, 0, -1}, {1, 0.75f, 1}, {-1, 0.75f, -1}
    };
    vec3 up = {0, 1, 0}, center, extent, back, right;
    if (index < 2) {
        glm_vec3_copy((vec3){0, 0, index == 0 ? -1 : 1}, up);
    }

    glm_vec3_add(min, max, center);
    glm_vec3_scale(center, 0.5f, center);
    glm_vec3_sub(max, min, extent);
    glm_vec3_normalize_to(directions[index], back);
    glm_vec3_crossn(up, back, right);
    glm_vec3_crossn(back, right, up);

    float padding = fmaxf(glm_vec3_norm(extent) * 0.025f, 0.001f);
    float half_width = 0, half_height = 0, half_depth = 0;
    float distance = padding;
    float tangent = tanf(glm_rad(BOX_CAMERA_FOV) * 0.5f);
    for (int32_t i = 0; i < 8; i ++) {
        vec3 point = {
            ((i & 1) ? max[0] : min[0]) - center[0],
            ((i & 2) ? max[1] : min[1]) - center[1],
            ((i & 4) ? max[2] : min[2]) - center[2]
        };
        float x = fabsf(glm_vec3_dot(point, right));
        float y = fabsf(glm_vec3_dot(point, up));
        float z = glm_vec3_dot(point, back);
        half_width = fmaxf(half_width, x);
        half_height = fmaxf(half_height, y);
        half_depth = fmaxf(half_depth, fabsf(z));
        distance = fmaxf(distance, z + fmaxf(
            (x + padding) / (tangent * aspect), (y + padding) / tangent));
    }

    if (index < BOX_DESIGN_ORTHO_COUNT) {
        distance = half_depth + padding * 2;
    }

    distance = fmaxf(distance, half_depth + padding * 2);

    Camera *camera = ecs_get_mut(world, design->camera[index], Camera);
    camera->aspect_ratio = aspect;
    camera->ortho_height = 2 * fmaxf(half_height + padding,
        (half_width + padding) / aspect);
    camera->near_ = fmaxf(distance - half_depth - padding, padding * 0.1f);
    camera->far_ = distance + half_depth + padding;
    float far = camera->far_;
    ecs_modified(world, design->camera[index], Camera);

    mat4 matrix = GLM_MAT4_IDENTITY_INIT;
    glm_vec3_copy(right, matrix[0]);
    glm_vec3_copy(up, matrix[1]);
    glm_vec3_copy(back, matrix[2]);
    glm_vec3_scale(back, distance, matrix[3]);
    glm_vec3_add(matrix[3], center, matrix[3]);
    box_design_pose(world, design->camera[index], matrix);

    vec3 sun;
    for (int32_t i = 0; i < 3; i ++) {
        sun[i] = back[i] + 0.35f * right[i] + 0.6f * up[i];
    }

    glm_vec3_normalize(sun);
    glm_mat4_identity(matrix);
    glm_vec3_negate_to(sun, matrix[2]);
    glm_vec3_crossn(up, matrix[2], matrix[0]);
    glm_vec3_crossn(matrix[2], matrix[0], matrix[1]);
    box_design_pose(world, design->light[index], matrix);

    View *view = ecs_get_mut(world, design->view[index], View);
    view->shadows.max_distance = far;
    ecs_modified(world, design->view[index], View);
}

static char *box_design_filename(
    const char *path,
    const char *name)
{
    char *result = NULL;
    if (!path || !path[0]) {
        goto done;
    }

    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    int length = (int)((dot && (!slash || dot > slash)) ? dot - path : (ptrdiff_t)strlen(path));
    result = flecs_asprintf("%.*s-%s.png", length, path, name);
done:
    return result;
}

static void BoxDesignUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    box_design_t *design = it->ctx;
    vec3 min = {FLT_MAX, FLT_MAX, FLT_MAX};
    vec3 max = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    bool found = false;
    ecs_iter_t shapes = ecs_each(world, ShapeMesh);
    while (ecs_each_next(&shapes)) {
        for (int32_t i = 0; i < shapes.count; i ++) {
            vec3 lo, hi;
            ecs_entity_t scene;
            if (box_spatial_bounds(world, shapes.entities[i], &scene, lo, hi) &&
                scene == design->scene)
            {
                glm_vec3_minv(min, lo, min);
                glm_vec3_maxv(max, hi, max);
                found = true;
            }
        }
    }

    if (!found) {
        glm_vec3_copy((vec3){-0.5f, -0.5f, -0.5f}, min);
        glm_vec3_copy((vec3){0.5f, 0.5f, 0.5f}, max);
    }

    const Surface *surface = ecs_get(world, design->surface, Surface);
    bool capture_changed = strcmp(surface->write_to_file ? surface->write_to_file : "",
        design->capture ? design->capture : "") != 0;
    if (capture_changed) {
        ecs_os_free(design->capture);
        design->capture = ecs_os_strdup(surface->write_to_file);
    }

    const SurfaceStatus *status = ecs_get(world, design->surface, SurfaceStatus);
    int32_t width = status && status->window ? status->point_width : surface->width;
    int32_t height = status && status->window ? status->point_height : surface->height;
    width = width > 1 ? width : 2;
    height = height > 0 ? height : 1;
    int32_t target_width = width / 2;
    Camera *overview = ecs_get_mut(world, design->overview, Camera);
    overview->ortho_height = (float)height;
    ecs_modified(world, design->overview, Camera);

    for (int32_t i = 0; i < BOX_DESIGN_VIEW_COUNT; i ++) {
        bool ortho = i < BOX_DESIGN_ORTHO_COUNT;
        float w = ortho ? width * 0.25f : width * 0.5f;
        float h = ortho ? height / 3.0f : height * 0.5f;
        int32_t row = ortho ? i / 2 : i - BOX_DESIGN_ORTHO_COUNT;
        float caption_height = fminf(16, h * 0.2f);
        float content_height = h - caption_height;
        int32_t target_height = (int32_t)(content_height * (ortho ? 2 : 1));
        target_height = target_height > 0 ? target_height : 1;
        float aspect = (float)target_width / target_height;
        Surface *target = ecs_get_mut(world, design->target[i], Surface);
        if (capture_changed) {
            ecs_os_free(target->write_to_file);
            target->write_to_file = box_design_filename(design->capture, box_design_names[i]);
        }

        if (target->width != target_width || target->height != target_height || capture_changed) {
            target->width = target_width;
            target->height = target_height;
            ecs_modified(world, design->target[i], Surface);
        }

        box_design_fit(world, design, i, min, max, aspect);
        const Rect *rect = ecs_get(world, design->tile[i], Rect);
        if (rect->width != w || rect->height != content_height) {
            ecs_set(world, design->tile[i], Rect, {w, content_height});
        }

        mat4 matrix = GLM_MAT4_IDENTITY_INIT;
        matrix[3][0] = ortho ? -width * 0.5f + w * ((i % 2) + 0.5f) : width * 0.25f;
        matrix[3][1] = height * 0.5f - h * (row + 0.5f);
        matrix[3][1] -= caption_height * 0.5f;
        box_design_pose(world, design->tile[i], matrix);
        ecs_set(world, design->caption[i], Rect, {w, caption_height});
        matrix[3][1] += h * 0.5f;
        matrix[3][2] = 0.1f;
        box_design_pose(world, design->caption[i], matrix);
        matrix[3][2] = 0.2f;
        if (ortho) {
            int32_t horizontal = i < 2 ? 0 : (i < 4 ? 2 : 0);
            int32_t vertical = i < 2 ? 2 : 1;
            const char *axes = "XYZ";
            char caption[128];
            if (found) {
                snprintf(caption, sizeof(caption), "%s (%c: %.4g, %c: %.4g)",
                    box_design_names[i], axes[horizontal],
                    max[horizontal] - min[horizontal], axes[vertical],
                    max[vertical] - min[vertical]);
            } else {
                snprintf(caption, sizeof(caption), "%s", box_design_names[i]);
            }

            caption[0] -= 'a' - 'A';
            Text *text = ecs_get_mut(world, design->label[i], Text);
            if (strcmp(text->value, caption)) {
                ecs_os_free(text->value);
                text->value = ecs_os_strdup(caption);
                ecs_modified(world, design->label[i], Text);
            }
        }

        const TextBounds *bounds = ecs_get(world, design->label[i], TextBounds);
        matrix[3][0] += -w * 0.5f + 5 + (bounds ? bounds->width * 0.5f : 0);
        box_design_pose(world, design->label[i], matrix);
    }
}

static void box_design_lighting(
    ecs_world_t *world,
    ecs_entity_t scene)
{
    ecs_id_t components[] = {
        ecs_id(DirectionalLight), ecs_id(PointLight), ecs_id(SpotLight),
        ecs_id(AmbientLight), ecs_id(LightBake), ecs_id(ReflectionBake),
        ecs_id(Atmosphere), ecs_id(TimeOfDay)
    };
    ecs_vec_t entities = {0};
    for (int32_t c = 0; c < 8; c ++) {
        ecs_iter_t it = ecs_each_id(world, components[c]);
        while (ecs_each_next(&it)) {
            for (int32_t i = 0; i < it.count; i ++) {
                if (box_render_scene_of(world, it.entities[i]) == scene) {
                    *ecs_vec_append_t(NULL, &entities, ecs_entity_t) = it.entities[i];
                }
            }
        }

        ecs_entity_t *values = ecs_vec_first(&entities);
        for (int32_t i = 0; i < ecs_vec_count(&entities); i ++) {
            ecs_remove_id(world, values[i], components[c]);
        }

        ecs_vec_clear(&entities);
    }

    ecs_vec_fini_t(NULL, &entities, ecs_entity_t);
    ecs_set(world, scene ? scene : ecs_id(AmbientLight), AmbientLight, {
        .sky = {255, 255, 255}, .ground = {255, 255, 255}, .intensity = 0.08f
    });
}

int box_app_views(
    ecs_world_t *world)
{
    int result = 0;
    const App *app = ecs_singleton_get(world, App);
    if (!app->design && !app->camera && !app->out) {
        goto done;
    }

    if (app->design && app->camera) {
        ecs_err("--design and --camera cannot be combined");
        result = -1;
        goto done;
    }

    ecs_entity_t selected = app->camera ? ecs_lookup(world, app->camera) : 0;
    if (app->camera && (!selected || !ecs_has(world, selected, Camera))) {
        ecs_err("camera '%s' does not name an existing Camera", app->camera);
        result = -1;
        goto done;
    }

    bool design_mode = app->design;
    ecs_entity_t surface = box_app_surface(world);
    if (app->out) {
        char cwd[4096];
        if (app->out[0] != '/' && !getcwd(cwd, sizeof(cwd))) {
            ecs_err("cannot resolve output path '%s'", app->out);
            result = -1;
            goto done;
        }

        char *path = app->out[0] == '/' ? ecs_os_strdup(app->out)
            : flecs_asprintf("%s/%s", cwd, app->out);
        const char *dot = strrchr(path, '.');
        const char *slash = strrchr(path, '/');
        if (!dot || (slash && dot < slash)) {
            char *png = flecs_asprintf("%s.png", path);
            ecs_os_free(path);
            path = png;
        }

        Surface *settings = ecs_get_mut(world, surface, Surface);
        ecs_os_free(settings->write_to_file);
        settings->write_to_file = path;
        ecs_modified(world, surface, Surface);
    }

    if (!design_mode && !selected) {
        goto done;
    }

    ecs_entity_t source = box_app_view(world, surface, selected);
    ecs_entity_t root = ecs_new(world);
    if (!design_mode) {
        ecs_entity_t view = box_app_child(world, root, "camera");
        if (source) {
            ecs_set_ptr(world, view, View, ecs_get(world, source, View));
        }

        View *v = ecs_ensure(world, view, View);
        v->camera = selected;
        ecs_modified(world, view, View);
        ecs_get_mut(world, surface, Surface)->view_root = root;
        ecs_modified(world, surface, Surface);
        goto done;
    }

    box_design_t *design = ecs_os_calloc_t(box_design_t);
    design->surface = surface;
    if (source) {
        design->scene = box_render_scene_of(world, ecs_get(world, source, View)->camera);
    } else {
        ecs_iter_t shapes = ecs_each(world, ShapeMesh);
        if (ecs_each_next(&shapes)) {
            design->scene = box_render_scene_of(world, shapes.entities[0]);
            ecs_iter_fini(&shapes);
        }
    }

    box_design_lighting(world, design->scene);
    ecs_entity_t cameras = box_app_child(world, design->scene, "DesignCameras");
    ecs_entity_t gallery = box_app_child(world, root, "overview");
    ecs_add(world, gallery, Scene);
    design->overview = box_app_child(world, gallery, "camera");
    ecs_set(world, design->overview, Position, {0, 0, 1});
    ecs_set(world, design->overview, Camera, {
        .ortho = true, .ortho_height = 720, .near_ = 0.1f, .far_ = 10
    });
    ecs_entity_t view = box_app_child(world, root, "view");
    ecs_set(world, view, View, {
        .camera = design->overview,
        .clear = {.enabled = true, .color = {16, 16, 16, 255}}
    });

    Surface settings = *ecs_get(world, surface, Surface);
    design->capture = ecs_os_strdup(settings.write_to_file);
    for (int32_t i = 0; i < BOX_DESIGN_VIEW_COUNT; i ++) {
        ecs_entity_t camera = box_app_child(world, cameras, box_design_names[i]);
        design->camera[i] = camera;
        ecs_add(world, camera, ManualTransform);
        ecs_set(world, camera, Camera, {
            .fov = BOX_CAMERA_FOV, .near_ = 0.1f, .far_ = 1000,
            .ortho = i < BOX_DESIGN_ORTHO_COUNT, .ortho_height = 10
        });
        ecs_entity_t target = ecs_new(world);
        ecs_entity_t texture = ecs_new(world);
        ecs_entity_t target_root = ecs_new(world);
        ecs_entity_t target_view = box_app_child(world, target_root, box_design_names[i]);
        design->target[i] = target;
        design->view[i] = target_view;
        ecs_entity_t light = box_app_child(world, target_root, "light");
        design->light[i] = light;
        ecs_add(world, light, Scene);
        ecs_add(world, light, ManualTransform);
        ecs_set(world, light, AmbientLight, {
            .sky = {255, 255, 255}, .ground = {255, 255, 255}, .intensity = 0.08f
        });
        ecs_set(world, light, DirectionalLight, {
            .intensity = 1, .bake = LightBakeModeRealtime
        });
        ecs_set(world, light, Rgb, {255, 255, 255});
        View *v = ecs_ensure(world, target_view, View);
        v->camera = camera;
        v->lighting = (LightSettings){.enabled = true, .scene = light};
        v->clear = (ClearSettings){.enabled = true, .color = {24, 24, 24, 255}};
        v->shadows.enabled = true;
        v->shadows.fade = 0;
        v->culling.min_dimension_px = 0;
        ecs_modified(world, target_view, View);
        char *file = box_design_filename(design->capture, box_design_names[i]);
        Surface target_settings = settings;
        target_settings.width = settings.width > 1 ? settings.width / 2 : 1;
        target_settings.texture = texture;
        target_settings.view_root = target_root;
        target_settings.write_to_file = file;
        ecs_set_ptr(world, target, Surface, &target_settings);
        ecs_os_free(file);
        ecs_entity_t tile = box_app_child(world, gallery, box_design_names[i]);
        design->tile[i] = tile;
        ecs_add(world, tile, ManualTransform);
        ecs_set(world, tile, Rect, {1, 1});
        ecs_set(world, tile, Image, {.texture = texture, .u1 = 1, .v1 = 1});
        ecs_set(world, tile, Border, {.width = 2, .color = {0, 0, 0, 255}});
        ecs_entity_t caption = ecs_new_w_pair(world, EcsChildOf, gallery);
        design->caption[i] = caption;
        ecs_add(world, caption, ManualTransform);
        ecs_set(world, caption, Rect, {1, 1});
        ecs_set(world, caption, Rgb, {0, 0, 0});
        ecs_entity_t label = ecs_new_w_pair(world, EcsChildOf, gallery);
        design->label[i] = label;
        ecs_add(world, label, ManualTransform);
        Text *text = ecs_ensure(world, label, Text);
        text->value = ecs_os_strdup(i == BOX_DESIGN_VIEW_COUNT - 1
            ? "perspective (back)" : box_design_names[i]);
        text->value[0] -= 'a' - 'A';
        text->size = 9;
        text->align = AlignLeft;
        ecs_modified(world, label, Text);
        ecs_set(world, label, Rgb, {255, 255, 255});
    }

    ecs_get_mut(world, surface, Surface)->view_root = root;
    ecs_modified(world, surface, Surface);

    ecs_entity_t phase = ecs_entity(world, {.name = "box.app.DesignFit"});
    ecs_add_id(world, phase, EcsPhase);
    ecs_add_pair(world, phase, EcsDependsOn, EcsOnStore);
    ecs_add_pair(world, RenderExtract, EcsDependsOn, phase);
    ecs_system(world, {
        .entity = ecs_entity(world, {.name = "box.app.DesignUpdate"}),
        .phase = phase,
        .immediate = true,
        .run = BoxDesignUpdate,
        .ctx = design,
        .ctx_free = box_design_free
    });
done:
    return result;
}
