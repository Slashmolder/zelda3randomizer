# randomizer-save — delta for add-rando-ow-warp-shuffle

## ADDED Requirements

### Requirement: Warp layout persists as attempt + digest and regenerates gated

The sidecar slot SHALL persist the warp layout as `ow_attempt` (u8, ext
@58) plus `ow_digest24` (24-bit layout digest, 3 bytes LE, ext @59-61),
growing the extension block 58 → 62 under a `format_version` bump 11 → 12
(implementation SHALL confirm 12 is unused — the version history skips
numbers) gated in BOTH directions (older binaries refuse newer files; the
new binary reads older files with both fields absent = vanilla warps). The layout itself is never
serialized: activation SHALL regenerate it from (seed, canonical settings,
`ow_attempt`), recompute the digest, and HARD-FAIL slot activation on
mismatch or on a warp-axis slot meeting an empty compiled graph — the
certified-placement drift class that warrants refusal, not a warning. The
snapshot cold-replay path SHALL carry the same two fields in the existing
settings-restore TLV payload without changing the TLV count, so a cold
replay reconstructs the identical warp state or refuses by the same gates.
Slot deactivation SHALL tear down all installed warp state symmetrically.

#### Scenario: Round trip reproduces the layout
- **WHEN** a warp-axis slot is saved, the game restarts, and the slot is
  activated
- **THEN** the regenerated spot list and pair table match the generation-time
  layout exactly (digest equality), and the flute map shows the same blips

#### Scenario: Older binary refuses the newer sidecar
- **WHEN** a pre-change binary opens a sidecar written at the bumped
  format_version
- **THEN** it refuses the file per the established two-way gating rather
  than misparsing the extension block

#### Scenario: Cold replay is covered
- **WHEN** a snapshot recorded on a warp-axis slot is replayed on a fresh
  launch (no sidecar activation preceding it)
- **THEN** the rebuilt slot state includes the identical warp layout, or the
  replay refuses on digest/graph mismatch — it never silently plays with
  vanilla warps against a shuffled placement
