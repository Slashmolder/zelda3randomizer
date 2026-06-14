# add-rando-share-string-v2 — share string embeds full settings

## Why

The current share string (v1) is `magic[4] | version[1] | settings_hash[16] |
seed_u64[8] | crc[2]` = 31 raw bytes → exactly 50 base32 chars
(`src/rando/rando_share.c`). The `settings_hash` is a truncated SHA-256 of the
canonical settings — **one-way**. So the paste path can only adopt the SEED:
the native window's `RenderShareRow` (`src/rando/rando_window/rando_window.cpp`)
adopts `seed_u64` and shows a small amber note when the user's current
`pending_hash` differs from the pasted hash; it cannot restore the settings.

Real incident (2026-06-12, the `add-rando-field-item-custom-art` playtest —
recorded at its `tasks.md` §5.3): a playtester pasted a share string while the
window held different settings. The seed was adopted, the quiet warning was
missed, Generate produced a **completely different seed**, and a debugging
round was spent on "is paste broken?". The honest root cause is the format:
v1 cannot make paste lossless.

This change defines share-string **v2**, which embeds the full
`kSettingsCanonicalLen`-byte canonical settings blob (`Settings_CanonicalSerialize`,
`src/rando/rando_settings.c`) so that "Paste share string" restores **settings
AND seed**. v1 strings remain decodable forever (seed-only + warning). As an
interim safety net that also covers v1 pastes, Generate gains a loud
confirmation modal when the current settings no longer match the last pasted
string's settings.

Note on the spec baseline: the existing `randomizer-native-window /
Share-string copy and paste via OS clipboard` scenario "Paste of a valid share
string populates fields" already *claims* "the settings widgets update to
reflect the decoded settings" — which the v1 implementation has never been
able to do (hash is one-way). This change makes the spec claim true and
rewrites that requirement to describe both formats accurately.

## What Changes

- **New v2 wire format** (design D2): `magic "ZRS2"[4] | generator_version[1] |
  settings_len[1] | settings_canonical[28] | seed_u64[8] | crc16[2]` = 44 raw
  bytes → exactly **71 base32 chars** (`ceil(44*8/5)`). `settings_hash` is NOT
  embedded — it is recomputed from the canonical bytes on decode. The decoder
  dispatches on the decoded **magic** (`ZRSS` = v1, `ZRS2` = v2), not on string
  length, so both formats coexist in one entry point.
- **v2 is a transport/exchange format only** (design D1). The 31-byte v1 raw
  blob remains the internal **seed identity** everywhere it is stored or
  compared: sidecar slot header `share_string[32]` (`rando_save.h`), the ZRSR
  suppressed-spoiler file's 64-byte field (`rando_spoiler.h`), the spoiler
  filename + JSON `meta.share_string`, the 5-icon visual hash input, and the
  race-reveal share compares (`rando.c`). **No save-format change, no ZRSR
  change, no race-stamp change, no `kGeneratorVersion` bump, corpus
  byte-identical.**
- **Native window**: "Copy share string" emits v2 (v1 when `customizer_active`
  — see design D5); the live share-string display shows the same v2 string.
  Paste of v2 restores all settings widgets via `Settings_CanonicalDeserialize`
  + adopts the seed + pins it (clears auto-randomize), gated through
  `RandoWindowBridge_Validate`. Paste of v1 keeps today's seed-only + warning
  behavior.
- **Generate-while-mismatched confirmation modal** (interim loud fix, ships in
  this change): pressing Generate while the recorded last-pasted settings hash
  differs from the current settings hash opens a confirmation modal instead of
  silently generating a different seed.
- **Generator-version handling** (design D3): v2 embeds the producing binary's
  `kGeneratorVersion`. Mismatched-version v2 strings still restore (canonical
  layout is append-only) with a visible "placement may differ" warning; strings
  whose `settings_len` exceeds the binary's `kSettingsCanonicalLen` are refused.
- **Headless CLI**: `--out-share-string` writes the v2 string (it is the
  distribution artifact; nothing machine-parses it — verified, no script under
  `assets/scripts/` or `tests/rando_corpus/` reads that file) and the
  `--generate-seed` stderr summary prints both forms. The spoiler JSON's
  `meta.share_string` stays the v1 identity string (keeps race stamps
  byte-identical).
- **Switch surfaces: v2 deferred.** The alphabet picker / `Share_PastePath`
  path stays v1-only (a 71-char string does not fit `kRandoTextFieldMaxLen`
  = 64, `rando_textfield.h`); deferral rides with the parked
  `add-rando-switch-swkbd` work.

## Capabilities

### Modified Capabilities

- `randomizer-core`: MODIFIED Requirement — **Share-string format** (defines
  v1 + v2 layouts, magic-based dispatch, emission surfaces, refusal rules,
  and the no-identity-change clause).
- `randomizer-native-window`: MODIFIED Requirement — **Share-string copy and
  paste via OS clipboard** (copy emits v2; v2 paste restores settings; v1
  paste keeps the warning; customizer + version-mismatch handling); ADDED
  Requirement — **Generate confirmation on pasted-settings mismatch**.

### Modified Capabilities (statement-only)

- `randomizer-ui`: ADDED Requirement — **Share-string v2 on Switch surfaces is
  deferred** (explicit: Switch paste stays v1-compatible; banner/identity
  surfaces unchanged).

## Impact

- **Code**: `src/rando/rando_share.{h,c}` (v2 encode/decode, new decode
  statuses, `kShareStringBase32MaxLen` 64→96 with coupled-buffer sweep),
  `src/rando/rando_window/rando_window.cpp` + `rando_window_bridge.{h,c}`
  (paste-restore, copy, modal, last-pasted hash), `src/main.c`
  (`--out-share-string` + summary line). NOT touched: `rando_save.*`,
  `rando_spoiler.*` layouts, `select_file.c`, placement/logic/codegen.
- **Determinism**: none. Share strings are generator OUTPUT — `Share_Encode`
  consumes `(settings_hash, seed)`; nothing feeds share bytes into
  `settings_hash`, placement, or the race stamp (the stamped
  `meta.share_string` stays v1). Verified by grep and by a full corpus run with
  zero digest changes and no manifest bump.
- **Save/race formats**: byte-identical (D1/D4). Older binaries decode v2
  strings as a length reject (their v1 decoder hard-rejects non-50-char
  input) — old binaries cannot half-apply a v2 string.
- **Effort**: small surface; the remaining gate is owner playtest of copy/paste
  round-trips, including race mode and a legacy v1 string.
