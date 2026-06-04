# Lessons — add-rando-retro-world-state (apply)

Captured at the end of the apply + playtest cycle, before merge. Grounded in
what actually happened, not generic advice.

## 1. A missing gitignored codegen asset can silently disable a whole runtime subsystem

The single most expensive bug of the playtest: in the worktree build, **every
chest granted its vanilla item** instead of the placed item. The placement,
share string, and spoiler were all correct. Root cause: `assets/rando/chest_table.gen.bin`
(a gitignored, ROM-extracted asset) was absent when the rando codegen ran, so
`rando_logic_gen.py` emitted an EMPTY `src/rando/chest_lookup.h`
(`kRandoChestLookup_COUNT == 0`). With an empty lookup, `Rando_ChestDispatch`
returns 0xFFFF for every `(room, ordinal)` and the chest falls through to its
vanilla ROM content. Nothing headless catches it — the corpus and
`--rando-selftest` only exercise placement *generation*, never runtime chest
dispatch, and the chest table is a build-time asset. `setup_worktree.py` mirrored
the ROM + asset blob + ini but **not** the chest table, so every worktree build
had silently-broken chests. Fixes: `setup_worktree.py` now mirrors it (with a
loud warning if absent), and CLAUDE.md + `[[worktree_assets_setup]]` document the
trap. **General rule:** when a runtime feature reads a generated/extracted asset,
a missing asset usually fails OPEN (empty table → vanilla fallthrough), not loud
— so a fresh build env is a first-class suspect for "placement right, runtime
wrong." The F12 g_ram dump (`RandomizerActive` was set, so it wasn't a "rando
off" problem) pointed straight at per-grant resolution.

## 2. Shipping half of an ALTTPR flag *pair* inverts the intended difficulty

ALTTPR Retro forces `wildKeys` **and** `genericKeys` together: keys go everywhere
**and** any key opens any door — net *more forgiving* than vanilla. We shipped
`wildKeys` and deferred `genericKeys` (it needs the hard per-dungeon key-door
logic rewrite). The interim — keys scattered but each still only opens its own
dungeon — is *keysanity*, which is *harder* than vanilla: the opposite direction
from where ALTTPR Retro lands. The owner immediately flinched at it in playtest.
**Lesson:** when an upstream mode forces a *pair* of coupled flags, shipping one
without the other is not "80% there" — it can land somewhere the upstream never
goes. Either ship the pair together, or make the interim an explicit, owner-chosen
state (we kept `wildKeys` forced by owner decision, with the difficulty caveat
documented, and queued `genericKeys` as `add-rando-retro-generic-keys`).

## 3. A world-state-forced setting must be reflected in the UI, or it reads as a free toggle

`wildKeys` is forced by pinning `dungeon_small_keys_mode = Wild` for Retro, but
the PC settings window still rendered the "Small keys" combo as an editable
toggle — so it looked like the player's choice mattered when the generator
silently overrode it. The fix mirrors the existing Completionist→accessibility
lock: under Retro the control is disabled, shows "wild", and carries a "forced by
Retro" tooltip. **Lesson:** any setting a preset/world-state silently overrides
needs a UI lock + reason, or it's a confusing lie. There was already a precedent
in the codebase (the accessibility lock) — grep for one before inventing a new
pattern.

## 4. One helper, two call sites, keyed off an already-hashed field = determinism-safe override

`wildKeys` had a real trap: the canonical settings hash (via `apply_derived_rules`)
and the actual placement (via the placer's reads) MUST agree, or a seed hashes one
way and places another → share-string/reveal/corpus breakage. The clean solution
was `Settings_EffectiveSmallKeysMode(s)` — a single helper applied *identically* in
`apply_derived_rules` (hash) and at every placer + reachability-bridge read — both
keyed off `world_state`, so they can never desync. The fresh-eyes audit's main job
was confirming every raw `dungeon_small_keys_mode` read that affects placement was
routed through it. **Pattern:** for a computed/forced setting override, compute it
in ONE helper and apply it at BOTH the hash-normalization point and every
generation/reachability read; never mutate the stored field.

## 5. Two branches bumping kGeneratorVersion from the same base collide at merge

Main bumped `kGeneratorVersion` 50→51→52 (ROM-version scaffolding, then
swordless); this branch independently bumped 50→51 (Retro wildKeys). Both "51"s
meant different things. At merge the fix was to re-version the wildKeys bump
*above* main's history (→ 53), keep both of main's entries as history, then re-run
`bump_rando_corpus.py` against the merged binary and confirm ONLY the intended
(Retro) digests moved — every one of main's trick/swordless seeds stayed
byte-identical. **Lesson:** `kGeneratorVersion` is a scalar two branches will
collide on; expect it as a merge conflict, resolve by stacking (not picking a
side), and let the corpus dry-run prove the digest delta is exactly what you
intended.

## 6. The corpus proves placement; the playtest proves runtime — and they fail independently

This change was corpus-green (87/87) at every step, yet playtest found two real
problems the corpus structurally cannot see: the empty-chest-table dispatch
failure (§1) and the missing UI lock (§3). Neither touches placement output.
Reinforces the standing rule: a green corpus is necessary, not sufficient — budget
a real in-game run for any change with a runtime/dispatch/UI surface.
