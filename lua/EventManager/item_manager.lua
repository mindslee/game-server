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

-- 상점 목록: getInfo()가 있는 아이템 중 price > 0 인 것
function item_manager.getShopList()
    local list = {}
    for id, item in pairs(items) do
        if item.getInfo then
            local info = item.getInfo()
            if info.price and info.price > 0 then
                table.insert(list, info)
            end
        end
    end
    table.sort(list, function(a, b) return a.id < b.id end)
    return list
end

-- 아이템 정보 조회
function item_manager.getInfo(item_id)
    local item = item_manager.get(item_id)
    if item and item.getInfo then
        return item.getInfo()
    end
    return nil
end
