# Scroll component for UI elements

## Built
`Scroll {x, y, offset}` with the CSS `Overflow` modes (Visible, Hidden, Scroll,
Auto) in `box.ui`. The layout measures the content of an element without its
box, clamps the offset to what does not fit and moves the children a Layout
places and the glyphs of a Text by it; an anchored child stays pinned. The clip
is a rectangle per node (`RenderClip`) that extract packs into the instance
(`clip` words, a mode bit) and `mesh.wgsl` discards fragments outside, so a
subtree is cut off on every draw and the hit test skips what it cuts. The wheel
moves the innermost scrollable ancestor of the hovered element, and an offset
that sat at the end of the content follows that end while the content grows,
which is what keeps the console log on its last line. `console.flecs` gives its
scrollback `Fill {height: 0.9}` and `Scroll {y: Auto}`.

## Verified
`test/ui -- -j 8`: 211 passed, 24 new `Scroll` cases; `test/render`: 415 passed;
lint clean. New `ui_scroll` visual scene (screenshot here) and the console
baseline refreshed; full `--env scroll` sweep green: visual (97) and command
(18), no other baseline touched. `bench/ui` gains two scrolled layout cases:
a 10k scrolled tree lays out in 732 us against 572 us unscrolled.

## Left out
No scrollbar and no drag to scroll; the clip is the Bounds of the element, not
its padding box, and it is axis aligned, so a rotated subtree clips by its
rectangle in the plane of the tree.
