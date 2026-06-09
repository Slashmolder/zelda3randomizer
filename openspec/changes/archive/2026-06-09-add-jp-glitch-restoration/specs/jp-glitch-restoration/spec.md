## ADDED Requirements

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

The randomizer's `tricks` bitmask already includes a `fake-flippers` *placement* trick (the placer may assume a flipperless swim when routing). That trick and this runtime flag are independent and are NOT auto-coupled by this change. Enabling the `fake-flippers` placement trick while this runtime flag is off is an **unsupported combination**: the placer would assume a swim the runtime cannot perform, risking an unreachable item / soft-softlock. The combination SHALL be documented as unsupported (auto-coupling is a separate future change).

#### Scenario: Trick-on + runtime-flag-off is flagged unsupported
- **WHEN** a seed enables the `fake-flippers` placement trick but the player has not set `kFeatures0_RestoreJpGlitches`
- **THEN** the documentation warns the combination is unsupported (the assumed swim is not executable at runtime); this change does not silently enable the runtime flag
