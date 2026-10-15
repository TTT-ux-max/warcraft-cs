#include "BuyMenuView.hpp"
#include "Canvas.hpp"
#include "../combat/UnitScaling.hpp"
#include "BuyMenuLayout.hpp"
#include <cstdio>

namespace {
// Old colors map to explicit tint parameters; the canvas supplies blend/fill state itself.
constexpr float Amber[4] = {1, .72f, .22f, .95f};
constexpr float AmberStrong[4] = {1, .72f, .22f, 1};
constexpr float Red[4] = {.95f, .025f, .005f, .94f};
const char* MainLabel(int key) {
    switch (key) { case 1:return "PISTOLS";case 4:return "RIFLES";case 6:return "PRIMARY AMMO";
        case 7:return "SECONDARY AMMO";case 8:return "EQUIPMENT";default:return ""; }
}
}
namespace BuyMenuView {
void Draw(Canvas& canvas,const ShooterController& controller,int width,int height,HWND window,const Text& text,const Text& titleText,const Preview& preview) {
    if (!controller.Buying()) return;
    BuyMenuLayout box(width,height);auto page=controller.BuyPage();const auto& settings=controller.Settings();
    const char* title=page==BuyMenu::Page::Main ? "BUY MENU" : page==BuyMenu::Page::Rifles ? "BUY RIFLES (PRIMARY WEAPON)" :
        page==BuyMenu::Page::Pistols ? "BUY PISTOLS (SECONDARY WEAPON)" : "BUY EQUIPMENT";
    // Recreate the original translucent black/amber panel, sparse category keys and red hovered rows.
    canvas.FillRect(box.x,box.y,box.width,box.height,0,0,0,.77f);
    canvas.Line(box.x,box.y+box.height*.12f,box.x+box.width,box.y+box.height*.12f,1,1,.72f,.22f,.9f);
    // The category heading uses its own larger HUD font, like the original CS panel.
    titleText(box.listX,box.y+box.height*.078f,title,AmberStrong[0],AmberStrong[1],AmberStrong[2],AmberStrong[3]);
    POINT cursor={};GetCursorPos(&cursor);ScreenToClient(window,&cursor);
    int hover=BuyMenuLayout::Key(cursor.x,cursor.y,width,height,page);
    auto keys=BuyMenuLayout::Keys(page);int previewSlot=-1;
    for (size_t i=0;i<keys.size();++i) {
        int key=keys[i];float rowY=box.listY+float(i)*box.row;
        auto item=BuyCatalog::Find(BuyMenu::Category(page),key);
        bool selected=hover==key;
        if (selected) canvas.FillRect(box.listX,rowY,box.listWidth,box.row*.63f,Red[0],Red[1],Red[2],Red[3]);
        canvas.OutlineRect(box.listX,rowY,box.listWidth,box.row*.63f,Amber[0],Amber[1],Amber[2],Amber[3]);
        char label[100];sprintf_s(label,"%d %s",key,item ? item->name : MainLabel(key));
        // Long labels use familiar short gun names in the list; full names remain in the detail panel.
        if (item) sprintf_s(label,"%d %s",key,ShooterController::weapons[item->slot].name);
        text(box.listX+box.width*.009f,rowY+box.row*.43f,label,Amber[0],Amber[1],Amber[2],Amber[3]);
        if (item && (previewSlot<0 || selected)) previewSlot=item->slot;
    }
    canvas.OutlineRect(box.listX,box.cancelY,box.listWidth,box.row*.63f,Amber[0],Amber[1],Amber[2],Amber[3]);
    if (hover==0) canvas.FillRect(box.listX,box.cancelY,box.listWidth,box.row*.63f,Red[0],Red[1],Red[2],Red[3]);
    text(box.listX+box.width*.009f,box.cancelY+box.row*.43f,page==BuyMenu::Page::Main ? "0 CANCEL" : "0 BACK",AmberStrong[0],AmberStrong[1],AmberStrong[2],AmberStrong[3]);
    if (previewSlot>=0) {
        auto item=BuyCatalog::Slot(previewSlot);const auto& weapon=ShooterController::weapons[previewSlot];
        canvas.OutlineRect(box.detailX,box.previewY,box.detailWidth,box.previewHeight,Amber[0],Amber[1],Amber[2],Amber[3]);
        preview(previewSlot,box.detailX,box.previewY,box.detailWidth,box.previewHeight);
        // Show actual configurable gameplay values rather than unrelated real-world ballistics.
        char value[128];float infoY=box.previewY+box.previewHeight+box.row*.55f;
        auto line=[&](const char* label,const char* contents) {
            text(box.detailX,infoY,label,AmberStrong[0],AmberStrong[1],AmberStrong[2],AmberStrong[3]);
            text(box.detailX+box.detailWidth*.43f,infoY,contents,AmberStrong[0],AmberStrong[1],AmberStrong[2],AmberStrong[3]);infoY+=box.row*.48f;
        };
        text(box.detailX,infoY,item->name,AmberStrong[0],AmberStrong[1],AmberStrong[2],AmberStrong[3]);infoY+=box.row*.75f;
        sprintf_s(value,": %d GOLD",settings.weaponPrice[previewSlot]);line("PRICE",value);
        sprintf_s(value,": %s",item->caliber);line("CALIBER / TYPE",value);
        // Keep magazine/reserve in one row so the existing panel also fits small windows.
        sprintf_s(value,": %d / %d",weapon.magazine,settings.maxReserve[previewSlot]);
        if (!WeaponSlots::Melee(previewSlot) && previewSlot!=WeaponSlots::C4) line("CLIP / RESERVE",value);
        sprintf_s(value,": %.0f",settings.ContactDamage(previewSlot,false,controller.Status().attackDamage));line("BASE DAMAGE",value);
        if (weapon.interval>0 && !WeaponSlots::Melee(previewSlot)) {
            // Display the cadence actually used by combat, including the optional native IAS multiplier.
            float interval=UnitScaling::FireInterval(weapon.interval,settings.scaleFireRateFromAttackSpeed,controller.Status().attackSpeed);
            sprintf_s(value,": %.0f RPM",60/interval);line("RATE OF FIRE",value);
        }
        if (!WeaponSlots::Melee(previewSlot)) { sprintf_s(value,": %d GOLD",settings.ammoPrice[previewSlot]);line("AMMO PACK",value); }
        line("INVENTORY",controller.OwnsWeapon(previewSlot) ? ": OWNED" : ": NOT OWNED");
    } else {
        text(box.detailX,box.listY+box.row*.45f,"SELECT A CATEGORY",AmberStrong[0],AmberStrong[1],AmberStrong[2],AmberStrong[3]);
        text(box.detailX,box.listY+box.row*1.2f,"WEAPONS AND AMMO COST GOLD",AmberStrong[0],AmberStrong[1],AmberStrong[2],AmberStrong[3]);
        // Display the same reloaded bindings that dispatch these actions.
        auto refill=controller.Shortcut(ShortcutAction::Refill)+": FREE AMMO REFILL";
        auto all=controller.Shortcut(ShortcutAction::AllWeapons)+": GET ALL WEAPONS FREE";
        text(box.detailX,box.listY+box.row*2.1f,refill.c_str(),AmberStrong[0],AmberStrong[1],AmberStrong[2],AmberStrong[3]);
        text(box.detailX,box.listY+box.row*2.85f,all.c_str(),AmberStrong[0],AmberStrong[1],AmberStrong[2],AmberStrong[3]);
    }
    char money[160];sprintf_s(money,"GOLD: %d   |   %s / ESC: CLOSE   |   %s: CURRENT AMMO",controller.Gold(),controller.Shortcut(ShortcutAction::Buy).c_str(),controller.Shortcut(ShortcutAction::BuyAmmo).c_str());
    text(box.listX,box.y+box.height*.95f,money,AmberStrong[0],AmberStrong[1],AmberStrong[2],AmberStrong[3]);
    // Warcraft's pointer is hidden with its RTS HUD; retain a visible pointer for mouse purchases.
    const float pointer[6]={float(cursor.x),float(cursor.y),float(cursor.x+3),float(cursor.y+18),float(cursor.x+13),float(cursor.y+12)};
    canvas.TriangleFan(pointer,3,1,1,1,1);
}
}
