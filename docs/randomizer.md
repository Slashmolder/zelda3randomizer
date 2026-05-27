# Zelda3 Randomizer

> **Phase A1 status (kGeneratorVersion=10).** Foundation, RNG, share-string,
> predicate VM, codegen, audit, logic graph (28 regions / 28 edges / 237
> location predicates across all 13 dungeons + 11 overworld regions in
> Standard mode), assumed-fill placement with bounded retry + wall-clock
> budget, prize/medallion shuffles + dungeon-mode per-dungeon containment,
> sphere computation, goal-completability with strict refusal of
> un-completable seeds + Pedestal pendant-reachability + boss-heart
> identity pinning + item-pool difficulty caps, JSON + text spoilers with
> fallback-warning rollup + sphere_digest + seed_u64, sidecar save format
> aligned to spec, §6 grant-site dispatch (13 NPC sites + universal chest
> hook + ITEM→LttP translation for 53 of ~125 item ids including
> progressive items / rupees / bottles / dungeon items / SilverArrowUpgrade),
> snapshot tail TLV preserves rando placement across save/load,
> `--assets-must-be-vanilla` check, and `--print-assets-hash` are all
> landed. All 7 Phase A goals produce winnable seeds with 0 unreachable
> placements in Open / Standard / Retro modes at all dungeon-item modes.
> The regression corpus exercises 50 (settings × seed) configurations
> across the full Phase A axis matrix + 5 named celebrity seeds; CI runs
> the corpus on Linux + macOS for cross-platform digest determinism.
> Audit guard runs in `--strict` mode (every grant-site write either
> dispatches or carries an explicit exemption comment).

**Known limitations** as of this status:
- Inverted world-state seeds report ~32 unreachable until
  LinksHouse_Inverted region is declared (Phase A2 follow-on).
- §6 grant-site dispatch table for the universal chest hook is empty —
  individual chests still grant their vanilla items until the
  (dungeon_room, ordinal) → location_id table is authored.
  13 NPC sites (Bottle Merchant, Sahasrahla, Mushroom, Library, Uncle,
  Sick Kid, Purple Chest, Hobo, Stumpy, Old Man, Blacksmith, Master
  Sword Pedestal, Flute Spot) are wired and dispatch correctly when
  `kFeatures1_RandomizerActive` is set.
- TriforcePiece, HalfMagic/QuarterMagic, Rupoor, and the 10 prize
  items have no vanilla LttP receive code — when placed at a §6-wired
  site, they currently fall back to the slot's vanilla item. §6.2 work
  introduces direct receive helpers (counter increments, magic_consumption
  direct write). Triforce-Hunt seeds in particular need this to track
  pieces collected at non-Triforce slots.
- File-select / settings UI not yet wired — only CLI generation works.
- Per-item rewind inside an assumed-fill attempt is not implemented;
  the placer instead retries with a perturbed seed up to 8 times.
  The `--budget-seconds` flag bounds wall-clock time across retries.
- vanilla_assets_hash.h ships with an all-zeros placeholder; activate
  `--assets-must-be-vanilla` by running
  `python assets/scripts/dump_vanilla_assets_hash.py` against a clean
  extraction (or `./zelda3 --print-assets-hash` to view).

This document covers user-facing operation of the in-binary randomizer, the
share-string format, save behavior, audit conventions for contributors, and
the Phase B+ roadmap. Authoritative source for behavior decisions is the
OpenSpec change at `openspec/changes/add-randomizer-support/`.

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
| `accessibility` | `items`, `locations` | `items` (auto-set to `locations` for Completionist) |
| `dungeon_items.{small_keys,big_keys,maps,compasses}` | `vanilla`, `dungeon`, `wild` | `vanilla` |
| `prize_shuffle` | `true`, `false` | `true` |
| `medallion_shuffle` | `true`, `false` | `true` |
| `pieces_required`, `pieces_placed` | uint16 | (Triforce Hunt / Ganon Hunt only) |

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
- 3 slots × {80-byte header + embedded placement table + checked-location bitmap}.
- No 4th slot anywhere (per `audit.md` §0.6 and `randomizer-save` spec).

Slot header records: `slot_kind`, `generator_version`, `settings_hash`,
`share_string`, `last_vanilla_write_version`, `sram_slot_checksum_at_last_write`,
`placement_table_size`, `flags`, reserved.

Atomic-commit: write `<file>.tmp`, fflush, fsync (POSIX) / `_commit` (Windows),
rename atomically. Save order: sidecar first, then `sram.dat`.

Cross-version forward-compatibility (per `randomizer-save / Embedded placement
table — upgrade safety`): a slot written by `generator_version = N` loads on a
binary with version `N+1` and surfaces a one-time informational warning. The
embedded placement table is consulted; no regeneration is required.

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
- `assets/rando/item_registry.yaml` — item pool / registry changes
- `assets/rando/location_registry.yaml` — location registry changes (append-only adds advance the count, which is part of the determinism input)
- `assets/rando/op_registry.yaml` — op-code assignments
- `assets/rando/icon_atlas.yaml` — 5-icon hash output changes when atlas changes
- The `RandoSettings` struct or its canonical serialization order

Append-only location-registry additions advance the version and regenerate
the regression corpus for new seeds, but do NOT invalidate existing saves
(the embedded placement table preserves the older slot's interpretation).

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

See [`docs/randomizer_phase_b.md`](randomizer_phase_b.md) for the per-slice
scope detail (files-to-touch, ALTTPR references, effort estimates) and
[`docs/randomizer_phase_b_chunking.md`](randomizer_phase_b_chunking.md)
for the chunking plan, critique-agent history, and audit findings.

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

Entrance shuffle (uses the `RegionRemap` overlay reserved in Phase A and
activated in #4a Inverted).

### Phase D

Cosmetic shuffles (palette/sprite), customizer mode (uses dispatcher API
unchanged), major-glitch logic level (extends #5's
`OP_GLITCH_LEVEL_AT_LEAST` threshold space), auto-tracker server.

See `openspec/changes/add-randomizer-support/tasks.md` §7 and §14 for the
acceptance gates per phase.

## References

- OpenSpec change: `openspec/changes/add-randomizer-support/`
  - `proposal.md` — high-level scope
  - `design.md` — design decisions and trade-offs
  - `tasks.md` — implementation task list (the source of truth for what's done / what's left)
  - `audit.md` — Phase 0 audit deliverable (closes the §6 gate)
  - `specs/randomizer-*/spec.md` — normative requirements per subsystem
- Upstream provenance: `alttp_vt_randomizer` (MIT) — sibling checkout
  expected at `../alttp_vt_randomizer/` for translation work; not required
  to build or play.
