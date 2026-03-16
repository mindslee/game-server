-- local buffs = {}
local buffs = buff_manager and buff_manager.__buffs or {}

buff_manager = {__buffs = buffs}

function buff_manager.register(buff, events)
    if not buff.id then
        print('no buff.id '..buff)
        return
    end
    
    if buffs[buff.id] then
        -- hotfix: 기존 리스너 해제 후 재등록
        event_manager.registerListener("buff."..buff.id, nil)
        print("buff."..buff.id..' hotfix reload')
    end

    -- buff의 name을 할당함
    buff.__name = "buff."..buff.id

    -- buff가 가진 event목록을 미리 정리한다.
    buff.__events = events

    -- event_manager에 listener로 등록
    event_manager.registerListener(buff.__name, buff)

    buffs[buff.id] = buff

    print(buff.__name..' registered')
end

function buff_manager.unregister(buffId)
    buffs[buffId] = nil
    print('buff.'..buffId..' unregistered')
end

function buff_manager.get(buffId)
    return buffs[buffId]
end

function buff_manager.applyBuff(owner, buffId)
    local buff = buff_manager.get(buffId)
    if buff then
        if buff.__events then
            event_manager.subscribe(owner, buff.__name, buff.__events)
        end

        return buff.onApplied and buff.onApplied(owner)
    end
end

function buff_manager.removeBuff(owner, buffId)
    local buff = buff_manager.get(buffId)
    if buff then
        if buff.__events then
            event_manager.unsubscribe(owner, buff.__name)
        end

        return buff.onRemoved and buff.onRemoved(owner)
    end
end
