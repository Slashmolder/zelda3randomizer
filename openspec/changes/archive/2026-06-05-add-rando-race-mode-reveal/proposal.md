## Why

Phase A reserved the `race_mode` setting bit in `settings_hash` (`randomizer-core/spec.md:191`) and drafted a Race-mode spoiler-suppression requirement in `randomizer-save/spec.md:163-173` with two minimal scenarios:
- on-disk file contains only the share string + SHA-256 stamp;
- reveal regenerates and verifies.

**Both are unimplemented.** `race_mode` is currently pinned to 0 in the settings struct (`rando_settings.h:88`); the spoiler writer always emits the full payload.

Race mode is a community feature: tournament admins want to seed a race without distributing the placement table. Each player loads the same share string, plays "blind" with no spoiler available, and the race admin can reveal the spoiler post-race for verification (checking that all players ran the same actual placement, that no one's reconstructed binary modified placements out-of-band).

The local-binary equivalent is small in scope but high in value for the ALTTPR-style race community. Phase A scoped it as Phase B; this change ships it.

## What Changes

- **Suppress-mode spoiler writer**: when `race_mode == 1` at generation time, the on-disk spoiler at `spoilers/<share-string>.json` contains ONLY the share string, a 4-byte magic header (`ZRSR` — Zelda Rando Spoiler Race), the `kGeneratorVersion` used to generate the stamp, and a 32-byte SHA-256 stamp of what the full spoiler *would have been* with `race_mode == 0`. The sibling `.txt` text spoiler is also suppressed; both files are tiny.
- **Stamp algorithm**: SHA-256 over the byte-canonical JSON form of the full spoiler (with `race_mode` cleared in the JSON's settings object — the stamp is over the placement, not over the race-mode flag itself). This makes the reveal verifiable: anyone with the share string can regenerate the spoiler, hash it, and compare.
- **`RevealSpoiler` action**: new entry point `Rando_RevealSpoiler(slot_index)` that:
  1. Reads the suppressed spoiler at `spoilers/<share-string>.json`.
  2. Extracts the share string + stamp.
  3. Regenerates the placement table from the share string + settings (the same path the CLI generator uses).
  4. Writes the full spoiler to disk (overwriting the suppressed file).
  5. Computes the SHA-256 of the regenerated spoiler with `race_mode == 0` in the canonical settings object; compares to the stamped value.
  6. Returns success / failure result; on failure, the suppressed file is preserved (no overwrite).
- **UI entry point**: the file-select screen's per-slot action menu gains a "Reveal Spoiler" option, available only when the active slot is a race-mode slot with a suppressed spoiler on disk. UI flow: confirm → run action → display result (success: "Spoiler revealed at `<path>`"; failure: "Stamp mismatch — spoiler may have been tampered with").
- **Settings-screen toggle**: race-mode toggle becomes user-controllable from the settings screen (currently pinned to 0). When the user toggles race mode on, the settings-screen preview clarifies the consequence ("No spoiler will be written until reveal is invoked").
- **CLI flag**: `--race-mode` on `--generate-seed` enables suppression for CLI-generated seeds. `--reveal-spoiler=<path>` is a CLI counterpart to the in-binary reveal action.
- **No `kGeneratorVersion` bump.** Spoiler-write behavior changes; placement output is unchanged.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `randomizer-core`: MODIFIED Requirement on the spoiler-writer contract — when `race_mode == 1`, the spoiler-writer emits only the share string + stamp.
- `randomizer-save`: MODIFIED Requirement on "Race-mode spoiler suppression (Phase B)" — flesh out the suppress-mode file format, the stamp algorithm, and the reveal action's success/failure handling. The Phase A scenarios become a subset of the expanded requirement.
- `randomizer-ui`: ADDED Requirement for the file-select reveal action UI; ADDED Requirement (or modification) for race-mode settings-screen toggle availability.

## Impact

- **Code**: `src/rando/rando_spoiler.c` (suppress-mode writer + stamp algorithm), `src/rando/rando.c` + `src/rando/rando.h` (`Rando_RevealSpoiler` entry point), `src/select_file.c` (file-select UI action), `src/main.c` (CLI `--race-mode` + `--reveal-spoiler` flag parsing).
- **Settings struct**: `race_mode` un-pinned in `rando_settings.h`; settings-screen toggle wired.
- **No new assets**.
- **Determinism guard**: no new `rand`/`time` symbols.
- **Audit guard**: no new tracked-cell writes.
- **`placement_digest_hex` byte-identical** before/after for non-race seeds. Race-mode seeds bypass the digest check since the spoiler doesn't ship.
- **CLI test**: the regression-corpus's CI run includes a race-mode pass that verifies `RevealSpoiler` round-trips on at least 3 corpus seeds.
- **File-format compat**: the suppress-mode file format is small and entirely new. A vanilla zelda3 binary doesn't read these files; only the rando-aware build does, so no cross-binary compatibility issues.
