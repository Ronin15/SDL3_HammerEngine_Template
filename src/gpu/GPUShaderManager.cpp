/* Copyright (c) 2025 Hammer Forged Games
 * Licensed under the MIT License */

#include "gpu/GPUShaderManager.hpp"
#include "core/Logger.hpp"
#include "utils/ResourcePath.hpp"
#include <format>
#include <fstream>
#include <vector>

namespace HammerEngine {

GPUShaderManager& GPUShaderManager::Instance() {
    static GPUShaderManager instance;
    return instance;
}

bool GPUShaderManager::init(SDL_GPUDevice* device) {
    if (!device) {
        GAMEENGINE_ERROR("GPUShaderManager::init: null device");
        return false;
    }

    m_device = device;

    // Determine shader format based on backend
    SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);

    if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
        m_useSPIRV = true;
        GAMEENGINE_INFO("GPUShaderManager: using SPIR-V shaders");
    } else if (formats & SDL_GPU_SHADERFORMAT_MSL) {
        m_useSPIRV = false;
        GAMEENGINE_INFO("GPUShaderManager: using MSL shaders");
    } else {
        GAMEENGINE_ERROR("GPUShaderManager: no supported shader format");
        return false;
    }

    return true;
}

void GPUShaderManager::shutdown() {
    size_t count = m_shaders.size();
    for (auto& [name, shader] : m_shaders) {
        if (shader && m_device) {
            SDL_ReleaseGPUShader(m_device, shader);
        }
    }
    m_shaders.clear();
    m_device = nullptr;

    GAMEENGINE_INFO(std::format("GPUShaderManager shutdown: released {} shaders", count));
}

SDL_GPUShader* GPUShaderManager::loadShader(const std::string& basePath,
                                             SDL_GPUShaderStage stage,
                                             const ShaderInfo& info) {
    if (!m_device) {
        GAMEENGINE_ERROR("GPUShaderManager::loadShader: not initialized");
        return nullptr;
    }

    // Check cache using base path as key
    auto it = m_shaders.find(basePath);
    if (it != m_shaders.end()) {
        return it->second;
    }

    SDL_GPUShader* shader = nullptr;

    // Build full path with extension, then resolve for bundle compatibility
    // ResourcePath::resolve() checks if file exists, so we must include the extension
    if (m_useSPIRV) {
        std::string path = ResourcePath::resolve(basePath + ".spv");
        shader = loadSPIRV(path, stage, info);
    } else {
        std::string path = ResourcePath::resolve(basePath + ".metal");
        std::string entryPoint = (stage == SDL_GPU_SHADERSTAGE_VERTEX) ? "vertexMain" : "fragmentMain";
        shader = loadMSL(path, stage, info, entryPoint);
    }

    if (shader) {
        m_shaders[basePath] = shader;
        GAMEENGINE_DEBUG(std::format("Loaded shader: {}", basePath));
    }

    return shader;
}

SDL_GPUShader* GPUShaderManager::getShader(const std::string& name) const {
    auto it = m_shaders.find(name);
    return (it != m_shaders.end()) ? it->second : nullptr;
}

bool GPUShaderManager::hasShader(const std::string& name) const {
    return m_shaders.find(name) != m_shaders.end();
}

SDL_GPUShader* GPUShaderManager::loadSPIRV(const std::string& path,
                                           SDL_GPUShaderStage stage,
                                           const ShaderInfo& info) {
    // Read binary file
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        GAMEENGINE_ERROR(std::format("Failed to open shader file: {}", path));
        return nullptr;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        GAMEENGINE_ERROR(std::format("Failed to read shader file: {}", path));
        return nullptr;
    }

    SDL_GPUShaderCreateInfo createInfo{};
    createInfo.code = buffer.data();
    createInfo.code_size = buffer.size();
    createInfo.entrypoint = "main";
    createInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    createInfo.stage = stage;
    createInfo.num_samplers = info.numSamplers;
    createInfo.num_storage_textures = info.numStorageTextures;
    createInfo.num_storage_buffers = info.numStorageBuffers;
    createInfo.num_uniform_buffers = info.numUniformBuffers;

    SDL_GPUShader* shader = SDL_CreateGPUShader(m_device, &createInfo);

    if (!shader) {
        GAMEENGINE_ERROR(std::format("Failed to create SPIR-V shader {}: {}", path, SDL_GetError()));
    }

    return shader;
}

SDL_GPUShader* GPUShaderManager::loadMSL(const std::string& path,
                                          SDL_GPUShaderStage stage,
                                          const ShaderInfo& info,
                                          const std::string& entryPoint) {
    // Read text file
    std::ifstream file(path);
    if (!file.is_open()) {
        GAMEENGINE_ERROR(std::format("Failed to open shader file: {}", path));
        return nullptr;
    }

    std::string source((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    SDL_GPUShaderCreateInfo createInfo{};
    createInfo.code = reinterpret_cast<const uint8_t*>(source.c_str());
    createInfo.code_size = source.size();
    createInfo.entrypoint = entryPoint.c_str();
    createInfo.format = SDL_GPU_SHADERFORMAT_MSL;
    createInfo.stage = stage;
    createInfo.num_samplers = info.numSamplers;
    createInfo.num_storage_textures = info.numStorageTextures;
    createInfo.num_storage_buffers = info.numStorageBuffers;
    createInfo.num_uniform_buffers = info.numUniformBuffers;

    SDL_GPUShader* shader = SDL_CreateGPUShader(m_device, &createInfo);

    if (!shader) {
        GAMEENGINE_ERROR(std::format("Failed to create MSL shader {}: {}", path, SDL_GetError()));
    }

    return shader;
}

} // namespace HammerEngine
