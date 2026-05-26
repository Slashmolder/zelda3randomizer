# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

This is a fork of [snesrev/zelda3](https://github.com/snesrev/zelda3) — a reverse-engineered C reimplementation of *The Legend of Zelda: A Link to the Past*. About 70-80 kLOC of C reimplements the full game; PPU/DSP/CPU emulation comes from LakeSnes and is used both for rendering and for an optional side-by-side verification mode.

The original US ROM (`zelda3.sfc`, SHA256 `66871d66be19ad2c34c927d6b14cd8eb6fc3181965b6e517cb361f7316009cfb`) is required only for one-time asset extraction; after that the game runs from `zelda3_assets.dat`.

## Build & asset workflow

Two things must exist before the game runs: the `zelda3` executable and `zelda3_assets.dat` next to it.

### Asset extraction (run once, before any build)

Place `zelda3.sfc` (US ROM) in the repo root, then:

- Windows: double-click `extract_assets.bat`
- Any platform: `python assets/restool.py --extract-from-rom`

Python deps: `pip install -r requirements.txt` (Pillow, PyYAML).

`restool.py` is the entry point — it dispatches to `extract_resources.py` and `compile_resources.py` in `assets/`. Useful flags: `--no-build`, `--print-strings`, `--languages de,fr,...`, `--sprites-from-png`.

### Building the game

- **Linux/macOS**: `make` (or `make -j$(nproc)`; `CC=clang make` to swap compiler). `CFLAGS` defaults to `-O2 -Werror`.
- **Windows + Visual Studio**: open `Zelda3.sln`, install the *Desktop development with C++* workload, switch to Release, build.
- **Windows + TCC**: drop TCC into `third_party/tcc/` and SDL2 into `third_party/SDL2-2.26.3/`, then run `run_with_tcc.bat`.
- **Nintendo Switch**: `cd src/platform/switch && make` (requires DevKitPro + `switch-sdl2`).

The Makefile glob-builds `src/*.c snes/*.c` plus `third_party/gl_core/gl_core_3_1.c` and `third_party/opus-1.3.1-stripped/opus_decoder_amalgam.c`. There is no test target — verification is done by running the game itself in side-by-side mode (see below).

### Running

`./zelda3` runs the C reimplementation standalone. `./zelda3 path/to/zelda3.sfc` runs the C code *and* the original ROM through the SNES emulator side-by-side, comparing RAM after every frame. Config lives in `zelda3.ini` (parsed by `src/config.c`); see `README.md` for the full key map (notable: `Tab` turbo, `Shift+F1-F10` save snapshot, `Ctrl+F1-F10` replay snapshot, `R` toggle renderer).

## Architecture

### Top-level layout

- `src/` — the C reimplementation: game logic, audio, rendering glue, platform code.
- `snes/` — LakeSnes-derived SNES emulator (CPU/APU/PPU/DSP/DMA). The reimplementation always uses the PPU and SPC player for output; the rest of the emulator is only active when the original ROM is loaded for verification.
- `assets/` — Python toolchain that extracts resources (sprites, music, dialogue, palettes, levels) from the ROM and packs them into `zelda3_assets.dat`. The build wires this up via the `zelda3_assets.dat` target.
- `third_party/` — vendored SDL2 (Windows), gl_core (OpenGL loader), Opus decoder, stb.
- `other/`, `saves/`, `assets/sprites/` — auxiliary scripts, reference savestates per chapter, sprite-sheet sources.
- `src/platform/{win32,switch}/` — platform-specific shims (resource files, Switch makefile, Windows volume mixer).

### The dual-runtime model (read this before touching game logic)

The single most important architectural fact: the C code is structured so that **the original 65816 machine code can run in parallel against it** for correctness verification. Two pieces enable this:

1. **`src/zelda_rtl.{c,h}` defines `ZeldaEnv g_zenv` and the global `uint8 g_ram[131072]`.** All game state lives in `g_ram`; `src/variables.h` is a huge wall of `#define foo (*(uint8*)(g_ram+0xNN))` macros that name each RAM byte (e.g. `link_x_coord`, `main_module_index`). When you see `link_x_coord = 5;` in `player.c`, it's writing into the shared RAM buffer at a fixed SNES address. This RAM layout matches the original game's RAM map exactly — that's what makes byte-for-byte comparison work.

2. **`src/zelda_cpu_infra.{c,h}` drives the emulated side.** It owns `g_emulated_ram[0x20000]`, a `Snes *g_snes`, and snapshot routines (`MakeSnapshot` / `MakeMySnapshot`) that diff RAM, SRAM, and VRAM between the two runtimes each frame. This file is the only one that links against `snes/`; the rest of the game compiles and runs without the emulator.

**Implication for changes**: if you add behavior to `src/*.c`, the emulated ROM won't perform it, so a side-by-side run will diff and assert. Behavior-changing modifications are gated through the `kFeatures0_*` flags in `src/features.h` (stored at `g_ram[0x64c]`); flags marked "visual fixes that don't affect game behavior but will affect ram compare" (`kFeatures0_WidescreenVisualFixes`) and similar exist specifically so users can opt in/out depending on whether they care about the RAM diff. Keep new enhancements behind a flag if they would diverge from original RAM state.

### Game-code module map (in `src/`)

The reimplementation roughly follows the original ROM's bank structure. Major entry points:

- `main.c` — SDL host: window, input, renderer selection, snapshot keys, audio thread, main loop. Calls `ZeldaRunFrame()`.
- `zelda_rtl.c` — frame driver, HDMA simulation, audio/PPU/DMA glue, save/load orchestration, init.
- `nmi.c` — VBlank / NMI handler equivalent; pumps PPU register updates queued during the frame.
- `player.c`, `player_oam.c` — Link's state machine and sprite assembly.
- `sprite.c`, `sprite_main.c` — enemy/object sprites.
- `ancilla.c` — projectiles, effects, "ancillary" objects (arrows, bombs, etc.).
- `overlord.c` — spawner entities that manage groups of sprites.
- `tagalong.c` — follower NPCs.
- `overworld.c`, `dungeon.c` — area logic.
- `hud.c`, `messaging.c`, `select_file.c`, `attract.c`, `ending.c` — UI/screens.
- `load_gfx.c` — graphics streaming into VRAM.
- `audio.c`, `spc_player.c` — audio mixing; SPC plays SNES sound driver bytecode via the DSP.
- `poly.c`, `tile_detect.c`, `misc.c` — polygon renderer (Triforce/intro), tile collision, misc helpers.
- `opengl.c`, `glsl_shader.c` — optional OpenGL renderer + shader support; selected by `g_config.output_method`.
- `config.c` — INI parser for `zelda3.ini`; defines `kKeys_*` command IDs and key-binding tables.
- `assets.h` — loads `zelda3_assets.dat` and exposes asset pointers/blobs.

### Renderer/audio pipeline

`ZeldaRunFrame()` (in `zelda_rtl.c`) executes one logical frame of game code, queuing PPU/DMA register writes against `g_zenv.ppu` (a LakeSnes `Ppu`). `ZeldaDrawPpuFrame()` then runs the PPU scanline renderer to produce the RGB buffer. The host (`main.c`) hands that buffer to one of several `RendererFuncs` (SDL software, SDL hardware, OpenGL, OpenGL ES) selected via `g_config.output_method`. Audio runs on an SDL callback thread that pulls samples from `spc_player.c`.

### Enhancement flags

`src/features.h` documents the `kFeatures0_*` bitfield (extended aspect ratio, switch L/R, mirror to dark world, carry more rupees, bug fixes, etc.). These are configured via `zelda3.ini` (`features0 = ...`) and read at runtime through the `enhanced_features0` macro. When adding optional behavior, prefer a new flag here over an unconditional change.

## Things to know before editing

- **Don't introduce new global state with arbitrary offsets into `g_ram`.** Reuse the addresses already documented in `variables.h`, or add to the "unused RAM repurposed for compat" block at the top of `features.h` (`kRam_*` enum).
- **Asset format changes require both ends.** Adding a new resource means updating `assets/extract_resources.py` and `assets/compile_resources.py` *and* the C-side reader in `assets.h` / `load_gfx.c`. Bump or invalidate any cached `zelda3_assets.dat`.
- **`-Werror` is on.** The CI matrix builds on Linux and macOS via `make -j$(nproc) zelda3`; treat warnings as build breaks.
- **No automated test suite.** Behavioral verification = run with the ROM attached and watch for RAM-compare assertions, or replay the per-chapter savestates in `saves/ref/`.
- **Python tooling has no requirements pin beyond Pillow + PyYAML.** It runs on Python 3.10+ in CI.

## Claim-grounding discipline (read before asserting facts about external code or this codebase)

The single most impactful lesson from prior spec/doc work on this project: when asserting facts about external code, this codebase, or any referenced upstream, read the source before the assertion lands in an artifact. Memory-based assertions have been the largest single source of avoidable error in plan- and spec-level work on this repo — see `openspec/changes/add-randomizer-support/lessons.md` for the catalog of failure modes and the discipline that prevents them.

### Upstream references

This repo references the ALTTPR PHP implementation at `../alttp_vt_randomizer/` (when checked out as a sibling directory). Facts about that upstream:

- **License: MIT** (verified at `../alttp_vt_randomizer/LICENSE` and `composer.json`).
- Logic lives in `app/Region/{Standard,Open,Inverted}/*.php` (~4,000 lines of PHP closures wired via `setRequirements` / `setFillRules` / `setAlwaysAllow` per location).
- The 43 named macros (`canShootArrows`, `canKillMostThings`, `canGetGoodBee`, `hasBottle`, etc.) are public methods on `app/Support/ItemCollection.php`.
- Placement algorithm is `app/Filler/RandomAssumed.php` (assumed fill with fix-point reachability expansion).
- **Seed reproducibility is database storage** (`app/Seed.php`), not seeded-deterministic RNG. The "seed hash" is a Hashids-encoded primary key; placement is regenerated by DB lookup, not RNG replay.
- Goal names are **snake_case + hyphenated** (`ganon`, `fast_ganon`, `dungeons`, `pedestal`, `triforce-hunt`, `ganonhunt`, `completionist`), not PascalCase.

Claims about ALTTPR behavior — config keys, naming, RNG, file layouts, line numbers — come from a grep against that checkout, never from memory. Source-code comments can be stale: `EntranceRandomizer.php:10` falsely claims "we use mt_rand" but the actual code shells out to Python. Trust the code, not the comments.

Speculative community-repo names are forbidden in committed artifacts. If a resource might exist (e.g., a separate "logic file" repo), verify before citing.

### Internal references

Same discipline for this codebase:

- **`g_ram[]` offsets**: grep `src/variables.h` and `src/features.h`, don't guess. Free region between `hud_cur_item_r` (0x658) and `spotlight_var3` (0x670) is 23 bytes; new `kRam_*` allocations land there per `features.h`'s "unused RAM repurposed for compat" block convention.
- **Save format**: `saves/sram.dat` is one 8 KB SRAM image structured as 3 slots × {primary, backup} per `src/messaging.c:103, 257-268` and `src/select_file.c:43`. `kSrmOffsets[4] = {0, 0x500, 0xa00, 0xf00}` is misleading — the fourth entry is the backup-region base, NOT a 4th slot.
- **Snapshot format**: `StateRecorder_Save` in `src/zelda_rtl.c:533-558` writes header + input log + `base_snapshot` + `SaveSnesState` dump. It is an input-replay system, NOT a raw `g_ram` dump.
- **Item-grant call sites**: `Link_ReceiveItem` is not the actual grant — it triggers the receive-animation ancilla. The real `link_item_*` write happens in `AncillaAdd_ItemReceipt` (`src/misc.c:713-844`) via `kMemoryLocationToGiveItemTo[]` / `kValueToGiveItemTo[]` plus a long chain of special-case branches. Enumerating every grant site is multi-day audit work, not a single grep.

### Fresh-eyes audit cadence

After landing any substantial code surface change on this project (a sprint's worth of work — new subsystem, large refactor, sweep of audit fixes), spawn a parallel review agent with a self-contained prompt before declaring the work done. Don't review your own work as the final pass.

Each audit pass in this project's history has found 5-10 NEW bugs the previous reviewer missed, including HIGH-severity issues that the familiar author would never have noticed (off-by-one in formula-encoded list mappings, spec/impl enum disagreements, full-inventory-vs-sphere-walked goal-check inconsistencies). The pattern is reliable enough to treat as workflow, not optional polish.

The prompt should brief the agent on what changed (`git log --oneline <baseline>..HEAD`), point to the spec scenarios, and ask explicitly for *new* findings — not re-litigation of previously-closed bugs. Cap the response length so the agent prioritizes signal over coverage.
