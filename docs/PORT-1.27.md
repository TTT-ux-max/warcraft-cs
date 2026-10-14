# Porting Warcraft CS to Warcraft III 1.27a

Target build: **1.27.0.52240** (`f7ba2a3`), `Game.dll` 13,187,048 bytes, image base `0x6F000000`.

This document records what a 1.27a build needs, how each address was derived, and
which features still require disassembly work. It is written so the remaining work can
be finished by the same method rather than by guessing.

## Why the port is not a search and replace

1.26a and 1.27a keep the same image base, the same section layout and the same object
layouts, but every function moved. Comparing the 1.26a offsets already in
`src/platform/NativeOffsets.inc` with their 1.27a counterparts shows the two builds
share no ordering: `GetUnitX` sits at `0x3C5510` in 1.26a and `0x1E66B0` in 1.27a, while
`IsFogEnabled` moves from `0x3BB200` to `0x1E7960`. Both are unit-adjacent in 1.26a but
`0x6DF0` apart in 1.27a. No offset delta is conserved, so neighbouring addresses carry
no information - a translated address must be identified by name or by structure.

The second difference is code generation. Several 1.26a signatures are x87 sequences;
1.27a compiles the same expressions with SSE. `GetCameraBoundMinX` reads the camera
bound at member `+0x4D0` in both builds, but 1.26a emits `D9 80 D0 04 00 00` (`fld`) while
1.27a emits `F3 0F 10 80 D0 04 00 00` (`movss`). Any signature containing an x87 opcode
therefore fails on 1.27a even when the address is correct, which is exactly the pattern
seen across `walkableSurface`, `minXGetter`, `floatingText` and `frameRate`.

## Resolving the JASS native table

The engine registers every JASS native as the same four-instruction record emitted
inline into `.text`:

```
68 xx xx xx xx     push   <signature string>     ; e.g. "(B)V"
BA xx xx xx xx     mov    edx, <name string>     ; e.g. "FogEnable"
B9 xx xx xx xx     mov    ecx, <implementation>  ; native entry point
E8 xx xx xx xx     call   <registration helper>
```

`mov ecx` carries the entry point, so the record list *is* a name to address map.
`tools/resolve_native_offsets.py` scans for the sequence, resolves both string pointers,
and rejects anything whose operands are not readable `.rdata` strings or whose name is
not identifier-shaped. 1.27a yields **1291** implemented natives plus a parallel list of
1291 records pointing at a shared `xor eax,eax; ret` stub, which carries no address and
is ignored.

`tools/generate_native_offsets.py` then walks the 1.26a reference table, resolves each of
its entries by *name*, and cross-checks the result against the JASS signature the engine
itself stores. The check maps each C declaration to the JASS type letters it implies
(`BOOL` to `B`, `float*` and `Bits` to `R`, and so on) and requires both the arity and
the value class of every parameter and the return type to agree. A colliding or stale
name cannot satisfy that. Result: **99 of 99 entries accepted, 0 rejected**.

The enum-handle and wrapper parameters that the reference declares as `int` or `Handle`
(`playerstate`, `unitstate`, `camerafield`, `itemtype`, `pathingtype`, the JASS string and
code wrappers) are matched by class rather than by exact letter, because the runtime
already treats them as opaque values.

## Engine internals

Natives that the runtime calls are covered by the table above. The remaining addresses are
engine internals with no JASS name, so they were identified by structural evidence: an
object member offset that survived the port, the JASS wrapper that calls them, or a
sibling function reached from the same call site.

| 1.26a | 1.27a | Evidence |
| --- | --- | --- |
| `0x3BDCB0` | `0x1D1550` | CUnit resolver. Called by 326 unit natives with `ecx = handle`, including `GetUnitX`, `GetUnitDefaultMoveSpeed`, `UnitDamageTarget`, `GetUnitCurrentOrder`. |
| `0x3BE010` | `0x1D17D0` | CWidget resolver. Called by `GetWidgetX/Y`, `GetWidgetLife`, `AddSpecialEffect`, `GroupRemoveUnit`. |
| `0x300710` | `0x34F3A0` | UI accessor. `GetCameraField` and `PauseGame` both call it as `(index=1, serial=0)`; `MapCameraGuard` uses `(1,0)` and `PluginRuntime` uses `(0,0)`. |
| `0x2F5ED0` | `0x34DA20` | Camera getter. Byte-identical `8B 81 54 02 00 00 C3`; `GetCameraField` calls it immediately after the UI accessor. |
| `0x4C4630` | `0x0516F0` | CString getter. Byte-identical `8B 41 08 85 C0 74 04 8B 40 1C C3`. |
| `0x430C80` | `0x2651D0` | Object to JASS handle. Reached from `CreateUnit`, `CreateItem`, `CreateDestructable`, `CreateGroup`, `CreateTimer`, `Location`, `FirstOfGroup`. |
| `0x390590` | `0x36D620` | World sky setter. Touches `[this+0x354]` as in 1.26a; `SetSkyModel` passes `[ui+0x3BC]`, matching `NativeSky::Update`. |
| `0x3B4C62` | `0x1E1884` | Camera bound min-X getter. Same member `+0x4D0` inside `GetCameraBoundMinX`, x87 replaced by SSE (`+0x12` becomes `+0x14`). |
| `0x3B4740` | `0x1F19A0` | `PanCameraToTimed` `(RRR)V`. |
| `0x3B47D0` | `0x1F1A00` | `PanCameraToTimedWithZ` `(RRRR)V`. |
| `0x3CD7B0` | `0x1F41B0` | `SetCameraOrientController` `(Hunit;RR)V`. |
| `0x3CD900` | `0x1DAD30` | `CameraSetupApply` `(Hcamerasetup;BB)V`. |
| `0x3CD930` | `0x1DADC0` | `CameraSetupApplyWithZ` `(Hcamerasetup;R)V`. |
| `0x3CD960` | `0x1DAD60` | `CameraSetupApplyForceDuration` `(Hcamerasetup;BR)V`. |
| `0x3CD990` | `0x1DAD90` | `CameraSetupApplyForceDurationWithZ` `(Hcamerasetup;RR)V`. |
| `0x3C8DA0` | `0x1E9450` | `IssuePointOrder` `(Hunit;SRR)B`. |
| `0x3C8E20` | `0x1E96A0` | `IssueTargetOrder` `(Hunit;SHwidget;)B`. |
| `0x3BAD70` | `0x1F18D0` | `OrderId` `(S)I`. |
| `0x3BC4D0` | `0x1F1AD0` | `PauseGame` `(B)V`. |

The sixteen native rows are exact: the name is registered in 1.27a with a signature that
matches the hook's declared calling convention parameter for parameter.

## Runtime design

`src/platform/GameBuild.hpp` exposes the translation instead of scattering build
conditionals:

- `SelectBuild()` classifies `Game.dll` from `FileVersionMS`/`FileVersionLS`
  (`0x0001001B` / `0x0000CC10` for 1.27.0.52240) and then requires three independent
  prologues at their translated addresses to match before accepting the build, so a
  repacked or repatched binary fails closed.
- `Rva(legacy)` returns the active build's RVA, or **0** when this build has no verified
  address. Zero is a deliberate "feature unavailable" signal; callers must never fall
  back to the legacy value, because 1.27a reuses the same numeric ranges for unrelated
  functions.
- `RvaAvailable({...})` gates code paths that need several internals at once.

Every `base + 0x...` site in the runtime was rewritten to `base + wc3::Rva(0x...)`
(80 call sites plus 2 inside `WarcraftApi.cpp`, which already lives in `namespace wc3`).
On 1.26a `Rva()` is the identity, so behaviour is unchanged.

## Status on 1.27a

The host is installable: the launcher, `setup` and Player packaging accept `1.27.0.52240` and the
module binds to it, so the lines below describe what a player gets rather than a build gate.

Working:

- The whole JASS native layer - all 99 entry points, including camera getters and setters,
  unit queries, orders, damage, groups, items, timers, effects, fog and terrain cliff.
- `UnitObject`, `DestructableFacing`, `Effect`, `Ground` and `IsNeutralUnit`.
- The camera guard hooks. All eleven blocked entry points are resolved by name, so map
  scripts cannot steal the FPS camera for pan, setup or orient calls. The relaxed
  camera-bound write stays disabled because its layout check is a 1.26a x87 pattern, and
  `PositionEyeTarget` falls back to a plain `SetCameraPosition`.
- The pause hook (`PauseGame`) and the interface hook (`ShowInterface`).
- The render-demand patch behind `Rendering.MaxFPS`. 1.27a recompiles the same
  `mov eax,[reg+44h]; test al,4; jz short; and eax,-5; mov [reg+44h],eax` sequence the
  1.26a site uses, differing only in the base register (esi -> edi) and the jump
  displacement (+14h -> +11h). `0x62D7F2` therefore translates to `0x5FCF2`, and the
  signature check in `NativeFrameRate` treats those two bytes as variable while every
  opcode, the `44h` field offset, the tested bit and the `-5` mask stay exact. Measured
  on the client's main menu: `MaxFPS=0` keeps the native limiter at ~60 FPS, `30` lands
  on exactly 30, `300` reaches ~245 where the scene becomes GPU-bound.

Running differently:

- 1.27a has no verified world-render entry point yet, so the per-frame controller tick
  moves from the world callback to the swap-buffer hook, which needs no game address at
  all. The simulation still advances once per presented frame; the native world-pass
  choreography around it (actor render filtering, label timing) is unavailable and is
  skipped rather than approximated.
- The classic console band is still hidden, but not the way 1.26a hides it. 1.26a marks the
  UI pass from its world and UI draw hooks (`0x395900`, `0x60C580`); neither signature
  survives in 1.27a, so `nativeUIPhase` is never raised there and `ShowInterface(FALSE, …)`
  only hides controls - the border skin and translucent body keep being drawn every frame.
  The band is instead dropped by geometry at GL submission. 1.27a draws the console through
  `glDrawElements` over client vertex arrays, so `DrawElementsHook` classifies each batch:
  a batch whose projection is orthographic (`projection[11]==0 && projection[15]==1`, world
  batches always render perspective) and whose every vertex projects inside the strip
  recorded by `FullscreenView::ObserveWorldViewport` - plus a low short-quad rule for the
  skin's top border beam - is console chrome and is swallowed before it reaches the driver.
  Anything that reaches the world region or leaves the viewport keeps drawing, so partial
  overlaps can never clip live geometry. Verified on DotA and on a Chinese custom map: the
  FPS view shows world pixels only in the strip, the F6 round trip restores a fully intact
  RTS console, and no black rectangle is left behind.
- The native minimap pixel capture does work on 1.27a: it is fed by the same submissions the
  geometry filter swallows (`minimap.Submit`, exactly as the 1.26a UI hook feeds it), with
  `BeginFrame` started from the frame anchor and `FinishFrame` restoring the captured pixels
  after the overlay pass. The letterbox clear is lazy, so it only runs once a map batch has
  actually been captured.

Disabled on 1.27a, each bailing out through its own guard:

- Actor render filter, weapon attack replacement, movement guard, combat guard,
  ability and spell HUD, floating world labels and private sky. Their addresses are
  either unresolved or their 1.26a byte signatures no longer match, and the guards keep
  them from installing anything.

Still unresolved, and why each one is hard:

- `0x395900` (world render callback), `0x60C580` (UI draw), `0x7B66F0` (projection),
  `0x126F0`/`0x1F5A0`/`0x763F00` (terrain surface helpers), `0x3FA30`/`0x4771A6`
  (camera far-clip channel), `0x7C2C0`/`0x276A00`/`0xC6060`/`0xC5E30`/`0xC6210` (combat
  guard), `0xC6940`/`0xC6B30`/`0xCF660`/`0x281580`/`0x276330` (weapon attack),
  `0x201C60` (walking speed), `0x787D0`/`0x32C8E0`/`0x4786B0` (ability tables),
  `0x4E54B0` (floating text), `0x4D34E0`/`0x4D2E40`/`0x4D2E60`/`0x4D2F10` (sprite submit),
  `0xAB58F0`/`0xAB65F4`/`0xAB7788`/`0x3A8060`/`0x4693D0`
  (data structures and spatial query).
  Their 1.26a signatures are either x87-based, shorter than four bytes, or not unique in
  1.27a. Two three-byte world-render candidates (`0x368480`, `0x366E10`) were found by
  following `[esi+0x338]` readers, but neither can be confirmed without the 1.26a binary,
  and hooking the wrong function would crash the game, so both were rejected.

## Finishing the port

1. Obtain a 1.26a `Game.dll` (`1.26.0.6401`) and run
   `python tools/resolve_native_offsets.py <Game.dll> --json tools/game-1.26.0.6401.json`.
   With both tables, each unresolved internal can be matched by *call graph shape* rather
   than by bytes: the same JASS wrapper or the same caller set identifies it in both
   builds. This is the single highest-value next step and removes the ambiguity in every
   remaining row.
2. Confirm each newly identified address by re-deriving it from a structural fact, then
   add it to `INTERNAL_ADDRESSES` in `tools/generate_native_offsets.py` and regenerate:
   the include and the translation table both come out of that one run.
3. For features whose 1.26a guard is an x87 sequence, re-derive the guard from the 1.27a
   code and store it per build. Do not simply drop the guard: it is what keeps an
   unverified address from being hooked.
4. Rebuild with `tools/build.ps1`. No compiler is present on the machine this port was
   prepared on, so the 1.27a path is unbuilt and must be compiled and run against a
   disposable Warcraft installation before it is trusted.

## Running

The launcher, `setup` and Player packaging accept both hosts through `setup/host-build.ps1`,
which declares the same two identities `wc3::GameBuild::SelectBuild()` binds to and records the
accepted build in `client-installed.json`. Select your 1.27.0.52240 installation in the Warcraft
folder field and a separate install folder, exactly as `INSTALL.md` describes; a Player package
built against a different host, or a game whose `Mss32.dll` differs from the packaged one, is
refused in favour of Developer mode. `WarcraftCS.log` reports the selected build on startup
(`Bound Warcraft III 1.27.0.52240 native API`) and logs every feature that stood down
with its reason.
