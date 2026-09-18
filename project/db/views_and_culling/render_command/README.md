# render command

render [entity|prefab|template] renders any subject on its own and returns the path of the image.

## Details

The command instantiates the subject into a scratch scene when it is a prefab or a template, creates a temporary view with its own camera framed on the subject's AABB, a neutral light and its own layer, renders one frame into a texture target and writes a PNG to the session resources folder. The entity form renders the live entity without instantiating anything. The scratch scene and the view are deleted afterwards and the command prints the path relative to the session folder, exactly as capture does.

Verification: command based integration tests render an entity, a prefab and a template and keep the command log and the images as deliverables. Unit tests in test/command cover argument parsing, framing from the AABB, the scratch scene cleanup and the error path for an unknown name.
