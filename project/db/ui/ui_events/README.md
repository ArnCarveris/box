# UI events

Hover, click and drag events on UI elements through the script event framework, with bubbling and action emission.

## Details

Hit testing uses the Bounds rectangles and the tree order so the topmost element under the cursor wins, and raises the same script events as 3D picking, bubbling up the UI tree until a handler consumes them. A widget can emit an input action on click, so a button and a key binding share one action. All widget behaviour, including hover styling, is written in script; the module holds no widget specific logic.

Verification: unit tests cover hit testing with overlapping, clipped and rotated elements, bubbling, consumption, drag and action emission. A command based integration test clicks a scripted button through the input commands and checks that the action fired.
