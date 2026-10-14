#pragma once

// Expand the main world rectangle without moving native UI/portrait frame anchors.
class FullscreenView {
public:
    static void Expand(int clientWidth, int clientHeight, int& x, int& y, int& width, int& height);
    // Record the pre-expansion world viewport the engine submitted; its bottom offset is the
    // native console strip height, which gates console-batch filtering at GL submission.
    static void ObserveWorldViewport(int clientWidth, int clientHeight, int x, int y, int width, int height);
    // Bottom strip in client pixels; 0 until a world viewport has been observed.
    static int ConsoleStrip();
};
