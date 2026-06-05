# OpenSpec changes — entry-point index

This directory holds every active and pending OpenSpec change for the zelda3 randomizer. Use this index to navigate.

## Status overview

| Phase | # | Change | Scope | Authoring | kGen bump | Effort |
|---|---|---|---|---|---|---|
| **A** | — | [`add-randomizer-support`](archive/2026-05-29-add-randomizer-support/) | Foundation, RNG, share string, predicate VM, codegen, audit, logic graph (Open + Standard), assumed-fill placement, prize/medallion shuffles, sphere computation, goal-completability, JSON+text spoilers, sidecar save format, §6 grant-site dispatch (43 sites), snapshot tail TLV. | **Archived 2026-05-29** (119/139; the 20 deferrals are tracked in the Phase B/C/D changes below) | n/a (baseline) | ✅ shipped + archived → `openspec/specs/randomizer-*` (6 capabilities) |
| **B** | 1 | [`add-rando-confirmation-icons`](archive/2026-06-04-add-rando-confirmation-icons/) | Visible direct-grant icon ancilla (extends §7.6 audio-only) | ✅ **Archived 2026-06-04** | No | ✅ shipped + archived |
| **B** | 2 | [`add-rando-trackers`](add-rando-trackers/) | In-game item + location overlays; checked-bitmap r/w | Full (4/45) | No | 1-2w |
| **B** | 3 | [`add-rando-race-mode-reveal`](add-rando-race-mode-reveal/) | Spoiler suppression + `RevealSpoiler` action with SHA-256 stamp | Full (29/38) — **archive-blocker: delta commits 2 unbuilt `randomizer-ui` SHALLs** (in-binary "Race-mode reveal UI" + settings-screen warning, deferred §4.1-4.5/§5.2); CLI `--reveal-spoiler` + suppression built; carve out deferred UI before archive + playtest | No | 3-5d |
| **B** | 3b | [`add-rando-beatable-accessibility`](archive/2026-06-03-add-rando-beatable-accessibility/) | ALTTPR three-way accessibility (`Accessibility_SeedAcceptable`); "beatable only" 3rd tier | ✅ **Archived 2026-06-03** (owner playtest-confirmed) | **Yes** (45→46) | ✅ shipped + archived |
| **B** | 3c | [`add-rando-boss-heart-pool-toggle`](archive/2026-06-03-add-rando-boss-heart-pool-toggle/) | UI toggle for `region_boss_hearts_in_pool` (value inverted vs name); comment fix | ✅ **Archived 2026-06-03** (owner playtest-confirmed) | No | ✅ shipped + archived |
| **B** | 4a | [`add-rando-inverted-world-state`](archive/2026-06-03-add-rando-inverted-world-state/) | Inverted region graph (2977 PHP lines) via static world-state-keyed graph (RegionRemap retired) + Bug #12 starting-inv | ✅ **Archived 2026-06-03** (owner playtest-confirmed for implemented scope; Ganon-under-HC relocation tracked separately in #4a-ii) | **Yes** | ✅ shipped + archived |
| **B** | 4a-i | [`add-rando-inverted-dark-chapel-spawn`](archive/2026-06-03-add-rando-inverted-dark-chapel-spawn/) | Inverted spawn-select Dark Chapel / Dark Mountain respawn-world fix (follow-on to #4a; depends on `add-rando-inverted-world-state`) | ✅ **Archived 2026-06-03** (playtest-confirmed) | No | ✅ shipped + archived |
| **B** | 4a-ii | [`add-rando-inverted-ganon-relocation`](add-rando-inverted-ganon-relocation/) | Inverted Ganon-under-HC relocation (follow-on to #4a; depends on `add-rando-inverted-world-state`; high-risk gfx spike) | Full (0/19) | **Yes** | 2-3w |
| **B** | 4b | [`add-rando-retro-world-state`](archive/2026-06-04-add-rando-retro-world-state/) | Retro shop dispatch + 3 Retro flag pin (rupeeBow/takeAnys/wildKeys); genericKeys carved to #4b-i | ✅ **Archived 2026-06-04** (owner playtest-confirmed; full goal clear pending as a final check) | **Yes** | ✅ shipped + archived |
| **B** | 4b-i | [`add-rando-retro-generic-keys`](add-rando-retro-generic-keys/) | Retro `rom.genericKeys` — one shared small-key pool (any key opens any door): keys → GenericKey + per-dungeon key-door **logic rewrite** + shared-counter runtime (follow-on to #4b; the deferred 4th Retro flag) | Scaffolded (proposal+design+tasks+2 spec deltas; 0 impl) | **Yes** | playtest-gated |
| **B** | 4c | [`add-rando-retro-takeany`](archive/2026-05-29-add-rando-retro-takeany/) | 31 TakeAny shop dispatch + RNG-driven activation (promoted from 4b's design §5 split) | ✅ **Archived 2026-05-29** | **Yes** | ✅ shipped + archived |
| **B** | 5 | [`add-rando-trick-logic-and-axes`](archive/2026-06-04-add-rando-trick-logic-and-axes/) | OP_TRICK/OP_DIFFICULTY_AT_LEAST/OP_GLITCH_LEVEL_AT_LEAST + 5 settings un-pins + Bug #7 per-item rewind + swordless mode | ✅ **Archived 2026-06-04** | **Yes** | ✅ shipped + archived |
| **B** | 6 | [`add-rando-hints`](add-rando-hints/) | New `randomizer-hints` capability: 15 telepathic tiles (ship) + Storyteller/Fortune-Teller fork NPCs (landed 2026-06-01); Murahdahla spoiler-only (needs sprite); bookshelf dropped; binary mode | Full (gen/spoiler/determinism/docs done — open: in-game NPC playtest + fresh-eyes audit) | **Yes** | 2-3w |
| **B** | 7 | [`add-rando-shuffles-and-minigames`](add-rando-shuffles-and-minigames/) | Boss + drop-pool shuffles + §6.8 minigame dispatch | Full (34/48) — drop-shuffle playable; boss-shuffle generation-only | **Yes** | 2-3w |
| **B** | 8 | [`add-rando-switch-swkbd`](add-rando-switch-swkbd/) | libnx swkbd wrapper for Switch text input | ⏸ **PARKED** (0/31) — Switch env unavailable; see "Switch work — parked" below | No | parked |
| **B** | PC | [`add-rando-native-settings-window`](archive/2026-06-03-add-rando-native-settings-window/) | Dear ImGui second OS window owns the rando settings UI on PC; in-game screen compile-guarded out behind `Z3R_NATIVE_SETTINGS_WINDOW` | ✅ **Archived 2026-06-03** (owner playtest-confirmed; created capability `randomizer-native-window`) | No | ✅ shipped + archived |
| **B** | 9 | [`add-rando-item-progression-and-swap`](add-rando-item-progression-and-swap/) | Progressive boomerang/magic, bow never-downgrade + item-menu Press-A swap (flute/shovel/boomerang/bow), never-downgrade clamp (sword/shield/gloves/mail; arrow-filler exempt), ownership persistence (`@73`/`@74`); + Magic Bat (missable) & Flute Spot (dupe) location-guard fixes | As-built (impl `b73e9bb` on main, docs `7e54117`; spec reconciliation, all impl tasks [x]) — **archive-gate: owner playtest §5** | No | ✅ built — playtest-pending |
| **C** | C1 | [`add-rando-entrance-shuffle`](add-rando-entrance-shuffle/) | Multi-mode composable entrance shuffle (Stages 1-3 built; Stage 4/Insanity deferred); per-seed edge overlay (retires RegionRemap) | Full (46/50) — **archive-blocker: delta commits an unbuilt Insanity mode SHALL+scenario** (`specs/randomizer-shuffles/spec.md:9,21`; Stage 4 + Skull Woods + Link's House deferred); carve Insanity out before archive. Built modes owner-playtested. Also reconcile canonical `randomizer-logic` "Per-seed RegionRemap overlay for entrance shuffle" (retired) at archive-time | **Yes** | 4-8w |
| **D** | D1 | [`add-rando-cosmetic-shuffles`](archive/2026-06-02-add-rando-cosmetic-shuffles/) | Palette (4 MVP modes) / sprite (zspr folder pick) / music (song remap) shuffles; `cosmetic_seed` is **client config**, not a slot field — no save-format change | ✅ **Archived 2026-06-02** (owner playtest-confirmed) | No | ✅ shipped + archived |
| **D** | D2 | [`add-rando-customizer-mode`](add-rando-customizer-mode/) | Manual per-location placement + custom pool composition | Stub | No (per-seed; affects `customizer_seed`) | 2-3w |
| **D** | D3 | [`add-rando-major-glitch`](add-rando-major-glitch/) | Extends Phase B #5's `OP_GLITCH_LEVEL_AT_LEAST` to HybridMG + NoLogic | Stub | **Yes** | 2-3w |
| **D** | D4 | [`add-rando-auto-tracker`](add-rando-auto-tracker/) | Local TCP server for external tracker clients | Stub | No | 2-3w |

Total: 12 active changes + 14 archived. The 12 rando-prefixed archived changes are `add-rando-retro-world-state`, `add-rando-trick-logic-and-axes`, `add-randomizer-support`, `add-rando-retro-takeany`, `add-rando-cosmetic-shuffles`, `add-rando-inverted-dark-chapel-spawn`, `add-rando-beatable-accessibility`, `add-rando-boss-heart-pool-toggle`, `add-rando-native-settings-window`, `add-rando-inverted-world-state`, `add-rando-confirmation-icons`, and `add-rando-fairy-chest-model`; the 2 native-UI archived changes are `add-native-debug-cheats` and `add-native-game-config-ui`. Run `openspec validate --changes` to confirm all validate.

## Switch work — PARKED (owner, 2026-06-02)

The owner has no Switch development environment and has punted **all** Switch work until one is available. This is a standing policy, not a per-change decision:

- **`add-rando-switch-swkbd`** (the only pure-Switch change) is **parked** — authored but not being worked, and not a blocker for anything else.
- **Switch build-verification + dev-unit-bench tasks** in other changes (e.g. `add-rando-trackers` §4.5/§8.3/§10.3) are **deferred**. These are release-cut manual gates per the archived `add-randomizer-support / tasks.md §12.3a` (dev-unit bench) and `§12.3b` (build verification) — they were never per-change CI blockers, so deferring them does not block any change from archiving.
- **"Switch budget" thresholds** cited in budget-bench tasks (`add-rando-hints` §10.5.1, `add-rando-shuffles-and-minigames` §9.5.1, `add-rando-trick-logic-and-axes` §12.5.1, `add-rando-trackers` §6.4) are covered by this policy: run the **desktop** bench as normal; the Switch-budget number is verified later when a Switch env exists.
- **Editing `src/platform/switch/Makefile`** to register a new generated header (codegen-wiring tasks) is *not* parked — it needs no Switch and keeps the build wiring honest; it just isn't compile-verified on Switch until the env exists.
- **Switch is not broken** by the PC native-settings-window move: the in-game settings screen + alphabet picker were compiled out *only on PC*; Switch retains them as the text-input path. The Switch build remains *unverified* (can't test), but is designed to exclude PC-only ImGui/`rando_window` paths via the `Z3R_NATIVE_SETTINGS_WINDOW` guard.

When a Switch env returns, un-park `add-rando-switch-swkbd` and clear the `⏸ DEFERRED` markers; nothing else needs to change.

## Implementation order

Recommended ordering:

1. ✅ **Phase A archived** (2026-05-29) — baseline specs live in `openspec/specs/randomizer-*`. Phase B/C/D changes now modify those specs.
2. **Phase B warm-up**: #1 confirmation-icons → #2 trackers → #3 race-mode reveal.
3. **Phase B world-states**: #4b Retro (small) → #4a Inverted (large).
4. **Phase B logic + UX**: #5 trick logic → #6 hints → #7 shuffles+minigames. (#8 swkbd ⏸ parked — Switch env unavailable.)
5. **Phase B exit**: each change archives independently once its tasks + fresh-eyes audit complete.
6. **Phase C**: C1 entrance shuffle (requires #4a archived).
7. **Phase D**: D1-D4 (D3 requires #5 archived; D1/D2/D4 are independent).

## Dependencies graph

```
Phase A archive ✅ (2026-05-29)
       │
       ├──► #1, #2, #3 (warm-up, parallel-safe)
       ├──► #4b Retro
       ├──► #4a Inverted ──┬───────────────────────► C1 entrance shuffle
       │                   ├──► add-rando-inverted-dark-chapel-spawn ✅ (archived 2026-06-03)
       │                   └──► add-rando-inverted-ganon-relocation (follow-on)
       ├──► #5 trick logic ─────────────────────────► D3 major glitch
       ├──► #6 hints
       ├──► #7 shuffles + minigames
       ├──► #8 switch swkbd  [PARKED — Switch env unavailable]
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
- `openspec list --specs` — view the spec baseline (6 `randomizer-*` capabilities live as of the Phase A archive, 2026-05-29).

## When you're ready to start a change

1. Pick the change folder from the table above.
2. Read its `README.md` for the entry-point index, then `proposal.md`, then `design.md` (if present), then spec deltas, then `tasks.md`.
3. Run `openspec apply <change-name>` (or `/openspec-apply`) to walk through tasks.
4. For stub-status changes (Phase C/D + new Phase B work that hasn't been promoted), run `/openspec-explore <change-name>` first to flesh out tasks/design.

## Cross-cutting conventions

Every kGen-bump change SHALL:
- Bump `kGeneratorVersion` in `src/rando/rando.h`.
- Regenerate the regression corpus via `assets/scripts/bump_rando_corpus.py`.
- Verify default-settings digests remain byte-identical to the pre-change baseline (the canonical settings byte-layout is **append-only**; never change byte positions, widths, or enum values for existing fields — Phase A pre-declared the full enum space, so Phase B+ only *un-pins* user input to existing values).
- If `location_registry.yaml` / `item_registry.yaml` grew, verify existing IDs are unchanged (append-only registry).
- Backward-load test: a slot written by `generator_version = N` SHALL load on an `N+1` binary with a one-time informational warning (per `randomizer-save / Embedded placement table — upgrade safety`); no regeneration required.
- Pass audit-guard (`assets/scripts/check_audit_guard.py`), determinism (`check_determinism.py`), and codegen-wiring (`check_codegen_wiring.py`) checks.

Per `CLAUDE.md` "Fresh-eyes audit cadence": every Phase B change SHALL schedule a fresh-eyes audit pass after the main authoring lands. Memory `[[cluster-audit-cadence]]` documents the pattern.
