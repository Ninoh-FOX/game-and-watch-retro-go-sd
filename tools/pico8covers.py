#!/usr/bin/env python3
"""
Extract PICO-8 cart labels and generate G&W cover art (.img JPEG files).

Reads .p8 (text) or .p8.png (PNG) carts, extracts the 128x128 label,
renders it with the PICO-8 palette, and saves as a JPEG cover.

Usage:
  # Single cart:
  python3 pico8covers.py --cart celeste.p8 --output covers/pico8/celeste.img

  # All carts in a directory:
  python3 pico8covers.py --src roms/pico8 --dst covers/pico8

  # Custom size/quality:
  python3 pico8covers.py --src roms/pico8 --dst covers/pico8 --width 128 --jpg_quality 90
"""

import os
import sys
import argparse
import struct
from pathlib import Path
from PIL import Image

# PICO-8 16-color palette (standard)
PICO8_PALETTE = [
    (0, 0, 0),        # 0  black
    (29, 43, 83),      # 1  dark-blue
    (126, 37, 83),     # 2  dark-purple
    (0, 135, 81),      # 3  dark-green
    (171, 82, 54),     # 4  brown
    (95, 87, 79),      # 5  dark-grey
    (194, 195, 199),   # 6  light-grey
    (255, 241, 232),   # 7  white
    (255, 0, 77),      # 8  red
    (255, 163, 0),     # 9  orange
    (255, 236, 39),    # 10 yellow
    (0, 228, 54),      # 11 green
    (41, 173, 255),    # 12 blue
    (131, 118, 156),   # 13 lavender
    (255, 119, 168),   # 14 pink
    (255, 204, 170),   # 15 light-peach
]

# Extended palette (colors 128-143, used with -1..-16 in pal())
PICO8_PALETTE_EXT = [
    (41, 24, 20),      # 128 / -1
    (17, 29, 53),      # 129 / -2
    (66, 33, 54),      # 130 / -3
    (18, 83, 89),      # 131 / -4
    (116, 47, 41),     # 132 / -5
    (73, 51, 59),      # 133 / -6
    (162, 136, 121),   # 134 / -7
    (243, 239, 125),   # 135 / -8
    (190, 18, 80),     # 136 / -9
    (255, 108, 36),    # 137 / -10
    (168, 231, 46),    # 138 / -11
    (0, 181, 67),      # 139 / -12
    (6, 90, 181),      # 140 / -13
    (117, 70, 101),    # 141 / -14
    (255, 110, 89),    # 142 / -15
    (255, 157, 129),   # 143 / -16
]

# Full 32-color palette for label rendering
FULL_PALETTE = PICO8_PALETTE + PICO8_PALETTE_EXT


# ============================================================
# .p8 text format: extract __label__ section
# ============================================================

def extract_label_from_p8(filepath):
    """Extract 128x128 label from a .p8 text file's __label__ section."""
    with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
        lines = f.readlines()

    in_label = False
    label_lines = []

    for line in lines:
        stripped = line.rstrip('\n\r')
        if stripped == '__label__':
            in_label = True
            continue
        if in_label:
            if stripped.startswith('__'):
                break
            label_lines.append(stripped)

    if len(label_lines) < 128:
        return None

    # Parse hex/extended color indices
    img = Image.new('RGB', (128, 128))
    pixels = img.load()

    for y in range(128):
        row = label_lines[y]
        for x in range(min(128, len(row))):
            c = row[x]
            if '0' <= c <= '9':
                idx = int(c)
            elif 'a' <= c <= 'f':
                idx = 10 + ord(c) - ord('a')
            elif 'g' <= c <= 'v':
                # Extended palette: g=16, h=17, ..., v=31
                idx = 16 + ord(c) - ord('g')
            else:
                idx = 0
            if idx < len(FULL_PALETTE):
                pixels[x, y] = FULL_PALETTE[idx]

    return img


# ============================================================
# .p8.png format: extract label from PNG image pixels
# ============================================================

def find_closest_p8_color(r, g, b):
    """Find the closest PICO-8 palette index for an RGB color."""
    best_idx = 0
    best_dist = 999999
    for i, (pr, pg, pb) in enumerate(FULL_PALETTE):
        dr, dg, db = pr - r, pg - g, pb - b
        dist = dr * dr + dg * dg + db * db
        if dist < best_dist:
            best_dist = dist
            best_idx = i
    return best_idx


def extract_label_from_p8png(filepath):
    """Extract 128x128 label from a .p8.png file.

    The label is embedded in the PNG image at pixel offset (16, 24)
    in the 160x205 cart image.
    """
    try:
        src = Image.open(filepath).convert('RGB')
    except Exception as e:
        print(f"  Error opening {filepath}: {e}")
        return None

    w, h = src.size
    label_x, label_y = 16, 24
    label_size = 128

    if w < label_x + label_size or h < label_y + label_size:
        print(f"  Image too small ({w}x{h}), need at least {label_x+label_size}x{label_y+label_size}")
        return None

    # Extract and re-palette the label region
    img = Image.new('RGB', (label_size, label_size))
    pixels = img.load()
    src_pixels = src.load()

    for y in range(label_size):
        for x in range(label_size):
            r, g, b = src_pixels[label_x + x, label_y + y]
            idx = find_closest_p8_color(r, g, b)
            pixels[x, y] = FULL_PALETTE[idx]

    return img


# ============================================================
# Cover generation
# ============================================================

def extract_label(filepath):
    """Extract label from a .p8 or .p8.png file. Returns PIL Image or None."""
    path = Path(filepath)
    name = path.name.lower()

    if name.endswith('.p8.png') or name.endswith('.png'):
        return extract_label_from_p8png(filepath)
    elif name.endswith('.p8'):
        return extract_label_from_p8(filepath)
    else:
        print(f"  Unknown format: {filepath}")
        return None


def create_cover(label_img, output_path, width=128, height=128, jpg_quality=85):
    """Resize label to cover dimensions and save as JPEG."""
    target_w = width or 128
    target_h = height or 128

    # Scale maintaining aspect ratio
    ow, oh = label_img.size
    scale = min(target_w / ow, target_h / oh)
    new_w = int(ow * scale)
    new_h = int(oh * scale)

    try:
        resample = Image.Resampling.LANCZOS
    except AttributeError:
        resample = Image.ANTIALIAS

    resized = label_img.resize((new_w, new_h), resample)

    output = Path(output_path)
    output.parent.mkdir(parents=True, exist_ok=True)
    resized.save(str(output), format='JPEG', optimize=True, quality=jpg_quality)


def process_single(cart_path, output_path, width=128, height=None, jpg_quality=85):
    """Process a single cart file."""
    label = extract_label(cart_path)
    if label is None:
        print(f"  No label found in {cart_path}")
        return False

    create_cover(label, output_path, width, height, jpg_quality)
    print(f"  {Path(cart_path).name} -> {output_path}")
    return True


def process_directory(src_dir, dst_dir, width=128, height=None, jpg_quality=85,
                      force=False):
    """Process all .p8 and .p8.png files in a directory."""
    src = Path(src_dir)
    dst = Path(dst_dir)
    count = 0
    skipped = 0

    for f in sorted(src.iterdir()):
        name = f.name.lower()
        if not (name.endswith('.p8') or name.endswith('.p8.png') or name.endswith('.png')):
            continue

        # Output filename: replace final extension with .img
        # .p8.png -> .p8.img, .p8 -> .img, .png -> .img
        name_str = f.name
        if name_str.lower().endswith('.p8.png'):
            out_name = name_str[:-4] + '.img'  # strip .png, add .img -> .p8.img
        elif name_str.lower().endswith('.png'):
            out_name = name_str[:-4] + '.img'
        elif name_str.lower().endswith('.p8'):
            out_name = name_str[:-3] + '.img'
        else:
            out_name = name_str + '.img'
        out_file = dst / out_name

        if out_file.exists() and not force:
            skipped += 1
            continue

        label = extract_label(str(f))
        if label:
            create_cover(label, str(out_file), width, height, jpg_quality)
            print(f"  {f.name} -> {out_file.name}")
            count += 1
        else:
            print(f"  {f.name}: no label found, skipping")

    print(f"\nDone: {count} covers created, {skipped} skipped (already exist)")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Generate G&W cover art from PICO-8 cart labels')
    parser.add_argument('--cart', type=str,
                        help='Single cart file (.p8 or .p8.png)')
    parser.add_argument('--output', type=str,
                        help='Output .img file (only with --cart)')
    parser.add_argument('--src', type=str,
                        help='Source directory with cart files')
    parser.add_argument('--dst', type=str,
                        help='Destination directory for .img covers')
    parser.add_argument('--width', type=int, default=128,
                        help='Cover width (default: 128)')
    parser.add_argument('--height', type=int, default=128,
                        help='Cover height (default: 128)')
    parser.add_argument('--jpg_quality', type=int, default=85,
                        help='JPEG quality 0-100 (default: 85)')
    parser.add_argument('--force', action='store_true',
                        help='Overwrite existing covers')

    args = parser.parse_args()

    if args.cart:
        output = args.output
        if not output:
            p = Path(args.cart)
            stem = p.stem
            if stem.lower().endswith('.p8'):
                stem = stem[:-3]
            output = str(p.parent / (stem + '.img'))
        success = process_single(args.cart, output, args.width, args.height,
                                 args.jpg_quality)
        if not success:
            sys.exit(1)
    elif args.src and args.dst:
        process_directory(args.src, args.dst, args.width, args.height,
                          args.jpg_quality, args.force)
    else:
        parser.print_help()
        sys.exit(1)
