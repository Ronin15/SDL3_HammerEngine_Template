/* Copyright (c) 2025 Hammer Forged Games
 * Licensed under the MIT License */

/**
 * Unit tests for GPU type layouts.
 * These tests validate vertex and UBO struct layouts without requiring GPU.
 */

#define BOOST_TEST_MODULE GPUTypesTests
#include <boost/test/unit_test.hpp>

#include "gpu/GPUTypes.hpp"
#include <cstddef>

using namespace HammerEngine;

// ============================================================================
// SPRITE VERTEX TESTS
// Validates SpriteVertex layout for GPU shader compatibility
// ============================================================================

BOOST_AUTO_TEST_SUITE(SpriteVertexTests)

BOOST_AUTO_TEST_CASE(SpriteVertexSize) {
    // SpriteVertex must be exactly 20 bytes for GPU vertex buffer layout
    // Layout: x,y (8) + u,v (8) + rgba (4) = 20 bytes
    BOOST_CHECK_EQUAL(sizeof(SpriteVertex), 20u);
}

BOOST_AUTO_TEST_CASE(SpriteVertexPositionOffset) {
    // Position (x,y) must be at offset 0
    BOOST_CHECK_EQUAL(offsetof(SpriteVertex, x), 0u);
    BOOST_CHECK_EQUAL(offsetof(SpriteVertex, y), sizeof(float));
}

BOOST_AUTO_TEST_CASE(SpriteVertexTexCoordOffset) {
    // Texture coordinates (u,v) must be at offset 8
    BOOST_CHECK_EQUAL(offsetof(SpriteVertex, u), 8u);
    BOOST_CHECK_EQUAL(offsetof(SpriteVertex, v), 12u);
}

BOOST_AUTO_TEST_CASE(SpriteVertexColorOffset) {
    // Color (r,g,b,a) must be at offset 16
    BOOST_CHECK_EQUAL(offsetof(SpriteVertex, r), 16u);
    BOOST_CHECK_EQUAL(offsetof(SpriteVertex, g), 17u);
    BOOST_CHECK_EQUAL(offsetof(SpriteVertex, b), 18u);
    BOOST_CHECK_EQUAL(offsetof(SpriteVertex, a), 19u);
}

BOOST_AUTO_TEST_CASE(SpriteVertexTriviallyCopiable) {
    // Must be trivially copyable for GPU buffer uploads
    BOOST_CHECK(std::is_trivially_copyable_v<SpriteVertex>);
}

BOOST_AUTO_TEST_CASE(SpriteVertexStandardLayout) {
    // Standard layout ensures predictable memory representation
    BOOST_CHECK(std::is_standard_layout_v<SpriteVertex>);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// COLOR VERTEX TESTS
// Validates ColorVertex layout for primitive/particle rendering
// ============================================================================

BOOST_AUTO_TEST_SUITE(ColorVertexTests)

BOOST_AUTO_TEST_CASE(ColorVertexSize) {
    // ColorVertex must be exactly 12 bytes
    // Layout: x,y (8) + rgba (4) = 12 bytes
    BOOST_CHECK_EQUAL(sizeof(ColorVertex), 12u);
}

BOOST_AUTO_TEST_CASE(ColorVertexPositionOffset) {
    // Position (x,y) must be at offset 0
    BOOST_CHECK_EQUAL(offsetof(ColorVertex, x), 0u);
    BOOST_CHECK_EQUAL(offsetof(ColorVertex, y), sizeof(float));
}

BOOST_AUTO_TEST_CASE(ColorVertexColorOffset) {
    // Color (r,g,b,a) must be at offset 8
    BOOST_CHECK_EQUAL(offsetof(ColorVertex, r), 8u);
    BOOST_CHECK_EQUAL(offsetof(ColorVertex, g), 9u);
    BOOST_CHECK_EQUAL(offsetof(ColorVertex, b), 10u);
    BOOST_CHECK_EQUAL(offsetof(ColorVertex, a), 11u);
}

BOOST_AUTO_TEST_CASE(ColorVertexTriviallyCopiable) {
    BOOST_CHECK(std::is_trivially_copyable_v<ColorVertex>);
}

BOOST_AUTO_TEST_CASE(ColorVertexStandardLayout) {
    BOOST_CHECK(std::is_standard_layout_v<ColorVertex>);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// UBO LAYOUT TESTS
// Validates uniform buffer layouts match std140/std430 shader expectations
// ============================================================================

BOOST_AUTO_TEST_SUITE(UBOLayoutTests)

BOOST_AUTO_TEST_CASE(ViewProjectionUBOSize) {
    // ViewProjectionUBO contains a 4x4 matrix (16 floats = 64 bytes)
    BOOST_CHECK_EQUAL(sizeof(ViewProjectionUBO), 64u);
}

BOOST_AUTO_TEST_CASE(ViewProjectionUBOArrayOffset) {
    // Matrix array must be at offset 0
    BOOST_CHECK_EQUAL(offsetof(ViewProjectionUBO, viewProjection), 0u);
}

BOOST_AUTO_TEST_CASE(CompositeUBOSize) {
    // CompositeUBO must be exactly 32 bytes for std140 alignment
    BOOST_CHECK_EQUAL(sizeof(CompositeUBO), 32u);
}

BOOST_AUTO_TEST_CASE(CompositeUBOSubPixelOffsets) {
    // Sub-pixel offsets at start
    BOOST_CHECK_EQUAL(offsetof(CompositeUBO, subPixelOffsetX), 0u);
    BOOST_CHECK_EQUAL(offsetof(CompositeUBO, subPixelOffsetY), 4u);
}

BOOST_AUTO_TEST_CASE(CompositeUBOZoomOffset) {
    // Zoom must be at offset 8 (after subpixel X and Y)
    BOOST_CHECK_EQUAL(offsetof(CompositeUBO, zoom), 8u);
}

BOOST_AUTO_TEST_CASE(CompositeUBOPaddingOffset) {
    // Padding at offset 12 for std140 alignment
    BOOST_CHECK_EQUAL(offsetof(CompositeUBO, _pad0), 12u);
}

BOOST_AUTO_TEST_CASE(CompositeUBOAmbientOffset) {
    // Ambient lighting must be at offset 16 (vec4 aligned)
    BOOST_CHECK_EQUAL(offsetof(CompositeUBO, ambientR), 16u);
    BOOST_CHECK_EQUAL(offsetof(CompositeUBO, ambientG), 20u);
    BOOST_CHECK_EQUAL(offsetof(CompositeUBO, ambientB), 24u);
    BOOST_CHECK_EQUAL(offsetof(CompositeUBO, ambientAlpha), 28u);
}

BOOST_AUTO_TEST_CASE(CompositeUBOTriviallyCopiable) {
    BOOST_CHECK(std::is_trivially_copyable_v<CompositeUBO>);
}

BOOST_AUTO_TEST_CASE(ViewProjectionUBOTriviallyCopiable) {
    BOOST_CHECK(std::is_trivially_copyable_v<ViewProjectionUBO>);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// VERTEX DATA TESTS
// Validates vertex data can be correctly initialized
// ============================================================================

BOOST_AUTO_TEST_SUITE(VertexDataTests)

BOOST_AUTO_TEST_CASE(SpriteVertexInitialization) {
    SpriteVertex v{};
    v.x = 100.0f;
    v.y = 200.0f;
    v.u = 0.5f;
    v.v = 0.75f;
    v.r = 255;
    v.g = 128;
    v.b = 64;
    v.a = 200;

    BOOST_CHECK_EQUAL(v.x, 100.0f);
    BOOST_CHECK_EQUAL(v.y, 200.0f);
    BOOST_CHECK_EQUAL(v.u, 0.5f);
    BOOST_CHECK_EQUAL(v.v, 0.75f);
    BOOST_CHECK_EQUAL(v.r, 255);
    BOOST_CHECK_EQUAL(v.g, 128);
    BOOST_CHECK_EQUAL(v.b, 64);
    BOOST_CHECK_EQUAL(v.a, 200);
}

BOOST_AUTO_TEST_CASE(ColorVertexInitialization) {
    ColorVertex v{};
    v.x = 50.0f;
    v.y = 75.0f;
    v.r = 0;
    v.g = 255;
    v.b = 128;
    v.a = 255;

    BOOST_CHECK_EQUAL(v.x, 50.0f);
    BOOST_CHECK_EQUAL(v.y, 75.0f);
    BOOST_CHECK_EQUAL(v.r, 0);
    BOOST_CHECK_EQUAL(v.g, 255);
    BOOST_CHECK_EQUAL(v.b, 128);
    BOOST_CHECK_EQUAL(v.a, 255);
}

BOOST_AUTO_TEST_CASE(CompositeUBOInitialization) {
    CompositeUBO ubo{};
    ubo.subPixelOffsetX = 0.25f;
    ubo.subPixelOffsetY = 0.5f;
    ubo.zoom = 2.0f;
    ubo.ambientR = 1.0f;
    ubo.ambientG = 0.9f;
    ubo.ambientB = 0.8f;
    ubo.ambientAlpha = 0.5f;

    BOOST_CHECK_EQUAL(ubo.subPixelOffsetX, 0.25f);
    BOOST_CHECK_EQUAL(ubo.subPixelOffsetY, 0.5f);
    BOOST_CHECK_EQUAL(ubo.zoom, 2.0f);
    BOOST_CHECK_EQUAL(ubo.ambientR, 1.0f);
    BOOST_CHECK_EQUAL(ubo.ambientG, 0.9f);
    BOOST_CHECK_EQUAL(ubo.ambientB, 0.8f);
    BOOST_CHECK_EQUAL(ubo.ambientAlpha, 0.5f);
}

BOOST_AUTO_TEST_SUITE_END()
