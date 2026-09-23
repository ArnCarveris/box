# svg_publish_readonly

## Built
- `BoxJobUpdate` (src/job/job.c) now runs as an immediate system. Job completions publish
  results with `ecs_modified`; as a regular system the drain ran with the world readonly and
  not deferred, so an OnSet observer on `RenderTexture` (a reactive UI script) that changed the
  world, or the command flush after the event, hit the readonly assert.

## Verified
- Reproducer `SvgJobs.publish_from_progress_with_observer` aborted with the readonly assert
  before the fix and passes after it.
- test/render: 424 passed, 0 failed (`tests.json`); lint 0 problems.
- `box_app --headless --timeout 60` on megatower with `fps on` and with `stats renderer` both
  ran to their timeout with exit 0.

## Left out
- No bench: the change only alters how the completion drain is scheduled.
