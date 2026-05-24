-- ============================================================
--  script/npc/guide.lua  —— 新手引导官对话脚本
-- ============================================================

local Guide = {}

-- ============================================================
--  对话树结构：
--    每个节点包含 text(对话文本) 和 options(选项列表)
--    options[i] = { text="选项文本", next=下一节点ID, acceptQuest=任务ID(可选) }
--    next = -1 表示结束对话
--    根节点 ID = 0，通过 optionIndex 或 step 跳转
-- ============================================================
Guide.DIALOGS = {
    [0] = {
        text = "欢迎来到这片大陆，冒险者！我是新手引导官，让我来带你认识这个世界。",
        options = {
            { text="告诉我更多", next=1 },
            { text="我已经知道了，谢谢", next=-1 },
        }
    },
    [1] = {
        text = "这片大陆被黑暗势力侵占已久，我们需要像你这样的英雄来拯救世界！",
        options = {
            { text="我接受这个使命！", next=2, acceptQuest=1001 },
            { text="我还没准备好", next=-1 },
        }
    },
    [2] = {
        text = "太好了！请先去消灭附近的哥布林，收集 5 个哥布林耳朵带回来。",
        options = {
            { text="好的，我去了", next=-1 },
        }
    },
}

-- ============================================================
--  选项处理逻辑：
--    1. 根据 optionIndex 定位对话节点（nil → 根节点 0）
--    2. 遍历 options，若存在 acceptQuest 字段则自动触发任务接取事件
--    3. 返回当前对话节点供客户端渲染
-- ============================================================
function Guide.OnTalk(userID, npcID, optionIndex)
    -- optionIndex = nil 表示初始对话
    local step = optionIndex or 0
    local dialog = Guide.DIALOGS[step]
    if not dialog then return end

    -- 检查是否接受任务
    for _, opt in ipairs(dialog.options) do
        if opt.acceptQuest then
            log_info(string.format("[Guide] user=%d accepted quest=%d", userID, opt.acceptQuest))
            EventSystem.Fire("quest_accept", userID, opt.acceptQuest)
        end
    end

    return dialog
end

return Guide
