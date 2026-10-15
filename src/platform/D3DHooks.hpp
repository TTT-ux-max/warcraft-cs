#pragma once
#include <windows.h>
#include <functional>

class Canvas;

// Platform adapter that routes the FPS HUD into Direct3D 9. Warcraft can render through
// OpenGL or Direct3D 9; this module hooks the Direct3DCreate9 export, the returned factory's
// CreateDevice vtable slot and the device Present/Reset slots, so the same overlay draws
// through the D3D canvas without touching the OpenGL path. Only one renderer is active per
// frame, so the GL present anchor and these hooks never race.
namespace D3DHooks {
// Called from the device Present hook before the real Present flips the back buffer.
using PresentCallback = std::function<void(Canvas&, HWND, int, int)>;
// Called for every projection transform the game sets, with the matrix (16 floats, row major)
// and the viewport that is active at that moment, so the caller can keep only the world pass.
using ProjectionCallback = void(*)(const float* matrix, const int* viewport);
// Loads d3d9 and hooks device creation. Safe on renders that never use D3D: the hook stays
// inert unless the game actually creates a Direct3D device.
bool Install(PresentCallback present, ProjectionCallback projection);
bool Installed();
}
