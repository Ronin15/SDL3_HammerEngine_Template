/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "gameStates/LogoState.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"
#include "managers/SoundManager.hpp"
#include "managers/FontManager.hpp"
#include "managers/TextureManager.hpp"
#include "managers/UIManager.hpp"
#include "managers/GameStateManager.hpp"
#include <algorithm>
#include <format>

#ifdef USE_SDL3_GPU
#include "gpu/GPURenderer.hpp"
#include "gpu/GPUTypes.hpp"
#include "gpu/GPUVertexPool.hpp"
#include "gpu/SpriteBatch.hpp"
#include <SDL3/SDL_gpu.h>
#endif


bool LogoState::enter() {
  GAMESTATE_INFO("Entering LOGO State");

  // Pause all gameplay managers during logo display
  GameEngine::Instance().setGlobalPause(true);

  // Reset timer when entering state
  m_stateTimer = 0.0f;

  // Calculate initial layout
  recalculateLayout();

  // Cache SoundManager reference for better performance
  SoundManager& soundMgr = SoundManager::Instance();
  soundMgr.playSFX("sfx_logo", 0, 0);//change right value from 0 -> 1. For dev.
  return true;
}

void LogoState::recalculateLayout() {
  // Cache layout calculations
  const GameEngine& gameEngine = GameEngine::Instance();
  m_windowWidth = gameEngine.getLogicalWidth();
  m_windowHeight = gameEngine.getLogicalHeight();

  // Calculate scale factor for resolution-aware sizing (1920x1080 baseline)
  // Cap at 1.0 to prevent logos from scaling larger than original at high resolutions
  float scale = std::min(1.0f, std::min(m_windowWidth / 1920.0f, m_windowHeight / 1080.0f));

  // Scale all image dimensions proportionally
  m_bannerSize = static_cast<int>(256 * scale);
  m_engineSize = static_cast<int>(128 * scale);
  m_sdlSize = static_cast<int>(128 * scale);
  m_cppSize = static_cast<int>(50 * scale);

  // Calculate positions
  m_bannerX = m_windowWidth / 2 - m_bannerSize / 2;
  m_bannerY = (m_windowHeight / 2) - static_cast<int>(300 * scale);

  m_engineX = m_windowWidth / 2 - m_engineSize / 2;
  m_engineY = (m_windowHeight / 2) + static_cast<int>(10 * scale);

  m_cppX = m_windowWidth / 2 + static_cast<int>(155 * scale);
  m_cppY = (m_windowHeight / 2) + static_cast<int>(205 * scale);

  m_sdlX = (m_windowWidth / 2) - (m_sdlSize / 2) + static_cast<int>(20 * scale);
  m_sdlY = (m_windowHeight / 2) + static_cast<int>(260 * scale);

  m_titleY = (m_windowHeight / 2) + static_cast<int>(180 * scale);
  m_subtitleY = (m_windowHeight / 2) + static_cast<int>(220 * scale);
  m_versionY = (m_windowHeight / 2) + static_cast<int>(260 * scale);
}

void LogoState::update(float deltaTime) {
  m_stateTimer += deltaTime;

  if (m_stateTimer > 3.0f) {
    // Use immediate state change - proper enter/exit sequencing handles timing
    if (mp_stateManager->hasState("MainMenuState")) {
      mp_stateManager->changeState("MainMenuState");
    }
  }
}
void LogoState::render(SDL_Renderer* renderer, [[maybe_unused]] float interpolationAlpha) {
  // Check if window dimensions changed (fullscreen toggle, etc.)
  GameEngine& gameEngine = GameEngine::Instance();
  int currentWidth = gameEngine.getLogicalWidth();
  int currentHeight = gameEngine.getLogicalHeight();
  if (currentWidth != m_windowWidth || currentHeight != m_windowHeight) {
    recalculateLayout();
  }

  // Cache manager references for better performance
  TextureManager& texMgr = TextureManager::Instance();
  FontManager& fontMgr = FontManager::Instance();

  // Draw banner logo (positions cached in enter())
  texMgr.draw("HammerForgeBanner", m_bannerX, m_bannerY, m_bannerSize, m_bannerSize, renderer);

  // Draw engine logo
  texMgr.draw("HammerEngine", m_engineX, m_engineY, m_engineSize, m_engineSize, renderer);

  // Draw C++ logo
  texMgr.draw("cpp", m_cppX, m_cppY, m_cppSize, m_cppSize, renderer);

  // Render text using SDL_TTF
  SDL_Color fontColor = {200, 200, 200, 255}; // Light gray
  int centerX = m_windowWidth / 2;

  // Draw title text
  fontMgr.drawText("<]==={ }* Hammer Game Engine *{ }===]>", "fonts_Arial",
                   centerX, m_titleY, fontColor, renderer);

  // Draw subtitle text
  fontMgr.drawText("Powered by SDL3", "fonts_Arial",
                   centerX, m_subtitleY, fontColor, renderer);

  // Draw version text
  fontMgr.drawText("v0.8.5", "fonts_Arial",
                   centerX, m_versionY, fontColor, renderer);

  // Draw SDL logo centered below version text
  texMgr.draw("sdl_logo", m_sdlX, m_sdlY, m_sdlSize, m_sdlSize, renderer);
}

bool LogoState::exit() {
  GAMESTATE_INFO("Exiting LOGO State");

  // LogoState doesn't create UI components, so no UI cleanup needed

  return true;
}

void LogoState::handleInput() {
  // LogoState doesn't need input handling
}

std::string LogoState::getName() const {
  return "LogoState";
}

#ifdef USE_SDL3_GPU
void LogoState::recordGPUVertices(HammerEngine::GPURenderer& gpuRenderer,
                                   [[maybe_unused]] float interpolationAlpha) {
  // Check if window dimensions changed
  GameEngine& gameEngine = GameEngine::Instance();
  int currentWidth = gameEngine.getLogicalWidth();
  int currentHeight = gameEngine.getLogicalHeight();
  if (currentWidth != m_windowWidth || currentHeight != m_windowHeight) {
    recalculateLayout();
  }

  m_drawCommands.clear();
  TextureManager& texMgr = TextureManager::Instance();

  auto& vertexPool = gpuRenderer.getSpriteVertexPool();
  auto* basePtr = static_cast<HammerEngine::SpriteVertex*>(vertexPool.getMappedPtr());
  if (!basePtr) {
    return;
  }

  constexpr float sceneScale = 1.0f;  // Render at screen coordinates
  uint32_t vertexOffset = 0;

  // Helper to add a logo sprite
  auto addLogo = [&](const char* textureName, int x, int y, int size) {
    const GPUTextureData* texData = texMgr.getGPUTextureData(textureName);
    if (!texData || !texData->texture) {
      return;
    }

    // Write 4 vertices for this quad
    HammerEngine::SpriteVertex* v = basePtr + vertexOffset;
    float sx = static_cast<float>(x) * sceneScale;
    float sy = static_cast<float>(y) * sceneScale;
    float sw = static_cast<float>(size) * sceneScale;
    float sh = static_cast<float>(size) * sceneScale;

    // Top-left
    v[0] = {sx, sy, 0.0f, 0.0f, 255, 255, 255, 255};
    // Top-right
    v[1] = {sx + sw, sy, 1.0f, 0.0f, 255, 255, 255, 255};
    // Bottom-right
    v[2] = {sx + sw, sy + sh, 1.0f, 1.0f, 255, 255, 255, 255};
    // Bottom-left
    v[3] = {sx, sy + sh, 0.0f, 1.0f, 255, 255, 255, 255};

    // Record draw command
    GPUDrawCommand cmd;
    cmd.texture = texData->texture->get();
    cmd.vertexOffset = vertexOffset;
    cmd.vertexCount = 4;
    m_drawCommands.push_back(cmd);
    vertexOffset += 4;
  };

  // Add all logos
  addLogo("HammerForgeBanner", m_bannerX, m_bannerY, m_bannerSize);
  addLogo("HammerEngine", m_engineX, m_engineY, m_engineSize);
  addLogo("cpp", m_cppX, m_cppY, m_cppSize);
  addLogo("sdl_logo", m_sdlX, m_sdlY, m_sdlSize);

  // Set vertex count for pool upload
  vertexPool.setWrittenVertexCount(vertexOffset);

  // Record text vertices to UI vertex pool (rendered to swapchain)
  m_textDrawCommands.clear();
  FontManager& fontMgr = FontManager::Instance();
  SDL_Color fontColor = {200, 200, 200, 255};

  auto& uiPool = gpuRenderer.getUIVertexPool();
  auto* uiBasePtr = static_cast<HammerEngine::SpriteVertex*>(uiPool.getMappedPtr());
  if (!uiBasePtr) {
    return;
  }

  uint32_t uiVertexOffset = 0;
  int centerX = m_windowWidth / 2;

  // Helper to add text
  auto addText = [&](const std::string& text, int x, int y) {
    const GPUTextData* textData = fontMgr.renderTextGPU(text, "fonts_Arial", fontColor);
    if (!textData || !textData->texture || !textData->texture->isValid()) {
      return;
    }

    float dstX = static_cast<float>(x - textData->width / 2);
    float dstY = static_cast<float>(y - textData->height / 2);
    float dstW = static_cast<float>(textData->width);
    float dstH = static_cast<float>(textData->height);

    HammerEngine::SpriteVertex* v = uiBasePtr + uiVertexOffset;
    v[0] = {dstX, dstY, 0.0f, 0.0f, 200, 200, 200, 255};
    v[1] = {dstX + dstW, dstY, 1.0f, 0.0f, 200, 200, 200, 255};
    v[2] = {dstX + dstW, dstY + dstH, 1.0f, 1.0f, 200, 200, 200, 255};
    v[3] = {dstX, dstY + dstH, 0.0f, 1.0f, 200, 200, 200, 255};

    GPUDrawCommand cmd;
    cmd.texture = textData->texture->get();
    cmd.vertexOffset = uiVertexOffset;
    cmd.vertexCount = 4;
    m_textDrawCommands.push_back(cmd);
    uiVertexOffset += 4;
  };

  addText("<]==={ }* Hammer Game Engine *{ }===]>", centerX, m_titleY);
  addText("Powered by SDL3", centerX, m_subtitleY);
  addText("v0.8.5", centerX, m_versionY);

  uiPool.setWrittenVertexCount(uiVertexOffset);
}

void LogoState::renderGPUScene(HammerEngine::GPURenderer& gpuRenderer,
                                SDL_GPURenderPass* scenePass,
                                [[maybe_unused]] float interpolationAlpha) {
  if (m_drawCommands.empty()) {
    return;
  }

  const auto* sceneTexture = gpuRenderer.getSceneTexture();
  if (!sceneTexture) {
    return;
  }

  // Create orthographic projection for scene texture (3x viewport size)
  float orthoMatrix[16];
  HammerEngine::GPURenderer::createOrthoMatrix(
      0.0f, static_cast<float>(sceneTexture->getWidth()),
      static_cast<float>(sceneTexture->getHeight()), 0.0f,
      orthoMatrix);

  // Push view-projection matrix
  gpuRenderer.pushViewProjection(scenePass, orthoMatrix);

  // Bind pipeline once
  SDL_BindGPUGraphicsPipeline(scenePass, gpuRenderer.getSpriteAlphaPipeline());

  // Bind vertex buffer once
  SDL_GPUBufferBinding vertexBinding{};
  vertexBinding.buffer = gpuRenderer.getSpriteVertexPool().getGPUBuffer();
  vertexBinding.offset = 0;
  SDL_BindGPUVertexBuffers(scenePass, 0, &vertexBinding, 1);

  // Bind index buffer once
  const auto& batch = gpuRenderer.getSpriteBatch();
  SDL_GPUBufferBinding indexBinding{};
  indexBinding.buffer = batch.getIndexBuffer();
  indexBinding.offset = 0;
  SDL_BindGPUIndexBuffer(scenePass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

  // Draw each texture with its vertices
  for (const auto& cmd : m_drawCommands) {
    // Bind texture
    SDL_GPUTextureSamplerBinding texSampler{};
    texSampler.texture = cmd.texture;
    texSampler.sampler = gpuRenderer.getNearestSampler();
    SDL_BindGPUFragmentSamplers(scenePass, 0, &texSampler, 1);

    // Draw this sprite (6 indices per quad)
    // firstIndex = (vertexOffset / 4) * 6 because index buffer has 6 indices per 4 vertices
    uint32_t firstIndex = (cmd.vertexOffset / 4) * 6;
    SDL_DrawGPUIndexedPrimitives(scenePass, 6, 1, firstIndex, 0, 0);
  }
}

void LogoState::renderGPUUI(HammerEngine::GPURenderer& gpuRenderer,
                             SDL_GPURenderPass* swapchainPass) {
  if (!swapchainPass || m_textDrawCommands.empty()) {
    return;
  }

  // Create orthographic projection for screen-space rendering
  float orthoMatrix[16];
  HammerEngine::GPURenderer::createOrthoMatrix(
      0.0f, static_cast<float>(gpuRenderer.getViewportWidth()),
      static_cast<float>(gpuRenderer.getViewportHeight()), 0.0f,
      orthoMatrix);

  // Bind UI sprite pipeline
  SDL_BindGPUGraphicsPipeline(swapchainPass, gpuRenderer.getUISpritePipeline());

  // Push view-projection matrix
  gpuRenderer.pushViewProjection(swapchainPass, orthoMatrix);

  // Bind vertex buffer
  SDL_GPUBufferBinding vertexBinding{};
  vertexBinding.buffer = gpuRenderer.getUIVertexPool().getGPUBuffer();
  vertexBinding.offset = 0;
  SDL_BindGPUVertexBuffers(swapchainPass, 0, &vertexBinding, 1);

  // Bind index buffer
  const auto& batch = gpuRenderer.getSpriteBatch();
  SDL_GPUBufferBinding indexBinding{};
  indexBinding.buffer = batch.getIndexBuffer();
  indexBinding.offset = 0;
  SDL_BindGPUIndexBuffer(swapchainPass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

  // Draw each text texture
  for (const auto& cmd : m_textDrawCommands) {
    SDL_GPUTextureSamplerBinding texSampler{};
    texSampler.texture = cmd.texture;
    texSampler.sampler = gpuRenderer.getLinearSampler();
    SDL_BindGPUFragmentSamplers(swapchainPass, 0, &texSampler, 1);

    uint32_t firstIndex = (cmd.vertexOffset / 4) * 6;
    SDL_DrawGPUIndexedPrimitives(swapchainPass, 6, 1, firstIndex, 0, 0);
  }
}
#endif // USE_SDL3_GPU
