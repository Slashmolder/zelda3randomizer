# Proposal: tracker-player-knowledge

## Why

The tracker surfaces (Check/Map tracker windows, Reachability panel, auto-tracker
export) display live reachability computed over the TRUE shuffled topology, so a
dungeon-shuffle or dungeon-chains seed shows "Misery Mire — 4 avail" the moment
some accessible door leads there — revealing which dungeon is behind an
unentered door before the player could possibly know. The same class covers cave
contents listed under their rebound overworld region (cave-entrance shuffle) and
destination-side availability through un-ridden whirlpools and untraversed
shuffled doors. Owner-set governing principle: **a tracker may only reveal
information the player can possibly know at that moment** — everything a tracker
states must be true under every assignment consistent with what the player has
observed in-game so far.

## What Changes

- **Persisted per-slot discovery state** (dungeons entered, cave interiors
  entered, whirlpools ridden; door bits reserved) written at runtime choke
  points, stored in a sidecar slot-extension bump (v12 → v13) and a snapshot-tail
  TLV, with a checked-location backfill for pre-existing saves.
- **Knowledge-limited live reachability**: `Rando_GetLiveReachability()` becomes
  the player-knowledge view — dungeons whose identity is hidden by the active
  settings (dungeon-entrance shuffle pool, chains pool) are excluded from the
  region flood until first entry (their checks stay unavailable AND nothing
  propagates through them, closing the aux-exit/ledge pass-through channel);
  un-ridden shuffled whirlpool edges are skipped. Unconditional — no
  reveal-topology toggle (owner decision). Generation/placer reachability is
  untouched (mask provably absent on that path; corpus byte-identical).
- **Cave contents join the same knowledge view** (premise corrected during
  grounding: tracker names/grouping are static and never leaked, so no
  aggregation rows or search-filter hardening are needed — the cave leak is the
  availability channel): an undiscovered shuffled cave interior's locations are
  suppressed from the live reachable set, and the decoupled variants' cave-exit
  added edges are gated on interior discovery.
- **API hardening as a durable guard**: the full-knowledge flood keeps an
  explicit greppable internal name; a new CI check script (modeled on
  `check_grant_consumers.py`) fails on new full-knowledge/assignment-getter
  consumers outside an allowlist, so future player-facing surfaces are
  knowledge-limited by default.
- **Audit closure**: the change carries a verified leak-audit matrix (design.md)
  over every shuffled-assignment producer × every player-facing consumer,
  including in-game (SNES-rendered) surfaces, with explicit verdicts; a
  post-implementation fresh-eyes audit re-checks the invariant.
- **Deferred final phase (owner decision): door shuffle knowledge gating** —
  intra-dungeon reachability limited to traversed doors (`DoorRt` discovery bits,
  persisted). Flagged, scheduled at the very end after everything else wraps.
- Out of scope (owner decisions): debug widgets (cheat tools by nature);
  accepted-LOW inference leaks — boss-shuffle kill-predicate inference (bosses
  are visible on approach) and medallion-requirement inference (the requirement
  is deliberately rendered at the MM/TR entrance in-game). Documented in the
  audit matrix, not gated.

## Capabilities

### New Capabilities

- `randomizer-player-knowledge`: the player-knowledge invariant itself, the
  discovery model (what counts as "observed", per axis), the knowledge-limited
  reachability semantics, discovery persistence, and the future-axis obligation
  (any new topology/assignment axis must define its discovery model to pass
  review). Also carries the deferred door-shuffle gating requirement.

### Modified Capabilities

- `randomizer-logic`: the live-reachability bridge gains a knowledge-exclusion
  input (region/edge suppression); the generation-side compute is explicitly
  required to run mask-free.
- `randomizer-native-window`: Check/Map tracker and Reachability panel
  requirements gain knowledge-limited display semantics ("(unexplored)" tags,
  cave aggregation rows, search-filter hardening, undiscovered-dungeon summary
  line).
- `randomizer-ui`: the auto-tracker server's `reachable` array becomes
  knowledge-limited; its spoiler-safety contract is restated to the invariant.
- `randomizer-save`: sidecar per-slot extension v13 (discovery state), snapshot
  tail TLV, and the checked-location backfill rule for older saves/snapshots.

## Impact

- **Code**: `src/rando/rando_logic.c` (flood exclusion input), `src/rando/rando.c`
  (live bridge, discovery marking, memo key, activation backfill),
  `src/rando/rando_save.{h,c}` + `rando_snapshot_tail.c` (v13 + TLV),
  `src/rando/rando_window/tracker_windows.cpp` + `rando_reach_panel.cpp`
  (display), `src/rando/auto_tracker.c` (comment/contract), new
  `assets/scripts/check_knowledge_consumers.py` + CI wiring, `--rando-selftest`
  group.
- **Determinism**: no `kGeneratorVersion` bump; corpus regen A/B against
  unmodified main must be byte-identical (the proof the placer path is
  mask-free).
- **Persistence**: sidecar format v12 → v13 (single bump sized for all phases,
  including reserved door bits); older sidecars/snapshots load with backfilled
  discovery.
- **Player-facing**: seeds with no topology axes behave exactly as today (empty
  mask). Topology seeds show fewer "avail" checks until discovery — matching
  community entrance-rando tracker conventions.
