# fo4roulette: project handoff

**Real Roulette** (repo `evodube/fo4roulette`): a playable roulette table for Fallout 4, F4SE plugin, OG 1.10.163.
Built on Playable Pool's foundations (`evodube/fo4pool`): its free camera hooks, Present hook, Papyrus quest pattern
for walking NPCs, HUD fonts, XAudio2 sounds, settings menu and CI.

## Where things live

- `main` is built by CI (Windows: xmake + Dear-Modding CommonLibF4 at ef995295; Caprica for the scripts; the ESL
  from `tools/make_esl.py`). Each push to `main` publishes the `Data` folder to the `builds` branch.
- Meshes and textures are in `dist/Data` (the model is CC BY 4.0: credit it). They're made on the user's PC:
  - model: `C:\Users\Dan\Desktop\table\model\RuletMasası.obj` + `textures\` (Sketchfab "Roulette Table 2" by Dudzy, CC BY 4.0)
  - `C:\Users\Dan\Desktop\table\work\build_meshes.py` (= `tools/build_meshes.py`), run in Blender 5.2 with PyNifly:
    writes `work\Data\Meshes\fo4roulette\*.nif`. Uses vanilla `PoolTable01.nif` (extracted to `work\`) only as a
    shader template.
  - textures: `python tools/make_textures.py <maps folder> <Data folder>` (cloud session or anywhere with Pillow).
  - the full mod = `builds` branch `Data` + those meshes and textures.

## The table model, in game units

Scale 6.6 about model point (12.005, 3.755, -8.22): origin = footprint centre on the floor. Length along X (wheel at
-X), players' side -Y. Wheel centre (-79.23, -24.78). All the numbers are in `src/rl/Table.h`. The NIF has NiNodes
`RotorEU` / `RotorUS` (disc + turret, origin on the wheel axis: the plugin sets their rotation) and shapes
`LayoutEU` / `LayoutUS`; the plugin hides the pair not in use. One convex collision prism (compound collision built
from scratch crashes the game, per PyNifly). BSXFlags Havok | Articulated.

Pocket k of the wheel's sequence is centred at 90 - k*360/N degrees in the rotor's frame (0 at +Y, clockwise from
above): `make_textures.py` paints it so and `rl::PocketAngle` assumes it.

## Controls (defaults)

E (activate) at the table: sit down. Mouse moves the chip; LMB bet, RMB take back; 1-4 chip; E spin; B rebet;
C clear; V wheel/layout view; X get up. Numpad 1 at a settler: dealer for the nearest table (again: stops).

## Next ideas

- Dealer animations (spin the wheel, sweep chips) via the pool's Blender + PyNifly HKX pipeline and dynamic idles.
- NPCs reacting to wins and losses; the house's take going to the settlement.
- Chip colours per player instead of offsets.
