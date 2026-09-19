#include "include/engine_part.wgsl"

fn engine_only() -> i32 {
    return engine_part();
}
