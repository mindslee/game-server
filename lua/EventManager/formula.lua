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
--   음수(-1)이면 해당 값 유지
function formula.deathPenalty(player)
    return {
        resetLevel = 1,
        resetExp   = 0,
    }
end

-- ============================================================================
-- P1: 몬스터 스폰·리스폰·AI
-- ============================================================================

--------------------------------------------------------------------------------
-- 6. 초기 몬스터 스폰 테이블
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
-- 7. 리스폰 설정
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
-- 8. 리스폰 시 레벨 변동
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
-- 9. 몬스터 기본 스탯 (HP / 코인)
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
-- 10. 몬스터 AI 파라미터
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
-- 11. 맵 설정
--------------------------------------------------------------------------------
-- @return { width, height }
function formula.getMapConfig()
    return {
        width  = 100.0,
        height = 100.0,
    }
end

--------------------------------------------------------------------------------
-- 12. HP 자동 회복
--------------------------------------------------------------------------------
-- @return { intervalSec }
function formula.getRegenConfig()
    return {
        intervalSec = 5.0,   -- HP 회복 주기 (초)
    }
end

--------------------------------------------------------------------------------
-- 13. 게임 루프
--------------------------------------------------------------------------------
-- @return { tickMs }
function formula.getTickConfig()
    return {
        tickMs = 50,   -- 게임 루프 주기 (ms) — 20 TPS
    }
end

return formula
