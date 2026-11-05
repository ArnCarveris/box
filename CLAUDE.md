# Documentation

Never write new documentation or add to existing documentation unless the task explicitly asks for it.

# Tests

Tests never rely on anything in `etc` (or an app's `etc`). A script, font, image or other file a
test needs lives in the test folder, in `test/assets` or next to the test, and the test names it
from the repository root (`test/assets/ui/widgets.flecs`). Only the files the engine itself loads
from `etc`, such as shaders, are exempt.
