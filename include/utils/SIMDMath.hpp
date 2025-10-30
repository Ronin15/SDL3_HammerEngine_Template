/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef SIMD_MATH_HPP
#define SIMD_MATH_HPP

/**
 * @file SIMDMath.hpp
 * @brief Cross-platform SIMD abstraction layer for HammerEngine
 *
 * Provides unified SIMD operations that work across:
 * - x86-64: SSE2, AVX2 (Linux, Windows)
 * - ARM64: NEON (Apple Silicon Mac)
 *
 * This abstraction layer allows writing SIMD code once and compiling
 * for multiple platforms without duplicating logic.
 */

#include "utils/Vector2D.hpp"
#include <cmath>

// ============================================================================
// Platform Detection
// ============================================================================

// SSE2 detection (x86-64)
#if defined(__SSE2__) || \
    (defined(_MSC_VER) && \
     (defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)))
#define HAMMER_SIMD_SSE2 1
#include <emmintrin.h>
#endif

// SSE4.1 detection (x86-64)
#if defined(__SSE4_1__) || (defined(_MSC_VER) && defined(__AVX__))
#define HAMMER_SIMD_SSE4 1
#include <smmintrin.h>
#endif

// AVX2 detection (x86-64)
#if defined(__AVX2__) || (defined(_MSC_VER) && defined(__AVX2__))
#define HAMMER_SIMD_AVX2 1
#include <immintrin.h>
#endif

// ARM NEON detection (Apple Silicon)
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#define HAMMER_SIMD_NEON 1
#include <arm_neon.h>
#endif

// ============================================================================
// SIMD Type Definitions
// ============================================================================

namespace HammerEngine {
namespace SIMD {

/**
 * @brief 4-wide float vector (cross-platform)
 * Maps to __m128 on x86 or float32x4_t on ARM
 */
#if defined(HAMMER_SIMD_SSE2)
    using Float4 = __m128;
#elif defined(HAMMER_SIMD_NEON)
    using Float4 = float32x4_t;
#else
    // Scalar fallback
    struct Float4 {
        float data[4];
    };
#endif

/**
 * @brief 4-wide integer vector (cross-platform)
 * Maps to __m128i on x86 or uint32x4_t on ARM
 */
#if defined(HAMMER_SIMD_SSE2)
    using Int4 = __m128i;
#elif defined(HAMMER_SIMD_NEON)
    using Int4 = uint32x4_t;
#else
    // Scalar fallback
    struct Int4 {
        int data[4];
    };
#endif

// ============================================================================
// Load/Store Operations
// ============================================================================

/**
 * @brief Load 4 floats from memory (aligned or unaligned)
 * @param ptr Pointer to 4 consecutive floats
 * @return SIMD vector containing the 4 floats
 */
inline Float4 load4(const float* ptr) {
#if defined(HAMMER_SIMD_SSE2)
    return _mm_loadu_ps(ptr);
#elif defined(HAMMER_SIMD_NEON)
    return vld1q_f32(ptr);
#else
    Float4 result;
    result.data[0] = ptr[0];
    result.data[1] = ptr[1];
    result.data[2] = ptr[2];
    result.data[3] = ptr[3];
    return result;
#endif
}

/**
 * @brief Store 4 floats to memory
 * @param ptr Destination pointer
 * @param v SIMD vector to store
 */
inline void store4(float* ptr, Float4 v) {
#if defined(HAMMER_SIMD_SSE2)
    _mm_storeu_ps(ptr, v);
#elif defined(HAMMER_SIMD_NEON)
    vst1q_f32(ptr, v);
#else
    ptr[0] = v.data[0];
    ptr[1] = v.data[1];
    ptr[2] = v.data[2];
    ptr[3] = v.data[3];
#endif
}

/**
 * @brief Create SIMD vector by broadcasting a single float to all lanes
 * @param value Float to broadcast
 * @return SIMD vector with all 4 lanes set to value
 */
inline Float4 broadcast(float value) {
#if defined(HAMMER_SIMD_SSE2)
    return _mm_set1_ps(value);
#elif defined(HAMMER_SIMD_NEON)
    return vdupq_n_f32(value);
#else
    Float4 result;
    result.data[0] = result.data[1] = result.data[2] = result.data[3] = value;
    return result;
#endif
}

/**
 * @brief Create SIMD vector from 4 individual floats
 * @param x, y, z, w Individual float values
 * @return SIMD vector [x, y, z, w]
 */
inline Float4 set(float x, float y, float z, float w) {
#if defined(HAMMER_SIMD_SSE2)
    return _mm_set_ps(w, z, y, x); // Note: SSE uses reverse order
#elif defined(HAMMER_SIMD_NEON)
    const float data[4] = {x, y, z, w};
    return vld1q_f32(data);
#else
    Float4 result;
    result.data[0] = x;
    result.data[1] = y;
    result.data[2] = z;
    result.data[3] = w;
    return result;
#endif
}

// ============================================================================
// Arithmetic Operations
// ============================================================================

/**
 * @brief Add two SIMD vectors
 */
inline Float4 add(Float4 a, Float4 b) {
#if defined(HAMMER_SIMD_SSE2)
    return _mm_add_ps(a, b);
#elif defined(HAMMER_SIMD_NEON)
    return vaddq_f32(a, b);
#else
    Float4 result;
    result.data[0] = a.data[0] + b.data[0];
    result.data[1] = a.data[1] + b.data[1];
    result.data[2] = a.data[2] + b.data[2];
    result.data[3] = a.data[3] + b.data[3];
    return result;
#endif
}

/**
 * @brief Subtract two SIMD vectors
 */
inline Float4 sub(Float4 a, Float4 b) {
#if defined(HAMMER_SIMD_SSE2)
    return _mm_sub_ps(a, b);
#elif defined(HAMMER_SIMD_NEON)
    return vsubq_f32(a, b);
#else
    Float4 result;
    result.data[0] = a.data[0] - b.data[0];
    result.data[1] = a.data[1] - b.data[1];
    result.data[2] = a.data[2] - b.data[2];
    result.data[3] = a.data[3] - b.data[3];
    return result;
#endif
}

/**
 * @brief Multiply two SIMD vectors (component-wise)
 */
inline Float4 mul(Float4 a, Float4 b) {
#if defined(HAMMER_SIMD_SSE2)
    return _mm_mul_ps(a, b);
#elif defined(HAMMER_SIMD_NEON)
    return vmulq_f32(a, b);
#else
    Float4 result;
    result.data[0] = a.data[0] * b.data[0];
    result.data[1] = a.data[1] * b.data[1];
    result.data[2] = a.data[2] * b.data[2];
    result.data[3] = a.data[3] * b.data[3];
    return result;
#endif
}

/**
 * @brief Fused multiply-add: result = a * b + c
 * More efficient than separate mul + add on modern CPUs
 */
inline Float4 madd(Float4 a, Float4 b, Float4 c) {
#if defined(HAMMER_SIMD_AVX2)
    return _mm_fmadd_ps(a, b, c);
#elif defined(HAMMER_SIMD_NEON)
    return vmlaq_f32(c, a, b);
#elif defined(HAMMER_SIMD_SSE2)
    return _mm_add_ps(_mm_mul_ps(a, b), c);
#else
    Float4 result;
    result.data[0] = a.data[0] * b.data[0] + c.data[0];
    result.data[1] = a.data[1] * b.data[1] + c.data[1];
    result.data[2] = a.data[2] * b.data[2] + c.data[2];
    result.data[3] = a.data[3] * b.data[3] + c.data[3];
    return result;
#endif
}

// ============================================================================
// Comparison Operations
// ============================================================================

/**
 * @brief Less-than comparison (returns mask)
 */
inline Float4 cmplt(Float4 a, Float4 b) {
#if defined(HAMMER_SIMD_SSE2)
    return _mm_cmplt_ps(a, b);
#elif defined(HAMMER_SIMD_NEON)
    return vreinterpretq_f32_u32(vcltq_f32(a, b));
#else
    Float4 result;
    for (int i = 0; i < 4; ++i) {
        result.data[i] = (a.data[i] < b.data[i]) ? -1.0f : 0.0f;
    }
    return result;
#endif
}

/**
 * @brief Bitwise OR
 */
inline Float4 bitwise_or(Float4 a, Float4 b) {
#if defined(HAMMER_SIMD_SSE2)
    return _mm_or_ps(a, b);
#elif defined(HAMMER_SIMD_NEON)
    return vreinterpretq_f32_u32(vorrq_u32(vreinterpretq_u32_f32(a), vreinterpretq_u32_f32(b)));
#else
    Float4 result;
    uint32_t* ra = reinterpret_cast<uint32_t*>(&result.data);
    const uint32_t* aa = reinterpret_cast<const uint32_t*>(&a.data);
    const uint32_t* ba = reinterpret_cast<const uint32_t*>(&b.data);
    for (int i = 0; i < 4; ++i) {
        ra[i] = aa[i] | ba[i];
    }
    return result;
#endif
}

/**
 * @brief Extract movemask (for early-exit tests)
 * @return Bitmask where bit i = sign bit of lane i
 */
inline int movemask(Float4 v) {
#if defined(HAMMER_SIMD_SSE2)
    return _mm_movemask_ps(v);
#elif defined(HAMMER_SIMD_NEON)
    // ARM NEON doesn't have direct movemask - use comparison trick
    uint32x4_t mask = vreinterpretq_u32_f32(v);
    uint32x4_t shifted = vshrq_n_u32(mask, 31); // Extract sign bits
    uint32_t result = vgetq_lane_u32(shifted, 0) |
                     (vgetq_lane_u32(shifted, 1) << 1) |
                     (vgetq_lane_u32(shifted, 2) << 2) |
                     (vgetq_lane_u32(shifted, 3) << 3);
    return static_cast<int>(result);
#else
    int result = 0;
    for (int i = 0; i < 4; ++i) {
        if (reinterpret_cast<const uint32_t*>(&v.data)[i] & 0x80000000) {
            result |= (1 << i);
        }
    }
    return result;
#endif
}

// ============================================================================
// Min/Max Operations
// ============================================================================

/**
 * @brief Component-wise minimum
 */
inline Float4 min(Float4 a, Float4 b) {
#if defined(HAMMER_SIMD_SSE2)
    return _mm_min_ps(a, b);
#elif defined(HAMMER_SIMD_NEON)
    return vminq_f32(a, b);
#else
    Float4 result;
    result.data[0] = std::min(a.data[0], b.data[0]);
    result.data[1] = std::min(a.data[1], b.data[1]);
    result.data[2] = std::min(a.data[2], b.data[2]);
    result.data[3] = std::min(a.data[3], b.data[3]);
    return result;
#endif
}

/**
 * @brief Component-wise maximum
 */
inline Float4 max(Float4 a, Float4 b) {
#if defined(HAMMER_SIMD_SSE2)
    return _mm_max_ps(a, b);
#elif defined(HAMMER_SIMD_NEON)
    return vmaxq_f32(a, b);
#else
    Float4 result;
    result.data[0] = std::max(a.data[0], b.data[0]);
    result.data[1] = std::max(a.data[1], b.data[1]);
    result.data[2] = std::max(a.data[2], b.data[2]);
    result.data[3] = std::max(a.data[3], b.data[3]);
    return result;
#endif
}

/**
 * @brief Component-wise clamp: min(max(v, minVal), maxVal)
 */
inline Float4 clamp(Float4 v, Float4 minVal, Float4 maxVal) {
    return min(max(v, minVal), maxVal);
}

// ============================================================================
// Integer Operations (for layer masks, etc.)
// ============================================================================

/**
 * @brief Broadcast integer to all lanes
 */
inline Int4 broadcast_int(int32_t value) {
#if defined(HAMMER_SIMD_SSE2)
    return _mm_set1_epi32(value);
#elif defined(HAMMER_SIMD_NEON)
    return vdupq_n_u32(static_cast<uint32_t>(value));
#else
    Int4 result;
    result.data[0] = result.data[1] = result.data[2] = result.data[3] = value;
    return result;
#endif
}

/**
 * @brief Bitwise AND (integer)
 */
inline Int4 bitwise_and(Int4 a, Int4 b) {
#if defined(HAMMER_SIMD_SSE2)
    return _mm_and_si128(a, b);
#elif defined(HAMMER_SIMD_NEON)
    return vandq_u32(a, b);
#else
    Int4 result;
    result.data[0] = a.data[0] & b.data[0];
    result.data[1] = a.data[1] & b.data[1];
    result.data[2] = a.data[2] & b.data[2];
    result.data[3] = a.data[3] & b.data[3];
    return result;
#endif
}

/**
 * @brief Compare integers for equality (returns mask)
 */
inline Int4 cmpeq_int(Int4 a, Int4 b) {
#if defined(HAMMER_SIMD_SSE2)
    return _mm_cmpeq_epi32(a, b);
#elif defined(HAMMER_SIMD_NEON)
    return vceqq_u32(a, b);
#else
    Int4 result;
    for (int i = 0; i < 4; ++i) {
        result.data[i] = (a.data[i] == b.data[i]) ? -1 : 0;
    }
    return result;
#endif
}

/**
 * @brief Extract movemask from integer vector
 */
inline int movemask_int(Int4 v) {
#if defined(HAMMER_SIMD_SSE2)
    return _mm_movemask_epi8(v);
#elif defined(HAMMER_SIMD_NEON)
    // Similar to float movemask
    uint32x4_t shifted = vshrq_n_u32(v, 31);
    uint32_t result = vgetq_lane_u32(shifted, 0) |
                     (vgetq_lane_u32(shifted, 1) << 1) |
                     (vgetq_lane_u32(shifted, 2) << 2) |
                     (vgetq_lane_u32(shifted, 3) << 3);
    return static_cast<int>(result);
#else
    int result = 0;
    for (int i = 0; i < 4; ++i) {
        if (v.data[i] & 0x80000000) {
            result |= (1 << i);
        }
    }
    return result;
#endif
}

// ============================================================================
// Vector Math Utilities
// ============================================================================

/**
 * @brief Compute dot product of two 2D vectors (returns scalar)
 * Used for: velocity damping, projection
 */
inline float dot2D(Float4 a, Float4 b) {
#if defined(HAMMER_SIMD_SSE2)
    Float4 prod = _mm_mul_ps(a, b);
    Float4 shuf = _mm_shuffle_ps(prod, prod, _MM_SHUFFLE(2, 3, 0, 1));
    Float4 sum = _mm_add_ps(prod, shuf);
    return _mm_cvtss_f32(sum);
#elif defined(HAMMER_SIMD_NEON)
    Float4 prod = vmulq_f32(a, b);
    float32x2_t sum = vpadd_f32(vget_low_f32(prod), vget_high_f32(prod));
    return vget_lane_f32(sum, 0);
#else
    return a.data[0] * b.data[0] + a.data[1] * b.data[1];
#endif
}

/**
 * @brief Compute squared length of 2D vector (returns scalar)
 * Used for: distance calculations
 */
inline float lengthSquared2D(Float4 v) {
    return dot2D(v, v);
}

/**
 * @brief Compute length of 2D vector (returns scalar)
 */
inline float length2D(Float4 v) {
    return std::sqrt(lengthSquared2D(v));
}

} // namespace SIMD
} // namespace HammerEngine

#endif // SIMD_MATH_HPP
