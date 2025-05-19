#!/bin/bash

# Script to update all include paths in the project
# It performs multiple sed replacements in each source file

echo "Updating include paths in source files..."

# Process all .cpp and .hpp files in src, include, and tests directories
find src include tests -type f \( -name "*.cpp" -o -name "*.hpp" \) | while read -r file; do
  echo "Processing $file"

  # Update all include statements to use the new module paths
  # Core files
  sed -i.bak 's|#include "GameEngine.hpp"|#include "core/GameEngine.hpp"|g' "$file"
  sed -i.bak 's|#include "InputHandler.hpp"|#include "managers/InputManager.hpp"|g' "$file"

  # Manager files
  sed -i.bak 's|#include "GameStateManager.hpp"|#include "managers/GameStateManager.hpp"|g' "$file"
  sed -i.bak 's|#include "EntityStateManager.hpp"|#include "managers/EntityStateManager.hpp"|g' "$file"
  sed -i.bak 's|#include "FontManager.hpp"|#include "managers/FontManager.hpp"|g' "$file"
  sed -i.bak 's|#include "TextureManager.hpp"|#include "managers/TextureManager.hpp"|g' "$file"
  sed -i.bak 's|#include "SoundManager.hpp"|#include "managers/SoundManager.hpp"|g' "$file"

  # State files
  sed -i.bak 's|#include "GameState.hpp"|#include "states/GameState.hpp"|g' "$file"
  sed -i.bak 's|#include "LogoState.hpp"|#include "states/LogoState.hpp"|g' "$file"
  sed -i.bak 's|#include "MainMenuState.hpp"|#include "states/MainMenuState.hpp"|g' "$file"
  sed -i.bak 's|#include "GamePlayState.hpp"|#include "states/GamePlayState.hpp"|g' "$file"
  sed -i.bak 's|#include "PauseState.hpp"|#include "states/PauseState.hpp"|g' "$file"
  sed -i.bak 's|#include "AIDemoState.hpp"|#include "states/AIDemoState.hpp"|g' "$file"

  # Entity files
  sed -i.bak 's|#include "Entity.hpp"|#include "entities/Entity.hpp"|g' "$file"
  sed -i.bak 's|#include "Player.hpp"|#include "entities/Player.hpp"|g' "$file"
  sed -i.bak 's|#include "NPC.hpp"|#include "entities/NPC.hpp"|g' "$file"

  # Entity state files
  sed -i.bak 's|#include "EntityState.hpp"|#include "entities/states/EntityState.hpp"|g' "$file"
  sed -i.bak 's|#include "PlayerIdleState.hpp"|#include "entities/states/PlayerIdleState.hpp"|g' "$file"
  sed -i.bak 's|#include "PlayerRunningState.hpp"|#include "entities/states/PlayerRunningState.hpp"|g' "$file"

  # AI files
  sed -i.bak 's|#include "AIManager.hpp"|#include "ai/AIManager.hpp"|g' "$file"
  sed -i.bak 's|#include "AIBehavior.hpp"|#include "ai/AIBehavior.hpp"|g' "$file"

  # AI behavior files
  sed -i.bak 's|#include "ChaseBehavior.hpp"|#include "ai/behaviors/ChaseBehavior.hpp"|g' "$file"
  sed -i.bak 's|#include "PatrolBehavior.hpp"|#include "ai/behaviors/PatrolBehavior.hpp"|g' "$file"
  sed -i.bak 's|#include "WanderBehavior.hpp"|#include "ai/behaviors/WanderBehavior.hpp"|g' "$file"

  # Utils files
  sed -i.bak 's|#include "ThreadSystem.hpp"|#include "utils/ThreadSystem.hpp"|g' "$file"
  sed -i.bak 's|#include "Vector2D.hpp"|#include "utils/Vector2D.hpp"|g' "$file"

  # I/O files
  sed -i.bak 's|#include "SaveGameManager.hpp"|#include "io/SaveGameManager.hpp"|g' "$file"

  # Clean up backup files
  rm "${file}.bak"
done

echo "Include path update complete!"
