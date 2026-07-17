## ADDED Requirements

### Requirement: Key-item settings and append-only registry entries

`RandoSettings` SHALL add `key_rings` (`off=0`, `random=1`, `all=2`) and boolean
`skeleton_key`, both default off. Canonical byte `[30]` SHALL encode key rings in
bits 0-1 and Skeleton Key in bit 2; bits 3-7 SHALL be refused until defined.
`kSettingsCanonicalLen` SHALL grow 30→31. Item registry IDs 220..232 SHALL be the
13 dungeon Key Rings in small-key-family order and ID 233 SHALL be Skeleton Key,
without changing any existing item ID.

#### Scenario: Default append-only byte

- **WHEN** default settings are canonicalized
- **THEN** byte `[30]` is `0x00`, all previous 30 bytes retain their field
  meanings, and no existing item ID changes

#### Scenario: Older settings decode safely

- **WHEN** a share, sidecar, snapshot, or other versioned outer reader accepts a
  supported older settings payload that ends before byte `[30]`
- **THEN** that reader zero-extends a 31-byte buffer before fixed-length canonical
  deserialization, restoring `key_rings=off` and `skeleton_key=false`

#### Scenario: Unsupported tail bits are refused

- **WHEN** canonical byte `[30]` has any bit 3-7 set
- **THEN** settings deserialization refuses the blob rather than guessing a
  future feature

### Requirement: Key-ring effective-setting normalization

Canonical settings SHALL preserve the requested Key Rings mode. The single
effective-mode resolver SHALL derive Off when effective small keys are Vanilla or
Retro generic keys are active; otherwise Dungeon and Wild keys SHALL retain the
requested mode. Skeleton Key SHALL remain independent. Canonical hashing, shares,
sidecars, snapshots, and race-suppressed settings SHALL retain the request, while
placement, logic, runtime, and ring selection SHALL consume the effective mode.
UI and spoiler output SHALL report both requested and effective modes.

#### Scenario: Retro uses one collapse model

- **WHEN** world state is Retro and `key_rings=all` is requested
- **THEN** effective Key Rings are Off, the shared GenericKey model remains
  authoritative, and a requested Skeleton Key remains enabled

#### Scenario: Door shuffle supports rings

- **WHEN** door shuffle forces effective small keys to Dungeon and
  `key_rings=random` is requested
- **THEN** Random remains effective and the canonical settings retain the Random
  request

### Requirement: Share and generator provenance for key-item settings

The generator version SHALL bump for the new placement semantics. V2 share strings
SHALL carry the 31-byte canonical blob (`47` raw bytes total, `76` unpadded base32
characters); older shorter v2 strings SHALL zero-extend and longer future strings
SHALL be refused. V1 identity-only strings SHALL retain their 50-character wire
format and identity role, while token values SHALL change with the bumped generator
version and canonical-settings hash.

#### Scenario: Current v2 exact length

- **WHEN** a v2 share string is encoded with 31-byte canonical settings
- **THEN** its raw payload is 47 bytes and its base32 token is exactly 76
  characters
