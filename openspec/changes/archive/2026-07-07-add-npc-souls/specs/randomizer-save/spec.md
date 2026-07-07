# Delta: randomizer-save (NPC souls)

## ADDED Requirements

### Requirement: Sidecar format_version 7 widens soul ownership

The sidecar SHALL bump to format_version 7, appending the widened soul-flag bytes (total at least 12) to the per-slot extension block. Readers SHALL zero-fill NPC-soul bits when loading format_version ≤ 6 files, and version gating SHALL follow the existing convention (older readers reject newer files; newer readers accept older files with safe defaults).

#### Scenario: v6 file loads safely
- **WHEN** a format_version-6 sidecar (written by the enemy-souls build) is loaded by a v7 build
- **THEN** all slots load, enemy/boss soul bits are preserved, and every NPC-soul bit reads as un-owned

### Requirement: Snapshot-tail souls TLV is length-gated

The type-8 souls TLV SHALL carry the widened payload; readers SHALL accept both the legacy 8-byte and the widened payload, zero-filling the tail for legacy snapshots, and cold-replay reconstruction SHALL restore the full widened field.

#### Scenario: Legacy snapshot replays
- **WHEN** a snapshot recorded before the widening (8-byte type-8 payload) is cold-replayed on a widened build
- **THEN** the replay succeeds with enemy/boss souls restored and NPC souls zeroed
