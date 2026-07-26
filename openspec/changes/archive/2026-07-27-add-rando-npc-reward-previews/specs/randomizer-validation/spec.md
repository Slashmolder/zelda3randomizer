## ADDED Requirements

### Requirement: NPC reward-preview validation gates

Automated validation SHALL cover settings, the shared resolver and formatters
for every registered item ID, representative audited source-specific contexts
and transaction paths across paid, one-time, shop, and Take-Any sources, locale
fallback, lifecycle, spoilers, race reveal, and post-placement orthogonality.
Source audits and guards SHALL retain ownership of the full source roster.
Owner gameplay SHALL remain a separate closeout gate.

Current-schema JSON settings and text spoilers SHALL report the canonical
preview boolean. Generator-156 compatibility JSON/text SHALL omit it. A
CRC-correct generator-156 fixture mutation setting `[25]` bit 7 SHALL be refused
as `SettingsCorrupt` without modifying the artifact or writing output/scratch.

The same-seed Off/On pair SHALL differ in canonical `[25]` bit 7, settings hash,
and share identity only. Placement digest, sphere digest, clue plan/digest, and
hint rows SHALL remain identical.

#### Scenario: Public spoiler pair proves independence
- **WHEN** the public generator creates the same Hints-Off seed with previews
  Off and On
- **THEN** JSON/text mirror each boolean, settings identities differ, and
  placement/sphere/plan/row identities match

#### Scenario: Every registered item fits the shared formatters
- **WHEN** validation renders every registered item ID through the shared
  reward-page and inventory formatters and exercises representative seller,
  free/trade, Fairy, shop-slot, and Take-Any templates
- **THEN** the full qualified identity and price fit or the renderer preserves
  the prior complete dialogue without partial output

#### Scenario: Representative sources preserve transaction ownership
- **WHEN** source-specific and grant-transaction self-tests exercise
  representative paid and one-time view, decline, insufficient-funds,
  acceptance, retryable-failure, checked-replay, repeated-input, and lifecycle
  paths
- **THEN** presentation remains read-only and each successful transaction
  charges, grants, and checks exactly once

#### Scenario: Locale boundary is explicit and safe
- **WHEN** Original/US, German, and French cases exercise the shared formatters
  and representative source families
- **THEN** Original/US receives complete exact previews, German/French remain
  byte-identical, and both configuration UIs disclose the supported locale

#### Scenario: Corpus does not hide presentation drift
- **WHEN** the full generator-160 corpus runs with focused preview-on coverage
- **THEN** existing placement and sphere digests remain unchanged and any drift
  is diagnosed instead of rebaselined

#### Scenario: Owner gameplay remains open after automation
- **WHEN** all automated and cross-platform gates pass
- **THEN** paid/free/trade NPCs, shopsanity, Take-Any, retry/replay, race,
  locale, save/snapshot, and slot-switch gameplay remain explicitly pending
  until the owner signs them off
