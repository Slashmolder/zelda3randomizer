## ADDED Requirements

### Requirement: Switch software-keyboard wrapper

The Switch build SHALL implement text-field input via a libnx software-keyboard wrapper (`swkbdCreate` / `swkbdShow` / `swkbdInputText`). When the `RandoTextField` widget's "begin input" hook fires on the Switch build, the wrapper SHALL be invoked to open the swkbd, await the player's input, and return the entered string to the widget.

The wrapper SHALL be synchronous: it blocks the main thread until the player confirms or cancels. On confirmation, the entered string SHALL be passed to the widget. On cancellation, the widget's current value SHALL be unchanged.

The character set SHALL be restricted (or post-filtered) to the base32 alphabet used by the share-string format (per `randomizer-core / Share string`). Non-base32 characters SHALL be rejected silently in the post-filter or via swkbd's input-restriction API where available.

PC builds (SDL2 with hardware keyboard) SHALL continue to use SDL_TEXTINPUT events unchanged; the Switch wrapper SHALL NOT affect PC code paths.

> **Stub status**: exact libnx API call sequence + character-set restriction mechanism deferred to apply-time. Some libnx versions have changed swkbd behavior; verification against current DevKitPro is needed.

#### Scenario: Switch text-field opens swkbd on input
- **WHEN** the player navigates to the share-string field on Switch and triggers input
- **THEN** the libnx swkbd opens; the player types via the on-screen keyboard; on confirmation, the entered string populates the text-field

#### Scenario: Cancel preserves field value
- **WHEN** the player opens the swkbd and cancels without confirming
- **THEN** the text-field's current value is preserved; no draw glitches; game flow resumes

#### Scenario: PC text-field unchanged
- **WHEN** the binary is a PC build (SDL2 hardware keyboard available)
- **THEN** SDL_TEXTINPUT events drive the text-field; the libnx wrapper code path is not invoked

#### Scenario: Non-base32 characters rejected
- **WHEN** the player enters characters outside the base32 alphabet (e.g., '8', '9', 'o', 'i', etc.)
- **THEN** the characters are silently dropped before reaching the text-field; the validator's standard "invalid share string" path runs on confirm if the result is invalid
