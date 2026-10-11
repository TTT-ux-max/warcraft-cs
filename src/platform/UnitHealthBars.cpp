#include "UnitHealthBars.hpp"
#include "SpriteTransform.hpp"
#include "../geometry/ModelBounds.hpp"
#include <algorithm>
#include <cmath>

void UnitHealthBars::Anchor(wc3::Handle unit,float* position) {
    int type=wc3::GetUnitTypeId(unit);
    auto found=models_.find(type);
    if(found==models_.end()) {
        Bounds3 bounds;
        if(wc3::IsUnitType(unit,2)) bounds={{-110,-110,0},{110,110,220}};
        ModelBounds::Load(wc3::UnitModelPath(type),bounds);
        found=models_.emplace(type,bounds).first;
    }
    uintptr_t object=wc3::UnitObject(unit),sprite=0;SIZE_T got=0;
    if(object) ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(object+0x28),&sprite,sizeof(sprite),&got);
    ModelTransform transform;
    if(sprite && SpriteTransform::Read(sprite,transform)) {
        // Use the rendered model's highest transformed corner, including scale, tilt and flight.
        position[0]=transform.position[0];position[1]=transform.position[1];position[2]=-INFINITY;
        const auto& bounds=found->second;
        for(int corner=0;corner<8;++corner) {
            float height=transform.position[2];
            for(int axis=0;axis<3;++axis) height+=transform.matrix[6+axis]*
                ((corner&(1<<axis)) ? bounds.maximum[axis] : bounds.minimum[axis]);
            position[2]=std::max(position[2],height);
        }
    } else position[2]=wc3::Ground(position[0],position[1])+wc3::Real(wc3::GetUnitFlyHeight(unit))+found->second.maximum[2];
    position[2]+=12; // Leave a small world-space gap above the model instead of covering its head.
}
void UnitHealthBars::Update(wc3::Handle actor,const HealthBarSettings& settings) {
    items_.clear();
    if(!actor || (!settings.allies && !settings.enemies) || settings.distance<=0) return;
    float x=wc3::Real(wc3::GetUnitX(actor)),y=wc3::Real(wc3::GetUnitY(actor)),radius=settings.distance;
    auto player=wc3::GetLocalPlayer(),group=wc3::CreateGroup();
    wc3::GroupEnumUnitsInRange(group,&x,&y,&radius,0);
    wc3::Handle unit;
    while((unit=wc3::FirstOfGroup(group))!=0) {
        wc3::GroupRemoveUnit(group,unit);
        // Keep map visibility intact: dead, hidden, transported and fog-hidden units have no bar.
        if(unit==actor || !wc3::GetUnitTypeId(unit) || wc3::IsUnitHidden(unit) || wc3::IsUnitLoaded(unit) ||
            !wc3::IsUnitVisible(unit,player)) continue;
        bool ally=wc3::IsUnitAlly(unit,player)!=FALSE;
        if(ally ? !settings.allies : (!settings.enemies || !wc3::IsUnitEnemy(unit,player))) continue;
        UnitHealthBar item;
        item.health=wc3::Real(wc3::GetUnitState(unit,0));item.maximum=wc3::Real(wc3::GetUnitState(unit,1));item.ally=ally;
        if(!std::isfinite(item.health) || !std::isfinite(item.maximum) || item.health<=.405f || item.maximum<=0) continue;
        item.position[0]=wc3::Real(wc3::GetUnitX(unit));item.position[1]=wc3::Real(wc3::GetUnitY(unit));
        Anchor(unit,item.position);items_.push_back(item);
    }
    wc3::DestroyGroup(group);
}
