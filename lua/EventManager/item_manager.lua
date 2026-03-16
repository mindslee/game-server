local items = item_manager and item_manager.__items or {}

item_manager = {__items = items}

function item_manager.register(item, events)
    if not item.id then
        print('no item_id '..item)
        return
    end
    item.__name = item.name or "item."..tostring(item.id)
    item.__events = events

    if items[item.id] then
        -- hotfix: 기존 항목 교체 (아이템은 리스너 없음)
        print(item.__name..' hotfix reload')
    end

    items[item.id] = item

    print(item.__name..' registered')
end

function item_manager.unregister(item_id)
    items[item_id] = nil
    print('item.'..item_id..' unregistered')
end

function item_manager.get(item_id)
    return items[item_id]
end

function item_manager.canUse(owner, item_id)
    local item = item_manager.get(item_id)
    if item then
        return item.canUse and item.canUse(owner)
    end
    -- item이 lua에 존재하지 않을때는 기본값으로 true로 함
    return true
end

function item_manager.use(owner, item_id)
    local item = item_manager.get(item_id)
    if item and item.useItem then
        item.useItem(owner)
    end
end
