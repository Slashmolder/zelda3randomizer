## Context

The reimplementation copied US-1.0 behavior into C. A JP-1.0 glitch that was "patched" in US 1.0 therefore shows up as a specific US-side guard/branch in `src/*.c`. Restoring the glitch = bypassing that guard behind a flag. The hard part is not the flag — it is **knowing, per glitch, exactly which US instruction(s) changed**, which for five of the six is undocumented (see proposal catalog). This design fixes the flag mechanism and the Fake Flippers slice, and frames the rest as a grounded spike.

## Goals / Non-Goals

**Goals**
- One master "Restore JP 1.0 glitches" checkbox (per the owner's decision: single toggle, not per-glitch).
- Default off; side-by-side-off; vanilla/rando byte-identical unless opted in.
- Ship Fake Flippers as the first verified glitch under the flag.
- A repeatable per-glitch verification gate so no glitch ships from memory.

**Non-Goals**
- Per-glitch sub-toggles (deferred — see D3).
- Coupling to randomizer glitch-logic (`OP_GLITCH_LEVEL_AT_LEAST`) — a future change.
- JP/US *content* differences that are not glitches (text speed, item names, the JP intro) — out of scope.
- A full JP-1.0 emulation mode. This restores discrete glitches, not the whole ROM revision.

## Decisions

### D1: Flag placement — `kFeatures0` bit 18, no new RAM

`kFeatures0_RestoreJpGlitches = 262144` (bit 18). Verified free: `src/features.h:55-90` uses bits up to `kFeatures0_EasyMinigames = 131072` (bit 17); bits 18–31 of the `Features0` uint32 (`g_ram[0x64c]`) are unused. No `kRam_*` allocation is needed (the 14-byte `0x662-0x66f` headroom block is untouched). Read at point-of-use with `enhanced_features0 & kFeatures0_RestoreJpGlitches` (`features.h:104`). This mirrors how `kFeatures0_EasyMinigames` is declared and consumed.

**Why `features0`, not `features1`**: `features1` (`g_ram[0x659]`) is the *randomizer* flag bank (`kFeatures1_RandomizerActive`). JP-glitch restoration is a general game feature available in plain vanilla play, so it belongs in `features0` next to the other gameplay toggles.

### D2: Default off + side-by-side off (RAM-compare invariant)

Every restored glitch makes the C runtime diverge from the original US ROM, which would trip the per-frame RAM comparator in side-by-side mode. So:
- INI default is off (the default for the new named `[Features]` key is 0).
- **Primary safety is default-off.** This is the actual `kFeatures0_EasyMinigames` precedent — contrary to an earlier draft, EasyMinigames is NOT gated by `ZeldaIsEmulatorAttached()`: its point-of-use gates (`src/dungeon.c`, `src/player.c`) are `(enhanced_features1 & kFeatures1_RandomizerActive) || (enhanced_features0 & kFeatures0_EasyMinigames)`, and its side-by-side cleanliness comes solely from being off in vanilla (`features.h:82-89` comment). 
- **Additional stronger suppression (new pattern).** Because these glitches diverge every frame (unlike a once-per-minigame divergence), each gate ALSO checks `!ZeldaIsEmulatorAttached()` (`src/zelda_rtl.c:469`) so a user who manually enables the flag and then attaches the ROM still gets a clean compare. Concretely a glitch's gate is `(enhanced_features0 & kFeatures0_RestoreJpGlitches) && !ZeldaIsEmulatorAttached()`. This is a new, stronger guard than EasyMinigames uses — owned as new, not attributed to an existing pattern.

This keeps the corpus, `--rando-selftest`, and the side-by-side reference runs untouched.

### D3: Master toggle now; per-glitch later

The owner chose a single master checkbox. The flag is one bit and means "enable every JP-1.0 glitch this build has verified-and-implemented." If per-glitch control is later wanted, the clean extension is a **second uint32 glitch-select bitfield** (a free `features0` bit or a new `kRam_*` byte from the `0x662-0x66f` headroom) read only when the master bit is set — additive, no migration. Designed-for, not built.

### D4: Per-glitch verification gate (claim-grounding)

No glitch ships behind the flag until its JP behavior is grounded:
- **Fake Flippers** — the JP-vs-US ROM diff is now done (the spike). ROM truth: JP simply **omits** the per-frame flipper recheck US added to `PlayerHandler_04_Swimming` (`:1721`). The swim-ENTRY ejects (`CheckAbilityToSwim` `:129-136`, `LinkState_CrossingWorlds` `:3192-3213`) are **byte-identical JP↔US** — NOT gated. There is **no grace counter** (the wiki's "8-frame grace" was a runner description, not JP code). Faithful restoration = gate exactly the one handler site; the glitch is reached via the un-flipper-checked ledge/recoil entry (`:710`, `:918`). IMPLEMENTED faithful on the branch.
- **The other five** require Task 1's JP-vs-US 65816 disassembly diff. For each, record in `audit.md`: the JP instruction(s), the US instruction(s) that replaced them, the corresponding `src/*.c` site, and a verdict — IMPLEMENTABLE (with the gated bypass) or INFEASIBLE/N-A (the reimpl's C control flow doesn't reproduce the raced behavior, or the glitch depends on a hardware/timing artifact the port doesn't model). An INFEASIBLE verdict is a valid, documented result — record why.

### D5: Per-glitch source mapping (spike starting points)

From the research dossier (anchors to confirm during the spike, not asserted as the fix):

- **Fake Flippers** → the gate is a SET of sites, not one branch (audit correction):
  - **Deep-water swim guard** in `LinkState_CrossingWorlds` (`src/player.c:3172`; guard at `:3192-3213`) — `:3193 if (link_item_flippers)` sets `kPlayerState_Swimming`; the no-flippers fall-through calls `CheckAbilityToSwim()` (`:3212`).
  - **Eject decision** `CheckAbilityToSwim` `:129-136` — no flippers ⇒ `submodule_index = player_is_indoors ? 20 : 42` (the eject). This is what blocks the normal walk-in case; `:1721` is *unreachable* there.
  - **Handler eject** `PlayerHandler_04_Swimming` `:1721` (`if (!link_item_flippers) return;`) — reached only via the **un-flipper-checked** ledge/jump-into-water sites that set swim state directly: `:710-712` and `:918-920` (both `kPlayerState_Swimming` + `Link_SetToDeepWater()` with no flipper check).
  - **No grace counter (ROM diff).** JP omits the per-frame recheck entirely — there is no 8-frame countdown in the ROM. The faithful change gates ONLY the `:1721` handler recheck; the entry ejects are byte-identical JP↔US and stay ungated (gating them is the rejected always-swim bug). The glitch is reached via the un-flipper-checked ledge/recoil entry (`:710`/`:918`).
- **Superspeed** → `link_speed_setting`; dash arms `=16` at `:1269`; the suspect is one of the transition resets at `:789,973,1042,1071,1166` that JP omitted. Depends on the dash-arming half (Spindash/Itemdash), so sequence it after those.
- **Spindash** → `Player_Sword_SpinAttackJerks_HoldDown` `:2281+`, charge-complete `:2300` (`==48`), `Link_ActivateSpinAttack` `:2958`; the same-frame B-release+A arbitration lives in the control handler.
- **Itemdash** → `LinkState_Dashing` `:1196+`, `Link_HandleYItem` (def `:2036`; called `:457`); same-frame Y+A arbitration. Several payloads chain into Superspeed.
- **Mirror Block Erase** → movable-block motion `src/dungeon.c:5637-5676` + stepping `:8779-8807`; mirror dispatch `src/player.c:2107`. Suspect: a US guard that finalizes/aborts the in-flight block before the mirror routine runs.
- **Death Hole** → death-vs-pit submode arbitration across `src/player.c:822-934,1447-1524`; not localized to one line; the reimpl may already resolve the race deterministically (US behavior baked in). Highest-risk; treat as a pure research spike.

### D6: UI + config wiring

- **Checkbox**: gameplay-feature panel in `src/rando/rando_window/game_config_panels.cpp` (where the existing `features0` checkboxes live, e.g. `FeatureCheckbox`). PC-only under `Z3R_NATIVE_SETTINGS_WINDOW`; Switch keeps its in-game config path (the bit still parses from INI on Switch).
- **INI**: each `features0` bit is its own named boolean key (not an integer mask — audit correction). Add a new key (e.g. `RestoreJpGlitches`) with: a `ParseBoolBit(value, &g_config.features0, kFeatures0_RestoreJpGlitches)` branch in `src/config.c:615-646`, plus aligned entries in `kFeatKeys[]` and `kFeatMasks[]` (`config.c:1193-1208`, guarded by their `_Static_assert`). A short tooltip per `[[tooltip-brevity]]`: a durable one-liner ("Re-enables glitches removed in the US 1.0 release"), no WIP/caveats.

## Risks / Trade-offs

| Risk | Mitigation |
|---|---|
| Five glitches' causes are undocumented; some may be INFEASIBLE in the C reimpl | The spike (Task 1) is allowed to return INFEASIBLE per glitch with a recorded reason; the master flag ships with whatever subset is verified (Fake Flippers at minimum) |
| A restored glitch silently breaks side-by-side RAM compare | D2 gate forces the glitch off whenever the original ROM is attached; default off |
| Input-arbitration glitches can't be reproduced without reconstructing the 65816 frame race | Spike documents this per glitch; reconstruction (if attempted) is a separate, larger effort flagged in the verdict |
| Master toggle hides which glitches are actually active | `docs/` note + the spec's per-glitch verification table is the source of truth for what the master bit currently enables |

## Open Questions

1. Does the reimplementation already exhibit any of the five (i.e. the US "fix" wasn't carried over)? The spike answers this per glitch — if a glitch already works, the flag is a no-op for it and that is recorded.
2. Should the eventual per-glitch bitfield (D3) live in a `features0` bit + a new `kRam_*` select byte, or fold into a single richer encoding? Deferred until per-glitch control is actually requested.
3. Switch UX: the in-game config screen has no panel for this; is INI-only acceptable on Switch (consistent with the PC-only native window policy)? Assumed yes.

## Audit notes (fresh-eyes pass, 2026-06-08)

Corrections folded in from an adversarial review (each re-verified against source):

- **Fake Flippers is not a one-line flip (HIGH).** `:1721` is dead for the walk-in case; the real gate set is `:3193` + `CheckAbilityToSwim` `:129-136` + the ledge/jump-in sites `:710`/`:918`. D5 rewritten. Feasibility downgraded EASY→MEDIUM.
- **JP behavior re-grounded (HIGH).** The audit had described an "8-frame grace + transition lock"; the later JP-vs-US ROM diff disproved the grace counter (JP just omits the per-frame recheck) — see Post-implementation reconciliation. Faithful = gate the single `:1721` site.
- **EasyMinigames is NOT gated by `ZeldaIsEmulatorAttached()` (HIGH).** Its safety is default-off only. We adopt `!ZeldaIsEmulatorAttached()` as a *new* stronger guard, no longer misattributed. D2 corrected.
- **No `[Features] features0` integer mask (MED).** Each bit is a named key + `kFeatKeys`/`kFeatMasks` + `ParseBoolBit`. D6 + tasks corrected.
- **Rando `fake-flippers` trick ↔ this flag (MED).** Trick-on + flag-off is a soft-softlock hazard; marked unsupported in the spec delta.

Confirmed sound by the audit: flag-bit arithmetic (`262144` = bit 18, free), "no version flag in src/", OpenSpec structure (new capability + ADDED requirement on `game-config-ui`), `FeatureCheckbox` helper exists.

## Post-implementation reconciliation (2026-06-08)

Folded back from the implementation branch `claude/jp-glitch-restoration` (build-verified, not playtested):

- **Function name.** The deep-water swim guard cited at `src/player.c:3192-3213` lives in `LinkState_CrossingWorlds` (def `:3172`), not a distinct "walk-in" handler — the line numbers and the eject set are correct; only the colloquial name was loose. The shipped code names the real function. References above corrected.
- **WSL build covers the UI.** The WSL `make` build defines `Z3R_NATIVE_SETTINGS_WINDOW`, so the ImGui checkbox compiles under it — there is no MSBuild-only gap for this change (contrary to D6's caution).
- **JP-vs-US disassembly spike (2026-06-08) — overturns the "8-frame grace".** The ROM diff (both ROMs headerless 1 MB LoROM; relocation-normalized, code banks separately re-linked) proved JP has **no grace counter**. `CheckAbilityToSwim` and `LinkState_CrossingWorlds` are byte-identical JP↔US; the *only* swim delta is the per-frame recheck US added to `PlayerHandler_04_Swimming` (`:1721`). The earlier branch implementation **over-gated** all three sites → "always-swim", which the owner rejected on playtest. **Faithful fix (shipped on branch): gate ONLY `:1721`.** Walk-into-water still ejects; flipperless swim only via the un-flipper-checked ledge/recoil entry — byte-faithful to JP. The labeled free-swim approximation is removed.
- **Two more glitches IMPLEMENTED faithful from the spike's exact deltas:** Death Hole — gate the US-added `link_disable_sprite_damage++` per pit-fall frame (`LinkState_Pits`, `:1576`); Itemdash — relax the US-added StartDash guard so `Link_HandleYItem` fires on the dash frame (`PlayerHandler_00_Ground_3`, `:249`). Both byte-faithful; full-tech (warp / payloads) playtest-pending.
- **§5.3 deeper spike (2026-06-08) — Mirror Block Erase FOUND + IMPLEMENTED; Superspeed/Spindash have no isolated delta.** Extended the JP-vs-US diff (relocation-normalized + lockstep) to the controller-read/NMI input-latch path and the area/transition + mirror/block handlers:
  - **Input-latch** (`Interrupt_NMI`, `NMI_ReadJoypads`): byte-identical → the same-frame input-race glitches are not an input-filter delta.
  - **Mirror Block Erase — IMPLEMENTABLE, now shipped.** In `DoSwordInteractionWithTiles_Mirror` (`$87:A95C`) indoor path, JP 1.0 clears `index_of_changable_dungeon_objs` (`$5FC/$5FD`) **unconditionally and BEFORE** `Mirror_SaveRoomData` (proven: JP's `$82:A0A8` == US `Mirror_SaveRoomData` `$82:A1B1`, relocation-only) → a mid-push block is flagged absent and erased on the mirror reload. US 1.0 reordered the save ahead of the now-`sound_effect_1!=60`-gated clear. Restored the JP order under `JpGlitchEnabled()`; else-branch byte-identical. Playtest-CONFIRMED (owner). (The earlier "NEEDS-MORE-WORK" verdict missed this — D5 anchored on push-block motion + a stale "mirror dispatch :2107" line, not `DoSwordInteractionWithTiles_Mirror`.)
  - **Superspeed / Spindash — SAME root cause as Itemdash (playtest-CONFIRMED).** The 723-routine sweep found no separate delta because there isn't one: the single US-added StartDash guard in `PlayerHandler_00_Ground_3` (`BEQ +$19`) skips the WHOLE item-and-sword block on the dash-start frame — both `Link_HandleYItem` (Itemdash; Hookdash/Medalliondash arm Superspeed) AND `Link_HandleSwordCooldown` (the spin-charge pipeline → Spindash). The armed `link_speed_setting==0x10`+walking-control state, activated by walking off a manual staircase, is Superspeed. The one `JpGlitchEnabled()` bypass restores all three; the owner confirmed Superspeed + Spindash work. The stairs handlers carry no `$5E`/`$372` reset delta (the "stairs-exit `$5E` reset" hypothesis is disproven). **All six cataloged JP-1.0 glitches are restored.**
