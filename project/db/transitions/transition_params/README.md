# Transition components

A Transition map from a component or member to its duration, delay and timing function.

## Details

Transition is a map from a component or member entity to TransitionParams with duration, delay and a timing function, inherited by instances so a prefab can define the feel of its whole family. The author facing component keeps the assigned value; setting it starts a new transition from the value currently displayed and replaces any transition in flight for that component. Position, Rotation, Scale, Rgb, Tint, Opacity and Emissive are supported at minimum, with a registration point for any reflected component of floats. TransitionValue exposes the interpolated value back to the world as a pair for scripts that need to read it.

Verification: unit tests cover starting, replacing, delay, completion, each timing function, inheritance from a prefab, a value assigned twice in one frame and an unsupported component. A visual scene transitions colors and positions and captures intermediate frames.
