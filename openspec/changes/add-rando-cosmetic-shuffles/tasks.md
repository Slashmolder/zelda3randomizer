# Tasks — Cosmetic shuffles

> Implementation landed 2026-06-01 (build + selftest + corpus green). Remaining
> work is provenance pin, the cosmetic-determinism CI step, docs, audit, and
> playtest.

## 1. Apply-time pre-flight (grounding)

- [x] 1.1 Pin upstream `../alttp_vt_randomizer/` commit hash + `../z3randomizer/` (asm) hash in `audit.md §"Cosmetic provenance"`. Note: ALTTPR palette transforms are JS-patcher, NOT PHP — the asm/JS are reference only; the fork re-implements transforms in C. <!-- 2026-06-02: alttp_vt @219fcafd, z3randomizer @dcb0a2b; recorded in audit.md. -->`
- [x] 1.2 ZSPR loader entry path confirmed: `LoadLinkGraphics` → `ParseLinkGraphics` (`main.c`), driven by `g_config.link_graphics`. The folder-pick feeds the same path.
- [x] 1.3 Palette-load sites: superseded by a single chokepoint — `nmi.c:211` is the *only* flush of `main_palette_buffer` → PPU CGRAM. Transform there (on the CGRAM copy), not per load site.
- [x] 1.4 **Music chokepoint spike**: `ZeldaPlayMsuAudioTrack` (`audio.c:137`) is the single point a song reaches both MSU and the SPC `APUI00`. `nmi.c:116` stored the original id in `last_music_control` first, so remapping here leaves game-facing state vanilla. Control codes = `& 0xf0 == 0xf0`.
- [x] 1.5 No `kGeneratorVersion` / settings-hash / corpus impact — **confirmed**: corpus 67/67 byte-identical post-change (cosmetics are outside the canonical input).

## 2. New module skeleton

- [x] 2.1 Created `src/rando/shuffle_cosmetic.{c,h}` (`Cosmetic_SetSeed`, `Cosmetic_ApplyPaletteCgram`, `Cosmetic_PickSpriteFile`, `Cosmetic_RemapSong`, `Cosmetic_ParsePaletteMode`).
- [x] 2.2 Wired: Makefile glob (`src/rando/*.c`, auto) + `zelda3.vcxproj` `<ClCompile>` + Switch `SOURCES` dir glob (auto). NOT added to `check_codegen_wiring.py` — that guard is for codegen *outputs*; this is hand-written.
- [x] 2.3 Dedicated `RandoRng` forked from `cosmetic_seed`; `Cosmetic_SetSeed` resolves `cosmetic_seed == 0` → slot `seed_u64` (or 0 for vanilla). Sprite pick uses a salted sub-stream so it doesn't perturb palette/music tables.

## 3. Client config (`zelda3.ini`)

- [x] 3.1 `[Graphics] CosmeticSeed = <u64>` (default 0) — `config.c` section 1, `strtoull` (accepts 0x… hex).
- [x] 3.2 `[Graphics] PaletteShuffle = vanilla|shuffled|grayscale|negative` (default vanilla) via `Cosmetic_ParsePaletteMode`.
- [x] 3.3 `[Graphics] SpriteShuffle = off | <folder path>` (default off → uses the explicit `LinkGraphics`).
- [x] 3.4 `[Sound] MusicShuffle = off|on` (default off) — `config.c` section 2.
- [x] 3.5 Round-trip safe + **managed for the UI**: the four keys are now in the writer's managed tables (`kGfxKeys`/`kSndKeys` + `RenderManagedValue`), so `Config_WriteIniFile` writes them on Apply and inserts them if absent (all render non-empty at defaults). Hand-edited values still round-trip. Cosmetics never enter the slot/share string. <!-- updated when the native-window panel landed; superseded the original "unmanaged/preserve-only" plan so UI changes persist. -->

## 5b. Native settings-window panel (PC)

> Added after the owner noted the cosmetics had no discoverable UI. PC-only
> (`Z3R_NATIVE_SETTINGS_WINDOW`); the INI keys remain the normative source.

- [x] 5b.1 New "Cosmetics" tab in the native settings window (`game_config_panels.cpp`, next to Audio), following the existing `s_cfg`/`s_dirty` working-copy + Apply pattern.
- [x] 5b.2 Controls: palette-mode combo, music-shuffle checkbox, sprite-folder path (restart-tagged), and a **hex cosmetic-seed field + "New random seed" button** (UI-only SplitMix64 entropy mirroring the rando window's seed roll; `.cpp` is outside the determinism guard's `.c/.h` scan).
- [x] 5b.3 Apply path: sprite folder interned like the other path fields; `Config_ApplyLive` rebuilds the cosmetic tables on a `cosmetic_seed` change (palette mode + music toggle are live; sprite is launch-time → restart-tagged).
- [x] 5b.4 Persistence verified structurally: `Config_SelfCheckIniWriter` green with the new managed keys.
- [ ] 5b.5 **Playtest**: open the Cosmetics tab, change each control + Apply, confirm it takes effect and persists to `zelda3.ini`; "New random seed" fills a fresh hex value.

## 4. Palette axis

- [x] 4.1 `Cosmetic_ApplyPaletteCgram`: shuffled (per-16-color-group channel permutation, 1 of 6, precomputed in `SetSeed`), grayscale (luma collapse, weights 77/150/29), negative (`c ^ 0x7fff`). bit15 preserved.
- [x] 4.2 Called at `nmi.c:213` right after the CGRAM memcpy; gated on `mode != vanilla` (no-op otherwise → vanilla byte-identical).
- [ ] 4.3 **Playtest**: verify HUD/text legibility under each mode (no all-black-on-black). Adjust group masks if a transform destroys a fixed-contrast group.

## 5. Sprite axis

- [x] 5.1 `Cosmetic_PickSpriteFile`: enumerate `*.zspr` (Win32 `FindFirstFileA` / POSIX `dirent`), sort by filename (`qsort` + case-insensitive cmp, stable cross-platform), pick via salted cosmetic RNG; interned path feeds the existing ZSPR loader.
- [x] 5.2 Empty/missing/invalid folder → returns NULL → falls back to the configured `LinkGraphics`; an unreadable picked sprite also falls back (no crash) before `Die`.

## 6. Music axis

- [x] 6.1 `Cosmetic_RemapSong`: Fisher-Yates permutation of the song band `[0x01, 0x0F]` built in `SetSeed`; bijection within band so no song is silenced/duplicated.
- [x] 6.2 Applied at the `ZeldaPlayMsuAudioTrack` chokepoint; passes through control codes (`& 0xf0 == 0xf0`), 0, and ids outside the band. Band is a documented playtest-tunable.
- [ ] 6.3 **Playtest**: confirm MSU-1 still resolves a track for shuffled-area music when a pack is loaded (remap precedes `MsuPlayer_Open`, so it keys off the remapped id — verify audibly).

## 7. CI + determinism

- [x] 7.1 Add a cosmetic-determinism CI step: fixed `cosmetic_seed` → identical palette transform output hash + sprite index + song permutation across builds (mirrors the hint-determinism step). NO corpus regen. <!-- 2026-06-02: `Cosmetic_SelfCheck` added to `Rando_RunAllSelfChecks`, mirroring `Hints_SelfCheck`. CI already runs `--rando-selftest` (rando_ci.yaml:184), so this is gated on every push/PR. Asserts: fixed seed → identical group-perm + song tables (within-run determinism), config-seed==0→slot-seed fallback equivalence, seed-sensitivity, song-map band bijection, group-perm range. Sprite INDEX is excluded (folder/IO-dependent); its cross-platform determinism is handled by the C1 byte-wise sort fix. No corpus regen; no kGen bump. -->`
- [x] 7.2 Default-settings `placement_digest_hex` byte-identical pre/post — **confirmed** (corpus 67/67 OK).
- [x] 7.3 `check_audit_guard.py --strict` + `check_determinism.py` green (no new writes; no `rand`/`time`; uses `Rng_*` only).

## 8. Audit

- [x] 8.1 Fresh-eyes audit per `[[cluster-audit-cadence]]` after authoring lands; document in `audit.md`, address every HIGH. <!-- 2026-06-02: audit done, 0 HIGH. Findings C1-C4 in audit.md §"Fresh-eyes audit"; C1 (non-ASCII sprite-sort determinism) fixed in-branch; C2-C4 deferred to playtest/spec follow-ups. -->`
- [ ] 8.2 Confirm no axis-off path diverges from vanilla rendering (RAM/PPU compare clean for vanilla + each-axis-off).

## 9. Documentation

- [x] 9.1 Add a "Cosmetics" section to `docs/randomizer.md`: the 4 INI keys, the `CosmeticSeed=0`→slot-seed default, tournament decoupling, and the out-of-scope list. <!-- 2026-06-02: added after the Hints section. -->`
- [x] 9.2 `openspec/changes/README.md` D1 row updated (effort revised; no kGen bump; client-config not slot-field).

## 10. Playtest

- [x] 10.1 Each palette mode renders correctly across overworld / dungeon / HUD; legible. <!-- owner playtest-confirmed 2026-06-02 -->
- [x] 10.2 Sprite shuffle picks a sprite from a folder; same `CosmeticSeed` → same sprite. <!-- owner playtest-confirmed 2026-06-02 -->
- [x] 10.3 Music shuffle remaps area music; same `CosmeticSeed` → same remap; MSU pack still plays. <!-- owner playtest-confirmed 2026-06-02 -->
- [x] 10.4 Tournament check: two runs, same `share_string`, different `CosmeticSeed` → identical gameplay, distinct look. <!-- owner playtest-confirmed 2026-06-02 ("tested these we're good") -->
- [x] 10.5 All axes off → indistinguishable from vanilla. <!-- owner playtest-confirmed 2026-06-02 -->

## 11. Archive readiness

- [ ] 11.1 CI green on Linux + macOS + Windows; cosmetic-determinism step passes; corpus unchanged.
- [ ] 11.2 Playtest §10 ticked across all three axes + all-off.
- [ ] 11.3 Fresh-eyes audit findings addressed.
- [ ] 11.4 `openspec archive add-rando-cosmetic-shuffles` runs cleanly; `randomizer-shuffles` cosmetic requirement + new `randomizer-ui` cosmetic-settings requirement merge into `openspec/specs/`.
