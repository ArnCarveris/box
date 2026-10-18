/**
 * @file atmosphere/atmosphere.c
 * @brief Registers the box.atmosphere components and computes the transmittance on the CPU.
 *
 * The medium follows Hillaire 2020 as the transmittance LUT of the shaders does, with the
 * coefficients of etc/shaders/include/atmosphere_medium.h.
 */

#define BOX_ATMOSPHERE_IMPL
#include "atmosphere.h"
#include "../render/render.h"
#include <math.h>

/** Environment.resolution an Atmosphere adds */
#define BOX_ATMOS_ENVIRONMENT_RESOLUTION (128)

/** Error of a planet radius that is not positive */
#define BOX_ATMOS_RADIUS_ERROR "planet_radius must be positive"

/** Error of a thickness that is not positive */
#define BOX_ATMOS_THICKNESS_ERROR "thickness must be positive"

/** Error of a ground altitude outside the atmosphere */
#define BOX_ATMOS_GROUND_ERROR "ground_altitude must be within 0 and thickness"

/** Error of a units_per_km that is not positive */
#define BOX_ATMOS_UNITS_ERROR "units_per_km must be positive"

/** Error of a scale height that is not positive */
#define BOX_ATMOS_HEIGHT_ERROR "rayleigh and mie scale_height must be positive"

/** Error of a negative scattering or absorption scale */
#define BOX_ATMOS_SCALE_ERROR "scattering_scale and ozone_scale must not be negative"

/** Error of a Mie anisotropy outside -1 to 1 */
#define BOX_ATMOS_ANISOTROPY_ERROR "mie anisotropy must be within -1 and 1"

/** Error of a negative disk radius */
#define BOX_ATMOS_DISK_ERROR "sun_radius and moon_radius must not be negative"

/** Error of a negative aerial perspective distance */
#define BOX_ATMOS_AERIAL_ERROR "aerial_distance must not be negative"

/** Construct Atmosphere values with the Earth defaults.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_atmosphere_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Atmosphere *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Atmosphere){
            .planet_radius = 6360.0f,
            .thickness = 100.0f,
            .ground_altitude = 0.5f,
            .units_per_km = 1000.0f,
            .rayleigh = { .scale_height = 8.0f, .scattering_scale = 1.0f },
            .mie = {
                .scale_height = 1.2f, .anisotropy = 0.8f, .scattering_scale = 1.0f,
                .tint = {255, 255, 255}
            },
            .ozone_scale = 1.0f,
            .ground_albedo = {77, 77, 77},
            .night_tint = {70, 110, 230},
            .night_intensity = 0.003f,
            .sun_disk_intensity = 1.0f,
            .sun_radius = 0.266f,
            .moon_disk_intensity = 1.0f,
            .moon_radius = 0.259f,
            .moon_albedo = 0.12f,
            .aerial_distance = 32.0f,
            .aerial_intensity = 1.0f,
            .ibl_update_angle = 0.5f
        };
    }
}

/** Add AtmosphereStatus to an entity that got Atmosphere, and Environment with the defaults of
 * an atmosphere unless it has one.
 *
 * @param it The observer iterator
 */
static void box_atmosphere_add(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_set(it->stage, it->entities[i], AtmosphereStatus, {0});
        if (!ecs_has(it->world, it->entities[i], Environment)) {
            ecs_set(it->stage, it->entities[i], Environment, {
                .resolution = BOX_ATMOS_ENVIRONMENT_RESOLUTION,
                .filter = EnvironmentFilterBox
            });
        }
    }
}

/** Remove AtmosphereStatus from an entity that lost Atmosphere.
 *
 * @param it The observer iterator
 */
static void box_atmosphere_remove(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_remove(it->stage, it->entities[i], AtmosphereStatus);
    }
}

const char* box_atmos_error(
    const Atmosphere *a)
{
    const char *result = NULL;
    if (!(a->planet_radius > 0)) {
        result = BOX_ATMOS_RADIUS_ERROR;
    } else if (!(a->thickness > 0)) {
        result = BOX_ATMOS_THICKNESS_ERROR;
    } else if (!(a->ground_altitude >= 0 && a->ground_altitude <= a->thickness)) {
        result = BOX_ATMOS_GROUND_ERROR;
    } else if (!(a->units_per_km > 0)) {
        result = BOX_ATMOS_UNITS_ERROR;
    } else if (!(a->rayleigh.scale_height > 0) || !(a->mie.scale_height > 0)) {
        result = BOX_ATMOS_HEIGHT_ERROR;
    } else if (!(a->rayleigh.scattering_scale >= 0) || !(a->mie.scattering_scale >= 0) ||
        !(a->ozone_scale >= 0))
    {
        result = BOX_ATMOS_SCALE_ERROR;
    } else if (!(a->mie.anisotropy > -1 && a->mie.anisotropy < 1)) {
        result = BOX_ATMOS_ANISOTROPY_ERROR;
    } else if (!(a->sun_radius >= 0) || !(a->moon_radius >= 0)) {
        result = BOX_ATMOS_DISK_ERROR;
    } else if (!(a->aerial_distance >= 0)) {
        result = BOX_ATMOS_AERIAL_ERROR;
    }

    return result;
}

/** Decode a display encoded channel to linear.
 *
 * @param c The channel
 * @return The linear value, 0 to 1
 */
static float box_atmos_linear(
    uint8_t c)
{
    float v = (float)c / 255.0f;

    return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
}

void box_atmos_medium(
    const Atmosphere *a,
    box_atmos_medium_t *out)
{
    const float tint[3] = {
        box_atmos_linear(a->mie.tint.r),
        box_atmos_linear(a->mie.tint.g),
        box_atmos_linear(a->mie.tint.b)
    };
    const float rayleigh[3] = {
        (float)BOX_ATMOS_RAYLEIGH_R, (float)BOX_ATMOS_RAYLEIGH_G, (float)BOX_ATMOS_RAYLEIGH_B
    };
    const float ozone[3] = {
        (float)BOX_ATMOS_OZONE_R, (float)BOX_ATMOS_OZONE_G, (float)BOX_ATMOS_OZONE_B
    };
    float mie = a->mie.scattering_scale;
    int32_t c;
    out->bottom = a->planet_radius;
    out->top = a->planet_radius + a->thickness;
    out->rayleigh_height = a->rayleigh.scale_height;
    out->mie_height = a->mie.scale_height;
    for (c = 0; c < 3; c ++) {
        out->rayleigh_scattering[c] = rayleigh[c] * a->rayleigh.scattering_scale;
        out->mie_scattering[c] = (float)BOX_ATMOS_MIE_SCATTERING * mie * tint[c];
        out->mie_extinction[c] = (float)BOX_ATMOS_MIE_EXTINCTION * mie * tint[c];
        out->ozone_absorption[c] = ozone[c] * a->ozone_scale;
    }
}

void box_atmos_extinction(
    const box_atmos_medium_t *m,
    float altitude,
    float *out)
{
    float rayleigh = expf(-altitude / m->rayleigh_height);
    float mie = expf(-altitude / m->mie_height);
    float ozone = 1.0f - fabsf(altitude - (float)BOX_ATMOS_OZONE_CENTER) /
        (float)BOX_ATMOS_OZONE_WIDTH;
    int32_t c;
    ozone = ozone > 0 ? ozone : 0;
    for (c = 0; c < 3; c ++) {
        out[c] = m->rayleigh_scattering[c] * rayleigh + m->mie_extinction[c] * mie +
            m->ozone_absorption[c] * ozone;
    }
}

/** Distance along a ray from a point at a radius to the nearest hit ahead with a sphere
 * around the planet center, as ray_sphere_nearest of the shaders.
 *
 * @param r Distance of the start from the planet center
 * @param mu Cosine of the angle between the ray and the up direction at the start
 * @param radius Radius of the sphere
 * @return The distance, -1 when the ray misses the sphere ahead
 */
static double box_atmos_ray_sphere(
    double r,
    double mu,
    double radius)
{
    double b = r * mu;
    double c = r * r - radius * radius;
    double disc = b * b - c;
    double result = -1.0;
    if (disc >= 0) {
        double sd = sqrt(disc);
        double t0 = -b - sd, t1 = -b + sd;
        result = t0 >= 0 ? t0 : t1 >= 0 ? t1 : -1.0;
    }

    return result;
}

void box_atmos_transmittance(
    const box_atmos_medium_t *m,
    float altitude,
    float cos_zenith,
    float *out)
{
    double top = m->top, bottom = m->bottom;
    double r = bottom + glm_max(altitude, (float)BOX_ATMOS_PLANET_OFFSET);
    double mu = glm_clamp(cos_zenith, -1.0f, 1.0f);
    double sin_zenith = sqrt(glm_max(0.0f, 1.0f - (float)(mu * mu)));
    double depth[3] = {0, 0, 0};
    int32_t i, c, steps = BOX_ATMOS_TRANSMITTANCE_STEPS;
    r = r < top ? r : top;
    double length = box_atmos_ray_sphere(r, mu, top);
    bool ground = box_atmos_ray_sphere(r, mu, bottom) > 0;
    if (!ground && length > 0) {
        double dt = length / steps;
        for (i = 0; i < steps; i ++) {
            double t = ((double)i + 0.5) * dt;
            double x = sin_zenith * t, y = r + mu * t;
            float extinction[3];
            box_atmos_extinction(m, (float)(sqrt(x * x + y * y) - bottom), extinction);
            for (c = 0; c < 3; c ++) {
                depth[c] += extinction[c] * dt;
            }
        }
    }

    for (c = 0; c < 3; c ++) {
        out[c] = ground ? 0.0f : (float)exp(-depth[c]);
    }
}

float box_atmos_horizon_fade(
    float sin_elevation,
    float radius)
{
    float elevation = asinf(glm_clamp(sin_elevation, -1.0f, 1.0f));
    float r = glm_rad(radius);
    float result = elevation > 0 ? 1.0f : 0.0f;
    if (r > 0) {
        float t = glm_clamp((elevation + r) / (2.0f * r), 0.0f, 1.0f);
        result = t * t * (3.0f - 2.0f * t);
    }

    return result;
}

float box_atmos_moon_illumination(
    const float *sun,
    const float *moon)
{
    float cos_elongation = sun[0] * moon[0] + sun[1] * moon[1] + sun[2] * moon[2];

    return glm_clamp(0.5f * (1.0f - cos_elongation), 0.0f, 1.0f);
}

void BoxAtmosphereImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxAtmosphere);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxLight);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxRender);

    ECS_META_COMPONENT(world, AtmosphereRayleigh);
    ECS_META_COMPONENT(world, AtmosphereMie);
    ECS_META_COMPONENT(world, Atmosphere);
    ecs_set_hooks(world, Atmosphere, { .ctor = box_atmosphere_ctor });
    ECS_META_COMPONENT(world, AtmosphereStatus);
    box_owned_hooks(world, AtmosphereStatus, BOX_OWNED_STR(AtmosphereStatus, error));
    ECS_META_COMPONENT(world, TimeOfDay);
    ECS_META_COMPONENT(world, TimeOfDayStatus);

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Atmosphere) }},
        .events = { EcsOnAdd },
        .callback = box_atmosphere_add
    });
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Atmosphere) }},
        .events = { EcsOnRemove },
        .callback = box_atmosphere_remove
    });

    box_render_lights_atmosphere(world);
    box_render_atmosphere_import(world);
    box_render_lights_time_of_day(world);
    box_tod_init(world);
}
