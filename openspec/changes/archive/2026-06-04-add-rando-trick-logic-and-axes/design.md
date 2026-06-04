## Context

Phase A reserved 3 op-codes (`OP_TRICK=15`, `OP_DIFFICULTY_AT_LEAST=16`, `OP_GLITCH_LEVEL_AT_LEAST=17`) and pinned 5 user-input settings axes to subsets of their enum spaces:
- `tricks` pinned to 0 (`rando_settings.h:86`)
- `logic` pinned to NoGlitches (`rando_settings.h:88`)
- `mode_weapons` pinned to `randomized`/`assured` only (excludes `vanilla=2`, `swordless=3`)
- `accessibility` pinned to `items`/`locations` only (excludes `none=2`)
- `region_pyramid_bow_upgrade` pinned to `true` (BowAndSilverArrows)

Phase A1 audit Bug #7 (per-item bounded rewind in `Place_AssumedFill`) is still open per `audit_phase_a1.md:79-80`. The spec at `randomizer-core/spec.md:344` says "the placer rewinds the last N placements" but the implementation does whole-attempt retry with `kAssumedFillMaxAttempts=8`.

This change unifies 5 settings-axis un-pins + 3 op-code handler implementations + Bug #7 implementation alignment. Pre-priming work in `assets/rando/op_registry.yaml` `tricks:` section (#29) already scaffolded the initial 8-trick bit table.

## Goals / Non-Goals

**Goals**:
- 3 op-code handlers implemented in `src/rando/rando_logic.c`.
- 5 settings axes un-pinned with full CSV + settings-screen support.
- Per-item bounded rewind in `Place_AssumedFill` matches the spec's drafted SHALL.
- Trick predicates authored across Phase A's existing logic graph (Open + Standard); Inverted trick predicates are an apply-time follow-on if #4a archived first.
- Default-settings seeds (tricks=0, logic=NoGlitches, etc.) remain byte-identical in `placement_digest_hex`.

**Non-Goals**:
- `vanilla` mode.weapons value (still reserved post-Phase-B per Phase A spec).
- Glitch levels 3 + 4 (HybridMG, NoLogic — handled in Phase D `add-rando-major-glitch`).
- Inverted-specific trick predicates (follow-on after `add-rando-inverted-world-state` archives).
- Widening `settings.tricks` beyond uint8 (cap at 8 tricks in Phase B; Phase C may widen).

## Decisions

### D1: Per-item rewind algorithm

The spec at `randomizer-core/spec.md:344` says "the placer rewinds the last N placements and retries." Implementation:

```c
while (current_item != NULL) {
  if (try_place(current_item) succeeds) {
    advance to next item;
    continue;
  }
  // No valid location. Per-item rewind.
  if (per_item_rewind_budget > 0) {
    rewind last N placements (N = kPerItemRewindBudget, default 10);
    recompute simulated inventory;
    per_item_rewind_budget--;
    retry current_item;
    continue;
  }
  // Per-item rewind exhausted. Escalate.
  fail current attempt; bump kAssumedFillMaxAttempts counter; restart;
}
```

**Per-item rewind budget**: default `kPerItemRewindBudget = 10`. Configurable via INI `[Randomizer] per_item_rewind_budget`. The per-attempt rewind budget resets at the start of each whole-attempt iteration.

**Decision rationale**: N=10 is a guess; prototype during apply-time on a known-hard seed (Triforce Hunt + hard pool) and tune. Risk: an ill-tuned rewind budget regresses generation time (rewind-tuning risk).

### D2: Trick bit-position assignments

Per the priming work in `assets/rando/op_registry.yaml` `tricks:` section, the initial 8 tricks are:

| Bit | Trick id | Source priority |
|---:|---|---|
| 0 | `boots-clip` | Community-canonical |
| 1 | `fake-flippers` | Community-canonical |
| 2 | `bunny-revival` | Community-canonical |
| 3 | `dark-room-nav` | Community-canonical |
| 4 | `bomb-jump` | Community-canonical |
| 5 | `pearl-bypass` | Mid-tier |
| 6 | `hookshot-clip` | Mid-tier |
| 7 | `lobotomy` | Placeholder; replace if higher-priority trick emerges |

Phase B caps at 8 tricks per the uint8 settings field. Phase C may widen to uint16/uint64 if community demands more.

### D3: CSV syntax for `tricks=` bitmask

Decision: comma-separated trick names that resolve against the `tricks:` table in `op_registry.yaml`:

```
--settings=tricks=boots-clip,fake-flippers,bunny-revival
```

The CSV parser computes the bitmask by OR'ing the bit positions for each named trick. Unknown trick names are an error.

### D4: Glitch-level CSV syntax

Decision: snake_case enum values:

```
--settings=logic=overworld_glitches
--settings=logic=major_glitches
```

`no_glitches` (Phase A default) and the Phase D values (`hybrid_major_glitches`, `no_logic`) follow the same convention.

### D5: mode_weapons=swordless predicate impact

Per `Randomizer.php:230-240`, swordless changes "can damage Ganon" predicate semantics: silvers + hammer/fire-rod start required (in lieu of sword). The Phase B implementation:

- `mode_weapons == swordless` triggers a `can_place` predicate addition on the `LOC_Pyramid_Fairy_Sword` slot to reject all sword items.
- The `CanDamageGanon` macro in `macros.yaml` adds a swordless branch: when `mode_weapons == swordless`, requires `(HAS_SilverArrowUpgrade AND (HAS_Hammer OR HAS_FireRod))` instead of `HAS_AnySword`.

### D6: accessibility=none semantics

`accessibility == none` disables the goal-completability strict-refusal at `main.c:482`. The generator produces seeds where un-reachable junk is permitted; un-completable seeds also succeed (no winnable-state guarantee).

Spoiler emits a `fallback_warnings` entry: `{"code": "accessibility_none_seed", "detail": "Seed generated with accessibility=none; reachability is not enforced."}`.

### D7: pyramid_bow_upgrade=false behavior

Per `Randomizer.php:150-152`, this is the BowAndArrows variant (no silvers from Pyramid Fairy). The implementation:

- Pyramid Fairy bow trade-in at `sprite_main.c:1273` grants `ProgressiveBow` (advancing to next bow level) but does NOT advance to SilverArrows when `region_pyramid_bow_upgrade == false`.
- The `LOC_Pyramid_Fairy_Bow` slot may still place a `SilverArrowUpgrade` separately; this is the "arrows variant" where silvers must be placed elsewhere in the world.

### D8: Trick predicate authoring scope

To bound scope creep: Phase B trick predicate authoring is bounded to the initial 8 tricks. ALTTPR has more trick definitions; the long-tail is Phase C work.

Authoring discipline per `audit.md §0.10`: each trick's per-location applicability is sourced from ALTTPR PHP grep; per-location SOURCE citations recorded.

## Risks / Trade-offs

| Risk | Mitigation |
|---|---|
| Per-item rewind changes default-settings digests | Default settings should NOT trigger rewind (assumed-fill on Open + tricks=0 + NoGlitches always succeeds without rewind). Verify on full corpus before kGen-bump. |
| Trick predicate authoring incomplete | Phase B ships with 8-trick set + per-location applicability for top dungeons; long-tail in Phase C. Document the cap. |
| Swordless predicate edge cases | The `CanDamageGanon` macro is the load-bearing predicate; corpus seeds with `mode_weapons=swordless` exercise it. |
| Bug #7 rewind exhausts budget on hard seeds | Forward-fill fallback (existing Phase A mechanism) is the safety net; spoiler warning surfaces it. |

## Migration Plan

No user-data migration required. Default-settings slots remain valid because:
- Default `tricks=0`, `logic=NoGlitches`, etc. produce byte-identical digests.
- Existing slots have these values in their stored settings.

Users opt into Phase B values via CSV (CLI) or settings-screen toggles.

## Open Questions

1. Per-item rewind budget tuning (N=10 is a guess) — prototype on a known-hard seed.
2. Initial 8 tricks vs. specific community preferences — confirm via community poll OR ALTTPR-default tricks list at apply-time.
3. Trick predicate density per Inverted dungeons — if #4a Inverted ships first, the Inverted YAML adds trick gating; if this change ships first, Inverted YAML inherits a clean baseline.
