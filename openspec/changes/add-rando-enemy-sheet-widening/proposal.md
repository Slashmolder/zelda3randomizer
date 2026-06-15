## Why

Enemy-shuffle's GFX-sheet **widening** — rewriting a room's owned, unpinned subgroup
slots to load *other* sheets so the substitution pool grows beyond the room's vanilla
enemies — was built but force-disabled (`ES_ENABLE_SHEET_WIDENING 0`,
`src/rando/shuffle_enemies.c`). With it off, a dungeon room's candidate pool is just the
randomizable enemies already on its vanilla sheets (often ~5), so dungeon variety is low.
The `randomizer-shuffles` spec already carries placeholder clauses ("Until palette
requirements are modeled…", "while palette-aware widening is disabled…") reserving this
work. This change models the dungeon sprite palette so widening can be safely enabled.

It was disabled for two runtime hazards — both invisible to the corpus and
`--rando-selftest`, which are generation-only:

1. **Palette.** A widened sheet's enemies render under the wrong sprite palette. Dungeon
   sprite palettes come from `kDungPalinfos[hdr[1]] → palette_sp0l/sp5l/sp6l`; an enemy
   pulled from another room's sheet expects a palette this room never loaded.
2. **Forced-substitution fillability.** Widening *replaces* a slot's sheet, so the room's
   own enemies on that slot lose their tiles and the picker is *forced* to substitute
   them. If the new sheet carries no palette-compatible `killable && !cannot_key`
   substitute, the picker returns the vanilla type and that enemy spawns with its sheet
   gone — a garbage render, and in a key/shutter room a potential softlock.

## What Changes

- **Dungeon sprite-palette allowlist.** Build a per-enemy "seen-on-signature" set during
  the existing vanilla room scan, where a room's signature is its
  `kDungPalinfos`-resolved `(sp0l, sp5l, sp6l)` triple — the direct analog of the
  existing overworld `g_enemy_overworld_palette` allowlist.
- **Dungeon palette gate.** Extend the candidate-context gate
  (`candidate_allowed_in_context`, today overworld-only) to dungeons, active only when
  widening is enabled so the widening-off path stays byte-identical to today's
  playtested behavior.
- **Verify-then-commit sheet choice.** After choosing widened sheets for owned/unpinned
  slots, verify the resulting live 4-slot set still yields ≥1 valid substitute
  (`killable && !cannot_key && palette-compatible && sheets-loaded`, plus a water
  substitute where the room has a water source) for the room's forced substitutions;
  otherwise revert that slot to the vanilla-resolved sheet. This makes the garbage /
  softlock hazard impossible by construction and reuses the existing `pick_replacement`
  candidate logic. Overworld widening is symmetric (no killable requirement; gated on the
  area sprite-palette id that already exists).
- **Enable widening** (`ES_ENABLE_SHEET_WIDENING → 1`) and **bump `kGeneratorVersion`**
  to version-lock the live runtime behavior. Placement is untouched: no fill RNG, no
  predicate, so `placement_digest_hex` stays byte-identical and the corpus stays
  byte-identical. No new canonical setting and no canonical-length change (widening rides
  the existing `enemy_shuffle` axis, always-on when it is set).
- **Offline render validator.** A Python tool over `zelda3_assets.dat` that decompresses a
  candidate sheet and applies a room's `kDungPalinfos`-resolved sprite palette, to confirm
  widened enemies are palette-correct without a playtest (this feature's render
  correctness is otherwise playtest-gated).

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `randomizer-shuffles`: MODIFY the "Enemy shuffle GFX-sheet reshuffle and replacement
  safety" requirement — replace the "until palette requirements are modeled" /
  "while palette-aware widening is disabled" placeholder clauses with the dungeon
  palette-signature model, the dungeon palette gate, and the verify-then-commit
  fillability guarantee for both loader contexts. The `kGeneratorVersion` bump
  (`77→78` at merge — main concurrently took 77 for its medallion-config change, so the version-drift convention applied) is recorded
  as an inline note within this requirement, matching the existing convention
  (e.g. retro-generic-keys' `> kGeneratorVersion 53→54`) — `randomizer-core`'s
  `generator_version` *semantics* requirement is unchanged, so it is not a delta target.

## Impact

- **Code**: `src/rando/shuffle_enemies.c` (palette-signature build, dungeon gate,
  verify-then-commit sheet choice, flag flip, selfcheck invariants); a small
  signature accessor added to `src/dungeon.c` / `dungeon.h`
  (`kDungPalinfos` + `GetRoomHeaderPtr` are in scope there);
  `src/rando/rando.h` (`kGeneratorVersion`).
- **Generated data**: none required at build time — the dungeon palette allowlist is
  built at runtime from the shipped vanilla sprite blobs and room headers, like the
  overworld allowlist. `assets/scripts/gen_enemy_shuffle_tables.py` is unaffected.
- **Tooling**: a new offline render-validation script under `assets/scripts/`; no change
  to `gen_enemy_shuffle_tables.py` or the corpus tooling.
- **Corpus / determinism**: `placement_digest_hex` byte-identical for every seed; the
  corpus regenerates byte-identical at the new `kGeneratorVersion`. Race/version-lock
  behavior is the only generation-visible change (the bump itself).
- **Verification**: corpus regen (byte-identical), extended `--rando-selftest`
  invariants, offline render spot-checks; one owner end-to-end playtest for final
  render confirmation.
- **No `make clean` ABI trap** unless the `RandoSettings` struct changes — it does not
  here (no new canonical field). Editing `kGeneratorVersion` in `rando.h` still requires
  `make clean` under the Makefile (no header-dep tracking).
