## ADDED Requirements

### Requirement: Entrance-permutation persistence via the header reserved tail

When an entrance shuffle is active for a generated slot, the sidecar slot SHALL
persist enough state to restore the entrance permutation π on slot load **without
storing the full permutation**, by carrying it additively in the previously-zero
header reserved tail and **regenerating π deterministically** at load. This
supersedes the original stubbed `TAIL_ENTRANCE_MAP` TLV design: the slot file
format has no TLV-skip infrastructure after the bitmap (unlike the snapshot-tail
format), and three multi-slot packers sum the fixed `RandoSave_SlotOnDiskSize`, so
a variable-length tail is not viable without a format break. Because π for a given
attempt is a pure function of `(seed, settings axes, attempt)`, regeneration is
cheaper and equally robust — the same precedent the Phase B hints settings
extension established.

The slot header (80 bytes, unchanged size) SHALL carry, in the reserved tail:
- `@70 entrance_axes` (uint8) — the packed entrance-axis byte (identical to the
  canonical settings byte `[25]`; `kEntranceAxis_*` bits). `0` ⇒ no entrance
  shuffle.
- `@71 entrance_attempt` (uint8) — the accepted goal-retry attempt index whose
  permutation π was used.

On slot load, when `entrance_axes` has the cave-shuffle bit set AND the slot's
`world_state` is in the supported set (Open/Standard), the runtime SHALL
regenerate π from the seed (recovered from the stored `share_string`),
`entrance_axes`, and `entrance_attempt`, then install the door overlay + per-seed
region overrides. When `entrance_axes == 0` the bytes are zero and the slot is
byte-identical (sans these two additive bytes, which older binaries already treat
as reserved) to a non-entrance-shuffle slot.

#### Scenario: Entrance-shuffle slot round-trip
- **WHEN** a coupled cave-shuffle slot is written and read back
- **THEN** `entrance_axes` + `entrance_attempt` round-trip byte-identical, and the
  runtime regenerates the SAME permutation π (so the same door→interior mapping
  and region overrides are restored)

#### Scenario: Older binary ignores the entrance bytes
- **WHEN** a Phase A or Phase B binary (no Phase C support) reads a Phase C
  entrance-shuffle slot
- **THEN** it reads the header through `@69` and treats `@70`/`@71` as reserved
  (ignored); the slot loads as a vanilla-entrance seed (graceful degradation — the
  door overlay is simply not installed)

#### Scenario: No entrance bytes when shuffle is off
- **WHEN** a Phase C binary writes a slot with no entrance shuffle active
- **THEN** `entrance_axes == 0` and `entrance_attempt == 0`; the slot is
  byte-identical to a Phase B slot for the same seed (the two additive bytes were
  already zero)
