## 1. Settings struct un-pinning

- [ ] 1.1 In `src/rando/rando_settings.h`, un-pin `race_mode` — currently forced to 0; allow user input from settings struct.
- [ ] 1.2 Update CSV parser in `src/rando/rando_settings.c` to accept `race_mode=true/false`. Default false.
- [ ] 1.3 Confirm `race_mode` already participates in `settings_hash` canonical-serialization (Phase A spec already states this; verify by toggling and checking hash diffs).

## 2. Suppress-mode spoiler writer

- [ ] 2.1 Define the on-disk format struct in `src/rando/rando_spoiler.h`:
  ```c
  typedef struct {
    uint8 magic[4];          // 'ZRSR'
    uint16 generator_version; // LE
    uint8 spoiler_stamp[32]; // SHA-256
    uint32 share_string_len; // LE
    uint8 share_string[32];  // variable; pad zero
    uint32 crc32;            // LE, over previous bytes
  } RandoSuppressedSpoiler;
  ```
- [ ] 2.2 In `src/rando/rando_spoiler.c::Spoiler_Write` add the race-mode branch:
  - When `settings.race_mode == 1`:
    1. Build the full-spoiler JSON in memory with `race_mode = 0` in the canonical settings object.
    2. SHA-256 the canonical JSON bytes → `spoiler_stamp`.
    3. Populate `RandoSuppressedSpoiler`, CRC32 the prefix.
    4. Write the binary file at `<spoiler_dir>/<share_string>.json` (filename is the share string, but contents are binary, not JSON — the magic header distinguishes).
    5. Do NOT write `.txt` companion.
  - When `settings.race_mode == 0`: existing behavior (full JSON + text).
- [ ] 2.3 Confirm the canonical JSON byte-form is deterministic across Linux/macOS/Windows/Switch (this is already the existing spec requirement; verify with cross-platform stamp diff on at least 3 corpus seeds).
- [ ] 2.4 Add a discriminator in the spoiler-reader so callers know which file format is on disk: read the first 4 bytes; if `ZRSR`, it's suppressed; if `{` (JSON open-brace), it's the full form.

## 3. Reveal entry point

- [ ] 3.1 Add `Rando_RevealSpoiler(slot_index)` in `src/rando/rando.c` + declare in `src/rando/rando.h`. Return type: `RandoRevealResult` enum (`kRandoReveal_Ok`, `kRandoReveal_CrcMismatch`, `kRandoReveal_ShareStringMismatch`, `kRandoReveal_StampMismatch`, `kRandoReveal_VersionMismatch`, `kRandoReveal_FileNotFound`, `kRandoReveal_ParseError`).
- [ ] 3.2 Implementation steps (per spec §3):
  1. Compute path: `<spoiler_dir>/<slot_share_string>.json`.
  2. Read file; if missing, return `kRandoReveal_FileNotFound`.
  3. Read first 4 bytes; if not `ZRSR`, return `kRandoReveal_ParseError`.
  4. Parse the suppressed struct; verify CRC32. If mismatch, return `kRandoReveal_CrcMismatch`.
  5. Compare parsed share_string to slot's share_string. If mismatch, return `kRandoReveal_ShareStringMismatch`.
  6. Compare parsed generator_version to runtime's. If mismatch, return `kRandoReveal_VersionMismatch`.
  7. Regenerate full spoiler in memory using the slot's settings (with `race_mode` substituted to 0 for stamp purposes) + slot's seed_u64. This is the same code path the CLI's `--generate-seed` uses.
  8. SHA-256 the regenerated canonical JSON; compare to `spoiler_stamp`. If mismatch, return `kRandoReveal_StampMismatch`.
  9. Write the full JSON + `.txt` companion to disk; return `kRandoReveal_Ok`.
- [ ] 3.3 Failure paths leave the on-disk suppressed file unchanged.
- [ ] 3.4 Add `Rando_RevealResultDescription(RandoRevealResult)` helper that returns a player-facing string for the dialog.

## 4. File-select UI

- [ ] 4.1 In `src/select_file.c`, add the "Reveal Spoiler" entry to the per-slot action menu. Visibility gate per spec: `slot.kind == Randomizer && slot.race_mode == 1 && file_exists(<spoiler_dir>/<share_string>.json)`.
- [ ] 4.2 Confirmation dialog: "Reveal spoiler? This writes the placement to `<spoiler_dir>/<share_string>.json`. [Reveal / Cancel]".
- [ ] 4.3 On confirm: invoke `Rando_RevealSpoiler(slot_index)`; show a progress indicator (regeneration is bounded by the generation performance budget).
- [ ] 4.4 Success dialog: "Spoiler revealed at `<spoiler_dir>/<share_string>.json`."
- [ ] 4.5 Failure dialog: use `Rando_RevealResultDescription` for the message body.

## 5. Settings-screen toggle

- [ ] 5.1 Re-enable the Race-mode field in `src/select_file.c`'s settings-screen layout (currently the `race_mode` row is gated out — find the gate and remove it).
- [ ] 5.2 Add the one-line preview warning when race-mode is toggled on: "Spoiler will be suppressed until Reveal is invoked."
- [ ] 5.3 Verify the toggle has no effect on already-generated slots (toggle on settings screen is for NEW slot generation only).

## 6. CLI

- [ ] 6.1 Add `--race-mode` flag to `--generate-seed` in `src/main.c`. Sets `settings.race_mode = 1` before generation.
- [ ] 6.2 Add `--reveal-spoiler=<path>` flag to `src/main.c`. Reads the suppressed file at `<path>`, runs the reveal pipeline, exits zero on success / non-zero with the failure code to stderr on any failure.
- [ ] 6.3 Document both flags in `README.md` randomizer-CLI table.

## 7. Determinism + audit + corpus

- [ ] 7.1 `check_determinism.py` — no new `rand`/`time` symbols.
- [ ] 7.2 `check_audit_guard.py` — no new tracked-cell writes.
- [ ] 7.3 `placement_digest_hex` byte-identical for non-race seeds before/after this change.
- [ ] 7.4 Add a race-mode round-trip CI step: at least 3 corpus seeds generated with `--race-mode` and then revealed via `--reveal-spoiler`; all 3 must stamp-match.

## 8. Testing

- [ ] 8.1 Manual: generate a race-mode seed; verify file at `<spoiler_dir>/<share_string>.json` is 78 bytes or less (depending on share-string length).
- [ ] 8.2 Manual: invoke `Rando_RevealSpoiler` (or the CLI counterpart); verify the file is overwritten with the full JSON.
- [ ] 8.3 Tamper test: hex-edit a single byte in the suppressed file; reveal should fail with `kRandoReveal_CrcMismatch`.
- [ ] 8.4 Cross-version test: hand-craft a suppressed file with a `generator_version` that differs from the runtime's; reveal should fail with `kRandoReveal_VersionMismatch`.
- [ ] 8.5 Idempotency: reveal a slot that's already been revealed (suppressed file is now the full JSON); behavior should be a no-op success (or an overwrite-confirmation, per spec §4.4).

## 9. Documentation

- [ ] 9.1 `docs/randomizer.md` — add a "Race mode" section explaining the suppression + reveal flow with the file-format details.
- [ ] 9.2 `docs/randomizer.md` — un-gate the race-mode bullet in the CLI flags table.
- [ ] 9.3 `docs/randomizer_phase_b.md` Slice 6 status: mark complete; cross-link to this change.

## 10. Archive readiness

- [ ] 10.1 CI green on Linux + macOS + Windows; race-mode round-trip step passes on at least 3 corpus seeds.
- [ ] 10.2 Manual UI flow exercised on at least 1 desktop platform.
- [ ] 10.3 `openspec archive add-rando-race-mode-reveal` runs cleanly; spec deltas merge into `openspec/specs/randomizer-{core,save,ui}/spec.md`.
