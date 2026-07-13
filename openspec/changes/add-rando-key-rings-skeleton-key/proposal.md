## Why

The randomizer currently emits one progression item for every shuffled small key.
That is manageable for the 29 chest/placed keys in the base pool, but key-heavy
Wild seeds become increasingly noisy when pot sanity and enemy-drop sanity turn
the dungeons' additional key sources into checks. A player may need to find many
copies of the same dungeon key before the seed meaningfully changes.

OoTR's key-ring idea gives us a useful alternative: one dungeon-specific item can
stand for that dungeon's whole small-key stock. It preserves the dungeon identity
and key-door logic while reducing repetitive progression checks. Per-seed mixing
also lets one dungeon use a ring while another retains ordinary keys.

A Skeleton Key is a complementary optional toy. It should let the player open any
small-key door once found, but it should never be required by generation. That
makes it a fun sequence-breaking bonus rather than another mandatory progression
axis.

## What Changes

- Add `key_rings=off|random|all`, default `off`.
  - `off`: every dungeon keeps its ordinary shuffled small-key copies.
  - `random`: choose a deterministic non-empty, non-total subset of eligible key
    families, guaranteeing rings and regular keys are both present.
  - `all`: every eligible key family becomes a ring.
- Add one appended Key Ring item for each of the 13 randomizer dungeon key
  families. A family is eligible only when its pre-collapse shuffled pool
  actually contains at least one dungeon-specific small key; this lets Eastern
  Palace become eligible when itemized pot/enemy sources contribute Eastern keys
  without inventing a ring on ordinary seeds where Eastern has no shuffled key.
- Collapse every shuffled small-key copy for a selected family—including active
  pot and enemy-drop key copies—into exactly one ring. Junk padding fills the
  released slots, so location count and the one-check contract remain stable.
- Treat a held ring as satisfying every small-key count requirement for its
  family, including door-shuffle oracle thresholds and item-placement predicates.
  At runtime the ring grants the family's complete authored key stock into the
  existing per-dungeon counter.
- Add `skeleton_key=false|true`, default `false`. When enabled, exactly one
  Skeleton Key replaces one junk slot. It opens small-key doors without consuming
  a regular key, does not open big-key doors, and is classified as a bonus/non-
  progression item so every seed remains beatable without it.
- Add custom receipt art, names, spoiler output, tracker/autotracker state, native
  settings controls, and documentation for both item families.
- Grow canonical settings from 30 to 31 bytes, update v2 share strings, sidecar
  format/versioned settings reads, suppressed-spoiler sizing, snapshots, generator
  version, and the corpus.

## Capabilities

### New Capabilities

- `randomizer-key-items`: deterministic key-ring selection, one-check pool
  collapse, ring grants, Skeleton Key ownership, and the small-key-door runtime
  behavior.

### Modified Capabilities

- `randomizer-core`: new settings axes, appended item IDs, canonical byte `[30]`,
  share-string sizing, and generator-version provenance.
- `randomizer-placement`: key-source collapse, dungeon confinement, customizer
  validation, and logic-neutral Skeleton Key insertion.
- `randomizer-logic`: ring-aware key counts and placement-item aliases; no Skeleton
  Key reachability effect.
- `randomizer-door-runtime`: per-family ring counters and the Skeleton Key bypass.
- `randomizer-save`: 31-byte settings persistence and derived ownership rebuild.
- `randomizer-pot-sanity` / `randomizer-enemy-drop-sanity`: active itemized key
  sources participate in the selected family's collapse.
- `randomizer-shuffles` / `randomizer-dungeon-chains`: door oracle and chained
  dungeon traversal consume the same ring-aware key model.
- `randomizer-ui`: settings, effective-state explanations, spoiler, tracker,
  autotracker, names, and icons.

## Non-Goals

- The Skeleton Key does not open big-key doors, replace Big Keys, satisfy Big Key
  logic, or change boss/prize gates.
- Key rings do not apply to Retro's shared `GenericKey` pool and do not turn
  Vanilla-mode small keys into shuffled checks.
- Rings do not remove vanilla/free key drops that are not active randomizer
  locations. Such surplus pickups are harmless; all randomized copies are still
  collapsed.
- The Skeleton Key is not guaranteed reachable under beatable-only accessibility,
  is not assumed during fill, and does not certify any sphere or goal.
- This change does not introduce a user-edited per-dungeon ring checklist. The
  requested modes are Off, deterministic Random, and All.

## Impact

- **Settings/provenance**: `RandoSettings`, canonical serialization/deserialization,
  v2 share transport, sidecar format, suppressed-spoiler payload, generator
  version, corpus manifest, settings UI persistence, and CLI grammar.
- **Items/art**: append-only `item_registry.yaml` IDs 220..233 (13 rings plus the
  Skeleton Key), generated item IDs/names/dispatch/icon tables, and two shared
  custom receipt/field-item graphics.
- **Placement/logic**: `BuildItemPool`, progression/dungeon-item classification,
  assumed-fill counts, `OP_ITEM_IS` equivalence, door oracle held-key input,
  customizer validation, sphere/goal verification, and deterministic selection.
- **Runtime/save**: dungeon-item direct grant, the small-key-door consumption site,
  per-dungeon/live counters, placement+checked-derived ownership caches, slot
  activation, snapshot restore, tracker/autotracker, and HUD refresh.
- **Verification**: strict OpenSpec validation, source/codegen guards, settings /
  placement / logic / runtime selfchecks, door selftest, corpus interaction rows,
  save/snapshot tests, and owner playtests.
