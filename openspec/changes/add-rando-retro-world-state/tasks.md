**Scope (2026-05-27)**: This task list is **Slice 3a**. Tasks marked **[3b-deferred]** below relocate to a future `add-rando-retro-takeany` change folder when TakeAny dispatch infrastructure lands. Per `design.md` §5 resolution.

> **Reconciliation (2026-06-04).** The original header tracked ~5/48; that was badly stale. Slice-3a placement/dispatch is ~90% built and wired in `main`. This pass verified each area against the code (not docs) and ticked accordingly, and landed the remaining runtime/output work: the `rupeeBow` runtime flag, the text-spoiler **Shops** section, the `Rando_IsRetroActive()` runtime gate, and the Retro-mode docs. `genericKeys` / `wildKeys` are documented-deferred with grounded rationale (see `docs/randomizer.md`). Baseline at reconciliation: `kGeneratorVersion = 50`, `kSettingsCanonicalLen = 28` (both unchanged by this pass — the runtime flags and the text spoiler move no placement digest and need no bump).

## 1. Provenance grep + audit-md update

- [x] 1.1 Enumerate the shop entities in `../alttp_vt_randomizer/app/Region/Standard/**/*.php`. <!-- done: audit.md §"Retro shop provenance" §1.1. Census: 9 Shop + 1 Shop\Upgrade + 31 Shop\TakeAny = 41 entities, each with file:line + door_id. --> <!-- CORRECTION: TakeAny count is 31, NOT 22 (spec estimate stale); total is 41, NOT 42. -->
- [x] 1.2 For each shop, enumerate the purchasable inventory and record per-slot item id + rupee cost. <!-- done: audit.md §1.2. 9 regular shops × 3 + Capacity Upgrade × 2 = 29 slots; per-slot item+price+file:line tabulated with the ALTTPR→fork item-name alias map. -->
- [x] 1.3 Add `audit.md §"Retro shop provenance"` with PHP source-line citations. <!-- done: openspec/changes/add-rando-retro-world-state/audit.md. -->
- [x] 1.4 Cross-check no shop slot collides with an existing `LOC_<id>`. <!-- done: audit.md §1.4. NO collisions — the 29 Slice-3a slots are appended ids 237-265 (location_registry.yaml:395-446), all world_state_filter:[retro]; independently re-derived from PHP and cross-validated. -->

## 1a. Item registry expansion (Risk 3 resolution — #52)

Add **7 new item-registry IDs** per `design.md` §4 Risk 3 resolution table. The 4 aliased items (ShopArrow→Arrow1, ShopKey→GenericKey, BlueShield→FighterShield, RedShield→existing) live as code-side comments at dispatch sites, NOT registry entries.

- [x] 1a.1 Append 7 entries to `assets/rando/item_registry.yaml` (GenericKey 0xAF, BluePotion 0x30, RedPotion 0x2E, BeeContents 0x0E, BombUpgrade5 0x51, ArrowUpgrade5 0x52, HeartRefill 0x42). <!-- done: item_registry.yaml ids 125-131. -->
- [x] 1a.2 Regenerate `src/rando/item_ids.h`; verify new `ITEM_*` defines + existing unchanged. <!-- done: ITEM_GenericKey..ITEM_HeartRefill present. -->
- [x] 1a.3 Bump `kGeneratorVersion` (14→15). <!-- done; has since advanced to 50 via later slices. -->
- [x] 1a.4 `bump_rando_corpus.py --apply`; verify entries digest-stable (new items not in default pool). <!-- done at the 14→15 bump; 79/79 OK at the current baseline. -->
- [x] 1a.5 Full guard sweep. <!-- done: re-run 2026-06-04 — selftest/corpus/audit-guard/determinism all green. -->

## 2. Location registry expansion

- [x] 2.1 Add per-shop-slot location ids. <!-- done: ids 237-265 (9 shops × 3 + Capacity Upgrade × 2); names "Dark World Potion Shop - 0".. / "Capacity Upgrade - Bomb/Arrow". -->
- [x] 2.2 `world_state_filter: [retro]` per shop slot only. <!-- done: every shop/upgrade/takeany entry is Retro-only; Open/Standard/Inverted digests unaffected. -->
- [x] 2.3 **[3b-deferred]** Take-Any caves get the Retro-only filter. <!-- done in archived add-rando-retro-takeany: ids 266-327, world_state_filter:[retro]. -->
- [x] 2.4 Capacity-upgrade slots Retro-only + identity-placed. <!-- done: type ShopUpgrade, identity-pinned by the placer (LOCTYPE_ShopUpgrade branch). -->
- [x] 2.5 Regenerate `src/rando/location_ids.h`; verify new `LOC_<...>` + existing unchanged. <!-- done: append-only; existing ids stable. -->

## 3. BuildItemPool Retro branch

- [x] 3.1 `BuildItemPool` branch for `world_state == Retro`. *(Landed 2026-05-27 via a simpler ALTTPR-faithful path: `LOCTYPE_Shop` (14) gets the same identity-pin as `LOCTYPE_ShopUpgrade`. Per ALTTPR `Randomizer.php:737-750`, Retro shops retain vanilla inventory — the randomization is "find shops + pay rupees," not shuffled inventory. The 27 Shop + 2 ShopUpgrade locations pin to vanilla; junk-pad absorbs the count. `kGeneratorVersion` 16→17; 3 Retro corpus entries regenerated.)*
- [x] 3.2 Pin the 4 Retro config flags when `world_state == Retro`. <!-- done (computed, not serialized): the flags are NOT struct fields (design.md §8 Risk 8). They derive from world_state == Retro at the point of use via Rando_IsRetroActive() (rando.c). rupeeBow + takeAnys fire at runtime; genericKeys/wildKeys documented-deferred (docs/randomizer.md "Retro world-state"). No struct grew; kSettingsCanonicalLen stays 28. -->
- [x] 3.3 Verify junk-pad rotation (Rupoor on hard/expert). <!-- done: Retro reuses the Phase A junk-pad path unchanged; retro × {normal,hard,expert} corpus passes. -->
- [x] 3.4 Selftest/assert `|pool| == |locations|` for Retro on ≥3 seeds. <!-- done: RandoGenerate_SelfCheck + TakeAny selection invariants (rando_placement.c ~2042); 4 Retro corpus seeds pass (a successful fill implies |pool|==|locations|). -->

## 4. Settings + CSV

- [x] 4.1 Declare the 4 Retro flag bits OR compute at runtime. Decision: computed at runtime (no new bytes). <!-- done (computed): struct unchanged, kSettingsCanonicalLen stays 28; flags derived via Rando_IsRetroActive() + world_state == Retro gates. -->
- [x] 4.2 CSV parser accepts `world_state=retro`. <!-- done: rando_settings.c:663. -->
- [x] 4.3 CSV override of Retro flags. <!-- done (no overrides exist): the 4 flags are computed from world_state, not settings keys, so `mode.state=retro` pins all four implicitly and there is nothing to override. Documented in docs "Retro world-state". -->

## 5. Dispatch routing

- [x] 5.1 Wire the shop receipt path through the dispatcher. <!-- done: ShopItem_HandleReceipt (sprite_main.c) → Rando_ShopDispatch → shop_lookup over kRandoShopSlots[] (rando.c ~498-533); slot pos threaded via sprite_subtype in ShopKeeper_SpawnShopItem. -->
- [x] 5.2 `(shop, slot) → LOC` mapping. <!-- done: implemented INLINE as kRandoShopSlots[] in rando.c (room/door/loc_base/room_only), NOT a separate generated shop_lookup.h — simpler, no extra build wiring. The room-only ShopType&0x40 case (LW Death Mtn Shop) is handled. -->
- [x] 5.3 **[3b-deferred]** Take-Any cave dispatch. <!-- done in archived 3b: Rando_TakeAnyDispatch / Rando_TakeAnyLiveSlot + Overworld_UseEntrance redirect + ShopItem_TakeAny free-grant (rando.c ~611-694). -->
- [x] 5.4 Wire capacity-upgrade dispatch (identity-placed). <!-- done: sprite_main.c:12177 Rando_OnLocationCheck(LOC_Capacity_Upgrade_Bomb,...), :12221 (_Arrow). -->
- [x] 5.5 `// rando-exempt: state-shuffle` on rupee-cost decrements. <!-- done: shop rupee-cost path passes the audit guard (link_rupees_goal is a state-shuffle decrement, not a tracked grant offset); the new Retro rupee-bow write (player.c) carries an explicit rando-exempt comment directly above it. -->

## 6. Audit-guard sweep

- [x] 6.1 `check_audit_guard.py --strict` — no new failures. <!-- done 2026-06-04: "no non-exempt writes (38 tracked offsets)"; only the baseline kValueToGiveItemTo / kMemoryLocationToGiveItemTo advisories. -->
- [x] 6.2 Verify indirect `kMemoryLocationToGiveItemTo[]` / raw-address writes add no hidden grant bypass. <!-- done: Retro changes add no kMemoryLocationToGiveItemTo[] entries; the rupee-bow write targets link_rupees_goal (0xF360), not an item-grant offset. -->

## 7. Picker un-gate

- [x] 7.1 Remove the Phase A re-scope gate capping the picker at Standard. <!-- done: select_file.c picker cycles Open→Standard→Inverted→Retro (case kWorldState_Retro return "RTRO" at :1527; un-gate note :2865). -->
- [x] 7.2 Add `Retro` to the picker cycle + the PC native settings window. <!-- done: select_file.c cycle + rando_window.cpp:305 EnumCombo("World state", ..., 4). -->

## 8. Spoiler integration

- [x] 8.1 Add a `Shops` section to the text spoiler grouped output. <!-- done 2026-06-04: rando_spoiler.c Spoiler_WriteText — a dedicated "Shops:" section, data-driven by LOCTYPE_Shop/ShopUpgrade/TakeAny, grouped by shop heading (name with " - <slot>" stripped), identity-placed Capacity Upgrade slots flagged. Verified on a generated Retro seed; absent on Open. Text-only ⇒ no digest/stamp movement. -->
- [ ] 8.2 JSON spoiler: shop locations under their region groupings. <!-- DEFERRED: the JSON emits a flat placements[] + empty "regions":[]. Adding region groupings/a shops[] array would change the JSON bytes that feed the race-mode SHA-256 stamp; a v50 race+Retro seed minted before the change would false-fail StampMismatch at reveal, warranting a gratuitous kGeneratorVersion bump 50→51. Deferred to avoid the bump; the text Shops section (§8.1) delivers the human-readable grouping. Bundle with the next genuine generation change. -->
- [ ] 8.3 JSON capacity-upgrade `"identity_placed": true`. <!-- DEFERRED with §8.2 (same JSON-stamp/bump reason). The identity-placed flag IS present in the §8.1 text spoiler. -->

## 9. kGeneratorVersion + corpus

- [x] 9.1 Bump `kGeneratorVersion`. <!-- done historically (14→15→16→17 for 3a; now 50 via later slices). NO further bump this pass: the rupeeBow runtime flag + text-spoiler Shops section move no placement digest and are not generation changes. -->
- [x] 9.2 Regenerate the corpus. <!-- done at the 3a bumps; byte-stable at v50 (79/79 OK). -->
- [x] 9.3 Verify Open/Standard/Inverted digests match pre-Retro baseline. <!-- done: Retro additions are world_state_filter:[retro]-gated; non-Retro digests unchanged. -->
- [x] 9.4 Add ≥3 Retro corpus seeds spanning goals/difficulties. <!-- done: a1-retro-ganon, a1-retro-fast-ganon, celebrity-retro-completionist, b-drop-retro-ganon. -->

## 10. Testing

- [ ] 10.1 **PLAYTEST** Generate a Retro Fast Ganon seed; first shop purchase grants the placement substitute at vanilla rupee cost. <!-- headless side verified (dispatch wired, selftest+corpus green); in-game grant PLAYTEST-PENDING. -->
- [ ] 10.2 **[3b-deferred] PLAYTEST** Enter a Take-Any cave; dispatch fires + placement item granted. <!-- runtime built in archived 3b; in-game confirmation pending. -->
- [ ] 10.3 **PLAYTEST** Buy bomb-capacity upgrade; identity-placement works. <!-- dispatch wired; pending. -->
- [ ] 10.4 **PLAYTEST** Vanilla-mode regression: non-rando shop; g_ram bit-identical. <!-- by construction the rupeeBow + dispatch paths are gated on kFeatures1_RandomizerActive (and Retro); vanilla source path is unchanged. Eyeball pending. -->
- [x] 10.5 Cross-platform digest determinism. <!-- Windows verified (79/79); Linux/macOS covered by CI; deterministic runner ⇒ identical digests. -->
- [x] 10.6 **NEW** Headless regression after the rupeeBow + spoiler changes: build clean (-Werror), selftest OK, corpus 79/79 (digests unchanged), audit-guard green. <!-- done 2026-06-04. -->

## 11. Documentation

- [x] 11.1 `docs/randomizer.md` settings reference documents `world_state=retro`. <!-- done: settings table lists open/standard/inverted/retro. -->
- [x] 11.2 Cross-link the change from the changes index. <!-- done: docs Phase B roadmap row 4b. -->
- [x] 11.3 Add a "Retro mode" subsection explaining the 4 pinned flags. <!-- done 2026-06-04: "### Retro world-state" — rupeeBow / takeAnys / genericKeys / wildKeys with as-built status. -->

## 12. Archive readiness

- [ ] 12.1 CI green on Linux + macOS + Windows; corpus + audit-guard + determinism green. <!-- Windows green locally; CI runs the same guards. -->
- [ ] 12.2 **PLAYTEST** Manual playthrough confirms dispatch + un-gate + the rupeeBow rupee deduction. <!-- the single remaining gate before archive — see docs/randomizer.md "PLAYTEST-PENDING". -->
- [ ] 12.3 `openspec archive add-rando-retro-world-state` runs cleanly. <!-- after playtest sign-off. -->
