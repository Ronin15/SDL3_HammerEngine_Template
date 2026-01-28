/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef LOGO_STATE_HPP
#define LOGO_STATE_HPP

#include "gameStates/GameState.hpp"
#include <cstdint>
#include <vector>

#ifdef USE_SDL3_GPU
struct SDL_GPUTexture;
#endif

class LogoState : public GameState {
 public:
  bool enter() override;
  void update(float deltaTime) override;
  void render(SDL_Renderer* renderer, float interpolationAlpha = 1.0f) override;
  void handleInput() override;
  bool exit() override;
  std::string getName() const override;

#ifdef USE_SDL3_GPU
  // GPU rendering support
  void recordGPUVertices(HammerEngine::GPURenderer& gpuRenderer,
                          float interpolationAlpha) override;
  void renderGPUScene(HammerEngine::GPURenderer& gpuRenderer,
                       SDL_GPURenderPass* scenePass,
                       float interpolationAlpha) override;
  void renderGPUUI(HammerEngine::GPURenderer& gpuRenderer,
                    SDL_GPURenderPass* swapchainPass) override;
  bool supportsGPURendering() const override { return true; }
#endif

 private:
  void recalculateLayout();

  float m_stateTimer{0.0f};

  // Cached layout calculations (computed once in enter())
  int m_windowWidth{0};
  int m_windowHeight{0};
  int m_bannerSize{0};
  int m_engineSize{0};
  int m_sdlSize{0};
  int m_cppSize{0};

  // Cached positions
  int m_bannerX{0}, m_bannerY{0};
  int m_engineX{0}, m_engineY{0};
  int m_cppX{0}, m_cppY{0};
  int m_sdlX{0}, m_sdlY{0};
  int m_titleY{0};
  int m_subtitleY{0};
  int m_versionY{0};

#ifdef USE_SDL3_GPU
  // GPU draw commands for multiple textures (scene rendering)
  struct GPUDrawCommand {
    SDL_GPUTexture* texture{nullptr};
    uint32_t vertexOffset{0};
    uint32_t vertexCount{0};
  };
  std::vector<GPUDrawCommand> m_drawCommands;

  // GPU draw commands for UI text (swapchain rendering)
  std::vector<GPUDrawCommand> m_textDrawCommands;
#endif
};

#endif  // LOGO_STATE_HPP
