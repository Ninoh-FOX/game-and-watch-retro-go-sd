from PIL import Image, ImageOps
import os
import argparse

# Function to convert an image to the STM32 monochrome format
def convert_image_to_stm32_format(input_image_path, output_file_path, variable_name="LOGO_DATA", invert=False, target_height=None, target_width=None):
    # Open the image
    img = Image.open(input_image_path)
    print(f"Original image mode: {img.mode}")
    
    # Resize if target dimensions are specified
    if target_height is not None or target_width is not None:
        original_width, original_height = img.size
        aspect_ratio = original_width / original_height
        
        if target_width is not None and target_height is not None:
            # Both dimensions specified, use them directly
            new_width = target_width
            new_height = target_height
        elif target_width is not None:
            # Only width specified, calculate height
            new_width = target_width
            new_height = int(target_width / aspect_ratio)
        else:
            # Only height specified, calculate width (existing behavior)
            new_height = target_height
            new_width = int(target_height * aspect_ratio)
            
        print(f"Resizing image to {new_width}x{new_height}")
        # Use NEAREST resampling to preserve sharp edges
        img = img.resize((new_width, new_height), Image.Resampling.NEAREST)
    
    # Convert to RGBA if not already
    if img.mode != 'RGBA':
        img = img.convert('RGBA')
    
    # Create a new image for the result
    result = Image.new('1', img.size, 0)  # Start with black background
    
    # For each pixel, decide if it should be white (visible) or black (background)
    for x in range(img.width):
        for y in range(img.height):
            r, g, b, a = img.getpixel((x, y))
            if a == 0:
                continue  # transparent, leave as black
            if (r + g + b) < 384:
                result.putpixel((x, y), 255)  # white (visible)
            # else: leave as black
    
    # Invert if requested
    if invert:
        result = ImageOps.invert(result)
        print("Image inverted")
    
    # Get image dimensions
    width, height = result.size
    print(f"Image size: {width}x{height}")
    
    # Ensure width is a multiple of 8 (padding if necessary)
    padded_width = ((width + 7) // 8) * 8  # Round up to nearest multiple of 8
    if padded_width != width:
        print(f"Padding width to {padded_width}")
        # Create new image with black background (0)
        new_img = Image.new("1", (padded_width, height), 0)
        # Paste the original image
        new_img.paste(result, (0, 0))
        result = new_img
        width = padded_width
    
    img = result
    
    # Get pixel data as a flat list of 0s and 1s (0 = black, 1 = white)
    pixels = list(img.getdata())
    pixels = [0 if p == 0 else 1 for p in pixels]  # Normalize to 0 (black) and 1 (white)
    
    # Convert pixel data to byte array
    byte_data = []
    for y in range(height):
        row_bytes = []
        for x in range(0, width, 8):  # Process 8 pixels at a time
            byte = 0
            for bit in range(8):
                if x + bit < width:  # Avoid going out of bounds
                    pixel_value = pixels[y * width + (x + bit)]
                    byte |= (pixel_value << (7 - bit))  # MSB first
            row_bytes.append(byte)
        byte_data.extend(row_bytes)
    
    # Generate visual representation for comments
    def get_visual_row(row_bytes, width):
        visual = ""
        for byte in row_bytes:
            for bit in range(7, -1, -1):  # MSB to LSB
                visual += "#" if (byte & (1 << bit)) else "_"
        return visual[:width]  # Trim to actual image width (not padded)
    
    # Write the output to a file
    with open(output_file_path, "w") as f:
        # Write the header
        f.write(f"const retro_logo_image header_logo {variable_name} = {{\n")
        f.write(f"    {width},\n")
        f.write(f"    {height},\n")
        f.write("    {\n")
        
        # Write each row of data
        bytes_per_row = width // 8
        for y in range(height):
            row_start = y * bytes_per_row
            row_bytes = byte_data[row_start:row_start + bytes_per_row]
            
            # Format the hex values
            hex_values = ", ".join(f"0x{byte:02x}" for byte in row_bytes)
            
            # Generate visual comment
            visual = get_visual_row(row_bytes, img.size[0])  # Use original width for visual
            
            # Write the line with padding for alignment
            f.write(f"        {hex_values},")
            padding = " " * (80 - len(f"        {hex_values}"))
            f.write(f"{padding} //  {visual}\n")
        
        # Close the data block
        f.write("    },\n")
        f.write("};\n")
    
    print(f"Conversion complete. Output written to {output_file_path}")

# Example usage
if __name__ == "__main__":
    # Set up argument parser
    parser = argparse.ArgumentParser(description='Convert PNG image to STM32 monochrome format')
    parser.add_argument('input_path', help='Path to input PNG file')
    parser.add_argument('output_path', help='Path to output text file')
    parser.add_argument('--variable-name', default='MY_IMAGE_DATA',
                      help='Name of the C variable (default: MY_IMAGE_DATA)')
    parser.add_argument('--invert', action='store_true',
                      help='Invert the colors of the source image (black becomes white)')
    parser.add_argument('--height', type=int,
                      help='Target height in pixels (width will be calculated to maintain aspect ratio)')
    parser.add_argument('--width', type=int,
                      help='Target width in pixels (height will be calculated to maintain aspect ratio)')
    
    # Parse arguments
    args = parser.parse_args()
    
    # Check if input file exists
    if not os.path.exists(args.input_path):
        print(f"Error: Input file '{args.input_path}' not found.")
    else:
        # Convert the image
        convert_image_to_stm32_format(args.input_path, args.output_path, args.variable_name, args.invert, args.height, args.width)