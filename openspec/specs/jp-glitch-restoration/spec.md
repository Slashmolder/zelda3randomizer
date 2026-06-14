# jp-glitch-restoration Specification

## Purpose
TBD - created by archiving change add-jp-glitch-restoration. Update Purpose after archive.
## Requirements
### Requirement: JP-1.0 glitch master toggle

The system SHALL expose a single gameplay-feature flag, `kFeatures0_RestoreJpGlitches` (a bit in the `features0` field at `g_ram[0x64c]`), that re-introduces glitches present in the Japanese 1.0 ALTTP ROM but patched out of the US 1.0 ROM this project reimplements. The flag SHALL default to **off**. When off, runtime behavior SHALL be byte-identical to the unmodified US-1.0 reimplementation.

The flag's contract is "enable every JP-1.0-exclusive glitch that this build has verified-and-implemented." Adding a verified glitch later SHALL NOT require a new flag, a save-format change, or a migration.

#### Scenario: Default off is vanilla-identical
- **WHEN** `kFeatures0_RestoreJpGlitches` is not set (the default)
- **THEN** every JP-glitch code path takes its US-1.0 branch and the running game is behavior-identical to the unmodified port (clean side-by-side RAM compare)

#### Scenario: Master toggle enables the verified glitch set
- **WHEN** the player sets `kFeatures0_RestoreJpGlitches`
- **THEN** every glitch the build has verified-and-implemented becomes active at once, with no per-glitch configuration required

### Requirement: Side-by-side RAM-compare safety

Because every restored glitch diverges from the original US ROM, each glitch's runtime gate SHALL additionally suppress the glitch whenever the original ROM is attached for side-by-side comparison (`ZeldaIsEmulatorAttached()` is true). Enabling the flag SHALL NOT cause the per-frame RAM/SRAM/VRAM comparator to diverge during a verification run.

#### Scenario: Glitch suppressed under side-by-side compare
- **WHEN** the original ROM is attached for comparison AND `kFeatures0_RestoreJpGlitches` is set
- **THEN** each glitch takes its US-1.0 branch for the comparison run and the RAM comparator does not diverge on account of this feature

### Requirement: Per-glitch verification gate (no glitch from memory)

A glitch SHALL NOT ship behind the master flag until its JP-1.0 behavior is grounded in source: either a documented community description of the exact US patch (as for Fake Flippers) or a JP-vs-US 65816 disassembly diff recorded in the change's `audit.md`. For each catalogued glitch the project SHALL record the JP behavior, the US-side change, the corresponding `src/*.c` site, and a verdict of IMPLEMENTED, PENDING-SPIKE, or INFEASIBLE/N-A (with the reason — e.g. the C reimplementation does not reproduce the raced input arbitration, or the glitch depends on a hardware/timing artifact the port does not model).

#### Scenario: Unverified glitch is not silently active
- **WHEN** a catalogued glitch has not passed the verification gate
- **THEN** the master flag does not activate it; the per-glitch verification table marks it PENDING-SPIKE or INFEASIBLE and the flag's active set excludes it

### Requirement: Fake Flippers restoration (first target)

When `kFeatures0_RestoreJpGlitches` is set (and the original ROM is not attached), the system SHALL reproduce JP-1.0 Fake Flippers byte-for-byte by gating EXACTLY the one site the JP-vs-US 65816 ROM diff shows US 1.0 added: the per-frame flipperless eject in `PlayerHandler_04_Swimming` (`src/player.c:1721`). The two swim-ENTRY ejects — `CheckAbilityToSwim` (`:129-136`) and the deep-water guard in `LinkState_CrossingWorlds` (`:3192-3213`) — are **byte-identical JP↔US** and SHALL NOT be gated: a flipperless Link who walks onto deep water is ejected in *both* versions. The glitch is reached only via the un-flipper-checked ledge/recoil entry sites (`:710`, `:918`), which set swim state directly; with the recheck gated, that state persists, exactly as on JP 1.0.

There is **no 8-frame grace counter** in the ROM (that was a runner-technique description, not JP code — JP simply omits the recheck). The "free-swim approximation" that *also* gated the entry sites (letting Link walk into water flipperless) is **rejected**: it is a strictly-easier always-on ability, not the glitch, and does not match JP.

#### Scenario: JP flipperless swim restored with the flag on (correct entry)
- **WHEN** Link has no flippers, `kFeatures0_RestoreJpGlitches` is set, the original ROM is not attached, and Link enters deep water via a ledge/recoil path (which sets swim state without a flipper check)
- **THEN** the per-frame recheck no longer ejects him and he keeps swimming (JP-1.0 behavior)

#### Scenario: Walk-into-water still ejects (entry is JP-identical)
- **WHEN** Link has no flippers, the flag is set, and he walks directly onto a deep-water tile
- **THEN** he is ejected — the entry guard is byte-identical JP↔US, so a flipperless walk-in never swims in either version (this is what makes it the glitch, not an "always-swim" ability)

#### Scenario: US per-frame eject preserved with the flag off
- **WHEN** Link has no flippers and `kFeatures0_RestoreJpGlitches` is not set
- **THEN** every swim-entry site ejects him per US-1.0 behavior (unchanged), at every entry path

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

