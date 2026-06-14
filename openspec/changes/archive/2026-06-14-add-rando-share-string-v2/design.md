# add-rando-share-string-v2 — design

## Context

Grounded facts from the implementation:

- **v1 wire format** (`src/rando/rando_share.c:1-22`): magic `ZRSS`
  (0x5A 0x52 0x53 0x53) + `version[1]` + `settings_hash[16]` + `seed_u64[8]` LE
  + CRC-16-CCITT-FALSE over bytes [0..28] (LE) = 31 bytes → exactly 50 base32
  chars (RFC 4648 uppercase, no padding). The v1 parser requires exactly this
  length.
- **The v1 "version" byte IS `kGeneratorVersion`** — every producer writes
  `ss.version = (uint8)kGeneratorVersion`, and `rando.h` carries a
  `_Static_assert(kGeneratorVersion <= 0xFFu)` specifically for this byte.
- **Canonical settings**: `Settings_CanonicalSerialize` /
  `Settings_CanonicalDeserialize` (`rando_settings.c:218-345`),
  `kSettingsCanonicalLen` = 28. Layout is pinned and
  **append-only** (changes/README.md "Cross-cutting conventions"): [0..24]
  scalar fields, [25] packed entrance axes, [26] bit0 `enemy_shuffle` + bit1
  `customizer_active` + bits2-3 `traps`, [27] bits 0-1 `door_shuffle`. The
  deserializer is deliberately permissive about undefined bits. Serialize
  applies `apply_derived_rules`, so
  deserialize→serialize is a fix-point (self-checked round-trip,
  `rando_settings.c:853-888`).
- **Where the v1 string/blob lives** (the "identity" surfaces — all unchanged
  by this design):
  - Sidecar slot header `share_string[32]` raw blob — fixed size; a 44-byte v2
    blob does NOT fit.
  - Slot activation extracts the seed from raw blob bytes [21..28] LE
    (`rando.c:1633,1678-1679`, `SlotSeedFromShareString`).
  - The 5-icon visual hash input is the full 32-byte stored blob
    (`rando.c:2903-2929`; `randomizer-ui` spec ~line 120).
  - The active slot's display/reveal string is `Share_EncodeRaw` of the stored
    blob into `g_rando_active_share_string`,
    used for spoiler-path resolution (`Spoiler_ResolvePath` — the spoiler
    FILENAME is the share string), the beaten-seed reveal gate
    (`rando.c:2840-2896`), and the ZRSR expected-share compare
    (`rando.c:2598-2601`).
  - The ZRSR suppressed-spoiler file stores the share-string TEXT in a fixed
    64-byte field; a 71-char v2 string does NOT fit. The race stamp is a
    SHA-256 over the revealed JSON, which includes `meta.share_string`.
- **Native-window paste today** (`rando_window.cpp:1294-1341`,
  `RenderShareRow`): decodes, adopts `seed_u64`, sets
  `s_randomize_seed_each_generate = false`, and shows an amber inline note
  when `pending_hash` ≠ pasted hash. It cannot restore settings. The baseline
  `randomizer-native-window` spec scenario (~line 147) already over-claims
  widget restore — rotted vs v1 reality; this change makes it true.
- **Tooling**: `assets/scripts/check_rando_invariants.py:192,253` checks
  `meta.share_string` against `^[A-Z2-7]+$` with `len >= 16` (both forms
  pass; the JSON field stays v1 anyway). The corpus manifest stores **no**
  share strings (grep `tests/rando_corpus/`). No script consumes
  `--out-share-string` output.
- **Customizer**: `customizer_active` is canonical [26] bit1; the manifest
  pins live in a FILE, not in settings. `Rando_GenerateSlot` refuses
  `customizer_active` without a loaded manifest and refuses
  customizer+race-mode (`rando_generate.c:310-329`). The customizer change
  deferred a `customizer_seed` share field
  (`add-rando-customizer-mode/tasks.md` §6.4).

## Goals / Non-Goals

**Goals:**
- Lossless paste: a v2 string restores settings AND seed; Generate then
  reproduces the sharer's placement (same generator version).
- v1 strings keep decoding forever (seed-only + warning).
- A loud Generate-time confirmation when settings have drifted from the last
  pasted string (covers the v1-paste incident class immediately).
- Zero determinism / save-format / race-format impact; no
  `kGeneratorVersion` bump.

**Non-Goals:**
- NOT changing the stored seed identity (sidecar blob, ZRSR field, spoiler
  filename/JSON, icon hash) — v2 is transport-only (D1).
- NOT resolving the deferred customizer `customizer_seed` share encoding
  (`add-rando-customizer-mode` §6.4 stays open; D5 fences it off instead).
- NOT building Switch v2 entry (parked with `add-rando-switch-swkbd`; D7).
- NOT an auto-generate-on-paste flow — paste populates, the user still
  presses Generate.

## Decisions

### D1 — v2 is a transport format; the v1 raw blob stays the internal identity

Everything that *stores or compares* a share string keeps the v1 form: the
sidecar's 32-byte raw field (no save-format bump), the ZRSR 64-byte text field
(no race-file bump), the spoiler filename + `meta.share_string` (race stamp
byte-identical — adding/altering that JSON field would change the stamp for
every race seed and force a `kGeneratorVersion` bump per the 46→47 precedent in
`rando.h`), the icon-hash input, `SlotSeedFromShareString`, and the
reveal-gate compares. v2 exists at the **exchange surfaces** only: the native
window's display/copy/paste and the headless CLI's human-facing outputs.

*Alternative rejected*: "v2 everywhere" — would cascade into a sidecar
format_version 3 (the 32-byte field can't hold 44 bytes), a new ZRSR layout
(64-char field can't hold 71 chars), changed spoiler filenames that v1 slots
could no longer re-derive at reveal time, a race-stamp change (kGen bump), and
a corpus-manifest regen — all for zero functional gain, since the v2 string is
always *reconstructible* from data the storing side already has
(settings + seed).

Consequence to document (player-visible): the window displays/copies the
71-char v2 string while the file-select banner prefix comes from the v1
identity string — the two strings share only the first ~6 chars (the magics
differ in the 4th byte). The banner prefix remains the per-slot identity;
matching a friend's banner prefix still works because both slots store the
same v1 blob.

### D2 — v2 wire format (exact layout + length)

```
offset  size  field
0       4     magic "ZRS2" (0x5A 0x52 0x53 0x32)
4       1     generator_version (uint8 = kGeneratorVersion at encode time;
              mirrors v1 byte [4]; covered by the existing <=255 static assert)
5       1     settings_len (uint8 = kSettingsCanonicalLen at encode time, = 28)
6       28    settings_canonical (Settings_CanonicalSerialize output, verbatim)
34      8     seed_u64 (LE)
42      2     CRC-16-CCITT-FALSE over bytes [0..41] (LE) — same poly/init as v1
```

Total **44 bytes → exactly 71 base32 chars** (`ceil(44*8/5)` = 71; the final
char carries 2 payload bits + 3 zero-pad bits). General formula:
`chars = ceil((16 + settings_len) * 8 / 5)`.

- `settings_hash` is **not** embedded: it is derivable
  (`Settings_HashShort(Settings_CanonicalDeserialize(bytes))`) — embedding it
  would cost 16 bytes (~26 chars) for redundancy the CRC already covers.
  Decoders that need the hash (UI mismatch displays, slot stamping) recompute
  it; because serialize re-applies `apply_derived_rules`, recompute-after-
  round-trip is stable (existing self-check).
- `settings_len` is the forward-growth hinge: if a future change grows
  `kSettingsCanonicalLen` (a kGen-bump event with its own coupled-site
  cascade, see `[[canonical-size-coupling]]`), v2 strings grow with it and
  old/new binaries can still *locate* the seed+CRC. Rules in D3.
- **Dispatch is on the decoded magic, not the string length**: base32-decode
  the input (bounded by the new max), then `ZRSS` ⇒ require exactly 31 bytes
  (v1 parse), `ZRS2` ⇒ require exactly `16 + settings_len` bytes (v2 parse).
  This is airtight against the degenerate coincidence that some future
  `settings_len` makes a v2 string 50 chars long, and keeps one entry point.
- Constants: `kShareStringBase32MaxLen` 64 → **96** (max binary 60 bytes ⇒
  headroom for `settings_len` up to 44). Coupled buffers include the bridge
  share-string fields, CLI output buffers, and slot-generation result buffers.
  Active-slot identity strings remain v1/50-char data encoded through the same
  bound. The Switch textfield cap (`kRandoTextFieldMaxLen` 64) is deliberately
  NOT grown (D7).
- New decode statuses: `kShareDecodeNewerSettings` (v2 with
  `settings_len > kSettingsCanonicalLen` — "made by a newer version") joins
  the existing reject enum. The existing length/alphabet/alttpr pre-checks
  stay (the lowercase/base64-special early-reject in `Share_Decode:179-190`
  applies to both formats).
- Compile-time coupling: `_Static_assert(kSettingsCanonicalLen <= 255)` (fits
  the `settings_len` byte) and an assert tying `kShareStringBase32MaxLen` to
  `ceil((16 + kSettingsCanonicalLen) * 8 / 5) + 1` headroom, so a future
  canonical growth can't silently overflow the encode buffers.

### D3 — generator-version + settings_len compatibility rules

v2 embeds the producing binary's `kGeneratorVersion` (byte [4], like v1).
Canonical settings only mean exactly the same thing inside the same
generator's enum space, but the layout is **append-only** (pre-declared enums;
new axes pack into formerly-zero pad bits) — so cross-version deserialize is
layout-safe, while the *resulting placement* may differ. Rules on paste:

1. `settings_len == kSettingsCanonicalLen` and `generator_version ==
   kGeneratorVersion`: full restore, no warning.
2. `generator_version != kGeneratorVersion` (either direction),
   `settings_len <= kSettingsCanonicalLen`: restore settings (zero-extend the
   canonical tail when `settings_len <` — zero is the append-only default for
   every later-added axis) + adopt seed + show a visible warning that the
   generated placement may differ from the sharer's. This mirrors the slot
   loader's warn-don't-refuse convention (`randomizer-save / Embedded
   placement table — upgrade safety`) — except that unlike a slot, a pasted
   string has no embedded placement table, so the warning must say
   "may generate a different seed", not just "different version".
3. `settings_len > kSettingsCanonicalLen` (string from a newer binary that
   grew the canonical layout): **refuse** (`kShareDecodeNewerSettings`,
   widgets untouched). Honoring a prefix would silently drop unknown axes and
   generate a different seed — exactly the silent-divergence class this change
   exists to kill.
4. Restored settings additionally pass through `RandoWindowBridge_Validate`
   (`rando_window_bridge.h:100`); a validation failure refuses adoption
   (widgets untouched) with the validator's message. This catches
   out-of-range enum values a newer same-length string might carry.

*Alternative rejected*: hard-refusing ANY version mismatch — stricter than the
fork's own slot-load convention, and pointlessly hostile for the common case
(most bumps don't change placement for most settings); the warning + the D6
modal keep the failure loud without blocking.

### D4 — Sidecar and ZRSR storage: byte-identical (no new fields)

The slot keeps storing the v1 31-byte raw blob (+1 pad) at `@23`; format_v2
slots already carry the full `settings_canonical` after the checked bitmap
(`rando_save.h:239-248`), so nothing new is needed to *reconstruct* a v2
string from a slot if a future change wants a "copy from slot" surface (it
would require `settings_present == 1`; v1-era slots and snapshot-restores
without settings fall back to the v1 string). This change does NOT add that
surface — copy operates on the window's `pending` settings, which is where
paste lands anyway. ZRSR layout, stamp normalization scripts, and
`--reveal-spoiler`'s expected-share compare (v1 text vs v1 text) are
untouched.

### D5 — Customizer seeds are fenced out of v2

`customizer_active` travels in canonical byte [26] bit1, but the manifest
pins do NOT travel in settings — a v2 paste that restored
`customizer_active=1` without the manifest would either hard-fail at Generate
(`rando_generate.c:316-321`) or, worse, mask a wrong placement if a
*different* local manifest is loaded (the same settings_hash cannot
distinguish manifests; `customizer_seed` exists for that and is NOT yet in
any share string — deferred `add-rando-customizer-mode` §6.4). Stance
(recommended-simple per that deferral):

- **Copy** while `customizer_active` is set emits the **v1** string (status
  quo for customizer seeds — seed + hash only). No regression: that is
  exactly what customizer seeds share today.
- **Paste** of a v2 string whose canonical [26] bit1 is set is **refused**
  (no adoption, clear message naming the manifest as the reason). Such a
  string can only be hand-crafted or from a future version, and silently
  restoring it would produce a non-reproducible-by-string placement.

When §6.4 lands (`customizer_seed` + a manifest-distribution story), it can
lift both fences as its own format extension (the `settings_len` mechanism
plus the version byte give it room).

### D6 — Generate-while-mismatched confirmation modal (interim loud fix)

The bridge records `last_pasted_settings_hash16[16]` + an armed flag on EVERY
successful paste (v1: the embedded hash; v2: `Settings_HashShort` of the
restored settings). When the user presses Generate while armed and
`memcmp(pending_hash, last_pasted_settings_hash16, 16) != 0`, the window opens
a confirmation modal — "Settings no longer match the pasted share string.
Generating now makes a different seed." [Generate anyway / Cancel] — instead
of generating. Confirming proceeds and disarms; cancelling returns to the
window; a new paste re-arms with the new hash. The modal slots into the
existing `TryBeginGenerate` gate chain (alongside the asset-hash modal,
`rando_window.cpp:1353+`). With a v2 paste the hashes match by construction at
paste time, so the modal only fires if the user *edits* settings afterward —
which is precisely the "you are deviating from the shared seed" case. UI copy
follows the tooltip-brevity rule: short durable player-facts, no caveat dumps.

### D7 — Switch / in-game surfaces: v2 deferred, v1 path untouched

The Switch alphabet picker submits through `Share_PastePath`
(`select_file.c:2428`) into a `kRandoTextFieldMaxLen`(=64)-capped textfield —
a 71-char v2 string physically does not fit, and growing the grid-typing flow
for 71 chars is miserable UX with no Switch dev environment to verify
(standing parked-Switch policy, `changes/README.md`). Decision: Switch paste
stays v1-compatible; v2 entry on Switch rides with the parked
`add-rando-switch-swkbd` change (swkbd text entry is the right vehicle; bump
the textfield cap there). `Share_PastePath`'s contract (seed + hash16 out) is
unchanged. The file-select banner and icon hash are identity surfaces (D1) —
unchanged.

### D8 — Headless CLI emission

`--generate-seed` keeps `meta.share_string` (JSON) and the spoiler filename on
the v1 identity string (D1), and additionally emits the v2 exchange string:
the stderr summary prints both (`share_string:` v1 + `share_string_v2:`), and
`--out-share-string=<path>` writes the **v2** string (it is the
distribute-to-players artifact; the baseline spec scenario's wording — "the
base32-encoded share string … single line, no trailing newline" — still holds,
and no committed script parses that file). Race flow becomes: admin generates
with `race_mode=true`, distributes the v2 string; players paste → full
settings (incl. `race_mode=1`) + seed → Generate reproduces the placement;
spoiler suppression is slot-side and fail-closed
(`Rando_ActiveSlotHidesSpoiler`), so v2 leaks nothing the race sheet doesn't
already publish (the ZRSR file already embeds `settings_canonical` in the
clear — `docs/randomizer.md` "the settings are public on race sheets").

## Determinism / corpus verification (claim + how it was checked)

Share strings are generator **output**: `Share_Encode`/`Share_PackBinary`
consume `(settings_hash, seed_u64)`; `settings_hash` is computed by
`Settings_ComputeHash` from canonical bytes only (`rando_settings.c:365-369`);
the placer consumes `(settings, seed)`. Grep across `src/` shows no path that
feeds share-string bytes into hashing or placement — the only share-string
*readers* are the paste paths, `SlotSeedFromShareString` (seed extraction at
slot activation, v1 blob, unchanged), the icon hash (v1 blob, unchanged), and
the reveal compares (v1 text, unchanged). The race stamp covers the spoiler
JSON whose `share_string` field stays v1. Therefore: placement digests, sphere
digests, settings hashes, race stamps, and the corpus manifest are all
byte-identical — **no `kGeneratorVersion` bump**. Verification includes a full
corpus run with zero digest changes and no manifest regeneration.

## Risks / Trade-offs

- **Two user-visible string forms during transition** (v1 banner prefix vs
  v2 window string; old binaries reject v2 as wrong length). Mitigation: the
  paste error for a too-long string on an OLD binary is the existing
  "wrong length" message — acceptable; docs note "share strings from newer
  versions need that version". New binaries accept both forms forever.
- **A v2 string from a different kGen can still generate a different
  placement** (D3 rule 2). Mitigation: explicit warning text + the version
  byte displayed; refusing entirely was rejected as over-strict.
- **Out-of-range enum bytes in a crafted/foreign canonical blob.** Mitigation:
  `Settings_CanonicalDeserialize` itself range-checks every enum byte via
  `Settings_Validate` (FIX #5) and returns non-zero, which the paste path
  refuses (widgets untouched). `RandoWindowBridge_Validate` (D3.4) is the
  *cross-field* gate on top (pieces/customizer/race coherence), not the enum-
  range guard. CRC + magic + the self-check's new adversarial cases back both.
- **Buffer-size sweep risk** (`kShareStringBase32MaxLen` consumers). Mitigation:
  compile-time asserts (D2) + grep sweep task; MSVC and WSL builds both run
  (vcxproj registration not needed — no new TU).
- **Spec/impl drift in the baseline native-window scenario** is *resolved*,
  not introduced (the over-claiming paste scenario becomes true).

## Migration Plan

Purely additive. v1 strings decode forever; nothing stored changes form. No
slot, ZRSR, snapshot, INI, or corpus migration. Rollback = revert the commits;
v2 strings then fail decode with the v1 "wrong length" reject (safe).
