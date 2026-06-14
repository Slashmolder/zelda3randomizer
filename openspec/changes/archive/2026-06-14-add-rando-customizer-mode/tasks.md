# Tasks — add-rando-customizer-mode

> **As-built status.** Headless generation, playable-slot generation, and the PC
> native-window customizer UI are implemented. `customizer_active` is canonical
> byte `[26]` bit1 (`kCustomizerAxis_Active`), sharing that byte with
> `enemy_shuffle` and `traps`; door shuffle owns byte `[27]` bits 0-1.
> Owner playtest of the in-window flow is complete as of 2026-06-14.
> Share-string encoding for `customizer_seed` (§6.4) is explicitly deferred to a
> future share-string-format change; current customizer seeds intentionally fall
> back to the v1 identity string.

## 1. Manifest format + parser

- [x] 1.1 `src/rando/customizer.{c,h}` — strict line-based YAML subset: `placements:` map (`<location>: <item>`). Comments (`#`), CRLF, blank lines handled; 1 MiB file cap.
- [x] 1.2 Normalized (lowercase, alphanumeric-only) name→id resolution over the codegen `kRandoLocationNames` / `kRandoItemNames` via the public `Rando_GetLocationName` / `Rando_GetItemName` accessors. Accepts the symbol form (`Eastern_Palace_Boss`) OR the human form (`Eastern Palace - Boss`). `logic_data.c` is codegen output — NOT hand-edited.
- [x] 1.3 `customizer_seed = sha256(manifest_bytes)[0..8]` computed at parse time (share-string `customizer_seed` per spec; share-string ENCODING still a stub).
- [x] 1.4 Parse-time rejections with line numbers: unknown section, malformed entry, unknown location/item, duplicate location key, pin overflow.
- [x] 1.5 `pool_overrides:` (`add:`/`remove:` item lists) — parsed (bracketed list or bare item), resolved, `add` rejects prize/event items. Applied in the §3c block remove-then-add before pins (`customizer_pool_add_one` mirrors `BuildItemPool`'s tier partition + dungeon prefix). `remove` is best-effort. Verified: `add:[ProgressiveSword]` yields 5 swords; deterministic; changes the placement vs no overrides.

## 2. Placer integration

- [x] 2.1 `place_assumed_fill_attempt` §3c block (`rando_placement.c`): pin each manifest location's slot in `placement_at[]` + `customizer_pool_remove_one` from `progression[]`/`junk[]` BEFORE the assumed-inventory seeding. Preserves the `dungeon_prog_n` invariant.
- [x] 2.2 The block is gated on `settings.customizer_active`: with no manifest the placer is byte-for-byte unchanged (regression corpus is the proof).
- [x] 2.3 Non-customizable location TYPES rejected (Prize/Medallion/Shop/ShopUpgrade/TakeAny) + already-vanilla-placed slots (vanilla-mode dungeon items, boss-heart drops). Hard errors surface via `Customizer_LastError()` + an early-return in `Place_AssumedFill` (no 256× retry on a deterministic config error).
- [x] 2.4 The runtime dispatcher is UNCHANGED — a customizer placement table is just `RandoPlacement` pairs.

## 3. Settings + canonical serialization

- [x] 3.1 `settings.customizer_active` at canonical byte `[26]` bit1 (`kCustomizerAxis_Active`, sharing the pad byte with enemy_shuffle's bit0; relocated at rebase from `[27]` bit0, which door-shuffle claimed). Default 0 ⇒ default settings_hash + the whole corpus byte-identical; `kSettingsCanonicalLen` stays 28 (no size-coupling cascade).
- [x] 3.2 `Settings_CanonicalDeserialize` reads `in[26]` bit1; round-trips (`Settings_SelfCheck` block: pack, only-[26]-moves, round-trip, bit0/bit1 coexistence); the deserializer forward-compat comment updated.
- [x] 3.3 `kGeneratorVersion` bumped to version-lock the new settings-hash-bearing bit + the manual-placement mode. Corpus manifest regenerated; customizer-off digests did not change.

## 4. CLI

- [x] 4.1 `--customizer=<path>` on `--generate-seed`: load + parse + install + `settings.customizer_active = 1`. Usage string updated.
- [x] 4.2 Reuses the existing `Goal_ShouldRefuse` completability gate + `--allow-broken-seed` bypass (un-completable hand-placement refused).
- [x] 4.3 Customizer hard errors reported distinctly from ordinary placement failure.

## 5. Verification

- [x] 5.1 `Customizer_SelfCheck` (in `Rando_RunAllSelfChecks`): name-table normalized-uniqueness (locations + items), resolution sanity (symbol == human form), parse round-trip + duplicate/unknown rejection.
- [x] 5.2 WSL `-Werror` clean build; `src/rando/customizer.c` registered in `Zelda3.vcxproj` (MSBuild enumerates — a new `.c` breaks the Windows build otherwise).
- [x] 5.3 Corpus byte-identical via `run_rando_corpus.py`.
- [x] 5.4 End-to-end: 3 pins resolve to their items in the spoiler; deterministic `placement_digest` across runs; validation rejects unknown/non-customizable/duplicate; uncompletable manifest refused. Example: `assets/rando/customizer.example.yaml`.
- [x] 5.5 Review pass completed: rejected pinning prize/event/virtual items, enforced `location_accepts_item` on each pin, and early-broke deterministic customizer errors in the CLI entrance-shuffle loop. Re-verified with corpus, invalid-pin rejections, and a valid in-dungeon-key pin.
- [x] 5.6 Deferred audit LOW dispositioned as non-blocking follow-ups: **L1** `rando_logic_gen.py` emits `item_<N>` for item-id gaps (resolvable placeholder) — latent, no current item gaps; **L3** an out-of-pool pin silently drops a junk item (documented; a per-attempt stderr note would be noisy).

## 6. Slot + UI slice (built at rebase pickup, 2026-06-10)

- [x] 6.1 Playable-slot path: `Rando_GenerateSlot` honors an installed manifest — the §3c pin block keys on `settings.customizer_active`, and the slot persists the full placement table + canonical settings blob, so the manifest is needed ONLY at generation time (reload reads the stored placement). Guards added: customizer+race_mode refused (the race reveal regenerates from (seed,settings) and cannot reproduce pins — refused on BOTH the slot path and the CLI); customizer_active with no installed manifest refused; `Customizer_LastError` surfaced through the slot error channel; the entrance-π and door-attempt retry loops early-break on a deterministic customizer error (mirrors CLI audit L2). **Headless test**: `--generate-slot --customizer=<path>` (new flag on the existing slot-path CI seam) — verified ok+roundtrip_ok, and the digest for (open/fast_ganon, seed 0x1, example manifest) is IDENTICAL to the `--generate-seed` path under MSVC (`26b667c3152b87f2…`).
- [x] 6.2 Native settings window: Randomizer → General → "Customizer" section — toggle + manifest path field + "Load manifest" button (text-entry per the spoiler-save precedent; SDL2 has no native file dialog), inline load error / pin-count + pool +N/-M summary, capped per-pin preview tree, Generate button relabeled "Generate from manifest & start new slot", `RandoWindowBridge_Validate` blocks Generate on (toggle on + no manifest) and (customizer + race mode). The manifest is session state: startup settings-restore clears a persisted `customizer_active` bit (main.c) so a stale flag can't block Generate after restart.
- [x] 6.3 `pool_overrides:` (add/remove) — DONE (see §1.5). Cardinality is handled by the existing fill (excess pool drops junk; shortfall uses the slot's vanilla item).
- [x] 6.4 Share-string encoding of `customizer_seed` (reproduce-by-manifest across users) — deferred by design. Current customizer seeds fall back to the v1 identity string because no shipped share-string format carries the manifest identity; a future share-string-format change must add that field before this can become reproducible-by-manifest across users.
- [x] 6.5 Owner playtest of the in-window flow (load manifest → generate → load slot → confirm a pinned item grants in-game). Owner-confirmed complete on 2026-06-14. The slot path has no end-to-end automated net beyond the §6.1 headless digest parity; local pre-playtest smoke used an isolated temp slot with `Link's House: Hookshot` generated and round-tripped (`26e16f55ea3c8ab8…`).
