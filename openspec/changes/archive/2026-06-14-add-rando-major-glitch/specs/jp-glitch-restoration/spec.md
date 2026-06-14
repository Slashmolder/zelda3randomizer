## MODIFIED Requirements

### Requirement: Interaction with the randomizer fake-flippers placement trick

A generated seed whose placement assumes a restored JP-1.0 glitch SHALL have `kFeatures0_RestoreJpGlitches` forced on at runtime, so the runtime can perform the swim/movement the placer assumed. A seed "assumes a restored glitch" when `logic >= OverworldGlitches` OR the `fake-flippers` placement trick is enabled. This resolves the deferral from
`add-jp-glitch-restoration` (which made Fake Flippers executable behind that flag
but left coupling to the randomizer as a future change): the `fake-flippers`
trick + flag combination, previously documented as "unsupported", is now
SUPPORTED. The force MUST be applied on every slot activation
(`Rando_ActivateSidecarSlot`) so it holds for both generate→play and reload→play
(including imported share strings), and also at generate time via the slot's
`recommended_features0`.

Only `fake-flippers` among the placement tricks couples to this flag (it maps 1:1
to the restored Fake Flippers glitch). The other tricks (boots-clip, pearl-bypass,
bunny-revival, hookshot-clip, etc.) are cross-version or unrestored and SHALL NOT
force the flag. A plain `logic = NoGlitches` / non-glitch-trick seed SHALL NOT get
the flag forced (the player's own setting stands).

Because `features0` is config state and not part of the canonical settings, the
coupling SHALL be placement-digest-neutral (it does not alter `RandoSettings`,
`kSettingsCanonicalLen`, or `settings_hash`). Under side-by-side emulator attach,
the point-of-use gate (`!ZeldaIsEmulatorAttached()`) still suppresses the glitch,
so the forced flag neither diverges the RAM compare nor performs the glitch — an
inherently non-glitch mode, out of scope.

#### Scenario: fake-flippers trick forces the runtime flag

- **WHEN** a seed enables the `fake-flippers` placement trick (with or without a
  glitch logic tier) and is generated or loaded
- **THEN** `kFeatures0_RestoreJpGlitches` is forced on at runtime (the assumed
  flipperless swim is executable); the placement digest is unchanged

#### Scenario: glitch logic tier forces the runtime flag

- **WHEN** a seed is generated or loaded with `logic >= OverworldGlitches`
- **THEN** `kFeatures0_RestoreJpGlitches` is forced on at runtime

#### Scenario: non-glitch seed does not force the flag

- **WHEN** a seed has `logic = NoGlitches` and no `fake-flippers` trick
- **THEN** `kFeatures0_RestoreJpGlitches` is NOT forced; the player's existing
  setting is left untouched
