## Context

Phase A's text-input infrastructure (`src/rando/rando_textfield.c`) drives the settings-screen share-string field on PC builds via SDL_TEXTINPUT events. On Switch, SDL_TEXTINPUT does NOT fire — the libnx text-input model is a modal software-keyboard (`swkbdCreate` / `swkbdShow` / `swkbdInputText`) that takes over the screen, blocks until the user confirms, and returns the entered string.

Phase A re-scoped §9.1c to Phase B per `tasks.md §9.1c` + the §12.3a/§12.3b Switch-manual-gate precedent. Currently Switch builds fall back to the on-screen alphabet picker for share-string entry, which works but is slow.

## Goals / Non-Goals

**Goals**:
- libnx swkbd wrapper at `src/platform/switch/swkbd.{c,h}`.
- `RandoTextField` widget routes through the wrapper on Switch builds.
- Base32-character-set restriction (only a-z + 2-7 are valid base32 characters).
- Cancel preserves field value.

**Non-Goals**:
- PC code path changes (zero — SDL_TEXTINPUT unchanged).
- Other Switch text-input sites (Phase A had ONE: share-string field; future Phase D customizer-manifest picker is its own change).
- swkbd theming / customization (use libnx defaults).

## Decisions

### D1: Synchronous vs. async wrapper

libnx's `swkbdShow` is blocking; the function returns after the user confirms or cancels. The wrapper exposes this as a synchronous call: `Switch_OpenSwkbd(prompt, initial, out, out_max_len) -> bool` returning true on confirm, false on cancel.

**Decision**: synchronous. Matches the `RandoTextField` consumer model (the widget's "begin input" hook is called synchronously from the input dispatcher).

### D2: Character-set restriction approach

libnx's swkbd supports input-restriction modes (`SwkbdType_Normal`, `SwkbdType_NumPad`, etc.). For base32, no built-in mode fits.

**Options**:
- (a) Use `SwkbdType_Normal` + post-filter rejected characters before returning.
- (b) Use libnx's character whitelist (if available in current SDK version) to reject at-input-time.

**Decision**: **(a) post-filter**. Simpler; SDK-version-portable; the share-string validator already runs after input so an additional filter is cheap.

Rejected characters are silently dropped; if the entire input is filtered out, the field is set to empty and the validator's standard "invalid share string" path runs on confirm.

### D3: Cancel behavior

When the user cancels swkbd, the `RandoTextField`'s current value SHALL be preserved (NOT cleared). This matches PC SDL_TEXTINPUT behavior where Escape returns control without committing.

**Decision**: cancel preserves; confirm commits.

### D4: Initial value seeding

libnx swkbd supports an initial string in the input buffer. The wrapper passes the `RandoTextField`'s current value as the initial; the user can edit or replace it.

**Decision**: pass current value as initial. UX standard.

## Risks / Trade-offs

| Risk | Mitigation |
|---|---|
| libnx SDK API changes across DevKitPro versions | Pin DevKitPro version in `audit.md`; verify the wrapper at apply-time against current SDK |
| Switch-only code path means PC CI can't verify | Per `tasks.md §12.3a/§12.3b`, Switch is a release-cut manual gate. Manual Switch dev-unit smoke before archive |
| swkbd cancel without confirm could orphan UI state | Wrapper's cancel return is explicit; widget code handles |

## Migration Plan

No migration. Switch builds gain swkbd support; PC builds unchanged.

## Open Questions

1. Current DevKitPro version's swkbd API stability — re-verify at apply-time.
2. Base32 character whitelist via `SwkbdInline` vs. post-filter — pick whichever works in current SDK.
3. Does swkbd preserve game audio playback while modal? If not, briefly pause audio — apply-time question.
