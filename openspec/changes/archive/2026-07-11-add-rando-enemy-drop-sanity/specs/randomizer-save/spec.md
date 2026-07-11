## ADDED Requirements

### Requirement: Enemy-drop checked state and snapshot safety

Enemy-drop check locations SHALL use the existing checked-location bitmap and
placement-table sizing model. Newer binaries SHALL read older slots with no enemy-drop
ids as valid prefixes and default the setting to `Off`. Older binaries that cannot
hold a larger placement table or checked bitmap SHALL refuse expanded slots
non-destructively according to `Embedded placement table - upgrade safety` and
`Checked-location bitmap read invariant`.

Transient in-room enemy-drop identity SHALL be snapshot-safe. Phase 1 stores the
carrier source slot in existing snapshot-persisted sprite state and recomputes the
location id from the generated `(room, source_slot, drop_kind)` lookup at pickup time.
If a later phase adds a separate pending table, that table SHALL either live in
reserved `g_ram` captured by F-key snapshots or be serialized in a snapshot-tail TLV.
Plain save/reload before pickup MAY recompute the unchecked state from the generated
registry when the room reloads.

#### Scenario: Older slot loads with enemy-drop checks off
- **WHEN** a pre-enemy-drop sidecar slot is loaded by a newer binary
- **THEN** `enemy_drop_checks` defaults to `Off`, no enemy-drop checked bits are
  required, and the slot loads through the normal prefix-compatible path

#### Scenario: Snapshot before pickup preserves retry semantics
- **WHEN** the player snapshots after killing an active forced-key carrier but before
  collecting the drop, then restores the snapshot
- **THEN** the pending location identity is restored or recomputed, the location is
  still unchecked, and the drop/check remains collectable exactly once

#### Scenario: Old binary refuses expanded slot safely
- **WHEN** an older binary reads a slot whose placement table or checked bitmap exceeds
  its supported capacity
- **THEN** it refuses the slot non-destructively rather than truncating enemy-drop
  checked state
