#!/usr/bin/env python3
"""Sync the byte arrays in a logo block from their // ASCII comments.

Only touches the `header_pico8` entry in rg_logos.c. The ASCII comment is
the source of truth: '#' = set pixel, '_' or ' ' = clear pixel. Each row is
padded to a whole number of bytes (MSB first), so e.g. width 138 → 18 bytes
(144 bits), with the last 6 bits being padding.

Usage:
    python tools/ascii_to_logo.py [path/to/rg_logos.c]
"""
import re
import sys

DEFAULT_PATH = "Core/Src/retro-go/rg_logos.c"
ENTRY_NAME = "header_pico8"

def ascii_to_bytes(ascii_row, width):
    """Convert an ASCII row of '#'/'_'/' ' into a list of bytes (MSB first)."""
    # Normalize: only keep first `width` non-padding chars; treat space/'_' as 0
    bits = []
    for ch in ascii_row:
        if ch == "#":
            bits.append(1)
        elif ch == "_" or ch == " ":
            bits.append(0)
        # ignore anything else (tabs, etc.)
        if len(bits) >= width:
            break
    if len(bits) < width:
        print(f"WARN: row only has {len(bits)} pixels, expected {width}", file=sys.stderr)
        bits += [0] * (width - len(bits))
    # pad to whole bytes
    pad = (8 - (width % 8)) % 8
    bits += [0] * pad
    out = bytearray()
    for i in range(0, len(bits), 8):
        b = 0
        for k in range(8):
            b = (b << 1) | bits[i + k]
        out.append(b)
    return out

def find_block(text, name):
    """Return (start_idx, end_idx, width, height) for the given LOGO_DATA block."""
    pat = re.compile(
        rf"const\s+retro_logo_image\s+{re.escape(name)}\s+LOGO_DATA\s*=\s*\{{\s*"
        rf"(\d+)\s*,\s*(\d+)\s*,\s*\{{",
        re.S,
    )
    m = pat.search(text)
    if not m:
        return None
    width = int(m.group(1))
    height = int(m.group(2))
    # Find the matching closing brace for the inner '{'  (the byte array)
    # We're now at position m.end(), just past the inner '{'
    pos = m.end()
    # Walk forward, balancing braces (the outer '},' closes the struct).
    # The inner array ends at the FIRST '}' we encounter at the same depth.
    end = text.find("\n    }", pos)
    if end < 0:
        end = text.find("\n}", pos)
    return (pos, end, width, height)

def process(path):
    with open(path) as f:
        text = f.read()

    info = find_block(text, ENTRY_NAME)
    if not info:
        print(f"ERROR: '{ENTRY_NAME}' block not found in {path}", file=sys.stderr)
        sys.exit(1)
    start, end, width, height = info
    print(f"found {ENTRY_NAME}: {width}x{height}, body chars {start}..{end}")

    body = text[start:end]
    # Each data row matches:  0x.., 0x.., ..., //COMMENT
    line_re = re.compile(r"^([ \t]*)((?:0x[0-9a-fA-F]{2},\s*)+)(//)([^\n]*)$", re.M)

    bytes_per_row = (width + 7) // 8
    rows_seen = 0
    new_lines = []
    last = 0
    for m in line_re.finditer(body):
        indent, byte_run, slash, comment = m.group(1), m.group(2), m.group(3), m.group(4)
        # comment may have a leading space; preserve original spacing as-is
        # but use the comment characters for ASCII parsing.
        bytes_out = ascii_to_bytes(comment, width)
        if len(bytes_out) != bytes_per_row:
            print(f"  row {rows_seen}: bytes {len(bytes_out)} != expected {bytes_per_row}", file=sys.stderr)
        hex_out = ", ".join(f"0x{b:02x}" for b in bytes_out) + ", "
        new_line = f"{indent}{hex_out}{slash}{comment}"
        # Splice into body via offsets
        new_lines.append((m.start(), m.end(), new_line))
        rows_seen += 1

    if rows_seen != height:
        print(f"WARN: parsed {rows_seen} rows, expected height {height}", file=sys.stderr)

    # Apply replacements bottom-up so offsets stay valid
    new_body = body
    for s, e, repl in reversed(new_lines):
        new_body = new_body[:s] + repl + new_body[e:]

    new_text = text[:start] + new_body + text[end:]
    if new_text == text:
        print("no changes")
        return
    with open(path, "w") as f:
        f.write(new_text)
    print(f"updated {rows_seen} rows in {path}")

if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_PATH
    process(path)
