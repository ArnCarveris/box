# Material layers

Scene global material layers such as dust, edge wear, grime and wetness, enabled per material and toggleable per view.

## Details

A material layer is an entity holding the parameters of one effect; the scene owns one instance per layer and a material enables it with a pair so the parameters stay shared and global while the choice is per material. The included layers are dust, edge wear, grime and wetness, each a shader fragment applied after the PBR textures are sampled. A project registers its own layer by adding a component and a shader include, which is the extension point the brief requires, and View gains a toggle that renders a view without material layers.

Verification: unit tests cover registration, enabling a layer per material, shared parameter updates, unregistration and the per view toggle. Visual scenes render the same object with each layer on and off and with all four combined, and a benchmark compares shading cost with four layers enabled against none.

Introduces the MaterialLayer registration, the Dust, EdgeWear, Grime and Wetness components and the material_layers toggle on View.
