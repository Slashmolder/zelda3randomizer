## ADDED Requirements

### Requirement: Ganon located under Hyrule Castle in Inverted

When `Rando_GetActiveWorldState() == Inverted` (under `kFeatures1_RandomizerActive`), the runtime SHALL relocate the Ganon fight from the Dark-World pyramid (overworld area 0x5B) to under the Light-World Hyrule Castle (area 0x1B). The relocation SHALL cover the Ganon fall-pit at area 0x1B (drop destination → the Ganon room), the removal of the Dark-World pyramid as a Ganon access, and the flute travel-slot destination. All behavior SHALL be gated so that non-Inverted / Open / Standard / Retro play is byte-identical (the fork RAM-compares against the original ROM).

#### Scenario: Non-Inverted play is unchanged
- **WHEN** a seed is played in Open, Standard, or Retro, or with no active rando slot
- **THEN** Ganon remains in the Dark-World pyramid (area 0x5B); the screen-0x1B overlay, fall-hole routing, flute slot 8, and death respawn are byte-identical to the pre-change build

#### Scenario: Inverted Ganon drop lands under Hyrule Castle
- **WHEN** an Inverted player, post-Agahnim, falls into the Ganon pit on the Light-World Hyrule-Castle screen (area 0x1B)
- **THEN** they arrive in the Ganon fight room (entrance 0x7B), not the Hyrule-Castle interior or the Chris-Houlihan fallback

#### Scenario: The Dark-World pyramid is no longer a Ganon access
- **WHEN** an Inverted player falls at the Dark-World pyramid (area 0x5B)
- **THEN** they do NOT reach Ganon — the pyramid's Ganon fall-hole entries no longer match, so the fall drops to the Chris-Houlihan fallback (kept in the Dark World)

#### Scenario: Death at Ganon respawns via the Inverted spawn-select menu
- **WHEN** an Inverted player dies during the Ganon fight under Hyrule Castle (post-Agahnim)
- **THEN** they respawn via the existing Inverted spawn-select menu (Dark-World options) and can re-reach Ganon (NOTE: ALTTPR's Light-World-castle respawn is intentionally NOT ported — it conflicts with the fork's Dark-World-home spawn system and risks the Light-World-spawn trap)

#### Scenario: Flute slot 8 destination points at the relocated Ganon spot
- **WHEN** the Inverted flute slot-8 destination is consulted
- **THEN** it resolves to area 0x1B (the relocated Hyrule-Castle Ganon location), not the Dark-World pyramid (0x5B) — though slot 8 is not yet selectable in the current 8-spot flute menu (a deferred cosmetic gap)

### Requirement: The Inverted Ganon pit on screen 0x1B renders without corruption

When the Inverted world-state is active and Agahnim is defeated, the screen-0x1B Ganon pit SHALL render as a clean dark pit using graphics already loaded on the Hyrule-Castle screen (no new/custom art), with no garbage / mint-green tiles, leaving the surrounding castle unchanged. The full ALTTPR "pyramid facade" overlay is intentionally NOT rendered — it requires unlicensed custom graphics (`z3randomizer/data/sheet73.gfx`) and is deferred.

#### Scenario: The pit renders cleanly on a full screen rebuild
- **WHEN** an Inverted player arrives at screen 0x1B post-Agahnim via a full screen rebuild (mirror / cave exit / flute / save-and-quit)
- **THEN** the Ganon pit renders as a coherent dark pit (a 2-tone dark diamond) with no garbage / mint-green tiles, and the surrounding Hyrule Castle is unchanged

#### Scenario: Pre-Agahnim the castle is unchanged
- **WHEN** an Inverted player is on screen 0x1B before Agahnim is defeated
- **THEN** no pit is drawn and the Hyrule-Castle screen is byte-identical to the non-relocated build (the pit is gated on the pyramid-hole bit `save_ow_event_info[0x5B] & 0x20`)
