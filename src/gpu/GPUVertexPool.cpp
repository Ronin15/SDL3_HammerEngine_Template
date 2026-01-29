/* Copyright (c) 2025 Hammer Forged Games
 * Licensed under the MIT License */

#include "gpu/GPUVertexPool.hpp"
#include "core/Logger.hpp"
#include <algorithm>
#include <format>

namespace HammerEngine {

bool GPUVertexPool::init(SDL_GPUDevice* device, uint32_t vertexSize, size_t maxVertices) {
    if (!device) {
        GAMEENGINE_ERROR("GPUVertexPool::init: null device");
        return false;
    }

    if (vertexSize == 0 || maxVertices == 0) {
        GAMEENGINE_ERROR(std::format("GPUVertexPool::init: invalid parameters (vertexSize={}, maxVertices={})",
                         vertexSize, maxVertices));
        return false;
    }

    m_device = device;
    m_vertexSize = vertexSize;
    m_maxVertices = maxVertices;

    uint32_t bufferSize = vertexSize * static_cast<uint32_t>(maxVertices);

    // Create triple-buffered transfer buffers
    for (size_t i = 0; i < FRAME_COUNT; ++i) {
        m_transferBuffers[i] = GPUTransferBuffer(device,
            SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, bufferSize);

        if (!m_transferBuffers[i].isValid()) {
            GAMEENGINE_ERROR(std::format("GPUVertexPool: failed to create transfer buffer {}", i));
            shutdown();
            return false;
        }
    }

    // Create persistent GPU buffer
    m_gpuBuffer = GPUBuffer(device, SDL_GPU_BUFFERUSAGE_VERTEX, bufferSize);

    if (!m_gpuBuffer.isValid()) {
        GAMEENGINE_ERROR("GPUVertexPool: failed to create GPU vertex buffer");
        shutdown();
        return false;
    }

    GAMEENGINE_INFO(std::format("GPUVertexPool initialized: {} vertices x {} bytes = {} KB",
                    maxVertices, vertexSize, bufferSize / 1024));
    return true;
}

void GPUVertexPool::shutdown() {
    m_gpuBuffer = GPUBuffer();

    std::generate(m_transferBuffers.begin(), m_transferBuffers.end(),
                  []() { return GPUTransferBuffer(); });

    m_device = nullptr;
    m_frameIndex = 0;
    m_currentVertexCount = 0;
    m_mappedPtr = nullptr;
}

void* GPUVertexPool::beginFrame() {
    if (!m_device) {
        GAMEENGINE_ERROR("GPUVertexPool::beginFrame: not initialized");
        return nullptr;
    }

    // Advance to next frame's transfer buffer
    m_frameIndex = (m_frameIndex + 1) % FRAME_COUNT;
    m_currentVertexCount = 0;
    m_pendingVertexCount = 0;

    // Map with cycle=true to handle if previous frame's upload is still in flight
    m_mappedPtr = m_transferBuffers[m_frameIndex].map(true);

    if (!m_mappedPtr) {
        GAMEENGINE_ERROR("GPUVertexPool::beginFrame: failed to map transfer buffer");
    }

    return m_mappedPtr;
}

void GPUVertexPool::endFrame(size_t vertexCount) {
    if (!m_device) {
        return;
    }

    if (vertexCount > m_maxVertices) {
        GAMEENGINE_WARN(std::format("GPUVertexPool::endFrame: vertex count {} exceeds max {}, clamping",
                        vertexCount, m_maxVertices));
        vertexCount = m_maxVertices;
    }

    m_currentVertexCount = vertexCount;
    m_transferBuffers[m_frameIndex].unmap();
    m_mappedPtr = nullptr;
}

void GPUVertexPool::upload(SDL_GPUCopyPass* copyPass) {
    if (!copyPass || m_currentVertexCount == 0) {
        return;
    }

    SDL_GPUTransferBufferLocation src = m_transferBuffers[m_frameIndex].asLocation(0);

    SDL_GPUBufferRegion dst{};
    dst.buffer = m_gpuBuffer.get();
    dst.offset = 0;
    dst.size = static_cast<uint32_t>(m_currentVertexCount * m_vertexSize);

    SDL_UploadToGPUBuffer(copyPass, &src, &dst, false);
}

} // namespace HammerEngine
