# randomizer-ui Specification (delta)

## MODIFIED Requirements

### Requirement: Auto-tracker server

The randomizer SHALL optionally expose a local-only TCP server that emits per-state-change JSON messages describing the player's current inventory, reachability state, and checked-location bitmap. External tracker clients (EmoTracker, PopTracker, custom OBS overlays) subscribe to the stream and render state without needing to peek at `g_ram` via emulator memory APIs.

The emitted reachability state SHALL be the knowledge-limited live view (see `randomizer-player-knowledge / Player-knowledge invariant for tracker surfaces`): the `reachable` array never includes locations whose availability would reveal an undiscovered shuffled assignment. This makes the export's spoiler-safety contract hold by construction for every seed — race or casual — because it emits only the player's own inventory, their checked locations, discovered connections, and knowledge-limited availability.

**Lifecycle**:
- Disabled by default. Enabled via `[AutoTracker] enabled = true` in `zelda3.ini` or via CLI flag `--auto-tracker`.
- TCP listener binds to `127.0.0.1:<port>` (default 17400, configurable via INI). A malformed or out-of-range `Port` value (valid range 1..65535) SHALL be rejected with the standard config-parse warning, keeping the default.
- Remote bind (`0.0.0.0`) SHALL require explicit opt-in via INI `[AutoTracker] allow_remote = true`. Default localhost-only.
- Subscribe-only: external clients receive state; they CANNOT write back. No state-injection API.

**Emission triggers** (event-driven; not per-frame):
- `reachability_state_counter` advances (same trigger as the in-game tracker overlay refresh).
- Checked-location bitmap updates.
- Goal-completion state changes.

**Protocol**: newline-delimited JSON. Each message is a single line; the schema is documented in `docs/randomizer.md` Auto-tracker section.

> **Stub status**: exact JSON message schema, transport details (TCP-only vs. TCP+UDP option), and per-platform networking (Switch libnx integration) deferred to Phase D apply-time.

#### Scenario: Auto-tracker disabled by default
- **WHEN** a player launches the binary with default settings
- **THEN** no TCP socket is opened; no protocol overhead; the binary behavior matches pre-this-change exactly

#### Scenario: Localhost binding by default
- **WHEN** `[AutoTracker] enabled = true` but `allow_remote` is unset
- **THEN** the listener binds to `127.0.0.1:17400`; remote machines cannot connect

#### Scenario: Subscriber receives state on counter advance
- **WHEN** an external client is connected and the player picks up an item that advances `reachability_state_counter`
- **THEN** the server emits a JSON message describing the new state within one frame of the counter advance

#### Scenario: Reachable export is knowledge-limited
- **WHEN** a dungeon-topology slot is active, no hidden-identity dungeon has been entered, and a client receives a state message
- **THEN** the `reachable` array contains no location inside an undiscovered hidden-identity dungeon and no overworld location whose only route passes through one

#### Scenario: Determinism unaffected by auto-tracker
- **WHEN** the same seed is generated with auto-tracker on vs. off
- **THEN** the `placement_digest_hex` is byte-identical; auto-tracker is observation-only

#### Scenario: Auto-tracker disabled during self-test
- **WHEN** `--rando-selftest` is invoked
- **THEN** the auto-tracker server is not started (regardless of INI setting); self-tests run in an isolated environment

#### Scenario: Switch builds may omit auto-tracker
- **WHEN** the Switch build is compiled
- **THEN** the auto-tracker SHALL be optional (depending on libnx networking support availability); a Switch build that omits the server is a valid configuration
