## ADDED Requirements

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

Under door shuffle OR cave-entrance shuffle `pot_shuffle` SHALL normalize entirely
to `Off`: `apply_derived_rules` sets `pot_shuffle = Off` whenever
`Settings_PotShuffleForcedOff` (= `Settings_EffectiveDoorShuffle != Vanilla` OR
`Settings_EffectiveShuffleCaveEntrances`, the latter honored only on Open/Standard), and
`pot_active()` returns false for EVERY pot (key and non-key) in that case, so all pots are
inactive and resolve to vanilla — a door+pots or cave-entrance+pots seed is byte-identical
to the same seed without pots, keeping the door-key prover (which does not model pot
locations) provably correct and preventing a cave/house pot from being certified against
its vanilla overworld region while the runtime reaches the interior through the shuffled
entrance. **The placer and logic consume RAW (non-normalized) settings** —
`Settings_CanonicalSerialize` runs `apply_derived_rules` only on a private copy for the
settings hash — so EVERY pot/accessibility predicate that branches on a normalized field
SHALL read it through the matching accessor (`Settings_PotShuffleForcedOff`,
`Settings_PotKeysActive`, `Settings_EffectiveShuffleCaveEntrances`,
`Settings_EffectiveAccessibility`), never the raw struct field, so the placer cannot
diverge from the canonical hash / spoiler / runtime (the audit-fixed raw-vs-normalized
bug class — door-only pot-key gates wrongly refusing cave+pot+wild/dungeon-keys seeds;
`goal=completionist,accessibility=none` skipping the 100%-locations walk). (An earlier
"pin key-pots as fixed vanilla keys and reduce the shuffled pool count" design was NOT
adopted: the prover's key count and the pool's key count are independently driven, so
a pinned key-pot — equally invisible to the prover — still risks an unprovable
softlock. Full door×pot integration, modeling pot-key locations inside the prover, is
a deferred follow-on phase.) The runtime pot grant SHALL dispatch through a single point keyed on
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

#### Scenario: Pots are inert under door OR cave-entrance shuffle (v1)
- **WHEN** a seed has `door_shuffle != vanilla` OR cave-entrance shuffle (on Open/Standard)
  and any `pot_shuffle` tier was requested
- **THEN** `pot_shuffle` normalizes to `Off` (`Settings_PotShuffleForcedOff`), every pot
  (key and non-key) is inactive and resolves to vanilla, and the seed's placement is
  byte-identical to the same seed generated without pot shuffle — so the door-key prover
  cannot strand and no cave/house pot is certified against a region the shuffled entrance
  moved it out of

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
