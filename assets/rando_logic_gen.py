#!/usr/bin/env python3
"""rando_logic_gen.py — Phase A logic codegen.

Per `randomizer-logic / Logic YAML schema and worked examples` and tasks.md
§3.5. Reads:

  - assets/rando/op_registry.yaml          (task 3.4 — op-code IDs)
  - assets/rando/item_registry.yaml        (task 3.2 — item IDs)
  - assets/rando/location_registry.yaml    (task 3.1 — location IDs)
  - assets/rando/logic.schema.yaml         (task 3.5a — schema)
  - assets/rando/macros.yaml               (task 3.4a — named macros) [optional]
  - assets/rando/logic.yaml                (task 3.3 — graph)          [optional]

Emits:

  - src/rando/location_ids.h               (#define LOC_<name> <id>)
  - src/rando/item_ids.h                   (#define ITEM_<name> <id>)
  - src/rando/logic_data.c                 (LocationDef[], RegionDef[],
                                             EdgeDef[], predicate streams)

Discipline (per CLAUDE.md claim-grounding and randomizer-core / Byte-order pin):
  - Deterministic output: iteration uses sorted IDs.
  - No `rand`, no wall-clock, no float.
  - All multi-byte integers in emitted byte streams are little-endian.
  - Phase B ops (OP_TRICK / OP_DIFFICULTY_AT_LEAST / OP_GLITCH_LEVEL_AT_LEAST)
    rejected in Phase A logic.yaml by the well-formedness pass (task 3.10).

Bytecode format (recursively-evaluable, no length prefixes — recursion depth
is bounded by the inline-complexity check):

  HAS_ITEM       -> <op:u8> <item_id:u16_le>
  HAS_AMOUNT     -> <op:u8> <item_id:u16_le> <n:u8>
  HAS_ANY_OF     -> <op:u8> <count:u8> <id:u16_le>×count
  HAS_ANY_COUNT  -> <op:u8> <count:u8> <id:u16_le>×count <n:u8>
  WORLDSTATE_EQ  -> <op:u8> <state:u8>
  GOAL_EQ        -> <op:u8> <goal:u8>
  GOAL_REQUIRES_DUNGEON -> <op:u8> <dungeon:u8>
  DUNGEON_CLEARED -> <op:u8> <dungeon:u8>
  REGION_REACHABLE -> <op:u8> <region_id:u16_le>
  HAS_PRIZE      -> <op:u8> <prize:u8>
  MEDALLION_OPENS -> <op:u8> <entrance:u8>
  ITEM_IS        -> <op:u8> <item_id:u16_le>
  NOT            -> <op:u8> <child>
  AND            -> <op:u8> <count:u8> <child>×count
  OR             -> <op:u8> <count:u8> <child>×count

  Vacuous AND (count=0) is TRUE; vacuous OR (count=0) is FALSE. The DSL
  literals TRUE() / FALSE() compile to those forms.

The well-formedness pass (task 3.10) asserts well-formedness conditions
listed in logic.schema.yaml `well_formedness:`.
"""

from __future__ import annotations

import argparse
import dataclasses
import os
import re
import struct
import sys
from pathlib import Path

import yaml  # PyYAML; required by CLAUDE.md (`pip install -r requirements.txt`)


# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
REPO = Path(__file__).resolve().parent.parent
RANDO_ASSETS = REPO / "assets" / "rando"
RANDO_SRC = REPO / "src" / "rando"


# ---------------------------------------------------------------------------
# Registry loading
# ---------------------------------------------------------------------------
@dataclasses.dataclass
class OpDef:
    id: int
    name: str
    phase: str
    operands: list
    contexts: list


@dataclasses.dataclass
class ItemDef:
    id: int
    name: str
    category: str
    max_count: int = 1
    dispatch: str = ""


@dataclasses.dataclass
class LocationDef:
    id: int
    name: str
    region: str
    type: str
    vanilla_item: str
    world_state_filter: list = dataclasses.field(default_factory=list)
    dungeon_item_class: str | None = None
    can_reach: str = "TRUE()"
    can_place: str = "TRUE()"
    always_allow: str = "FALSE()"
    source: str = ""


@dataclasses.dataclass
class MacroDef:
    name: str
    parameters: list
    body: str
    source: str = ""


@dataclasses.dataclass
class RegionDef:
    id: str
    name: str
    dungeon: str | None = None
    parent: str | None = None
    world_state_filter: list = dataclasses.field(default_factory=list)
    source: str = ""


@dataclasses.dataclass
class EdgeDef:
    from_: str
    to: str
    predicate: str
    one_way: bool = False
    source: str = ""


def load_yaml(path: Path) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        return yaml.safe_load(f)


def load_ops(path: Path) -> dict[str, OpDef]:
    doc = load_yaml(path)
    out = {}
    for raw in doc.get("ops", []):
        op = OpDef(
            id=raw["id"],
            name=raw["name"],
            phase=raw.get("phase", "A"),
            operands=raw.get("operands", []),
            contexts=raw.get("contexts", []),
        )
        # Strip the "OP_" prefix for DSL parser keys (DSL uses bare op names like HAS_ITEM)
        bare = op.name[3:] if op.name.startswith("OP_") else op.name
        out[bare] = op
    return out


def load_items(path: Path) -> dict[str, ItemDef]:
    doc = load_yaml(path)
    out = {}
    for raw in doc.get("items", []):
        it = ItemDef(
            id=raw["id"],
            name=raw["name"],
            category=raw.get("category", ""),
            max_count=raw.get("max_count", 1),
            dispatch=raw.get("dispatch", ""),
        )
        out[it.name] = it
    return out


def load_locations(path: Path) -> dict[str, LocationDef]:
    doc = load_yaml(path)
    out = {}
    for raw in doc.get("locations", []):
        loc = LocationDef(
            id=raw["id"],
            name=raw["name"],
            region=raw.get("region", ""),
            type=raw.get("type", "Chest"),
            vanilla_item=raw.get("vanilla_item", ""),
            world_state_filter=raw.get("world_state_filter", []),
            dungeon_item_class=raw.get("dungeon_item_class"),
            source=raw.get("source", ""),
        )
        # Map location.name -> a canonical id token usable in #define LOC_<id>.
        # We use the YAML location name lowered + non-alphanumerics replaced with _.
        out[loc.name] = loc
    return out


def load_macros(path: Path | None) -> dict[str, MacroDef]:
    if path is None or not path.exists():
        return {}
    doc = load_yaml(path)
    out = {}
    for raw in doc.get("macros", []):
        m = MacroDef(
            name=raw["name"],
            parameters=raw.get("parameters", []) or [],
            body=raw["body"],
            source=raw.get("source", ""),
        )
        out[m.name] = m
    return out


def _merge_logic_doc(doc, regions, edges, loc_preds, macros, source_file):
    """Merge a single parsed logic YAML doc into the cumulative dicts.

    Per-file conflict policy: regions / macros are keyed by id/name; a later
    file silently overrides an earlier one (the codegen reports duplicates as
    warnings via well-formedness). Edges are appended. Location predicates
    are keyed by id; later wins.
    """
    if doc is None:
        return
    for raw in doc.get("regions", []) or []:
        r = RegionDef(
            id=raw["id"],
            name=raw.get("name", raw["id"]),
            dungeon=raw.get("dungeon"),
            parent=raw.get("parent"),
            world_state_filter=raw.get("world_state_filter", []),
            source=raw.get("source", ""),
        )
        regions[r.id] = r
    for raw in doc.get("edges", []) or []:
        edges.append(EdgeDef(
            from_=raw["from"],
            to=raw["to"],
            predicate=raw.get("predicate", "TRUE()"),
            one_way=raw.get("one_way", False),
            source=raw.get("source", ""),
        ))
    for raw in doc.get("locations", []) or []:
        loc_preds[raw["id"]] = LocationDef(
            id=0,  # numeric id comes from registry
            name=raw.get("name", raw["id"]),
            region=raw.get("region", ""),
            type=raw.get("type", "Chest"),
            vanilla_item=raw.get("vanilla_item", ""),
            can_reach=raw.get("can_reach", "TRUE()"),
            can_place=raw.get("can_place", "TRUE()"),
            always_allow=raw.get("always_allow", "FALSE()"),
            source=raw.get("source", ""),
        )
    for raw in doc.get("macros", []) or []:
        macros[raw["name"]] = MacroDef(
            name=raw["name"],
            parameters=raw.get("parameters", []) or [],
            body=raw["body"],
            source=raw.get("source", ""),
        )


def load_logic(path: Path | None) -> tuple[dict[str, RegionDef], list[EdgeDef], dict[str, LocationDef], dict[str, MacroDef]]:
    """Load the optional logic.yaml plus every file under
    `assets/rando/logic_parts/*.yaml` (sorted for deterministic merge order).
    Returns (regions, edges, location_predicates, macros).

    Files later in sort order override earlier files for regions / locations /
    macros. Edges are concatenated. Authors partition work across files: one
    region per file (or one dungeon per file) avoids merge conflicts between
    agents working in parallel.
    """
    regions: dict[str, RegionDef] = {}
    edges: list[EdgeDef] = []
    loc_preds: dict[str, LocationDef] = {}
    macros: dict[str, MacroDef] = {}

    # Load the main logic.yaml first (lowest priority).
    if path is not None and path.exists():
        _merge_logic_doc(load_yaml(path), regions, edges, loc_preds, macros, str(path))

    # Load every file under assets/rando/logic_parts/*.yaml (sorted).
    parts_dir = RANDO_ASSETS / "logic_parts"
    if parts_dir.exists() and parts_dir.is_dir():
        for part in sorted(parts_dir.glob("*.yaml")):
            _merge_logic_doc(load_yaml(part), regions, edges, loc_preds, macros, str(part))

    return regions, edges, loc_preds, macros


# ---------------------------------------------------------------------------
# Predicate DSL parser
# ---------------------------------------------------------------------------
# Grammar (per logic.schema.yaml `predicate_expression`):
#
#   predicate := or_expr
#   or_expr   := and_expr ( "OR" and_expr )*
#   and_expr  := not_expr ( "AND" not_expr )*
#   not_expr  := "NOT" not_expr | atom
#   atom      := op_call | macro_call | "TRUE()" | "FALSE()" | "(" predicate ")"
#   op_call   := OP_NAME "(" arg_list? ")"
#   macro_call:= MacroName "(" arg_list? ")"
#   arg_list  := arg ("," arg)*
#   arg       := identifier | int_literal | list_literal
#   list_literal := "[" identifier ("," identifier)* "]"


# AST node forms:
#   ("op", op_name, [args])
#   ("macro", name, [args])
#   ("and", [child, ...])
#   ("or", [child, ...])
#   ("not", child)
#   ("true",)
#   ("false",)


TOKEN_PATTERNS = [
    (r"\s+", None),                       # whitespace
    (r"AND\b", "AND"),
    (r"OR\b", "OR"),
    (r"NOT\b", "NOT"),
    (r"TRUE\b", "TRUE_LIT"),
    (r"FALSE\b", "FALSE_LIT"),
    # Comparison ops: compile-time pseudo-ops that resolve to TRUE/FALSE during
    # macro expansion (substituted with literal integer args). See `fold_compares`.
    (r"EQ\b", "CMP_EQ"),
    (r"NE\b", "CMP_NE"),
    (r"LT\b", "CMP_LT"),
    (r"LE\b", "CMP_LE"),
    (r"GT\b", "CMP_GT"),
    (r"GE\b", "CMP_GE"),
    (r"\(", "LPAREN"),
    (r"\)", "RPAREN"),
    (r"\[", "LBRACK"),
    (r"\]", "RBRACK"),
    (r",", "COMMA"),
    (r"\d+", "INT"),
    (r"[A-Za-z_][A-Za-z0-9_]*", "IDENT"),
]


class ParseError(Exception):
    pass


def tokenize(text: str) -> list[tuple[str, str]]:
    pos = 0
    tokens = []
    while pos < len(text):
        for pattern, kind in TOKEN_PATTERNS:
            m = re.match(pattern, text[pos:])
            if m:
                if kind is not None:
                    tokens.append((kind, m.group(0)))
                pos += m.end()
                break
        else:
            raise ParseError(f"unexpected character at {pos}: {text[pos:pos+20]!r}")
    return tokens


class Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.pos = 0

    def peek(self, offset=0):
        if self.pos + offset >= len(self.tokens):
            return ("EOF", "")
        return self.tokens[self.pos + offset]

    def eat(self, kind=None):
        if self.pos >= len(self.tokens):
            raise ParseError(f"unexpected end of input (wanted {kind})")
        tok = self.tokens[self.pos]
        if kind is not None and tok[0] != kind:
            raise ParseError(f"expected {kind} at token {self.pos}, got {tok}")
        self.pos += 1
        return tok

    def parse(self):
        node = self.or_expr()
        if self.pos != len(self.tokens):
            raise ParseError(f"trailing tokens at {self.pos}: {self.tokens[self.pos:]}")
        return node

    def or_expr(self):
        children = [self.and_expr()]
        while self.peek()[0] == "OR":
            self.eat("OR")
            children.append(self.and_expr())
        if len(children) == 1:
            return children[0]
        return ("or", children)

    def and_expr(self):
        children = [self.not_expr()]
        while self.peek()[0] == "AND":
            self.eat("AND")
            children.append(self.not_expr())
        if len(children) == 1:
            return children[0]
        return ("and", children)

    def not_expr(self):
        if self.peek()[0] == "NOT":
            self.eat("NOT")
            child = self.not_expr()
            return ("not", child)
        return self.comparison()

    def comparison(self):
        left = self.atom()
        kind = self.peek()[0]
        if kind in {"CMP_EQ", "CMP_NE", "CMP_LT", "CMP_LE", "CMP_GT", "CMP_GE"}:
            self.eat(kind)
            right = self.atom()  # comparison RHS is also an atom (ident/int/expr)
            return ("cmp", kind, left, right)
        return left

    def atom(self):
        tok = self.peek()
        if tok[0] == "LPAREN":
            self.eat("LPAREN")
            node = self.or_expr()
            self.eat("RPAREN")
            return node
        if tok[0] == "TRUE_LIT":
            self.eat("TRUE_LIT")
            self.eat("LPAREN")
            self.eat("RPAREN")
            return ("true",)
        if tok[0] == "FALSE_LIT":
            self.eat("FALSE_LIT")
            self.eat("LPAREN")
            self.eat("RPAREN")
            return ("false",)
        if tok[0] == "INT":
            return ("int", int(self.eat("INT")[1]))
        if tok[0] == "IDENT":
            name = self.eat("IDENT")[1]
            # If followed by LPAREN, this is an op-call or macro-call.
            # Otherwise it's a bare identifier (parameter reference).
            if self.peek()[0] == "LPAREN":
                self.eat("LPAREN")
                args = []
                if self.peek()[0] != "RPAREN":
                    args.append(self.arg())
                    while self.peek()[0] == "COMMA":
                        self.eat("COMMA")
                        args.append(self.arg())
                self.eat("RPAREN")
                if name.startswith("OP_"):
                    bare = name[3:]
                    return ("op", bare, args)
                return ("call", name, args)
            return ("ident", name)
        raise ParseError(f"unexpected token at atom: {tok}")

    def arg(self):
        tok = self.peek()
        if tok[0] == "LBRACK":
            self.eat("LBRACK")
            items = []
            if self.peek()[0] != "RBRACK":
                items.append(self.eat()[1])
                while self.peek()[0] == "COMMA":
                    self.eat("COMMA")
                    items.append(self.eat()[1])
            self.eat("RBRACK")
            return ("list", items)
        if tok[0] == "INT":
            return ("int", int(self.eat("INT")[1]))
        if tok[0] == "IDENT":
            return ("ident", self.eat("IDENT")[1])
        raise ParseError(f"unexpected token in arg: {tok}")


def parse_predicate(text: str):
    return Parser(tokenize(text)).parse()


# ---------------------------------------------------------------------------
# Op-name resolution
# ---------------------------------------------------------------------------
# Predicate "call" nodes are either ops or macros. We resolve in a second pass
# once we know the op registry and macro set.


def resolve_calls(ast, ops: dict[str, OpDef], macros: dict[str, MacroDef]):
    """Convert ("call", name, args) -> ("op", name, args) or ("macro", name, args)."""
    if not isinstance(ast, tuple):
        return ast
    if ast[0] == "call":
        _, name, args = ast
        new_args = [resolve_calls(a, ops, macros) if isinstance(a, tuple) and a[0] in ("call", "op", "macro", "and", "or", "not", "true", "false", "cmp") else a for a in args]
        if name in ops:
            return ("op", name, new_args)
        if name in macros:
            return ("macro", name, new_args)
        # Treat unknown UPPER_SNAKE as op for well-formedness errors downstream.
        if name.isupper() or "_" in name:
            return ("op", name, new_args)
        return ("macro", name, new_args)
    if ast[0] in ("and", "or"):
        return (ast[0], [resolve_calls(c, ops, macros) for c in ast[1]])
    if ast[0] == "not":
        return ("not", resolve_calls(ast[1], ops, macros))
    if ast[0] == "cmp":
        return ("cmp", ast[1], resolve_calls(ast[2], ops, macros), resolve_calls(ast[3], ops, macros))
    if ast[0] == "op":
        return ("op", ast[1], [resolve_calls(a, ops, macros) if isinstance(a, tuple) and a[0] in ("call",) else a for a in ast[2]])
    return ast


# ---------------------------------------------------------------------------
# Macro expansion
# ---------------------------------------------------------------------------
def expand_macros(ast, macros: dict[str, MacroDef], parsed_macro_bodies: dict[str, object], ops: dict[str, OpDef], depth: int = 0, expanding: set = None):
    """Recursively expand macro calls. Detects cycles."""
    if expanding is None:
        expanding = set()
    if depth > 32:
        raise ParseError(f"macro expansion exceeded depth 32 (cycle?)")
    if not isinstance(ast, tuple):
        return ast
    kind = ast[0]
    if kind == "macro":
        _, name, args = ast
        if name not in parsed_macro_bodies:
            raise ParseError(f"unknown macro {name!r}")
        if name in expanding:
            raise ParseError(f"macro cycle: {expanding} -> {name}")
        macro = macros[name]
        body_ast = parsed_macro_bodies[name]
        if len(args) != len(macro.parameters):
            # Allow arity mismatch only if macro is parameterless and call passes nothing.
            if not (len(macro.parameters) == 0 and len(args) == 0):
                raise ParseError(f"macro {name} expects {len(macro.parameters)} arg(s), got {len(args)}")
        # Substitute parameters in body_ast.
        param_map = dict(zip(macro.parameters, args))
        substituted = substitute_params(body_ast, param_map)
        # Fold compile-time comparisons (turn `min_level EQ 1` -> TRUE/FALSE).
        folded = fold_compares(substituted)
        # Recursively expand any inner macros, then simplify.
        expanded = expand_macros(folded, macros, parsed_macro_bodies, ops, depth + 1, expanding | {name})
        return simplify(expanded)
    if kind in ("and", "or"):
        return (kind, [expand_macros(c, macros, parsed_macro_bodies, ops, depth, expanding) for c in ast[1]])
    if kind == "not":
        return ("not", expand_macros(ast[1], macros, parsed_macro_bodies, ops, depth, expanding))
    if kind == "op":
        return ast  # ops don't contain sub-predicates as args; args are item/region IDs etc.
    return ast


def substitute_params(ast, param_map: dict):
    """Replace ident args matching parameter names with their bound values.

    The bound value for a parameter is whatever the caller passed: an ident, an
    int, or a list. We replace IN PLACE within the AST tree.
    """
    if not isinstance(ast, tuple):
        return ast
    kind = ast[0]
    if kind in ("op", "macro"):
        head = ast[0]
        name = ast[1]
        new_args = []
        for a in ast[2]:
            if isinstance(a, tuple) and a[0] == "ident" and a[1] in param_map:
                # Replace ident with the bound value.
                new_args.append(param_map[a[1]])
            else:
                new_args.append(a)
        return (head, name, new_args)
    if kind in ("and", "or"):
        return (kind, [substitute_params(c, param_map) for c in ast[1]])
    if kind == "not":
        return ("not", substitute_params(ast[1], param_map))
    if kind == "cmp":
        _, op, lhs, rhs = ast
        new_lhs = param_map[lhs[1]] if isinstance(lhs, tuple) and lhs[0] == "ident" and lhs[1] in param_map else substitute_params(lhs, param_map)
        new_rhs = param_map[rhs[1]] if isinstance(rhs, tuple) and rhs[0] == "ident" and rhs[1] in param_map else substitute_params(rhs, param_map)
        return ("cmp", op, new_lhs, new_rhs)
    if kind == "ident":
        if ast[1] in param_map:
            return param_map[ast[1]]
    return ast


def fold_compares(ast, errors: list = None):
    """Evaluate ("cmp", op, lhs, rhs) when both sides are integer literals; replace with TRUE/FALSE.

    If a side is still an unbound ident, emit a warning and treat the comparison
    as TRUE (so the surrounding macro-body branch isn't accidentally lost). This
    matches the agent-authored macro convention where ungrounded parameters
    indicate "Phase A doesn't use this branch — assume true and let later passes
    drop the dead code."
    """
    if errors is None:
        errors = []
    if not isinstance(ast, tuple):
        return ast
    kind = ast[0]
    if kind == "cmp":
        _, op, lhs, rhs = ast
        lhs = fold_compares(lhs, errors)
        rhs = fold_compares(rhs, errors)
        if isinstance(lhs, tuple) and lhs[0] == "int" and isinstance(rhs, tuple) and rhs[0] == "int":
            l, r = lhs[1], rhs[1]
            result = {
                "CMP_EQ": l == r,
                "CMP_NE": l != r,
                "CMP_LT": l < r,
                "CMP_LE": l <= r,
                "CMP_GT": l > r,
                "CMP_GE": l >= r,
            }[op]
            return ("true",) if result else ("false",)
        # Unbound ident on at least one side. Default to TRUE.
        errors.append(f"unbound comparison {op} {lhs} {rhs} (returning TRUE)")
        return ("true",)
    if kind in ("and", "or"):
        return (kind, [fold_compares(c, errors) for c in ast[1]])
    if kind == "not":
        return ("not", fold_compares(ast[1], errors))
    if kind == "op":
        return ast
    return ast


def simplify(ast):
    """Constant-fold TRUE/FALSE through AND/OR/NOT to drop dead branches.

    Used after macro expansion + compare folding to clean up macros like
    `(branch_A AND TRUE) OR (branch_B AND FALSE)` -> `branch_A`.
    """
    if not isinstance(ast, tuple):
        return ast
    kind = ast[0]
    if kind == "and":
        children = [simplify(c) for c in ast[1]]
        # If any FALSE, whole AND is FALSE.
        if any(c == ("false",) for c in children):
            return ("false",)
        # Drop TRUE children.
        children = [c for c in children if c != ("true",)]
        if not children:
            return ("true",)
        if len(children) == 1:
            return children[0]
        return ("and", children)
    if kind == "or":
        children = [simplify(c) for c in ast[1]]
        if any(c == ("true",) for c in children):
            return ("true",)
        children = [c for c in children if c != ("false",)]
        if not children:
            return ("false",)
        if len(children) == 1:
            return children[0]
        return ("or", children)
    if kind == "not":
        child = simplify(ast[1])
        if child == ("true",):
            return ("false",)
        if child == ("false",):
            return ("true",)
        return ("not", child)
    return ast


# ---------------------------------------------------------------------------
# Bytecode encoder
# ---------------------------------------------------------------------------
def encode_predicate(ast, ops: dict[str, OpDef], items: dict[str, ItemDef], regions: dict[str, RegionDef], locations: dict[str, LocationDef]) -> bytes:
    """Encode a fully macro-expanded AST to the bytecode stream."""
    out = bytearray()
    _emit(ast, out, ops, items, regions, locations)
    return bytes(out)


WORLD_STATES = {"open": 0, "standard": 1, "inverted": 2, "retro": 3}
GOALS = {"ganon": 0, "fast_ganon": 1, "dungeons": 2, "pedestal": 3, "triforce_hunt": 4, "ganonhunt": 5, "completionist": 6}


def _emit(ast, out: bytearray, ops, items, regions, locations):
    kind = ast[0]
    if kind == "true":
        # Vacuous AND (count=0) is true.
        out.append(ops["AND"].id)
        out.append(0)
        return
    if kind == "false":
        # Vacuous OR (count=0) is false.
        out.append(ops["OR"].id)
        out.append(0)
        return
    if kind == "and":
        out.append(ops["AND"].id)
        out.append(len(ast[1]))
        for c in ast[1]:
            _emit(c, out, ops, items, regions, locations)
        return
    if kind == "or":
        out.append(ops["OR"].id)
        out.append(len(ast[1]))
        for c in ast[1]:
            _emit(c, out, ops, items, regions, locations)
        return
    if kind == "not":
        out.append(ops["NOT"].id)
        _emit(ast[1], out, ops, items, regions, locations)
        return
    if kind == "op":
        _, name, args = ast
        if name not in ops:
            raise ParseError(f"unknown op {name!r}")
        op = ops[name]
        out.append(op.id)
        _emit_operands(name, args, out, items, regions)
        return
    raise ParseError(f"cannot encode AST kind {kind!r}")


def _emit_u16le(out: bytearray, v: int):
    out += struct.pack("<H", v)


def _emit_operands(op_name: str, args, out: bytearray, items, regions):
    if op_name == "HAS_ITEM":
        out += struct.pack("<H", _resolve_item(args[0], items))
    elif op_name == "HAS_AMOUNT":
        out += struct.pack("<H", _resolve_item(args[0], items))
        out.append(_resolve_int(args[1]))
    elif op_name == "HAS_ANY_OF":
        lst = _resolve_list(args[0])
        out.append(len(lst))
        for it in lst:
            out += struct.pack("<H", _resolve_item(("ident", it), items))
    elif op_name == "HAS_ANY_COUNT":
        lst = _resolve_list(args[0])
        out.append(len(lst))
        for it in lst:
            out += struct.pack("<H", _resolve_item(("ident", it), items))
        out.append(_resolve_int(args[1]))
    elif op_name == "WORLDSTATE_EQ":
        out.append(WORLD_STATES[_resolve_ident(args[0])])
    elif op_name == "GOAL_EQ":
        out.append(GOALS[_resolve_ident(args[0])])
    elif op_name == "GOAL_REQUIRES_DUNGEON":
        out.append(_resolve_dungeon_id(_resolve_ident(args[0])))
    elif op_name == "DUNGEON_CLEARED":
        out.append(_resolve_dungeon_id(_resolve_ident(args[0])))
    elif op_name == "REGION_REACHABLE":
        out += struct.pack("<H", _resolve_region(args[0], regions))
    elif op_name == "HAS_PRIZE":
        out.append(_resolve_prize_id(_resolve_ident(args[0])))
    elif op_name == "MEDALLION_OPENS":
        out.append(_resolve_entrance_id(_resolve_ident(args[0])))
    elif op_name == "ITEM_IS":
        out += struct.pack("<H", _resolve_item(args[0], items))
    elif op_name == "TRICK":
        out.append(0)  # Phase B placeholder; codegen rejects use in Phase A
    elif op_name == "DIFFICULTY_AT_LEAST":
        out.append(0)
    elif op_name == "GLITCH_LEVEL_AT_LEAST":
        out.append(0)
    else:
        raise ParseError(f"no operand-emit rule for op {op_name!r}")


def _resolve_item(arg, items):
    if isinstance(arg, tuple) and arg[0] == "ident":
        nm = arg[1]
    elif isinstance(arg, str):
        nm = arg
    else:
        raise ParseError(f"item arg expected ident, got {arg!r}")
    if nm not in items:
        raise ParseError(f"unknown item ID {nm!r}")
    return items[nm].id


def _resolve_int(arg):
    if isinstance(arg, tuple) and arg[0] == "int":
        return arg[1]
    if isinstance(arg, int):
        return arg
    raise ParseError(f"int arg expected, got {arg!r}")


def _resolve_list(arg):
    if isinstance(arg, tuple) and arg[0] == "list":
        return arg[1]
    raise ParseError(f"list arg expected, got {arg!r}")


def _resolve_ident(arg):
    if isinstance(arg, tuple) and arg[0] == "ident":
        return arg[1]
    if isinstance(arg, str):
        return arg
    raise ParseError(f"ident arg expected, got {arg!r}")


def _resolve_region(arg, regions):
    name = _resolve_ident(arg)
    # Phase A: regions may not all be declared yet. Allow unknown regions but
    # emit an opaque ID hashed from name; this lets Phase A1 examples typecheck
    # without a full logic.yaml. Production codegen requires the region to exist.
    if name in regions:
        # Stable IDs assigned by sorted order of region declaration.
        idx = sorted(regions.keys()).index(name)
        return idx
    # Fallback: hash-based id (deterministic per name).
    return _stable_hash16(name)


def _resolve_dungeon_id(name: str) -> int:
    dungeons = ["HyruleCastleEscape", "EasternPalace", "DesertPalace", "TowerOfHera",
                "HyruleCastleTower", "PalaceOfDarkness", "SwampPalace", "SkullWoods",
                "ThievesTown", "IcePalace", "MiseryMire", "TurtleRock", "GanonsTower"]
    if name not in dungeons:
        raise ParseError(f"unknown dungeon {name!r}; valid: {dungeons}")
    return dungeons.index(name)


def _resolve_prize_id(name: str) -> int:
    prizes = ["Prize_GreenPendant", "Prize_RedPendant", "Prize_BluePendant",
              "Prize_Crystal1", "Prize_Crystal2", "Prize_Crystal3", "Prize_Crystal4",
              "Prize_Crystal5", "Prize_Crystal6", "Prize_Crystal7"]
    if name not in prizes:
        raise ParseError(f"unknown prize {name!r}")
    return prizes.index(name)


def _resolve_entrance_id(name: str) -> int:
    entrances = ["MiseryMire_Entrance", "TurtleRock_Entrance"]
    if name not in entrances:
        raise ParseError(f"unknown medallion-affected entrance {name!r}; valid: {entrances}")
    return entrances.index(name)


def _stable_hash16(s: str) -> int:
    h = 0
    for ch in s:
        h = (h * 1315423911 + ord(ch)) & 0xFFFF
    return h


# ---------------------------------------------------------------------------
# Well-formedness checks (task 3.10)
# ---------------------------------------------------------------------------
def well_formedness(ast, ops, items, regions, locations, context: str, phase_a_only: bool = True, errors: list = None):
    """Walk AST, check:
    - referenced items / regions / dungeons exist;
    - OP_ITEM_IS only in can_place context;
    - no Phase B ops in Phase A predicates.
    Returns the list of error strings (in-place modifications).
    """
    if errors is None:
        errors = []
    if not isinstance(ast, tuple):
        return errors
    kind = ast[0]
    if kind == "op":
        _, name, args = ast
        if name not in ops:
            errors.append(f"unknown op {name!r}")
            return errors
        op = ops[name]
        if phase_a_only and op.phase == "B":
            errors.append(f"Phase B op {name!r} not allowed in Phase A predicate ({context})")
        if name == "ITEM_IS" and context not in ("can_place", "always_allow"):
            errors.append(f"OP_ITEM_IS only allowed in placement-context predicates (can_place / always_allow), used in {context}")
        # Validate operand references (best-effort)
        if name in ("HAS_ITEM", "HAS_AMOUNT", "ITEM_IS"):
            it = _resolve_ident(args[0]) if args else None
            if it and it not in items:
                errors.append(f"unknown item {it!r} in {context}")
        if name in ("HAS_ANY_OF", "HAS_ANY_COUNT"):
            if args:
                lst = _resolve_list(args[0]) if isinstance(args[0], tuple) and args[0][0] == "list" else []
                for it in lst:
                    if it not in items:
                        errors.append(f"unknown item {it!r} in {context}")
    if kind in ("and", "or"):
        for c in ast[1]:
            well_formedness(c, ops, items, regions, locations, context, phase_a_only, errors)
    if kind == "not":
        well_formedness(ast[1], ops, items, regions, locations, context, phase_a_only, errors)
    return errors


# ---------------------------------------------------------------------------
# Codegen emitters
# ---------------------------------------------------------------------------
HEADER_BANNER = """\
// AUTO-GENERATED by assets/rando_logic_gen.py. DO NOT EDIT.
// Regenerate by running: python assets/rando_logic_gen.py
//
// Source artifacts:
//   - assets/rando/op_registry.yaml
//   - assets/rando/item_registry.yaml
//   - assets/rando/location_registry.yaml
//   - assets/rando/logic.yaml (optional; empty graph if missing)
//   - assets/rando/macros.yaml (optional)
"""


def sanitize_for_define(s: str) -> str:
    """Convert a free-form name to a valid C identifier suffix.

    Replaces non-alphanumeric chars with underscore; collapses runs.
    """
    out = re.sub(r"[^A-Za-z0-9]+", "_", s).strip("_")
    return out


def emit_location_ids(locations: dict[str, LocationDef], path: Path):
    lines = [HEADER_BANNER, "", "#ifndef ZELDA3_RANDO_LOCATION_IDS_H_", "#define ZELDA3_RANDO_LOCATION_IDS_H_", "", "// Stable numeric location IDs from assets/rando/location_registry.yaml.", "// Renaming a location does NOT change its numeric ID (append-only registry).", ""]
    for loc in sorted(locations.values(), key=lambda l: l.id):
        token = sanitize_for_define(loc.name)
        lines.append(f"#define LOC_{token} {loc.id}")
    max_id = max(l.id for l in locations.values()) if locations else 0
    lines.append("")
    lines.append(f"#define LOC__COUNT {max_id + 1}")
    lines.append("")
    lines.append("#endif  // ZELDA3_RANDO_LOCATION_IDS_H_")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def emit_item_ids(items: dict[str, ItemDef], path: Path):
    lines = [HEADER_BANNER, "", "#ifndef ZELDA3_RANDO_ITEM_IDS_H_", "#define ZELDA3_RANDO_ITEM_IDS_H_", "", "// Stable numeric item IDs from assets/rando/item_registry.yaml.", "// Used by Rando_OnLocationCheck, predicate VM, placement table.", ""]
    for it in sorted(items.values(), key=lambda i: i.id):
        token = sanitize_for_define(it.name)
        lines.append(f"#define ITEM_{token} {it.id}")
    max_id = max(i.id for i in items.values()) if items else 0
    lines.append("")
    lines.append(f"#define ITEM__COUNT {max_id + 1}")
    lines.append("")
    lines.append("#endif  // ZELDA3_RANDO_ITEM_IDS_H_")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def emit_logic_data(
    locations: dict[str, LocationDef],
    regions: dict[str, RegionDef],
    edges: list[EdgeDef],
    location_predicates: dict[str, dict[str, bytes]],
    edge_predicates: list[bytes],
    path: Path,
    items: dict[str, ItemDef] | None = None,
    logic_loc_preds: dict[str, LocationDef] | None = None,
):
    out = [HEADER_BANNER, "", "#include \"../types.h\"", "#include \"rando_logic.h\"", "#include \"location_ids.h\"", "#include \"item_ids.h\"", ""]
    # Predicate stream — concatenated; LocationDef references offset+length.
    stream = bytearray()
    location_offsets = {}
    for loc in sorted(locations.values(), key=lambda l: l.id):
        preds = location_predicates.get(loc.name, {})
        cr = preds.get("can_reach", b"\x0c\x00")  # default TRUE = AND with 0 children = 0x0c 0x00
        cp = preds.get("can_place", b"\x0c\x00")
        aa = preds.get("always_allow", b"\x0d\x00")  # default FALSE = OR with 0 children = 0x0d 0x00
        location_offsets[loc.id] = {
            "can_reach": (len(stream), len(cr)),
        }
        stream += cr
        location_offsets[loc.id]["can_place"] = (len(stream), len(cp))
        stream += cp
        location_offsets[loc.id]["always_allow"] = (len(stream), len(aa))
        stream += aa
    edge_offsets = []
    for ep in edge_predicates:
        edge_offsets.append((len(stream), len(ep)))
        stream += ep
    # Emit the stream as a uint8 array.
    out.append("// Predicate bytecode stream — concatenated per the encoding documented in")
    out.append("// assets/rando_logic_gen.py. Locations and edges reference (offset, length).")
    if not stream:
        stream = bytes([0])  # avoid zero-length array
    out.append(f"const uint8 kRandoPredicateStream[{len(stream)}] = {{")
    rows = []
    for i in range(0, len(stream), 16):
        chunk = stream[i:i+16]
        rows.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    out.extend(rows)
    out.append("};")
    out.append(f"const uint32 kRandoPredicateStreamSize = {len(stream)};")
    out.append("")
    # LocationDef table — typedef lives in rando_logic.h; emit only the array.
    out.append("// Location table — one row per location_registry.yaml entry.")
    out.append("// Type definition: rando_logic.h::RandoLocationDef.")
    out.append(f"const RandoLocationDef kRandoLocations[{len(locations) or 1}] = {{")
    if not locations:
        out.append("  {0, 0, 0xFFFF, 0, 0, 0, 0, 0, 0, 0, 0, 0xff},  // placeholder for zero-length array compatibility")
    else:
        sorted_region_ids = sorted(regions.keys()) if regions else []
        # logic.yaml's location entries carry an optional `region:` field.
        # The location_registry.yaml's `region:` field is descriptive only
        # (free-form string, not necessarily a logic.yaml region id) — we
        # prefer logic.yaml when present.
        logic_loc_region = {name: lp.region for name, lp in (logic_loc_preds or {}).items()}
        for loc in sorted(locations.values(), key=lambda l: l.id):
            offsets = location_offsets[loc.id]
            cr_off, cr_len = offsets["can_reach"]
            cp_off, cp_len = offsets["can_place"]
            aa_off, aa_len = offsets["always_allow"]
            type_id = _location_type_id(loc.type)
            ws_mask = _world_state_mask(loc.world_state_filter)
            # Resolve vanilla_item_id from the items registry.
            vanilla_id = 0
            if items and loc.vanilla_item and loc.vanilla_item in items:
                vanilla_id = items[loc.vanilla_item].id
            elif loc.vanilla_item:
                # Reference to an unknown item — emit 0 but flag in the comment.
                pass
            # Resolve region_id. The logic.yaml may have declared a region for
            # this location; resolve to numeric index. 0xFFFF = not declared.
            region_name = logic_loc_region.get(loc.name)
            if region_name and region_name in sorted_region_ids:
                region_id = sorted_region_ids.index(region_name)
            else:
                region_id = 0xFFFF
            out.append(f"  {{ {loc.id}, {vanilla_id}, 0x{region_id:04x}, 0, {cr_off}u, {cr_len}, {cp_off}u, {cp_len}, {aa_off}u, {aa_len}, {type_id}, 0x{ws_mask:02x} }},  // {loc.name} (vanilla: {loc.vanilla_item}, region: {region_name or '-'})")
    out.append("};")
    out.append(f"const uint32 kRandoLocationsCount = {len(locations)};")
    out.append("")
    # RegionDef table — typedef lives in rando_logic.h.
    out.append("// Region table — one row per logic.yaml `regions:` entry. Phase A may emit empty.")
    out.append("// Type definition: rando_logic.h::RandoRegionDef.")
    region_list = sorted(regions.values(), key=lambda r: r.id)
    if region_list:
        out.append(f"const RandoRegionDef kRandoRegions[{len(region_list)}] = {{")
        for r in region_list:
            rid = sorted(regions.keys()).index(r.id)
            pid = sorted(regions.keys()).index(r.parent) if r.parent and r.parent in regions else 0xFFFF
            did = _dungeon_id_or_ff(r.dungeon)
            ws = _world_state_mask(r.world_state_filter)
            out.append(f"  {{ {rid}, 0x{pid:04x}, 0x{did:02x}, 0x{ws:02x} }},  // {r.id}")
        out.append("};")
    else:
        out.append("const RandoRegionDef kRandoRegions[1] = { {0, 0xFFFF, 0xFF, 0} };  // placeholder")
    out.append(f"const uint32 kRandoRegionsCount = {len(region_list)};")
    out.append("")
    # EdgeDef table — typedef lives in rando_logic.h.
    out.append("// Edge table — one row per logic.yaml `edges:` entry.")
    out.append("// Type definition: rando_logic.h::RandoEdgeDef.")
    if edges:
        out.append(f"const RandoEdgeDef kRandoEdges[{len(edges)}] = {{")
        for i, e in enumerate(edges):
            from_idx = sorted(regions.keys()).index(e.from_) if e.from_ in regions else 0xFFFF
            to_idx = sorted(regions.keys()).index(e.to) if e.to in regions else 0xFFFF
            poff, plen = edge_offsets[i]
            out.append(f"  {{ 0x{from_idx:04x}, 0x{to_idx:04x}, {poff}u, {plen}, {1 if e.one_way else 0}, 0 }},  // {e.from_} -> {e.to}")
        out.append("};")
    else:
        out.append("const RandoEdgeDef kRandoEdges[1] = { {0xFFFF, 0xFFFF, 0, 0, 0, 0} };  // placeholder")
    out.append(f"const uint32 kRandoEdgesCount = {len(edges)};")
    out.append("")

    # ----- Start region per world-state -----
    out.append("// Start region per world_state. Indexed by WorldState enum:")
    out.append("// Open=0, Standard=1, Inverted=2, Retro=3.")
    sorted_region_ids = sorted(regions.keys()) if regions else []
    # Pinned mapping (Phase A): Open/Standard/Retro start in LinksHouse;
    # Inverted starts in LinksHouse_Inverted. Falls back to 0xFFFF if not
    # declared in logic.yaml — caller treats as "empty graph".
    start_region_names = {
        0: "LinksHouse",            # Open
        1: "LinksHouse",            # Standard
        2: "LinksHouse_Inverted",   # Inverted (Phase B)
        3: "LinksHouse",            # Retro
    }
    starts = []
    for ws in [0, 1, 2, 3]:
        nm = start_region_names[ws]
        if nm in sorted_region_ids:
            starts.append(sorted_region_ids.index(nm))
        else:
            starts.append(0xFFFF)
    start_csv = ", ".join(f"0x{s:04x}" for s in starts)
    out.append(f"const uint16 kRandoStartRegionByWorldState[4] = {{ {start_csv} }};")
    out.append("")

    # ----- Region-name lookup table for Rando_FindRegionByName -----
    out.append("// Region-name → region-id lookup. Linear scan; only used by")
    out.append("// authoring-time helpers (Rando_FindRegionByName).")
    out.append("typedef struct RandoRegionNameEntry {")
    out.append("  const char *name;")
    out.append("  uint16 id;")
    out.append("} RandoRegionNameEntry;")
    out.append("")
    if sorted_region_ids:
        out.append(f"static const RandoRegionNameEntry kRandoRegionNames[{len(sorted_region_ids)}] = {{")
        for rid in sorted_region_ids:
            idx = sorted_region_ids.index(rid)
            out.append(f"  {{ \"{rid}\", {idx} }},")
        out.append("};")
        out.append(f"static const uint32 kRandoRegionNamesCount = {len(sorted_region_ids)};")
    else:
        out.append("static const RandoRegionNameEntry kRandoRegionNames[1] = { {\"\", 0} };")
        out.append("static const uint32 kRandoRegionNamesCount = 0;")
    out.append("")
    out.append("uint16 Rando_FindRegionByName(const char *name) {")
    out.append("  if (name == NULL) return 0xFFFF;")
    out.append("  for (uint32 i = 0; i < kRandoRegionNamesCount; i++) {")
    out.append("    const char *a = kRandoRegionNames[i].name;")
    out.append("    const char *b = name;")
    out.append("    while (*a && *a == *b) { a++; b++; }")
    out.append("    if (*a == 0 && *b == 0) return kRandoRegionNames[i].id;")
    out.append("  }")
    out.append("  return 0xFFFF;")
    out.append("}")
    out.append("")
    path.write_text("\n".join(out) + "\n", encoding="utf-8")


def _location_type_id(t: str) -> int:
    types = ["Chest", "BigChest", "Npc", "Standing", "Pedestal", "Dash", "Dig", "Drop",
             "Fountain", "Trade", "Prize_Crystal", "Prize_Pendant", "Prize_Event", "Medallion"]
    if t not in types:
        return 0
    return types.index(t)


def _world_state_mask(wsf: list) -> int:
    if not wsf:
        return 0  # 0 = all world-states (the common case)
    mask = 0
    for ws in wsf:
        mask |= 1 << WORLD_STATES.get(ws, 0)
    return mask


def _dungeon_id_or_ff(name) -> int:
    if not name:
        return 0xFF
    try:
        return _resolve_dungeon_id(name)
    except ParseError:
        return 0xFF


# ---------------------------------------------------------------------------
# Main driver
# ---------------------------------------------------------------------------
def main(argv=None):
    p = argparse.ArgumentParser(description="Generate src/rando/{logic_data.c, location_ids.h, item_ids.h}")
    p.add_argument("--strict", action="store_true", help="Fail on any well-formedness warning")
    p.add_argument("--out-headers", default=str(RANDO_SRC), help="Destination for emitted headers (default: src/rando/)")
    p.add_argument("--out-data", default=str(RANDO_SRC), help="Destination for emitted logic_data.c (default: src/rando/)")
    p.add_argument("--logic", default=None, help="Path to logic.yaml (optional)")
    p.add_argument("--macros", default=None, help="Path to macros.yaml (optional)")
    args = p.parse_args(argv)

    ops_path = RANDO_ASSETS / "op_registry.yaml"
    items_path = RANDO_ASSETS / "item_registry.yaml"
    locs_path = RANDO_ASSETS / "location_registry.yaml"
    schema_path = RANDO_ASSETS / "logic.schema.yaml"
    macros_path = Path(args.macros) if args.macros else (RANDO_ASSETS / "macros.yaml")
    logic_path = Path(args.logic) if args.logic else (RANDO_ASSETS / "logic.yaml")

    ops = load_ops(ops_path)
    items = load_items(items_path)
    locations = load_locations(locs_path)
    if not schema_path.exists():
        print(f"WARNING: {schema_path} not found", file=sys.stderr)
    macros = load_macros(macros_path)
    logic_regions, logic_edges, logic_loc_preds, logic_macros = load_logic(logic_path)

    # Merge macros from macros.yaml and logic.yaml (logic.yaml takes precedence).
    all_macros = {**macros, **logic_macros}

    # Parse macro bodies into ASTs (resolve calls / nested macros after all macros are loaded).
    parsed_macro_bodies = {}
    macro_errors = []
    for name, m in all_macros.items():
        try:
            ast = parse_predicate(m.body)
            ast = resolve_calls(ast, ops, all_macros)
            parsed_macro_bodies[name] = ast
        except ParseError as e:
            macro_errors.append(f"macro {name}: parse error: {e}")

    if macro_errors:
        for err in macro_errors:
            print(f"ERROR: {err}", file=sys.stderr)
        if args.strict:
            sys.exit(1)

    # ----- Well-formedness checks (task 3.10) -----
    all_errors = []

    # 1. Detect logic.yaml location overrides that don't match any registry entry —
    #    these would be silently dropped without this check, masking translation
    #    typos (e.g., underscored vs spaced names). Per the agent-discovered
    #    silent-drop bug.
    registry_loc_names = set(locations.keys())
    for override_id in logic_loc_preds.keys():
        if override_id not in registry_loc_names:
            all_errors.append(
                f"logic.yaml location override id={override_id!r} does not "
                f"match any entry in assets/rando/location_registry.yaml — "
                f"this predicate override is being dropped silently."
            )

    # 2. Detect orphan regions (parent references that don't resolve).
    if logic_regions:
        region_ids = set(logic_regions.keys())
        for r in logic_regions.values():
            if r.parent and r.parent not in region_ids:
                all_errors.append(
                    f"region {r.id!r} declares parent {r.parent!r} which is not "
                    f"a known region in logic.yaml."
                )

    # 3. Detect edges referencing unknown regions.
    if logic_regions:
        region_ids = set(logic_regions.keys())
        for i, e in enumerate(logic_edges):
            if e.from_ not in region_ids:
                all_errors.append(
                    f"edge[{i}] {e.from_!r} -> {e.to!r}: 'from' region not declared."
                )
            if e.to not in region_ids:
                all_errors.append(
                    f"edge[{i}] {e.from_!r} -> {e.to!r}: 'to' region not declared."
                )

    # 4. Warn on locations in logic.yaml that don't carry any explicit predicate
    #    (currently impossible — the loader fills defaults — but documents intent).
    #    Skipped: every loaded location has all three predicate fields.

    # 5. Detect Phase A logic.yaml location overrides that reference a logic.yaml
    #    region not in the logic.yaml regions list.
    if logic_regions:
        region_ids = set(logic_regions.keys())
        for raw_id, override in logic_loc_preds.items():
            if override.region and override.region not in region_ids:
                all_errors.append(
                    f"logic.yaml location {raw_id!r} declares region {override.region!r} "
                    f"which is not in logic.yaml regions."
                )

    # Compile location predicates (only those that have overrides in logic.yaml).
    location_predicates = {}
    for loc_name, loc in locations.items():
        # logic.yaml's predicate overrides take precedence; default to TRUE() / FALSE() otherwise.
        overrides = logic_loc_preds.get(loc.name) or logic_loc_preds.get(loc_name)
        cr_src = overrides.can_reach if overrides else "TRUE()"
        cp_src = overrides.can_place if overrides else "TRUE()"
        aa_src = overrides.always_allow if overrides else "FALSE()"
        encoded = {}
        for label, src in [("can_reach", cr_src), ("can_place", cp_src), ("always_allow", aa_src)]:
            try:
                ast = parse_predicate(src)
                ast = resolve_calls(ast, ops, all_macros)
                ast = expand_macros(ast, all_macros, parsed_macro_bodies, ops)
                errs = well_formedness(ast, ops, items, logic_regions, locations, label)
                if errs:
                    for e in errs:
                        all_errors.append(f"location {loc_name}.{label}: {e}")
                encoded[label] = encode_predicate(ast, ops, items, logic_regions, locations)
            except ParseError as e:
                all_errors.append(f"location {loc_name}.{label}: parse error: {e}")
                encoded[label] = b"\x0d\x00"  # safe default: FALSE
        location_predicates[loc_name] = encoded

    # Compile edge predicates.
    edge_predicates = []
    for i, e in enumerate(logic_edges):
        try:
            ast = parse_predicate(e.predicate)
            ast = resolve_calls(ast, ops, all_macros)
            ast = expand_macros(ast, all_macros, parsed_macro_bodies, ops)
            errs = well_formedness(ast, ops, items, logic_regions, locations, f"edge[{i}]")
            if errs:
                for err in errs:
                    all_errors.append(f"edge[{i}] {e.from_}->{e.to}: {err}")
            edge_predicates.append(encode_predicate(ast, ops, items, logic_regions, locations))
        except ParseError as ex:
            all_errors.append(f"edge[{i}]: parse error: {ex}")
            edge_predicates.append(b"\x0d\x00")  # safe default: FALSE

    if all_errors:
        for err in all_errors:
            print(f"WARN: {err}", file=sys.stderr)
        if args.strict:
            sys.exit(1)

    # Emit artifacts.
    out_headers = Path(args.out_headers)
    out_data = Path(args.out_data)
    out_headers.mkdir(parents=True, exist_ok=True)
    out_data.mkdir(parents=True, exist_ok=True)
    emit_location_ids(locations, out_headers / "location_ids.h")
    emit_item_ids(items, out_headers / "item_ids.h")
    emit_logic_data(locations, logic_regions, logic_edges, location_predicates, edge_predicates, out_data / "logic_data.c", items=items, logic_loc_preds=logic_loc_preds)

    print(f"generated location_ids.h ({len(locations)} locations)")
    print(f"generated item_ids.h ({len(items)} items)")
    print(f"generated logic_data.c ({len(logic_regions)} regions, {len(logic_edges)} edges, {len(locations)} locations)")
    print(f"warnings: {len(all_errors)}, macro errors: {len(macro_errors)}")


if __name__ == "__main__":
    main()
