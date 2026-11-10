// Light cookie of the first directional light of the scene (LightCookie), read by the forward
// pass and the particle draw: a tileable single channel texture that scales the direct light of
// that light, diffuse and specular, together with its shadow.
#pragma once

@group(0) @binding(17) var cookie_texture: texture_2d<f32>;
@group(0) @binding(18) var cookie_sampler: sampler;

// Smallest ramp of the remap, which makes a softness of 0 a step at the threshold.
const COOKIE_MIN_RAMP: f32 = 1e-5;

// Smallest |dot(n, v)| of the pixel steps on a surface, which keeps them finite at grazing.
const COOKIE_MIN_GRAZING: f32 = 1e-3;

// World step of one pixel along the camera axis r, scaled to a pixel, on the tangent plane of n
// for the camera ray v.
fn cookie_step(r: vec3f, v: vec3f, n: vec3f) -> vec3f {
    let nv = dot(n, v);
    let d = select(-1.0, 1.0, nv >= 0.0) * max(abs(nv), COOKIE_MIN_GRAZING);
    return r - v * (dot(n, r) / d);
}

// Cookie of the first directional light at a world position, with l the normalized direction
// toward that light and n the geometric normal of the surface, or zero for the isotropic
// footprint of a particle: 1 where it does not apply.
fn light_cookie(world: vec3f, l: vec3f, n: vec3f) -> f32 {
    if (view.cookie_config.x == 0u) {
        return 1.0;
    }

    let plane_projection = view.cookie_config.y == 0u;
    var plane: vec2f;
    if (plane_projection) {
        let height = view.cookie.y;
        if (l.y <= 0.0 || world.y >= height) {
            return 1.0;
        }

        let q = world + l * ((height - world.y) / l.y);
        plane = q.xz;
    } else {
        plane = vec2f(dot(world, view.cookie_x.xyz), dot(world, view.cookie_y.xyz));
    }

    let uv = (plane + view.cookie_offset.xy) * view.cookie.x;
    var duv_x: vec2f;
    var duv_y: vec2f;
    let orthographic = view.lights.w != 0u;
    if (dot(n, n) == 0.0) {
        var pixel = 2.0 * view.cluster.y * view.viewport.w;
        if (!orthographic) {
            pixel *= length(world - view.camera.xyz);
        }

        duv_x = vec2f(pixel * view.cookie.x, 0.0);
        duv_y = vec2f(0.0, pixel * view.cookie.x);
    } else {
        let right = vec3f(view.view[0].x, view.view[1].x, view.view[2].x);
        let up = vec3f(view.view[0].y, view.view[1].y, view.view[2].y);
        let toward = vec3f(view.view[0].z, view.view[1].z, view.view[2].z);
        var size = 2.0 * view.cluster.xy * view.viewport.zw;
        var v = -toward;
        if (!orthographic) {
            size *= max(cluster_view_position(view, world).z, 0.0);
            v = normalize(world - view.camera.xyz);
        }

        let dp_x = cookie_step(right * size.x, v, n);
        let dp_y = cookie_step(up * size.y, v, n);
        if (plane_projection) {
            let dq_x = dp_x - l * (dp_x.y / l.y);
            let dq_y = dp_y - l * (dp_y.y / l.y);
            duv_x = dq_x.xz * view.cookie.x;
            duv_y = dq_y.xz * view.cookie.x;
        } else {
            duv_x = vec2f(dot(dp_x, view.cookie_x.xyz), dot(dp_x, view.cookie_y.xyz)) *
                view.cookie.x;
            duv_y = vec2f(dot(dp_y, view.cookie_x.xyz), dot(dp_y, view.cookie_y.xyz)) *
                view.cookie.x;
        }
    }

    let t = textureSampleGrad(cookie_texture, cookie_sampler, uv, duv_x, duv_y).r;
    let ramp = max(2.0 * view.cookie.w, COOKIE_MIN_RAMP);
    let lit = saturate((t - view.cookie.z) / ramp + 0.5);
    return mix(1.0, lit, view.cookie_offset.z);
}
