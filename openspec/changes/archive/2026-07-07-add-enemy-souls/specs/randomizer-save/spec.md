# randomizer-save Specification (delta)

## ADDED Requirements

### Requirement: Soul ownership in sidecar v5 extension block and snapshot tail
The sidecar format SHALL gain a version-5 extension block carrying the 8-byte soul-ownership bitfield (bit index = soul item ID minus the first soul ID), and the snapshot tail SHALL gain a new TLV type carrying the same bitfield for cold-replay slot reconstruction; readers SHALL treat an absent block/TLV (pre-v5 files) as zero souls owned.

#### Scenario: v5 sidecar round-trip
- **WHEN** a slot with owned souls is saved and the sidecar re-activated
- **THEN** the soul bitfield is restored exactly and suppression reflects it

#### Scenario: Pre-v5 sidecar accepted
- **WHEN** a v4-or-earlier sidecar is activated by a souls-aware build
- **THEN** it loads without error and the soul bitfield initializes to zero

#### Scenario: Cold replay restores souls
- **WHEN** a snapshot containing the souls TLV is replayed on a cold start (no live slot)
- **THEN** the rebuilt slot's soul ownership matches the snapshot's bitfield
