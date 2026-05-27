## 1. Provenance grep + audit-md update

- [ ] 1.1 Enumerate the 42 shop entities in `../alttp_vt_randomizer/app/Region/Standard/**/*.php`. Run `grep -hE "new Shop|new Shop\\\\(Upgrade|TakeAny)"` and record per-shop file/line. Classify each as `Shop` / `Shop\Upgrade` / `Shop\TakeAny`.
- [ ] 1.2 For each `Shop` and `Shop\TakeAny`, enumerate the purchasable inventory by inspecting `->setInventory(...)` calls. Record per-slot item id and rupee cost. Expected total: ~30-40 purchasable slots.
- [ ] 1.3 Add a new section `audit.md §"Retro shop provenance"` to the parent change's audit (or as a Phase B addendum) listing every shop with PHP source-line citation per `add-randomizer-support / audit.md §0.10` translation discipline.
- [ ] 1.4 Cross-check: confirm no shop slot collides with an existing `LOC_<id>` in `assets/rando/location_registry.yaml`. Each new shop slot gets a unique numeric id appended to the registry (append-only convention preserves Open/Standard digests).

## 2. Location registry expansion

- [ ] 2.1 Add per-shop-slot location ids to `assets/rando/location_registry.yaml`. Naming convention: `Light_World_Kakariko_Shop_Slot1`, `Light_World_Kakariko_Shop_Slot2`, `Dark_World_Potion_Shop_Slot1`, etc. — kebab-cased into the existing snake_case-ish convention.
- [ ] 2.2 Add `world_state_filter` per location: every Retro shop slot has the `Retro` bit set, NOT the `Open/Standard/Inverted` bits — only Retro seeds include them in the pool.
- [ ] 2.3 Take-Any caves get the same Retro-only filter.
- [ ] 2.4 Capacity-upgrade slots (`Light_World_Lake_Hylia_Capacity_Upgrade_Bomb` etc.) get Retro-only filter AND a `identity_placed` marker so the placer pins them to their vanilla item.
- [ ] 2.5 Run `assets/rando_logic_gen.py` to regenerate `src/rando/location_ids.h`; verify the new `LOC_<...>` defines appear and the existing ids are unchanged.

## 3. BuildItemPool Retro branch

- [ ] 3.1 In `src/rando/rando_placement.c`, add a `BuildItemPool` branch for `world_state == Retro` that:
  - Starts from the Open pool (Retro inherits Open's region graph + item set).
  - Appends shop-purchase locations from `location_registry.yaml` whose `world_state_filter` includes the Retro bit.
  - Junk-pads to match `|locations|`.
- [ ] 3.2 Pin the 4 Retro config flags (`rupeeBow`, `genericKeys`, `takeAnys`, `wildKeys`) in the effective settings struct when `world_state == Retro` is detected at generation start. Decide whether to reject CSV overrides or silently apply Retro defaults — recorded in design.md.
- [ ] 3.3 Verify junk-pad rotation: pool difficulty `hard`/`expert` still adds `Rupoor`; `normal` does not. Same as Phase A.
- [ ] 3.4 Add a unit-style assert (or selftest entry) confirming `|pool| == |locations|` for the Retro branch on at least 3 corpus seeds.

## 4. Settings + CSV

- [ ] 4.1 Update `src/rando/rando_settings.h` to declare the 4 Retro flag bits in the settings struct (if not already present). Decision: are they real bits, or computed at runtime from `world_state`? Recorded in design.md. Default: computed at runtime (no new bytes in serialization).
- [ ] 4.2 CSV parser: accept `world_state=retro` as a valid value (Phase A already does this since the enum was reserved at Phase A; verify).
- [ ] 4.3 If the design.md decision is to allow CSV overrides of Retro flags (e.g., `mode.state=retro,wildKeys=false`), implement the parsing; otherwise, silently ignore or reject.

## 5. Dispatch routing

- [ ] 5.1 Wire `Rando_OnLocationCheck` at `src/sprite_main.c:25308` (`ShopItem_HandleReceipt`). The patch resolves the active shop's slot id to a `LOC_<...>` via a shop-lookup table (similar to `chest_lookup.h` for §6.3 chests).
- [ ] 5.2 Author `src/rando/shop_lookup.h` (generated from `retro_shops.yaml` or inline) mapping `(shop_id, slot_index) → LOC_<...>`. Codegen extends `assets/rando_logic_gen.py`; new generated header registered in build wiring (`Makefile`, `Zelda3.vcxproj`, `src/platform/switch/Makefile`).
- [ ] 5.3 Grep `src/sprite_main.c` for the Take-Any cave sprite handler. Decide whether it goes through the same `ShopItem_HandleReceipt` path or needs its own dispatch site. Wire accordingly.
- [ ] 5.4 Wire capacity-upgrade dispatch at `sprite_main.c:11483-11484` (bomb) and `11520-11521` (arrow). Identity-placed; dispatch is for uniformity.
- [ ] 5.5 Add `// rando-exempt: state-shuffle — rupee cost deduction, not a grant` comments at the `link_rupees_goal -= cost` sites (`sprite_main.c:1067, 1845, 3025, 6197, 6831, 6871, 6912, 9794, 10155, 11425, 19176, 19883, 24682, 25329` per `audit.md` §280). Per Phase A audit, these are already classified state-shuffle; ensure the comments are present.

## 6. Audit-guard sweep

- [ ] 6.1 Run `assets/scripts/check_audit_guard.py` after wiring. Every shop-related `link_item_*` write must either dispatch through `ShopItem_HandleReceipt` (now goes through `Rando_OnLocationCheck`) OR carry an explicit exemption. No new audit-guard failures.
- [ ] 6.2 Verify the `kMemoryLocationToGiveItemTo[]` / `kValueToGiveItemTo[]` indirect writes (per memory `[[audit-guard-indirect-writes]]`) don't introduce a hidden grant site bypass — grep raw addresses too.

## 7. Picker un-gate

- [ ] 7.1 Remove the Phase A re-scope gate in `src/select_file.c:2520-2527` that caps the world-state picker at Standard. Comment update: replace the "ALTTPR's `Region/Inverted/*.php` ~1500 lines" stale citation if still present (per audit issue #5 in chunking notes).
- [ ] 7.2 Add `Retro` to the picker's enum cycle. Verify the picker now cycles `Open → Standard → Retro → Open` (if #4a Inverted has not shipped) OR `Open → Standard → Inverted → Retro → Open` (if it has).

## 8. Spoiler integration

- [ ] 8.1 Add a `Shops` section to the text spoiler grouped output. Each shop name is a region heading; per-shop slots are listed with their placement.
- [ ] 8.2 JSON spoiler: shop locations appear under their region groupings (e.g., `Light_World_Kakariko_Shop_Slot1` under the `Light World Kakariko` region).
- [ ] 8.3 Capacity-upgrade slots in the spoiler are flagged identity-placed (`"identity_placed": true` in the JSON entry) for clarity.

## 9. kGeneratorVersion + corpus

- [ ] 9.1 Bump `kGeneratorVersion` in `src/rando/rando.h`.
- [ ] 9.2 Run `assets/scripts/bump_rando_corpus.py` to regenerate `tests/rando_corpus/manifest.yaml`. The corpus regenerates because the location registry grew.
- [ ] 9.3 Verify Open + Standard + (Inverted if shipped) digests match pre-Retro-change baseline for the existing corpus seeds — Retro additions should not alter non-Retro placements.
- [ ] 9.4 Add at least 3 Retro corpus seeds covering: Fast Ganon / item_pool=normal, Triforce Hunt / item_pool=hard, Completionist / item_pool=expert.

## 10. Testing

- [ ] 10.1 Manual playtest: generate a Retro Fast Ganon seed; play until first shop purchase; confirm placement-table substitute is granted at vanilla rupee cost.
- [ ] 10.2 Manual playtest: enter a Take-Any cave in the same seed; confirm dispatch fires and placement-table item is granted.
- [ ] 10.3 Manual playtest: buy bomb-capacity upgrade; confirm identity-placement works (player still gets the capacity upgrade, dispatch fires but no rando-side effect).
- [ ] 10.4 Vanilla-mode regression: run a non-rando game; visit a shop; confirm `g_ram` after purchase is bit-identical to pre-change baseline (per `randomizer-placement / Single dispatch point per grant site` Scenario "Vanilla path bit-identical when rando inactive").
- [ ] 10.5 Cross-platform digest determinism: regenerate the Retro corpus on Linux + macOS + Windows; all digests must match.

## 11. Documentation

- [ ] 11.1 Update `docs/randomizer.md` settings reference: confirm `world_state=retro` is documented (Phase A reserved it; this change activates it).
- [ ] 11.2 Update `docs/randomizer_phase_b.md` Slice 3 status: mark complete; the change-folder link should already exist per the cross-link pass.
- [ ] 11.3 Add a short "Retro mode" subsection to `docs/randomizer.md` explaining the 4 pinned flags and what they mean for gameplay.

## 12. Archive readiness

- [ ] 12.1 CI green on Linux + macOS + Windows; corpus regen passes; audit-guard + determinism guards green.
- [ ] 12.2 Manual playthrough confirms the dispatch + un-gate work end-to-end.
- [ ] 12.3 `openspec archive add-rando-retro-world-state` runs cleanly; spec deltas merge into `openspec/specs/randomizer-{core,placement,ui}/spec.md`.
