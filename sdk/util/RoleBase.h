#pragma once
#include <cstdint>
#include <string>

// ============================================================
//  基础角色实例（所有服务器共用的角色数据骨架）
// ============================================================

using RoleID = uint64_t;
constexpr RoleID INVALID_ROLE_ID = 0;

// 角色基本属性
struct RoleBase
{
    RoleID      roleID   = INVALID_ROLE_ID;
    std::string name;
    uint32_t    level    = 1;
    uint32_t    vocation = 0;   // 职业
    uint32_t    sex      = 0;   // 0=男 1=女
    uint32_t    mapID    = 0;   // 当前地图
    float       posX     = 0.f;
    float       posY     = 0.f;
    float       posZ     = 0.f;
    uint32_t    hp       = 100;
    uint32_t    maxHP    = 100;
    uint32_t    mp       = 100;
    uint32_t    maxMP    = 100;
    uint64_t    gold     = 0;
    uint32_t    connID   = 0;   // 对应网络连接 ID（在各服务器含义不同）
};

// 角色状态（在各服务器内存中维护）
enum class RoleState : uint8_t
{
    OFFLINE    = 0,
    ONLINE     = 1,
    LOADING    = 2,
    SAVING     = 3,
};

// 简单的角色基类（各服务器继承扩展）
class IRole
{
public:
    explicit IRole(const RoleBase& base) : m_base(base), m_state(RoleState::OFFLINE) {}
    virtual ~IRole() = default;

    RoleID      GetID()    const { return m_base.roleID; }
    const char* GetName()  const { return m_base.name.c_str(); }
    RoleState   GetState() const { return m_state; }
    void        SetState(RoleState s) { m_state = s; }
    RoleBase&   Base()           { return m_base; }
    const RoleBase& Base() const { return m_base; }

    virtual void OnTick(uint64_t nowMs) {}   // 每帧回调
    virtual void OnLogin()             {}
    virtual void OnLogout()            {}

protected:
    RoleBase  m_base;
    RoleState m_state;
};
