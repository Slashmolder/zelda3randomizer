## ADDED Requirements

### Requirement: JP-1.0 overworld music selection toggle

The system SHALL expose a gameplay-feature flag `kFeatures0_JpOverworldMusic` (a bit in `features0` at `g_ram[0x64c]`) that selects the destination-screen overworld music + ambient sound the way the Japanese 1.0 ALTTP ROM does instead of US 1.0, in the two routines that run after a mirror warp / long screen transition (`MirrorWarp_FinalizeAndLoadDestination`, `Overworld_FinalizeEntryOntoScreen`). The flag SHALL default to **off**; when off, runtime behavior SHALL be byte-identical to the unmodified US-1.0 reimplementation. ONLY the `music_control` (`$12C`) and `sound_effect_ambient` (`$12D`) writes may differ between the JP and US branches — the warp/transition destination, Link position, overworld screen index, and camera/scroll math are byte-identical JP↔US and SHALL be untouched.

The JP branch SHALL reproduce the JP-1.0 disassembly faithfully (it derives the post-warp track from world state + screen index + progress rather than re-reading the `overworld_music[]` scratch table; the transition routine writes `music_control` only when the transition track is armed, `music_unk1 == 0xf1`, and leaves `sound_effect_ambient` untouched there). The US else-branch SHALL stay byte-identical to the prior port.

#### Scenario: Default off is vanilla-identical
- **WHEN** `kFeatures0_JpOverworldMusic` is not set (the default)
- **THEN** both routines take their US-1.0 music branch and the running game is behavior-identical to the unmodified port

#### Scenario: JP music selection when enabled
- **WHEN** the player sets `kFeatures0_JpOverworldMusic` and walks through a mirror warp or long screen transition
- **THEN** the destination screen's track + ambient are chosen by the JP-1.0 logic (world state + screen index + progress), not the US `overworld_music[]` table — while Link's landing position and the camera are unchanged

### Requirement: Side-by-side RAM-compare safety

The flag's point-of-use gate SHALL additionally suppress the JP behavior whenever the original ROM is attached for side-by-side comparison (`ZeldaIsEmulatorAttached()` is true), so the per-frame RAM/SRAM/VRAM comparator does not diverge. The concrete gate is `(enhanced_features0 & kFeatures0_JpOverworldMusic) && !ZeldaIsEmulatorAttached()`. The JP track diverges from the US-1.0 reference RAM every frame the JP branch runs, so default-off plus this suppression keeps verification runs clean.

#### Scenario: Suppressed under side-by-side compare
- **WHEN** the original US ROM is attached for comparison AND `kFeatures0_JpOverworldMusic` is set
- **THEN** both routines take their US-1.0 music branch for the comparison run and the RAM comparator does not diverge on account of this feature

### Requirement: No randomizer or save-format impact

Enabling or disabling `kFeatures0_JpOverworldMusic` SHALL NOT affect randomizer placement, the canonical settings serialization, `settings_hash`, `kGeneratorVersion`, or the regression corpus. It is a general gameplay-audio toggle, not a randomizer axis, and the headless `--generate-seed` placement path never runs the gated routines.

#### Scenario: Corpus byte-identical
- **WHEN** the regression corpus is regenerated with this feature present (default-off)
- **THEN** every entry's `placement_digest` is byte-identical and no `kGeneratorVersion` bump is required
