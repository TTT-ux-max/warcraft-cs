#include "FullscreenView.hpp"

namespace {
    // Console strip below the world viewport, in client pixels. Observed from the engine's
    // own world viewport submission, so it follows resolution and layout changes.
    int consoleStrip = 0;
}

void FullscreenView::Expand(int clientWidth, int clientHeight, int& x, int& y, int& width, int& height) {
    // Warcraft submits its world separately from small portrait/minimap viewports.
    // Fullscreen UI rectangles remain identical; smaller native viewports stay intact.
    if (clientWidth<=0 || clientHeight<=0 || width<=clientWidth*3/4 || height<=clientHeight/2) return;
    x=y=0; width=clientWidth; height=clientHeight;
}

void FullscreenView::ObserveWorldViewport(int clientWidth, int clientHeight, int x, int y, int width, int height) {
    // The world viewport is bottom-anchored above the console: everything below its GL y
    // offset belongs to the native console chrome. Sanity-check the shape before trusting it.
    if (clientWidth<=0 || clientHeight<=0 || x<0 || width<=clientWidth*3/4 || height<=clientHeight/3) return;
    if (y<=0 || y+height>clientHeight) return;
    consoleStrip = y;
}

int FullscreenView::ConsoleStrip() { return consoleStrip; }
