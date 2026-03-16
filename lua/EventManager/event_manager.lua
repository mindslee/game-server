-- local __event_datas = {}
local __event_datas = event_manager and event_manager.__event_datas or {}
-- __event_datas = {owner: {eventName: {event_object.__name: eventName}}}

local _listeners = event_manager and event_manager._listeners or {}

event_manager = {__event_datas = __event_datas, _listeners = _listeners}

function event_manager.registerListener(name, listener)
    _listeners[name] = listener
end

local function __getEventMaps(owner)
    local eventMaps = __event_datas[owner]
    if not eventMaps then eventMaps = {}; __event_datas[owner] = eventMaps end

    return eventMaps
end

function event_manager.subscribe(owner, listenerName, events)
    local eventMaps = __getEventMaps(owner)

    for _, eventName in pairs(events) do
        local eventMap = eventMaps[eventName]
        if not eventMap then eventMap = {}; eventMaps[eventName] = eventMap end

        eventMap[listenerName] = eventName
    end

    print("event_manager.subscribe called by "..listenerName)
end

function event_manager.unsubscribe(owner, listenerName)
    local eventMaps = __getEventMaps(owner)

    for eventName, eventMap in pairs(eventMaps) do
        eventMap[listenerName] = nil
    end
    
    print("event_manager.unsubscribe called by "..listenerName)
end

local function __findEventMap(owner, eventName)
    local eventMaps = __event_datas[owner]
    return eventMaps and eventMaps[eventName]
end

local function __getEventProc(objName, eventName)
    local obj = _listeners[objName]
    if obj then
        local eventProc = obj[eventName]
        if eventProc and type(eventProc) == 'function' then
            return eventProc
        end
    end
    return nil
end

function event_manager.publish(owner, eventName, ...)
    local eventMap = __findEventMap(owner, eventName)
    if eventMap then
        for objName, _eventName in pairs(eventMap) do
            local eventProc = __getEventProc(objName, _eventName)
            if eventProc then
                local success, err = pcall(eventProc, owner, ...)
                if not success then
                    print(string.format("Error in event '%s.%s' for owner '%s': %s", objName, eventName, owner.name, tostring(err)))
                end
            end
        end
    end
end
