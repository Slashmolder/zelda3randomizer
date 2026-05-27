# add-rando-confirmation-icons

Phase B Slice 9. Extends Phase A §7.6 `Rando_ShowDirectGrantConfirmation()` (audio + HUD-refresh only) with a per-item-type visible icon ancilla. **Warm-up Phase B change** — small, contained, immediate UX win.

## Status

Authored: 2026-05-26. **Pending Phase A archive** before this change can validate (`openspec validate` runs against archived specs).

## Read these in order

| File | Purpose |
|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities |
| [specs/randomizer-placement/spec.md](specs/randomizer-placement/spec.md) | Spec delta — ADDED Requirements for visible confirmation + call-site enumeration |
| [tasks.md](tasks.md) | Implementation checklist (9 sections, ~25 tasks) |

No `design.md` — change is small enough that the proposal + spec carry the design.

## Key files touched

- **New asset**: `assets/rando/direct_grant_icons.yaml`
- **New generated**: `src/rando/direct_grant_icons.h`
- **New ancilla type**: `src/ancilla.c` + `src/ancilla.h`
- **Modified helper**: `src/rando/rando.c:393` + `src/rando/rando.h:88-114`
- **5 call sites**: `src/player.c:594`, `src/player.c:634`, `src/player.c:3886`, `src/sprite_main.c:1273`, `src/sprite_main.c:18586`

## Verification

- `assets/scripts/check_codegen_wiring.py` — multi-build-system header registration.
- `assets/scripts/check_determinism.py` — no new `rand`/`time` symbols (no-op for this change).
- `assets/scripts/check_audit_guard.py` — no new tracked-cell writes (no-op for this change).
- Manual playtest per `docs/randomizer_playthrough.md` spot-tests + `tasks.md §7`.
- **`placement_digest_hex` byte-identical** before/after — no `kGeneratorVersion` bump.

## Dependencies

- **Phase A archived first.** This change deltas `randomizer-placement` post-archive; cannot validate before `openspec archive add-randomizer-support` runs.
- No upstream slice dependency.

## Why first

Per `docs/randomizer_phase_b.md` recommended ordering: smallest contained slice, immediate UX payoff, exercises the Phase B change-authoring pattern end-to-end (proposal → specs → tasks → apply → archive) with low blast radius.
