## Why

"Pot Sanity" turns the game's pots into randomizer checks: an un-checked pot is
marked with an animated gold glint to signal it holds a placed item, breaking/lifting
it grants that item once, and thereafter the glint clears and the pot reverts to its
vanilla behavior (its vanilla drop, or empty for a pot that hid a small key). This is the single
largest expansion of the location pool the randomizer has taken on — the engine
has **835 liftable pots across 164 dungeon rooms** (counted directly from
`zelda3_assets.dat`: 835 single `Pot` objects; the 16 `Fairy Pot` objects are a
distinct mechanic and are out of scope), of which **~613 hide a vanilla item**
(594 hearts/rupees/bombs/arrows/magic + **19 small keys**) and ~222 are empty.

Today the randomizer tops out at **328 locations** and every placement working
array and the checked-location bitmap are hardcoded to **512**. Pot Sanity at its
maximal tier needs **328 + 835 = 1163** locations, so the headline engineering
prerequisite is raising that ceiling to **2048**. The good news, established by
research against the as-built code:

- The **on-disk** sidecar format already scales — the checked-bitmap and
  placement table are sized per slot by `placement_table_size`, and location IDs
  are append-only (`randomizer-save / Sidecar slot contents`, `… / Checked-location
  bitmap read invariant`). Pots take IDs 328…1162 with no save-format change.
- Pot **logic is nearly free**: a location's `can_reach` defaults to `TRUE()` and
  reachability is gated by region membership, so a pot bound to its room's region
  is reachable exactly when that region is — no per-pot predicate for the vast
  majority (`randomizer-logic / Location and region model with stable IDs`).
- The placer's junk-padder fills open contents-pot slots with no manual item-pool
  authoring; the `All` tier's empty pots are filled with an `ITEM_Nothing` item in a
  dedicated pre-pass (`randomizer-core / Item pool construction …`).

The hard, playtest-only part is the **runtime grant hook**: pots are BG-layer
tiles, not sprites and not chests, so there is no existing per-pot identity or
grant site. This lands squarely in the randomizer's dominant bug class
(`CLAUDE.md`: "vanilla state reused as a progress proxy" + re-collect/dupe) and
is invisible to `--rando-selftest` and the corpus. The design pins the hook to a
single point and gates it on `Rando_IsLocationChecked`.

No upstream ALTTP randomizer ships this in mainline (ALTTPR does not shuffle
pots; its asm has dormant, never-wired pot scaffolding). The design follows the
two real precedents — aerinon's **ALttPDoorRandomizer** (tiered pot-shuffle +
"Colorize Pots") and **OoTR pot sanity** (Off/Dungeon/Overworld tiers,
individually-named checks, an "include empty pots" option backed by a "Literally
Nothing" filler, and appearance-matches-contents).

## What Changes

- **New tiered `pot_shuffle` setting axis** (`Off` / `Keys` / `Contents` / `All`),
  off by default. The tier is a generation-time filter over a fixed, append-only
  pot-location ID space; it does not change which IDs exist, only which are placed.
  - `Keys` (~19): only small-key pots become checks.
  - `Contents` (~610): every pot with a vanilla *item* becomes a check (incl. keys;
    Cucco / creature-spawn pots are excluded as non-loot).
  - `All` (~835): also the ~225 empty/non-loot pots, filled with a new **"Literally
    Nothing"** item in a dedicated pre-pass (NOT the junk rotation) so the economy
    isn't flooded. All counts are generator-authoritative (provisional here).
- **Build-time pot enumeration** (`assets/scripts/gen_pot_tables.py`, committed
  generator over `zelda3_assets.dat`, mirroring `gen_enemy_shuffle_tables.py` /
  `chest_table.gen.bin`): emits the 835 append-only `location_registry.yaml` rows,
  a `(dungeon_room, tile_position) → LOC` runtime lookup table, per-pot tier
  membership + vanilla-content classification, and region-bound logic entries
  (`can_reach: TRUE()`; gates are inherited only for uniform rooms — non-uniform rooms
  require a reviewed override or the build fails).
- **Runtime pot dispatch** (`Rando_PotDispatch`) at the **top of `RevealPotItem`**
  (`src/dungeon.c`, before its secret scan, covering all three callers): compute
  `(room, pos)`, look up the pot LOC, and — when in scope and unchecked — grant via
  `Rando_DispatchVanillaGrant` → `Rando_ReceiveOrConfirm` (which marks the LOC checked
  internally, before the lookup), then suppress the vanilla secret on every break
  path. Out-of-scope / Off pots are pure vanilla; a *checked* pot re-drops vanilla for
  item-pots but is **explicitly suppressed (empty) for key/one-shot pots** to avoid
  key duplication.
- **Un-checked pot check glint** (sprite-layer overlay): an in-scope, un-checked pot
  draws an animated gold glint over it (registered in `RoomDraw_SinglePot`, drawn in
  `Module07_Dungeon`, gold injected into the PPU CGRAM copy in NMI); checked /
  out-of-scope pots draw none, and the pot's BG tile / floor are not recolored.
  Cosmetic-only (OAM + PPU-CGRAM, no `g_ram`), gated behind the rando flag.
- **Capacity raise to 2048** via a typed audit of EVERY location-id-keyed array /
  constant — `1163` exceeds BOTH the 512 caps (checked-bitmap, placer arrays, the
  placement-digest cap) AND the 1024 caps (tracker / customizer / spoiler / snapshot
  tables) — each guarded by a `_Static_assert` tied to `LOC__COUNT`. No on-disk
  save-format change (sized by `placement_table_size`; compat is one-directional —
  new reads old, old refuses pot-expanded slots).
- **`Placement_Lookup` binary-search** rewrite (today an O(N) linear scan per
  check) so per-pot-break dispatch stays cheap at ~1163 locations.
- **`kGeneratorVersion` bump + corpus regen.** Pot-shuffle `Off` MUST remain
  placement-byte-identical — out-of-scope pots are skipped in the open-location and
  junk-pad loops (so they draw no fill RNG and never enter the placement table), NOT
  filtered out of the digest after the fact; validated by a corpus 3-way diff.

## Capabilities

### New Capabilities

- `randomizer-pot-sanity`: pot enumeration & stable identity, the tiered scope
  model, the single-point runtime grant hook with re-collect safety, the
  un-checked check glint, the "Literally Nothing" filler, fairy-pot exclusion, and the
  committed `gen_pot_tables.py` generator.

### Modified Capabilities

- `randomizer-core`: ADD the `pot_shuffle` canonical settings axis (free bits in
  the existing canonical byte — no `kSettingsCanonicalLen` change) and the
  "Literally Nothing" filler item; record the `kGeneratorVersion` bump.
- `randomizer-logic`: raise the location ceiling 512 → 2048; specify auto-generated,
  region-bound pot locations with the `can_reach: TRUE()` region default and a
  mandatory `region:` per pot; update the reachability budget for the larger graph.
- `randomizer-placement`: ADD the pot dispatch point + trigger-based re-collect
  safety; key-pots follow the dungeon key mode (pinned in vanilla mode; under door
  shuffle, pinned AND subtracted from the shuffled pool to avoid duplication); the
  `ITEM_Nothing` pre-pass; the typed capacity audit (512 + 1024 sites); a sorted
  placement table + binary-search `Placement_Lookup`.
- `randomizer-ui`: ADD the `pot_shuffle` tier selector to the native settings
  window, the check glint as a cosmetic axis, and pot handling in the location tracker
  (room-grouping / show-pots toggle) so 835 entries stay usable.
- `randomizer-save`: the per-slot checked-bitmap / placement-table sizing already
  accommodates ~1163 locations (on-disk format unchanged; in-memory caps grow);
  compatibility is one-directional — a new binary reads old slots, an old binary
  refuses pot-expanded slots.

## Impact

- **Code**: `src/dungeon.c` (`RevealPotItem`/`Dungeon_LiftAndReplaceLiftable` hook,
  `RoomDraw_SinglePot` glint-capture + `RandoPot_DrawGoldOverlay`, a `(room,pos)→LOC`
  accessor), `src/nmi.c` (gold-ramp CGRAM injection), `src/rando/rando.{c,h}`
  (`Rando_PotDispatch`, cap raise, `kGeneratorVersion`), `src/rando/rando_placement.*`
  (`[512]→[2048]`, binary-search lookup, pot pool inclusion),
  `src/rando/rando_logic.{c,h}` (`kReachabilityMaxLocations`, bitset),
  `src/rando/rando_settings.*` (canonical axis), `src/rando/rando_save.*`
  (in-memory struct sizes), `src/rando/rando_window/` (tier selector + tracker),
  `src/rando/auto_tracker.c` (pot grouping).
- **Generated data**: NEW `assets/scripts/gen_pot_tables.py` → local gitignored
  `assets/rando/pots.gen.yaml`, generated `src/rando/pot_lookup.h`, and pot
  logic entries (`assets/rando/logic_parts/` or a generated include), all
  gitignored-artifact-friendly per the embedded-data guard.
- **Determinism / corpus**: pot-shuffle `Off` byte-identical for every existing
  seed; corpus regenerates at the new `kGeneratorVersion`. New corpus entries cover
  each tier.
- **Verification**: `--rando-selftest` pot-lookup/identity invariants, corpus regen,
  and a **load-bearing owner playtest** of each tier (key-pot, item-pot re-break,
  empty-pot, check glint, room re-entry, a door-shuffle + pot-shuffle seed).
- **`make clean`** required after the `kGeneratorVersion` / `RandoSettings` /
  cap-constant header edits (Makefile has no header-dep tracking).
