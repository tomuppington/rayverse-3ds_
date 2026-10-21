# Rayverse 3DS

A work-in-progress port of [Rayverse](https://github.com/Falcury/rayverse) — the open-source modern port of
**Rayman 1 (PC, version 1.21)** — to the Nintendo 3DS (targeting the New Nintendo 3DS XL), as homebrew.

**No game data is included.** You need your own legally obtained copy of Rayman 1 for PC (e.g. *Rayman Forever* on GOG).
The PlayStation version's data is a different format and does not work.

## Installing

1. Download `rayverse3ds.3dsx` from the latest successful run under the **Actions** tab, and copy it to `sdmc:/3ds/`.
2. Copy the contents of your Rayman 1 PC game folder to `sdmc:/3ds/rayverse/`
   (so `sdmc:/3ds/rayverse/RAY.LNG`, `sdmc:/3ds/rayverse/PCMAP/` etc. exist; add the `Music` folder for music).
   Optional: a `MusicPS1` folder of OGG files converted from your own PlayStation disc switches to the PS1
   soundtrack (per-level music, death and level-complete music). Remove/rename the folder to use the PC music.
3. Sound needs `sdmc:/3ds/dspfirm.cdc` (create it once with the DSP1 homebrew).
4. Start **Rayverse 3DS** from the Homebrew Launcher.

## Controls

D-pad / Circle Pad: move · B: jump · Y: punch · X: action · Down+Left/Right: crawl · A: confirm ·
START: menu · SELECT: pause · tap touch screen: change picture size · hold L+R+SELECT: quit

## Status

Early: the game code compiles for the 3DS; gameplay has not yet been tested on hardware.

## Credits and licence

Rayverse is by Falcury and collaborators (see the original README credits: RayCarrot, fuerchter and others), released
under CC0 1.0 (see `LICENSE`). Rayverse source in `src/` is taken from upstream commit `57625f8`; 3DS-specific code is in
`src/ctr_main.c` and `src/ctr_sound.c`, with small `__3DS__` additions to `rayverse.c`, `types.h`, `proto.h`,
`engine.c` and `common.h`. Rayman is a trademark of Ubisoft; this project is not affiliated with Ubisoft.
