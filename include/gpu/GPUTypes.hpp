/* Copyright (c) 2025 Hammer Forged Games
 * Licensed under the MIT License */

#ifndef GPU_TYPES_HPP
#define GPU_TYPES_HPP

#include <cstdint>

namespace HammerEngine {

/**
 * Vertex format for textured sprites.
 */
struct SpriteVertex {
    float x, y;           // Position (8 bytes)
    float u, v;           // Texture coords (8 bytes)
    uint8_t r, g, b, a;   // Color packed (4 bytes)
    // Total: 20 bytes per vertex
};

static_assert(sizeof(SpriteVertex) == 20, "SpriteVertex must be 20 bytes");

/**
 * Vertex format for colored primitives and particles.
 */
struct ColorVertex {
    float x, y;           // Position (8 bytes)
    uint8_t r, g, b, a;   // Color packed (4 bytes)
    // Total: 12 bytes per vertex
};

static_assert(sizeof(ColorVertex) == 12, "ColorVertex must be 12 bytes");

/**
 * View-projection uniform buffer data.
 */
struct ViewProjectionUBO {
    float viewProjection[16];  // 4x4 matrix
};

/**
 * Composite uniform buffer data.
 */
struct CompositeUBO {
    float subPixelOffsetX;
    float subPixelOffsetY;
    float zoom;
    float padding;
};

} // namespace HammerEngine

#endif // GPU_TYPES_HPP
