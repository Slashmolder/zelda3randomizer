# Phase A1 Implementation Audit

Audit of `src/rando/*`, `assets/rando/*.yaml`, and `assets/rando_logic_gen.py`
against the OpenSpec scenarios in `openspec/changes/add-randomizer-support/specs/`.

Every claim cites a file:line. Findings sorted by severity, not by spec order.

## Fix status (since audit landed)

Subsequent commits have addressed the following:

| Bug | Status | Resolved by |
|---|---|---|
| #1 prize-slot pinning | ✅ closed | c70f38e Placer: prize-shuffle pinning |
| #2 region tags on dungeon/overworld | ✅ closed | 89f5405 Logic: add region tags to 190 logic_parts locations |
| #3 settings struct missing 4 fields | ✅ closed | 4757e3d Settings: align canonical layout to spec |
| #4 FastGanon ignores crystal counts | ✅ closed | 72cc7db Goal: crystals.ganon check + strict refusal |
| #5 SetDefaults completionist→accessibility | ✅ closed | 599f8a1 Settings: completionist auto-set accessibility |
| #6 placement_table_size byte semantics | ✅ closed | 5913318 rando_save: align on-disk format to spec |
| #7 per-item bounded rewind | ⚠ partial | 2bf28b0 (--budget-seconds wired; per-attempt retry remains; intra-attempt rewind deferred to A2) |
| #8 fallback_warnings hardcoded empty | ✅ closed | 599f8a1 Spoiler: surface placer fallbacks |
| #9 text spoiler not grouped by region | ✅ closed | 599f8a1 Spoiler: group text by region |
| #10 un-completable seed exits 0 | ✅ closed | 72cc7db Strict refusal of un-completable seeds |
| #11 dead is_progression_item code | ✅ closed (now used; Prize_* ids pin correctly per #1) |
| #12 Rando_TryGrantStartingInventory uncalled | ⚠ deferred (wires when §6 grant-site dispatch lands) |
| #13 pieces_required > pieces_placed silent | ✅ closed | 599f8a1 BuildItemPool: validate Triforce-Hunt |
| #14 HAS_ANY_COUNT vacuous selftest | ✅ closed | 2bf28b0 Logic_SelfCheck: vacuous HAS_ANY_COUNT cases |
| #15 eval_glitch comparison | ✅ closed | 599f8a1 eval_glitch: use ctx->settings->logic |
| #16 world_state_filter all 0 | ⚠ documented (no scenario violated; filter is opt-in) |

Plus issues discovered during audit-fix work:

| Issue | Status | Resolved by |
|---|---|---|
| Vanilla-mode dungeon items not in reachability inventory | ✅ closed | ec3c723 placer: vanilla-mode dungeon items pre-granted |
| Second-audit N1: slot_kind enum disagrees with spec (Empty=0 missing) | ✅ closed | efbce92 slot_kind enum: Empty=0, Vanilla=1, Randomizer=2 |
| Second-audit N2: Pedestal goal missing pendant-reachability check | ✅ closed | 1f68e51 goal: pendant-holding dungeons reachable |
| Second-audit N4: sphere cap silent fail | ✅ closed | 0a18dd0 spheres: surface kSphereMaxCount cap warning |
| Second-audit N5: BuildItemPool padded unconditionally to 237 | ✅ closed | 0a18dd0 pool: world-state-filtered location count |
| Second-audit N7: 5 settings fields missing CSV parser | ✅ closed | efbce92 CSV: tricks/logic/race_mode/pyramid_bow_upgrade/region_boss_hearts_in_pool |
| Third-audit NEW-1: dungeon_id_for_item off-by-one for BigKey/Map/Compass | ✅ closed | 0917299 lookup table replaces arithmetic, + 4908b68 regression selftest |
| Third-audit NEW-3: Magic Bat vanilla_item should be HalfMagic | ✅ closed | a57cd95 registry: Magic Bat HalfMagic |

Spec scenarios also closed beyond the original audit:
- Dungeon-mode key/big/map/compass containment (3195b85)
- Boss-heart slots identity-placed (0917299)
- Item-pool difficulty downgrade (cd2dace)
- Triforce-Hunt junk-padding rotation (d767563)
- sphere_digest in spoiler meta (d0bd1b5)
- seed_u64 in spoiler meta (000566c)
- generation_wall_clock_ms wired (f8c6d88)

Still open (deferred, substantial work):
- NEW-2: §6.2 receive helpers for 0xFF-dispatch items (TriforcePiece,
  small/big keys, multi-tier rupees, HalfMagic/QuarterMagic, etc.)
- NEW-4: StateRecorder snapshot doesn't preserve rando placement
- Inverted/Retro world-state pool augmentation + start region declaration
- Per-item bounded rewind (vs current whole-attempt retry)
- §6.4-§6.8 NPC/static/minigame dispatch for the long tail (~80 sites)
- §8 sidecar save load/write integration
- §9 UI work (file-select / settings screen / tracker)

## 1. Scenario-by-scenario gap report

Legend: `✓` implemented and matches | `⚠` partial (notes) | `✗` not implemented | `❓` unclear.

### randomizer-core (`specs/randomizer-core/spec.md`)

| Scenario | Status | Evidence |
|---|---|---|
| Same inputs yield identical placements across platforms | ❓ | Determinism guards in place (`assets/scripts/check_determinism.py:1`, `assets/scripts/check_link_symbols.py:1`); per `tasks.md:122`, Windows-local run produced identical digests but Linux/macOS CI corpus diff still open (`tasks.md §4.5`). |
| Generator-version change invalidates determinism | ✓ | `kGeneratorVersion=1` (`src/rando/rando.h:22`); bump-corpus tool exists (`assets/scripts/bump_rando_corpus.py:1`). |
| Big-endian conversion macros are rejected | ✓ | `assets/scripts/check_byte_order.py:1`; wired in `.github/workflows/rando_ci.yaml`. |
| Cross-platform byte sequence stability | ❓ | All LE serialization helpers explicit in `rando_save.c:44-62` and `rando_settings.c:53-76`; not yet exercised on BE platform. |
| Reordering fields breaks settings_hash | ⚠ | Hash recomputes correctly, but the CANONICAL ORDER ITSELF VIOLATES THE SPEC — see Bug #5 below. |
| Phase A defaults | ⚠ | `Settings_SetDefaults` at `rando_settings.c:33-51` sets `world_state=Open`, `goal=FastGanon`, `crystals_ganon=crystals_tower=7`, `item_pool_difficulty=Normal`, `dungeon_items_*=Vanilla`, `prize_shuffle=1`, `medallion_shuffle=1`, `mode_weapons=Randomized`, `accessibility=Items`, `pyramid_bow_upgrade=Silvers`, `pieces_required=20`, `pieces_placed=30`. **Missing from struct: `tricks`, `logic`, `region_boss_hearts_in_pool`, `race_mode`.** Default settings happen to be representable, but the spec fields don't exist in the struct (`rando_settings.h:80-98`). |
| Unwinnable Triforce-Hunt input is refused | ✗ | No check anywhere; `pieces_required > pieces_placed` proceeds silently through `BuildItemPool` (`rando_placement.c:305-307`). |
| Static check fails on forbidden symbols | ✓ | `assets/scripts/check_determinism.py:1`. |
| CI corpus cross-platform diff | ⚠ | Single-platform corpus entry only; cross-platform diff not yet wired (`tasks.md §12.3` open). |
| Round-trip encoding (share string) | ✓ | `Share_SelfCheck` in `rando_share.c` exercises encode/decode round-trip; runs in `--rando-selftest`. |
| External ALTTPR hash is rejected | ✓ | `kShareDecodeAlttprFormat` status in `rando_share.h:24`; selftest covers it. |
| Corrupted share string | ✓ | `kShareDecodeBadChecksum` per `rando_share.h:23`. |
| TriforcePiece is in the pool for Triforce Hunt and Ganon Hunt | ⚠ | `rando_placement.c:305-307` adds `pieces_placed` copies of `ID_TriforcePiece`. But goal completability only verifies them count-wise (`rando_placement.c:854-865`); the placer doesn't enforce reachability per piece. |
| SilverArrowUpgrade only when absolute bow | ⚠ | In progressive mode, `ID_SilverArrowUpgrade` is NOT added (`rando_placement.c:185-189`). In absolute mode it IS added (line 205). Matches spec direction. |
| Rupoor gated by item_pool_difficulty | ✓ | `rando_placement.c:298-302`. |
| After padding, pool cardinality matches location count | ⚠ | Padded with Rupee20 to `kRandoLocationsCount` (`rando_placement.c:312-315`), but no per-world-state pool adjustment; Inverted has the same count as Open. |
| Triforce Hunt junk-padding | ⚠ | Same Rupee20 padding; doesn't differentiate `pieces_required` vs `pieces_placed` mismatch cases. |
| Item-pool difficulty downgrade | ✗ | Hard/Expert add Rupoor but no downgrade table (silvers→wooden, mirror→red, bottles→hearts) is applied. The spec's "downgrade in the constructed pool" is not implemented. |
| Ganon Hunt is distinct from Triforce Hunt | ⚠ | `Goal_IsCompletable` differentiates them: Triforce Hunt needs Pedestal reachable (`rando_placement.c:861-864`), Ganon Hunt needs Ganon reachable (`rando_placement.c:874-877`). Predicate-VM goal-EQ ops distinguish them. ✓ semantically. |
| Completionist requires every location reachable | ⚠ | `Goal_IsCompletable(Completionist)` iterates every placement (`rando_placement.c:880-889`). But Settings_SetDefaults doesn't auto-force `accessibility=locations` (only `Settings_ParseCsv` does at `rando_settings.c:328-329`). Direct API users get the wrong default. |
| Independent crystals.ganon and crystals.tower | ⚠ | Struct holds both (`rando_settings.h:84-85`) and they serialize independently. **BUT** `Goal_IsCompletable(FastGanon)` does not consult `crystals.ganon` — it merely checks Ganon-reachable (`rando_placement.c:819-826`); the predicate VM also has no path that consumes these counts. |
| Settings serialization order is stable | ⚠ | Stable across runs, but the order does not match the spec (`randomizer-core` lists 20-item order starting with mode_state; impl starts with settings_version and is missing `tricks`/`logic`/`region_boss_hearts_in_pool`/`race_mode`). |
| Completionist forces accessibility = locations | ⚠ | Only honored in `Settings_ParseCsv` (`rando_settings.c:328-329`); `Settings_SetDefaults` (used by CLI defaults + UI presets) doesn't apply this. |
| Race-mode toggle changes settings hash | ✗ | `race_mode` field absent from `RandoSettings` struct (`rando_settings.h:80`); toggling it can't change anything because it doesn't exist. |
| meta block contains all documented fields | ⚠ | `Spoiler_WriteJson` emits `spoiler_format_version`, `generator_version`, `settings_hash_hex`, `settings_hash_full_hex`, `placement_digest_hex`, `share_string`, `world_state`, `goal`, `generation_wall_clock_ms`, `goal_completable`, `fallback_warnings` (`rando_spoiler.c:64-86`). **Missing: `seed_u64` field in meta**. Spec requires it explicitly. |
| Spoiler JSON parses with ALTTPR-shaped tooling | ⚠ | Field names mostly match: `placements`, `regions`, `meta`, `playthrough` are present; `regions: []` and `playthrough: []` are always empty. |
| CLI mode does not open a window | ✓ | `MaybeRunGenerateSeedAndExit` runs before any SDL_Init (`main.c:530-548`). |
| Budget override extends the generation budget | ⚠ | `--budget-seconds` parsed (`main.c:318`) but `Place_AssumedFill` ignores it (`rando_placement.c:425`). No wall-clock measurement. |
| --assets-must-be-vanilla refuses non-vanilla blobs | ✗ | Flag is parsed (`main.c:319`) but explicitly ignored via `(void)assets_must_be_vanilla;` (`main.c:343, 485`). |
| Unknown CLI setting key rejected | ✓ | `Settings_ParseCsv` returns non-zero on unknown key (per `rando_settings.h:135` doc). |
| Duplicate settings key in --settings rejected | ❓ | Not visibly enforced in `Settings_ParseCsv`; needs verification. |
| Duplicate (settings, seed) in manifest rejected | ✗ | Batch/manifest form is still a stub (`main.c:347-355`). |
| --out-share-string writes share string only | ⚠ | Implemented (`main.c:472-479`) but writes with trailing newline-less `fputs` — spec compliant. |
| Forward-fill fallback is surfaced prominently | ✗ | `fallback_warnings: []` is hardcoded empty in `rando_spoiler.c:85`. Bit 0 of slot header `flags` (per spec §randomizer-save / Slot header) for forward-fill never set. No `!` marker on banner. |
| Un-completable seed rejected | ✗ | Generator does NOT abort. `Goal_IsCompletable` is computed and stored in spoiler (`main.c:448, rando_spoiler.c:84`), but the binary still writes the spoiler and exits 0 even when `goal_completable=false`. |
| Spoiler header records completability | ✓ | `goal_completable` written to meta block (`rando_spoiler.c:83-84`). |
| Progressive sword counter increments | ✓ | Counts model in `RandoCounts.by_item_id[]` accumulates correctly per `eval_has_amount` (`rando_logic.c:66-71`). Placer doesn't enforce max-cap (Bug #11). |
| Bottle cap holds at 4 | ✗ | `BuildItemPool` adds exactly 4 `BottleEmpty` (`rando_placement.c:246`), but no 5th-bottle refusal anywhere in placement. Spec mandates a check during fill. |
| Starting hearts visible to logic | ✓ | `counts.by_item_id[121] = 3` set in two places (`rando_placement.c:597, 920`). |
| Progression items placed only in reachable locations | ⚠ | `place_assumed_fill_attempt` filters candidates by `Reachability_HasLocation(r, loc->id)` (`rando_placement.c:634-636`). Falls back to forward-fill on dead-end (line 643+) but does NOT do bounded-retry rewind — instead retries entire attempts with different perturbed seeds (`rando_placement.c:434-462`). Spec says "rewinds the last N placements and retries". |
| Bounded retry on dead-end | ⚠ | Outer loop retries the WHOLE seed up to `kAssumedFillMaxAttempts=8` times (`rando_placement.c:419, 434`); spec wants finer-grained rewind. |
| Forward-fill fallback after timeout | ⚠ | Forward-fill triggers on `cand_n == 0` (dead-end) not on a 5-second wall-clock timer (`rando_placement.c:643-668`). The budget parameter is unused. |
| Both JSON and text spoilers are emitted | ✓ | `main.c:450-470`. |
| fallback_warnings records forward-fill fallback | ✗ | Hardcoded empty (`rando_spoiler.c:85`). |
| Text spoiler is grouped by region | ✗ | `Spoiler_WriteText` writes flat list of `LOC %3u -> ITEM %3u` (`rando_spoiler.c:240-243`). No region grouping. |
| Sphere 0 is starting-inventory reachable | ✓ | `Logic_ComputeSpheres` starts with empty counts + StartingHeart + RescuedZelda (`rando_placement.c:915-923`). |
| Spheres are strictly monotonic | ⚠ | Walker accumulates items in sphere N before computing sphere N+1 (`rando_placement.c:943-953`). But the "newly reachable" property is not actively enforced via re-checking; a location simply gets the first sphere number where it becomes reachable. Correct in effect, though. |
| Sphere data partitions the placement table | ⚠ | Locations get assigned at most one sphere (`rando_placement.c:931, 934-935`), but **unreachable placements** (the majority of dungeon locations per the known issue) get `kSphereIndexUnreachable=0xFF` and are excluded — so the partition is incomplete. |
| JSON spoiler matches placement table | ✓ | All placements emitted sorted by location_id (`rando_spoiler.c:117-130`). |
| Spoiler is written at slot creation | ❓ | CLI writes spoiler eagerly; UI flow not yet wired (`tasks.md §9.8` open). |
| Default-settings benchmark < 2s | ❓ | No benchmark in CI (`tasks.md §3.11` open). |
| Switch budget < 5s | ❓ | Same. |

### randomizer-logic (`specs/randomizer-logic/spec.md`)

| Scenario | Status | Evidence |
|---|---|---|
| Rename does not change ID | ✓ | Registry ID assignments append-only per `location_registry.yaml:7-12` discipline note. |
| ID removal requires explicit deprecation | ❓ | No removed IDs to test against; mechanism documented but unused. |
| Composition of presence and amount | ✓ | `eval_and`/`eval_or`/`eval_not` correct (`rando_logic.c:189-216`). `Logic_SelfCheck` verifies. |
| Region-reachability sub-predicate | ⚠ | `OP_REGION_REACHABLE` consults the RegionRemap overlay then queries `g_reachability.region_bitset` (`rando_logic.c:139-154`). Memoization is implicit (one shared bitset per call to `Logic_ComputeReachability`) but **NOT explicitly per fixed-point iteration as the spec demands** — the spec wants cache invalidation on each fixed-point boundary; the impl uses one bitset that grows monotonically inside one call. In effect correct for `can_reach` (monotone) but doesn't model the spec's invalidation contract. |
| OP_HAS_PRIZE resolves against per-seed prize assignment | ⚠ | `eval_has_prize` (`rando_logic.c:156-167`) reads `dungeon_prize_assignment` and `cleared_dungeons_bitmask`. The bitmask IS now updated by Logic_ComputeReachability's outer loop (`rando_logic.c:453-487`). Works when boss location is reachable. **BUT see Bug #1: most boss locations are unreachable due to the unbound-region issue, so the bitmask stays mostly 0.** |
| OP_MEDALLION_OPENS resolves against per-seed medallion assignment | ⚠ | `eval_medallion_opens` (`rando_logic.c:169-177`) — reads inventory directly. Functionally correct when assignments are installed. |
| OP_HAS_ANY_COUNT sums distinct IDs | ✓ | `eval_has_any_count` (`rando_logic.c:84-96`). |
| Phase A identity overlay | ✓ | `RegionRemap_Lookup` returns identity when `g_region_remap_table == NULL` (`rando_logic.c:317-321`). |
| Phase C non-identity overlay | ✓ | `Rando_SetRegionRemap` installs the table (`rando_logic.c:323-326`). |
| Append-only op-registry | ✓ | `op_registry.yaml` IDs 0-14 (Phase A) and 15-17 (Phase B placeholders); enum in `rando_logic.h:40-62` matches. |
| Standard mode gates progression on uncle pickup | ⚠ | Standard's `start_region` is `LinksHouse` (`logic_data.c:709` = 0x0012 for all four world states including Standard). The `LinksHouse -> LightWorld_NorthEast` edge predicate is `OR(WORLDSTATE_EQ(open), WORLDSTATE_EQ(retro), HAS_ITEM(RescuedZelda))`. Standard correctly requires RescuedZelda. **However**, `place_assumed_fill_attempt` and `Logic_ComputeSpheres` pre-grant RescuedZelda in non-Standard worlds only (`rando_placement.c:603-605, 921-923`) — Standard correctly omits it. ✓ |
| Inverted mode reverses light/dark default and entrance pair | ✗ | `kRandoStartRegionByWorldState[2] = 0xFFFF` (`logic_data.c:709`) — **Inverted has NO start region**. The graph is treated as empty, so no Inverted location is reachable. |
| Retro mode adds shop-purchased items to pool | ✗ | No Retro-specific pool augmentation in `BuildItemPool`. |
| Macros keep predicates readable | ✓ | `assets/rando/macros.yaml` defines named macros; codegen expands them. |
| Inline complexity check fails the build | ✓ | Codegen's well-formedness pass (`rando_logic_gen.py:1210+`) per task 3.4a description. |
| Monotonic in inventory | ✓ | `Logic_ComputeReachability` only sets bits, never clears (`rando_logic.c:374-490`). |
| Budget benchmark | ❓ | Not implemented. |
| Triforce Hunt completion | ⚠ | `Goal_IsCompletable(TriforceHunt)` counts reachable placements containing `TriforcePiece` (`rando_placement.c:854-865`). But the inventory accumulation in `build_final_inventory` (`rando_placement.c:747-757`) adds every placed item, not just reachable ones — the predicate "inventory contains ≥ N triforce pieces" is conflated with "≥ N reachable triforce-piece placements". |
| Fast Ganon gates on crystals and tower access | ✗ | `Goal_IsCompletable(FastGanon)` only checks Ganon-reachable (`rando_placement.c:819-826`). Does NOT verify `crystals.ganon` or `crystals.tower` thresholds. |
| All Dungeons requires every required dungeon cleared | ⚠ | `Goal_IsCompletable(Dungeons)` checks every boss location reachable (`rando_placement.c:828-840`). Spec says "every dungeon for which `OP_GOAL_REQUIRES_DUNGEON` evaluates true". `eval_goal_requires_dungeon` returns true universally for Dungeons goal (`rando_logic.c:118-129`), so practically equivalent. |
| Schema published before A1 | ✓ | `assets/rando/logic.schema.yaml` exists. |
| Standard-mode uncle restriction | ⚠ | `Link's Uncle` location has `can_place: NotADungeonItem()` macro (`logic.yaml:138-141`). Macro is enumerated in `logic.yaml:265-316`. ✓ for dungeon-item ban, ✗ for the canonical `NOT OP_ITEM_IS(SilverArrowUpgrade) AND NOT OP_ITEM_IS(MirrorShield)` style uncle restriction the spec calls out. |
| Dungeon-mode small key stays in its dungeon | ✗ | Only HCE / EP have `NotADungeonItem()` can_place on a handful of slots (`logic.yaml:128`). No dungeon-specific can_place for "key stays in this dungeon" on ANY of the other dungeons. The dungeon-item-mode logic at `rando_placement.c:577-590` pins keys to vanilla locations in Vanilla mode but doesn't restrict them in Dungeon mode. |
| always_allow overrides can_place rejection | ❓ | `location_accepts_item` evaluates `can_place OR always_allow` (`rando_placement.c:377-386`). But no Phase A `always_allow` predicates are authored beyond the default FALSE. |
| Missing item reference fails the build | ✓ | Per codegen well-formedness (`rando_logic_gen.py:1238-1241`). |
| Orphan location fails the build | ✓ | Same. |
| Unreachable-under-full-inventory location fails the build | ✗ | Per `tasks.md:111` — "Full-inventory reachability ... are Phase A1 follow-ons once the full logic graph is populated." |

### randomizer-placement (`specs/randomizer-placement/spec.md`)

| Scenario | Status | Evidence |
|---|---|---|
| Known location grants substitute | ✓ | `Placement_Lookup` does linear scan (`rando_placement.c:54-62`). |
| Unknown location falls back to vanilla | ✓ | Returns `vanilla_item_id` when not found (`rando_placement.c:61`). |
| Vanilla path bit-identical when rando inactive | ❓ | No grant-site dispatches wired (§6 tasks 6.1-6.10 all open in `tasks.md`). |
| Audit deliverable is checked in before implementation | ✓ | `audit.md` exists; §0 gates ticked. |
| New grant site without dispatch fails the build | ⚠ | Guard script `assets/scripts/check_audit_guard.py:1` exists but is in non-blocking mode pending audit.md activation (`tasks.md:59`). |
| Small-key receive | ✗ | No receive paths added; §6.2 open. |
| Multi-tier rupee receive — small tiers silent | ✗ | No dispatcher route added; §6.2 open. |
| Rupee300 plays the item-receive cutscene | ✗ | Same. |
| Progressive sword grant advances by one level | ✗ | No `ProgressiveSword` receive helper. |
| Progressive sword grant at max is a no-op or junk-fill | ✗ | Same. |
| TriforcePiece grants increment counter and HUD | ✗ | No grant path. |
| HalfMagic / QuarterMagic grant | ✗ | No grant path. |
| PieceOfHeart vs BossHeartContainer routing | ✗ | No grant path. |
| Boss kill dispatches TWO locations | ✗ | §6.6 open. |
| Phase A boss-heart slots are identity-placed | ⚠ | The `_BossHeart` locations have vanilla_item_id=51 (BossHeartContainer) and are not in BuildItemPool's restriction logic; pool includes 10 BossHeartContainer items (`rando_placement.c:254`). They will be placed somewhere but the placer doesn't pin them to their vanilla slot. |
| Pyramid Fairy is a synthesized multi-slot grant site | ✗ | Location IDs 210-211 (Sword/Bow) and 213-214 (Left/Right) exist in the registry, but no grant-site code is wired. |
| Injection is atomic across a frame | ⚠ | `Rando_TryGrantStartingInventory` is a single function (`rando_placement.c:1003-1022`) — atomic at the language level. But it's never called from any new-game init flow. |
| Non-idempotent items don't double-grant | ⚠ | Gate cell `g_rando_starting_inventory_granted` (`rando_placement.c:1006`) handles idempotency. |
| Rupoor decrements wallet | ✗ | No grant path. |
| 5th-bottle grant is refused at generation time | ✗ | No 5th-bottle check anywhere. |
| Bottle substitute uses bottle-insertion path | ✗ | No grant path. |
| Non-receivable item rejected by generator | ✗ | No item-id validation against receivable enumeration. |
| Default drops unaffected in Phase A | ✓ | No drop-path code touched. |
| Open mode starting inventory applied at new game | ⚠ | `Rando_TryGrantStartingInventory` for Inverted grants MoonPearl+MagicMirror (`rando_placement.c:1009-1012`); Open path grants nothing. Spec doesn't explicitly require items for Open beyond the standard vanilla items, so technically OK. |
| No double-grant on reload | ⚠ | Gate cell handles this — but the function isn't yet called from anywhere. |
| Vanilla assets bypass the dialog | ❓ | UI dialog not implemented; §9 open. |
| Non-vanilla assets prompt once per unique hash | ✗ | UI not implemented. |
| Persisted decision is honored | ✗ | Same. |
| asset_hash_decisions INI format | ✗ | Same. |
| CLI --assets-must-be-vanilla refuses non-vanilla blobs | ✗ | Flag parsed but ignored (`main.c:343, 485`). |
| Aga 1 defeat updates tracker reachability | ⚠ | `Rando_BumpReachabilityCounter` exists (`rando.c:53-55`) but no call sites in the §6 grant-site PRs yet. |
| Audit deliverable enumerates every event flag | ✓ | `tasks.md §0.4a` ticked. |

### randomizer-save (`specs/randomizer-save/spec.md`)

| Scenario | Status | Evidence |
|---|---|---|
| Vanilla file untouched | ❓ | Sidecar code lives in `rando_save.c`; not yet wired to vanilla save path. |
| Absent sidecar is normal vanilla | ❓ | Same — load path not yet wired. |
| Round-trip | ✓ | `RandoSave_SelfCheck` exercises serialize/deserialize. |
| Slot header byte layout is exact | ⚠ | Slot header offsets match spec (`rando_save.c:117-138`): @4=slot_kind, @5=generator_version u16le, @7=settings_hash[16], @23=share_string[32], @55=last_vanilla_write_version u16le, @57=sram_slot_checksum u32le, @61=placement_table_size u16le, @63=flags, @64=reserved[16]. **However, `placement_table_size` is treated as a location-count (multiplied by 4), not bytes — see Bug #6.** |
| Sentinel placement entry is recognized | ⚠ | `Placement_Lookup` returns `vanilla_item_id` when location_id not present (`rando_placement.c:54-62`), but doesn't specifically check for `0xFFFF` item_id sentinel. |
| last_vanilla_write_version advances on every write | ❓ | Not wired to the actual write flow yet (§8.4 open). |
| Slot-kind discriminator | ⚠ | Enum exists but not consumed by file-select flow. |
| Truncation is first-16-bytes | ✓ | `Settings_HashShort` (`rando_settings.h:122`) writes 16 bytes of hash; `Spoiler_WriteJson` writes first 16 bytes as settings_hash_hex (`rando_spoiler.c:69`). |
| Same generator version loads cleanly | ❓ | Load path not wired. |
| Version drift loads with warning | ✗ | §8.5 open. |
| Location registry is append-only | ✓ | Registry discipline `location_registry.yaml:7-12`. |
| Newer binary reading older slot ignores beyond-prefix bitmap bits | ✗ | §8.5 open. |
| Bitmap sizing formula | ⚠ | `rando_save.c:69` computes bitmap as `(placement_table_size + 7) >> 3` — treating placement_table_size as location count. Spec says `(placement_table_size / 2 + 7) >> 3` treating it as bytes. Math agrees if and only if placement_table_size is interpreted as location count consistently, which contradicts the spec's header table that lists it as "bytes". |
| Drift detected after downgrade-then-re-upgrade | ✗ | §8.6 open. |
| No drift on clean upgrade | ✗ | Same. |
| Orphan sidecar handling | ✗ | Not implemented. |
| Sidecar present but sram.dat slot empty | ✗ | Not implemented. |
| sram.dat present but sidecar absent | ❓ | Trivially true if rando_save doesn't run. |
| Crash between sidecar and sram.dat | ❓ | Atomic-commit protocol partially in `rando_save.c` (`atomic_write_and_commit`); not wired to save order. |
| Crash mid-write of either file | ❓ | Same. |
| fsync ordering | ⚠ | `atomic_write_and_commit` calls fflush/fsync/rename per POSIX/Windows. Containing-directory fsync is implemented for POSIX. Save-order between sidecar/sram.dat not coordinated. |
| Spoiler written at slot creation | ✓ | CLI writes spoiler; `Spoiler_ResolvePath` derives path from share_string + config dir (`rando_spoiler.c:193-209`). |
| Race-mode file contains only stamp | ✗ | Phase B. |
| Reveal verifies stamp | ✗ | Phase B. |
| Vanilla snapshot unchanged | ❓ | Snapshot tail-TLV chain not yet wired (§8.8 open). |
| Rando snapshot reload restores placement | ✗ | Same. |
| Replay mode preserves rando | ✗ | Same. |
| Older binary degrades gracefully | ✗ | Same. |
| Newer binary reads older rando snapshot (cross-version TLV) | ✗ | Same. |

### randomizer-shuffles (`specs/randomizer-shuffles/spec.md`)

| Scenario | Status | Evidence |
|---|---|---|
| Phase A randomizes prize and medallion by default | ✓ | `Settings_SetDefaults` sets both flags to 1 (`rando_settings.c:44-45`). |
| Prize shuffle disabled — vanilla mapping populates placement table | ⚠ | `PrizeShuffle_Run` returns identity assignment when disabled (`rando_shuffles.c:132-145`). **BUT the `_Prize` locations (boss-drop crystals/pendants) are not pinned by `place_assumed_fill_attempt` — see Bug #1.** |
| Medallion shuffle disabled — vanilla mapping populates predicate | ✓ | `MedallionShuffle_Run` returns vanilla MM=Ether, TR=Quake (`rando_shuffles.c:175-180`). |
| Boss shuffle is Phase B (disabled in Phase A) | ✓ | No boss_shuffle field. |
| Disabling a Phase A shuffle reverts to vanilla | ⚠ | Prize shuffle identity returns vanilla mapping. **However the placement RNG is still advanced** because `place_assumed_fill_attempt` always calls `PrizeShuffle_Run`/`MedallionShuffle_Run` on the shuffle_rng (`rando_placement.c:508-512`) regardless of whether the shuffle is enabled. **Bug**: the spec says "Disabled modules SHALL NOT advance the placement RNG" — but the shuffle_rng is a dedicated RNG separate from the placer's, so it doesn't advance placement directly. Still, the call to `Rng_NextRange` inside the shuffle (`rando_shuffles.c:114-120`) DOES consume bytes from this RNG. |
| Shuffle toggles are reflected in settings hash | ⚠ | `prize_shuffle`/`medallion_shuffle` are in canonical bytes (`rando_settings.c:65-66`), so toggling them changes the hash. ✓ semantically. |
| Wild small keys adds locations to the world pool | ✗ | No location-pool augmentation logic for Wild mode. `BuildItemPool` adds the items but `kRandoLocations[]` is static. |
| Mixed modes per item class | ⚠ | `BuildItemPool` consults per-class mode (`rando_placement.c:259-278`); pool size differs. But the `_BossHeart` slot pool participation isn't toggleable; the location pool doesn't change. |
| Sahasrahla unlocks when Green Pendant dungeon is cleared | ⚠ | `OP_HAS_PRIZE(Prize_GreenPendant)` predicate evaluates iff (a) some dungeon holds that prize and (b) that dungeon is in `cleared_dungeons_bitmask`. The bitmask is updated based on whether the dungeon's boss location is reachable (`rando_logic.c:459-482`). Functionally correct IF the dungeon boss is reachable. |
| Sword Pedestal requires all three colored pendants | ❓ | Master Sword Pedestal (location 151) has its own can_reach predicate; need to verify the predicate uses three OP_HAS_PRIZE calls. |
| Misery Mire entrance gates on its assigned medallion | ✓ | The edge `DarkWorld_Mire -> MiseryMire_Entrance` predicate uses `OP_MEDALLION_OPENS(MiseryMire)` per the YAML (`logic_data.c:688`). Evaluator consults the per-seed assignment. |
| Misery Mire and Turtle Rock have independent assignments | ✓ | `MedallionShuffle_Run` samples each entrance independently (`rando_shuffles.c:187-190`). |
| (Entrance shuffle Phase C) | ✗ | Out of scope for Phase A. |
| (Boss shuffle Phase B) | ✗ | Out of scope. |
| (Drop-pool Phase B) | ✗ | Out of scope. |
| (Cosmetic Phase D) | ✗ | Out of scope. |

### randomizer-ui (`specs/randomizer-ui/spec.md`)

Phase A1 deliberately defers UI to Phase A2 (`tasks.md §9` mostly open). Per the proposal at line 21-25, UI lands in Phase A2. Marking all UI scenarios `✗` here is expected, not a finding.

| Scenario | Status |
|---|---|
| PC keyboard typing | ✗ |
| Switch swkbd is invoked | ✗ |
| Switch swkbd dismissed without confirming | ✗ |
| Paste from clipboard | ✗ |
| Picker operable with d-pad and two buttons | ✗ |
| Default focus on handheld | ✗ |
| Empty slot prompts for kind on new game | ✗ |
| Existing slot loads its kind | ✗ |
| Copy refuses cross-kind | ✗ |
| Erase resets kind to empty | ✗ |
| Preset application | ✓ | `Settings_ApplyPreset` (`rando_settings.h:168`) — UI hook missing but logic done. |
| Share-string paste populates fields | ⚠ | `Share_PastePath` (`rando_share.h:62`) hook ready. |
| Invalid share string surfaces inline error | ✗ |
| Non-vanilla asset data triggers a one-time dialog per hash | ✗ |
| Icon hash distinguishes seeds with identical settings | ✗ |
| Icon hash matches for identical share strings | ✗ |
| Pause-menu shows the same hash as the banner | ✗ |
| Atlas size is registry-pinned | ✗ — no `assets/rando/icon_atlas.yaml` present. |
| Rando banner fits | ✗ |
| Vanilla slot unchanged | ✓ (no changes to vanilla file-select code) |
| Settings preserved without explicit opt-in | ✗ |
| Apply-recommendations flips all to recommended | ✗ |
| Toggle binding (tracker) | ✗ |
| Re-render gated on inventory-change counter | ✗ |
| Reachability updates on inventory change | ✗ |
| Checked location persists across save/load | ✗ |

## 2. Spec ↔ implementation contradictions

1. **Settings canonical serialization order disagrees with spec**. The spec at `specs/randomizer-core/spec.md:31-57` pins a 21-field, 20-byte normative layout: `mode_state, goal, crystals_ganon, crystals_tower, tricks, item_pool, logic, mode_weapons, accessibility, region_pyramid_bow_upgrade, region_boss_hearts_in_pool, 4 × dungeon_items, prize_shuffle, medallion_shuffle, race_mode, pieces_required(u16), pieces_placed(u16)`. The implementation at `src/rando/rando_settings.c:55-75` is a different 17-field 20-byte layout starting with `settings_version` and missing `tricks`, `logic`, `region_boss_hearts_in_pool`, and `race_mode` entirely. `pieces_required`/`pieces_placed` are u8 not u16. **Effect**: settings_hash is not what the spec defines.

2. **Save sidecar `placement_table_size` semantics**. Spec at `specs/randomizer-save/spec.md:42, 46, 48` defines `placement_table_size` as bytes and embedded table as a flat array of uint16 item IDs indexed by location_id. Implementation at `src/rando/rando_save.c:67-71, 148-181` treats it as a count of (location_id, item_id) pairs (`* 4` to size the data). The bitmap-sizing formula in the impl `(placement_table_size + 7) >> 3` only matches the spec if `placement_table_size` is interpreted as location count, but then the slot header field name in spec ("bytes") is misleading. Either way the on-disk format is incompatible with the spec's described layout.

3. **`Logic_ComputeReachability` location bitset is location_id-indexed, not iteration-indexed**. Spec at `specs/randomizer-logic/spec.md:32-35` says `OP_REGION_REACHABLE` should memoize "per fixed-point iteration of the reachability outer loop ... the cache is invalidated on every fixed-point iteration boundary because adding an item between iterations can make a previously unreachable region reachable." The impl at `src/rando/rando_logic.c:374-490` uses one growing bitset across iterations. **Reachability is monotone so this happens to be correct for `can_reach`, but it does not model the spec's cache-invalidation contract.**

4. **Inverted world-state is unimplemented**. `src/rando/logic_data.c:709`: `kRandoStartRegionByWorldState[2] = 0xFFFF` (Inverted has no start region). Spec scenario `Inverted mode reverses light/dark default and entrance pair` (`specs/randomizer-logic/spec.md:71-75`) cannot pass — Inverted seeds produce empty reachability.

5. **Retro world-state location pool is unimplemented**. Spec scenario `Retro mode adds shop-purchased items to pool` cannot pass — `BuildItemPool` doesn't differentiate Retro.

6. **The `_Prize` locations (boss-drop crystals/pendants) are not pinned to the prize-shuffled item**. The pin code at `src/rando/rando_placement.c:568-589` only pins type=12 (Prize_Event) and type=13 (Medallion). The `_Prize` boss-drop locations are type=10 (Prize_Crystal) or type=11 (Prize_Pendant). These get junk-filled. **Consequence**: counts[Prize_Crystal*] is never incremented in `Logic_ComputeSpheres`, so `HAS_ANY_COUNT([Prize_Crystal1..7], 7)` (the crystal-count gate for GT entry, Ganon vulnerability, etc.) never evaluates true. Same for OP_HAS_PRIZE pendant-gates (Sword Pedestal, Sahasrahla).

7. **Generator does not refuse un-completable seeds**. Spec at `specs/randomizer-core/spec.md:294-300`: "If the predicate evaluates false ... the generator SHALL fail with a clear error and SHALL NOT write a spoiler file or sidecar slot." Impl at `src/main.c:448-454`: writes spoiler regardless; only sets `goal_completable=false`. CLI exits 0.

## 3. Likely bugs (by severity)

### Bug #1 (CRITICAL — explains the user-reported "46/236 reachable")
**Location**: `src/rando/rando_placement.c:568-589`. **Symptom**: `_Prize` boss-drop locations (type=10/11) are not pinned to their prize-shuffled crystal/pendant. They get junk-filled. As a result:
- `counts[Prize_Crystal1..7]` stays at 0 throughout sphere walking.
- `HAS_ANY_COUNT([Prize_Crystal1..7], 7)` (the GT-entry gate, Ganon-vulnerability gate) never evaluates true.
- `OP_HAS_PRIZE` evaluation does function (boss-location reachability drives `cleared_dungeons_bitmask` via `rando_logic.c:453-487`), but `HAS_ANY_COUNT` over crystal items does not.
- Pyramid Fairy locations gate on `OP_HAS_PRIZE(Prize_Crystal5) AND OP_HAS_PRIZE(Prize_Crystal6)` — these can fire IF PoD and SP (or whichever dungeons hold those prizes post-shuffle) are reachable.
- Master Sword Pedestal predicate (need to verify) likely gates on three OP_HAS_PRIZE calls — works if all three pendant dungeons are reachable.

**Fix sketch**: Extend the pin loop to also pin type=10/11 locations to their prize-shuffled item per the assignment table. Then in sphere walking, when a `_Prize` location is reached, the placed Prize_Crystal item enters counts via the existing accumulation step.

### Bug #2 (CRITICAL)
**Location**: `src/rando/logic_data.c:398-636` (codegen output). **Symptom**: 207 of 237 locations have `region_id = 0xFFFF` because the YAML files for PoD, SP, SW, TT, IP, MM, TR, GT, and all overworld parts (LW NW/South/DM, DW, fountains) do not tag their locations with a `region:` field. Counts: `region: tags in locations` is zero for `20_palace_of_darkness.yaml`, `21_swamp_palace.yaml`, `22_skull_woods.yaml`, `23_thieves_town.yaml`, `30_ice_palace.yaml`, `31_misery_mire.yaml`, `32_turtle_rock.yaml`, `33_ganons_tower.yaml`, `40_lightworld_northwest.yaml`, `41_lightworld_south.yaml`, `42_lightworld_deathmountain.yaml`, `43_darkworld.yaml`, `44_fountains.yaml`.

The codegen at `assets/rando_logic_gen.py:1018-1024` defaults `region_id = 0xFFFF` when no region is specified. With `region_id = 0xFFFF`, `Logic_ComputeReachability` skips the region-reachability check (`rando_logic.c:442-445`), so locations are reachable purely by their `can_reach` predicate.

**Effect**: dungeon-entry edges (e.g., `LightWorld_South -> IcePalace_Lobby` requiring melt/MoonPearl/Flippers/CanLiftDarkRocks) are dead code — they never gate any location. Many dungeon-internal locations become "trivially reachable" since the edge into the dungeon doesn't gate them. Conversely, the assumed-fill placer doesn't know that placing Hookshot in IP requires being able to enter IP first, so it can produce unwinnable placements where Hookshot is at IP Spike Room and the player can't get there without Hookshot.

**Fix sketch**: Add `region:` tags to every location in every `logic_parts/*.yaml` file. (10 files, ~150 locations to tag.)

### Bug #3 (HIGH)
**Location**: `src/rando/rando_settings.h:80-98`. **Symptom**: `RandoSettings` struct is missing 4 spec-mandated fields: `tricks`, `logic`, `region_boss_hearts_in_pool`, `race_mode`. Spec scenarios "Race-mode toggle changes settings hash" and "Phase A setting axes (pinned values)" cannot pass. **Fix**: add fields; bump `kSettingsCanonicalLen` to 24; reorder canonical serialization to match spec; bump kGeneratorVersion.

### Bug #4 (HIGH)
**Location**: `src/rando/rando_placement.c:819-826`. **Symptom**: `Goal_IsCompletable(FastGanon)` does not check `crystals.ganon` or `crystals.tower` — it only checks Ganon-reachable. A seed with `crystals.ganon=7` and only 3 crystals placed at reachable locations should fail, but currently succeeds. Spec scenario "Fast Ganon gates on crystals and tower access" fails.

### Bug #5 (HIGH)
**Location**: `src/rando/rando_settings.c:33-51`. **Symptom**: `Settings_SetDefaults` does NOT auto-set `accessibility=locations` when `goal=Completionist`. Only `Settings_ParseCsv` does (`rando_settings.c:328-329`). Direct API users (e.g., a future UI that calls `Settings_SetDefaults` then sets `goal=Completionist`) get `accessibility=items`. Spec scenario "Completionist forces accessibility = locations" fails for non-CLI paths.

### Bug #6 (HIGH — save format)
**Location**: `src/rando/rando_save.c:67-71, 148-181`. **Symptom**: `placement_table_size` semantics disagree with spec. Spec defines it as bytes (location_id is the array index). Impl treats it as a count of (location_id, item_id) pairs. Cross-version upgrade safety the spec relies on (newer-binary-reads-older-slot by reading exactly N bytes) breaks because the on-disk format is denser than the spec describes.

### Bug #7 (MEDIUM)
**Location**: `src/rando/rando_placement.c:421-479`. **Symptom**: `Place_AssumedFill`'s "bounded retry" is whole-attempt retry (`kAssumedFillMaxAttempts=8` with `attempt_seed = seed_u64 ^ ...`), not the per-item rewind the spec describes ("rewinds the last N placements and retries"). Also: the budget_seconds parameter is unused (line 425: `(void)budget_seconds;`).

### Bug #8 (MEDIUM)
**Location**: `src/rando/rando_spoiler.c:85, 172-173`. **Symptom**: `fallback_warnings: []`, `playthrough: []`, `regions: []` are hardcoded empty. The forward-fill counter is computed (`rando_placement.c:691`) but not propagated to the spoiler. Spec scenarios `fallback_warnings records forward-fill fallback` and `Forward-fill fallback is surfaced prominently` fail.

### Bug #9 (MEDIUM)
**Location**: `src/rando/rando_spoiler.c:240-243`. **Symptom**: Text spoiler is NOT grouped by region. Spec scenario `Text spoiler is grouped by region` fails. The codegen has region metadata available; the writer just doesn't use it.

### Bug #10 (MEDIUM — silent corruption)
**Location**: `src/rando/rando_placement.c:421-479`. **Symptom**: `Place_AssumedFill` returns `true` even when `best_complete=false` (placements unreachable). Caller (`main.c:404-409`) treats `true` as "OK" and proceeds. The user gets a spoiler that says `goal_completable=false` and a CLI exit 0. Should fail per spec.

### Bug #11 (LOW)
**Location**: `src/rando/rando_placement.c:331-357`. **Symptom**: `is_progression_item` classifies item ids 111..120 (prize pendants/crystals) as progression but these items are NEVER added to `BuildItemPool` (see `BuildItemPool` line by line; no `pool_add(pool, n, capacity, ID_Prize_*, ...)` exists). The progression filter consequently never partitions any Prize_* item into `progression[]`. Dead classifier code that doesn't affect runtime today but is misleading.

### Bug #12 (LOW)
**Location**: `src/rando/rando_placement.c:1003-1022`. **Symptom**: `Rando_TryGrantStartingInventory` is implemented but no production code path calls it. Starting-inventory injection scenarios are vacuously satisfied because injection never runs.

### Bug #13 (LOW)
**Location**: `src/rando/rando_placement.c:308-307`. **Symptom**: `pieces_required > pieces_placed` is not validated before pool construction; would cause an unwinnable seed silently. Spec scenario `Unwinnable Triforce-Hunt input is refused` fails.

### Bug #14 (LOW — predicate-VM edge case)
**Location**: `src/rando/rando_logic.c:84-96` (`eval_has_any_count`). The threshold `n` is read AFTER all ids, but a malformed/zero-id-count bytecode (count=0 followed by just an `n` byte) is accepted; it returns `sum=0 >= n`. With count=0 + n=0 → true; with count=0 + n=1 → false. The vacuous-AND/OR convention is documented; vacuous HAS_ANY_COUNT is not. Probably benign but worth a self-test.

### Bug #15 (LOW)
**Location**: `src/rando/rando_logic.c:230-235` (`eval_glitch`). Reads operand into `level` but then evaluates `0 >= level` — i.e., true only when level=0. Comment claims Phase A pins logic=NoGlitches (level 0). OK but the comparison should use `ctx->settings->logic >= level` once `logic` is added to settings. Currently always-true at level 0.

### Bug #16 (CONCERN)
**Location**: `src/rando/rando_placement.c:534-535`. The world-state filter `if (loc->world_state_filter != 0 && !(loc->world_state_filter & (1u << settings->world_state)))` is correctly applied during open-location collection. But all 237 locations have `world_state_filter = 0x00` per `logic_data.c:398+`, meaning NO location is filtered. Inverted/Retro-only locations and Inverted-exclusion locations aren't yet declared.

## 4. Phase A1 completeness assessment

Per `tasks.md §14.1b` Phase A1 acceptance criteria:

| Criterion | Status |
|---|---|
| `logic.yaml` covers all Phase A locations across 4 world-states | ✗ Standard only; Open/Inverted/Retro share the same graph; Inverted has no start region. |
| Predicate VM passes the per-registry full op-suite test (all 15 Phase A ops) | ✓ `Logic_SelfCheck` covers each op (`rando_logic.c:488-710`). |
| Reachability median time green on reference and Switch | ❓ Benchmark not in CI. |
| Goal predicates green for all 7 Phase A goals | ⚠ `Goal_IsCompletable` covers all 7 but FastGanon ignores crystal counts (Bug #4) and Triforce Hunt conflates "reachable pieces" with "inventory pieces" (Bug). |
| Assumed fill with forward-fill fallback passes 50-seed regression corpus | ⚠ Corpus harness exists with 1 entry (`tests/rando_corpus/manifest.yaml`); not 50. |
| Spoiler writer schema-validates against ALTTPR-mirrored meta block | ⚠ Missing `seed_u64` in meta block; `playthrough` / `regions` always empty. |
| `randomizer-core` and `randomizer-logic` spec scenarios green | ✗ Multiple ✗ above. |

**Overall**: Phase A1 is **substantially behind** its acceptance gate. The logic graph is roughly half-translated (regions wired but locations unbound to regions in 13 of 16 logic_parts files), the prize-pinning gap renders GT/Pedestal/Pyramid-Fairy unreachable for most seeds, and settings are missing 4 mandated fields. Determinism guards, RNG, share-string, predicate VM, codegen pipeline, and CLI single-seed mode are solid. The structural gaps are concentrated in the logic graph and the placement-pinning step.

## 5. Focused recommendations (1-3 things)

1. **Fix Bug #1 first (prize-slot pinning)**. Extending the pin loop at `src/rando/rando_placement.c:568-589` to also pin type=10/11 locations to the prize-shuffled item is a ~20-line change. Run the user's repro and see if reachable-count jumps from 46 to ~140. This validates the prize-shuffle plumbing end-to-end and unblocks GT/Pedestal/Pyramid-Fairy.

2. **Fix Bug #2 next (region tags on dungeon/overworld locations)**. Edit the 13 `logic_parts/*.yaml` files that have zero `region:` tags. Mechanically: every location belongs to the region named in the file's `regions:` section. Once tags are present, the dungeon-entry edges become live gates, the assumed-fill placer can't lock keys inside their own dungeon without the right entrance items, and the structural logic-graph correctness goes from "207 locations region-unbound" to "0".

3. **Fix Bug #6 (save sidecar format)** before any §8 task lands a real save flow. The placement_table_size byte/count confusion will become much more expensive to untangle once slot files exist in users' filesystems. Consensus on whether the spec's byte interpretation or the impl's pair interpretation is correct should land first; pick one and align both. The spec's "indexed by location_id" interpretation enables cross-version compatibility cleanly and is the harder one to retrofit later.

Items #1 and #2 together likely lift Phase A1 from "produces broken seeds" to "produces playable seeds in Open mode" — the largest single win on the path to the §14.1b gate.
