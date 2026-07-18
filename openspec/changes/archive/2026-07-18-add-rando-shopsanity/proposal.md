# Proposal: Shopsanity (shop slots as randomized purchase checks)

## Why

Shops are the last classic check family the randomizer does not randomize.
The Retro world-state work already turned the 9 regular shops' 27 slots into
*locations* (ids 237–263, `type: Shop` in `assets/rando/location_registry.yaml`)
with a working purchase→dispatch runtime (`Rando_ShopDispatch` →
`Rando_DispatchVanillaGrant` → `Rando_ReceiveOrConfirm`), but they are
identity-pinned to their vanilla inventory (`location_is_prepinned` in
`src/rando/rando_placement.c`), Retro-only (`world_state_filter: [retro]`),
region-less in logic (`REGION_OPTIONAL_TYPES` → `region_id=0xFFFF` =
always-reachable), and restock forever. No upstream implements this either —
ALTTPR mainline keeps shop inventory entirely outside the item fill (verified:
`app/Shop.php` inventories are code-stocked in region files /
`Randomizer::setShops()`; shop wrappers feed only logic accessibility, never
`getEmptyLocations()`) — so this is fork-original scope built on the fork's own
shipped machinery.

Shopsanity makes buying a check a real decision: rupees become a resource that
matters (composing with grass/rock junk-rupee farming and Retro's rupee-bow
economy), and 27 well-spread locations join the pool at a fraction of the
engineering cost of the terrain families, because the location registry,
purchase dispatch, spoiler `shops[]` section, and checked-bitmap persistence
already exist.

## What Changes

- **One new settings axis**: `shopsanity` (`false`/`true`, default `false`),
  carried in existing reserved canonical byte `[29]` **bit 4** — no
  `kSettingsCanonicalLen` growth, so default-settings `settings_hash` is
  unchanged (unlike the terrain append). NOTE: authored while
  `add-rando-key-rings-skeleton-key` (canonical byte `[30]`, len 30→31) is
  in flight on its branch; this change lands after it and its deltas are
  reconciled against the post-keyrings baseline before archive.
- **The 27 regular shop slots become ordinary open fill locations** when the
  axis is on, in ALL world states (Open/Standard/Inverted/Retro): the
  `location_is_prepinned` Shop arm and the `world_state_filter: [retro]` gate
  become conditional on the axis, and `BuildItemPool`'s junk-pad target grows
  by the newly open slots (the established skip-triple discipline).
- **Real logic regions for shops**: `Shop` leaves `REGION_OPTIONAL_TYPES`;
  each of the 9 shops gets a `region:` binding + entry predicate in
  `logic.yaml` (with `logic_parts/inverted/**` overrides where the world
  assignment flips), so reachability, accessibility tiers, spheres, and the
  entrance-shuffle region-override seam all see shops truthfully.
- **Deterministic per-seed prices**: each unchecked slot sells its placed item
  for a price derived from `(seed, loc_id)` via a salted RNG stream —
  re-derived identically at generation (spoiler) and slot activation, never
  stored. Affordability is deliberately NOT modeled in logic (rupees are
  farmable; matches upstream's model where shop slots feed accessibility
  only).
- **Buy-once, then vanilla restock**: an unchecked slot spawns the placed item
  at the generated price; purchasing grants through the existing dispatch
  chain (all placement classes — no vanilla-consolation fallback) and marks
  the location checked; a checked slot reverts to its **vanilla item at the
  vanilla price** forever (the grass/rock revert pattern — potion/shield/
  arrow/bomb shopping stays available after the check).
- **Draw**: unchecked slots draw the placed item's icon via the shared
  field-item resolution chain (registry-primary + `progressive_to_lttp`, the
  same resolver the grant uses, per the draw-must-mirror-grant rule) with a
  generic-merchandise fallback for unrepresentable items — never a misleading
  vanilla icon — plus dynamic price digits (the current per-kind static
  price/tile tables can only draw the 7 vanilla kinds).
- **Out of scope (explicitly unchanged)**: Capacity Upgrade slots (264/265,
  stay identity-placed), Take-Any caves (266–327, stay Retro-only per-seed),
  the Bomb Shop (scripted Big Bomb progression), the witch's powder trade
  (already NPC check 147), and Retro's genericKey 4th slot (an unlimited
  economy purchase, not a location).
- **`kGeneratorVersion` bump + corpus entries** for the axis and its
  compositions; shopsanity-off seeds are proven byte-identical by corpus
  regen, EXCEPT Retro rows may move once via the shop region binding (a
  scoped, intended correctness fix — Retro shop slots stop being
  "always-reachable" in sphere/accessibility data).

## Capabilities

### New Capabilities

- `randomizer-shopsanity`: the shop-check family — slot scope, axis
  semantics, region binding and logic contract, price generation, buy-once /
  vanilla-restock runtime, draw rules, and composition rules with Retro,
  entrance shuffle, souls, traps, and the accessibility tiers.

### Modified Capabilities

- `randomizer-core`: "Settings canonical serialization order (normative)" —
  byte `[29]` bit 4 carries `shopsanity`; bits 5–7 remain reserved. (Delta
  restated against the post-keyrings 31-byte table; reconcile before
  archive.)
- `randomizer-ui`: new requirement for the Shopsanity toggle, tracker
  presentation of the 27 shop rows, and the spoiler `shops[]`/`Shops:` price
  emission across all world states.

## Impact

- **Rando module**: `src/rando/rando_settings.{c,h}` (axis + canonical bit +
  CSV key), `src/rando/rando_placement.c` (conditional prepin/world-filter,
  junk-pad target, price stream, self-check counts),
  `src/rando/rando.c` (activation-time price table, checked-gate helpers),
  `src/rando/rando_spoiler.c` (price field; sections now emit in non-Retro
  seeds), `src/rando/rando_window/rando_window.cpp` (toggle + tooltip).
- **Engine**: `src/sprite_main.c` — `SpritePrep_Shopkeeper` /
  `ShopKeeper_SpawnShopItem` (checked-gate + check-kind spawn),
  `Sprite_BB_Shopkeeper` dispatch (new check-slot handler),
  `ShopItem_HandleCost` call sites (generated price), `SpriteDraw_ShopItem`
  (placed-item icon + dynamic price digits).
- **Assets/codegen**: `assets/rando/location_registry.yaml` (Shop rows'
  world-state filter), `assets/rando/logic.yaml` + `logic_parts/inverted/**`
  (9 shop region bindings; mind the last-wins merge trap),
  `assets/rando_logic_gen.py` (`REGION_OPTIONAL_TYPES` shrink).
- **Validation**: `kGeneratorVersion` bump, corpus regen + new manifest
  entries (open/standard/inverted/retro × shopsanity, plus composition rows),
  `--rando-selftest` additions (price determinism, pin/pool accounting), CI
  guards untouched; end-to-end playtest is the only net for the runtime
  purchase path (corpus/selftest are generation-only).
