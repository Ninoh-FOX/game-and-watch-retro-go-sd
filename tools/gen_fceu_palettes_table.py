#!/usr/bin/env python3
import sys
import struct

def parse_and_write_binary(input_file, output_file):
    with open(input_file, 'r') as f:
        data = f.read()

    palettes = []
    for block in data.split("},"):
        lines = [line.strip() for line in block.splitlines() if line.strip()]
        if not lines or "{" not in lines[0]:
            continue
        
        # Extract palette name
        name_line = lines[0]
        if '"' in name_line:
            name = name_line.split('"')[1]
        else:
            continue

        # Extract colors
        colors_str = "".join(lines[1:]).split("{")[1].replace("}", "").strip()
        colors = [int(color.strip(), 16) for color in colors_str.split(",") if color.strip()]
        
        palettes.append((name, colors))

    with open(output_file, 'wb') as f:
        for name, colors in palettes:
            name_bytes = name.encode('utf-8').ljust(32, b'\0')[:32]
            f.write(name_bytes)
            f.write(struct.pack(f'{len(colors)}I', *colors))

n = len(sys.argv)

if n < 2: print("Usage :\ngen_fceu_palettes_table.py output_file.bin\n"); sys.exit(0)

parse_and_write_binary('Core/Inc/porting/nes_fceu/fceu_palettes.h', sys.argv[1])
