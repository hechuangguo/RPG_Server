/**
 * @file    MoveValidator.cpp
 * @brief  MoveValidator 实现
 */

#include "MoveValidator.h"

#include <cmath>

namespace
{

constexpr float kFallbackMaxCoord = 100000.f;

float dist3(float ax, float ay, float az, float bx, float by, float bz)
{
    const float dx = bx - ax;
    const float dy = by - ay;
    const float dz = bz - az;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool inBounds(const MapWorldBounds& b, float x, float y, float z)
{
    return x >= b.minX && x <= b.maxX && y >= b.minY && y <= b.maxY && z >= b.minZ && z <= b.maxZ;
}

} // namespace

MoveValidateResult validateMoveRequest(const MapRuntimeData* mapData,
                                       float curX, float curY, float curZ,
                                       float dstX, float dstY, float dstZ,
                                       uint8_t moveType,
                                       std::string* reasonOut)
{
    auto fail = [&](MoveValidateResult r, const char* msg) {
        if (reasonOut)
            *reasonOut = msg;
        return r;
    };

    if (moveType != 0 && moveType != 1)
        return fail(MoveValidateResult::BAD_MOVE_TYPE, "非法 moveType");

    if (!mapData)
    {
        if (dstX < -kFallbackMaxCoord || dstX > kFallbackMaxCoord ||
            dstY < -kFallbackMaxCoord || dstY > kFallbackMaxCoord ||
            dstZ < -kFallbackMaxCoord || dstZ > kFallbackMaxCoord)
            return fail(MoveValidateResult::OUT_OF_BOUNDS, "坐标越界");
        return MoveValidateResult::OK;
    }

    if (!inBounds(mapData->bounds, dstX, dstY, dstZ))
        return fail(MoveValidateResult::OUT_OF_BOUNDS, "超出 worldBounds");

    const float step = dist3(curX, curY, curZ, dstX, dstY, dstZ);
    const float maxStep = (moveType == 1) ? mapData->maxStepRun : mapData->maxStepWalk;
    if (step > maxStep)
        return fail(MoveValidateResult::STEP_TOO_LONG, "单步距离超限");

    return MoveValidateResult::OK;
}
