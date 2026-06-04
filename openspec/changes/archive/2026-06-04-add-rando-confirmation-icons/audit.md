# Audit log — add-rando-confirmation-icons

## Fresh-eyes audit 2026-06-01 (archive-readiness)

Reviewed: proposal.md, the generated `src/rando/direct_grant_icons.h`,
`assets/rando/direct_grant_icons.yaml`, `src/rando/rando.c` (`Rando_ShowDirectGrantConfirmation`),
`src/ancilla.c` (`Ancilla44_RandoIconReceipt`, `AncillaAdd_RandoIconReceipt`, dispatch tables),
and all 8 live call sites in player.c / sprite_main.c / dungeon.c.

Verified clean:
- Dispatch index: type 0x44 → `kAncilla_Funcs[type-1]` (index 67) → `Ancilla44_RandoIconReceipt`. Correct.
- Pflags index by raw type: `kAncilla_Pflags[0x44]` = 8 (reserves 2 OAM), table sized [69] (0..68). Correct.
- `kDirectGrantIcons[]` sized [132]; max used index 124; `Rando_ShowDirectGrantConfirmation` bounds-checks `item_id < icon_table_len` before subscript — no OOB even if ITEM__COUNT < 132.
- YAML gfx/big values cross-checked against the vanilla receive tables in misc.c (`kReceiveItemGfx`, `kReceiveItem_Tab1`) by LttP receive code: pendant 0x23/big2, crystal 0x28/big2, bigkey 0x2f/big2, map 0x21/big2, compass 0x16/big2, smallkey 0x0f/big0. All match.
- Audio-only sentinel (gfx=0) correctly set for HalfMagic/QuarterMagic/TriforcePiece — these have no vanilla receive-GFX bundle; falls back to Phase A audio+HUD path.
- Every call site passes `Rando_LastDispatchedItemId()` immediately after a `Rando_DispatchVanillaGrant`, so the last-dispatched id is fresh — no stale-icon risk.
- Boss-prize duplicate-icon suppression in dungeon.c:4724 is intentional and cleanly revertible; grant already happened upstream, so suppressing the cue has no side effect.
- `AncillaAdd_RandoIconReceipt` retires any prior live rando-icon before DMA (lines 7030-7033) so the shared 0x24/0x34 VRAM slot can't show stale tiles. Good defensive design.
- audit-guard / determinism / codegen-wiring all PASS.

### NEW findings

**LOW — Pendant palette→color assignment is unverified (cosmetic).**
`assets/rando/direct_grant_icons.yaml:77-79` / `direct_grant_icons.h:87-89` map
Green→oam_flags 0x38 (lttp 0x37), Red→0x34 (lttp 0x39), Blue→0x32 (lttp 0x38).
The oam_flags values are arithmetically correct for those LttP codes
(`(kWishPond2_OamFlags[code]*2)|0x30`: [55]=4→0x38, [57]=2→0x34, [56]=1→0x32),
but the *color↔LttP-code* attribution (which receive code is green vs blue vs red)
was not independently confirmed against a runtime/source authority during this audit —
`kValueToGiveItemTo[55..57]` are all -1 and don't disambiguate. If the palette mapping
were swapped the icon would render the wrong-colored pendant. Worst case is a cosmetic
mismatch (no crash, no softlock, race-mode-safe), so it does not block archive, but it
is exactly the kind of thing only a playtest catches — confirm the three pendant icons
show the correct colors during the visual playtest.
Suggested fix direction: none unless playtest shows a swap; then re-key the three
oam_flags by observed color.

**LOW — Stale comment: "index 68".**
`src/ancilla.c:19` says "Type 0x44 (index 68) is ... Ancilla44_RandoIconReceipt", but
dispatch is `kAncilla_Funcs[type-1]` so the correct funcs-table index is 67 (the comment
at line 259 says 67 correctly). Cosmetic doc drift only; no behavioral impact.

### Verdict
Archive-ready (audit-wise). No HIGH/MED. The two LOW items are a doc nit and a
playtest-confirmable cosmetic; neither blocks `openspec archive`. Standard reminder:
the per-item icon visuals (correct sprite + pendant color) are only confirmable by the
user's visual playtest, which the proposal already calls out.

### Playtest finding 2026-06-03 (HIGH — wrong icon, isolated to BigKey)
A Big Key randomized to the Sunken Treasure slot popped a **heart-piece** icon. Root
cause: the YAML derived every `BigKey_*` entry from LttP receive code **0x17**, but
`kMemoryLocationToGiveItemTo[0x17] == 0xf36b == link_heart_pieces` (NOT link_bigkey).
The real Big Key receive code is **0x32** (`kMemoryLocationToGiveItemTo[0x32] == 0xf366
== link_bigkey`), whose `kReceiveItemGfx[0x32] == 0x22`. The discovery-method comment in
the YAML had `link_bigkey=0xf36b` (a `0xf366` typo, last digit 6↔b); 0xf36b is a real
cell (heart pieces) that is ALSO in the table, so the wrong code resolved to a plausible-
looking heart bundle (gfx 0x2f). Triangulated against z3randomizer
`itemdatatables.asm:293` which lists big-key gfx `$22`.
- Why the 2026-06-01 audit missed it: the "cross-check" re-indexed `kReceiveItemGfx`
  by the SAME wrong code 0x17, so it confirmed internal consistency (0x17→0x2f) without
  ever checking that 0x17 was the right code for a big key. Circular verification.
- The grant itself was always correct — `dungeon_item_direct_grant` writes
  `link_bigkey |= bit` at 0xf366 regardless of the icon. Only the visible icon was wrong.
- Fix: 11 `BigKey_*` YAML entries → `gfx 0x22, big 2, oam_flags 0x38` (vanilla big-key
  receipt values: `kReceiveItemGfx[0x32]=0x22`, `kReceiveItem_Tab1[0x32]=2` single-tile,
  `(kWishPond2_OamFlags[0x32]=4)*2|0x30=0x38`). Map (0x33→0xf368) and Compass
  (0x25→0xf364) audited and use the CORRECT cells — bug isolated to BigKey.
- Follow-up correction (same day): the first fix attempt set `big=0` from a hand-miscount
  of the flat `kReceiveItem_Tab1` array (read line 44 as `2,2,0,0,...` when it is
  `2,2,2,2,0,0,0,...` → idx 50 = 2, not 0). A fresh-eyes audit caught it before build.
  `big` must be 2 (single 16x16 tile), matching the original (coincidentally-correct)
  value — only `gfx` and `oam_flags` were ever wrong. Lesson reaffirmed: index flat
  receive tables by re-reading the file, never by recalling the row layout.

### Resolution 2026-06-03
- LOW (stale "index 68" comment): FIXED at `src/ancilla.c:19` — comment now states
  both indices explicitly (funcs dispatch index 67 via `[type-1]`; Pflags index 68 via
  raw type 0x44).
- LOW (pendant palette→color unverified): folded into the playtest checklist as an
  explicit pendant-color check at tasks.md §7.1 (still owner-playtest-pending).
- Spec/tasks reconciliation: the §6.x audio-only items (HalfMagic / QuarterMagic /
  TriforcePiece, all `gfx == 0`) were described as icon-popping in the original spec
  Scenario 1 and task §7.2. Both rewritten to match the as-built `gfx == 0` audio-only
  fallback; spec Scenario 5 + the requirement body likewise corrected (the old
  "debug-build assert flags the missing entry" text described an assert that does not
  exist — the real gate is a silent `gfx == 0` sentinel). `openspec validate --strict`
  green after the edits.
