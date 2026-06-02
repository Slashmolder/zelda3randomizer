# Zelda3 Randomizer

> **Status.** The randomizer is well past its Phase A foundation. Phase B
> work — Inverted/Retro world-states, in-game trackers, hints, race-mode
> reveal, boss/drop shuffles, the native settings window, and more — has
> landed incrementally. For current scope and per-change status, see the
> OpenSpec changes under `openspec/changes/` (start at its `README.md`
> index). `kGeneratorVersion` in `src/rando/rando.h` is the authoritative
> marker of the live placement/serialization format; it advances whenever a
> placement-affecting change ships.

This document covers user-facing operation of the in-binary randomizer, the
share-string format, save behavior, audit conventions for contributors, and
the generator-version bump policy. The authoritative source for behavior
decisions is the OpenSpec spec baseline at `openspec/specs/randomizer-*/` (Phase A
was archived 2026-05-29 to `openspec/changes/archive/2026-05-29-add-randomizer-support/`).

## Getting started

The randomizer lives inside the same `zelda3` executable as the vanilla port.

1. Extract assets per the top-level `README.md` (one-time `python assets/restool.py --extract-from-rom`).
2. Build per the standard instructions (`make` on Linux/macOS; `Zelda3.sln` on Windows).
3. The randomizer activates on a per-slot basis from the file-select screen
   (Phase A2). Until the UI lands, headless CLI generation is the entry point:

   ```sh
   ./zelda3 --generate-seed \
     --settings=mode.state=open,goal=fast_ganon,crystals.ganon=7,crystals.tower=7 \
     --seed=0xDEADBEEFCAFEBABE \
     --out-spoiler=./spoilers/demo.json
   ```

   See `randomizer-core / CLI generation mode` (in `openspec/changes/.../specs/`)
   for the full grammar and exit codes.

### CLI flags

| Flag | Effect |
|---|---|
| `--generate-seed` | Headless generation mode. Required to enter the rando pipeline; otherwise the binary boots the normal game. |
| `--settings=k=v,...` | Comma-separated overrides for any settings axis (table below). |
| `--seed=0x...` | uint64 seed value. Required for single-seed mode. |
| `--out-spoiler=<path>` | JSON spoiler path. Also writes a sibling `.txt` text spoiler. |
| `--out-share-string=<path>` | Optional file for the raw base32 share string. |
| `--budget-seconds=<n>` | Bounds the placement retry budget (default 5). Exhausted budget accepts the best-so-far attempt. |
| `--assets-must-be-vanilla` | Refuses non-vanilla `zelda3_assets.dat` (compares against `kVanillaAssetsHash` in `src/rando/vanilla_assets_hash.h`). |
| `--allow-broken-seed` | Bypass the goal-completability refusal — writes a spoiler even when `goal_completable=false`. Diagnostic use only. |
| `--print-assets-hash` | Print the SHA-256 of the loaded `zelda3_assets.dat` and exit. Useful for baking the vanilla hash. |
| `--rando-selftest` | Run subsystem self-tests (SHA-256 vectors, RNG, settings, logic, placement, shuffles, save, textfield, dispatch) and exit. CI invokes this on every Linux / macOS / Windows runner. |
| `--race-mode` | Generate a race-mode seed. Overrides `--settings=race_mode=false`. The spoiler is written as a 138-byte suppressed `ZRSR` binary file at the same path (instead of full JSON + .txt sibling); reveal via `--reveal-spoiler` to expand. |
| `--reveal-spoiler=<path>` | Read a suppressed `ZRSR` file at `<path>`, validate magic + CRC + version + stamp, regenerate the placement, and overwrite the file in place with the full JSON. Writes a sibling `.txt` text spoiler. Exits 0 on success; non-zero with a numeric `kRandoReveal_*` code on any failure (CrcMismatch, VersionMismatch, StampMismatch, ParseError, FileNotFound). Idempotent: a second invocation on an already-revealed file is a no-op success. |

Examples:
```sh
# Print the asset hash to bake vanilla_assets_hash.h
./zelda3 --print-assets-hash

# Run the regression-corpus self-tests
./zelda3 --rando-selftest

# Generate a Completionist seed at hard pool difficulty with a 30-second budget
./zelda3 --generate-seed \
  --settings=mode.state=open,goal=completionist,item_pool=hard \
  --seed=0x1234567890ABCDEF \
  --budget-seconds=30 \
  --out-spoiler=./spoilers/comp-hard.json
```

## Settings reference

Full per-axis documentation lives in `randomizer-core / Settings canonical
serialization order`. Phase A axes:

| Axis | Values | Default |
|---|---|---|
| `world_state` | `open`, `standard`, `inverted`, `retro` | `open` |
| `goal` | `ganon`, `fast_ganon`, `dungeons`, `pedestal`, `triforce-hunt`, `ganonhunt`, `completionist` | `fast_ganon` |
| `crystals.ganon` | 0..7 | 7 |
| `crystals.tower` | 0..7 | 7 |
| `item_pool_difficulty` | `easy`, `normal`, `hard`, `expert` | `normal` |
| `mode.weapons` | `randomized`, `assured` | `randomized` |
| `accessibility` | `items`, `locations`, `none` (alias `beatable`; UI label "beatable only") | `items` (auto-set to `locations` for Completionist) |
| `dungeon_items.{small_keys,big_keys,maps,compasses}` | `vanilla`, `dungeon`, `wild` | `vanilla` |
| `prize_shuffle` | `true`, `false` | `true` |
| `medallion_shuffle` | `true`, `false` | `true` |
| `pieces_required`, `pieces_placed` | uint16 | (Triforce Hunt / Ganon Hunt only) |

**Accessibility tiers** (ALTTPR three-way; all three guarantee the seed is
beatable, differing only in how much *extra* reachability the generator
enforces):

- `items` — "100% Inventory": every progression item is reachable. Junk
  (rupees/arrows/bombs), maps, compasses, and heart pieces may be unreachable.
- `locations` — "100% Locations": every location is reachable (strictest).
  Auto-selected and locked when the goal is Completionist.
- `none` — UI label **"beatable only"** (ALTTPR's "Not Guaranteed"): only the
  goal must be reachable; some items or locations may be unreachable. CSV accepts
  `beatable` as an alias. This still produces a beatable seed — the old
  "possibly unwinnable" behavior is no longer on this axis (use the CLI
  `--allow-broken-seed` flag for diagnostic seeds).

Phase B+ axes (`tricks`, `logic` glitch level, `swordless`, `pyramid_bow_upgrade=arrows`, `race_mode`) are reserved in the settings struct from Phase A.

## Share-string format

Magic prefix: `ZRSS` (Zelda Rando Share String). Distinct from alttpr.com's
share format; **the two are not cross-compatible in either direction** (a
deliberate choice — different generator, different placement output for the
same notional "seed").

Encoding: base32, with a CRC-16-CCITT-FALSE checksum.

Payload layout: `(magic | generator_version | settings_hash[16] | seed_u64 | checksum)`.

`Share_SelfCheck` round-trips the encoding and exercises explicit-reject paths
(alttpr.com format, corrupted base32, wrong-length input, wrong magic prefix).

## Save behavior

The randomizer's per-slot state lives in `saves/sram_rando.dat` — a sidecar
file alongside the existing `saves/sram.dat`. The vanilla save file is
**byte-untouched** by randomizer mode, so a vanilla-only binary sees those
slots as vanilla. Sidecar layout:

- 16-byte file header (magic `ZRSC`, format_version, slot_count, file_crc).
- 3 slots × {80-byte header + embedded placement table + checked-location bitmap
  + (format_version ≥ 2) a 28-byte canonical `RandoSettings` blob}.
- No 4th slot anywhere (per `audit.md` §0.6 and `randomizer-save` spec).

Slot header records: `slot_kind`, `generator_version`, `settings_hash`,
`share_string`, `last_vanilla_write_version`, `sram_slot_checksum_at_last_write`,
`placement_table_size`, `flags`, `mushroom_held`, hints/`goal`/`world_state`/
`flute_shovel_owned` ext bytes, and (`@70`) `settings_present`.

**format_version 2** (added with the rich tracker windows): each slot appends a
`kSettingsCanonicalLen`-byte canonical `RandoSettings` blob after the checked
bitmap. This lets a reloaded slot reproduce the seed's full settings *and*
recompute the prize/medallion shuffle assignments (from the share string's seed,
in the exact placer order) — both of which the runtime reachability engine
needs. Older v1 slots have no blob and load unchanged via the version-aware
deserializer (`RandoSave_ReadFile` keys body layout on the file
`format_version`); on such slots `settings_present` is forced off and the
trackers **suppress** the reachability display rather than guess (a wrong
`prize_shuffle` flag would mis-seed the shuffle stream). The blob size is coupled
to `kSettingsCanonicalLen` by a `_Static_assert`; the round-trip + a v1-compat
case are covered by `RandoSave_SelfCheck` (`--rando-selftest`).

Atomic-commit: write `<file>.tmp`, fflush, fsync (POSIX) / `_commit` (Windows),
rename atomically. Save order: sidecar first, then `sram.dat`.

Cross-version forward-compatibility (per `randomizer-save / Embedded placement
table — upgrade safety`): a slot written by `generator_version = N` loads on a
binary with version `N+1` and surfaces a one-time informational warning. The
embedded placement table is consulted; no regeneration is required.

## Race mode

Race-mode seeds suppress the spoiler at generation time so tournament admins
can distribute the share string without leaking item placement. Reveal happens
later via `--reveal-spoiler` (the binary regenerates and overwrites the file
in place with the full JSON).

**Generation**: pass `--race-mode` (or set `race_mode=true` via `--settings`).
The spoiler path receives a 138-byte binary file with magic `ZRSR` instead
of the usual JSON + .txt pair. File layout (all multi-byte fields LE):

```
+0    4 bytes   magic "ZRSR"
+4    2 bytes   generator_version (u16 LE)
+6    32 bytes  spoiler_stamp[32] = SHA-256 of the canonical revealed JSON
                (with race_mode and wall-clock fields normalized to 0)
+38   4 bytes   share_string_len (u32 LE)
+42   64 bytes  share_string (zero-padded)
+106  28 bytes  settings_canonical (= kSettingsCanonicalLen)
+134  4 bytes   crc32 (IEEE 802.3 over bytes 0..133, LE on disk)
```

Total: 138 bytes. The settings are public on race sheets, so including the
canonical bytes does not leak the placement — they're needed at reveal time
to regenerate.

**Reveal**: `--reveal-spoiler=<path>` runs the full validation chain — magic,
CRC32, generator-version match, settings canonical-deserialize, share-string
decode, `Place_AssumedFill` regenerate, `Logic_ComputeSpheres`, compare stamp
against a re-hashed canonical revealed JSON; on match, rename the
`.reveal-tmp` over the original suppressed file and write a sibling `.txt`
spoiler. Exit codes (`kRandoReveal_*`):

| Code | Meaning |
|---|---|
| 0 | Ok — file overwritten with full JSON (or no-op success if already revealed) |
| 1 | FileNotFound |
| 2 | ParseError — file lacks `ZRSR` magic or has unexpected size |
| 3 | CrcMismatch — file is corrupt or tampered |
| 4 | ShareStringMismatch — caller-provided expected_share_string doesn't match |
| 5 | VersionMismatch — file was produced by a different `kGeneratorVersion` |
| 6 | SettingsCorrupt — `settings_canonical` failed deserialization |
| 7 | StampMismatch — regenerated placement does not produce the recorded SHA-256 (bug, file tampering, or undetected determinism drift) |
| 8 | PlacementFailed — `Place_AssumedFill` could not regenerate |
| 9 | WriteFailed — partial-write failure during the rename step |

The reveal action is **idempotent**: a second invocation on an already-revealed
file (first byte `{` instead of `Z`) returns Ok without rewriting. The
regression corpus exercises the round-trip via `run_rando_corpus.py`'s ZRSR
sub-path — the race-mode entries in `tests/rando_corpus/manifest.yaml` (labels
prefixed `b-race-`, plus `*-race` variants covering entrance shuffle and
Triforce-Hunt) are part of the determinism CI matrix. Because the stamp is over
the full canonical JSON, these entries also transitively assert that the
regenerated `hints[]` and `entrance_mapping` sections are byte-identical at
reveal — a regression in either fails the round-trip.

**Tamper detection**: any single-byte flip in the suppressed file produces a
CRC mismatch on read; reveal returns code 3 without touching the original.
Hand-crafting a file with a divergent `generator_version` + recomputed CRC
returns code 5 (`VersionMismatch`) — the gen-version check fires before
expensive placement regeneration.

## Hints

Hints are **on by default** (`hints=on`; binary on/off, part of the canonical
settings since `kGeneratorVersion` 14). They never affect placement or logic —
hint text is generated *after* placement from a sub-RNG seeded by the placement
digest, so the same `(settings, seed)` always yields byte-identical hints.

**Telepathic tiles (in-game).** Reading any of the 15 vanilla telepathic tiles
surfaces a generated item-location hint instead of its vanilla flavor text. The
messaging engine (`Text_LoadCharacterBuffer`) gives the hint system first refusal
on the 15 vanilla tele-tile message IDs via `Rando_RenderHintMessage`; the
substitution is gated on an active rando slot, so vanilla play (and the RAM
compare) is unchanged. `hints=off` leaves the vanilla tile text in place.

**Murahdahla.** On Triforce-Hunt / Ganon-Hunt goals an extra hint summarizes how
many Triforce pieces are spread across how many regions. It is **spoiler-only
today** — the in-game Murahdahla NPC is an ALTTPR asm-added sprite the fork has
not ported, so there is no in-game surface for it yet.

**Fork-extension NPCs** (Storyteller + the Kakariko / Dark-World Fortune Tellers)
route existing vanilla NPC dialogue through the hint generator for extra in-game
hint locations. These are a fork addition (not in ALTTPR); spoiler-JSON keys for
them are `fork_`-prefixed so the ALTTPR-equivalent core (15 tiles + Murahdahla)
stays diff-clean. The Lake-Hylia Fortune Teller is not wired — it shares its room
index with the Kakariko one, so it has no runtime discriminator.

**Spoiler + race mode.** The JSON/text spoiler carries a `hints[]` array; under
race mode it is suppressed inside the ZRSR until `RevealSpoiler`. Because the
reveal stamp is over the full canonical JSON, the race-mode corpus entries
(including a Triforce-Hunt variant that exercises the Murahdahla path) assert hint
determinism: a drift in regenerated hint text fails the reveal round-trip.

## Cosmetics

Cosmetic shuffles (palette / sprite / music) are **client-local presentation**
settings, deliberately kept *outside* the seed. They never touch the placement
table, share string, `settings_hash`, or any canonical serialization — so they
cause **no `kGeneratorVersion` bump and no corpus change**, and two players on the
same `share_string` with different cosmetics play a byte-identical game that merely
*looks* different (the tournament-decoupling property). Each axis defaults OFF, so
an unopted run — rando or vanilla — is byte-identical to the original game (the
RAM/PPU compare stays clean).

They are configured via `zelda3.ini` (the normative source; the PC native settings
window has a convenience "Cosmetics" tab that writes the same keys):

| Key | Section | Values | Meaning |
|---|---|---|---|
| `CosmeticSeed` | `[Graphics]` | u64 (accepts `0x…`); default `0` | Seeds the cosmetic RNG. **`0` ⇒ derive from the active slot's `seed_u64`** (a fresh user still gets a shuffled-but-reproducible look); any non-zero value overrides. |
| `PaletteShuffle` | `[Graphics]` | `vanilla` \| `shuffled` \| `grayscale` \| `negative` (default `vanilla`) | BGR555 CGRAM transform applied after the vanilla palette flush. `shuffled` = per-16-color-group channel permutation; `grayscale` = luma collapse; `negative` = per-channel complement. |
| `SpriteShuffle` | `[Graphics]` | `off` \| `<folder>` (default `off`) | Deterministically picks one `.zspr` from the folder (filenames sorted with a locale-independent ASCII comparator so the pick is identical on every platform) and feeds it to the existing ZSPR loader. Resolved at launch. |
| `MusicShuffle` | `[Sound]` | `off` \| `on` (default `off`) | Remaps area background songs (band `[0x01,0x0F]`) via a Fisher-Yates bijection — no song is silenced or duplicated; control codes and out-of-band ids pass through. Game-facing music state is unchanged; only the audible output differs. MSU-1 keys off the remapped id. |

The cosmetic RNG is a **separate stream** from the placement/fill RNG, so cosmetics
can never perturb generation. Determinism (same effective seed ⇒ identical look on
every platform) is asserted in CI by `Cosmetic_SelfCheck` (part of
`--rando-selftest`).

**Out of scope** (tracked elsewhere or deferred): gimmick palette modes
(dizzy/sick/puke/blackout — need per-frame animated transforms); heart color /
heart-beep / menu speed / quickswap (cosmetic *setters*, not shuffles — a separate
QoL change); enemy/enemizer shuffle (gameplay — `add-rando-shuffles-and-minigames`).

## Tracker windows (PC)

On PC (behind `Z3R_NATIVE_SETTINGS_WINDOW`, the same gate as the native settings
window) the randomizer ships three rich, separate-OS-window trackers that
auto-update from live game state — no RAM-watcher heuristics or emulator/tool
desync, the headline advantage of a native port:

- **Item Tracker** — a live grid of the **real HUD item icons** (decoded from the
  game's 2bpp HUD graphics + palette into an RGBA atlas; dimmed when not owned,
  with level/count overlays), plus per-dungeon small/big-key/map/compass tracking,
  prizes, hearts, and magic. Consumables (mushroom/powder & flute/shovel) use the
  rando-aware shared-byte ownership.
- **Check Tracker** — every location grouped by region, tri-state
  **checked / available (reachable, unchecked) / locked**, with region counts, a
  summary + progress bar, filters (hide-checked, only-available, search), and an
  optional "show items" spoiler toggle (off by default, force-hidden for race
  seeds). This is the direct "what can I do right now given my items?" view.
- **Map Tracker** — the **real light- and dark-world overworld maps** (decoded
  from the in-game Mode-7 map graphic; Light/Dark toggle) with status-coloured
  region pins overlaid (hover for the region's check list); dungeon interiors are
  listed in a panel below. The decoders are verified by dumping PNGs via the
  `--dump-overworld-map` / `--dump-item-icons` dev flags.

### Architecture

- **`imgui_host.{h,cpp}`** — a small multi-window Dear ImGui host. One
  `ImGuiContext` per window; every entry point saves/restores the current ImGui
  + SDL-GL context so the single-context settings window (left untouched) keeps
  working. Tracker windows are created hidden at startup and driven once per game
  frame from `main.c` (event routing + `Z3RHost_RenderAll`).
- **`tracker_windows.{h,cpp}`** — the three window bodies; opened from the
  settings window's **Trackers** tab or via the optional hotkeys
  `RandoItemTrackerWindow` / `RandoCheckTrackerWindow` / `RandoMapTrackerWindow`
  (default unbound; bind in `zelda3.ini`). Windows opened at seed setup persist
  into gameplay.
- **Reachability bridge** (`rando.c`) — `Rando_BuildRuntimeCounts` maps the live
  `g_ram` inventory into the logical `RandoCounts` the predicate VM reads (the
  logic macros accept the progressive form, so progressive counts satisfy every
  tier); `Rando_GetLiveReachability` runs `Logic_ComputeReachability` memoized on
  the reachability-state counter and snapshots the result out of the shared
  buffer. Requires the format_version 2 settings blob (see *Save behavior*);
  absent → reachability suppressed.

On PC the legacy in-game OAM-overlay trackers (`hud.c`) are compiled out — the
ImGui windows are the single tracker system, and the legacy toggle keys
(`RandoToggleItemTracker` / `RandoToggleLocationTracker`) open the corresponding
window. Switch (no native-window support) keeps the OAM overlay. Open the windows
during play via `Ctrl+I` / `Ctrl+C` / `Ctrl+M` (default-bound) or from the
settings window's **Trackers** tab.

**Known limitations / follow-ups:** region pins are hand-placed (no per-location
geographic pin coordinates yet); and per-window visibility/geometry persistence
in `saves/rando_window.ini` is not yet implemented (windows open from the
Trackers tab / hotkeys each session). (Keysanity reachability and the dark-world
map background — earlier follow-ups — are now implemented.)

## Audit comment convention (for contributors)

Per `audit.md` §0.9, every write to a tracked inventory cell (`link_item_*`,
`link_bottle_info[*]`, `link_has_crystals`, etc.) MUST either:

1. Flow through `Rando_OnLocationCheck` (the §6 dispatch path); OR
2. Carry an explicit `// rando-exempt: <reason>` comment immediately above
   the write line.

Valid exemption reasons (per `audit.md` §0.2 classification):

| Tag | When to use |
|---|---|
| `state-shuffle` | The write preserves existing state (e.g., bottle drink → empty), not a new grant. |
| `cosmetic` | HUD redraw / animation only; does not affect game state. |
| `consumption` | The inverse of a grant (bomb use, arrow use). |
| `progress` | Story event flag; relevant to logic graph but not a §6 dispatch target. |

Example:

```c
// rando-exempt: state-shuffle — drink consumes the bottle contents
link_bottle_info[btidx] = 2;
```

`assets/scripts/check_audit_guard.py` enforces this convention in CI. After
Phase 0 closes (now done — see `audit.md` §0.9), the guard transitions from
report-only to strict at the start of §6 work.

## Generator version (`kGeneratorVersion`) bump policy

Per tasks.md §13.6, bump `kGeneratorVersion` (defined in `src/rando/rando.h`)
whenever a placement-affecting change lands. Triggers:

- `src/rando/rando_logic.c` — predicate VM changes
- `src/rando/rando_placement.c` — placement algorithm changes
- `src/rando/rando_rng.c` — RNG changes
- `assets/rando/logic.yaml` — logic graph changes
- `assets/rando/logic_parts/*.yaml` — per-region predicate authoring (merges into logic.yaml)
- `assets/rando/macros.yaml` — named-predicate macros (mirrors `app/Support/ItemCollection.php`)
- `assets/rando/item_registry.yaml` — item pool / registry changes
- `assets/rando/location_registry.yaml` — location registry changes (append-only adds advance the count, which is part of the determinism input)
- `assets/rando/op_registry.yaml` — op-code assignments
- `assets/rando/icon_atlas.yaml` — 5-icon hash output changes when atlas changes
- The `RandoSettings` struct or its canonical serialization order

Append-only location-registry additions advance the version and regenerate
the regression corpus for new seeds, but do NOT invalidate existing saves
(the embedded placement table preserves the older slot's interpretation).

### Serialization invariants

Two invariants underpin the bump policy and the byte layout pinned in
`openspec/specs/randomizer-core/spec.md`:

1. **The canonical settings serialization is append-only.** Byte positions,
   widths, and enum value assignments for existing fields never change. A new
   axis is appended at the end and bumps `kGeneratorVersion`; it never reorders
   or re-widths an existing field. This is what lets a slot written at version
   `N` load verbatim on an `N+k` binary via its embedded placement table.
2. **The enum space was fully declared up front.** Every enum value a setting
   can take (e.g. `world_state` open/standard/inverted/retro, `logic`
   NoGlitches..NoLogic, `mode_weapons` swordless, `accessibility` none) was
   enumerated at the canonical-layout stage. Later work *un-pins user input* to
   subsets of those already-declared values — it does not expand the enum
   space, so the byte representation of a given setting is stable across
   versions.

A corollary used throughout the corpus: the **default-settings placement
digest is preserved across bumps** — a bump that changes the default seed's
`placement_digest_hex` is a real placement change, not an inert one (see
below).

### Inert-change exception

A change in any of the listed paths that is provably **corpus
byte-identical under default settings** does not require a bump. The
proof shape: run the regression corpus against the modified binary
and confirm all 55 entries pass without manifest changes. This
exception covers:

- Trick-predicate authoring that adds new disjuncts evaluating FALSE
  under `tricks=0`, `logic=NoGlitches` (i.e., the gates collapse to
  their Phase A shape on every existing corpus seed). Slice 4 §7
  batches landed without bumps via this exception.
- Comment / formatting changes in any of the listed YAML files.
- Renames of internal symbols whose canonical-byte representation is
  unchanged.

When in doubt, bump. The corpus regen via
`python assets/scripts/bump_rando_corpus.py --apply` is cheap (~5
seconds locally) and the bumper is idempotent.

### Bumping procedure

1. Edit `src/rando/rando.h:22` — increment the integer and update
   the comment with a brief reason.
2. Rebuild the binary.
3. Run `python assets/scripts/bump_rando_corpus.py --binary=./bin/x64-Release/zelda3.exe --apply`.
4. Verify with `python assets/scripts/run_rando_corpus.py --binary=./bin/x64-Release/zelda3.exe` (expect all entries OK at the new version).
5. Commit the `rando.h` bump, the manifest changes, and any sources
   in the same commit. The commit message states the new version
   and the reason.

### Save / snapshot compatibility across bumps

- **Sidecar slots** (`saves/sram_rando.dat`): the embedded placement
  table preserves the older slot's interpretation. Loading a v=N
  slot on a v=N+k binary surfaces a one-time informational warning
  (`Rando_DetectVersionDrift`) and uses the embedded data verbatim.
  The slot is NOT regenerated.
- **Snapshots** (`Shift+F1..F10` save / `Ctrl+F1..F10` replay): the
  TLV-tail format carries `generator_version` in the
  `TAIL_RANDO_STATE` payload. Replay on a different version uses the
  embedded settings + placement; the runtime treats them as
  authoritative.
- **Suppressed race-mode files** (`<spoiler>.json` ZRSR format):
  `Rando_RevealSpoiler` enforces version match — a v=N file
  produced against the runtime's current v=N+k binary returns
  `kRandoReveal_VersionMismatch` (code 5). The race admin must
  reveal on the same generator version the seed was produced
  against.

### Bump case studies (recent)

For maintainers: real-world examples of when to bump and what
shifts as a result.

Current `kGeneratorVersion` is in `src/rando/rando.h` (search for `#define kGeneratorVersion`); recent bumps including the post-phase-b-merge regen at v32 are tracked in the manifest commit history. The case studies below show the bump *pattern* — see `git log -- src/rando/rando.h tests/rando_corpus/manifest.yaml` for the canonical bump history.

| Version | Change | Corpus impact |
|---|---|---|
| 12→13 | Slice 2 Standard EP YAML promoted from inverted-only | 11 placement_digests + 13 sphere_digests changed (EP-region-only); 28 unchanged. See `27b52dd` |
| 13→14 | Slice 7+8 §66 — settings canonical layout 24→28 bytes (`hints`, `boss_shuffle`, `drop_shuffle` added) | 52/52 corpus entries unchanged — canonical layout grew but defaults are all 0, so placement digest doesn't move. Only seeds with non-default new settings would diverge. Sidecar `kRandoSuppressedSpoilerSettingsLen` static-asserts the coupling |
| 14→15 | Slice 3a #52 — 7 new item-registry IDs for Retro shop consumables | Pool composition unchanged at default settings; Retro entries shift if pool difficulty changes |
| 15→16 | Cluster-audit H1 fix — `PlacementTable_ComputeDigest` 256→512 entry cap | 3 Retro corpus entries get new digests (the truncation was silently dropping 9 slots from the hash) |
| 16→17 | Slice 3a #53 part 2 — `LOCTYPE_Shop` identity-pinned per ALTTPR `Randomizer.php:737-750` | Retro placement changes; 3 Retro entries regenerated |
| 17→32 | Phase-b merge cumulative — slice 4 trick predicates, slice 5 hints generator, slice 7+8 boss/drop algorithms, inverted parity translation, audit-fix passes | 55/55 corpus regenerated (`baa393b`); most defaults inert per the `kgenver_inert_change_exception` invariant but several intermediate bumps shifted Retro/Inverted digests. See `git log v17..v32 -- src/rando/ assets/rando/` |
| 46→47 | Fork-extension hint NPCs (Storyteller + Kakariko/Dark-World Fortune Tellers, ids 17-19) add 3 entries to the spoiler `hints[]` | **Placement/sphere digests unchanged** (hints are post-placement; `generator_version` is not an RNG input) — corpus 69/69 byte-identical, **not regenerated**. The bump exists only so a pre-fork v46 **race-mode** seed fails reveal with an honest `VersionMismatch` ("regenerate") instead of a misleading stamp `Tampered`, since the race stamp is a SHA over the full spoiler JSON incl. `hints[]`. A reveal-only reproducibility bump, not a placement bump. |

The pattern: predicate changes that affect only one region (12→13's
EP gate) hit a subset of seeds; layout-only changes with default-zero
new fields (13→14) leave the corpus untouched; Retro-isolated rule
changes hit ~3 entries; a global predicate change (every
`CanKillEscapeThings`-gated location, every `CanBombThings()` caller,
etc.) would hit ~30+ Standard entries. Plan corpus regen time
accordingly when scoping a sprint.

## ALTTPR cross-compatibility (none)

The zelda3 randomizer is provenance-derived from ALTTPR (`alttp_vt_randomizer`)
in two ways:

- The 43 named macros in `assets/rando/macros.yaml` were hand-translated from
  `app/Support/ItemCollection.php` with per-method line-range citations.
- Location names, region grouping, prize/medallion conventions mirror
  `app/Region/{Standard,Open,Inverted}/*.php`.

ALTTPR's MIT license requires preserving its copyright in derivative works;
attribution appears in `NOTICE` (per task 13.9).

**The two share-string / spoiler formats are not interoperable.** An ALTTPR
share string fed to this binary is rejected with a "format mismatch" error
(per `Share_SelfCheck`); a zelda3-rando share string is not parseable by
alttpr.com. The settings semantics overlap but the canonical-byte order
differs, so `settings_hash` will not match between the two systems.

## Troubleshooting

### "BPS conflict" on extract

If `python assets/restool.py --extract-from-rom` fails complaining about an
asset hash mismatch and the rom path is correct, the extracted file likely
got patched by another tool. Delete `zelda3_assets.dat` and re-extract from
a clean US ROM (SHA-256 `66871d66be19ad2c34c927d6b14cd8eb6fc3181965b6e517cb361f7316009cfb`).

### Version drift warning on save load

Phase B feature. When a slot was written by an older `generator_version` than
the binary currently runs, a one-time informational warning surfaces on slot
load. The embedded placement table is honored; gameplay is unaffected.

### Sidecar atomicity

A crash during save leaves `sram_rando.dat.tmp` and the previous
`sram_rando.dat` intact (since the atomic rename only completes on full
flush). The next boot reads the previous-good `sram_rando.dat`.

## World-state notes

### Standard mode and the uncle's gift

In ALTTPR's Standard mode (and in this rando), the uncle's gift is **part of
the placement pool** — it can be any item allowed by the slot's `can_place`
restriction, not just the L1 Sword as in vanilla. Link starts swordless
(`link_sword_type = 0`) and the chosen item is collected from the uncle's
sprite handler at the standard receive path (`sprite_main.c:5733`, item id
0x00 in vanilla; the dispatcher rewrites the granted item).

The placement restriction on the uncle's slot rejects items that don't make
sense as a starting item: `MirrorShield`, `SilverArrowUpgrade`, `TitanMitt`,
`L4Sword`, `MagicMirror`, `MoonPearl`, `BookOfMudora` — see `02_uncle_standard_mode.yaml`
under `assets/rando/logic_examples/`. Other world-states (Open, Retro) place
no restriction on the uncle's slot — `WORLDSTATE_EQ(open)` short-circuits
the can_place predicate to true.

Standard mode also gates broader progression on the uncle pickup having
occurred; the virtual `RescuedZelda` item is granted when the uncle/sanctuary
escort completes, and dark-world / overworld access predicates reference it.

### Inverted world-state

Inverted flips the relationship between the light and dark worlds. Link
**starts in the dark world as a bunny** — the overworld the player wakes into
is the dark-world map, and progression runs **dark-world-first**: the dark
world is the early game, and the light world is opened up later (the reverse of
Open/Standard). This mirrors ALTTPR's `App\World\Inverted`, whose 24
`app/Region/Inverted/**/*.php` region files (2977 lines) re-route every region
gate for the inverted topology.

To make the bunny start survivable and the dark world traversable from the
first frame, Inverted seeds grant a fixed **starting inventory** of two items:

- **Moon Pearl** — keeps Link in human form in the dark world (without it he is
  stuck in the powerless bunny state and most actions are disabled).
- **Magic Mirror** — provides the dark→light return warp that the inverted
  routing depends on.

This is the only world-state with a non-empty starting inventory. Open, Retro,
and Standard grant nothing here (Standard's uncle's-gift is a separate
placement-pool mechanism, described above). The grant happens once at file-load
(`Rando_TryGrantStartingInventory`, gated on `kFeatures1_RandomizerActive` and
the `kRam_RandoStartingInventoryGranted` sentinel) and is idempotent across
save/reload — the sentinel persists in the Phase A `kRam_*` block so a mid-run
load does not re-grant.

Because the inverted topology relabels which overworld tiles a region occupies,
Inverted ships via a **static alternate edge table** (`kRandoEdges_Inverted[]`,
walked when `world_state == Inverted`) plus a per-screen visual tile overlay —
NOT a runtime region remap. The reachability seed starts from the inverted
counterpart of Link's House rather than the light-world spawn. (The Phase A
`RegionRemap` scaffold was dead identity code and was **retired** in the Phase C
entrance-shuffle work; see `add-rando-entrance-shuffle/design.md §1`.)

## Phase B+ roadmap

Planned (not promised) follow-on work.

### Phase B — chunked into 9 OpenSpec changes (2026-05-26)

All 9 changes are authored at `openspec/changes/add-rando-*` and pass
`openspec validate --changes`. Warm-up changes are fully authored
(proposal + spec deltas + tasks); larger changes are proposal-only stubs
with detail deferred to `/openspec-explore` at apply-time.

| # | Change | Slice | Scope | Status |
|---|---|---|---|---|
| 1 | [`add-rando-confirmation-icons`](../openspec/changes/add-rando-confirmation-icons/) | 9 | Visible per-item icon ancilla for §6.2 direct-grant placements | Full |
| 2 | [`add-rando-trackers`](../openspec/changes/add-rando-trackers/) | 1 | In-game item + location tracker overlays + checked-bitmap r/w paths | Full |
| 3 | [`add-rando-race-mode-reveal`](../openspec/changes/add-rando-race-mode-reveal/) | 6 | Spoiler suppression + `RevealSpoiler` action with SHA-256 stamp verify | Full |
| 4a | [`add-rando-inverted-world-state`](../openspec/changes/add-rando-inverted-world-state/) | 2 | Inverted region graph (2977 lines PHP) + Bug #12 starting-inventory wire | Stub |
| 4b | [`add-rando-retro-world-state`](../openspec/changes/add-rando-retro-world-state/) | 3 | Retro shop locations + dispatch + 4 Retro flags pinned | Full |
| 5 | [`add-rando-trick-logic-and-axes`](../openspec/changes/add-rando-trick-logic-and-axes/) | 4 + misc | `OP_TRICK` / `OP_DIFFICULTY_AT_LEAST` / `OP_GLITCH_LEVEL_AT_LEAST` handlers + `swordless` + `accessibility=none` + `pyramid_bow_upgrade=arrows` un-pin + Bug #7 per-item rewind | Stub |
| 6 | [`add-rando-hints`](../openspec/changes/add-rando-hints/) | 5 | New `randomizer-hints` capability: Sahasrahla / storyteller / bookshelf / Murahdahla generation + dialogue-ID injection | Stub |
| 7 | [`add-rando-shuffles-and-minigames`](../openspec/changes/add-rando-shuffles-and-minigames/) | 7 + 8 | Boss + drop-pool shuffles + §6.8 minigame dispatch (digging, hype-cave NPC, peg cave, treasure-chest minigame) | Stub |
| 8 | [`add-rando-switch-swkbd`](../openspec/changes/add-rando-switch-swkbd/) | §9.1c | libnx `swkbdCreate` / `swkbdShow` / `swkbdInputText` wrapper routed into `RandoTextField` | Stub |

See the [`openspec/changes/` index](../openspec/changes/README.md) for the
per-slice scope detail (files-to-touch, ALTTPR references) and the
change-folder breakdown.

Items folded into the changes above:
- `swordless`, `accessibility=none`, `pyramid_bow_upgrade=arrows`,
  Phase A1 audit Bug #7 (per-item rewind) — all in **#5
  `add-rando-trick-logic-and-axes`**.
- §6.8 minigame dispatch — in **#7 `add-rando-shuffles-and-minigames`**.
- §9.1c Switch software-keyboard — **own change #8
  `add-rando-switch-swkbd`** (Switch-manual-gated; no PC code path).
- §7.6 follow-on visible confirmation icons — **#1
  `add-rando-confirmation-icons`** (warm-up).
- Inverted + Retro picker un-gates — split across **#4a Inverted** and
  **#4b Retro** (each as ADDED Requirements to sidestep archive
  sequencing).

### Phase C

| # | Change | Scope | Status |
|---|---|---|---|
| C1 | [`add-rando-entrance-shuffle`](../openspec/changes/add-rando-entrance-shuffle/) | Entrance shuffle, composable axes (caves / dungeons / coupled / crossed / decoupled); Simple/Restricted/Crossed/Insanity as presets. **Coupled cave + dungeon entrance shuffle implemented** (Open/Standard), playtest-confirmed. ALL 38 cave interiors + **11 of 12 dungeons** shuffle (everything except Skull Woods; Ganon's Tower is an advanced opt-in, `shuffle_ganons_tower_entrance`). Caves use a per-seed region override, dungeons a per-seed edge overlay; both share the door overlay + coupled exit (capture source room at entry). The generation retry requires FULL reachability, so no entrance seed ships with stranded items. Save = regenerate π from (seed, packed axes, attempt) at slot load — entrance seeds are version-locked (a version-drift warning fires; regenerate after an update). `RegionRemap` scaffold retired. Crossed (cross-category) + Insanity (decoupled) modes and Skull Woods multi-entrance are still open. | Stages 1–2 done (playtest-confirmed) |

### Phase D

| # | Change | Scope | Status |
|---|---|---|---|
| D1 | [`add-rando-cosmetic-shuffles`](../openspec/changes/add-rando-cosmetic-shuffles/) | Palette + sprite + music shuffles. Cosmetic only; `cosmetic_seed` separate from `settings_hash`. | Stub |
| D2 | [`add-rando-customizer-mode`](../openspec/changes/add-rando-customizer-mode/) | Manual per-location placement + custom pool composition. Dispatcher API unchanged. | Stub |
| D3 | [`add-rando-major-glitch`](../openspec/changes/add-rando-major-glitch/) | Major-glitch logic level. Extends Phase B #5's `OP_GLITCH_LEVEL_AT_LEAST` to support `HybridMajorGlitches` + `NoLogic`. | Stub |
| D4 | [`add-rando-auto-tracker`](../openspec/changes/add-rando-auto-tracker/) | Local TCP server emitting per-event inventory + reachability state for external tracker clients. | Stub |

All Phase C/D changes are proposal-only stubs (proposal + 1-3 minimal spec deltas) — full design + tasks deferred to apply-time. Phase C requires Phase B #4a archived; Phase D D3 requires Phase B #5 archived.

See `openspec/changes/archive/2026-05-29-add-randomizer-support/tasks.md` §7 and §14 for the
acceptance gates per phase.

## References

- Spec baseline (Phase A, archived 2026-05-29): `openspec/specs/randomizer-*/spec.md` — normative requirements per subsystem (the live source of truth).
- Archived Phase A change: `openspec/changes/archive/2026-05-29-add-randomizer-support/`
  - `proposal.md` — high-level scope
  - `design.md` — design decisions and trade-offs
  - `tasks.md` — implementation task list (119/139; the 20 deferrals are tracked in the active Phase B/C/D changes)
  - `audit.md` — Phase 0 audit deliverable (closes the §6 gate)
- Active follow-on changes: `openspec/changes/` (see its `README.md` index)
- Upstream provenance: `alttp_vt_randomizer` (MIT) — sibling checkout
  expected at `../alttp_vt_randomizer/` for translation work; not required
  to build or play.
