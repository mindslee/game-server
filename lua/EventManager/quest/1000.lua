local quest = {
    id = 1000,

}

local kill_target_type = "Monster"
local kill_target_id = 2000

function quest.onAccept(owner)
    local quest_table = quest_manager.getQuestTable(quest.id)
    owner:setQuestData(quest.id, kill_target_id, 0)
end

function quest.onKill(owner, target)
    if target.type == kill_target_type and target.id == kill_target_id then
        owner:addQuestData(quest.id, kill_target_id, 1)
    end
end

function quest.onComplete(owner, is_success)

end

function quest.onRemove(owner)
    -- owner:removeQuestData(quest.id)
end

quest_manager.register(quest, {'onKill'})
