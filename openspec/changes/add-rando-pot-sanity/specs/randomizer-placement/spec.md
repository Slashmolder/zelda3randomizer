## ADDED Requirements

### Requirement: Pot locations in assumed-fill with raised capacity and pot dispatch

When the `pot_shuffle` tier selects them, pot locations SHALL enter the assumed-fill
location pool like any other location; the remaining pot slots are filled by the
existing junk padder. **Pot-key locations are dungeon small-key locations and follow
the dungeon's `dungeon_small_keys_mode` exactly like vanilla key locations** — the
dungeon's vanilla small-key COUNT is fixed and a pot-key is one of those existing keys,
never an extra: in **vanilla key mode** (the default) the pot-key location is
identity-pinned to the pot (the existing vanilla key pre-seed/pin path SHALL be
extended to include pot-key locations, since it currently knows only vanilla *chest*
key locations); in **shuffled key modes** the pot-key location is open and the fixed
count is distributed across the dungeon's key-eligible locations incl. pots
(`Assumed-fill awareness of per-seed dungeon key-door logic`). **Empty-pot locations
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
module SHALL be raised to a single 2048 ceiling (`328 + 835 = 1163`) by a **typed
audit, NOT a `512` grep** — `1163` exceeds BOTH the 512 caps (placer working arrays
+ session buffer + in-memory checked-bitmap; the placement-digest cap `kDigestLocalCap`
+ its buffer, which otherwise silently TRUNCATES the digest at 512; the reachability
OOB guards) AND the 1024 caps (the auto-tracker / native-tracker / reach-panel
`s_loc_*[1024]` tables; the customizer probe cap; `kSpoilerMaxRows`; and
`rando_snapshot_tail`'s `raw[1024]` + `> 1024` reject — past which locations are
silently DROPPED). Each raised capacity SHALL carry a `_Static_assert` tying it to
`LOC__COUNT` (≥) so a future overflow / truncation / drop is a build break, not a
silent fail-open.

Under door shuffle (`door_shuffle != vanilla`, which forces Dungeon key mode so the
full vanilla key count enters the shuffled pool), key-pots SHALL be excluded from the
active pot checks AND pinned as fixed vanilla keys, AND the shuffled key-pool count for
each affected dungeon SHALL be **reduced by the pinned pot-key count** — keeping the
pot's vanilla key alone would duplicate it (once pinned, once pooled). Non-key pots
still shuffle. (The door key prover runs over its own namespace; integrating pot-key
locations into it is the long-term fix and is deferred.) The runtime pot grant SHALL dispatch through a single point keyed on
`(dungeon_room, tile_position) → location_id` (`randomizer-pot-sanity / Single-point
runtime pot dispatch …`), subject to the existing `Trigger-based location re-collect
safety` invariant: a checked pot is never re-granted. `Placement_Lookup` SHALL use a
sorted table + binary search; the sorted invariant SHALL hold at EVERY install
boundary (assumed-fill output, sidecar deserialization, snapshot-tail reinstall,
customizer, race/spoiler reveal, tests), enforced by a `--rando-selftest` sortedness
check and a sort-on-install fallback.

#### Scenario: Pot key follows the dungeon key mode (count-preserving)
- **WHEN** `pot_shuffle` includes a small-key pot
- **THEN** in vanilla key mode the pot-key is identity-pinned to the pot (not pooled);
  in shuffled key modes it is placed logic-aware across the dungeon's key locations —
  in both cases the dungeon's vanilla key count is preserved, never an extra key

#### Scenario: Capacity covers the maximal pool
- **WHEN** `pot_shuffle = All` in a Retro seed (the largest combined pool)
- **THEN** the placement working arrays, session buffer, and checked-bitmap hold
  every location without overflow, and the capacity `_Static_assert` passes at
  build time

#### Scenario: Out-of-scope pot falls back to vanilla
- **WHEN** a pot is not selected by the active tier and the player breaks it
- **THEN** it has no placement entry (or a `0xFFFF` sentinel) and the dispatcher
  reveals the vanilla content, with no recolor and no check

#### Scenario: Dispatch stays cheap at scale
- **WHEN** a pot is broken and the runtime resolves its placed item
- **THEN** `Placement_Lookup` resolves via binary search (not an O(N) linear scan),
  so frequent pot-breaks do not degrade frame timing at ~1163 locations

#### Scenario: ITEM_Nothing is pre-placed, never on a real location
- **WHEN** `pot_shuffle = All` and the placer runs
- **THEN** empty-pot locations are filled with `ITEM_Nothing` in the dedicated
  pre-pass and removed from the open set, so assumed-fill and junk padding never place
  `ITEM_Nothing` on a chest/non-empty pot nor a real item on an empty pot

#### Scenario: Key-pots are inert under door shuffle without duplicating keys (v1)
- **WHEN** a seed has `door_shuffle != vanilla` and a key-pot tier is selected
- **THEN** key-pots are pinned as fixed vanilla keys, the shuffled key-pool count is
  reduced by the pinned count (the dungeon's total key count is unchanged — no
  duplication), non-key pots still shuffle, and no unbeatable key-before-door placement
  results

#### Scenario: Placement table is sorted at every install boundary
- **WHEN** a placement table is installed (assumed-fill, sidecar load, snapshot-tail,
  customizer, or reveal)
- **THEN** its entries are sorted by location_id (sorted-on-install where a producer
  can't guarantee it), and `--rando-selftest` asserts sortedness so the binary-search
  `Placement_Lookup` is always correct
