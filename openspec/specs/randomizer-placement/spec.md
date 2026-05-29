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
- **Magic upgrades** as items: `HalfMagic`, `QuarterMagic` — grant path writes to `link_magic_consumption` (1 = full, 2 = half, 4 = quarter per vanilla semantics; new helper).
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

#### Scenario: HalfMagic / QuarterMagic grant
- **WHEN** the dispatcher grants `HalfMagic`
- **THEN** `link_magic_consumption` is set to 2 (half) if currently 1 (full); the standard receive animation plays; subsequent `QuarterMagic` grant sets it to 4

#### Scenario: PieceOfHeart vs BossHeartContainer routing
- **WHEN** the dispatcher grants `PieceOfHeart`
- **THEN** the vanilla PoH path runs (4-quarter mechanic; +1 max HP every 4 pieces)
- **AND WHEN** the dispatcher grants `BossHeartContainer`
- **THEN** `+1 max HP` is applied directly without consuming any piece-of-heart quarters

#### Scenario: Boss kill dispatches TWO locations
- **WHEN** the player defeats a dungeon boss (e.g., Helmasaur King in Palace of Darkness)
- **THEN** the boss-death code path calls `Rando_OnLocationCheck(PalaceOfDarkness_BossHeart, BossHeartContainer)` AND separately `Rando_OnLocationCheck(PalaceOfDarkness_Prize, Prize_Crystal_PoD_Vanilla)`, granting whatever the placement table has at each location ID

#### Scenario: Phase A boss-heart slots are identity-placed
- **WHEN** Phase A generates a seed
- **THEN** each of the 10 `<Dungeon>_BossHeart` slots in the placement table is hardcoded to `BossHeartContainer` (the placer does not shuffle them); the dispatcher still fires uniformly via the existing code path but every boss kill grants the heart container at that dungeon. Phase B's `bossHeartsInPool=true` setting (when added) would let these slots participate in the shuffle

#### Scenario: Pyramid Fairy is a synthesized multi-slot grant site
- **WHEN** the player triggers the Pyramid Fairy upgrade event AND `kFeatures1_RandomizerActive` is set
- **THEN** new rando-mode code (added by this change; vanilla LttP has a fixed-item shrine, not multi-slot) calls `Rando_OnLocationCheck` once per active slot. Always-active slots: `pyramid_fairy_sword` (fall-back `L1Sword`) and `pyramid_fairy_bow` (fall-back `Bow`). Conditionally-active slots (added when world config enables them, mirroring ALTTPR's behavior at `NorthEast.php:40-41`): `pyramid_fairy_left` and `pyramid_fairy_right` (vanilla chests; fall-back items per audit)
- **AND WHEN** `kFeatures1_RandomizerActive` is clear
- **THEN** the vanilla fixed-item shrine runs unchanged and no `Rando_OnLocationCheck` fires

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

The placement layer SHALL grant the seed's configured starting inventory exactly once at new-game initialization. A `kRam_RandoStartingInventoryGranted` bit SHALL be set after injection so subsequent save reloads do not re-inject.

#### Scenario: Open mode starting inventory applied at new game
- **WHEN** world-state is Open and the player starts a new randomizer slot
- **THEN** Link begins with the Open-mode starting items reflected in `link_item_*` and the HUD, and `kRam_RandoStartingInventoryGranted` is set

#### Scenario: No double-grant on reload
- **WHEN** the player saves and reloads a randomizer slot mid-run
- **THEN** the starting-inventory injection does not run again because `kRam_RandoStartingInventoryGranted` is already set in the loaded save

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

