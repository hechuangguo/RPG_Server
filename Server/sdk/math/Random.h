/**
 * @file    Random.h
 * @brief  随机数工具库 —— 常用随机数接口封装（整数/浮点/概率/洗牌/取样）
 *
 * 约定：
 * - 全部为静态方法，无需实例化，调用形如 Random::range(1, 6)。
 * - 内部使用 thread_local 的 std::mt19937 引擎（梅森旋转），各线程独立，
 *   单线程事件循环下无锁、无竞争。
 * - 区间约定：整数 range 系列为 **闭区间** [minV, maxV]；浮点为 **半开区间** [minV, maxV)。
 *
 * 使用示例：
 * @code
 *   int dice    = Random::range(1, 6);          // 骰子点数 1..6
 *   double f    = Random::next01();             // [0,1)
 *   bool crit   = Random::percent(15);          // 15% 概率暴击
 *   Random::shuffle(vec.begin(), vec.end());    // 洗牌
 *   const auto& item = Random::pick(lootTable); // 等概率取一个元素
 * @endcode
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

/**
 * @brief 随机数工具类（静态方法，线程内有状态：thread_local 引擎）
 */
class Random
{
public:
    /**
     * @brief 重设当前线程随机引擎的种子
     * @param s 种子值（相同种子产生可复现序列，便于测试回放）
     */
    static void seed(uint32_t s);

    /**
     * @brief 闭区间整数随机
     * @param minV 下界（含）
     * @param maxV 上界（含）
     * @return [minV, maxV] 内的随机整数；若 minV>maxV 自动交换
     */
    static int range(int minV, int maxV);

    /**
     * @brief 闭区间 64 位整数随机
     * @param minV 下界（含）
     * @param maxV 上界（含）
     * @return [minV, maxV] 内的随机整数；若 minV>maxV 自动交换
     */
    static int64_t range64(int64_t minV, int64_t maxV);

    /**
     * @brief 闭区间无符号整数随机
     * @param minV 下界（含）
     * @param maxV 上界（含）
     * @return [minV, maxV] 内的随机无符号整数；若 minV>maxV 自动交换
     */
    static uint32_t rangeU(uint32_t minV, uint32_t maxV);

    /**
     * @brief 半开区间浮点随机
     * @param minV 下界（含）
     * @param maxV 上界（不含）
     * @return [minV, maxV) 内的随机浮点；若 minV>maxV 自动交换
     */
    static double rangeF(double minV, double maxV);

    /** @brief 半开区间标准随机浮点 [0,1) */
    static double next01();

    /**
     * @brief 按概率返回真
     * @param prob 概率，取值钳制到 [0,1]
     * @return 命中返回 true
     */
    static bool chance(double prob);

    /**
     * @brief 按百分比返回真
     * @param p 百分比，取值钳制到 [0,100]
     * @return 命中返回 true
     */
    static bool percent(int p);

    /** @brief 等概率返回 true/false */
    static bool boolean();

    /**
     * @brief 原地洗牌（Fisher-Yates，使用本类引擎）
     * @tparam It 随机访问迭代器类型
     * @param first 起始迭代器
     * @param last  结束迭代器
     */
    template <class It>
    static void shuffle(It first, It last)
    {
        std::shuffle(first, last, engine());
    }

    /**
     * @brief 从非空容器等概率取一个元素的只读引用
     * @tparam T 元素类型
     * @param v 容器（**不可为空**，空容器为未定义行为）
     * @return 随机选中的元素引用
     */
    template <class T>
    static const T& pick(const std::vector<T>& v)
    {
        return v[(size_t)range64(0, (int64_t)v.size() - 1)];
    }

private:
    /** @brief 获取当前线程的随机引擎（首次调用以 random_device 播种） */
    static std::mt19937& engine();
};
