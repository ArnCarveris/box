// Spectra of WaterSpectrum (gpu_spectrum.c): the waves of a Water as a grid of wave vectors per
// cascade, evolved to the time and inverse FFT'd into a tiling texture per cascade.
//
// spectrum draws the initial amplitudes h0(k) of every wave vector k of the grid of each cascade
// from the seed when the parameters change: a complex normal random number times
// sqrt(S(k) / 2) dk / sqrt(2), with S the directional wave spectrum (spectrum_density) and dk the
// spacing of the grid, so the variance of the heights is the integral of S. Each texel keeps
// h0(k) and h0(-k), so the evolved spectrum h(k, t) = h0(k) e^(i w t) + conj(h0(-k) e^(i w t))
// is Hermitian and transforms to real heights. Texel x of a grid of n holds the wave number
// 2 pi (x or x - n, whichever lies in -n / 2 .. n / 2 - 1) / size, so the plain inverse DFT
// gives the heights at texel x of the tile; the Nyquist row and column are left 0. A cascade
// holds the wave vectors between its cuts, so the cascades do not double up.
//
// rows evolves the spectrum of a row of a cascade to the time with the deep water dispersion
// w = sqrt(g k), rounded down to a multiple of 2 pi / PERIOD so the waves loop over PERIOD
// seconds, packs the slopes i kx h + i (i kz h) and the height h into two complex values and
// inverse FFTs the row (Stockham radix-2 in workgroup memory, one workgroup per row); columns
// inverse FFTs the columns and stores the real slopes along X and Z, the height and the square
// of the slope into mip 0 of the texture array of the spectrum, a layer per cascade. mip then
// averages each mip into the next, so the square of the slope of a mip minus the square of its
// mean slope is the slope variance the filtering took away.

// The parameters of a spectrum (box_gpu_spectrum_params_t).
struct Params {
    // Texels per side, cascades, log2 of the texels per side, 1 to transform the initial
    // spectrum as is (box_render_gpu_spectrum_fft).
    grid: vec4u,
    // Seed of the random amplitudes, three unused.
    seed: vec4u,
    // World units of the tile of each cascade (xyz), time wrapped to PERIOD (w).
    sizes: vec4f,
    // Wave numbers between the cascades: cascade c holds cuts[c] <= |k| < cuts[c + 1].
    cuts: vec4f,
    // Normalized wind direction in world XZ (xy), wind speed (z), directionality (w).
    wind: vec4f,
    // Amplitude (x), three unused.
    shape: vec4f,
};

// Gravity of the deep water dispersion, BOX_MATERIAL_WATER_GRAVITY.
const GRAVITY: f32 = 9.81;

// Seconds after which the waves repeat, BOX_RENDER_SPECTRUM_PERIOD.
const PERIOD: f32 = 256.0;

// Wavelength below which the waves are damped, BOX_RENDER_SPECTRUM_SHORTEST.
const SHORTEST: f32 = 0.01;

const PI: f32 = 3.14159265358979;

// Most texels per side, BOX_MATERIAL_SPECTRUM_RESOLUTION_MAX, and the invocations of a
// workgroup of rows and columns, one per butterfly.
const MAX_SIZE: u32 = 256u;
const BUTTERFLIES: u32 = 128u;

@group(0) @binding(0) var<uniform> params: Params;
@group(0) @binding(1) var<storage, read_write> initial: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> transformed: array<vec4f>;
@group(0) @binding(3) var slopes: texture_storage_2d_array<rgba16float, write>;

@group(0) @binding(4) var mip_source: texture_2d_array<f32>;
@group(0) @binding(5) var mip_target: texture_storage_2d_array<rgba16float, write>;

var<workgroup> fft_a: array<vec4f, MAX_SIZE>;
var<workgroup> fft_b: array<vec4f, MAX_SIZE>;

// Index of texel x, z of cascade c in the buffers.
fn texel_index(x: u32, z: u32, c: u32) -> u32 {
    let n = params.grid.x;
    return (c * n + z) * n + x;
}

// Product of two complex numbers.
fn cmul(a: vec2f, b: vec2f) -> vec2f {
    return vec2f(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

// Wave vector of texel x, z of cascade c, radians per world unit.
fn wave_vector(x: u32, z: u32, c: u32) -> vec2f {
    let n = i32(params.grid.x);
    let mx = select(i32(x) - n, i32(x), i32(x) < n / 2);
    let mz = select(i32(z) - n, i32(z), i32(z) < n / 2);
    return vec2f(f32(mx), f32(mz)) * (2.0 * PI / params.sizes[c]);
}

// PCG hash of a 32 bit value.
fn pcg(v: u32) -> u32 {
    let state = v * 747796405u + 2891336453u;
    let word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Uniform random number in 0..1, never 0, from a hash.
fn uniform01(h: u32) -> f32 {
    return (f32(h >> 8u) + 0.5) / 16777216.0;
}

// Two independent standard normal random numbers of texel x, z of cascade c (Box-Muller).
fn gauss(x: u32, z: u32, c: u32) -> vec2f {
    let h1 = pcg(params.seed.x ^ pcg(x + pcg(z + pcg(c))));
    let h2 = pcg(h1);
    let r = sqrt(-2.0 * log(uniform01(h1)));
    let a = 2.0 * PI * uniform01(h2);
    return vec2f(r * cos(a), r * sin(a));
}

// The directional wave spectrum at a wave vector, the variance of the height per unit area of
// wave vectors: JONSWAP of a fully developed sea for the wind speed, spread by cos^2 toward the
// wind times the directionality and the same in every direction for the rest, damped below
// SHORTEST, times the square of the amplitude. box_render_spectrum_density is its C twin.
fn spectrum_density(k: vec2f) -> f32 {
    let kl = length(k);
    if (kl <= 0.0 || params.wind.z <= 0.0 || params.shape.x <= 0.0) {
        return 0.0;
    }
    let omega = sqrt(GRAVITY * kl);
    let peak = 0.855 * GRAVITY / (1.026 * params.wind.z);
    let sigma = select(0.09, 0.07, omega <= peak);
    let d = (omega - peak) / (sigma * peak);
    let ratio = peak / omega;
    let r2 = ratio * ratio;
    let jonswap = 0.0081 * GRAVITY * GRAVITY / pow(omega, 5.0) * exp(-1.25 * r2 * r2) *
        pow(3.3, exp(-0.5 * d * d));
    let c = dot(k, params.wind.xy) / kl;
    let spread = params.wind.w * select(0.0, 2.0 / PI * c * c, c > 0.0) +
        (1.0 - params.wind.w) / (2.0 * PI);
    let s = kl * SHORTEST;
    return jonswap * (GRAVITY / (2.0 * omega)) / kl * spread * exp(-s * s) * params.shape.x *
        params.shape.x;
}

// Initial amplitude of texel x, z of cascade c: 0 on the Nyquist row and column and outside the
// cuts of the cascade.
fn initial_amplitude(x: u32, z: u32, c: u32) -> vec2f {
    let n = params.grid.x;
    if (x == n / 2u || z == n / 2u) {
        return vec2f(0.0);
    }
    let k = wave_vector(x, z, c);
    let kl = length(k);
    if (kl < params.cuts[c] || kl >= params.cuts[c + 1u]) {
        return vec2f(0.0);
    }
    let dk = 2.0 * PI / params.sizes[c];
    return gauss(x, z, c) * (0.5 * sqrt(spectrum_density(k)) * dk);
}

// Draw the initial spectrum: h0(k) (xy) and h0(-k) (zw) of every texel of every cascade.
@compute @workgroup_size(8, 8, 1)
fn spectrum(@builtin(global_invocation_id) id: vec3u) {
    let n = params.grid.x;
    if (id.x >= n || id.y >= n || id.z >= params.grid.y) {
        return;
    }
    let a = initial_amplitude(id.x, id.y, id.z);
    let b = initial_amplitude((n - id.x) % n, (n - id.y) % n, id.z);
    initial[texel_index(id.x, id.y, id.z)] = vec4f(a, b);
}

// The spectrum of texel x, z of cascade c at the time: the slopes i kx h + i (i kz h) (xy)
// and the height h (zw), h = h0(k) e^(i w t) + conj(h0(-k) e^(i w t)) with w the dispersion
// rounded down to a multiple of 2 pi / PERIOD; the initial values as they are in raw mode.
fn evolved(x: u32, z: u32, c: u32) -> vec4f {
    let e = initial[texel_index(x, z, c)];
    if (params.grid.w != 0u) {
        return e;
    }
    let k = wave_vector(x, z, c);
    let cycles = floor(sqrt(GRAVITY * length(k)) * PERIOD / (2.0 * PI));
    let phase = 2.0 * PI * fract(cycles * (params.sizes.w / PERIOD));
    let w = vec2f(cos(phase), sin(phase));
    let a = cmul(e.xy, w);
    let b = cmul(e.zw, w);
    let h = a + vec2f(b.x, -b.y);
    return vec4f(cmul(h, vec2f(-k.y, k.x)), h);
}

// One radix-2 Stockham step of the inverse DFT of a line of n in workgroup memory, from fft_a
// into fft_b or, when odd, from fft_b into fft_a: butterfly j of the step whose transforms are
// ns long so far.
fn fft_step(j: u32, n: u32, ns: u32, odd: bool) {
    let half = n / 2u;
    if (j >= half) {
        return;
    }
    var v0: vec4f;
    var v1: vec4f;
    if (odd) {
        v0 = fft_b[j];
        v1 = fft_b[j + half];
    } else {
        v0 = fft_a[j];
        v1 = fft_a[j + half];
    }
    let angle = 2.0 * PI * f32(j % ns) / f32(ns * 2u);
    let w = vec2f(cos(angle), sin(angle));
    v1 = vec4f(cmul(v1.xy, w), cmul(v1.zw, w));
    let d = (j / ns) * ns * 2u + (j % ns);
    if (odd) {
        fft_a[d] = v0 + v1;
        fft_a[d + ns] = v0 - v1;
    } else {
        fft_b[d] = v0 + v1;
        fft_b[d + ns] = v0 - v1;
    }
}

// Inverse DFT of the line of fft_a, every invocation of the workgroup taking part; the result
// is in fft_b after an odd number of steps (fft_result).
fn fft(j: u32) {
    let n = params.grid.x;
    for (var s = 0u; s < params.grid.z; s++) {
        fft_step(j, n, 1u << s, (s & 1u) == 1u);
        workgroupBarrier();
    }
}

// Element i of the line fft transformed.
fn fft_result(i: u32) -> vec4f {
    if ((params.grid.z & 1u) == 1u) {
        return fft_b[i];
    }
    return fft_a[i];
}

// Evolve row z (workgroup x) of cascade workgroup y and inverse DFT it into transformed.
@compute @workgroup_size(BUTTERFLIES, 1, 1)
fn rows(@builtin(local_invocation_id) local: vec3u, @builtin(workgroup_id) group: vec3u) {
    let n = params.grid.x;
    let half = n / 2u;
    let j = local.x;
    let z = group.x;
    let c = group.y;
    if (j < half) {
        fft_a[j] = evolved(j, z, c);
        fft_a[j + half] = evolved(j + half, z, c);
    }
    workgroupBarrier();
    fft(j);
    if (j < half) {
        transformed[texel_index(j, z, c)] = fft_result(j);
        transformed[texel_index(j + half, z, c)] = fft_result(j + half);
    }
}

// Inverse DFT column x (workgroup x) of cascade workgroup y of transformed and store its slopes,
// height and square of the slope into mip 0.
@compute @workgroup_size(BUTTERFLIES, 1, 1)
fn columns(@builtin(local_invocation_id) local: vec3u, @builtin(workgroup_id) group: vec3u) {
    let n = params.grid.x;
    let half = n / 2u;
    let j = local.x;
    let x = group.x;
    let c = group.y;
    if (j < half) {
        fft_a[j] = transformed[texel_index(x, j, c)];
        fft_a[j + half] = transformed[texel_index(x, j + half, c)];
    }
    workgroupBarrier();
    fft(j);
    if (j < half) {
        let a = fft_result(j);
        let b = fft_result(j + half);
        textureStore(slopes, vec2u(x, j), c, vec4f(a.xyz, dot(a.xy, a.xy)));
        textureStore(slopes, vec2u(x, j + half), c, vec4f(b.xyz, dot(b.xy, b.xy)));
    }
}

// Average 2x2 texels of the previous mip into the next, every layer (z).
@compute @workgroup_size(8, 8, 1)
fn mip(@builtin(global_invocation_id) id: vec3u) {
    let size = textureDimensions(mip_target);
    if (id.x >= size.x || id.y >= size.y) {
        return;
    }
    let p = vec2i(id.xy) * 2;
    let l = i32(id.z);
    let v = textureLoad(mip_source, p, l, 0) + textureLoad(mip_source, p + vec2i(1, 0), l, 0) +
        textureLoad(mip_source, p + vec2i(0, 1), l, 0) +
        textureLoad(mip_source, p + vec2i(1, 1), l, 0);
    textureStore(mip_target, id.xy, id.z, v * 0.25);
}
