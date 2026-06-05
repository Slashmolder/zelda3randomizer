## Why

The ALTTPR community uses external tracker tools (EmoTracker, PopTracker, etc.) extensively. These tools display per-item / per-location state in a window separate from the game; race streamers overlay the tracker on their stream. Today these tools work via SNES-emulator memory reads (`usb2snes` / SNI) — they peek directly at `g_ram` at known offsets.

**This change adds a native auto-tracker server** to the zelda3 randomizer binary. Instead of relying on external memory peeks, the binary emits per-frame inventory + reachability + checked-location state via a local TCP/UDP socket. External tracker tools subscribe to the socket and render state without needing to poke at emulator memory.

Why Phase D: this is a community-features-polish item, not a gameplay-affecting change. The in-game tracker (Phase B #2) covers the player's in-game needs; the auto-tracker server is for external display tooling.

## What Changes (intended scope)

- **New module**: `src/rando/auto_tracker.{c,h}`. Lifecycle: optional, disabled by default; enabled via `zelda3.ini [AutoTracker]` settings or CLI flag.
- **Wire protocol**: a small newline-delimited JSON protocol. Each frame (or each state-change event) the server emits a message like:
  ```json
  {"frame": 12345, "items": {"bow": 1, "hookshot": 1, ...}, "checked": [10, 23, 47, ...], "reachable": [...]}
  ```
- **Transport**: TCP listener on `127.0.0.1:<port>` (default 17400, configurable). UDP broadcast option for multi-client setups.
- **Subscribe-only**: external clients connect, receive the stream; they cannot inject state back. No write-back API.
- **Event-driven emission**: per-frame is too noisy; emit on `reachability_state_counter` advances (same trigger as the in-game tracker overlay) + on checked-location bitmap updates.
- **Determinism**: the auto-tracker is observation-only; it does not affect placement, RNG, or game state. Disabled when `--rando-selftest` runs.
- **Cross-platform**: SDL_net or raw POSIX socket / Winsock. Switch: optional (libnx supports networking; Switch homebrew network apps need care).
- **`kGeneratorVersion`**: no bump. Auto-tracker is pure observation.

## Capabilities

### New Capabilities

(none — auto-tracker is an observation surface on the existing state; not a new capability in spec terms.)

### Modified Capabilities

- `randomizer-ui`: ADDED Requirement for the auto-tracker server lifecycle + protocol.

## Impact

- **Code**: `src/rando/auto_tracker.{c,h}` (new), `src/main.c` (lifecycle wiring), `src/config.c` (INI section).
- **Network**: TCP listener. Bind to `127.0.0.1` by default (localhost-only) to avoid exposing the surface to external networks unless the user opts in via INI.
- **Effort**: **2-3 weeks of focused work.** Protocol design + cross-platform socket plumbing + integration with the in-game tracker's state-change events.
- **Regression risk**: zero by design. Disabled by default; opt-in.
- **Dependencies**: Phase A archived; benefits from Phase B #2 trackers (shares the `reachability_state_counter` advance signal).
- **Security**: bind to localhost by default. Allow remote-bind only via explicit INI opt-in.

## Status (stub)

Proposal-only Phase D stub. Detail deferred to Phase D apply-time. Phase D cannot start before Phase A archives.
