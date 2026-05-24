/**
 * @file    RecordUser.h
 * @brief  RecordServer 用户对象 —— 持久化数据与读存档
 */

#pragma once
#include "../sdk/util/UserBase.h"
#include <cstdint>
#include <memory>
#include <string>

/**
 * @brief 用户完整存档（UserBase + JSON 扩展字段）
 */
struct UserRecord
{
    UserBase    base;
    std::string bagJson;
    std::string skillJson;
    std::string questJson;
    uint64_t    lastSaveTime = 0;
    bool        dirty        = false;
};

/**
 * @brief RecordServer 内存中的用户实例
 */
class RecordUser : public IUser
{
public:
    static std::shared_ptr<RecordUser> create(const UserBase& base);

    /** @brief 初始化 Record 结构与脏标记 */
    bool init();

    /** @brief 将 IUser 基础属性同步到 Record 并清除脏标记 */
    bool save();

    /** @brief 从 Record 恢复到 IUser 基础属性 */
    bool load();

    UserRecord& record() { return m_record; }
    const UserRecord& record() const { return m_record; }

    void markDirty() { m_record.dirty = true; }
    bool needSave() const { return m_record.dirty; }

private:
    explicit RecordUser(const UserBase& base);

    UserRecord m_record;
    bool       m_initialized = false;
};
