# add-rando-share-string-v2 — fresh-eyes audit

Date: 2026-06-12. Reviewer: independent agent (read-only), briefed on
`git log 99f8476..HEAD` + the two spec deltas. Baseline: main @ 99f8476.
Scope: the v2 codec, the native-window paste/modal flow, the CLI emission,
and every consumer of the changed share-string surfaces.

**Verdict: 0 HIGH, 1 MED, 5 LOW.** The encoder/decoder is memory-safe across
the full adversarial input range (`bin[]` indexing is bounded by the exact-
length check forcing `t <= 60 = sizeof bin`; `bin_len == t` for every
`settings_len`), magic-based dispatch is sound (a coincidentally-50-char v2
still parses as v2), all `Share_Decode` consumers gate field reads behind
`kShareDecodeOk`, the customizer fence holds on every emit path with paste-side
defense-in-depth, and the v1 identity surfaces are untouched.

## Findings

- **MED-1 — `Share_PastePath` would silently seed-only a short v2 string.**
  FIXED (commit `fix(rando/share): Share_PastePath refuses a v2 string`). The
  v1-only Switch/textfield surface (D7) claimed a v2 token "cannot fit"
  `kRandoTextFieldMaxLen` (64), but a v2 string with `settings_len <= 24` is
  `<= 64` chars (`settings_len 15` = exactly 50) — it fits, decodes as v2 with
  a ZEROED `settings_hash`, and its canonical settings are dropped, so
  `Share_PastePath` returned it seed-only: the exact silent-divergence class v2
  exists to kill. Latent today (the encoder only mints 71-char v2), reachable
  via a crafted/future string, and the Switch hash consumers are deferred — but
  closed now with an explicit non-v1 refusal (`kShareDecodeBadMagic`) + a
  `Share_SelfCheck` case. Header comment corrected.

- **LOW-6 — design.md risk note credited the wrong enum guard.** FIXED. The
  "Risks" note said `Settings_CanonicalDeserialize` does not range-check enums
  and relied on `RandoWindowBridge_Validate`; in fact the deserializer calls
  `Settings_Validate` (FIX #5) and returns -2 on out-of-range enums, which the
  paste path refuses. `RandoWindowBridge_Validate` is the cross-field gate, not
  the enum-range guard. Corrected so the archived baseline is accurate.

- **LOW-2 — v2 paste skips the widget piece-count clamp.** ACCEPTED. The
  interactive spinners clamp `pieces_placed`/`pieces_required`; a pasted `tmp`
  is committed after only `Settings_Validate` + `RandoWindowBridge_Validate`,
  which leave `pieces_*` at any CLI-legal value. This mirrors the headless CLI's
  permissiveness (the generator handles it identically); a UI-consistency wart,
  not a crash/softlock. Left as "paste mirrors CLI."

- **LOW-3 — the >96-char prefix-classification path skips CRC.** ACCEPTED. A
  corrupted over-long `ZRS2` token reports `NewerSettings` rather than
  `BadChecksum`, because a CRC can't be checked on a length the binary can't
  parse. Genuine limitation, no behavioral risk; the block comment already
  explains the prefix decode.

- **LOW-4 — `paste_armed` persists across window close/reopen.** ACCEPTED
  (within D6 intent). Arming is cleared only at process start and on
  Generate-anyway; reopening for a new slot keeps the last paste armed. The D6
  modal then correctly fires when settings deviate from that paste — the user
  IS still deviating from the last thing they pasted. No incorrect generation
  results (the modal only gates).

- **LOW-5 — Copy can copy a phantom seed when auto-randomize is on.**
  ACCEPTED (pre-existing, not a v2 regression). When
  `s_randomize_seed_each_generate` is set the live display already shows
  "(rolled at Generate — seed not chosen yet)", but Copy still emits a v2 token
  for the stale `seed_u64`. Predates v2; the primary post-generate share
  affordance (`RenderGenerateModal` success → `last_generated_share_string`) is
  unaffected. Out of scope for this change.
