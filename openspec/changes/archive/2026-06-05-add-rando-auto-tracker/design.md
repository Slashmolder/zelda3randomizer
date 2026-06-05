# Design — add-rando-auto-tracker

## Context

External ALTTPR tracker tools (EmoTracker, PopTracker, OBS overlays) read SNES
state by peeking at emulator RAM via `usb2snes` / SNI. This change gives the
native binary a first-party way to publish the same state: a small, opt-in TCP
server that re-emits the in-game tracker snapshot as newline-delimited JSON.

The in-game ImGui trackers (`tracker_windows.cpp`) already expose a clean C-side
state boundary — item view, live reachability, checked bitmap — built once and
reused. The auto-tracker is "the tracker windows, but over a socket": it calls the
same accessors so the two can never disagree.

## Goals / non-goals

- **Goal:** zero-overhead-when-off, observation-only, localhost-by-default,
  subscribe-only publication of tracker state to local clients.
- **Goal:** swap-able transport contained in one file (P2P-multiplayer may later
  standardize a net library).
- **Non-goal:** any inbound command/state-injection path; any game-state mutation;
  any placement/RNG/determinism effect; vendoring a networking library now.

## Decisions

### Raw sockets, not SDL_net (or any net library)
No networking library is vendored today (`third_party/` has only SDL2, gl_core,
imgui, opus, sha256, stb). A future P2P cross-world multiplayer feature *may*
standardize the project on a net lib (SDL_net / ENet / GameNetworkingSockets), but
that transport is **undecided**. Adding SDL_net here would commit all three build
systems (Make / MSBuild / Switch) to a dependency we may not keep, and SDL_net is
not bundled with SDL2 (= +1 DLL + CI + Switch dep). So the server uses raw sockets
— Winsock (`ws2_32`, linked via `#pragma comment(lib,...)` so no `.vcxproj` edit)
on `_WIN32`, BSD sockets in libc elsewhere — behind a handful of static helpers
(`at_listen` / `at_accept` / `at_send` / `at_close` + non-blocking / SIGPIPE
shims). When the multiplayer decision lands, only this one file changes.

### Observation-only; reuse the existing accessor boundary
The snapshot is built entirely from existing public accessors:
`Rando_FillItemView`, `Rando_GetLiveReachability` + `Reachability_HasLocation`,
`Rando_IsLocationChecked`, `Placement_GetActive`, `Rando_GetActiveSettings`,
`Rando_GetReachabilityCounter`, `Rando_IsActive`, location/region name lookups.
The module never writes `g_ram` (confirmed by `check_audit_guard.py`). It reads
one named RAM byte — `main_module_index` — only to derive `game_completed`
(≥24 = Ending/Credits, the same threshold the race-mode reveal anti-cheat uses).

### Emission trigger: the reachability counter (+ active / completion)
`g_reachability_state_counter` is bumped on item grant, location check, and slot
activation — i.e. it already covers item *and* checked-bitmap changes. The server
caches the last `(counter, active, game_completed)` it sent and emits a fresh full
snapshot whenever any of the three changes, plus an immediate snapshot to each
newly-connected client. This is event-driven (not per-frame) and matches the
in-game tracker overlay's refresh signal. The public getter
`Rando_GetReachabilityCounter()` is used directly — no new declaration is added to
`rando.h`.

### Full-snapshot protocol (not deltas)
Every `state` message is a complete snapshot, so a client only needs the latest
line and the server needs no per-client delta bookkeeping. Framing is
newline-delimited JSON (one object per line). A one-time `catalog` message
(location id → name + region) makes the numeric id space self-describing so
external tools need not hardcode this fork's ids.

### Spoiler-safety by construction
The stream publishes only the player's own inventory, the locations they have
*checked*, and which unchecked locations are *reachable under logic* — never which
item sits at an unchecked location. That is exactly the information the player
already sees in-game, so it carries no placement spoiler and needs no race-mode
gate. (Settings `goal`/`world_state`/crystal counts are the player's own chosen
settings, not spoiler; included only when recoverable, else `settings:null`.)

### Backpressure / robustness
Each client has a fixed-cap heap send buffer (allocated on accept, freed on drop).
Sends are non-blocking; an unsent remainder is retried next frame. A buffer
overflow (client too slow / stuck) or a fatal send error / peer-close drops just
that client — the game frame never blocks and the server never crashes. Inbound
bytes are drained and discarded (subscribe-only) and also used to detect peer
close.

### Platform gating
The whole socket implementation is gated `#if !defined(__SWITCH__)`. The Switch
Makefile's non-recursive `src/rando/*.c` glob still compiles the TU, so on Switch
it provides no-op stubs and `main.c` calls the same three entry points
unconditionally. On Windows, `<winsock2.h>`/`<ws2tcpip.h>` are included at the top
of the TU (with `WIN32_LEAN_AND_MEAN`) before any `windows.h` pull-in, avoiding the
legacy-`winsock.h` double-declaration clash.

## Risks / mitigations

- **Determinism guard** forbids `float`/`double`/`time(`/`rand(` in `src/rando/`.
  The serializer formats integers and strings only; the message ordinal is a plain
  counter (no wall-clock). Verified: `check_determinism.py` scans the module clean.
- **Security:** localhost bind by default; `0.0.0.0` only via explicit
  `AllowRemote = true` (logged with a warning). No inbound command channel.
- **Untested live path:** the `active:true` snapshot exercises the same accessors
  as the shipped in-game trackers; left to the owner's playtest (task 7.4), which is
  the same setup as the real external-client test.
