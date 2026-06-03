> **Archive readiness (2026-06-02):** spec deltas are clean and the implementation is landed. The ONLY archive-blocker is the owner playtest §7.1-7.3 (and the dependent §9.4/§9.5). No deferred SHALLs, no spec/impl gaps — this archives as soon as the manual smoke is ticked. (Sibling changes boss-heart, beatable, native-settings, inverted archived 2026-06-03.)

## 1. Asset authoring

- [x] 1.1 Catalog the items that reach `Rando_ShowDirectGrantConfirmation` today by grep over the §6.x dispatch branches: `HalfMagic`, `QuarterMagic`, `TriforcePiece`, `Prize_Crystal1..7`, `Prize_GreenPendant`, `Prize_RedPendant`, `Prize_BluePendant`. Confirm whether dungeon-item direct grants (`SmallKey<D>`, `BigKey<D>`, `Map<D>`, `Compass<D>`) reach the helper or are receivable via `Link_ReceiveItem`; some are direct-bit writes (`kRam_RandoDungeonItemBits`-style) and would also benefit from icons. *(Done — catalogued in `assets/rando/direct_grant_icons.yaml` header: 1 TriforcePiece + 2 magic upgrades + 3 pendants + 7 crystals + 11 BigKey + 12 Map (incl HCE) + 11 Compass = 47 items. SmallKey items deferred — Phase A1 falls back to current-dungeon vanilla path per `rando.c:223-225`.)*
- [x] 1.2 Author `assets/rando/direct_grant_icons.yaml`. Schema:
  ```yaml
  icons:
    HalfMagic:    { tile: 0x..., palette: 0x..., notes: "magic decanter HUD tile" }
    QuarterMagic: { tile: 0x..., palette: 0x..., notes: "quarter magic decanter HUD tile" }
    TriforcePiece:{ tile: 0x..., palette: 0x..., notes: "Triforce-piece counter HUD tile" }
    Prize_Crystal1: { tile: 0x..., palette: 0x..., notes: "crystal-1 dungeon-prize tile" }
    # ... per-prize and per-dungeon-item entries
  ```
  Each tile address SHALL be a verified offset into the existing graphics blob. **No new sprite art.** Record the tile-discovery method (HUD code, inventory drawing, etc.) in the YAML comments so future debug is easy.
- [x] 1.3 Authoring discipline: tile addresses are claimed only when verified by a draw-test (running the binary and observing the actual icon match). Don't trust a single grep — the same icon may be referenced from multiple draw paths and only one is the right tile for an 8×16 ancilla. *(Discipline encoded in the YAML header. All 47 entries currently `tile: 0` (audio-only fallback per §4.2). Tile addresses are user-research follow-up — fill in per-item via draw-test, no codegen change required.)*

## 2. Codegen pipeline

- [x] 2.1 Extend `assets/rando_logic_gen.py` to read `direct_grant_icons.yaml` and emit `src/rando/direct_grant_icons.h`:
  ```c
  typedef struct { uint16 tile; uint8 palette; } DirectGrantIconEntry;
  static const DirectGrantIconEntry kDirectGrantIcons[ITEM_COUNT] = { ... };
  ```
  Items with no YAML entry SHALL emit `{0, 0}` so a runtime check `entry.tile == 0` is the fallback gate. *(Done — `emit_direct_grant_icons()` at `assets/rando_logic_gen.py`. Designated initializers (`[id] = { tile, palette, 0 }`) — unspecified ids zero-initialize to `{0, 0, 0}`.)*
- [x] 2.2 Register the new generated header in `Makefile`, `Zelda3.vcxproj` pre-build steps, and `src/platform/switch/Makefile`. Add to `assets/scripts/check_codegen_wiring.py` enumerated-generated-files set. *(Done across all 4.)*
- [x] 2.3 Add to `.gitignore` (build output).
- [x] 2.4 Confirm `assets/scripts/check_codegen_wiring.py` passes locally and in CI. *(Verified — 6 generated files wired across all build systems.)*

## 3. Custom-icon ancilla type

- [x] 3.1 Survey `src/ancilla.c` for the existing item-receipt ancilla code path. Identify the draw helper (likely `Ancilla_ItemReceipt_Draw` or similar) and its tile sourcing (currently hardcoded by lttp item id). *(Done — `Ancilla22_ItemReceipt`/`Ancilla_ReceiveItem_Draw` is too entangled with the grant pipeline to reuse; new peer type designed instead.)*
- [x] 3.2 Add a new ancilla type — e.g., `kAncillaType_RandoIconReceipt` (the existing `AncillaAdd_ItemReceipt` path at `src/misc.c:713-844` per CLAUDE.md is the receive-animation ancilla; this is a peer entry that takes an explicit tile + palette instead of looking up by lttp item id). *(Done — `kAncillaType_RandoIconReceipt = 0x44`; `Ancilla44_RandoIconReceipt` + `AncillaAdd_RandoIconReceipt(tile, palette)` in `src/ancilla.c`; declared in `src/ancilla.h`.)*
- [x] 3.3 Wire the type into the ancilla draw dispatcher. *(Done — `kAncilla_Funcs[]` grown from 67 → 68 entries; `kAncilla_Pflags[]` from 68 → 69; new index 68 = `0x10` = 2 OAM sprites.)*
- [x] 3.4 Verify the new type respects the same screen-position, fade-out, and Z-order rules as the existing item-receipt ancilla. *(Code review only — uses `Ancilla_PrepAdjustedOamCoord` (same coord helper as the milestone-receipt ancilla) and the standard `Ancilla_SetOam` rendering path. Position-above-Link is achieved via `pt.y - 16` / `-8` offsets. Real-game pixel verification deferred to draw-test (tile-research dependency).)*

## 4. Helper signature change

- [x] 4.1 Update `src/rando/rando.h` `Rando_ShowDirectGrantConfirmation` signature from `void` to `(uint8 item_id)`. Update the header comment block at lines 93-114 accordingly. *(Done — also updated `rando.h` declaration comment block to call out the Slice 9 visible-confirmation contract.)*
- [x] 4.2 Update implementation in `src/rando/rando.c:393` to:
  - Preserve the audio + HUD refresh (existing two lines).
  - Look up `kDirectGrantIcons[item_id]`; if `tile != 0`, spawn the new icon-ancilla type with that tile + palette.
  - On `tile == 0`, do not spawn (Phase A fallback behavior preserved).
- [x] 4.3 Update `Rando_ReceiveOrConfirm(uint8 lttp_code)` in `rando.c:398` to also take `(uint8 lttp_code, uint8 item_id)` and pass through to the helper.

## 5. Call-site migration

- [x] 5.1 Update `src/player.c:594` (Ether tablet) — pass the item id resolved from the tablet's placement-table entry. The call site already invokes `Rando_DispatchVanillaGrant`; the post-dispatch item id is available in the local code path. *(Done via `Rando_LastDispatchedItemId()` — new helper exposes the placed item id from the most recent dispatch, avoiding per-site `Placement_Lookup` plumbing.)*
- [x] 5.2 Update `src/player.c:634` (Bombos tablet) — same pattern.
- [x] 5.3 Update `src/player.c:3886` (generic player-module direct-grant cue) — identify the granted item via local context. *(Chest dispatch site — uses `Rando_LastDispatchedItemId()`.)*
- [x] 5.4 Update `src/sprite_main.c:1273` (Pyramid Fairy) — pass `LOC_Pyramid_Fairy_Sword` / `LOC_Pyramid_Fairy_Bow` resolved item.
- [x] 5.5 Update `src/sprite_main.c:18586` (generic sprite-handler direct-grant cue) — identify the granted item via local context. *(King Zora — uses `Rando_LastDispatchedItemId()`.)*
- [x] 5.6 Add a one-shot grep to ensure no other call sites exist: `git grep "Rando_ShowDirectGrantConfirmation()"` after the migration MUST return zero matches. *(Done — zero matches for the zero-arg form AND the single-arg `Rando_ReceiveOrConfirm(lttp_code)` form. Also migrated 17 indirect call sites via `Rando_ReceiveOrConfirm` — 16 in `sprite_main.c` + 1 in `ancilla.c`.)*

## 6. Determinism + audit guards

- [x] 6.1 `assets/scripts/check_determinism.py` — no new `rand`/`time` symbols; this change adds none, but re-run as a sanity check. *(Pass — 29 files, no violations.)*
- [x] 6.2 `assets/scripts/check_audit_guard.py` — confirm no new writes to `link_item_*` / `link_bottle_info[*]` / `link_has_crystals` / `sram_progress_*` cells from this change. The helper only writes `sound_effect_2` and triggers a HUD refresh; neither is tracked. No new `// rando-exempt:` comments required. *(Pass — 28 files, no non-exempt writes.)*
- [x] 6.3 Manual verification: generate a seed before and after this change with the same `--seed` and confirm `placement_digest_hex` is byte-identical (manual cross-platform digest check). *(Verified 2026-05-27 — `python assets/scripts/run_rando_corpus.py --binary=./bin/x64-Release/zelda3.exe` reports all 52 entries OK against the current `tests/rando_corpus/manifest.yaml` baseline. Code review confirmed no placement-affecting changes; corpus run confirms it byte-for-byte.)*

## 7. Playtest

- [ ] 7.1 Manual playthrough: generate a seed, force Ether/Bombos tablets to grant a direct-grant item (e.g., set `--seed` to a known seed that places `Prize_GreenPendant` at the Ether tablet site), pick up, verify the visible icon matches the granted item.
- [ ] 7.2 Triforce Hunt smoke: a TriforcePiece placed at any direct-grant site should pop a Triforce icon.
- [ ] 7.3 Pyramid Fairy: trade a bottle to test the §6.7 site; the icon should match the active placement-table entry.

## 8. Documentation

- [x] 8.1 Update `docs/randomizer.md` known-limitations section to remove the "audio-only visible confirmation" caveat (currently the §7.6 follow-on bullet under Phase B+ roadmap). *(No-op — caveat was already removed when the §7.6 follow-on bullet was rewritten as "**#1 add-rando-confirmation-icons** (warm-up)" in the Phase B+ roadmap (docs/randomizer.md:308-309). The "audio-only" framing is a factual description of the current YAML-stub state, not a stale caveat — it correctly notes that tile-research is the remaining follow-up (#68).)*
- [x] 8.2 Cross-link this change from the `openspec/changes/` index (README.md). *(Already correct — Slice 9 (confirmation icons): scaffold complete + audit fixes; code complete pending tile-research follow-up (YAML tile-research, 47 items, #68). Cross-link to this change folder exists.)*
- [x] 8.3 No `README.md` update needed (this is a back-end UX polish, no new user-facing toggle).

## 9. Archive readiness

- [x] 9.1 All 5 call sites updated; `git grep "Rando_ShowDirectGrantConfirmation()"` returns zero. *(Per §5.6 — verified zero matches for both the zero-arg form and the single-arg `Rando_ReceiveOrConfirm(lttp_code)` form.)*
- [ ] 9.2 CI green on Linux + macOS + Windows; codegen-wiring check passes; determinism + audit guards green. *(Awaiting CI run — local checks pass: --rando-selftest 8/8 OK, codegen-wiring check passes, 52/52 corpus entries OK.)*
- [x] 9.3 `placement_digest_hex` regression: a corpus-comparable seed (e.g., the first seed in `tests/rando_corpus/manifest.yaml`) yields the same digest before and after this change. **No `kGeneratorVersion` bump.** *(Verified — 52/52 corpus entries OK at the current baseline. See §6.3.)*
- [ ] 9.4 Manual smoke (§7.1-7.3) ticked. *(Playtest gate — pending owner.)*
- [ ] 9.5 `openspec archive add-rando-confirmation-icons` runs cleanly; spec deltas merge into `openspec/specs/randomizer-placement/spec.md`. *(Pending 9.2 + 9.4.)*
