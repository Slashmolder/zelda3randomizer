## ADDED Requirements

### Requirement: Itemized pot keys participate in key-ring collapse

Every active pot location whose vanilla item is a dungeon small key SHALL add that
family's key to the pre-ring multiset before selection and collapse. If the family
is selected, no ordinary copy from the pot or base pool SHALL survive; the pot
location remains active and receives an item through normal placement/junk padding.
Free non-itemized pot/drop keys MAY remain vanilla surplus.

#### Scenario: Pot-heavy selected family still has one ring

- **WHEN** pot key checks add multiple keys to a selected dungeon family
- **THEN** the final shuffled pool contains one family ring and zero ordinary
  family keys while every active pot check remains filled
