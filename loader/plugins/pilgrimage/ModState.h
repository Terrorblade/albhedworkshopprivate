#pragma once

#include <windows.h>

// State shared between the game thread and the UI thread, split by who owns it.
//
//   Settings   the UI writes, the game thread reads
//   Requests   the UI raises, the game thread consumes and clears
//   Telemetry  the game thread writes, the UI reads for display
//   Counters   the game thread writes, both read
//
// Plain volatile plus interlocked requests is enough. The UI only writes tuning
// scalars and request flags, and a half-applied float for one frame is harmless.
// No game function is ever called off the game thread, which is the rule that
// makes this safe.
//
// The UI draws inside Present, which is normally the game's own thread but is not
// guaranteed to be, so the panel still goes through these rather than acting
// directly. See ui/OverlayPanel.h.

namespace pilgrimage
{

	// Where the clone's movement input comes from.
	namespace InputSource
	{
		const LONG Auto = 0; // pad slot 1 if it is there, keyboard otherwise
		const LONG Keyboard = 1;
		const LONG Pad = 2;
		const LONG Count = 3;
	} // namespace InputSource

	struct Settings
	{
		volatile LONG spawnIdIndex;     // index into SpawnableChrIds
		volatile LONG spawnPartyIndex;  // passed to SetPartyIndex, 255 means none
		volatile float spawnOffset;     // world units beside the player, per clone
		volatile LONG autoBindWalkmesh; // re-bind whenever m_walkmeshTri goes to -1

		volatile float walkSpeed;
		volatile float runSpeed;
		volatile float yawBiasDegrees; // dialled in by hand, see CloneDriver

		volatile LONG inputEnabled;
		volatile LONG requireForeground;
		volatile LONG cameraRelative; // 1 means input is relative to the camera
		volatile LONG inputSource;    // one of InputSource
		volatile LONG padSlot;        // which Phyre pad slot drives the clone

		volatile LONG forceVisible; // the detour clears Disp and Z on our clones
		volatile LONG disableCull;  // hold the engine's cull override on

		volatile LONG hostPort; // the UDP port a host listens on, and the
		                        // one a joiner dials on 127.0.0.1

		// Steam P2P when set, UDP on loopback when clear. Steam is the shipping
		// path. Loopback is how the layers above get tested without a second Steam
		// account and a second machine.
		volatile LONG useSteam;

		// The host's SteamID as text, 17 digits off their profile page. Not volatile
		// and not touched on the frame path: the UI thread sets it, and the game
		// thread reads it once when a connection starts.
		char hostSteamId[24];
	};

	// Raised by the UI thread or a hotkey, drained by the game thread. Everything
	// that has to touch a character goes through here, which is what keeps the UI
	// thread's "never call into FFX" rule absolute rather than case by case.
	struct Requests
	{
		volatile LONG spawn;
		volatile LONG despawn;
		volatile LONG despawnAll;
		volatile LONG dumpDiff;
		volatile LONG cycleClone;
		volatile LONG cycleSpawnIdForward;
		volatile LONG cycleSpawnIdBack;

		// Networking. Starting a link opens a socket and must not happen on the UI
		// thread, same rule as everything that touches a character.
		volatile LONG startHosting;
		volatile LONG startJoining;
		volatile LONG stopNetworking;

		// Toggles the engine's fixed timestep on and off at runtime. This is the
		// experiment that settles whether the step count is a pure function of a
		// constant or a function of the wall clock, which the whole lockstep plan
		// rests on. Writing app+0x3C touches the game, so it goes through here.
		volatile LONG toggleFixedTimeStep;

		// Starts the save block stability probe. It reads save data, so it has to
		// run on the game thread like everything else here.
		volatile LONG startHashProbe;

		// Lets the lockstep gate actually refuse a step. Off until asked for, so
		// the exchange can be watched running with no risk of freezing the game.
		volatile LONG toggleLockstepEnforce;

		// The two experiments the reversing work handed over, each one a single
		// keypress because each one decides a whole feature.
		//
		// lookAtInteractables and fireNearestExamine answer "can a character other
		// than the bound player fire a world trigger", which is the chest problem.
		// armEscMenuRow answers "does escmenu.swf append a row or fill fixed slots",
		// which is the only thing still standing between the mod's settings and the
		// game's own menu.
		volatile LONG lookAtInteractables;
		volatile LONG fireNearestExamine;
		volatile LONG armEscMenuRow;

		// The remote trigger pass, which is how a second player's movement fires the
		// world the way the first player's does.
		//
		// toggleTriggerFollow points the pass at the active clone, so the whole thing can
		// be proved with one spawned character and no network. toggleTriggerArmed decides
		// whether it actually commits the events it finds or only reports them, because
		// watching it pick the right target is worth doing before letting it fire.
		volatile LONG toggleTriggerFollow;
		volatile LONG toggleTriggerArmed;
		volatile LONG logTriggerPass;

		// World sync. The resync is for testing the transfer without reconnecting, and it
		// is a client-side action: a host asked to resync has nobody to ask.
		volatile LONG requestWorldResync;
		volatile LONG logWorldSync;

		// ctrl+F4: the host takes or gives back control of whatever menu is up. It
		// goes out as an ordered command, which needs the session, so it goes through
		// here like everything else that touches the game or the wire.
		volatile LONG toggleMenuOverride;
	};

	struct Telemetry
	{
		// position and motion of the clone holding the input focus
		volatile float posX, posY, posZ;
		volatile float speed;
		volatile float moveDirection;
		volatile float facing;
		volatile LONG walkmeshTriangle;

		// the render chain, which is what the invisibility work left behind
		volatile LONG instanceAttached;
		volatile LONG secondInstanceAttached;
		volatile LONG subMeshCount;
		volatile LONG playerSubMeshCount;
		volatile LONG subMeshesLinked;
		volatile LONG instanceShownByte;
		volatile LONG hideFlags;
		volatile LONG flags1, flags2;
		volatile LONG partyIndex;
		volatile float shadeAlpha;
		volatile float cameLength, clipZ;
		char drawGate[80];

		// camera and input
		volatile float cameraYaw;
		volatile LONG cameraYawValid;
		volatile LONG padBound;
		volatile LONG padsPresent;
		volatile LONG usingPad;

		// the pool
		volatile LONG poolLive, poolTotal;

		// the id the most recent clone was allocated with
		volatile LONG spawnedChrId;

		// The local Steam identity, read once on the game thread and published here
		// so the UI thread never calls into Steam. Empty until Steam is logged on,
		// which during startup is the normal state rather than a problem.
		char localSteamId[24];
		char personaName[40];
		volatile LONG steamLoggedOn;
	};

	struct Counters
	{
		volatile LONG frames;
		volatile LONG spawnAttempts;
		volatile LONG visibilityForcedFrames;
		volatile LONG cullOverrideHeld; // 1 while we are holding it on
	};

	extern Settings settings;
	extern Requests requests;
	extern Telemetry telemetry;
	extern Counters counters;

	// Startup values. Called once from DllMain, before any thread can read them.
	void ApplyDefaultSettings();

	// Describes every field of Settings to workshop's settings registry, which is what
	// lets the control panel, the settings file and eventually the game's own options
	// screen all work from one list instead of three. Also declares, per setting,
	// whether it would have to match on both machines in a networked session.
	//
	// Call it after ApplyDefaultSettings and before LoadModSettings, because loading
	// writes through the registry and so needs the records to exist.
	void RegisterModSettings();

	// The saved settings file, under <game dir>\AlBhedWorkshop\. Loading is best effort:
	// anything missing or out of range keeps the default.
	extern const wchar_t* const ModSettingsFileName;

	void LoadModSettings();

	// Writes the file if a setting has changed since the last call. Polled from the
	// UI timer, which is more reliable than saving at process shutdown.
	void SaveModSettingsIfChanged();

} // namespace pilgrimage
