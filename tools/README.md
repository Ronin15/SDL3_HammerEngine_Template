# HammerEngine Tools

Tools for managing textures and sprite atlases.

## Quick Reference

| Tool | Purpose |
|------|---------|
| `texture_mapper.py` | Map entity IDs → texture IDs (visual browser) |
| `atlas_tool.py` | Extract/pack/update atlas sprites |
| `atlas_picker.html` | Select atlas regions by clicking (x,y,w,h coords) |

---

## Texture Mapping Workflow

### For Items/Materials (atlas-based)

Items use `atlas.png` with coordinates. The JSON stores `atlasX`, `atlasY`, `atlasW`, `atlasH`.

```bash
# 1. See what's in the atlas
python3 tools/atlas_tool.py extract
# Extracts sprites to res/sprites/ so you can see them

# 2. Map items to atlas regions
open tools/atlas_picker.html
# Click and drag to select sprite region
# Copy the x,y,w,h coordinates to items.json
```

### For NPCs/World Objects (individual textures)

These use individual PNG files. The JSON stores `textureId` which maps to a filename.

```bash
# Visual mapper - browse textures by folder, map to entities
python3 tools/texture_mapper.py
# Opens in browser automatically
```

---

## Tool Details

### texture_mapper.py

Visual tool to map entity IDs to texture IDs.

```bash
python3 tools/texture_mapper.py          # Generate HTML and open in browser
python3 tools/texture_mapper.py --no-open # Generate only, don't open
```

**Features:**
- Browse all textures by folder (root, buildings, obstacles, biomes, etc.)
- Shows seasonal variants with orange badge
- Map Items, Materials, or Custom entities
- Export JSON mappings

**Output:** `tools/texture_mapper.html`

---

### atlas_tool.py

Manage sprite atlas workflow.

```bash
# Extract sprites from atlas.png to individual files
python3 tools/atlas_tool.py extract
# Output: res/sprites/*.png

# Pack individual sprites back into atlas
python3 tools/atlas_tool.py pack
# Output: res/img/atlas.png + res/data/atlas.json

# Pack AND update items.json with new coordinates
python3 tools/atlas_tool.py pack --update

# Just update items.json from existing atlas.json
python3 tools/atlas_tool.py update
```

**Workflow for adding new item sprites:**
1. Add sprite to `res/sprites/<name>.png`
2. Run `python3 tools/atlas_tool.py pack --update`
3. Atlas repacked, items.json updated with coordinates

---

### atlas_picker.html

Simple HTML tool to select regions from atlas.png.

```bash
open tools/atlas_picker.html
```

**Usage:**
1. Select a resource ID from the list
2. Click and drag on atlas to select sprite region
3. Copy JSON output with x, y, w, h coordinates

---

## File Locations

**Textures:**
- `res/img/atlas.png` - Sprite atlas (items)
- `res/img/*.png` - Individual textures (NPCs, player, logos)
- `res/img/buildings/` - Building textures (seasonal)
- `res/img/obstacles/` - Obstacle textures (seasonal)
- `res/img/biomes/` - Biome tiles (seasonal)

**Data:**
- `res/data/items.json` - Item definitions with atlas coords
- `res/data/materials_and_currency.json` - Material definitions
- `res/data/atlas.json` - Atlas region definitions

**Extracted:**
- `res/sprites/` - Individual sprites extracted from atlas

---

## How Textures Work at Runtime

```
JSON (defines texture ID)
    ↓ Load time
TextureManager.getTexturePtr(textureId) → SDL_Texture*
    ↓ Cache in
RenderData.cachedTexture (stored in EDM)
    ↓ Runtime
Render directly from cached pointer - NO LOOKUPS
```

JSON files just define mappings. Code resolves to cached pointers at load time.
