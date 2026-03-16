local scripts = player_manager and player_manager.__scripts or {}

player_manager = {__scripts = scripts}

-- player 스크립트 등록 (buff_manager와 동일한 패턴)
function player_manager.register(script, events)
    if not script.id then
        print("no script.id " .. tostring(script))
        return
    end

    local sname = "player." .. script.id

    if scripts[script.id] then
        -- hotfix: 기존 리스너 교체
        event_manager.registerListener(sname, nil)
        print(sname .. " hotfix reload")
    end

    script.__name   = sname
    script.__events = events

    event_manager.registerListener(sname, script)
    scripts[script.id] = script

    print(sname .. " registered")
end

function player_manager.unregister(script_id)
    local sname = "player." .. script_id
    event_manager.registerListener(sname, nil)
    scripts[script_id] = nil
    print(sname .. " unregistered")
end

-- 플레이어 생성 시 호출 — 등록된 모든 스크립트를 player에게 구독
function player_manager.initPlayer(owner)
    for _, script in pairs(scripts) do
        if script.__events then
            event_manager.subscribe(owner, script.__name, script.__events)
        end
        if script.onInit then
            script.onInit(owner)
        end
    end
end
