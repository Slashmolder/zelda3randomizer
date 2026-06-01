## ADDED Requirements

### Requirement: Inverted respawn at the Dark Chapel (Dark-World Sanctuary)

When `Rando_GetActiveWorldState() == Inverted` (under `kFeatures1_RandomizerActive`), the save / respawn spawn point SHALL be the "Dark Chapel" — the Dark-World Sanctuary at overworld screen `0x53` (the Dark-World mirror of the Light-World Sanctuary at `0x13`) — matching ALTTPR `app/Rom.php setInvertedMode()` (the `StartingArea*` block). The Inverted player SHALL spawn and respawn (new game, save-and-quit, death) in the Dark World at the Dark Chapel — not at Link's House and not in the Light World. All behavior SHALL be gated so that non-Inverted / Open / Standard / Retro spawn-select is byte-identical (the fork RAM-compares against the original ROM).

> **Stub status**: this supersedes the interim Link's-House Inverted spawn (`docs/inverted_alttpr_gaps.md` §B6). The exact spawn-data mechanism — overriding the `kStartingPoint_*` Sanctuary entry's post-exit world/screen for Inverted vs. adding a dedicated Dark-Chapel spawn — is resolved at apply-time per `design.md`; this requirement fixes the contract (a navigable DW-Sanctuary respawn), not the byte values. If the apply-time spike finds the Sanctuary-exit field is not cleanly overridable, the accepted fallback is to keep the Link's-House spawn and close this change as won't-port.

#### Scenario: Non-Inverted spawn-select is unchanged
- **WHEN** a seed is played in Open, Standard, or Retro, or with no active rando slot
- **THEN** the Sanctuary spawn (`which_starting_point` 1) lands at the Light-World Sanctuary (screen `0x13`), byte-identical to the pre-change build

#### Scenario: Inverted new game spawns at the Dark Chapel
- **WHEN** a fresh Inverted slot is generated and loaded
- **THEN** the player begins in the Dark World at the Dark Chapel (screen `0x53`, `savegame_is_darkworld = 0x40`) — not at Link's House (screen `0x6C`) and not in the Light World

#### Scenario: Inverted save-and-quit respawns at the Dark Chapel
- **WHEN** an Inverted player saves-and-quits outdoors in the Dark World and reloads
- **THEN** they respawn at the Dark Chapel in the Dark World — not the trapped pyramid ledge (screen `0x5B`) and not the Light-World Sanctuary

#### Scenario: Inverted death respawns at the Dark Chapel
- **WHEN** an Inverted player dies outside a dungeon-specific respawn
- **THEN** they respawn at the Dark Chapel in the Dark World
