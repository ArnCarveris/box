# Impostors

Render very distant subtrees as a small set of billboarded views.

## Details

For subtrees that are still too expensive at extreme distance, render a few views into an atlas and draw a billboard that picks the nearest view, regenerated only when the subtree changes. Impostors are the last resort and are only selected when their error is below the threshold. Unit tests cover atlas allocation, view selection and invalidation.
