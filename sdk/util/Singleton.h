/**
 * @file    Singleton.h
 * @brief  SDK 通用单例基础类（懒汉/饿汉/不可拷贝）
 *
 * 设计目标：
 * - 统一项目内单例写法，减少重复样板代码
 * - 显式表达“不可拷贝、不可移动”的语义约束
 * - 默认提供线程安全的懒汉式单例（Meyers Singleton）
 *
 * 使用示例：
 * @code
 *   class TimerMgr final : public LazySingleton<TimerMgr> {
 *       friend class LazySingleton<TimerMgr>;
 *   private:
 *       TimerMgr() = default;
 *   };
 *
 *   TimerMgr::Instance().Update();
 * @endcode
 */

#pragma once

/**
 * @brief 不可拷贝/不可移动基类
 *
 * 用于表达类实例的唯一所有权语义，避免对象被复制或移动。
 */
class NonCopyable
{
protected:
    NonCopyable() = default;
    ~NonCopyable() = default;

public:
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
    NonCopyable(NonCopyable&&) = delete;
    NonCopyable& operator=(NonCopyable&&) = delete;
};

/**
 * @brief 懒汉式单例模板（线程安全）
 * @tparam T 目标类型
 *
 * 首次调用 Instance() 时构造对象，生命周期持续到进程结束。
 * 依赖 C++11 对函数内静态对象初始化的线程安全保证。
 */
template <typename T>
class LazySingleton : public NonCopyable
{
public:
    /** @brief 获取全局唯一实例 */
    static T& Instance()
    {
        static T instance;
        return instance;
    }

protected:
    LazySingleton() = default;
    ~LazySingleton() = default;
};

/**
 * @brief 饿汉式单例模板（进程启动期初始化）
 * @tparam T 目标类型
 *
 * 在静态初始化阶段构造对象。仅在确实需要“尽早可用”时使用；
 * 否则优先 LazySingleton 以避免静态初始化顺序问题。
 */
template <typename T>
class EagerSingleton : public NonCopyable
{
public:
    /** @brief 获取全局唯一实例 */
    static T& Instance()
    {
        return instance;
    }

protected:
    EagerSingleton() = default;
    ~EagerSingleton() = default;

private:
    inline static T instance{};
};
