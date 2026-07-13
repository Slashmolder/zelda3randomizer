## ADDED Requirements

### Requirement: Seed QoL tab exposes the new per-slot QoL recommendations

The native settings window's Seed QoL tab SHALL expose the new per-slot Seed QoL
bits (F1 dungeon check-info, F3 fast item fanfare, F4 cutscene fast-forward,
F6 auto-dash) alongside the existing `kRecRows` entries, so a
generated slot's `recommended_features0` round-trips them and the "recommended
defaults" action sets each to its recommended state. Bits added to
`kFeatures0_RandoSeedQolMask` SHALL appear here; local-only bindings that are not
per-slot preferences (F5 warp-to-spawn) live in Game
Settings, not this tab. The window SHALL also surface the auto-tracker connection
state where its tracker panels report client connections (F7), without adding an
in-game overlay. Text speed is a global Game Settings value, not a per-slot bit.

#### Scenario: A recommended QoL bit round-trips through the slot
- **WHEN** a Seed QoL bit is set on in the Seed QoL tab and a slot is generated,
  then that slot is reloaded
- **THEN** the bit is restored from the slot's `recommended_features0` and shown in
  the tab as recommended

#### Scenario: Recommended-defaults action covers the new bits
- **WHEN** the player clicks the "recommended defaults" action in the Seed QoL tab
- **THEN** each new per-slot QoL bit is set to its recommended on/off state
