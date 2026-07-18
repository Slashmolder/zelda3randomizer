# Proposal: Random crystal counts (`crystals.ganon=random` / `crystals.tower=random`)

## Why

The two crystal-requirement axes accept only fixed 0..7 today, so every
player knows the GT-entry and Ganon-vulnerability thresholds before the seed
starts. Upstream ALTTPR offers "random" but rolls it at the WEB layer
(`get_random_int(0,7)` in `RandomizerController.php:107-109`) — the rolled
number is baked into the request and visible. A fork-native version can do
better: keep the **sentinel** in the canonical settings (share strings
preserve "random", the settings hash is distinct from every fixed count) and
resolve the effective counts **deterministically from the seed**, so the
requirement is discovered in play (Ganon already tells you his count via the
existing msg-0x16F rewrite; the tracker shows both once the slot loads).

## What Changes

- **Sentinel value 8 (`random`)** accepted for `crystals_ganon` /
  `crystals_tower` (canonical bytes [2]/[3] — full bytes, no bit surgery).
  `Settings_Validate` / deserialize widen from `>7` to `>8`; CSV gains the
  `random` keyword; defaults unchanged.
- **`Crystals_Resolve(settings, seed_u64, *ganon, *tower)`** — one shared
  deterministic resolver (salted per-axis streams, `Rng_NextRange(0..7)` per
  random axis, fixed axes pass through), following the `KeyRings_Resolve`
  requested/effective template. The REQUESTED sentinel is never normalized
  away (stays in canonical/hash/share, mirroring the keyrings note); the
  resolve stays OUT of `apply_derived_rules`.
- **Generation reads resolved values**: `Goal_IsCompletable`'s
  ganon/fast_ganon arms certify against the RESOLVED ganon count (today the
  only generation-time reader; a sentinel read there would refuse every
  seed). `crystals_tower` has no generation reader — its only consistency
  obligation is that generation and runtime resolve identically, which the
  single shared resolver plus a selfcheck vector pins.
- **Runtime caches resolved values at slot activation**: two new getters
  backed by fields populated in `Rando_ActivateSidecarSlot` (seed already
  decoded there); ALL five live read sites move to the getters — the GT
  door gate, Ganon vulnerability, the zero-crystal GT pre-open, the Ganon
  dialogue rewrite (msg 0x16F — this becomes the in-world reveal of the
  rolled ganon count), and the auto-tracker settings emission (external
  trackers need real numbers, not 8).
- **Spoiler** emits requested-vs-resolved (keyrings precedent):
  `crystals_ganon`/`_tower` stay the requested bytes; when either is random,
  add `crystals_ganon_resolved` / `crystals_tower_resolved` +
  `crystals_salt_version: 1`.
- **UI**: native window sliders become 0..8 with value 8 rendered "Random";
  the in-game (Switch) settings rows render "RAND" and cycle 0..8; both
  keep 0..7 behavior byte-identical when the sentinel is unused.
- **Validation**: resolver selfcheck (determinism + range + per-axis
  independence + pinned vector), corpus rows for random ganon / random
  tower / both (including a `goal=ganon` row so the resolved-count
  completability path is pinned), all under the branch's existing
  kGeneratorVersion 146 (unreleased). Fixed-count seeds are byte-identical
  (proven by the existing 221 rows).

## Capabilities

### New Capabilities

- `randomizer-random-crystals`: the sentinel semantics, deterministic
  resolution contract, generation/runtime consistency rule, reveal surfaces,
  and UI presentation.

### Modified Capabilities

- (none as deltas — the canonical-table rows for bytes [2]/[3] gain the
  sentinel note at archive time alongside this change's ADDED requirements;
  authored as ADDED per house preference to avoid a second whole-table
  restatement colliding with add-rando-shopsanity's pending MODIFIED delta.
  Archive order on this branch: shopsanity first, then this change.)

## Impact

- `src/rando/rando_settings.{c,h}` (validate/deserialize bounds, CSV
  keyword, sentinel enum), `src/rando/rando_placement.{c,h}`
  (`Crystals_Resolve`, Goal_IsCompletable resolved reads, selfcheck),
  `src/rando/rando.c` (slot-load cache + getters + gates + dialogue +
  tracker), `src/rando/rando_spoiler.c` (resolved fields),
  `src/rando/rando_window/rando_window.cpp` + `src/select_file.c` (UI),
  `tests/rando_corpus/manifest.yaml` (new rows).
- No new sidecar fields (resolved values re-derived from the stored seed at
  every activation); no placement change for fixed-count seeds.
