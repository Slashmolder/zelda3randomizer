## ADDED Requirements

### Requirement: Accessibility tier selection in the native settings window

The PC native (Dear ImGui) settings window SHALL expose all three accessibility
tiers as a single selector whose option order matches the enum values:

- index 0 → `items` (`kAccessibility_Items`)
- index 1 → `locations` (`kAccessibility_Locations`)
- index 2 → **`beatable only`** (`kAccessibility_None`)

The selector SHALL present a help affordance describing that all three tiers
guarantee a beatable seed and differ only in the extra reachability enforced
(`items` = every progression item; `locations` = every location; `beatable only`
= goal only).

When the goal is Completionist, the selector SHALL be forced to `locations` and
shown read-only (the existing Completionist lock); leaving Completionist SHALL
restore the user's previously-selected tier, including `beatable only`.

#### Scenario: Beatable-only is selectable on PC

- **WHEN** the goal is not Completionist
- **THEN** the accessibility selector offers `items`, `locations`, and
  `beatable only`, and selecting `beatable only` sets `accessibility` to
  `kAccessibility_None`

#### Scenario: Completionist locks the selector to locations

- **WHEN** the user selects goal = Completionist
- **THEN** the accessibility selector is forced to `locations` and is read-only

#### Scenario: Leaving Completionist restores the prior tier

- **WHEN** the user had `beatable only` selected, switched the goal to
  Completionist (forcing `locations`), then switched the goal back
- **THEN** the selector returns to `beatable only`
