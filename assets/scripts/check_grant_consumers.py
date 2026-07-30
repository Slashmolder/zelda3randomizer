#!/usr/bin/env python3
"""Reject gameplay use of raw randomizer grant-resolution primitives.

Grant sites must use a transaction API that owns resolution, delivery, and
checked-state commit.  Keeping the raw resolver/dispatch/sentinel helpers in
the rando core is useful for compatibility and focused tests, but permitting
gameplay callers to compose them recreates the project's dominant class of
lost-placement bugs.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


REPO = Path(__file__).resolve().parents[2]
SOURCE_ROOT = REPO / "src"

# Raw definitions, transaction internals, and exhaustive compatibility tests
# live in rando.c, but the whole large translation unit is not trusted. Every
# permitted function is named with a reason; adding a bypass elsewhere fails.
RAW_CORE_FILE = Path("src/rando/rando.c")
RAW_CORE_ALLOWED_CALLS = {
    ("Rando_ResolveGrantPlan", "Rando_ResolveGrantPlan"): "pure resolver definition",
    ("Rando_ResolveLiveGrantPlan", "Rando_ResolveLiveGrantPlan"): "live resolver definition",
    ("Rando_ResolveGrantPlan", "Rando_ResolveLiveGrantPlan"): "live-to-pure resolver adapter",
    ("Rando_ResolveGrantPlan", "Rando_PrepareGrant"): "transaction-core resolver",
    ("Rando_OnLocationCheck", "Rando_OnLocationCheck"): "legacy API definition",
    ("Rando_DispatchVanillaGrant", "Rando_DispatchVanillaGrant"): "legacy API definition",
    ("Rando_OnLocationCheck", "Rando_DispatchVanillaGrant"): "legacy compatibility adapter",
    ("Rando_ResolveLiveGrantPlan", "Rando_DispatchVanillaGrant"): "legacy compatibility plan",
    ("Rando_ResolveLiveGrantPlan", "Rando_CommitRepeatableShopIdentity"): "validated vanilla-identity commit",
    ("Rando_ResolveLiveGrantPlan", "Rando_ShopSlotGrantIsNoOp"): "side-effect-free no-op probe (paid-site pricing)",
    ("Rando_ResolveLiveGrantPlan", "rando_show_direct_grant_icon_only"): "presentation-only plan lookup",
    ("Rando_ResolveLiveGrantPlan", "rando_item_display_lttp"): "presentation-only plan lookup",
    ("Rando_ResolveLiveGrantPlan", "rando_placed_item_icon"): "presentation-only plan lookup",
    ("Rando_OnLocationCheck", "Rando_SelfCheck"): "legacy compatibility tests",
    ("Rando_DispatchVanillaGrant", "Rando_SelfCheck"): "legacy compatibility tests",
    ("Rando_ResolveGrantPlan", "Rando_GrantPlanSelfCheck"): "pure resolver tests",
}

RAW_CALLS = (
    "Rando_ResolveGrantPlan",
    "Rando_ResolveLiveGrantPlan",
    "Rando_OnLocationCheck",
    "Rando_DispatchVanillaGrant",
    # Deleted legacy adapters — kept guarded so a reintroduction (or a stale
    # copy/paste of the pre-transaction delivery pattern) fails the source
    # guard instead of quietly reviving the raw API.
    "Rando_ReceiveOrConfirm",
    "Rando_QuietReceiveOrConfirm",
    "Rando_LastDispatchedItemId",
)

# Receipt/inventory entry points are implementation details of the shared
# transaction path. Direct callers otherwise risk bypassing checked-state
# ordering or omitting deferred rupee/heart/refill/palette effects.
LOW_LEVEL_CALLS = (
    "ItemReceipt_GrantInventory",
    "ItemReceipt_GrantWithoutAnimation",
    "Link_ReceiveItem",
    "AncillaAdd_ItemReceipt",
)
LOW_LEVEL_ALLOWED_CALLS = {
    (Path("src/misc.c"), "ItemReceipt_GrantInventory", "ItemReceipt_GrantInventory"): "inventory helper definition",
    (Path("src/misc.c"), "ItemReceipt_GrantWithoutAnimation", "ItemReceipt_GrantWithoutAnimation"): "quiet receipt definition",
    (Path("src/misc.c"), "ItemReceipt_GrantInventory", "ItemReceipt_GrantWithoutAnimation"): "quiet receipt implementation",
    (Path("src/misc.c"), "ItemReceipt_GrantInventory", "AncillaAdd_ItemReceipt"): "animated receipt implementation",
    (Path("src/misc.c"), "ItemReceipt_GrantInventory", "ItemReceipt_LosslessSelfCheck"): "receipt implementation tests",
    (Path("src/misc.c"), "ItemReceipt_GrantWithoutAnimation", "ItemReceipt_LosslessSelfCheck"): "receipt implementation tests",
    (Path("src/misc.c"), "AncillaAdd_ItemReceipt", "AncillaAdd_ItemReceipt"): "animated receipt definition",
    (Path("src/player.c"), "Link_ReceiveItem", "Link_ReceiveItem"): "animated receipt definition",
    (Path("src/player.c"), "AncillaAdd_ItemReceipt", "Link_ReceiveItem"): "animated receipt allocation handoff",
    (Path("src/player.c"), "ItemReceipt_GrantWithoutAnimation", "Link_ReceiveItem"): "lossless saturation handoff",
    (Path("src/rando/rando.c"), "ItemReceipt_GrantWithoutAnimation", "Rando_CommitPreparedGrant"): "transaction-core quiet delivery",
    (Path("src/rando/rando.c"), "Link_ReceiveItem", "Rando_CommitPreparedGrant"): "transaction-core animated delivery",
    (Path("src/ancilla.c"), "Link_ReceiveItem", "Ancilla22_ItemReceipt"): "vanilla receipt continuation",
    (Path("src/ancilla.c"), "Link_ReceiveItem", "Ancilla29_MilestoneItemReceipt"): "validated tablet/boss fallback",
    (Path("src/ancilla.c"), "Link_ReceiveItem", "Ancilla36_Flute"): "vanilla flute receipt",
    (Path("src/misc.c"), "Link_ReceiveItem", "ItemReceipt_LosslessSelfCheck"): "receipt implementation tests",
    (Path("src/player.c"), "Link_ReceiveItem", "Link_PerformOpenChest"): "validated chest vanilla fallback",
    (Path("src/rando/rando_placement.c"), "Link_ReceiveItem", None): "link receipt declaration",
    (Path("src/sprite.c"), "Link_ReceiveItem", "Sprite_HandleAbsorptionByPlayer"): "vanilla key pickup fallback",
    (Path("src/sprite_main.c"), "Link_ReceiveItem", "Sprite_GrantAnimatedOrVanilla"): "validated inactive vanilla fallback",
    (Path("src/sprite_main.c"), "Link_ReceiveItem", "Sprite_WishPond3"): "vanilla pond return",
    (Path("src/sprite_main.c"), "Link_ReceiveItem", "Sprite_Witch"): "vanilla witch receipt",
    (Path("src/sprite_main.c"), "Link_ReceiveItem", "Sprite_HeartContainer"): "validated heart-container fallback",
    (Path("src/sprite_main.c"), "Link_ReceiveItem", "Sprite_HeartPiece"): "validated standing-heart fallback",
    (Path("src/sprite_main.c"), "Link_ReceiveItem", "Sprite_GreenCauldron"): "vanilla potion receipt",
    (Path("src/sprite_main.c"), "Link_ReceiveItem", "Sprite_BlueCauldron"): "vanilla potion receipt",
    (Path("src/sprite_main.c"), "Link_ReceiveItem", "Sprite_RedCauldron"): "vanilla potion receipt",
    (Path("src/sprite_main.c"), "Link_ReceiveItem", "Sprite_Catfish_QuakeMedallion"): "validated Catfish fallback",
    (Path("src/sprite_main.c"), "Link_ReceiveItem", "ShopItem_HandleReceipt"): "validated shop vanilla/restock receipt",
}


def strip_comments_and_literals(text: str) -> str:
    """Blank comments/string/char literals while preserving line numbers."""
    pattern = re.compile(
        r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'",
        re.DOTALL,
    )

    def blank(match: re.Match[str]) -> str:
        value = match.group(0)
        return "".join("\n" if ch == "\n" else " " for ch in value)

    return pattern.sub(blank, text)


def function_ranges(clean: str) -> list[tuple[int, int, str]]:
    """Return best-effort top-level C function byte ranges."""
    header = re.compile(
        r"(?m)^[ \t]*(?!(?:if|for|while|switch)\b)"
        r"(?:static\s+)?(?:inline\s+)?"
        r"(?:[A-Za-z_]\w*[ \t*]+)+"
        r"(?P<name>[A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{"
    )
    ranges: list[tuple[int, int, str]] = []
    for match in header.finditer(clean):
        brace = clean.find("{", match.start(), match.end())
        depth = 1
        pos = brace + 1
        while pos < len(clean) and depth:
            if clean[pos] == "{":
                depth += 1
            elif clean[pos] == "}":
                depth -= 1
            pos += 1
        if depth == 0:
            ranges.append((match.start(), pos, match.group("name")))
    return ranges


def call_lines(text: str, names: tuple[str, ...]) -> list[tuple[int, str, str | None]]:
    clean = strip_comments_and_literals(text)
    ranges = function_ranges(clean)
    found: list[tuple[int, str, str | None]] = []
    for name in names:
        for match in re.finditer(rf"\b{re.escape(name)}\s*\(", clean):
            owner = next(
                (function for start, end, function in ranges
                 if start <= match.start() < end),
                None,
            )
            found.append((clean.count("\n", 0, match.start()) + 1, name, owner))
    return sorted(found)


def audit(root: Path = REPO) -> list[str]:
    failures: list[str] = []
    for path in sorted((root / "src").rglob("*.c")):
        rel = path.relative_to(root)
        text = path.read_text(encoding="utf-8")
        for line, name, owner in call_lines(text, RAW_CALLS):
            allowed_core = rel == RAW_CORE_FILE and (name, owner) in RAW_CORE_ALLOWED_CALLS
            if not allowed_core:
                failures.append(
                    f"{rel.as_posix()}:{line}: raw {name} call in "
                    f"{owner or '<top-level>'}; use a grant transaction"
                )
        for line, name, owner in call_lines(text, LOW_LEVEL_CALLS):
            if (rel, name, owner) not in LOW_LEVEL_ALLOWED_CALLS:
                failures.append(
                    f"{rel.as_posix()}:{line}: low-level {name} bypass in "
                    f"{owner or '<top-level>'}; use receipt/transaction API"
                )
    return failures


def selftest() -> None:
    fixture = r'''
// Rando_OnLocationCheck(1, 2);
const char *s = "Rando_DispatchVanillaGrant(1, 2, 3)";
/* Rando_ReceiveOrConfirm(1, 2); */
void f(void) {
  Rando_OnLocationCheck(1, 2);
  Rando_ResolveGrantPlan(1, 2, 0, 0);
  Rando_ResolveLiveGrantPlan(1, 2, 0);
  Rando_LastDispatchedItemId (
  );
  Link_ReceiveItem(1, 0);
  AncillaAdd_ItemReceipt(0x22, 4, 0);
}
'''
    got_raw = call_lines(fixture, RAW_CALLS)
    expected_raw = [
        (6, "Rando_OnLocationCheck", "f"),
        (7, "Rando_ResolveGrantPlan", "f"),
        (8, "Rando_ResolveLiveGrantPlan", "f"),
        (9, "Rando_LastDispatchedItemId", "f"),
    ]
    if got_raw != expected_raw:
        raise AssertionError(
            f"raw parser selftest mismatch: expected {expected_raw}, got {got_raw}"
        )
    got_low = call_lines(fixture, LOW_LEVEL_CALLS)
    expected_low = [
        (11, "Link_ReceiveItem", "f"),
        (12, "AncillaAdd_ItemReceipt", "f"),
    ]
    if got_low != expected_low:
        raise AssertionError(
            f"low-level parser selftest mismatch: expected {expected_low}, got {got_low}"
        )


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args(argv)
    if args.selftest:
        selftest()
        print("check_grant_consumers: parser selftest OK")
        return 0

    failures = audit()
    if failures:
        print("check_grant_consumers: FAIL", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print("check_grant_consumers: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
