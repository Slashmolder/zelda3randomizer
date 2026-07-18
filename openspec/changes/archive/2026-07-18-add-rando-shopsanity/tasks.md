# Tasks: add-rando-shopsanity

Phases match design.md's Migration Plan. Every phase ends buildable (MSVC +
WSL gcc `-Werror`) with the corpus green at the then-current
`kGeneratorVersion`. Standing traps that apply throughout: `make clean` after
ANY header edit (no header-dep tracking in the Makefile); re-grep
`kGeneratorVersion` + the corpus manifest version from LIVE `main`
immediately before branching/committing (keyrings lands first and moves
both); stage explicit paths, never `git add -A`; read dumps/logs via
absolute worktree paths.

## 0. Sequencing gate

- [x] 0.1 Confirm `add-rando-key-rings-skeleton-key` is merged to `main`;
      branch `feature/rando-shopsanity` off post-keyrings `main`. Verify
      `kSettingsCanonicalLen == 31` and byte [30] assignments match the
      randomizer-core delta's sequencing note (fix the delta now if not).

## 1. Settings axis (inert)

- [x] 1.1 `rando_settings.h`: `uint8 shopsanity;` field + axis enum
      (`kShopsanityAxis_Shift = 4`, canonical byte [29] bit 4); update the
      byte-layout comment block.
- [x] 1.2 `rando_settings.c`: default `false`; serialize/deserialize byte
      [29] bit 4 (shrink the refused-undefined mask by exactly this bit);
      range/validation unchanged elsewhere; CSV key `shopsanity`
      (`true`/`false`, hard error otherwise, `MARK_SEEN` duplicate guard);
      NO derived-rule coupling.
- [x] 1.3 `rando_window.cpp`: "Shopsanity" checkbox in the Shuffles block +
      tooltip (1-2 durable player facts, per the tooltip-brevity rule); no
      disable coupling.
- [x] 1.4 Verify inertness: default `settings_hash` unchanged vs. `main`
      (no canonical length change), `--rando-selftest` green, corpus green
      with NO regen at this phase.

## 2. Logic regions (the scoped Retro movement)

- [x] 2.1 Author `region:` bindings + entry predicates for the 9 shops in
      `assets/rando/logic.yaml` — region ids verified against the live
      region graph (Q5: source-check LW Death Mountain's door region, don't
      guess); dark-world shops inherit their region's Pearl/world-state
      gates.
- [x] 2.2 `logic_parts/inverted/**` overrides where inverted world
      assignment differs. FIRST AS-BUILT CLAIM ("none needed") was WRONG —
      the fresh-eyes audit (HIGH-2/MED-1) caught it: upstream re-authors
      all three callback shops for Inverted, and the fork's inverted graph
      reaches LW Death Mountain East as a bunny, so the base
      CanBombThings() entry could strand progression (even the Pearl)
      behind the bombable wall. Fixed by
      logic_parts/inverted/Shops.yaml (LW DM: CanBombThings+MoonPearl per
      the Paradox-sibling pattern; Outcasts: Hammer; DW Potion: the
      Inverted traversal disjunction, trick arms dropped per house style).
      Last-wins guard held (ids exist only in 50_shops.yaml + the inverted
      override file, which feeds the world-state override table).
- [x] 2.3 `rando_logic_gen.py`: remove `Shop` from `REGION_OPTIONAL_TYPES`
      (keep `ShopUpgrade`/`TakeAny`); codegen now errors on a region-less
      Shop row.
- [x] 2.4 `kGeneratorVersion` +1 (single bump for the whole change lands
      here). `make clean` (header edit).
- [x] 2.5 Corpus regen + 3-way diff vs fresh-built unmodified `main`
      (`rm src/rando/logic_data.c` on both sides first): ONLY Retro rows may
      move; explain any Retro `placement_digest` (not just sphere) movement
      before accepting; any Open/Standard/Inverted movement = regression.
      Restore manifest CRLF before committing.

## 3. Placement + prices + spoiler

- [x] 3.1 `rando_placement.c`: shared `shop_slots_open(settings)` predicate;
      bypass the `LOCTYPE_Shop` world-state filter and the
      `location_is_prepinned` Shop arm when true; extend the skip-triple
      (open-location loop, `BuildItemPool` junk-pad target,
      `Placement_SelfCheck` expected counts) by the same predicate.
- [x] 3.2 Price stream: `ShopPrice(seed, loc_id)` salted-RNG helper
      (multiples of 5 in [10,250]), shared between generation and runtime
      (single function, one TU — no duplicated formula).
- [x] 3.3 `rando_spoiler.c`: `SpoilerShopRow` gains `price`; JSON `shops[]`
      `"price"` field (omit on identity-placed ShopUpgrade rows); text
      `Shops:` prints `(NN rupees)`; confirm sections emit for non-Retro
      shopsanity seeds and axis-off non-Retro spoilers are byte-identical.
- [x] 3.4 `--rando-selftest`: price-determinism vector; shop-slot open/pin
      accounting per world state × axis; customizer pin accept/refuse per
      the axis.
- [x] 3.5 Customizer: verify manifest pinning of ids 237–263 works with the
      axis on and refuses with it off (clear error). DONE end-to-end:
      the pin validator's Shop rejection is conditional on the axis, and
      `Customizer_PlacementSelfCheck` now installs a real manifest pinning
      "Dark World Forest Shop - 0" = Hookshot — accepted AND honored with
      shopsanity on; refused with the non-customizable-type message under
      Retro with the axis off.
- [x] 3.6 New corpus rows: open/standard/inverted/retro × shopsanity=true;
      shopsanity + cave-entrance shuffle; completionist + shopsanity
      (locations tier). Regen (absolute `--binary`; manifest version
      pre-bump check) + 3-way diff.

## 4. Runtime purchase + draw

- [x] 4.1 `sprite_main.c` spawn gate. AS-BUILT (design refined during
      implementation — simpler than a new spawn kind): spawns stay vanilla;
      the seven kind handlers get a shared per-frame front-end
      (`ShopItem_ShopsanityCheckSlot`) and `SpriteDraw_ShopItem` a check
      branch, both keyed on `Rando_ShopSlotCheckInfo` (re-verified every
      frame, pos 1..3 only — never the Retro genericKey 4th column; the
      `room_only` quirk lives untouched in `shop_lookup`). Take-Any
      redirect + genericKey short-circuits untouched. Behavior matches the
      spec (unchecked = placed item at derived price; checked/off =
      vanilla); the spec delta's "spawn as a check slot" wording is
      reconciled at 5.2.
- [x] 4.2 Check-slot handler in `Sprite_BB_Shopkeeper`: A-press →
      re-verify unchecked (double-purchase guard) →
      `ShopItem_HandleCost(derived_price)` (vanilla can't-afford feedback on
      refusal) → `Rando_ShopDispatch` → `Rando_ReceiveOrConfirm` → despawn.
      ALL placement classes route through the dispatch chain — audit that no
      branch falls back to a vanilla item spawn (the Digging-Game
      consolation-fallback class).
- [x] 4.3 `SpriteDraw_ShopItem`: placed-item icon via the SHARED field-item
      resolver (no shop-local item→icon table); generic merchandise fallback
      icon; dynamic 3-digit price draw at the vanilla price-row offset;
      explicit OAM region allocation (byte-budget rule) — verify no clobber
      in all 9 shop layouts.
- [ ] 4.4 Playtest matrix (the only net for this phase): buy progression /
      junk / direct-grant classes; can't-afford refusal; restock item+price
      after purchase; save/reload mid-shop + snapshot cold-replay of a
      purchased save; Retro + shopsanity (take-any + genericKey slots
      coexist); Inverted DW shop; Standard pre/post-escape reachability.
- [x] 4.5 `docs/randomizer.md`: settings-table row + a Shopsanity section
      (semantics, prices, restock, out-of-scope list).

## 5. Close-out

- [ ] 5.1 Fresh-eyes independent review (standing cadence — self-contained
      prompt, `git log --oneline <baseline>..HEAD`, ask for NEW findings,
      cap response length); fix and re-verify.
- [ ] 5.2 Reconcile all three spec deltas against as-built source
      (especially the randomizer-core table vs. post-keyrings reality) —
      deltas rot; a checked tasks box is not a spec update.
- [ ] 5.3 Re-run full validation yourself (never trust a sub-agent's
      reported result): MSVC + WSL gcc `-Werror` clean builds, corpus green,
      `--rando-selftest`, CI guard scripts.
- [ ] 5.4 `openspec archive add-rando-shopsanity --yes` as the last branch
      commit; squash-merge to `main`.
