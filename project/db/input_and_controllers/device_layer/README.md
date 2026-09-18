# Device input layer

An Input singleton with per frame key, mouse and text state, fed by the OS and by injected events.

## Details

Input holds 128 key slots plus the left and right mouse buttons as codes 128 and 129 and a text character buffer, and each slot has pressed, state and current so a frame can tell a new press from a held key. The mouse is stored as window coordinates, a per frame delta, view centred coordinates, normalised minus one to one coordinates and accumulated scroll. A single OnLoad system resets the per frame state, applies OS events and then injected events, so injected input behaves exactly like real input. Real input is ignored while the window is unfocused or minimised, losing focus clears all key and button state, and regaining focus causes neither a phantom press nor a cursor jump.

Verification: unit tests drive the system with injected events and cover pressed versus held, release, the text buffer, scroll accumulation, every coordinate space, focus loss clearing state and no phantom press on refocus. Key codes are ASCII where possible with spare codes for non printable keys and are exported so the action layer and scripts can name them.

The two subtasks map the platform events: GLFW natively and HTML5 canvas callbacks on the web.
