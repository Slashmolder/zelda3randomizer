# randomizer-player-knowledge Specification

## Purpose
TBD - created by archiving change tracker-player-knowledge. Update Purpose after archive.
## Requirements
### Requirement: Player-knowledge invariant for tracker surfaces

Every player-facing tracker surface (native tracker windows, reachability
panel, auto-tracker export, and any future surface) SHALL only present
statements that are true under EVERY shuffled assignment consistent with the
player's in-game observations so far. Where a surface cannot compute a
knowledge-consistent statement, it SHALL degrade toward showing less
(under-report availability), never toward revealing the true assignment. This
gating SHALL be unconditional — no setting, toggle, or non-race status disables
it; the spoiler reveal flow remains the only intentional disclosure path.

#### Scenario: Sphere-0 dungeon shuffle seed shows no hidden-dungeon availability

- **WHEN** a `shuffle_dungeon_entrances` or `dungeon_chains` slot is active,
  no shuffled dungeon has been entered, and the Check Tracker is open
- **THEN** no location inside any hidden-identity dungeon is presented as
  available or reachable, global and per-region availability counts include
  none of them, and no overworld region is presented as available when its only
  logical route passes through an unentered hidden-identity dungeon

#### Scenario: Non-topology seed is pixel-identical

- **WHEN** the active slot enables no topology axis (no entrance/chains/warp/
  door shuffle)
- **THEN** the knowledge mask is empty and every tracker surface renders
  exactly as it did before this change

### Requirement: Per-axis discovery model

Discovery — the transition from hidden to known — SHALL be defined per axis as
follows, marked at runtime choke points and never derived from placement or
settings data: a hidden-identity DUNGEON is discovered when the player is
observed inside it (dungeon module active with a valid dungeon id, Hyrule
Castle proper folding into the Escape bucket), covering walk-ins, drop-ins,
chain seams, spiral entries, and spawn points without per-entry-path hooks; a
shuffled CAVE interior is discovered when its door is entered (the existing
entrance-discovery mark); a shuffled WHIRLPOOL pair is discovered when either
direction is ridden (the layout is an involution, so one ride reveals both).
Discovery SHALL immediately invalidate the memoized live reachability so the
surfaces light up on the same session.

#### Scenario: First entry reveals a dungeon live

- **WHEN** the player enters a hidden-identity dungeon through any path for the
  first time
- **THEN** the discovery bit is set, the live reachability memo is invalidated,
  and the tracker surfaces present that dungeon's true availability without a
  restart or reload

#### Scenario: Identity-mapped assignment stays hidden

- **WHEN** the shuffle assigns a dungeon behind its own vanilla door and the
  player has not entered it
- **THEN** the dungeon is still treated as hidden (the player cannot know a
  door maps to itself)

### Requirement: Knowledge-limited live reachability semantics

The live reachability consumed by player-facing surfaces SHALL exclude
undiscovered hidden-identity dungeons from the region fixpoint entirely — their
regions never enter the reachable set and never propagate reachability onward —
SHALL suppress the locations (and decoupled exit edges) of undiscovered
shuffled cave interiors, and SHALL skip undiscovered shuffled warp edges. The
hidden-identity set SHALL
derive from the ACTIVE slot's installed layout tables (the entrance-shuffle
dungeon assignment including the Ganon's-Tower opt-in, and the chains pool),
never from hardcoded dungeon lists. Generation and placer reachability SHALL be
computed with full knowledge, unaffected by any mask.

#### Scenario: Pass-through channel is closed

- **WHEN** a chains slot places a multi-entrance dungeon (whose auxiliary exits
  stay vanilla) mid-chain behind an accessible door and the player has not
  entered it
- **THEN** overworld locations reachable only through that dungeon's interior
  (e.g. its exterior ledge) are not presented as available

#### Scenario: Generation is mask-free

- **WHEN** a seed is generated while a knowledge-masked live view exists for
  the active slot
- **THEN** the generation-side reachability, placement result, and
  `placement_digest` are byte-identical to a process that never computed a
  masked view

### Requirement: Future topology axes declare a discovery model

Any future axis that shuffles world topology or a per-seed assignment consumed
by logic SHALL, in its change spec, either define its discovery model (what the
player must observe for each fact to become known, and how the tracker gates
on it) or explicitly classify itself as safe with a stated reason (e.g. edges
gated on an item whose in-game UI reveals the mapping). The CI knowledge-guard
allowlist SHALL only be extended with a written justification.

#### Scenario: New axis without a discovery model is rejected in review

- **WHEN** a change proposes a topology-shuffling axis whose spec neither
  defines a discovery model nor claims the safe classification
- **THEN** the change fails spec review against this capability

### Requirement: Door-shuffle knowledge gating (deferred final phase)

Intra-dungeon reachability under `door_shuffle` SHALL eventually be limited to
door connections the player has traversed (the persisted door-discovery bits),
making in-dungeon availability reflect the doors actually mapped. This phase is
DEFERRED by owner decision: it SHALL start only after every other requirement
in this capability is implemented, validated, and audited, and its detailed
scope SHALL be re-confirmed with the owner at that time. Until then, door
shuffle's intra-dungeon full-knowledge availability is a documented, accepted
limitation.

#### Scenario: Deferred phase does not block the rest of the change

- **WHEN** the non-door phases of this capability are complete and validated
- **THEN** the change is archivable with door gating recorded as deferred, and
  the sidecar format already reserves the door-discovery bits

### Requirement: Knowledge-guard CI check

A CI check script SHALL fail the build when a source file outside an explicit
allowlist (generation, gameplay delivery, spoiler writer, selftests, and the
audited display sites) consumes the full-knowledge reachability API or the raw
assignment getters (prize, medallion, boss, entrance-connection, door-link
accessors, or placement item ids). An intentional exception SHALL require a
`knowledge-guard: allow <reason>` annotation at the call site.

Because the guard is file-granular, a new audited display site SHALL be placed
in a purpose-scoped source file rather than allowlisting a large general-purpose
file, so the allowlist entry cannot silently authorize unrelated future reads in
the same file.

#### Scenario: New unaudited consumer fails CI

- **WHEN** a new source file reads a raw assignment getter without the
  allowlist entry or annotation
- **THEN** the check script exits nonzero and CI fails

#### Scenario: Display site is scoped to its own file

- **WHEN** an in-world display surface needs a raw assignment getter
- **THEN** the read lives in a purpose-scoped module that the allowlist names,
  not in the vanilla engine file that draws the surface

### Requirement: In-world surfaces are knowledge-gated too

An in-world (diegetic) surface that displays a per-seed assignment — a pause-map
marker, a HUD glyph, an in-world sprite or plaque — SHALL be held to the same
invariant as the tracker surfaces: it may only assert what is true under EVERY
assignment consistent with the player's in-game observations. Where the surface
cannot make a knowledge-consistent assertion it SHALL degrade to an explicitly
unknown presentation (the vanilla red-X/blank marker form where one exists),
never to the vanilla assumption, and never to the true assignment.

Observation for a dungeon prize SHALL mean that dungeon's prize location is
checked. Ownership of the prize TYPE, ownership of the dungeon's map or compass,
and having entered or cleared the dungeon SHALL NOT count as observation of its
prize. Introducing any other disclosure path (for example an upstream-style
compass-reveals-prize axis) is a settings-level decision outside this
requirement and SHALL NOT be introduced as part of a correctness fix.

A surface whose position or presence is a vanilla fact independent of the seed
(a dungeon's vanilla overworld coordinates, the existence of a prize at a prize
dungeon) MAY be shown unconditionally; a surface whose position would encode a
shuffled assignment (a marker relocated to a shuffled entrance) SHALL NOT.

#### Scenario: Unobserved shuffled prize shows the unknown form

- **WHEN** a `prize_shuffle` slot is active and the player has not collected a
  given dungeon's prize
- **THEN** that dungeon's pause-map marker draws the unknown (blinking red X) form

#### Scenario: A known un-gated surface is recorded, not silently tolerated

- **WHEN** an in-world surface is found to encode a per-seed assignment and is
  not yet gated
- **THEN** the gap is named in the owning change's design and in
  `docs/randomizer.md` rather than left implicit, and a surface investigated and
  found NOT to leak is recorded with its reasoning so it is not re-opened

#### Scenario: Prize TYPE is invariant, so type-encoding surfaces do not leak

- **WHEN** assessing whether a surface that distinguishes only pendant-from-
  crystal (for example the dungeon-area overworld music) leaks under
  `prize_shuffle`
- **THEN** it does not, because `PrizeShuffle_Run` permutes the pendant pool
  only among the three pendant dungeons and the crystal pool only among the
  seven crystal dungeons: a dungeon's prize TYPE never changes, and only WHICH
  pendant or WHICH crystal is secret. A surface must distinguish individual
  prizes, not merely their type, before this capability governs it

#### Scenario: Identity roll is still hidden

- **WHEN** `prize_shuffle` is enabled and the roll happens to reproduce the
  vanilla assignment, and the player has not collected the prize
- **THEN** the marker still shows the unknown form (the player cannot know the
  roll was the identity)

#### Scenario: Unrecoverable settings fail closed

- **WHEN** the active slot's settings cannot be recovered (snapshot replay or a
  pre-settings slot) and a per-seed assignment surface must render
- **THEN** the surface treats the assignment as shuffled and renders the unknown
  form rather than the vanilla assumption

