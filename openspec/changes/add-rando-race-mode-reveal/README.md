# add-rando-race-mode-reveal

Phase B Slice 6. Implements Phase A's drafted race-mode spoiler suppression (`randomizer-save/spec.md:163-173`) plus a full reveal action with SHA-256 stamp verification.

## Status

Authored: 2026-05-26. **Pending Phase A archive** before this change can validate.

## Read these in order

| File | Purpose |
|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities |
| [specs/randomizer-core/spec.md](specs/randomizer-core/spec.md) | Spec delta — MODIFIED spoiler-log emission (race-mode branch) + ADDED reveal-action entry point |
| [specs/randomizer-save/spec.md](specs/randomizer-save/spec.md) | Spec delta — MODIFIED race-mode-spoiler-suppression (file-format details + reveal cross-ref) |
| [specs/randomizer-ui/spec.md](specs/randomizer-ui/spec.md) | Spec delta — ADDED settings-screen toggle + file-select reveal UI |
| [tasks.md](tasks.md) | Implementation checklist (10 sections, ~40 tasks) |

No `design.md` — the spec carries the design decisions (file format struct, reveal entry point, version-mismatch refusal).

## Key files touched

- **Spoiler**: `src/rando/rando_spoiler.c` + `src/rando/rando_spoiler.h` (suppress-mode branch + on-disk struct)
- **Entry point**: `src/rando/rando.c` + `src/rando/rando.h` (`Rando_RevealSpoiler` + result enum)
- **UI**: `src/select_file.c` (settings-screen un-gate + file-select reveal menu entry)
- **Settings**: `src/rando/rando_settings.{c,h}` (un-pin `race_mode`)
- **CLI**: `src/main.c` (`--race-mode`, `--reveal-spoiler` flags)
- **Docs**: `README.md`, `docs/randomizer.md`

## Verification

- **Race-mode round-trip CI step**: at least 3 corpus seeds generated `--race-mode`, then `--reveal-spoiler`, all 3 must stamp-match.
- **Tamper test**: hex-edit a byte; reveal should fail with `kRandoReveal_CrcMismatch`.
- **Cross-version refusal**: hand-craft a suppressed file with mismatched `generator_version`; reveal should fail.
- **`placement_digest_hex` byte-identical** for non-race seeds before/after.

## Dependencies

- **Phase A archived first.** Deltas `randomizer-core`, `randomizer-save`, `randomizer-ui` post-archive.
- No upstream slice dependency.

## Why third

Small, well-scoped, learns the change-authoring pattern end-to-end. Per `docs/randomizer_phase_b.md` recommended ordering: ships before the big logic-translation slices.
