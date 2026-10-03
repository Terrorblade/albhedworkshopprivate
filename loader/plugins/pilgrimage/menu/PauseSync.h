#pragma once

// Making the Esc pause menu stop both machines instead of one.
//
// The Esc menu is the PC port's escmenu.swf overlay and it is the real pause: when it
// is open, FFXApplication::animate skips FFX_MainStepLoop entirely, so FFX_MainStep is
// never called on that machine.
//
// ## WHAT THAT MEANS FOR LOCKSTEP, measured rather than assumed
//
// The lockstep gate IS FFX_MainStep. ffx::HookMainStep detours that function, and
// LockstepLink's GateCallback is what submits this machine's input for a step a little
// way ahead. So when the Esc menu goes up:
//
//   * FFX_MainStep stops being called, so the gate stops being called, so
//     Lockstep::SubmitLocalInput stops being called. The pausing machine stops feeding
//     the other one.
//   * The animate HOOK still runs, because it runs after the original animate and the
//     original is what skips the step loop. So the session still pumps, messages still
//     flow, and the pausing machine can still SEND. It just cannot consume a command,
//     because commands are only visible on the step they were stamped for and its clock
//     has stopped.
//   * The other machine therefore runs exactly InputDelay more steps, finds nothing in
//     the ring, and StepGate returns GateWaiting.
//
// SO THE ANSWER TO "DOES IT STARVE OR RUN AWAY" IS BOTH, DEPENDING ON ONE FLAG. With
// the gate enforcing it starves: two refused steps and then LockstepLink escalates to
// the hold byte, so roughly 100 ms after the pause the other machine freezes with no
// explanation at all, looking exactly like a dropped connection. With the gate only
// measuring, which is still the default until shift+F4 is pressed, it runs away and
// keeps simulating alone, which is an unrecoverable divergence rather than a hitch.
//
// Both of those are bad for different reasons, and that is the case for doing this
// deliberately.
//
// ## THE ASYMMETRY, which is the one awkward thing in this file
//
// The RAISE is ordered: the pausing peer asks for a command, the host stamps it, and on
// that step every other machine raises kHoldPauseMenu. Both machines then stop on a
// named step rather than drifting into a stall.
//
// The RELEASE cannot be ordered, and this is not laziness. Ordering a command requires
// the clock to reach the step it was stamped for, and while everyone is paused nobody's
// clock is moving. A resume delivered as an ordered command would be queued on every
// machine and consumed by none of them, and the pause would be permanent. So the
// release is driven from the frame path instead, from three conditions that need no
// step to be observed:
//
//   1. OUR OWN Esc menu, read straight out of the engine every frame.
//   2. A paused peer's replicated input starting to flow again, which is the ground
//      truth for "their FFX_MainStep is running". Lockstep::PeerInputLead is frozen
//      while they are paused because both halves of it are frozen, and it climbs the
//      moment they resume.
//   3. That peer leaving the session.
//
// Releasing a few frames apart on the two machines costs nothing, because nothing was
// simulated while the hold was up: whichever machine resumes first immediately stalls
// at the gate waiting for the other, which is the mechanism that was already there.
//
// There is deliberately NO WALL-CLOCK TIMEOUT in here. A pause is a human action with
// no sensible duration, and condition 2 is a real observation rather than a guess, so a
// timer would only ever fire when it was wrong. The lockstep gate is the backstop: if
// the hold is released early for any reason, the gate refuses the step instead.
//
// ## LEGIBILITY, which matters more here than anywhere else in the mod
//
// Over the internet a remote pause arrives after a delay with no visible cause. A
// player whose game has just stopped dead will assume the connection dropped. So the
// frozen side is told, by name, who paused it, in the log and in the control panel
// line. The same goes for the host override in MenuSync: a silent loss of control reads
// as a broken mod.

namespace pilgrimage
{

	void StartPauseSync();
	void StopPauseSync();
	bool PauseSyncActive();

	// Once per FRAME, from the animate hook, BEFORE ServiceLockstep so the hold reason
	// is current by the time the one owner of the hold byte acts on it.
	//
	// The frame path rather than the step path on purpose: this has to keep working
	// while no step is running, which is the entire point.
	void StepPauseSync();

	// Once per simulation STEP, from the lockstep gate beside PrepareMenuInput. This is
	// the ordered half: it is where a peer's pause is raised on the exact step the host
	// named.
	void ApplyPauseCommands();

	// Is anybody, us included, holding the game paused.
	bool AnyonePaused();

	// Which peers report being paused, as a bit per peer. For the readout.
	int PausedPeerMask();

	const char* PauseSyncStatus();
	void LogPauseSync();

} // namespace pilgrimage
