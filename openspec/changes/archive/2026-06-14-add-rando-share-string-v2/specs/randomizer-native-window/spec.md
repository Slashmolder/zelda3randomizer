## MODIFIED Requirements

### Requirement: Share-string copy and paste via OS clipboard

The settings window SHALL provide a "Copy share string" button that writes the current share string to the OS clipboard via `SDL_SetClipboardText`, and a "Paste share string" button that reads via `SDL_GetClipboardText` and parses through the shared share-string decoder (`randomizer-core / Share-string format`).

**Copy** SHALL emit the **v2** string encoding the current widget values (`Settings_CanonicalSerialize(pending)`) plus the current seed — except when `pending.customizer_active` is set, where copy SHALL emit the **v1** string (seed + hash; customizer placements depend on a local manifest file that no share string can carry until the deferred `customizer_seed` encoding lands — `add-rando-customizer-mode` tasks §6.4). The live share-string display SHALL show the same string copy would emit.

When "Randomize seed each generate" is enabled, the pre-generate Copy button SHALL be disabled because the seed is not chosen until Generate. After a successful generation, the result popup's Copy button SHALL copy the generated seed's v2 exchange string (or v1 for customizer), and unchecking randomize-each-generate / pasting / typing a seed SHALL re-enable the regular Copy button for that pinned seed.

**Paste of a v2 string** SHALL restore ALL settings widgets via `Settings_CanonicalDeserialize`, adopt the decoded `seed_u64`, and pin the seed (clear the randomize-each-generate flag), so that pressing Generate reproduces the sharer's seed. Restored settings SHALL pass the window's cross-field validation (`RandoWindowBridge_Validate`); a validation failure SHALL refuse the paste with the validator's message and leave all widgets untouched. A v2 string whose canonical `customizer_active` bit is set SHALL be refused with a message naming the manifest as the reason (no adoption). A v2 string whose embedded `generator_version` differs from the binary's `kGeneratorVersion` SHALL still restore (settings layout is append-only) but SHALL surface a visible warning that Generate may produce a different placement than the sharer's; a v2 string refused by the decoder ("newer version" `settings_len`) SHALL leave widgets untouched.

**Paste of a v1 string** SHALL keep the legacy behavior: adopt the seed, pin it, and show the settings-mismatch warning when the current settings hash differs from the embedded hash (v1 cannot restore settings).

#### Scenario: Copy writes the v2 share string
- **WHEN** the user clicks Copy share string with `customizer_active` off and the seed pinned
- **THEN** the OS clipboard contains the v2 token encoding the current widget values and seed, identical to the displayed share string

#### Scenario: Auto-randomized seed copies from the result popup
- **WHEN** "Randomize seed each generate" is enabled before generation
- **THEN** the pre-generate Copy button is disabled, and after Generate succeeds the result popup's Copy button copies the generated seed's share string

#### Scenario: v2 paste restores settings and seed losslessly
- **WHEN** the user pastes a valid v2 share string produced by the same `kGeneratorVersion`
- **THEN** every settings widget updates to the decoded settings, the seed input shows the decoded `seed_u64`, the randomize-each-generate flag is cleared, the live `settings_hash` equals `Settings_HashShort` of the decoded settings, and pressing Generate reproduces the sharer's placement

#### Scenario: v1 paste keeps seed-only adoption with warning
- **WHEN** the user pastes a valid v1 (50-char) share string while the current settings' hash differs from the embedded hash
- **THEN** only the seed is adopted (and pinned) and an inline warning states that the settings could not be restored from a v1 string and do not match

#### Scenario: Version-mismatch v2 paste warns
- **WHEN** the user pastes a valid v2 string whose `generator_version` byte differs from the binary's `kGeneratorVersion`
- **THEN** settings and seed are restored and a visible warning states the string came from a different version and Generate may produce a different seed than the sharer's

#### Scenario: Customizer-bearing v2 paste is refused
- **WHEN** the user pastes a v2 string whose canonical `customizer_active` bit is set
- **THEN** no settings or seed are adopted and the inline error explains that customizer seeds require their manifest file and cannot be restored from a share string

#### Scenario: Copy under customizer falls back to v1
- **WHEN** the user clicks Copy share string while `customizer_active` is set
- **THEN** the clipboard receives the v1 (seed + hash) string — today's customizer-sharing behavior, unchanged until `customizer_seed` share encoding lands

#### Scenario: Malformed paste leaves state untouched
- **WHEN** the user pastes a malformed, corrupted, alttpr.com-format, or newer-version string
- **THEN** an inline error describes the specific failure and no widget, seed, or flag changes

## ADDED Requirements

### Requirement: Generate confirmation on pasted-settings mismatch

The settings window SHALL record the settings hash associated with the most recent successful share-string paste (v1: the embedded `settings_hash`; v2: the hash recomputed from the restored settings). When the user activates Generate while a pasted hash is recorded and differs from the current settings' hash, the window SHALL interpose a confirmation modal stating that the settings no longer match the pasted share string and that generating now produces a different seed, with explicit Generate-anyway and Cancel choices. Confirming SHALL proceed with generation and clear the recorded paste; cancelling SHALL return to the window unchanged; a subsequent paste SHALL re-arm the check with the new hash. The modal copy SHALL be one to two short, durable player-facts (per the project's UI-brevity feedback rule).

#### Scenario: Mismatched Generate is interrupted loudly
- **WHEN** the user pastes a share string and then changes any settings widget so the current hash differs, and presses Generate
- **THEN** the confirmation modal appears before any generation starts, and Cancel leaves settings, seed, and slots untouched

#### Scenario: Confirming generates once and disarms
- **WHEN** the user chooses Generate-anyway in the modal
- **THEN** generation proceeds with the current (edited) settings and the recorded paste is cleared, so the next Generate with unchanged settings does not re-prompt

#### Scenario: Matching settings generate without interruption
- **WHEN** the user pastes a v2 string and presses Generate without editing settings
- **THEN** no modal appears (the hashes match by construction) and generation reproduces the pasted seed
