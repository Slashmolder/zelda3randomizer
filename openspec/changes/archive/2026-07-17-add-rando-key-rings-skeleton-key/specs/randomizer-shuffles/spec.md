## ADDED Requirements

### Requirement: Door shuffle consumes the ring-aware key model

Door-shuffle layout reachability SHALL treat a held family Key Ring as sufficient
for every worst-case small-key threshold and SHALL include ring ownership in its
cache/fingerprint state. Ring selection SHALL not consume the door-layout or main
fill RNG stream. Skeleton Key SHALL be ignored by the door-shuffle generation
oracle even though runtime may use it as a bonus bypass.

#### Scenario: Door layout remains logically beatable without Skeleton Key

- **WHEN** door shuffle, Key Rings, and Skeleton Key are enabled
- **THEN** layout generation and final validation certify the seed from ordinary
  keys/rings alone, while the runtime Skeleton Key may only make traversal easier
