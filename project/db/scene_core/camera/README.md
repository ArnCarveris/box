# Camera module

Camera component with perspective and orthographic reverse Z projection and Billboard.

## Details

`Camera` holds fov, near, far, aspect ratio and orthographic settings; zero fields take engine defaults and a zero aspect ratio follows the surface. The render module builds a reverse Z projection with an infinite far plane for perspective cameras and a reverse Z orthographic projection otherwise, and inverts the camera's `WorldTransform` for the view matrix. Without a camera entity a default view is used. `Billboard` rotates entities every frame so their local +Z faces the camera.
