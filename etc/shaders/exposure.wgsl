// Auto exposure: `histogram` counts the log luminance of every pixel with alpha into 256 bins,
// bin 0 the pixels darker than the min log luminance, black included. `adapt` leaves bin 0 out,
// averages the other bins between the low and high percentile of their total and moves the
// exposure toward the one that brings that average into the accepted brightness range, by
// 1 - exp(-dt * speed) of the way. The first adapt of an exposure, with ready still 0, sets
// the target exposure at once.
struct Settings {
    // min log2 luminance, log2 luminance range, low and high percentile
    range: vec4f,
    // speed up, speed down, log2 min and max brightness
    adapt: vec4f,
    // log2 target brightness, compensation, log2 min and max exposure
    target_ev: vec4f,
    // x: frame delta in seconds, y: 1 with a target brightness
    frame: vec4f,
};

struct Exposure {
    ev: f32,
    scale: f32,
    average: f32,
    // 1 once the exposure was adapted, the buffer starts out zeroed
    ready: f32,
};

const BINS: u32 = 256u;

@group(0) @binding(0) var input_texture: texture_2d<f32>;
@group(0) @binding(1) var<storage, read_write> histogram: array<atomic<u32>, 256>;
@group(0) @binding(2) var<storage, read_write> exposure: Exposure;
@group(0) @binding(3) var<uniform> settings: Settings;

var<workgroup> local_bins: array<atomic<u32>, 256>;
var<workgroup> shared_bins: array<u32, 256>;

@compute @workgroup_size(16, 16)
fn histogram_main(
    @builtin(global_invocation_id) gid: vec3u,
    @builtin(local_invocation_index) lid: u32)
{
    atomicStore(&local_bins[lid], 0u);
    workgroupBarrier();
    let size = textureDimensions(input_texture, 0);
    let stride = u32(max(settings.frame.z, 1.0));
    let pixel = gid.xy * stride + vec2u(stride / 2u);
    if (pixel.x < size.x && pixel.y < size.y) {
        let c = textureLoad(input_texture, vec2i(pixel), 0);
        if (c.a > 0.0) {
            let luma = dot(max(c.rgb, vec3f(0.0)), vec3f(0.2126, 0.7152, 0.0722));
            var bin = 0u;
            if (luma > 0.0) {
                let t = (log2(luma) - settings.range.x) / settings.range.y;
                if (t >= 0.0) {
                    bin = u32(min(t, 1.0) * f32(BINS - 2u) + 1.0);
                }
            }
            atomicAdd(&local_bins[bin], 1u);
        }
    }

    workgroupBarrier();
    let count = atomicLoad(&local_bins[lid]);
    if (count > 0u) {
        atomicAdd(&histogram[lid], count);
    }
}

@compute @workgroup_size(256)
fn adapt_main(@builtin(local_invocation_index) idx: u32) {
    shared_bins[idx] = atomicLoad(&histogram[idx]);
    atomicStore(&histogram[idx], 0u);
    workgroupBarrier();
    if (idx != 0u) {
        return;
    }

    var total = 0u;
    for (var i = 1u; i < BINS; i++) {
        total += shared_bins[i];
    }

    let low = u32(f32(total) * settings.range.z);
    let high = u32(f32(total) * settings.range.w);
    var running = 0u;
    var weight = 0.0;
    var sum = 0.0;
    for (var i = 1u; i < BINS; i++) {
        let before = running;
        running += shared_bins[i];
        let lo = max(low, before);
        let hi = min(high, running);
        if (hi > lo) {
            let count = f32(hi - lo);
            let log_luma = settings.range.x + (f32(i) - 0.5) / f32(BINS - 2u) * settings.range.y;
            sum += log_luma * count;
            weight += count;
        }
    }

    var target_ev = 0.0;
    if (weight > 0.0) {
        let average = sum / weight;
        exposure.average = average;
        if (settings.frame.y > 0.5) {
            target_ev = settings.target_ev.x - average;
        } else if (average < settings.adapt.z) {
            target_ev = settings.adapt.z - average;
        } else if (average > settings.adapt.w) {
            target_ev = settings.adapt.w - average;
        }
    }

    target_ev = clamp(target_ev + settings.target_ev.y, settings.target_ev.z, settings.target_ev.w);
    let speed = select(settings.adapt.y, settings.adapt.x, target_ev > exposure.ev);
    var alpha = clamp(1.0 - exp(-settings.frame.x * speed), 0.0, 1.0);
    if (exposure.ready < 0.5) {
        alpha = 1.0;
    }

    exposure.ev = mix(exposure.ev, target_ev, alpha);
    exposure.scale = exp2(exposure.ev);
    exposure.ready = 1.0;
}
