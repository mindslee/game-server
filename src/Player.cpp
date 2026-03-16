#include "Player.h"
#include <algorithm>
#include <iostream>

Player::Player(int id, const std::string& name, int level, int gender)
    : Entity(id, name, "Player", level, 100, 100, 0, 15.f)  // speed = 15 units/sec
    , gender(gender)
{
    hpRegen = 10;
    lastRegenTime = std::chrono::steady_clock::now();
}

void Player::addCoin(int amount, const std::string& source) {
    coin += amount;
    std::cout << "[Player:" << name << "] addCoin(" << amount
              << ") from '" << source << "', total=" << coin << "\n";
}

void Player::addHP(int amount, const std::string& source) {
    hp = std::min(hp + amount, maxHp);
    std::cout << "[Player:" << name << "] AddHP(" << amount
              << ") from '" << source << "', hp=" << hp << "/" << maxHp << "\n";
}

void Player::setQuestData(int questId, int paramId, int value) {
    questData[questId][paramId] = value;
    std::cout << "[Player:" << name << "] setQuestData"
              << " quest=" << questId << " param=" << paramId << " value=" << value << "\n";
}

void Player::addQuestData(int questId, int paramId, int value) {
    questData[questId][paramId] += value;
    std::cout << "[Player:" << name << "] addQuestData"
              << " quest=" << questId << " param=" << paramId
              << " +=" << value << " total=" << questData[questId][paramId] << "\n";
}

int Player::getQuestData(int questId, int paramId) const {
    auto qi = questData.find(questId);
    if (qi != questData.end()) {
        auto pi = qi->second.find(paramId);
        if (pi != qi->second.end()) return pi->second;
    }
    return 0;
}

void Player::removeQuestData(int questId) {
    questData.erase(questId);
}

void Player::addItem(int itemId, int count) {
    inventory[itemId] += count;
    std::cout << "[Player:" << name << "] addItem(" << itemId << " x" << count
              << "), total=" << inventory[itemId] << "\n";
}

bool Player::removeItem(int itemId, int count) {
    auto it = inventory.find(itemId);
    if (it == inventory.end() || it->second < count) return false;
    it->second -= count;
    if (it->second <= 0) inventory.erase(it);
    std::cout << "[Player:" << name << "] removeItem(" << itemId << " x" << count << ")\n";
    return true;
}

int Player::getItemCount(int itemId) const {
    auto it = inventory.find(itemId);
    return it != inventory.end() ? it->second : 0;
}

void Player::printStatus() const {
    std::cout << "=== Player: " << name << " (ID=" << id << ") ===\n"
              << "  Level=" << level << " Gender=" << gender << "\n"
              << "  HP=" << hp << "/" << maxHp << "  Coin=" << coin << "\n"
              << "  Pos=" << pos.toString() << "\n";
    if (!inventory.empty()) {
        std::cout << "  Inventory:";
        for (auto& [id, cnt] : inventory)
            std::cout << " [" << id << "]x" << cnt;
        std::cout << "\n";
    }
}
