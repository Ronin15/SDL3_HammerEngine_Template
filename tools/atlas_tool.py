#!/usr/bin/env python3
"""
Atlas Tool for HammerEngine

Manages the sprite atlas workflow:
1. map      - Open visual mapper to define sprite regions in atlas.png
2. extract  - Extract sprites from atlas using atlas.json regions
3. pack     - Pack individual sprites into atlas.png + atlas.json
4. update   - Update all JSON files with coordinates from atlas.json

Workflow:
  # First time: map sprites in atlas.png
  python3 tools/atlas_tool.py map
  # (Use visual tool to define regions, save atlas.json)

  # Extract sprites from atlas
  python3 tools/atlas_tool.py extract

  # After adding/editing sprites in res/sprites/:
  python3 tools/atlas_tool.py pack
  python3 tools/atlas_tool.py update

  # Or do both:
  python3 tools/atlas_tool.py pack --update

Requires: pip install pillow
"""

import argparse
import json
import sys
import webbrowser
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Required: pip install pillow")
    sys.exit(1)


def get_paths():
    """Get standard project paths."""
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    return {
        'sprites_dir': project_root / "res" / "sprites",
        'atlas_png': project_root / "res" / "img" / "atlas.png",
        'atlas_json': project_root / "res" / "data" / "atlas.json",
        'items_json': project_root / "res" / "data" / "items.json",
        'materials_json': project_root / "res" / "data" / "materials_and_currency.json",
        'npc_types_json': project_root / "res" / "data" / "npc_types.json",
        'world_objects_json': project_root / "res" / "data" / "world_objects.json",
        'mapper_html': script_dir / "atlas_mapper.html",  # Unified mapper tool
    }


def cmd_map(paths: dict):
    """Open visual mapper to assign texture IDs to extracted sprites."""
    import base64

    mapper_html = paths['mapper_html']
    sprites_dir = paths['sprites_dir']

    if not mapper_html.exists():
        print(f"Error: Mapper not found: {mapper_html}")
        return False

    if not sprites_dir.exists() or not list(sprites_dir.glob("*.png")):
        print(f"Error: No sprites found in {sprites_dir}")
        print(f"Run 'extract' first: python3 tools/atlas_tool.py extract")
        return False

    # Read sprites and convert to base64
    sprite_data = []
    for png_file in sorted(sprites_dir.glob("*.png")):
        img = Image.open(png_file)
        with open(png_file, 'rb') as f:
            b64 = base64.b64encode(f.read()).decode('utf-8')
        sprite_data.append({
            'filename': png_file.stem,
            'dataUrl': f'data:image/png;base64,{b64}',
            'width': img.width,
            'height': img.height
        })

    print(f"Loading {len(sprite_data)} sprites into mapper...")

    # Generate HTML with embedded sprite data
    sprite_json = json.dumps(sprite_data)

    # Read template and inject data
    with open(mapper_html, 'r') as f:
        html_content = f.read()

    # Inject sprite data before closing </script> of init
    inject_code = f'''
        // Auto-loaded sprites from res/sprites/
        const PRELOADED_SPRITES = {sprite_json};

        function autoLoadSprites() {{
            sprites = PRELOADED_SPRITES.map(s => ({{...s, textureId: null}}));
            document.getElementById('instructions').style.display = 'none';
            applyFilters();
            renderIDList();
            updateStatus(`Loaded ${{sprites.length}} sprites`);
            document.getElementById('totalCount').textContent = sprites.length;
            document.getElementById('exportBtn').disabled = false;
        }}

        // Auto-load on page ready
        autoLoadSprites();
    '''

    # Insert before the closing </script> tag (before "// Init")
    html_content = html_content.replace('// Init\n        renderIDList();', inject_code)

    # Write to temp file and open
    temp_html = sprites_dir.parent.parent / "tools" / "atlas_mapper_session.html"
    with open(temp_html, 'w') as f:
        f.write(html_content)

    print(f"Opening mapper with {len(sprite_data)} sprites pre-loaded...")
    print(f"\nIn the browser:")
    print(f"  1. Click a sprite, then click a texture ID to assign it")
    print(f"  2. Use arrow keys to navigate between sprites")
    print(f"  3. Click 'Preview & Export' when done")
    print(f"\nAfter export, run the rename script, then: python3 tools/atlas_tool.py pack --update")

    webbrowser.open(f"file://{temp_html.resolve()}")
    return True


def detect_sprite_regions(atlas: Image.Image) -> list:
    """Auto-detect sprite regions by finding non-transparent bounded areas."""
    width, height = atlas.size
    pixels = atlas.load()
    visited = set()
    regions = []

    def is_transparent(x, y):
        if x < 0 or x >= width or y < 0 or y >= height:
            return True
        return pixels[x, y][3] == 0  # Alpha channel

    def flood_fill(start_x, start_y):
        """Find bounding box of connected non-transparent pixels."""
        stack = [(start_x, start_y)]
        min_x, min_y = start_x, start_y
        max_x, max_y = start_x, start_y

        while stack:
            x, y = stack.pop()
            if (x, y) in visited:
                continue
            if is_transparent(x, y):
                continue

            visited.add((x, y))
            min_x, min_y = min(min_x, x), min(min_y, y)
            max_x, max_y = max(max_x, x), max(max_y, y)

            # Check 4-connected neighbors
            for dx, dy in [(0, 1), (0, -1), (1, 0), (-1, 0)]:
                nx, ny = x + dx, y + dy
                if (nx, ny) not in visited:
                    stack.append((nx, ny))

        return min_x, min_y, max_x, max_y

    # Scan for non-transparent pixels
    for y in range(height):
        for x in range(width):
            if (x, y) not in visited and not is_transparent(x, y):
                min_x, min_y, max_x, max_y = flood_fill(x, y)
                w = max_x - min_x + 1
                h = max_y - min_y + 1
                if w > 1 and h > 1:  # Skip single pixels
                    regions.append({
                        'x': min_x,
                        'y': min_y,
                        'w': w,
                        'h': h
                    })

    # Sort by position (top-to-bottom, left-to-right)
    regions.sort(key=lambda r: (r['y'], r['x']))
    return regions


def cmd_extract(paths: dict):
    """Extract sprites from atlas - auto-detects regions if no atlas.json."""
    atlas_path = paths['atlas_png']
    sprites_dir = paths['sprites_dir']
    atlas_json = paths['atlas_json']

    if not atlas_path.exists():
        print(f"Error: Atlas not found: {atlas_path}")
        return False

    # Load atlas image
    atlas = Image.open(atlas_path).convert('RGBA')

    # Try to load atlas.json, otherwise auto-detect
    regions = {}
    if atlas_json.exists():
        with open(atlas_json, 'r') as f:
            atlas_data = json.load(f)
        regions = atlas_data.get('regions', {})
        print(f"Using atlas.json ({len(regions)} regions)")

    if not regions:
        print("No atlas.json found - auto-detecting sprite regions...")
        detected = detect_sprite_regions(atlas)
        print(f"  Detected {len(detected)} sprites")

        # Name them sprite_001, sprite_002, etc. (user will rename via mapper)
        for i, r in enumerate(detected):
            regions[f"sprite_{i+1:03d}"] = r

    if not regions:
        print("No sprites found in atlas")
        return False

    # Create sprites directory
    sprites_dir.mkdir(parents=True, exist_ok=True)

    # Extract each region
    count = 0
    for sprite_id, coords in regions.items():
        x, y, w, h = coords['x'], coords['y'], coords['w'], coords['h']
        sprite = atlas.crop((x, y, x + w, y + h))

        output_path = sprites_dir / f"{sprite_id}.png"
        sprite.save(output_path)
        print(f"  Extracted: {sprite_id}.png ({w}x{h})")
        count += 1

    print(f"\nExtracted {count} sprites to {sprites_dir}")
    return True


def cmd_pack(paths: dict, update: bool = False):
    """Pack individual sprites into atlas.png + atlas.json."""
    sprites_dir = paths['sprites_dir']
    atlas_png = paths['atlas_png']
    atlas_json = paths['atlas_json']

    if not sprites_dir.exists():
        print(f"Error: Sprites directory not found: {sprites_dir}")
        print("Run 'extract' first or create sprites manually")
        return False

    # Collect all PNG files
    sprites = []
    for png_file in sorted(sprites_dir.glob("*.png")):
        img = Image.open(png_file).convert('RGBA')
        world_id = png_file.stem  # filename without extension
        sprites.append({
            'id': world_id,
            'image': img,
            'width': img.width,
            'height': img.height
        })

    if not sprites:
        print("No PNG files found in sprites directory")
        return False

    print(f"Packing {len(sprites)} sprites...")

    # Simple packing: sort by height, pack in rows
    sprites.sort(key=lambda s: s['height'], reverse=True)

    # Calculate atlas dimensions (power of 2 for GPU efficiency)
    max_width = 512
    padding = 1

    # Pack sprites into rows
    rows = []
    current_row = {'sprites': [], 'width': 0, 'height': 0}

    for sprite in sprites:
        if current_row['width'] + sprite['width'] + padding > max_width:
            if current_row['sprites']:
                rows.append(current_row)
            current_row = {'sprites': [], 'width': 0, 'height': 0}

        current_row['sprites'].append(sprite)
        current_row['width'] += sprite['width'] + padding
        current_row['height'] = max(current_row['height'], sprite['height'])

    if current_row['sprites']:
        rows.append(current_row)

    # Calculate total atlas size
    atlas_width = max_width
    atlas_height = sum(row['height'] + padding for row in rows)

    # Round up to power of 2
    def next_power_of_2(n):
        p = 1
        while p < n:
            p *= 2
        return p

    atlas_height = next_power_of_2(atlas_height)

    # Create atlas image
    atlas = Image.new('RGBA', (atlas_width, atlas_height), (0, 0, 0, 0))

    # Place sprites and record coordinates
    regions = {}
    y_offset = 0

    for row in rows:
        x_offset = 0
        for sprite in row['sprites']:
            atlas.paste(sprite['image'], (x_offset, y_offset))
            regions[sprite['id']] = {
                'x': x_offset,
                'y': y_offset,
                'w': sprite['width'],
                'h': sprite['height']
            }
            x_offset += sprite['width'] + padding
        y_offset += row['height'] + padding

    # Save atlas image
    atlas.save(atlas_png)
    print(f"Saved: {atlas_png} ({atlas_width}x{atlas_height})")

    # Save atlas.json
    atlas_data = {
        'atlasId': 'atlas',
        'regions': regions
    }
    with open(atlas_json, 'w') as f:
        json.dump(atlas_data, f, indent=2)
    print(f"Saved: {atlas_json}")

    if update:
        cmd_update(paths)

    return True


def update_items_json(json_path: Path, regions: dict) -> int:
    """Update items.json or materials.json with atlas coordinates."""
    if not json_path.exists():
        return 0

    with open(json_path, 'r') as f:
        data = json.load(f)

    updated = 0
    for resource in data.get('resources', []):
        world_id = resource.get('worldTextureId')
        if world_id and world_id in regions:
            coords = regions[world_id]
            resource['atlasX'] = coords['x']
            resource['atlasY'] = coords['y']
            resource['atlasW'] = coords['w']
            resource['atlasH'] = coords['h']
            updated += 1

    with open(json_path, 'w') as f:
        json.dump(data, f, indent=2)

    return updated


def update_npc_types_json(json_path: Path, regions: dict) -> int:
    """Update npc_types.json with atlas coordinates if sprites are in atlas."""
    if not json_path.exists():
        return 0

    with open(json_path, 'r') as f:
        data = json.load(f)

    updated = 0
    for npc in data.get('npcTypes', []):
        texture_id = npc.get('textureId')
        if texture_id and texture_id in regions:
            coords = regions[texture_id]
            npc['atlasX'] = coords['x']
            npc['atlasY'] = coords['y']
            npc['atlasW'] = coords['w']
            npc['atlasH'] = coords['h']
            updated += 1

    with open(json_path, 'w') as f:
        json.dump(data, f, indent=2)

    return updated


def update_world_objects_json(json_path: Path, regions: dict) -> int:
    """Update world_objects.json with atlas coordinates if sprites are in atlas."""
    if not json_path.exists():
        return 0

    with open(json_path, 'r') as f:
        data = json.load(f)

    updated = 0
    for category in ['biomes', 'obstacles', 'buildings', 'decorations']:
        cat_data = data.get(category, {})
        for key, obj in cat_data.items():
            texture_id = obj.get('textureId')
            if texture_id and texture_id in regions:
                obj['atlasX'] = regions[texture_id]['x']
                obj['atlasY'] = regions[texture_id]['y']
                obj['atlasW'] = regions[texture_id]['w']
                obj['atlasH'] = regions[texture_id]['h']
                updated += 1

            # Also check for seasonal variants
            if obj.get('seasonal', False):
                for prefix in ['spring_', 'summer_', 'fall_', 'winter_']:
                    seasonal_id = prefix + texture_id
                    if seasonal_id in regions:
                        season_key = prefix.rstrip('_')
                        if 'seasonalAtlas' not in obj:
                            obj['seasonalAtlas'] = {}
                        obj['seasonalAtlas'][season_key] = {
                            'x': regions[seasonal_id]['x'],
                            'y': regions[seasonal_id]['y'],
                            'w': regions[seasonal_id]['w'],
                            'h': regions[seasonal_id]['h']
                        }

    with open(json_path, 'w') as f:
        json.dump(data, f, indent=2)

    return updated


def cmd_update(paths: dict):
    """Update all JSON files with coordinates from atlas.json."""
    atlas_json = paths['atlas_json']

    if not atlas_json.exists():
        print(f"Error: atlas.json not found: {atlas_json}")
        return False

    with open(atlas_json, 'r') as f:
        atlas_data = json.load(f)

    regions = atlas_data.get('regions', {})
    print(f"Found {len(regions)} regions in atlas.json")

    # Update items.json
    count = update_items_json(paths['items_json'], regions)
    if count > 0:
        print(f"  Updated {count} items in items.json")

    # Update materials.json
    count = update_items_json(paths['materials_json'], regions)
    if count > 0:
        print(f"  Updated {count} materials in materials_and_currency.json")

    # Update npc_types.json
    count = update_npc_types_json(paths['npc_types_json'], regions)
    if count > 0:
        print(f"  Updated {count} NPCs in npc_types.json")

    # Update world_objects.json
    count = update_world_objects_json(paths['world_objects_json'], regions)
    if count > 0:
        print(f"  Updated {count} world objects in world_objects.json")

    print("Done!")
    return True


def cmd_list(paths: dict):
    """List all sprites that would be packed."""
    sprites_dir = paths['sprites_dir']

    if not sprites_dir.exists():
        print(f"Sprites directory not found: {sprites_dir}")
        return

    sprites = sorted(sprites_dir.glob("*.png"))
    print(f"\nSprites in {sprites_dir}:")
    for s in sprites:
        img = Image.open(s)
        print(f"  {s.stem} ({img.width}x{img.height})")
    print(f"\nTotal: {len(sprites)} sprites")


def main():
    parser = argparse.ArgumentParser(description='Atlas Tool for HammerEngine')
    subparsers = parser.add_subparsers(dest='command', help='Commands')

    # Map command
    subparsers.add_parser('map', help='Open visual mapper to define sprite regions')

    # Extract command
    subparsers.add_parser('extract', help='Extract sprites from atlas using atlas.json')

    # Pack command
    pack_parser = subparsers.add_parser('pack', help='Pack sprites into atlas')
    pack_parser.add_argument('--update', action='store_true',
                            help='Also update all JSON files after packing')

    # Update command
    subparsers.add_parser('update', help='Update all JSON files from atlas.json')

    # List command
    subparsers.add_parser('list', help='List sprites that would be packed')

    args = parser.parse_args()
    paths = get_paths()

    if args.command == 'map':
        cmd_map(paths)
    elif args.command == 'extract':
        cmd_extract(paths)
    elif args.command == 'pack':
        cmd_pack(paths, update=args.update)
    elif args.command == 'update':
        cmd_update(paths)
    elif args.command == 'list':
        cmd_list(paths)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
