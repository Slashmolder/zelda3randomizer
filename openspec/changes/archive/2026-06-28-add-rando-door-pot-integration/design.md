# Door shuffle x Pot shuffle integration - design

## Current Blocker

Door shuffle has two separate correctness surfaces:

1. `OP_DOORS_LOC_REACHABLE(loc)` runs the door explorer against
   `kDoorTblLocations`. Generated pot locations are not in that table, so a pot
   location currently has no door-oracle answer.
2. `DoorKeys_ShuffleDungeon()` proves the relocated small-key doors using the count
   of static door-table locations plus `kDoorTblDropKeys`. Pot keys are currently
   represented as free drop-key rows in the door table. Once pot shuffle itemizes
   those keys, the prover must stop counting them as free drops and must count the
   active pot checks as possible in-dungeon key-source locations.

Removing the settings guard without fixing both surfaces is unsound.

## Architecture

### Generated pot-door bridge

The bridge is emitted by `assets/rando_logic_gen.py` into `src/rando/logic_data.c`.
It is generated only when the local pot registry and key-depth artifacts are present.
Public/assetless builds continue to emit empty pot tables and fail closed when a user
requests an active pot tier.

The generated bridge row for each door-shuffled dungeon pot records:

- fork location id,
- door-table dungeon id,
- active tier threshold (`keys`, `contents`, or `all`),
- door-table region list,
- optional compiled pot-specific predicate,
- whether the pot's vanilla content is a small key,
- the matching `kDoorTblDropKeys` index for key pots.

Static `door_tables.gen.*` remain generated from the door-randomizer reference and do
not read gitignored pot data.

The generator also emits a stable `kRandoPotDoorBridgeDigest` over the bridge row
content and over whether the bridge is present. Door+pot generation and activation
must use that digest as part of the door-layout drift check. A build with
`pots.gen.yaml` present but no `door_pot_rooms` bridge artifact, stale key-depth
data, or missing drop-index joins is a codegen error rather than a partial feature.

### Region mapping policy

Key pots require exact door-region mapping. The key-depth dump already joins key-pot
drops to door regions; the bridge must use that exact region and must hard-fail on an
ambiguous key-pot join.

Loot and empty pots may be conservative. If exact per-position door-region data is
not available, the bridge uses the full set of door-table regions for that room and
requires all of them to be reached before certifying the pot. This can delay a
non-key check, but it cannot strand a key behind the door it opens. A future change
may refine non-key pots to exact per-position regions.

Some pot-table rows are attached to broad dungeon logic regions even though their
SNES room is outside the generated door-table graph. Those non-key pots do not get a
generated door bridge row; they keep the existing vanilla/fork predicate path. Key
pots are not allowed to use that fallback.

### Oracle behavior

For an existing door-table location, `OP_DOORS_LOC_REACHABLE(loc)` keeps the current
path: binary-search `kDoorTblLocations`, flood with `DoorExplore_Run`, check the
location region, and evaluate the static door-table rule.

For a generated pot-door row, the same op:

- verifies the row's tier is active under the effective settings,
- floods the row's dungeon through the same `door_oracle_get()` path,
- requires every mapped door region in the row to be reached,
- evaluates the generated pot predicate against the fork predicate VM when present.

The active branch of a wrapped pot predicate uses the door-oracle answer and does
not carry the static vanilla-door `POT_KEYS_*` key-depth terms. The inactive/pinned
branch keeps the existing vanilla predicate so pots-off and pinned-dungeon behavior
stay unchanged.

To make that possible, codegen keeps two predicate forms for pots:

- the existing full predicate, including `POT_KEYS_*`, used by non-door seeds and
  by the inactive/pinned branch;
- a base pot predicate captured before the static pot-key-depth terms are appended,
  used only by the generated active door branch.

The generated active-door predicate must not call `OP_DOORS_LOC_REACHABLE` from a
door VM leaf, and it must not recursively re-enter the door oracle.

### Door key prover behavior

`DoorShuffle_Generate()` receives the effective pot tier used for that generation and
persists it in the in-memory `DoorShuffleLayout` digest input. For the active tier:

- active non-empty pot rows in a dungeon count as free/key-source locations for
  `AvailSmall()` just like static door-table chest locations. Empty pots in the
  `all` tier are pre-pinned to `ITEM_Nothing` and must not count as possible key
  sources;
- the maximum itemized small-key count for the dungeon becomes
  `door_table.chest_small_keys + active_key_pots`;
- key-pot rows matching `kDoorTblDropKeys` are excluded from free drops through one
  shared helper used by the prover, `DoorExplore_Core`, `Door_CountDropKeys`, door
  self-tests, and key-depth dumping;
- non-pot drop-key rows remain free drops;
- door count and worst-case threshold caps still use total vanilla keys
  (`chest_small_keys + all drop rows`) because itemized pot keys plus non-pot drops
  preserve the total key count.

Big-key accounting is separate from small-key-source accounting. If active pots are
not allowed to hold big keys, they must be excluded from the prover's big-key
availability count as well as rejected by placement. The preferred implementation is
to include active non-empty pot rows in the big-key analysis and emit a
digest-covered `bk_restricted` bitset sized by `kRandoLocationCapacity`; placement
then rejects exactly the restricted loc ids. A conservative pot-wide big-key ban is
valid only if those active pots are also excluded from big-key availability inside
the prover.

### Settings and placement

`Settings_PotShuffleForcedOff()` becomes cave-entrance-only. Door shuffle still
forces small and big keys to dungeon mode.

`pot_active()` continues to be the single placement filter. It no longer treats door
shuffle as a forced-off case. `Settings_PotKeysActive()` therefore becomes true for
door+pot seeds when the tier is at least `keys`.

### Validation strategy

Required local validation:

- `python assets/scripts/check_door_tables.py`
- `python assets/scripts/check_codegen_wiring.py`
- `python assets/scripts/check_corpus_version_sync.py`
- `python assets/scripts/run_rando_corpus.py --schema-only`
- `python assets/scripts/check_logic_overrides.py`
- `python assets/scripts/check_placer_determinism.py --source-only`
- `git diff --check`
- Release x64 build
- `--rando-selftest`
- `--door-selftest`
- local pot artifact preparation and checks when a binary is available
- full corpus against a local binary when pot artifacts are present
- `openspec validate add-rando-door-pot-integration --strict`

Focused generation smoke tests must include `door_shuffle=basic,pot_shuffle=keys`,
`contents`, and `all`, plus dungeon-key and wild-key raw requests that normalize to
dungeon keys under door shuffle.

Additional required self-check coverage:

- generated bridge digest and bridge-present/bridge-absent drift paths;
- key-pot drop-row exclusion in every shared drop-count call path;
- active empty pots excluded from key-source counts;
- prover big-key availability agrees with placement's big-key restriction;
- assetless and partial-artifact builds fail closed for active door+pot requests.
