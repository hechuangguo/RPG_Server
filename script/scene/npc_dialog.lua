-- ============================================================
--  npc_dialog.lua —— C++ callScriptBool 与 NPC 脚本（如 guide.lua）桥接
--
--  C++ 调用：
--    callScriptBool(npcEntry, "OnNpcTalk", { userId, dialogStep, templateId })
--  成功时本模块通过 send_npc_talk_rsp 下发 S2C_NPC_TALK_RSP
-- ============================================================

NpcDialog = {}

--- templateId → require 模块路径（相对 package.path）
NpcDialog.BY_TEMPLATE = {
    [1] = "npc.guide",
}

--- @param npc SceneEntry userdata（NPC）
--- @param userId number
--- @param dialogStep number 对话节点 ID
--- @param templateId number NPC 模板 ID
--- @return boolean 是否处理成功（供 callScriptBool）
function OnNpcTalk(npc, userId, dialogStep, templateId)
    if not npc then
        return false
    end

    local modPath = NpcDialog.BY_TEMPLATE[templateId]
    if not modPath then
        log_info(string.format("[NpcDialog] no script for templateId=%d", templateId or -1))
        return false
    end

    local ok, mod = pcall(require, modPath)
    if not ok or not mod or type(mod.OnTalk) ~= "function" then
        log_info(string.format("[NpcDialog] load failed: %s", modPath))
        return false
    end

    local step = dialogStep
    if step == nil or step < 0 then
        step = 0
    end

    local dialog = mod.OnTalk(userId, npc:getEntryId(), step)
    if not dialog then
        return false
    end

    NpcDialog.sendToUser(userId, npc:getEntryId(), step, dialog)
    return true
end

--- 将 guide.lua 返回的对话表打包发给客户端
function NpcDialog.sendToUser(userId, npcId, step, dialog)
    send_npc_talk_rsp(userId, npcId, step, dialog.text or "", dialog.options or {})
end
