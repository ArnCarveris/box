# Render layers

User defined layers as a fragmenting relationship that entities and lights belong to, with per view visible and shadow caster lists.

## Details

A layer is an entity created by the project and membership is a pair on an entity or a light, which fragments the table so a view can query its layers without testing every entity. An entity may hold more than one layer pair. View gains a layers list and a shadow_layers list where an empty list means every layer, restricting the visible set and the shadow casters independently. Lights follow the same rule so a layer can be lit by its own lights.

Verification: unit tests cover membership, multiple layers on one entity, empty lists meaning all, and lights restricted to a layer. A visual scene renders one world through two views that each show a different layer, and a benchmark checks that the layer machinery costs nothing in a scene without layers.

Introduces the Layer relationship and the layers and shadow_layers fields on View.
