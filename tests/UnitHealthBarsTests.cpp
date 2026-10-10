#include "../src/platform/UnitHealthBars.hpp"
#include "../src/platform/SpriteTransform.hpp"
#include "../src/geometry/ModelBounds.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <limits>

namespace {
int created=0,destroyed=0;std::vector<wc3::Handle> group;
wc3::Bits FloatBits(float value) {wc3::Bits bits;std::memcpy(&bits,&value,4);return bits;}
}
namespace wc3 {
float Real(Bits bits) {float value;std::memcpy(&value,&bits,4);return value;}
uintptr_t UnitObject(Handle) {return 0;}
std::string UnitModelPath(int) {return "";}
float Ground(float,float) {return 0;}
Handle (__cdecl* GetLocalPlayer)()=[]()->Handle {return 100;};
Handle (__cdecl* CreateGroup)()=[]()->Handle {++created;return 1;};
void (__cdecl* DestroyGroup)(Handle)=[](Handle) {++destroyed;};
void (__cdecl* GroupEnumUnitsInRange)(Handle,float*,float*,float*,Handle)=[](Handle,float*,float*,float*,Handle) {group={1,2,3,4,5,6,7,8,9,10,11};};
Handle (__cdecl* FirstOfGroup)(Handle)=[](Handle)->Handle {return group.empty() ? 0 : group.back();};
void (__cdecl* GroupRemoveUnit)(Handle,Handle)=[](Handle,Handle) {group.pop_back();};
int (__cdecl* GetUnitTypeId)(Handle)=[](Handle h) {return int(h);};
Bits (__cdecl* GetUnitX)(Handle)=[](Handle) {return FloatBits(400.f);};
Bits (__cdecl* GetUnitY)(Handle)=[](Handle) {return FloatBits(0.f);};
Bits (__cdecl* GetUnitFlyHeight)(Handle)=[](Handle) {return FloatBits(0.f);};
Bits (__cdecl* GetUnitState)(Handle,int)=[](Handle h,int state) {return FloatBits(h==11 ? std::numeric_limits<float>::quiet_NaN() :
    state==1 ? (h==7 ? 0.f : 100.f) : (h==6 ? 0.f : 50.f));};
BOOL (__cdecl* IsUnitType)(Handle,int)=[](Handle h,int)->BOOL {return h==10;};
BOOL (__cdecl* IsUnitVisible)(Handle,Handle)=[](Handle h,Handle)->BOOL {return h!=5;};
BOOL (__cdecl* IsUnitHidden)(Handle)=[](Handle h)->BOOL {return h==8;};
BOOL (__cdecl* IsUnitLoaded)(Handle)=[](Handle h)->BOOL {return h==9;};
BOOL (__cdecl* IsUnitAlly)(Handle,Handle)=[](Handle h,Handle)->BOOL {return h==2 || h==10;};
BOOL (__cdecl* IsUnitEnemy)(Handle,Handle)=[](Handle h,Handle)->BOOL {return h!=2 && h!=4 && h!=10;};
}
namespace SpriteTransform {bool Read(uintptr_t,ModelTransform&) {return false;}}
namespace ModelBounds {bool Load(std::string,Bounds3&,TreeTrunkMesh*) {return false;}}
int main() {
    UnitHealthBars bars;HealthBarSettings settings;
    bars.Update(1,settings);assert(bars.Items().empty() && created==0);
    settings.allies=true;bars.Update(1,settings);assert(bars.Items().size()==2);
    // A building uses its model/fallback top, not the ordinary unit's eye height.
    bool building=false;for(const auto& bar:bars.Items()) {assert(bar.ally && bar.health==50 && bar.maximum==100);building|=bar.position[2]==232;}
    assert(building);settings.allies=false;settings.enemies=true;bars.Update(1,settings);assert(bars.Items().size()==1 && !bars.Items()[0].ally);
    settings.allies=true;bars.Update(1,settings);assert(bars.Items().size()==3);
    bars.Update(0,settings);assert(bars.Items().empty());settings.distance=0;bars.Update(1,settings);assert(bars.Items().empty());
    bars.Reset();assert(created==destroyed);std::puts("Health bar team/visibility/life filtering, anchors and group cleanup passed.");
}
