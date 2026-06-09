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

Python deps: `pip install -r requirements.txt` (Pillow, PyYAML). On macOS/Linux the `Makefile` prefers `uv` when installed (runs the tooling in an isolated env auto-provisioned from `requirements.txt`; falls back to system `python3`), so a manual install is optional there. Windows invokes `python` directly (extract_assets.bat / .vcxproj) and is uv-agnostic.

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
- `rando/rando_window/` (PC only, behind `Z3R_NATIVE_SETTINGS_WINDOW`) — Dear ImGui **second OS window** that owns the randomizer **settings UI on PC**; the in-game SNES settings screen + alphabet picker are compiled out on PC (kept for Switch). New PC rando-settings axes go in the ImGui panels here, NOT the in-game screen. `rando/rando_generate.c` (`Rando_GenerateSlot`) is the shared playable-slot generator both UIs call; the bridge (`rando_window_bridge.c`) carries pending settings/result across the UI↔game-frame boundary on the single main thread.
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
- **Keep generated/extracted data out of tracked files.** Asset data lives in **gitignored** artifacts (`zelda3_assets.dat`, `assets/dungeon/*.yaml`, `assets/rando/chest_table.gen.bin`), read at runtime/codegen — never inline large data tables into source. Structural facts (addresses in `variables.h`/`features.h`, the MIT ALTTPR `CHEST_NAME_BY_ROM_ADDR` catalog) are fine. `assets/scripts/check_no_embedded_data.py` (in `rando_ci.yaml`) fails the build on any long inline hex blob; allow a true exception with `embedded-data-guard: allow <reason>`.
  - **Trap — these gitignored assets fail OPEN, silently.** If `assets/rando/chest_table.gen.bin` is ABSENT when the codegen runs (e.g. a fresh worktree where `setup_worktree.py` didn't mirror it — it does now), `src/rando/chest_lookup.h` is emitted EMPTY (`kRandoChestLookup_COUNT == 0`) and **every chest grants its vanilla ROM item** — placement/spoiler are correct, but no chest resolves to its placed item. The corpus + `--rando-selftest` are blind to this (they test placement *generation*, not runtime chest dispatch), so a fresh-build env is a first-class suspect whenever "placement is right but the runtime grant is wrong." An F12 g_ram dump (is `kFeatures1_RandomizerActive` even set?) localizes it fast.
- **`-Werror` is on.** The CI matrix builds on Linux and macOS via `make -j$(nproc) zelda3`; treat warnings as build breaks.
- **No automated test suite.** Behavioral verification = run with the ROM attached and watch for RAM-compare assertions, or replay the per-chapter savestates in `saves/ref/`.
- **The rando regression corpus (`assets/scripts/run_rando_corpus.py`) + `--rando-selftest` cover ONLY the headless CLI `--generate-seed` path.** The *playable-slot* generator `Rando_GenerateSlot` (`src/rando/rando_generate.c`) — called by both the in-game settings screen AND the PC native settings window — is different code (no boss/drop shuffle inline; the sidecar slot stores placement + settings_hash + seed + hints + goal, NOT the full `RandoSettings`) and has **no automated test**. Verify slot-path changes by playtest + `saves/sram_rando.dat` diff, not by a green corpus. (A file-select render regression on this path passed the corpus + `--rando-selftest` + 4 fresh-eyes audit rounds; only an end-to-end playtest caught it — reinforces the "playtest is the only reliable net" rule above.)
- **Placer determinism: `--generate-seed` MUST default to `budget_seconds = 0`.** `Place_AssumedFill`'s retry loop ships a best-so-far when no attempt fully completes; a positive `budget_seconds` is a *wall-clock* cutoff (`clock()` is wall-clock on Windows, CPU-time on POSIX) that selects a *different* best-so-far under load → machine-speed-dependent `placement_digest`/`goal_completable`, breaking the corpus's cross-platform contract and flaky-refusing hard seeds (e.g. triforce-hunt `pieces_placed=30`). The in-game `Rando_GenerateSlot` already uses budget=0; headless must match. Guarded by `assets/scripts/check_placer_determinism.py` (static prong asserts the default is 0; runtime prong runs a retry-loop-exhausting canary N times for identical digests). The corpus only contains attempt-0 seeds, so it never guards the retry loop — that guard does.
- **Hunt goals (`triforce-hunt`/`ganonhunt`) with `pieces_placed=30` often can't meet the strict `items` ("100% inventory") accessibility tier** — all placed pieces must be reachable, and Retro (which pins ~50 shop/take-any locations) tightens the fill further. This is correct behavior (the generator honestly refuses a seed it can't make 100%-accessible); the convention is `accessibility=none` for hunt goals (see the corpus's `a1-standard-triforce-hunt-beatable`). When sweeping for completability, judge accept/refuse by whether the binary *writes the spoiler*, not by `unreachable_placements` being non-empty (which `accessibility=none` permits).
- **Validate any placement-affecting change with a CORPUS REGEN, not a code-review claim of "digest-neutral."** Build a fresh binary (a WSL `make zelda3` is fine — `placement_digest` is cross-platform deterministic, Win MSVC == Linux gcc, verified), run `bump_rando_corpus.py --apply`, and diff the result against unmodified `main` built fresh in the same env (`rm src/rando/logic_data.c` first to force codegen). An *intended* logic change is **scoped** — e.g. the NotADungeonItem key-ban moved exactly 13/110 entries (Retro + the one wild-keys seed) and left every default/open/standard/inverted seed byte-identical; a **regression also moves default seeds** the change can't legitimately affect. A guard claimed "provably dead / draws no RNG / byte-identical" can still fire: a "defensive" assumed-fill over-cap guard counted PRE-PLACED reward/prize/vanilla items in `placement_at[]` against the pool cap and skipped real placements on every seed — and it passed the MSVC build, `--rando-selftest`, and `check_corpus_version_sync` silently; only the corpus regen + 3-way diff caught it. (Tooling gotchas: `bump_rando_corpus.py` only regenerates when `manifest generator_version < kGeneratorVersion` — drop the manifest version if a patch pre-bumped it; it needs an ABSOLUTE `--binary` path; and it rewrites the manifest as LF — restore CRLF before committing.)
- **Python tooling has no requirements pin beyond Pillow + PyYAML.** It runs on Python 3.10+ in CI.
- **Rando logic-graph "last-wins" merge.** `assets/rando_logic_gen.py` loads `assets/rando/logic.yaml` first, then `assets/rando/logic_parts/*.yaml` sorted, then `logic_parts/inverted/**`. For location entries appearing in multiple files, the LAST file wins silently — no warning. Authoring a `logic_parts/*` file that duplicates a `logic.yaml` entry with a different predicate (or with no `region:` field where logic.yaml has one) silently overrides the canonical entry. This trap fired twice in one audit session: King Zora et al. lost their `RescuedZelda` gate when a logic_parts duplicate was removed and dropped the `region:` binding; Eastern Palace Boss/Prize had weaker predicates in logic_parts than logic.yaml and the placer routed through EP without arrows or lamp. The codegen now warns on `region: 0xFFFF` for non-allowlisted location types (see `REGION_OPTIONAL_TYPES` in `rando_logic_gen.py`), but the warning does NOT catch the weaker-predicate case — diff before adding or removing logic_parts entries that share names with logic.yaml content.

## Claim-grounding discipline (read before asserting facts about external code or this codebase)

The single most impactful lesson from prior spec/doc work on this project: when asserting facts about external code, this codebase, or any referenced upstream, read the source before the assertion lands in an artifact. Memory-based assertions have been the largest single source of avoidable error in plan- and spec-level work on this repo — see `openspec/changes/archive/2026-05-29-add-randomizer-support/lessons.md` for the catalog of failure modes and the discipline that prevents them.

**Verify a root cause against baseline behavior.** Before accepting a diagnosis — your own, a sub-agent's, or a remembered one — check whether it implies the *unmodified* game is also broken. If the explanation would mean vanilla behavior fails too, but vanilla demonstrably works, the diagnosis is wrong. (A Tower of Hera "no prize + softlock" was confidently misattributed to the boss heart never setting its prize-ready bit; that would have broken vanilla ToH, which works, so it couldn't be right — the real cause was a rando-only prize-gate bug.)

**Reconcile a change's spec deltas against as-built source BEFORE archiving.** A spec delta is a claim about shipped behavior; once `openspec archive` merges it, it becomes the permanent baseline and is much costlier to fix. Deltas ROT when the implementation evolves after the delta was authored and `tasks.md` gets updated but the spec does not. Read every requirement + scenario against the *current source* before archiving. The `add-rando-hints` deltas, authored pre-fork-wiring, asserted the OPPOSITE of shipped reality on three counts (fork-extension NPCs "NOT wired in-game" when they are; hint format "The \<item\> lies at \<location\>" when it is "\<item\> is in \<location\>"; `meta.hints_count` "does not exist" when it does). `openspec validate` does NOT catch this — it checks delta *structure*, not *truth*. Note the asymmetry: `docs/randomizer.md` was correct because it was updated alongside the feature; only the openspec delta drifted — so don't assume "the docs are fine" means "the spec is fine."

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
- **Room-data redirect carries SPRITES, not the room ENVIRONMENT** (learned from boss shuffle — applies to any future room/entrance redirect). Redirecting a room's sprite list (`Dungeon_LoadSprites`) is only part of the room: the sprite GFX index (`hdr[3]`) and sprite palette (`palette_sp0l/sp5l/sp6l` from `kDungPalinfos[hdr[1]]`) are set in the room-header load (`src/dungeon.c`); the room "effect" byte (`dung_hdr_collision_2` = `$00AD` = `hdr[4]`, which drives ice/lava/freeze mechanics), BG2 objects, and room-state-gated spawns (e.g. Blind only materializes when `dung_savegame_state_bits & 0x2000`, set by a Thieves'-Town-only maiden sequence) live elsewhere again. A "swap the sprites" feature that forgets the rest renders/ fights wrong (Lanmolas drew with the wrong palette; Kholdstare spawned un-encased). Boss shuffle therefore pins the three environment-dependent bosses (Blind/Kholdstare/Trinexx) — see boss-shuffle `design.md` D7.
- **Dungeon-sprite room-data y/x bytes are bit-packed** (`Dungeon_LoadSingleSprite`, `src/sprite.c`): bits 0-4 = the 16px-granular intra-quadrant POSITION, y bits 5-6 + x bits 5-7 = the sprite SUBTYPE, y bit 7 = the FLOOR flag (`x >= 0xe0` is the overlord marker; `type == 0xe4` is a control entry). Any code that MUTATES these coords (e.g. shifting a redirected boss formation) must mask to the position field (`& 0x1f`) and preserve bits 5-7 — a whole-byte add corrupts floor/subtype and can push x into the `0xe0` overlord range.

### Fresh-eyes audit cadence

After landing any substantial code surface change on this project (a sprint's worth of work — new subsystem, large refactor, sweep of audit fixes), spawn a parallel review agent with a self-contained prompt before declaring the work done. Don't review your own work as the final pass.

Each audit pass in this project's history has found 5-10 NEW bugs the previous reviewer missed, including HIGH-severity issues that the familiar author would never have noticed (off-by-one in formula-encoded list mappings, spec/impl enum disagreements, full-inventory-vs-sphere-walked goal-check inconsistencies). The pattern is reliable enough to treat as workflow, not optional polish.

**Saturation model** (validated across a 5-round recursive audit-fix-of-audit-fix sequence): rounds 1-3 each find ≥1 HIGH; rounds 4-5 found 0 HIGH but 3 MED apiece. After two consecutive HIGH-free rounds the marginal value of another audit drops sharply, but MED-severity findings keep coming when a single drift class (e.g., a PHP idiom translation pattern that cuts across many YAMLs) hasn't been swept end-to-end. The right response to repeated MED finds in the same class is usually a class-driven `grep` across ALL sites at once, not another iterative audit round.

The prompt should brief the agent on what changed (`git log --oneline <baseline>..HEAD`), point to the spec scenarios, and ask explicitly for *new* findings — not re-litigation of previously-closed bugs. Cap the response length so the agent prioritizes signal over coverage.

### Rando's dominant bug class: vanilla state reused as a progress proxy

Vanilla ALTTP repeatedly uses one item/state as a stand-in for "the player reached milestone Y," because in vanilla `has(X)` and `past(Y)` are equivalent. Rando breaks that equivalence by shuffling the proxy. This is the single most common bug class in the randomizer work, and it is invisible to `--rando-selftest`, the placement corpus, and `check_audit_guard` — those check grant sites and placement, but these bugs live at *gameplay/consume sites*. **End-to-end playtest is the only reliable net.** When reviewing a rando change, audit these forms:

- **Vanilla receive *codes* with side effects** — `progressive_to_lttp` (`src/rando/rando.c`) reuses LttP receive codes whose receipt carries cutscenes/text/immobilization (Master Sword code 0x01 → pedestal cutscene), are dead garbage-text entries, or write the wrong RAM byte (silver-arrows code 0x29 writes the *mushroom* byte).
- **Shared bytes** holding what rando treats as independent items — `link_item_mushroom` (0xF344) is one byte for Mushroom(1)/Powder(2); Powder-first locks out the Witch/Potion-Shop check. General fix when two shuffled tiers share a byte: track true ownership in a persisted bitfield and treat the byte as the *selected* tier (never-downgrade on grant; an item-menu Press-A swap when both owned — see flute/shovel + boomerang/bow in `Rando_Grant*` / `Hud_NormalMenu`). Only *collapse* two such items to a single progressive ladder (1st pickup→tier 1, 2nd→tier 2, item identity ignored — as done for boomerang/magic) when the tiers are **logically interchangeable**; if any logic predicate distinguishes them (silver vs wood bow), keep item identity + never-downgrade instead, or the placer desyncs from runtime. A *blanket* never-downgrade clamp (`v > *p`) must EXEMPT non-tier bytes: `link_arrow_filler` is a per-frame drain counter, not a tier, and the clamp silently dropped a paid arrow grant.
- **Spawn/collect GUARDS on a shuffled LOCATION** — an NPC/sprite/ancilla gates whether it appears or grants on the vanilla item/state *it itself* would normally give. Two directions: (a) MISSABLE — once you obtain that state elsewhere the guard blocks the location *forever* (the Magic Bat refused to summon at ¼ magic → uncollectable); (b) RE-GRANT/dupe — vanilla's anti-re-collect relied on the grant flipping a byte that disabled the trigger, but a rando feature re-enables the trigger (the flute/shovel item-menu toggle re-enabled the shovel dig → infinite Flute Spot re-grant; a Triforce piece there = instant win). Fix both by gating on `Rando_IsLocationChecked(LOC_*)` under rando, vanilla path untouched (most NPCs already do — Magic Bat/Sahasrahla/King Zora/Catfish/tablets/…). **Corollary: whenever you add a feature that re-enables a vanilla ONE-SHOT action (a toggle, a re-openable menu, a re-enterable room), audit every grant that action can now re-trigger.**
- **Per-dungeon / per-event *bit gates*** the shuffle reassigns — prize gates test `kDungeonCrystalPendantBit[dungeon]` (the dungeon's *vanilla* prize bit), wrong under `prize_shuffle`; gate on the rando *location-checked* state instead.
- **Consume-site *guards*** (`link_item_*`, `sram_progress_indicator`) used as pre/post-milestone sentinels — fix with `(vanilla_gate) || (enhanced_features1 & kFeatures1_RandomizerActive)`.
- **Control-flow SIGNALS a vanilla branch consumes** — when a new feature adds a branch that *diverts* from a vanilla code branch, audit every flag the vanilla branch CLEARS or CONSUMES; the diverted branch must replicate it, or the stale flag corrupts the *next* pass through that function. The Inverted Dark Chapel spawn took `Dungeon_LoadEntrance`'s entrance branch, which — unlike the `kStartingPoint` branch it diverted from — never cleared `death_var4`; on the next walk-in through the chapel door the stale flag re-routed back into the `kStartingPoint` branch → Link's House, discarding the correct `which_entrance=0x5A`. (~10 build/test cycles were lost tuning exit position / tilemap / camera before tracing the branch — see the runtime-debugging note below.)

- **Draw-side resolution that must mirror the grant chain** — a feature that *renders* a placed item (e.g. field-item sprites: drawing the placed item's gfx at heart-piece/book/etc. locations) must resolve `item → gfx` the EXACT way the grant resolves `item → effect`: `Rando_VanillaItemForRegistryId` PRIMARY (+ special remaps like the progressive blue→red boomerang), THEN `progressive_to_lttp`. Resolving via only one piece silently draws the wrong/vanilla sprite for everything the other piece handles — using only `progressive_to_lttp` broke rupees; `kDirectGrantIcons` only covers DIRECT-GRANT items (keys/magic/prizes), not rupees/equipment/boomerang. Each gap was a separate playtest round. Fix: a single shared resolver so draw and grant can't drift. (See memory `[[field-item-sprites-asbuilt]]`. Sibling gotcha for sprite draws: the per-sprite OAM budget `((sprite_flags2&0x1f)+1)*4` is in BYTES — a standing sprite reserves ONE entry, so a 2-tile (8×16) draw needs an explicit `Oam_AllocateFromRegion*` or the 2nd tile is scene-dependently clobbered.)

- **Grant-site fallback that silently drops the placed item** — a minigame/NPC dispatch that grants only *some* placement classes (e.g. direct-grant items — keys/magic/prizes/dungeon bits via `Rando_DispatchVanillaGrant`'s `kRandoLttpSkip` path) and falls back to spawning the vanilla CONSOLATION prize for the rest *loses* every non-direct-grant placement (Bow/Bottle/Hookshot/equipment/…): `Rando_DispatchVanillaGrant` already marked the location checked, so the placed item is gone forever and the assumed-fill-certified seed becomes UNBEATABLE. Every dispatch site MUST route ALL placements through `Rando_ReceiveOrConfirm` (direct-grant → confirmation cue; everything else → `Link_ReceiveItem`) and then RETURN — never fall through to a vanilla-prize spawn. The Digging Game shipped with exactly this gap (granted only direct-grant items, threw a vanilla Piece of Heart for the rest) while Hammer Pegs / Hype Cave were correct; `--rando-selftest` + the corpus are blind to it (generation-only — they never run the runtime grant). When adding/auditing a dispatch site, check every vanilla consolation/fallback branch.

Corollary: a freeze inside a prize/cutscene/receipt sequence is often a *downstream* symptom — the sequence is waiting on an object a grant/gate bug prevented from spawning. Chase the un-spawned awaited object before hunting the immobilizer.

### Runtime-debugging discipline (learned the hard way on the Dark Chapel re-entry — days of it)

- **When the trigger fires correctly but the wrong thing happens, trace the BRANCH, not the trigger.** If the right value is set (e.g. `which_entrance=0x5A`) yet the result is wrong, stop tuning coordinates/tiles/positions — instrument the *downstream* decision and see which branch ran. A *missing* expected log line is the diagnosis: the chapel re-entry logged its entrance trigger but produced NO room-load line → it had taken the other (`kStartingPoint`) branch. The bug was one stale flag, not the dozen position/tilemap things tried first.
- **Instrument the whole decision chain at once when stuck >2 cycles**, not one variable per build. Log every relevant branch (and log *both* arms, so a missing line is informative). Incremental, one-variable-at-a-time logging turned a one-cycle find into ~ten.
- **Read F12 dumps / log files via ABSOLUTE worktree paths.** The Bash shell's cwd resets to the *main* repo, so `cd <worktree> && python open('dump_gram.bin')` silently reads a STALE main-repo dump. This poisoned a captured value (`0x9A` vs `0x9C`) and sent the hunt chasing a non-existent tilemap bug for several turns. (See memory: "wrong-folder dump trap".)
- **A static-analysis diagnosis (yours or an agent's) is a hypothesis until a runtime dump/log confirms it.** A fresh-eyes agent's asset decode was correct and useful, but its proposed *cause* (an entrance-marker tile) was a red herring; the real cause only fell out of instrumenting the branch actually taken at runtime. This extends to **delegated VALIDATION results, not just diagnoses**: a sub-agent's *reported* build/corpus/test outcome (and its explanation for a failure) is itself a claim — rebuild and re-run the validation yourself before trusting it, **especially before merging**. A music-impl sub-agent reported "3 corpus failures from generator-version drift (manifest v64 vs base v61)"; a clean rebuild (`rm src/rando/logic_data.c` + rebuild) showed **0 failures** — its fails were a stale-build artifact rationalized into a plausible-but-false root cause (the manifest was actually v61, matching the base). Its *conclusion* (corpus-clean, no kGen bump) happened to be right; its *evidence* was fabricated. Likewise a first-pass ROM-version *classification* is a hypothesis: a `$7F5B00,x` table read got labeled "us10-different destination math" until a deeper disassembly traced the actual RAM writes and showed it was the overworld **music** table — same routine, byte-identical destination/camera, only `$12C`/`$12D` differ. Trace the actual writes before classifying a JP-vs-US delta.
- **For a playtest-only VISUAL/gfx question, build an offline renderer from `zelda3_assets.dat` — the autonomous F12-dump equivalent.** When you can't playtest and the result is a render (overlay/palette/tile question), reproduce the engine's pipeline offline in Python: parse the `.dat` (e.g. asset 70 map16→map8, asset 65 LZ2 BG sheets, the per-area tile-theme/palette assets), decompress the sheets, and reproduce the **4bpp `Do3To4High/Low` → overworld-palette assembly** exactly (`LoadBackgroundGraphics` / `Palette_Load_OWBG*`: for OW `main_theme>=0x20`, char-regions {0,3,4,5} use the High half = palette colors 9-15, {1,2,6,7} the Low half = 1-7). **The renderer is itself a hypothesis** — a first pass that ignored the High/Low split rendered a broken overlay as "clean"; only the corrected model (cross-validated by re-rendering a KNOWN-good screen and matching it pixel-for-pixel) revealed the real corruption. This resolved the inverted screen-0x1B Ganon-overlay spike without a single playtest, and is reusable for any overworld gfx/palette change. (See memory `inverted-overlay-mirror-glitch`.)
