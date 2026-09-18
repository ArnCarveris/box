#include "nested_b.wgsl"

fn outer() -> i32 {
  return middle();
}
