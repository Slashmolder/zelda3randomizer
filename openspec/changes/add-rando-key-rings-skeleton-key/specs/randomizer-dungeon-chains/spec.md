## ADDED Requirements

### Requirement: Dungeon chains preserve key-ring family identity

Under dungeon chains, each Key Ring SHALL remain bound to its original dungeon key
family and Dungeon-mode confinement. Boss-approach predicates and successor edges
SHALL use the ring-aware effective count. A ring required to cross a dungeon's boss
seam SHALL not be placed only beyond that seam. Skeleton Key SHALL remain absent
from chain logic.

#### Scenario: Ring is not placed past its own chain seam

- **WHEN** a chain predecessor requires its selected family ring to reach the
  successor
- **THEN** assumed fill does not place that ring in the successor or terminal
  region whose edge depends on the ring
