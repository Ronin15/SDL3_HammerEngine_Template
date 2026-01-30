/* Copyright (c) 2025 Hammer Forged Games
 * Licensed under the MIT License */

#ifndef GPU_SHADER_MANAGER_HPP
#define GPU_SHADER_MANAGER_HPP

#include <SDL3/SDL_gpu.h>
#include <string>
#include <unordered_map>

namespace HammerEngine {

/**
 * Shader resource information for creation.
 */
struct ShaderInfo {
    uint32_t numSamplers{0};
    uint32_t numStorageTextures{0};
    uint32_t numStorageBuffers{0};
    uint32_t numUniformBuffers{0};
};

/**
 * Singleton manager for GPU shaders.
 *
 * Handles loading of SPIR-V (Vulkan) and MSL (Metal) shaders
 * based on the current GPU backend.
 */
class GPUShaderManager {
public:
    static GPUShaderManager& Instance();

    /**
     * Initialize the shader manager.
     * @param device GPU device
     * @return true on success
     */
    bool init(SDL_GPUDevice* device);

    /**
     * Shutdown and release all shaders.
     */
    void shutdown();

    /**
     * Load a shader from file.
     *
     * Automatically selects the correct format based on GPU backend:
     * - Vulkan: loads .spv file
     * - Metal: loads .metal file
     *
     * @param basePath Path without extension (e.g., "res/shaders/sprite.vert")
     * @param stage Shader stage (VERTEX or FRAGMENT)
     * @param info Resource binding information
     * @return Loaded shader, or nullptr on failure
     */
    SDL_GPUShader* loadShader(const std::string& basePath,
                              SDL_GPUShaderStage stage,
                              const ShaderInfo& info);

    /**
     * Get a previously loaded shader by name.
     * @param name Shader name (usually the basePath used during loading)
     * @return Shader pointer, or nullptr if not found
     */
    SDL_GPUShader* getShader(const std::string& name) const;

    /**
     * Check if a shader is already loaded.
     */
    bool hasShader(const std::string& name) const;

private:
    GPUShaderManager() = default;
    ~GPUShaderManager() = default;

    // Non-copyable
    GPUShaderManager(const GPUShaderManager&) = delete;
    GPUShaderManager& operator=(const GPUShaderManager&) = delete;

    /**
     * Load SPIR-V binary shader.
     */
    SDL_GPUShader* loadSPIRV(const std::string& path,
                             SDL_GPUShaderStage stage,
                             const ShaderInfo& info);

    /**
     * Load MSL (Metal Shading Language) source.
     */
    SDL_GPUShader* loadMSL(const std::string& path,
                           SDL_GPUShaderStage stage,
                           const ShaderInfo& info,
                           const std::string& entryPoint);

    SDL_GPUDevice* m_device{nullptr};
    std::unordered_map<std::string, SDL_GPUShader*> m_shaders;
    bool m_useSPIRV{true};  // Determined at init based on backend
};

} // namespace HammerEngine

#endif // GPU_SHADER_MANAGER_HPP
