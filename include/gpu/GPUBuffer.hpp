/* Copyright (c) 2025 Hammer Forged Games
 * Licensed under the MIT License */

#ifndef GPU_BUFFER_HPP
#define GPU_BUFFER_HPP

#include <SDL3/SDL_gpu.h>
#include <cstdint>

namespace HammerEngine {

/**
 * RAII wrapper for SDL_GPUBuffer.
 *
 * Used for vertex buffers, index buffers, and uniform buffers on the GPU.
 * Data must be uploaded via transfer buffers and copy passes.
 */
class GPUBuffer {
public:
    GPUBuffer() = default;

    /**
     * Create a GPU buffer with specified usage and size.
     * @param device GPU device
     * @param usage Buffer usage flags (VERTEX, INDEX, etc.)
     * @param size Buffer size in bytes
     */
    GPUBuffer(SDL_GPUDevice* device, SDL_GPUBufferUsageFlags usage, uint32_t size);

    ~GPUBuffer();

    // Move-only
    GPUBuffer(GPUBuffer&& other) noexcept;
    GPUBuffer& operator=(GPUBuffer&& other) noexcept;
    GPUBuffer(const GPUBuffer&) = delete;
    GPUBuffer& operator=(const GPUBuffer&) = delete;

    SDL_GPUBuffer* get() const { return m_buffer; }
    uint32_t getSize() const { return m_size; }
    SDL_GPUBufferUsageFlags getUsage() const { return m_usage; }
    bool isValid() const { return m_buffer != nullptr; }

    /**
     * Create a buffer binding for use in draw calls.
     * @param offset Byte offset into the buffer
     */
    SDL_GPUBufferBinding asBinding(uint32_t offset = 0) const;

    /**
     * Create a buffer region for copy operations.
     * @param offset Byte offset into the buffer
     * @param size Size of the region (0 = entire buffer from offset)
     */
    SDL_GPUBufferRegion asRegion(uint32_t offset = 0, uint32_t size = 0) const;

private:
    void release();

    SDL_GPUBuffer* m_buffer{nullptr};
    SDL_GPUDevice* m_device{nullptr};
    uint32_t m_size{0};
    SDL_GPUBufferUsageFlags m_usage{0};
};

} // namespace HammerEngine

#endif // GPU_BUFFER_HPP
