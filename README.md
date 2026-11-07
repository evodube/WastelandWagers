# Real Roulette (fo4roulette)

A playable roulette table for Fallout 4, as an F4SE plugin: build the table in a settlement, sit down at it, put
chips on the layout from a top-down camera and spin. The rotor turns, the ball runs round the track, rattles over the
diamonds and frets and drops into a pocket. The wheel spins itself 15 seconds after the round's first bet; settlers
wander over and bet with their own caps, cheering or shaking their heads at the result.

**Status: 0.3.2.**

## Requirements

- Fallout 4 1.10.163 ("Old Gen"; also built for Next-Gen and Anniversary).
- F4SE and Address Library for F4SE Plugins.
- [F4SE Menu Framework](https://www.nexusmods.com/fallout4/mods/105090) for the HUD and the settings pages (the game
  works without it; results then come as corner messages).

## Playing

1. **Get a table**: build one in a settlement (workshop menu, Furniture > Tables > Roulette Table), or use a
   **Roulette Table Kit** anywhere: Vadim sells them at the Dugout Inn, and you can make one at a chem station
   (Utility). Use the kit from the Pip-Boy, aim where the table should go, Left/Right to turn it, activate to set it
   down (X puts the kit away). Sitting at a kit table, **P** packs it back into the kit. Up to two tables near you
   are in play at once.
2. **Sit down**: activate it (E). The camera goes over the layout.
   - **Move the mouse** to move the chip over the layout; it snaps to the bet under it (straight up, split, street,
     corner, six line, trio, first four / top line, dozens, columns, red/black, odd/even, 1-18/19-36). The HUD shows
     the bet, what it pays and what's on it.
   - **Left button**: put a chip down (your caps go on the table). **Right button**: take your chips off that bet.
   - **1 2 3 4**: the 1, 5, 25 and 100 cap chips. **B**: the same bets as last round. **C**: all your chips back.
   - The wheel spins **15 seconds after the first bet** of the round (yours or a settler's; the HUD counts down).
     **E** (activate) spins it straight away. The camera goes over the wheel while the ball goes round (**V**
     switches view any time).
   - **X**: get up. Bets on the table stay there and are paid while you're nearby.
3. **Other players**: settlers near the table come over now and then, each to their own spot at the layout (two on each side), and put
   25 to 100 caps a round on it from their own purse, a few rounds each, until they've had enough or run out. While
   they play they look the layout over, reach in to bet, and cheer or shake their heads at the
   result (vanilla idles; `[Bettors] bIdles=0` turns them off). Their bets start the countdown too, so the table
   keeps going with you away from it.

The HUD shows while you're sitting at the table.

Winning bets are paid at the usual odds (35 to 1 down to even money). The result is drawn fairly first, and the ball
is then thrown until it lands there, so every number is exactly as likely as it should be.

### European or American

`[Table] iWheel` (or the Table page in the menu): 0 European (one zero), 1 American (0 and 00, and the 0-00-1-2-3 top
line bet). Every table shows the matching wheel and layout; a table in play switches between rounds.

### Settings

Every setting is in `Data\F4SE\Plugins\fo4roulette.ini` and on the Real Roulette pages in the F4SE Menu Framework
menu (Table, Camera, NPCs, HUD & sound, Keys).

`Documents\My Games\Fallout4\F4SE\fo4roulette.log` records what happened: tables, spins (and how many throws it took
to land on the drawn number), the bettors' walks.

## What's in it

- `fo4roulette.dll` (F4SE plugin), `fo4roulette.esl` (the table, ball and chips, the workshop recipe, the quest that
  walks NPCs to the table), `Scripts\fo4rouletteQuest.pex` and `fo4rouletteGroupAlias.pex`.
- `Meshes\fo4roulette`, `Textures\fo4roulette`: the table (from a Sketchfab model; see Credits), its two rotors and
  layouts (European and American, painted by `tools/make_textures.py`), ball and chips.
- `Sound\fo4roulette`: the ball's roll, hits, small knocks and settling cut from a recording by Maxwell Dufresne (gfxsounds.com), each played as the ball does it; chips and the rotor synthesised by `tools/make_sounds.py`.
- `F4SE\Plugins\fo4roulette\hud`: the HUD's fonts (Chakra Petch, Share Tech Mono; OFL).

## Building

```
git submodule update --init --recursive
xmake f -m releasedbg
xmake build fo4roulette
python tools/make_esl.py Data/fo4roulette.esl
```

GitHub Actions builds the DLL, compiles the scripts, makes the ESL and publishes the `Data` folder to the `builds`
branch on every push to `main`.

The meshes are built in Blender with PyNifly from the table model (`tools/build_meshes.py`, run inside Blender), and
the textures from its maps (`tools/make_textures.py <maps folder> <Data folder>`); the results are in `dist/Data`.

### Tests

The wheel physics and the layout have no game code and are tested on their own:

```
g++ -std=c++20 -O2 -Isrc tests/wheel_test.cpp src/rl/Wheel.cpp -o wheel_test && ./wheel_test
g++ -std=c++20 -O2 -Isrc tests/layout_test.cpp src/rl/Layout.cpp src/rl/Wheel.cpp -o layout_test && ./layout_test
```

## How it works

- `src/rl/Wheel`: the wheel's pockets, and a spin worked out start to finish before it's shown: the ball round the
  track until it slows, down the cone (knocked about by the diamonds), onto the turning rotor, bouncing off the frets
  until it rests. The winning pocket is drawn first; throws are simulated until one lands there.
- `src/rl/Layout`: what bet each point on the felt is, its chips' spot, its payout.
- `src/Game.cpp`: the table in play, its rotor node turned every frame, the ball, the round, the chips (statics
  spawned on the felt). `src/Seat.cpp`: you at the table (the free camera held over the layout, the mouse moving the
  cursor). `src/Npcs.cpp`: the settlers betting.
- Before a save, the chips, ball and markers come off (and your caps on the table go back in your pocket for the
  save); after it they're put back.

## Credits

- Roulette table model: ["Roulette Table 2"](https://sketchfab.com/3d-models/roulette-table-2-downloadable-b1d33eebb4f54b8aa250be2f49e87fbb) by [Dudzy](https://sketchfab.com/Dudzy) on Sketchfab, [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Changed for the game: split into parts, scaled, given collision; the rotor rebuilt from its profile with frets and metal parts, new felt and layout textures (European and American).
- Roulette ball sound: recorded by [Maxwell Dufresne](https://gfxsounds.com/blog/maxwell-dufresne/), [GFXSounds](https://gfxsounds.com) (Standard licence).
- HUD fonts: Chakra Petch and Share Tech Mono (SIL Open Font License).
- CommonLibF4 (Dear-Modding), F4SE, F4SE Menu Framework.

## License

GPL-3.0 (the code). The meshes and textures made from the table model are CC BY 4.0, like the model.
