## Why

Customizer mode is the "I want this specific item at this specific location" feature. Instead of running the assumed-fill placer with random RNG, the user supplies a manual placement table. ALTTPR's customizer is a popular community feature for creating themed seeds (e.g., "Boss Rush starter" with all swords + medallions at sphere 0) and for race-admin troubleshooting.

Per Phase A `proposal.md` line 75 — *"customizer mode (uses dispatcher API unchanged)"* — customizer was always intended to ship in a later phase using Phase A's dispatch infrastructure unchanged. The dispatcher's contract (`Rando_OnLocationCheck(location_id, vanilla_item_id)`) doesn't care whether the placement came from assumed fill or from a hand-written customizer manifest; it just reads the placement table.

This is a **Phase D** change. Low risk (no logic changes), medium reward (significant community feature).

## What Changes

- **Customizer manifest format**: a YAML or JSON file describing per-location placements. Example:
  ```yaml
  placements:
    Eastern_Palace_Boss: BowAndArrows
    Hyrule_Castle_BoomerangChest: Hookshot
    Master_Sword_Pedestal: TitanMitt
  pool_overrides:
    add: [ProgressiveSword, ProgressiveSword]
    remove: [Rupoor]
  ```
- **CLI and slot entry points**: `--customizer=<path>` on `--generate-seed` and `--generate-slot`, plus the PC native settings window manifest field. The manifest is validated against the location registry + item registry, pins a subset of placements, and then assumed-fill completes the rest.
- **Validation**: the customizer pipeline SHALL run the goal-completability predicate to confirm the manual placement is winnable. If un-completable and `--allow-broken-seed` is NOT set, generation fails.
- **Settings struct field**: `customizer_active` boolean, serialized as canonical byte `[26]` bit1. `customizer_seed` is the SHA-256 of the manifest content truncated to 8 bytes.
- **Share-string compatibility**: current share strings do not carry the manifest identity. Customizer copy/emission falls back to the v1 seed+hash identity string, and reproduce-by-manifest sharing is deferred until a `customizer_seed` share-string field is designed.
- **NO new dispatcher work**: every Phase A dispatch site already routes the placement-table entry; customizer just writes the table differently.

## Capabilities

### Modified Capabilities

- `randomizer-core`: ADDED Requirement for the customizer-mode generation pipeline (alongside the existing assumed-fill pipeline). MODIFIED Requirement on settings canonical-serialization to add `customizer_active` byte.
- `randomizer-ui`: ADDED Requirement for the PC native settings-window customizer toggle + manifest field.

## Impact

- **Code**: `src/rando/customizer.{c,h}`, CLI parsing in `src/main.c`, slot generation in `src/rando/rando_generate.c`, and native-window/bridge UI wiring.
- **Regression risk**: zero by design. Non-customizer seeds run the existing pipeline unchanged.
- **Dependencies**: Phase A archived; benefits from #2 trackers (manual placement is much more verifiable with a tracker overlay).
