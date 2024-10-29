# This script allows to convert raw 320x240 framebuffer dumps into png files
# It can be used to convert savestates .raw screenshot into png.
import sys
from PIL import Image
import numpy as np
import os

def convert_rgb565_to_png(input_file, width=320, height=240):
    output_file = os.path.splitext(input_file)[0] + ".png"
    
    with open(input_file, "rb") as f:
        raw_data = f.read()

    data = np.frombuffer(raw_data, dtype=np.uint16)

    red = ((data >> 11) & 0x1F) * 255 // 31
    green = ((data >> 5) & 0x3F) * 255 // 63
    blue = (data & 0x1F) * 255 // 31

    rgb_array = np.stack((red, green, blue), axis=-1).reshape((height, width, 3))

    image = Image.fromarray(rgb_array.astype('uint8'), 'RGB')
    image.save(output_file)
    print(f"Image saved as {output_file}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python binary_rgb565_to_png.py <binary file name>")
        sys.exit(1)

    input_file = sys.argv[1]
    convert_rgb565_to_png(input_file)
