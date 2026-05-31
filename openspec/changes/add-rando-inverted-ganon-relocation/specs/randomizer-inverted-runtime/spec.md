## ADDED Requirements

### Requirement: Ganon located under Hyrule Castle in Inverted

When `Rando_GetActiveWorldState() == Inverted` (under `kFeatures1_RandomizerActive`), the runtime SHALL relocate the Ganon fight from the Dark-World pyramid (overworld area 0x5B) to under the Light-World Hyrule Castle (area 0x1B), matching ALTTPR `app/Rom.php setInvertedMode()`. The relocation SHALL cover: the fall-hole drop destination, the carved overworld hole, the pyramid/HC exit-data bindings, the flute travel slot, and the death-to-Ganon respawn world. All behavior SHALL be gated so that non-Inverted / Open / Standard / Retro play is byte-identical (the fork RAM-compares against the original ROM).

> **Stub status**: the exact overlay/hole/exit data and the gfx/palette solution (see the next requirement) are resolved at apply-time per `design.md`; this requirement fixes the contract, not the byte values.

#### Scenario: Non-Inverted play is unchanged
- **WHEN** a seed is played in Open, Standard, or Retro, or with no active rando slot
- **THEN** Ganon remains in the Dark-World pyramid (area 0x5B); the screen-0x1B overlay, fall-hole table, exit data, flute slot 8, and death respawn are byte-identical to the pre-change build

#### Scenario: Inverted Ganon drop lands under Hyrule Castle
- **WHEN** an Inverted player falls through the carved Hyrule-Castle (area 0x1B) hole
- **THEN** they arrive in the Ganon fight room (not the Hyrule-Castle interior or the Chris-Houlihan fallback)

#### Scenario: Death to Ganon respawns at the castle in the Light World
- **WHEN** an Inverted player dies during the Ganon fight under Hyrule Castle and the Ganon-respawn flag is set
- **THEN** the respawn world is the Light World (the castle), mirroring `darkworldspawn.asm SetDeathWorldChecked_Inverted` `.castle`

#### Scenario: Flute travels to the relocated Ganon spot
- **WHEN** an Inverted player flute-travels to the former-pyramid spot (flute slot 8)
- **THEN** they arrive at area 0x1B (the relocated Hyrule-Castle Ganon location), not the Dark-World pyramid (0x5B)

### Requirement: Screen-0x1B inverted pyramid overlay renders without corruption

When the Inverted world-state is active, the screen-0x1B overlay (currently suppressed in `src/rando/inverted_maps_apply.c` because the pyramid map16 blocks clashed with the castle gfx/palette) SHALL render correctly — the relocated pyramid/Ganon-hole tiles SHALL display with correct graphics and palette on every entry method (mirror, cave exit, flute, save-and-quit, and walking-scroll). The renderer SHALL NOT display the garbage tiles that caused the original suppression.

> **Stub status**: the gfx-set / palette-slot mechanism that makes the pyramid blocks coexist with the castle gfx on screen 0x1B is the load-bearing apply-time spike; the requirement fixes the acceptance criterion (no corruption, all entry methods), not the technique.

#### Scenario: Overlay renders cleanly on a full screen rebuild
- **WHEN** an Inverted player arrives at screen 0x1B via a full screen rebuild (mirror / cave exit / flute / save-and-quit)
- **THEN** the relocated pyramid + Ganon-hole tiles render with correct gfx and palette, with no garbage/mint-green tiles

#### Scenario: Overlay renders cleanly on walking-scroll
- **WHEN** an Inverted player walks onto screen 0x1B by scrolling from an adjacent screen
- **THEN** the overlay tiles render correctly (the walking-scroll path applies the same corrected overlay as the full-rebuild path)
