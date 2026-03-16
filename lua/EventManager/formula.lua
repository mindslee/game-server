-- formula.lua
-- 게임 공식·설정 통합 모듈. RELOAD formula 로 핫픽스 가능.
-- C++ 하드코딩 제거 → 런타임 밸런싱 조정 지원.
formula = {}

-- ============================================================================
-- P0: 전투·경험치·사망
-- ============================================================================

--------------------------------------------------------------------------------
-- 1. 크리티컬 히트
--------------------------------------------------------------------------------
-- @param player  Player userdata
-- @return { chance = 0~100(%), multiplier = 배율 }
function formula.getCritical(player)
    return {
        chance     = 15,    -- 15%
        multiplier = 2,     -- 2배
    }
end

--------------------------------------------------------------------------------
-- 2. 몬스터 공격력
--------------------------------------------------------------------------------
-- @param monster  Monster userdata
-- @return int  데미지
function formula.monsterDamage(monster)
    return math.max(1, monster.level * 5)
end

--------------------------------------------------------------------------------
-- 3. 경험치 — 킬 보상
--------------------------------------------------------------------------------
-- @param player   Player userdata (킬한 플레이어)
-- @param monster  Monster userdata (처치된 몬스터)
-- @return int  획득 경험치
function formula.expReward(player, monster)
    return monster.maxHp
end

--------------------------------------------------------------------------------
-- 4. 경험치 — 레벨업 필요량
--------------------------------------------------------------------------------
-- @param level  int  현재 레벨
-- @return int  다음 레벨까지 필요 경험치
function formula.expToLevel(level)
    return level * 100
end

--------------------------------------------------------------------------------
-- 5. 사망 페널티
--------------------------------------------------------------------------------
-- @param player  Player userdata
-- @return { resetLevel, resetExp }
--   resetLevel 음수(-1)이면 레벨 유지
function formula.deathPenalty(player)
    local newLevel = math.max(1, math.ceil(player.level / 2))  -- 레벨 절반 (올림, 최소 1)
    return {
        resetLevel = newLevel,
        resetExp   = 0,
    }
end

-- ============================================================================
-- P0.6: 몬스터 아이템 드롭 테이블
-- ============================================================================

--------------------------------------------------------------------------------
-- 6. 드롭 테이블
--------------------------------------------------------------------------------
-- @param monster  Monster userdata
-- @return array of { itemId, chance(0~100), minQty, maxQty }
function formula.getDropTable(monster)
    local drops = {
        { itemId = 1000, chance = 15, minQty = 1, maxQty = 1 },  -- 소형 체력물약 15%
        { itemId = 1001, chance = 8,  minQty = 1, maxQty = 1 },  -- 중형 체력물약 8%
        { itemId = 1002, chance = 3,  minQty = 1, maxQty = 1 },  -- 대형 체력물약 3%
    }
    -- 엘리트 몬스터는 드롭률 2배
    if monster.isElite then
        for _, d in ipairs(drops) do
            d.chance = math.min(100, d.chance * 2)
        end
    end
    return drops
end

-- ============================================================================
-- P0.5: 공격 기여도 · 경험치 분배
-- ============================================================================

--------------------------------------------------------------------------------
-- 6. 기여도 설정
--------------------------------------------------------------------------------
-- @return { expireSec, tankingWeight, minShareRatio, partyBonus }
function formula.getContributionConfig()
    return {
        expireSec      = 30.0,  -- 기여도 만료 시간 (초). 이 시간 내 공격/탱킹 없으면 기여 소멸
        tankingWeight  = 0.5,   -- 탱킹 기여를 데미지 환산할 때의 가중치 (흡수 1 = 데미지 0.5)
        minShareRatio  = 0.10,  -- 참여자 최소 경험치 비율 (10%)
        partyBonus     = {      -- 참여 인원에 따른 총 EXP 배율
            [1] = 1.0,
            [2] = 1.2,
            [3] = 1.4,
            [4] = 1.5,          -- 4인 이상 동일
        },
    }
end

--------------------------------------------------------------------------------
-- 7. 경험치 분배
--------------------------------------------------------------------------------
-- C++ 에서 기여도 비율을 계산하여 contributions 배열로 넘겨준다.
-- @param totalExp       int    몬스터 처치 기본 경험치 (formula.expReward 결과)
-- @param contributions  array  { {playerId, damage, tanking, ratio}, ... }
-- @param config         table  getContributionConfig() 결과
-- @return array of { playerId, exp }
function formula.expDistribute(totalExp, contributions, config)
    local numPlayers = #contributions
    if numPlayers == 0 then return {} end

    -- 참여 인원 보너스 배율
    local bonusTable = config.partyBonus or {}
    local bonus = bonusTable[numPlayers] or bonusTable[4] or 1.5
    if numPlayers > 4 then bonus = bonusTable[4] or 1.5 end
    local boostedExp = math.floor(totalExp * bonus)

    local minShare = config.minShareRatio or 0.10
    local result = {}

    if numPlayers == 1 then
        -- 솔로: 보너스 없이 전체 지급
        table.insert(result, {
            playerId = contributions[1].playerId,
            exp = totalExp,
        })
        return result
    end

    -- 최소 보장 비율 적용: 각 기여자에게 minShare 보장 후 나머지를 비율 분배
    local reservedRatio = minShare * numPlayers
    local poolRatio     = math.max(0, 1.0 - reservedRatio)

    for _, c in ipairs(contributions) do
        local share = minShare + poolRatio * c.ratio
        local expGain = math.floor(boostedExp * share)
        table.insert(result, {
            playerId = c.playerId,
            exp = math.max(1, expGain),  -- 최소 1 보장
        })
    end

    return result
end

-- ============================================================================
-- P1: 몬스터 스폰·리스폰·AI
-- ============================================================================

--------------------------------------------------------------------------------
-- 8. 초기 몬스터 스폰 테이블
--------------------------------------------------------------------------------
-- 서버 시작 시 1회 호출. RELOAD formula 후 다음 서버 시작 시 반영.
-- @return array of { id, name, level, x, y }
function formula.getSpawnTable()
    return {
        { id = 2000, name = "Goblin",      level = 1, x = 20, y = 20 },
        { id = 2001, name = "Orc",         level = 1, x = 50, y = 50 },
        { id = 2002, name = "Dark Knight", level = 1, x = 80, y = 80 },
    }
end

--------------------------------------------------------------------------------
-- 9. 리스폰 설정
--------------------------------------------------------------------------------
-- @return { minMs, maxMs, warningMs, eliteChance, splitChance }
function formula.getRespawnConfig()
    return {
        minMs        = 4000,   -- 최소 리스폰 딜레이 (ms)
        maxMs        = 7000,   -- 최대 리스폰 딜레이 (ms)
        warningMs    = 1000,   -- 리스폰 예고 선행 시간 (ms)
        eliteChance  = 10,     -- 엘리트 확률 (%)
        splitChance  = 20,     -- 군집 분열 확률 (%)
        maxMonsters  = 9,      -- 몬스터 인구 상한
        homeRadius   = 20.0,   -- 구역 귀환 반경
    }
end

--------------------------------------------------------------------------------
-- 10. 리스폰 시 레벨 변동
--------------------------------------------------------------------------------
-- @param baseLevel  int  원래 레벨
-- @param currentLevel  int  현재 레벨
-- @return int  새 레벨
function formula.respawnLevel(baseLevel, currentLevel)
    local roll = math.random(0, 9)
    local delta
    if roll < 4 then      -- 40% 확률: +1
        delta = 1
    elseif roll < 7 then  -- 30% 확률: 유지
        delta = 0
    else                  -- 30% 확률: -1
        delta = -1
    end
    local newLevel = currentLevel + delta
    return math.max(1, math.min(baseLevel + 5, newLevel))
end

--------------------------------------------------------------------------------
-- 11. 몬스터 기본 스탯 (HP / 코인)
--------------------------------------------------------------------------------
-- @param level    int   몬스터 레벨
-- @param isElite  bool  엘리트 여부
-- @return { maxHp, coin }
function formula.monsterBaseStats(level, isElite)
    local baseHp   = 100 + (level - 1) * 20
    local baseCoin = level * 10
    if isElite then
        return { maxHp = baseHp * 2, coin = baseCoin * 3 }
    else
        return { maxHp = baseHp, coin = baseCoin }
    end
end

--------------------------------------------------------------------------------
-- 12. 몬스터 AI 파라미터
--------------------------------------------------------------------------------
-- @param monster  Monster userdata
-- @return { aggroSpeed, attackSpeed }
function formula.monsterAI(monster)
    return {
        aggroSpeed  = 4.0,   -- 추격 이동 속도 (units/sec)
        attackSpeed = 0.8,   -- 초당 공격 횟수
    }
end

-- ============================================================================
-- P2: 맵·리젠·서버 설정
-- ============================================================================

--------------------------------------------------------------------------------
-- 13. 맵 설정
--------------------------------------------------------------------------------
-- @return { width, height }
function formula.getMapConfig()
    return {
        width  = 100.0,
        height = 100.0,
    }
end

--------------------------------------------------------------------------------
-- 14. HP 자동 회복
--------------------------------------------------------------------------------
-- @return { intervalSec }
function formula.getRegenConfig()
    return {
        intervalSec = 5.0,   -- HP 회복 주기 (초)
    }
end

--------------------------------------------------------------------------------
-- 15. 게임 루프
--------------------------------------------------------------------------------
-- @return { tickMs }
function formula.getTickConfig()
    return {
        tickMs = 50,   -- 게임 루프 주기 (ms) — 20 TPS
    }
end

return formula
