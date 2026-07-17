## ADDED Requirements

### Requirement: Itemized enemy keys participate in key-ring collapse

Every active forced-enemy location whose vanilla item is a dungeon small key SHALL
add that family's key to the pre-ring multiset before selection and collapse. This
rule SHALL compose with pot keys, and an Eastern enemy key SHALL make Eastern
eligible even though its base chest-key count is zero. Checked enemy carriers SHALL
retain the existing one-shot grant/respawn behavior; only the placed item identity
changes.

#### Scenario: Combined key sources collapse together

- **WHEN** a selected family has base, pot, and forced-enemy shuffled key copies
- **THEN** all three sources contribute to one collapse and the final pool has one
  ring and no ordinary family key
