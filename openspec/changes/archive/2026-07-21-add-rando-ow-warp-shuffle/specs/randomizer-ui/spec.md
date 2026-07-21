# randomizer-ui — delta for add-rando-ow-warp-shuffle

## ADDED Requirements

### Requirement: Warp-axis controls and surfaces stay coherent with the seed

The native settings window SHALL expose the two axes in a new "Overworld"
group — an enum combo for `flute_shuffle` (Off / Balanced / Random) and a
checkbox for `whirlpool_shuffle` — with tooltips limited to durable player
facts, controls disabled-with-reason under Inverted (mirroring the derived
normalization), and full participation in share-string round-tripping and
preset packing. UI surfaces that render warp information SHALL follow the
active seed, not vanilla assumptions: the in-game flute map already follows
the runtime override; the native-window map overlay (`rando_map.c`) SHALL be
audited for vanilla flute-blip assumptions as part of this change. Check
tracker, reachability counts, and the auto-tracker wire protocol are
unchanged (the axes add no item locations); the spoiler tab SHALL present the
`ow_warps` section under the same race-mode gating as every other spoiler
surface.

#### Scenario: Controls round-trip through the share string
- **WHEN** the user sets Flute=Random + Whirlpool=on, generates, copies the
  share string, and pastes it into a fresh session
- **THEN** the restored settings show the same control states and produce an
  identical seed

#### Scenario: Inverted disables with reason
- **WHEN** the world state combo is set to Inverted
- **THEN** both warp controls render disabled with a tooltip naming the v1
  Inverted exclusion, and generated seeds carry both axes off

#### Scenario: Race seed leaks nothing through UI
- **WHEN** a race-mode warp-axis slot is active
- **THEN** the spoiler tab, map overlay, and tracker surfaces reveal no spot
  or pair assignments
