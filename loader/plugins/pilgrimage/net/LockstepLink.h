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
// A remote peer's input IS now applied, but not by writing the pad. It is read
// out of the clock by world/RemotePlayers.cpp and turned straight into a heading
// and a speed on that peer's character, which skips the engine's player control
// path entirely. That is better than a pad write rather than a workaround for
// one: the engine's path subtracts the LOCAL camera yaw, and the camera is not
// replicated, so two machines would turn the same stick into two different
// headings. The wire carries a world direction instead. See the InputFrame
// comment in workshop/Protocol.h.
//
// What is still not injected is the BOUND PLAYER's own input. The engine reads
// the pad inside FFX_MainStep, ahead of FFX_StepPacing, and drives the bound
// player from it using that local camera. So the host's own character is the one
// character in the world that is not yet driven from replicated bytes, which
// means it is the one that can still diverge. Closing that is the same frame
// order question as before, and it is now the only place it matters.
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

	// The live clock, or null when it is not running. Exposed so the remote player layer
	// can read the replicated input for the current step without this file having to know
	// what a character is.
	workshop::Lockstep* ActiveLockstep();

} // namespace pilgrimage
