local monsters = monster_manager and monster_manager.__monsters or {}
monster_manager = {__monsters = monsters}

function monster_manager.register(monster, events)
    if not monster.id then
        print("no monster.id.."..monster)
        return
    end

    if monsters[monster.id] then
        -- hotfix
    end
    
    monster.__name = "monster."..monster.id
    monster.__events = events

    monsters[monster.id] = monster

    print(monster.__name..' registered')
end

function monster_manager.unregister(monster_id)
    monsters[monster_id] = nil
    print('monster.'..monster_id..' unregistered')
end

function monster_manager.get(monster_id)
    return monsters[monster_id]
end
