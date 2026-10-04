#pragma once

#include <windows.h>

namespace workshop
{
	class Lockstep;
}

// The lockstep clock, attached to the game's own simulation step.
//
// The kit's Lockstep class decides WHETHER a step may run. This file is the part
// that knows where a step actually happens in FFX, which is FFX_MainStep, and it
// is deliberately the only place in the mod that knows that.
//
// ## Why the gate goes on FFX_MainStep and not on animate
//
// One presented frame can run more than one simulation step. FFX_MainStepLoop
// loops on the pending step count, which is how the HD fast forward catches up.
// So counting animate calls would count frames, not steps, and the two machines
// would be agreeing on the wrong number. ffx::HookMainStep already offers a gate
// callback and an after-step observer, which is exactly the pair this needs.
//
// ## Why there are two kinds of hold
//
// Refusing a step inside FFX_MainStep is cheap and precise, but it leaves the
// render path drawing a scene whose display list was not rebuilt this frame.
// That is a configuration the shipped game never produces, so it is fine for a
// frame or two of network jitter and not fine for a long wait. For a long wait
// the right mechanism is the stall byte at FFXApplication+0x3AD, which is what
// the shipped pause menu uses and which every one of the four vtable bodies
// honours. So: short stalls are refused steps, long stalls escalate to the real
// hold, and the hold is released the moment input arrives.
//
// ## What this does and does not inject
//
// EVERY player character, this machine's own included, is now driven from the
// replicated bytes. The pad is read in exactly one place, the gate callback
// below, where it goes into the ring. Nothing downstream of that reads it.
//
// The driving itself is not here. world/PlayerDrive.cpp patches the engine's one
// call to FFX_Player__stepControl and runs it once per player character with
// that character's own ramps, which means both machines put identical input
// through identical code. It deliberately does NOT write the pad globals to do
// that: the pad is one slot and there are up to three characters, so the state
// is swapped per call instead. See ffx/Input.h.
//
// The one thing the wire has to carry for that to work is the OWNER'S camera
// yaw, because the driver resolves the stick against the camera and the camera
// is local. It goes through the engine's own fixed-yaw override. See the
// InputFrame comment in workshop/Protocol.h.
//
// Game thread only.

namespace pilgrimage
{

	// Installs the step hook. Call once at startup, whether or not networking is
	// ever used, because the hook has to be in place before a session starts. With
	// no session the gate always allows the step, so this costs one call per step.
	bool InstallLockstep();
	bool LockstepInstalled();

	// Starts and stops the lockstep clock against the live session. Called from the
	// networking layer when a session becomes active and when it goes away.
	void BeginLockstep();
	void EndLockstep();
	bool LockstepRunning();

	// Called once per frame from the animate hook, after the session has pumped.
	// Escalates a long stall to the real simulation hold, releases it when the wait
	// is over, and feeds the desync checksum.
	void ServiceLockstep(void* application);

	// Whether the gate is allowed to actually refuse a step. Off by default so the
	// exchange can be watched running with no risk of freezing the game, which
	// matters a lot while this is being brought up. The setting is local, since both
	// machines refusing is not a requirement for the measurement, only for the real
	// thing.
	void SetLockstepEnforced(bool enforced);
	bool LockstepEnforced();

	// A line for the control panel and the log.
	const char* LockstepSummary();

	// ---------------------------------------------------------------------------
	// Asking for the simulation to be held
	//
	// The hold byte at Application+0x3AD has to be re-asserted from the animate hook every
	// frame, so ServiceLockstep is its only writer. A second subsystem setting it directly
	// would be overwritten by that loop on the next frame, and the two would then fight.
	//
	// So anything that wants the game frozen raises a reason here instead, and the one
	// owner ORs every reason together. A reason stays raised until it is lowered, and a
	// reason raised while the lockstep stall escalation already holds the game costs
	// nothing.
	// ---------------------------------------------------------------------------
	enum HoldReason
	{
		// A world snapshot is going out. The host must not advance while the client is
		// assembling the block, or the client installs a world the host has already left.
		kHoldWorldTransfer = 1 << 0,

		// Somebody else has the Esc pause menu open. Their FFX_MainStep is not being
		// called at all, so they have stopped feeding the input ring and this machine
		// would otherwise either starve at the gate with no explanation or, with the
		// gate only measuring, run away alone. See menu/PauseSync.h, including why the
		// release of this one is driven by watching their input start flowing again
		// rather than by a second command.
		//
		// Raised only for a REMOTE pause. A local Esc menu already holds this machine
		// through the engine's own byte, and a second writer for the same condition
		// would just be racing animate's own latch.
		kHoldPauseMenu = 1 << 1,

		// An FMV is playing and this machine has reached the end of it while another
		// has not, or has not reached it yet. The barrier, in other words.
		//
		// Reserved here because this file is the single writer of the hold byte, and
		// adding a second writer for a new condition is the one thing the comment
		// above forbids. The FMV layer raises and lowers this through
		// HoldSimulationFor and ReleaseSimulationFor like everything else.
		kHoldFmv = 1 << 2,
	};

	void HoldSimulationFor(int reason);
	void ReleaseSimulationFor(int reason);
	int SimulationHoldReasons();

	// The live clock, or null when it is not running. Exposed so the remote player layer
	// can read the replicated input for the current step without this file having to know
	// what a character is.
	workshop::Lockstep* ActiveLockstep();

} // namespace pilgrimage
