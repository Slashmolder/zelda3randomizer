# Tasks — add-rando-fairy-chest-model

## 1. Runtime: drop the toss, grant on contact
- [x] 1.1 `Sprite_WishPond3` case 1: under `kFeatures1_RandomizerActive`, grant the next un-collected of the pond's two `Left`/`Right` checks directly (LW=Waterfall, DW=Pyramid). Receivable → `Link_CancelDash(); item_receipt_method=0; Link_ReceiveItem(lttp,0)`; direct-write → `Rando_ShowDirectGrantConfirmation`. Both-collected → dismiss (0x14b). Idle `ai_state=0; delay=255`.
- [x] 1.2 Remove the case-1 freeze-guard (moot: no item-picker is opened under rando).
- [x] 1.3 Remove both case-6 rando dispatch blocks (Waterfall + Pyramid) — dead once rando never advances past `ai_state 1`. Vanilla fall-through (cases 2-13) untouched.
- [x] 1.4 Pyramid Left vanilla code: `0x3b` (silver bow), NOT `0x43` (link_arrow_filler). Verified against `progressive_to_lttp` (rando.c:171). Waterfall Left=RedShield `0x05`, both Rights=BottleEmpty `0x16`.
- [x] 1.5 Confirm off-rando vanilla flow is byte-identical (RAM-compare safe); the rando branch is fully `kFeatures1_RandomizerActive`-gated.

## 2. Retire Pyramid Sword/Bow from the placement pool
- [x] 2.1 Delete registry entries 210/211 (`location_registry.yaml`).
- [x] 2.2 Delete the Standard predicate blocks (`logic_parts/43_darkworld.yaml`).
- [x] 2.3 Delete the Inverted override blocks (`logic_parts/inverted/DarkWorld/NorthEast.yaml`).
- [x] 2.4 Delete the stale `entrance_registry.yaml` doc entries (210/211) for hygiene.
- [x] 2.5 Regenerate codegen (`rando_logic_gen.py`): 328→326 locations, 0 warnings; `LOC_Pyramid_Fairy_Left/Right` keep ids 213/214 (no enum shift), `LOC__COUNT` unchanged.

## 3. Generator version + corpus
- [x] 3.1 Bump `kGeneratorVersion` 49→50 (`rando.h`) with provenance comment.
- [x] 3.2 `bump_rando_corpus.py --apply` — 79 digests regenerated, version 49→50.
- [x] 3.3 `run_rando_corpus.py` passes 79/79 against the regenerated baseline.

## 4. Guards + build
- [x] 4.1 Release build clean (`-Werror`); the retired `LOC_*` references compile-out.
- [x] 4.2 `--rando-selftest` all subsystems OK.
- [x] 4.3 codegen-wiring / determinism / audit-guard --strict / no-embedded-data all green.

## 5. Cross-change reconciliation
- [x] 5.1 Spec delta MODIFIES `randomizer-placement` "Item types receivable via dispatcher" (the great-fairy scenario) — authored here, not hand-edited into the published spec.
- [x] 5.2 Note in `add-rando-trick-logic-and-axes` that `LOC_Pyramid_Fairy_Sword` is retired and swordless logic must re-point.
- [x] 5.3 Note in `add-rando-confirmation-icons` that the Pyramid Fairy grant call site relocated (case 1) and now grants Left/Right; the §7.6 cue still fires there.

## 6. Playtest (owner — slot grant path has no automated test)
- [x] 6.1 Pyramid Fairy: contact grants exactly TWO checks, no toss, no freeze. *(VERIFIED 2026-06-03 — owner: item on the 1st and 2nd contact, nothing on the 3rd. The SilverArrowUpgrade-identity sub-case (`0x3b`) is source-verified against progressive_to_lttp; not necessarily exercised by the test seed's placement.)*
- [x] 6.2 Waterfall Fairy: same — two checks on contact, no toss. *(VERIFIED 2026-06-03 — owner: same two-then-nothing behavior.)*
- [x] 6.3 No double-grant from a phantom room-22 / waterfall-room chest. *(VERIFIED 2026-06-03 — exactly TWO grants per pond, nothing on the 3rd contact, on BOTH ponds → the dead chest path does not double-fire.)*
- [x] 6.4 Empty Y-inventory → no freeze. *(Structurally guaranteed: the rando path never opens the toss picker, so `RenderText_FindYItem`'s exitless loop is unreachable. The freeze code path is removed, not merely guarded.)*
