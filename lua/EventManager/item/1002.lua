-- 체력 회복 물약 (대형)
local item = {
    id    = 1002,
    name  = "대형 체력물약",
    price = 500,
    heal  = 150,
}

function item.canUse(owner)
    return true
end

function item.useItem(owner)
    owner:addHP(item.heal, item.name)
    print(string.format("[item.%d] %s used %s (+%d HP)",
          item.id, owner.name, item.name, item.heal))
end

function item.getInfo()
    return {
        id    = item.id,
        name  = item.name,
        price = item.price,
        desc  = "HP " .. item.heal .. " 회복",
    }
end

item_manager.register(item)
