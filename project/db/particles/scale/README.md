# Ten million particles

Reach the brief's target of ten million particles at 60 FPS with budgets, culling and a compact particle layout.

## Details

Bring the system to the target: indirect dispatch sized by the live particle count, a global budget shared between emitters, frustum and distance culling of emitters and a reduced simulation rate for distant ones. Alpha sorting is done per emitter rather than globally and skipped for additive blending. The per particle record is kept small enough that ten million fit in a reasonable buffer, and the budget degrades gracefully instead of stalling.

Verification: a release benchmark scene with ten million particles reports frame time, simulation cost and memory and must stay above 60 FPS; the report is the deliverable. Unit tests cover budget distribution between emitters and the emitter culling decisions.
