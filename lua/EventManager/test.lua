require('event_manager')
require('item_manager')
require('buff_manager')
require('quest_manager')
require('monster_manager')

require('item.1000')

require('buff.1000')

require('quest.1000')


function tprint(t)
    for k, v in pairs(t) do
        print(k, v)
    end
end

function reload(name)
    package.loaded[name] = nil
    require(name)
end

player1 = {
    name = 'player1',
    level = 10,
    gender = 0,
    coin = 0
}

function player1:addCoin(coin, source)
    self.coin = self.coin + coin
    print('addCoin called by '..source..', recent coin='..self.coin)
end

function player1:addQuestData(quest_id, param_id, value)

end