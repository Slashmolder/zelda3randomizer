## Why

The two great-fairy "ponds" (`Sprite_WishPond3`) — Waterfall of Wishing (Light World) and the Pyramid Fairy (Dark World) — were wired under rando using the **vanilla "throw an item in to upgrade it" mechanic**. That mechanic is a poor fit for an item-shuffled world and produced a recurring bug class, found during playtest:

- **Upgrade lockout** — the toss is item-keyed (sword→Sword check, bow→Bow check) and the vanilla pond *consumes/upgrades* the tossed item, so owning a higher tier first (Gold sword, silver arrows) produced the "already maxed" reject branch and permanently locked the check out.
- **Deadlock** — requiring the *specific* sword/bow toss could strand a player when that item was placed past the fairy in logic.
- **Hard freeze** — the toss item-picker (`RenderText_FindYItem`) has no loop exit on an empty Y-inventory, freezing the game. A player can reach the pond (the Pyramid crack stays bombed open; the Waterfall needs only Flippers) before owning any throwable item, because the *logic* gates **placement** reachability, not **physical** reachability.
- **Wrong-icon / dead slots (Pyramid)** — the Pyramid modeled **four** locations (Sword/Bow Trade + Left/Right Chest). The runtime only granted Sword/Bow via the toss; Left/Right mapped to room-22 chests that **do not spawn** in the fork's vanilla-extracted rooms (the user confirmed both fairy rooms have no chests), so Left/Right were **dead placement slots** — an unbeatable-seed risk if progression landed there.

ALTTPR resolves all of this by **replacing each pond with two chests** (`app/Region/Standard/DarkWorld/NorthEast.php:39-41` adds Left/Right; the z3randomizer ROM asm `hooks.asm:805` inserts "2 chests, fat fairy room"). The fork cannot cheaply insert chest *objects* into the vanilla room layout, but it can make the fairy sprite hand the items over directly.

## What Changes

- **Drop the toss; grant on contact.** `Sprite_WishPond3` case 1, under `kFeatures1_RandomizerActive`, now grants the next un-collected of the pond's **two reach-only `Left`/`Right` checks** directly (like opening a chest) and idles — no item-picker, no toss, no consume, no fairy cutscene. Receivable items use the standard over-head `Link_ReceiveItem` (method 0, mirroring the freestanding-PoH grant site); direct-write items use the §7.6 confirmation cue. Off-rando the vanilla throw-in upgrade path is byte-for-byte unchanged (RAM-compare preserved). The case-6 rando dispatch blocks (both ponds) and the case-1 freeze-guard are removed — dead once rando never advances past `ai_state 1`.
- **Retire the Pyramid `Sword`/`Bow` Trade slots** (registry ids 210/211) from the placement pool — they were the vanilla-toss artifact, not ALTTPR's two chests. `Left`/`Right` (213/214), whose logic is already reach-only, become the Pyramid's two real checks. Deletion is id-keyed and safe: no `LOC_*` enum shift, `LOC__COUNT` unchanged, save/spoiler/lookups are gap-tolerant. The Waterfall already had exactly `Left`/`Right` — it reaches the identical end-state.
- **`kGeneratorVersion` 49 → 50** — two fewer placeable locations shifts every seed's open-count/junk-pad, so `placement_digest`/`sphere_digest` change for all corpus entries (regenerated). `settings_hash` / `kSettingsCanonicalLen` are unaffected (settings struct unchanged).

## Decisions and rationale (for the record)

- **Augment the fairy vs. real chests:** chose to augment the fairy sprite. Real chests need room *object/layout* surgery (ALTTPR's ROM patch) — high risk (room corruption, render glitches) for zero functional gain over a direct grant. The fairy already runs the interaction.
- **Drop the toss entirely (not just patch it):** every bug above is a symptom of the toss. Granting on contact makes the runtime requirement exactly "reach the pond," which **matches** the reach-only placement logic for Left/Right — closing the logic-vs-runtime gap in *both* directions (no throwable-item residual, no freeze). Patching the toss could only ever leave that residual.
- **Keep `Left`/`Right`, retire `Sword`/`Bow`:** Left/Right are ALTTPR's actual chest locations and their logic is already reach-only, so the no-toss runtime matches with **zero** logic edits. Keeping Sword/Bow would have required relaxing their `HasSword`/`CanShootArrows` gates and diverged from ALTTPR.
- **`SilverArrowUpgrade` vanilla code = `0x3b`, not `0x43`:** a critique pass caught that the old Bow code passed `0x43`, which writes `link_arrow_filler` (an arrow-refill placeholder), not the silver bow. `0x3b` sets `link_item_bow=3` (silver), matching `progressive_to_lttp`. Fixed for the Pyramid Left grant.

## Impact

- **Code:** `src/sprite_main.c` (`Sprite_WishPond3`).
- **Data:** `assets/rando/location_registry.yaml` (−2 entries), `logic_parts/43_darkworld.yaml` + `logic_parts/inverted/DarkWorld/NorthEast.yaml` (−2 predicate blocks each), `src/rando/rando.h` (`kGeneratorVersion`), `tests/rando_corpus/manifest.yaml` (corpus regen, 79 digests).
- **Capabilities:** `randomizer-placement` — MODIFIED "Item types receivable via dispatcher" (the Pyramid Fairy scenario).
- **Cross-change:** supersedes the Pyramid Fairy grant call-site enumerated by the in-flight `add-rando-confirmation-icons` change; the in-flight `add-rando-trick-logic-and-axes` change references the now-retired `LOC_Pyramid_Fairy_Sword` for swordless mode and must re-point (noted in that change).
- **Verification:** build + `--rando-selftest` green; corpus 79/79 against the regenerated baseline; all four CI guards green. **Playtest-pending (owner):** reach each pond, confirm it grants exactly TWO checks on contact (no toss, no freeze, no double-grant from a phantom chest), and that the Pyramid Left grant yields silver arrows (`0x3b`).
