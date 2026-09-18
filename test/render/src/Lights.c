/**
 * @file Lights.c
 * @brief Tests for point and spot light extraction, frustum culling and the cluster grid.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void Lights_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
}

void Lights_teardown(void)
{
    box_packet_fini(&packet);
    ecs_fini(w);
}

void Lights_point_light_extracted(void)
{
    // lighting/clustered_forward
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {1, 2, 8});
    ecs_set(w, e, PointLight, {.intensity = 5, .range = 20});
    box_render_extract(w, &packet);
    test_int(packet.lights.count, 1);
    test_int(packet.view.light_count, 1);
    box_light_t *light = box_vec_at(&packet.lights, 0);
    test_flt(light->position[0], 1);
    test_flt(light->position[1], 2);
    test_flt(light->position[2], 8);
    test_flt(light->intensity, 5);
    test_flt(light->range, 20);
    test_int(light->type, BOX_LIGHT_POINT);
    test_flt(light->color[0], 1);
    test_flt(light->color[1], 1);
    test_flt(light->color[2], 1);
}

void Lights_point_light_color_is_linear(void)
{
    // lighting/clustered_forward
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 5});
    ecs_set(w, e, PointLight, {.intensity = 2, .range = 10});
    ecs_set(w, e, Rgb, {255, 128, 0});
    box_render_extract(w, &packet);
    test_int(packet.lights.count, 1);
    box_light_t *light = box_vec_at(&packet.lights, 0);
    test_flt(light->color[0], 1);
    test_assert(fabsf(light->color[1] - box_srgb_to_linear(128)) < 1e-6f);
    test_assert(light->color[1] < 0.3f);
    test_flt(light->color[2], 0);
    test_flt(light->intensity, 2);
}

void Lights_spot_light_direction_and_cone(void)
{
    // lighting/clustered_forward
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 3, 6});
    ecs_set(w, e, Rotation, {3.14159265f / 2, 0, 0});
    ecs_set(w, e, SpotLight, {
        .intensity = 4, .range = 12, .inner_angle = 20, .outer_angle = 40});
    box_render_extract(w, &packet);
    test_int(packet.lights.count, 1);
    box_light_t *light = box_vec_at(&packet.lights, 0);
    test_int(light->type, BOX_LIGHT_SPOT);
    test_assert(fabsf(light->direction[1] + 1) < 1e-4f);
    test_assert(fabsf(light->cos_inner - cosf(20.0f * 3.14159265f / 180.0f)) < 1e-6f);
    test_assert(fabsf(light->cos_outer - cosf(40.0f * 3.14159265f / 180.0f)) < 1e-6f);
    test_assert(light->cos_inner > light->cos_outer);
}

void Lights_point_light_is_not_a_spot_cone(void)
{
    // lighting/clustered_forward
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 5});
    ecs_set(w, e, PointLight, {.intensity = 1, .range = 10});
    box_render_extract(w, &packet);
    box_light_t *light = box_vec_at(&packet.lights, 0);
    test_flt(light->cos_inner, 1);
    test_flt(light->cos_outer, -1);
}

void Lights_light_behind_camera_culled(void)
{
    // lighting/clustered_forward
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, -20});
    ecs_set(w, e, PointLight, {.intensity = 1, .range = 2});
    box_render_extract(w, &packet);
    test_int(packet.lights.count, 0);
    test_int(packet.view.light_count, 0);
}

void Lights_light_far_to_the_side_culled(void)
{
    // lighting/clustered_forward
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {200, 0, 10});
    ecs_set(w, e, PointLight, {.intensity = 1, .range = 5});
    box_render_extract(w, &packet);
    test_int(packet.lights.count, 0);
}

void Lights_light_beyond_far_plane_culled(void)
{
    // lighting/clustered_forward
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 5000});
    ecs_set(w, e, PointLight, {.intensity = 1, .range = 5});
    box_render_extract(w, &packet);
    test_int(packet.lights.count, 0);
}

void Lights_light_inside_frustum_kept(void)
{
    // lighting/clustered_forward
    ecs_entity_t inside = ecs_new(w);
    ecs_set(w, inside, Position, {0, 0, 10});
    ecs_set(w, inside, PointLight, {.intensity = 1, .range = 5});
    ecs_entity_t behind = ecs_new(w);
    ecs_set(w, behind, Position, {0, 0, -20});
    ecs_set(w, behind, PointLight, {.intensity = 1, .range = 2});
    ecs_entity_t aside = ecs_new(w);
    ecs_set(w, aside, Position, {200, 0, 10});
    ecs_set(w, aside, PointLight, {.intensity = 1, .range = 5});
    box_render_extract(w, &packet);
    test_int(packet.lights.count, 1);
    box_light_t *light = box_vec_at(&packet.lights, 0);
    test_flt(light->position[2], 10);
}

void Lights_range_reaches_into_the_frustum(void)
{
    // lighting/clustered_forward
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, -5});
    ecs_set(w, e, PointLight, {.intensity = 1, .range = 40});
    box_render_extract(w, &packet);
    test_int(packet.lights.count, 1);
}

void Lights_directional_light_stays_the_sun(void)
{
    // lighting/clustered_forward
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Rotation, {3.14159265f / 2, 0, 0});
    ecs_set(w, e, DirectionalLight, {.intensity = 3});
    box_render_extract(w, &packet);
    test_int(packet.lights.count, 0);
    test_flt(packet.view.sun_intensity, 3);
}

void Lights_settings_copied_into_view(void)
{
    // lighting/clustered_forward
    box_render_extract(w, &packet);
    test_int(packet.view.cluster.x, BOX_CLUSTER_X);
    test_int(packet.view.cluster.y, BOX_CLUSTER_Y);
    test_int(packet.view.cluster.z, BOX_CLUSTER_Z);
    test_int(packet.view.cluster.max_lights, BOX_CLUSTER_MAX_LIGHTS);
    test_flt(packet.view.cluster.near_, 0.1f);
    test_flt(packet.view.cluster.far_, 1000);
}

void Lights_settings_override_copied_into_view(void)
{
    // lighting/clustered_forward
    ecs_singleton_set(w, ClusterSettings, {.x = 8, .y = 4, .z = 12, .max_lights = 32});
    box_render_extract(w, &packet);
    test_int(packet.view.cluster.x, 8);
    test_int(packet.view.cluster.y, 4);
    test_int(packet.view.cluster.z, 12);
    test_int(packet.view.cluster.max_lights, 32);
}

void Lights_camera_range_is_the_cluster_depth(void)
{
    // lighting/clustered_forward
    ecs_entity_t cam = box_render_active_camera(w);
    ecs_set(w, cam, Camera, {.fov = 60, .near_ = 0.5f, .far_ = 250});
    box_render_extract(w, &packet);
    test_flt(packet.view.cluster.near_, 0.5f);
    test_flt(packet.view.cluster.far_, 250);
}

void Lights_cluster_index_of_view_center(void)
{
    // lighting/clustered_forward
    box_render_extract(w, &packet);
    box_cluster_t *c = &packet.view.cluster;
    float center[3] = {0, 0, 12};
    uint32_t slice = box_cluster_slice(c, 12);
    uint32_t expect = 8 + 4 * c->x + slice * c->x * c->y;
    test_int(box_cluster_index(c, &packet.view, center), expect);
    test_int(slice, 12);
}

void Lights_cluster_index_follows_screen_position(void)
{
    // lighting/clustered_forward
    box_render_extract(w, &packet);
    box_cluster_t *c = &packet.view.cluster;
    float left[3] = {-4, 0, 10};
    float right[3] = {4, 0, 10};
    float up[3] = {0, 3, 10};
    uint32_t center_x = box_cluster_index(c, &packet.view, (float[3]){0, 0, 10}) % c->x;
    test_assert(box_cluster_index(c, &packet.view, left) % c->x < center_x);
    test_assert(box_cluster_index(c, &packet.view, right) % c->x > center_x);
    test_assert(box_cluster_index(c, &packet.view, up) / c->x % c->y < 4);
}

void Lights_cluster_slices_grow_with_depth(void)
{
    // lighting/clustered_forward
    box_render_extract(w, &packet);
    box_cluster_t *c = &packet.view.cluster;
    test_int(box_cluster_slice(c, 0.05f), 0);
    test_int(box_cluster_slice(c, 5000), c->z - 1);

    float ratio = c->far_ / c->near_;
    uint32_t k;
    for (k = 0; k < c->z; k ++) {
        float depth = c->near_ * powf(ratio, ((float)k + 0.5f) / (float)c->z);
        test_int(box_cluster_slice(c, depth), k);
    }

    uint32_t near_step = box_cluster_slice(c, 2) - box_cluster_slice(c, 1);
    uint32_t far_step = box_cluster_slice(c, 101) - box_cluster_slice(c, 100);
    test_assert(near_step > far_step);
}

void Lights_lights_land_in_expected_clusters(void)
{
    // lighting/clustered_forward
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {0, 0, 10});
    ecs_set(w, a, PointLight, {.intensity = 1, .range = 1});
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Position, {2, 0, 4});
    ecs_set(w, b, PointLight, {.intensity = 1, .range = 1});
    box_render_extract(w, &packet);
    test_int(packet.lights.count, 2);
    box_cluster_t *c = &packet.view.cluster;
    box_light_t *first = box_vec_at(&packet.lights, 0);
    box_light_t *second = box_vec_at(&packet.lights, 1);
    uint32_t ia = box_cluster_index(c, &packet.view, first->position);
    uint32_t ib = box_cluster_index(c, &packet.view, second->position);
    test_int(ia, 8 + 4 * c->x + box_cluster_slice(c, 10) * c->x * c->y);
    test_assert(ia != ib);
    test_assert(ib % c->x > ia % c->x);
    test_assert(ib / (c->x * c->y) < ia / (c->x * c->y));
    test_assert(ia < box_cluster_count(c));
    test_assert(ib < box_cluster_count(c));
}

void Lights_moved_light_is_reextracted(void)
{
    // lighting/clustered_forward
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, DynamicTransform);
    ecs_set(w, e, Position, {0, 0, 10});
    ecs_set(w, e, PointLight, {.intensity = 1, .range = 4});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    test_int(packet.lights.count, 1);
    box_packet_reset(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_set(w, e, Position, {3, 0, 10});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    test_int(packet.lights.count, 1);
    box_light_t *light = box_vec_at(&packet.lights, 0);
    test_flt(light->position[0], 3);
}
