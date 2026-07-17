# Audit: vanilla dialogue fixed-item location claims

## Method and ID correction

The current extracted `assets/dialogue.txt` in the main checkout was swept in
full against `assets/rando/item_registry.yaml`. The source contains 397 numbered
rows. Its printed dialogue number is one-based while runtime
`dialogue_message_index` is zero-based:

```text
runtime_id = user_facing_dialogue_number - 1
```

The archived `2026-06-11-add-rando-hints/audit.md` mixed those domains. In
particular, its Aginah `294 (0x126)` row is actually user dialogue 294 and runtime
`0x125`; its bully gate polarity is also reversed. The archived file is not
modified; this audit supersedes those facts for the new change.

The sweep classified a row as a candidate only when it both names a current
registry item (or an unambiguous player-facing spelling of one) and claims a
concrete vanilla source/location. Acquisition/tutorial text, item-use advice,
generic location-only flavor, and messages whose fixed item cannot be represented
under current settings are recorded separately rather than treated as redirects.

## Implemented fixed-item redirects

| User dialogue | Runtime ID | Sprite/cutscene handler | Referenced item | Vanilla claim | Existing intercept | Shared-ID/discriminator audit |
|---:|---:|---|---|---|---|---|
| 52 | `0x33` | `Sprite_Sahasrahla` state 1 / `Sasha_Idle`, `src/sprite_main.c` | `ITEM_Prize_GreenPendant` | Pendant of Courage is in Eastern Palace | No | Dialogue assignment occurs only in Sahasrahla's map-marking state; no second message handler found. Globally safe. |
| 55 | `0x36` | `KillAghanim_Func7`, `src/misc.c` | `ITEM_MoonPearl` | Moon Pearl is on Death Mountain | No; story fast-forward currently includes it | Only the post-Agahnim cutscene assigns this dialogue ID, and only while Moon Pearl is missing. Globally safe. |
| 159 | `0x9E` | `Sprite_AD_OldMan`, `src/sprite_main.c` | `ITEM_MoonPearl` | Moon Pearl is in the tower on top of Death Mountain | No | First element of `kOldMountainManMsgs`; selected at home before Pearl. No other message handler found. Globally safe. |
| 294 | `0x125` | `Sprite_Aginah`, `src/sprite_main.c` | `ITEM_BookOfMudora` | Book is in the village house of books (Library) | No | Only Aginah's default/missing-Book branch assigns the message. Other raw `0x125` values in door/entrance data are not dialogue assignments. Globally safe. |
| 350 | `0x15D` | `Bully_HandleMessage`, `src/sprite_main.c` | `ITEM_MoonPearl` | Moon Pearl is in Tower of Hera | No | The bully uses `0x15D` without Pearl and adjacent `0x15E` with Pearl. No other message handler found. Globally safe. |

All five are noninteractive whole messages. None needs a room, sprite, or world
discriminator today; the redirect-table flags remain zero. The resolver still
has an explicit discriminator-mismatch status so a future shared ID cannot be
added without documenting and implementing its predicate.

## Other exact-claim candidates and disposition

| User dialogue | Runtime ID | Handler | Item/claim | Existing intercept | Shared/discriminator need | Disposition |
|---:|---:|---|---|---|---|---|
| 27 | `0x1A` | `Priest_Chillin` | Master Sword in Lost Woods | No | Priest-only | Excluded from fixed-item redirects: shown after all pendants but not keyed to an exact placeable registry tier; progressive swords have duplicate `ITEM_ProgressiveSword` rows. |
| 45 | `0x2C` | `Sprite_78_MrsSahasrahla` | Master Sword in forest | No | Mrs. Sahasrahla-only; message carries a choice and repeats on the choice result | Excluded: progressive-tier ambiguity and replacing the whole message would remove required choice control flow. |
| 49 | `0x30` | `Sasha_Idle` | Master Sword in Lost Woods | No | Sahasrahla-only | Excluded: missing-sword gate is confirmed, but `Master Sword` is not a stable placement item in progressive-sword mode. |
| 92 | `0x5B` | item-receipt text | Master Sword in Lost Woods | Runtime grant/receipt path, not an NPC hint surface | Receipt ID | Excluded: acquisition/progression text, not vanilla NPC location advice. |
| 132 | `0x83` | item-receipt text | Master Sword in Lost Woods | Runtime grant/receipt path, not an NPC hint surface | Receipt ID | Excluded: acquisition/progression text, not vanilla NPC location advice. |
| 182 | `0xB5` | `Dungeon_GetTeleMsg` tile dispatch | Moon Pearl in this tower | Yes, `kHintTileMsgIds[]` | Physical tile mapping already owns it | Already handled by the generated telepathic-tile interceptor. This corrects the archived audit's mistaken `0xB6` classification. |
| 230 | `0xE5` | Stumpy/Flute Boy (`Sprite_ShowSolicitedMessage`) | Flute buried in the grove | No | Stumpy-only; message carries the Yes/No choice that advances to the Shovel grant | Excluded from this whole-message layer: interception without a choice command breaks gameplay. Requires a separate interactive-dialogue design. |
| 240 | `0xEF` | `Sprite_FortuneTeller` reading range | Moon Pearl in mountain tower | Yes | Fortune Teller world discriminator already implemented | Already mapped to fork-generated hints by `is_fortune_reading_msg()`. |
| 295 | `0x126` | `Sprite_Aginah` | Master Sword in forest | No | Aginah-only | Excluded: progressive-tier ambiguity; adjacent `0x125` remains independently safe. |

## Explicit exclusions from the full sweep

- User dialogue 56 / runtime `0x37` says only “a helpful item” is east of Lake
  Hylia; no fixed registry item is named. The later Sahasrahla path uses the same
  runtime ID as Ice Rod advice, but the dialogue itself deliberately remains
  generic.
- User dialogue 169 / runtime `0xA8` mentions a Piece of Heart and Cape as a
  prerequisite/reward statement but does not claim a concrete location for the
  Piece of Heart.
- User dialogue 236 / runtime `0xEB` mentions using the Book on a desert lock;
  this is usage advice, not a location claim, and is already in the Fortune Teller
  reading range.
- User dialogue 254 / runtime `0xFD` mentions using Silver Arrows on Ganon; this is
  usage advice and is already in the Fortune Teller reading range.
- Master Sword lore, Silver Arrow combat advice, item-get messages, shop sales,
  tablet rewards, and NPC-owned reward prose were excluded when they do not tell
  the player where to find the named item. Reward surfaces remain owned by
  `Rando_RewriteRewardDialogue()` or the grant/confirmation path.

## Collision verification

Call-site searches covered direct `Sprite_Show*` arguments,
`dialogue_message_index` assignments, and the small message-ID arrays feeding
those functions. Raw numeric matches in generated door tables, entrance tables,
graphics/palette tables, and item receive codes are unrelated data domains and do
not constitute dialogue collisions. The five implemented IDs have one confirmed
dialogue producer each; adjacent states (`0x34`, `0x35`, `0x37`; `0x9F`, `0xA0`;
`0x124`, `0x126`; `0x15C`, `0x15E`) are intentionally not intercepted.
