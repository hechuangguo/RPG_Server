/**
 * @file    UserBase.h
 * @brief  基础用户数据与接口定义
 *
 * 所有服务器共享的用户最小数据集 + 状态枚举 + 虚基类 IUser。
 * 各服务器通过继承 IUser 扩展各自关心的字段（如 SessionServer 追加 SocialData）。
 *
 * 数据流：
 * @code
 *   DB (MySQL CharBase) → RecordServer → SceneServer → AOIServer → 客户端
 * @endcode
 */

#pragma once
#include <cstdint>
#include <string>

/** @brief 用户唯一 ID（64 位无符号整数） */
using UserID = uint64_t;

/** @brief 无效用户 ID */
constexpr UserID INVALID_USER_ID = 0;

/**
 * @brief 用户基本属性（纯数据结构）
 *
 * 这是服务器间传递用户数据的最小载体。
 * 内存布局紧凑，可直接序列化/反序列化。
 */
struct UserBase
{
    UserID      userID   = INVALID_USER_ID;  /**< 用户唯一 ID */
    std::string name;                        /**< 用户名称 */
    uint32_t    level    = 1;                /**< 等级 */
    uint32_t    vocation = 0;                /**< 职业（0=战士 1=法师 2=弓手 ...） */
    uint32_t    sex      = 0;                /**< 性别：0=男 1=女 */
    uint32_t    modelID  = 1;                /**< 角色模型 ID（1=男大 2=男小 3=女大 4=女小） */
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
 * @brief 用户在线状态
 *
 * 各服务器维护用户在自身内存中的生命周期状态。
 */
enum class UserState : uint8_t
{
    OFFLINE    = 0,  /**< 离线 */
    ONLINE     = 1,  /**< 在线（场景中活跃） */
    LOADING    = 2,  /**< 数据加载中 */
    SAVING     = 3,  /**< 数据保存中 */
};

/**
 * @brief 用户虚基类
 *
 * 所有服务器通过继承此类扩展各自逻辑。
 * 提供 OnTick / OnLogin / OnLogout 虚函数供子类覆写。
 *
 * 示例：
 * @code
 *   class SceneUser : public IUser {
 *   public:
 *       explicit SceneUser(const UserBase& base) : IUser(base) {}
 *       void OnTick(uint64_t nowMs) override { ... }
 *   };
 * @endcode
 */
class IUser
{
public:
    /** @brief 以基础数据构造用户对象，初始状态为 OFFLINE */
    explicit IUser(const UserBase& base) : m_base(base), m_state(UserState::OFFLINE) {}

    /** @brief 虚析构，支持经基类指针安全释放派生类 */
    virtual ~IUser() = default;

    /** @brief 获取用户唯一 ID */
    UserID GetID() const { return m_base.userID; }

    /** @brief 获取用户名的 C 字符串视图 */
    const char* GetName() const { return m_base.name.c_str(); }

    /** @brief 获取当前在线状态 */
    UserState GetState() const { return m_state; }

    /** @brief 设置当前在线状态 */
    void SetState(UserState s) { m_state = s; }

    /** @brief 获取可写基础属性引用 */
    UserBase& Base() { return m_base; }

    /** @brief 获取只读基础属性引用 */
    const UserBase& Base() const { return m_base; }

    /** @brief 每帧回调（子类可选覆写） */
    virtual void OnTick(uint64_t /*nowMs*/) {}

    /** @brief 用户登录时回调 */
    virtual void OnLogin()             {}

    /** @brief 用户登出时回调 */
    virtual void OnLogout()            {}

protected:
    UserBase  m_base;   /**< 基础用户数据 */
    UserState m_state;  /**< 当前状态 */
};
