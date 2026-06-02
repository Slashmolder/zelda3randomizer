## ADDED Requirements

### Requirement: Cosmetic settings surface (client config)

The cosmetic-shuffle axes SHALL be configured through client-local `zelda3.ini` keys, parsed by `config.c`, and SHALL NOT appear in the playable-slot settings (they are presentation-only and never participate in generation, the `share_string`, or the `settings_hash`). This requirement is ADDED (not a modification of the existing rando settings-screen requirements) precisely because cosmetics are a separate client surface from the generation-driving settings UI.

The keys SHALL be:

- `[Graphics] CosmeticSeed` — unsigned 64-bit; `0` (default) resolves to the active slot's `seed_u64`.
- `[Graphics] PaletteShuffle` — `vanilla` (default) / `shuffled` / `grayscale` / `negative`.
- `[Graphics] SpriteShuffle` — `off` (default) or a folder path of `.zspr` files.
- `[Sound] MusicShuffle` — `off` (default) / `on`.

Unknown or hand-edited values in these keys SHALL NOT break INI round-trip (consistent with the existing foreign-section handling in `config.c`). A PC build MAY additionally surface these in the native settings window, but the INI keys are the normative source.

#### Scenario: Cosmetic keys parse and round-trip
- **WHEN** `zelda3.ini` sets `PaletteShuffle = grayscale` and `CosmeticSeed = 12345`
- **THEN** the values are read at startup and a write-back of the config preserves them without corrupting other sections

#### Scenario: Defaults are vanilla-safe
- **WHEN** none of the cosmetic keys are present in `zelda3.ini`
- **THEN** all axes default off / vanilla and rendering + audio match the unmodified game

#### Scenario: Cosmetic keys never enter the slot
- **WHEN** a playable slot is generated while cosmetic keys are set
- **THEN** the written sidecar slot and `share_string` are byte-identical to a generation with cosmetic keys unset
