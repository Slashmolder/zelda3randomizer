## Why

Phase A's `randomizer-shuffles / Cosmetic shuffles do not affect logic (Phase D)` drafted the contract: palette, sprite, and music shuffles SHALL be cosmetic-only and SHALL NOT alter the placement table, predicate evaluation, or the `settings_hash`. A separate `cosmetic_seed` setting drives cosmetic outputs.

**This is a Phase D change.** The community typically considers cosmetic shuffles "polish" — they make a seed feel personalized but don't change gameplay. Many tournaments distribute the same `share_string` to all players but allow each player to add their own `cosmetic_seed` so screenshots look distinct.

## What Changes (intended scope)

- **Palette shuffle**: randomize the active palette set within the 25 vanilla palettes (Link's tunic, dungeon palettes, overworld palettes). Per ALTTPR convention, palette shuffle has sub-modes (`vanilla / shuffled / negative / blackout`).
- **Sprite shuffle**: replace Link's sprite with one from the community sprite pack. Phase D references the existing community sprite-pack format (BPS patches keyed by SHA-256 of sprite asset).
- **Music shuffle**: shuffle the dungeon background music. Optional MSU-1 integration: shuffled tracks pull from the active MSU-1 pack if one is loaded.
- **Decoupling**: cosmetic state is per-slot but does NOT participate in `settings_hash`. The `cosmetic_seed` byte stream is a separate slot-header field; two slots with identical placement + different `cosmetic_seed` are gameplay-equivalent.
- **Per-cosmetic-axis toggles** in the settings screen (each is independently on/off).

## Capabilities

### Modified Capabilities

- `randomizer-shuffles`: MODIFIED Requirement on "Cosmetic shuffles do not affect logic (Phase D)" — flesh out the 3-axis (palette / sprite / music) contract.
- `randomizer-save`: ADDED Requirement for a `cosmetic_seed` slot-header field separate from `settings_hash`.
- `randomizer-ui`: ADDED Requirement for cosmetic settings in the settings screen.
- `randomizer-core`: MODIFIED Requirement on `Settings canonical serialization order (normative)` to note that `cosmetic_seed` is NOT part of the canonical-serialization input.

## Impact

- **Code**: `src/rando/shuffle_cosmetic.{c,h}` (new), palette table edits in `src/load_gfx.c`, sprite-replacement integration with the existing BPS patcher (commit `fbbb3f9`), music-shuffle integration with `src/audio.c` / `src/spc_player.c` / MSU-1.
- **Effort**: **3-4 weeks of focused work.** Each axis is independent.
- **Regression risk**: zero by design. Placement and `settings_hash` are untouched.
- **Dependencies**: Phase A archived; no Phase B dependency.

## Status (stub)

Proposal-only Phase D stub. Detail deferred to Phase D apply-time. Phase D cannot start before Phase A archives.
