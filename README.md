# Zelda 3 Randomizer

An **in-binary item randomizer** for *The Legend of Zelda: A Link to the Past*,
built on top of [snesrev/zelda3](https://github.com/snesrev/zelda3) — the
reverse-engineered C reimplementation of the full game.

The randomizer ships **inside the same `zelda3` executable** as the vanilla
port. There is no separate patcher and no `.sfc` to distribute: you build the
game once, enable the randomizer per save slot, and play. Seed generation
(logic, placement, spoilers) runs natively in C; placement is deterministic and
reproducible from a short share string.

Our Discord server is: https://discord.gg/AJJbJAzNNJ

## What this is (and what it's built on)

This project stands on two bodies of prior work, and would not exist without
either of them:

- **[snesrev/zelda3](https://github.com/snesrev/zelda3)** — the ~70–80 kLOC C
  reimplementation of ALTTP that this is a fork of. It reimplements the entire
  game (playable start to finish) and uses the PPU/DSP from
  [LakeSnes](https://github.com/elzo-d/LakeSnes) for rendering and for an
  optional side-by-side RAM-compare verification mode. The original authors also
  drew heavily on spannerism's Zelda 3 JP disassembly and other community
  documentation of the game's functions and RAM map.

- **ALTTPR (`alttp_vt_randomizer`)** — the long-running community Link to the
  Past randomizer. **Our randomizer logic is hand-translated from ALTTPR**: the
  region/location graph (`assets/rando/logic.yaml` and
  `assets/rando/logic_parts/`), the named logic macros
  (`assets/rando/macros.yaml`), the canonical location/region/prize naming, and
  the assumed-fill placement approach all derive from the upstream MIT-licensed
  PHP randomizer. Every translated predicate carries an inline
  `source: app/<path>:<line-range>` citation back to the upstream source.
  Full attribution and licensing are in [`NOTICE`](NOTICE).

ALTTPR is MIT-licensed; so is snesrev/zelda3; so is this project. See
[`LICENSE.txt`](LICENSE.txt) and [`NOTICE`](NOTICE) for the complete set of
upstream copyright notices we preserve.

> You need a copy of the original US ROM only to **extract game resources**
> (levels, images, music) into `zelda3_assets.dat`. After that one-time step the
> ROM is no longer needed — the game and the randomizer run entirely from the
> extracted assets.

## Randomizer

The randomizer activates **per save slot** from the file-select screen. On
PC, seeds are configured in a dedicated native settings window (see below); a
headless CLI is available for automation and for the regression test corpus.

`kGeneratorVersion` in [`src/rando/rando.h`](src/rando/rando.h) is the
authoritative marker of the live placement/serialization format — it advances
whenever a placement-affecting change ships. The
[`docs/randomizer.md`](docs/randomizer.md) reference and the OpenSpec changes
under [`openspec/changes/`](openspec/changes/) (start at its `README.md` index)
are the source of truth for current scope and per-feature status.

Capabilities that have landed include:

- **World states**: Open, Standard, Inverted, and Retro.
- **Goals**: `ganon`, `fast_ganon`, `dungeons`, `pedestal`, `triforce-hunt`,
  `ganonhunt`, and `completionist`, with configurable crystal/tower
  requirements and Triforce-piece counts.
- **Shuffles**: prize shuffle, medallion shuffle, multi-mode entrance shuffle
  (caves / dungeons / Ganon's-Tower / crossed), and experimental boss / enemy-
  drop shuffles.
- **Item / logic options**: accessibility tiers (items / locations / beatable),
  swordless mode, progressive item handling, trick-logic difficulty tiers, and
  a Retro mode (shops, take-anys, rupee bow, wild small keys).
- **Hints**: telepathic-tile hints plus fork hint NPCs.
- **Spoilers**: JSON + text spoilers with sphere breakdown, region-grouped
  placements, and fallback-warning rollup.
- **Race mode**: on-disk spoiler suppression at generate time, with a verified
  `--reveal-spoiler` flow that regenerates placement and checks a SHA-256 stamp.
- **Deterministic, reproducible placement** with a share-string format, a
  sidecar save format, and a cross-platform (Linux/macOS) regression corpus
  wired into CI.

### Generating a seed from the command line

The CLI generation mode is the headless entry point (it's also what the
regression corpus drives). It needs the extracted `zelda3_assets.dat`:

```sh
./zelda3 --generate-seed \
  --settings=mode.state=open,goal=fast_ganon,crystals.ganon=7,crystals.tower=7 \
  --seed=0xDEADBEEFCAFEBABE \
  --out-spoiler=./spoilers/demo.json
```

| Flag | Effect |
|---|---|
| `--settings=k=v,...` | Comma-separated settings overrides (full key reference in [`docs/randomizer.md`](docs/randomizer.md)). |
| `--seed=0x...` | uint64 seed. |
| `--out-spoiler=<path>` | JSON spoiler output path (also writes a sibling `.txt`). |
| `--out-share-string=<path>` | Optional file for the base32 share string. |
| `--budget-seconds=<n>` | Bound the placement retry budget (default 0). |
| `--shape-filter=<tokens>` | Search forward from `--seed` until the spoiler/sphere shape matches comma-separated tokens such as `short`, `long`, `early_boots`, `max_sphere=4`, or `item:Hookshot<=3`. |
| `--shape-search-limit=<n>` | Maximum candidate seeds to try for `--shape-filter` (default 100 when a filter is present). |
| `--assets-must-be-vanilla` | Refuse a non-vanilla `zelda3_assets.dat`. |
| `--allow-broken-seed` | Skip the goal-completability refusal (diagnostic). |
| `--race-mode` | Suppress the on-disk spoiler at generate time. |
| `--reveal-spoiler=<path>` | Reveal a previously-suppressed race spoiler (regenerate, verify SHA-256 stamp, write full JSON). |

Shape filters are generator-side search constraints, not settings. The accepted
candidate seed is written into the share string/spoiler, so the result remains
reproducible without the original filter text.

Determinism self-tests (no ROM/assets required — they run before asset load):

```sh
./zelda3 --rando-selftest
```

For contributors: the regression corpus, the logic-VM benchmark
(`./zelda3 --rando-bench-logic`), the init-order replay guard
(`assets/scripts/check_init_order.py`), and a set of pure-Python source guards
(`assets/scripts/check_*.py`) run in CI. See
[`docs/randomizer.md`](docs/randomizer.md) ("Source-level CI guards") for the
full list and the generator-version bump policy.

### Native game-settings window (PC)

On Windows/Linux/macOS, press `` ` `` (backquote) — or whatever you bind
`OpenSettings` to under `[KeyMap]` — to open the **Z3R Settings** window. It
owns the randomizer settings UI on PC and also configures the game without
hand-editing `zelda3.ini`:

- **Game Settings** — rebind keyboard/controller, window scale, fullscreen,
  renderer, widescreen, audio device/MSU, and the `[Features]` gameplay toggles.
- **Randomizer** — configure and generate a playable seed slot. The `Seed Tools`
  tab groups generation-time tools such as Customizer manifests and the same
  generator-side search filters as `--shape-filter`; while a shape filter is
  active, the share string is shown after generation because the accepted seed
  may differ from the starting seed.

Click **Apply** to save. Bindings and gameplay toggles take effect immediately;
options marked *(restart)* are written to the INI and apply on next launch.
Apply rewrites only the keys it manages in your loaded INI (`zelda3.user.ini` if
present, else `zelda3.ini`), preserving comments; a one-time `.bak` is made
before the first rewrite.

### `[Randomizer]` INI section

```ini
[Randomizer]
; Bitmask of kFeatures1_* (src/features.h). 1 = kFeatures1_RandomizerActive.
; Per-slot rather than global once a slot is active; this is the global default.
Features1 = 0

; Directory where the JSON / text spoilers land. Default: ./spoilers/
SpoilerDir = ./spoilers

; Race-mode default for new slots. When true, the spoiler is stamped but
; suppressed from the in-game tracker.
RaceMode = false

; Developer-only override (see docs/randomizer.md "RAM-compare safety"). When
; true AND a rando slot is active, the dual-runtime RAM compare keeps running
; (expect spew). Default false.
DebugForceRamCompare = false
```

### Randomizer keybindings

Bind these in `zelda3.ini` under `[KeyMap]`. Default: unbound (overlays hidden).
Both toggles reset to hidden on each launch and on slot deactivate.

| Key id | Action |
| ---- | ------ |
| RandoToggleItemTracker | Show/hide the in-game item-tracker overlay |
| RandoToggleLocationTracker | Show/hide the in-game location-tracker overlay |

Full reference: [`docs/randomizer.md`](docs/randomizer.md).

## Additional (non-randomizer) features

A number of quality-of-life features inherited from / added on top of the
upstream reimplementation are also available:

- Pixel-shader support.
- Enhanced aspect ratios (16:9 or 16:10).
- Higher-quality world map.
- MSU audio-track support.
- Secondary item slot on button X (hold X in the inventory to select).
- Switching the current item with the L/R keys.

These are configured via the `[Features]` section of `zelda3.ini` (see
`src/features.h` for the full `kFeatures0_*` bitfield) or the native settings
window.

## How to play

**Option 1 — Launcher** by RadzPrower (Windows only):
https://github.com/ajohns6/Zelda-3-Launcher

**Option 2 — Build it yourself** (see below). The upstream build wiki is also a
useful reference: https://github.com/snesrev/zelda3/wiki

## Asset extraction (run once, before any build)

Place the USA ROM named `zelda3.sfc` (US region, SHA256
`66871d66be19ad2c34c927d6b14cd8eb6fc3181965b6e517cb361f7316009cfb`) in the repo
root, then:

- **Windows**: double-click `extract_assets.bat`
- **Any platform**: `python assets/restool.py --extract-from-rom`

This creates `zelda3_assets.dat`. Both the `zelda3` executable and
`zelda3_assets.dat` must sit next to each other for the game to run.

## Installing Python & libraries on Windows (required for asset extraction)
1. Download [Python](https://www.python.org/ftp/python/3.11.1/python-3.11.1-amd64.exe) and install with "Add to PATH" checked.
2. Open the command prompt.
3. Run `python -m pip install --upgrade pip pillow pyyaml`.
4. Close the command prompt.

## Compiling on Windows with TCC (1 MB Tiny C Compiler)
1. Download the project ("Code > Download ZIP" on GitHub) and extract it.
2. Place the USA ROM named `zelda3.sfc` in the root directory.
3. Double-click `extract_assets.bat` to create `zelda3_assets.dat`.
4. Download [TCC](https://github.com/FitzRoyX/tinycc/releases/download/tcc_20221020/tcc_20221020.zip) and extract to `third_party/`.
5. Download [SDL2](https://github.com/libsdl-org/SDL/releases/download/release-2.26.3/SDL2-devel-2.26.3-VC.zip) and extract to `third_party/`.
6. Double-click `run_with_tcc.bat` to create `zelda3.exe`.
7. Configure via `zelda3.ini` in the main dir.

## Compiling on Windows with Visual Studio (4.5 GB IDE and compiler)
Same asset-extraction steps as above, then:
1. Double-click `Zelda3.sln`.
2. Install the **Desktop development with C++** workload with the VS Installer if you don't have it (it should prompt you).
3. Change "debug" to "release" in the top dropdown.
4. Choose "Build > Build Zelda3" to create `zelda3.exe` in the `bin/<platform>-<config>` subfolder (e.g. `bin/x64-Release/`).
5. Configure via `zelda3.ini` in the main dir.

## Installing libraries on Linux/macOS
1. Open a terminal.
2. Install pip if needed: `python3 -m ensurepip`.
3. Clone the repo and `cd` into it:
   ```sh
   git clone https://github.com/snesrev/zelda3
   cd zelda3
   ```
4. Install requirements: `python3 -m pip install -r requirements.txt`.
   > **Tip:** if you have [uv](https://docs.astral.sh/uv/), you can skip this
   > step. The `Makefile` auto-detects `uv` and runs the asset / codegen tooling
   > in an isolated, auto-provisioned environment (deps from `requirements.txt`)
   > — no manual `pip install` or venv needed. It falls back to system `python3`
   > when `uv` isn't installed. (Windows builds use `python` directly and are
   > unaffected.)
5. Install SDL2:
   * Ubuntu/Debian: `sudo apt install libsdl2-dev`
   * Fedora: `sudo dnf install SDL2-devel`
   * Arch: `sudo pacman -S sdl2`
   * macOS: `brew install sdl2` (homebrew [here](https://brew.sh/))

## Compiling on Linux/macOS
1. Place the US ROM named `zelda3.sfc` in the repo root.
2. Build:
   ```sh
   make
   ```
   <details>
   <summary>Advanced make usage …</summary>

   ```sh
   make -j$(nproc) # use all cores
   make clean all  # clear gen+obj and rebuild
   CC=clang make   # specify compiler
   ```
   </details>

## Nintendo Switch

You need [DevKitPro](https://devkitpro.org/wiki/Getting_Started) and
[Atmosphere](https://github.com/Atmosphere-NX/Atmosphere) installed.

```sh
(dkp-)pacman -S git switch-dev switch-sdl2 switch-tools
cd src/platform/switch
make # add -j$(nproc) to build using all cores (optional)
nxlink -s zelda3.nro # test directly on the switch (optional)
```

## More compilation help

See the upstream wiki: https://github.com/snesrev/zelda3/wiki

The ROM must be named `zelda3.sfc`, US region, with SHA256
`66871d66be19ad2c34c927d6b14cd8eb6fc3181965b6e517cb361f7316009cfb`. If you move
the executable, bring `zelda3_assets.dat` with it.

## Usage and controls

The game supports snapshots. The joypad input history is saved in the snapshot,
so a playthrough can be replayed in turbo mode to verify the game behaves
correctly. Running `./zelda3` with an optional path to the ROM file verifies,
each frame, that the C code matches the original behavior (side-by-side mode).

| Button | Key         |
| ------ | ----------- |
| Up     | Up arrow    |
| Down   | Down arrow  |
| Left   | Left arrow  |
| Right  | Right arrow |
| Start  | Enter       |
| Select | Right shift |
| A      | X           |
| B      | Z           |
| X      | S           |
| Y      | A           |
| L      | C           |
| R      | V           |

Keys are reconfigurable in `zelda3.ini` (or the native settings window).

Additional commands:

| Key | Action                |
| --- | --------------------- |
| Tab | Turbo mode |
| W   | Fill health/magic     |
| Shift+W   | Fill rupees/bombs/arrows     |
| Ctrl+E | Reset            |
| P   | Pause (with dim)                |
| Shift+P   | Pause (without dim)                |
| Ctrl+Up   | Increase window size                |
| Ctrl+Down   | Decrease window size                |
| T   | Toggle replay turbo mode  |
| O   | Set dungeon key to 1  |
| K   | Clear all input history from the joypad log  |
| L   | Stop replaying a snapshot  |
| R   | Toggle between fast and slow renderer |
| F   | Display renderer performance |
| F1-F10 | Load snapshot      |
| Alt+Enter | Toggle fullscreen     |
| Shift+F1-F10 | Save snapshot |
| Ctrl+F1-F10 | Replay the snapshot |
| 1-9 | Load a dungeons playthrough snapshot |
| Ctrl+1-9 | Run a dungeons playthrough in turbo mode |
| ` (backquote) | Open the native game-settings window (PC; configurable as `OpenSettings`) |
| F12 | Dump developer debug state — g_ram/VRAM/OAM/CGRAM + hint state + a log line (configurable as `DumpDebugState`; clear its binding to disable, or trigger it from the Debug tab) |

## Credits & license

- **snesrev / elzo_d** — the zelda3 reimplementation and the LakeSnes PPU/DSP
  core this is forked from (MIT).
- **ALTTPR (`alttp_vt_randomizer`), © 2016 sporchia and contributors** — the
  randomizer logic, macros, naming, and placement approach our generator is
  hand-translated from (MIT).
- spannerism and the wider community for the JP disassembly and the function /
  RAM-map documentation the reimplementation relied on.
- Opus, SHA-256, and the other vendored third-party components — see
  [`LICENSE.txt`](LICENSE.txt) and [`NOTICE`](NOTICE).

This project is licensed under the MIT license. See [`LICENSE.txt`](LICENSE.txt)
for the full text and [`NOTICE`](NOTICE) for the upstream copyright notices we
preserve.
