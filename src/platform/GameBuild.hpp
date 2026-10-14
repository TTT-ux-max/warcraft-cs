#pragma once
#include <cstdint>
#include <initializer_list>

namespace wc3 {
// Warcraft III builds differ in every hard-coded address: the JASS native table is
// recompiled, the engine internals are re-laid out, and float code generation moved
// from x87 to SSE. Only the object layouts survived. Addresses therefore live in a
// per-build translation table instead of being baked into each adapter.
enum class GameBuildKind { Unsupported, Classic126a, Classic127a };

// Classify the loaded Game.dll. Sets the active build used by Rva().
GameBuildKind SelectBuild(void* module);
GameBuildKind ActiveBuild();
const char* ActiveBuildName();
// True once Bind() accepted the module and the native table has been populated.
bool IsBound();
// Recorded by Bind() after every native pointer has been installed.
void MarkBound();

// Translate a 1.26a relative virtual address into the active build's RVA.
// Returns 0 when this build has no verified address for that legacy offset. A zero
// result is a deliberate "feature unavailable" signal: callers must not fall back to
// the legacy value, because 1.26a and 1.27a reuse the same numeric ranges for
// completely different functions.
uintptr_t Rva(uintptr_t legacy126a);
// True when every listed legacy RVA resolves in the active build.
bool RvaAvailable(std::initializer_list<uintptr_t> legacy126a);
}
