#include "HeroSpellSlots.hpp"
#include <algorithm>
#include <sstream>
#include <cstdlib>
#include <cctype>

int HeroSpellSlots::Rawcode(const std::string& text) {
    if(text.size()!=4)return 0;
    unsigned result=0;for(unsigned char c:text)result=(result<<8)|c;return int(result);
}
void HeroSpellSlots::LoadUnits(const std::string& slk) {
    // SYLK cells inherit X/Y coordinates; select named columns rather than assuming table widths.
    units_.clear();int x=1,y=1,idColumn=0,listColumn=0;
    std::unordered_map<int,std::unordered_map<int,std::string>> rows;
    std::istringstream input(slk);std::string line;
    while(std::getline(input,line)) {
        if(line.rfind("C;",0)!=0)continue;
        std::istringstream cells(line.substr(2));std::string cell,value;bool hasValue=false;
        while(std::getline(cells,cell,';')) {
            if(cell.empty())continue;
            if(cell[0]=='X')x=std::atoi(cell.c_str()+1);
            if(cell[0]=='Y')y=std::atoi(cell.c_str()+1);
            if(cell[0]=='K') {value=cell.substr(1);while(!value.empty() && (value.back()=='\r' || value.back()=='\n'))value.pop_back();
                if(value.size()>=2 && value.front()=='"' && value.back()=='"')value=value.substr(1,value.size()-2);hasValue=true;}
        }
        if(hasValue && x>0 && x<128 && y>0 && y<20000)rows[y][x]=value;
    }
    for(const auto& c:rows[1]) {if(c.second=="unitAbilID")idColumn=c.first;if(c.second=="heroAbilList")listColumn=c.first;}
    if(!idColumn || !listColumn)return;
    for(const auto& row:rows) {
        const auto& cells=row.second;auto id=cells.find(idColumn),list=cells.find(listColumn);
        if(id==cells.end() || list==cells.end())continue;
        // Custom heroes can list stats and ultimate first, with a primary skill after the fourth entry.
        std::vector<int> slots;std::istringstream ids(list->second);std::string raw;
        while(slots.size()<128 && std::getline(ids,raw,','))slots.push_back(Rawcode(raw));
        if(int type=Rawcode(id->second))units_[type]=slots;
    }
}
void HeroSpellSlots::LoadPositions(const std::string& text) {
    // Research button columns give the stable four hero slots, including unlearned/passive gaps.
    std::istringstream input(text);std::string line;int id=0;
    while(std::getline(input,line)) {
        if(!line.empty() && line[0]=='[' && line.size()>=6)id=Rawcode(line.substr(1,4));
        if(id && line.rfind("Art=",0)==0) {
            // Unlearned slots still need their stock/map icon when no live ability object exists yet.
            auto icon=line.substr(4);icon.resize(icon.find_first_of(",\r\n") == std::string::npos ? icon.size() : icon.find_first_of(",\r\n"));
            icons_[id]=icon;
            // Stock passive buttons can expose a native order ID that accepts a no-op; keep their slots inert.
            auto art=line.substr(4);for(auto& c:art)c=char(std::tolower(static_cast<unsigned char>(c)));
            if(art.find("\\passivebuttons\\")!=std::string::npos)passive_.insert(id);else passive_.erase(id);
        }
        if(id && line.rfind("Researchbuttonpos=",0)==0) {
            const char* first=line.c_str()+18;char* end=nullptr;long column=std::strtol(first,&end,10);
            // Remember extra/hidden columns too; they must not fall back into the four spell controls.
            if(end!=first && *end==',' && column>=-16 && column<16)positions_[id]=int(column);
        }
    }
}
std::array<int,4> HeroSpellSlots::ForUnit(int type,const std::function<bool(int)>& learned) const {
    auto found=units_.find(type);if(found==units_.end())return {};
    std::array<int,4> result={};
    // Explicit research positions win over list order, even when their abilities appear fifth or later.
    for(int ability:found->second) {
        auto position=positions_.find(ability);
        if(!ability || position==positions_.end() || position->second<0 || position->second>=4)continue;
        auto& selected=result[size_t(position->second)];
        // Maps can replace a skill with another ID in the same column; prefer the one actually learned.
        if(!selected || (learned && !learned(selected) && learned(ability)))selected=ability;
    }
    // Preserve ordinary list-order gaps, then fit unknown entries around the declared map buttons.
    for(size_t i=0;i<found->second.size();++i) {
        int ability=found->second[i];if(!ability || positions_.count(ability))continue;
        auto empty=std::find(result.begin(),result.end(),0);
        if(empty==result.end())break;
        if(i<4 && !result[i])result[i]=ability;else *empty=ability;
    }
    return result;
}
