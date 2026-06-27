## MODIFIED Requirements

### Requirement: Door-shuffle layout digest and pot-tier input

Door-shuffle layout generation SHALL include the effective pot tier and the generated
pot-door bridge digest as explicit inputs. The accepted layout's digest SHALL cover
that model data so a slot generated with active pot locations cannot later be
activated with a door layout proven under the pots-off model or under different local
pot-door metadata.

#### Scenario: Door layout regenerated with matching pot model

- **WHEN** a saved door+pot seed is activated
- **THEN** runtime regeneration uses the canonical settings' effective pot tier
- **AND** the regenerated bridge digest and layout digest match the digests accepted
  during generation

#### Scenario: Pot-tier drift fails activation

- **WHEN** code changes cause the same saved door+pot seed to regenerate a different
  layout or prover model
- **THEN** the door digest check fails activation instead of installing mismatched
  runtime reachability

#### Scenario: Bridge drift fails activation

- **WHEN** a door+pot slot was generated with one pot-door bridge and the current
  build has different bridge rows, predicates, drop-index joins, or no bridge
- **THEN** activation refuses the slot through the same hard-fail path as door layout
  digest drift
