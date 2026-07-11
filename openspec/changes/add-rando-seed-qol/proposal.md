## Why

Running randomizer seeds — grinding or racing them — carries a steady tax of
friction the placement pipeline never touches: dead-time animations, missing
information that forces a second-monitor tracker, and unskippable cutscenes seen
hundreds of times. A player-perspective review of a candidate list (an
experienced seed runner reacting to each idea from the chair, not the code)
sharpened the set: the biggest wins are **information** features that stop
backtracking and prevent run-enders, not menu/animation speed. The runner's
verdict, paraphrased: "where real minutes go in a run" is *knowing where to go*
and *not re-clearing dungeons you've emptied* — followed by the recurring
cutscenes and a couple of routing tools.

This change bundles the QoL features judged **worth building ever** into one
coherent, incrementally-shippable set. Several ideas were **deliberately cut**:

- **Fast menu** — the inventory menu is already fast; the runner "would not
  notice this," rated it the weakest item on the list.
- **Auto-equip on pickup** — an outright footgun: a surprise Y-swap onto a
  just-grabbed item pulls the bow/hookshot off exactly when it's needed.
- **Heart-beep speed tiers** — `kFeatures0_DisableLowHealthBeep` already solves
  the on/off need; tiers polish a solved problem.
- **Start-with-half-magic** — marginal (only swordless / early-fire-rod), and the
  item-pool difficulty already governs magic.
- **Screen-scroll transition speedup** — a muscle-memory footgun; changing scroll
  timing makes routing *feel* wrong. (Mirror/flute animation speed is kept; scroll
  is not.)

**The unifying design property: none of these affect placement.** Every toggle is
a new `kFeatures0_*` feature bit (bits 20-31 are free) or a local `zelda3.ini`
keybind/value, gated at point-of-use; the rendering features are client-side
(HUD/OAM/PPU) and read data the runtime already has. So the whole bundle needs
**no `kGeneratorVersion` bump, no `settings_hash` / `kSettingsCanonicalLen`
change, and leaves the regression corpus byte-identical.** The per-slot subset
joins `kFeatures0_RandoSeedQolMask`, so a shared seed can recommend them without
pinning them. The off-path stays RAM-compare-clean because these bits — like the
existing Seed QoL bits — are suppressed / no-op under side-by-side emulation.

## What Changes

Six shipping features plus the deferred seed overlay, ordered by the reconciled priority (justification in
`design.md`). Each is independently gated and shippable; `tasks.md` phases them so
every feature ends at a buildable, playtestable checkpoint.

- **F1 — Dungeon check-info on the pause map** (top priority). Show, per dungeon,
  how many of its randomizer checks remain (a `checked/total` count), and — as a
  second phase — dots on the dungeon map at the *located* remaining checks. The
  count is a small new cached accessor over the existing checked-location state
  (`Rando_IsLocationChecked`), so phase 1 is a lightweight HUD render. Rando-only,
  default on, not a spoiler (it's counts, not item names). Client-side only.
- **F2 — Deferred.** The in-game seed info overlay is not shipping in this change:
  playtest showed the surface was ugly, easy to cover, and redundant with the native
  tracker/generate-copy UI. Bit 21 remains reserved so later features keep their
  test-branch values.
- **F3 — Message & fanfare speed.** A configurable **text speed** (e.g.
  `normal` / `fast` / `instant`) — a *speed setting*, not on/off, so hint /
  telepathy text is never blown through before it can be read (hint-safe by
  design) — plus fast advance of the item-pickup fanfare and the recurring
  dungeon small-key / map / compass "get" holds.
- **F4 — Cutscene & transition fast-forward.** Skip or auto-advance the recurring
  animations: crystal/pendant prize-get, the GT crystal-barrier, the
  pyramid-opening, the post-Agahnim defeat transition, the Zelda escort dialogue, and the
  death / game-over fade. **The load-bearing invariant: skip the *animation*,
  never the *flag*.** Several of these set progression bits (post-Agahnim, Zelda
  escort, prize-get); a skip that drops one is a run-killer far worse than the time
  saved, so each cutscene's fast-path must preserve every flag/SRAM write the
  vanilla path performs. Per-cutscene, so it lands incrementally.
- **F5 — Quick reset / warp-to-spawn.** A "warp to your spawn point" that skips the
  Save-and-Quit → file-select round-trip. S&Q
  is already a routing tool in ALTTP; this is a routing feature, not just comfort.
  **Cleanly toggleable for race legality** — an instant reposition is close to a
  movement exploit, so a race org can ban it.
- **F6 — Auto / hold-to-dash.** An option to remove the Pegasus-boots charge-up so
  dash triggers on hold (auto-Pegasus). Per-screen comfort, natural sibling to the
  existing `kFeatures0_TurnWhileDashing`.
- **F7 — Entrance/door connection feed to the auto-tracker.** Emit discovered
  entrance-shuffle / door-shuffle connections over the **existing** auto-tracker
  TCP protocol as the player walks them — NOT an in-game overlay. (The runner
  already runs an external whole-graph entrance tracker; a one-door-at-a-time
  in-game overlay would add clutter without replacing it. Feed the external tool
  instead.)

## Capabilities

### New Capabilities

- `randomizer-seed-qol`: the QoL feature-bit family and its invariants
  (placement-neutral, replay-safe via the Seed QoL mask, RAM-compare-clean
  off-path); the dungeon check-info HUD render (F1); the message/fanfare speed
  model incl. the hint-safe guarantee (F3); the
  skip-animation-not-flag cutscene/transition fast-forward contract (F4); the
  auto/hold-dash option (F6); and the quick reset / warp-to-spawn tool with its
  race-toggle (F5). F2 is explicitly deferred/reserved.

### Modified Capabilities

- `game-config-ui`: ADD the new Seed-QoL toggles (F3 text-speed selector, F4
  cutscene-FF, F5 warp-to-spawn + its race note, F6 auto-dash) to the PC Game
  Settings panels and the corresponding `zelda3.ini` keys, following the existing
  `kFeatures0_*` checkbox / keybind pattern. The stale branch-test F2 key is
  accepted but ignored.
- `randomizer-native-window`: ADD F1/F3/F4/F5/F6 to the Seed QoL tab
  (`kRecBits`/`kRecLabels`) so per-slot recommended values round-trip, and surface
  the F7 auto-tracker connection state where the tracker panels live.
- `randomizer-ui`: ADD the dungeon check-info counts + located dots to the pause
  dungeon-map render (F1); specify these read the tracker's existing per-dungeon
  checked/total accessor and draw no spoiler content.

> No `randomizer-core` / `randomizer-placement` / `randomizer-save` delta: the
> bundle changes no canonical settings, no placement, and no on-disk save format.
> The per-slot recommended bits ride the existing `recommended_features0`
> (format_version ≥ 3) field with no layout change.

## Impact

- **Feature flags** (`src/features.h`): new `kFeatures0_*` bits in the free
  20-31 range for F1, F3, F4, F6, and the local F5 toggle; each added to
  `kFeatures0_RandoSeedQolMask` where it is a per-slot preference. Bit 21 remains
  reserved for deferred F2, and the 0x66d-0x66f RAM block remains unused.
- **Runtime code**: `src/messaging.c` (F3 text speed + fanfare), `src/misc.c`
  item-receipt path (F3 fanfare), `src/hud.c` / pause-map module (F1 counts + dots),
  the prize/cutscene sites and `MirrorWarp_*` / bird-travel and the
  game-over module (F4), `src/player.c` boots charge (F6), the Save-and-Quit /
  spawn path (F5), `src/config.c` (F5 keybinds, F3 speed value),
  `src/rando/auto_tracker.c` (F7 connection emission),
  `src/rando/rando_window/` (all toggles). Exact anchors enumerated in `design.md`
  from a source read, not memory.
- **Determinism / corpus**: **byte-identical** for every existing seed; **no
  `kGeneratorVersion` bump**; `--rando-selftest` and the placement corpus are
  unaffected (they never exercise these runtime paths — the load-bearing net is
  playtest, per the project's dominant-bug-class discipline).
- **RAM-compare**: every new bit is a Seed-QoL-class runtime option, suppressed /
  no-op under side-by-side emulation so the per-frame comparator stays clean; the
  cutscene fast-paths (F4) MUST NOT change any `g_ram`/SRAM write vs. vanilla
  beyond the animation timing, which is the explicit skip-animation-not-flag
  invariant.
- **Verification**: because these live at gameplay/render sites invisible to the
  corpus and self-tests, each feature carries a **playtest checkpoint** in
  `tasks.md`; F4 additionally requires a per-cutscene flag-preservation check
  (grant/progression bit set identically to vanilla), and F3 a hint-tile
  readability check.
- **`make clean`** required after the `features.h` bit additions (Makefile has no
  header-dependency tracking).
