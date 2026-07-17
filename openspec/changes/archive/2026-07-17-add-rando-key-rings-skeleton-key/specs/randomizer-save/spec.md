## ADDED Requirements

### Requirement: Key-item settings and derived ownership persistence

Sidecar format version 10 SHALL persist the 31-byte canonical settings blob. The
v10 reader SHALL accept versioned v1..v9 bodies and zero-extend their missing
key-item byte. The v10 reader SHALL refuse future versions and trailing bytes.
Version 10 is not backward-compatible with pre-v10 readers; their behavior is outside this
change's compatibility guarantee and SHALL be documented as unsupported.
Ring/Skeleton ownership SHALL be reconstructed from placements plus checked bits
after sidecar or snapshot restore rather than stored as an independent authority.

#### Scenario: Older slot loads with both features off

- **WHEN** a v10 binary loads a supported v9 sidecar slot
- **THEN** its 30-byte settings blob is zero-extended, Key Rings and Skeleton Key
  are Off, and the slot otherwise retains its existing placements/checks

#### Scenario: Checked ownership survives snapshot replay

- **WHEN** a snapshot restores placements and a checked bitmap containing collected
  ring and Skeleton locations
- **THEN** ownership caches are rebuilt before gameplay resumes

#### Scenario: Deactivation clears derived ownership

- **WHEN** a randomizer slot is deactivated
- **THEN** no Key Ring or Skeleton ownership leaks into the next slot/session
