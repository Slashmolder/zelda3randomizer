## MODIFIED Requirements

### Requirement: Hints viewer respects race-mode suppression

The native Hints panel SHALL be a read-only discovered-hint journal for the
active validated `HintPlan`. Its default view SHALL list only delivery facts
whose discovery bits are set. Each row SHALL show the source at which the fact
was discovered, the canonical complete human-readable text produced by the
active plan/text schema, and a
resolved marker when an exact fact's target location is checked. Resolved state
SHALL be derived live from the checked-location bitmap rather than persisted as
separate journal state.

The journal text SHALL be `RandoHintFact.text`; the compact verified
three-row `RandoHintFact.game_text` form remains an in-game delivery rendering
and is not required to replace the fuller journal wording.

Outside race mode, the panel MAY offer an explicit
**View all seed hints — placement spoilers** action. The action SHALL require a
clear confirmation before showing undiscovered facts and SHALL be session-local.
Opening, confirming, closing, filtering, or rendering the full view SHALL NOT
set discovery bits, move a paid queue, mark a location checked, change the
active plan/digest, or mutate other gameplay state.

In race mode the panel SHALL show discovered facts only and SHALL NOT render,
enable, or advertise the full-deck action. It SHALL not expose undiscovered
fact text, targets, assignment order, paid reserve order, or hidden plan state
through normal UI. This restriction SHALL remain tied to the active slot's
persisted race identity, not the pending settings editor.

If no randomizer slot is active, no facts are discovered, or plan identity
validation failed, the panel SHALL show the corresponding honest empty or
unavailable state. It SHALL NOT display stale facts from a prior slot.

F12 is outside this UI requirement. It is an intentional developer diagnostic
and MAY contain the full spoiler-bearing plan in race mode.

#### Scenario: Discovered facts appear in the journal
- **WHEN** the player successfully reads one assigned tile and buys one paid
  fact
- **THEN** the default panel lists exactly those discovered facts with their
  discovery sources and active-schema text

#### Scenario: Undiscovered facts stay hidden by default
- **WHEN** a valid plan has additional undiscovered tile or reserve facts
- **THEN** they do not appear in the default journal outside or inside race mode

#### Scenario: Exact fact becomes resolved
- **WHEN** an exact discovered fact targets a location that later becomes
  checked
- **THEN** its journal row gains the resolved marker without changing discovery
  or persisted plan identity

#### Scenario: Full viewer requires non-race confirmation
- **WHEN** a non-race player selects View all seed hints
- **THEN** the panel warns that it reveals placement spoilers and shows the full
  deck only after explicit confirmation

#### Scenario: Full viewer is read-only
- **WHEN** the confirmed non-race full viewer renders every primary and reserve
  fact
- **THEN** discovery, queue selection, checked state, plan digest, and gameplay
  state remain byte-identical before and after

#### Scenario: Race mode exposes discovered facts only
- **WHEN** the active slot is race mode
- **THEN** discovered journal rows remain visible but the full-view action and
  every undiscovered fact/target/order are absent

#### Scenario: Pending race toggle does not change active journal policy
- **WHEN** the user edits the pending `race_mode` checkbox while another slot is
  active
- **THEN** the Hints panel continues to follow the active slot's persisted race
  identity

#### Scenario: Failed plan identity shows no stale deck
- **WHEN** sidecar or snapshot activation disables hints for unsupported or
  mismatched identity
- **THEN** the panel reports hints unavailable and shows neither the prior
  slot's journal nor a reconstructed current-version deck

#### Scenario: Empty discovery is honest
- **WHEN** a valid Balanced plan is active but no delivery fact is discovered
- **THEN** the default journal reports that no hints have been discovered yet,
  rather than claiming the seed has no hint plan

#### Scenario: Hint text hidden in race mode
- **WHEN** the active slot is race mode and has undiscovered facts
- **THEN** those undiscovered fact strings, targets, and order remain hidden;
  only facts already discovered through gameplay appear in the journal

#### Scenario: Hint text shown outside race mode
- **WHEN** the active slot is not race mode
- **THEN** discovered facts appear in the default journal and the full deck is
  shown only after the explicit placement-spoiler confirmation
