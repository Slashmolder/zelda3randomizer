# Proposal: NPC Souls

## Why

Enemy/boss souls (add-enemy-souls) gate enemy existence on collectible soul items; the owner wants the same treatment for the game's check-giving PEOPLE as an option: no Stumpy check until you find his soul, no maze race until you find both people involved, no digging or chest game until their proprietor's soul is found. NPC-driven checks are currently always available once their item/region requirements are met — this adds a new progression layer (24 souls) that composes with the existing souls tiers and makes NPC absence a player-visible logic signal.

## What Changes

- New independent setting `npc_souls` (`off`/`on`, canonical byte [28] bit 4, default off) — orthogonal to `souls_shuffle` (works with souls off, bosses, or all). No door-shuffle degrade: none of the gated checks are door-oracle-controlled (verified: 0 of the roster's locations are inside dungeons).
- 24 new soul items (registry ids 196-219, one per PERSON — multi-person checks require every involved person's soul, per the owner's maze-race framing): Sahasrahla, King Zora, Witch, Magic Bat, Sick Kid, Bottle Merchant, Hobo, Old Man, Stumpy, Catfish, Waterfall Fairy, Pyramid Fairy, home Smith, Frog Smith, Middle-Aged Man, Digging Game proprietor, Chest Game host, Maze Game Lady, Maze Game Guy, Mini Moldorm Cave NPC, Hype Cave NPC, Kiki, Bomb Shop dealer, and Link's Uncle (owner decision 2026-07-06, reversing the initial exclusion: gated in EVERY world state; only his secret-passage grant site is suppressed — the house/sanctuary 0x73 actors are choreography — and the Standard sphere-0 weapon/lamp accept bars exclude his slot so the escape's guaranteed weapon lands elsewhere).
- Runtime: with `npc_souls=on`, an un-souled NPC does not spawn (site-scoped suppression through the three existing soul spawn hooks; dynamic/scripted NPCs gate at their interaction-trigger sites). The player sees the person missing.
- Logic: the corresponding check predicates AND in the soul terms (collapse to true when off — off-path placement stays byte-identical). Two checks gain NEW progression modeling: Palace of Darkness ENTRY requires Kiki's soul (his payoff opens the dungeon), and the Pyramid Fairy checks require the Bomb Shop dealer's soul (he sells the Big Bomb).
- Soul-ownership persistence widens: 46 existing + 24 NPC souls = 70 > the current 64-bit flags field → `kSoulFlagsBytes` 8→12, sidecar format_version 7, snapshot-tail type-8 TLV payload widened with length-gated reads.
- Placement: NPC souls join the progression pool when on; the enemies-tier collecting fill model also activates for `npc_souls=on` (24 scattered cross-gating items risk mutual-lock fill failures under the conservative assumed-only model).
- `kGeneratorVersion` bump + corpus entries (on-path witnesses per world state; off-path digest-inertness proven by rebaseline diff).

## Capabilities

### New Capabilities

(none — all changes extend existing capability specs)

### Modified Capabilities

- `randomizer-souls`: NPC soul roster, the `npc_souls` toggle semantics, site-scoped suppression + trigger-site gating, ownership-flags widening. NOTE: this spec materializes in `openspec/specs/` when add-enemy-souls archives — **this change depends on add-enemy-souls landing first**.
- `randomizer-logic`: soul terms on the 22 NPC-driven check locations + the Kiki entry edge; the new `OP_NPC_SOULS_ACTIVE` predicate op; the Kiki term on the Palace of Darkness entry edge (per world state); Bomb Shop terms on the Pyramid Fairy checks.
- `randomizer-core`: pool composition (+24 progression when on); the collecting fill model's activation condition gains `npc_souls=on`.
- `randomizer-save`: sidecar format_version 7 (widened soul flags), snapshot-tail type-8 length-gated widening.
- `randomizer-native-window`: `npc_souls` toggle in the souls settings group + tracker souls-grid NPC section.

## Impact

- **Depends on add-enemy-souls** (branch `claude/vigorous-bhaskara-d0cf97`, pending playtest + archive): souls.c machinery, spawn hooks, direct-grant dispatch, TLV/sidecar souls blocks, ImGui souls UI, `OP_SOULS_TIER_AT_LEAST` codegen pattern.
- Code: `src/rando/souls.{c,h}` (site-scoped allow query, flags widening), `src/sprite.c` (hook call-site context), NPC interaction handlers in `src/sprite_main.c` (trigger-site gates: Magic Bat altar summon, Catfish throw, Witch mushroom hand-in + powder-bag grant, both fairy pond grant branches), `src/rando/rando_settings.{c,h}` (incl. relaxing the canonical [28] bits-4-7 deserialize refusal to admit bit 4), `rando_placement.c`, `rando_save.{c,h}`, `rando_snapshot_tail.c`, `rando_window` panels, `assets/rando/item_registry.yaml`, `assets/rando_logic_gen.py` (location wraps + a NEW edge-wrap pass for the Kiki term) + a new committed `assets/rando/npc_souls.yaml` site/term table whose C table is emitted by a committed generator script (generate-don't-transcribe; committable structural data, no gitignored codegen needed).
- Sprite ground truth (research + independent plan review, 2026-07-06): every gated person resolves to a suppressible or trigger-gatable site; shared-id cases (0x16 Sahasrahla/Aginah, 0x1A smiths/frog, 0x2E Stumpy/flute-boy, 0xBB chest-game-host/shopkeepers) disambiguate by ROOM/AREA + WORLD (subtype is derived after the spawn decision and is NOT available at the hooks). CRITICAL invariants: 0xC0 is never suppressed (both King Zora's and the Catfish's grant deliveries ride 0xC0 spawns — those gate at trigger sites); 0xE9 is never suppressed (its subtypes include the potion cauldrons — logic-assumed magic refills — and the powder-bag sprite that IS the Potion Shop grant); the fairies have no discrete sprite — their checks gate at the room-scoped wish-pond grant branch.
- Save-compat: pre-v7 sidecars and pre-widening snapshots load with NPC flags zeroed (safe default; `npc_souls=off` seeds suppress nothing).
