// rando.h — randomizer master header.
//
// This module is the seed-generation pipeline, the runtime override layer for
// vanilla item-grant sites, and the host of the per-slot randomizer state. All
// public types and entry points the rest of the codebase consumes live here;
// the .c files under src/rando/ implement them.

#ifndef ZELDA3_RANDO_H_
#define ZELDA3_RANDO_H_

#include "../types.h"
#include "rando_settings.h"  // RandoSettings (Rando_GetActiveSettings)
#include "rando_logic.h"     // RandoCounts, RandoReachability (live reachability bridge)

// ---------------------------------------------------------------------------
// kGeneratorVersion — bumped per tasks.md §13.6 whenever placement output
// could change. The bump triggers regression-corpus regeneration.
// ---------------------------------------------------------------------------
#define kGeneratorVersion 89u  // 88→89: add-rando-pot-sanity (task #25) — model dungeon POT keys under WILD keys. When pot_shuffle turns a dungeon's key pots into live checks, the pot small keys must (a) enter the item pool, or they vanish (kVanillaSmallKeyCounts is the placed/chest count only) — the wild/pot strand the owner hit, and (b) gate the dungeon's deep locations + the pots themselves, or the placer mis-reaches them. Both gate on the new OP_POT_KEYS_WILD (pot_shuffle>=Keys AND door-off AND small keys WILD): rando_placement.BuildItemPool pools each active key pot's vanilla SmallKey (GenericKey under Retro), and rando_logic_gen wraps the 54 affected chest/boss/prize locations + 102 pot rooms as `<vanilla> AND (NOT POT_KEYS_WILD OR HAS_AMOUNT(SmallKey_X, full))`, where `full` is the prover WORST-CASE key-door depth (--dump-key-depth) CAPPED at the pooled key count (chest+pot_keys) — the external requirement under wild, since nonpot drops auto-collect in context. DUNGEON-keys in-context modeling is a follow-on (kept on the vanilla branch). Validated 100%-accessible (accessibility=items) across wild pot_shuffle keys/contents/all + Retro + triforce-hunt. Corpus: ONLY pot-all-retro-ganon moves (the lone wild+pot seed); every pots-off / Vanilla-keys-pot / dungeon-keys-pot / non-pot seed stays byte-identical (126/127). New wild+pot corpus entries added. — 87→88: add-rando-pot-sanity — close the in-dungeon chest-less-pot sphere-0 LEAK class the kGen-87 guard surfaced (21 flagged rooms). 10 get a reviewed room_can_reach ENTER gate, traced from the door-shuffle compiled door tables (src/rando/door_tables.gen.c — REAL per-door SmallKey/BigKey flags + region graph, authoritative over the grid-adjacency pot_dump whose listed edges are often walls; cross-checked vs ALTTPR PHP chest predicates; never-under-gate): TR 015/023=SmallKey_TurtleRock>=3, PoD 01b=CanShootArrowsL1, SW 049=SmallKey_SkullWoods, TT 054=BigKey_ThievesTown, DP 063=CanLiftRocks (free back-door floor, pre-key), GT 05d/06c/06d=CanShootArrowsL1 AND SmallKey_GanonsTower>=1, GT 09b=Hammer AND Hookshot AND SmallKey_GanonsTower>=1. The 3 key pots are deadlock-safe (063 pre-key on the free floor; 09b non-blocking; 09f free). The other 11 are GENUINELY FREE (entrance/lobby, GT Hope/catwalk, MM bridge, TR hub, + 4 IP free-fall descent rooms incl. the 09f key pot — key-free, Iced T Room=TRUE) and go in pot_logic_overrides.yaml room_free:, which the now-HARD-FAILING guard skips. 4 corpus entries move — the Contents/All-tier pot seeds (incl. All+open-dungeon-keys, where the now-gated key pots 063/09b shift the fill); the Keys-tier (pinned), Off, door, and every non-pot seed stay byte-identical. Corpus regenerated.
                               // 86→87: add-rando-pot-sanity — fix 2 fresh-eyes-reviewed in-dungeon chest-less-pot sphere-0 leaks (the in-dungeon sibling of the dark/HCE class the no-item probe couldn't see): ToH room 0x017 + 0x096 = HAS_ITEM(BigKey_TowerOfHera) (the stair floor between the Big-Key Big Chest 0x027 and boss 0x007); Swamp 0x056 + 0x026 = SmallKey_SwampPalace AND Hammer (water-drained interior). Both were region-bound/flooded but un-gated (TRUE), so the placer treated 30+ pots as sphere-0 and could strand the dungeon's own Big/small key there -> unbeatable. ALSO: gen_pot_tables.py grows a GUARD that flags any chest-less TRUE() pot room whose in-region door/vstair base-chest neighbors are ALL gated (surfaces the whole class; 21 more candidates across PoD/TR/GT/Ice/MM/TT/SW/DP remain, follow-up). Contents/All move; Keys/Off/non-pot byte-identical. Corpus regenerated.
                               // 85→86: add-rando-pot-sanity — HCE sewer pot rooms (0x002/0x021/0x031) gated via reviewed pot_logic_overrides.yaml room_can_reach. The dark-flag Lamp gate (kGen 85) missed HCE's CanKillEscapeThings (escape guards) + the SmallKey-locked sewer door — HCE is the only free-entry dungeon with a small key, so this was the last no-item sphere-0 pot leak. Grounded in the door graph + base predicates (logic.yaml): 002 (a dead-end reached only through the key-locked Sanctuary 012) = CanKill+SmallKey+Lamp; 021 (dark) + 031 (Dark-Cross side, before the locked door) = CanKill+Lamp. gen_pot_tables.py derive_regions now also applies a room_can_reach override WITHOUT a redundant room_region (gate-only, for a correctly-flooded chest-less sub-area). Probe-verified: no dungeon pot is reachable with zero items except genuinely-free rooms (HCE castle, EP lobby — base chests TRUE). Contents/All move; Keys+Off+non-pot byte-identical. Corpus regenerated.
                               // 84→85: add-rando-pot-sanity — DARK pot rooms (engine room-header dark bit hdr[0]&1, emitted as 'K' lines by --dump-pot-table) are now gated on (HAS_ITEM(Lamp) OR CanDarkRoomNav()) — the exact base-logic dark-room predicate (EP 01_eastern_palace.yaml:87, HCE Sewers). Fixes the dominant chest-less-room→TRUE() sphere-0 leak: dark pot rooms (HCE sewers, EP's Big Key Chest room, every dungeon's dark interior) were reachable with NO Lamp, so the placer could strand the Lamp (or any progression) behind a pot that itself needs the Lamp. 28 dark rooms; chest-BEARING dark rooms already inherited the Lamp from their chest, so they're skipped. Placement-affecting: Contents/All (+ any dark KEY pot → Keys) move; pot_shuffle=Off + every non-pot seed byte-identical. Corpus regenerated. KNOWN RESIDUAL (follow-up): HCE's LIT sewer-access rooms still need SmallKey_HyruleCastleEscape — a locked-door gate the door graph doesn't model; HCE is the only free-entry dungeon with a small key, so it's the lone remaining no-item key leak.
                               // 83→84: add-rando-pot-sanity — room 0x104 (Link's House, shared with the glitch-only Chris-Houlihan "Top Secret Room" via entrance 130) UN-excluded by owner request: its 3 HeartRefill pots become Contents-tier checks, sphere-0 reachable from the start. The generator assigns pot ids by (room,pos4) order, so inserting room 0x104 shifts the ~82 higher-room pots by +3 — but those are ALL overworld interiors (Contents tier); dungeon key pots live in rooms < 0x104 and base locations are unmoved, so pot_shuffle=Off, Keys, and every non-pot seed stay byte-identical. Only Contents/All move (3 new pots + the id shift change the fill). Corpus regenerated.
                               // 82→83: add-rando-pot-sanity — Dark World OVERWORLD pots now require the not-a-bunny gate `(HAS_ITEM(MoonPearl) OR CanPearlBypass())`. 21 chest-less DW cave/house pots (DW DeathMountain/Mire/NorthWest, incl. Blind's Hideout 0x119 + Village House 0x11f) had defaulted to can_reach=TRUE() — reachable the instant the region is, which you reach as a BUNNY via a portal; but you can't LIFT a pot as a bunny, so the placer could strand progression (even the Moon Pearl itself) behind one. Fix in gen_pot_tables.py mirrors every base DW location (all require Moon Pearl). Open/Standard correct; Inverted's DW is the bunny-free home so this slightly OVER-restricts inverted pots (untested combo — follow-up). Placement-affecting: pot_shuffle seeds with an active DW pot MOVE; pot_shuffle=Off + all non-pot seeds byte-identical. Corpus regenerated (the 6 pot entries' digests change; non-pot entries identical).
                               // 81→82: add-rando-pot-sanity Phase 3 — the `pot_shuffle` axis (Off/Keys/Contents/All; canonical [26] bits 6-7 + [27] bit 7, the LAST free bits at length 28) turns LOCTYPE_Pot rows into live placement locations per tier. pot_shuffle=Off keeps the default settings_hash AND every existing placement/sphere digest byte-identical (the shared pot_active() predicate skips every pot in the open-loc + junk-pad + selfcheck loops — corpus verified 121/121 at v81 before the bump). Non-off tiers are new: loot/key pots become fillable open slots (key pots follow dungeon_small_keys_mode via location_is_prepinned — pinned vanilla, open when shuffled), and empty pots are pinned to the new ITEM_Nothing filler (id 148) in a dedicated §3b pre-pass (never a pool item, never a hint/customizer target). Door shuffle forces every pot inactive (v1 — the key-prover doesn't model pot locations; apply_derived_rules normalizes pot_shuffle off, keyed on Settings_EffectiveDoorShuffle so hash and placement agree). Placement_Lookup now binary-searches the location-id-sorted table. Corpus MUST be regenerated (bump_rando_corpus.py --apply): all existing entries stay byte-identical, new Keys/Contents/All/door×Keys/Retro×All entries added.
                               // 80→81: add-rando-trap-catalog — new `insanity` trap frequency (every eligible junk pickup becomes a trap). The `traps` field widens to 3 bits, non-contiguously: low 2 bits stay in canonical [26] bits 2-3 (so off/low/medium/high are byte-identical and instant_flute at bit4 is untouched), the 3rd bit (Insanity=4) goes in the free [26] bit5. Default (traps off) byte-identical; only `insanity` seeds are new.
                               // 79→80: add-rando-trap-catalog — context-locked traps (Cucco/Darkness) are now placement-filtered to compatible locations (Cucco only at overworld free-standing types, Darkness only in dungeon regions) via Rando_PickTrapEffectId's loc_flags, so the placed effect always matches the spoiler instead of silently falling back at runtime. Moves traps-on placement digests; default (traps off) stays byte-identical.
                               // 78→79: add-rando-trap-catalog — the trap TYPE at each junk-replaced slot is now selected deterministically per (seed, location_id) across a 16-effect catalog filtered by the trap_categories mask, replacing the positional Damage/Freeze alternation. Only traps-on seeds move placement_digest (traps-off byte-identical); trap_categories packs into canonical [27] bits 2-6 (default 0 ⇒ byte-identical), kSettingsCanonicalLen unchanged.
                               // 77→78: enemy-shuffle GFX-sheet widening enabled (dungeon sprite-palette gate + verify-then-commit fillability), POSITION-aware sheet loaded-check (Javelin Soldier tile bug), and contact-damage class-2 exclusion (zero-damage bug). Runtime-only — placement draws no fill RNG and adds no predicate, so placement_digest stays byte-identical; settings_hash / kSettingsCanonicalLen unchanged. The bump version-locks the now-live enemy-shuffle behavior. (v77 was taken concurrently by main's medallion-config change below.)
                               // 76→77: MM/TR medallion config slots no longer behave like item placements in spoilers, hints, accessibility, or sphere item grants. Text/JSON spoilers emit actual medallion requirements from the assignment table. Hints-on JSON/race stamps can change; seeds that previously used fake config-slot medallions for reachability may move. settings_hash unchanged.
                               // 75→76: instant_flute becomes a canonical seed setting (default on, [26] bit4 disables). CanFly now branches on OP_INSTANT_FLUTE, so instant_flute=false restores the old activation route. Default settings_hash / placement digests stay byte-identical vs v75; kSettingsCanonicalLen unchanged.
                               // 74→75: rando flute pickup now grants the active bird-woken flute immediately (link_item_flute=3 + FluteActive ownership), and CanFly no longer requires the old separate activation route. Inverted flute-gated placement/sphere digests can move; settings_hash / kSettingsCanonicalLen unchanged.
                               // 73→74: fix generation-time reachability certification for retry-exhausting seeds. Place_AssumedFill's best-so-far return reinstalls the restored attempt's prize/medallion assignment, and Rando_Set*Assignment copies into owned stores. Placement digests stay byte-identical; sphere_digest moves only for seeds whose old post-placement pass read a stale later attempt's assignment. settings_hash / kSettingsCanonicalLen unchanged.
                               // 72→73: Inverted reachability now ignores base edges that duplicate Inverted edges, so placement evaluates the Inverted graph instead of base ∪ inverted. Inverted placement/sphere digests move; Open/Standard/Retro stay byte-identical. Also emits cross_decoupled_exit JSON only for cross-decoupled seeds, affecting only those race stamps.
                               // 71→72: decoupled entrance shuffles pin Ice Palace + Swamp Palace exits as fixed points so one-way decoupled exits cannot strand Link behind Flippers-only, water-isolated doors. Placement/sphere digests stay byte-identical; decoupled runtime permutations and race stamps are version-locked.
                               // 70→71: retire the native boss-heart shuffle checkbox and always shuffle boss-heart drops. The legacy region_boss_hearts_in_pool byte remains parseable but canonicalizes to 0; default settings_hash and placement digests move because boss drops become fillable locations and boss-heart items enter the pool.
                               // 69→70: fix boss-heart pool double-counting. Pinned mode no longer adds extra BossHeartContainer pool copies, and junk padding now excludes every pre-pinned slot so pool size matches fillable open slots. Also includes digest-neutral/runtime fixes for Silver Arrow door predicates, deterministic generation budgets, customizer caps, sidecar v3, and door-logic cleanup.
                               // 68->69: add traps={off,low,medium,high} in canonical byte [26] bits2-3 plus TrapDamage/TrapFreeze item ids. traps=off keeps existing settings/placement digests byte-identical; traps-on seeds deterministically replace eligible junk and need version-locking.
                               // 67→68: retire the two Fountain bottle slots from the registry + pool. They had no runtime grant site under the chest model, so placed items there were uncollectable. Removing two placeable locations changes placement/sphere digests; settings_hash / kSettingsCanonicalLen unchanged.
                               // 66→67: add customizer mode. A manifest pins selected locations and assumed-fill completes the rest, gated by canonical [26] bit1 (customizer_active). Non-customizer seeds stay byte-identical; customizer settings and manual-placement generation are version-locked.
                               // 65->66: add door_shuffle in canonical byte [27] bits 0-1. Door-controlled dungeon locations wrap reachability with OP_DOORS_ACTIVE/OP_DOORS_LOC_REACHABLE, evaluating exactly vanilla when off. kSettingsCanonicalLen and default settings_hash unchanged.
                               // 64→65: enemy_shuffle now reshuffles sprite-group sheets and randomizes per-type HP/contact damage under the existing enemy_shuffle axis. It is orthogonal to placement and adds no canonical axis; no-corpus enemy_shuffle coverage means only generator_version changes there.
                               // 63→64: NoLogic now short-circuits can_place predicates as well as reachability. Dungeon item containment still applies in C, so key mode safety remains. NoLogic + shuffled-key seeds can move; settings_hash/canonical unchanged.
                               // 62→63: add additional ALTTPR glitch disjuncts for non-nested sites such as Superbunny Cave, Mire Shed, Desert Past Lanmolas, King's Tomb, and inverted Ice Palace. Only the HybridMajorGlitches coverage entries move; settings_hash / canonical layout unchanged.
                               // 61→62: reclassify raw major-glitch thresholds to technique macros where canOneFrameClipOW is MajorGlitches-only and canOneFrameClipUW stays HybridMajorGlitches+MajorGlitches. This closes overreach at HybridMajorGlitches and moves only the affected HMG coverage entry.
                               // 60→61: add the enemy type substitution axis as live runtime slot behavior. enemy_shuffle packs into canonical byte [26] bit0 and stays orthogonal to placement; default settings_hash and placement/sphere digests remain byte-identical.
                               // 59→60: fold ALTTPR glitch tiers into CanBootsClip, CanFakeFlippers, and CanPearlBypass so OverworldGlitches, HybridMajorGlitches, and MajorGlitches are distinct. Logic-tier coverage entries move; settings_hash / kSettingsCanonicalLen unchanged.
                               // 58→59: expose HybridMajorGlitches and NoLogic in CSV/native settings. NoLogic short-circuits reachability evaluation but leaves can_place confinement live, emits no_logic_seed, and version-locks the new logic values.
                               // 57→58: also pin Kholdstare (Ice Palace) + Trinexx (Turtle Rock) in the boss-shuffle pool (9→7 shuffleable bosses). Like Blind, both depend on their HOME room's ENVIRONMENT — the room "effect" byte ($00AD / header byte 4) + a BG2 ice-block / lava-floor object — which the sprite+gfx+palette redirect can't supply. Playtest: Kholdstare shuffled into Desert Palace spawned with its shell sprite present but un-encased (the room's freeze effect was DP's 0, not Ice Palace's), so the melt-to-expose fight couldn't proceed. Trinexx uses the identical room-object mechanism (Enemizer special-cases both: AddShellAndMoveObjectData + header bytes). Pinning is the clean fix; porting the room environment is enemizer-class + unvalidatable headless. Net shuffleable set = the 7 bosses that work with a pure redirect (Armos, Lanmolas, Moldorm, Helmasaur, Arrghus, Mothula, Vitreous). Changes the boss assignment for every boss_shuffle=true seed, so via OP_CAN_KILL_BOSS the boss-on placement/sphere digests move; boss_shuffle=false stays BYTE-IDENTICAL. Corpus MUST be regenerated. settings_hash / canonical layout unchanged.
                               // 56→57: pin Blind to Thieves' Town in the boss-shuffle pool (10→9 shuffleable bosses). Blind has no boss sprite in its room data — Thieves' Town spawns it via a maiden-follower sequence and the boss only materializes when dung_savegame_state_bits & 0x2000 (set by that TT-only sequence) is true — so a Blind shuffled into any other dungeon never spawns (playtest-confirmed strand). Pinning Blind (like Agahnim 1/2) is the clean fix; making it shuffleable is enemizer-class (synthetic 0xCE spawn + forcing the 0x2000 gate + suppressing the maiden both directions) and is deferred. This changes the boss assignment for every boss_shuffle=true seed (9-permutation + Blind pinned vs the old 10-permutation), so via OP_CAN_KILL_BOSS the placement_digest/sphere_digest move for the boss-on corpus entries; boss_shuffle=false (default + every non-boss entry) stays BYTE-IDENTICAL. Corpus MUST be regenerated. settings_hash / canonical layout unchanged. Runtime-render-only otherwise (the pin is a generation change; the spawn-coord alignment + redirect are runtime).
                               // 55→56: boss-shuffle runtime — boss-kill predicate override. Each dungeon's `- Boss`/`- Prize` location now gates on the new OP_CAN_KILL_BOSS(dungeon) (macro CanKillBoss) instead of the inline CanKill<VanillaBoss> macro, so the kill requirement tracks the SHUFFLED boss (a fire-gated boss shuffled into a fireless-reachable dungeon can no longer strand its prize). With boss_shuffle OFF the per-seed assignment is the vanilla identity, so OP_CAN_KILL_BOSS resolves to the dungeon's vanilla boss-kill predicate — placement is BYTE-IDENTICAL for every boss_shuffle=false seed (verified: only the 8 boss_shuffle=true corpus entries move). The bump version-locks the new op (id 19) + the boss-on placement change so a v55 boss-on slot surfaces the version warning instead of regenerating a different placement. GT's internal miniboss gauntlet + the pinned Agahnim 1/2 keep their direct CanKill<Boss> calls (never shuffled). settings_hash / kSettingsCanonicalLen unchanged (boss_shuffle was already canonical field #23). Corpus MUST be regenerated (bump_rando_corpus.py --apply). NOTE: runtime sprite substitution stays DEACTIVATED (renders garbage pending per-boss GFX + formation-spawn work); this logic is gated-off-safe until that lands.
                               // 54→55: NotADungeonItem fill-rule completeness — SmallKey_EasternPalace (id 54), SmallKey_DesertPalace (id 55) and the Retro GenericKey (id 125) are now banned from the always-reachable Link's Uncle / Secret Passage placement slots, matching ALTTPR's `!(item instanceof Key)` setFillRules. Default-settings placement is BYTE-IDENTICAL (default dungeon_small_keys=Vanilla locks each small key to its own dungeon, and GenericKey is not in the default pool), but this CAN change placement_digest/sphere_digest for the wild-keys (a1-open-fg-wild-keys) and Retro corpus entries (Retro pins genericKeys at kGenVer 54), where those keys are general-pool candidates. The bump version-locks the stricter fill rule so a v54 wild/Retro slot surfaces the version warning instead of silently regenerating a different placement. settings_hash / kSettingsCanonicalLen unchanged (no canonical-settings byte changed). Corpus MUST be regenerated (bump_rando_corpus.py --apply). The companion macros.yaml CanBootsClip/CanPearlBypass change is documentation-only (notes:, ignored by codegen) and does not affect placement.
                               // 53→54: Retro genericKeys (one shared small-key pool — any key opens any door). Completes the Retro flag set the v53 comment noted was still missing. Under world_state==Retro, BuildItemPool substitutes every per-dungeon SmallKey (ids 53-65) with the fungible GenericKey (id 125, ROM 0xAF) — same counts/slots, so pool size is unchanged — and the predicate VM collapses any per-dungeon small-key requirement onto "hold >=1 GenericKey" (a port of ALTTPR ItemCollection::has()'s ShopKey wildcard, app/Support/ItemCollection.php:271-273). Runtime: a single SRAM-persisted shared counter (link_generic_keys = link_keys_earned_per_dungeon[15] = ALTTPR $7EF38B) backs link_num_keys via enter-load/exit-save/door-consume write-through, all gated on Rando_IsGenericKeysActive(). This changes Retro placement_digest + sphere_digest for the 4 Retro corpus entries; Open/Standard/Inverted are byte-identical (every seam is world_state==Retro-gated). settings_hash / kSettingsCanonicalLen unchanged (genericKeys is computed from world_state, no new canonical byte).
                               // 52→53: Retro wildKeys + JSON shops[]. Retro world-state now forces region.wildKeys, moving small keys into the general/wild pool and changing Retro placement/sphere/settings digests. The Retro-only `shops[]` spoiler array also changes race-mode stamps for race+Retro seeds. genericKeys are not included here; keys keep dungeon identity.
                               // 51→52: swordless mode (mode.weapons=swordless) lands end-to-end. New predicate op OP_MODEWEAPONS_EQ (id 18) + swordless predicate branches (Ganon/Agahnim/medallion-casts/Skull-Woods/Kholdstare/Trinexx/tablets, Std+Inv) + a sword-removed item pool, plus the runtime sword-substitution patches (hammer damages Ganon, medallions cast w/o sword, tablets hammer-read, Agahnim/Skull-Woods curtains pre-opened via slot SRAM-init). ALL of it is gated on mode.weapons==swordless / Rando_IsSwordlessActive(), so EVERY existing corpus entry's placement_digest + sphere_digest is byte-identical (swordless branches evaluate false under the default randomized weapons — verified 83/83 unchanged). The bump version-locks the new op + the runtime swordless behavior so a v51 swordless slot can't load under a binary lacking the runtime patches (and the share-string version byte distinguishes the new op space). Corpus regenerated with 0 digest changes to existing entries + one new swordless seed. settings_hash / kSettingsCanonicalLen unchanged (mode_weapons was already canonical field #8; swordless=3 is a pre-declared enum value).
                               // 50→51: ROM-version verification warnings. The spoiler now appends `unverified_tricks_enabled` when tricks or glitch levels use op_registry entries without US 1.0 verification. That changes race-mode stamps for tricks/glitches-on seeds while placement/sphere digests stay unchanged. settings_hash / kSettingsCanonicalLen unchanged.
                               // 49→50: Pyramid Fairy chest model — the two Trade slots (Pyramid Fairy - Sword 210 / Bow 211) are RETIRED from the placement pool (ALTTPR delivers this pond as two chests Left/Right, not a throw-in upgrade; the fork's runtime Sprite_WishPond3 now grants Left/Right directly on contact for BOTH ponds — no toss). Removing two placeable locations shifts every seed's open-location count / junk-pad, so placement_digest + sphere_digest change for all entries → corpus regenerated. settings_hash / kSettingsCanonicalLen are NOT affected (settings struct unchanged). The bump version-locks the smaller pool so a v49-minted slot/seed surfaces the version warning instead of regenerating a different placement.
                               // 48→49: boss + drop shuffle are now LIVE in playable slots — the per-seed assignment is installed at slot load (Rando_ActivateSidecarSlot) so the runtime sprite substitution actually fires, and the native settings window exposes both as (experimental) toggles. The drop shuffle gained a heart-drop floor (changes drop_shuffle=true output) and the spoiler now emits boss_assignments/drop_tables (changes the race-mode stamp for a shuffle-on race seed). Boss/drop shuffle is ORTHOGONAL to item placement, so default-settings AND shuffle-on placement_digest + sphere_digest are byte-identical (corpus regenerated → 0 digest changes; boss/drop determinism is pinned by BossShuffle_SelfCheck/DropShuffle_SelfCheck instead). The bump version-locks the new runtime-drop algorithm + the shuffle-on spoiler stamp so a v48-minted slot/seed surfaces the honest version warning rather than regenerating different drops/stamp.
                               // 47→48: Inverted Floating Island (loc 205) gets an Inverted logic override gating it on region access only (`TRUE()`), matching ALTTPR (Inverted East.php's initalize() never sets its requirements). Previously the codegen "last-wins" merge left the STRICT Standard predicate applied to Inverted, making the Piece of Heart harder/potentially unplaceable. This changes Inverted reachability for that location, so an Inverted seed's placement could route differently and its sphere structure changes. Corpus regenerated (only b-inverted-ganon-7-7's sphere_digest shifted; placement digests stable).
                               // 46→47: fork-extension hint NPCs (Storyteller + Kakariko/Dark-World Fortune Tellers, ids 17-19) add three entries to the spoiler `hints[]` for any hints-on seed. The race-mode anti-tamper stamp is a SHA-256 over the full spoiler JSON, so this changes the stamp bytes for race-mode + hints-on seeds. Without this bump a v46 race seed (minted before the fork NPCs) would regenerate a 3-entry-larger hints[] at reveal → false `kRandoReveal_StampMismatch` ("tampered"). The bump makes the version gate fire first → honest `kRandoReveal_VersionMismatch` ("regenerate the seed"). Placement/sphere digests are unchanged (hints are generated post-placement; generator_version is not an RNG input), so the corpus is byte-identical — verified, not regenerated.
                               // 45→46: ALTTPR three-way accessibility ("beatable only"). The per-tier acceptance gate (Accessibility_SeedAcceptable) changes the semantics of all three accessibility values — `items` now requires every progression item reachable, `locations` every location, and `none`/"beatable only" is now guaranteed beatable (no longer ships unwinnable seeds). Share strings carry the version byte so old `none` seeds don't silently reproduce under the new semantics. Corpus regenerated.

// The share-string binary layout packs version into 1 byte
// (rando_share.h: ShareString.version is uint8). Compile-time enforce
// kGeneratorVersion ≤ 255 so silent truncation can't ship.
// C++ uses the static_assert keyword; C11 uses _Static_assert. rando.h is
// included from the C++ tracker-window TUs, so pick the right spelling.
#ifdef __cplusplus
static_assert(kGeneratorVersion <= 0xFFu,
#else
_Static_assert(kGeneratorVersion <= 0xFFu,
#endif
               "kGeneratorVersion exceeds the share-string uint8 version field; "
               "bump ShareString.version to uint16 and rev the share-string binary layout "
               "before incrementing past 255.");

// ---------------------------------------------------------------------------
// g_assets_hash — SHA-256 of the loaded asset blob (computed once after
// LoadAssets returns; see tasks.md §1.1a). Compared against kVanillaAssetsHash
// from the generated header src/rando/vanilla_assets_hash.h for the
// non-vanilla-asset-warning flow.
// ---------------------------------------------------------------------------
extern uint8 g_assets_hash[32];

// ---------------------------------------------------------------------------
// Rando_OnLocationCheck — the universal grant-site dispatcher
// (tasks.md §6.1; randomizer-placement spec).
//
// Called from every registered vanilla grant site with:
//   - location_id : ALTTPR canonical numeric id (from location_registry.yaml)
//   - vanilla_item_id : the item the vanilla game would have granted
//
// Returns the item_id to actually grant. On an unknown location_id (e.g., a
// rando slot from a future binary that has more locations than this one
// knows), the dispatcher returns vanilla_item_id unchanged so the vanilla
// grant path runs.
//
// Wrapped at each call site by:
//   if (enhanced_features1 & kFeatures1_RandomizerActive) {
//     item = Rando_OnLocationCheck(<location_id_const>, item);
//   }
// ---------------------------------------------------------------------------
uint16 Rando_OnLocationCheck(uint16 location_id, uint16 vanilla_item_id);

// ---------------------------------------------------------------------------
// Rando_DispatchVanillaGrant — convenience for §6 grant-site wrappers that
// route through the existing Link_ReceiveItem(uint8 lttp_code, ...) path.
//
// At every grant site that grants a vanilla LttP item code, wrap the call:
//
//     uint8 lttp_code = 0x16;  // BottleEmpty
//     if (enhanced_features1 & kFeatures1_RandomizerActive) {
//       lttp_code = Rando_DispatchVanillaGrant(LOC_<X>, ITEM_<vanilla>, lttp_code);
//     }
//     Link_ReceiveItem(lttp_code, 0);
//
// `vanilla_registry_id` is the rando registry id of the vanilla item this
// site would have granted (looked up via item_ids.h's ITEM_<Name>).
// `vanilla_lttp_code` is the LttP receive-item code the site would have
// passed to Link_ReceiveItem in vanilla play (e.g. 0x16 for a bottle).
//
// Returns the LttP code to actually grant. If the placed item has no
// vanilla LttP dispatch (progressive / dungeon-item / prize / virtual),
// returns `vanilla_lttp_code` unchanged — those item classes need
// per-class handlers (§6.2 work) that the universal dispatcher cannot
// emit through the existing receive-item path.
//
// §6.2 sentinel return: `kRandoLttpSkip` (0xFE) means "the rando subsystem
// has already granted the placed item via a direct write; caller MUST NOT
// invoke Link_ReceiveItem". This signals to the caller that all bookkeeping
// is done. Use the convenience wrapper `Rando_ShouldSkipReceive(code)` to
// test the return value.
// ---------------------------------------------------------------------------
#define kRandoLttpSkip 0xFEu
uint8 Rando_DispatchVanillaGrant(uint16 location_id,
                                 uint16 vanilla_registry_id,
                                 uint8 vanilla_lttp_code);

// Runtime-only message id for trap pickups. messaging.c asks the randomizer to
// render this id before falling back to the vanilla dialogue table, so no asset
// text row is required.
#define kRandoTrapDialogueId 0x0220u
bool Rando_RenderTrapMessage(uint16 msg_id, uint8 *out_buffer);

// Returns the item id resolved by the most recent Rando_DispatchVanillaGrant
// (or Rando_ChestDispatch) call. Used by direct-grant
// confirmation sites to feed the per-item icon lookup. Returns 0xFFFF when
// no dispatch has run yet this slot.
uint16 Rando_LastDispatchedItemId(void);

// ---------------------------------------------------------------------------
// Race-mode reveal action.
//
// Rando_RevealSpoiler reads a race-mode suppressed spoiler at the given path
// (or, if `suppressed_path == NULL`, derives the path from `share_string`
// via Spoiler_ResolvePath), verifies its integrity, reproduces the placement
// deterministically from the embedded settings + seed (decoded from the
// share string), confirms the stamp matches the regenerated spoiler, and
// writes the full JSON + .txt spoiler back to disk (overwriting the
// suppressed binary).
//
// The reveal flow is also the verification flow: stamp mismatch means the
// suppressed file no longer corresponds to the current generator's output
// for this (settings, seed_u64) pair — either the generator changed or the
// file was tampered with.
// ---------------------------------------------------------------------------
typedef enum RandoRevealResult {
  kRandoReveal_Ok = 0,
  kRandoReveal_FileNotFound = 1,
  kRandoReveal_ParseError = 2,
  kRandoReveal_CrcMismatch = 3,
  kRandoReveal_ShareStringMismatch = 4,
  kRandoReveal_VersionMismatch = 5,
  kRandoReveal_StampMismatch = 6,
  kRandoReveal_PlacementFailed = 7,
  kRandoReveal_SettingsCorrupt = 8,
  kRandoReveal_WriteFailed = 9,
} RandoRevealResult;

// Reveal the suppressed spoiler at `suppressed_path`. If `expected_share_string`
// is non-NULL, the reveal also verifies the file's embedded share string
// matches. Caller may pass either (the file path explicitly) OR (an
// expected share string only; path resolved via Spoiler_ResolvePath).
RandoRevealResult Rando_RevealSpoiler(const char *suppressed_path,
                                      const char *expected_share_string);

// Player-facing one-line description for a reveal result, suitable for
// surfacing in the file-select dialog or the CLI's stderr line. Never NULL.
const char *Rando_RevealResultDescription(RandoRevealResult r);

// §62 — in-binary reveal action. Reveals the active slot's suppressed
// spoiler (race-mode `.json` containing the ZRSR header). Returns the
// reveal result enum; emits a one-line log to stderr describing the
// outcome. Returns kRandoReveal_FileNotFound when no slot is active or
// the slot's share string was never captured (no race-mode spoiler to
// reveal).
RandoRevealResult Rando_RevealActiveSlotSpoiler(void);

// True when an in-binary reveal of the active slot's spoiler is allowed right
// now: a race-mode slot is active AND the anti-cheat completion gate inside
// Rando_RevealActiveSlotSpoiler() would currently pass (seed completed). UI uses
// this to enable the Reveal button with a clear "after you finish the seed"
// state instead of surfacing a confusing FileNotFound when the gate refuses
// pre-completion. (Tournament admins bypass the gate via --reveal-spoiler.)
bool Rando_CanRevealActiveSlotSpoiler(void);

// Per-frame tick (main loop) that latches "this slot's seed was beaten" so the
// reveal survives leaving the credits screen. See rando.c.
void Rando_NoteFrameForReveal(void);

// Per-frame trap effect tick. Runs before Module_MainRouting so trap freeze can
// neutralize movement/input before the active player handler consumes them.
void Rando_TickTrapEffects(void);
// add-rando-trap-catalog — placement-time location-compatibility flags passed to
// Rando_PickTrapEffectId so context-locked effects (Cucco needs the overworld,
// Darkness needs a dungeon) are only ever placed where they will actually fire.
// Derived from committed location data (region dungeon_id + location type).
enum {
  kTrapLoc_IsDungeon = 1u << 0,  // the location's region is a dungeon
  kTrapLoc_IsOutdoor = 1u << 1,  // the location is an overworld free-standing type
};
// Placement-time trap effect selector (defined in rando.c, called from
// rando_placement.c). Deterministic in (seed, location_id) and the enabled-category
// mask; returns an ITEM_Trap* id compatible with loc_flags.
uint16 Rando_PickTrapEffectId(uint64 seed, uint16 location_id, uint8 categories,
                              uint8 loc_flags);

// Returns true if `lttp_code` is the §6.2 "skip Link_ReceiveItem" sentinel.
// Enabled for HalfMagic/QuarterMagic/TriforcePiece/prize-bit items, which
// dispatch via direct writes inside Rando_DispatchVanillaGrant.
static inline int Rando_ShouldSkipReceive(uint8 lttp_code) {
  return lttp_code == kRandoLttpSkip;
}

// ---------------------------------------------------------------------------
// Rando_ShowDirectGrantConfirmation — generic visual+audio confirmation
// for direct-grant placements.
//
// When Rando_DispatchVanillaGrant returns kRandoLttpSkip the caller skips
// Link_ReceiveItem entirely — no animation, no sound. For sites that have
// no other confirmation visual (the §6.5 tablets, several §6.4 NPCs, the
// §6.7 Pyramid Fairy item drop), the player can't tell that anything was
// granted. Callers should invoke this immediately in the skip branch:
//
//   if (Rando_ShouldSkipReceive(lttp_code))
//     Rando_ShowDirectGrantConfirmation(placed_item_id);
//   else
//     Link_ReceiveItem(lttp_code, 0);
//
// where `placed_item_id` is the ITEM_* id of the item that was actually
// direct-written by the dispatcher (resolve via Placement_Lookup or via the
// vanilla item id when no rando swap occurred).
//
// Plays the standard item-receipt sound effect and refreshes the HUD so
// any visible inventory change (prize icons, dungeon-item bits, Triforce
// counter) updates immediately. Trap items get a deterministic good-item
// decoy icon based on the active seed/location/trap type; otherwise, when
// `item_id` maps to a non-zero gfx bundle in `kDirectGrantIcons[]`
// (codegen'd from `assets/rando/direct_grant_icons.yaml`), this additionally
// spawns the `kAncillaType_RandoIconReceipt` ancilla so the player sees what
// they got. Audio-only (gfx==0) entries fall back to the audio+HUD path.
// ---------------------------------------------------------------------------
void Rando_ShowDirectGrantConfirmation(uint8 item_id);

// ---------------------------------------------------------------------------
// Rando_ReceiveOrConfirm — convenience wrapper that combines the standard
// NPC grant pattern into a single call.
//
// Replaces:
//   if (Rando_ShouldSkipReceive(lttp_code))
//     Rando_ShowDirectGrantConfirmation(item_id);
//   else
//     Link_ReceiveItem(lttp_code, 0);
//
// with:
//   Rando_ReceiveOrConfirm(lttp_code, item_id);
//
// Behavior: when `lttp_code` is the §6.2 skip-sentinel, fires the §7.6
// confirmation cue (sound + HUD refresh + icon ancilla when `item_id` is a
// trap or has a verified entry in `kDirectGrantIcons[]`). Otherwise invokes
// Link_ReceiveItem with chest_position=0 — every existing call site at the
// NPC dispatch pattern passes 0; sites that need non-zero chest_position
// (chest opens) continue to call Link_ReceiveItem directly with an explicit
// confirmation gate.
//
// `item_id` is the ITEM_* id of the placed item (Placement_Lookup result).
// When the dispatcher returns a vanilla lttp_code (no swap), `item_id` is
// unused; pass the vanilla ITEM_* for symmetry and forward-compat.
// ---------------------------------------------------------------------------
void Rando_ReceiveOrConfirm(uint8 lttp_code, uint8 item_id);

// ---------------------------------------------------------------------------
// add-rando-pot-sanity Phase 4 — runtime pot grant hook + recolor (design D3/D4).
//
// Rando_PotBreakHook runs at the TOP of RevealPotItem (dungeon.c) for all three
// callers (lift / ThievesAttic-hole / sword-break). On an active + un-checked pot
// it grants the placed item here (+ fires the receive/confirm cue); on a CHECKED
// key/empty pot it signals suppression (no duplicate-key re-drop). The return
// tells RevealPotItem whether to suppress its vanilla secret. Rando_PotShouldRecolor
// gates the un-checked-pot palette swap in RoomDraw_SinglePot. Both are inert
// (Vanilla / false) when rando is off or (room,pos4) is not a registered,
// tier-active pot — so the non-rando draw/grant path stays byte-identical.
// ---------------------------------------------------------------------------
enum {
  kRandoPot_Vanilla = 0,   // let RevealPotItem run its normal vanilla path
  kRandoPot_Suppress = 1,  // hook handled it; RevealPotItem must spawn no secret
};
// Alternate CGRAM sub-palette row (0..7) for un-checked in-scope pots. Vanilla
// pots use row 3. INTERIM / theme-fragile: BG sub-palette rows are loaded with
// per-room object colors, so no single row is "distinct but sane" in every
// dungeon — row 5 rendered a bright-green pot in a LightWorld_NW cave (F12 dump
// 2026-06-20). Row 2 is the current best-effort (muted browns, no saturated
// accent in that dump). The robust fix is a palette-independent overlay marker
// (planned), which supersedes this. Tunable here without touching the draw.
#define kRandoPotAltPalette 2u
// NOTE: the first param is named `room` (not `dungeon_room_index`) — the latter
// is a variables.h g_ram-accessor MACRO and can't be a parameter name. Callers
// pass the global `dungeon_room_index` value as the argument.
uint8 Rando_PotBreakHook(uint16 room, uint16 pos4);
bool Rando_PotShouldRecolor(uint16 room, uint16 pos4);
// Exposed for the --rando-selftest (room,pos4) -> LOC round-trip assertion.
uint16 Rando_GetPotLocation(uint16 room, uint16 pos4);

// ---------------------------------------------------------------------------
// Field item sprites — draw the PLACED item's
// graphics at free-standing item locations instead of the vanilla sprite.
//
// The gfx come from the existing per-item receipt decompressor
// (DecodeAnimatedSpriteTile_variable) writing the shared receive-item VRAM slot
// (chars 0x24/0x34); the item->gfx mapping reuses kDirectGrantIcons[]. The draw
// helper loads the slot on demand (cached via g_recv_item_slot_owner). Because
// that slot holds ONE item at a time, only one field item per screen renders its
// real gfx — a second standing item on the same screen shows this one's
// (documented limitation; phase 2 = a dedicated slot).
//
// Split across TUs: the resolver (placement + icon table) lives in rando.c; the
// gfx-load + OAM draw live in sprite.c (the OAM/gfx primitives are there).
// ---------------------------------------------------------------------------

// True when field-item sprites should be drawn: active rando slot AND the
// client-local field_item_sprites toggle (read live). Inert in non-rando play.
bool Rando_FieldItemSpritesActive(void);

// Resolve a free-standing location to a drawable placed-item icon. Returns
// false (draw the vanilla sprite) when field-item sprites are inactive, the
// placement equals the vanilla item, or the placed item has no receive gfx
// (the audio-only sentinel). On true, fills the kDirectGrantIcons fields.
bool Rando_GetFieldItemIcon(uint16 location_id, uint16 vanilla_item_id,
                            uint8 *out_gfx, uint8 *out_big, uint8 *out_oam_flags);

// DRAW (call each frame in place of the vanilla sprite draw): draw the placed
// item's icon (chars 0x24/0x34) at sprite `k`'s position, loading its gfx into
// the shared slot on demand. Returns true when it drew (caller skips the vanilla
// draw); false when the vanilla sprite should be drawn instead.
bool Rando_TryDrawFieldItemSprite(int k, uint16 location_id, uint16 vanilla_item_id);

// As above, but draw the icon at a fixed screen offset (dx,dy) from sprite `k` —
// for an NPC whose vanilla pose holds an item out (the Hobo's bottle). The caller
// still draws the NPC's body; this only adds the held icon.
bool Rando_TryDrawHeldItemSprite(int k, uint16 location_id, uint16 vanilla_item_id,
                                 int dx, int dy);

// Draw a trap-cucco (sprite 0x0B with the trap signal byte set) as a single 16x16
// large OAM entry from the shared recv-item slot with the custom cucco tile +
// palette, so the flock renders in ANY area. Mirrors the vanilla large draw
// (Sprite_PrepAndDrawSingleLargeNoPrep) but with a fixed charnum/palette. Vanilla
// and home-area cuccos keep the sheet-3 draw. Implemented in sprite.c.
void Rando_DrawTrapCucco(int k);

// Load `gfx` into the shared receive-item VRAM slot (chars 0x24/0x34) unless it
// already holds it (g_recv_item_slot_owner cache; handles the sword/shield
// decompress side-loads). For draw sites that render the slot themselves —
// e.g. the Flute Spot dig ancilla. Implemented in sprite.c.
void Rando_EnsureRecvItemSlotGfx(uint8 gfx);

// ---------------------------------------------------------------------------
// Custom item art — gfx ids with the 0x80
// bit set are NOT vanilla DecodeAnimatedSpriteTile_variable bundle indices
// (vanilla tops out around 0x4B); they select custom art + palette for
// the ALTTPR items that have no vanilla receive bundle. The tile comes from
// the kRandoCustomItemGfx asset (or a re-coloured vanilla bundle) and an
// 8-colour palette is loaded into sprite palette 3's upper half (CGRAM words
// 0xB8..0xBF) — byte-for-byte the slot ALTTPR's LoadItemPalette uses — so the
// colour is stable regardless of the area's sprite palettes. Entries draw
// with OAM palette 3 (oam_flags 0x36). Routed exclusively through
// Rando_EnsureRecvItemSlotGfx; never pass these ids to
// DecodeAnimatedSpriteTile_variable (it asserts).
// ---------------------------------------------------------------------------
// Blob-backed ids are CONTIGUOUS from 0x80 (id 0x80+N = kRandoCustomItemGfx
// entry N — keep in lockstep with assets/rando/custom_item_gfx.png via
// assets/scripts/gen_custom_item_gfx_png.py); ids that re-colour a vanilla
// bundle instead of using the blob (Rupoor) come AFTER all blob entries.
enum {
  // kRandoCustomItemGfx entry 0 (16x16 triforce) + green_blue_guard upper-half
  // palette (kPalette_MainSpr[52..59] — what ALTTPR's
  // PalettesVanilla_green_blue_guard+$0E points at).
  kRandoCustomGfx_TriforcePiece = 0x80,
  // Entries 1/2: the 1/2 and 1/4 Magic decanters (16x16, same green_blue_guard
  // palette — z3r SpriteProps $4E/$4F).
  kRandoCustomGfx_HalfMagic = 0x81,
  kRandoCustomGfx_QuarterMagic = 0x82,
  // Vanilla green-rupee tile (bundle 0x24) + ALTTPR's off_black palette: the
  // Rupoor has NO custom tile upstream either (z3r itemdatatables.asm $59).
  kRandoCustomGfx_Rupoor = 0x83,
  // Cucco trap avenger (add-rando-trap-catalog): a dispatch id, NOT a blob cell —
  // the real cucco tile + palette load at runtime from VANILLA sheet 80 (the cucco
  // ALSO uses there) in the cucco branches of Rando_EnsureRecvItemSlotGfx /
  // Rando_ApplyCustomItemGfxPalette, drawn by Rando_DrawTrapCucco. So the flock
  // renders in EVERY area, not just the two that load that sheet into sprite slot 3.
  kRandoCustomGfx_Cucco = 0x84,
  kRandoCustomGfx_BlobEntries = 3,  // kRandoCustomItemGfx cell count (idx 0..2)
};

// (Re)load the custom item's 8-colour palette into SP3's upper half if it is
// not already there (compares the AUX buffer so it doesn't fight a palette
// fade). Called per draw — area/room transitions reload SP1-4 over the slot
// and a sprite that stays on screen must repaint it. Implemented in sprite.c.
void Rando_ApplyCustomItemGfxPalette(uint8 gfx);

// ---------------------------------------------------------------------------
// Rando_ChestDispatch — universal chest grant-site hook.
//
// Hooked at Link_PerformOpenChest, AFTER OpenChestForItem returns the item
// to give. Maps (dungeon_room_index, chest_ordinal) to an ALTTPR location_id
// via a generated lookup table, then invokes Rando_DispatchVanillaGrant.
//
// `dungeon_room` is the global dungeon room index (matches ALTTPR's room id).
// `chest_ordinal` is the 0-based index of this chest within the room (0..5).
// `vanilla_lttp_code` is the LttP receive-item code OpenChestForItem returned.
//
// Returns the LttP code to grant. When the (room, ordinal) pair isn't in the
// lookup table (universal hook covers chests we haven't enumerated yet), the
// vanilla code is returned — the chest grants its vanilla item, as if rando
// were inactive for that specific chest.
//
// Lookup table (src/rando/chest_lookup.h) is generated by rando_logic_gen.py
// from the chest_table.gen.bin artifact, cross-referenced with ALTTPR names +
// location_registry.yaml. 164 entries; the 165th ("Chest Game") is the
// minigame path handled at §6.8.
// ---------------------------------------------------------------------------
uint8 Rando_ChestDispatch(uint16 dungeon_room, uint8 chest_ordinal,
                          uint8 vanilla_lttp_code);

// ---------------------------------------------------------------------------
// Rando_ShopDispatch — Retro-world-state shop-purchase grant hook (#53).
//
// Hooked at ShopItem_HandleReceipt (src/sprite_main.c), the universal
// shop-item grant point. Maps a shop purchase to an ALTTPR shop-slot
// location_id via the (room, entrance-door, slot-position) disambiguation
// contract, then routes through Rando_DispatchVanillaGrant.
//
// Disambiguation mirrors ALTTPR's SpritePrep_ShopKeeper (z3randomizer
// shopkeeper.asm): vanilla LttP reuses one physical shop room for several
// overworld entrances (e.g. low-byte room 0x0F backs the DW Potion,
// Lumberjack, Outcasts, and Lake-Hylia shops; room 0x12 backs both the DW
// Death-Mountain and LW Lake-Hylia shops). Room alone is ambiguous, so the
// entrance door (`which_entrance`, g_ram+0x10E — ALTTPR's
// PreviousOverworldDoor) selects the specific shop, and `pos` (0..2) selects
// the slot within it.
//
// `room` is BYTE(dungeon_room_index); `entrance` is `which_entrance`;
// `pos` is the 0-based slot index the shopkeeper spawned the item at.
// `vanilla_lttp_code` is the LttP receive code the shop would grant in vanilla.
//
// Returns the LttP code to grant. When (room, entrance, pos) isn't a known
// shop slot — every non-shop ShopItem_HandleReceipt caller (gift thief,
// bomb shop) and every non-Retro seed (shop slots absent from the placement
// table) — the vanilla code is returned unchanged, so behavior is identical
// to vanilla. May return kRandoLttpSkip (test with Rando_ShouldSkipReceive)
// for direct-grant placements.
// ---------------------------------------------------------------------------
uint8 Rando_ShopDispatch(uint8 room, uint8 entrance, uint8 pos,
                         uint8 vanilla_lttp_code);

// ---------------------------------------------------------------------------
// Retro TakeAny caves.
//
// ALTTPR converts 5-of-31 ordinary overworld caves into "take-any" caves per
// seed (an old man offers a free item; take ONE of two and the cave locks).
// The generator pins the active caves' rewards into the placement table
// (LOC ids 266..327 = 31 caves x 2 slots; only ~9 active). The runtime:
//   1. Overworld_UseEntrance redirects an active cave's overworld door to its
//      take-any host room and captures the door id in g_rando_takeany_door_id.
//   2. SpritePrep_Shopkeeper presents the live slots and suppresses the host
//      room's regular shop (the host rooms ARE regular shops — collision).
//   3. ShopItem_TakeAny grants the taken item free and locks the cave.
// ---------------------------------------------------------------------------

// Set by Overworld_UseEntrance when the player steps into an active take-any
// cave (= ALTTPR PreviousOverworldDoor = overworld row-index lx + 1). 0 means
// "this entrance is not an active take-any" — read by the host-room shopkeeper
// prep/dispatch to disambiguate from a normal visit to the shared host room.
// Transient (per cave visit), reset on every overworld entrance.
extern uint8 g_rando_takeany_door_id;

// Dungeon entrance-shuffle coupling. The overworld entry hook
// sets g_rando_entrance_exit_room (via Rando_EntranceCoupledExitRoom) when a
// shuffled dungeon door is entered; the dungeon-exit room-keyed search uses it so
// the player returns to the SOURCE door. 0 = no override (caves auto-couple).
extern uint16 g_rando_entrance_exit_room;
uint16 Rando_EntranceCoupledExitRoom(uint16 lx);
// Dungeon decoupled (Insanity): the one-way exit-search target room for door-slot
// lx, keyed on the LOADED dungeon (overlay) so the exit emerges at net'[loaded]'s
// door instead of the source. 0 when inactive / not a pooled dungeon / self-map
// (then the coupled return-to-source applies). Overrides the coupled room at entry.
uint16 Rando_EntranceDungeonDecoupledExitRoom(uint16 lx);
// Cross-category (Stage 3): set at the entry hook for a cave→dungeon redirect so
// the dungeon exit uses the cached source-cave position. Consumed at the exit.
extern uint8 g_rando_entrance_force_cached;
// Source cave's room for the force-cached exit, set together with the flag by
// Rando_EntranceForceCachedExit; consumed at the exit to restore the Y-adjust.
extern uint16 g_rando_force_cached_room;
bool Rando_EntranceForceCachedExit(uint16 lx);

// Inverted spawn-select respawn redirect.
// Set by Module1B_SpawnSelect when an Inverted slot commits a respawn-menu choice
// ("@'s House" / "Dark Chapel" / "Dark Mountain"); consumed by the next
// LoadOverworldFromDungeon, which forces the spawn-anchor's overworld exit into
// the Dark World (screen |= 0x40). Runtime-gated rather than an asset override so
// the Sanctuary / Mountain-Cave *checks* (same rooms, but entered from the
// overworld, not the menu) still exit to the Light World unchanged. 0 = inactive.
extern uint8 g_rando_inverted_spawn_redirect;

// For an active Inverted slot, rename the
// post-Agahnim spawn-select options "Sanctuary" -> "Dark Chapel" and "The Mountain
// Cave" -> "Dark Mountain" (ALTTPR labels). Called on the FINISHED character
// buffer (after Text_LoadCharacterBuffer's vanilla decode) for dialogue 0x184 /
// 0x185 — swaps the location word's font-byte run in place. No-op for any other
// id / non-Inverted slot, so vanilla / Open / Standard / Retro are byte-identical.
void Rando_RewriteInvertedSpawnMenu(uint16 msg_id, uint8 *buf);

// Decoupled / Insanity (Stage 4, D.4) runtime hooks. SetEnteredDoor: at the
// overworld entry hook, record the entered cave door. CaptureArrival: after
// Dungeon_LoadEntrance caches *_exit, snapshot that door's overworld arrival.
// ReplaceArrival: at the cave-class exit (before LoadCachedEntranceProperties),
// swap in net[entered]'s captured arrival so Link emerges at a DIFFERENT door;
// returns false (→ coupled return) when inactive / target uncaptured.
void Rando_DecoupledSetEnteredDoor(uint16 lx);
// Read+clear the entered-interior global (consume-once at LoadOverworldFromDungeon top).
uint16 Rando_DecoupledConsumeEntered(void);
bool Rando_DecoupledReplaceArrival(uint16 entered);

// Cross + decoupled runtime: SetExit resolves the source door's one-way exit
// target (cave or dungeon) at the entry hook. ConsumeExit reads+clears the stashed
// kind (0 none / 1 cave / 2 dungeon) at the LoadOverworldFromDungeon top, writing
// the target cave interior to *out_cave. ReplayCave emerges at that cave's arrival.
void Rando_CrossDecoupledSetExit(uint16 lx);
uint8 Rando_CrossDecoupledConsumeExit(uint8 *out_cave);
bool Rando_CrossDecoupledReplayCave(uint8 target_cave);
// D.3 capture-for-bake (dev opt-in via ZELDA3_CAPTURE_ARRIVALS): snapshot each
// entered cave's overworld arrival, keyed by the entered cave interior.
// RecordEnteredDoorForCapture runs at the walk-in entry hook; RecordEnteredFallhole
// at the genuine fall-hole path (entrance-ids 0x76-0x81; also sets the decoupled
// exit key so falling in emerges decoupled). NOTE: heart_piece_cave_3 is a normal
// WALK-IN cave (dark screen 0x22), not a drop cave — it is NOT in the fall-hole table.
void Rando_RecordEnteredDoorForCapture(uint16 lx);
void Rando_RecordEnteredFallhole(void);
void Rando_CaptureArrivalForBake(void);

// If the cave at overworld row-index `lx` (door_id = lx+1) is an ACTIVE
// take-any this seed (rando active + Retro + its slot-0 LOC is in the placement
// table), return its host-room entrance (0x58/0x60/0x46). Else 0.
uint8 Rando_TakeAnyHostByDoorIndex(uint8 lx);

// For host-room presentation: the take-any LOC id for (door_id, slot pos) if it
// is active AND not yet collected (should be presented), else 0xFFFF. `room`
// (BYTE(dungeon_room_index)) is a sanity cross-check against the host room.
uint16 Rando_TakeAnyLiveSlot(uint8 room, uint8 door_id, uint8 pos);

// On taking a take-any item at (door_id, pos): grant the placed item and LOCK
// the whole cave (mark every active slot LOC checked, matching the asm
// ShopState|=$07). Returns the LttP code (caller drives Rando_ReceiveOrConfirm).
uint8 Rando_TakeAnyDispatch(uint8 room, uint8 door_id, uint8 pos,
                            uint8 vanilla_lttp_code);

// Icon kind (shop-item subtype2) for a take-any slot's placed item: 14 = heart,
// 15 = potion (generic). Both tiles are present in every host room's shop GFX.
uint8 Rando_TakeAnyDrawKind(uint8 door_id, uint8 pos);

// ---------------------------------------------------------------------------
// Rando_BumpReachabilityCounter — invalidates the tracker's memoized
// reachability cache when a story-progress event flag is written
// (tasks.md §0.4a). Called from every reachability-affecting write site.
// ---------------------------------------------------------------------------
void Rando_BumpReachabilityCounter(void);
uint32 Rando_GetReachabilityCounter(void);

// ---------------------------------------------------------------------------
// Tracker overlay toggle state. In-memory only, not
// persisted. Both default to false at process start AND on every
// Rando_DeactivateSlot (so launching/loading defaults to hidden).
// Keybindings: kKeys_RandoToggleItemTracker, kKeys_RandoToggleLocationTracker.
// ---------------------------------------------------------------------------
extern bool g_rando_show_item_tracker;
extern bool g_rando_show_location_tracker;

// ---------------------------------------------------------------------------
// Checked-location bitmap (one bit per location_id, 0..kRandoLocationCapacity-1).
// Set when Rando_OnLocationCheck fires for a location, OR when a reachability
// event-flag bump site updates Rando_BumpReachabilityCounter AND has a
// corresponding LOC_*.
//
// Bitmap is heap-resident (NOT in g_ram). Loaded from
// RandoSidecarSlot.checked_bitmap on activate; written back on sidecar save.
// Sized by kRandoLocationCapacity (rando_logic.h) so it tracks the registry.
// ---------------------------------------------------------------------------
#define kRandoCheckedBitmapBytes ((kRandoLocationCapacity + 7) >> 3)
extern uint8 g_rando_checked_bitmap[kRandoCheckedBitmapBytes];

// Set the bit for `location_id`. No-op if loc_id >= kRandoLocationCapacity or
// rando not active.
void Rando_MarkLocationChecked(uint16 location_id);
// Test the bit for `location_id`. Returns false for OOB or no slot active.
bool Rando_IsLocationChecked(uint16 location_id);

// Rando Mushroom/Powder decouple. The two share link_item_mushroom (0xF344):
// 1=mushroom, 2=powder. Vanilla never holds both (the Witch trade consumes the
// Mushroom to make Powder), but rando shuffles them as INDEPENDENT items, so we
// track the decoupled ownership here:
//   * bit 0 (Held)        — an undelivered Mushroom is in inventory. The Witch
//     trade keys off THIS, not the byte, so a Powder-first pickup can't lock out
//     the Potion Shop check (the byte may show Powder=2 the whole time).
//   * bit 1 (PowderOwned) — Magic Powder has been obtained. Lets the item-menu
//     swap show the Mushroom icon (byte=1) without logic/tracking reading the
//     player as having lost Powder.
// Persisted via RandoSlotHeader.mushroom_held.
enum {
  kRandoMushroom_Held        = 0x01,  // undelivered Mushroom in inventory
  kRandoMushroom_PowderOwned = 0x02,  // Magic Powder obtained
};
extern uint8 g_rando_mushroom_held;
// True iff a rando slot is active and the player holds an undelivered Mushroom.
bool Rando_MushroomHeld(void);
// Clear the Held bit — call when the Witch accepts the Mushroom. Leaves the
// PowderOwned bit intact (delivering the Mushroom doesn't surrender Powder).
void Rando_DeliverMushroom(void);
// True iff the player has Magic Powder regardless of which icon the shared slot
// currently shows: Powder in the byte (==2) counts in vanilla and rando; under
// an active rando slot the PowderOwned bit also counts, so a player who swapped
// the slot to the Mushroom icon still reads as Powder-capable. Single source of
// truth for "owns Powder"; mirrors Rando_MushroomHeld for the other tier. In
// vanilla this is exactly `link_item_mushroom == 2`.
bool Rando_PowderOwned(void);
// True iff a rando slot is active and the player owns BOTH an undelivered
// Mushroom and Powder, so the shared Y-slot icon can be toggled in the item
// menu. Cosmetic: the Witch trade and Powder use both already work off the
// decoupled state regardless of which icon is currently selected.
bool Rando_MushroomPowderCanToggle(void);

// Flute/shovel decouple (old-style single inventory slot).
//
// Vanilla packs the shovel and flute into one byte, link_item_flute (0xF34C):
// 1=shovel, 2=flute (inactive), 3=flute (activated). Rando shuffles the flute
// and the shovel as INDEPENDENT items, but that byte can't represent owning
// both — so acquiring the second of the pair used to overwrite (and lose) the
// first, softlocking seeds that require both (e.g. Shovel to dig "Flute Spot"
// AND the flute to fly). We track true ownership in g_rando_flute_shovel_owned
// and treat link_item_flute purely as the currently-SELECTED function, which
// the player swaps in the item menu (see Hud_NormalMenu). Mirrors ALTTPR's
// itemdowngrade.asm / inventory.asm model. Persisted via
// RandoSlotHeader.flute_shovel_owned.
enum {
  kRandoFluteShovel_Shovel      = 0x01,  // shovel obtained
  kRandoFluteShovel_Flute       = 0x02,  // flute obtained
  kRandoFluteShovel_FluteActive = 0x04,  // flute can summon the bird (selects level 3)
};
extern uint8 g_rando_flute_shovel_owned;
// Record a shovel/flute pickup: set the ownership bit and raise link_item_flute
// (the selected function) to this item's level without ever downgrading, so the
// shovel can't drop the slot below an owned flute. `lttp_code` is the vanilla
// receive code (0x13 shovel, 0x14 flute, 0x4a active flute). When the active
// seed's instant_flute setting is on, 0x14 is promoted to active immediately.
// Call only when rando is active, from the receive path in misc.c.
void Rando_GrantFluteShovel(uint8 lttp_code);
// True iff a rando slot is active and the player owns BOTH a flute and the
// shovel, so the single Y-slot's function can be toggled in the item menu.
bool Rando_FluteShovelCanToggle(void);
// Effective flute/shovel level for OWNERSHIP tests (0 none, 1 shovel, 2 flute,
// 3 active flute): the highest function the player actually owns, regardless of
// which one is currently selected in link_item_flute. Use this — not the raw
// link_item_flute byte — wherever a sprite/NPC asks "does the player have the
// flute" (vs. "is the flute the selected Y item"), so toggling to the shovel
// doesn't make the player look flute-less. Returns the raw byte verbatim when
// no rando slot is active, so the vanilla side-by-side path is byte-identical.
uint8 Rando_FluteShovelEffectiveLevel(void);

// Boomerang / bow decouple + item-menu swap.
//
// Like the flute/shovel pair, these are independent shuffled items that vanilla
// packs into a single Y-slot byte:
//   * Boomerang (link_item_boomerang, 0xF341): 1 = blue, 2 = magical/red.
//   * Bow       (link_item_bow,       0xF340): 1/2 = wood (no-arrows/arrows),
//                                              3/4 = silver (no-arrows/arrows).
// Both are tracked with persistent ownership bits, but they grant differently:
//   * Boomerang is STRICTLY PROGRESSIVE — the 1st collected is always blue, the
//     2nd always red, regardless of which item is placed there (item color is
//     ignored). Safe because no logic predicate requires a boomerang.
//   * Bow is NEVER-DOWNGRADE — each item grants its own tier (wood item → wood,
//     silver arrows → silver) and a lower pickup can never lower the slot. (Bow
//     IS distinguished by logic — silver vs wood — so its item identity is kept.)
// In both cases the byte tracks the currently-SELECTED tier; once the player
// owns BOTH tiers, pressing A on the highlighted slot (Hud_NormalMenu) swaps
// which tier the slot performs — mirroring ALTTPR's item-menu swap (and the
// existing flute/shovel toggle). Ownership is persisted via
// RandoSlotHeader.boomerang_owned / .bow_owned so a swapped-down byte can't lose
// the higher tier across save/reload.
enum {
  kRandoBoomerang_Blue = 0x01,  // blue boomerang obtained
  kRandoBoomerang_Red  = 0x02,  // magical (red) boomerang obtained
};
extern uint8 g_rando_boomerang_owned;
enum {
  kRandoBow_Wood   = 0x01,  // a bow obtained (wood-arrow capability)
  kRandoBow_Silver = 0x02,  // silver arrows obtained (implies a bow)
};
extern uint8 g_rando_bow_owned;
// Record a boomerang pickup (progressive: advances to the next unowned tier —
// 1st collected = blue, 2nd = red — regardless of which boomerang item it is).
// Call only when rando is active, from the receive path in misc.c.
void Rando_GrantBoomerang(void);
// Record a bow pickup (lttp_code: 0x0b/0x3a = wood bow, 0x3b = silver arrows):
// set the ownership bit(s) and raise link_item_bow's strength tier without
// downgrading, preserving the current arrow bit. Call only when rando is active.
void Rando_GrantBow(uint8 lttp_code);
// True iff a rando slot is active and the player owns BOTH boomerang colors, so
// the Y-slot can be toggled between blue and red in the item menu.
bool Rando_BoomerangCanToggle(void);
// True iff a rando slot is active and the player owns silver arrows (which imply
// a bow), so the Y-slot can be toggled between wood and silver arrows.
bool Rando_BowCanToggle(void);

// Inverted runtime — the active slot's world_state (WorldState enum),
// captured at Rando_ActivateSidecarSlot from the slot header's additive @68
// byte. Returns kWorldState_Open (0) when no slot is active or the slot
// predates the world_state ext. Used by the starting-inventory grant to
// recognize an Inverted slot on reload, where the full RandoSettings struct
// is unavailable.
uint8 Rando_GetActiveWorldState(void);

// True iff a rando slot is active AND its world-state is Retro. This is the
// canonical RUNTIME gate for the four Retro gameplay flags (rupeeBow /
// genericKeys / takeAnys / wildKeys). Per design.md §8 Risk 8 the flags are
// NOT serialized bits — they are *computed* from `world_state == Retro` at the
// point of use, so no new bytes enter the settings struct and the canonical
// length stays 28. A gameplay site that must diverge under Retro wraps its
// divergence in this test; when it returns false the vanilla code path runs
// byte-identically (rando inactive OR non-Retro world-state). Mirrors the
// inline gate the TakeAny runtime already uses (Rando_TakeAnyHostByDoorIndex).
bool Rando_IsRetroActive(void);

// True iff ALTTPR's `rom.genericKeys` is in effect for the active slot — i.e. a
// rando slot is active AND its world-state is Retro (Retro pins genericKeys on,
// per app/World/Retro.php). Under genericKeys small keys form one shared pool:
// any key opens any locked door. The RUNTIME small-key sites (enter-load,
// exit-save, door-consume, key grants in dungeon.c / sprite*.c / this file) gate
// on this to route through the shared counter `link_generic_keys` instead of the
// per-dungeon `link_keys_earned_per_dungeon[]` cells. Equal to Rando_IsRetroActive
// today; kept as a distinct name so a future standalone genericKeys axis can
// diverge, and so the runtime intent reads clearly at each site. When it returns
// false the vanilla per-dungeon key path runs byte-identically.
bool Rando_IsGenericKeysActive(void);

// Rando_GrantGenericKeyPurchase — grant one shared generic small key, the way an
// in-world GenericKey pickup would (bumps the persisted shared counter, and the
// live link_num_keys when standing in a dungeon). Called by the Retro genericKeys
// BUYABLE shop slot (ShopItem_GenericKey, src/sprite_main.c) on each purchase to
// provide ALTTPR's unlimited ShopKey supply, so the predicate VM's >=1 small-key
// wildcard is sound against the finite placed-key pool. The shop handler owns the
// rupee cost and purchase feedback; this only does the grant.
void Rando_GrantGenericKeyPurchase(void);

// True when the Hyrule Castle escape story sequence (uncle death / sewers /
// cell rescue / throne push) must NOT engage. Non-Standard rando seeds
// (Open / Inverted / Retro) start POST-escape: the placer pre-grants
// RescuedZelda and treats the overworld as free-roam, and the fresh-save SRAM
// is seeded post-escape (sram_progress_indicator=2, sram_progress_flags=0x14).
// But the HC interior is still physically reachable, and the vanilla escape
// story-beat sprites would re-run the sequence on entry — dropping
// sram_progress_indicator below 2 and writing an escape-only which_starting_point
// (cell=2, post-uncle sewers=3, throne=4), which both hard-traps a save-and-quit
// in the sealed escape and leaves the sprite CHR in its escape-specific
// half-refreshed state (the green-guard body-tile GFX corruption). Standard
// returns false: its escape is the real, intended start and progress climbs
// through it. Gate any HC escape story-beat trigger on this.
bool Rando_SuppressHyruleCastleEscape(void);

// Forward-declared so the prototypes below that take a RandoSidecarSlot* are
// at file scope (clang -Wvisibility errors if a struct tag is first introduced
// inside a function-parameter list). The full definition lives in rando_save.h.
struct RandoSidecarSlot;

// Copy g_rando_checked_bitmap into the supplied slot's checked_bitmap field.
// Callers about to write the ACTIVE rando slot to disk should invoke this
// just before calling Rando_WriteSidecarSlot so the in-memory checks survive
// the save/reload cycle. Safe to call when no slot is active (no-op).
void Rando_PopulateSlotBitmap(struct RandoSidecarSlot *out_slot);

// Persist the active session's checked-location bitmap to the sidecar slot
// at `slot_index`. Hook from in-game save points (SaveGameFile and its
// peers). Reads the existing sidecar slot (which has the header and
// placement table from the last write), overwrites checked_bitmap from
// the in-memory session, and writes back. No-op when no rando slot is
// active or `slot_index` is out of range.
void Rando_OnGameSave(int slot_index, const uint8 *paired_sram_slot, uint32 paired_sram_slot_size);

// ---------------------------------------------------------------------------
// §6.6 boss-kill dispatch helpers. The boss-kill code path in dungeon.c
// fires TWO grant sites per boss: the BossHeart drop (Sprite_HeartContainer)
// and the Prize crystal/pendant (RoomTag_GetHeartForPrize). Each has its
// own LOC_*.
//
// `dungeon_id` is `cur_palace_index_x2 >> 1` — the GAME's dungeon index,
// NOT the ALTTPR id ordering. Range 0..13:
//   0 HCE  1 HC        2 EP  3 DP  4 HCT  5 SP   6 PoD 7 MM
//   8 SW   9 IP       10 TH 11 TT 12 TR  13 GT
// Returns 0xFFFF for dungeons without a boss drop (HCE/HCT/GT — those have
// their own dispatch paths: Sanctuary chest, Agahnim event, Agahnim 2 event).
// ---------------------------------------------------------------------------
uint16 Rando_GetBossHeartLocation(uint8 dungeon_id);
uint16 Rando_GetBossPrizeLocation(uint8 dungeon_id);

// ---------------------------------------------------------------------------
// Active per-seed shuffle assignments. The predicate VM's OP_HAS_PRIZE and
// OP_MEDALLION_OPENS consult these via PredicateContext; the placer + sphere
// computation + tracker call Logic_ComputeReachability which reads them.
//
// Callers MUST set these before generating reachability — otherwise both ops
// degrade to "false", which makes prize-gated locations (Sahasrahla, GT
// entry, Master Sword Pedestal) and medallion-gated dungeons (MM, TR)
// unreachable.
//
// Pass NULL to clear (resets to "no assignment installed" — falls back to
// false). Non-NULL inputs are copied into owned storage, so callers may pass
// stack or temporary assignment buffers.
// ---------------------------------------------------------------------------
void Rando_SetDungeonPrizeAssignment(const uint8 *assignment);    // [kRandoDungeonCount]
void Rando_SetMedallionAssignment(const uint8 *assignment);       // [kRandoMedallionEntranceCount]
const uint8 *Rando_GetDungeonPrizeAssignment(void);
const uint8 *Rando_GetMedallionAssignment(void);

// Boss-shuffle assignment for the LOGIC VM (OP_CAN_KILL_BOSS). [16];
// entry = boss-pool index (kBoss_* in shuffle_boss.c) for that dungeon's boss
// room. Installed by the placer (from the base seed, matching the runtime
// install) so reachability gates each `- Boss`/`- Prize` on the SHUFFLED boss's
// kill predicate. This is INDEPENDENT of the sprite-substitution activation in
// shuffle_boss.c (g_boss_assignment_active): the logic table can be installed
// without the runtime sprite swap. NULL ⇒ OP_CAN_KILL_BOSS falls back to the
// vanilla boss (kRandoDungeonVanillaBoss). Non-NULL inputs are copied into
// owned storage. Pass NULL to clear.
void Rando_SetBossAssignment(const uint8 *assignment);           // [16]
const uint8 *Rando_GetBossAssignment(void);

// Runtime medallion-open gate for the Misery Mire (entrance_index 0) and Turtle
// Rock (entrance_index 1) overworld doors. `cast_medallion` is the item-registry
// id of the medallion the player just cast (Bombos=25, Ether=26, Quake=27).
// Returns true iff that medallion matches the shuffled assignment for the
// entrance. False when no assignment is installed (v1 / snapshot-restored slot)
// or entrance_index is out of range — callers fall back to the vanilla mapping.
bool Rando_MedallionOpens(uint8 cast_medallion, uint8 entrance_index);

// ---------------------------------------------------------------------------
// Active-slot settings recovery (format_version >= 2). On slot activation the
// canonical settings blob is deserialized and the prize/medallion shuffle
// assignments recomputed from (settings, seed). Rando_HasActiveSettings() is
// true only when that succeeded — the tracker windows gate their reachability
// display on it (false = "settings unknown", show checked/unchecked only, never
// confidently-wrong reachability). Rando_GetActiveSettings() returns the
// recovered settings, or NULL when unavailable.
// ---------------------------------------------------------------------------
bool Rando_IsActive(void);
bool Rando_HasActiveSettings(void);
const RandoSettings *Rando_GetActiveSettings(void);

// Regenerate the hint table for the CURRENTLY-ACTIVE slot, replaying exactly
// the activation-time hint block — including the v1/no-blob fallbacks
// (header-ext hints_setting/goal, default hints-on for the oldest slots),
// which Rando_GetActiveSettings() cannot express (it returns NULL there even
// though activation regenerated hints). Call after any out-of-band
// Rando_GenerateHints clobber (e.g. the native window's spoiler export) to
// restore the active seed's in-game telepathic-tile / fortune-teller hints.
// Clears the table (vanilla text) when no slot is active.
void Rando_RegenerateActiveSlotHints(void);

// Sibling of the above — reinstall the LOGIC-side overlays
// (entrance region/edge overrides + door logic layout) AND the logic-VM
// shuffle-assignment stores (prize/medallion/boss — the
// placer replaces them with per-run assignment bytes on every
// Place_AssumedFill, and
// they are not tracker-only: dungeon.c's falling-prize pick and ancilla.c's
// MM/TR medallion gates read them) for the CURRENTLY-ACTIVE slot, replaying
// the activation-time install sub-steps from the state captured at
// activation. Call after any out-of-band generation (Rando_GenerateSlot
// clears/replaces these global stores and never restores) so tracker/map
// LOGIC reachability and the prize/medallion gameplay gates don't follow the
// unloaded seed until slot reload. Gameplay-side installs (the asset-126
// entrance overlay, DoorRt runtime redirects, decoupled nets, boss/drop/enemy
// RENDER shuffles — generation only uses the pure *_ComputeAssignment forms)
// are untouched by generation and by this helper. No active slot: leaves the
// stores cleared and the assignments NULL (fail-closed, the correct idle
// state — mirrors activation's !settings_valid arm).
void Rando_ReinstallActiveSlotLogicOverlays(void);

// True when the active slot's spoiler-grade data (placed item names, hint text)
// must stay HIDDEN: either it is a race seed, OR the settings could not be
// recovered (snapshot-restore / legacy format_version-1 slot), in which case we
// fail CLOSED. Deriving this policy per call site previously fail-OPENED on NULL
// settings (`settings && settings->race_mode` => false => revealed), leaking a
// race seed's placements in the tracker after a snapshot replay. The tracker /
// reachability / hints panels MUST gate on this, never on the raw settings.
bool Rando_ActiveSlotHidesSpoiler(void);

// True when the active slot is swordless (mode.weapons=swordless). Gates the
// runtime swordless patches; false (vanilla behavior) when not swordless or when
// settings are unknown. See Rando_IsSwordlessActive in rando.c.
bool Rando_IsSwordlessActive(void);

// ---------------------------------------------------------------------------
// Live reachability bridge for the tracker windows. Rando_BuildRuntimeCounts
// maps the live g_ram inventory into a logical RandoCounts; Rando_GetLiveReach-
// ability computes (memoized on the reachability counter) the set of currently
// reachable locations/regions, or NULL when settings are unavailable (caller
// then shows checked/unchecked only). See rando_logic.h Reachability_Has*.
// ---------------------------------------------------------------------------
void Rando_BuildRuntimeCounts(RandoCounts *out);
const RandoReachability *Rando_GetLiveReachability(void);

// ---------------------------------------------------------------------------
// Compact item-tracker view of the live inventory, filled from g_ram by
// Rando_FillItemView. A clean data boundary so the ImGui item-tracker window
// renders from this struct instead of the full variables.h RAM-macro namespace.
// Levels: 0 means "not obtained". Bow/boomerang/mushroom-powder/flute-shovel
// use the rando-aware decoupling (shared vanilla bytes resolved here).
// ---------------------------------------------------------------------------
typedef struct RandoItemView {
  uint8 sword;        // 0..4 (0 none; 1 fighter .. 4 gold)
  uint8 shield;       // 0..3 (0 none; 1 fighter, 2 red, 3 mirror)
  uint8 mail;         // 0 green (start), 1 blue, 2 red
  uint8 gloves;       // 0 none, 1 power, 2 titan
  uint8 bow;          // 0 none, 1 wood, 2 silver
  uint8 boomerang;    // 0 none, 1 blue, 2 red
  uint8 bottles;      // 0..4
  uint8 magic;        // 0 normal, 1 half, 2 quarter
  uint8 hearts;       // heart containers
  uint8 heart_pieces; // 0..3 toward next container
  uint8 crystals;     // count obtained (0..7)
  uint8 pendants;     // count obtained (0..3)
  uint8 crystal_mask; // bit per crystal# (0..6) obtained
  uint8 pendant_mask; // bit0 green(courage actually idx), see fill code
  bool hookshot, firerod, icerod, hammer, lamp, net, book;
  bool somaria, byrna, cape, mirror, boots, flippers, moon_pearl;
  bool bombos, ether, quake;
  bool mushroom, powder, flute, shovel;
  bool agahnim;       // Agahnim 1 defeated
  // Per-dungeon items. bigkey/map/compass are bitfields with bit
  // (0x8000 >> game_index) where game_index = cur_palace_index_x2>>1. Hyrule
  // Castle's big-key/compass bit is at game_index 1 (0x4000) — when standing in
  // HC the live cur_palace_index_x2 is 2 (verified by F12 dump). Small keys are
  // a separate axis: SaveDungeonKeys folds HC (raw dungeon id 2) into key slot
  // 0, so HC's bit index (1) and key slot (0) differ. (Map_HCE, the rando map
  // GRANT, dispatches to index 0 = the sewers/escape sub-area — a separate
  // index from the HC dungeon proper; do not conflate the two.)
  uint8 dungeon_small_keys[16];
  uint16 bigkey_bits;
  uint16 map_bits;
  uint16 compass_bits;
} RandoItemView;

void Rando_FillItemView(RandoItemView *out);

// ---------------------------------------------------------------------------
// Rando_ActivateSidecarSlot / Rando_DeactivateSlot — bridge between the
// file-select slot pick and the runtime randomizer dispatcher.
//
// Activate copies the sidecar slot's placements into a session-persistent
// buffer, installs them via Placement_Install, sets g_rando_slot_active = 1,
// and ORs kFeatures1_RandomizerActive into enhanced_features1 +
// g_wanted_zelda_features1. Subsequent Rando_OnLocationCheck calls consult
// the installed placement table.
//
// Deactivate is the inverse — clears the installed placement, the slot-
// active flag, and the feature bit. Call when a vanilla slot is picked, or
// when the user returns to the title screen.
//
// Caller passes a sidecar slot in `src`; if NULL or its slot_kind is not
// kSlotKind_Randomizer, Activate falls through to Deactivate.
// ---------------------------------------------------------------------------
struct RandoSidecarSlot;
void Rando_ActivateSidecarSlot(const struct RandoSidecarSlot *src);
void Rando_DeactivateSlot(void);

// Snapshot cold-replay restore. Reconstructs the active-slot logic-side
// state (prize/medallion/boss/drop/enemy assignments + Inverted installs +
// JP-glitch coupling) from a type-2 RandoSettings snapshot TLV, given the
// recovered canonical settings, the raw 32-byte share string (→ base seed) and
// the accepted prize_attempt. GATED: a no-op when a slot is already validly
// active (within-session replay); fires only on a genuine cold replay (fresh
// launch, slot never loaded). Called by RandoSnapshotTail_Load.
void Rando_SnapshotColdReplayRestore(const RandoSettings *s,
                                     const uint8 *share_string_raw,
                                     uint8 prize_attempt);

// ---------------------------------------------------------------------------
// Rando_DrawHashIcons (tasks.md §9.4b — 5-icon visual hash widget).
//
// Renders a 5-tile horizontal strip starting at (x, y), one tile per index
// from kHashIconAtlas. The tile indices are derived from
// `SHA-256(share_string_binary)[0..4] mod kHashIconAtlasSize` per the
// randomizer-ui spec. Critical: the hash input is the FULL share-string
// binary (31 bytes: magic + version + settings_hash + seed_u64 + checksum),
// NOT settings_hash alone — otherwise every seed with identical settings
// would render identical icons.
//
// Writes 5 consecutive OAM entries beginning at *oam. The widget reserves a
// horizontal strip of 5*8 = 40 px starting at x. The OAM palette flags
// match the file-select font palette so the icons read cleanly against the
// existing background.
//
// `share_string_binary` MUST be the 31-byte raw binary blob (the same data
// that Share_EncodeRaw base32-encodes for display). The buffer is sized to
// 32 to match RandoSlotHeader.share_string[]; the last byte is zero-pad
// per the rando_save.h spec.
// ---------------------------------------------------------------------------
struct OamEnt;  // forward decl (defined in zelda_rtl.h via spc_player.h)
void Rando_DrawHashIcons(int x, int y,
                         struct OamEnt *oam,
                         const uint8 share_string_binary[32]);

// ---------------------------------------------------------------------------
// Self-tests (tasks.md §2.2, §13.x). Always linked; CI invokes via
// `--rando-selftest`. Exits with code 2 on any failure.
// ---------------------------------------------------------------------------
void Rando_SelfCheck(void);            // SHA-256 NIST vectors
void Rando_RunAllSelfChecks(void);     // SHA-256 + RNG (+ future subsystems)

#endif  // ZELDA3_RANDO_H_

