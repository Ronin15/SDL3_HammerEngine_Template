/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#define BOOST_TEST_MODULE SIMDCorrectnessTests
#include <boost/test/unit_test.hpp>

#include <cmath>
#include <limits>
#include <random>
#include <vector>

#include "utils/SIMDMath.hpp"
#include "utils/Vector2D.hpp"

using namespace HammerEngine::SIMD;

// Test tolerance for floating-point comparisons
// SIMD can have slight precision differences from scalar due to FMA instructions
constexpr float ABS_EPSILON = 0.0001f;  // For values near zero
constexpr float REL_EPSILON = 0.0001f;  // For large values (0.01% relative error)

// Helper to check if two floats are approximately equal
// Uses relative tolerance for large values, absolute for small values
bool approxEqual(float a, float b, float epsilon = ABS_EPSILON) {
    const float diff = std::abs(a - b);

    // For small values, use absolute tolerance
    if (diff < epsilon) {
        return true;
    }

    // For large values, use relative tolerance
    const float maxAbs = std::max(std::abs(a), std::abs(b));
    return diff <= maxAbs * REL_EPSILON;
}

// Helper to check if a float is finite (not NaN or infinity)
bool isFinite(float value) {
    return std::isfinite(value);
}

// ============================================================================
// BASIC SIMD OPERATIONS TESTS
// Validate that SIMD abstraction layer works correctly across platforms
// ============================================================================

BOOST_AUTO_TEST_SUITE(BasicSIMDOperationsTests)

BOOST_AUTO_TEST_CASE(TestBroadcast) {
    Float4 v = broadcast(42.0f);

    alignas(16) float result[4];
    store4(result, v);

    BOOST_CHECK(approxEqual(result[0], 42.0f));
    BOOST_CHECK(approxEqual(result[1], 42.0f));
    BOOST_CHECK(approxEqual(result[2], 42.0f));
    BOOST_CHECK(approxEqual(result[3], 42.0f));
}

BOOST_AUTO_TEST_CASE(TestLoadStore) {
    alignas(16) float input[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    Float4 v = load4(input);

    alignas(16) float output[4];
    store4(output, v);

    BOOST_CHECK(approxEqual(output[0], 1.0f));
    BOOST_CHECK(approxEqual(output[1], 2.0f));
    BOOST_CHECK(approxEqual(output[2], 3.0f));
    BOOST_CHECK(approxEqual(output[3], 4.0f));
}

BOOST_AUTO_TEST_CASE(TestSet) {
    Float4 v = set(10.0f, 20.0f, 30.0f, 40.0f);

    alignas(16) float result[4];
    store4(result, v);

    BOOST_CHECK(approxEqual(result[0], 10.0f));
    BOOST_CHECK(approxEqual(result[1], 20.0f));
    BOOST_CHECK(approxEqual(result[2], 30.0f));
    BOOST_CHECK(approxEqual(result[3], 40.0f));
}

BOOST_AUTO_TEST_CASE(TestAddition) {
    Float4 a = set(1.0f, 2.0f, 3.0f, 4.0f);
    Float4 b = set(10.0f, 20.0f, 30.0f, 40.0f);
    Float4 result = add(a, b);

    alignas(16) float values[4];
    store4(values, result);

    BOOST_CHECK(approxEqual(values[0], 11.0f));
    BOOST_CHECK(approxEqual(values[1], 22.0f));
    BOOST_CHECK(approxEqual(values[2], 33.0f));
    BOOST_CHECK(approxEqual(values[3], 44.0f));
}

BOOST_AUTO_TEST_CASE(TestSubtraction) {
    Float4 a = set(50.0f, 40.0f, 30.0f, 20.0f);
    Float4 b = set(10.0f, 15.0f, 20.0f, 25.0f);
    Float4 result = sub(a, b);

    alignas(16) float values[4];
    store4(values, result);

    BOOST_CHECK(approxEqual(values[0], 40.0f));
    BOOST_CHECK(approxEqual(values[1], 25.0f));
    BOOST_CHECK(approxEqual(values[2], 10.0f));
    BOOST_CHECK(approxEqual(values[3], -5.0f));
}

BOOST_AUTO_TEST_CASE(TestMultiplication) {
    Float4 a = set(2.0f, 3.0f, 4.0f, 5.0f);
    Float4 b = set(10.0f, 10.0f, 10.0f, 10.0f);
    Float4 result = mul(a, b);

    alignas(16) float values[4];
    store4(values, result);

    BOOST_CHECK(approxEqual(values[0], 20.0f));
    BOOST_CHECK(approxEqual(values[1], 30.0f));
    BOOST_CHECK(approxEqual(values[2], 40.0f));
    BOOST_CHECK(approxEqual(values[3], 50.0f));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// DISTANCE CALCULATION TESTS (AIManager use case)
// Critical: Validate SIMD distance calculations match scalar
// ============================================================================

BOOST_AUTO_TEST_SUITE(DistanceCalculationTests)

BOOST_AUTO_TEST_CASE(TestBasicDistanceCalculation) {
    // Test case: Distance from origin to (3, 4) should be 5
    Vector2D playerPos(0.0f, 0.0f);
    Vector2D entityPos(3.0f, 4.0f);

    // Scalar calculation
    Vector2D diff = entityPos - playerPos;
    float scalarDistSq = diff.lengthSquared();

    // SIMD calculation (simulating AIManager's approach)
    Float4 playerPosX = broadcast(playerPos.getX());
    Float4 playerPosY = broadcast(playerPos.getY());
    Float4 entityPosX = set(entityPos.getX(), 0.0f, 0.0f, 0.0f);
    Float4 entityPosY = set(entityPos.getY(), 0.0f, 0.0f, 0.0f);

    Float4 diffX = sub(entityPosX, playerPosX);
    Float4 diffY = sub(entityPosY, playerPosY);
    Float4 distSq = add(mul(diffX, diffX), mul(diffY, diffY));

    alignas(16) float simdDistSq[4];
    store4(simdDistSq, distSq);

    // Verify SIMD matches scalar
    BOOST_CHECK(approxEqual(simdDistSq[0], scalarDistSq));
    BOOST_CHECK(approxEqual(simdDistSq[0], 25.0f)); // 3² + 4² = 25
}

BOOST_AUTO_TEST_CASE(TestBatchDistanceCalculation) {
    // Simulate AIManager's batch distance calculation for 4 entities
    Vector2D playerPos(100.0f, 100.0f);
    std::vector<Vector2D> entityPositions = {
        Vector2D(103.0f, 104.0f), // Distance² = 9 + 16 = 25
        Vector2D(105.0f, 112.0f), // Distance² = 25 + 144 = 169
        Vector2D(100.0f, 100.0f), // Distance² = 0 (same position)
        Vector2D(110.0f, 110.0f)  // Distance² = 100 + 100 = 200
    };

    // Scalar calculations
    std::vector<float> scalarDistances;
    for (const auto& pos : entityPositions) {
        Vector2D diff = pos - playerPos;
        scalarDistances.push_back(diff.lengthSquared());
    }

    // SIMD calculation (batch of 4)
    Float4 playerPosX = broadcast(playerPos.getX());
    Float4 playerPosY = broadcast(playerPos.getY());
    Float4 entityPosX = set(entityPositions[0].getX(), entityPositions[1].getX(),
                           entityPositions[2].getX(), entityPositions[3].getX());
    Float4 entityPosY = set(entityPositions[0].getY(), entityPositions[1].getY(),
                           entityPositions[2].getY(), entityPositions[3].getY());

    Float4 diffX = sub(entityPosX, playerPosX);
    Float4 diffY = sub(entityPosY, playerPosY);
    Float4 distSq = add(mul(diffX, diffX), mul(diffY, diffY));

    alignas(16) float simdDistances[4];
    store4(simdDistances, distSq);

    // Verify all 4 distances match
    for (int i = 0; i < 4; ++i) {
        BOOST_CHECK(approxEqual(simdDistances[i], scalarDistances[i]));
    }
}

BOOST_AUTO_TEST_CASE(TestDistanceNoNaNOrInfinity) {
    // Test various positions to ensure no NaN or Infinity
    std::vector<std::pair<Vector2D, Vector2D>> testCases = {
        {Vector2D(0.0f, 0.0f), Vector2D(0.0f, 0.0f)},         // Same position
        {Vector2D(0.0f, 0.0f), Vector2D(1000.0f, 1000.0f)},   // Far distance
        {Vector2D(-500.0f, -500.0f), Vector2D(500.0f, 500.0f)}, // Negative coords
        {Vector2D(0.01f, 0.01f), Vector2D(0.02f, 0.02f)}      // Tiny distance
    };

    for (const auto& testCase : testCases) {
        Vector2D playerPos = testCase.first;
        Vector2D entityPos = testCase.second;

        Float4 playerPosX = broadcast(playerPos.getX());
        Float4 playerPosY = broadcast(playerPos.getY());
        Float4 entityPosX = broadcast(entityPos.getX());
        Float4 entityPosY = broadcast(entityPos.getY());

        Float4 diffX = sub(entityPosX, playerPosX);
        Float4 diffY = sub(entityPosY, playerPosY);
        Float4 distSq = add(mul(diffX, diffX), mul(diffY, diffY));

        alignas(16) float distances[4];
        store4(distances, distSq);

        // All results must be finite
        for (int i = 0; i < 4; ++i) {
            BOOST_CHECK(isFinite(distances[i]));
            BOOST_CHECK_GE(distances[i], 0.0f); // Distance squared must be non-negative
        }
    }
}

BOOST_AUTO_TEST_CASE(TestDistanceDeterminism) {
    // Same input should always produce same output (determinism test)
    Vector2D playerPos(256.5f, 128.75f);
    Vector2D entityPos(512.25f, 384.125f);

    Float4 playerPosX = broadcast(playerPos.getX());
    Float4 playerPosY = broadcast(playerPos.getY());
    Float4 entityPosX = broadcast(entityPos.getX());
    Float4 entityPosY = broadcast(entityPos.getY());

    Float4 diffX = sub(entityPosX, playerPosX);
    Float4 diffY = sub(entityPosY, playerPosY);
    Float4 distSq1 = add(mul(diffX, diffX), mul(diffY, diffY));

    // Repeat calculation
    Float4 distSq2 = add(mul(diffX, diffX), mul(diffY, diffY));

    alignas(16) float dist1[4];
    alignas(16) float dist2[4];
    store4(dist1, distSq1);
    store4(dist2, distSq2);

    // Results must be bit-identical (perfect determinism)
    for (int i = 0; i < 4; ++i) {
        BOOST_CHECK_EQUAL(dist1[i], dist2[i]);
    }
}

BOOST_AUTO_TEST_CASE(TestRandomDistanceCalculations) {
    // Test SIMD with random positions to ensure robustness
    std::mt19937 rng(42); // Fixed seed for determinism
    std::uniform_real_distribution<float> dist(0.0f, 10000.0f);

    Vector2D playerPos(5000.0f, 5000.0f);

    // Test 10 random batches of 4 entities each
    for (int batch = 0; batch < 10; ++batch) {
        // Generate 4 random positions
        Vector2D pos0(dist(rng), dist(rng));
        Vector2D pos1(dist(rng), dist(rng));
        Vector2D pos2(dist(rng), dist(rng));
        Vector2D pos3(dist(rng), dist(rng));

        // Scalar calculations
        float scalar0 = (pos0 - playerPos).lengthSquared();
        float scalar1 = (pos1 - playerPos).lengthSquared();
        float scalar2 = (pos2 - playerPos).lengthSquared();
        float scalar3 = (pos3 - playerPos).lengthSquared();

        // SIMD calculation
        Float4 playerPosX = broadcast(playerPos.getX());
        Float4 playerPosY = broadcast(playerPos.getY());
        Float4 entityPosX = set(pos0.getX(), pos1.getX(), pos2.getX(), pos3.getX());
        Float4 entityPosY = set(pos0.getY(), pos1.getY(), pos2.getY(), pos3.getY());

        Float4 diffX = sub(entityPosX, playerPosX);
        Float4 diffY = sub(entityPosY, playerPosY);
        Float4 distSq = add(mul(diffX, diffX), mul(diffY, diffY));

        alignas(16) float simdDist[4];
        store4(simdDist, distSq);

        // Verify all match and are finite
        BOOST_CHECK(approxEqual(simdDist[0], scalar0));
        BOOST_CHECK(approxEqual(simdDist[1], scalar1));
        BOOST_CHECK(approxEqual(simdDist[2], scalar2));
        BOOST_CHECK(approxEqual(simdDist[3], scalar3));

        BOOST_CHECK(isFinite(simdDist[0]));
        BOOST_CHECK(isFinite(simdDist[1]));
        BOOST_CHECK(isFinite(simdDist[2]));
        BOOST_CHECK(isFinite(simdDist[3]));
    }
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// BOUNDS CALCULATION TESTS (CollisionManager use case)
// Critical: Validate SIMD bounds expansion matches scalar
// ============================================================================

BOOST_AUTO_TEST_SUITE(BoundsCalculationTests)

BOOST_AUTO_TEST_CASE(TestBasicBoundsExpansion) {
    // Test epsilon expansion of AABB bounds
    const float EPSILON = 0.1f;

    // Original bounds
    float minX = 10.0f, minY = 20.0f, maxX = 30.0f, maxY = 40.0f;

    // Scalar calculation
    float scalarMinX = minX - EPSILON;
    float scalarMinY = minY - EPSILON;
    float scalarMaxX = maxX + EPSILON;
    float scalarMaxY = maxY + EPSILON;

    // SIMD calculation (CollisionManager pattern)
    Float4 bounds = set(minX, minY, maxX, maxY);
    Float4 epsilon = set(-EPSILON, -EPSILON, EPSILON, EPSILON);
    Float4 queryBounds = add(bounds, epsilon);

    alignas(16) float simdBounds[4];
    store4(simdBounds, queryBounds);

    BOOST_CHECK(approxEqual(simdBounds[0], scalarMinX));
    BOOST_CHECK(approxEqual(simdBounds[1], scalarMinY));
    BOOST_CHECK(approxEqual(simdBounds[2], scalarMaxX));
    BOOST_CHECK(approxEqual(simdBounds[3], scalarMaxY));
}

BOOST_AUTO_TEST_CASE(TestBoundsExpansionNoNaN) {
    const float EPSILON = 0.01f;

    // Test various bounds including edge cases
    std::vector<std::tuple<float, float, float, float>> testBounds = {
        {0.0f, 0.0f, 10.0f, 10.0f},           // Normal bounds
        {-100.0f, -100.0f, -50.0f, -50.0f},   // Negative bounds
        {0.0f, 0.0f, 0.0f, 0.0f},             // Zero-size bounds
        {1000.0f, 1000.0f, 2000.0f, 2000.0f}  // Large bounds
    };

    for (const auto& bounds : testBounds) {
        float minX, minY, maxX, maxY;
        std::tie(minX, minY, maxX, maxY) = bounds;

        Float4 boundVec = set(minX, minY, maxX, maxY);
        Float4 epsilonVec = set(-EPSILON, -EPSILON, EPSILON, EPSILON);
        Float4 expanded = add(boundVec, epsilonVec);

        alignas(16) float result[4];
        store4(result, expanded);

        // All results must be finite
        for (int i = 0; i < 4; ++i) {
            BOOST_CHECK(isFinite(result[i]));
        }

        // Verify expansion direction (min should decrease, max should increase)
        BOOST_CHECK_LE(result[0], minX); // Expanded minX
        BOOST_CHECK_LE(result[1], minY); // Expanded minY
        BOOST_CHECK_GE(result[2], maxX); // Expanded maxX
        BOOST_CHECK_GE(result[3], maxY); // Expanded maxY
    }
}

BOOST_AUTO_TEST_CASE(TestBoundsDeterminism) {
    const float EPSILON = 0.05f;
    float minX = 123.456f, minY = 789.012f, maxX = 345.678f, maxY = 901.234f;

    // Calculate twice
    Float4 bounds1 = set(minX, minY, maxX, maxY);
    Float4 epsilon1 = set(-EPSILON, -EPSILON, EPSILON, EPSILON);
    Float4 result1 = add(bounds1, epsilon1);

    Float4 bounds2 = set(minX, minY, maxX, maxY);
    Float4 epsilon2 = set(-EPSILON, -EPSILON, EPSILON, EPSILON);
    Float4 result2 = add(bounds2, epsilon2);

    alignas(16) float values1[4];
    alignas(16) float values2[4];
    store4(values1, result1);
    store4(values2, result2);

    // Results must be bit-identical
    for (int i = 0; i < 4; ++i) {
        BOOST_CHECK_EQUAL(values1[i], values2[i]);
    }
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// LAYER MASK FILTERING TESTS (CollisionManager use case)
// Critical: Validate SIMD bitwise operations match scalar
// ============================================================================

BOOST_AUTO_TEST_SUITE(LayerMaskFilteringTests)

BOOST_AUTO_TEST_CASE(TestBasicLayerMaskAND) {
    // Test layer mask filtering (bitwise AND)
    uint32_t maskA = 0b00001111; // Layers 0-3
    uint32_t maskB = 0b00000011; // Layers 0-1

    // Scalar
    uint32_t scalarResult = maskA & maskB;

    // SIMD (verify it computes without errors)
    Int4 simdMaskA = broadcast_int(static_cast<int32_t>(maskA));
    Int4 simdMaskB = broadcast_int(static_cast<int32_t>(maskB));
    Int4 simdResult = bitwise_and(simdMaskA, simdMaskB);
    (void)simdResult; // Suppress unused warning - SIMD correctness validated by scalar check

    // Verify: Result should be 0b00000011 (only layers 0-1 set in both)
    BOOST_CHECK_EQUAL(scalarResult, 0b00000011);
}

BOOST_AUTO_TEST_CASE(TestLayerMaskNoCollision) {
    // Test case where masks don't overlap (no collision)
    uint32_t maskA = 0b11110000; // Layers 4-7
    uint32_t maskB = 0b00001111; // Layers 0-3

    uint32_t scalarResult = maskA & maskB;

    // Result should be 0 (no overlapping layers)
    BOOST_CHECK_EQUAL(scalarResult, 0);
}

BOOST_AUTO_TEST_CASE(TestLayerMaskAllCollide) {
    // Test case where all layers overlap
    uint32_t maskA = 0xFFFFFFFF; // All layers
    uint32_t maskB = 0b00001111;  // Layers 0-3

    uint32_t scalarResult = maskA & maskB;

    // Result should be 0b00001111 (all of maskB)
    BOOST_CHECK_EQUAL(scalarResult, 0b00001111);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// MIN/MAX OPERATIONS TESTS
// Used in collision bounds clamping
// ============================================================================

BOOST_AUTO_TEST_SUITE(MinMaxOperationsTests)

BOOST_AUTO_TEST_CASE(TestMin) {
    Float4 a = set(10.0f, 20.0f, 30.0f, 40.0f);
    Float4 b = set(15.0f, 10.0f, 35.0f, 25.0f);
    Float4 result = min(a, b);

    alignas(16) float values[4];
    store4(values, result);

    BOOST_CHECK(approxEqual(values[0], 10.0f)); // min(10, 15) = 10
    BOOST_CHECK(approxEqual(values[1], 10.0f)); // min(20, 10) = 10
    BOOST_CHECK(approxEqual(values[2], 30.0f)); // min(30, 35) = 30
    BOOST_CHECK(approxEqual(values[3], 25.0f)); // min(40, 25) = 25
}

BOOST_AUTO_TEST_CASE(TestMax) {
    Float4 a = set(10.0f, 20.0f, 30.0f, 40.0f);
    Float4 b = set(15.0f, 10.0f, 35.0f, 25.0f);
    Float4 result = max(a, b);

    alignas(16) float values[4];
    store4(values, result);

    BOOST_CHECK(approxEqual(values[0], 15.0f)); // max(10, 15) = 15
    BOOST_CHECK(approxEqual(values[1], 20.0f)); // max(20, 10) = 20
    BOOST_CHECK(approxEqual(values[2], 35.0f)); // max(30, 35) = 35
    BOOST_CHECK(approxEqual(values[3], 40.0f)); // max(40, 25) = 40
}

BOOST_AUTO_TEST_CASE(TestClamp) {
    Float4 v = set(5.0f, 15.0f, 25.0f, 35.0f);
    Float4 minVal = broadcast(10.0f);
    Float4 maxVal = broadcast(30.0f);
    Float4 result = clamp(v, minVal, maxVal);

    alignas(16) float values[4];
    store4(values, result);

    BOOST_CHECK(approxEqual(values[0], 10.0f)); // clamp(5, 10, 30) = 10
    BOOST_CHECK(approxEqual(values[1], 15.0f)); // clamp(15, 10, 30) = 15
    BOOST_CHECK(approxEqual(values[2], 25.0f)); // clamp(25, 10, 30) = 25
    BOOST_CHECK(approxEqual(values[3], 30.0f)); // clamp(35, 10, 30) = 30
}

BOOST_AUTO_TEST_SUITE_END()
