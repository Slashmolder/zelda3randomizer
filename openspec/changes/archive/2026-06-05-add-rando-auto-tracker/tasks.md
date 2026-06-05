# Tasks — add-rando-auto-tracker

Implementation checklist. The feature is **off by default**, **observation-only**,
and **localhost-bound**; regression risk is zero by design. No `kGeneratorVersion`
bump and no corpus regeneration (pure observation — placement bytes unchanged).

## 1. Module skeleton
- [x] 1.1 New module `src/rando/auto_tracker.{c,h}` with the three lifecycle entry
  points (`AutoTracker_Init` / `AutoTracker_ServiceFrame` / `AutoTracker_Shutdown`).
  The API lives in the new header — NOT in `rando.h`.
- [x] 1.2 Whole module is a no-op when disabled (single early-return per frame;
  no socket opened). On Switch (`__SWITCH__`) the module compiles to no-op stubs
  so the build links cleanly with the server omitted.
- [x] 1.3 Wire all three entry points into `src/main.c` (init after SRAM read,
  service after `ZeldaRunFrame`, shutdown before `SDL_Quit`).

## 2. Config
- [x] 2.1 `[AutoTracker]` INI section in `config.c` (section id 9 in
  `GetIniSection` + handler): `Enabled` (default false), `Port` (default 17400),
  `AllowRemote` (default false → bind `127.0.0.1`; `0.0.0.0` only on opt-in).
- [x] 2.2 Fields on the `Config` struct (`config.h`) + defaults set in
  `ParseConfigFile` before the file is read.
- [x] 2.3 `--auto-tracker` CLI flag force-enables the server (extracted + compacted
  out of `argv` so it is not mistaken for the ROM path).
- [x] 2.4 PC settings-window control: a start/stop toggle + status line (bind
  address / port / client count) in the **Trackers** tab (`Panel_Trackers` in
  `rando_window.cpp`). Backed by a runtime start/stop API
  (`AutoTracker_SetEnabled` / `IsRunning` / `GetClientCount` / `GetBindInfo`);
  the listener opens/closes live, the bind config stays INI-sourced. Session-only
  (matches the tracker-window toggles); the INI `Enabled` is the boot default.

## 3. TCP listener / transport
- [x] 3.1 Non-blocking listener, localhost by default; multiple subscribers
  (fixed cap); per-client outbound buffer; drop on overflow / error.
- [x] 3.2 Raw sockets behind a tiny internal interface (listen / accept / send /
  close) — Winsock on `_WIN32` (`ws2_32.lib` via `#pragma comment`), BSD sockets
  elsewhere. No SDL_net / no vendored net library (deliberate; see design.md).
- [x] 3.3 SIGPIPE-safe sends (`MSG_NOSIGNAL` / `SO_NOSIGPIPE`); `EPIPE` /
  `ECONNRESET` / `WSAECONNRESET` and peer-close drop the client, never crash.

## 4. Snapshot → JSON
- [x] 4.1 Build the snapshot from the existing accessors (`Rando_FillItemView`,
  `Rando_GetLiveReachability` + `Reachability_HasLocation`,
  `Rando_IsLocationChecked`, `Placement_GetActive`, `Rando_GetActiveSettings`) so
  the stream stays byte-consistent with the in-game trackers.
- [x] 4.2 Hand-rolled newline-delimited JSON serializer (no floats — determinism
  guard). One message per line; full snapshot on connect, then on each
  `Rando_GetReachabilityCounter()` advance (+ active-slot / goal-completion change).
- [x] 4.3 One-time `catalog` message (location id → name + region) so external
  clients resolve ids without hardcoding the fork's id space.
- [x] 4.4 Spoiler-safe: never emit placement (item-at-unchecked-location); only
  inventory + checked + reachable. No race-mode gate needed.

## 5. Lifecycle gates
- [x] 5.1 Server NOT started under `--rando-selftest` or headless CLI paths
  (`g_headless_mode` guard; headless paths also exit before init).
- [x] 5.2 Full inventory/reachability flows only while `Rando_IsActive()`; inactive
  → minimal `active:false` line on connect.

## 6. Docs
- [x] 6.1 New "Auto-tracker (external clients)" section in `docs/randomizer.md`
  (INI keys, CLI flag, wire protocol + message schema, security, Switch note).

## 7. Verify
- [x] 7.1 Clean build on both platforms; guards green (`check_determinism.py` /
  `check_audit_guard.py` / `check_codegen_wiring.py`).
  - Windows: MSVC x64 Release builds + runs.
  - Linux (the CI `-Werror` path the Windows build never exercises): validated in
    WSL Ubuntu, gcc 15.2.0. `auto_tracker.c` compiles `-O2 -Wall -Werror` clean,
    and the full CI-equivalent `make -j zelda3` (`-O2 -Werror`) links a working
    ELF (rc=0). Only warnings are pre-existing `-Wformat-truncation` in
    `rando_window.cpp` (C++, built without `-Werror`; not this change).
- [x] 7.2 `--rando-selftest` OK and the server is NOT started under it.
- [x] 7.3 Default-off opens no socket; loopback client receives `catalog` + `state`
  on connect; multiple clients + reconnect; client disconnect does not crash the
  game. (Verified headlessly with a throwaway Python client.)
- [x] 7.4 **Owner real-client test** — confirmed end-to-end: a raw TCP client
  received the `catalog` + a full `active:true` snapshot on slot load, and a
  location check (Link's House) advanced the counter and updated checked/reachable.
  Caught + fixed a `game_completed` false-positive at load (the load path
  transiently hits main_module 0x1B SpawnSelect; gate is now TriforceRoom 0x19 /
  Credits 0x1A). Turnkey EmoTracker/PopTracker support (their usb2snes/UAT
  protocols) is a separate follow-up; custom OBS/script consumers work today.
- [x] 7.5 Fresh-eyes audit on the diff (separate review agent). Confirmed zero
  game-state mutation, no spoiler leak, correct NULL-guards / client lifecycle,
  clean determinism. Fixed one MED (bounded `at_drain_client` so a flooding
  localhost client can't stall the game frame) + a diagnostic for the latent
  handshake-overflow case.

## 8. Out of scope (future)
- UDP broadcast option for multi-client setups (the proposal lists it as optional;
  TCP fan-out to multiple subscribers covers the same need today).
- Switch libnx networking (server is intentionally omitted there for now).
