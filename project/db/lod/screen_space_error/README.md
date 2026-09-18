# Screen space error

The projected error metric with hysteresis and the per view LOD quality settings.

## Details

LOD selection uses projected error in pixels computed from a node's bounds and the error of its representation, for perspective and orthographic cameras alike. Hysteresis keeps a node at its current level until the error crosses a widened band, so a camera hovering at a boundary does not cause thrashing. View gains quality and error threshold fields, and a shadow view may use a more aggressive threshold than the primary view.

Verification: unit tests compute the error for known bounds and cameras, cover both projections, the hysteresis band, the per view settings and the shadow override. A test walks a camera towards and away from an object and records the selected level per frame to show there is no oscillation.
