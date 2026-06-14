# add-rando-hints — audit

Provenance + grounding notes for the hints change. Every claim below is grounded
against a file:line in this repo (or the sibling `../alttp_vt_randomizer/`
checkout). Per `CLAUDE.md` "Claim-grounding discipline": source over memory.

---

## Hint provenance (task 1.1)

The generation algorithm (`design.md §3`, the 6-step `HintService.applyHints`) was
grounded against the ALTTPR PHP `app/Services/HintService.php`. Pinned upstream,
verified present as a sibling checkout and `git rev-parse`'d on 2026-06-02:

| Upstream | Role | Pinned commit | Date |
|---|---|---|---|
| `../alttp_vt_randomizer/` (PHP) | `HintService.php` (15-tile algorithm), `Text.php` hint strings, `strings/hint.txt` joke pool | `219fcafd029dab597b8db400efafd8f56f8b4edb` | 2024-02-18 |

Note: the fork's implementation deliberately diverges from the 6-step PHP pool
(no GT-BigKey / Pegasus-Boots pins, no joke-hint fallback; a junk-filtered
Fisher-Yates over a telepathic-tile pool + Murahdahla on Triforce/Ganon hunt) —
see task 3.1's `done-differently` note. The pin records the *reference upstream
the design grounded against*, not a byte-for-byte port target. Determinism is
self-consistency (same `seed_u64` → same hints), per `design.md D6`.

---

## Vanilla NPC hint redirects

**Task 1.5 deliverable.** A subset of vanilla NPC dialogue spoils the *vanilla*
location of a specific named item. Under randomization the named item is shuffled
elsewhere, so the line becomes misleading. This section enumerates the candidate
NPC dialogue lines (the redirect targets) so §4's dynamic-dialogue-ID path can
later rewrite the location-referencing portion.

### Methodology

Full sweep of `assets/dialogue.txt` (398 entries, ids 1..397). Each entry was
read and classified by whether it names **a specific item from
`assets/rando/item_registry.yaml`** *together with* **a concrete vanilla location
phrase**. Entries that name only a location (no specific item), or only generic
flavor, are excluded per the task criteria. Telepathic-tile messages and the
fortune-teller NPC are tracked separately below (different dispatch paths).

### Confirmed redirect targets (specific item + vanilla location)

| dialogue_id | NPC | referenced_item (ITEM_*) | vanilla_location_phrase | sprite-handler file:line |
|---|---|---|---|---|
| 294 (0x126) | Aginah (desert hut) | `ITEM_BookOfMudora` | "It should be in the house of books in the village" → vanilla **Library** | `Sprite_Aginah` `src/sprite_main.c:6741` → `Sprite_ShowSolicitedMessage(k, 0x126)` `src/sprite_main.c:6747` |
| 350 (0x15E) | Dark-World "freak" (transformed treasure-hunter, DW around Tower of Hera) | `ITEM_MoonPearl` | "If I only had the Moon Pearl from the Tower Of Hera" → vanilla **Tower of Hera** | dispatched at `src/sprite_main.c:25532` (`int msg = link_item_moon_pearl & 1 ? 0x15e : 0x15d;`) — line is shown when Moon Pearl absent (the `0x15d`=349 branch is the post-pearl line; verify exact gate polarity at apply-time) |

The Aginah anchor (294) is the canonical case and is fully grounded: the sprite
handler, the message id (0x126 = 294), and the dialogue text all line up.

### Secondary candidates (named item + vanilla location, but weaker / story-beat)

These name a specific item *and* a location but are scripted one-time story
telepathy rather than a re-visitable hint NPC. They mislead under rando but are
lower-priority redirect targets; handler lines flagged for apply-time confirmation.

| dialogue_id | NPC / context | referenced_item | vanilla_location_phrase | handler note |
|---|---|---|---|---|
| 55 (0x37) | Sahasrahla telepathy (Master-Sword quest) | `ITEM_MoonPearl` | "find the Moon Pearl on Death Mountain" | shown via `Sprite_ShowMessageUnconditional(0x37)` `src/sprite_main.c:6783` and `Sprite_ShowSolicitedMessage(k, 0x37)` `src/sprite_main.c:6812` (Sahasrahla sprite region, adjacent to Aginah) |
| 159–161 (0x9F–0xA1) | Lost/old-man-on-the-mountain | `ITEM_MoonPearl` | "the Moon Pearl, which is in the tower on top of the mountain" (159) | handler **TBD at apply-time** — not grounded in this pass; grep the Death-Mountain old-man escort sprite + these msg ids before wiring |

### Excluded — location named but NO specific item (generic per task criteria)

- **56 (0x39)** Sahasrahla: "A helpful item is hidden in the cave on the east
  side of Lake Hylia." — names a location, but the item is "a helpful item"
  (not a registry item name). Excluded.
- **376 (0x178)**: "Check out the cave east of Lake Hylia. Strange and wonderful
  things live in it." — same Lake Hylia cave; "strange and wonderful things",
  no named item. Excluded.

### Excluded — telepathic tiles (already handled by the runtime intercept)

Per `specs/randomizer-hints/spec.md`, the shipped intercept
(`Rando_RenderHintMessage` / `Rando_IsHintTileMessage`) acts on the 15 tele-tile
message ids in `kHintTileMsgIds[]` (`0xB4,0xB5,0xB8..0xBB,0xBE..0xC6`; 0xB4 the
Eastern Palace tile IS included, 0xC7 the Chris Houlihan room is excluded) ONLY.
Those are NOT vanilla-NPC redirect targets — they are replaced wholesale by
generated tile hints. Notable members that *look* like item+location spoilers but
fall in (or adjacent to) this range:

- **182 (0xB6)** "An orb known as the Moon Pearl is in this tower" — edge case:
  0xB6 is *between* `0xB5` and `0xB8..0xBB`, so it is **not** in the intercepted
  set. If 182 renders in-game as a Moon-Pearl-location spoiler, it is an NPC-class
  line, not a tele-tile — re-evaluate at apply-time. (Flagged, not resolved.)
- 0xB8..0xBB + 0xBE..0xC6: Sahasrahla/Zelda tele-tile hints — handled. (0xBC,
  0xBD, and 0xC7 are NOT tiles and are not intercepted.)

### Excluded — fortune-teller NPC (distinct hint dispatcher, RNG flavor)

Dialogue 235–254 (the "Hocus pocus / Abracadabra alakazam" set, prefixed
`[Position 01]`) is served by `Sprite_FortuneTeller` /
`FortuneTeller_PerformPseudoScience` (`src/sprite_main.c:1024,1058`;
`SpritePrep_FortuneTeller` at `src/sprite_main.c:8141`). Several name a specific
item + location and so mislead under rando:

- **236 (0xEC)** Book of Mudora + "open a desert lock" (usage, not find-location)
- **240 (0xF0)** Moon Pearl + "in the mountain tower"
- **254 (0xFE)** Silver Arrows + "give Ganon his last moment"

These are a paid fortune-teller giving deliberate rotating hints, not a vanilla
NPC who happens to spoil a location. They are a separate redirect surface (the
fortune-teller could itself become a rando hint source); kept out of the
NPC-redirect table above. Flagged for a future design decision.

### Open follow-ups (not closed by this audit)

1. Ground the old-man-on-the-mountain handler (msgs 0x9F–0xA1) before wiring 159–161.
2. Decide 182 (0xB6) classification (NPC vs tele-tile) by observing in-game render.
3. Decide whether the fortune-teller (235–254) is an additional redirect surface.
4. The §4 dynamic-dialogue-ID rewrite itself (replacing the *location-referencing
   portion* with the randomized `LOC_*`) remains DEFERRED per
   `specs/randomizer-hints/spec.md` "Vanilla NPC hint redirects (DEFERRED)".

---

## Fresh-eyes audit — 2026-06-02 (task 9.3)

Read-only fresh-eyes pass over `rando_hints.{c,h}`, the hint-NPC dispatch in
`sprite_main.c`, and `rando_hints_panel.cpp`. **No HIGH.** Hint determinism, buffer
sizing, race-mode panel suppression, hints-off→vanilla fallback, and dynamic
dialogue-ID non-collision all verified sound. Findings (detail + ready patches below):

- **MED — Storyteller subtype-4 (msg `0x103`) shows vanilla text, not a hint.**
  `hint_npc_for_msg` (`rando_hints.c:489`) misses `0x103`. One-line fix
  (`|| msg_id == 0x103u`). VERIFY=PLAYTEST.
- **MED — tile-id set doc drift.** `kHintTileMsgIds[]` (`rando_hints.c:374`, 0xB4 in /
  0xC7 out) disagrees with the header docstring + `design.md:6` (0xB5..0xC7). Code is
  correct/internally-consistent; reconcile the docs. VERIFY=PLAYTEST (one 0xB4 read).
- **LOW — fortune-teller world discriminator** uses byte-truthiness vs the sprite's
  `>>6&1` (`rando_hints.c:495`); diverges only on the cross-world-exit value 1. VERIFY=PLAYTEST.
- **LOW — `Hints_SelfCheck`** reuses one table; doesn't exercise entry-order stability
  (order-stability separately verified to hold). VERIFY=BUILD.
- **LOW — Murahdahla `:` glyph** maps to none (harmless; spoiler-only today). VERIFY=PLAYTEST if NPC wired.

Disposition: 0 HIGH to address before archive; MED/LOW dispositions tracked in the
report. Task 12.3 (findings addressed) stays open pending the M2/M3/L3 decisions.
