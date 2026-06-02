## Why

Phase A's `randomizer-shuffles / Cosmetic shuffles do not affect logic (Phase D)` drafted the contract: palette, sprite, and music shuffles SHALL be cosmetic-only and SHALL NOT alter the placement table, predicate evaluation, or the `settings_hash`. A separate `cosmetic_seed` setting drives cosmetic outputs.

**This is a Phase D change.** The community typically considers cosmetic shuffles "polish" — they make a seed feel personalized but don't change gameplay. Many tournaments distribute the same `share_string` to all players but allow each player to add their own `cosmetic_seed` so screenshots look distinct.

## What Changes (scope)

The fork already owns the cosmetic *rendering* primitives ALTTPR applies through its browser ROM-patcher (ZSPR sprite loader `main.c:2278`; MSU-1 / Opus / Deluxe audio; direct palette buffers + `ApplyPaletteFilter`). This change is the deterministic **selection / transform driver** over those primitives — not a from-scratch build. See `design.md` for the per-axis grounding table.

- **Palette shuffle**: deterministic one-shot transforms over the BGR555 palette buffers at palette-load time. MVP modes `vanilla / shuffled (hue-rotate) / grayscale / negative`. ALTTPR's animated gimmick modes (dizzy/sick/puke/blackout) are deferred to a follow-up.
- **Sprite shuffle**: when pointed at a folder of `.zspr` files, deterministically pick one and load it through the **existing** ZSPR path. Off preserves the configured single sprite.
- **Music shuffle**: deterministically remap the song the engine queues per area (chokepoint `queued_music_control`, `g_ram+0x132`); MSU-1, when loaded, keys off the remapped id.
- **Decoupling — client-config, NOT slot-header**: `cosmetic_seed` is a `[Graphics]` key in `zelda3.ini`, separate from the slot and the `share_string`. Same `share_string` + different `cosmetic_seed` ⇒ gameplay-identical, visually-distinct (the tournament use case). `cosmetic_seed = 0` resolves to the active slot's `seed_u64`. **No save-format change.**
- **Per-axis client config keys**, each defaulting off so unopted play is vanilla-identical.

## Capabilities

### Modified Capabilities

- `randomizer-shuffles`: MODIFIED Requirement "Cosmetic shuffles do not affect logic (Phase D)" — fleshes the 3-axis contract; pins `cosmetic_seed` as client-local config (not a slot field) and the MVP palette modes.

### Added Capabilities

- `randomizer-ui`: ADDED Requirement "Cosmetic settings surface (client config)" — the four `[Graphics]`/`[Sound]` INI keys. ADDED (not a modification of the existing settings-screen requirements) to avoid stacking an archive-sequencing conflict on the `randomizer-ui` requirements that `add-rando-native-settings-window` already MODIFIES.

> The stub's `randomizer-save` ADDED delta and `randomizer-core` canonical-serialization MODIFIED delta are **dropped**: under the client-config decision there is no slot-header field and `cosmetic_seed` never enters canonical serialization, so neither capability changes.

## Impact

- **Code**: `src/rando/shuffle_cosmetic.{c,h}` (new); palette-load call sites in `src/load_gfx.c`; ZSPR folder-pick at `src/main.c:2278`; song remap at the `queued_music_control` consume site (`src/spc_player.c` / `src/nmi.c` — apply-time spike); four INI keys in `src/config.c`.
- **Effort**: revised down from the stub's "3-4 weeks each axis" — the primitives exist; this is a selection/transform layer. Sprite axis is small; palette + music are medium.
- **Determinism**: **no `kGeneratorVersion` bump, no corpus regen, no canonical-size cascade** — cosmetics are outside the generation path. A separate cosmetic-determinism CI step replaces corpus regen.
- **Regression risk**: structural zero on the generation path; the only runtime risk is rendering/audio, caught by the all-axes-off vanilla-compare + playtest.
- **Dependencies**: Phase A archived; no Phase B dependency.

## Status

Design + tasks authored (was stub). Ready to implement; no generation-path blockers. The one apply-time unknown is the music chokepoint (task 1.4 spike).
