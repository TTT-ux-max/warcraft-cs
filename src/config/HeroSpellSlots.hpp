#pragma once
#include <array>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>

// Parse only slot metadata from the owner's archives; game tables are never bundled with the mod.
class HeroSpellSlots {
public:
    void LoadUnits(const std::string& slk);
    void LoadPositions(const std::string& text);
    std::array<int,4> ForUnit(int type,const std::function<bool(int)>& learned={}) const;
    bool Passive(int ability) const { return passive_.count(ability)!=0; }
    static int Rawcode(const std::string& text);
private:
    std::unordered_map<int,std::vector<int>> units_;
    std::unordered_map<int,int> positions_;
    std::unordered_set<int> passive_;
};
