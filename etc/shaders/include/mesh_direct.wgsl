// Direct light of the mesh shaders: the GGX terms and the shadow terminator of a shading normal
// (shadow_terminator) both share, and the loop over the lights of a fragment, with the shadows
// of the spot and point lights (scene_punctual_shadows) and the cookie of the first directional
// light (scene_cookies). The includer includes mesh_common.wgsl first and declares the Surface of
// its fragments, with at least the shading normal and the geometric normal, and light_lobes, the
// light its surface reflects of one light, whose lobes of the shading normal fade by
// shadow_terminator.
#pragma once

// Light reflected toward the eye, split into its diffuse and specular parts; the emission is
// part of the specular one.
struct Lit {
    diffuse: vec3f,
    specular: vec3f,
};

fn distribution_ggx(ndh: f32, a: f32) -> f32 {
    let a2 = a * a;
    let d = ndh * ndh * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

fn visibility_smith(ndv: f32, ndl: f32, a: f32) -> f32 {
    let a2 = a * a;
    let gv = ndl * sqrt((ndv - a2 * ndv) * ndv + a2);
    let gl = ndv * sqrt((ndl - a2 * ndl) * ndl + a2);
    return 0.5 / max(gv + gl, 1e-7);
}

fn fresnel_schlick(cos_theta: f32, f0: vec3f) -> vec3f {
    let x = 1.0 - cos_theta;
    let x2 = x * x;
    return f0 + (vec3f(1.0) - f0) * (x2 * x2 * x);
}

// Shadow terminator of a shading normal ns, such as a normal map, on a surface of geometric
// normal ng for a light toward l (Chiang, Li and Burley 2019, "Taming the Shadow Terminator").
// A shading normal that faces the light where the geometric surface turns away from it would
// light the side the surface shadows, up to a hard cut where the light is shadowed. The fraction
// G = dot(ng, l) / (dot(ns, l) * dot(ng, ns)) of the light that the bumps of the shading normal
// leave unshadowed fades it smoothly as -G^3 + G^2 + G: 0 with the light below the geometric
// surface, 1 where ns is ng or the light is high enough above it.
fn shadow_terminator(ng: vec3f, ns: vec3f, l: vec3f) -> f32 {
    let ngl = dot(ng, l);
    if (ngl <= 0.0) {
        return 0.0;
    }

    let d = dot(ns, l) * dot(ng, ns);
    if (d <= ngl) {
        return 1.0;
    }

    let g = ngl / d;
    return g * (1.0 + g * (1.0 - g));
}

// Direct light: the directional lights, then the lights of the cluster. With
// scene_planet_light the sun of the planet of the fragment (fragment_planet) is attenuated by
// the transmittance of its atmosphere at world. With sides 2 each light
// is evaluated again thickness behind the surface along the view ray, for the back lobe of a
// diffuse transmission, which light_lobes returns for side 1. The directional and cluster lights
// share one loop, and the sides one loop per light, so the shadows, the cookie and the lobes each
// have one call site: D3D compilers inline every call.
fn lights_direct(s: Surface, v: vec3f, world: vec3f, cluster: u32, sides: u32,
    thickness: f32) -> Lit
{
    var sum = Lit(vec3f(0.0), vec3f(0.0));
    var planet = PlanetSun(vec3f(0.0), vec3f(1.0));
    if (scene_planet_light) {
        planet = planet_light_sun(fragment_planet, world);
    }
    let directional = view.lights.x;
    var count = directional;
    var first = 0u;
    if (view.lights.y > view.lights.x) {
        first = 1u + 2u * cluster_count(view) + clusters[1u + 2u * cluster];
        count += cluster_lights(clusters[2u + 2u * cluster]);
    }

    for (var i = 0u; i < count; i++) {
        var light: Light;
        if (i < directional) {
            light = lights[i];
        } else {
            light = lights[clusters[first + i - directional]];
        }

        for (var side = 0u; side < sides; side++) {
            var at = world;
            var n = s.geometric;
            if (side == 1u) {
                at = world - v * thickness;
                n = -s.geometric;
            }

            var l: vec3f;
            var radiance = light.color;
            var visible = 1.0;
            if (i < directional) {
                l = normalize(light.position);
                if (scene_planet_light) {
                    radiance *= planet_light_factor(planet, l);
                }
                if (i == 0u) {
                    visible = shadow_factor(at, n);
                    if (scene_cookies) {
                        visible *= light_cookie(at, l, n);
                    }
                }
            } else {
                let delta = light.position - at;
                let dist = length(delta);
                l = delta / max(dist, 1e-4);
                radiance *= attenuation(light, l, dist);
                if (scene_punctual_shadows && light.shadow < view.spot_config.x) {
                    visible = punctual_shadow(light, at, n);
                }
            }

            let lit = light_lobes(s, v, l, radiance, side);
            sum.diffuse += lit.diffuse * visible;
            sum.specular += lit.specular * visible;
        }
    }

    return sum;
}
