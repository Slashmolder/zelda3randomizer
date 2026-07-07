# Design: NPC Souls

## Context

add-enemy-souls shipped the full souls machinery: soul items with `direct_soul` dispatch, a 3-hook spawn-suppression architecture keyed on sprite type (`Souls_SpriteAllowed`), `OP_SOULS_TIER_AT_LEAST` logic wraps, an 8-byte ownership bitfield persisted via sidecar v6 + snapshot-tail TLV type 8, and tracker/pool/settings integration. NPC souls reuse all of it, with three deltas this design resolves: (1) suppression must be SITE-scoped, not species-scoped, because five roster NPCs share sprite ids with other actors; (2) two NPCs carry progression side effects beyond their own check (Kiki opens Palace of Darkness, the Bomb Shop dealer sells the Big Bomb) that logic has never modeled as NPC-dependent; (3) the ownership bitfield is over capacity (46+24=70 > 64 bits).

Sprite-level ground truth. An independent plan review (2026-07-06) falsified several first-pass entries — the corrected table is below, but its lesson is structural: **the whole table MUST be re-derived by a committed verification script at implementation** (generate-don't-transcribe; the first pass carried 3 wrong ids and ~5 wrong spawn-path labels while claiming "verified"). Review-corrected facts are marked ✔R.

| Soul | Sprite | Scoping / gating notes |
|---|---|---|
| Sahasrahla | 0x16 | shared with Aginah — `SpritePrep_Sage` discriminates by ROOM (room byte 10 = Aginah) ✔R; scope by room |
| King Zora | 0x52 | static; grant delivery rides a DYNAMIC 0xC0 spawn (`Sprite_Zora_RegurgitateFlippers`) |
| Witch | 0x36 (outside witch ONLY) | **0xE9 is NEVER suppressed** ✔R: `Sprite_E9_PotionShop` subtype2 0=assistant, 1=the powder bag that IS the `LOC_Potion_Shop` grant, 2/3/4=the green/blue/red potion CAULDRONS — the game's only purchasable magic refill, which `CanExtendMagic`'s bottle disjunct (macros.yaml) assumes. Gate the check at the hand-in trigger (`Witch_AcceptShroom`) + the powder-bag grant branch instead (see D2) |
| Magic Bat | 0x3A | ROOM-STATIC (idles awaiting a powder ancilla; already has a location-checked despawn guard) ✔R — plain site suppression by room |
| Sick Kid | 0x1F | static (house room) |
| Bottle Merchant | 0x75 ✔R (0x77 is an Antifairy alias — first pass was wrong) | overworld proxima |
| Hobo | 0x2B | spawn path re-derive (likely proxima, not static ✔R) |
| Old Man | 0xAD | static (cave rooms); no logic/runtime consumer beyond his own check ✔R |
| Stumpy | 0x2E | shared with LW flute boy — `SpritePrep_FluteKid` discriminates by WORLD (`savegame_is_darkworld`) ✔R; scope by area+world |
| Catfish | 0xC0 | shared with King Zora reward delivery — gate at the THROW TRIGGER, never the 0xC0 spawn funnel (BOTH grants ride dynamic 0xC0 ✔R) |
| Waterfall Fairy | — | **no discrete "fairy sprite" to suppress** ✔R: 0x37 is `Sprite_37_Waterfall` (subtype 1 = `Sprite_BatCrash`, part of the MAGIC BAT summon!) — first pass was wrong. The checks grant on contact via `Sprite_72_FairyPond`/`Sprite_WishPond3`, which also runs the wishing/happiness ponds. Gate the WishPond grant branch, room-scoped (see D2) |
| Pyramid Fairy | — | same ✔R: 0x38 is `Sprite_38_EyeStatue` (a dungeon water-puzzle driver) — do NOT touch; gate the WishPond grant branch, room-scoped |
| Home Smith | 0x1A | `SpritePrep_Smithy` state routing; scope by room (smithy house) |
| Frog Smith | 0x1A | scope by room/area (DW frog spot) |
| Middle-Aged Man | 0x39 | static |
| Digging Game | 0xD5 | static |
| Chest Game host | 0xBB | shared with ALL shopkeepers/thieves — `SpritePrep_Shopkeeper` discriminates by 8-bit room lookup (`kShopKeeperWhere`) ✔R; the site table stores the FULL room id and the generator asserts no 0xBB site collides with a Retro take-any host room (0x10F/0x112/0x11F are regular shops) |
| Maze Game Lady | 0x2F | static (screen 0x28) |
| Maze Game Guy | 0x30 | static (screen 0x28) |
| MMC NPC | ⚠ id unverified — pin from room 0x123's sprite list at implementation | registry check 185, real + separate from the chests ✔R |
| Hype Cave NPC | ⚠ id unverified — pin from Hype Cave room sprite list | registry check 227, real + separate from the chests ✔R |
| Kiki | 0xB6 | `SpritePrep_Kiki` gates on `save_ow_event_info & 0x20`; overworld proxima (DW ruins area) |
| Bomb Shop dealer | 0xB5 | static (shop room) |
| Link's Uncle (24th, owner decision 2026-07-06) | 0x73 | shared `Sprite_73_UncleAndPriest`: bind ONLY room 0x055 (secret-passage grant site); room 0x104 = Standard opening choreography, room 0x012 = the PRIEST — both untouched. Standard's sphere-0 weapon/lamp accept bars EXCLUDE the Uncle slot when npc_souls is on (an item there is soul-gated and cannot anchor the escape) |

All roster ids map to 0xFF in `kSoulForSprite` (enemy shuffle never touches them; no pin interaction). None of the gated locations are inside door-shuffled dungeons.

## Goals / Non-Goals

**Goals:**
- `npc_souls=on`: an un-souled NPC visibly does not exist; every check they grant or enable is logic-gated on the involved souls; seeds remain beatable-by-construction.
- `npc_souls=off` (default): placement byte-identical to pre-feature (corpus-proven), zero runtime behavior change.
- Composes with every `souls_shuffle` tier, world state, and shuffle axis without new degrade rules.

**Non-Goals:**
- No gating of story-critical Standard-mode actors (Zelda, the sanctuary priest, and Uncle's HOUSE/opening appearance — his secret-passage GRANT site is gated per the 2026-07-06 owner decision, with the Standard accept bars excluding his slot).
- No shuffling of NPC positions — souls gate existence, not location.
- No per-NPC settings; one toggle covers the whole roster.
- No Switch in-game settings surface (PC ImGui only, per repo convention).

## Decisions

### D1. Site-scoped suppression: `kNpcSoulSites[]` + `Souls_NpcSpriteAllowed`

A committed table `{sprite_type, room_or_area (0xFFFF=any), world (LW/DW/any), npc_soul_index}` generated into C from `assets/rando/npc_souls.yaml`. New query `Souls_NpcSpriteAllowed(type, room_or_area, world)` consulted by the SAME hook sites `Souls_SpriteAllowed` already owns:
- **Dungeon/house static hook** (`Dungeon_LoadSingleSprite`): room id (`dungeon_room_index2`) is in scope. **subtype2 is NOT** (review-verified): the prep handlers derive it AFTER the spawn decision (`SpritePrep_Sage` by room byte, `SpritePrep_FluteKid` by `savegame_is_darkworld`, `SpritePrep_Shopkeeper` by `kShopKeeperWhere[room]`), and the suppression branch runs BEFORE the packed coordinate-bit decode — so the table key is (type, room, world), never subtype2. Every shared-id roster case is room- or world-discriminable (see the ground-truth table).
- **Overworld proxima hook**: area index (`overworld_area_index`) + type; subtype is hard-zeroed on this path. Area+world scoping suffices for the proxima-loaded roster NPCs (Bottle Merchant 0x75, Kiki 0xB6, Stumpy-vs-flute-boy by world).
- **Dynamic funnel hook** (`Sprite_SpawnDynamicallyEx`): NOT used for NPC souls (see D2).

**Slot consumption (round-2 HIGH)**: the static-hook NPC branch MUST consume the sprite slot exactly like the enemy-souls branch does (inert `sprite_state[k]=0` slot, `sprite_N[k]=k`, no killed bit, `return k`), differing ONLY in not calling `Souls_NoteRoomSuppressed()`. A skip-without-consume branch shifts every later entry's structural slot index — and room 0x123 (Mini Moldorm Cave) concretely holds four enemy-check rows keyed on slots 0-3 (`enemy_check_lookup.h`) in the same sprite list as the roster's MMC NPC (0xBB via `kShopKeeperWhere` room byte 0x23): a shift mis-keys `Rando_FindEnemyCheck(room, k)` grants and `sprite_where_in_room` kill bits.

**Kill-gate counter**: the static hook's enemy-souls branch calls `Souls_NoteRoomSuppressed()`, which holds room-clear kill-gates shut. NPC suppression MUST NOT increment it — no roster NPC participates in a kill gate, and a counted NPC would wedge any future kill-tagged room it shares.

**Interior sites are `world=any` (round-2 MED)**: Inverted swaps interior entrances (e.g. the Bomb Shop is entered from the LIGHT world), so a vanilla-derived world value on a room-keyed site silently stops suppressing there. The generator ASSERTS room-keyed (interior) sites carry `world=any`; world scoping is legal only on area-keyed overworld sites (the Stumpy/flute-boy pair, which Inverted does not relocate).

Rationale: species-keyed `kSoulForSprite` cannot express "suppress 0x2E only in the Dark World"; a flat extension would kill the light-world flute boy (breaking the flute quest visuals) and the Aginah/shop/thief actors. The site table is small, hand-curated, structural — committable under the embedded-data policy.

### D2. Dynamic/scripted NPCs and contact-granted checks gate at the interaction TRIGGER, not the spawn funnel

- **Magic Bat (round-2 corrected)**: there is NO dynamic summon spawn — 0x3A is a ROOM-STATIC sprite that idles in its case 0 scanning for a powder ancilla (`ancilla_type[i] == 0x1a`), and it already carries a `Rando_IsLocationChecked(LOC_Magic_Bat)` despawn guard. Plain site-scoped static suppression of 0x3A by room is the gate (visible absence ✓); powder on the altar does nothing while suppressed, and after the soul is found the sprite spawns on room re-entry — re-armable, not missable.
- **Catfish (as-built simplification)**: the asset scan showed the Great Catfish is itself a STATIC overworld sprite (0xC0 in area 0x4F) — so it gets plain area-scoped site suppression like every other overworld NPC, and no throw-trigger gate is needed (no sprite → the circle-of-stones throw finds nothing to wake). The load-bearing invariant is unchanged: NEVER consult NPC sites from the dynamic funnel — BOTH the King Zora flippers grant AND the Catfish medallion delivery ride dynamic 0xC0 spawns (`Sprite_Zora_RegurgitateFlippers` / `Catfish_RegurgitateMedallion`), and those must always spawn once granted. The generator restricts 0xC0 sites to area-kind, and the emitted header documents the funnel exclusion.
- **Witch (review HIGH)**: the check is gated at TWO trigger sites — `Witch_AcceptShroom` (the outside witch's mushroom hand-in; suppressing 0x36 covers the visible absence) and the 0xE9-subtype-1 powder-bag grant branch (belt-and-suspenders for saves where the hand-in flag `save_dung_info[0x109] & 0x80` is already set). The 0xE9 sprite itself is NEVER suppressed: its other subtypes are the shop assistant and the three potion cauldrons — the game's only purchasable magic refill, which `CanExtendMagic`'s bottle disjunct assumes is always for sale. Suppressing them would let the placer certify Byrna/magic routes that are physically impossible.
- **Waterfall/Pyramid Fairies (review HIGH)**: there is no discrete fairy sprite to suppress — the checks grant on contact through the shared wish-pond machinery (`Sprite_72_FairyPond`/`Sprite_WishPond3`), which also runs the wishing/happiness ponds. Gate the pond GRANT branch for the two fairy rooms on the respective souls (room-scoped trigger-site gate); other pond rooms are untouched. The visible effect is "the pond does nothing here yet" rather than a missing person — acceptable for spirits.
- **King Zora** himself is a static 0x52 whose emergence is a state change — static hook covers him; his reward spawn is gated by HIS soul via the trigger site only if suppression of the static sprite proves insufficient (verify in playtest).
- **Kiki** rides as a follower after the pickup interaction (`SpritePrep_Kiki` also self-despawns on `save_ow_event_info & 0x20`); the proxima hook suppresses his spawn, so the PoD-opening payoff can never start without the soul. Audit his follower/tagalong state for stale-flag leaks (control-flow-signal class) at implementation.

### D3. Logic: `OP_NPC_SOULS_ACTIVE` + wrap injection from the committed table

New predicate op (registry-append, value 29): true iff `settings->npc_souls != 0`. Soul terms compile as `(NOT OP_NPC_SOULS_ACTIVE) OR HAS_ITEM(Soul_X)` — identical collapse pattern to the enemy-souls tier terms, so off-path bytecode evaluates true and placement digests stay byte-identical (rebaseline-diff proven, not assumed). The VM's short-circuit evaluation (skip_pred) means the wraps cost nothing when decided; extend `skip_pred`'s operand table + the `Logic_SelfCheck` structural walk covers the new op automatically via the generated tables.

Wraps are injected by `rando_logic_gen.py` from `npc_souls.yaml`'s `gates:` section (location name → required souls), NOT by logic_parts duplicate entries (last-wins trap). Multi-person checks AND every involved soul:
- Maze Race ← Lady + Guy (also see D5)
- Blacksmith ← Home Smith + Frog
- Purple Chest ← Home Smith + Frog + Middle-Aged Man
- Potion Shop ← Witch (one soul; the counter assistant is the same business)
- All single-NPC checks ← their soul
- Waterfall Fairy L/R ← Waterfall Fairy; Pyramid Fairy L/R ← Pyramid Fairy + Bomb Shop dealer (Big Bomb purchase)
- **Kiki**: the Palace of Darkness ENTRY EDGE gains the Kiki term. Review-verified mechanics: the base edge lives in `logic_parts/20_palace_of_darkness.yaml`; Inverted adds an ADDITIVE `world_state_edges` TRUE() edge (`logic_parts/inverted/PalaceOfDarkness.yaml`), so a Kiki term on the base edge is automatically bypassed in Inverted ✔; entrance shuffle redirects `to_region` while KEEPING the edge predicate (`Rando_GetEntranceEdgeOverride`), so the term stays with the physical gate ✔. Two open items the implementation must close: (a) `_apply_soul_room_wraps` wraps LOCATIONS only — an EDGE-wrap pass must be built in `rando_logic_gen.py` (small: find edge by from/to region names, AND the term); (b) verify the fork's Inverted RUNTIME actually opens the PoD gate without Kiki's payoff (`SpritePrep_Kiki` gates on `save_ow_event_info & 0x20`) — if Inverted needs Kiki at runtime, the TRUE() logic edge is a LATENT PRE-EXISTING softlock that npc_souls would inherit and amplify; F12-verify before shipping. Under dungeon-chains, a PoD-as-successor entry legitimately bypasses the overworld entrance (the seam teleports into the lobby), which matches runtime: no Kiki needed through the seam.

### D4. Settings, items, pool

- `npc_souls` u8 (0/1), canonical byte [28] bit 4 (bits 4-7 were free; claim bit 4 in the allocation comment immediately — concurrent-drift discipline). **Deserialize-mask relax (review MED)**: `Settings_FromCanonical` currently HARD-REFUSES any [28] bits-4-7 content (`return -2`) as corruption/newer-axis rejection — the mask must widen to accept bit 4, and the refusal of bits 5-7 stays. Cross-version behavior is the existing convention and is correct as-is: an old build refuses a share string carrying bit 4 (forward-compat refusal), and the `kGeneratorVersion` bump fences race shares regardless; add a Settings_SelfCheck case for both directions. Parse key `npc_souls=off|on`; rides the canonical blob into share strings and the settings hash. No derived rules: no door-shuffle degrade (nothing door-oracle-controlled), no world-state coercion.
- Item ids 196-218 appended to `item_registry.yaml` (`category: soul`, `dispatch: direct_soul`), `ITEM__COUNT` 196→219 (< 256 capacity `_Static_assert` holds). Souls are progression iff `npc_souls` on; `pool_add` gated on the setting; pool self-checks assert 23-when-on / 0-when-off.
- Placement fill model: `souls_collect_model` activation becomes `effective souls_shuffle >= all OR npc_souls on`. Rationale: 23 scattered items whose gates reference EACH OTHER (e.g. Stumpy soul placed at Sick Kid while Sick Kid soul sits at Stumpy) can mutually lock under the conservative assumed-only model; the collecting fix-point already handles exactly this class. Off-path unaffected (model selection is per-seed).

### D5. Runtime check gating beyond spawn suppression

Suppressing the person does not always physically gate the check — audit each grant path:
- **Maze Race**: the prize is a STANDING heart piece; absent race NPCs may leave it grabbable. Gate the standing-item spawn/collect for `LOC_Maze_Race` on both souls (same guard pattern as the existing `Rando_IsLocationChecked` sprite guards, inverted polarity). Verify the start-gate behavior in playtest; the logic wrap makes the seed sound either way, but the runtime must not hand out an item logic says is gated.
- **Chest Game**: verify the chests refuse to open without the host (they are host-brokered in vanilla); if any path opens them hostless, guard the room's chest dispatch on the soul.
- **Digging Game**: dig mode is proprietor-dialogue-entered — suppression suffices; the 30-second dig window cannot start.
- All other roster checks are NPC-dialogue-granted — suppression suffices.

### D6. Persistence: flags widening

`kSoulFlagsBytes` 8→12 (96 bits; 69 used, 27 spare). `_Static_assert(kSoulCount + kNpcSoulCount <= kSoulFlagsBytes * 8)`. **Every 8-byte assumption site must move together** (review-enumerated): the live `g_soul_flags[kSoulFlagsBytes]`, `RandoSaveSlot.header.soul_flags[8]` (a LITERAL 8 in `rando_save.h`) and its `sizeof`-copy sites in `rando.c` (slot capture + activation), the sidecar v7 extension layout, and the TLV writer. Sidecar format_version 6→7: the v7 extension appends the 4 new flag bytes (pre-v7 reads zero them — safe default). Snapshot-tail TLV type 8: reader is already length-tolerant (skips excess, zero-extends short — review-verified); KEEP the payload's internal `format_version==1` and grow only the length, so pre-widening builds still restore the first 8 bytes of a widened snapshot instead of rejecting it. RandoState-accept clears all 12 bytes. Cold-replay + round-trip selfchecks extend to the widened field. Souls_SelfCheck gains NPC-range assertions (site table sorted/valid rooms/soul indices in range).

### D7. UI / visibility

- ImGui souls group gains the `npc_souls` checkbox with a 1-2 durable-fact tooltip (tooltip-brevity rule).
- Tracker souls grid gains an NPC section (names via generated `Rando_GetItemName`; hints and spoilers get soul names for free through the registry).
- Icons: reuse the generic soul icon cell for all 23 (per-NPC art is out of scope; the confirmation cue + tracker text disambiguate).

## Risks / Trade-offs

- **[Kiki edge is new progression modeling]** Wrongly scoped per world state it either strands PoD (too strict) or lies to the placer (too loose) — and the Inverted TRUE() entry edge may hide a LATENT runtime softlock (does Inverted's PoD gate really open without Kiki's payoff?) that this feature would amplify. → Model from the current logic graph per world state; F12-verify the Inverted runtime gate; corpus witnesses for open/standard/inverted with npc on; playtest PoD entry with and without the soul.
- **[0xC0 shared spawn]** Any future refactor that moves catfish gating into the funnel breaks King Zora's grant. → The npc_souls.yaml site table carries an explicit `never_funnel: [0xC0]` assertion the generator enforces; comment at the funnel hook.
- **[Standing-item leak (D5)]** A physically grabbable Maze Race prize while logic says gated = the runtime hands out an "unreachable" item (harmless to beatability but breaks the accessibility contract and race fairness). → Runtime guard + playtest.
- **[Missable-check inversions]** The feature INVERTS the usual guard direction (absent until soul). Sweep every roster NPC's one-shot vanilla flags for states that could make the check unobtainable when the soul arrives late (bat altar re-trigger ✓ by design; smith chain flags; witch mushroom hand-in; Old Man escort re-entry). → Dedicated audit task + playtest matrix rows.
- **[Fill pressure]** +23 progression items on top of 46 souls tightens Retro/hunt/strict-accessibility fills further. → Corpus entries at the pressure combos; judge refusals by spoiler-write (existing convention).
- **[Bitfield widening touches persistence on 3 fronts]** sidecar v7 + TLV width + live array; a missed site truncates ownership on save/load. → Selfchecks + cold-replay test + the existing round-trip harness extended before playtest.
- **[Transcribed sprite table was WRONG — this risk already materialized]** The first-pass table shipped 3 wrong ids (Bottle Merchant 0x77→0x75; both fairies pointed at unrelated mechanism sprites) and ~5 wrong spawn-path labels while claiming "verified". → The site table is GENERATED by a committed script that parses the sprite handler table + prep handlers and asserts its own invariants (ids resolve to the expected handler symbols, no 0xC0/0xE9 site, no take-any-room 0xBB collision); hand transcription is banned for this data (generate-don't-transcribe discipline).
