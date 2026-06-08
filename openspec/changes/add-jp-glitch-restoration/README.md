# add-jp-glitch-restoration

A single "Restore JP 1.0 glitches" gameplay checkbox that re-introduces glitches present in the **Japanese 1.0** ALTTP ROM but patched out of the **US 1.0** ROM this project reimplements. General game feature (a `features0` bit) — **not** a randomizer axis.

## Status

**Proposal — not yet implemented.** Authored 2026-06-07; revised 2026-06-08 after a fresh-eyes audit. The "find what's patched" research is complete (six-glitch catalog in `proposal.md`, web-grounded). **No glitch is a zero-spike slice.** **Fake Flippers** is the lowest-risk first target but is **MEDIUM**: the often-cited `src/player.c:1721` is dead code for the walk-in case (blocked earlier at `:3193`), the real gate is a multi-site set, and the true JP glitch is an ~8-frame swim grace + transition lock, not free-swim. The other five glitches are gated behind a JP-vs-US disassembly-diff spike (Task 1) and may individually resolve to INFEASIBLE.

## Read these in order

| File | Purpose |
|---|---|
| [proposal.md](proposal.md) | Why, the six-glitch catalog + confidence, the master checkbox, RAM-compare gating, rando-logic relationship |
| [design.md](design.md) | Flag placement (`kFeatures0` bit 18), per-glitch source mapping, the verification gate, UI/config wiring |
| [specs/jp-glitch-restoration/spec.md](specs/jp-glitch-restoration/spec.md) | NEW capability — master toggle, side-by-side safety, per-glitch gate, Fake Flippers MVP |
| [specs/game-config-ui/spec.md](specs/game-config-ui/spec.md) | MODIFIED — the gameplay-panel checkbox |
| [tasks.md](tasks.md) | Implementation checklist (spike → flag → Fake Flippers → UI → follow-ons) |

## Key facts

- **Flag**: `kFeatures0_RestoreJpGlitches = 262144` (bit 18, the next free `features0` bit; bits ≤17 are taken per `src/features.h:55-90`). No new `kRam_*` byte.
- **Default off** (primary safety, the actual `kFeatures0_EasyMinigames` precedent) + **side-by-side off** via a `!ZeldaIsEmulatorAttached()` gate (a *new*, stronger guard than EasyMinigames uses — it diverges every frame). Every restored glitch diverges from the original US ROM.
- **INI**: a new named `[Features]` boolean key (`kFeatKeys`/`kFeatMasks` + `ParseBoolBit`) — not an integer mask.
- **No rando / corpus / `kGeneratorVersion` impact** — general game feature.
- **Single master toggle** (owner decision); per-glitch sub-control is a designed-for future extension (`design.md` D3).

## Sources

- Glitch catalog: <https://alttp-wiki.net/index.php/JP_1.0>, <https://alttp-wiki.net/index.php/Version_Differences> (per-glitch pages cited in `proposal.md`).
- Fake Flippers cause + JP 8-frame grace mechanism: <https://alttp-wiki.net/index.php/Fake_Flippers> → gate set `src/player.c:3193`, `:129-136`, `:1721`, `:710`, `:918`.
