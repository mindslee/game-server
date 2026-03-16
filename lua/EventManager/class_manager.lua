-- class_manager.lua
-- 직업별 스탯 테이블. RELOAD class_manager 로 핫픽스 가능.
class_manager = {}

-- 직업·레벨별 스탯 반환
-- 반환 테이블: { maxHp, attackPower, attackSpeed, attackRange }
function class_manager.getStats(job, level)
    local base   = 100 + (level - 1) * 20
    local atkPow = 10  + (level - 1) * 2

    if job == "archer" then
        return {
            maxHp      = math.floor(base * 0.8),
            attackPower = atkPow,
            attackSpeed = 0.8,
            attackRange = 20.0,
        }
    else  -- warrior (default)
        return {
            maxHp      = base,
            attackPower = atkPow,
            attackSpeed = 1.0,
            attackRange = 5.0,
        }
    end
end
