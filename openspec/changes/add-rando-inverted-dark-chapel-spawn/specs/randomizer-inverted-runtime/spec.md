## ADDED Requirements

### Requirement: Inverted spawn-select respawn points are in the Dark World

When `Rando_GetActiveWorldState() == Inverted` (under `kFeatures1_RandomizerActive`), the post-Agahnim spawn-select menu's **"Dark Chapel"** option (Sanctuary, `which_starting_point = 1`) and **"Dark Mountain"** option (Mountain Cave, `which_starting_point = 6`) SHALL respawn the player in the **Dark World** — the Dark Chapel at the Dark-World Sanctuary-mirror (overworld screen `0x53`, the DW mirror of LW `0x13`) and Dark Mountain at the Dark-World Death Mountain — matching ALTTPR `app/Rom.php setInvertedMode()`. The player SHALL NOT land in the Light World from either option. The initial spawn (Link's House, `which_starting_point = 0`) is out of scope and unchanged. All behavior SHALL be gated so that non-Inverted / Open / Standard / Retro spawn-select is byte-identical (the fork RAM-compares against the original ROM).

> **Stub status**: the exact mechanism (an Inverted `kExitData_ScreenIndex` asset-shadow override on the Sanctuary + Mountain-Cave exit-ids, mirroring the existing Link's-House idx-0 → `0x6C` override, vs. a runtime spawn redirect) and the exact exit-ids are resolved at apply-time per `design.md`. This requirement fixes the contract (both menu options land in a navigable Dark World), not the byte values. This supersedes `docs/inverted_alttpr_gaps.md` §D1 (the Mountain-Cave spawn-select world question) and closes the Dark-Chapel half of §B6.

#### Scenario: Non-Inverted spawn-select is unchanged
- **WHEN** a seed is played in Open, Standard, or Retro, or with no active rando slot
- **THEN** the spawn-select Sanctuary (idx 1) and Mountain Cave (idx 6) options land in the Light World (screens `0x13` / the LW DM cave), byte-identical to the pre-change build

#### Scenario: Inverted "Dark Chapel" respawns in the Dark World
- **WHEN** an Inverted player opens the respawn menu and selects "Dark Chapel" (the Sanctuary slot)
- **THEN** they respawn in the Dark World at the Dark Chapel (screen `0x53`, `savegame_is_darkworld = 0x40`) — not in the Light World — and the spot is navigable

#### Scenario: Inverted "Dark Mountain" respawns in the Dark World
- **WHEN** an Inverted player opens the respawn menu and selects "Dark Mountain" (the Mountain-Cave slot)
- **THEN** they respawn in the Dark World on Death Mountain — not in the Light-World DM cave

#### Scenario: The Link's House spawn is untouched
- **WHEN** an Inverted slot is freshly generated (the baked `which_starting_point = 0`) or the player selects "@'s House" from the menu
- **THEN** the spawn is Link's House landing in the Dark World (screen `0x6C`), exactly as before this change (§B6 behavior preserved)
