#!/bin/bash

# Script to update all include paths to use the new module-based structure
# Run this from the root of the project

# Define directories to search for .cpp and .hpp files
SRC_DIRS=("src" "include" "tests")

# Define the mapping of headers to their new paths
declare -A HEADER_MAP
HEADER_MAP["AIBehavior.hpp"]="ai/AIBehavior.hpp"
HEADER_MAP["AIDemoState.hpp"]="states/AIDemoState.hpp"
HEADER_MAP["AIManager.hpp"]="ai/AIManager.hpp"
HEADER_MAP["ChaseBehavior.hpp"]="ai/behaviors/ChaseBehavior.hpp"
HEADER_MAP["Entity.hpp"]="entities/Entity.hpp"
HEADER_MAP["EntityState.hpp"]="entities/states/EntityState.hpp"
HEADER_MAP["EntityStateManager.hpp"]="managers/EntityStateManager.hpp"
HEADER_MAP["FontManager.hpp"]="managers/FontManager.hpp"
HEADER_MAP["GameEngine.hpp"]="core/GameEngine.hpp"
HEADER_MAP["GamePlayState.hpp"]="states/GamePlayState.hpp"
HEADER_MAP["GameState.hpp"]="states/GameState.hpp"
HEADER_MAP["GameStateManager.hpp"]="managers/GameStateManager.hpp"
HEADER_MAP["InputHandler.hpp"]="core/InputHandler.hpp"
HEADER_MAP["LogoState.hpp"]="states/LogoState.hpp"
HEADER_MAP["MainMenuState.hpp"]="states/MainMenuState.hpp"
HEADER_MAP["NPC.hpp"]="entities/NPC.hpp"
HEADER_MAP["PatrolBehavior.hpp"]="ai/behaviors/PatrolBehavior.hpp"
HEADER_MAP["PauseState.hpp"]="states/PauseState.hpp"
HEADER_MAP["Player.hpp"]="entities/Player.hpp"
HEADER_MAP["PlayerIdleState.hpp"]="entities/states/PlayerIdleState.hpp"
HEADER_MAP["PlayerRunningState.hpp"]="entities/states/PlayerRunningState.hpp"
HEADER_MAP["SaveGameManager.hpp"]="io/SaveGameManager.hpp"
HEADER_MAP["SoundManager.hpp"]="managers/SoundManager.hpp"
HEADER_MAP["TextureManager.hpp"]="managers/TextureManager.hpp"
HEADER_MAP["ThreadSystem.hpp"]="utils/ThreadSystem.hpp"
HEADER_MAP["Vector2D.hpp"]="utils/Vector2D.hpp"
HEADER_MAP["WanderBehavior.hpp"]="ai/behaviors/WanderBehavior.hpp"

# Function to update includes in a file
update_includes() {
    local file=$1
    local original_content
    original_content=$(cat "$file")
    local new_content="$original_content"
    
    for header in "${!HEADER_MAP[@]}"; do
        new_path="${HEADER_MAP[$header]}"
        # Match include statement with the header, ensuring it's not already updated
        # This pattern matches both #include "Header.hpp" and #include<Header.hpp>
        # but not if the header already has a path prefix
        new_content=$(echo "$new_content" | sed -E "s/^([ \t]*#include[ \t]*[\"<])([^\/\"<>]*)$header([>\"])/\1$new_path\3/g")
    done
    
    # Only write if content changed
    if [ "$original_content" != "$new_content" ]; then
        echo "Updating includes in $file"
        echo "$new_content" > "$file"
    fi
}

# Find and process all source files
for dir in "${SRC_DIRS[@]}"; do
    echo "Processing directory: $dir"
    find "$dir" -type f \( -name "*.cpp" -o -name "*.hpp" \) | while read -r file; do
        update_includes "$file"
    done
done

echo "Include path update complete!"