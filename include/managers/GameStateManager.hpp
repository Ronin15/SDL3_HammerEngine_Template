/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef GAME_STATE_MANAGER_HPP
#define GAME_STATE_MANAGER_HPP

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <SDL3/SDL_gpu.h>
#include "gameStates/GameState.hpp"

namespace HammerEngine {
class GPURenderer;
}

class GameStateManager {

 public:
  GameStateManager();
  void addState(std::unique_ptr<GameState> state);
  void pushState(const std::string& stateName);
  void popState();
  void changeState(const std::string& stateName); // Pops the current state and pushes a new one

  void update(float deltaTime);
  void handleInput();

  /**
   * Record vertices for GPU rendering (called before scene pass).
   * Delegates to active state's recordGPUVertices().
   */
  void recordGPUVertices(HammerEngine::GPURenderer& gpuRenderer, float interpolationAlpha);

  /**
   * Issue GPU draw calls during scene pass.
   * Delegates to active state's renderGPUScene().
   */
  void renderGPUScene(HammerEngine::GPURenderer& gpuRenderer,
                       SDL_GPURenderPass* scenePass,
                       float interpolationAlpha);

  /**
   * Render UI/overlays during swapchain pass.
   * Delegates to active state's renderGPUUI().
   */
  void renderGPUUI(HammerEngine::GPURenderer& gpuRenderer,
                    SDL_GPURenderPass* swapchainPass);

  bool hasState(const std::string& stateName) const;
  std::shared_ptr<GameState> getState(const std::string& stateName) const;
  void removeState(const std::string& stateName);
  void clearAllStates();

  // Frame data pushed from GameEngine - avoids states calling GameEngine::Instance()
  void setCurrentFPS(float fps) { m_currentFPS = fps; }
  float getCurrentFPS() const { return m_currentFPS; }

 private:
  // All registered states, available for activation
  std::unordered_map<std::string, std::shared_ptr<GameState>> m_registeredStates;
  // The stack of active states
  std::vector<std::shared_ptr<GameState>> m_activeStates;

  float m_lastDeltaTime{0.0f}; // Store deltaTime from update to pass to render
  float m_currentFPS{0.0f};    // Current FPS pushed from GameEngine
};

#endif  // GAME_STATE_MANAGER_HPP
