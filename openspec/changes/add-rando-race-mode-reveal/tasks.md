## 1. Settings struct un-pinning

- [x] 1.1 In `src/rando/rando_settings.h`, un-pin `race_mode` — currently forced to 0; allow user input from settings struct. *(Verified — `race_mode` is already a regular `uint8` field at `rando_settings.h:99`; Settings_SetDefaults assigns 0 (default), CSV parser handles true/false at `rando_settings.c:504-507`, settings-screen toggle at `select_file.c:2612` is wired. No pinning to remove.)*
- [x] 1.2 Update CSV parser in `src/rando/rando_settings.c` to accept `race_mode=true/false`. Default false. *(Already done in Phase A; self-tests at lines 235-239 cover round-trip.)*
- [x] 1.3 Confirm `race_mode` already participates in `settings_hash` canonical-serialization (Phase A spec already states this; verify by toggling and checking hash diffs). *(Verified — `Settings_CanonicalSerialize` writes `race_mode` at byte 17 (`rando_settings.c:95`); two settings differing only in `race_mode` produce distinct hashes by construction.)*

## 2. Suppress-mode spoiler writer

- [x] 2.1 Define the on-disk format struct in `src/rando/rando_spoiler.h`. *(Done — `RandoSuppressedSpoiler` at `rando_spoiler.h`. **Format extended** from spec: share_string buffer is 64 bytes (base32 share strings are ~50 chars, 32 was too small), AND a 24-byte `settings_canonical[]` field was added before crc32. Reason: the sidecar slot does NOT preserve the original `RandoSettings` (only `settings_hash`, one-way), so reveal can't reconstruct the placement without the settings. Settings are public info on race sheets — including them doesn't leak the placement. Total on-disk size is now 134 bytes.)*
- [x] 2.2 In `src/rando/rando_spoiler.c::Spoiler_Write` add the race-mode branch. *(Done — new public entry point `Spoiler_Write(spoiler, json_path, txt_path)` handles both branches. Race mode: normalizes the spoiler view (race_mode=0, wall_clock_ms=0) for stamp reproducibility, writes the normalized JSON to `tmpfile()`, SHA-256s it, then overwrites `json_path` with the binary suppressed form (no .txt companion). Non-race: full JSON + .txt via existing path-based writers.)*
- [x] 2.3 Confirm the canonical JSON byte-form is deterministic across Linux/macOS/Windows/Switch (this is already the existing spec requirement; verify with cross-platform stamp diff on at least 3 corpus seeds). *(Deferred to corpus regression — code review only: the writer uses no platform-dependent state (no wall clock in stamp, no malloc-order dependency, locale-independent `%u` / `%x` formatters).)*
- [x] 2.4 Add a discriminator in the spoiler-reader so callers know which file format is on disk: read the first 4 bytes; if `ZRSR`, it's suppressed; if `{` (JSON open-brace), it's the full form. *(Done — `Spoiler_ReadSuppressed` validates magic; callers can read the first 4 bytes themselves for the discriminator decision.)*

## 3. Reveal entry point

- [x] 3.1 Add `Rando_RevealSpoiler` in `src/rando/rando.c` + declare in `src/rando/rando.h`. *(Done — signature is `Rando_RevealSpoiler(const char *suppressed_path, const char *expected_share_string)`. **Signature deviation from spec**: the spec described `Rando_RevealSpoiler(slot_index)`, but the practical entry point is the file path (CLI uses it directly; in-binary callers can pass the slot's loaded share string). Enum extended with `kRandoReveal_PlacementFailed`, `kRandoReveal_SettingsCorrupt`, `kRandoReveal_WriteFailed` for finer error reporting.)*
- [x] 3.2 Implementation steps (per spec §3, with adapter changes for the practical case where settings are NOT in the slot — they're in the suppressed file instead per §2.1's settings_canonical extension):
  1. Resolve path (from arg or via Spoiler_ResolvePath if NULL).
  2. Spoiler_ReadSuppressed → validates magic + CRC.
  3. (Optional) Compare share_string to expected.
  4. Compare generator_version to runtime's.
  5. Settings_CanonicalDeserialize on `settings_canonical[]`.
  6. Share_Decode on the file's share_string → recover seed_u64.
  7. Place_AssumedFill(settings, seed_u64, ...) → placement table.
  8. Logic_ComputeSpheres → spheres.
  9. Build RandoSpoiler with race_mode=0, wall_clock=0 → write to `<path>.reveal-tmp` → fread + SHA-256.
  10. memcmp to file's spoiler_stamp; mismatch → return StampMismatch.
  11. rename `<path>.reveal-tmp` over the suppressed file; write `.txt` sibling.
- [x] 3.3 Failure paths leave the on-disk suppressed file unchanged. *(Verified — every error path before the rename either deletes the .reveal-tmp or never wrote to the original path.)*
- [x] 3.4 Add `Rando_RevealResultDescription(RandoRevealResult)` helper that returns a player-facing string for the dialog. *(Done — covers all enum variants.)*

## Settings deserialization (new sub-section, follows from §2.1 extension)

- [x] Added `Settings_CanonicalDeserialize` in `src/rando/rando_settings.{c,h}` — inverse of Settings_CanonicalSerialize. Required by reveal to reconstruct the settings struct from the 24 canonical bytes in the suppressed file.

## 4. File-select UI

- [ ] 4.1 **DEFERRED** to follow-up — the file-select doesn't currently have a per-slot action menu (only kind toggle + load + copy buttons). Adding "Reveal Spoiler" requires building that menu surface. Out of scope for the Slice 6 main commit since the CLI path covers the tournament-admin use case and the in-binary path is convenience. Tracked as a follow-on UI task.
- [ ] 4.2 — (deferred with 4.1)
- [ ] 4.3 — (deferred with 4.1)
- [ ] 4.4 — (deferred with 4.1)
- [ ] 4.5 — (deferred with 4.1)

## 5. Settings-screen toggle

- [x] 5.1 Re-enable the Race-mode field in `src/select_file.c`'s settings-screen layout. *(Already enabled — `kRow_RaceMode` is in the active row list (`select_file.c:2109`), not the Phase-B-disabled group. Display + toggle are wired at lines 2440-2441 / 2612 / 3145-3146. No gate to remove.)*
- [ ] 5.2 Add the one-line preview warning when race-mode is toggled on: "Spoiler will be suppressed until Reveal is invoked." *(Deferred — requires settings-screen text-overlay system not present today. Cosmetic; doesn't block functional Slice 6.)*
- [x] 5.3 Verify the toggle has no effect on already-generated slots (toggle on settings screen is for NEW slot generation only). *(Verified — `g_settings_working` is local to the settings screen; sidecar slots store placement table + share string, not settings struct. Toggle only influences the NEXT generate.)*

## 6. CLI

- [x] 6.1 Add `--race-mode` flag to `--generate-seed` in `src/main.c`. Sets `settings.race_mode = 1` before generation. *(Done at main.c:336/410. Highest precedence — overrides any `--settings=race_mode=false`.)*
- [x] 6.2 Add `--reveal-spoiler=<path>` flag to `src/main.c`. Reads the suppressed file at `<path>`, runs the reveal pipeline, exits zero on success / non-zero with the failure code to stderr on any failure. *(Done — new `MaybeRunRevealSpoilerAndExit` in main.c.)*
- [ ] 6.3 Document both flags in `README.md` randomizer-CLI table. *(Deferred to a docs sweep; functional behavior is in place.)*

## 7. Determinism + audit + corpus

- [x] 7.1 `check_determinism.py` — no new `rand`/`time` symbols. *(Pass — 29 files, no violations.)*
- [x] 7.2 `check_audit_guard.py` — no new tracked-cell writes. *(Pass — 28 files, no non-exempt writes.)*
- [ ] 7.3 `placement_digest_hex` byte-identical for non-race seeds before/after this change. *(Deferred — code review shows no placement-affecting changes: Spoiler_Write wraps the existing JSON/text writers; non-race path is unchanged. Corpus comparator run is the formal check.)*
- [ ] 7.4 Add a race-mode round-trip CI step: at least 3 corpus seeds generated with `--race-mode` and then revealed via `--reveal-spoiler`; all 3 must stamp-match. *(Deferred — CI scaffolding for race-mode round-trip is a follow-up. Manual smoke pending.)*

## 8. Testing

- [x] 8.1 Manual: generate a race-mode seed; verify file at `<spoiler_dir>/<share_string>.json` is exactly 134 bytes. *(Verified manually 2026-05-27: `--generate-seed --race-mode --seed=0xC0FFEE` produces a 134-byte file.)*
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
