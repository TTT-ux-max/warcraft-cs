#include "../src/config/HeroSpellSlots.hpp"
#include "../src/input/ShortcutBindings.hpp"
#include <cassert>
#include <cstdio>

int main() {
    ShortcutBindings keys;assert(keys.Valid());
    assert(keys.Key(ShortcutAction::Spell1)=='Z' && keys.Key(ShortcutAction::Spell4)=='V' && keys.Key(ShortcutAction::Speed)=='P');
    assert(ShortcutBindings::Parse(" q ")=='Q' && ShortcutBindings::Parse("control")==VK_CONTROL);
    assert(ShortcutBindings::Parse("F12")==VK_F12 && ShortcutBindings::Parse("F13")==-1 && ShortcutBindings::Parse("CTRL+Q")==-1);
    keys.Set(ShortcutAction::Spell1,'X');assert(!keys.Valid());keys.Set(ShortcutAction::Spell2,'Z');assert(keys.Valid());
    keys.Set(ShortcutAction::Speed,0);assert(keys.Valid());keys.Set(ShortcutAction::Spell3,VK_F6);assert(!keys.Valid());
    keys=ShortcutBindings();keys.Set(ShortcutAction::ToggleFPS,0);assert(!keys.Valid());
    // Deliberately reordered SYLK columns and inherited coordinates exercise real metadata parsing.
    HeroSpellSlots slots;
    slots.LoadUnits("ID;P\r\nC;X3;Y1;K\"heroAbilList\"\r\nC;X1;K\"unitAbilID\"\r\nC;Y2;K\"H000\"\r\nC;X3;K\"A003,A002,A001,A004\"\r\n");
    slots.LoadPositions("[A001]\nResearchbuttonpos=0,0\n[A002]\nResearchbuttonpos=1,0\n[A003]\nResearchbuttonpos=2,0\n[A004]\nResearchbuttonpos=3,0\n");
    auto spells=slots.ForUnit(HeroSpellSlots::Rawcode("H000"));
    assert(spells[0]==HeroSpellSlots::Rawcode("A001") && spells[2]==HeroSpellSlots::Rawcode("A003"));
    assert((slots.ForUnit(123)==std::array<int,4>{}));
    // Passive stock artwork overrides the misleading native no-op order (for example, Bash).
    slots.LoadPositions("[A003]\nArt=ReplaceableTextures\\PassiveButtons\\PASBTNTest.blp\n");
    assert(slots.Passive(HeroSpellSlots::Rawcode("A003")) && !slots.Passive(HeroSpellSlots::Rawcode("A001")));
    slots.LoadPositions("[A003]\nArt=ReplaceableTextures\\CommandButtons\\BTNTest.blp\n");
    assert(!slots.Passive(HeroSpellSlots::Rawcode("A003")));
    slots.LoadUnits("broken");assert((slots.ForUnit(HeroSpellSlots::Rawcode("H000"))==std::array<int,4>{}));
    std::puts("PASS shortcut parsing/conflicts/swaps and owned-table hero spell slot metadata.");
}
