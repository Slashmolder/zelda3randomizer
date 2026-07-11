# randomizer-placement Specification

## Purpose
TBD - created by archiving change add-randomizer-support. Update Purpose after archive.
## Requirements
### Requirement: Dispatcher signature and fall-back behavior

The placement layer SHALL expose `Rando_OnLocationCheck(location_id, vanilla_item_id)`. The function resolves the location ID to a substitute item via the active placement table and applies the appropriate item-receipt routine. When the location ID is not in the table (or the table is absent), the dispatcher SHALL grant the supplied `vanilla_item_id` via the standard vanilla code path.

#### Scenario: Known location grants substitute
- **WHEN** the location ID is present in the active placement table and maps to "Hookshot"
- **THEN** the hookshot is granted via its standard receive path regardless of the supplied `vanilla_item_id`

#### Scenario: Unknown location falls back to vanilla
- **WHEN** the location ID is not present in the placement table
- **THEN** the dispatcher logs a single warning (debug-build assert), grants `vanilla_item_id` via the vanilla receive path, and gameplay does not soft-lock

### Requirement: Single dispatch point per grant site

Every vanilla item-grant site enumerated in the Phase 0 audit (`audit.md`) SHALL route through exactly one `Rando_OnLocationCheck` call. The grant sites enumerated by the audit SHALL include, at minimum: every chest open path (dungeon chest, big-key chest, dungeon-prize chest, overworld chest, cave chest); every NPC gift (uncle, sahasrahla, magic bat, library, hobo, sick kid, bottle merchant, dwarf brothers, smith bros initial + tempering, witch, bombo merchant, bee merchant, mushroom→powder turn-in); every static-location pickup (master sword pedestal, ether tablet, bombos tablet, pyramid plaque, sanctuary chest); every dungeon-prize grant (heart container + crystal/pendant from boss); every event-flag grant that affects inventory (king zora flippers, zora's lake flippers, fat fairy upgrades, pyramid fairy upgrades); and every minigame chest (digging game, treasure chest minigame, hype cave, peg cave). Sites that are not item grants in the rando sense (e.g., `link_item_in_hand` clearing on holster, wishing-pond temporary-store-and-restore) SHALL be marked exempt in the audit with a documented reason.

#### Scenario: Vanilla path bit-identical when rando inactive
- **WHEN** `kFeatures1_RandomizerActive` is clear and the player opens a chest
- **THEN** `g_ram` after the grant is bit-identical to the pre-change binary's behavior, validated by replaying the per-chapter savestates

#### Scenario: Audit deliverable is checked in before implementation
- **WHEN** any task in section 6 of `tasks.md` (dispatch additions) begins
- **THEN** `audit.md` exists in the change folder enumerating every grant site, its file/line, its classification (grant / state-shuffle / cosmetic / progress), and its target location ID

#### Scenario: New grant site without dispatch fails the build
- **WHEN** a developer adds a write to `link_item_*` outside the dispatch layer without an audit-exemption comment
- **THEN** the audit check step fails the build with the file/line and a request to either route via dispatch or annotate exemption

### Requirement: Item types receivable via dispatcher

The dispatcher SHALL grant any item type listed in the audit deliverable's "receivable items" enumeration. Phase A receivable items include:

- **Progressive items** (mandatory for ALTTPR-style placement): `ProgressiveSword`, `ProgressiveShield`, `ProgressiveArmor`, `ProgressiveGlove`, `ProgressiveBow`. Each grant advances the corresponding `link_item_*` by one level via a new helper; at-max grants are a generator error in well-formed seeds.
- All vanilla absolute `link_item_*` items, **`SilverArrowUpgrade`** (standalone upgrade item used in absolute-bow mode).
- **`TriforcePiece`** — required by Triforce Hunt and Ganon Hunt; grant path adds 1 to the player's triforce-piece counter and updates the HUD.
- **Magic upgrades** as items: `HalfMagic`, `QuarterMagic` — the grant is **strictly progressive**: each magic upgrade advances `link_magic_consumption` by exactly one tier (capped at the maximum), regardless of which of the two items it is. **This port uses `0 = full, 1 = half, 2 = quarter`** (the cost table is indexed `item*3 + consumption`), NOT the vanilla-SNES `1/2/4` convention. Grant path: `magic_upgrade_direct_grant` plus the Magic Bat identity write. This deliberately diverges from ALTTPR (whose `QuarterMagic` jumps straight to quarter); capping at +1 tier wastes no pickup and cannot skip the half tier, and is placement-neutral because no logic predicate requires quarter specifically (the magic macro is satisfied at ≥ half).
- **Bottle-with-contents** as distinct item IDs: `BottleEmpty`, `BottleWithFairy`, `BottleWithBee`, `BottleWithGoodBee`, `BottleWithRedPotion`, `BottleWithGreenPotion`, `BottleWithBluePotion`.
- **Heart items** as two distinct IDs: `PieceOfHeart` (granted via the vanilla PoH path — 4 pieces = +1 max HP via the existing quarters mechanic) and `BossHeartContainer` (granted via a direct +1 max HP path, no quarters mechanic). The dispatcher routes each ID to the correct receive code.
- Small keys (per dungeon), big keys (per dungeon), maps, compasses.
- Multi-tier rupees: `Rupee1`, `Rupee5`, `Rupee20`, `Rupee100`, `Rupee300`.
- Junk pool: `SmallMagic`, `Arrow1`, `Arrow10`, `Bombs1`, `Bombs3`, `Bombs10`. **`Rupoor` is in the receivable enumeration but enters the pool only when `item_pool_difficulty ∈ {hard, expert}`** — the dispatcher still needs the grant path so a hard-pool seed actually works.
- Prize items (when prize shuffle is enabled): `Prize_Crystal1..7`, `Prize_GreenPendant`, `Prize_RedPendant`, `Prize_BluePendant`. Granted by clearing the dungeon to which prize shuffle assigned the prize; dispatch site is the boss-death code path in `dungeon.c`.

Item types not in the receivable enumeration SHALL NOT be placed by the generator.

#### Scenario: Small-key receive
- **WHEN** the dispatcher grants a small key from a chest in a dungeon other than that key's vanilla home
- **THEN** the small-key counter for the destination dungeon increments, a small-key receive animation plays (new code path; vanilla had no such animation), and the chest-cleared flag is set

#### Scenario: Multi-tier rupee receive — small tiers silent
- **WHEN** the dispatcher grants `Rupee1`, `Rupee5`, `Rupee20`, or `Rupee100` into any chest
- **THEN** Link's rupee counter increments by the documented value, the standard rupee-pickup SFX plays, and no item-receive cutscene fires

#### Scenario: Rupee300 plays the item-receive cutscene (ALTTPR convention)
- **WHEN** the dispatcher grants `Rupee300` (purple rupee — the canonical Misery Mire gift)
- **THEN** Link's rupee counter increments by 300, the full item-receive cutscene plays mirroring ALTTPR's behavior (player gets the "you found a purple rupee" beat), and the chest-cleared flag is set

#### Scenario: Progressive sword grant advances by one level
- **WHEN** the dispatcher grants `ProgressiveSword` while `link_item_sword == L2_MasterSword`
- **THEN** `link_item_sword` advances to L3, the appropriate receive animation plays, and HUD updates accordingly

#### Scenario: Progressive sword grant at max is a no-op or junk-fill
- **WHEN** the dispatcher grants `ProgressiveSword` while `link_item_sword == L4_GoldenSword`
- **THEN** the dispatcher records a generator error in the audit log (this case SHALL NOT occur for valid placements: the item pool contains exactly the right number of progressive items for the active mode); a debug-build asserts; release behavior is to silently grant a `Rupee5` placeholder so gameplay does not soft-lock

#### Scenario: TriforcePiece grants increment counter and HUD
- **WHEN** the dispatcher grants `TriforcePiece` and the player has `triforce_pieces < pieces_required`
- **THEN** the triforce-piece counter increments, the HUD updates, the goal predicate is re-evaluated, and the standard receive animation/SFX plays

#### Scenario: HalfMagic / QuarterMagic grant is strictly progressive
- **WHEN** the dispatcher grants a magic upgrade (`HalfMagic` OR `QuarterMagic`) while `link_magic_consumption == 0` (full)
- **THEN** `link_magic_consumption` advances to 1 (half) — the 1st magic upgrade collected is always half, regardless of which of the two items it is
- **AND WHEN** a second magic upgrade (`HalfMagic` OR `QuarterMagic`) is granted while `link_magic_consumption == 1`
- **THEN** it advances to 2 (quarter); any further magic-upgrade grant is capped at 2 (never exceeds quarter, never downgrades)

#### Scenario: PieceOfHeart vs BossHeartContainer routing
- **WHEN** the dispatcher grants `PieceOfHeart`
- **THEN** the vanilla PoH path runs (4-quarter mechanic; +1 max HP every 4 pieces)
- **AND WHEN** the dispatcher grants `BossHeartContainer`
- **THEN** `+1 max HP` is applied directly without consuming any piece-of-heart quarters

#### Scenario: Boss kill dispatches TWO locations
- **WHEN** the player defeats a dungeon boss (e.g., Helmasaur King in Palace of Darkness)
- **THEN** the boss-death code path calls `Rando_OnLocationCheck(PalaceOfDarkness_BossHeart, BossHeartContainer)` AND separately `Rando_OnLocationCheck(PalaceOfDarkness_Prize, Prize_Crystal_PoD_Vanilla)`, granting whatever the placement table has at each location ID

#### Scenario: Boss-heart slots are shuffled locations
- **WHEN** Phase A generates a seed
- **THEN** each of the 10 `<Dungeon>_BossHeart` slots is a normal shuffled drop location. The dispatcher still fires uniformly via the existing code path; if the placed item is `BossHeartContainer`, the boss kill behaves like vanilla, otherwise it grants the placed item.

#### Scenario: Great-fairy ponds grant two reach-only checks on contact (chest model)
- **WHEN** the player contacts a great-fairy pond — the Pyramid Fairy in the Dark World or the Waterfall of Wishing in the Light World — AND `kFeatures1_RandomizerActive` is set
- **THEN** the pond grants the next un-collected of its TWO checks DIRECTLY on contact, with no "throw an item in" interaction, no item-picker, and no consume/upgrade: Waterfall `waterfall_fairy_left` / `waterfall_fairy_right`, Pyramid `pyramid_fairy_left` / `pyramid_fairy_right`. This mirrors ALTTPR, which replaces both ponds with two chests. A receivable item runs the standard over-head receive; a direct-write item fires the §7.6 confirmation cue. Each pond's two checks are **reach-only** (no sword/bow/throwable-item requirement), so the runtime requirement equals the placement logic ("reach the pond")
- **AND** the Pyramid's vanilla "throw your sword/bow in to upgrade" Trade slots (`pyramid_fairy_sword`, `pyramid_fairy_bow`) are NOT used under rando and are RETIRED from the placement pool; the Waterfall has no such Trade slots
- **AND WHEN** `kFeatures1_RandomizerActive` is clear
- **THEN** the vanilla throw-in upgrade shrine runs unchanged and no `Rando_OnLocationCheck` fires

### Requirement: Starting-inventory injection atomicity

Starting-inventory injection SHALL run synchronously within a single `ZeldaRunFrame` call, with all per-item grants completing in the same tick. The runtime SHALL NOT call `ZeldaWriteSram` during injection. `kRam_RandoStartingInventoryGranted` SHALL be set in the same frame as the grants. This guarantees that a crash mid-injection cannot leave the game with a partially-applied starting inventory.

#### Scenario: Injection is atomic across a frame
- **WHEN** starting-inventory injection runs
- **THEN** every starting item is granted within the same `ZeldaRunFrame` invocation, no save points fire mid-injection, and `kRam_RandoStartingInventoryGranted = 1` is set in the same frame; a crash before that frame's completion leaves the slot in the pre-injection state (next boot re-runs full injection cleanly)

#### Scenario: Non-idempotent items don't double-grant
- **WHEN** the starting inventory includes additive counters (rupees, arrows, bombs) and a crash occurs mid-frame
- **THEN** because `kRam_RandoStartingInventoryGranted` was not set, the next boot re-runs injection from a clean state — no double-grant of rupees/arrows/bombs because no partial grant was committed

#### Scenario: Rupoor decrements wallet
- **WHEN** the dispatcher grants a `Rupoor`
- **THEN** Link's rupee counter decreases by the documented Rupoor value (or to zero if the wallet would go negative — the game floors at zero), the Rupoor-receive SFX plays, no item-receive cutscene fires

#### Scenario: 5th-bottle grant is refused at generation time
- **WHEN** the placement algorithm would create a placement that grants a bottle to a player who already has 4 bottles in their accumulating inventory snapshot
- **THEN** generation SHALL fail with a clear error before writing the spoiler; the dispatcher SHALL NEVER be asked to grant a 5th bottle at runtime
- **AND IF** the runtime nevertheless encounters a 5th-bottle grant (e.g., due to a downgrade-then-re-upgrade scenario or a corrupted placement table)
- **THEN** the dispatcher silently converts it to a `Rupee5` placeholder, logs a warning in debug builds, and gameplay continues

#### Scenario: Bottle substitute uses bottle-insertion path
- **WHEN** the dispatcher grants a bottle into a slot that vanilla would have granted (e.g.) a heart piece
- **THEN** the bottle-insertion code (`ItemReceipt_GiveBottledItem` equivalent) is invoked, finds the next empty bottle slot, and the new bottle appears in inventory — without routing through `kValueToGiveItemTo[bottle_id]` which only writes to `0xf35c`

#### Scenario: Non-receivable item rejected by generator
- **WHEN** the placement algorithm attempts to select an item not in the receivable enumeration
- **THEN** generation fails fast with a clear error naming the offending item

### Requirement: Prize-pack and drop substitutions

Random drops from grass, pots, and defeated enemies SHALL remain unrandomized in Phase A. The drop-pool shuffle (Phase B) SHALL, when enabled, apply substitutions via the same dispatch entrypoint, not by patching individual drop code paths.

#### Scenario: Default drops unaffected in Phase A
- **WHEN** randomizer mode is active in Phase A and a small heart drops from a pot
- **THEN** the heart is granted via the vanilla drop path with no dispatch override

### Requirement: Starting inventory injection

The placement layer SHALL grant the seed's configured starting inventory exactly once per new-save lifetime at the game-start hook. `Rando_TryGrantStartingInventory` SHALL be invoked at the end of `Module05_LoadFile` so the injection fires before any game frame runs against the new save.

**Cold-boot exploit guard**: the `kRam_RandoStartingInventoryGranted` cell at `g_ram[0x65e]` is OUTSIDE the SRAM-saved range (which covers `0xF000-0xF4FF`) and is re-zeroed by `ZeldaInitialize` on every cold boot. To prevent cold-boot reloads of in-progress saves from re-firing the injection, granters that write to additive HUD-filler registers (escape-fill arrows/magic/bombs) SHALL gate on a save-state-persisted signal — `sram_progress_indicator == 0` is the canonical brand-new-save check (advances to 1+ once Uncle's gift is received). Idempotent grants (e.g., Moon Pearl bit-set via `Link_ReceiveItem`) MAY skip this guard.

**Escape-fill**: when a sphere-0 weapon needs ammo (Bow / FireRod / CaneOfSomaria / CaneOfByrna / bombs-only) at any of the four sphere-0 chests (Link's House, Uncle, HC Map Chest, Secret Passage), the injection SHALL queue 70 arrows / 0x80 magic / 50 bombs into the HUD filler cells. Numbers mirror ALTTPR's `Rom.php` `EscapeRefills.Uncle.*` defaults. This is a one-shot stopgap; a faithful port of `World::setEscapeFills` (per-respawn refills hooked at Uncle's death / Zelda's cell / Sanctuary mantle, plus `rom.EscapeAssist` infinite-ammo toggle) is the v2 follow-up.

**In-session slot-switch reset**: `Rando_DeactivateSlot` SHALL clear `g_rando_starting_inventory_granted` so a user backing out of slot A (post-grant) and loading slot B in the same boot lets slot B's grant fire on its next `Module05_LoadFile`.

Inverted in particular requires this fix: Inverted Link receives `MoonPearl + MagicMirror` per the bunny-state contract. Without the call site, Inverted seeds are unplayable.

#### Scenario: Open mode starting inventory applied at new game
- **WHEN** world-state is Open and the player starts a new randomizer slot
- **THEN** Link begins with the Open-mode starting items reflected in `link_item_*` and the HUD, and `kRam_RandoStartingInventoryGranted` is set

#### Scenario: No double-grant within a single boot
- **WHEN** the player loads a randomizer slot, plays for a while, then reloads the same save without exiting the game
- **THEN** the starting-inventory injection does not run again because `kRam_RandoStartingInventoryGranted` was set on the first load and remains set for the duration of the boot

#### Scenario: No double-grant on cold-boot reload of in-progress save
- **WHEN** the player saves their Standard randomizer run past Uncle's gift, exits the program, restarts the program, and reloads the same slot
- **THEN** the escape-fill ammo grant does NOT re-fire, because the cold-boot exploit guard checks `sram_progress_indicator != 0` and short-circuits the filler-register writes
- **AND** the same-boot gate `kRam_RandoStartingInventoryGranted` is still set after this guard fires, so subsequent calls within the boot also short-circuit

#### Scenario: Standard escape-fill on a brand-new save with bow at Uncle
- **WHEN** the player starts a new Standard randomizer slot where the placement table puts a Bow at the Uncle's slot
- **THEN** `link_arrow_filler` (g_ram[0xF376]) is set to 70 so the HUD drain ticks arrows into `link_num_arrows` over the next few seconds during escape

#### Scenario: Inverted mode receives MoonPearl + MagicMirror
- **WHEN** world-state is Inverted and the player starts a new randomizer slot
- **THEN** Link begins with MoonPearl + MagicMirror in `link_item_*`, the HUD reflects them, and `kRam_RandoStartingInventoryGranted` is set in the same frame

#### Scenario: In-session slot switch re-fires the grant for the new slot
- **WHEN** the player loads slot A (escape-fill fires, gate=1), backs out to file-select, and loads slot B which is a brand-new save in the same boot
- **THEN** slot B's escape-fill fires because `Rando_DeactivateSlot` cleared the gate when the user backed out of slot A

### Requirement: Asset-data integrity (warn-not-block)

The host SHALL compute `g_assets_hash = SHA-256(asset_blob)` immediately after `LoadAssets()` returns. The settings screen SHALL warn — not refuse — the player when `g_assets_hash != kVanillaAssetsHash`. On first generation attempt with a non-vanilla hash, the settings screen SHALL display a dialog explaining the risk (cosmetic-only mods are safe; chest/NPC table mods may misbehave) with three choices: **Always allow** (persists `(asset_hash → allow)` in `zelda3.ini` `[randomizer] asset_hash_decisions`), **Allow once** (in-memory only), **Cancel**. The decision SHALL be keyed by hash so the dialog does not reappear for the same asset blob. The CLI mode SHALL NOT show the dialog; it bypasses unless `--assets-must-be-vanilla` is passed.

#### Scenario: Vanilla assets bypass the dialog
- **WHEN** `g_assets_hash` matches `kVanillaAssetsHash`
- **THEN** the dialog is not shown and generation proceeds normally

#### Scenario: Non-vanilla assets prompt once per unique hash
- **WHEN** the loaded asset blob does not match `kVanillaAssetsHash` and the player has not previously decided for this hash
- **THEN** the dialog is shown with three options; on "Always allow", the decision is persisted in `zelda3.ini`; on "Allow once", generation proceeds for this session only; on "Cancel", generation does not begin

#### Scenario: Persisted decision is honored
- **WHEN** the player previously chose "Always allow" for this asset hash and reopens the settings screen
- **THEN** the dialog does not reappear and generation proceeds

#### Scenario: asset_hash_decisions INI format
- **WHEN** the user has approved one or more non-vanilla asset hashes
- **THEN** `zelda3.ini` `[randomizer] asset_hash_decisions` stores the decisions as a comma-separated list of `<hex_hash>:<decision>` pairs (e.g., `asset_hash_decisions = abc123...:allow,def456...:allow`); the file is read at startup and decisions are loaded into memory; the list is unbounded but in practice contains at most a handful of entries

#### Scenario: CLI --assets-must-be-vanilla refuses non-vanilla blobs
- **WHEN** the CLI mode is invoked with `--assets-must-be-vanilla` and `g_assets_hash != kVanillaAssetsHash`
- **THEN** the process exits non-zero with a clear message before any generation work begins

### Requirement: Reachability-affecting event-flag bumps

The runtime SHALL bump `reachability_state_counter` not only on `Rando_OnLocationCheck` and direct `link_item_*` writes, but ALSO at every documented progression event-flag write site enumerated in `audit.md` §"Reachability-affecting events". Examples include but are not limited to: Aga 1 defeat, dungeon-boss-cleared flags, NPC-satisfied flags (sahasrahla, sick kid, magic-bat / mushroom→powder), pyramid-opened, master-sword-pulled, king's tomb item taken.

#### Scenario: Aga 1 defeat updates tracker reachability
- **WHEN** the player defeats Aga 1 and the Pyramid opens
- **THEN** `reachability_state_counter` bumps at the Aga-defeat code site, and the location-tracker's next-frame recompute discovers Pyramid Fairy as newly reachable (no inventory item picked up)

#### Scenario: Audit deliverable enumerates every event flag
- **WHEN** the Phase 0 audit is complete
- **THEN** `audit.md` §"Reachability-affecting events" lists every progression event flag the logic graph references, with the existing write site's file/line and a one-line patch adding the bump call

### Requirement: Retro TakeAny per-seed activation

In Retro world-state, the generator SHALL replicate ALTTPR's TakeAny activation model (per `app/Randomizer.php:716-735`) over the **31** `Shop\TakeAny` caves (ALTTPR declares 31 in `app/Region/Standard/**`; Inverted declares 0):

1. 4 caves are selected via deterministic pick-without-replacement (see `randomizer-shuffles`) and gain fixed inventory `BluePotion @ slot 0` + `BossHeartContainer @ slot 1`.
2. A 5th distinct cave is selected and gains a single-slot inventory: `ProgressiveSword` by default, OR `Rupee300` when `mode.weapons ∈ {swordless, vanilla}`.
3. The remaining 26 caves are NOT active for this seed (no redirect, no inventory).

Encoding is **Option B (active-only)**: each cave has 2 reserved LOC ids (266..327 = 31 × 2). Only active caves' slots emit into the placement table — 9 per seed (4 potion caves × 2 + 1 weapon cave × 1). Rewards are **role-pinned, not pool-shuffled** (take-any inventory is fixed). The player takes ONE offered item and the cave locks (runtime, see `randomizer-core`).

#### Scenario: Same seed produces same 4 + 5th TakeAny selection
- **WHEN** two `--generate-seed` invocations run with identical `(settings, seed_u64)` against Retro world-state
- **THEN** the 4 BluePotion+BossHeart caves AND the 5th weapon cave are byte-identical between the two runs

#### Scenario: mode.weapons toggles the 5th TakeAny inventory
- **WHEN** a Retro seed with `mode.weapons=randomized` is generated
- **THEN** the 5th active TakeAny carries `ProgressiveSword`
- **AND WHEN** the same seed is regenerated with `mode.weapons=swordless`
- **THEN** the 5th active TakeAny carries `Rupee300` (NOTE: vanilla/swordless weapon modes are reserved/unreachable in current Phase B; this scenario activates once those modes land)

#### Scenario: Inactive TakeAny caves are not in the placement table
- **WHEN** a Retro seed is generated
- **THEN** the placement table contains exactly 9 TakeAny slot entries (4 caves × 2 + the 5th cave × 1), not 62

### Requirement: Retro regular-shop inventory (superseded — identity-placed in Slice 3a)

This change SHALL NOT modify the regular-shop inventory: the 9 regular Retro shops MUST remain identity-placed with their vanilla inventory as shipped by Slice 3a (`add-rando-retro-world-state`), and SHALL NOT be given per-seed `randomCollection(5)` extras. This supersedes the earlier draft of this requirement.

Rationale: ALTTPR's `randomCollection(5)` regular-shop extras (`app/Randomizer.php:737-750`) add `ShopArrow` only under `rom.rupeeBow`, `ShopKey` only under `rom.genericKeys` (both out of scope — see `design.md` §8), and `TenBombs` unconditionally. Slice 3a chose to model the 9 shops as identity-placed vanilla inventory ("the randomization is that the player must find shops + pay rupees, not that shop inventory is shuffled"), which already shipped in the corpus. This change does NOT re-open that decision; it touches TakeAny only. See `design.md` §6.

#### Scenario: Regular Retro shops keep vanilla inventory
- **WHEN** a Retro seed is generated
- **THEN** each of the 9 regular shop slots holds its vanilla item (identity-placed by Slice 3a), and this change adds no regular-shop extras

### Requirement: Boss-heart-container pool semantics and logic safety

The legacy `region_boss_hearts_in_pool` settings axis SHALL be accepted for old
CSV/share compatibility but canonicalized to `0` and ignored by placement. Each
of the 10 `<Dungeon> - Boss` Drop locations (`type == Drop`,
`vanilla_item == BossHeartContainer`/`51`) SHALL remain a free assumed-fill
target. The current item-pool difficulty's `BossHeartContainer` count SHALL
always enter the item pool (10 Easy/Normal, 6 Hard, 2 Expert), where they
participate in the general fill. Players who want boss hearts pinned SHALL use
Customizer pins.

To keep assumed-fill sound when the boss slots are unpinned, every `<Dungeon> -
Boss` Drop location's `can_reach` predicate SHALL require defeating that dungeon's
boss — i.e. include the dungeon's `CanKill<Boss>()` macro plus the items needed to
reach and open the boss room — in both the Standard and Inverted logic graphs.
No boss Drop location's `can_reach` may be `TRUE()` or otherwise omit the
boss-kill requirement.

#### Scenario: Boss-heart slots join the assumed-fill pool

- **WHEN** any seed is generated
- **THEN** the 10 boss Drop slots are free placement targets, non-heart items may
  be placed there, and the item-pool difficulty's `BossHeartContainer` count is
  placed by assumed fill

#### Scenario: Legacy pinned value is ignored

- **WHEN** `region_boss_hearts_in_pool` is loaded as non-zero from an old input
- **THEN** canonical settings and placement still behave as `0`

#### Scenario: Boss Drop reachability requires the boss kill

- **WHEN** the assumed-fill placer evaluates reachability for any `<Dungeon> -
  Boss` Drop location with the slots unpinned
- **THEN** that location is reachable only when the inventory satisfies the
  dungeon's boss-kill predicate (the `CanKill<Boss>()` macro plus reach/open-room
  items), so progression is never stranded behind an unbeatable boss

### Requirement: Direct-grant visible confirmation ancilla

When the dispatcher returns `kRandoLttpSkip` (direct-grant items: `HalfMagic`, `QuarterMagic`, `TriforcePiece`, prize bits, dungeon-item bits), the call site SHALL invoke `Rando_ShowDirectGrantConfirmation(item_id)` — extending the Phase A signature (`void`) to take the granted item id. The implementation SHALL spawn a per-item-type icon ancilla — drawn above Link in the same screen-position style as the vanilla item-receipt ancilla — for every direct-grant item that has a vanilla receive-sprite bundle. Items with no such bundle to borrow (the magic upgrades and the Triforce piece) SHALL keep the Phase A audio + HUD-only behavior via the `gfx == 0` audio-only sentinel. Audio + HUD refresh behavior from Phase A §7.6 SHALL be preserved; the visible ancilla is added alongside, not replacing.

Items SHALL be mapped to tile addresses via `assets/rando/direct_grant_icons.yaml`, which the codegen pipeline emits as `src/rando/direct_grant_icons.h` (a `static const DirectGrantIconEntry kDirectGrantIcons[]` table indexed by item id). Icons SHALL reference existing tiles in the bundled graphics blob (HUD/inventory tiles); no new sprite art SHALL be added.

#### Scenario: Prize grant pops the matching prize icon
- **WHEN** the player obtains a dungeon prize (a pendant or crystal) or a per-dungeon item (Big Key / Map / Compass) from a §6.x direct-grant site (e.g., a chest, tablet, or NPC grant placed there by the shuffler), excluding the boss-prize drop — which shows the recolored falling-prize sprite instead and deliberately suppresses this icon to avoid a duplicate visual
- **THEN** the standard item-receipt sound plays, the HUD refreshes, and an item-receipt ancilla draws the matching item icon above Link's head (for pendants, in the correct green / red / blue palette)

#### Scenario: Tablet pickup shows visible feedback
- **WHEN** the player strikes the Ether tablet (`player.c:594`) or Bombos tablet (`player.c:634`) and the dispatched item is a direct-grant item
- **THEN** in addition to the screen flash and magic consumption, a per-item icon ancilla draws above Link

#### Scenario: Pure UX — no kGeneratorVersion bump
- **WHEN** this change ships and the player generates a seed that previously generated cleanly
- **THEN** the placement output is byte-identical (`placement_digest_hex` unchanged) — the icon table affects only the visible animation path

#### Scenario: Icon lookup table is built into the binary
- **WHEN** the build runs
- **THEN** `assets/rando/direct_grant_icons.yaml` is consumed by `assets/rando_logic_gen.py`, producing `src/rando/direct_grant_icons.h`; the header is registered in `Makefile`, `Zelda3.vcxproj` pre-build, and `src/platform/switch/Makefile`; `assets/scripts/check_codegen_wiring.py` asserts the registration consistency across all three build systems

#### Scenario: Item with no receive-sprite bundle falls back to audio-only
- **WHEN** a direct-grant call site passes an item whose `kDirectGrantIcons` entry has `gfx == 0` — the audio-only sentinel, which covers both items absent from the table (zero-initialized) and items deliberately left audio-only because no vanilla receive-sprite bundle exists to borrow (`HalfMagic`, `QuarterMagic`, `TriforcePiece`)
- **THEN** the helper plays the audio + HUD-refresh portion only (Phase A behavior) and does NOT spawn a draw-tile ancilla

### Requirement: Direct-grant confirmation call sites enumerated

Every call site that invokes the dispatcher and may receive `kRandoLttpSkip` SHALL invoke `Rando_ShowDirectGrantConfirmation(item_id)` in the skip branch. The Phase A call-site set, with item ids passed at each site, SHALL be:

- `src/player.c:594` (Ether tablet) — passes the item id resolved from the tablet's placement-table entry.
- `src/player.c:634` (Bombos tablet) — passes the item id resolved from the tablet's placement-table entry.
- `src/player.c:3886` (generic direct-grant cue from the player module) — passes the granted item id from the call-site's local context.
- `src/sprite_main.c` `Sprite_WishPond3` (great-fairy pond grant, §6.7) — passes the placement-table-resolved item id for the pond's granted check. (The Pyramid `Sword`/`Bow` Trade slots originally enumerated here were retired and the grant relocated to a chest-model contact grant of `*_Fairy_Left`/`Right` by `add-rando-fairy-chest-model`; the §7.6 cue still fires from the relocated site.)
- `src/sprite_main.c:18586` (generic direct-grant cue from a sprite handler) — passes the granted item id from the call-site's local context.

When this change lands, all 5 call sites SHALL be updated in the same commit; no partial migration is permitted. New call sites added by future changes (e.g., Slice 8 minigame dispatch) inherit the same contract.

#### Scenario: Adding a new direct-grant site without passing the item id fails CI
- **WHEN** a developer adds a `Rando_ShowDirectGrantConfirmation()` (zero-arg form) call after this change lands
- **THEN** the compile fails because the signature no longer accepts zero arguments

### Requirement: Shop-handler dispatch routing

Shop-purchase sites SHALL route through `Rando_OnLocationCheck(LOC_<Shop_Slot>, vanilla_item_id)` instead of granting the shop's vanilla item inline. The primary dispatch site is `src/sprite_main.c:25308` (`ShopItem_HandleReceipt`, per `audit.md §0.1.4` shop subsystem enumeration). The dispatcher resolves the slot to the placement-table entry; the rupee cost SHALL remain under vanilla shop-pricing semantics (Phase B does not randomize shop prices).

When `kFeatures1_RandomizerActive` is clear (vanilla mode), shop handlers SHALL preserve byte-identical vanilla behavior — the dispatcher fall-back path returns the vanilla item id and the inline grant proceeds.

For shops outside the Retro placement pool (Open / Standard / Inverted seeds — where shops grant vanilla items), the dispatcher SHALL fall back to vanilla via the absent-from-table path; the audit-guard remains green because the dispatch fires uniformly regardless of placement-pool membership.

#### Scenario: Rupee cost preserved
- **WHEN** the player purchases at a shop in a Retro seed
- **THEN** rupees are deducted at the vanilla shop-pricing rate; the granted item is the placement-table substitute; the shop-purchase animation runs as in vanilla

#### Scenario: Vanilla mode shop unchanged
- **WHEN** the binary is in vanilla mode (`kFeatures1_RandomizerActive` clear) and the player purchases at any shop
- **THEN** the shop grants its vanilla item via the standard inline path; `g_ram` after the purchase is bit-identical to pre-rando-change behavior

#### Scenario: Non-Retro rando mode shops unchanged
- **WHEN** a non-Retro rando seed (Open / Standard / Inverted) is loaded and the player purchases at a shop
- **THEN** the dispatcher fires but the shop slot is not in the placement table; fall-back returns the vanilla item id; the standard inline grant proceeds

#### Scenario: Capacity upgrades dispatch identity
- **WHEN** the player buys a bomb-capacity upgrade in a Retro seed (`sprite_main.c:11483-11484`)
- **THEN** `Rando_OnLocationCheck(LOC_<CapacityUpgradeSlot>, ITEM_BombCapacityUpgrade)` fires; the placement table returns the identity substitute (`ITEM_BombCapacityUpgrade`); the vanilla capacity-upgrade write to `link_bomb_upgrades` proceeds

### Requirement: Take-Any cave shop dispatch

Take-Any caves (`20 Rupee Cave`, `50 Rupee Cave`, `Bonk Fairy (Light)`, `Bonk Fairy (Dark)`, `Desert Fairy`, `Good Bee Cave`, `Light Hype Fairy`, `Long Fairy Cave`, and any other `Shop\TakeAny` instance enumerated in ALTTPR) SHALL route through the same dispatcher entry point as standard shops, OR a peer entry if the Take-Any sprite handler is a different code path.

The decision (single entry vs. peer entry) is recorded in design.md after apply-time `src/sprite_main.c` grep against the Take-Any sprite-handler code. The spec's normative requirement is "Take-Any purchases are dispatched"; the implementation choice is open.

#### Scenario: Take-Any cave in Retro grants placement-table substitute
- **WHEN** the player enters `20 Rupee Cave` in a Retro seed and accepts the offer
- **THEN** dispatch fires with `LOC_<TakeAny_20RupeeCave>` (or similar id); the placement-table substitute is granted; the 20-rupee cost is deducted at vanilla rate

#### Scenario: Take-Any cave in Open is not enterable
- **WHEN** the player walks up to the entrance of `20 Rupee Cave` in an Open seed
- **THEN** the entrance does not accept the player; vanilla behavior holds (Take-Any caves are gated by `region.takeAnys = false` in Open)

### Requirement: Shared-byte item grants — never-downgrade, progressive collapse, and item-menu swap

Several items that vanilla packs two-or-more tiers into a single Y-slot byte are shuffled by the randomizer as independent items. Under `kFeatures1_RandomizerActive`, the grant path SHALL NOT let acquiring a lower tier after a higher one downgrade the slot, and SHALL track true ownership independently of the shared byte so a player who owns multiple tiers never loses one. Specifically:

- **Never-downgrade (all shared-byte upgrade items).** An absolute byte-write SHALL only raise the byte (write iff `v > current`), never lower it — protecting sword, shield, gloves, mail, bow, and boomerang from out-of-order downgrades. This clamp SHALL EXEMPT non-tier counter bytes: `link_arrow_filler` (receive codes `0x43`/`0x44`) is a per-frame drain countdown, not a tier, and SHALL keep the vanilla absolute write (the clamp would otherwise silently drop a paid arrow grant while a prior fill is still draining).
- **Progressive collapse (boomerang, magic).** Where the two shuffled tiers are **logically interchangeable** (no logic predicate distinguishes them), the grant SHALL collapse them into a single progressive ladder: the 1st collected gives tier 1, the 2nd gives tier 2, regardless of which item is placed. Boomerang (blue = 1, red = 2) and magic (half = 1, quarter = 2) are collapsed this way.
- **Item identity preserved when a tier is a logic gate (bow).** Where a logic predicate DOES distinguish the tiers — silver vs wood arrows for the bow — the grant SHALL keep item identity (a wood-bow item grants wood, a silver-arrow item grants silver) under never-downgrade, so the placer cannot desync from runtime.
- **Item-menu swap (Press A).** When the player owns BOTH tiers of a shared-slot item, pressing A on that highlighted slot in the inventory menu SHALL swap which tier the slot performs (the shared byte tracks the *selected* tier): flute↔shovel, blue↔red boomerang, wood↔silver arrows. Ownership SHALL persist across save/reload via per-item bitfields carried additively in the slot-header reserved tail (`boomerang_owned` @73, `bow_owned` @74 — no `format_version` bump), so a swapped-down byte never loses the higher tier.

#### Scenario: Progressive boomerang ignores item identity
- **WHEN** the dispatcher grants either a `BlueBoomerang` or a `RedBoomerang` item while the player owns no boomerang
- **THEN** the player receives the blue boomerang (tier 1), regardless of which item was placed
- **AND WHEN** a second boomerang item (either color) is granted
- **THEN** the player receives the red boomerang (tier 2) and owns both; pressing A on the boomerang slot swaps the selected color

#### Scenario: Bow keeps identity and never downgrades
- **WHEN** the player owns the silver bow and the dispatcher grants a wood-bow item
- **THEN** the bow strength is unchanged (stays silver) — never downgraded
- **AND WHEN** the player owns both wood and silver
- **THEN** pressing A on the bow slot swaps between wood and silver arrows, preserving the arrow-present state

#### Scenario: Never-downgrade exempts the arrow-fill counter
- **WHEN** the dispatcher grants an arrow refill (receive code `0x43`/`0x44` → `link_arrow_filler`) while a prior fill is still draining
- **THEN** the new fill value is written absolutely (vanilla behavior), NOT clamped — the never-downgrade rule does not apply to drain/counter bytes

### Requirement: Trigger-based location re-collect safety

A location whose grant fires from a re-triggerable in-world action (an NPC summon, a dig, a tablet read) SHALL, under `kFeatures1_RandomizerActive`, gate its grant on `Rando_IsLocationChecked(LOC_*)` so the shuffled item is granted exactly once. Vanilla often relied on the grant's own side effect to disable re-triggering; the randomizer breaks that equivalence in two directions the gate fixes: (a) a vanilla precondition that, once satisfied from elsewhere, makes the location uncollectable (MISSABLE); (b) a re-enabled one-shot action that re-runs the non-idempotent grant (RE-GRANT / duplicate). The non-rando path SHALL remain byte-identical (RAM-compare preserved).

#### Scenario: Magic Bat is collectable regardless of magic level
- **WHEN** the player reaches the Magic Bat having already obtained quarter magic (`link_magic_consumption == 2`) from another location
- **THEN** the bat still appears and grants its shuffled item — the vanilla `link_magic_consumption >= 2` summon guard applies only off-rando; under rando `Rando_IsLocationChecked(LOC_Magic_Bat)` is the sole re-grant gate

#### Scenario: Flute Spot grants exactly once
- **WHEN** the player digs the Flute Spot, collects its shuffled item, then (via the flute/shovel decouple) toggles back to the shovel and re-digs the same tile
- **THEN** the re-dig grants nothing — the grant is gated on `!Rando_IsLocationChecked(LOC_Flute_Spot)`, preventing the duplicate that vanilla avoided only by flipping `link_item_flute` out of a shovel-selectable state

### Requirement: Assumed-fill awareness of per-seed dungeon key-door logic

The assumed-fill placer SHALL see the per-seed door layout through the wrapped
location predicates (the `OP_DOORS_*` oracle from `randomizer-logic` — per-door
worst-case key thresholds + item gates), AND SHALL honor the prover's
`bk_restricted` set as a **big-key placement ban** evaluated alongside
`dungeon_mode_accepts_item` (`DoorShuffle_BkRestricted` against the installed
layout): the big key SHALL NOT be placed at any location the prover marks as
reachable only past the dungeon's big-key door — a self-locking placement the
reachability gate alone cannot catch. The ban is inert when no layout is
installed. In-dungeon containment SHALL remain consistent with the generated
layout: the committed scope forces `dungeon_small_keys_mode == Dungeon` AND
`dungeon_big_keys_mode == Dungeon` (normalized in `apply_derived_rules`, see
`randomizer-shuffles`), so containment + the `bk_restricted` ban together keep the
big key beatably placed. The generation pipelines SHALL wrap placement in a
`door_attempt` retry loop: a layout whose placement or accessibility check fails
tries the next attempt. Placement determinism SHALL be preserved
(`budget_seconds = 0`). When `door_shuffle == vanilla`, placement is
byte-identical to the baseline.

#### Scenario: Placer respects per-seed key thresholds

- **WHEN** door shuffle is active and the placer evaluates a dungeon location
- **THEN** the wrapped predicate queries the oracle under the seed's layout (not
  the vanilla key count), so the assumed-fill certification matches what the
  player can actually reach

#### Scenario: In-dungeon key containment under shuffle

- **WHEN** `door_shuffle == basic` with the forced in-dungeon small + big keys
- **THEN** each dungeon's small keys are placed only within that dungeon,
  consistent with the door layout, and the seed remains beatable

#### Scenario: Big key is never placed behind its own big-key door

- **WHEN** the prover marks a set of dungeon locations as reachable only past the
  big-key door (`bk_restricted`)
- **THEN** the placer forbids the big key from every such location, so no seed
  locks the big key behind the door it opens

#### Scenario: Failed placement tries the next door attempt

- **WHEN** a generated layout passes the prover but assumed-fill or the
  accessibility tier rejects the resulting placement
- **THEN** the pipeline uninstalls the layout, bumps `door_attempt`, and retries
  (bounded), persisting the accepted attempt + digest with the slot

#### Scenario: Disabled door shuffle preserves placement

- **WHEN** `door_shuffle == vanilla`
- **THEN** the placer never consults the door oracle or the ban, and placement is
  byte-identical to the baseline (regression corpus digests unchanged)

### Requirement: Telepathic-tile hint dispatch

The randomizer SHALL surface generated hints in-game by intercepting the vanilla dialogue read, NOT by carving a dynamic dialogue-ID range. `Text_LoadCharacterBuffer` (`src/messaging.c`) SHALL call `Rando_RenderHintMessage(dialogue_message_index, messaging_text_buffer)` before the vanilla dialogue decode; when the randomizer slot is active, `settings.hints == on`, and `dialogue_message_index` is one of the 15 hint-bearing vanilla US telepathic-tile message ids (`0xB5, 0xB8, 0xB9, 0xBA, 0xBB, 0xBE, 0xBF, 0xC0..0xC7`; `0xB4` generic-default excluded), the function SHALL render the generated hint (font-encoded, `0x7f`-terminated) into the buffer and the engine SHALL skip the vanilla decode.

The same intercept ALSO reroutes the fork-extension NPCs through `Rando_RenderHintMessage`: the Storyteller's paid-tip message ids (`0xFF, 0x101, 0x102, 0x103`) and the Fortune-Teller reading ids (`0xEA..0xF1, 0xF6..0xFD`, mapped to the Kakariko or Dark-World hint by the current world bit `(savegame_is_darkworld >> 6) & 1`) render their generated hints the same way. The Lake-Hylia Fortune Teller shares the Kakariko room with no runtime discriminator, so it surfaces the Kakariko hint.

When the slot is inactive, hints are off, or the id is not a hint-tile id, `Rando_RenderHintMessage` SHALL return false and the vanilla text-engine flow SHALL proceed byte-identically.

`Rando_GetHintDialogueId(npc) → uint16` (returning `0x200 + (npc-1)`, or `0xFFFF` when no hint is allocated) SHALL exist and is consumed by the **spoiler emitter** as the entry's `dialogue_id` label. It is NOT consulted by any in-game sprite handler.

> **As-built note**: an earlier draft had hint NPC *sprite handlers* (Sahasrahla, storyteller, bookshelf, Murahdahla) invoking `Rando_GetHintDialogueId` to dispatch a slot-specific dialogue id from a carved dynamic range. The implementation instead intercepts the vanilla message ids in the messaging engine, and `Rando_GetHintDialogueId` survives only as a spoiler label. The Storyteller and the Kakariko/Dark-World Fortune Tellers ARE wired in-game through this same intercept (on their own message ids, above); the bookshelf was dropped and Murahdahla is spoiler-only (the fork has no Murahdahla sprite). `Rando_RemapTeleMsg` exists but is a vestigial unused stub.

#### Scenario: Telepathic tile in rando mode renders the generated hint
- **WHEN** the player reads a hint-bearing telepathic tile in an active randomizer slot with `hints=on`
- **THEN** `Rando_RenderHintMessage` returns true and the message box shows the slot's generated hint instead of the vanilla telepathic text

#### Scenario: Vanilla mode tiles unchanged
- **WHEN** no randomizer slot is active (or `hints=off`) and the player reads a telepathic tile
- **THEN** `Rando_RenderHintMessage` returns false and the standard vanilla text plays byte-identically

### Requirement: §6.8 Minigame dispatch sites

Four minigame reward sites SHALL route their reward through the rando dispatcher
(`Rando_OnLocationCheck` / `Rando_DispatchVanillaGrant`) so a seed can place any
item at the site, and SHALL preserve byte-identical vanilla behavior when
`kFeatures1_RandomizerActive` is clear:

1. **`LOC_Digging_Game`** (location id 228) — the Digging Game. Dispatched at the
   PoH "win" outcome in `DiggingGameGuy_AttemptPrizeSpawn` (`src/player.c`); on a
   direct-grant placement the `0xeb` reward sprite is suppressed so the player
   doesn't also receive the vanilla Piece of Heart.
2. **`LOC_Hype_Cave_NPC`** (location id 227) — the gift NPC in Hype Cave (the 4
   Hype Cave chests are wired separately via the §6.3 universal chest hook).
   Dispatched in `NiceThiefWithGift` (`src/sprite_main.c`), gated on the full
   16-bit room index `0x11E` and passing the `0xFFFF` registry-id convention so a
   placed item can't mis-grant the vanilla 300-rupee code.
3. **`LOC_Hammer_Pegs`** (location id 218) — the hammer-pegs Piece of Heart.
   Dispatched at the once-per-save 22nd-peg trigger in `HandlePegPuzzles`
   (`src/overworld.c`, screen 98); the obtained-bit is set before the tile reveal
   so the vanilla standing Piece of Heart self-cancels.
4. **`LOC_Chest_Game`** — the Treasure-Chest minigame in the Village of Outcasts.
   Dispatched at the rare-prize branch of `OpenMiniGameChest` (`src/dungeon.c`),
   which fires once per save (a `dung_savegame_state_bits` gate). The fork models
   the game as this single rare-prize location, not three placement slots.

#### Scenario: Digging Game routed through dispatcher
- **WHEN** the player wins a Digging Game dig in a rando slot
- **THEN** `Rando_DispatchVanillaGrant(LOC_Digging_Game, ...)` fires; the placed
  item is granted and the vanilla Piece-of-Heart reward sprite is suppressed on a
  direct-grant placement

#### Scenario: Hammer Pegs and Hype Cave NPC route through the dispatcher
- **WHEN** the player hammers the last peg (Hammer Pegs) or receives the Hype
  Cave gift NPC's item in a rando slot
- **THEN** the placed item for `LOC_Hammer_Pegs` / `LOC_Hype_Cave_NPC` is granted
  once, and the corresponding vanilla reward does not also spawn

#### Scenario: Vanilla mode minigames unchanged
- **WHEN** the binary is in vanilla mode (`kFeatures1_RandomizerActive` clear) and
  the player wins any of the four minigames
- **THEN** the minigame grants its vanilla item; `g_ram` after the grant is
  bit-identical to pre-rando-change behavior

#### Scenario: Treasure-Chest minigame dispatches its single reward location
- **WHEN** the player wins the once-per-save rare prize in the Treasure-Chest
  minigame
- **THEN** dispatch fires once for `LOC_Chest_Game`; consolation outcomes stay
  vanilla

### Requirement: Door-shuffle placement model with active pot checks

Placement SHALL treat active pot checks as ordinary fillable locations under door
shuffle, subject to the selected pot tier and dungeon-item containment.

When door shuffle is active, the door key prover SHALL model the active pot locations
before placement begins. A dungeon's possible small-key sources SHALL be:

- static door-table item locations,
- active non-empty pot locations in that dungeon,
- non-pot drop-key rows that remain free in-context.

Itemized key-pot drop rows SHALL be excluded from free-drop accounting so a key under
an active pot is never counted both as a placement item and as a free drop.
Empty pots in the `all` tier SHALL be pre-pinned to `ITEM_Nothing` and SHALL NOT
increase the prover's possible small-key source count.

The free-drop exclusion SHALL be shared by every door-explorer call path that counts
reachable drops, including placement proving, runtime logic-oracle reachability,
door self-tests, and key-depth dumping.

Small-key-source counts and big-key-availability counts SHALL be distinct. If active
pots are rejected as big-key placements, the prover SHALL also exclude them from
big-key availability. If active pots are counted as big-key-capable locations, the
layout SHALL emit digest-covered `bk_restricted` coverage for the exact pot loc ids
that placement must reject.

#### Scenario: Key pot counted as a location, not a free drop

- **WHEN** a door+pot seed itemizes a dungeon's vanilla pot key
- **THEN** the key-door prover counts the pot as an active key-source location
- **AND** the matching drop-key row does not increase the free-drop key count

#### Scenario: Non-pot drop remains free

- **WHEN** a dungeon has a vanilla small-key drop that is not an active pot key
- **THEN** the prover and logic oracle keep counting it as a free in-context drop

#### Scenario: Logic oracle also excludes itemized pot drops

- **WHEN** active door+pot logic evaluates a key-door threshold
- **THEN** reachable itemized key-pot drop rows do not increase the free-drop key
  budget
- **AND** non-pot drop rows still do

#### Scenario: Empty pot is not a key-source location

- **WHEN** `pot_shuffle=all` activates an empty pot under door shuffle
- **THEN** that pot is reachable and checkable as `ITEM_Nothing`
- **AND** it does not increase the key-door prover's available key-source count

#### Scenario: Big-key placement and prover counts agree

- **WHEN** placement rejects a dungeon big key from an active pot location due to a
  door-shuffle big-key restriction
- **THEN** the prover did not count that location as available for satisfying the
  big-key-open branch

#### Scenario: Assetless build fails closed

- **WHEN** a binary was built without generated pot locations and a user requests
  `door_shuffle=basic,pot_shuffle=keys`
- **THEN** generation fails with the same pot-registry-missing error used for
  non-door pot seeds
- **AND** it does not silently normalize pots off

### Requirement: Pot locations in assumed-fill with raised capacity and pot dispatch

When the `pot_shuffle` tier selects them, pot locations SHALL enter the assumed-fill
location pool like any other location; the remaining pot slots are filled by the
existing junk padder. **Pot-key locations are dungeon small-key locations that follow
the dungeon's `dungeon_small_keys_mode`** — identity-pinned to the pot in vanilla key
mode, an open shuffled slot otherwise — but a pot key is an ADDITIONAL key the pool
must carry, NOT one of the fixed vanilla *chest* keys (`kVanillaSmallKeyCounts` counts
chests only). Its full economy (separate pooling, the in-dungeon vs world distribution,
and the non-pot-drop free-grant) and the in-context key-door logic gating are specified
in `Pot-key small-key economy` (this capability) and `randomizer-logic / Pot-key
small-key logic gating`. **Empty-pot locations
SHALL be filled with `ITEM_Nothing` in a dedicated pre-pass** — removed from the open
set before assumed-fill and junk padding (like vanilla-dungeon-item pre-placement) —
so `ITEM_Nothing` can never land on a real location and no real item lands on an
empty pot; it is NOT a free entry in the junk rotation. Out-of-scope pots (tier excludes
them, or `pot_shuffle = Off`) SHALL be skipped in the open-location collection loop
and the junk-pad target loop (mirroring the inactive-Take-Any skip) so they draw no
fill RNG and do not enter the placement table or digest; at runtime they resolve to
vanilla via the dispatcher's no-placement fall-back — **absent from the table, or the
`0xFFFF` sentinel only when present below a higher placed id** (`Dispatcher signature
and fall-back behavior`). **Every** location-id-keyed capacity across the randomizer
module SHALL be raised to a single 2048 ceiling (`328 + 799 = 1127`) by a **typed
audit, NOT a `512` grep** — `1127` exceeds BOTH the 512 caps (placer working arrays
+ session buffer + in-memory checked-bitmap; the placement-digest cap `kDigestLocalCap`
+ its buffer, which otherwise silently TRUNCATES the digest at 512; the reachability
OOB guards) AND the 1024 caps (the auto-tracker / native-tracker / reach-panel
`s_loc_*[1024]` tables; the customizer probe cap; `kSpoilerMaxRows`; and
`rando_snapshot_tail`'s `raw[1024]` + `> 1024` reject — past which locations are
silently DROPPED). Each raised capacity SHALL carry a `_Static_assert` tying it to
`LOC__COUNT` (≥) so a future overflow / truncation / drop is a build break, not a
silent fail-open.

Cave-entrance shuffle SHALL normalize `pot_shuffle` entirely to `Off` through
`Settings_PotShuffleForcedOff` (= `Settings_EffectiveShuffleCaveEntrances`, honored only
on Open/Standard): `apply_derived_rules` sets the canonical copy to Off, and
`pot_active()` returns false for EVERY pot (key and non-key) in that case. A
cave-entrance+pots seed is byte-identical to the same seed without pots, preventing a
cave/house pot from being certified against its vanilla overworld region while the
runtime reaches the interior through the shuffled entrance.

Door shuffle SHALL NOT be part of `Settings_PotShuffleForcedOff`: door-shuffled pot
locations remain active when selected, and their key economy/reachability is governed by
the baseline requirements `randomizer-pot-sanity / Pot keys are first-class shuffled
checks under shuffled key modes`, `randomizer-placement / Door-shuffle placement model
with active pot checks`, and `randomizer-logic / Door-shuffle reachability via static
oracle ops`. **The placer and logic consume RAW (non-normalized) settings** —
`Settings_CanonicalSerialize` runs `apply_derived_rules` only on a private copy for the
settings hash — so EVERY pot/accessibility/door predicate that branches on an effective
field SHALL read it through the matching accessor (`Settings_PotShuffleForcedOff`,
`Settings_PotKeysActive`, `Settings_EffectiveDoorShuffle`,
`Settings_EffectiveShuffleCaveEntrances`, `Settings_EffectiveAccessibility`), never the
raw struct field, so the placer cannot diverge from the canonical hash / spoiler /
runtime (the audit-fixed raw-vs-normalized bug class — cave-forced-off pot-key gates,
`goal=completionist,accessibility=none` skipping the 100%-locations walk, and
Inverted/Retro retaining a cave bit but wrongly forcing pots off). The runtime pot grant
SHALL dispatch through a single point keyed on
`(dungeon_room, tile_position) → location_id` (`randomizer-pot-sanity / Single-point
runtime pot dispatch …`), subject to the existing `Trigger-based location re-collect
safety` invariant: a checked pot is never re-granted. `Placement_Lookup` SHALL use a
sorted table + binary search; the sorted invariant SHALL hold at EVERY install
boundary (assumed-fill output, sidecar deserialization, snapshot-tail reinstall,
customizer, race/spoiler reveal, tests), enforced by a `--rando-selftest` sortedness
check and a sort-on-install fallback.

#### Scenario: Pot key follows the dungeon key mode
- **WHEN** `pot_shuffle` includes a small-key pot
- **THEN** in vanilla key mode the pot-key is identity-pinned to the pot (not pooled,
  drops its own key in place); in a shuffled key mode it is an open slot whose vanilla
  small key enters the item pool and is placed logic-aware (`Pot-key small-key
  economy`) — under wild keys into the world pool, under dungeon keys confined to its
  own dungeon

#### Scenario: Capacity covers the maximal pool
- **WHEN** `pot_shuffle = All` in a Retro seed (the largest combined pool)
- **THEN** the placement working arrays, session buffer, and checked-bitmap hold
  every location without overflow, and the capacity `_Static_assert` passes at
  build time

#### Scenario: Out-of-scope pot falls back to vanilla
- **WHEN** a pot is not selected by the active tier and the player breaks it
- **THEN** it has no placement entry (or a `0xFFFF` sentinel) and the dispatcher
  reveals the vanilla content, with no glint and no check

#### Scenario: Dispatch stays cheap at scale
- **WHEN** a pot is broken and the runtime resolves its placed item
- **THEN** `Placement_Lookup` resolves via binary search (not an O(N) linear scan),
  so frequent pot-breaks do not degrade frame timing at ~1127 locations

#### Scenario: ITEM_Nothing is pre-placed, never on a real location
- **WHEN** `pot_shuffle = All` and the placer runs
- **THEN** empty-pot locations are filled with `ITEM_Nothing` in the dedicated
  pre-pass and removed from the open set, so assumed-fill and junk padding never place
  `ITEM_Nothing` on a chest/non-empty pot nor a real item on an empty pot

#### Scenario: Cave-entrance shuffle keeps pots inert
- **WHEN** a seed has effective cave-entrance shuffle (on Open/Standard) and any
  `pot_shuffle` tier was requested
- **THEN** `pot_shuffle` normalizes to `Off` (`Settings_PotShuffleForcedOff`), every pot
  (key and non-key) is inactive and resolves to vanilla, and the seed's placement is
  byte-identical to the same seed generated without pot shuffle, so no cave/house pot is
  certified against a region the shuffled entrance moved it out of

#### Scenario: Door shuffle keeps selected pots active
- **WHEN** a seed has `door_shuffle != vanilla` and any `pot_shuffle` tier was requested
- **THEN** `Settings_PotShuffleForcedOff` remains false for the door shuffle alone, the
  selected pot tier enters placement/reachability through the door-pot baseline, and the
  seed is NOT treated as byte-identical to pots-off solely because door shuffle is active

#### Scenario: Inverted/Retro cave bit does not force pots off
- **WHEN** a seed is Inverted or Retro with a retained cave-entrance bit and any
  `pot_shuffle` tier
- **THEN** because cave-entrance shuffle is inert off Open/Standard
  (`Settings_EffectiveShuffleCaveEntrances`), `pot_shuffle` is NOT forced off and the
  seed generates WITH pots — matching the canonical hash, which zeroes the inert axis

#### Scenario: Placement table is sorted at every install boundary
- **WHEN** a placement table is installed (assumed-fill, sidecar load, snapshot-tail,
  customizer, or reveal)
- **THEN** its entries are sorted by location_id (sorted-on-install where a producer
  can't guarantee it), and `--rando-selftest` asserts sortedness so the binary-search
  `Placement_Lookup` is always correct

### Requirement: Pot-key small-key economy

A dungeon's POT keys SHALL be economy-correct when `pot_shuffle` turns them into
checks. `kVanillaSmallKeyCounts` counts only a dungeon's vanilla *chest* keys, so a pot
key is NOT in it; the economy SHALL treat an active key pot's vanilla small key as an
ADDITIONAL pooled item, never relying on the chest count to cover it. Specifically:

- **Vanilla key mode:** the key pot is identity-pinned (`location_is_prepinned`) and
  drops its own key in place — no pool entry, exactly like a vanilla key location.
- **Shuffled key modes** (`Settings_EffectiveSmallKeysMode != Vanilla`): `BuildItemPool`
  SHALL pool each active key pot's vanilla `SmallKey_X` (or the shared `GenericKey`
  under Retro). This is slot-balanced — every active key pot is itself a fillable open
  slot counted by the junk-pad target, so pool and slots grow together. Under **wild**
  keys the key joins the general world pool; under **dungeon** keys it shuffles within
  its own dungeon (the assumed-fill confines per-dungeon small keys to that dungeon),
  graduated by the per-pot key-door depth gates of `randomizer-logic / Pot-key
  small-key logic gating`.
- **Non-pot free-grant (dungeon keys only):** under dungeon keys + pots a dungeon's
  deep locations gate on the prover MIN-depth over ALL its key doors, but only the
  chest + pot keys are pooled — the dungeon's NON-pot small-key drops (enemy / guard /
  under-block keys, which `pot_shuffle` never itemizes) are still collected in-context,
  exactly as in pots-off. The placer SHALL pre-grant those non-pot drops into the
  assumed inventory (`seed_pot_nonpot_drops`, count = door-rando drop total − fork pot
  keys, per dungeon) so the min-depth gates stay satisfiable. This pre-grant SHALL be
  shared by the assumed-fill seeding and the goal/sphere verifier; at runtime the live
  per-dungeon SRAM key counter (which already includes those drops) OVERWRITES it, so
  it is placer-effective only and never double-counts. Wild keys cap their own
  requirement at the pooled key count and need no free-grant.

A `Placement_SelfCheck` prong SHALL re-derive each dungeon's pot-key count from the
registry and assert the free-grant table equals (door-rando drop total − pot keys), so
a future pot-set change cannot silently desync the economy.

#### Scenario: A shuffled key pot adds its key to the pool (not the chest count)
- **WHEN** `pot_shuffle >= Keys` and `dungeon_small_keys_mode` is wild or dungeon
- **THEN** each active key pot's vanilla small key enters `BuildItemPool` as an extra
  pooled item and the pot is a fillable open slot — the dungeon's chest-key count is
  unchanged and the key never vanishes (the pre-task-#25 strand)

#### Scenario: Dungeon-keys pot keys stay in their dungeon, non-pot drops free-granted
- **WHEN** a seed has dungeon keys + `pot_shuffle` and is generated at
  `accessibility = items`
- **THEN** each dungeon's pot keys are placed within that dungeon, the deep locations
  require the prover min-depth, the non-pot drops are pre-granted into the assumed
  inventory so the gates are satisfiable, and every location is reachable (no refuse,
  no strand at generation time)

#### Scenario: Free-grant is placer-only (no runtime double count)
- **WHEN** the live reachability bridge builds counts during play under dungeon keys +
  pots
- **THEN** the per-dungeon small-key count is taken from the live SRAM counter (which
  already counts the collected non-pot drops), overwriting the seeding pre-grant, so
  the player's key count is correct and never doubled

### Requirement: Enemy forced-key drops as shuffled key checks

The placer SHALL treat each generated enemy forced small-key drop and the one-shot
castle big-key drop as a real location in assumed-fill when `enemy_drop_checks = Keys`
is effective. Small-key rows SHALL add their vanilla key item to the item pool and
their source locations SHALL enter the open-location set. The one-shot big-key row
SHALL add only a deterministic filler item because no modeled castle big-key item
exists. Wild effective small-key mode uses the world pool; under Retro it is
represented by the shared `GenericKey`. Vanilla-door Dungeon small-key mode SHALL
use generated DROP-region min-depth gates and combined free-drop accounting.

When the effective small-key mode is vanilla, enemy-drop checks SHALL be inactive
rather than identity-pinned: no enemy-drop location enters placement, no mapped row
is removed from free-drop accounting, and forced key drops remain vanilla. When door
shuffle is active, enemy-drop checks SHALL stay active through the door x enemy-drop
bridge.

The same `enemy_drop_keys_active(settings)` predicate SHALL gate open-location
collection, item-pool construction, junk padding, self-checks, spoiler/tracker
emission, logic wrapping, and runtime activation. Assetless builds with an effective
active setting SHALL fail closed instead of generating a seed with missing
enemy-drop locations.

#### Scenario: Shuffled key mode pools enemy-drop keys
- **WHEN** `enemy_drop_checks = Keys` and small keys are Wild or Retro
- **THEN** mapped enemy forced small-key drops are fillable locations whose keys enter
  the correct world or generic-key pool, and the one-shot big-key row is a fillable
  non-key check

#### Scenario: Dungeon key mode pools enemy-drop keys per dungeon
- **WHEN** `enemy_drop_checks = Keys` and small keys are Dungeon
- **THEN** mapped enemy forced small-key drops are fillable locations whose keys enter
  their own dungeon pools, and remaining free drops are seeded as
  `door_drop_total - active key pots - active enemy key drops`

#### Scenario: Door shuffle itemizes enemy drops
- **WHEN** door shuffle is active
- **THEN** mapped enemy forced small-key drops remain active locations, the door
  prover removes their DROP rows from free-drop accounting, and their keys enter
  their own dungeon pools

#### Scenario: Vanilla key mode leaves drops vanilla
- **WHEN** small keys are vanilla
- **THEN** enemy-drop checks are inactive, mapped rows remain free vanilla drops, and
  the placement digest matches enemy-drop checks off

### Requirement: Enemy-drop staged grant and suppression

Active enemy-drop checks SHALL dispatch placed items through a staged grant path, not
through the vanilla small-key increment path. The staged path SHALL guard checked
locations before dispatch, then force placement resolution with
`vanilla_registry_id = 0xFFFF` for unchecked locations. The existing dispatch path
marks the checked bit as pickup intent and routes direct grants, confirmations, and
receive animations according to the placed item.

After a successful grant, the runtime SHALL suppress future forced-key behavior for
that checked source. This suppression SHALL apply even when the placed item is the
same small key that the vanilla drop would have granted; identity placements still use
placement semantics and SHALL NOT also run the vanilla current-dungeon key increment.

Customizer may pin items on active key-tier enemy-drop locations like other non-empty
locations. Trap shuffle SHALL target enemy-drop locations only for trap classes whose
delivery path is proven safe through the staged pickup hook.

#### Scenario: Identity placement does not double grant
- **WHEN** an active enemy-drop location is identity-placed with its own small key
- **THEN** the staged dispatch grants the placed key once and the vanilla case-12
  current-dungeon increment is bypassed

#### Scenario: Trap eligibility is delivery-safe
- **WHEN** trap shuffle considers an enemy-drop location
- **THEN** only trap classes that can be delivered through the staged forced-key
  pickup path are eligible

