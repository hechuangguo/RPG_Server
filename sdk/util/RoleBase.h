/**
 * @file    RoleBase.h
 * @brief  基础角色数据与接口定义
 *
 * 所有服务器共享的角色最小数据集 + 状态枚举 + 虚基类 IRole。
 * 各服务器通过继承 IRole 扩展各自关心的字段（如 SessionServer 追加 SocialData）。
 *
 * 数据流：
 * @code
 *   DB (MySQL t_role) → RecordServer → SceneServer → AOIServer → 客户端
 * @endcode
 */

#pragma once
#include <cstdint>
#include <string>

/** @brief 角色唯一 ID（64 位无符号整数） */
using RoleID = uint64_t;

/** @brief 无效角色 ID */
constexpr RoleID INVALID_ROLE_ID = 0;

/**
 * @brief 角色基本属性（纯数据结构）
 *
 * 这是服务器间传递角色数据的最小载体。
 * 内存布局紧凑，可直接序列化/反序列化。
 */
struct RoleBase
{
    RoleID      roleID   = INVALID_ROLE_ID;  /**< 角色唯一 ID */
    std::string name;                        /**< 角色名称 */
    uint32_t    level    = 1;                /**< 等级 */
    uint32_t    vocation = 0;                /**< 职业（0=战士 1=法师 2=弓手 ...） */
    uint32_t    sex      = 0;                /**< 性别：0=男 1=女 */
    uint32_t    mapID    = 0;                /**< 当前所在地图 ID */
    float       posX     = 0.f;              /**< X 坐标 */
    float       posY     = 0.f;              /**< Y 坐标（高度） */
    float       posZ     = 0.f;              /**< Z 坐标 */
    uint32_t    hp       = 100;              /**< 当前生命值 */
    uint32_t    maxHP    = 100;              /**< 最大生命值 */
    uint32_t    mp       = 100;              /**< 当前魔法值 */
    uint32_t    maxMP    = 100;              /**< 最大魔法值 */
    uint64_t    gold     = 0;                /**< 金币数量 */
    uint32_t    connID   = 0;                /**< 关联的网络连接 ID（上下文相关） */
};

/**
 * @brief 角色在线状态
 *
 * 各服务器维护角色在自身内存中的生命周期状态。
 */
enum class RoleState : uint8_t
{
    OFFLINE    = 0,  /**< 离线 */
    ONLINE     = 1,  /**< 在线（场景中活跃） */
    LOADING    = 2,  /**< 数据加载中 */
    SAVING     = 3,  /**< 数据保存中 */
};

/**
 * @brief 角色虚基类
 *
 * 所有服务器通过继承此类扩展各自逻辑。
 * 提供 OnTick / OnLogin / OnLogout 虚函数供子类覆写。
 *
 * 示例：
 * @code
 *   class SceneRole : public IRole {
 *   public:
 *       explicit SceneRole(const RoleBase& base) : IRole(base) {}
 *       void OnTick(uint64_t nowMs) override { ... }
 *   };
 * @endcode
 */
class IRole
{
public:
    explicit IRole(const RoleBase& base) : m_base(base), m_state(RoleState::OFFLINE) {}
    virtual ~IRole() = default;

    RoleID      GetID()    const { return m_base.roleID; }   /**< 获取角色 ID */
    const char* GetName()  const { return m_base.name.c_str(); } /**< 获取角色名 */
    RoleState   GetState() const { return m_state; }         /**< 获取状态 */
    void        SetState(RoleState s) { m_state = s; }       /**< 设置状态 */
    RoleBase&   Base()           { return m_base; }          /**< 获取基础属性（可写） */
    const RoleBase& Base() const { return m_base; }          /**< 获取基础属性（只读） */

    /** @brief 每帧回调（子类可选覆写） */
    virtual void OnTick(uint64_t /*nowMs*/) {}
    /** @brief 角色登录时回调 */
    virtual void OnLogin()             {}
    /** @brief 角色登出时回调 */
    virtual void OnLogout()            {}

protected:
    RoleBase  m_base;   /**< 基础角色数据 */
    RoleState m_state;  /**< 当前状态 */
};
