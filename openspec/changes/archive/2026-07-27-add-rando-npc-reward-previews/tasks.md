# Tasks: add-rando-npc-reward-previews

This change depends on `enhance-rando-hints-v2` and
`add-configurable-hint-options`. The preview bit retains the historical additive
schema floor at generator 157, while the rebased integrated current output and
corpus are generator 160. Do not archive this change first, do not rebaseline
placement/sphere drift, and do not use automation to close owner gameplay.

## 1. Specification and source audit

- [x] 1.1 Create proposal, design, task ledger, and delta specs with an explicit
      owned source roster, independent-setting boundary, locale policy, and
      generator-156 compatibility contract.
- [x] 1.2 Map every owned source to its exact runtime message IDs, plus actor/
      room/entrance context wherever an ID is shared, placement location,
      choice commands, payment boundary, grant/check order, retry behavior,
      and checked replay.
- [x] 1.3 Reconcile the final as-built hooks against that audit; remove any
      ambiguous “vendor/etc.” ownership before archive.

## 2. Settings and lifecycle

- [x] 2.1 Add strict boolean `hint_npc_reward_reveal`, default false, canonical
      `[25]` bit 7, CLI/INI/native/share round-trip, validation, copy, equality,
      and exact bit-isolation self-tests.
- [x] 2.2 Keep the field outside clue normalization/profile application and
      plan/digest construction. Cover Hints Off plus previews On and every named
      profile preserving the independent value.
- [x] 2.3 Preserve canonical length 31 and share length 76 while using the
      integrated sidecar v14 exact 278-byte extension, snapshot hint TLV type
      11, and generator 160. Add no persistence growth for this option and
      exercise activation/save/load/export/warm/cold/slot-switch recovery
      without stale presentation.

## 3. Runtime and text

- [x] 3.1 Add one fail-closed active-placement/context resolver shared by the
      owned pre-commit preview surfaces; never use vanilla placement fallback.
- [x] 3.2 Implement paid previews for Bottle Merchant, King Zora, Blacksmith,
      Chest Game, and Digging Game with exact item and price before acceptance;
      reveal King Zora's item once, then use a price-only confirmation.
- [x] 3.3 Implement exact pre-grant/trade previews for Sahasrahla, Potion Shop,
      Magic Bat, Hobo, Old Man, Catfish, Purple Chest, Stumpy, and the next Fairy
      gift without inventing new decline branches.
- [x] 3.4 Add context-bound shopsanity clerk summaries of unchecked items/prices
      and Take-Any clerk summaries of currently live irreversible choices.
- [x] 3.5 Preserve existing post-acceptance exact rewrites unconditionally and
      preserve complete truthful generic/vanilla text on every failed gate.
- [x] 3.6 Add bounded multi-page Original/US composition that preserves prices,
      exact qualified item identity, cursor-safe choice/control commands, and
      Stumpy's variable-row fact/choice stream with commit-after-complete
      success. Leave German/French buffers byte-identical.
- [x] 3.7 Verify shared preview preparation is read-only. Exercise
      representative paid and one-time acceptance paths plus grant-presentation
      and grant-transaction handler/order/retry self-checks so successful
      transactions retain one charge/grant/check and failure, checked, and
      same-frame replay paths do not duplicate them.

## 4. UI, spoiler, and compatibility tooling

- [x] 4.1 Label the native selector “Clue profile”; add an independent pending
      checkbox and active-slot status with “Original/US dialogue only.”
- [x] 4.2 Add the independent in-game advanced row, cursor/navigation/help
      coverage, and named-profile preservation.
- [x] 4.3 Add schema-gated current JSON/text spoiler reporting decoded from
      canonical `[25]` bit 7; omit both fields for generator 156.
- [x] 4.4 Extend `check_hint_spoiler.py` with Hints Off plus previews On,
      JSON/text parity, exact canonical bit isolation, settings-identity
      difference, and placement/sphere/plan/row orthogonality.
- [x] 4.5 Extend `check_hint_legacy_reveal.py` to require legacy JSON/text
      omission and reject a CRC-correct generation-156 `[25]` bit-7 mutation as
      `SettingsCorrupt` without output/scratch.
- [x] 4.6 Document the option, exact source roster, independence, transaction
      behavior, locale envelope, spoiler output, and legacy reveal fence.

## 5. Automated validation

- [x] 5.1 Cover shared-resolver disabled/enabled, missing-placement, and
      checked-replay gates, plus representative source-specific contexts and
      collision canaries for shared shop/Take-Any message IDs. Keep race/schema
      compatibility in the dedicated reveal validator.
- [x] 5.2 Walk every registered item ID through the shared reward-page and
      inventory formatters, including qualified dungeon items, prizes,
      progressives, traps, souls, bottles, Nothing, and Rupoor. Exercise
      representative seller, free/trade, Fairy, shop, and Take-Any templates,
      slot positions, prices, and worst-case summaries rather than claiming the
      full item-by-source Cartesian product.
- [x] 5.3 Exercise representative paid and one-time source flows for decline,
      insufficient funds, acceptance, checked replay, and repeated input. Use
      grant-presentation and grant-transaction self-checks for handler order and
      retryable failure, and save/snapshot/slot validators for lifecycle
      recovery without stale disclosure.
- [x] 5.4 Run config/dialogue/UI/full self-tests, spoiler and legacy validators,
      slot-path/snapshot validators, source/codegen/version guards, strict
      OpenSpec, `git diff --check`, MSVC Release, and GCC/Clang `-Werror`.
- [x] 5.5 Run the full generator-160 corpus. Require every existing placement
      and sphere digest to remain unchanged; add only a focused preview-on
      composition row if needed for public boundary coverage.
- [x] 5.6 Complete independent fresh-eyes review of source inventory, exact text
      fit, context discrimination, transaction order, locale behavior, settings
      lifecycle, spoiler/race compatibility, and spec/as-built consistency.

## 6. Required owner gameplay and closeout

- [x] 6.1 Complete the owner's focused gameplay pass on the highest-risk paid
      previews, including Bottle Merchant item composition/cursor redraw and
      King Zora's staged offer, then obtain explicit acceptance after the
      reported fixes.
- [x] 6.2 Record owner acceptance after the focused repeated-navigation and
      offer/confirmation staging pass. Treat unreported checked/repeat and
      transaction variants as accepted residual manual risk covered by
      automated handler tests.
- [x] 6.3 Obtain owner acceptance of the focused Original/US gameplay pass after
      the Bottle Merchant graphical/cursor and King Zora duplicate-text fixes;
      retain Hints-Off-plus-previews, race, and German/French byte-identity
      guarantees in automated validation.
- [x] 6.4 Reconcile source, tests, docs, delta specs, and task evidence. Archive
      only after dependency order and explicit owner gameplay acceptance;
      merge/push remain separately authorized.
