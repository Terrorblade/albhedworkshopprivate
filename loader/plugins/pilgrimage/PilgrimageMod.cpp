// AlBhedWorkshop clone mod: spawn extra playable characters into FINAL FANTASY X and
// drive one of them from a second input device.
//
// Build with ..\build_pilgrimage.bat and drop the DLL into <game dir>\AlBhedWorkshop\plugins\.
// See hooks/Hotkeys.h for the keys.
//
// WHAT THIS PLUGIN IS, VERSUS WHAT THE LIBRARY IS
//   Everything this plugin knows about FFX.exe comes from workshop, which is linked
//   in: addresses, structure layouts, the resolved API, the per-frame hook point,
//   the walkmesh binding, the render probes. None of that is duplicated here, and
//   a second plugin gets all of it by linking the same library. See
//   ..\..\workshop\include\ffx\Ffx.h.
//
//   What is left in this plugin is only its own behaviour: which clones exist, how
//   one is made, how input drives it, and the control panel.
//
// WHAT THIS IS BUILT ON
//   reversing\PHASE1_CLONE.md is the research this implements. The short version
//   is that movement in this game is already fully per-character: everything below
//   the input layer takes a character pointer and reads no player global. So
//   movement is not reimplemented. A second character is allocated, its speed and
//   heading are written every frame, and the engine turns it, animates it, moves
//   it, collides it and clamps it to the ground for free.
//
//   Two things do not come free. The walkmesh binding, which is in the library at
//   ffx/Walkmesh.h because every plugin that places a character needs it, and the
//   one-slot Tidus cache, which is in clones/CloneSpawner.cpp because only
//   spawning hits it. Do not remove either.
//
// HOW THIS PLUGIN IS LAID OUT
//   clones/     which clones exist, how one is made, and how input drives it
//   diag/       dumps and watches, for when a spawn looks wrong
//   hooks/      the per-frame body and the hotkeys
//   ui/         the control panel, an ImGui panel on the workshop overlay
//   ModState.h  the state those share, split by which thread owns it

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wchar.h>

#include "ModState.h"
#include "ffx/Api.h"
#include "ffx/Input.h"
#include "ffx/MainLoop.h"
#include "ffx/Pad.h"
#include "ffx/VerifyLayout.h"
#include "battle/BattleSync.h"
#include "hooks/FrameHook.h"
#include "menu/CoopConfig.h"
#include "menu/MenuSync.h"
#include "net/LockstepLink.h"
#include "world/EncounterSync.h"
#include "world/RemotePlayers.h"
#include "world/MinigameSync.h"
#include "world/PlayerDrive.h"
#include "world/DialogueSync.h"
#include "world/FmvSync.h"
#include "world/HeldObjects.h"
#include "world/TriggerPass.h"
#include "hooks/Hotkeys.h"
#include "hooks/VisibilityDetour.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"
#include "ui/OverlayPanel.h"

#if !defined(_M_IX86)
#error "AlBhedWorkshop plugins must be 32-bit x86."
#endif

namespace pilgrimage
{

	using namespace ffx;
	using namespace workshop;

	namespace
	{

		// Just the filename, so the host check does not care where the game is installed.
		const wchar_t* FileNameOf(const wchar_t* path)
		{
			const wchar_t* name = path;
			for (const wchar_t* p = path; *p; ++p)
				if (*p == L'\\' || *p == L'/')
					name = p + 1;
			return name;
		}

		// The startup sequence, in the order it has to happen. This is the contract from
		// ffx/Ffx.h: log, bind the module, verify the build, bind the API, then hook.
		// Returns false when something went wrong badly enough that nothing should be
		// hooked, in which case the log already says why.
		bool Startup()
		{
			OpenLog(L"pilgrimage_together.log");
			BindHostModule();

			// Defaults first, then the table that describes them, then the saved file on
			// top. Loading goes through the registry, so the records have to exist by then.
			ApplyDefaultSettings();
			RegisterModSettings();
			LoadModSettings();

			wchar_t exePath[MAX_PATH] = { 0 };
			GetModuleFileNameW(NULL, exePath, MAX_PATH);
			Log("=== AlBhedWorkshop clone mod ===");
			Log("host       : %S", exePath);
			Log("module base: 0x%08X (preferred 0x00400000, ASLR slide %+d)",
			    (unsigned)(UINT_PTR)ModuleBase(), (int)((INT_PTR)ModuleBase() - 0x00400000));

			if (_wcsicmp(FileNameOf(exePath), L"FFX.exe") != 0)
			{
				Log("host '%S' is not FFX.exe, doing nothing", FileNameOf(exePath));
				return false;
			}

			if (!VerifyLayout())
			{
				Log("LAYOUT CHECK FAILED. This is not the analysed build, so nothing will be "
				    "hooked. Re-derive the addresses in workshop/include/ffx/Addresses.h from "
				    "the IDB.");
				return false;
			}
			Log("layout check passed");

			BindApi();
			LogApiState();

			if (!InstallFrameHook())
			{
				Log("could not install the frame hook, giving up");
				return false;
			}

			// The step hook has to be in place before a session starts, and it costs one
			// call per simulation step when nothing is connected, so it goes in
			// unconditionally.
			InstallLockstep();

			// The remote player table and the per-player trigger shadows start empty. Both
			// are pure bookkeeping until somebody joins, so this is just making sure a
			// reload does not inherit the last session's bindings.
			ResetRemotePlayers();
			ResetTriggerPass();

			// The RNG state and the three bytecode swap flags, neither of which the save
			// block carries. Not fatal: without it a joiner draws different numbers from
			// the host until something reseeds.
			if (!InstallMinigameSync())
				Log("minigame sync is off, so the RNG state will not be sent to a joiner "
				    "and the bytecode swap flags will not be checked");

			// Takes over the engine's single player driver so that every player
			// character, this machine's own included, is moved by that same function from
			// replicated bytes. Installed unconditionally and it passes straight through
			// with no clock, so a solo game runs the shipped code path.
			//
			// Not fatal, and this is the one failure that costs the most. Without it the
			// character this machine owns is computed here by the engine and on the other
			// machine by the mod, which is a position disagreement that grows the whole
			// time anybody is walking. RemotePlayers keeps its old approximate drive for
			// exactly this case.
			if (!InstallPlayerDrive())
				Log("the player this machine owns will be driven differently from how the "
				    "other machine drives it, so positions will drift while walking");

			// Not fatal. Without it a remote player's walking does not count toward a
			// random battle, and the engine behaves exactly as the shipped game does.
			if (!InstallEncounterSync())
				Log("remote movement will not contribute to random encounters");

			// Installed unconditionally and harmless when nothing is connected, because
			// the callback leaves the pad block alone unless a session is active. Without
			// it a message box runs off whichever hardware pad is plugged into the machine
			// showing it, which diverges the moment anybody talks to anything.
			if (!InstallDialogueSync())
				Log("message boxes will not be driven by replicated input, so talking to "
				    "anything in a session is a divergence");

			// The FMV barrier, on the four ATEL Movie wait handlers. Installed
			// unconditionally and invisible with no session, because the gate lets every
			// wait complete the way the shipped game does until a session starts.
			//
			// Not fatal, and the consequence is specific. A movie ends when each
			// machine's own decode thread finishes, which is a wall clock event on
			// hardware the other machine knows nothing about, so without this the two
			// scripts resume on different simulation steps and stay that far apart for
			// the rest of the session. That is a permanent divergence on every FMV in
			// the game, not a hitch, and a skip by one player would only skip their own
			// copy.
			if (!InstallFmvSync())
				Log("FMVs will end on a different simulation step on each machine, which "
				    "leaves the two scripts permanently out of step, and skipping one "
				    "will only skip it for the player who pressed the button");

			// The carry census, installed unconditionally because it is read-only and
			// useful with no session: it is how the "a carried object is a bone parent"
			// finding can be re-checked in a solo game.
			//
			// Not fatal, and the consequence is narrower than it looks. The carry itself
			// does not need us at all, because the engine derives a held object's
			// transform from its carrier on both machines for free. What is lost is the
			// hash that would notice the two machines disagreeing about who is holding
			// what, and the ability to hand an object to the character the player who
			// picked it up is actually driving.
			if (!InstallHeldObjects())
				Log("nothing will be watching who is carrying what, and a carried object "
				    "will always end up on the game's single bound-player character "
				    "rather than on whoever picked it up");

			// Installed unconditionally, same reasoning as the dialogue hook: both
			// callbacks pass straight through until a session starts, so a solo game is
			// byte for byte the shipped game with these in.
			//
			// Not fatal, and the consequence splits in two. Without the capture sites a
			// battle action is committed on this machine alone. Without the open hook
			// both machines open a menu for the same unit, and since the staging record
			// is one global, the two cursors corrupt each other's command. Either way a
			// battle in a session is a divergence, which InstallBattleSync says out loud
			// per hook.
			if (!InstallBattleSync())
				Log("battle commands will not be replicated, so the first battle in a "
				    "session will diverge");

			// Installed unconditionally for the same reason as the two above: the
			// callback leaves the menu pad block exactly as the engine filled it until a
			// session is active, so a solo game is byte for byte the shipped game.
			//
			// Not fatal, and the consequence is worth spelling out because it is not
			// obvious from the outside. Without it, two players share ONE menu cursor and
			// drive it from two controllers at the same time, and the first sphere spent
			// or piece of gear equipped is a divergence. The menu would still look like it
			// works, which is what makes it worth a loud line here.
			if (!InstallMenuSync())
				Log("the in-game menu will be driven by both controllers at once, so the "
				    "first sphere spent or item used in a session will diverge");

			// The co-op rows on the game's own Config screen. Installed
			// unconditionally and inert until a session starts, like the three above:
			// nothing is added to the Config screen while nobody is connected, so a
			// solo player's screen is byte for byte the shipped game's.
			//
			// Not fatal, and the consequence is only a missing feature rather than a
			// hazard. Without it the ownership settings and the host menu override are
			// reachable from the control panel and ctrl+F4 and nowhere a player would
			// look, which is the thing this was added to fix.
			if (!InstallCoopConfig())
				Log("the co-op settings will not appear on the in-game Config screen, so "
				    "who plays which character can only be changed from the control panel "
				    "and the host override only from ctrl+F4");

			// Not fatal. Only the force-visible diagnostic depends on it.
			if (!InstallVisibilityDetour())
				Log("the force-visible diagnostic (F5) is unavailable without that detour");

			// The control panel, drawn in game through the workshop's ImGui overlay.
			// The swapchain hook goes in on a worker thread, so this returning true
			// only means it started, not that it worked. Watch for the "overlay:" lines.
			if (!StartOverlayPanel())
				Log("the control panel is unavailable, so the co-op settings on the game's "
				    "own Config screen are the only way to change anything");

			LogPadSlots();

			// The input research left one unknown that only a running game can settle,
			// and it decides whether input injection at animate time works at all. Non
			// zero moves the pad latch inside animate, which is after the point we would
			// be writing. Logged first thing so the answer is in every log we ever read.
			Log("threaded pad mode = %lu (%s)", ThreadedPadMode(),
			    ThreadedPadMode() ? "LATCH IS INSIDE ANIMATE, injection order needs rechecking"
			                      : "latch is before animate, which is what injection assumes");
			LogInputState();

			// The frame loop, including the two things only a running game can answer:
			// whether the fixed timestep argument is in effect, and whether this build
			// is pacing from the wall clock, which is the one determinism hole lockstep
			// has to know about.
			LogMainLoopState();
			Log("clone input will use pad slot %ld", settings.padSlot);
			Log("ready. %s", HotkeySummary());
			return true;
		}

	} // namespace
} // namespace pilgrimage

// A plugin DLL attaches AFTER the exe's CRT startup, so FFXApplication already
// exists. That is fine, because patching a vtable slot is time-independent: the
// slot lives in .rdata at a fixed RVA and the next frame picks it up. Do not add a
// hook on a one-shot startup function here, it is a race you will lose.
//
// DllMain runs under the loader lock, so no LoadLibrary and no waiting on other
// threads. VirtualProtect, a dword store and CreateThread are all fine.
//
// There is deliberately no DLL_PROCESS_DETACH handler. This mod is loaded for the
// life of the process, and the one piece of engine state it modifies, the cull
// override, is restored whenever that override is switched back off.
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
	if (reason != DLL_PROCESS_ATTACH)
		return TRUE;

	DisableThreadLibraryCalls(module);
	pilgrimage::Startup();
	return TRUE;
}
