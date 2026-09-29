# Fears to Fathom: Ironbark Lookout for Nintendo DS Lite

A port of Episode 4 (12 Unity scenes, 8 story sequences) to the Nintendo DS Lite, built with
BlocksDS + Nitro Engine and packaged as a single `.nds` (NitroFS inside the ROM, DLDI-patched for the R4).

## Source material (kept out of git, see `tools/README.md`)

| Unity scene | Role |
|---|---|
| Disclaimer, Gamma Correction | boot screens |
| Main Menu | New Game + chapter select (First Night, Smoke in the Woods, Seq 4, Something Strange, Lost Hiker, Abandoned, Billy/Trail Start) |
| EP4 Intro, Seq 4 Intro | typed story text |
| First Scene | drive the RV to the diner, diner conversations |
| Trail Start | meet the ranger (Billy), keys, flashlight |
| Watchtower | Seq 1-8: the tower, radio, phone, computer, cooking, cultists |
| Camping | campfire scene |
| Zombie Game | minigame played on the tower computer |
| Trail End, Credits Scene | ending and credits |

Story data (extracted, exact): `data/text_en.json` (1,323 strings), `data/yarn.json` (64 Yarn nodes, 3,594
instructions, decoded from the compiled protobuf), `data/story_dump.txt` (readable script).

## Hardware budget and consequences

| Resource | DS Lite | Consequence |
|---|---|---|
| ARM9 | 67 MHz, no FPU | fixed-point (f32) everywhere, no per-frame float math |
| RAM | 4 MB | stream everything from NitroFS; one scene resident at a time |
| 3D | 2048 polys / 6144 verts per frame, 512 KB texture VRAM | offline decimation, per-cell visibility, fog to hide draw distance |
| Screens | 2 x 256x192 | top: 3D world. bottom: touch UI (phone, computer, radio, map, inventory) |
| Audio | 16 ch, ADPCM | offline resample to 8-16 kHz ADPCM/8-bit, streaming for long loops |
| Mic | built in | used for the "shout under the bed" mechanic in Seq 8 |
| Frame rate | fixed 20 fps | vblank every 3rd frame; game logic runs on a fixed 1/20 s step |

## Controls (fixed design)

- D-pad: move. Face buttons / stylus drag on bottom screen: look. L: crouch. R: run/zoom.
- A: interact (the crosshair click). B: back / get up (Space in the PC build). X: flashlight/use. Y: throw/drop.
- Touch screen: phone, computer (mouse), radio dial, map. Start: pause. Select: controls hint.

## Software architecture (ARM9, C)

- `source/core`: fixed-point math, camera, collision (AABB from Unity BoxColliders), input, save (SRAM/FAT), timers.
- `source/gfx`: Nitro Engine scene loader, model/texture streaming, fade, fog, particle/billboard effects, 2D UI.
- `source/story`: a small bytecode VM for the Yarn nodes (RUN_LINE, options, commands, jumps) plus the
  sequence state machine translated from `WatchTowerManager`.
- `source/scenes`: one module per Unity scene.
- `tools/`: Python asset pipeline (scene -> compact level files, mesh decimation, texture quantization,
  audio conversion, text/yarn packing). Everything the ROM loads is generated; nothing is hand-copied.

## Phases

1. Toolchain and emulator test loop (done).
2. Story data and Yarn VM with text UI, dialogue, subtitles, chapter select.
3. Asset pipeline: tower interior/exterior, terrain, props.
4. Sequences 1-8 gameplay, jumpscares, radio/phone/computer, cooking, cultists.
5. Remaining scenes (diner, trail, camping, credits, zombie minigame).
6. Audio, polish, performance to a locked 20 fps, full playthrough tests in the emulator.
