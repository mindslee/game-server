local item = {
    id = 1000,
}

function item.canUse(owner)
    if owner.level < 10 then
        return false
    end

    if owner.gender == 1 then
        return false
    end

    return true
end

function item.useItem(owner)
    -- local o = math.random(1, 100)
    -- owner:AddHp(100)

    -- owner:AddBuff(1000)
    print(item.__name.." used")
end

function item.onTimer(owner, timer)

end

item_manager.register(item)