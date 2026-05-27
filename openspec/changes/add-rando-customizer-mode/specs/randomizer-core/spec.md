## ADDED Requirements

### Requirement: Customizer-mode generation pipeline

The generator SHALL support customizer mode as a peer pipeline to assumed-fill. When `settings.customizer_active == true`, the generator SHALL:

1. Read the customizer manifest from a path supplied via CLI flag `--customizer=<path>` (or via the settings-screen picker).
2. Validate every `placements:` key against the location registry (`assets/rando/location_registry.yaml`) and every value against the item registry (`assets/rando/item_registry.yaml`).
3. Apply `pool_overrides:` to the standard pool (add/remove operations).
4. Build the placement table directly from the manifest (no assumed-fill RNG involvement).
5. Run the existing goal-completability predicate (per `randomizer-core / Generation rejects un-completable seeds`).
6. Emit the spoiler with a `customizer_manifest` reference recorded in the meta block.

The dispatcher SHALL NOT distinguish customizer-built placements from assumed-fill-built placements; both produce identical `LocationDef → ItemDef` mappings.

> **Stub status**: exact manifest schema, pool-override semantics, and share-string-encoding of `customizer_seed` deferred to Phase D apply-time.

#### Scenario: Customizer manifest builds the placement table directly
- **WHEN** `--generate-seed --customizer=manifest.yaml --settings=mode.state=open,goal=fast_ganon` is invoked
- **THEN** the placement table reflects the manifest's `placements:` entries exactly; assumed-fill is not run

#### Scenario: Un-completable customizer manifest is rejected
- **WHEN** a customizer manifest places `Hookshot` at a location that no progression path reaches
- **THEN** generation fails with the same un-completable error as assumed-fill seeds; the spoiler is not written; `--allow-broken-seed` bypasses

#### Scenario: Customizer share-string round-trips
- **WHEN** two users have the same manifest file and exchange a customizer-mode share string
- **THEN** both generate byte-identical placement tables

#### Scenario: Dispatcher unchanged
- **WHEN** a customizer seed is loaded and the player opens a chest
- **THEN** the dispatcher fires identically to a standard seed; no dispatcher branch needs to know the placement came from customizer

### Requirement: Settings canonical serialization order — customizer extension

The `settings.customizer_active` byte SHALL be added to the canonical-serialization order at a stable position (deferred to Phase D apply-time after audit of any Phase B / Phase C extensions). Default false.

`settings.customizer_seed` (uint64 LE) SHALL be the SHA-256 of the manifest contents truncated to 8 bytes. This makes share-strings reproducible across users who have the same manifest.

> **Stub status**: exact byte position + truncation pattern deferred.

#### Scenario: customizer_active byte participates in settings_hash
- **WHEN** two seeds have identical settings except `customizer_active`
- **THEN** their `settings_hash` values differ

#### Scenario: Identical manifests produce identical customizer_seed
- **WHEN** two users run customizer mode with the same manifest file
- **THEN** the computed `customizer_seed = SHA-256(manifest_bytes)[0..8]` is byte-identical; their share-strings are identical
