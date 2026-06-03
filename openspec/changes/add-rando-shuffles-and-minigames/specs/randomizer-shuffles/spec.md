## MODIFIED Requirements

### Requirement: Boss shuffle (Phase B)

The boss-shuffle generator SHALL randomize the boss assigned to each dungeon's
boss room from a 10-boss pool while keeping the dungeon's reward (crystal or
pendant from prize shuffle) tied to the dungeon, not to the boss. Bosses required
by the active goal SHALL remain at their canonical slots. The assignment SHALL be
deterministic from `(settings, seed)` and SHALL be emitted in the spoiler under
`boss_assignments`.

The 10-boss permutation runs at generation time and is orthogonal to item
placement (it never changes `placement_digest` / `sphere_digest`). Goal-required
bosses pinned at their canonical slots:
- Agahnim 1 (Hyrule Castle Tower)
- Agahnim 2 (Ganon's Tower top)
- Ganon (Pyramid) — out of the boss pool entirely.

**Runtime substitution is DEFERRED (generation-only).** A pure sprite-type swap
renders incorrectly: a dungeon boss room loads the *vanilla* boss's graphics
sheet (so a substituted boss draws with the wrong tiles), and multi-entry vanilla
bosses (e.g. Eastern Palace is six Armos Knight sprite entries) each remap into N
copies of the substituted boss. Correct runtime substitution requires per-boss
sprite-GFX loading and spawn-count handling, which is deferred to a follow-up.
Until then `Rando_ActivateSidecarSlot` SHALL NOT install the boss assignment
(bosses render vanilla), and the `boss_shuffle` toggle SHALL be disabled in the
PC native settings window. (A separate, also-deferred gap: the logic graph gates
each dungeon's `"<Dungeon> - Boss"` location on its vanilla boss-kill items, so a
per-seed boss-kill predicate override is also required before boss shuffle is
beatability-safe — design.md D6.)

#### Scenario: Boss assignment is generated and deterministic
- **WHEN** `boss_shuffle == true` for a given seed
- **THEN** the per-dungeon boss assignment is computed deterministically from
  `(settings, seed)`, the goal-required bosses stay pinned, the 10 shuffleable
  dungeons hold a permutation of the 10-boss pool, and the spoiler lists the
  assignment under `boss_assignments`

#### Scenario: Boss shuffle does not perturb item placement
- **WHEN** the same seed is generated with `boss_shuffle` on versus off
- **THEN** the `placement_digest` and `sphere_digest` are byte-identical

#### Scenario: Boss runtime substitution is deferred
- **WHEN** a `boss_shuffle == true` slot is loaded and played
- **THEN** every dungeon's boss renders as its vanilla boss (the runtime
  substitution is not installed), pending the per-boss GFX-loading follow-up

#### Scenario: Disabled boss shuffle preserves vanilla bosses
- **WHEN** `boss_shuffle == false`
- **THEN** every dungeon's boss is its vanilla boss; the spoiler `boss_assignments`
  section is omitted

### Requirement: Drop-pool shuffle (Phase B)

When enabled, the drop-pool shuffle SHALL randomize the enemy drop-prize table
(the 56-entry `kPrizeItems` table — 7 packs × 8 slots) as a deterministic
permutation keyed on `(settings, seed)`, and SHALL enforce a heart-drop floor so
weak early-game enemies still drop hearts. It SHALL be installed at slot load
(`Rando_ActivateSidecarSlot`) and consumed at the sprite-drop site
(`ForcePrizeDrop`); drop sprites use the always-loaded common prize graphics, so
shuffled drops render correctly. It is orthogonal to item placement (never changes
`placement_digest` / `sphere_digest`).

**Heart-drop floor**: pack 0 — the vanilla heart-heavy pack that weak overworld
enemies draw from, hence reachable from sphere 0 — SHALL keep at least one heart
entry after the shuffle. A violating draw is re-rolled on the same RNG stream
within a bounded budget; if the budget is exhausted the table falls back to the
vanilla identity and the spoiler records a `drop_heart_floor_fallback` warning.
Because the enemy→pack binding is static (per sprite type, not sphere-indexed),
the floor is enforced structurally on pack 0 rather than against live sphere data
— the faithful realization, in this fork's drop model, of "a tier reachable in
spheres 0-2 keeps a heart".

#### Scenario: Heart drop survives early game
- **WHEN** drop-pool shuffle is enabled
- **THEN** pack 0 of the shuffled drop table contains at least one heart entry

#### Scenario: Drop table is deterministic
- **WHEN** the same seed and drop-pool-shuffle setting are used
- **THEN** the generated drop table is byte-identical across generations and
  across host platforms

#### Scenario: Drop shuffle does not perturb item placement
- **WHEN** the same seed is generated with `drop_shuffle` on versus off
- **THEN** the `placement_digest` and `sphere_digest` are byte-identical; with the
  shuffle on, the spoiler `drop_tables` section is populated

#### Scenario: Disabled drop-pool preserves vanilla drops
- **WHEN** `drop_pool_shuffle == false`
- **THEN** the drop table is the vanilla identity; the spoiler `drop_tables`
  section is omitted
