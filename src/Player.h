#pragma once
#include <map>
#include "Entity.h"

class Player : public Entity {
public:
    int         gender;          // 0=male, 1=female
    std::string job = "warrior"; // "warrior" | "archer"

    // quest_id -> (param_id -> value)
    std::map<int, std::map<int, int>> questData;

    int exp = 0;  // 누적 경험치

    // 인벤토리: itemId → 수량
    std::map<int, int> inventory;

    std::chrono::steady_clock::time_point lastRegenTime;  // 마지막 HP 회복 시각

    Player(int id, const std::string& name, int level = 1, int gender = 0);

    void addCoin(int amount, const std::string& source);
    void addHP(int amount, const std::string& source);
    void setQuestData(int questId, int paramId, int value);
    void addQuestData(int questId, int paramId, int value);
    int  getQuestData(int questId, int paramId) const;
    void removeQuestData(int questId);

    // 인벤토리 관리
    void addItem(int itemId, int count = 1);
    bool removeItem(int itemId, int count = 1);  // 수량 부족 시 false
    int  getItemCount(int itemId) const;

    void printStatus() const;
};
