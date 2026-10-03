#include "world/DialogueSync.h"

#include <stdio.h>
#include <string.h>

#include "ffx/Cutscene.h"
#include "ffx/Input.h"
#include "net/LockstepLink.h"
#include "net/NetLink.h"
#include "workshop/Lockstep.h"
#include "workshop/Log.h"
#include "workshop/Session.h"

namespace pilgrimage
{

	using namespace ffx;
	using namespace workshop;

	namespace
	{

		// The buttons a message window actually reads. Everything else is masked off
		// before it reaches the block.
		//
		// Why mask at all rather than pass the whole thing through: the replicated mask is
		// the FFX global button mask, which carries PC-only high bits that the 16-bit PS2
		// block has no room for, and FFX_Field_StepMenuOpenRequest tests
		// (pressed & 0xFFF) == 0x800 as an EXACT EQUALITY. Handing the window layer bits it
		// was not written to see is how an exact-equality test somewhere else quietly stops
		// matching.
		const DWORD DialogueButtons = Btn::Circle | Btn::Cross | Btn::Triangle | Btn::Square |
		                              Btn::Up | Btn::Down | Btn::Left | Btn::Right;

		bool g_installed = false;
		bool g_started = false;

		// The mask PrepareDialogueInput worked out for this step, which the hook callback
		// applies and does not recompute. See the header for why that split exists.
		WORD g_injectHeld = 0;
		WORD g_injectPressed = 0;
		bool g_injectArmed = false;

		// Per-peer button history for the dialogue edge, kept separate from the movement
		// one in RemotePlayers. They have to be separate: that one is consumed once per
		// step by the drive loop, and sharing it would mean whichever ran first ate the
		// edge and the other saw nothing.
		DWORD g_lastButtons[MaxPlayers];
		bool g_haveLast[MaxPlayers];
		uint32_t g_lastEdgeStep = 0;

		int g_owner = -1;        // whose box is open, -1 for none
		int g_pendingOwner = -1; // who asked for an examine and may be about to own one
		uint32_t g_pendingStep = 0;
		bool g_boxWasOpen = false;

		// How long an examine request stays a candidate for owning a box. An examine that
		// wins opens a box on the step it fires or the one after, so a short window is
		// enough, and a long one would hand a box to somebody whose examine was refused
		// several steps ago.
		const uint32_t PendingOwnerSteps = 4;

		LONG g_boxesDriven = 0;
		char g_status[200] = "dialogue: not hooked";

		// Runs INSIDE FFX_MesWin_SamplePadPort0, after the game filled the block from port
		// 0 and before the 8 windows are walked.
		bool __cdecl PadCallback(void)
		{
			if (!g_started || !g_injectArmed)
			{
				// Leave the block exactly as the engine filled it. This is the path solo
				// play takes, so the shipped behaviour is untouched when nothing is
				// running.
				return false;
			}

			return SetDialoguePad(g_injectPressed, g_injectHeld);
		}

		int LocalPeerId()
		{
			Session* session = ActiveSession();
			if (!session)
				return -1;

			const uint8_t local = session->LocalPeer();
			return (local == PeerUnassigned) ? -1 : (int)local;
		}

	} // namespace

	bool InstallDialogueSync()
	{
		if (g_installed)
			return true;

		g_installed = HookDialoguePad(&PadCallback);
		if (g_installed)
		{
			Log("dialogue: pad hook installed on the message window sampler. Boxes will run "
			    "off replicated input while a session is active.");
			strcpy_s(g_status, sizeof(g_status), "dialogue: hooked, idle");
		}
		else
		{
			Log("dialogue: could not hook the message window sampler, so a box will be "
			    "driven by whichever pad is plugged into each machine. That is a divergence "
			    "the moment anybody talks to anything.");
			strcpy_s(g_status, sizeof(g_status), "dialogue: HOOK FAILED");
		}

		return g_installed;
	}

	bool DialogueSyncInstalled()
	{
		return g_installed;
	}

	void StartDialogueSync()
	{
		g_started = true;
		g_owner = -1;
		g_pendingOwner = -1;
		g_boxWasOpen = false;
		g_injectArmed = false;
		g_boxesDriven = 0;

		for (int i = 0; i < MaxPlayers; ++i)
		{
			g_lastButtons[i] = 0;
			g_haveLast[i] = false;
		}

		strcpy_s(g_status, sizeof(g_status), "dialogue: syncing, no box open");
	}

	void StopDialogueSync()
	{
		g_started = false;
		g_injectArmed = false;
		g_owner = -1;
		g_pendingOwner = -1;
		strcpy_s(g_status, sizeof(g_status),
		    g_installed ? "dialogue: hooked, idle" : "dialogue: not hooked");
	}

	void NoteDialogueOwner(int peer)
	{
		if (peer < 0 || peer >= MaxPlayers)
			return;

		Lockstep* clock = ActiveLockstep();
		g_pendingOwner = peer;
		g_pendingStep = clock ? clock->CurrentStep() : 0;
	}

	int DialogueOwner()
	{
		return g_owner;
	}

	void PrepareDialogueInput()
	{
		if (!g_started || !g_installed)
			return;

		Lockstep* clock = ActiveLockstep();
		if (!clock)
		{
			g_injectArmed = false;
			return;
		}

		const uint32_t step = clock->CurrentStep();

		// Only once per step. The gate can be asked more than once in a frame, and
		// recomputing the edge on the second call would consume it.
		if (g_injectArmed && step == g_lastEdgeStep)
			return;

		const bool boxOpen = DialogueOpen();

		// A box just appeared. Decide whose it is, now, while the examine that opened it is
		// still recent.
		if (boxOpen && !g_boxWasOpen)
		{
			const bool pendingIsFresh = g_pendingOwner >= 0 &&
			                            (step - g_pendingStep) <= PendingOwnerSteps;

			// Falling back to the local peer is right rather than lazy. The local player's
			// examine goes through the engine's own path, which this layer never sees, so
			// "a box opened and nobody asked for it through the remote pass" means the
			// person sitting here opened it.
			g_owner = pendingIsFresh ? g_pendingOwner : LocalPeerId();
			g_pendingOwner = -1;

			InterlockedIncrement(&g_boxesDriven);
			Log("dialogue: a box opened on step %u, peer %d is driving it%s",
			    (unsigned)step, g_owner,
			    pendingIsFresh ? "" : " (nobody asked through the remote pass, so it is ours)");
		}

		if (!boxOpen && g_boxWasOpen)
		{
			g_owner = -1;
			g_injectArmed = false;
		}

		g_boxWasOpen = boxOpen;

		if (!boxOpen || g_owner < 0 || g_owner >= MaxPlayers)
		{
			// Nothing open, so do not touch the block at all. Field input has to keep
			// working and the menu open test lives in the same block.
			g_injectArmed = false;
			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "dialogue: syncing, no box open, %ld driven so far", g_boxesDriven);
			return;
		}

		const InputFrame* frame = clock->InputForStep((uint8_t)g_owner);
		if (!frame)
		{
			g_injectArmed = false;
			return;
		}

		const DWORD buttons = frame->buttons & DialogueButtons;

		// The edge, from two replicated step values rather than from the engine's
		// wall-clock timers. A first step with no history reports no press, which is right:
		// a button already held when a box opened must not count as the press that answers
		// it.
		const DWORD previous = g_haveLast[g_owner] ? g_lastButtons[g_owner] : buttons;
		const DWORD pressed = buttons & ~previous;

		g_lastButtons[g_owner] = buttons;
		g_haveLast[g_owner] = true;
		g_lastEdgeStep = step;

		g_injectHeld = (WORD)buttons;
		g_injectPressed = (WORD)pressed;
		g_injectArmed = true;

		_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
		    "dialogue: peer %d driving a box, held %04X pressed %04X", g_owner,
		    (unsigned)g_injectHeld, (unsigned)g_injectPressed);
	}

	const char* DialogueSyncStatus()
	{
		return g_status;
	}

	void LogDialogueSync()
	{
		Log("=== dialogue ===");
		Log("%s", g_status);

		if (!g_installed)
		{
			Log("the sampler hook is not installed, so every box runs off the local pad of "
			    "whichever machine is showing it");
			return;
		}

		DialogueState state;
		if (ReadDialogueState(&state))
		{
			const char* kindName = "none";
			if (state.kind == dialogueOpen)
				kindName = "open, not waiting";
			else if (state.kind == dialogueWaiting)
				kindName = "WAITING for a press";

			Log("box: %s, window %d, state %u, attr %04X, flags %02X, chosen option %d, %d "
			    "of 8 windows open",
			    kindName, state.windowIndex, (unsigned)state.state, (unsigned)state.attr,
			    (unsigned)state.flags, (int)state.chosenOption, state.openWindows);

			if (state.openWindows > 1)
				Log("MORE THAN ONE WINDOW IS OPEN. The input focus is a single global that "
				    "gets re-asserted onto the highest-indexed waiting window every frame, "
				    "so a lower-indexed one waits forever. See COOP_DESIGN.md.");
		}
		else
		{
			Log("box: the records are not readable yet");
		}

		WORD held = 0, pressed = 0;
		if (ReadDialoguePad(&held, &pressed))
			Log("pad block: held %04X, pressed %04X", (unsigned)held, (unsigned)pressed);

		Log("owner %d, %ld boxes driven, local peer %d", g_owner, g_boxesDriven,
		    LocalPeerId());
		Log("the engine's auto-repeat does not apply to the bits we replace, because the "
		    "edges come from comparing two replicated steps rather than from a timer");
	}

} // namespace pilgrimage
