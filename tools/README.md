# HammerEngine Tools

Tools for managing sprite atlases and texture mappings.

## Atlas Tool - Coherent Workflow

The atlas tool manages the complete sprite atlas lifecycle:

```
atlas.png → EXTRACT → res/sprites/ → MAP → rename → PACK → atlas.png + all JSON files
```

### Quick Start

```bash
# 1. Extract sprites from atlas
python3 tools/atlas_tool.py extract

# 2. Map sprites to texture IDs (opens browser with local server)
python3 tools/atlas_tool.py map
# → Click sprite, click texture ID, repeat
# → Click "Save Mappings" → saves directly to res/sprites/mappings.json
# → Press Ctrl+C to stop server

# 3. Pack and export all JSON files
python3 tools/atlas_tool.py pack
# → Applies renames from mappings.json (handles circular renames safely)
# → Packs atlas and updates all JSON files
# → Cleans up sprite files from res/sprites/
```

### Commands

| Command | Description |
|---------|-------------|
| `extract` | Extract sprites from atlas.png → res/sprites/ |
| `extract-from` | Extract sprites from any source image → res/sprites/ |
| `map` | Visual tool to assign texture IDs to sprites |
| `pack` | Pack sprites into atlas.png + export all JSON |
| `list` | Show current sprites status |

### Workflow Details

**1. EXTRACT** - Pull sprites from atlas.png
```bash
python3 tools/atlas_tool.py extract
```
- Auto-detects sprite regions in atlas.png
- Extracts to res/sprites/ as individual PNGs
- Uses existing atlas.json names if available
- Unnamed sprites get `sprite_001.png`, `sprite_002.png`, etc.

**1b. EXTRACT-FROM** - Import sprites from any source image
```bash
# Extract from external image (auto-names: mysheet_001.png, mysheet_002.png, ...)
python3 tools/atlas_tool.py extract-from mysheet.png

# Custom prefix
python3 tools/atlas_tool.py extract-from items.png --prefix sword_

# Custom output directory
python3 tools/atlas_tool.py extract-from source.png -o ./temp/

# Allow larger sprites without splitting
python3 tools/atlas_tool.py extract-from source.png --max-size 128
```
- Extract sprites from any PNG image into res/sprites/
- Auto-prefix from source filename (e.g., `items.png` → `items_001.png`, `items_002.png`)
- Appends to existing sprites (does not clear directory)
- Useful for importing sprites from external sprite sheets
- Optional `--prefix` to override auto-naming
- Optional `--output` for custom output directory

**2. MAP** - Assign texture IDs
```bash
python3 tools/atlas_tool.py map
```
- Starts local HTTP server on localhost:8000
- Opens visual mapper in browser
- Left panel: all sprites (click to select)
- Right panel: expected texture IDs from JSON files
- Click sprite → click ID to assign
- Arrow keys to navigate sprites
- "Save Mappings" saves directly to `res/sprites/mappings.json`
- Press Ctrl+C in terminal to stop server when done

**3. PACK** - Build atlas and export JSON
```bash
python3 tools/atlas_tool.py pack
```
- If `mappings.json` exists, applies renames first (two-phase rename handles circular dependencies)
- Packs all sprites into new atlas.png
- Creates atlas.json with all regions
- Updates items.json, materials.json, races.json, world_objects.json
- Adds atlasX, atlasY, atlasW, atlasH to matching entries
- Cleans up sprite files from res/sprites/ after successful pack

### File Locations

**Input/Output:**
- `res/img/atlas.png` - Sprite atlas image
- `res/sprites/` - Individual sprite files (temporary working directory)
- `res/sprites/mappings.json` - Rename mappings (created by map, consumed by pack)
- `res/data/atlas.json` - Atlas region definitions

**Output:**
- `res/data/atlas.json` - Atlas region coordinates (single source of truth)

Note: Data files (`resources.json`, `races.json`, etc.) define `textureId` but NOT atlas coordinates.
C++ code looks up coordinates from `atlas.json` at runtime using the `textureId`.

### Adding New Sprites

**Option A: Single sprite**
1. Add PNG to `res/sprites/` with the texture ID as filename
   - e.g., `res/sprites/new_item_world.png`
2. Run `python3 tools/atlas_tool.py pack`
3. Atlas rebuilt, JSON files updated automatically

**Option B: From external sprite sheet**
1. Extract sprites from source image:
   ```bash
   python3 tools/atlas_tool.py extract-from ~/Downloads/new_sprites.png
   ```
2. Rename extracted sprites to their texture IDs, or use `map` command
3. Run `python3 tools/atlas_tool.py pack`

### Modifying Existing Sprites

1. Edit the sprite in `res/sprites/`
2. Run `python3 tools/atlas_tool.py pack`
3. Atlas rebuilt with updated sprite

### Requirements

```bash
# Install pillow for image processing
pip install pillow
# or on Ubuntu/Debian:
sudo apt-get install python3-pil
```

---

## Texture Mapper (Legacy)

For mapping entities to textures without atlas workflow:

```bash
python3 tools/texture_mapper.py
```

Opens a visual browser showing all textures organized by category (biomes, obstacles, buildings, etc.) and allows mapping to items, materials, NPCs, and world objects.
