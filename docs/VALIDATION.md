# Validation of the 0.1.0 source baseline

## Custom hero lists beyond four entries (test branch, 2026-10-11)

The reported installed-game log resolved the first slot to an absent/unlearned rawcode. Inspection
of the owner's DotA 6.88f AI tables reproduced the cause: a five-entry hero list starts with the
ultimate/stats while the primary ability is fifth. The old four-entry truncation discarded it, and
an unknown list-order fallback also occupied columns before declared UI metadata was considered.
The production parser now reads the full list, applies declared columns first, fits unknown entries
into remaining slots and prefers learned replacements within a declared column. Explicit extra/hidden
columns do not fall back into the four spell controls. Synthetic regressions reproduce reordered
five-entry lists, missing metadata, passive gaps, extra/hidden columns and learned replacements.
The unchanged caster/aim suites pass. A private harness using the actual owned map tables now resolves
the requested hero's four slots in button order, with the primary first and ultimate fourth.
This data-level check does not claim a cast in the user's running match; that game remains untouched.

## Configurable keyboard and first-person spells (test branch, 2026-10-11)

Production shortcut parsing validates unique layouts atomically, permits swaps and disabled actions,
and preserves F6 as a recovery key. Real INI tests cover custom spell/pickup/speed keys, conflicting
layouts, bounded aiming distance, rawcodes/target modes and strictly validated discrete order IDs.
Synthetic owned-table metadata verifies SYLK coordinate inheritance, research-column order and
passive slot preservation. Spell order tests cover unit/point/immediate/self forms, rejection,
unlearned/passive slots, native completion, explicit interruption and unloaded-map reset. Reticle
world tests cover nearest visible living widgets, terrain, hidden/loaded/dead filtering, destructable
occlusion and balanced groups. The independent suites, buying regressions, actual XAudio2 gain and
INI migration in PowerShell 7/Windows PowerShell 5.1 pass. The ordinary x86 Player build passes.

The user-approved disposable native map verifies the 1.26a lookup signature and automatic Storm
Bolt/Thunder Clap/Avatar slots. Native logs show mana costs of 75/90/150 with regeneration between
samples, cooldown/insufficient-mana rejection and Q replacing C via live F8 reload. The initial
native run revealed Bash's misleading accepted no-op order; stock passive button metadata now
rejects that slot before dispatch, with an added regression. Blizzard's order was accepted but
interrupted by subsequent input in this session; full sustained channel verification remains pending.
The private game closed normally and its INI was restored exactly. No campaign save was used.

Automatic slots use stock/overlaid archive tables rather than arbitrary map object modifications;
custom heroes/edited ability lists can use documented ability/order/target overrides. Native costs,
legal target restrictions and cast ranges remain enforced by Warcraft. Multiplayer is not tested.

## Shared health-bar shortcut (test branch, 2026-10-11)

K now switches both teams together. The production settings toggle is checked for all four old
saved team combinations: mixed/off states enable both, both-on disables both, and a second press
reverses that shared state without changing distance. Both existing INI keys remain compatible
with loading and migration. The sampler and hidden WGL rendering checks pass, as do gameplay
configuration/migration checks and the ordinary Player build. The earlier native session below
verifies rendering; native input was not driven again for this shortcut-only follow-up.

## Allied/enemy health bars (initial two-key test, 2026-10-11)

The production native sampler in a synthetic world filters controlled/dead/hidden/transported,
fog-hidden and passive-neutral units, applies independent team switches, preserves actual health
fractions, computes fallback model tops and balances group creation/destruction. A hidden native
WGL window exercises actual green/red pixels, full/half fill, scope projection, behind-camera/range
culling, nearer-depth occlusion and GL-state restoration. The full independent suite passes.
Real INI loading covers legacy/empty/malformed/mixed-case/numeric switches, independent team reload
and finite bounded 0..15000 distance. Migration in PowerShell 7 and Windows PowerShell 5.1 preserves
custom switches/distance and remains idempotent. The ordinary x86 Player build also passes.

With user-approved focus, the disposable map has a damaged ally, enemy and enemy behind a scaled
castle, plus hidden/dead fixtures. Native captures show green/red bars above rendered model tops;
the damaged ally has a partial fill and the full-health replacement hostile target has a full fill.
The castle occludes its rear target's bar. K removes only the ally bar; L subsequently removes the
enemy bar. F8 logs both saved switches, then the independently saved false/true state. No controller
fault or GL error occurs. The scene starts with a native placement offset, so the test steps back
from the castle and uses an explicit private paused-target request after the first enemy dies.
Normal Exit closes the game, no test process remains, and the private INI snapshot is restored.
No user installation or campaign save is changed; full campaign/multiplayer testing is not claimed.

## Ground clipping and the native far-channel ceiling (test branch, 2026-10-11)

Inspection of the owner's supported Game.dll shows the camera controller's FARZ wrapper at +114
initialized with a 100..10000 range. Both scalar writes and evaluations clamp through the channel's
+80/+84 bounds. Comparing native far=10000 with requested far=15000 skips the old near correction
and leaves a 100-unit plane in front of an eye only 96 units above the ground. Earlier field stubs
did not emulate that native bound; the earlier 15000 tests prove parameter plumbing, not native reach.

The production platform owner now changes only the active FPS channel's ceiling to 20000 and restores
it through native generation keys. Tests reproduce the 15000/10000 failure, verify the live-field near
fallback, 15000 acceptance, untouched portrait projection, repeated updates, camera replacement,
stale generations and foreign limit preservation. The complete independent wrapper and ordinary
x86 runtime build pass. Native getter/resolver/scalar-limit signatures gate the layout; failure retains
the stock channel and the near correction still matches the actual camera field.

A private owned map with a 257x257 flat terrain, a player and a castle at 13000 was rebuilt; all 15
archive members round-trip exactly. With the user's save/close and input approval, the ordinary
Player DLL runs in the disposable Warcraft copy. Native signatures verify, the FPS channel ceiling
rises from 10000 to 20000, and the requested/live/projection far values settle at 15000. The initial
10000 transition also receives near=8, covering the fallback. A horizontal capture shows the castle
at 13000; the maximum downward pitch of -65 degrees shows continuous textured ground below the
96-unit eye, including during movement. F6 releases FPS and logs restoration of the stock 10000
ceiling. No controller fault or GL error occurs. The test game closes normally and its private INI
backup is restored. User installation files and campaign saves are untouched; this is a flat-map
ground/projection check, not a full campaign or ledge-collision validation.

## Draw-distance default 15000 (test branch, 2026-10-10)

The default settings snapshot, missing/malformed INI fallback and fresh-install template now use
15000 Warcraft units. Existing settings-loader and real camera/projection tests pass, including
explicit 15000 far clip and F8 snapshot reload. Configuration migration preserves custom distances
and uses the new default for missing keys. The owner's installed INI was backed up privately and
changed only from DrawDistance=10000 to 15000; F8 or restart is required to apply that file change.
No input was sent to the running campaign for this parameter adjustment.

## Ledge and stuck-position recovery (test branch, 2026-10-10)

The production collision regression reproduces a blocked lower rim extending beyond the old
64-unit cliff probe; the old implementation stops after landing, while the full-cell probe crosses
the rim, falls and lands. A separate existing-trunk-overlap trial retreats through its native
footprint even when native placement would relocate it, and rejects movement deeper into the trunk.
Existing uphill-cliff, bridge, shallow-water, unknown-flat-blocker and swept thin-wall checks pass.
The complete independent native wrapper passes; the final retreat change passes the targeted
production collision suite. Runtime rejection telemetry uses the existing bounded detailed logger.

A read-only capture of the user's running game shows the actor at a shoreline cliff; the old
module does not log rejection reasons, so this alone cannot identify its exact blocking sample.
A separate owned cliff map is prepared for native verification. Live gameplay verification is
pending user permission to save/close the campaign and temporarily hand over input; no campaign
state, save or installed DLL was changed during diagnosis.

## 0.7.0 release consolidation (2026-10-10)

All completed feature tips are ancestors of the consolidated release; the old crosshair patch is
equivalent to the retained implementation and the older summary parser is superseded by the tested
English resolver. Merges retain their histories without reverting newer behavior. Source changes
are compared with the immutable v0.6.1 tag. Previous tags and release/feature branches remain intact.

The complete independent gameplay/window/logging wrapper, actual OpenGL reticle and sky tests,
production-XAudio2 silent gain test, launcher/update/reinstall/replacement regressions, Miles provider
checks and config migration in PowerShell 7/Windows PowerShell 5.1 pass. Native evidence below was
recorded with the same consolidated gameplay implementation in disposable maps; release changes
update version, notes and documentation. No full campaign or additional driver-specific claim is made.
Both launchers package the reviewed 0.7.0 English list and identical audited sources; publication
uses complete release notes with separate hidden launcher-summary metadata.

## Native campaign movie input recovery (test branch, 2026-10-10)

Two disposable maps queued the owner's IntroX movie through PlayCinematic/ChangeLevel, then loaded
a flat map containing one owned footman. The previous ordinary Player build reproduced the failure:
Warcraft destroyed its original game HWND for movie playback and created a different HWND afterwards;
F6 no longer reached the mod and FPS stayed inactive. The repaired ordinary build logged destruction
and attachment to the replacement render window. After skipping IntroX, F6 enabled FPS, V displayed
speed, W moved the unit, and screenshots showed the weapon and crosshair. Repeated F6 toggled RTS/FPS;
N suppressed fog and the normal exit restored the map's fog switches. No controller faults occurred.
Both test sessions closed through Warcraft's own exit dialogue. The real campaign, its progress and
the user's installed files were not used; complete movie playback and a full campaign run are not claimed.

Hidden Win32-window tests exercise native F6 forwarding before repair, consumption after repair,
same-HWND procedure resets, repeated observation, an external forwarding subclass, two windows with
different native procedures, late destruction, invalid/foreign HWND rejection and fresh attachment.
The numerical test wrapper includes this adapter check. Ordinary x86 runtime compilation passes.

## Optional fog override (test branch, 2026-10-10)

The actual owned 1.26a native registration table verifies FogEnable/FogMaskEnable `(B)V` and
IsFogEnabled/IsFogMaskEnabled `()B`. A disposable flat map started with both switches enabled.
The ordinary Player build removed fog and the black mask on N, ignored repeat/release packets,
restored fog/mask on F6, reapplied the session choice on re-entry, and restored them on the next N.
F8 applied true and false DisableFogOfWar preferences. Real screenshots and native transition logs
were inspected; the HUD no longer contains Sword or Melee RMB hints. The test game closed normally.

Independent adapter tests cover all four fog/mask baselines, no repeated writes, suppression after
map-script changes, exact restoration, fresh re-entry capture and map-unload snapshot invalidation.
INI validation covers missing/empty/invalid/case-insensitive/0/1 values and true/false reloads;
PS7/Windows PS5 migration retains custom preferences and inserts missing defaults without duplicates.
Only the private runtime/map/config was used. The user's installed game and saves were unchanged.

## Optional speed counter (test branch, 2026-10-10)

Real Warcraft III 1.26a on a disposable single-player map verified the ordinary Player build,
without a compiled movement oracle. The HUD omits WASD/reload/jump/crouch explanations. V shows
250 CS units/s during a normal USP run and hides the counter on the next press; Windows key-repeat
and release packets do not cause another toggle. V in RTS does not change FPS visibility. F8 applies
both true and false INI preferences; screenshots confirm the counter stacks above reload notices
without covering health/ammunition. The test game exited through its own confirmation dialog.

The real INI loader checks missing/empty/malformed values, case-insensitive booleans and 0/1, plus
repeated snapshot loads. Config migration checks fresh/legacy files, true/false custom values,
single Interface/key occurrences and idempotence. Only a private runtime/config/map was used;
the user's installed game and saves were not modified. The default remains ShowSpeed=false.

## Full launcher reinstall (test branch)

Manual Update selects an identical latest GitHub release for repair; startup checks remain quiet
when version and revision match. Both launcher variants describe full private-game reinstall.
Production file-copy tests repair damaged/missing core files, official maps and original audio;
retain progress, custom INI, private assets/maps and the installed proxy until mod setup completes;
and back up edited official content without duplicating unchanged backups on a repeat run.
Original/overlapping/unmarked/active runtimes and junction redirection are rejected before file writes.

The setup-runner integration uses current embedded repair code with synthetic legacy/modern releases
in Player/Developer mode. Game-version metadata is provided by a project-authored test DLL. Only
conversion/module installation is stubbed; production source extraction, Windows PowerShell launch,
request serialization and runtime repair execute. This verifies repair before the downloaded backend,
module/asset replacement and save/config preservation without an actual game or dependency downloads.

## Unreleased bunnyhop test branch (2026-10-09)

Movement, native collision, real INI parsing and migration checks pass. The numerical oracles cover
repeated/manual jumps, acceleration and speed caps, stationary takeoffs, frame-rate behavior, air
strafing, gravity-driven cliff falls, low props, tall/unknown blockers, full bridge footprints,
uphill takeoff/landing momentum, shallow-water support and root/stun/slow restrictions. Configuration
checks cover documented bounds/defaults, malformed/non-finite values, repeated reloads and preservation
of custom movement values during repeated updates in both PowerShell 7 and Windows PowerShell 5.1.
All existing independent gameplay suites also pass.

An explicit movement fixture in a disposable Lost Temple copy on Warcraft III 1.26a verifies five
native checks: five held-Space hops accelerate from 375 to 551 Warcraft units/s; a scaled barricade
stops a walking actor but is cleared by jumping; a native town hall blocks the attempted jump; and
walking off an actual cliff falls gradually from 128 to 17.2 world units and lands. Native model
transforms include the separate visual-scale getter. Terrain uses the verified interpolated height
backend; ordinary GetLocationZ support is preserved for shallow water. Game logs and captures were
inspected. Test requests and destructive fixtures are excluded from ordinary builds.

This is feature-branch verification, without a release/version change. No campaign save or complete
campaign was tested. Irregular/imported geometry and other maps still require playtesting; collisions
use model boxes, not triangle-perfect mesh contact. F8's existing production reload path applies the
movement snapshot; no new live F8 keyboard session is claimed.

## Original source baseline

Windows, native x86 Warcraft III 1.26a / Game.dll 1.26.0.6401, OpenGL, offline private runtime.
No game footage, game content, raw memory dumps or personal campaign saves are included here.

Seven C++ suites independently validate movement, look-angle wrapping, model-bound ray contact,
melee timing/range, friendly/C4/AWP damage policy, INI/ammo calculations and classic recoil.
The recoil oracles check published coefficients, burst timers and the known ran1 seed-one sequence.

Native private stock-map checks confirmed AK first kick:1/0.9/1.5 degrees standing/crouched/moving;
M4:0.65/0.6/1; USP/AWP:2, sustained AK vertical cap5.75 and lateral cap1.75.
O/H recruitment and J release were tested in both modes with real owned footmen and native orders.
No further formation orders followed release, and FPS remained enabled.
One harness wrongly expected exactly two recruits after moving closer to starting workers;
repositioning the fixture confirmed the two-footman oracle without changing ownership filtering.

Earlier native checks covered FPS/RTS attack acquisition, real rune healing and one-time ammo reward,
ordinary equipment/full Backpack handling, H/O/H combat restoration, destructable gate leaves,
hero-model hiding, fullscreen/raw mouse rotation and OpenGL context recreation after Alt-Tab.
Native Backpack rejects auto-use runes even after research; the mod preserves that rule.
Friendly damage coefficient is0.5; C4 base damage2500 is still processed by native Warcraft damage rules.

The normal installed baseline loaded a preserved night-elf campaign with Maiev650HP,
correct physical camera position, J/F8 and no private-fixture/controller-fault output.
All campaign missions, map triggers and every custom map have not been verified.
Fresh source-archive setup was also checked outside the original development layout:
it created a separate owned Warcraft runtime, installed NumPy in a private venv,
verified the pinned MinHook checksum, built MinHook and the mod with MSVC x86,
generated Miles forwarding code locally and converted the owner's six CS models.
No prebuilt dependency from the private research checkouts was required.

The resulting normal build started a real Lost Temple map with all native hooks
reporting successful creation/enabling, raw mouse input and FPS rendering active.
All seven viewmodels loaded, including the procedural sword fallback; J and F8
worked without a controller fault. This confirms standalone setup and a native
smoke check; it does not establish complete campaign/custom-map compatibility.

The complete staged tree passed the source allowlist/credential/binary/path audit.
Adversarial audit checks rejected fake credentials, renamed binaries, game assets,
private paths and decompiler headers. An early audit pattern matched its own pattern
text; anchoring the header check fixed that false positive before publication.

## 0.2.0 original sword and English README

The release converter always generates the project's original straight silver blade,
gold guard and leather grip. An existing private Grudge model is not selected or read.
External sword parameters/imports were removed from both setup and conversion.
An owned-CS conversion produced the original procedural sword cache successfully;
its rig/animation data and audio timeline come from the owner's knife as before.
The sword geometry itself is unchanged from the previously verified original sword.
Runtime C++ and combat behavior are unchanged in this update.

PowerShell/Python syntax checks and source-tree audits were run for the changed tooling.
The complete English README includes requirements, setup, controls, settings, limitations
and legal references. Linked relative documents remain in the release tree.
No fresh automated input was sent to the user's active game for this documentation/tooling update.

## 0.3.0 standalone Windows client launcher

The WinForms EXE was built with the Windows .NET Framework compiler. Its embedded ZIP
contains the audited project source, without game assets, downloaded dependencies or build output.
An offscreen preview verified the complete folder-selection, consent and progress layout.
UI tests confirm that valid folders alone cannot enable Install: explicit agreement is required,
and revoking agreement disables it again. Runner/extractor and PowerShell no-consent tests
reject installation before creating files. Path/JSON tests include archive traversal rejection
and protection against installing into the original game folders or a drive root.

The real pinned Python installer passed SHA256 and PSF signature verification, and the real
Microsoft bootstrapper passed Microsoft signature verification. Negative checksum/publisher
checks rejected untrusted downloads. Existing Python and Visual Studio/SDK were reused for
the complete installation tests; fresh vendor installation on a clean Windows machine has
not been tested.

A complete C# runner installation extracted its embedded source into a separate client folder,
created an owned Warcraft copy, installed NumPy in a private venv, verified/downloaded MinHook,
converted the owner's CS models and built the normal 372736-byte native mod. The selected
installation path included Cyrillic characters, spaces, an ampersand and an apostrophe.
The completion marker and Play configuration point inside that client folder.
This test exposed and verified fixes for inherited PowerShell Core module paths, Windows
PowerShell UTF-8 JSON decoding and Unicode paths in the native build command file.

The user's active Warcraft session was left running and received no automated input.
Play was checked through its installed configuration; a fresh game launch was not performed
for this installer feature. Native gameplay behavior is unchanged by the launcher.
All seven independent C++ logic suites and launcher regression checks passed again
when preparing 0.3.0. The release EXE reports assembly version 0.3.0.0 and includes
the English installation guide and source-only release tree.

## Miles provider setup repair

A user's first client launch reported "Unable to initialize base sound services".
The sound proxy/original DLL checksums and standalone device-open tests passed, but
the new private runtime lacked the owned `redist/miles` folder. The older working
development runtime included it. The installer had copied only selected top-level
files and Maps/Campaigns/Movies/AI Scripts, omitting the actual Miles codec/providers.

The warning reproduced in the affected client and an isolated ASCII-path copy,
including a control run with the proxy/mod disabled. Restoring `redist/miles`
from the owner's original installation removed the warning. The affected client
then launched with its normal mod: 43 CS clips initialized, and process-only capture
confirmed Warcraft menu music (RMS about 0.214, versus effectively zero before repair).
No sound-device/registry settings, original game files or saved progress were changed.

The shared audio setup helper runs for both fresh and existing marked runtimes.
Regression checks cover all five provider/codec fixtures, repeat installation,
legacy missing-folder repair, preservation of unrelated runtime files, missing-source
rejection before writes, and rejection of original/unmarked destinations.
Launcher payload/consent tests and Windows PowerShell audio checks passed.
Only owned source is embedded in the rebuilt EXE; the audio components remain local game data.

## 0.4.0 tree targeting, wheel input and requirements

All nine C++ regression suites pass. New synthetic MDX fixtures verify that rays beside
the trunk miss the old canopy box, trunk faces remain solid from either side, short
death stumps are excluded, nearer targets win and scaled rays retain world distance.
The owner's Lordaeron/Cityscape tree models each decode twelve trunk triangles.
Malformed geometry is rejected without falling back to the oversized tree box.

An isolated Lost Temple session on Warcraft III 1.26a verified six native geometry
checks: direct tree trunk hits, adjacent misses and preserved elf gate hits. Separate
wheel notches selected slots 7 -> 1 -> 7 -> 1 -> 2 -> 3 in the real FPS controller.
The screenshot showed the new wheel control hint and functioning weapon rendering.
The fixture was opt-in; ordinary builds exclude it. Existing INI values were unchanged.
No campaign/save was opened. Arbitrary imported tree models and animated model
variants still require map-specific verification.

REQUIREMENTS.md accompanies the source and distribution packages and documents
owned game files, dependencies, manual builds and the release updater contract.

The original 0.3.0 launcher assembly accepted the verified 0.4.0 package and completed
source installation, local dependency setup, owned-asset conversion and the ordinary
native build in a disposable client. Its save sentinel, custom INI values and remembered
private sword were preserved. Launcher regressions also passed release/version checks,
corrupt package rejection, archive/junction protection, running-game refusal and delayed
EXE replacement with backup/restart. The 0.4.0 UI preview displayed the correct version.
Publication lint reported 147 source files, zero failures and zero warnings.

## 0.5.0 launcher Player/Developer installation and update confirmation

The launcher embeds a separate native-runtime ZIP containing two project-built x86 modules,
their SHA256/source-version/Miles-ABI manifest and the project/MinHook/mechanics notices.
DLL import inspection found only inbox Windows libraries; no Visual Studio runtime DLL,
compiler, SDK or proprietary Warcraft library is distributed. Git and the source ZIP remain
source-only. Player needs Python/NumPy to convert the owner's game assets locally.

A real Player installation/update completed with compiler detection and Build Tools installation
replaced by failing test sentinels. Installed DLL bytes matched the embedded package; no MinHook
source was fetched. A fresh installation also succeeded directly from the EXE without an update
cache. Explicit Developer installation fetched pinned MinHook and rebuilt locally using installed
MSVC/SDK. The original updater-enabled 0.3.0 assembly also completed installation of this new
bundle through the verified update cache. Save sentinel bytes, custom INI values and remembered
private sword selection were preserved across the mode changes.

Launcher regressions cover consent for both mode buttons, update prompt confirmation/refusal,
legacy source-only release rejection in Player mode, manifest/package hashes and extraction
guards. Native-payload tests reject corruption, traversal and source/version mismatches.
Offscreen previews verified both installation buttons and Player/Developer update dialogs.
GitHub latest release lookup succeeded; rate-limit fallback manifest parsing preserves canonical
repository URLs and stable-version/checksum validation. No new campaign or interactive game
session was opened for these installer changes. Fresh vendor-tool installation on a clean Windows
machine remains unverified; Developer mode reused installed tools in the integration test.

## Configurable allied damage

`Damage.FriendlyFirePercent` defaults to the previous 50%, accepts fractional values,
and clamps to 0–100. Real INI tests cover missing keys, 0/25/100/12.5, out-of-range
values and malformed/NaN input. Damage oracles cover firearms, heavy melee, C4,
hero-mode attack scaling, enemy damage independence and allied AWP finishing protection.
Zero allied damage skips the native damage call to avoid damage-trigger side effects.
Bullets/melee use the reloaded settings snapshot at contact; C4 retains its planting snapshot.
No campaign or live match was used for these independent combat/settings checks.

## Dual launcher and separate Install/Update regression checks

Both UI variants pass folder and download-consent checks. The source-only form exposes
one Install button; the DLL-included form retains explicit Player/Developer installation.
Both expose a separate Update button. Tests cover variant-specific GitHub API and
rate-limit fallback selection, independent executable/runtime checksums, incomplete
variant manifests, missing assets, unexpected launcher names and invalid assemblies.
Existing extraction, running-game, audio setup, delayed replacement and backup/restart
regressions pass. These checks do not open a campaign or interact with the user's game.

## Configurable CS volume (0.6.1)

Real INI regression checks cover missing/empty/malformed/non-finite values, mute, fractional
percentages, 0-100 clamping and repeated edits/reloads of the same settings file. Setup migration
checks in PowerShell 7 and Windows PowerShell 5.1 add Audio defaults to old/empty configs,
preserve an existing CSVolumePercent=35 and other preferences, avoid duplicate sections/keys
and remain byte-identical on repeated setup.

The native CS audio test uses the production GameAudio and settings loader, actual XAudio2
and a project-authored silent PCM clip. Native master-voice readback verifies 50, 0, 12.5 and
100 percent while source voices are queued; mute/restore, defensive bounds and the uninitialized
audio path pass. No game assets, audible test tones, saves or Warcraft window are used.
The master bus preserves per-event CS gains and does not access Warcraft's Miles engine.
Startup and F8 both call ReloadSettings, which applies the validated snapshot to this bus.
All independent gameplay suites and the normal x86 native build also pass.

The configurable-game-settings and feature-release-workflow skills pass the skill validator,
are available in the repository and installed personal skills, and are adopted by AGENTS.md.
No new complete campaign, live Warcraft F8-input session or clean-machine dependency install
is claimed for this audio/settings change.

## Experimental Half-Life-style air strafing

Independent production-physics trials run for eight seconds at 30/60/144 FPS with alternating
A/D turns, no W after the run-up and no takeoff bonus. Speed rises from 375 to approximately
792/1055/960 Warcraft units/s. Straight held-Space hops preserve 375; disabling air acceleration
removes gain, and disabling BunnyHop removes excess speed on every takeoff. Manual jumping,
stationary jumps, root/stun/slow restrictions, optional custom boosts and live cap replacement pass.

Actual production swept collision in a synthetic native world covers low props, tall/unknown
blockers, bridge destruction/edges, hills, cliff falling and water support. A maximum-speed trial
approaches 3000 Warcraft units/s and stops before a thin wall even when the frame's destination
would be beyond it. Real INI loading feeds physics and supports changing a cap without resetting
the current vertical velocity. Migration preserves older custom bonuses and adds the zero default.

The opt-in native scene includes separate `strafe` and straight `hop` trials. This revision's
real-game verification remains pending because the user's Warcraft process is active; no campaign
or running module is replaced. Measurements above are independent physics results only.

## Tree movement bounds (test branch)

The active client's log identifies AshenCanopyTree0 with standing visual extents approximately
427 by 425 Warcraft units. Privately decoding the owner's model yields 24 trunk triangles and
local trunk extents approximately 37 by 43 units. No extracted model is committed or packaged.

Tree-mesh regressions check exclusion of canopy cards and death stumps, invalid geometry,
and the narrower bounds of that owned model. The production swept-collision test reproduces
immobility inside the old canopy box, verifies passage beside the decoded trunk, and retains
blocking on a direct approach. Adapter checks cover separate unit/destructable model caches
and map reset, preventing tree animation names on units from changing their movement bounds.
All independent native suites pass. Interactive tree-corridor verification remains pending;
the original diagnosis was read-only while the user's campaign was open, without touching its saves.

## Configurable frame rate and draw distance (test branch)

The owner's 1.26.0.6401 Game.dll confirms the render-completion demand-bit instruction identified
through RenderEdge's published address. The adapter validates the surrounding opcode bytes before
changing one in-memory operand; it does not patch game files or alter simulation time. A synthetic
executable-memory oracle covers enable/disable/reload, unexpected instruction refusal and real
Windows high-resolution waiting (30 intervals at 120 FPS take at least 0.25 seconds).

Real INI tests cover missing/empty/malformed/non-finite values, native mode, 30–360 FPS clamping,
3000–20000-unit distance clamping and reload. Migration in PowerShell 7 and Windows PowerShell 5.1
preserves explicit caps/distances and remains idempotent. Production camera-field writes and
near-plane selection pass for 3000/5000/10000/15000/20000 while preserving portrait/RTS projection.
All independent native suites and the ordinary x86 build pass. With user-approved focus, a disposable
flat Warcraft map contains a player and a scaled castle at x=6500. In one ordinary Player session,
F8 changes MaxFPS/DrawDistance from 144/10000 to 0/5000 and back. Stable native-mode samples are
approximately 62–65 frames/s; the extended FPS world view is approximately 99–103 frames/s, with the
RTS view approximately 136–138. These are scene-specific measurements, not a promise of 144 FPS.
Matched captures at the same player position show the castle absent at 5000 and present at 10000.
The reticle and nearby terrain remain visible; a one-second W input moves the player normally.
No controller fault or GL error appears in this trial. The test exits normally, restores its private
INI snapshot and leaves no test process; user installations/campaign saves are untouched.
