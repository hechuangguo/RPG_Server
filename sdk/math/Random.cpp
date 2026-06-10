/**
 * @file    Random.cpp
 * @brief   随机数工具类实现（thread_local std::mt19937 引擎）
 */

#include "Random.h"

#include <algorithm>

std::mt19937& Random::engine()
{
    // 每线程独立引擎；首次访问用 random_device 播种，保证不同进程/线程序列各异
    static thread_local std::mt19937 eng{std::random_device{}()};
    return eng;
}

void Random::seed(uint32_t s)
{
    engine().seed(s);
}

int Random::range(int minV, int maxV)
{
    if (minV > maxV)
    {
        std::swap(minV, maxV);
    }
    std::uniform_int_distribution<int> dist(minV, maxV);
    return dist(engine());
}

int64_t Random::range64(int64_t minV, int64_t maxV)
{
    if (minV > maxV)
    {
        std::swap(minV, maxV);
    }
    std::uniform_int_distribution<int64_t> dist(minV, maxV);
    return dist(engine());
}

uint32_t Random::rangeU(uint32_t minV, uint32_t maxV)
{
    if (minV > maxV)
    {
        std::swap(minV, maxV);
    }
    std::uniform_int_distribution<uint32_t> dist(minV, maxV);
    return dist(engine());
}

double Random::rangeF(double minV, double maxV)
{
    if (minV > maxV)
    {
        std::swap(minV, maxV);
    }
    std::uniform_real_distribution<double> dist(minV, maxV);
    return dist(engine());
}

double Random::next01()
{
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(engine());
}

bool Random::chance(double prob)
{
    if (prob <= 0.0) return false;
    if (prob >= 1.0) return true;
    return next01() < prob;
}

bool Random::percent(int p)
{
    if (p <= 0) return false;
    if (p >= 100) return true;
    // [1,100] 命中 p 个点视为成功
    return range(1, 100) <= p;
}

bool Random::boolean()
{
    std::uniform_int_distribution<int> dist(0, 1);
    return dist(engine()) == 1;
}
