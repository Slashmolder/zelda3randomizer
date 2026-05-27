## Why

Phase A's `randomizer-ui` spec requires text-input infrastructure for the settings-screen share-string field and on-screen alphabet picker (`randomizer-ui/spec.md:3-23`). The desktop side (PC builds with hardware keyboard) is wired via SDL2's `SDL_TEXTINPUT` events. The Switch side was specified as a libnx `swkbdCreate` / `swkbdShow` / `swkbdInputText` wrapper routed into the existing `RandoTextField` widget — but **the wrapper was re-scoped to Phase B per the §12.3a/§12.3b Switch-manual-gate precedent** (Switch builds are a release-cut manual check, not per-PR CI).

Phase A1 status confirms: "§9.1c Switch software-keyboard (libnx swkbd) wrapper" is still open per `audit_phase_a1.md:86`. Switch builds today can't type into the share-string field; the picker-only fallback is the workaround.

This change ships the wrapper. Standalone (Slice 8 in the chunking plan) because:
- It has zero file overlap with the other Phase B slices.
- It only affects the Switch build; CI cannot validate it (Switch is manual-gated per `tasks.md §12.3a/§12.3b`).
- Bundling it into another slice would force that slice to also wait for Switch verification.

## What Changes

- **New libnx wrapper**: `src/platform/switch/swkbd.c` + matching header. Wraps `swkbdCreate` / `swkbdShow` / `swkbdInputText` from libnx into a synchronous "open keyboard, return the entered string" call that fits the existing `RandoTextField` consumer model.
- **`RandoTextField` integration**: when running on the Switch build, the text-field's "begin input" hook invokes the libnx wrapper instead of relying on SDL_TEXTINPUT events (SDL doesn't fire those on Switch).
- **Settings-screen share-string entry**: the share-string field becomes typeable on Switch — same UX as PC.
- **Backspace / clear**: libnx swkbd handles its own backspace UI; the wrapper returns the final string after the player confirms.
- **Cancel handling**: if the player cancels the swkbd, the `RandoTextField` is left unchanged.
- **No code change on the PC side.** SDL_TEXTINPUT continues to drive PC builds unchanged.
- **No `kGeneratorVersion` bump.** Pure UI; placement output unaffected.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `randomizer-ui`: ADDED Requirement for the Switch text-input wrapper. The text-input infrastructure requirement at `randomizer-ui/spec.md:3` already requires Switch parity; this change adds the implementation-side SHALL that completes the contract.

## Impact

- **New code**: `src/platform/switch/swkbd.c` + `src/platform/switch/swkbd.h` (~100-200 lines). Pure libnx call wrapping.
- **Modified code**: `src/rando/rando_textfield.c` — platform-gated branch for the "begin input" hook (uses the libnx wrapper on Switch; SDL_TEXTINPUT on PC).
- **Build**: `src/platform/switch/Makefile` adds the new file. PC builds unaffected.
- **Effort**: **3-5 days of focused work.** Small wrapper; tested manually on a Switch dev unit.
- **Regression risk**: Switch-only; PC builds unchanged. No corpus regen needed.
- **Manual gate**: Per `add-randomizer-support / tasks.md §12.3b`, Switch build verification is a release-cut manual check. This change can only be archived after a Switch dev-unit smoke confirms the swkbd flow works end-to-end.

## Status (stub)

This is a **proposal-only stub**. Specs deltas and `tasks.md` are deferred to `/openspec-explore` + `/openspec-propose` at apply-time.

**Stub-only because**: the exact libnx swkbd API call sequence (single vs. multi-step input, character-set restrictions for base32 share strings) needs apply-time verification against current DevKitPro / libnx; some Switch SDK versions have changed swkbd behavior.

Read the [README.md](README.md) for the stub's status.
