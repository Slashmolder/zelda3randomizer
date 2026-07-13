## ADDED Requirements

### Requirement: Key-item settings and derived ownership persistence

Sidecar format version 9 SHALL persist the 31-byte canonical settings blob. The v9
reader SHALL accept versioned v1..v8 bodies and zero-extend their missing key-item
byte; pre-v9 readers SHALL refuse v9 under the existing sequential-body rule.
Ring/Skeleton ownership SHALL be reconstructed from placements plus checked bits
after sidecar or snapshot restore rather than stored as an independent authority.

#### Scenario: Older slot loads with both features off

- **WHEN** a v9 binary loads a supported v8 sidecar slot
- **THEN** its 30-byte settings blob is zero-extended, Key Rings and Skeleton Key
  are Off, and the slot otherwise retains its existing placements/checks

#### Scenario: Checked ownership survives snapshot replay

- **WHEN** a snapshot restores placements and a checked bitmap containing collected
  ring and Skeleton locations
- **THEN** ownership caches are rebuilt before gameplay resumes

#### Scenario: Deactivation clears derived ownership

- **WHEN** a randomizer slot is deactivated
- **THEN** no Key Ring or Skeleton ownership leaks into the next slot/session
