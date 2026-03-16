local quests = quest_manager and quest_manager.__quests or {}

quest_manager = {__quests = quests}

function quest_manager.register(quest, events)
    if not quest.id then
        print("no quest.id.."..quest)
        return
    end

    if quests[quest.id] then
        -- hotfix: 기존 리스너 해제 후 재등록
        event_manager.registerListener("quest."..quest.id, nil)
        print("quest."..quest.id..' hotfix reload')
    end

    quest.__name = "quest."..quest.id
    quest.__events = events

    event_manager.registerListener(quest.__name, quest)

    quests[quest.id] = quest

    print(quest.__name..' registered')
end

function quest_manager.unregister(quest_id)
    quests[quest_id] = nil
    print('quest.'..quest_id..' unregistered')
end

function quest_manager.get(quest_id)
    return quests[quest_id]
end

function quest_manager.acceptQuest(owner, quest_id)
    local quest = quest_manager.get(quest_id)
    if quest then
        if quest.__events then
            event_manager.subscribe(owner, quest.__name, quest.__events)
        end

        return quest.onAccept and quest.onAccept(owner)
    end
end

function quest_manager.completeQuest(owner, quest_id)
    local quest = quest_manager.get(quest_id)
    if quest then
        if quest.__events then
            event_manager.unsubscribe(owner, quest.__name)
        end

        return quest.onComplete and quest.onComplete(owner)
    end
end

function quest_manager.removeQuest(owner, quest_id)
    local quest = quest_manager.get(quest_id)
    if quest then
        if quest.__events then
            event_manager.unsubscribe(owner, quest.__name)
        end

        return quest.onRemove and quest.onRemove(owner)
    end
end
