#include "../src/presentation/MinimapLayout.hpp"
#include "../src/presentation/HudHints.hpp"
#include <cassert>
#include <limits>

int main() {
    // Bounds must remain square and on screen across supported aspect ratios and tiny windows.
    for (auto size : {std::pair<int,int>{1024,768},{1920,1080},{3440,1440},{640,480},{16,16}}) {
        for (float percent : {10.f,22.f,40.f,std::numeric_limits<float>::quiet_NaN()}) {
            auto layout=MinimapLayout::Calculate(size.first,size.second,percent);
            assert(layout.side>0 && layout.x>=0 && layout.y>=0);
            assert(layout.x+layout.side<=size.first && layout.y+layout.side<=size.second);
        }
    }
    assert(MinimapLayout::Calculate(1920,1080,22).side==238);
    assert(!MinimapLayout::Calculate(0,0,22).side);
    // Every shortcut remains present after wrapping; long tokens and one-column windows cannot loop.
    for (int columns : {1,12,50,90}) {
        auto rows=HudHints::Wrap("F6: RTS | M: MINIMAP | F8: CONFIG",columns);
        std::string joined;for (const auto& row : rows) {assert(!row.empty() && row.size()<=size_t(columns));joined+=row;}
        for (const char* key : {"F6:","M:","F8:"}) {
            assert(joined.find(key)!=std::string::npos);
        }
    }
    assert(HudHints::Wrap("A | B | C",9).size()==1);
    assert(HudHints::Wrap("A | B | C",5).size()==2);
}
