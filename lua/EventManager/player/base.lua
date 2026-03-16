-- 기본 플레이어 스크립트
-- 모든 플레이어에게 자동 적용되는 공통 이벤트 처리
local script = {
    id = "base",
}

-- 플레이어 초기화 시 호출
function script.onInit(owner)
    -- 필요한 초기화 작업 수행 가능
end

-- 몬스터를 처치했을 때 — 몬스터가 보유한 코인을 획득
function script.onKill(owner, target)
    if target.type == "Monster" and target.coin > 0 then
        owner:addCoin(target.coin, "drop")
        print(string.format("[player.base] %s looted %d coins from %s",
                            owner.name, target.coin, target.name))
    end
end

player_manager.register(script, {"onKill"})
