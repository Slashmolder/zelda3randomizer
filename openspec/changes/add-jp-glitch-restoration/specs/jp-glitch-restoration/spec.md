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

When `kFeatures0_RestoreJpGlitches` is set (and the original ROM is not attached), the system SHALL restore the JP-1.0 swim behavior at the swim-entry gate set, so the documented Fake Flippers technique works. JP-1.0 did not re-evaluate `link_item_flippers` every frame — it allowed an **~8-frame grace** in deep water before ejecting a flipperless Link, which the technique exploits by triggering a screen transition during the grace window to lock the swim state. The US-1.0 path this port reimplements blocks it at multiple sites: the walk-into-water entry guard (`src/player.c:3193`) routes a flipperless Link to `CheckAbilityToSwim` (`:129-136`) which sets the eject submodule (20/42); and the swim handler re-ejects every frame (`:1721`) on the ledge/jump-into-water paths that set swim state without a flipper check (`:710`, `:918`). With the flag set, the implementation SHALL restore the grace-window behavior across that gate set (not merely delete one eject).

The restored behavior SHALL be the JP grace window, NOT permanent flipperless swimming. If reproducing the grace window in the C control flow proves impractical, the build MAY ship a **free-swim approximation** (eject suppressed) only if it is explicitly labeled as an approximation (not JP-faithful) in the UI/docs.

> **Spike status**: even this best-understood glitch requires a short grounding pass (per the verification gate) to confirm where the per-frame eject lives versus where a frame-counter grace slots in. Feasibility is MEDIUM, not a single branch flip.

#### Scenario: JP swim grace restored with the flag on
- **WHEN** Link has no flippers, `kFeatures0_RestoreJpGlitches` is set, the original ROM is not attached, and Link enters deep water and triggers a screen transition within the grace window
- **THEN** the swim state is retained (JP-1.0 Fake Flippers behavior), rather than Link being ejected by the US per-frame check

#### Scenario: US per-frame eject preserved with the flag off
- **WHEN** Link has no flippers and `kFeatures0_RestoreJpGlitches` is not set
- **THEN** every swim-entry site ejects him per US-1.0 behavior (unchanged), at every entry path

### Requirement: Interaction with the randomizer fake-flippers placement trick

The randomizer's `tricks` bitmask already includes a `fake-flippers` *placement* trick (the placer may assume a flipperless swim when routing). That trick and this runtime flag are independent and are NOT auto-coupled by this change. Enabling the `fake-flippers` placement trick while this runtime flag is off is an **unsupported combination**: the placer would assume a swim the runtime cannot perform, risking an unreachable item / soft-softlock. The combination SHALL be documented as unsupported (auto-coupling is a separate future change).

#### Scenario: Trick-on + runtime-flag-off is flagged unsupported
- **WHEN** a seed enables the `fake-flippers` placement trick but the player has not set `kFeatures0_RestoreJpGlitches`
- **THEN** the documentation warns the combination is unsupported (the assumed swim is not executable at runtime); this change does not silently enable the runtime flag
