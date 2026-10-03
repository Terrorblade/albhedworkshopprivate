#pragma once

#include <windows.h>

// Making a message box advance and a choice get answered the same way on both machines.
//
// This is the second half of "a client can open a chest". The trigger pass already gets
// the event fired on both machines on the same step. What happens next is that a script
// puts a box on screen and waits, and until this file existed that box was driven by
// whichever hardware pad happened to be plugged into the machine you were sitting at.
//
// THREE THINGS WERE WRONG WITH THAT, and all three go away together.
//
// 1. PORT 0 IS HARDCODED. FFX_MesWin_SamplePadPort0 fills the shared menu pad block from
//    pad port 0 and nothing else, so a second player's pad never reached a box at all.
//
// 2. THE LOCAL PAD IS UNDELAYED. Under lockstep every machine simulates the same step
//    from the same replicated input, and the live pad is not that: it is this frame's
//    hardware state, which the other machine will not see for another InputDelay steps.
//    A box advancing off the live pad therefore advances one machine early.
//
// 3. THE AUTO-REPEAT IS WALL-CLOCK DERIVED. First repeat at 0.2333 s, then every
//    0.1333 s, from FFX_Input__getTimeSeconds. A single confirm press is a pure edge test
//    and is deterministic. A HELD DIRECTION in a choice list is not, and a choice list is
//    exactly where a held direction gets used.
//
// THE FIX is one hook and one assignment. ffx::HookDialoguePad detours the sampler, which
// runs after the game has filled the block and before the 8 windows are walked, and that
// is the only point in the frame where a written value is both unclobbered and seen. From
// there ffx::SetDialoguePad REPLACES the block with the owner's replicated mask. Note
// replaces, not ORs: leaving the local bits in is bug 2 again.
//
// Replacing the mask also takes the engine's auto-repeat out of the picture for free,
// because the edges now come from comparing two replicated step values rather than from a
// timer. That is bug 3 gone without a line of code aimed at it.
//
// WHO OWNS A BOX. Whoever's examine opened it. A box opened by the local player's own
// examine is driven by the local player's replicated input, and a box opened by a remote
// player's is driven by theirs. The owner is noted when an examine is requested and
// committed when a box actually appears, because an examine request does not always win:
// a nearer actor can take the winner slot, or the engine can refuse it outright.
//
// WHAT THIS DOES NOT TRY TO FIX. A choice box freezes BOTH characters, because it sets
// ctx+1 bit 0x04 and that blocks FFX_Atel_BindPlayerChr, leaving the unbind's "zero every
// controllable CHR's move speed" in effect. That is the engine's own behaviour and it
// stays: being unable to wander off mid-conversation is what stops one player walking a
// script into a state it was never written for.
//
// Nor does it address the deeper problem that a choice ARMS inside
// FFX_MesWin_DrawAllWindows, which FFX_MainStep skips on a catch-up step. That is a draw
// pass dependency inside the simulation and it is recorded in COOP_DESIGN.md. Driving the
// input deterministically does not make the arming deterministic, and the two are separate
// jobs.

namespace pilgrimage
{

	// Installs the pad hook. Call once at startup, not per session: the hook is harmless
	// while nothing is running because the callback leaves the block alone when there is no
	// clock, so solo play behaves exactly as the shipped game does.
	bool InstallDialogueSync();
	bool DialogueSyncInstalled();

	void StartDialogueSync();
	void StopDialogueSync();

	// Call once per simulation step from the gate, BEFORE the game steps.
	//
	// This is what decides the mask for the step, and it is separate from the hook callback
	// on purpose. The sampler runs inside FFX_MainStep's sub-step loop, so the callback can
	// fire more than once per step. Working out the button EDGE in there would make the
	// edge depend on the sub-step count, and deriving anything in a lockstep simulation
	// from how many sub-steps a machine happened to run is the bug this whole layer exists
	// to avoid. So the edge is computed once here, per step, and the callback only applies
	// what it finds.
	void PrepareDialogueInput();

	// Called when a player asks for an examine, so a box that opens shortly afterwards
	// knows whose it is. peer is a session peer id, not a trigger slot.
	void NoteDialogueOwner(int peer);

	// Which peer is driving the open box, or -1 when no box is open.
	int DialogueOwner();

	const char* DialogueSyncStatus();
	void LogDialogueSync();

} // namespace pilgrimage
