/**
 * @file    LoginEnterErrorCode.h
 * @brief   登录→选角→进场景链路统一错误码（客户端与服间共用语义）
 */

#pragma once

#include <cstdint>

/**
 * @brief SuperServer 进世界编排失败码（GW_USER_LOGIN_RSP.code）
 */
enum class SuperEnterError : int32_t
{
    OK                  = 0,   /**< 成功 */
    NO_RECORD           = -1,  /**< 无可用存档服或 userID 非法 */
    NO_SESSION          = -2,  /**< 无可用会话服 */
    MAP_NOT_REGISTERED  = -3,  /**< 地图未注册或无可用场景实例 */
    SCENE_OFFLINE       = -4,  /**< 目标场景服未连接 */
    LOAD_USER_FAILED    = -5,  /**< Record 加载角色失败 */
    TXN_TIMEOUT         = -10, /**< 登录事务超时回滚 */
    TXN_IN_PROGRESS     = -11, /**< 同角色登录事务进行中（幂等键冲突） */
};

/**
 * @brief Record 创角错误码（S2C_CREATE_USER_RSP / REC_CREATE_CHARACTER_RSP）
 */
enum class CreateCharacterError : int32_t
{
    SYSTEM_ERROR  = -1, /**< 系统失败（DB 异常等） */
    OK            = 0,  /**< 成功 */
    NAME_EXISTS   = 1,  /**< 角色名重复 */
    LIMIT_REACHED = 2,  /**< 达每区角色上限 */
    INVALID_NAME  = 3,  /**< 名非法 */
};

/**
 * @brief Gateway 票据鉴权失败码（S2C_LOGIN_RSP.code，非 0 即失败）
 */
enum class GatewayAuthError : int32_t
{
    OK             = 0,
    INVALID_TOKEN  = 1, /**< token 无效或过期 */
};
