# Astrarium

A game interface written in flecs script: the title screen and three pages of a celestial RPG. It shows what the 2D layer of the engine can do and how far a widget goes in script. There is no C in it and no gameplay beyond what the interface needs to feel alive, but every number on screen is real state.

Run from the repository root:

```sh
bake3 --local-env
bake3 run apps/boxrun --local-env -- etc/scenes/astrarium.flecs
```

## Controls

| Input | Action |
| --- | --- |
| Click a menu entry, or `Enter` | Leave the title screen |
| `Q` `E`, or the tabs | Previous and next page |
| `1` to `6`, or click a slot | Cast a spell |
| Click a star, then hold the button | Select and awaken a star |
| Hover and click an item | Weigh it against what you wear, and equip it |
| `Escape` | Return to the title screen |

## Things to try

- Open the Atlas: the astrolabe of the title screen flies into the middle of the chart as one element, and the bars slide in around it.
- Hold to Awaken: a comet runs down the link, and when it lands the star ignites with a shockwave and sparks while the attributes, the pips and the star points follow. The keystones at the end of each constellation also raise a banner.
- Cast `5`, Starfall, and watch aether drain from the orb and experience fill the bar. Level ups grant star points, which closes the loop back to the Atlas.
- Cast `3`, Thorn Aegis: a ring locks around the vitality orb and vitality stops ebbing.
- Hover the Satchel: the tooltip glides between items, the attribute bars preview the change in green and red, and the live 3D relic swaps to the item.
- In Rites, pick another metal. The whole interface fades to it. Drag Aura to change the bloom of every view, and Clockwork to change the speed of the astrolabe.
- Resize the window. The interface is laid out on a virtual screen of at least 1440 by 810, like a game, and wider windows give the panels more room.

## How it is made

`Star` and `Item` are declarative: the chart and the satchel are lists of instances with their stats, their position and their text. A panel reads the selected one with `atlas_sel[Star]`, so the data lives where it is drawn.

Looks come from a few primitives, composed:

- Liquid orbs are large rounded rects with `AngularVelocity` turning inside a round `ClipChildren`. A `Position` transition moves the level, and their corners are the waves.
- Hollow rings are circles with a black fill and a `Border`, drawn with `BlendMode: Additive`, so the fill adds nothing.
- Glow is `Emissive` above the threshold of a `Bloom` on the view of the UI. Soft halos are one additive image.
- Fill bars are a rect whose `Scale` and `Position` transition together, which needs no new layout.
- The 3D relic is a scene rendered to a texture with a transparent clear color, shown by an `Image`.

Motion runs on the render thread through `Transition`. A value that has to start from somewhere, such as a shockwave or a cooldown wipe, is a `OneShot`: a template that is created with its start values when an `if` becomes true and sets `go` two frames later, so its transitions run to the end values. Nothing about it needs cleaning up. Its delays stage a sequence: the comet takes 0.44 s, and the star, the shockwave and the particle burst of the same `if` wait 0.42 s.

The widgets of Rites inherit the headless behavior of `box.widgets` (`RuneSlider : HeadlessSlider`, `RuneToggle : HeadlessToggle`) and only draw. The Codex is the stock `ScrollView`, styled by a `UiTheme`.

## Views

Views render in order: `sky` (nebula and astrolabe), `dust` (motes), `shade` (vignette), `ui`, `fx` (spell effects). Particles get views of their own because a view draws them before its 2D shapes, which would cover them. The sparks of a star are the exception: the chart has nothing opaque behind it.

## Files

`state.flecs` holds the state and the rules as functions, `assets.flecs` the fonts and textures. `kit.flecs` has the shared parts: the virtual screen, `OneShot`, `Frame` and the bursts of light. `sky`, `title`, `hud`, `atlas`, `relic`, `armory`, `rites`, `fx` and `ui` are the views and pages. The `logic` files run the game.

`assets/generate.py` writes every texture and icon in `assets`. It needs numpy and Pillow. Cinzel and Spectral are in `assets/fonts` under the SIL Open Font License.

## Rules of the script

These are what shaped the structure, and where a change is most likely to go wrong:

- The async blocks of a template restart when anything it reads changes, other than its own `mut` variables, and an event during a restart is lost. So a template that takes input only writes globals, and hands its look to a face template (`Star` and `StarFace`, `SpellSlot` and `SpellFace`) that reads them. Keep enter and leave in loops of their own.
- Game logic runs in async blocks at the root of a script, which do not restart. `rq` carries the requests of the widgets to them. A request holds one operation, so widgets that act in the same frame, such as the items equipped at the start, write the state directly.
- A script takes at most 64 reactive inputs, which is why the logic is split over several files.
- A template is defined above the templates that use it. A forward reference can keep the scene from loading without an error.
- A `DirectionNone` parent counts the positions of its children in its fit size, so the parent of offset shapes needs a fixed size. In a row or column a child with a shape takes part in the flow, also with a `Position`.
- A sized child of a `DirectionNone` layout anchors to the top left unless it sets `h` and `v`, whatever its `Position` says.
- An element without a `Position` does not follow its parent. A child of a `DirectionNone` layout needs a `Position` or a `LayoutItem`.
- An `entity` variable cannot be assigned 0: keep a separate flag, as `armory_hovering` does.
