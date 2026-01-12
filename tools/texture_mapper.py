#!/usr/bin/env python3
"""
Texture Mapper Tool for HammerEngine

Unified tool to map entity IDs to texture IDs for:
- Items (atlas-based)
- Materials/Currency
- NPCs
- World Objects (biomes, obstacles, buildings, decorations)

JSON files define the mapping, code resolves to cached pointers at load time.

Usage:
    python3 tools/texture_mapper.py
    # Opens texture_mapper.html in browser
"""

import argparse
import json
import webbrowser
from pathlib import Path


def get_paths():
    """Get standard project paths."""
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    return {
        'res_img': project_root / "res" / "img",
        'res_sprites': project_root / "res" / "sprites",
        'res_data': project_root / "res" / "data",
        'items_json': project_root / "res" / "data" / "items.json",
        'materials_json': project_root / "res" / "data" / "materials_and_currency.json",
        'npc_types_json': project_root / "res" / "data" / "npc_types.json",
        'world_objects_json': project_root / "res" / "data" / "world_objects.json",
        'output_html': project_root / "tools" / "texture_mapper.html",
    }


def scan_textures(img_dir: Path, sprites_dir: Path) -> dict:
    """Scan res/img and res/sprites, organize by category."""
    textures = {
        'sprites': [],      # res/sprites/ (source sprites for atlas)
        'npcs': [],         # NPC textures
        'biomes': [],       # Biome tiles
        'obstacles': [],    # Obstacles
        'buildings': [],    # Buildings
        'decorations': [],  # Flowers, mushrooms, etc.
        'other': [],        # Everything else
    }

    def categorize_texture(name: str, path: str, rel_path: str):
        """Categorize a texture based on its name and path."""
        # Detect seasonal prefix
        seasonal_prefix = None
        base_name = name
        for prefix in ['spring_', 'summer_', 'fall_', 'winter_']:
            if name.startswith(prefix):
                seasonal_prefix = prefix[:-1]
                base_name = name[len(prefix):]
                break

        texture_info = {
            'name': name,
            'baseName': base_name,
            'path': path,
            'relPath': rel_path,
            'seasonal': seasonal_prefix,
        }

        # Categorize by path or name pattern
        path_lower = path.lower()
        name_lower = name.lower()

        if 'biome' in path_lower or 'biome' in name_lower:
            textures['biomes'].append(texture_info)
        elif 'obstacle' in path_lower or name_lower.startswith('obstacle_'):
            textures['obstacles'].append(texture_info)
        elif 'building' in path_lower or name_lower.startswith('building_'):
            textures['buildings'].append(texture_info)
        elif any(x in path_lower for x in ['flower', 'mushroom', 'grass', 'bush', 'stump', 'dead_grass']):
            textures['decorations'].append(texture_info)
        elif any(x in name_lower for x in ['flower', 'mushroom', 'grass', 'bush', 'stump', 'dead_grass']):
            textures['decorations'].append(texture_info)
        elif name_lower in ['guard', 'villager', 'merchant', 'warrior', 'npc']:
            textures['npcs'].append(texture_info)
        else:
            textures['other'].append(texture_info)

    # Scan res/sprites/ first (source sprites for atlas)
    if sprites_dir.exists():
        for png_path in sorted(sprites_dir.rglob("*.png")):
            rel_path = png_path.relative_to(sprites_dir.parent.parent)  # relative to res/
            name = png_path.stem
            textures['sprites'].append({
                'name': name,
                'baseName': name,
                'path': f"sprites/{png_path.name}",
                'relPath': str(rel_path),
                'seasonal': None,
            })

    # Scan res/img/ (current textures)
    if img_dir.exists():
        for png_path in sorted(img_dir.rglob("*.png")):
            rel_path = png_path.relative_to(img_dir)
            name = png_path.stem
            path_str = str(rel_path)

            # Skip atlas.png and non-game assets
            if name in ['atlas', 'icon', 'HammerForgeBanner', 'HammerEngine', 'cpp']:
                continue

            categorize_texture(name, path_str, str(rel_path))

    return textures


def load_json_safe(path: Path) -> dict:
    """Load JSON file safely."""
    try:
        if path.exists():
            with open(path, 'r') as f:
                return json.load(f)
    except Exception as e:
        print(f"Warning: Could not load {path}: {e}")
    return {}


def generate_html(textures: dict, items_data: dict, materials_data: dict,
                  npc_data: dict, world_data: dict) -> str:
    """Generate the HTML texture mapper tool."""

    # Extract existing data
    items = [{'id': r.get('id', ''), 'name': r.get('name', ''),
              'textureId': r.get('worldTextureId', '')}
             for r in items_data.get('resources', [])]

    materials = [{'id': r.get('id', ''), 'name': r.get('name', ''),
                  'textureId': r.get('worldTextureId', '')}
                 for r in materials_data.get('resources', [])]

    npcs = [{'id': n.get('id', ''), 'name': n.get('name', n.get('id', '')),
             'textureId': n.get('textureId', '')}
            for n in npc_data.get('npcTypes', [])]

    # Flatten world objects into list format
    world_objects = []
    for category in ['biomes', 'obstacles', 'buildings', 'decorations']:
        cat_data = world_data.get(category, {})
        for key, val in cat_data.items():
            world_objects.append({
                'id': key,
                'name': f"{category}:{key}",
                'textureId': val.get('textureId', ''),
                'seasonal': val.get('seasonal', False),
                'category': category
            })

    # Serialize for JavaScript
    textures_json = json.dumps(textures, indent=2)
    items_json = json.dumps(items, indent=2)
    materials_json = json.dumps(materials, indent=2)
    npcs_json = json.dumps(npcs, indent=2)
    world_json = json.dumps(world_objects, indent=2)

    html = f'''<!DOCTYPE html>
<html>
<head>
    <title>Texture Mapper - HammerEngine</title>
    <style>
        * {{ box-sizing: border-box; }}
        body {{ font-family: 'Segoe UI', sans-serif; background: #1a1a2e; color: #eee; margin: 0; padding: 20px; }}
        h1 {{ color: #0f9; margin-bottom: 5px; }}
        .subtitle {{ color: #888; margin-bottom: 20px; }}

        .layout {{ display: flex; gap: 20px; height: calc(100vh - 200px); }}

        /* Texture Browser */
        .texture-panel {{ flex: 2; background: #252540; border-radius: 8px; overflow: hidden; display: flex; flex-direction: column; }}
        .folder-tabs {{ display: flex; flex-wrap: wrap; background: #1a1a2e; padding: 10px; gap: 5px; }}
        .folder-tab {{ padding: 8px 16px; background: #333; border: none; color: #aaa; cursor: pointer; border-radius: 4px; font-size: 13px; }}
        .folder-tab:hover {{ background: #444; color: #fff; }}
        .folder-tab.active {{ background: #0f9; color: #000; font-weight: bold; }}
        .folder-tab .count {{ font-size: 11px; opacity: 0.7; margin-left: 4px; }}

        .texture-grid {{ flex: 1; overflow-y: auto; padding: 15px; display: grid; grid-template-columns: repeat(auto-fill, minmax(120px, 1fr)); gap: 10px; }}
        .texture-card {{ background: #1a1a2e; border: 2px solid #333; border-radius: 8px; padding: 10px; text-align: center; cursor: pointer; }}
        .texture-card:hover {{ border-color: #0f9; }}
        .texture-card.selected {{ border-color: #0f9; background: #1a2a3e; }}
        .texture-card img {{ max-width: 100px; max-height: 80px; image-rendering: pixelated; background: #333; }}
        .texture-card .name {{ font-size: 11px; color: #aaa; margin-top: 8px; word-break: break-all; }}
        .texture-card .seasonal {{ font-size: 9px; background: #f90; color: #000; padding: 2px 6px; border-radius: 3px; margin-top: 4px; display: inline-block; }}

        /* Entity Panel */
        .entity-panel {{ width: 400px; background: #252540; border-radius: 8px; overflow: hidden; display: flex; flex-direction: column; }}
        .entity-tabs {{ display: flex; flex-wrap: wrap; background: #1a1a2e; }}
        .entity-tab {{ flex: 1; min-width: 60px; padding: 10px 5px; background: none; border: none; color: #888; cursor: pointer; font-size: 12px; }}
        .entity-tab:hover {{ color: #ccc; }}
        .entity-tab.active {{ background: #252540; color: #0f9; font-weight: bold; }}

        .entity-list {{ flex: 1; overflow-y: auto; padding: 10px; }}
        .entity-item {{ display: flex; align-items: center; padding: 10px; background: #1a1a2e; border-radius: 6px; margin-bottom: 8px; cursor: pointer; border: 2px solid transparent; }}
        .entity-item:hover {{ border-color: #444; }}
        .entity-item.selected {{ border-color: #0f9; }}
        .entity-item.mapped {{ background: #1a3a2e; }}
        .entity-info {{ flex: 1; }}
        .entity-info .id {{ font-weight: bold; color: #0f9; }}
        .entity-info .name {{ font-size: 12px; color: #888; }}
        .entity-info .texture {{ font-size: 11px; color: #666; }}
        .entity-info .category {{ font-size: 10px; background: #444; padding: 2px 6px; border-radius: 3px; margin-left: 5px; }}
        .entity-info .seasonal-badge {{ font-size: 10px; background: #f90; color: #000; padding: 2px 6px; border-radius: 3px; margin-left: 5px; }}

        /* Output */
        .output-panel {{ margin-top: 20px; background: #252540; border-radius: 8px; padding: 15px; }}
        .output-panel h3 {{ color: #0f9; margin: 0 0 10px 0; }}
        .export-btns {{ margin-bottom: 10px; display: flex; gap: 10px; flex-wrap: wrap; }}
        .btn {{ padding: 10px 20px; background: #0f9; color: #000; border: none; border-radius: 4px; cursor: pointer; font-weight: bold; }}
        .btn:hover {{ background: #0da; }}
        .btn-secondary {{ background: #444; color: #eee; }}
        .btn-secondary:hover {{ background: #555; }}
        textarea {{ width: 100%; height: 150px; background: #1a1a2e; color: #0f9; border: 1px solid #333; font-family: monospace; padding: 10px; border-radius: 4px; }}

        .search {{ width: 100%; padding: 10px; background: #333; border: 1px solid #444; color: #eee; border-radius: 4px; margin-bottom: 10px; }}

        .status {{ position: fixed; bottom: 0; left: 0; right: 0; padding: 10px 20px; background: #252540; border-top: 1px solid #333; font-size: 12px; color: #888; }}
    </style>
</head>
<body>
    <h1>Texture Mapper</h1>
    <p class="subtitle">Map entity IDs to texture IDs. JSON defines mapping, code caches pointers at load time.</p>

    <div class="layout">
        <div class="texture-panel">
            <div class="folder-tabs" id="folderTabs"></div>
            <input type="text" class="search" id="textureSearch" placeholder="Search textures..." oninput="renderTextures()">
            <div class="texture-grid" id="textureGrid"></div>
        </div>

        <div class="entity-panel">
            <div class="entity-tabs">
                <button class="entity-tab active" onclick="switchEntityTab('items')">Items</button>
                <button class="entity-tab" onclick="switchEntityTab('materials')">Materials</button>
                <button class="entity-tab" onclick="switchEntityTab('npcs')">NPCs</button>
                <button class="entity-tab" onclick="switchEntityTab('world')">World</button>
                <button class="entity-tab" onclick="switchEntityTab('custom')">Custom</button>
            </div>
            <input type="text" class="search" id="entitySearch" placeholder="Search entities..." oninput="renderEntities()">
            <div class="entity-list" id="entityList"></div>
        </div>
    </div>

    <div class="output-panel">
        <h3>JSON Output</h3>
        <div class="export-btns">
            <button class="btn" onclick="copyOutput()">Copy</button>
            <button class="btn btn-secondary" onclick="exportMappingsOnly()">Mappings Only</button>
            <button class="btn btn-secondary" onclick="exportForItems()">items.json</button>
            <button class="btn btn-secondary" onclick="exportForMaterials()">materials.json</button>
            <button class="btn btn-secondary" onclick="exportForNPCs()">npc_types.json</button>
            <button class="btn btn-secondary" onclick="exportForWorld()">world_objects.json</button>
            <button class="btn btn-secondary" onclick="clearMappings()">Clear</button>
        </div>
        <textarea id="output" readonly></textarea>
    </div>

    <div class="status">
        <span id="statusText">Select an entity, then click a texture to map it</span>
    </div>

    <script>
        // Data
        const textures = {textures_json};
        const itemsData = {items_json};
        const materialsData = {materials_json};
        const npcsData = {npcs_json};
        const worldData = {world_json};

        // State
        let currentFolder = 'biomes';
        let currentEntityTab = 'items';
        let selectedEntity = null;
        let selectedTexture = null;
        let mappings = {{}};
        let customEntities = [];

        // Initialize
        document.addEventListener('DOMContentLoaded', () => {{
            // Pick first non-empty category
            for (const cat of ['biomes', 'obstacles', 'buildings', 'decorations', 'npcs', 'sprites', 'other']) {{
                if (textures[cat] && textures[cat].length > 0) {{
                    currentFolder = cat;
                    break;
                }}
            }}
            renderFolderTabs();
            renderTextures();
            renderEntities();
        }});

        function renderFolderTabs() {{
            const container = document.getElementById('folderTabs');
            container.innerHTML = '';

            const order = ['biomes', 'obstacles', 'buildings', 'decorations', 'npcs', 'sprites', 'other'];

            order.forEach(folder => {{
                const items = textures[folder] || [];
                if (items.length === 0) return;

                const btn = document.createElement('button');
                btn.className = 'folder-tab' + (folder === currentFolder ? ' active' : '');
                btn.innerHTML = `${{folder}}<span class="count">(${{items.length}})</span>`;
                btn.onclick = () => {{
                    currentFolder = folder;
                    renderFolderTabs();
                    renderTextures();
                }};
                container.appendChild(btn);
            }});
        }}

        function getImagePath(tex) {{
            // Build path relative to HTML file location (tools/)
            if (tex.path.startsWith('sprites/')) {{
                return '../res/' + tex.path;
            }}
            return '../res/img/' + tex.path;
        }}

        function renderTextures() {{
            const grid = document.getElementById('textureGrid');
            const search = document.getElementById('textureSearch').value.toLowerCase();
            grid.innerHTML = '';

            const items = textures[currentFolder] || [];
            items.forEach(tex => {{
                if (search && !tex.name.toLowerCase().includes(search)) return;

                const card = document.createElement('div');
                card.className = 'texture-card' + (selectedTexture?.name === tex.name ? ' selected' : '');
                card.innerHTML = `
                    <img src="${{getImagePath(tex)}}" alt="${{tex.name}}" onerror="this.style.display='none'">
                    <div class="name">${{tex.name}}</div>
                    ${{tex.seasonal ? `<div class="seasonal">${{tex.seasonal}}</div>` : ''}}
                `;
                card.onclick = () => selectTexture(tex);
                grid.appendChild(card);
            }});
        }}

        function switchEntityTab(tab) {{
            currentEntityTab = tab;
            document.querySelectorAll('.entity-tab').forEach(t => t.classList.remove('active'));
            event.target.classList.add('active');
            renderEntities();
        }}

        function renderEntities() {{
            const list = document.getElementById('entityList');
            const search = document.getElementById('entitySearch').value.toLowerCase();
            list.innerHTML = '';

            let entities;
            if (currentEntityTab === 'items') entities = itemsData;
            else if (currentEntityTab === 'materials') entities = materialsData;
            else if (currentEntityTab === 'npcs') entities = npcsData;
            else if (currentEntityTab === 'world') entities = worldData;
            else entities = customEntities;

            // Add custom entity button for custom tab
            if (currentEntityTab === 'custom') {{
                const addBtn = document.createElement('div');
                addBtn.className = 'entity-item';
                addBtn.style.justifyContent = 'center';
                addBtn.style.color = '#0f9';
                addBtn.innerHTML = '+ Add Custom Entity';
                addBtn.onclick = addCustomEntity;
                list.appendChild(addBtn);
            }}

            entities.forEach(entity => {{
                if (search && !entity.id.toLowerCase().includes(search) && !(entity.name || '').toLowerCase().includes(search)) return;

                const mapping = mappings[entity.id];
                const div = document.createElement('div');
                div.className = 'entity-item' +
                    (selectedEntity?.id === entity.id ? ' selected' : '') +
                    (mapping ? ' mapped' : '');

                let badges = '';
                if (entity.category) {{
                    badges += `<span class="category">${{entity.category}}</span>`;
                }}
                if (entity.seasonal) {{
                    badges += `<span class="seasonal-badge">seasonal</span>`;
                }}

                div.innerHTML = `
                    <div class="entity-info">
                        <div class="id">${{entity.id}}${{badges}}</div>
                        <div class="name">${{entity.name || ''}}</div>
                        <div class="texture">&rarr; ${{mapping || entity.textureId || 'unmapped'}}</div>
                    </div>
                `;
                div.onclick = () => selectEntity(entity);
                list.appendChild(div);
            }});
        }}

        function selectEntity(entity) {{
            selectedEntity = entity;
            renderEntities();
            updateStatus(`Selected: ${{entity.id}} - Click a texture to map`);
        }}

        function selectTexture(tex) {{
            selectedTexture = tex;
            renderTextures();

            if (selectedEntity) {{
                mappings[selectedEntity.id] = tex.name;
                updateOutput();
                renderEntities();
                updateStatus(`Mapped ${{selectedEntity.id}} &rarr; ${{tex.name}}`);
            }} else {{
                updateStatus(`Selected texture: ${{tex.name}} - Select an entity first`);
            }}
        }}

        function addCustomEntity() {{
            const id = prompt('Entity ID:');
            if (!id) return;
            const name = prompt('Entity Name:', id);
            customEntities.push({{ id, name, textureId: '' }});
            renderEntities();
        }}

        function updateOutput() {{
            document.getElementById('output').value = JSON.stringify(mappings, null, 2);
        }}

        function exportMappingsOnly() {{
            document.getElementById('output').value = JSON.stringify(mappings, null, 2);
            updateStatus('Exported mappings only');
        }}

        function exportForItems() {{
            const result = itemsData.map(item => ({{
                ...item,
                worldTextureId: mappings[item.id] || item.textureId
            }}));
            document.getElementById('output').value = JSON.stringify({{ resources: result }}, null, 2);
            updateStatus('Exported for items.json');
        }}

        function exportForMaterials() {{
            const result = materialsData.map(mat => ({{
                ...mat,
                worldTextureId: mappings[mat.id] || mat.textureId
            }}));
            document.getElementById('output').value = JSON.stringify({{ resources: result }}, null, 2);
            updateStatus('Exported for materials.json');
        }}

        function exportForNPCs() {{
            const result = npcsData.map(npc => ({{
                ...npc,
                textureId: mappings[npc.id] || npc.textureId
            }}));
            document.getElementById('output').value = JSON.stringify({{ npcTypes: result }}, null, 2);
            updateStatus('Exported for npc_types.json');
        }}

        function exportForWorld() {{
            // Rebuild world_objects.json structure
            const result = {{
                biomes: {{}},
                obstacles: {{}},
                buildings: {{}},
                decorations: {{}}
            }};

            worldData.forEach(obj => {{
                const cat = obj.category;
                if (result[cat]) {{
                    result[cat][obj.id] = {{
                        textureId: mappings[obj.id] || obj.textureId,
                        seasonal: obj.seasonal
                    }};
                }}
            }});

            document.getElementById('output').value = JSON.stringify(result, null, 2);
            updateStatus('Exported for world_objects.json');
        }}

        function copyOutput() {{
            const output = document.getElementById('output');
            output.select();
            document.execCommand('copy');
            updateStatus('Copied to clipboard!');
        }}

        function clearMappings() {{
            if (confirm('Clear all mappings?')) {{
                mappings = {{}};
                updateOutput();
                renderEntities();
                updateStatus('Cleared');
            }}
        }}

        function updateStatus(msg) {{
            document.getElementById('statusText').innerHTML = msg;
        }}
    </script>
</body>
</html>
'''
    return html


def main():
    parser = argparse.ArgumentParser(description='Texture Mapper Tool')
    parser.add_argument('--no-open', action='store_true', help='Do not open browser')
    args = parser.parse_args()

    paths = get_paths()

    print("Scanning textures...")
    textures = scan_textures(paths['res_img'], paths['res_sprites'])

    # Print summary
    print("  Texture categories:")
    for cat in ['biomes', 'obstacles', 'buildings', 'decorations', 'npcs', 'sprites', 'other']:
        items = textures.get(cat, [])
        if items:
            seasonal = sum(1 for i in items if i['seasonal'])
            print(f"    {cat}: {len(items)} ({seasonal} seasonal)")

    print("\nLoading JSON data...")
    items = load_json_safe(paths['items_json'])
    materials = load_json_safe(paths['materials_json'])
    npcs = load_json_safe(paths['npc_types_json'])
    world = load_json_safe(paths['world_objects_json'])

    print(f"  Items: {len(items.get('resources', []))}")
    print(f"  Materials: {len(materials.get('resources', []))}")
    print(f"  NPCs: {len(npcs.get('npcTypes', []))}")
    world_count = sum(len(world.get(cat, {})) for cat in ['biomes', 'obstacles', 'buildings', 'decorations'])
    print(f"  World Objects: {world_count}")

    print("\nGenerating HTML...")
    html = generate_html(textures, items, materials, npcs, world)

    output_path = paths['output_html']
    with open(output_path, 'w') as f:
        f.write(html)
    print(f"  Saved: {output_path}")

    if not args.no_open:
        print("Opening in browser...")
        webbrowser.open(f'file://{output_path}')

    print("\nDone!")


if __name__ == "__main__":
    main()
