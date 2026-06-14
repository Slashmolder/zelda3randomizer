## ADDED Requirements

### Requirement: Customizer-mode generation pipeline

The generator SHALL support customizer mode as a partial-manual-placement layer over assumed-fill (faithful to ALTTPR's customizer: the manifest pins a SUBSET of locations; the standard fill places everything else). When `settings.customizer_active == true`, the generator SHALL:

1. Read the customizer manifest from a path supplied via CLI flag `--customizer=<path>` (on `--generate-seed` and `--generate-slot`) or via the native settings window's manifest field.
2. Validate every `placements:` key against the location registry (`assets/rando/location_registry.yaml`) and every value against the item registry (`assets/rando/item_registry.yaml`), accepting both the symbol form (`Eastern_Palace_Boss`) and the spoiler's human form (`Eastern Palace - Boss`) via normalized name match. Reject (with the offending line) unknown names, duplicate location keys, non-customizable location types (prize/medallion/shop/take-any), already-vanilla-placed slots, and pins of prize/event items.
3. Apply `pool_overrides:` (remove-then-add) to the settings-derived pool before fill; `add` SHALL refuse prize/event items, `remove` is best-effort.
4. PIN each manifest location's slot in the placement table and remove the pinned items from the to-place pool, then run the standard assumed-fill for all remaining locations. With `customizer_active == false` the placer SHALL be byte-for-byte unchanged (regression-corpus invariant).
5. Run the existing goal-completability + accessibility acceptance gates (per `randomizer-core / Generation rejects un-completable seeds`); `--allow-broken-seed` bypasses as for ordinary seeds.
6. Emit `customizer_active` in the spoiler meta block.

The dispatcher SHALL NOT distinguish customizer-built placements from assumed-fill-built placements; both produce identical `LocationDef → ItemDef` mappings.

Customizer mode SHALL be mutually exclusive with race mode: the race reveal regenerates placement from `(seed, settings)` alone and cannot reproduce manifest pins, so generation SHALL refuse the combination (CLI and slot paths both).

> **Deferred**: share-string encoding of `customizer_seed` (reproduce-by-manifest across users) — the value is computed (SHA-256 of manifest bytes, first 8) but not yet carried in the share string. See `tasks.md §6.4`.

#### Scenario: Customizer manifest pins are honored
- **WHEN** `--generate-seed --customizer=manifest.yaml --settings=mode.state=open,goal=fast_ganon` is invoked
- **THEN** every `placements:` entry appears verbatim in the resulting placement table, and every other location is filled by the standard assumed-fill

#### Scenario: Un-completable customizer manifest is rejected
- **WHEN** a customizer manifest places `Hookshot` at a location that no progression path reaches
- **THEN** generation fails with the same un-completable error as assumed-fill seeds; the spoiler is not written; `--allow-broken-seed` bypasses

#### Scenario: Race mode is refused
- **WHEN** generation is requested with both `race_mode` and `customizer_active` set
- **THEN** generation is refused with an error explaining the race reveal cannot regenerate manifest pins

#### Scenario: Dispatcher unchanged
- **WHEN** a customizer seed is loaded and the player opens a chest
- **THEN** the dispatcher fires identically to a standard seed; no dispatcher branch needs to know the placement came from customizer

#### Scenario: Slot path parity
- **WHEN** the same `(settings, seed, manifest)` is generated via `--generate-seed` and via the playable-slot path (`--generate-slot` / the native window)
- **THEN** both produce the identical `placement_digest`

### Requirement: Settings canonical serialization order — customizer extension

`settings.customizer_active` SHALL serialize as canonical byte `[26]` bit1 (`kCustomizerAxis_Active`, sharing the pad byte with `enemy_shuffle`'s bit0; `door_shuffle` owns `[27]` bits 0-1). Default false keeps the default `settings_hash` and the regression corpus byte-identical; `kSettingsCanonicalLen` stays 28.

`customizer_seed` (uint64 LE) SHALL be the SHA-256 of the manifest contents truncated to 8 bytes, computed at parse time. This makes share-strings reproducible across users who have the same manifest once the deferred share-string encoding lands (`tasks.md §6.4`).

#### Scenario: customizer_active bit participates in settings_hash
- **WHEN** two seeds have identical settings except `customizer_active`
- **THEN** their `settings_hash` values differ (only canonical byte `[26]` moves)

#### Scenario: Identical manifests produce identical customizer_seed
- **WHEN** two users run customizer mode with the same manifest file
- **THEN** the computed `customizer_seed = SHA-256(manifest_bytes)[0..8]` is byte-identical
