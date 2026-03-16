local buff = {
    id=1000,
    timer={id=1, interval=1000}
}

function buff.onTimer(owner, timer_id)
    owner:AddHP(100, 'buff')
    
    local data = owner:getCustomData('buff')
    data.count = data.count + 1
end

function buff.onApplied(owner)
    return true
end

function buff.onRemoved(owner)
    return true
end

function buff.onAttack(owner, target)
    -- if target.type == "Monster" then
    owner:addCoin(10, "buff")
    
    -- print(owner.coin)
    -- end
end

function buff.onStruck(owner, target)
    owner:addCoin(-10, "buff")
end

buff_manager.register(buff, {"onAttack", "onStruck"})
