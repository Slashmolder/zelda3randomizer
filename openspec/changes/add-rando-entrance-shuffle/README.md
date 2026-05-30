# add-rando-entrance-shuffle — Phase C entrance shuffle

> **Status: scoped (2026-05-29).** Design + staged tasks authored via
> `/openspec-explore` + the `pc-entrance-spike` worktree investigation and playtest.
> No longer a stub.

## Entry-point index

Read in this order:

1. [proposal.md](proposal.md) — why + scope + the RegionRemap-premise correction.
2. [design.md](design.md) — the model (one π → two halves), RegionRemap retirement,
   coupling mechanism, cave/dungeon fault line, functional-first staging.
3. [tasks.md](tasks.md) — Stages 0–4.
4. [specs/randomizer-shuffles/spec.md](specs/randomizer-shuffles/spec.md) — entrance-shuffle modes.
5. [specs/randomizer-logic/spec.md](specs/randomizer-logic/spec.md) — per-seed edge overlay (RegionRemap retired).
6. [specs/randomizer-save/spec.md](specs/randomizer-save/spec.md) — `TAIL_ENTRANCE_MAP` TLV.

## Key findings from scoping

- The Phase A `RegionRemap` scaffold is **dead code + wrong abstraction** — retired.
  Inverted never used it; entrance shuffle uses a per-seed alternate edge table
  instead (design §1).
- Runtime door redirect is **table-driven and playtest-proven** (design §3a).
- **Coupling** reuses the merged Retro TakeAny source-door-capture idiom (design §3b).
- **No hard dependency on #4a Inverted** — the original premise was false.

## Dependencies

- **Requires**: nothing hard. Reuses already-shipped Inverted (edge-table) +
  Retro TakeAny (door-capture) patterns.
- **Benefits from**: #2 trackers, #6 hints.

## Status checklist

- [x] Proposal drafted + premise corrected
- [x] Design captured (`design.md`)
- [x] Tasks staged (`tasks.md`, Stages 0–4)
- [x] Spec deltas updated (logic spec rewritten off RegionRemap; save spec
      reconciled to the reserved-byte + regen approach)
- [x] Apply-time plan grounded (`apply_plan.md`) + adversarially reviewed
- [x] **Stage 0** — settings axes packed + RegionRemap retired (corpus byte-identical)
- [x] **Stage 1** — coupled cave shuffle: engine + region overrides + door overlay
      + save-regen + spoiler + native UI. Fresh-eyes audited (1 HIGH + 2 MED + 3 LOW
      fixed).
- [x] **Stage 2** — dungeon shuffle (6 single-entrance dungeons): per-seed edge
      overlay + shared door overlay + coupled exit + save-regen + spoiler + UI.
      Fresh-eyes audited (1 HIGH + 1 LOW fixed). corpus(59) green.
- [ ] Stages 1–2 **playtest** (USER — `apply_plan.md` checklist) — esp. cave coupling
      + dungeon coupled-exit (the only seams no automated check covers).
- [ ] Stage 2b (multi-entrance / 2-region / medallion dungeons) + Stage 3 (crossed)
      + Stage 4 (decoupled) + presets
- [ ] Archive (after playtest)
