get scene.crate Position
checkpoint base
set scene.crate Position "{3, 0, 0}"
rm scene.ball
touch extra
set extra Position "{9, 0, 0}"
get scene.crate Position
ls scene
restore base
get scene.crate Position
ls scene
get extra Position
set scene.crate Position "{5, 0, 0}"
checkpoint moved
set scene.crate Position "{6, 0, 0}"
restore
get scene.crate Position
