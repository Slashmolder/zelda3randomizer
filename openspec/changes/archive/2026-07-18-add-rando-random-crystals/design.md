# Design: Random crystal counts

## Context

Grounded facts (research pass 2026-07-17, all verified in-worktree):

- `crystals_ganon`/`crystals_tower` are full canonical bytes [2]/[3], hard
  0..7 in `Settings_Validate` (`rando_settings.c:646`) which
  `Settings_CanonicalDeserialize` calls — a sentinel is REFUSED today on
  every share-paste/slot-load path.
- **No predicate-VM op reads either count** (op_registry + rando_logic
  grep-clean). The gates are C-level: generation reads ONLY
  `crystals_ganon`, in `Goal_IsCompletable`'s `kGoal_Ganon` /
  `kGoal_FastGanon` arms (`rando_placement.c:3179/3202`);
  **`crystals_tower` has zero generation readers** (the GT-entry edge
  comment at `rando_placement.c:3197` is aspirational).
- Runtime reads the live `g_rando_active_settings` at five sites:
  `Rando_HasRequiredTowerCrystals` (rando.c:5327, GT door via
  `AncillaAdd_GTCutscene`), `Rando_HasRequiredGanonCrystals` (rando.c:5337,
  Ganon damage via `Sprite_GiveDamage` + msg debounce),
  `Rando_ApplyLoadedSaveRuntimeSettings` (rando.c:5345, zero-crystal GT
  pre-open), `rewrite_ganon_crystal_warning` (rando_dialogue.c:263, msg
  0x16F "You need %u crystals to harm me"), and `at_append_settings`
  (auto_tracker.c:358).
- Template: keyrings' requested/effective split — requested value never
  normalized (serialize writes it verbatim; hash reflects it), seed-taking
  `KeyRings_Resolve(settings, seed, out)` with an FNV-derived salt, baked at
  generation, runtime re-derivation for consistency. The seed reaches slot
  activation via `Share_Decode(g_rando_active_share_string)`
  (`Rando_RebuildKeyItemOwnership`, rando.c:3926-3928).
- UI surfaces: native `SliderInt(0,7)` ×2 (rando_window.cpp:749-753);
  in-game rows print `"%u OF 7"` and cycle-wrap at 7
  (select_file.c:2746-2957); spoiler prints both raw
  (rando_spoiler.c:543-544); auto-tracker prints both raw.

## Goals / Non-Goals

**Goals**
- `random` requested value per axis, independently; preserved in
  canonical/hash/share; resolved 0..7 deterministically from the seed,
  identically at generation and every slot activation.
- Ganon's msg-0x16F rewrite becomes the in-world reveal of the rolled
  ganon count; tracker + spoiler expose resolved values.
- Fixed-count seeds byte-identical everywhere.

**Non-Goals**
- No in-world GT tower-count reveal beyond the tracker (the GT door stays
  a silent seal when unmet, as today) — candidate follow-up, not v1.
- No weighting control (uniform 0..7, upstream-faithful).
- No new sidecar fields.

## Decisions

### D1. Sentinel = 8 in the existing bytes

`enum { kCrystalsRandom = 8 }` (rando_settings.h). Bytes [2]/[3] are full
bytes — no packing changes, no length change, default hash untouched.
Validate/deserialize widen to `> kCrystalsRandom → reject`. CSV: `random`
keyword (hard error stays for 9+ / junk). Rejected alternative — a separate
"random flags" bit pair elsewhere: needless second source of truth.

### D2. One seed-taking resolver, requested never normalized

`Crystals_Resolve(const RandoSettings *s, uint64 seed_u64, uint8 *ganon,
uint8 *tower)` in rando_placement.c (declared rando_placement.h, next to
`KeyRings_Resolve`). Fixed axes pass through; a random axis draws
`Rng_NextRange(&rng, 8)` from its OWN stream:
`seed ^ 0x43727973746C5271ull /* "CrystlRq" */ ^ ((uint64)'G' << 56)` for
ganon, `^ ((uint64)'T' << 56)` for tower — per-axis independence regardless
of whether the other axis is random (the ShopPrice per-key-mix precedent).
The resolve stays OUT of `apply_derived_rules` (keyrings note verbatim):
canonical/hash/share carry 8.

### D3. Generation: resolve at the completability gate — BASE seed, never attempt seed

`Goal_IsCompletable(settings, table)` grows a `uint64 seed_u64` parameter;
its two crystal arms call `Crystals_Resolve(settings, seed_u64, ...)`.

**The seed MUST be the BASE `seed_u64`, never `placement_attempt_seed`**
(plan-review HIGH). The two most dangerous call sites are INSIDE
`Place_AssumedFill`'s retry loop (`rando_placement.c:1935/1955`), where
`attempt_seed` is the nearest in-scope seed — passing it compiles clean,
stays corpus-green (corpus seeds are attempt-0, where
`placement_attempt_seed(seed,0)==seed`), and silently certifies retry
attempts against a different resolved count than the outer acceptance /
spoiler / runtime. The file's own precedent applies verbatim: key rings and
the boss assignment are resolved from the BASE seed with explicit comments
(`rando_placement.c:1765-1785`) — the crystal calls carry the same
comment. Pinned by (a) the resolver selfcheck asserting
`Crystals_Resolve(s, base, ..)` equals the value used in a full
`Place_AssumedFill` run's accepted table for a retry-exercising fixture,
and (b) a corpus row whose seed is chosen to need `retry_attempts > 1`
(recorded in the row comment).

**Signature cascade (plan-review MED):** the seedless wrappers
`Accessibility_SeedAcceptable` (`rando_placement.c:3365`) and
`Goal_ShouldRefuse` (`:3385`) grow the same `seed_u64` parameter; their
callers (`rando_generate.c:406/441/475/499`, `main.c:1082`) all hold the
unperturbed base seed (the entrance/door/chains attempt counters are
separate parameters). `-Werror` enumerates the full set.

Rejected alternative — resolving into a settings copy at every generation
entry point: too easy for one call chain to use the wrong copy (research
risk 7). `crystals_tower` needs no generation hook at all — its consistency
is carried entirely by the shared resolver + the selfcheck vector. Note
also (plan-review LOW): the logic graph's GT-entry edges hardcode the full
7-crystal requirement (`logic_parts/33_ganons_tower.yaml:74/85` + inverted
variant) — pre-existing conservatism, deliberately untouched; do NOT wire
the resolver into those edges. Consequence: random-TOWER corpus rows are
placement-byte-identical to their fixed-tower twins and pin only the
hash/share/spoiler surfaces (their row comments say so).

### D4. Runtime: resolve once at activation, cache, and route ALL five sites

`Rando_ActivateSidecarSlot` calls `Crystals_Resolve` and stores
`g_rando_effective_crystals_ganon/_tower` — **inside the exact block where
`g_rando_active_settings_valid` is set** (`rando.c:4938-4952`, where BOTH
the canonical deserialize AND `Share_Decode` have succeeded, so "valid
settings but no seed" cannot occur; plan-review LOW). Getters
`Rando_EffectiveCrystalsGanon()/Tower()` replace the five direct reads
(gates ×2, zero-crystal pre-open, dialogue rewrite, auto-tracker).
Fail-closed default when no slot is active mirrors today's behavior (7/7).
The dialogue rewrite thus reveals the ROLLED count in-world for free.
Cache invalidation = slot activation/deactivation only.

**Sixth consumer (plan-review MED):** `Rando_CrystalGateSelfCheck`
(`rando.c:8713-8771`) mutates `g_rando_active_settings.crystals_*`
directly and asserts the gate helpers react — under the cache those
mutations become invisible and the selfcheck dies. It is REWRITTEN to
poke the cache through a test seam (a small internal setter used only by
the selfcheck), keeping its gate-reaction assertions, and gains a
sentinel-mode prong (cache == fresh `Crystals_Resolve`).

### D5. Spoiler and tracker emit requested-vs-resolved

Spoiler settings block keeps the raw requested bytes; when either axis is 8
it adds `crystals_ganon_resolved` / `crystals_tower_resolved` and
`crystals_salt_version: 1` (keyrings' `key_rings_selection_salt_version`
precedent). Auto-tracker emits the RESOLVED values in the existing fields
(external trackers need real thresholds; the requested sentinel is not
actionable there) — divergence from the spoiler is deliberate and
documented.

### D6. UI

Native window: `SliderInt(0, 8)` with format `"Random"` when the value is 8
(one widget, no layout change); tooltip notes the seed decides. In-game
(Switch-path) rows: cycle range grows to 8, `RowValueText` prints `RAND OF
7`-style token (`"RAND"`), mirroring the keyrings `"OFF*"` precedent. Both
inert at 0..7.

### D7. Validation

- `Crystals_ResolveSelfCheck`: determinism (double call), range (0..7),
  per-axis independence (random ganon with tower fixed vs both random →
  ganon draw identical), pinned vector (captured at implementation).
- Corpus rows: `crystals.ganon: random` (fast_ganon), `crystals.tower:
  random`, both random, `goal: ganon` + both random (pins the
  resolved-count completability path), and one random-axis row seeded to
  need `retry_attempts > 1` (the D3 base-seed pin). Existing 221 rows
  prove fixed-count byte-identity.
- kGeneratorVersion stays 146 — unreleased on this branch; the sentinel
  changes placement only for seeds that use it.

## Risks / Trade-offs

- **[Missed runtime read site]** a sixth crystals read added later could
  read the raw settings. → The getters are the only sanctioned access;
  a selfcheck asserts the active-slot cache matches a fresh
  `Crystals_Resolve` (the keyrings mask-mismatch-warning precedent, but
  hard-fail since there is no installed-table authority here).
- **[Goal_IsCompletable signature churn]** touches many selfcheck callers.
  → Mechanical; `-Werror` enumerates them; each caller has the seed.
- **[Spoiler consumers assume 0..7]** external tools reading the settings
  block see 8. → `_resolved` fields + salt version give them the real
  numbers; documented in docs/randomizer.md.
- **[Sentinel on old binaries]** pre-feature builds refuse byte 8 (validate
  -2) — correct forward-compat refusal, same class as new axis bits.

## Migration Plan

Single phase on `feature/rando-shopsanity` (per owner instruction to
converge the set on this branch): settings plumbing → resolver + selfcheck
→ generation choke point → runtime cache/getters → spoiler/tracker → UI →
corpus rows → docs. Every step ends buildable with selftest green.

## Open Questions

- Q1 (owner): should the GT door get an in-world tower-count reveal (a
  message when the seal refuses), or is tracker+Ganon-dialogue enough for
  v1? Planned: v1 ships without it.
- Q2 (owner): uniform 0..7 confirmed? (Upstream-faithful; no weighting.)
