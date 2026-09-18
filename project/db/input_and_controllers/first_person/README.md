# First person controller

A first person controller with walking, jumping, crouching and a free flight toggle.

## Details

FirstPersonController adds gravity, a ground check, a jump impulse, a crouch height and an F toggle that switches to free flight reusing the camera controller movement. Movement is relative to the view yaw and capped by a walk and a run speed, both bound to actions. The controller writes Position and Orientation like any other system, so nothing about it is special cased in the engine.

Verification: unit tests cover jump arcs, crouching and standing, the free flight toggle, speed caps and that all input arrives through actions. A visual scene walks a small level and captures frames along the path.
