## ADDED Requirements

### Requirement: Pot-shuffle tiered scope over a fixed location ID space

The randomizer SHALL provide a `pot_shuffle` setting with four values — `Off` (0),
`Keys` (1), `Contents` (2), `All` (3) — defaulting to `Off`. The setting SHALL
turn dungeon pots into randomizer check locations according to the tier:

- `Off`: no pot is a check; vanilla behavior is unchanged.
- `Keys`: only pots whose vanilla content is a small key (17 in the shipped
  registry) are checks.
- `Contents`: every pot with any vanilla item content (603, the small-key pots
  included) is a check.
- `All`: every liftable dungeon pot (799), including the 196 with no vanilla
  content, is a check. (These counts are the generator's current `tier_counts`
  output, asserted at build time — not hardcoded constants.)

Every liftable dungeon pot SHALL be assigned a stable, append-only location ID in
the committed `assets/rando/pots.gen.yaml` registry (ids from 328)
**independently of the tier** — the full 799-pot set exists in the ID space at all
times. The tier SHALL act as a generation-time
filter selecting which pot locations enter the placement pool; pots not selected
for a seed are **absent from the placement table (or carry the `0xFFFF` sentinel when
present below a higher placed id)** and behave exactly as vanilla at runtime (vanilla
content revealed, not recolored). The following SHALL NOT be assigned location IDs and
are never checks: `Fairy Pot` objects (16); **structural-secret pots** (whose vanilla
secret is a Hole / Warp / Staircase / Bombable / Switch — lifting them triggers a room
function, not a pickup); **creature-spawn pots** (whose vanilla content spawns an
enemy/NPC — Cucco, RockCrab, Bee, soldiers, etc. — not loot, per the D2 content
policy); and pots in **excluded rooms** (boss arenas, the pinned-boss environment
rooms, and cutscene/triggered rooms — see design D1; room 0x104 / Link's House, which
doubles as the glitch-only Chris-Houlihan "Top Secret Room", is INCLUDED per owner
decision — its heart pots are sphere-0 reachable from the start). The generator
SHALL maintain this exclusion list and assert no excluded pot emits a LOC. Overworld
bushes/rocks are out of scope. The exact per-tier counts SHALL be produced and
asserted by the generator (cross-referencing secret records against pot positions),
NOT hardcoded. The tier supersets SHALL be nested: `keys ⊆ contents ⊆ all`.

#### Scenario: Off changes nothing
- **WHEN** `pot_shuffle = Off`
- **THEN** no pot location is placed, no pot is recolored, and every pot reveals
  its vanilla content exactly as in the unmodified game

#### Scenario: Keys tier selects only key-pots
- **WHEN** `pot_shuffle = Keys`
- **THEN** exactly the pots whose vanilla content is a small key are in the
  placement pool and are checks; all other pots stay vanilla and un-recolored

#### Scenario: ID space is tier-invariant
- **WHEN** the same seed is generated at `Contents` and at `All`
- **THEN** every pot location's numeric ID is identical between the two; only the
  set of pots that are placed (in-pool) differs

#### Scenario: Fairy pots are never checks
- **WHEN** any tier is active and the player breaks a Fairy Pot
- **THEN** it summons a fairy as in vanilla and grants no placed item

### Requirement: Build-time pot enumeration with stable identity

A committed generator (`assets/scripts/gen_pot_tables.py`) SHALL enumerate every
liftable dungeon pot from `zelda3_assets.dat` (room object data + the
`kDungeonSecrets` table) and emit, deterministically, a single **committed**
registry `assets/rando/pots.gen.yaml` — append-only pot rows carrying
`{id (from 328), name, room, pos4, region, tier, vanilla_item, …}` plus an
asserted `tier_counts` header. The build-time codegen (`assets/rando_logic_gen.py`)
SHALL consume `pots.gen.yaml` to emit the pot `LOC_*` ids into
`location_ids.h`/`logic_data.c`, the region-bound logic entries, and a sorted
`(dungeon_room_index, tile_position) → location_id` runtime lookup
`src/rando/pot_lookup.h`. Because `pots.gen.yaml` is **committed** (unlike the
gitignored `chest_table.gen.bin`), it does NOT reproduce `chest_lookup.h`'s silent
fail-open hole: if it were ever absent the codegen emits an EMPTY `pot_lookup.h`
and every pot resolves to `0xFFFF` (pot-shuffle inert at runtime), never a silent
grant of wrong content. The generator SHALL assert its own invariants and fail on
violation: every pot has a `region:`; `(room, tile_position)` is unique across all
pots; the tier sets are nested; and content classification (item vs structural vs
empty) is exhaustive. A pot's
identity SHALL be `(dungeon_room_index, tile_position)` — fixed room geometry that
is stable across save/reload.

#### Scenario: Generator is the source of truth, not a transcription
- **WHEN** the pot tables are built
- **THEN** they are produced by `gen_pot_tables.py` parsing the shipped asset
  data, not hand-transcribed, and the build fails if any invariant assertion
  (unique identity, region present, nested tiers, exhaustive classification) is
  violated

#### Scenario: Identity is save-stable
- **WHEN** a pot is checked, the game is saved, and the slot is reloaded
- **THEN** the same `(room, tile_position)` resolves to the same location ID and
  the pot is still recognized as checked

#### Scenario: Pot location IDs are append-only
- **WHEN** the pot tables are regenerated after a registry change elsewhere
- **THEN** existing pot location IDs are unchanged; new IDs (if any) are appended,
  consistent with `randomizer-logic / Location and region model with stable IDs`

### Requirement: Single-point runtime pot dispatch with exactly-once grant

Pot item grants SHALL be dispatched at a single point: the **top of `RevealPotItem`
(`src/dungeon.c`), immediately after it zeroes `dung_secrets_unk1` and before its
secret-table scan**. `RevealPotItem` has THREE callers — the lift path
(`Dungeon_LiftAndReplaceLiftable`, fed by both `Link_PerformThrow` and the lift-timer
branch), the sword-break path (`HandleItemTileAction_Dungeon`, which OR's
`dung_secrets_unk1 |= 0x80` and spawns smashed-terrain *after* the call), and
`ThievesAttic_DrawLightenedHole` (a `0x2020` lightened-hole, NOT a pot). The hook
SHALL NOT be placed after the secret scan or in `Sprite_SpawnSecret`: a pot with no
secret record returns from the scan early, but the `All`-tier empty pots require the
hook.

`RevealPotItem` SHALL take an `is_pot` flag: only the two genuine-pot callers (lift +
sword-break, both gated to the `0x1010` liftable-pot tile class) pass `true`; the
`ThievesAttic_DrawLightenedHole` caller passes `false` so the hook never runs for it.
The hole shares the `dung_object_tilemap_pos[]` array with pots, so its `pos4` CAN
collide with a registered pot's (Thieves Town attic room `0x65` has pots at
`0x1c64`/`0x1c68`) — the `is_pot` flag, NOT a lookup miss, is what makes it inert
(relying on the lookup alone would let falling through that floor silently grant+check
a colliding pot).

When `is_pot` is true the hook (`Rando_PotBreakHook(room, pos4)`) SHALL, in order:
(1) return `kRandoPot_Vanilla` (run the vanilla path) if the randomizer is inactive,
the `(room, tile_position) → loc` lookup finds no LOC, or the LOC is not in the active
tier; (2) for a **checked** LOC, return `kRandoPot_Suppress` for a one-shot pot (small
key or `ITEM_Nothing`) and `kRandoPot_Vanilla` for an item-pot (per *Checked pot
reverts to vanilla appearance and behavior*); (3) for an **active, un-checked** LOC,
grant in exactly this sequence —
`uint8 lttp = Rando_DispatchVanillaGrant(loc, 0xFFFF, 0)` (the `0xFFFF`
vanilla-registry-id sentinel forces "always dispatch the placed item"), which
internally calls `Rando_OnLocationCheck` (MARKING the location checked **before** the
placement lookup) and resolves the placed item's class (direct-grant / `ITEM_Nothing`
→ `kRandoLttpSkip`; else its LttP receive code), then
`Rando_ReceiveOrConfirm(lttp, placed_item_id)` to deliver it (direct-grant
confirmation cue; `Link_ReceiveItem` otherwise; NO cue for `ITEM_Nothing` — the
recolor reverting on re-entry is the feedback), and return `kRandoPot_Suppress`. The
caller returns early on `kRandoPot_Suppress`, never falling through to a vanilla-secret
spawn. Marking is internal to `Rando_DispatchVanillaGrant` and happens before the
lookup, so the dispatch SHALL NOT additionally call `Rando_MarkLocationChecked`.

The persistent gate SHALL be the rando checked-location bit, NOT the transient
per-room `pots_revealed_in_room` mask. Suppression SHALL cover every break path
WITHOUT a dedicated "granted" flag: `RevealPotItem` zeroes `dung_secrets_unk1`, then
runs the hook; returning `kRandoPot_Suppress` makes `RevealPotItem` return early with
`dung_secrets_unk1 == 0`, so the sword-break path's post-call `dung_secrets_unk1 |=
0x80` yields `sprite_graphics = 0x80 & 0x7f = 0` and spawns no smashed-terrain content.
For `All`-tier empty pots (no `kDungeonSecrets` entry) the `(room, tile_position) →
loc` lookup SHALL be the sole grant trigger, so the hook SHALL run for every lifted
pot tile, not only secret-bearing ones.

#### Scenario: A placed pot grants exactly once
- **WHEN** an in-scope, un-checked pot is broken
- **THEN** its placed item is granted once via the dispatch chain, the location is
  marked checked, and the vanilla secret is suppressed

#### Scenario: Re-entering the room does not re-grant
- **WHEN** the player breaks an in-scope pot, leaves and re-enters the room (which
  resets `pots_revealed_in_room` and respawns vanilla pots), and breaks the same
  pot again
- **THEN** no placed item is granted the second time; the checked LOC bit gates it
  and the pot follows the vanilla path

#### Scenario: Empty pot is a check via the lookup, not the secret table
- **WHEN** `pot_shuffle = All` and the player breaks a pot that had no vanilla
  content
- **THEN** the `(room, tile_position) → loc` lookup triggers the grant of its
  placed item (which may be Literally Nothing), marking it checked

#### Scenario: Non-direct-grant placed item is never dropped
- **WHEN** an in-scope pot's placed item is a non-direct-grant class (e.g. a
  bottle, bow, equipment)
- **THEN** it is delivered via `Link_ReceiveItem`; the dispatch never marks the
  location checked and then spawns a vanilla consolation instead (which would lose
  the placed item and make an assumed-fill-certified seed unbeatable)

### Requirement: Checked pot reverts to vanilla appearance and behavior

Once a pot location is checked, the pot SHALL draw in its vanilla color, and its
on-break behavior SHALL branch on the pot's known vanilla content:
- **Item-pot** (heart/rupee/magic/etc.): the vanilla drop is re-droppable on each
  break, exactly like vanilla — the user's "vanilla item under it after checked."
- **Key-pot, or any one-shot content: the hook SHALL EXPLICITLY suppress the vanilla
  spawn (reveal nothing).** This SHALL NOT rely on vanilla behavior — vanilla has no
  persistent per-pot "key taken" flag and content byte `8` bypasses the
  `pots_revealed_in_room` mask, so a naive vanilla fall-back would DUPLICATE the key
  on every room re-entry. The runtime reads the vanilla content from the pot table and
  suppresses accordingly.

Out-of-scope pots (tier excludes them, or `pot_shuffle = Off`) SHALL follow the pure
vanilla path, byte-identical to the unmodified game.

#### Scenario: Checked item-pot re-drops vanilla content
- **WHEN** a pot whose vanilla content is a Heart is checked, then broken again
- **THEN** it drops a Heart (the vanilla content), repeatable as in vanilla, and
  draws in the vanilla color

#### Scenario: Checked key-pot is suppressed to empty (no key dup)
- **WHEN** a pot whose vanilla content is a small key is checked (its placed item
  granted), the player leaves and re-enters the room, and breaks the pot again
- **THEN** the hook explicitly suppresses the vanilla key spawn so the pot is empty,
  and does NOT re-spawn the vanilla key — preventing the key duplication a naive
  vanilla fall-back would cause (vanilla has no persistent per-pot key-taken flag)

### Requirement: Un-checked in-scope pots are recolored

The runtime SHALL draw an in-scope, un-checked pot under an alternate CGRAM
sub-palette (a palette-nibble swap in the four tilemap words emitted by
`RoomDraw_SinglePot`) so it is visually distinct from a vanilla pot. The recolor SHALL be gated on the randomizer being
active, the pot being in the active tier's pool, and `Rando_IsLocationChecked`
being false; checked and out-of-scope pots SHALL draw the vanilla words. The
recolor SHALL be code-only (no new graphics) and SHALL NOT alter non-randomizer
RAM-compare behavior. The alternate sub-palette SHALL be one that is loaded across
dungeon themes and visibly distinct. The shipped value is `kRandoPotAltPalette`;
its cross-theme loading is confirmed in source (the row is among those
`Palette_Load_DungeonSet` loads for every dungeon), and its visible distinctness is
confirmed at playtest (an offline render against `zelda3_assets.dat` was the planned
check, but the build worktree carried no asset blob).

#### Scenario: Un-checked check-pot looks different
- **WHEN** `pot_shuffle` is on and a pot in the active pool has not been checked
- **THEN** it is drawn under the alternate sub-palette, signalling it holds a
  placed item

#### Scenario: Recolor reverts on check and on re-entry
- **WHEN** an in-scope pot is checked and the room is later reloaded
- **THEN** the pot draws under the vanilla sub-palette

#### Scenario: Non-rando draw path is unchanged
- **WHEN** the randomizer is not active
- **THEN** `RoomDraw_SinglePot` emits the vanilla tilemap words with no palette
  change, preserving RAM-compare

### Requirement: Literally Nothing filler for empty pots

The item pool SHALL pad the `All` tier's 196 empty pots (pots with no vanilla
content that become checks) with a **Literally Nothing** filler item
(`ITEM_Nothing`) — a no-op grant that shows a brief "nothing" cue and marks the
location checked — so the pool is not flooded with real resources. `ITEM_Nothing`
SHALL be a logic no-op (never progression), SHALL be excluded from the
"100% inventory" accessibility count, and once checked the pot SHALL behave as a
vanilla-empty pot. The placer SHALL fill empty-pot locations with `ITEM_Nothing` in a
**dedicated pre-pass** (removed from the open set before assumed-fill and junk padding
— like vanilla-dungeon-item pre-placement), NOT as a `kJunkRotation` entry, so
`ITEM_Nothing` can never land on a chest or non-empty pot and no real item can land on
an empty pot. Its cross-cutting behavior SHALL be: a sphere-0 / always-reachable
placement for sphere math; **trap shuffle never replaces it and never targets
empty-pot slots**; **customizer cannot pin `ITEM_Nothing` nor pin any item onto an
empty-pot slot** (empty pots are a non-customizable location class, like prize/shop);
**hints never source an `ITEM_Nothing` pot**; the spoiler emits it only as a grouped
or omitted line (`randomizer-ui`); and it is **counted in the tracker completion
denominator** (so an `All` seed can reach 100%) even though it is excluded from the
`items` accessibility tier. The `Keys` and `Contents` tiers SHALL NOT require this
filler (every check has a real vanilla item to fall back to).

#### Scenario: Empty pot grants Literally Nothing
- **WHEN** `pot_shuffle = All` and an empty pot is placed with `ITEM_Nothing`
- **THEN** breaking it shows the "nothing" cue, marks the location checked, and
  thereafter the pot is a vanilla-empty pot

#### Scenario: Literally Nothing never satisfies logic
- **WHEN** the placer evaluates accessibility and `ITEM_Nothing` is placed
- **THEN** it contributes nothing to reachability and is not counted toward a
  "100% inventory" (`items`) accessibility tier

#### Scenario: Literally Nothing counts for tracker completion but not accessibility
- **WHEN** an `All` seed is fully cleared, including every empty pot
- **THEN** the tracker shows 100% (empty pots count in the completion denominator)
  even though `ITEM_Nothing` was excluded from the `items` accessibility tier

#### Scenario: Customizer and trap shuffle leave empty pots alone
- **WHEN** a customizer manifest or trap shuffle is active with `pot_shuffle = All`
- **THEN** neither can pin an item onto an empty-pot slot, pin `ITEM_Nothing`
  elsewhere, nor convert an empty pot into a trap

### Requirement: Pot-shuffle Off is placement-byte-identical

Pot locations SHALL draw no fill RNG and SHALL NOT contribute to
`placement_digest` when `pot_shuffle = Off` (and likewise for the out-of-scope
pots of any tier). Adding the pot location IDs to the registry SHALL NOT change
any existing seed's `placement_digest` or `settings_hash`. The implementation
SHALL achieve this by **skipping out-of-scope pot locations in the open-location
collection loop and the junk-pad target loop** (mirroring the existing
inactive-Take-Any skip), NOT by post-hoc digest filtering (the digest already
covers only placed entries). Without the loop skip, registry growth alone makes
every pot an open location that receives a placement entry and enters the digest,
changing every existing seed. This SHALL be verified by a corpus regen with a
3-way diff against unmodified `main`, not by a code-review claim of digest-neutrality.

#### Scenario: Existing seeds unchanged with pot-shuffle off
- **WHEN** the regression corpus is regenerated at the new `kGeneratorVersion`
  with `pot_shuffle = Off` (the default for all existing entries)
- **THEN** every existing seed's `placement_digest_hex` is byte-identical to its
  value on `main`

#### Scenario: Out-of-scope pots are skipped in the placement loops
- **WHEN** a seed is generated with `pot_shuffle = Off`
- **THEN** the open-location collection loop and the junk-pad target loop skip
  every pot location (the same way inactive Take-Any locations are skipped), so no
  pot receives a placement entry, draws fill RNG, or enters the digest — registry
  growth alone does not change the placement table

### Requirement: Pot keys are first-class shuffled checks under shuffled key modes

A dungeon's POT keys SHALL be genuine randomizer checks — a key pot can hold any placed
item and its small key can live elsewhere, NOT pinned to their own key — whenever
`pot_shuffle >= Keys` and small keys are shuffled (wild or dungeon) with door shuffle
off. This requires the key not to vanish from the pool (`randomizer-placement /
Pot-key small-key economy`) AND the dungeon's deep locations + pots to gate on the
key-door requirement the now-itemized keys add (`randomizer-logic / Pot-key small-key
logic gating`). The requirement is key-mode-dependent: under **wild** keys it is the
worst-case "hold N before entering"; under **dungeon** keys it is the in-context
shortest-path (min-depth) collected en route. In **vanilla** key mode the key pots stay
pinned, and under **door shuffle OR cave-entrance shuffle** every pot is forced inactive
(`Settings_PotShuffleForcedOff` — the door key-prover doesn't model pot locations, and a
cave/house pot's location id sits above the entrance region-override range so it would
evaluate from its vanilla overworld region; both full integrations are deferred follow-
ons). Cave-entrance shuffle is honored only on Open/Standard (`Settings_Effective-
ShuffleCaveEntrances`), so an inert cave bit retained under Inverted/Retro does NOT force
pots off — those seeds correctly generate WITH pots.

Because the key economy and the per-pot key-door gate both key off a pot's DUNGEON — and
because the same logic-region binding governs every loot/empty pot's reachability — the
generator's region binding for each pot room SHALL be the room's physically-correct
region, NEVER a neighbor's inherited via room-NUMBER grid-adjacency. The region flood
across reciprocal doors is valid only inside a multi-room dungeon; standalone cave/house
interiors (>= 0x100) and dungeon-boundary rooms require a reviewed
`pot_logic_overrides.yaml` entry grounded in the door tables, the fork's own location
predicate, or the overworld entrance world — not the grid-adjacency `D` edges (often
walls). Corrected mislabels: the Desert Palace key rooms `0x53`/`0x43` (had been bound to
Hyrule Castle Escape / Thieves' Town — and that fix had survived only as a hand-edit in
the generated `pots.gen.yaml`, which a regen silently reverted, so it is now in the
override store); and the overworld cave/house pot rooms `0x114` Pond-of-Wishing (→
DarkWorld_Mire), `0x11a` storyteller cave (→ DarkWorld_NorthEast), `0x119` Blind's Hideout
+ `0x11f` Lumberjack's House (→ LightWorld_NorthWest, were wrongly Dark World), `0x10c`
Mimic Cave (gains its Mirror-from-Turtle-Rock gate), and the shared `0x11b` refill cave
(SPLIT per entrance — a Graveyard-Ledge cluster + a Cave-45 cluster — via a new
`pot_room_split` mechanism that carries two reachability classes for one interior).

#### Scenario: A key pot holds a shuffled item, the key is elsewhere
- **WHEN** a seed has dungeon keys + `pot_shuffle = All` and is generated
- **THEN** a dungeon's small keys are distributed across its chests and key pots — a
  key pot commonly holds a non-key item while its key sits in another in-dungeon
  location — and the seed is 100% reachable at `accessibility = items`

#### Scenario: Vanilla keys still pin the key pots
- **WHEN** small keys are vanilla and `pot_shuffle >= Keys`
- **THEN** every key pot is identity-pinned and drops its own key in place, exactly
  like pots-off — placement is byte-identical to the same seed without the key pots
  participating

#### Scenario: Key-pot region binding is the physically-correct dungeon
- **WHEN** the pot registry is generated
- **THEN** every key pot's region/dungeon is its real in-game dungeon (Desert Palace's
  three pot keys are all bound to Desert Palace), so the key economy pools and gates
  the correct dungeon's `SmallKey_X` and no dungeon is left short or given a phantom key
