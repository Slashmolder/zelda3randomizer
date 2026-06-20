#!/usr/bin/env python3
"""Convert z3randomizer/invertedmaps.asm into a static C table for the fork.

This is a one-shot transcription helper, NOT part of the asset pipeline or the
rando codegen. The Inverted overworld topology is a single fixed data set (it
does not vary by seed), so the output is a hand-checked-in C source file:
src/rando/inverted_maps.c (+ .h). Re-run only if invertedmaps.asm changes.

Source: KatDevsGames/z3randomizer invertedmaps.asm (the OverworldMapChangePointers
RLE tile-overlay data driving Overworld_LoadNewTiles).

The fork's interpreter (Overworld_ApplyInvertedTiles in overworld.c) consumes the
SAME wire format as the asm interpreter, so we emit the `dw` stream verbatim as
uint16 words, with two transforms applied at conversion time because the fork's
engine only ever runs when ALREADY inverted (gated on Rando_GetActiveWorldState()
== kWorldState_Inverted):

  * !OWW_InvertedOnly        -> dropped (never cancels; we are always inverted)
  * !OWW_SkipIfInverted,addr -> unconditional jump to addr (always inverted), so
                                we splice in the post-label stream and drop the
                                pre-label words that the jump skips.
  * !OWW_SkipIfNotInverted    -> (not present in data, but would be) dropped.

The two !OWW_CustomCommand vectors (.map1B_check_aga pyramid-hole gate and
.map5B_pick_warp_tile progress-gated warp tile) carry real asm logic; they are
re-encoded as a fork-private opcode OWW_CMD_CUSTOM with a small custom-id the C
interpreter dispatches. See OWW_CUSTOM_* below.
"""
import re, sys, os

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
ASM = os.path.join(os.path.dirname(REPO_ROOT), 'z3randomizer', 'invertedmaps.asm')
env_dir = os.environ.get('Z3RANDOMIZER_DIR')
CANDIDATES = [
    ASM,
]
if env_dir:
    CANDIDATES.append(os.path.join(env_dir, 'invertedmaps.asm'))

# Command opcodes (high byte | id), mirroring invertedmaps.asm .command_vectors.
OWW_END        = 0xFFFF
OWW_STOP       = 0x8000
OWW_SKIP       = 0xFFFF
OWW_VERTICAL   = 0x0080
OWW_HORIZONTAL = 0x0000

CMD = {
    'Stripe':       0x8000,
    'StripeRLE':    0x8001,
    'StripeRLEINC': 0x8002,
    'ArbTileCopy':  0x8003,
    # SkipIfInverted/SkipIfNotInverted/InvertedOnly resolved at conversion time.
    'CustomCommand': 0x8010,  # fork re-encodes as OWW_CMD_CUSTOM (see below)
}
# Fork-private re-encoding of the two CustomCommand vectors.
OWW_CMD_CUSTOM = 0x8011  # followed by custom-id word
OWW_CUSTOM_MAP1B_AGA = 0
OWW_CUSTOM_MAP5B_WARP = 1
CUSTOM_LABEL = {
    '.map1B_check_aga': OWW_CUSTOM_MAP1B_AGA,
    '.map5B_pick_warp_tile': OWW_CUSTOM_MAP5B_WARP,
}


def find_asm():
    for c in CANDIDATES:
        if os.path.isfile(c):
            return c
    sys.exit('cannot locate invertedmaps.asm in: ' + ', '.join(CANDIDATES))


def strip_comment(line):
    # remove ; comments, keep code
    i = line.find(';')
    return (line if i < 0 else line[:i]).strip()


def eval_token(tok, rle_size_ctx):
    """Evaluate one OR-ed term to a 16-bit value, or return a sentinel string."""
    tok = tok.strip()
    if tok == '':
        return None
    if tok.startswith('$'):
        return int(tok[1:], 16) & 0xFFFF
    if tok == '!OWW_STOP':
        return OWW_STOP
    if tok == '!OWW_SKIP':
        return OWW_SKIP
    if tok == '!OWW_END':
        return OWW_END
    if tok == '!OWW_Vertical':
        return OWW_VERTICAL
    if tok == '!OWW_Horizontal':
        return OWW_HORIZONTAL
    if tok == '!OWW_InvertedOnly':
        return 'INVONLY'
    if tok == '!OWW_SkipIfInverted':
        return 'SKIPIFINV'
    if tok == '!OWW_SkipIfNotInverted':
        return 'SKIPIFNOTINV'
    if tok == '!OWW_CustomCommand':
        return 'CUSTOM'
    for name, val in CMD.items():
        if tok == '!OWW_' + name:
            return val
    m = re.match(r'OWW_RLESize\((\d+)\)', tok)
    if m:
        return int(m.group(1)) << 8
    if tok.startswith('.') or tok.startswith('#'):
        # a label reference (used as a word, e.g. jump target or custom vector)
        return ('LABEL', tok.lstrip('#'))
    raise ValueError('cannot evaluate token: %r' % tok)


def parse_dw(expr):
    """Parse one `dw a, b|c, d` expression into a list of word-values.
    Each comma-separated field may itself be OR-ed terms (a|b|c)."""
    words = []
    for field in expr.split(','):
        field = field.strip()
        if not field:
            continue
        parts = field.split('|')
        acc = 0
        labelref = None
        for p in parts:
            v = eval_token(p, None)
            if isinstance(v, tuple) and v[0] == 'LABEL':
                labelref = v[1]
            elif isinstance(v, str):
                acc = v  # sentinel macro like INVONLY / SKIPIFINV / CUSTOM
            elif v is not None:
                if isinstance(acc, str):
                    # sentinel OR value (shouldn't happen) – keep sentinel
                    pass
                else:
                    acc |= v
        if labelref is not None:
            words.append(('LABEL', labelref))
        else:
            words.append(acc)
    return words


def main():
    path = find_asm()
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        lines = f.readlines()

    # Locate the data region: from the first ".mapNN" after OverworldMapChangePointers
    # to end of file.
    start = None
    for i, ln in enumerate(lines):
        if ln.strip() == 'OverworldMapChangePointers:':
            start = i
            break
    if start is None:
        sys.exit('OverworldMapChangePointers not found')

    # 1) parse the pointer table (screen -> label or 0)
    screen_label = {}  # screen index -> label name or None
    i = start + 1
    ptr_re = re.compile(r'dw\s+(\$0000|\.map[0-9A-Fa-f]+)\s*;\s*([0-9A-Fa-f]{2})')
    while i < len(lines):
        s = strip_comment(lines[i])
        m = re.search(r'dw\s+(\$0000|\.map[0-9A-Fa-f]+)\s*$', s)
        # also accept lines with trailing index comment
        raw = lines[i]
        mm = ptr_re.search(raw)
        if mm:
            target = mm.group(1)
            scr = int(mm.group(2), 16)
            screen_label[scr] = None if target == '$0000' else target
            i += 1
            if scr == 0x7F:
                break
            continue
        i += 1
        if i - start > 300:
            break

    if 0x7F not in screen_label:
        sys.exit('did not parse full 0x00..0x7F pointer table (got %d entries)'
                 % len(screen_label))

    # 2) parse each .mapNN block into a token stream, recording label positions.
    # We collect, for each label (.mapNN and intra-screen labels), the linear
    # sequence of words plus where labels fall, so we can resolve jumps.
    data_start = None
    for i in range(start, len(lines)):
        if lines[i].strip().startswith('.map') and lines[i].strip().rstrip().endswith(
                lines[i].strip()):
            # first label that is a screen entry
            if re.match(r'\.map[0-9A-Fa-f]+\s*$', lines[i].strip()):
                data_start = i
                break
    if data_start is None:
        sys.exit('no .mapNN data blocks found')

    # Build a flat token list with label markers and custom-asm blocks excluded.
    # We treat the whole region as a sequence; labels are anchors. Custom command
    # asm bodies (.map1B_check_aga / .map5B_pick_warp_tile) are NOT data — skip
    # their instruction lines, but record the label so CustomCommand refs resolve.
    tokens = []  # list of ('label', name) | ('word', value) | ('labelword', name)
    in_custom_asm = None
    for i in range(data_start, len(lines)):
        raw = lines[i]
        s = strip_comment(raw)
        if not s:
            continue
        lab = re.match(r'(\.[A-Za-z0-9_]+|#[A-Za-z0-9_]+)\s*:?\s*$', s)
        if lab:
            name = lab.group(1).lstrip('#')
            tokens.append(('label', name))
            in_custom_asm = (name in CUSTOM_LABEL)
            continue
        if in_custom_asm:
            # skip asm instructions inside a custom-command body
            continue
        m = re.match(r'dw\s+(.*)$', s)
        if m:
            for w in parse_dw(m.group(1)):
                if isinstance(w, tuple) and w[0] == 'LABEL':
                    tokens.append(('labelword', w[1]))
                else:
                    tokens.append(('word', w))
            continue
        # Lines like 'function ...' or stray; ignore inside data region only if
        # they are not instructions we care about.
        if s.startswith('function') or s.startswith('!') or s.startswith('org'):
            continue
        # Unknown line in data region -> hard error so we never silently drop data.
        raise ValueError('unrecognized data line %d: %r' % (i + 1, raw.rstrip()))

    # index label positions in the token list
    label_pos = {}
    for idx, t in enumerate(tokens):
        if t[0] == 'label':
            label_pos[t[1]] = idx

    # 3) emit per-screen word streams by walking tokens from each screen label
    #    until OWW_END, applying always-inverted transforms.
    # Structural walker. The top-level loop only treats 0xFFFF as OWW_END; inside
    # a command's operand list (stripe tiles etc.) 0xFFFF is OWW_SKIP and must not
    # terminate the stream. Mirrors the asm interpreter's command dispatch.
    def emit_stream(start_label):
        out = []
        idx = [label_pos[start_label] + 1]
        # back-patch state for the map1B Agahnim-hole conditional skip count
        pending_hole_patch = [None]  # index into out[] of the placeholder word

        def map1b_hole_word_count():
            # placeholder; real value back-patched when .map1B_no_hole is reached
            pending_hole_patch[0] = len(out)
            return 0

        def cur():
            return tokens[idx[0]]

        def take_word():
            t = tokens[idx[0]]
            idx[0] += 1
            return t

        def emit_stripe_tilelist():
            # tiles until a word with bit15 set that is NOT bare SKIP(0xFFFF):
            # 0xFFFF -> skip-continue; (bit15 set, !=0xFFFF) -> place&7FFF, stop.
            while True:
                t = take_word()
                assert t[0] == 'word', t
                v = t[1]
                out.append(v)
                if v == OWW_SKIP:
                    continue
                if v & 0x8000:
                    return

        while idx[0] < len(tokens):
            t = cur()
            if t[0] == 'label':
                if t[1] == '.map1B_no_hole' and pending_hole_patch[0] is not None:
                    # words from just after the placeholder to here = skippable
                    # hole data. The placeholder itself is the count word, so the
                    # applier skips (count) words after consuming it.
                    out[pending_hole_patch[0]] = len(out) - pending_hole_patch[0] - 1
                    pending_hole_patch[0] = None
                idx[0] += 1
                continue
            if t[0] == 'labelword':
                idx[0] += 1
                continue
            val = t[1]
            # sentinel macros (conversion-time, always-inverted)
            if val == 'INVONLY':
                idx[0] += 1
                continue
            if val == 'SKIPIFINV':
                tgt = tokens[idx[0] + 1]
                assert tgt[0] == 'labelword', tgt
                idx[0] = label_pos[tgt[1]] + 1
                continue
            if val == 'SKIPIFNOTINV':
                idx[0] += 2
                continue
            if val == 'CUSTOM':
                tgt = tokens[idx[0] + 1]
                assert tgt[0] == 'labelword', tgt
                cid = CUSTOM_LABEL[tgt[1]]
                out.append(OWW_CMD_CUSTOM)
                out.append(cid)
                idx[0] += 2
                if cid == OWW_CUSTOM_MAP1B_AGA:
                    # The asm body jumps to .map1B_no_hole when Agahnim is NOT
                    # defeated, skipping the pyramid-hole tile data. Emit the
                    # word-count of that skippable data so the C applier can
                    # conditionally jump over it. The no-hole label's data
                    # follows immediately after the hole data in emission order.
                    out.append(map1b_hole_word_count())
                continue
            if val == OWW_END:
                out.append(OWW_END)
                return out
            # command opcodes carry structured operands
            base = val & 0x80FF  # strip size/direction bits for opcode id
            opid = val & 0x00FF
            if val & 0x8000 and opid in (0x00, 0x01, 0x02, 0x03):
                out.append(take_word()[1])  # the command word itself
                if opid == 0x00:  # Stripe: <start>, tile-list
                    out.append(take_word()[1])  # start address
                    emit_stripe_tilelist()
                elif opid in (0x01, 0x02):  # StripeRLE / StripeRLEINC: <tile>,<start>
                    out.append(take_word()[1])  # tile
                    out.append(take_word()[1])  # start
                elif opid == 0x03:  # ArbTileCopy: <tile>, pos-list (STOP on bit15)
                    out.append(take_word()[1])  # tile
                    while True:
                        w = take_word()[1]
                        out.append(w)
                        if w & 0x8000:
                            break
                continue
            # plain single tile-write: <tile>, <pos>
            out.append(take_word()[1])  # tile
            out.append(take_word()[1])  # pos
        raise ValueError('stream for %s ran off end without OWW_END' % start_label)

    # 4) build flat array + per-screen offset table
    flat = []
    offsets = {}
    for scr in range(0x80):
        lbl = screen_label.get(scr)
        if lbl is None:
            offsets[scr] = 0xFFFF  # sentinel: no overlay
            continue
        offsets[scr] = len(flat)
        flat.extend(emit_stream(lbl))

    # 5) write C source
    out_c = os.path.join(os.path.dirname(__file__), '..', '..', 'src', 'rando',
                         'inverted_maps.c')
    out_c = os.path.normpath(out_c)
    with open(out_c, 'w', encoding='utf-8', newline='\n') as f:
        f.write('// GENERATED by assets/scripts/gen_inverted_maps.py from\n')
        f.write('// z3randomizer/invertedmaps.asm — DO NOT EDIT BY HAND.\n')
        f.write('// Inverted overworld per-screen RLE tile-overlay data + offset table.\n')
        f.write('// One fixed topology (not seed-varying); see the generator header.\n')
        f.write('#include "inverted_maps.h"\n\n')
        f.write('// Per-screen byte/word stream consumed by Overworld_ApplyInvertedTiles.\n')
        f.write('const uint16 kInvertedMapData[] = {\n')
        # emit with screen-boundary comments
        # build reverse offset->screen list
        scr_at = {}
        for scr in range(0x80):
            if offsets[scr] != 0xFFFF:
                scr_at.setdefault(offsets[scr], []).append(scr)
        col = 0
        for i2, w in enumerate(flat):
            if i2 in scr_at:
                if col:
                    f.write('\n')
                    col = 0
                f.write('  // screen 0x%02X\n' % scr_at[i2][0])
            if col == 0:
                f.write('  ')
            f.write('0x%04X,' % w)
            col += 1
            if col == 12:
                f.write('\n')
                col = 0
            else:
                f.write(' ')
        if col:
            f.write('\n')
        f.write('};\n')
        f.write('const unsigned kInvertedMapDataLen = %d;\n\n' % len(flat))
        f.write('// Per-screen index into kInvertedMapData; 0xFFFF = no overlay.\n')
        f.write('const uint16 kInvertedMapOffsets[128] = {\n')
        for row in range(0, 128, 8):
            f.write('  ')
            f.write(' '.join('0x%04X,' % offsets[scr] for scr in range(row, row + 8)))
            f.write('  // 0x%02X\n' % row)
        f.write('};\n')

    print('wrote %s' % out_c)
    print('flat words: %d  screens with overlay: %d'
          % (len(flat), sum(1 for s in range(0x80) if offsets[s] != 0xFFFF)))


if __name__ == '__main__':
    main()
