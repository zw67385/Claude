# Handoff: Ironbark Lookout DS port

Branch: `claude/vigilant-ritchie-vkzmfi` (repo `zw67385/claude`). Working dir: `/home/user/Claude`.
User rules: do everything yourself, **no subagents**; port the whole episode (story, text, systems, layout) to the DS Lite at ~20 fps, using the hardware fully. Keep chat replies short.

## Honest status
Foundations only. No playable game yet. Nothing is in the ROM except the Makefile skeleton. Done so far:
toolchain proven, story text and dialogue extracted exactly, scene/mesh/material readers written, source game studied (partly).
A faithful 1:1 port is not possible (Unity HDRP game vs 4 MB RAM, 2048 polys/frame). The plan is a data-driven reinterpretation that keeps every story beat, text line and system.

## Environment (EPHEMERAL: rebuild if the container was reset)
1. Assets: `curl -L -o /tmp/ironbark.rar https://sendit.sh/sxLjD/EFGjc.rar` (**capital D** in `sxLjD`; `sxLjd` is 404). 2.77 GB.
2. Extract with official unrar (7z fails, "Unsupported Method"): download `https://www.rarlab.com/rar/rarlinux-x64-712.tar.gz` to /tmp, untar, then `cd /tmp/ib && /tmp/rar/unrar x -y -idq /tmp/ironbark.rar`. 4 files fail on Cyrillic name collisions (harmless). Result: `/tmp/ib/{FearsToFathomAssets,R4CardFilesStuff,nitro-engine-0.15.4}`.
3. Toolchain (github.com and pkg.devkitpro.org are blocked, wonderful.asie.pl and blocksds.skylyrac.net work):
   `curl -L https://wonderful.asie.pl/bootstrap/wf-bootstrap-x86_64.tar.gz | tar xz -C /opt/wonderful` (mkdir first), then
   `export PATH=/opt/wonderful/bin:$PATH; wf-pacman -Syu --noconfirm wf-tools; wf-config repo enable blocksds; wf-pacman -Syu --noconfirm; wf-pacman -S --noconfirm blocksds-toolchain`.
   Build env: `WONDERFUL_TOOLCHAIN=/opt/wonderful BLOCKSDS=/opt/wonderful/thirdparty/blocksds/core`.
4. Python venv: `python3 -m venv /tmp/venv && /tmp/venv/bin/pip install pyyaml numpy pillow pyfqmr meshoptimizer scipy` (needs libyaml-enabled PyYAML; system one is not).
5. Emulator: `apt-get update && apt-get install -y desmume xdotool imagemagick xvfb`; binary is `/usr/games/desmume-cli`. Headless test that worked:
   `xvfb-run -a -s "-screen 0 800x900x24" bash -c 'LIBGL_ALWAYS_SOFTWARE=1 desmume-cli X.nds & sleep 8; import -window root shot.png'`. Verified: a Nitro Engine ROM builds and boots, 3D renders (software rasterizer). Not yet verified: NitroFS reads, input injection via xdotool, audio.
6. Tool paths default to `/tmp/ib/FearsToFathomAssets/assets2/Assets` (env `F2F_ASSETS`), caches in `/tmp/f2f_cache`.

## Repo contents
- `DESIGN.md`: architecture, hardware budget, controls, phases (read first).
- `data/text_en.json` (6 tables: Dialogue 968, ep4_subs 128, ep4_chats 89, ep4_intro 62, ep3_controls 61, ep4_smiley 15), `data/yarn.json` (64 nodes, decoded Yarn Spinner 2 protobuf; ops RUN_LINE/ADD_OPTION/SHOW_OPTIONS/JUMP_TO/JUMP/RUN_COMMAND/PUSH_*/CALL_FUNC/JUMP_IF_FALSE/RUN_NODE etc.), `data/story_dump.txt` (readable script).
- `tools/`: `extract_text.py`, `yarn_dis.py`, `yarn_dump.py`, `unity.py` (scene parser: `Scene(name)` with world transforms, `guid_map()`), `meshio.py` (Unity mesh reader; channel `dimension & 0xF` fix applied), `matio.py` (material reader), `gx.py` (DS display-list encoder, untested on hardware), `survey.py` (geometry/texture survey).
- `third_party/nitro-engine/` vendored (source, include, Makefile.blocksds). `rom/Makefile` compiles NE sources directly (`NE := ../third_party/nitro-engine`, `-DNE_BLOCKSDS`, `NITROFSDIR := nitrofs`); `rom/source/` and `rom/nitrofs/` are empty. Makefile may still need `-lfilesystem`-style libs checked (NitroFS is in libnds9) and maxmod (`-lmm9`) added.

## Source game facts
- Build order: Disclaimer, Gamma Correction, Main Menu, EP4 Intro, First Scene (RV drive + diner), Trail Start (ranger Billy), Watchtower (Seq 1-8), Zombie Game (PC minigame, loaded additively), Camping, Seq 4 Intro, Trail End, Credits. Menu chapters: New Game, Billy (Trail Start), First Night (Seq1), Smoke in the Woods (Seq3), Seq4, Something Strange (Seq5), Lost Hiker (Seq6), Abandoned (Seq8).
- `Scripts/Assembly-CSharp/WatchTowerManager.cs` (6787 lines) is the story state machine. Read so far: fields, Awake/Start/Update, all `SeqNStart` (lines 1217-2134: each resets lights, fog, skybox, props, player pos, audio per sequence), `SeqAllUpdate` and Seq3-8 Update (2136-3430: interactions by raycast: generator, door, light switch, stove/wood, bed/sleep gating, radio, computer, thermometer, cooking, cultist jumpscare logic, Seq8 areas A-K, mic shout under bed). **Not yet read**: 3432-6787 (coroutines, OpenDoor etc., radio, conversations, hunts/jumpscares, Seq6 Billy/BadGuy, Seq7 power/rain, Seq8 ending), plus GameManager, UIManager (fades), ComputerManager (1605), ConsoleManager, MinigameManager, Phone, Radio, Binoculars(+Seq6), Anemometer, CampfireManager, Cultist*/BadGuy*/ParkRanger*, DinerManager etc.
- Yarn nodes for this episode: Campfire, Radiostart, Alone, Paradise, Fire, Stove, Firewood, Smoke, Report*, Unintellegible, ConnorCheck, Nothing, Seq3Start, Campers, Confirm, CheckCampers, Seq4*, Seq5Start, Seq6*, Hiker1/2, Anxious, Seq7*, Seq8*, TrailStart, Ranger*; Diner/SuitGuy/Trucker/ParkingLotGuy/Bad_Guy nodes belong to First Scene. Line text = `text_en.json["Dialogue"]["line:xxxxxxxx"]`; subtitles via `F2FLocalizedText.GetLocalizedText(table, key)`.
- Scenes are flattened YAML (no prefabs). Watchtower: 5418 objects; roots incl. Tower (584 meshes, cabin interior about x 312-318, y 43.7-46.5, z 324-330, plus LookoutTower structure 74 meshes), Outside Tower (233), Seq 3/5/6/7/8 groups, Terrain, Navigation (inactive). 150 of Tower's mesh objects start inactive (toggled by sequence code); 39 mesh guids do not resolve.
- **Static batching**: renderers with `m_StaticBatchInfo.subMeshCount>0` draw submeshes [firstSubMesh, +count) of a "Combined Mesh" whose vertices are already in **world space** (verified). Do not apply the object transform to them.
- Unity to DS: flip Z, keep triangle winding; `t = 1 - v`. Textures are huge (mostly 2K-4K PNG, about 118 unique for the Tower): downsample to 32-128 px, palettize.
- Materials: `matio.load(guid)` gives tex guid, tint, tiling; cutout/glass detection still to do.

## Planned pipeline (next steps, in order)
1. `tools/texconv.py`: PNG to DS format (256-color pal for detail, 16-color small, A3I5/A5I3 for alpha), power-of-2, output blob; runtime loads with `NE_MaterialTexLoad` + `NE_MaterialTexLoadPal` (see NETexture.h; flags `NE_TEXTURE_WRAP_S/T`, `NE_TEXTURE_COLOR0_TRANSPARENT`).
2. `tools/bake_level.py`: collect active renderers (handle batching), convert to DS space, decimate with `meshoptimizer.simplify_with_attributes` (budget: cabin about 4k tris total, about 1.5k visible), model-local coords scaled to |v|<8 for VTX_16 (1.3.12), flat face normals, emit display lists via `gx.py` plus a level table (models, materials, instances with pos/rot/scale, group ids per sequence).
3. C runtime in `rom/source`: NitroFS init, level loader (`glCallList`), free-look camera (D-pad move, face buttons or stylus look), AABB collision from BoxColliders, hardware lights (4 directional; light on/off by changing colors), fog, top screen 3D + bottom screen UI/subtitles, fixed 20 fps (vblank every 3rd), mic for Seq 8.
4. Test in DeSmuME: boot, screenshot, check fps; then Yarn VM + dialogue UI + Main Menu/intro text, then Seq 1 to 8 logic ported from WatchTowerManager, then the other scenes, audio (mmutil/maxmod, ADPCM), zombie minigame, credits.
5. Commit and push often to the branch; do not commit `/tmp` assets, `*.nds`, or generated bulk data unless small.

## Last action / progress (updated)
- `tools/bake_level.py Scene xmin xmax ymin ymax zmin zmax outdir [texsize]` works: handles static batching, decimates per renderer (meshopt simplify + sloppy fallback, budget env BUDGET, default 3500 tris), quantizes textures to 256 colours, writes `level.bin` (`LVL0`, group table + pal/tex/display-list blob). Cabin: `Watchtower 311 319 43.5 47 323 331` gives 2170 tris, 103 groups, 237 KB.
- `rom/source/main.c` (plain libnds, Nitro Engine no longer compiled in): loads `nitro:/tower.bin`, uploads textures, renders with light, D-pad move, stylus/face-button look. Verified in DeSmuME: `docs/slice1_cabin.png` shows a recognisable textured cabin interior. NitroFS reads work.
- Build: `cd rom && WONDERFUL_TOOLCHAIN=/opt/wonderful BLOCKSDS=/opt/wonderful/thirdparty/blocksds/core PATH=/opt/wonderful/bin:$PATH make`; regenerate data with `/tmp/venv/bin/python tools/bake_level.py ... /tmp/bake1 64 && cp /tmp/bake1/level.bin rom/nitrofs/tower.bin`.
- Next: per-sequence object groups (toggle by seq), BoxCollider collision, fps/poly measurement, texture-VRAM budget check, bottom-screen UI, Yarn VM + text, then sequences.

## Progress 2
- `tools/pack_story.py` -> `rom/nitrofs/story.bin` (74 KB, 64 nodes, 843 strings; YAML "yes/no" localized values fixed to strings). `rom/source/yarn.c` = Yarn VM (lines, options, commands, vars, Number/Bool funcs, RUN_NODE). Verified on emulator: story reader plays the Campfire node with real options.
- `rom/source/main.c`: boot menu (Walk the cabin / Read story nodes). Cabin rebaked at BUDGET=2600 (1668 tris, under 2048 cap).
- Emulator input injection WORKS: run desmume under xvfb, `xdotool mousemove 400 300 click 1` to focus, then `/tmp/press.sh Down x x` (keydown/sleep .25/keyup/sleep .5). Keys: x=A z=B s=X a=Y arrows, Return=Start. (Recreate press.sh if /tmp reset.) GFX poly/vert RAM counters read 0 in DeSmuME, so poly count must be computed offline.
- Next: bottom-screen proper UI (replace console), commands handler (RUN_COMMAND names: PlaySFX1, SetupName, ExitRadio, VO_*...), sequence state machine, collision, per-sequence prop groups, audio.
