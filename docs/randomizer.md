# Zelda3 Randomizer

> **Phase A1 status (kGeneratorVersion=2).** Foundation, RNG, share-string,
> predicate VM, codegen, audit, logic graph (28 regions / 28 edges / 207
> location predicates across all 13 dungeons + 11 overworld regions in
> Standard mode), assumed-fill placement, prize/medallion shuffles, sphere
> computation, and goal-completability checks are landed and locally
> verified end-to-end. All 7 Phase A goals produce winnable seeds with
> 0 unreachable placements in Open / Standard / Retro modes when
> `dungeon_items.small_keys=dungeon`. Inverted mode is partial (start
> region not yet declared). §6 grant-site dispatch and the file-select /
> settings UI land in subsequent Phase A2 work.

**Known limitations** as of this status:
- Vanilla dungeon-item mode (the default) leaves ~39 locations
  unreachable per seed because `location_registry.yaml` lacks
  SmallKey_<dungeon> pin sites. Workaround: pass
  `dungeon_items.small_keys=dungeon` to the CLI.
- Inverted world-state seeds report ~32 unreachable until
  LinksHouse_Inverted region is declared (Phase A2 follow-on).
- §6 dispatch is not yet wired into game code paths — playing a
  generated seed in-game requires the file-select UI work landing
  per task 9.x.

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

Planned (not promised) follow-on work:

- **Phase B**: hint generation (Sahasrahla / storyteller / bookshelf /
  Murahdahla per ALTTPR `app/Services/HintService.php`); trick logic
  + glitch-logic-level predicates; `swordless` weapon mode;
  `pyramid_bow_upgrade=arrows`; `accessibility=none`; race-mode reveal;
  in-game trackers; boss/drop-pool shuffles.
- **Phase C**: entrance shuffle (uses the `RegionRemap` overlay reserved in
  Phase A).
- **Phase D**: cosmetic shuffles (palette/sprite), customizer mode
  (uses dispatcher API unchanged), major-glitch logic level, auto-tracker
  server.

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
