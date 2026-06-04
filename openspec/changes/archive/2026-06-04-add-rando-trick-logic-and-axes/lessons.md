# Lessons — add-rando-trick-logic-and-axes (apply)

Apply-time lessons from landing trick logic + the ROM-version scaffolding + the
per-item rewind + swordless mode end-to-end. Companion to the archive's
`add-randomizer-support/lessons.md` (the canonical failure-mode catalog).

## 1. Read the code, not the task counts ("as-built ≠ planned")

The change's `tasks.md` said trick-predicate authoring was the unstarted "2-3 week
bulk." It wasn't — 5 of 8 tricks were already wired + PHP-cited across ~208
predicate uses (prior "Slice 4 §7" work that never ticked its boxes). The genuine
remaining work was the ROM-version scaffolding + the un-pins, which the stale counts
hid. **Before estimating or planning, grep the code for the as-built state.** (See
the `[[openspec-stale-task-counts]]` memory.)

## 2. A missing ROM patch is NOT a hard blocker in a from-scratch C port

Swordless was first assessed **BLOCKED**: ALTTPR's `setSwordlessMode` ROM patches
(Hammer-Ganon, swordless medallions, hammer-tablets, pre-opened curtains) had no
fork `src/` equivalent, so a logic-only swordless would softlock. That instinct was
right about the *risk* and wrong about the *remedy* — with the full game source you
**add the behavior the ROM patch encoded** instead of patching bytes. Each became a
small gated C edit. Don't treat "the upstream does it as a ROM patch we don't have"
as a wall.

## 3. The logic-vs-runtime gap is the dominant bug class, and playtest is the only net

Three swordless bugs each passed the corpus, `--rando-selftest`, AND two independent
fresh-eyes audits — and were caught only by end-to-end playtest:
- `CanKillKholdstare` + `CanKillTrinexx` hard-required a sword (no Hammer path),
  stranding Ice Palace + Turtle Rock crystals (32 unreachable on first generate).
- The **Evil Barrier** at the Agahnim-Tower entrance (`Sprite_EvilBarrier`) cancels
  any non-master-sword hit and electrocutes Link — a *sprite-runtime* gate, invisible
  to the logic graph the audits checked.

The corpus proves **reachability**; it can never prove **performability**. Playtest
at the *climax* (Ganon), not just generation. (Reinforces `[[logic-vs-runtime-gap]]`,
`[[playtest-vs-codegen-tests]]`.)

## 4. When a runtime-model change lands, re-check every design axis that assumed the old model

The `add-rando-fairy-chest-model` change retired the Pyramid Fairy Sword/Bow slots
and the bow trade-in. That silently invalidated **two** of this change's design
decisions: D5 (swordless `can_place` on `LOC_Pyramid_Fairy_Sword`) and D7
(`pyramid_bow_upgrade=arrows`). Both were caught only by reading the current runtime.
A model change is a prompt to diff every dependent axis.

## 5. When a swordless runtime gate's ALTTPR ROM flag contradicts the fork's logic, the fork's logic wins

The Evil Barrier's ALTTPR flag (`setHammerBarrier(false)`) literally says "don't make
it hammerable in swordless" — yet the fork's (and ALTTPR's) HCT-entry logic grants
entry via `swordless && Hammer`. The ROM flag likely relies on Open-mode-castle-open
or a different barrier; the fork must match its *own* logic graph, so the runtime was
made to honor the hammer path. Ground the fix in the fork's logic, not a literal read
of an upstream flag.

## 6. Inert-addition discipline is what preserves determinism — and the corpus diff is the proof

Every swordless/trick branch was authored as a pure inert addition
(`(swordless AND X) OR ((NOT swordless) AND Y)` → `Y` when not swordless;
`(swordless OR Z)` → `Z`). Result: all 83 pre-existing corpus digests stayed
byte-identical across two `kGeneratorVersion` bumps (50→51→52). The empirical
`bump_rando_corpus.py` diff ("N digest(s) updated" = only the intended seeds) is the
determinism proof — assert it on every bump.

## 7. A spec'd algorithm isn't automatically worth shipping — measure before defaulting it on

Bug #7's per-item rewind (design D1's reshuffle-and-retry) is implemented and proven
byte-identical at budget 0, but at the design's default (10) it did **not** improve
placement quality on the hard seeds measured (TH/expert went 1→3 forward-fills — it
churns RNG without reducing the forward-fill safety valve). Shipped **gated off** to
avoid churning the corpus for no benefit. The bar for defaulting a search heuristic
on is a measured win, not "the spec prescribes it."

## 8. Fresh-eyes audits check the logic graph; playtest checks the runtime — you need both

The two-audits-per-surface cadence held (each found real LOW issues), but the swordless
audit's thorough "missed-sword-gate" search cleared the *logic* class while the Evil
Barrier — a sprite-runtime gate — slipped through, because audits read predicates and
the barrier isn't one. Treat the audit and the playtest as covering disjoint surfaces.
