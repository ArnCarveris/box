// Ambient light of a view in a direction, shared by the forward pass and the fog volumes: the L2
// SH irradiance of the environment the view is lit with (LightSettings.environment), turned by
// its yaw, or the hemisphere ambient of its scene. The includer declares the View uniform as
// view (lights.wgsl) and PI. Views without an environment bind a zero SH, and their
// view.environment.w is 0.
#pragma once
#include "sh.wgsl"

@group(0) @binding(13) var<uniform> environment_sh: array<vec4f, 9>;

// Irradiance over pi in a world direction from the SH of the environment of the view, turned by
// its yaw, times its intensity.
fn environment_irradiance(dir: vec3f) -> vec3f {
    let d = environment_direction(view, dir);
    let e = sh_irradiance(environment_sh[0].rgb, environment_sh[1].rgb, environment_sh[2].rgb,
        environment_sh[3].rgb, environment_sh[4].rgb, environment_sh[5].rgb,
        environment_sh[6].rgb, environment_sh[7].rgb, environment_sh[8].rgb, d);
    return max(e / PI, vec3f(0.0)) * view.environment.x;
}

// Ambient light of the scene in a direction: the SH of the environment of a view lit by one,
// else the hemisphere ambient.
fn ambient(dir: vec3f) -> vec3f {
    if (view.environment.w > 0.0) {
        return environment_irradiance(dir);
    }

    return mix(view.ambient_ground.rgb, view.ambient_sky.rgb, dir.y * 0.5 + 0.5);
}
