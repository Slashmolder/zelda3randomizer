# add-rando-share-string-v2 — task tracking

> Transport-only format change: NO kGeneratorVersion bump, NO save/ZRSR/corpus
> change (design D1; the corpus run in §4.2 is the proof, not the assumption).
> Implemented 2026-06-12 on `claude/infallible-golick-a8edec` (rebased onto
> main @ 99f8476, kGeneratorVersion 70). Code + verification done; owner
> playtest (§4.5) and archive (§5.3) remain.

## 1. Encoder/decoder (`src/rando/rando_share.{h,c}`)

- [x] 1.1 Define the v2 wire constants per design D2: magic `ZRS2`
      (0x5A 0x52 0x53 0x32), layout `magic[4] | generator_version[1] |
      settings_len[1] | settings_canonical[kSettingsCanonicalLen] |
      seed_u64[8] LE | crc16[2] LE` (CRC-16-CCITT-FALSE over all prior bytes);
      44 bytes / 71 chars at the current canonical length.
- [x] 1.2 `kShareStringBase32MaxLen` 64 → 96 + compile-time asserts:
      `kSettingsCanonicalLen <= 255` (fits settings_len byte) and the encode
      buffer covers `ceil((16 + kSettingsCanonicalLen) * 8 / 5) + 1`. Asserts
      live in `rando_share.c`, not the header (it's included from a C++ TU
      where C11 `_Static_assert` is unavailable). Swept the constant's
      consumers and switched `g_rando_active_share_string[64]` (`rando.c`) to
      the constant. (As-built: the v2 payload size is the header constant
      `kShareStringV2BinaryLen = 16 + kSettingsCanonicalLen`.)
- [x] 1.3 v2 encode entry (`Share_EncodeV2`, from a `ShareString` carrying
      `version` / `seed_u64` / `settings_canonical[]`) + unified `Share_Decode`:
      base32-decode within bounds, dispatch on MAGIC (`ZRSS` ⇒ require 31 bytes
      / 50 chars, v1 parse; `ZRS2` ⇒ require exactly `16 + settings_len` bytes,
      v2 parse, CRC before newness), zero-extend a smaller-than-current
      `settings_len` tail, new `kShareDecodeNewerSettings` reject for a larger
      one (incl. a 16-char-prefix classification for >96-char tokens). All
      existing v1 rejects (alttpr/lowercase pre-check, length, base32, magic,
      checksum) kept live for both formats.
- [x] 1.4 `Share_SelfCheck` extension: v2 round-trip (real `Settings_SetDefaults`
      bytes + seed + recomputed-hash proof via `Settings_CanonicalDeserialize`
      + `Settings_HashShort`); **v1-compat decode of the pinned literal**
      `LJJFGU2DPXU3RXZRMN25JP63H4N25AK4VMOAAAAAAAAAAAEVFA` (genVer 67, seed 28)
      so v1 decoding can never silently rot; reject cases: truncated v2,
      corrupted v2 char, `settings_len > kSettingsCanonicalLen` (72-char +
      98-char-prefix forms), `settings_len < kSettingsCanonicalLen` zero-extend,
      v2-magic at v1 length + v1-magic at v2 length, and (audit MED-1)
      `Share_PastePath` refusing a short v2 string. Wired into `--rando-selftest`.

## 2. Native window (`rando_window.cpp`, `rando_window_bridge.{h,c}`)

- [x] 2.1 `RandoWindowBridge_RecomputeDerived` produces the v2 string for the
      display/copy surface (v1 when `pending.customizer_active` — design D5);
      `last_generated_share_string` (spoiler-save snapshot) stays the v1
      identity string (untouched — it is populated in `main.c` from the slot
      generator's result, not from `b->share_string`).
- [x] 2.2 `RenderShareRow` paste: v2 ⇒ `Settings_CanonicalDeserialize` into a
      temp, refuse if `customizer_active` set, refuse on
      `RandoWindowBridge_Validate` failure (widgets untouched), else commit to
      `pending` + `Pending_Changed()` + adopt seed +
      `s_randomize_seed_each_generate = false`; version-mismatch warning when
      the embedded `generator_version != kGeneratorVersion` (visible, 1-2
      short facts). v1 ⇒ today's seed-adopt + hash-mismatch warning (reworded
      to name v1 as the reason settings can't be restored). New error strings
      for `kShareDecodeNewerSettings` and the customizer refusal.
- [x] 2.3 Generate-mismatch confirmation modal (design D6): bridge records
      `last_pasted_settings_hash16` + `paste_armed` on every successful paste;
      `TryBeginGenerate` interposes the modal when armed and
      `memcmp(pending_hash, last_pasted_settings_hash16, 16) != 0` — checked
      BEFORE the seed reroll so Cancel mutates nothing; Generate-anyway disarms
      + re-enters the normal chain, Cancel stays armed; a new paste re-arms.
      The modal renders FIRST in the end-of-frame block so Generate-anyway can
      same-frame-chain into the asset/generating modals.
- [x] 2.4 71-char display fits: the share-string row uses `ImGui::TextWrapped`,
      which wraps (no truncation); Copy always sends the full NUL-terminated
      buffer. Confirmed `b->share_string` has exactly two consumers (display +
      Copy). No code change needed.

## 3. Headless CLI (`src/main.c`)

- [x] 3.1 `--generate-seed`: stderr summary prints `share_string:` (v1,
      unchanged) AND `share_string_v2:` (omitted for customizer seeds, which
      have no v2 form); `--out-share-string` writes the v2 string (v1 fallback
      for customizer), single line, no trailing newline. Spoiler JSON
      `meta.share_string` and the spoiler filename stay v1 (race-stamp
      neutrality — design D1/D8).
- [x] 3.2 Verified by run: a fresh `race_mode=1` seed generates, emits a
      71-char v2 string (independently decoded — magic `ZRS2`, genVer 70,
      settings_len 28, seed + CRC correct), and `--reveal-spoiler` on it still
      passes (ZRSR bytes + stamp untouched; no version gate fires — kGen
      unchanged).

## 4. Verification

- [x] 4.1 `--rando-selftest` green on both toolchains (incl. the new §1.4
      cases); `check_audit_guard --strict`, `check_determinism`,
      `check_codegen_wiring`, `check_no_embedded_data`,
      `check_placer_determinism`, `check_byte_order`, `check_door_tables`,
      `check_corpus_version_sync`, `check_logic_overrides`,
      `check_generator_version` all green.
- [x] 4.2 **Corpus run — zero-change**: fresh WSL build (`make clean` +
      forced codegen, then `make zelda3`); full 120-entry corpus byte-identical
      against the unmodified manifest; NO `generator_version` bump. (D1 holds.)
- [x] 4.3 Builds: MSVC Release x64 (no new TU; no vcxproj change) + WSL
      `make clean && make` (gcc `-Werror`) — both clean. One warning fixed
      mid-flight: `s_paste_error` sized 224→320 for a full validator message
      (`-Wformat-truncation`).
- [x] 4.4 Fresh-eyes audit per `[[cluster-audit-cadence]]`: 0 HIGH, 1 MED, 5
      LOW (recorded in [audit.md](audit.md)). MED-1 (`Share_PastePath` would
      seed-only a short v2 string with a zeroed hash) + LOW-6 (design.md risk
      note credited the wrong enum guard) FIXED; the other LOWs are within-spec
      or pre-existing (rationale in audit.md).
- [x] 4.5 Playtest (USER — the only reliable net for UI flows):
      (a) after pinning a seed with "Randomize seed each generate" off, copy v2
      → fresh window/changed settings → paste → all widgets restore + Generate
      reproduces the same placement (compare spoilers or banner icons). The
      pre-generate Copy button is disabled while the seed is still
      auto-randomized, because that seed is rolled at Generate time; generated
      random seeds are copied from the result popup instead;
      (b) paste a LEGACY v1 string (e.g. the §1.4 pinned one) → seed-only +
      warning;
      (c) edit a setting after paste → Generate interposes the modal; Cancel
      and Generate-anyway both behave;
      (d) race-mode v2 round-trip: settings restore incl. race_mode, spoiler
      stays suppressed on the generated slot;
      (e) customizer: copy emits v1; hand-built customizer v2 string is
      refused. <!-- done: owner playtest-confirmed the flow works and the
      pinned-seed/result-popup copy UX is acceptable. -->

## 5. Docs + index + archive

- [x] 5.1 `docs/randomizer.md`: share-string section documents both formats,
      the 71-char v2 length, paste-restore semantics, version handling, the
      customizer fence, the Generate confirmation, and the transport-only
      banner-vs-window consequence. CLI flag table row updated.
- [x] 5.2 `openspec/changes/README.md`: row updated to implemented / owner
      playtest pending / kGen "No".
- [x] 5.3 `openspec archive add-rando-share-string-v2` after owner playtest
      sign-off; re-read the deltas against as-built source FIRST (CLAUDE.md
      "Reconcile … BEFORE archiving"). <!-- ready: deltas re-read against source
      before archiving. -->
