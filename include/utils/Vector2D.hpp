/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef VECTOR_2D_HPP
#define VECTOR_2D_HPP

#include <math.h>
#include <iostream>
#include <cstdint>
#include "BinarySerializer.hpp"

// A simple 2D vector class
class Vector2D : public ISerializable {
public:
    // Constructors
    Vector2D() : m_x(0.0f), m_y(0.0f) {}
    Vector2D(float x, float y) : m_x(x), m_y(y) {}

    // Getters and setters
    float getX() const { return m_x; }
    float getY() const { return m_y; }
    void setX(float x) { m_x = x; }
    void setY(float y) { m_y = y; }

    // Vector operations
    float length() const { return sqrt(m_x * m_x + m_y * m_y); }
    float lengthSquared() const { return m_x * m_x + m_y * m_y; }

    // Operator overloads
    Vector2D operator+(const Vector2D& v2) const {
        return Vector2D(m_x + v2.m_x, m_y + v2.m_y);
    }

    friend Vector2D& operator+=(Vector2D& v1, const Vector2D& v2) {
        v1.m_x += v2.m_x;
        v1.m_y += v2.m_y;
        return v1;
    }

    Vector2D operator*(float scalar) const {
        return Vector2D(m_x * scalar, m_y * scalar);
    }

    Vector2D& operator*=(float scalar) {
        m_x *= scalar;
        m_y *= scalar;
        return *this;
    }

    Vector2D operator-(const Vector2D& v2) const {
        return Vector2D(m_x - v2.m_x, m_y - v2.m_y);
    }

    friend Vector2D& operator-=(Vector2D& v1, const Vector2D& v2) {
        v1.m_x -= v2.m_x;
        v1.m_y -= v2.m_y;
        return v1;
    }

    Vector2D operator/(float scalar) const {
        return Vector2D(m_x / scalar, m_y / scalar);
    }

    Vector2D& operator/=(float scalar) {
        m_x /= scalar;
        m_y /= scalar;
        return *this;
    }

    // Normalize the vector in place
    void normalize() {
        float l = length();
        if (l > 0) {
            (*this) *= 1 / l;
        }
    }
    
    // Return a normalized copy of the vector
    Vector2D normalized() const {
        Vector2D v = *this;
        float l = length();
        if (l > 0) {
            v *= 1 / l;
        }
        return v;
    }

    // Cross-platform binary serialization with proper endianness handling
    bool serialize(std::ostream& stream) const override {
        // Convert to little-endian format for cross-platform compatibility
        auto writeFloat = [&stream](float value) -> bool {
            union { float f; uint32_t i; } converter;
            converter.f = value;
            
            // Write as little-endian bytes
            for (int i = 0; i < 4; ++i) {
                char byte = static_cast<char>((converter.i >> (i * 8)) & 0xFF);
                stream.write(&byte, 1);
                if (!stream.good()) return false;
            }
            return true;
        };
        
        return writeFloat(m_x) && writeFloat(m_y);
    }

    bool deserialize(std::istream& stream) override {
        // Read from little-endian format for cross-platform compatibility
        auto readFloat = [&stream](float& value) -> bool {
            union { float f; uint32_t i; } converter;
            converter.i = 0;
            
            // Read little-endian bytes
            for (int i = 0; i < 4; ++i) {
                char byte;
                stream.read(&byte, 1);
                if (!stream.good() || stream.gcount() != 1) return false;
                converter.i |= (static_cast<uint32_t>(static_cast<unsigned char>(byte)) << (i * 8));
            }
            
            value = converter.f;
            return true;
        };
        
        return readFloat(m_x) && readFloat(m_y);
    }

private:
    float m_x{0.0f};
    float m_y{0.0f};
};

#endif  // VECTOR_2D_HPP