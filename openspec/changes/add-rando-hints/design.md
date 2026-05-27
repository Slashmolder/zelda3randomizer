## Context

ALTTPR's hint generation lives in `app/Services/HintService.php` (177 lines, verified) and the hint text body in `app/Text.php` (1110 lines). Combined ≈ 1287 lines.

Phase A roadmap (`docs/randomizer.md:294`) names four hint sources: Sahasrahla telepathic, storyteller, bookshelf, Murahdahla. Triforce Hunt is "almost unplayable without them" per the source doc — Murahdahla in particular surfaces Triforce-piece locations grouped by sphere.

The chunking critique flagged the capability-shape decision as worth resolving in design.md. This change records the decision.

## Goals / Non-Goals

**Goals**:
- New `randomizer-hints` capability that owns hint generation + per-NPC dispatch + spoiler integration.
- 4 hint sources implemented: Sahasrahla telepathic, storyteller, bookshelf, Murahdahla.
- Hints settings axis (`off | sahasrahla | full`) with goal-aware default.
- Deterministic hint generation: same `(share_string, generator_version)` → byte-identical hint set.
- JSON + text spoiler integration with `hints` section.

**Non-Goals**:
- Inverted-specific hints (follow-on after #4a if any new NPC sites emerge).
- Murahdahla in non-Triforce-Hunt goals (the source NPC exists but its hint role is Triforce-Hunt-specific; empty entry for other goals).
- Cosmetic / customizer hint customization (Phase D).
- Voice-acted / animated hint delivery (out of scope).

## Decisions

### D1: New `randomizer-hints` capability vs. extending existing capabilities

**Options**:
- (a) **New capability `randomizer-hints`**. Peer to `randomizer-core` / `randomizer-placement` / `randomizer-shuffles` / etc. Clean boundary; hint regressions don't bleed into other capabilities.
- (b) **Extend `randomizer-placement`**: hint NPCs are dispatch sites; their dialogue ID resolution is a placement concern. Lower-coupling argument: no new capability surface.
- (c) **Extend `randomizer-core`**: hints are spoiler-integrated and generation-time; spoiler emission is already in `randomizer-core`.

**Decision**: **(a) new `randomizer-hints` capability.** Reasoning:
- Hint generation is a separable subsystem (the upstream `HintService.php` is a self-contained service class).
- The integration surface (dialogue ID injection, text-engine hooks) is distinct from placement (dispatch returns items, not text) and from spoiler (spoiler emits hints, doesn't generate them).
- Future Phase C/D hint refinements (e.g., voice acted, animated) localize to this capability.
- Cost: one more spec file. Benefit: clean ownership.

Spec deltas in this change:
- `randomizer-hints/spec.md` — generation pipeline + per-source-NPC contracts (NEW).
- `randomizer-placement/spec.md` — hint-NPC dispatch routing (ADDED requirement).
- `randomizer-core/spec.md` — hints settings axis + spoiler section (ADDED requirements).

### D2: Dialogue-ID range carve-out

ALTTPR injects hint text by replacing existing vanilla dialogue IDs with per-slot hint strings. zelda3's text engine in `src/messaging.c` has a fixed dialogue table.

**Decision**: carve out a "dynamic" dialogue ID range — say IDs `0x300..0x3FF` (256 entries) — that the runtime treats as a slot-specific lookup. When the text engine encounters a dialogue ID in the dynamic range, it consults `g_rando_hint_dialogue_table` instead of the static dialogue blob.

The exact range is decided at apply-time by grepping `src/messaging.c` for the highest currently-used dialogue ID and carving an unused band above it. Recorded in `audit.md §"Hint dialogue ID range"`.

### D3: Hint-text storage

**Options**:
- (a) **Static dictionary** in a new C file or codegen-generated file. Predictable; in-binary; no runtime allocation.
- (b) **Heap-allocated per-slot table**. Slot-loaded; uses heap.

**Decision**: **(b) heap-allocated per-slot table.** Hint strings are per-seed and computed at generation time; storing in heap is the natural shape. The table is freed when the slot unloads.

Per-slot hint table size estimate: 4 sources × max 20 hints × ~100 bytes/hint ≈ 8KB per slot. Negligible.

### D4: Goal-aware hints default

Per the proposal, the default `hints=` value depends on goal:
- `goal == triforce-hunt | ganonhunt` → default `hints=full` (Murahdahla is critical).
- All other goals → default `hints=sahasrahla` (Sahasrahla + bookshelves enabled; Murahdahla empty; storyteller off).

**Decision**: implement the goal-aware default in `SetDefaults` (same site as Phase A's `completionist → accessibility=locations` auto-set). Documented in spec scenarios.

### D5: Murahdahla in non-Triforce-Hunt goals

ALTTPR shows Murahdahla in dark world; the NPC is always-present. The hint role is Triforce-specific.

**Decision**: when `goal != triforce-hunt | ganonhunt`, Murahdahla shows a generic "no progress to report" hint OR remains silent (sprite present, dialogue trigger is vanilla). The spec leaves this as design.md-soft; apply-time can pick whichever feels right after playtest.

### D6: Hint determinism RNG

Hint generation runs against the placement table + sphere data. Each hint source draws from a subset (e.g., Murahdahla draws from Triforce-piece locations grouped by sphere). The order in which hints are selected SHALL be deterministic.

**Decision**: use a `Rng_NextU32`-derived sub-RNG seeded from `(seed_u64 XOR 0xH1_HINTS)` (with `H1_HINTS` a magic constant) for hint generation. Distinct from the placement RNG so changing placement doesn't shift hint text and vice versa.

## Risks / Trade-offs

| Risk | Mitigation |
|---|---|
| Hint text format divergence from ALTTPR convention | Per-NPC corpus test (hints_corpus); CI catches drift. Acknowledge per `docs/randomizer_phase_b_risks.md` R6. |
| Dynamic dialogue ID range collides with future vanilla content | Carve a high range (0x300+); document in audit.md; verify at apply-time. |
| Hint text body in `Text.php` (1110 lines) is hard to translate | Translate incrementally; ship Sahasrahla first, then storyteller, then bookshelves, then Murahdahla. Per-source PR. |
| Triforce Hunt hints leak placement | They're supposed to — Murahdahla is the "where are the pieces" hint NPC. Race mode users have a separate concern (race-mode reveal handles spoiler suppression). |

## Migration Plan

No user-data migration. Hints default `off` for existing slots (Phase B default for non-Triforce-Hunt) or `full` (Triforce Hunt). Users on existing slots without hints just have empty `g_rando_hint_dialogue_table` on load; NPCs play vanilla text.

## Open Questions

1. Sahasrahla telepathic tile count: how many distinct tiles, and does each get a unique hint or share a pool? Apply-time grep against ALTTPR.
2. Bookshelf hint vs. book-of-mudora interaction collision — bookshelves in dungeons may have non-hint vanilla behavior; verify which interactions are hint-bearing.
3. Storyteller NPC location — which shrines have hint NPCs? Apply-time grep.
4. Per-hint length cap — the text engine has a buffer limit per dialogue ID. Verify the limit and constrain hint generation to fit.
