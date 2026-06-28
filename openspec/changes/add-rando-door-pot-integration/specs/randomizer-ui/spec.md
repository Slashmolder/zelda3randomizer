## MODIFIED Requirements

### Requirement: Pot-shuffle tier selector and pot tracker presentation

The native settings window SHALL keep the pot-shuffle selector enabled when effective
door shuffle is active. It SHALL still disable the selector when effective
cave-entrance shuffle forces pots off.

The spoiler SHALL emit the effective pot tier. Door shuffle SHALL NOT cause that
value to be `off`; cave-entrance shuffle SHALL.

#### Scenario: Door shuffle does not gray out pot shuffle

- **WHEN** the user selects `door_shuffle=basic`
- **THEN** the Pot shuffle selector remains editable
- **AND** no disabled-state text says door shuffle forces pots off

#### Scenario: Cave entrance shuffle still grays out pot shuffle

- **WHEN** the user enables effective cave-entrance shuffle
- **THEN** the Pot shuffle selector is disabled or annotated as forced off
- **AND** generation emits effective `pot_shuffle=off`
