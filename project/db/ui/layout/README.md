# Layout and anchoring

Layout and Anchor components that compute a read only Bounds rectangle for every element.

## Details

Layout arranges children in a row or a column in child order with padding, spacing, a size and horizontal and vertical alignment, and implies OrderedChildren so the order is authored. Anchor pins an element to an edge or the centre of its parent with a pixel offset. The layout pass writes Bounds on every element and only re-runs for trees that changed, so a static UI costs nothing per frame.

Verification: unit tests compute expected bounds for nested rows and columns, every alignment combination, padding, spacing, sizing from content, anchored children and a changed subtree. A visual scene renders a layout gallery, and a benchmark lays out 10k elements.
