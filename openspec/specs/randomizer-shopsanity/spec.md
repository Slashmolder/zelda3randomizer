# randomizer-shopsanity Specification

## Purpose
TBD - created by archiving change add-rando-shopsanity. Update Purpose after archive.
## Requirements
### Requirement: Shopsanity axis opens the 27 regular shop slots as fill locations

The generator SHALL treat the 27 regular shop slot locations (ids 237–263, `type: Shop` — 9 shops × 3 slots) as ordinary open fill locations, in every world state (Open, Standard, Inverted, Retro), when and only when the `shopsanity` settings axis is `true` (default `false`). With the axis on, the `location_is_prepinned` Shop arm and the Retro-only `world_state_filter` gate are both bypassed for `LOCTYPE_Shop` rows through one shared predicate, and `BuildItemPool`'s junk-pad target plus `Placement_SelfCheck`'s expected counts grow by the same predicate (the skip-triple discipline). With the axis off, behavior is unchanged: slots are identity-pinned to their vanilla inventory and active only under Retro. Capacity Upgrade slots (264/265) remain identity-placed, Take-Any slots (266–327) remain Retro-only per-seed selections, and Retro's genericKey fourth slot remains an unlimited economy purchase outside the location system, regardless of the axis.

#### Scenario: Axis off is byte-identical
- **WHEN** any seed is generated with `shopsanity=false` in Open, Standard, or Inverted
- **THEN** placement output is byte-identical to a pre-shopsanity build at the same `kGeneratorVersion` inputs (proven by the corpus 3-way diff), and no shop-class placement rows exist

#### Scenario: Axis on fills shop slots from the open pool
- **WHEN** a seed is generated with `shopsanity=true` in any world state
- **THEN** all 27 shop slot locations receive placed items from the ordinary fill (progression permitted), the junk-pad target reflects the 27 extra open slots, and `Placement_SelfCheck` passes with the extended expected counts

#### Scenario: Retro composes
- **WHEN** a seed is generated with `mode.state=retro` and `shopsanity=true`
- **THEN** the 27 slots carry placed items instead of their vanilla identity pins, while Take-Any selection, Capacity Upgrade identity placement, and the genericKey purchase slot behave exactly as under plain Retro

### Requirement: Shop locations carry real logic regions

The logic graph SHALL bind each of the 9 regular shops to the overworld region containing its entrance door, with the region's standard world-state and Moon-Pearl gating, so shop slots participate truthfully in reachability, sphere computation, accessibility enforcement, hints, and the entrance-shuffle region-override seam. `Shop` SHALL be removed from the codegen's `REGION_OPTIONAL_TYPES` so a region-less shop row becomes a codegen error; `ShopUpgrade` and `TakeAny` remain region-optional. The binding is static (not conditional on the axis): under plain Retro with `shopsanity=false`, shop slots stop evaluating as always-reachable (`region_id=0xFFFF`) — an intended correctness fix whose output movement MUST be confined to Retro corpus rows. Inverted SHALL be covered by `logic_parts/inverted/**` overrides for the three shops whose upstream requirement callbacks are re-authored in Inverted (LW Death Mountain: `CanBombThings AND MoonPearl` — the fork's inverted graph reaches that region as a bunny; Village of Outcasts: `Hammer` only; DW Potion Shop: the Inverted traversal disjunction), with the generated logic diffed before/after to guard the last-wins merge.

#### Scenario: Dark-world shop gated like its region
- **WHEN** reachability is evaluated for a Dark World shop slot in an Open seed where the player lacks Moon Pearl and dark-world access
- **THEN** the slot is unreachable, and it becomes reachable exactly when its containing region does

#### Scenario: Region binding movement is scoped to Retro
- **WHEN** the corpus is regenerated after the region binding lands with `shopsanity=false` everywhere
- **THEN** only Retro corpus rows change (sphere/accessibility data reflecting the new gating); any Open/Standard/Inverted movement is a regression

#### Scenario: Accessibility tiers see shop slots
- **WHEN** a `shopsanity=true` seed is generated at `accessibility=items` or `locations`
- **THEN** the accessibility guarantee extends to shop slots per tier (progression in a shop slot must be reachable at `items`; every shop slot reachable at `locations`)

### Requirement: Deterministic seed-derived shop prices

The generator and runtime SHALL derive each unchecked shop slot's price from `(seed_u64, location_id)` through a dedicated salted RNG stream, uniformly over multiples of 5 in [10, 250], identically at generation time (for the spoiler) and at slot activation (for the runtime) — the price is never stored in the slot, sidecar, share string, or settings hash. Prices are uniform across item classes (no progression/junk price signal) and are NOT modeled in logic (rupees are farmable; shop reachability alone gates the check, matching the upstream accessibility-only model). Checked slots and identity-placed slots use their vanilla price. Price determinism SHALL be pinned by a `--rando-selftest` vector.

#### Scenario: Spoiler and runtime agree
- **WHEN** a `shopsanity=true` seed's spoiler reports a slot price and the slot is later activated in-game
- **THEN** the runtime charges exactly the spoiler's price

#### Scenario: Price never leaks placement class
- **WHEN** prices are derived for a seed containing both progression and junk in shop slots
- **THEN** the price distribution is independent of the placed item's class

### Requirement: Buy-once purchase with vanilla restock

The runtime SHALL present an unchecked, shopsanity-active shop slot as a check slot offering the placed item at the derived price (as built: vanilla slot sprites spawn unchanged and the seven kind handlers + the slot draw branch per frame on `Rando_ShopSlotCheckInfo`); purchasing it SHALL deduct the price, route the grant through the single dispatch chain (`Rando_ShopDispatch` → `Rando_DispatchVanillaGrant` → `Rando_ReceiveOrConfirm`) for every placement class with no vanilla-consolation fallback, and mark the location checked. The check-slot purchase SHALL bypass the vanilla refusal gates (shield-owned, empty-bottle, ammo/health-full — they belong to the vanilla item and would make the check missable), enforcing only affordability. A checked slot (and every slot when the axis is off or the randomizer is inactive) SHALL present its vanilla item at its vanilla price with vanilla purchase behavior, permanently — so vanilla shop commerce (potions, shields, bombs, arrows) remains available after the check — and a re-buy of a checked slot SHALL NOT re-dispatch the placed item. The purchase path SHALL re-verify the location is unchecked in the same frame as the purchase (double-purchase guard), and checked state persists solely in the existing sidecar checked-location bitmap (no new SRAM or sidecar fields).

#### Scenario: First purchase grants the placed item
- **WHEN** the player A-presses an unchecked check slot with sufficient rupees
- **THEN** the price is deducted, the placed item is granted via `Rando_ReceiveOrConfirm` (direct-grant classes show the confirmation cue; the rest go through `Link_ReceiveItem`), the location is marked checked, and the slot sprite despawns for the visit

#### Scenario: Insufficient rupees refuse the sale
- **WHEN** the player A-presses a check slot costing more than their current rupees
- **THEN** the purchase is refused with the vanilla can't-afford feedback and the location stays unchecked

#### Scenario: Checked slot restocks vanilla
- **WHEN** the player re-enters a shop whose slot was already purchased
- **THEN** the slot offers its vanilla item at its vanilla price and behaves exactly like the vanilla shop (repeatable where vanilla is repeatable)

#### Scenario: Reload preserves purchase state
- **WHEN** the player saves and reloads (including snapshot cold-replay) after purchasing a check slot
- **THEN** the slot spawns in its checked (vanilla-restock) form and no duplicate placed-item grant is possible

### Requirement: Check-slot draw mirrors the grant chain

The runtime SHALL draw an unchecked check slot's placed item by resolving item → icon through the same shared resolution chain the grant uses (`Rando_VanillaItemForRegistryId` primary, then `progressive_to_lttp`, with the established special remaps), reusing the field-item icon assets; a placed item with no safe icon SHALL draw a generic merchandise icon (the gold sparkle cue), never a vanilla-item lookalike. The price SHALL render as dynamically-composed digits (the static per-kind price rows cover only the 7 vanilla kinds). All three columns SHALL render their icons concurrently through per-column VRAM tile slots, and every slot's OAM entries SHALL ride the same single deferred dmd block vanilla shop slots use (never a separate region allocation — in shop rooms that overflow-rotates into invisible scratch entries). ACCEPTED LIMITATIONS (owner decision 2026-07-18): the third column shares its tile slot with the item-receipt animation and yields to the sparkle cue while a receipt plays, restoring itself afterward; and two custom-art icons requiring different palette rows (Rupoor vs other custom art) cannot both be truthful in one frame — the later column shows the sparkle cue.

#### Scenario: Third column yields during a receipt animation
- **WHEN** a purchase's receive/confirmation animation plays while the third column holds an unchecked icon-bearing item
- **THEN** that column shows the sparkle cue for the animation's duration and re-displays its item icon afterward, with grant/price behavior unaffected

#### Scenario: Icon matches the grant
- **WHEN** a check slot holds any placement (equipment, progressive tier, rupees, keys, prizes)
- **THEN** the drawn icon corresponds to the item the purchase will actually grant, or is the generic merchandise icon — never a different concrete item's icon

#### Scenario: Prices render for arbitrary values
- **WHEN** a slot's derived price is any value in the band
- **THEN** the digits render correctly above the slot in every one of the 9 shop layouts

### Requirement: Shopsanity composition rules

The axis SHALL compose without derived-rule normalization against every other axis: entrance shuffle (shop interiors remain outside the entrance pool; static region bindings stay truthful), door shuffle, dungeon chains, pot/terrain/enemy-check families, souls (no shopkeeper is soul-gated), and boss/drop/enemy shuffles. Shop slots SHALL be excluded from the trap-masquerade eligible set. Customizer manifests SHALL be able to pin shop slots when the axis is on and SHALL refuse shop-slot pins when it is off. The spoiler SHALL emit shop rows with their prices (JSON `shops[]` gains `"price"`; the text `Shops:` section prints the price per slot) in every world state where shop-class placements exist, leaving axis-off non-Retro spoilers byte-identical.

#### Scenario: No derived-rule coupling
- **WHEN** `shopsanity=true` is combined with any other supported axis combination
- **THEN** canonical serialization preserves `shopsanity=true` (no normalization), and generation succeeds or refuses on the other axes' existing rules only

#### Scenario: Spoiler carries prices
- **WHEN** a `shopsanity=true` seed writes its spoiler
- **THEN** every shop slot row includes the derived price, and identity-placed Capacity Upgrade rows remain flagged without a derived price

#### Scenario: Customizer pin respects the axis
- **WHEN** a customizer manifest pins location 240 (`Dark World Forest Shop - 0`) with `shopsanity=true`
- **THEN** the pin is honored like any open location; the same manifest with `shopsanity=false` is refused with a clear error

