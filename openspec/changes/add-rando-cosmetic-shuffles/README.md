# add-rando-cosmetic-shuffles

**Phase D** — palette / sprite / music shuffles. Cosmetic only; SHALL NOT affect placement, `settings_hash`, or determinism. Drives off a `cosmetic_seed` that lives in **client config** (`zelda3.ini`), not the slot — so the same `share_string` yields a personalized look per player.

## Status

**Design + tasks authored** (was a proposal-only stub). Authored: 2026-05-26; design fleshed: 2026-06-01. Ready to implement; no generation-path blockers.

## Read these in order

| File | Purpose | Status |
|---|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities, impact | ✅ authored |
| [design.md](design.md) | The reframe (primitives already exist), client-config decision, MVP palette modes, hook points | ✅ authored |
| [specs/randomizer-shuffles/spec.md](specs/randomizer-shuffles/spec.md) | Cosmetic-shuffle contract (MODIFIED Phase A requirement) | ✅ authored |
| [specs/randomizer-ui/spec.md](specs/randomizer-ui/spec.md) | Cosmetic settings surface — the four INI keys (ADDED) | ✅ authored |
| [tasks.md](tasks.md) | Implementation checklist (11 sections) | ✅ authored |

> The original stub's `randomizer-save` delta (a `cosmetic_seed` slot-header field) was **dropped**: cosmetics are client-config, so no save-format / canonical change.

## Effort

Revised **down** from the stub's "3-4 weeks each axis." The fork already owns the rendering primitives (ZSPR loader, MSU-1 audio, palette buffers); this change is the deterministic selection/transform driver. Sprite axis is small; palette + music are medium.

## Determinism

**No `kGeneratorVersion` bump, no corpus regen, no canonical-size cascade** — cosmetics are outside the generation path. A separate cosmetic-determinism CI step replaces corpus regen.

## Dependencies

- Phase A archived. No Phase B / C dependency.
