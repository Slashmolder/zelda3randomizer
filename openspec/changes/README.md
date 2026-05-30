# OpenSpec changes — entry-point index

This directory holds every active and pending OpenSpec change for the zelda3 randomizer. Use this index to navigate.

## Status overview

| Phase | # | Change | Scope | Authoring | kGen bump | Effort |
|---|---|---|---|---|---|---|
| **A** | — | [`add-randomizer-support`](archive/2026-05-29-add-randomizer-support/) | Foundation, RNG, share string, predicate VM, codegen, audit, logic graph (Open + Standard), assumed-fill placement, prize/medallion shuffles, sphere computation, goal-completability, JSON+text spoilers, sidecar save format, §6 grant-site dispatch (43 sites), snapshot tail TLV. | **Archived 2026-05-29** (119/139; the 20 deferrals are tracked in the Phase B/C/D changes below) | n/a (baseline) | ✅ shipped + archived → `openspec/specs/randomizer-*` (6 capabilities) |
| **B** | 1 | [`add-rando-confirmation-icons`](add-rando-confirmation-icons/) | Visible direct-grant icon ancilla (extends §7.6 audio-only) | Full | No | 3-5d |
| **B** | 2 | [`add-rando-trackers`](add-rando-trackers/) | In-game item + location overlays; checked-bitmap r/w | Full | No | 1-2w |
| **B** | 3 | [`add-rando-race-mode-reveal`](add-rando-race-mode-reveal/) | Spoiler suppression + `RevealSpoiler` action with SHA-256 stamp | Full | No | 3-5d |
| **B** | 4a | [`add-rando-inverted-world-state`](add-rando-inverted-world-state/) | Inverted region graph (2977 PHP lines) + RegionRemap + Bug #12 starting-inv | Full (50/77 landed — graph + runtime in; B-inverted macros 2.x, docs, playtest still open) | **Yes** | 4-6w |
| **B** | 4b | [`add-rando-retro-world-state`](add-rando-retro-world-state/) | Retro shop dispatch + 4 Retro flag pin | Full (1/48) | **Yes** | 1w |
| **B** | 4c | [`add-rando-retro-takeany`](archive/2026-05-29-add-rando-retro-takeany/) | 31 TakeAny shop dispatch + RNG-driven activation (promoted from 4b's design §5 split) | ✅ **Archived 2026-05-29** | **Yes** | ✅ shipped + archived |
| **B** | 5 | [`add-rando-trick-logic-and-axes`](add-rando-trick-logic-and-axes/) | OP_TRICK/OP_DIFFICULTY_AT_LEAST/OP_GLITCH_LEVEL_AT_LEAST + 5 settings un-pins + Bug #7 per-item rewind | Full | **Yes** | 2-3w |
| **B** | 6 | [`add-rando-hints`](add-rando-hints/) | New `randomizer-hints` capability: Sahasrahla / storyteller / bookshelf / Murahdahla | Full | **Yes** | 2-3w |
| **B** | 7 | [`add-rando-shuffles-and-minigames`](add-rando-shuffles-and-minigames/) | Boss + drop-pool shuffles + §6.8 minigame dispatch | Full | **Yes** | 2-3w |
| **B** | 8 | [`add-rando-switch-swkbd`](add-rando-switch-swkbd/) | libnx swkbd wrapper for Switch text input | Full | No | 3-5d |
| **B** | PC | [`add-rando-native-settings-window`](add-rando-native-settings-window/) | Dear ImGui second OS window owns the rando settings UI on PC; in-game screen compile-guarded out behind `Z3R_NATIVE_SETTINGS_WINDOW` | Full (114/141 — **shipped & merged**; remaining = spoiler-viewer polish 13.7/14.3-5 + manual verify/audit/archive) | No | ✅ shipped, not yet archived |
| **C** | C1 | [`add-rando-entrance-shuffle`](add-rando-entrance-shuffle/) | 4-mode entrance shuffle (uses RegionRemap from #4a) | Stub | **Yes** | 4-8w |
| **D** | D1 | [`add-rando-cosmetic-shuffles`](add-rando-cosmetic-shuffles/) | Palette / sprite / music shuffles; `cosmetic_seed` separate from `settings_hash` | Stub | No | 3-4w |
| **D** | D2 | [`add-rando-customizer-mode`](add-rando-customizer-mode/) | Manual per-location placement + custom pool composition | Stub | No (per-seed; affects `customizer_seed`) | 2-3w |
| **D** | D3 | [`add-rando-major-glitch`](add-rando-major-glitch/) | Extends Phase B #5's `OP_GLITCH_LEVEL_AT_LEAST` to HybridMG + NoLogic | Stub | **Yes** | 2-3w |
| **D** | D4 | [`add-rando-auto-tracker`](add-rando-auto-tracker/) | Local TCP server for external tracker clients | Stub | No | 2-3w |

Total: 15 active changes + 2 archived (`add-randomizer-support`, `add-rando-retro-takeany`). All 15 active changes pass `openspec validate --changes`.

## Implementation order

Recommended ordering:

1. ✅ **Phase A archived** (2026-05-29) — baseline specs live in `openspec/specs/randomizer-*`. Phase B/C/D changes now modify those specs.
2. **Phase B warm-up**: #1 confirmation-icons → #2 trackers → #3 race-mode reveal.
3. **Phase B world-states**: #4b Retro (small) → #4a Inverted (large).
4. **Phase B logic + UX**: #5 trick logic → #6 hints → #7 shuffles+minigames → #8 swkbd.
5. **Phase B exit**: each change archives independently once its tasks + fresh-eyes audit complete.
6. **Phase C**: C1 entrance shuffle (requires #4a archived).
7. **Phase D**: D1-D4 (D3 requires #5 archived; D1/D2/D4 are independent).

## Dependencies graph

```
Phase A archive ✅ (2026-05-29)
       │
       ├──► #1, #2, #3 (warm-up, parallel-safe)
       ├──► #4b Retro
       ├──► #4a Inverted ──────────────────────────► C1 entrance shuffle
       ├──► #5 trick logic ─────────────────────────► D3 major glitch
       ├──► #6 hints
       ├──► #7 shuffles + minigames
       ├──► #8 switch swkbd
       └──► D1, D2, D4 (Phase D parallel-safe)
```

## File-overlap hotspots

- **`src/sprite_main.c`**: #1, #4b, #6, #7 — serialize.
- **`src/rando/rando.{c,h}`**: #1, #3, #6 — #1 first, then #3/#6 in parallel.
- **`src/select_file.c`**: #3, #4a, #4b — #4a and #4b touch the same line (un-gate at 2520); sequence them.
- **`src/rando/rando_placement.c`**: #4a (Bug #12), #4b (BuildItemPool), #5 (per-item rewind) — serialize.

## Tools

- `openspec validate --changes` — validate all changes.
- `openspec list` — view per-change task progress.
- `openspec list --specs` — view the spec baseline (6 `randomizer-*` capabilities live as of the Phase A archive, 2026-05-29).

## When you're ready to start a change

1. Pick the change folder from the table above.
2. Read its `README.md` for the entry-point index, then `proposal.md`, then `design.md` (if present), then spec deltas, then `tasks.md`.
3. Run `openspec apply <change-name>` (or `/openspec-apply`) to walk through tasks.
4. For stub-status changes (Phase C/D + new Phase B work that hasn't been promoted), run `/openspec-explore <change-name>` first to flesh out tasks/design.

## Cross-cutting conventions

Every kGen-bump change SHALL:
- Bump `kGeneratorVersion` in `src/rando/rando.h`.
- Regenerate the regression corpus via `assets/scripts/bump_rando_corpus.py`.
- Verify default-settings digests remain byte-identical to the pre-change baseline (the canonical settings byte-layout is **append-only**; never change byte positions, widths, or enum values for existing fields — Phase A pre-declared the full enum space, so Phase B+ only *un-pins* user input to existing values).
- If `location_registry.yaml` / `item_registry.yaml` grew, verify existing IDs are unchanged (append-only registry).
- Backward-load test: a slot written by `generator_version = N` SHALL load on an `N+1` binary with a one-time informational warning (per `randomizer-save / Embedded placement table — upgrade safety`); no regeneration required.
- Pass audit-guard (`assets/scripts/check_audit_guard.py`), determinism (`check_determinism.py`), and codegen-wiring (`check_codegen_wiring.py`) checks.

Per `CLAUDE.md` "Fresh-eyes audit cadence": every Phase B change SHALL schedule a fresh-eyes audit pass after the main authoring lands. Memory `[[cluster-audit-cadence]]` documents the pattern.
