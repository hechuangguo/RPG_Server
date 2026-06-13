/**
 * @file    Vec.h
 * @brief  坐标/向量类 —— 一维 Vec1、二维 Vec2、三维 Vec3（float 分量）
 *
 * 约定：
 * - 分量类型统一为 float，与存档 CharBase.pos_x/y/z 一致。
 * - 相等比较使用带 EPSILON 的近似比较，避免浮点裸 == 的精度误差。
 * - 声明在本头文件，运算实现在 Vec.cpp。
 *
 * 使用示例：
 * @code
 *   Vec3 a(1, 2, 3), b(4, 5, 6);
 *   Vec3 c   = a + b;                 // 分量相加
 *   float d  = a.distanceTo(b);       // 欧氏距离
 *   Vec3 dir = (b - a).normalized();  // 单位方向向量
 *   float dp = a.dot(b);             // 点乘
 *   Vec3 cp  = a.cross(b);           // 叉乘
 * @endcode
 */

#pragma once

#include <string>

/** @brief 浮点近似相等阈值（小于此差值视为相等） */
constexpr float VEC_EPSILON = 1e-6f;

/**
 * @brief 一维坐标/向量
 */
class Vec1
{
public:
    float x;  /**< X 分量 */

    /** @brief 构造（默认零向量） */
    explicit Vec1(float vx = 0.f);

    Vec1 operator+(const Vec1& o) const;  /**< 分量相加 */
    Vec1 operator-(const Vec1& o) const;  /**< 分量相减 */
    Vec1 operator*(float s) const;        /**< 数乘 */
    Vec1& operator+=(const Vec1& o);      /**< 累加 */
    Vec1& operator-=(const Vec1& o);      /**< 累减 */
    bool operator==(const Vec1& o) const; /**< 近似相等（EPSILON） */
    bool operator!=(const Vec1& o) const; /**< 近似不等 */

    /** @brief 模长 |x| */
    float length() const;
    /** @brief 到另一坐标的距离 */
    float distanceTo(const Vec1& o) const;
    /** @brief 是否为零向量（近似） */
    bool isZero() const;
    /** @brief 文本表示，如 "(1.000)" */
    std::string toString() const;
};

/**
 * @brief 二维坐标/向量
 */
class Vec2
{
public:
    float x;  /**< X 分量 */
    float y;  /**< Y 分量 */

    /** @brief 构造（默认零向量） */
    Vec2(float vx = 0.f, float vy = 0.f);

    Vec2 operator+(const Vec2& o) const;  /**< 分量相加 */
    Vec2 operator-(const Vec2& o) const;  /**< 分量相减 */
    Vec2 operator*(float s) const;        /**< 数乘 */
    Vec2& operator+=(const Vec2& o);      /**< 累加 */
    Vec2& operator-=(const Vec2& o);      /**< 累减 */
    bool operator==(const Vec2& o) const; /**< 近似相等（EPSILON） */
    bool operator!=(const Vec2& o) const; /**< 近似不等 */

    /** @brief 点乘 x*o.x + y*o.y */
    float dot(const Vec2& o) const;
    /** @brief 模长 sqrt(x^2+y^2) */
    float length() const;
    /** @brief 模长平方（避免开方，用于比较距离） */
    float lengthSq() const;
    /** @brief 到另一坐标的距离 */
    float distanceTo(const Vec2& o) const;
    /** @brief 到另一坐标的距离平方 */
    float distanceSqTo(const Vec2& o) const;
    /** @brief 原地归一化为单位向量（零向量保持不变） */
    void normalize();
    /** @brief 返回归一化后的副本（零向量返回自身副本） */
    Vec2 normalized() const;
    /** @brief 是否为零向量（近似） */
    bool isZero() const;
    /** @brief 文本表示，如 "(1.000, 2.000)" */
    std::string toString() const;
};

/**
 * @brief 三维坐标/向量
 */
class Vec3
{
public:
    float x;  /**< X 分量 */
    float y;  /**< Y 分量 */
    float z;  /**< Z 分量 */

    /** @brief 构造（默认零向量） */
    Vec3(float vx = 0.f, float vy = 0.f, float vz = 0.f);

    Vec3 operator+(const Vec3& o) const;  /**< 分量相加 */
    Vec3 operator-(const Vec3& o) const;  /**< 分量相减 */
    Vec3 operator*(float s) const;        /**< 数乘 */
    Vec3& operator+=(const Vec3& o);      /**< 累加 */
    Vec3& operator-=(const Vec3& o);      /**< 累减 */
    bool operator==(const Vec3& o) const; /**< 近似相等（EPSILON） */
    bool operator!=(const Vec3& o) const; /**< 近似不等 */

    /** @brief 点乘 x*o.x + y*o.y + z*o.z */
    float dot(const Vec3& o) const;
    /** @brief 叉乘，返回垂直于二者的向量 */
    Vec3 cross(const Vec3& o) const;
    /** @brief 模长 sqrt(x^2+y^2+z^2) */
    float length() const;
    /** @brief 模长平方（避免开方，用于比较距离） */
    float lengthSq() const;
    /** @brief 到另一坐标的距离 */
    float distanceTo(const Vec3& o) const;
    /** @brief 到另一坐标的距离平方 */
    float distanceSqTo(const Vec3& o) const;
    /** @brief 原地归一化为单位向量（零向量保持不变） */
    void normalize();
    /** @brief 返回归一化后的副本（零向量返回自身副本） */
    Vec3 normalized() const;
    /** @brief 是否为零向量（近似） */
    bool isZero() const;
    /** @brief 文本表示，如 "(1.000, 2.000, 3.000)" */
    std::string toString() const;
};
