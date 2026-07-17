# Design: vanilla NPC randomized-item hint redirects

## D1. Stay in the pre-decode hint path

`Text_LoadCharacterBuffer()` already gives `Rando_RenderHintMessage()` first
refusal before vanilla dialogue decoding. The new layer extends that subsystem;
it does not use `Rando_RewriteRewardDialogue()`, which is for post-decode reward
and choice text.

Generated telepathic-tile and fork-NPC lookup runs unchanged. A fixed redirect is
checked separately and does not read, advance, or regenerate `g_hint_table`.

## D2. Correct ID model and redirect table

`assets/dialogue.txt` prefixes each row with a one-based user-facing dialogue
number. Runtime uses the zero-based index, so user row `N` maps to runtime
`N - 1`. The redirect table stores only verified runtime indices.

Each row contains:

```c
typedef struct RandoNpcHintRedirect {
  uint16 message_id;
  uint16 item_id;
  const char *source_name;
  uint8 flags;
} RandoNpcHintRedirect;
```

The current implemented rows are globally safe after the collision audit, so
their discriminator flags are zero. The flags field and an explicit
`discriminator mismatch` resolution state keep future shared IDs from being
treated as globally safe without a room/sprite/world predicate.

## D3. Pure applicability and deterministic resolution

A read-only resolver applies gates in this order:

1. message ID is a confirmed redirect;
2. active randomizer slot;
3. supported US/original dialogue grammar (`g_zenv.dialogue_flags == 0`);
4. recovered active settings exist;
5. `settings.hints == kHintsMode_On`;
6. row discriminator matches;
7. active placement table exists and contains the referenced item.

The placement lookup scans all active rows and chooses the lowest numeric
`location_id`. This pins duplicate-item customizer behavior independently of
table iteration order. A missing item or malformed active table falls through to
byte-identical vanilla decoding; it never dereferences a missing row or invents a
location.

`Rando_IsDynamicHintMessage(msg_id)` calls the same resolver without encoding or
mutating any buffer. `Text_ShouldFastForwardStoryDialog()` exempts a message only
when that predicate says it is actively serving a redirect, so hints-off and
vanilla `0x36` retain their prior fast-forward behavior.

## D4. Text and truncation safety

The renderer reuses the existing friendly item/location and US-font encoder
helpers. Dynamic text is accepted only if the complete string fits the three
safe pre-decoded rows. Location formatting first tries the existing friendly
area name, then explicit aliases for prominent long names, then a deterministic
initials-plus-final-token form for generated registry names (for example,
`LightWorld_DeathMountain_West RockL S03 P1970` becomes `LWDMWRLS P1970`). A
numeric location ID exists only as a forward-compatibility fail-safe; the
current-registry self-check rejects any row that reaches it.

The existing encoder's no-paging contract remains: at most three rows and one
terminator. Self-check coverage walks every current location for every redirected
item and asserts complete encoding without the numeric fallback.

## D5. Non-US behavior

The pre-decoded hint encoder implements the US glyph/command set. This change
deliberately aligns generated and dynamic hint interception with the existing
reward-dialogue safety policy: when `g_zenv.dialogue_flags != 0`, the hint renderer
returns false and preserves the selected locale's vanilla buffer. This is a
safety improvement over emitting US font codes into an incompatible buffer and is
covered by self-checks.

## D6. Diagnostics

F12 keeps the generated hint-table dump and adds one redirect line for the
current runtime message. It reports whether the message is a generated hint, a
vanilla-NPC redirect, or neither. For a recognized redirect it includes source,
target registry item, resolved location, and one stable status string:

- `active`
- `slot inactive`
- `unsupported locale`
- `settings unavailable`
- `hints off`
- `discriminator mismatch`
- `placement unavailable`
- `item absent`

The diagnostic uses the same read-only resolver as rendering and fast-forward
classification, preventing report/runtime drift.

## D7. Restored-slot behavior

No redirect result is cached. Every render and F12 dump reads
`Rando_GetActiveSettings()` and `Placement_GetActive()`, so save activation and
snapshot restoration resolve against the currently installed settings and
placement table.
