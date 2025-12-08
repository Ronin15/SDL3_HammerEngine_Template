#!/bin/bash
# Copyright (c) 2025 Hammer Forged Games
# All rights reserved.
# Licensed under the MIT License - see LICENSE file for details

# Generate seasonal texture variants using ImageMagick
# Creates spring_, summer_, fall_, winter_ prefixed versions of tile textures

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
IMG_DIR="$PROJECT_ROOT/res/img"

echo "Generating seasonal texture variants..."
echo "Source directory: $IMG_DIR"

# Textures that get seasonal variants
TILE_TEXTURES=(
    "biome_celestial"
    "biome_default"
    "biome_desert"
    "biome_forest"
    "biome_haunted"
    "biome_mountain"
    "biome_ocean"
    "biome_swamp"
    "building_cityhall"
    "building_house"
    "building_hut"
    "building_large"
    "obstacle_building_solid"
    "obstacle_rock"
    "obstacle_tree"
    "obstacle_water"
)

# Track generated files
generated=0
skipped=0

for texture in "${TILE_TEXTURES[@]}"; do
    src="$IMG_DIR/${texture}.png"

    if [[ ! -f "$src" ]]; then
        echo "WARNING: Source texture not found: $src"
        skipped=$((skipped + 1))
        continue
    fi

    echo "Processing: $texture"

    # Spring - vibrant, slight green boost, bright
    spring_out="$IMG_DIR/spring_${texture}.png"
    convert "$src" -modulate 105,115,100 -level 0%,100%,1.02 "$spring_out"
    generated=$((generated + 1))

    # Summer - warm golden tones, high saturation
    summer_out="$IMG_DIR/summer_${texture}.png"
    convert "$src" -modulate 100,125,100 -colorize 3,3,0 "$summer_out"
    generated=$((generated + 1))

    # Fall - orange/brown hue shift (hue rotate toward warm colors)
    fall_out="$IMG_DIR/fall_${texture}.png"
    convert "$src" -modulate 95,90,115 "$fall_out"
    generated=$((generated + 1))

    # Winter - desaturated, blue tint, snow overlay effect
    winter_out="$IMG_DIR/winter_${texture}.png"
    convert "$src" -modulate 90,50,100 -colorize 0,0,12 \
        \( +clone -threshold 65% -blur 0x0.5 -modulate 100,0,100 \) \
        -compose screen -composite "$winter_out"
    generated=$((generated + 1))
done

echo ""
echo "Seasonal texture generation complete!"
echo "Generated: $generated textures"
echo "Skipped: $skipped textures"
echo ""
echo "Textures created in: $IMG_DIR"
ls -la "$IMG_DIR" | grep -E "^-.*_(spring|summer|fall|winter)_" | wc -l | xargs -I {} echo "Seasonal texture files: {}"
