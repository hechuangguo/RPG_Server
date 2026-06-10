/**
 * @file    Vec.cpp
 * @brief   坐标/向量类实现（Vec1 / Vec2 / Vec3）
 */

#include "Vec.h"

#include <cmath>
#include <cstdio>

namespace
{
/** @brief 浮点近似相等判断 */
inline bool nearlyEqual(float a, float b)
{
    return std::fabs(a - b) <= VEC_EPSILON;
}
}  // namespace

// ============================================================
//  Vec1
// ============================================================
Vec1::Vec1(float vx) : x(vx) {}

Vec1 Vec1::operator+(const Vec1& o) const { return Vec1(x + o.x); }
Vec1 Vec1::operator-(const Vec1& o) const { return Vec1(x - o.x); }
Vec1 Vec1::operator*(float s) const { return Vec1(x * s); }

Vec1& Vec1::operator+=(const Vec1& o)
{
    x += o.x;
    return *this;
}

Vec1& Vec1::operator-=(const Vec1& o)
{
    x -= o.x;
    return *this;
}

bool Vec1::operator==(const Vec1& o) const { return nearlyEqual(x, o.x); }
bool Vec1::operator!=(const Vec1& o) const { return !(*this == o); }

float Vec1::length() const { return std::fabs(x); }
float Vec1::distanceTo(const Vec1& o) const { return std::fabs(x - o.x); }
bool Vec1::isZero() const { return nearlyEqual(x, 0.f); }

std::string Vec1::toString() const
{
    char buf[32];
    snprintf(buf, sizeof(buf), "(%.3f)", x);
    return buf;
}

// ============================================================
//  Vec2
// ============================================================
Vec2::Vec2(float vx, float vy) : x(vx), y(vy) {}

Vec2 Vec2::operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
Vec2 Vec2::operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
Vec2 Vec2::operator*(float s) const { return Vec2(x * s, y * s); }

Vec2& Vec2::operator+=(const Vec2& o)
{
    x += o.x;
    y += o.y;
    return *this;
}

Vec2& Vec2::operator-=(const Vec2& o)
{
    x -= o.x;
    y -= o.y;
    return *this;
}

bool Vec2::operator==(const Vec2& o) const
{
    return nearlyEqual(x, o.x) && nearlyEqual(y, o.y);
}
bool Vec2::operator!=(const Vec2& o) const { return !(*this == o); }

float Vec2::dot(const Vec2& o) const { return x * o.x + y * o.y; }
float Vec2::length() const { return std::sqrt(lengthSq()); }
float Vec2::lengthSq() const { return x * x + y * y; }

float Vec2::distanceTo(const Vec2& o) const { return (*this - o).length(); }
float Vec2::distanceSqTo(const Vec2& o) const { return (*this - o).lengthSq(); }

void Vec2::normalize()
{
    float len = length();
    if (len > VEC_EPSILON)
    {
        x /= len;
        y /= len;
    }
}

Vec2 Vec2::normalized() const
{
    Vec2 r(*this);
    r.normalize();
    return r;
}

bool Vec2::isZero() const { return lengthSq() <= VEC_EPSILON * VEC_EPSILON; }

std::string Vec2::toString() const
{
    char buf[48];
    snprintf(buf, sizeof(buf), "(%.3f, %.3f)", x, y);
    return buf;
}

// ============================================================
//  Vec3
// ============================================================
Vec3::Vec3(float vx, float vy, float vz) : x(vx), y(vy), z(vz) {}

Vec3 Vec3::operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
Vec3 Vec3::operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
Vec3 Vec3::operator*(float s) const { return Vec3(x * s, y * s, z * s); }

Vec3& Vec3::operator+=(const Vec3& o)
{
    x += o.x;
    y += o.y;
    z += o.z;
    return *this;
}

Vec3& Vec3::operator-=(const Vec3& o)
{
    x -= o.x;
    y -= o.y;
    z -= o.z;
    return *this;
}

bool Vec3::operator==(const Vec3& o) const
{
    return nearlyEqual(x, o.x) && nearlyEqual(y, o.y) && nearlyEqual(z, o.z);
}
bool Vec3::operator!=(const Vec3& o) const { return !(*this == o); }

float Vec3::dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }

Vec3 Vec3::cross(const Vec3& o) const
{
    return Vec3(y * o.z - z * o.y,
                z * o.x - x * o.z,
                x * o.y - y * o.x);
}

float Vec3::length() const { return std::sqrt(lengthSq()); }
float Vec3::lengthSq() const { return x * x + y * y + z * z; }

float Vec3::distanceTo(const Vec3& o) const { return (*this - o).length(); }
float Vec3::distanceSqTo(const Vec3& o) const { return (*this - o).lengthSq(); }

void Vec3::normalize()
{
    float len = length();
    if (len > VEC_EPSILON)
    {
        x /= len;
        y /= len;
        z /= len;
    }
}

Vec3 Vec3::normalized() const
{
    Vec3 r(*this);
    r.normalize();
    return r;
}

bool Vec3::isZero() const { return lengthSq() <= VEC_EPSILON * VEC_EPSILON; }

std::string Vec3::toString() const
{
    char buf[64];
    snprintf(buf, sizeof(buf), "(%.3f, %.3f, %.3f)", x, y, z);
    return buf;
}
