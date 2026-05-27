## Why

Customizer mode is the "I want this specific item at this specific location" feature. Instead of running the assumed-fill placer with random RNG, the user supplies a manual placement table. ALTTPR's customizer is a popular community feature for creating themed seeds (e.g., "Boss Rush starter" with all swords + medallions at sphere 0) and for race-admin troubleshooting.

Per Phase A `proposal.md` line 75 — *"customizer mode (uses dispatcher API unchanged)"* — customizer was always intended to ship in a later phase using Phase A's dispatch infrastructure unchanged. The dispatcher's contract (`Rando_OnLocationCheck(location_id, vanilla_item_id)`) doesn't care whether the placement came from assumed fill or from a hand-written customizer manifest; it just reads the placement table.

This is a **Phase D** change. Low risk (no logic changes), medium reward (significant community feature).

## What Changes (intended scope)

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
- **CLI entry point**: `--customizer=<path>` flag on `--generate-seed`. Reads the manifest, validates against the location registry + item registry, builds the placement table directly (skipping assumed-fill).
- **Validation**: the customizer pipeline SHALL run the goal-completability predicate to confirm the manual placement is winnable. If un-completable and `--allow-broken-seed` is NOT set, generation fails.
- **Settings struct field**: `customizer_active` boolean. `customizer_seed` is the SHA-256 of the manifest content (for share-string compatibility).
- **Share-string compatibility**: customizer seeds use the same share-string format as standard seeds. The `share_string` encodes `(magic, generator_version, settings_hash, customizer_seed)` so another player can regenerate the same customizer placement (assuming they have the same manifest file).
- **NO new dispatcher work**: every Phase A dispatch site already routes the placement-table entry; customizer just writes the table differently.

## Capabilities

### Modified Capabilities

- `randomizer-core`: ADDED Requirement for the customizer-mode generation pipeline (alongside the existing assumed-fill pipeline). MODIFIED Requirement on settings canonical-serialization to add `customizer_active` byte.
- `randomizer-ui`: ADDED Requirement for the settings-screen customizer toggle + manifest file picker.

## Impact

- **Code**: `src/rando/customizer.{c,h}` (new module), CLI parsing in `src/main.c`, file-select UI in `src/select_file.c`.
- **Effort**: **2-3 weeks of focused work.** Customizer is a peer to assumed-fill; the validation + share-string-compat pieces are the bulk.
- **Regression risk**: zero by design. Non-customizer seeds run the existing pipeline unchanged.
- **Dependencies**: Phase A archived; benefits from #2 trackers (manual placement is much more verifiable with a tracker overlay).

## Status (stub)

Proposal-only Phase D stub. Detail deferred to Phase D apply-time. Phase D cannot start before Phase A archives.
