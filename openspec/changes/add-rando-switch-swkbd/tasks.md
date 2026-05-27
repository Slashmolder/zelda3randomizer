## 1. Apply-time pre-flight

- [ ] 1.1 Verify current DevKitPro version + libnx version in `src/platform/switch/Makefile`. Record in `audit.md §"DevKitPro version pin"`.
- [ ] 1.2 Skim libnx's `<switch.h>` swkbd headers — confirm `swkbdCreate` / `swkbdShow` / `swkbdInputText` exist and the SDK version's API shape.
- [ ] 1.3 Check libnx documentation for the swkbd character-set restriction options available in this SDK version.
- [ ] 1.4 Locate `RandoTextField`'s "begin input" hook in `src/rando/rando_textfield.c`. Identify where to insert the Switch-only branch.

## 2. New libnx wrapper

- [ ] 2.1 Create `src/platform/switch/swkbd.h`. Declare `bool Switch_OpenSwkbd(const char *prompt, const char *initial, char *out, size_t out_max_len);`.
- [ ] 2.2 Create `src/platform/switch/swkbd.c`. Implement:
  - Call `swkbdCreate` with `SwkbdType_Normal`.
  - Set the prompt + initial string + buffer size.
  - Call `swkbdShow`.
  - Read result via `swkbdInputText` (or whatever current SDK provides).
  - Post-filter for base32 charset (lowercase a-z, digits 2-7).
  - Return true on confirm, false on cancel.
- [ ] 2.3 Add the new file to `src/platform/switch/Makefile`.

## 3. Wire into RandoTextField

- [ ] 3.1 In `src/rando/rando_textfield.c`, identify the "begin input" hook. On Switch, branch to `Switch_OpenSwkbd` instead of waiting for SDL_TEXTINPUT.
- [ ] 3.2 Use `#ifdef __SWITCH__` or equivalent platform guard.
- [ ] 3.3 PC builds (no `__SWITCH__`): code path unchanged.
- [ ] 3.4 Cancel-preserves-field-value behavior: if swkbd returns false (cancel), the widget's current value is NOT modified.

## 4. Base32 post-filter

- [ ] 4.1 Implement `Switch_FilterBase32(char *buf)` — in-place removes non-base32 characters.
- [ ] 4.2 Invoke in the wrapper after swkbd returns and before passing to widget.
- [ ] 4.3 Confirm the share-string validator (Phase A) catches an empty or all-filtered-out result.

## 5. Build wiring

- [ ] 5.1 Confirm `src/platform/switch/Makefile` builds with the new file.
- [ ] 5.2 Confirm `Makefile` and `Zelda3.vcxproj` are NOT affected (PC builds don't include the swkbd wrapper).
- [ ] 5.3 Run `assets/scripts/check_codegen_wiring.py` — should pass unchanged.

## 6. Switch dev-unit smoke

- [ ] 6.1 Manual: build Switch image with the wrapper enabled.
- [ ] 6.2 Run on dev unit; navigate to share-string field in settings screen.
- [ ] 6.3 Trigger swkbd; type a known-good share string; confirm; verify field is populated.
- [ ] 6.4 Trigger swkbd; type a known-good share string; cancel; verify field is unchanged.
- [ ] 6.5 Trigger swkbd; type non-base32 characters; confirm; verify post-filter strips them.
- [ ] 6.6 Verify game audio behavior during swkbd modal (per design.md open question — briefly pause audio if needed).

## 7. Determinism + audit

- [ ] 7.1 `assets/scripts/check_determinism.py` — no new `rand`/`time` symbols (the wrapper doesn't seed RNG).
- [ ] 7.2 `assets/scripts/check_audit_guard.py` — no new tracked-cell writes.
- [ ] 7.3 No `kGeneratorVersion` bump — this change is UX-only.

## 8. Documentation

- [ ] 8.1 Update `README.md` Switch section: mention swkbd is now native.
- [ ] 8.2 Update `docs/randomizer_phase_b.md` §9.1c status: mark complete.

## 9. Archive readiness

- [ ] 9.1 Switch dev-unit smoke passes per §6.
- [ ] 9.2 PC builds (Linux + macOS + Windows) still pass CI unchanged.
- [ ] 9.3 `openspec archive add-rando-switch-swkbd` runs cleanly; spec delta merges into `openspec/specs/randomizer-ui/spec.md`.
