# randomizer-souls Specification (delta)

## ADDED Requirements

### Requirement: Souls setting axis
The randomizer SHALL expose a `souls_shuffle` setting with exactly three values — `off`, `bosses`, `bosses+enemies` — defaulting to `off`, encoded in canonical settings byte 28 bits 2-3 (alongside `enemy_drop_checks` in bits 0-1; `kSettingsCanonicalLen` stays 29), parseable as the CSV/share-string key `souls_shuffle=off|bosses|all`, and configurable from the PC native settings window.

#### Scenario: Off is placement-identical to baseline
- **WHEN** a seed is generated with `souls_shuffle=off` and otherwise identical settings to a pre-souls build
- **THEN** the `placement_digest` is byte-identical to that build's output (no soul items in the pool, no soul predicates evaluated)

#### Scenario: Tier is captured by settings hash and share string
- **WHEN** two otherwise-identical settings differ only in `souls_shuffle`
- **THEN** their canonical serializations, settings hashes, and share strings differ

### Requirement: Soul item family
The randomizer SHALL define soul items as append-only registry entries: 12 boss souls (the 10 dungeon bosses, one Agahnim Soul covering both Agahnim encounters, one Ganon Soul) plus one soul per curated enemy family covering every `kEnemyTable` species, all classified as progression items, granted via the direct-grant path (`kRandoLttpSkip` + confirmation cue) with a shared soul icon for chest/field/receipt rendering.

#### Scenario: Boss tier pool contents
- **WHEN** a seed is generated with `souls_shuffle=bosses`
- **THEN** exactly the 12 boss souls are added to the item pool, displacing junk padding, and no enemy-family souls are placed

#### Scenario: Enemy tier pool contents
- **WHEN** a seed is generated with `souls_shuffle=bosses+enemies`
- **THEN** the 12 boss souls plus one soul per enemy family are added to the pool, and the family map covers every `kEnemyTable` species exactly once (generator-asserted)

#### Scenario: Soul grant is a direct grant
- **WHEN** the player checks a location containing a soul
- **THEN** the corresponding ownership bit is set, the location is marked checked, the confirmation cue (sound + icon) plays, and no vanilla receipt animation or side-effect receive code runs

### Requirement: Spawn suppression keyed on final species
When a souls tier is active, the game SHALL suppress the spawn of any sprite whose final post-shuffle species maps to an un-owned soul, at all three spawn layers (static dungeon room load, overworld proximity load, and the dynamic spawn funnel `Sprite_SpawnDynamically(Ex)`); species without a soul mapping in the active tier always spawn.

#### Scenario: Boss absent without its soul
- **WHEN** the player enters a boss room whose assigned boss's soul is not owned
- **THEN** the boss (and its room-data secondary sprites) does not spawn, the room is walkable and exitable, and no prize or heart container can be obtained

#### Scenario: Boss spawns with its soul
- **WHEN** the player owns the assigned boss's soul and enters the boss room
- **THEN** the boss fight proceeds exactly as without the souls feature

#### Scenario: Unlisted species unaffected
- **WHEN** a sprite whose species has no soul in the active tier spawns (NPCs, traps, projectiles, one-offs; all non-boss enemies under the `bosses` tier)
- **THEN** it spawns normally

#### Scenario: Dynamic spawns covered
- **WHEN** an overlord or another sprite attempts to dynamically spawn an enemy whose soul is not owned
- **THEN** the spawn is suppressed at the dynamic funnel; non-enemy dynamic spawns (items, effects) are never suppressed

#### Scenario: Suppression preserves kill-state bookkeeping
- **WHEN** a room containing both suppressed and live sprites is cleared, saved, and re-entered after acquiring the missing soul
- **THEN** previously-killed non-respawning sprites remain dead, and the newly-unsuppressed sprites spawn at their room-data positions

#### Scenario: Mid-room acquisition takes effect on next load
- **WHEN** the player acquires a soul while inside a room/area containing that species
- **THEN** the enemies appear on the next room or area (re)load, not instantly

### Requirement: Kill-gates held shut while suppressed
While any soul-suppressed static spawn exists in the current room, the room-clear tests (`Sprite_CheckIfScreenIsClear` / `Sprite_CheckIfRoomIsClear` and the Ganon-door gate) SHALL report not-clear, so kill-triggered rewards (door open, chest spawn, key drop, shutter open) do not fire; door behavior is otherwise vanilla (shutters close at room load as always), so a held room is exactly as escapable as a vanilla kill room with unkillable enemies — walk-out where the entry door is a normal door, Save-and-Quit where the entry is a shutter (an off-logic dive: logic never requires entering without the souls).

#### Scenario: Kill room does not trivialize
- **WHEN** the player enters a kill-to-open-door room whose gating enemies are all soul-suppressed
- **THEN** the door does not open, and it opens only after the player returns with the soul(s) and kills the (now spawned) enemies

#### Scenario: Held room with a normal entry door is walk-out escapable
- **WHEN** the player enters a held kill room through a non-shutter door
- **THEN** they can leave through the entrance they came from without save-and-quit or mirror

#### Scenario: Shutter-entry held room escapes via Save-and-Quit
- **WHEN** the player enters a held kill room whose entry door is a shutter (e.g. a GT refight without that boss's soul)
- **THEN** the shutters stay closed (vanilla behavior for an uncleared kill room) and Save-and-Quit is the escape, exactly as in vanilla when the room's enemies cannot be killed with the current loadout

### Requirement: Soul ownership persistence
Soul ownership SHALL persist as a bitfield across save/load, sidecar slot activation, and snapshot cold-replay; saves and sidecars created before the souls feature SHALL load with zero souls owned.

#### Scenario: Save round-trip
- **WHEN** the player collects souls, saves, quits, and reloads the slot
- **THEN** the owned-soul set is identical and suppression behaves accordingly

#### Scenario: Pre-souls sidecar loads safely
- **WHEN** a sidecar written by a pre-souls build is activated
- **THEN** it loads without error with zero souls owned (its seeds were generated with souls off, so no suppression applies)

### Requirement: Soul visibility surfaces
Owned souls SHALL be visible in the tracker window, and soul placements SHALL appear in the spoiler log by their registry names. Auto-tracker (external EmoTracker protocol) exposure is DEFERRED: soul ownership lives outside `g_ram` (no free reserved bytes — the `g_rando_boomerang_owned` precedent), and the memory-snapshot protocol cannot carry it without a protocol extension; external clients see no soul state in v1.

#### Scenario: Tracker shows ownership
- **WHEN** the player owns a subset of souls and opens the tracker window
- **THEN** owned souls render distinctly from un-owned ones

#### Scenario: Spoiler lists souls
- **WHEN** a souls-tier seed's spoiler is written
- **THEN** every placed soul appears at its location with its item name
