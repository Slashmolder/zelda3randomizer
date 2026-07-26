# randomizer-save Specification (delta)

## ADDED Requirements

### Requirement: Per-slot discovery-state persistence (slot extension v13)

The sidecar per-slot extension SHALL grow one format version (v12 → v13)
carrying the player's topology-discovery state: dungeons-entered bits, cave
interiors entered, whirlpool pairs ridden, decoupled cave exits traversed, and
reserved door-discovery bits for the deferred door phase — sized in this single
bump so later phases of the player-knowledge capability need no further format
change. The same state
SHALL round-trip through a snapshot-tail TLV so M4 cold replay restores it.
Serializer, deserializer, size constants, and the TLV SHALL change in one
commit (the sidecar coupling rule), and the existing sidecar round-trip
selfchecks SHALL cover the new block.

#### Scenario: Discovery survives save and reload

- **WHEN** the player discovers a hidden-identity dungeon, saves, quits, and
  reloads the slot
- **THEN** the dungeon renders as discovered in the tracker surfaces without
  re-entering it

#### Scenario: Snapshot cold replay restores discovery

- **WHEN** a snapshot taken after a discovery is replayed cold in a fresh
  process
- **THEN** the rebuilt slot carries the discovery state from the TLV

### Requirement: Discovery backfill for older saves and snapshots

Loading a v12-or-older sidecar slot (or a snapshot without the discovery TLV)
SHALL backfill discovery exclusively from checked-location state: a checked
location whose region belongs to dungeon X marks X entered; a checked location
behind a shuffled cave door marks that interior entered. Backfill SHALL never
derive discovery from placement, settings, or layout data — checked state is
the only source, because having checked a location implies the player was
there, so backfill can under-reveal but never over-reveal.

#### Scenario: In-flight save does not regress to all-hidden

- **WHEN** a pre-v13 save with checked locations inside two shuffled dungeons
  is loaded
- **THEN** exactly those two dungeons load as discovered and every other
  hidden-identity dungeon stays hidden
