# First person controller

`FirstPersonController` walks a character with the same movement actions as the camera
controller, with `walk` (4), `run` (8), `jump` (5), `gravity` (18), `eye` (1.7), `crouch` (0.9),
`acceleration` (12) and `sensitivity` as its settings and `yaw`, `pitch`, `velocity`, `grounded`
and `flying` as the state it writes; a member that is zero or less takes its default. The module
adds the actions `Jump`, `Crouch`, `Run` and `Fly` to the map it already binds, on space, either
control, either shift and F, and the same `box.input.map.<Action>` bindings make them reboundable.

`BoxFirstPersonUpdate` runs in OnUpdate next to the camera controller and shares its mouse look:
it eases the horizontal velocity towards the direction the actions ask for at the walk or the run
speed, applies gravity while the character is in the air, jumps on the frame `Jump` is pressed and
lands when it reaches the ground. The ground is the top of the bounds of the `terrain` entity, or
the `ground` height when there is none, which is what the collision epic replaces with a real
sweep; standing on it the eye follows `eye` or `crouch` at once, so crouching does not fall. `Fly`
toggles free flight, which moves like the camera controller and ignores gravity and the ground.

12 tests pass in `test/controller` suite `Person`, `Person.update_100_characters` costs 13 us and
`test/integration/visual/first_person.flecs` is the baselined capture of the controller at rest.
