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

# 2. Map sprites to texture IDs (opens browser)
python3 tools/atlas_tool.py map
# → Click sprite, click texture ID, repeat
# → Export & Rename → downloads rename_sprites.sh

# 3. Run the rename script
bash rename_sprites.sh

# 4. Pack and export all JSON files
python3 tools/atlas_tool.py pack
```

### Commands

| Command | Description |
|---------|-------------|
| `extract` | Extract sprites from atlas.png → res/sprites/ |
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

**2. MAP** - Assign texture IDs
```bash
python3 tools/atlas_tool.py map
```
- Opens visual mapper in browser
- Left panel: all sprites (click to select)
- Right panel: expected texture IDs from JSON files
- Click sprite → click ID to assign
- Arrow keys to navigate sprites
- Export & Rename downloads a bash script

**3. RENAME** - Apply mappings
```bash
bash rename_sprites.sh
```
- Renames sprite files to their texture IDs
- e.g., `sprite_042.png` → `magic_sword_world.png`

**4. PACK** - Build atlas and export JSON
```bash
python3 tools/atlas_tool.py pack
```
- Packs all sprites into new atlas.png
- Creates atlas.json with all regions
- Updates items.json, materials.json, npc_types.json, world_objects.json
- Adds atlasX, atlasY, atlasW, atlasH to matching entries

### File Locations

**Input/Output:**
- `res/img/atlas.png` - Sprite atlas image
- `res/sprites/` - Individual sprite files (working directory)
- `res/data/atlas.json` - Atlas region definitions

**JSON files updated by pack:**
- `res/data/items.json` - Items (matches worldTextureId)
- `res/data/materials_and_currency.json` - Materials (matches worldTextureId)
- `res/data/npc_types.json` - NPCs (matches textureId)
- `res/data/world_objects.json` - World objects (matches textureId)

### Adding New Sprites

1. Add PNG to `res/sprites/` with the texture ID as filename
   - e.g., `res/sprites/new_item_world.png`
2. Run `python3 tools/atlas_tool.py pack`
3. Atlas rebuilt, JSON files updated automatically

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
