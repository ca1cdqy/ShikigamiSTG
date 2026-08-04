#!/usr/bin/env python3
"""
Touhou resource converter.
Splits multi-texture TH06 atlases into individual images and emits JSON metadata.

Output layout:
texture/
  bullet/
    etama3/
      sprite_000.png
      ...
  player/
    player00/
      sprite_000.png
      ...
  enemy/
  effect/
  face/
  ui/
  bg/
"""

import struct
import os
import json
import shutil
import argparse
from dataclasses import dataclass
from typing import List, Tuple, Dict
from PIL import Image


CATEGORY_MAP = {
    'etama3': 'bullet',
    'etama4': 'bullet',
    'eff00': 'effect',
    'eff01': 'effect',
    'eff02': 'effect',
    'eff03': 'effect',
    'eff04': 'effect',
    'eff05': 'effect',
    'eff06': 'effect',
    'eff07': 'effect',
    'player00': 'player',
    'player01': 'player',
    'stg1enm': 'enemy',
    'stg1enm2': 'enemy',
    'stg2enm': 'enemy',
    'stg2enm2': 'enemy',
    'stg3enm': 'enemy',
    'stg4enm': 'enemy',
    'stg5enm': 'enemy',
    'stg5enm2': 'enemy',
    'stg6enm': 'enemy',
    'stg6enm2': 'enemy',
    'stg7enm': 'enemy',
    'stg7enm2': 'enemy',
    'face00a': 'face',
    'face00b': 'face',
    'face00c': 'face',
    'face01a': 'face',
    'face01b': 'face',
    'face01c': 'face',
    'face03a': 'face',
    'face03b': 'face',
    'face05a': 'face',
    'face06a': 'face',
    'face06b': 'face',
    'face08a': 'face',
    'face08b': 'face',
    'face09a': 'face',
    'face09b': 'face',
    'face10a': 'face',
    'face10b': 'face',
    'face12a': 'face',
    'face12b': 'face',
    'face12c': 'face',
    # UI
    'front': 'ui',
    'loading': 'ui',
    'ascii': 'ui',
    'asciis': 'ui',
    'capture': 'ui',
    'text': 'ui',
    'stg1bg': 'bg',
    'stg2bg': 'bg',
    'stg3bg': 'bg',
    'stg4bg': 'bg',
    'stg5bg': 'bg',
    'stg6bg': 'bg',
    'stg7bg': 'bg',
    'staff01': 'staff',
    'staff02': 'staff',
    'staff03': 'staff',
    'music00': 'music',
    'music01': 'music',
    'music02': 'music',
    'replay00': 'replay',
    'result00': 'result',
    'result01': 'result',
    'result02': 'result',
    'result03': 'result',
    'select01': 'select',
    'select02': 'select',
    'select03': 'select',
    'select04': 'select',
    'select05': 'select',
    'slpl00a': 'slpl',
    'slpl00b': 'slpl',
    'slpl01a': 'slpl',
    'slpl01b': 'slpl',
    'title01': 'title',
    'title01s': 'title',
    'title02': 'title',
    'title03': 'title',
    'title04': 'title',
    'title04s': 'title',
}


@dataclass
class SpriteEntry:
    texture_id: int
    x: float
    y: float
    width: float
    height: float


def get_category(base_name: str) -> str:
    """Return the asset category inferred from a file name."""
    return CATEGORY_MAP.get(base_name, 'other')


def parse_anm_file(anm_path: str) -> Tuple[List[SpriteEntry], List[str]]:
    """Parse an ANM file."""
    with open(anm_path, 'rb') as f:
        data = f.read()

    entries = []
    texture_paths = []

    i = 0
    while i < len(data) - 4:
        if data[i:i+4] == b'.png':
            start = i
            while start > 0 and data[start-1] != 0:
                start -= 1
            end = i + 4
            while end < len(data) and data[end] != 0:
                end += 1
            path = data[start:end].decode('utf-8', errors='replace')
            if path and path not in texture_paths:
                texture_paths.append(path)
        i += 1

    skip_count = 9
    entry_index = 0
    for i in range(28, len(data), 4):
        if i + 4 > len(data):
            break
        offset = struct.unpack('<I', data[i:i+4])[0]
        if offset == 0 or offset >= len(data):
            entry_index += 1
            continue

        if entry_index < skip_count:
            entry_index += 1
            continue

        if offset + 20 <= len(data):
            try:
                texture_id = struct.unpack('<h', data[offset:offset+2])[0]
                x = struct.unpack('<f', data[offset+4:offset+8])[0]
                y = struct.unpack('<f', data[offset+8:offset+12])[0]
                w = struct.unpack('<f', data[offset+12:offset+16])[0]
                h = struct.unpack('<f', data[offset+16:offset+20])[0]

                if w > 0 and h > 0 and x >= 0 and y >= 0 and w < 10000 and h < 10000:
                    entries.append(SpriteEntry(texture_id, x, y, w, h))
            except:
                pass
        entry_index += 1

    return entries, texture_paths


def extract_sprites(anm_path: str, output_dir: str, input_base_dir: str):
    """
    Extract sprites from an ANM file into individual images.
    """
    print(f"Processing: {anm_path}")

    entries, texture_paths = parse_anm_file(anm_path)

    if not texture_paths:
        print(f"  No texture paths found, skipping")
        return

    base_name = os.path.splitext(os.path.basename(anm_path))[0]

    category = get_category(base_name)

    sprite_output_dir = os.path.join(output_dir, category, base_name)
    os.makedirs(sprite_output_dir, exist_ok=True)

    textures = {}
    for i, path in enumerate(texture_paths):
        possible_paths = [
            os.path.join(input_base_dir, path),
            os.path.join(input_base_dir, os.path.basename(path)),
            os.path.join(os.path.dirname(anm_path), os.path.basename(path)),
        ]

        texture_found = False
        for texture_path in possible_paths:
            if os.path.exists(texture_path):
                try:
                    main_img = Image.open(texture_path).convert('RGBA')

                    alpha_path = texture_path.replace('.png', '_a.png')
                    if os.path.exists(alpha_path):
                        # TH06 copies the auxiliary texture's blue byte into
                        # the destination alpha channel.
                        alpha_img = Image.open(alpha_path).convert('RGB').getchannel('B')
                        r, g, b, _ = main_img.split()
                        main_img = Image.merge('RGBA', (r, g, b, alpha_img))
                        print(f"  Loaded texture {i}: {texture_path} ({main_img.size}) with alpha from {alpha_path}")
                    else:
                        print(f"  Loaded texture {i}: {texture_path} ({main_img.size})")

                    textures[i] = main_img
                    texture_found = True
                    break
                except Exception as e:
                    print(f"  Failed to load {texture_path}: {e}")

        if not texture_found:
            print(f"  Warning: Could not find texture: {path}")

    if not textures:
        print(f"  No textures loaded, skipping")
        return

    sprite_files = []
    for i, entry in enumerate(entries):
        if not textures:
            continue

        texture = list(textures.values())[0]
        tex_width, tex_height = texture.size

        left = int(entry.x)
        top = int(entry.y)
        right = int(entry.x + entry.width)
        bottom = int(entry.y + entry.height)

        left = max(0, left)
        top = max(0, top)
        right = min(tex_width, right)
        bottom = min(tex_height, bottom)

        if right <= left or bottom <= top:
            continue

        sprite = texture.crop((left, top, right, bottom))

        sprite_filename = f"sprite_{i:03d}.png"
        sprite_path = os.path.join(sprite_output_dir, sprite_filename)
        sprite.save(sprite_path)

        sprite_files.append({
            "id": i,
            "file": sprite_filename,
            "width": right - left,
            "height": bottom - top,
            "origin_x": entry.width / 2,
            "origin_y": entry.height / 2
        })

    output_data = {
        "source": base_name,
        "textures": texture_paths,
        "sprites": sprite_files
    }

    json_path = os.path.join(output_dir, f"{base_name}.json")
    with open(json_path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(output_data, f, ensure_ascii=False, separators=(',', ':'),
                  sort_keys=True)
        f.write('\n')

    print(f"  Extracted {len(sprite_files)} sprites to {sprite_output_dir}")
    print(f"  JSON: {json_path}")


def process_directory(input_dir: str, output_dir: str):
    """Process every ANM file in a directory."""
    print(f"Processing directory: {input_dir}")
    print(f"Output directory: {output_dir}")

    for root, _, files in os.walk(input_dir):
        for file in files:
            if file.endswith('.anm'):
                anm_path = os.path.join(root, file)
                extract_sprites(anm_path, output_dir, input_dir)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()

    print("Touhou Resource Converter")
    print("=" * 50)

    process_directory(args.input, args.output)

    print("\nConversion complete!")


if __name__ == "__main__":
    main()
