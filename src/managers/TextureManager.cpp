/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/TextureManager.hpp"
#include "core/Logger.hpp"
#include <algorithm>
#include <filesystem>
#include <format>
#include <cstring>

#ifdef USE_SDL3_GPU
#include "gpu/GPUDevice.hpp"
#include "gpu/GPUTexture.hpp"
#include "gpu/GPUTransferBuffer.hpp"
#endif



bool TextureManager::load(const std::string& fileName,
                          const std::string& textureID,
                          SDL_Renderer* p_renderer) {
  if (m_texturesLoaded.load(std::memory_order_acquire)) {
    return true;
  }

  std::lock_guard<std::mutex> lock(m_textureLoadMutex);
  if (m_texturesLoaded.load(std::memory_order_acquire)) {
    return true;
  }

  // Check if the fileName is a directory
  if (std::filesystem::exists(fileName) && std::filesystem::is_directory(fileName)) {
    TEXTURE_INFO(std::format("Loading textures from directory: {}", fileName));

    bool loadedAny = false;
    int texturesLoaded{0};

    try {
      // Iterate through all files in the directory
      for (const auto& entry : std::filesystem::recursive_directory_iterator(fileName)) {
        if (!entry.is_regular_file()) {
          continue; // Skip directories and special files
        }

        // Get file path and extension
        const auto& filePath = entry.path();
        std::string extension = filePath.extension().string();

        // Convert extension to lowercase for case-insensitive comparison
        std::transform(extension.begin(), extension.end(), extension.begin(),
                      [](unsigned char c) { return std::tolower(c); });

        // Check if the file is a PNG
        if (extension == ".png") {
          std::string fullPath = filePath.string();
          std::string filename = filePath.stem().string(); // Get filename without extension

          // Create texture ID by combining the provided prefix and filename
          std::string combinedID = textureID.empty() ? filename : std::format("{}_{}", textureID, filename);

          // Load the individual file as a texture with immediate RAII
          auto surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(
              SDL_LoadPNG(fullPath.c_str()), SDL_DestroySurface);

          TEXTURE_INFO(std::format("Loading texture: {}", fullPath));

          if (!surface) {
            TEXTURE_ERROR(std::format("Could not load image: {}", SDL_GetError()));
            continue;
          }

          auto texture = std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>(
              SDL_CreateTextureFromSurface(p_renderer, surface.get()), SDL_DestroyTexture);

          if (texture) {
            // Fix for tile rendering artifacts on macOS (Metal backend) by forcing nearest-pixel sampling.
            SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_NEAREST);

            // Cache texture dimensions at load time to avoid per-frame SDL_GetTextureSize() calls
            float texWidth{0.0f}, texHeight{0.0f};
            SDL_GetTextureSize(texture.get(), &texWidth, &texHeight);

            TextureData data;
            data.texture = std::shared_ptr<SDL_Texture>(texture.release(), SDL_DestroyTexture);
            data.width = texWidth;
            data.height = texHeight;
            m_textureMap[combinedID] = std::move(data);

            loadedAny = true;
            texturesLoaded++;
          } else {
            TEXTURE_ERROR(std::format("Could not create texture: {}", SDL_GetError()));
          }
        }
      }
    } catch (const std::filesystem::filesystem_error& e) {
      TEXTURE_ERROR(std::format("Filesystem error: {}", e.what()));
    } catch (const std::exception& e) {
      TEXTURE_ERROR(std::format("Error while loading textures: {}", e.what()));
    }

    TEXTURE_INFO(std::format("Loaded {} textures from directory: {}", texturesLoaded, fileName));

    // Suppress unused variable warning in release builds
    (void)texturesLoaded;
    if(loadedAny) m_texturesLoaded.store(true, std::memory_order_release);
    return loadedAny; // Return true if at least one texture was loaded successfully
  }

  // Standard single file loading with immediate RAII
  auto surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(
      SDL_LoadPNG(fileName.c_str()), SDL_DestroySurface);

  TEXTURE_INFO(std::format("Loaded texture: {}", textureID));

  if (!surface) {
    TEXTURE_ERROR(std::format("Could not load image: {}", SDL_GetError()));
      return false;
  }

  auto texture = std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>(
      SDL_CreateTextureFromSurface(p_renderer, surface.get()), SDL_DestroyTexture);

  if (texture) {
    // Fix for tile rendering artifacts on macOS (Metal backend) by forcing nearest-pixel sampling.
    SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_NEAREST);

    // Cache texture dimensions at load time to avoid per-frame SDL_GetTextureSize() calls
    float texWidth{0.0f}, texHeight{0.0f};
    SDL_GetTextureSize(texture.get(), &texWidth, &texHeight);

    TextureData data;
    data.texture = std::shared_ptr<SDL_Texture>(texture.release(), SDL_DestroyTexture);
    data.width = texWidth;
    data.height = texHeight;
    m_textureMap[textureID] = std::move(data);

    m_texturesLoaded.store(true, std::memory_order_release);
    return true;
  }

  TEXTURE_ERROR(std::format("Could not create texture: {}", SDL_GetError()));

  return false;
}

void TextureManager::draw(const std::string& textureID,
                          int x,
                          int y,
                          int width,
                          int height,
                          SDL_Renderer* p_renderer,
                          SDL_FlipMode flip) {
  // Single map lookup - use cached dimensions instead of SDL_GetTextureSize()
  auto it = m_textureMap.find(textureID);
  if (it == m_textureMap.end()) {
    TEXTURE_ERROR(std::format("Texture not found: '{}'", textureID));
    return;
  }
  const TextureData& data = it->second;

  SDL_FRect srcRect;
  SDL_FRect destRect;
  SDL_FPoint center = {static_cast<float>(width) / 2.0f, static_cast<float>(height) / 2.0f};
  double angle = 0.0;

  // Inset source rectangle by a small amount to prevent texture bleeding
  // Use cached texture dimensions, not destination dimensions
  srcRect.x = 0.1f;
  srcRect.y = 0.1f;
  srcRect.w = data.width - 0.2f;
  srcRect.h = data.height - 0.2f;

  // Destination rectangle uses requested width/height for scaling
  destRect.w = static_cast<float>(width);
  destRect.h = static_cast<float>(height);
  destRect.x = static_cast<float>(x);
  destRect.y = static_cast<float>(y);

  SDL_RenderTextureRotated(p_renderer, data.texture.get(), &srcRect, &destRect, angle, &center, flip);
}

void TextureManager::drawTileF(const std::string& textureID,
                               float x,
                               float y,
                               int width,
                               int height,
                               SDL_Renderer* p_renderer,
                               SDL_FlipMode flip) {
  // Single map lookup
  auto it = m_textureMap.find(textureID);
  if (it == m_textureMap.end()) {
    return;  // Silent fail for tiles - avoid log spam
  }

  SDL_FRect srcRect;
  SDL_FRect destRect;
  SDL_FPoint center = {static_cast<float>(width) / 2.0f, static_cast<float>(height) / 2.0f};
  double angle = 0.0;

  // Perfect pixel source rectangle - no inset for seamless tiling
  srcRect.x = 0.0f;
  srcRect.y = 0.0f;
  srcRect.w = static_cast<float>(width);
  srcRect.h = static_cast<float>(height);

  destRect.w = static_cast<float>(width);
  destRect.h = static_cast<float>(height);
  destRect.x = x;  // Use sub-pixel precision - SDL3/GPU handles smooth rendering
  destRect.y = y;  // Use sub-pixel precision - SDL3/GPU handles smooth rendering

  SDL_RenderTextureRotated(p_renderer, it->second.texture.get(), &srcRect, &destRect, angle, &center, flip);
}

void TextureManager::drawTileDirect(SDL_Texture* texture,
                                    float x,
                                    float y,
                                    int width,
                                    int height,
                                    SDL_Renderer* p_renderer) {
  if (!texture) {
    return;  // Silent fail - caller should ensure valid texture
  }

  // Destination rectangle with requested dimensions
  SDL_FRect destRect = {x, y, static_cast<float>(width), static_cast<float>(height)};

  // Use nullptr for srcRect to sample entire texture - handles any texture size correctly
  // SDL will scale the entire texture to fit destRect dimensions
  SDL_RenderTexture(p_renderer, texture, nullptr, &destRect);
}

void TextureManager::drawFrame(const std::string& textureID,
                               float x,
                               float y,
                               int width,
                               int height,
                               int currentRow,
                               int currentFrame,
                               SDL_Renderer* p_renderer,
                               SDL_FlipMode flip) {
  // Single map lookup
  auto it = m_textureMap.find(textureID);
  if (it == m_textureMap.end()) {
    return;
  }

  SDL_FRect srcRect;
  SDL_FRect destRect;
  SDL_FPoint center = {static_cast<float>(width) / 2.0f, static_cast<float>(height) / 2.0f};
  double angle = 0.0;

  // Use exact source pixel bounds for sprite frames to avoid subpixel sampling jitter
  // Entities render at integer-aligned screen positions; exact src rects prevent hitching when camera moves
  srcRect.x = static_cast<float>(width * currentFrame);
  srcRect.y = static_cast<float>(height * (currentRow - 1));
  srcRect.w = static_cast<float>(width);
  srcRect.h = static_cast<float>(height);

  // Use float precision directly - no casting from integer
  destRect.w = static_cast<float>(width);
  destRect.h = static_cast<float>(height);
  destRect.x = x;  // Direct float assignment
  destRect.y = y;  // Direct float assignment

  SDL_RenderTextureRotated(p_renderer, it->second.texture.get(), &srcRect, &destRect, angle, &center, flip);
}

void TextureManager::drawParallax(const std::string& textureID,
                    int x,
                    int y,
                    int scroll,
                    SDL_Renderer* p_renderer) {
  // Verify the texture exists - single lookup
  auto it = m_textureMap.find(textureID);
  if (it == m_textureMap.end()) {
    TEXTURE_WARN(std::format("Texture not found: {}", textureID));
    return;
  }
  const TextureData& data = it->second;

  // Use cached dimensions instead of SDL_GetTextureSize()
  float width = data.width;
  float height = data.height;

  // Calculate scroll offset (make sure it wraps around)
  scroll = scroll % static_cast<int>(width);
  if (scroll < 0) {
    scroll += static_cast<int>(width); // Handle negative scroll values
  }

  SDL_FRect srcRect1, destRect1, srcRect2, destRect2;

  // First part of the background
  srcRect1.x = static_cast<float>(scroll);
  srcRect1.y = 0;
  srcRect1.w = width - static_cast<float>(scroll);
  srcRect1.h = height;

  destRect1.x = static_cast<float>(x);
  destRect1.y = static_cast<float>(y);
  destRect1.w = srcRect1.w;
  destRect1.h = height;

  // Second part of the background (wrapping around)
  srcRect2.x = 0;
  srcRect2.y = 0;
  srcRect2.w = static_cast<float>(scroll);
  srcRect2.h = height;

  destRect2.x = static_cast<float>(x) + srcRect1.w;
  destRect2.y = static_cast<float>(y);
  destRect2.w = srcRect2.w;
  destRect2.h = height;

  // Draw the two parts of the parallax background without rotation
  SDL_RenderTexture(p_renderer, data.texture.get(), &srcRect1, &destRect1);
  SDL_RenderTexture(p_renderer, data.texture.get(), &srcRect2, &destRect2);
}

void TextureManager::clearFromTexMap(const std::string& textureID) {
    TEXTURE_INFO(std::format("Cleared : {} texture", textureID));
  m_textureMap.erase(textureID);
}

bool TextureManager::isTextureInMap(const std::string& textureID) const {
  return m_textureMap.find(textureID) != m_textureMap.end();
}

std::shared_ptr<SDL_Texture> TextureManager::getTexture(const std::string& textureID) const {
  // Check if the texture exists in the map
  auto it = m_textureMap.find(textureID);
  if (it != m_textureMap.end()) {
    return it->second.texture;
  }

  // Return nullptr if the texture is not found
  return nullptr;
}

SDL_Texture* TextureManager::getTexturePtr(const std::string& textureID) const {
  auto it = m_textureMap.find(textureID);
  return (it != m_textureMap.end()) ? it->second.texture.get() : nullptr;
}

std::shared_ptr<SDL_Texture> TextureManager::getOrCreateDynamicTexture(const std::string& textureID,
                                                                       int width, int height,
                                                                       SDL_Renderer* p_renderer,
                                                                       bool forceRecreate) {
  if (m_isShutdown || !p_renderer) {
    return nullptr;
  }

  // Check if texture already exists in cache
  auto it = m_textureMap.find(textureID);
  if (it != m_textureMap.end() && !forceRecreate) {
    return it->second.texture;
  }

  // Remove old texture if recreating
  if (forceRecreate && it != m_textureMap.end()) {
    m_textureMap.erase(it);
  }

  // Create new dynamic texture
  SDL_Texture* rawTexture = SDL_CreateTexture(p_renderer, SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_TARGET, width, height);
  if (!rawTexture) {
    TEXTURE_ERROR(std::format("Failed to create dynamic texture: {}", textureID));
    return nullptr;
  }

  // Wrap in shared_ptr and add to cache with dimensions
  TextureData data;
  data.texture = std::shared_ptr<SDL_Texture>(rawTexture, SDL_DestroyTexture);
  data.width = static_cast<float>(width);
  data.height = static_cast<float>(height);
  m_textureMap[textureID] = std::move(data);

  return m_textureMap[textureID].texture;
}

void TextureManager::clean() {
  if (m_isShutdown) {
    return;
  }

#ifdef USE_SDL3_GPU
  // Clean up GPU textures
  {
    std::lock_guard<std::mutex> lock(m_gpuTextureMutex);
    m_gpuTextureMap.clear();
    m_pendingUploads.clear();
  }
#endif

  // Clear all textures before SDL shutdown
  m_textureMap.clear();
  m_isShutdown = true;
}

#ifdef USE_SDL3_GPU
bool TextureManager::loadGPU(const std::string& fileName, const std::string& textureID) {
  std::lock_guard<std::mutex> lock(m_gpuTextureMutex);

  // Check if already loaded
  if (m_gpuTextureMap.find(textureID) != m_gpuTextureMap.end()) {
    TEXTURE_INFO(std::format("GPU texture already loaded: {}", textureID));
    return true;
  }

  // Check if it's a directory
  if (std::filesystem::exists(fileName) && std::filesystem::is_directory(fileName)) {
    TEXTURE_INFO(std::format("Loading GPU textures from directory: {}", fileName));

    bool loadedAny = false;
    int texturesLoaded = 0;

    try {
      for (const auto& entry : std::filesystem::recursive_directory_iterator(fileName)) {
        if (!entry.is_regular_file()) {
          continue;
        }

        const auto& filePath = entry.path();
        std::string extension = filePath.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                      [](unsigned char c) { return std::tolower(c); });

        if (extension == ".png") {
          std::string fullPath = filePath.string();
          std::string filename = filePath.stem().string();
          std::string combinedID = textureID.empty() ? filename : std::format("{}_{}", textureID, filename);

          if (loadSingleGPUTexture(fullPath, combinedID)) {
            loadedAny = true;
            texturesLoaded++;
          }
        }
      }
    } catch (const std::filesystem::filesystem_error& e) {
      TEXTURE_ERROR(std::format("GPU texture filesystem error: {}", e.what()));
    } catch (const std::exception& e) {
      TEXTURE_ERROR(std::format("GPU texture load error: {}", e.what()));
    }

    TEXTURE_INFO(std::format("Loaded {} GPU textures from directory: {}", texturesLoaded, fileName));
    return loadedAny;
  }

  // Single file loading
  return loadSingleGPUTexture(fileName, textureID);
}

bool TextureManager::loadSingleGPUTexture(const std::string& fileName, const std::string& textureID) {
  // Load PNG to surface
  SDL_Surface* rawSurface = SDL_LoadPNG(fileName.c_str());
  if (!rawSurface) {
    TEXTURE_ERROR(std::format("GPU texture: could not load image: {} - {}", fileName, SDL_GetError()));
    return false;
  }

  // Convert to ABGR8888 for GPU upload (maps to R8G8B8A8_UNORM byte order on little-endian)
  // SDL_PIXELFORMAT_ABGR8888: 32-bit with A in high bits -> memory layout R,G,B,A on LE
  SDL_Surface* convertedSurface = rawSurface;
  if (rawSurface->format != SDL_PIXELFORMAT_ABGR8888) {
    convertedSurface = SDL_ConvertSurface(rawSurface, SDL_PIXELFORMAT_ABGR8888);
    SDL_DestroySurface(rawSurface);
    if (!convertedSurface) {
      TEXTURE_ERROR(std::format("GPU texture: could not convert surface: {}", SDL_GetError()));
      return false;
    }
  }

  // Premultiply alpha to ensure transparent pixels have zeroed RGB
  // This prevents "halos" and gray backgrounds from PNG transparent areas
  SDL_PremultiplyAlpha(convertedSurface->w, convertedSurface->h,
                        SDL_PIXELFORMAT_ABGR8888, convertedSurface->pixels, convertedSurface->pitch,
                        SDL_PIXELFORMAT_ABGR8888, convertedSurface->pixels, convertedSurface->pitch,
                        false);

  auto& gpuDevice = HammerEngine::GPUDevice::Instance();
  if (!gpuDevice.isInitialized()) {
    TEXTURE_ERROR("GPU texture: GPUDevice not initialized");
    SDL_DestroySurface(convertedSurface);
    return false;
  }

  uint32_t width = static_cast<uint32_t>(convertedSurface->w);
  uint32_t height = static_cast<uint32_t>(convertedSurface->h);

  // Create GPU texture with RGBA format
  auto gpuTexture = std::make_unique<HammerEngine::GPUTexture>(
      gpuDevice.get(),
      width, height,
      SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
      SDL_GPU_TEXTUREUSAGE_SAMPLER
  );

  if (!gpuTexture->isValid()) {
    TEXTURE_ERROR(std::format("GPU texture: failed to create texture for: {}", textureID));
    SDL_DestroySurface(convertedSurface);
    return false;
  }

  // Store in map
  GPUTextureData data;
  data.texture = std::move(gpuTexture);
  data.width = static_cast<float>(width);
  data.height = static_cast<float>(height);
  m_gpuTextureMap[textureID] = std::move(data);

  // Queue surface for upload (will be processed in copy pass)
  m_pendingUploads.push_back(PendingTextureUpload{
      textureID,
      std::unique_ptr<SDL_Surface, void(*)(SDL_Surface*)>(convertedSurface, SDL_DestroySurface),
      width,
      height
  });

  TEXTURE_INFO(std::format("GPU texture queued for upload: {} ({}x{})", textureID, width, height));
  return true;
}

void TextureManager::processPendingUploads(SDL_GPUCopyPass* copyPass) {
  if (!copyPass || m_pendingUploads.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(m_gpuTextureMutex);

  auto& gpuDevice = HammerEngine::GPUDevice::Instance();
  if (!gpuDevice.isInitialized()) {
    TEXTURE_ERROR("GPU texture upload: GPUDevice not initialized");
    return;
  }

  for (auto& pending : m_pendingUploads) {
    auto it = m_gpuTextureMap.find(pending.textureID);
    if (it == m_gpuTextureMap.end()) {
      TEXTURE_WARN(std::format("GPU texture upload: texture not found: {}", pending.textureID));
      continue;
    }

    SDL_Surface* surface = pending.surface.get();
    if (!surface) {
      TEXTURE_WARN(std::format("GPU texture upload: null surface for: {}", pending.textureID));
      continue;
    }

    // Calculate data size (pitch * height)
    uint32_t dataSize = static_cast<uint32_t>(surface->pitch * surface->h);

    // Create transfer buffer for upload
    HammerEngine::GPUTransferBuffer transferBuffer(
        gpuDevice.get(),
        SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        dataSize
    );

    if (!transferBuffer.isValid()) {
      TEXTURE_ERROR(std::format("GPU texture upload: failed to create transfer buffer for: {}", pending.textureID));
      continue;
    }

    // Map and copy pixel data
    void* mapped = transferBuffer.map(false);
    if (!mapped) {
      TEXTURE_ERROR(std::format("GPU texture upload: failed to map transfer buffer for: {}", pending.textureID));
      continue;
    }

    std::memcpy(mapped, surface->pixels, dataSize);
    transferBuffer.unmap();

    // Set up transfer info
    SDL_GPUTextureTransferInfo srcInfo{};
    srcInfo.transfer_buffer = transferBuffer.get();
    srcInfo.offset = 0;
    srcInfo.pixels_per_row = pending.width;
    srcInfo.rows_per_layer = pending.height;

    SDL_GPUTextureRegion dstRegion{};
    dstRegion.texture = it->second.texture->get();
    dstRegion.x = 0;
    dstRegion.y = 0;
    dstRegion.z = 0;
    dstRegion.w = pending.width;
    dstRegion.h = pending.height;
    dstRegion.d = 1;

    // Upload to GPU
    SDL_UploadToGPUTexture(copyPass, &srcInfo, &dstRegion, false);

    TEXTURE_DEBUG(std::format("GPU texture uploaded: {} ({}x{})", pending.textureID, pending.width, pending.height));
  }

  // Clear pending uploads (surfaces will be freed by unique_ptr destructors)
  m_pendingUploads.clear();
}

HammerEngine::GPUTexture* TextureManager::getGPUTexture(const std::string& textureID) const {
  std::lock_guard<std::mutex> lock(m_gpuTextureMutex);

  auto it = m_gpuTextureMap.find(textureID);
  if (it != m_gpuTextureMap.end()) {
    return it->second.texture.get();
  }
  return nullptr;
}

const GPUTextureData* TextureManager::getGPUTextureData(const std::string& textureID) const {
  std::lock_guard<std::mutex> lock(m_gpuTextureMutex);

  auto it = m_gpuTextureMap.find(textureID);
  if (it != m_gpuTextureMap.end()) {
    return &it->second;
  }
  return nullptr;
}
#endif
