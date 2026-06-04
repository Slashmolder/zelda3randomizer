# add-rando-retro-world-state

Phase B Slice 3. Shortest Phase B world-state slice. Adds shop-purchase locations to the pool when `world_state == Retro`, routes shop handlers through the dispatcher, pins Retro defaults from `app/World/Retro.php`.

## Status

**Fully authored.** Authored: 2026-05-26. Promoted from stub to full content after grep-grounding the ALTTPR shop subsystem (42 enumerated shop entities) and confirming the audit's §0.1.4 shop subsystem enumeration.

## Read these in order

| File | Purpose | Status |
|---|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities, impact | ✅ authored |
| [specs/randomizer-core/spec.md](specs/randomizer-core/spec.md) | Retro item-pool branch + Retro flag pinning + junk-pad accommodation | ✅ authored |
| [specs/randomizer-placement/spec.md](specs/randomizer-placement/spec.md) | Shop-handler dispatch routing + Take-Any cave dispatch | ✅ authored |
| [specs/randomizer-ui/spec.md](specs/randomizer-ui/spec.md) | World-state picker accepts Retro (ADDED Requirement, not MODIFIED) | ✅ authored |
| [tasks.md](tasks.md) | Implementation checklist (12 sections, ~45 tasks) | ✅ authored |
| `design.md` | Retro-flag-axis vs pinning decision; capacity-upgrade identity-placement | ⏳ deferred (apply-time after a quick prototype) |

## Effort

**~1 week of focused work.** No logic translation; mostly shop-handler instrumentation + pool composition.

## Key upstream references

- `../alttp_vt_randomizer/app/World/Retro.php` (44 lines — Retro extends Open with 4 config flags)
- `../alttp_vt_randomizer/app/Shop.php` (230 lines — shop subsystem)
- `../alttp_vt_randomizer/app/Support/ShopCollection.php` (64 lines — shop collection plumbing)
- `../alttp_vt_randomizer/app/Region/Standard/**/*.php` (42 enumerated shop entities across `Shop` / `Shop\Upgrade` / `Shop\TakeAny` — verified by grep at chunking time)

## Dependencies

- **Phase A archived first.** Deltas multiple specs post-archive.
- **No dependency on #4a Inverted.** Retro extends Open, not Inverted. Recommended order in the chunking plan puts Retro BEFORE Inverted because it's smaller and exercises the world-state-picker un-gate pattern.

## When work starts

1. `/openspec-apply` to walk through tasks.md directly — specs and tasks are authored.
2. design.md decision (Retro-flag-axis pin vs. expose) can be settled inline during §3.2-§4.1 work.
3. `/openspec-archive add-rando-retro-world-state` when done; spec deltas merge into `openspec/specs/randomizer-{core,placement,ui}/spec.md`.
