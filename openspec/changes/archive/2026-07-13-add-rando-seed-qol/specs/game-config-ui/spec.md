## ADDED Requirements

### Requirement: Seed QoL toggles are exposed in Game Settings and zelda3.ini

The PC Game Settings panels and `zelda3.ini` SHALL expose the new Seed QoL options
following the existing `kFeatures0_*` checkbox and keybind conventions: the F3 text
speed as a multi-value selector (`normal`/`fast`/`instant`) rather than a bare
checkbox, the F4 cutscene/transition fast-forward, the F6 auto-dash, and the F5
warp-to-spawn binding as a `zelda3.ini` key command
(`kKeys_*`). The F5 control SHALL carry a one-line note that a race ruleset may
ban it. Each feature bit SHALL persist through the existing named `[Features]`
boolean keys, and text speed plus the key binding SHALL use their existing managed
INI value/key-binding round trips.

#### Scenario: Text speed persists through the INI round-trip
- **WHEN** the player sets text speed to `instant` and the config is written and
  reloaded
- **THEN** the reloaded config restores `instant`, and dialogue renders at that speed

#### Scenario: Warp-to-spawn keybind is bindable and noted
- **WHEN** the player binds the warp-to-spawn command in Game Settings
- **THEN** the binding is written to `zelda3.ini` as a `kKeys_*` command and the
  control shows the race-legality note

#### Scenario: New feature bits survive features0 serialization
- **WHEN** the F4 or F6 bit is enabled and `zelda3.ini` is written and reloaded
- **THEN** the bit round-trips through its named `[Features]` boolean key unchanged
