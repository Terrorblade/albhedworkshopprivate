#pragma once

#include <stddef.h>
#include <stdint.h>

#include "workshop/Protocol.h"

// Making an FMV end at the same moment on both machines.
//
// ## WHAT THE NOTES SAID, AND WHAT THE BINARY SAYS
//
// The design this was built from said an FMV is a natural barrier because
// FFX_MainStep early-returns while a movie owns the frame, so neither machine is
// simulating and both sit on the same step for the whole video. The research note
// it came from, reversing\CUTSCENE.md section 7.2, said the same thing.
//
// THAT IS WRONG, and the whole shape of this file is a consequence of it being
// wrong. The early return tests the function the kit's address file calls
// FmvGetPlaybackState, and that function has nothing to do with video. It returns
// 2 when one of three USER-DRIVEN full-screen suspends is up: the overlay on
// remappable action 32, the Start-button in-cutscene pause, or the debug free
// camera. No FMV playback code writes any of them, and the step that forces the
// free-camera field to zero does so precisely WHILE a movie is running.
//
// The proof that settles it needs no new reversing at all. The script's movie
// waits are ATEL poll handlers, Movie library functions 1, 9, 10 and 11. The ATEL
// VM is stepped from inside the FFX_MainStep sub-step loop, which is after that
// early return. If the step were being thrown away, the polls would never be
// called and a movie could never finish. So the simulation runs all the way
// through a movie, and always has.
//
// ## SO WHAT IS ACTUALLY BROKEN
//
// Not the start. Movie:0 start and Movie:10 start are ATEL opcodes, the VM steps
// once per simulation step, and two lockstepped machines running the same script
// reach them on the same step. An FMV start needs no networked command and this
// file sends none.
//
// Not the body either. The simulation keeps stepping in lockstep, the video is
// decoded on a thread nobody is lockstepping, and that is fine because nothing
// simulated reads it.
//
// THE END IS BROKEN. Each machine's script unparks when ITS OWN decode thread
// finishes, which is a wall-clock event on hardware the other machine knows
// nothing about. One machine's script resumes at step 900 and the other's at step
// 950, and from the first step after that the two scripts are permanently fifty
// steps apart. That is not a hitch, it is an unrecoverable divergence, and it
// happens on every single FMV in the game.
//
// ## THE BARRIER GOES ON THE SCRIPT WAIT, NOT ON THE SIMULATION
//
// ffx::HookFmvWaits swaps the four Movie poll pointers in the ATEL library table.
// The shim calls the gate in this file on every poll, and when the engine's own
// wait has finished the gate may answer "not yet", which makes the poll return 0
// and the script stay parked for another step. Returning 0 is what the engine's
// own waits return while a movie plays, so this is not a new state for the VM.
//
// Each machine reports "my movie finished" as an ORDERED COMMAND. Every machine
// sees the same commands on the same steps, so every machine sees the last report
// land on the same step, so every machine lets its wait complete on that one
// step. Both scripts resume together. No extra release message is needed, and
// that matters, see the next section.
//
// ## WHY THE HOLD BYTE IS THE WRONG TOOL HERE, which was the first guess
//
// kHoldFmv exists and this file deliberately never raises it.
//
// Holding the simulation stops FFX_MainStep, and the mod's lockstep clock is
// advanced by that function's after-step observer, so a hold stops the clock. For
// a pause that is exactly right, because EVERY machine stops. For a movie it is
// exactly wrong, because the machine still playing its video must keep stepping
// to keep decoding and drawing it. Hold the finished machine and its clock stops
// while the playing machine's clock runs on, and when the hold lifts the two
// machines are on different step numbers, applying each other's input frames to
// the wrong steps, for the rest of the session.
//
// So the trap the design warned about, that an ordered command can never RELEASE a
// barrier because nobody's clock is advancing while anybody is held, does not
// apply to this barrier. Nothing is held. Every clock keeps running, so an ordered
// command is consumed normally, and the release is the ordered command. The
// warning is still correct about the mechanism it was written for, and the reason
// it does not bite here is worth stating plainly rather than looking like it was
// missed.
//
// ## THE CANCEL, WHICH WAS THE RESEARCH
//
// Found at FFX_Fmv_PollSkipButtons 0x6D9460, reached from
// FFX_Fmv_StepFromMainStep 0x645DF0 at the very top of FFX_MainStep. Two presses:
// START arms the on-screen prompt, then SQUARE performs the skip. Both go through
// FFX_Input__isPressed, a newly-pressed edge test on the PC action map, which is
// the merged keyboard-and-pad state with no port in it. So a skip is purely local
// input and without this file each player would skip their own movie and nobody
// else's.
//
// The local skip is NOT suppressed. It happens immediately, the way the shipped
// game does it, and then it is announced as an ordered command and every other
// machine applies the same skip when that command lands. Letting the local one
// through costs a few hundred milliseconds of the other player still watching,
// and it cannot cause a divergence because the barrier still decides the step
// both scripts resume on. Suppressing it would mean either re-implementing the
// Phyre seek the real path performs or scribbling the engine's input snapshot,
// for no gain.
//
// ## THE FOUR THINGS THAT WERE CALLED OUT AS LIKELY TO BITE
//
// 1. A CANCEL AND A NATURAL END ARE NOT THE SAME EVENT. They are separate command
//    reasons here. A cancel makes everybody skip, and then everybody's movie ends
//    and everybody reports done through the ordinary path, so the barrier logic
//    itself never has to know a cancel happened. Two players cancelling on the
//    same frame produces two cancel commands, and the second one is a no-op
//    because the skip flag is already set and the announce is once per barrier.
//
// 2. NO WALL-CLOCK TIMEOUT. The timeout is counted in SIMULATION STEPS from the
//    step the local wait finished, which both machines can compute and which does
//    not depend on anybody's frame rate. And it is measured from the END of the
//    local movie, not from its start, so the length of the movie does not come
//    into it: a five minute video and a five second one both leave the same
//    allowance for the two decode threads to disagree with each other.
//
// 3. A MACHINE THAT NEVER REACHES THE END must not freeze the other one forever.
//    On the timeout, the machine that is waiting asks for a give-up command, and
//    when it lands EVERY machine forces the skip and the barrier opens for a short
//    window counted from that same step. The stuck machine's movie is skipped out
//    from under it, which is what unparks its own wait. Both resume, both log it.
//
// 4. THE FROZEN MACHINE MUST SAY WHY. Nothing freezes here, which removes most of
//    that problem, but the script is parked and the readout and the log both name
//    the reason and the peer being waited on.
//
// ## A VIDEO THAT WILL NOT OPEN, which looked unfixable and is not
//
// Movie:10 start sets g_ffxFmvScriptRunning and g_ffxFmvScriptBlocking ONLY when
// FFX_Fmv_StartVideo actually created a player, and Movie:10's poll completes on
// its first look when blocking is 0. So a machine with a missing or unreadable
// video file finishes the wait on the step it entered it, while the other machine
// sits in it for two minutes.
//
// The first design read the local FMV state to decide whether a wait was worth
// arming, and that made this case unrecoverable: the machine with the video would
// arm, the machine without would not, and the first would wait out the whole
// timeout for a report that was never coming. So the gate arms for EVERY one of the
// four waits and asks nothing about local state. The machine with no video parks
// for as long as the other machine's video runs, both come out of the wait on the
// same step, and all the player on the broken machine loses is the video. It is
// still logged, because a player who sees a black screen for two minutes deserves
// to be told their video file is the problem.
//
// What that costs when nothing is wrong is one command round trip, about 200 ms of
// parked script per movie wait. On a wait that is already seconds to minutes long
// that is not worth optimising away, and trying to was the mistake.
//
// Game thread only, same as everything else in this layer.

namespace pilgrimage
{

	// ---------------------------------------------------------------------------
	// The command payload.
	//
	// It lives here and not in net/Commands.h because RequestCommand takes a void
	// pointer and a length, so nothing in that file needs to know its shape.
	//
	// WHY THERE IS NO BARRIER IDENTITY IN THE MATCH, which was the first design and
	// was wrong. The worry was that two movies back to back would let a report from
	// the first satisfy the barrier of the second, so reports carried a sequence
	// number that had to agree between machines. The case that breaks that is exactly
	// the case the number was for: a machine that never parked at all, because its
	// video would not open, never opens a barrier, so its counter stops matching
	// everybody else's for the rest of the session and every later report gets thrown
	// away as stale.
	//
	// It is not needed anyway. A report is stamped a few steps ahead and consumed on
	// exactly the step it is stamped for, and the barrier opens on the step the LAST
	// report lands. So at the instant a barrier opens there is no report for it still
	// in flight, and the next movie's reports are all stamped later than that. The
	// ordering the command channel already guarantees is the identity.
	//
	// The counter is still carried, because having the barrier number in both logs
	// when something goes wrong is worth one byte. Nothing matches on it.
	//
	// The video id is carried for the log only, for the same reason and one more: a
	// machine that failed to open the video has no id to report and would then never
	// match anybody.
	// ---------------------------------------------------------------------------
	enum FmvCommandReason
	{
		// This machine's movie wait has finished. The ordinary path, and the only one
		// the barrier needs in the normal case.
		kFmvReasonDone = 0,

		// Somebody pressed Start then Square. Everybody skips when this lands.
		kFmvReasonCancel = 1,

		// The barrier waited too many steps for a report that never came. Everybody
		// skips and the barrier opens. The loud, deliberate outcome rather than a
		// hang.
		kFmvReasonGiveUp = 2,
	};

	struct FmvCommand
	{
		uint8_t reason;  // FmvCommandReason
		uint8_t barrier; // the barrier sequence this is about, low byte
		uint8_t reserved[2];
		int32_t videoId; // for the log, never matched on
	};

	// 8 bytes against MaxCommandBytes of 96, so there is a lot of room to add to this
	// without touching the protocol.
	//
	// A static_assert rather than the array-size trick workshop/Protocol.h uses. That
	// file says it avoids static_assert for compiler-era reasons, but SteamAbi.h and
	// world/HeldObjects.h both use it and this toolchain is fine with it, so the
	// readable error message wins.
	static_assert(sizeof(FmvCommand) <= (size_t)workshop::MaxCommandBytes,
	    "FmvCommand does not fit in a command payload");

	// ---------------------------------------------------------------------------
	// How many simulation steps the barrier will wait for a report that has not
	// arrived before it gives up and makes everybody skip.
	//
	// 600 steps is about 20 seconds at 29.97 Hz. The number this has to cover is not
	// the length of a movie, it is how far apart two decode threads can finish the
	// SAME movie, because the count starts when the local wait finished. Twenty
	// seconds is enormous for that and still short enough that a genuinely stuck
	// machine does not leave the other player staring at a parked script.
	//
	// In steps rather than milliseconds on purpose. A wall-clock timeout fires at
	// different points on the two machines and would have to be sized for the worst
	// link rather than for the simulation.
	// ---------------------------------------------------------------------------
	const uint32_t kFmvBarrierTimeoutSteps = 600;

	// How long the barrier tolerates a wait that has NOT finished yet after it opened.
	//
	// The normal case never touches this. Every machine sees the last report land on
	// the same step, lets its wait through on that same call, and the open phase is
	// spent there and then. The window only matters on the give-up path, where the
	// forced skip needs a step or two before the engine's own wait agrees it is
	// finished.
	//
	// Eight steps is about 270 ms. Long enough for that, and short enough that the
	// next movie cannot plausibly start AND finish inside it, which is the one way
	// this number could let a barrier be skipped.
	const uint32_t kFmvBarrierOpenSteps = 8;

	// Installs the barrier hook on the four ATEL Movie poll handlers. Call once at
	// startup, not per session: with no session the gate lets every wait complete
	// exactly as the shipped game does, so a solo game is unchanged.
	bool InstallFmvSync();
	bool FmvSyncInstalled();

	void StartFmvSync();
	void StopFmvSync();
	bool FmvSyncActive();

	// Once per FRAME from the animate hook. Bookkeeping and the readout only: every
	// decision this file makes is made on the simulation step, inside the gate the
	// kit's shim calls. This exists so the state is dropped when a session goes away
	// and so the control panel line is current.
	void StepFmvSync();

	// True while this machine's script is parked waiting for another machine's
	// movie, which is the only thing this file ever does to the game.
	bool FmvBarrierWaiting();

	const char* FmvSyncStatus();
	void LogFmvSync();

} // namespace pilgrimage
