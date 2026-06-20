# add-jp-glitch-restoration

A single "Restore JP 1.0 glitches" gameplay checkbox that re-introduces glitches present in the **Japanese 1.0** ALTTP ROM but patched out of the **US 1.0** ROM this project reimplements. General game feature (a `features0` bit) — **not** a randomizer axis.

## Status

**Implementation in progress** on the JP-glitch restoration branch. The JP-vs-US disassembly spike (2026-06-08) ground-truthed all six glitches. **3 IMPLEMENTED faithful** (Fake Flippers, Death Hole, Itemdash — each an exact gated JP↔US delta; WSL `-Werror` build green; owner playtest-pending). **3 NEEDS-MORE-WORK** (Superspeed/Spindash/Mirror Block Erase — no JP↔US code delta; not shipped). **Fake Flippers ROM truth**: JP omits *only* the per-frame recheck in `PlayerHandler_04_Swimming` (`:1721`); the entry ejects are byte-identical JP↔US, so the faithful fix gates that one site (the over-gated free-swim approximation was rejected on playtest). No 8-frame grace counter exists.

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
- Fake Flippers / Death Hole / Itemdash mechanisms: the JP-vs-US ROM diff (see `design.md` Post-implementation reconciliation). Faithful gate sites: `PlayerHandler_04_Swimming` `:1721` (FF), `LinkState_Pits` `:1576` (Death Hole), `PlayerHandler_00_Ground_3` `:249` (Itemdash).
