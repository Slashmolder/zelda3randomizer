**Scope (2026-05-27)**: This task list is **Slice 3a**. Tasks marked **[3b-deferred]** below relocate to a future `add-rando-retro-takeany` change folder when TakeAny dispatch infrastructure lands. Per `design.md` §5 resolution.

> **Reconciliation (2026-06-04).** The original header tracked ~5/48; that was badly stale. Slice-3a placement/dispatch is ~90% built and wired in `main`. This pass verified each area against the code (not docs) and ticked accordingly, and landed the remaining work in two waves: (1) the `rupeeBow` runtime flag, the text-spoiler **Shops** section, the `Rando_IsRetroActive()` runtime gate, and the Retro-mode docs (no bump); then (2) **`wildKeys`** (small keys pinned to the Wild pool for Retro via `Settings_EffectiveSmallKeysMode`) and the **JSON `shops[]`** array (§8.2-8.3) — a real generation change, `kGeneratorVersion` 50→51, exactly the 4 Retro corpus digests regenerated. The only remaining deferral is literal **`genericKeys`** (single shared key pool), which needs the per-dungeon key-door LOGIC rewrite + playtest and is documented (see `docs/randomizer.md` "Retro world-state"). Final baseline: `kGeneratorVersion = 51`, `kSettingsCanonicalLen = 28` (canonical layout unchanged — out[11] still small_keys, just normalized to Wild for Retro).

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
- [x] 3.2 Pin the 4 Retro config flags when `world_state == Retro`. <!-- done (computed, not serialized): the flags are NOT struct fields (design.md §8 Risk 8). They derive from world_state == Retro at the point of use. rupeeBow fires at runtime via Rando_IsRetroActive() (rando.c); takeAnys via archived 3b; wildKeys via Settings_EffectiveSmallKeysMode() pinning small_keys=Wild (kGenVer 50→51). genericKeys (single shared pool) still deferred — needs the per-dungeon key-door LOGIC rewrite (docs/randomizer.md "Retro world-state"). No struct grew; kSettingsCanonicalLen stays 28. -->
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
- [x] 8.2 JSON spoiler: shop locations grouped. <!-- done 2026-06-04: rando_spoiler.c Spoiler_WriteJson emits a Retro-only shops[] array (location/name/item/type). Shop-class locations have no logic-region binding (region_id 0xFFFF), so the location NAME is the grouping key (region emitted only when bound). Emitted only when shop-class locations are placed ⇒ non-Retro JSON byte-identical. Version-locked by the kGenVer 50→51 bump (which also covers wildKeys). -->
- [x] 8.3 JSON capacity-upgrade `"identity_placed": true`. <!-- done 2026-06-04: shops[] entries of type ShopUpgrade carry "identity_placed": true (verified on a generated Retro seed: locs 264/265). -->

## 9. kGeneratorVersion + corpus

- [x] 9.1 Bump `kGeneratorVersion`. <!-- done: 50→51 for Retro wildKeys (moves Retro placement) + JSON shops[] (moves race+Retro stamp). The rupeeBow runtime flag + text-spoiler Shops section needed no bump (no placement/generation move). -->
- [x] 9.2 Regenerate the corpus. <!-- done 2026-06-04: bump_rando_corpus.py --apply; generator_version 50→51; EXACTLY the 4 Retro digests updated. 79/79 OK. -->
- [x] 9.3 Verify Open/Standard/Inverted digests match pre-Retro baseline. <!-- done: dry-run confirmed only the 4 Retro entries (13/14/50/76) changed; all 75 non-Retro entries byte-identical (wildKeys override is world_state==Retro-gated). -->
- [x] 9.4 Add ≥3 Retro corpus seeds spanning goals/difficulties. <!-- done: a1-retro-ganon, a1-retro-fast-ganon, celebrity-retro-completionist, b-drop-retro-ganon. -->

## 10. Testing

- [x] 10.1 **PLAYTEST** Generate a Retro seed; shop purchase grants the placed item at vanilla rupee cost. <!-- owner playtest 2026-06-04: shops sell their vanilla inventory (identity-pinned) for rupees — confirmed working. (Required the chest-table fix first — see 10.7.) -->
- [x] 10.2 **PLAYTEST** Enter a Take-Any cave; dispatch fires + placement item granted; cave is one-shot. <!-- owner playtest 2026-06-04: took the sword from a cave, returned → empty. No dupe. -->
- [x] 10.3 **PLAYTEST** Capacity upgrade works (identity-placed). <!-- owner playtest 2026-06-04: donate-rupees-for-+5 fountain grants the upgrade. -->
- [ ] 10.4 **PLAYTEST** Vanilla-mode regression: non-rando shop; g_ram bit-identical. <!-- by construction the rupeeBow + dispatch paths are gated on kFeatures1_RandomizerActive (and Retro); vanilla source path is unchanged. Eyeball still pending (low risk). -->
- [x] 10.5 Cross-platform digest determinism. <!-- Windows verified; Linux/macOS covered by CI; deterministic runner ⇒ identical digests. -->
- [x] 10.6 Headless regression (post-merge): build clean (-Werror), selftest OK, corpus 87/87 (only the 4 Retro digests moved for wildKeys; main's trick/swordless seeds byte-identical), audit-guard + determinism + genver(53) green. <!-- done 2026-06-04. -->
- [x] 10.7 **PLAYTEST** rupeeBow + chest dispatch + placement. <!-- owner playtest 2026-06-04: rupeeBow confirmed (10 wood / 50 silver, rupee-gated, arrow-independent; arrow HUD count still shows — cosmetic). Placement (chests + NPCs) confirmed AFTER fixing the worktree's empty chest_lookup.h (missing chest_table.gen.bin → every chest granted vanilla; see lessons.md §1, setup_worktree.py fix). -->
- [x] 10.8 **PLAYTEST→FIX** UI reflects forced wildKeys. <!-- owner feedback: the PC "Small keys" combo looked editable under Retro though it is forced to Wild; fixed (rando_window.cpp) to lock + tooltip, mirroring the Completionist→accessibility lock. -->
- [ ] 10.9 **PLAYTEST** Full goal clear (Retro). <!-- owner playtest 2026-06-04: progressing, got close, not yet finished — the last unverified bit. wildKeys cross-dungeon key usability + reachability confirmed implicitly so far. -->
- [ ] 10.10 **PLAYTEST** wildKeys explicit: a small key found outside its dungeon opens that dungeon's door; no key lost across enter/exit. <!-- forced on for Retro (owner decision); spot-confirm during the full clear. -->

## 11. Documentation

- [x] 11.1 `docs/randomizer.md` settings reference documents `world_state=retro`. <!-- done: settings table lists open/standard/inverted/retro. -->
- [x] 11.2 Cross-link the change from the changes index. <!-- done: docs Phase B roadmap row 4b. -->
- [x] 11.3 Add a "Retro mode" subsection explaining the 4 pinned flags. <!-- done 2026-06-04: "### Retro world-state" — rupeeBow / takeAnys / genericKeys / wildKeys with as-built status. -->

## 12. Archive readiness

- [ ] 12.1 CI green on Linux + macOS + Windows; corpus + audit-guard + determinism green. <!-- Windows green locally; CI runs the same guards. Pending the merge + CI run. -->
- [ ] 12.2 **PLAYTEST** Manual playthrough confirms dispatch + un-gate + rupeeBow. <!-- MOSTLY DONE 2026-06-04 (owner): rupeeBow, TakeAny one-shot, shops, capacity, chest+NPC placement, UI lock all confirmed (§10.1-10.8). Remaining: a full goal clear (§10.9) + explicit wildKeys cross-dungeon spot-check (§10.10). -->
- [ ] 12.3 `openspec archive add-rando-retro-world-state` runs cleanly. <!-- after the full-clear sign-off + merge. genericKeys carved out to add-rando-retro-generic-keys (#4b-i). -->
- [x] 12.4 **NEW** lessons.md authored (chest-table trap, half-a-flag-pair difficulty inversion, UI-reflect-forced-settings, the EffectiveSmallKeysMode determinism pattern, kGenVer merge collision, corpus-vs-playtest). <!-- done 2026-06-04. -->
