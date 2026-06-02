## MODIFIED Requirements

### Requirement: Cosmetic shuffles do not affect logic (Phase D)

Palette, sprite, and music shuffles SHALL be cosmetic-only and SHALL NOT alter the placement table, predicate evaluation, or the `settings_hash`. A separate `cosmetic_seed` SHALL drive cosmetic outputs.

`cosmetic_seed` SHALL be a **client-local configuration value** (a `[Graphics]` key in `zelda3.ini`), NOT a slot-header field and NOT part of any canonical serialization. It therefore never travels with the `share_string`: two players given the same `share_string` MAY each set their own `cosmetic_seed` and obtain gameplay-identical seeds with visually-distinct presentation. A `cosmetic_seed` of `0` SHALL resolve to the active slot's `seed_u64`, so a default install still yields a reproducible cosmetic result without INI editing.

Each axis is an independent per-axis setting, defaulting to **off** so that unopted play (rando or vanilla) is rendering-identical to the unmodified game:

- **Palette** SHALL support modes `vanilla` / `shuffled` / `grayscale` / `negative`, each a deterministic one-shot transform over the BGR555 palette buffers. (ALTTPR's animated gimmick modes — dizzy/sick/puke/blackout — are explicitly out of scope for this change.)
- **Sprite** SHALL, when pointed at a folder of `.zspr` files, deterministically pick one (stable filename sort) and load it through the existing ZSPR path; off preserves the configured single sprite.
- **Music** SHALL, when enabled, deterministically remap the song the engine queues per area; when an MSU-1 pack is loaded the remapped id SHALL drive MSU track selection.

Cosmetic outputs SHALL be derived from a dedicated RNG stream forked off `cosmetic_seed` (the `randomizer-core / RNG family` xoshiro256\*\*), never the fill RNG; the guarantee is cross-platform self-consistency, not byte-parity with ALTTPR's JS transforms.

#### Scenario: Cosmetic shuffle leaves placement untouched
- **WHEN** sprite shuffle is enabled with a fixed `cosmetic_seed`
- **THEN** the placement table is byte-identical to a non-sprite-shuffled run with the same `share_string`; only the rendered Link sprite differs

#### Scenario: cosmetic_seed is independent of settings_hash
- **WHEN** two runs share an identical `share_string` but differ in `cosmetic_seed`
- **THEN** their `settings_hash` and `placement_digest_hex` are identical; their on-screen palette / sprite / music differ

#### Scenario: Tournament cosmetic decoupling
- **WHEN** a tournament distributes one `share_string` to multiple players, each with their own `cosmetic_seed`
- **THEN** every player plays the same placement with personal cosmetic state; screenshots are visually distinct

#### Scenario: Default cosmetic_seed tracks the slot seed
- **WHEN** `cosmetic_seed` is `0` (default)
- **THEN** cosmetic outputs derive deterministically from the active slot's `seed_u64`, and re-loading the same slot reproduces the same look

#### Scenario: All axes off is vanilla-identical
- **WHEN** palette / sprite / music shuffle are all off
- **THEN** rendering and audio are byte-identical to the unmodified game (RAM/PPU compare clean)

#### Scenario: Music shuffle interacts cleanly with MSU-1
- **WHEN** music shuffle is enabled AND an MSU-1 pack is loaded
- **THEN** the remapped song id drives MSU-1 track selection; there is no MSU-1 incompatibility
