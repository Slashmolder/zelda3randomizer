# Design: vanilla dialogue randomized-item hint redirects

## D1. Stay in the hint subsystem at two text-buffer stages

`Text_LoadCharacterBuffer()` gives `Rando_RenderHintMessage()` first refusal
before vanilla decoding. Generated hints and every noninteractive redirect use
that existing one-box path.

Stumpy's runtime `0xE5` is different: the message's `Choose` command controls
whether `Sprite_FluteKid_Stumpy` advances to its independently randomized
`LOC_Stumpy` grant. The pre-decode renderer therefore returns false for this row,
vanilla decoding preserves the command grammar, and
`Rando_RewriteInteractiveHintMessage()` replaces the finished US buffer with one
item-location page plus one choice page. This function remains in
`rando_hints.c`; `Rando_RewriteRewardDialogue()` does not own hint selection or
applicability.

Neither path reads, advances, or regenerates `g_hint_table`.

## D2. Correct ID model and generalized redirect table

`assets/dialogue.txt` prefixes each row with a one-based user-facing dialogue
number. Runtime uses the zero-based index, so user row `N` maps to runtime
`N - 1`. The table stores only collision-audited runtime indices.

Each row records its message, referenced item or location, source, discriminator,
and one of three kinds:

```c
typedef enum RandoDialogueHintRedirectKind {
  kDialogueHintRedirect_ItemLocation,
  kDialogueHintRedirect_LocationItem,
  kDialogueHintRedirect_ItemLocationChoice,
} RandoDialogueHintRedirectKind;

typedef struct RandoDialogueHintRedirect {
  uint16 message_id;
  uint16 item_id;
  uint16 location_id;
  const char *source_name;
  uint8 flags;
  uint8 kind;
} RandoDialogueHintRedirect;
```

The original five rows and Stumpy are dialogue-ID exclusive. Bumper Cave is
additionally gated on outdoors plus overworld screen `0x4A`, even though its
`0xA8` sign mapping is currently unique, because the sign's physical placement
provides the semantic location context. Unknown flags fail closed.

## D3. Pure applicability and two placement directions

A read-only resolver applies gates in this order:

1. message ID is a confirmed redirect;
2. active randomizer slot;
3. supported US/original dialogue grammar (`g_zenv.dialogue_flags == 0`);
4. recovered active settings exist;
5. `settings.hints == kHintsMode_On`;
6. row discriminator matches;
7. an active placement table contains the referenced item or location.

Item-to-location rows scan every active placement and choose the lowest numeric
location ID containing the target item. This pins duplicate-item customizer
behavior independently of table order. Bumper Cave instead finds the exact
`LOC_Bumper_Cave` row and uses its placed item; it never reverse-searches the
duplicated `ITEM_PieceOfHeart`. The location must exist before its item is used,
so `Placement_Lookup()`'s vanilla fallback cannot fabricate a false Heart Piece.

Missing items, missing locations, malformed tables, or failed gates preserve
vanilla decoding without dereferencing a missing row. `Rando_IsDynamicHintMessage`
uses this same resolver without rendering or mutating buffers.

## D4. Text, choice, and truncation safety

Noninteractive output uses the shared friendly item/location names and US-font
encoder. Item-to-location text is accepted only if the complete location fits
the three safe rows; aliases, deterministic compact names, and finally the
explicit numeric compatibility fallback prevent silent loss of the location.
The current-registry self-check rejects any row that needs that numeric fallback.

Bumper Cave first tries `Cape prize is <item>`, then `Prize is <item>`, then the
item name alone. Wild dungeon-item modes can place dungeon-specific keys, maps,
and compasses there, so this path uses qualified short names such as `Eastern
Small Key` and `PoD Small Key` instead of the generic labels used by generated
item-location hints. Self-check coverage walks every registry item, requires one
complete form, and pins distinct dungeon identities so the useful placed item is
never silently clipped or made ambiguous.

Stumpy composes:

```text
Flute is in <location>

Can you help?
> Yes
  No way
```

The first box uses the same all-location-safe encoder. A single `Waitkey` +
`Scroll` transition and explicit `0x74` top-row command precede the fixed
three-row choice page (Scroll otherwise leaves the VWF cursor on the bottom
row), which ends in the US `0x68` Choose command. The ordinary early hint
renderer refuses `0xE5`, so it cannot accidentally replace the prompt with a
noninteractive one-box message.

## D5. Non-US behavior

Both redirect stages implement only the US glyph/command set. When
`g_zenv.dialogue_flags != 0`, the shared resolver rejects the redirect and leaves
the selected locale's vanilla buffer byte-for-byte unchanged. This matches the
existing generated-hint and reward-dialogue support envelope.

## D6. Diagnostics

F12 keeps the generated hint-table dump and identifies a recognized surface as
`vanilla-dialogue redirect`. Its redirect line reports source, surface kind,
target item, resolved location, and one stable status:

- `active`
- `slot inactive`
- `unsupported locale`
- `settings unavailable`
- `hints off`
- `discriminator mismatch`
- `placement unavailable`
- `item absent`
- `location absent`

For an unresolved location-to-item sign the target is explicitly reported as
unresolved rather than repeating its vanilla item claim. Rendering,
fast-forward classification, and diagnostics share the same resolver.

## D7. Restored-slot behavior

No result is cached. Every render, interactive rewrite, predicate, and F12 dump
reads `Rando_GetActiveSettings()` and `Placement_GetActive()`, so save activation
and snapshot restoration resolve against the currently installed slot state.
