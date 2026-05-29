# OpenSpec changes — entry-point index

This directory holds every active and pending OpenSpec change for the zelda3 randomizer. Use this index to navigate.

## Status overview

| Phase | # | Change | Scope | Authoring | kGen bump | Effort |
|---|---|---|---|---|---|---|
| **A** | — | [`add-randomizer-support`](add-randomizer-support/) | Foundation, RNG, share string, predicate VM, codegen, audit, logic graph (Open + Standard), assumed-fill placement, prize/medallion shuffles, sphere computation, goal-completability, JSON+text spoilers, sidecar save format, §6 grant-site dispatch (43 sites), snapshot tail TLV. | Full (119/139 tasks; remaining 20 are Phase B+ deferrals) | n/a (baseline) | shipped |
| **B** | 1 | [`add-rando-confirmation-icons`](add-rando-confirmation-icons/) | Visible direct-grant icon ancilla (extends §7.6 audio-only) | Full | No | 3-5d |
| **B** | 2 | [`add-rando-trackers`](add-rando-trackers/) | In-game item + location overlays; checked-bitmap r/w | Full | No | 1-2w |
| **B** | 3 | [`add-rando-race-mode-reveal`](add-rando-race-mode-reveal/) | Spoiler suppression + `RevealSpoiler` action with SHA-256 stamp | Full | No | 3-5d |
| **B** | 4a | [`add-rando-inverted-world-state`](add-rando-inverted-world-state/) | Inverted region graph (2977 PHP lines) + RegionRemap + Bug #12 starting-inv | Full | **Yes** | 4-6w |
| **B** | 4b | [`add-rando-retro-world-state`](add-rando-retro-world-state/) | Retro shop dispatch + 4 Retro flag pin | Full | **Yes** | 1w |
| **B** | 5 | [`add-rando-trick-logic-and-axes`](add-rando-trick-logic-and-axes/) | OP_TRICK/OP_DIFFICULTY_AT_LEAST/OP_GLITCH_LEVEL_AT_LEAST + 5 settings un-pins + Bug #7 per-item rewind | Full | **Yes** | 2-3w |
| **B** | 6 | [`add-rando-hints`](add-rando-hints/) | New `randomizer-hints` capability: Sahasrahla / storyteller / bookshelf / Murahdahla | Full | **Yes** | 2-3w |
| **B** | 7 | [`add-rando-shuffles-and-minigames`](add-rando-shuffles-and-minigames/) | Boss + drop-pool shuffles + §6.8 minigame dispatch | Full | **Yes** | 2-3w |
| **B** | 8 | [`add-rando-switch-swkbd`](add-rando-switch-swkbd/) | libnx swkbd wrapper for Switch text input | Full | No | 3-5d |
| **C** | C1 | [`add-rando-entrance-shuffle`](add-rando-entrance-shuffle/) | 4-mode entrance shuffle (uses RegionRemap from #4a) | Stub | **Yes** | 4-8w |
| **D** | D1 | [`add-rando-cosmetic-shuffles`](add-rando-cosmetic-shuffles/) | Palette / sprite / music shuffles; `cosmetic_seed` separate from `settings_hash` | Stub | No | 3-4w |
| **D** | D2 | [`add-rando-customizer-mode`](add-rando-customizer-mode/) | Manual per-location placement + custom pool composition | Stub | No (per-seed; affects `customizer_seed`) | 2-3w |
| **D** | D3 | [`add-rando-major-glitch`](add-rando-major-glitch/) | Extends Phase B #5's `OP_GLITCH_LEVEL_AT_LEAST` to HybridMG + NoLogic | Stub | **Yes** | 2-3w |
| **D** | D4 | [`add-rando-auto-tracker`](add-rando-auto-tracker/) | Local TCP server for external tracker clients | Stub | No | 2-3w |

Total: 15 changes. All pass `openspec validate --changes`.

## Implementation order

Recommended ordering:

1. **Wait** for Phase A archive.
2. **Phase B warm-up**: #1 confirmation-icons → #2 trackers → #3 race-mode reveal.
3. **Phase B world-states**: #4b Retro (small) → #4a Inverted (large).
4. **Phase B logic + UX**: #5 trick logic → #6 hints → #7 shuffles+minigames → #8 swkbd.
5. **Phase B exit**: each change archives independently once its tasks + fresh-eyes audit complete.
6. **Phase C**: C1 entrance shuffle (requires #4a archived).
7. **Phase D**: D1-D4 (D3 requires #5 archived; D1/D2/D4 are independent).

## Dependencies graph

```
Phase A archive
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
- `openspec list --specs` — view archived specs (currently none until Phase A archives).

## When you're ready to start a change

1. Pick the change folder from the table above.
2. Read its `README.md` for the entry-point index, then `proposal.md`, then `design.md` (if present), then spec deltas, then `tasks.md`.
3. Run `openspec apply <change-name>` (or `/openspec-apply`) to walk through tasks.
4. For stub-status changes (Phase C/D + new Phase B work that hasn't been promoted), run `/openspec-explore <change-name>` first to flesh out tasks/design.

## Cross-cutting conventions

Every kGen-bump change SHALL:
- Bump `kGeneratorVersion` in `src/rando/rando.h`.
- Regenerate the regression corpus via `assets/scripts/bump_rando_corpus.py`.
- Verify default-settings digests remain byte-identical to the pre-change baseline.
- Pass audit-guard (`assets/scripts/check_audit_guard.py`), determinism (`check_determinism.py`), and codegen-wiring (`check_codegen_wiring.py`) checks.

Per `CLAUDE.md` "Fresh-eyes audit cadence": every Phase B change SHALL schedule a fresh-eyes audit pass after the main authoring lands. Memory `[[cluster-audit-cadence]]` documents the pattern.
