# Tasks — add-jp-glitch-restoration

## 1. Discovery spike — JP-vs-US disassembly diff (the "find what's patched" half)

- [ ] 1.1 For each of the five UNVERIFIED glitches (Superspeed, Spindash, Itemdash, Mirror Block Erase, Death Hole), diff the JP 1.0 vs US 1.0 65816 at the relevant routine and record the exact instruction(s) the US release changed. JP 1.0 ROM reference is available at `C:\src\Enemizer\Zelda no Densetsu - Kamigami no Triforce (J) (V1.0).smc`; US disassembly reference per `README.md` (spannerism JP disasm noted as a dev source).
- [ ] 1.2 Map each diff to its `src/*.c` site (starting points in `design.md` D5) and confirm whether the reimplementation reproduces the relevant control flow at all.
- [ ] 1.3 Record per-glitch in `audit.md`: JP behavior, US change, `src/file.c:line`, verdict (IMPLEMENTED / PENDING-SPIKE / INFEASIBLE-N-A) + reason. INFEASIBLE is an acceptable outcome — document why.
- [ ] 1.4 Confirm none of the five already works in the port (the US "fix" might not have been carried over). If one does, record it as a no-op for the flag.

## 2. Flag + config plumbing

- [ ] 2.1 Add `kFeatures0_RestoreJpGlitches = 262144` (bit 18) to the `kFeatures0_*` enum in `src/features.h`, with a comment noting it is behavior-affecting + default-off + side-by-side-off.
- [ ] 2.2 Wire a new named `[Features]` boolean key (e.g. `RestoreJpGlitches`) in `src/config.c`: a `ParseBoolBit(value, &g_config.features0, kFeatures0_RestoreJpGlitches)` branch (`:615-646`), aligned entries in `kFeatKeys[]` + `kFeatMasks[]` (`:1193-1208`, guarded by their `_Static_assert`), and a default-off. (There is no `[Features] features0` integer mask — do not edit one.)
- [ ] 2.3 Verify the bit round-trips through `zelda3.ini` (write → re-parse → identical) per `game-config-ui / Round-trip fidelity`.

## 3. Fake Flippers (first target — MEDIUM, multi-site)

- [ ] 3.0 Grounding spike: confirm in source where the per-frame eject lives vs. where an ~8-frame grace counter would slot in, and which swim-entry sites must change. The JP behavior is a grace window + transition lock, not "never eject" (see design D5).
- [ ] 3.1 Restore the JP grace across the gate set, all under `(enhanced_features0 & kFeatures0_RestoreJpGlitches) && !ZeldaIsEmulatorAttached()`:
  - walk-in entry guard `src/player.c:3192-3213` (`:3193` flipper check → `CheckAbilityToSwim` at `:3212`);
  - eject decision `CheckAbilityToSwim` (`:129-136`) — the no-flippers submodule 20/42;
  - handler eject `PlayerHandler_04_Swimming` (`:1721`), reached via the ledge/jump-in sites `:710-712` / `:918-920`.
- [ ] 3.2 Implement the grace-window behavior (frame counter) so the transition-lock technique works; if impractical, ship a free-swim approximation **explicitly labeled** as not JP-faithful (UI + docs).
- [ ] 3.3 Playtest: flag off ⇒ every entry path ejects (unchanged); flag on ⇒ the grace + transition-lock technique works; original ROM attached ⇒ eject still fires (clean compare). End-to-end playtest is the only reliable net here.

## 4. UI

- [ ] 4.1 Add the "Restore JP 1.0 glitches" checkbox to the gameplay-feature panel in `src/rando/rando_window/game_config_panels.cpp` (PC-only, `Z3R_NATIVE_SETTINGS_WINDOW`), bound to the bit, with a one-line durable tooltip.
- [ ] 4.2 Verify live-apply (toggling reflects without restart) and Switch build excludes the checkbox.

## 5. Follow-on glitches (one per IMPLEMENTABLE spike result)

- [ ] 5.1 For each glitch Task 1 marked IMPLEMENTABLE, add its gated bypass behind the same master flag (default-off + side-by-side-off), grounded in the recorded diff, and playtest it. (Separate small slices; some may be deferred or dropped per the spike verdicts.)

## 6. Docs + audit

- [ ] 6.1 Document the feature + the per-glitch active-set table in `docs/` (the source of truth for what the master bit currently enables). Per `[[no-report-files-in-repo]]`, no status/report files — only the durable doc.
- [ ] 6.2 Confirm no rando impact: `RandoSettings` / canonical layout / `kGeneratorVersion` / corpus untouched (`game-config-ui / Determinism and randomizer isolation`).
- [ ] 6.3 Fresh-eyes audit pass after the slice lands, per `CLAUDE.md` audit cadence — focus: any glitch path that diverges under side-by-side compare, or a `features0` bit collision.
- [ ] 6.4 Document that the rando `fake-flippers` *placement* trick + this runtime flag OFF is an unsupported combination (assumed swim not executable → soft-softlock risk). No auto-coupling in this change.
