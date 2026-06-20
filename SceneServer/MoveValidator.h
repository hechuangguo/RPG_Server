/**
 * @file    MoveValidator.h
 * @brief  客户端移动请求校验（bounds + 步长）
 */

#pragma once

#include "../sdk/util/MapRuntimeTypes.h"

#include <cstdint>
#include <string>

/** @brief 移动校验结果 */
enum class MoveValidateResult : uint8_t
{
    OK = 0,
    BAD_MOVE_TYPE,
    OUT_OF_BOUNDS,
    STEP_TOO_LONG,
};

/**
 * @brief 校验 C2S 移动目标点
 * @param mapData 地图 runtime；nullptr 时仅做宽松坐标范围检查
 */
MoveValidateResult validateMoveRequest(const MapRuntimeData* mapData,
                                       float curX, float curY, float curZ,
                                       float dstX, float dstY, float dstZ,
                                       uint8_t moveType,
                                       std::string* reasonOut = nullptr);
