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
    UserBase    base;               /**< 基础角色数据（与 t_charbase 对齐） */
    std::string bagJson;            /**< 背包扩展 JSON */
    std::string skillJson;          /**< 技能扩展 JSON */
    std::string questJson;          /**< 任务扩展 JSON */
    uint64_t    lastSaveTime = 0;   /**< 上次成功落库时间戳（ms） */
    bool        dirty        = false; /**< 存档是否有改动 */
};

/**
 * @brief RecordServer 内存中的用户实例
 */
class RecordUser : public IUser
{
public:
    /** @brief 基于 UserBase 创建 RecordUser 实例 */
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
    /** @brief 仅允许工厂创建，确保初始化流程统一 */
    explicit RecordUser(const UserBase& base);

    UserRecord m_record;              /**< 内存态完整存档 */
    bool       m_initialized = false; /**< 初始化完成标记 */
};
