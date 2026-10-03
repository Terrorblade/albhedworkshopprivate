#include "menu/MenuSync.h"

#include <stdio.h>
#include <string.h>

#include "coop/Ownership.h"
#include "ffx/Cutscene.h"
#include "ffx/GameState.h"
#include "ffx/Input.h"
#include "ffx/MenuSystem.h"
#include "net/Commands.h"
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

		// ---------------------------------------------------------------------------
		// THE AUTO-REPEAT, derived from the step counter rather than transmitted.
		//
		// This is a deliberate departure from what reversing\MENU_SYSTEM.md recommends,
		// so here is the whole argument.
		//
		// The engine's repeat is paced off the wall clock. FFX_MenuSys_SamplePad keeps,
		// per button bit, an accumulator of real elapsed seconds and compares it against
		// 0.4666666388511658. When it trips it sets the repeat bit and RESETS the
		// accumulator to 0.29999998. That reset value is not an interval, which is the
		// detail that matters: the comparison is still against 0.4666666, so the gap
		// between repeats is
		//
		//     0.4666666388511658 - 0.29999998 = 0.1666666...  seconds
		//
		// and at one simulation step of 1/29.97 s that is 5 steps, not the 9 that
		// 0.3 s would suggest. The 14 and the 9 are the original PS2 frame counts at a
		// flat 30 fps, 14/30 = 0.46666 and 9/30 = 0.3, and the port turned the first
		// into a threshold and the second into a seed. That silently changed the shipped
		// interval from 9 frames to 5. The engine still writes holdCount[i] = 9 when it
		// fires, and that store is dead: holdCount is only ever tested == 0, and only on
		// a fresh-press frame, and a release zeroes it.
		//
		// WHY DERIVING BEATS TRANSMITTING, two reasons and the second is the real one.
		//
		// One, it costs nothing on the wire for something only menus use, and it is
		// deterministic by construction rather than by trusting that a payload arrived.
		//
		// Two, THERE IS NO STABLE SHIPPED BEHAVIOUR TO BE FAITHFUL TO. Both thresholds
		// are exact 30 fps frame counts and the game runs at 29.97, so both sit within
		// a fraction of a frame of a boundary: 14 steps clears the first threshold by
		// 1.4% of a frame and 5 steps clears the interval by 0.5% of a frame. At exactly
		// 1/30 they would tip over to 15 and 6. The accumulator adds real measured wall
		// time, so under ordinary frame jitter the shipped repeat flutters between 5 and
		// 6 steps on a single machine. Transmitting that would faithfully replicate
		// noise.
		//
		// Three, and this one only shows up over the internet: with the repeat derived,
		// a HELD direction repeats locally on both machines at the right rate from the
		// replicated held mask, so scrolling a list is smooth and only the initial press
		// and the release pay the input delay. Transmitting the mask would have made
		// every single repeat tick its own latency-sensitive wire event, so one held
		// press down a long item list would have been dozens of them instead of two.
		//
		// Start and Select are excluded from repeat by the engine and are excluded here.
		// ---------------------------------------------------------------------------

		// Steps held before the first repeat. 0.4666666388511658 / (1/29.97) = 13.985,
		// so the 14th accumulating step is the one that trips it.
		const int kFirstRepeatSteps = 14;

		// Steps between repeats after that. (0.4666666388511658 - 0.29999998) / (1/29.97)
		// = 4.995, so 5.
		const int kRepeatIntervalSteps = 5;

		// Where the counter is put back to after a repeat fires, so the next one is
		// kRepeatIntervalSteps away. It comes out as 9, which is exactly the dead
		// holdCount[i] = 9 the engine writes. That is not a coincidence, it is the
		// original PS2 frame count showing through.
		const int kRepeatReloadSteps = kFirstRepeatSteps - kRepeatIntervalSteps;

		// The bits the engine refuses to auto-repeat.
		const WORD kNoRepeatBits = (WORD)(Btn::Start | Btn::Select);

		// What reaches the menu. The whole 16-bit PS2 mask, because the menu uses all of
		// it and the block is 16 bits wide. The PC-only high bits of the replicated
		// button mask are dropped here rather than relied on being ignored.
		const DWORD kMenuButtons = Btn::Ps2Mask;

		// ---------------------------------------------------------------------------
		// THE SPHERE GRID'S RAW STICK, which the pad block does not cover.
		//
		// MENU_SYSTEM.md says the 192-byte block is the complete menu input surface.
		// For module 19 that is not true. Two functions in the grid read the pad
		// directly through FFX_MenuSys_ReadAnalogByte, which goes to
		// FFX_Pad__readAnalogByte and never looks at the block:
		//
		//   FFX_Menu_SphereGridReadPad 0xA57520   ORs a synthesised Up or Down bit into
		//                                         the grid's own held mask, threshold
		//                                         +-64 on axis 3
		//   FFX_Menu_SphereGridReadStick 0xA56BD0 reads axes 2 AND 3 for free cursor
		//                                         panning and accumulates a zoom float
		//                                         at MenuWork + 71140
		//
		// So on the one screen the co-op requirement cares most about, both machines
		// would still be reading whichever controller is plugged into them, and the grid
		// cursor would come apart on the first stick nudge.
		//
		// THE FIX IS TO NEUTRALISE IT, NOT REPLICATE IT, and the reason is that there is
		// nothing to replicate it with. InputFrame carries a camera-resolved world
		// heading and a magnitude, which is the right encoding for walking a character
		// and useless for a menu cursor, and adding raw axes to it is a wire format
		// change that affects every other subsystem.
		//
		// FFX_MenuSys_ReadAnalogByte checks FFX_VirtualPad_IsEnabled first and returns
		// FFX_VirtualPad_GetAxis when it is set. FFX_VirtualPad_Latch resets all four
		// axes to 0x80 every frame. So an ENABLED BUT NEVER WRITTEN virtual pad reads as
		// a permanently centred stick, identically on both machines, and the grid becomes
		// dpad-only. The dpad does everything the stick does on that screen.
		//
		// It is switched on only while module 19 is actually stepping, so every other
		// screen behaves exactly as briefed. The costs, stated plainly: while it is on
		// FFX_MenuSys_SamplePad takes its virtual branch and stops computing the
		// stick-synthesised dpad mask, which does not matter because this file overwrites
		// all nine masks anyway, and an ATEL script that wanted to drive the grid through
		// the same virtual pad would be fighting us. Scripts do that on both machines
		// equally, so it is a cosmetic risk rather than a divergence, but it is a risk.
		const bool kNeutraliseGridStick = true;

		bool g_installed = false;
		bool g_started = false;

		// The frame the callback applies. Decided once per step by PrepareMenuInput and
		// not recomputed inside the hook. See the header for why that split is not
		// optional.
		MenuPadFrame g_inject;
		bool g_armed = false;

		// Which peer opened the menu, which is rule 3. Defaults to the host rather than
		// to the local peer: a menu that is up with no open command behind it has to have
		// SOME driver, and the host is the only answer both machines reach with no
		// message. Guessing "us" would have both machines driving themselves, which
		// diverges on the first button.
		uint8_t g_opener = HostPeer;
		bool g_haveOpener = false;

		// The resolved driver for the current step, and the host override.
		int g_driver = -1;
		bool g_override = false;

		// Per-button step counters for the derived repeat. One set, not one per peer,
		// because only the driver's mask ever feeds them and they are cleared whenever
		// the driver changes. Both machines change driver on the same step, so both clear
		// on the same step.
		int g_holdSteps[16];
		WORD g_lastHeld = 0;
		bool g_haveLastHeld = false;

		// Only decide once per step. A refused step calls the gate again with the same
		// step number, and running the repeat counters twice would double the rate.
		uint32_t g_decidedStep = 0;
		bool g_decided = false;

		// An open we ordered ourselves is pending in the engine's global. Without this
		// the suppression pass would cancel our own replicated open on the next step.
		bool g_orderedOpenPending = false;

		// Did we turn the virtual pad on. Tracked so it is only ever turned off again by
		// the code that turned it on, and never while a script owns it.
		bool g_virtualPadOn = false;

		LONG g_opensRelayed = 0;
		LONG g_opensApplied = 0;
		LONG g_opensRefused = 0;  // the engine would not take a replicated open
		LONG g_missingInput = 0;  // the driver's input was not in the ring
		LONG g_dialogueClash = 0; // a box and a menu live in the same step
		LONG g_driverChanges = 0;

		char g_status[220] = "menu: not hooked";

		int LocalPeerId()
		{
			Session* session = ActiveSession();
			if (!session)
				return -1;

			const uint8_t local = session->LocalPeer();
			return (local == PeerUnassigned) ? -1 : (int)local;
		}

		// A player's name and peer id, for the log and the readout. Over the internet a
		// held or overridden machine has to be told WHO did it by name, or it reads as a
		// dropped connection or a broken mod.
		//
		// Three rotating buffers rather than one static, because two PeerLabel calls in
		// one Log would otherwise both point at the same bytes and print the same name,
		// which turns "the driver changed from A to B" into "from B to B". Three is
		// enough for every call site here and the rotation is per call, not per peer.
		const char* PeerLabel(int peer)
		{
			static char labels[3][48];
			static int next = 0;

			char* label = labels[next];
			next = (next + 1) % 3;

			Session* session = ActiveSession();
			const PeerInfo* info = (session && peer >= 0) ? session->Peer(peer) : NULL;

			if (info && info->name[0])
				_snprintf_s(label, 48, _TRUNCATE, "%s (peer %d)", info->name, peer);
			else
				_snprintf_s(label, 48, _TRUNCATE, "peer %d", peer);
			return label;
		}

		// A neutral frame: no buttons, sticks centred.
		//
		// NOT ffx::ClearMenuPad, and this one bit a hole in an earlier draft.
		// FFX_MenuSys_ClearPad memsets the whole 0xC0, which includes the two arm bytes
		// at +0x00 and +0x01 that FFX_MesWin_SamplePadPort0 bails out on, plus the 16
		// hold counters and both float timer arrays. The game only ever calls it on a
		// module boundary, from FFX_Module_Stop and FFX_MenuSys_Enter, and gets away with
		// it. Calling it every step would hold the message window's pad read disarmed and
		// stop the passenger being able to advance a dialogue box, with the menu looking
		// perfectly fine. WriteMenuPad touches the nine masks and the two analog bytes and
		// nothing else, which is exactly the right surface.
		//
		// The analog bytes are 0x80, not 0. Zero is full deflection and would synthesise
		// a held Up and Left.
		MenuPadFrame NeutralMenuPad()
		{
			MenuPadFrame f;
			memset(&f, 0, sizeof(f));
			f.analogX = MenuPadBlock::AnalogCentre;
			f.analogY = MenuPadBlock::AnalogCentre;
			return f;
		}

		// Spreads one set of masks across every field a screen can observe.
		//
		// Why all nine rather than just held and pressed: the three accessors every
		// screen reads through pick their field at runtime.
		// FFX_MenuSys_GetHeld returns the SYNTHESISED mask at +0x22 whenever no real dpad
		// bit is set in +0x12, and FFX_MenuSys_GetRepeat returns the pad layer's own
		// word10 at +0x16 when a real dpad bit IS set and the auto-repeat mask at +0x26
		// when it is not. That branch is a detail of how the game folds the stick into the
		// dpad, and the clean way to be immune to it is to make every field say the same
		// thing. MENU_SYSTEM.md lists the fields but does not mention the branch, and an
		// injector that writes only +0x12 and +0x26 gets a repeat that works with the
		// stick and not with the dpad.
		//
		// The sticky copy at +0x18 is in there too. It is the only field the Sphere Grid
		// reads, and it ignores +0x12 completely.
		MenuPadFrame SpreadMasks(WORD held, WORD pressed, WORD repeat)
		{
			MenuPadFrame f = NeutralMenuPad();
			f.held = held;
			f.synthHeld = held;
			f.heldSticky = held;
			f.pressed = pressed;
			f.synthPressed = pressed;
			f.pressedSticky = pressed;
			f.word10 = repeat;
			f.word10Sticky = repeat;
			f.repeat = repeat;
			return f;
		}

		void ResetRepeatState()
		{
			for (int i = 0; i < 16; ++i)
				g_holdSteps[i] = 0;
			g_lastHeld = 0;
			g_haveLastHeld = false;
		}

		// The engine's repeat, in steps instead of seconds. See the long note above.
		WORD DeriveRepeat(WORD held, WORD pressed)
		{
			WORD repeat = 0;

			for (int i = 0; i < 16; ++i)
			{
				const WORD bit = (WORD)(1u << i);
				if ((bit & kNoRepeatBits) != 0)
					continue;

				if ((pressed & bit) != 0)
				{
					// The press frame emits a repeat and does NOT advance the timer:
					// the engine's increment lives in the branch it only takes when the
					// button is not newly pressed. So the sequence is fire, then
					// kFirstRepeatSteps of nothing, then fire.
					repeat |= bit;
					g_holdSteps[i] = 0;
					continue;
				}

				if ((held & bit) == 0)
				{
					g_holdSteps[i] = 0;
					continue;
				}

				++g_holdSteps[i];
				if (g_holdSteps[i] >= kFirstRepeatSteps)
				{
					repeat |= bit;
					g_holdSteps[i] = kRepeatReloadSteps;
				}
			}

			return repeat;
		}

		// ---------------------------------------------------------------------------
		// WHO DRIVES. Three rules, replicated state only.
		// ---------------------------------------------------------------------------
		int DecideDriver()
		{
			// 1. The host took control. Everything else is off.
			if (g_override)
				return (int)HostPeer;

			// 2. The Sphere Grid, where control follows the character being edited.
			//
			//    SphereGridCharacter is MenuWork + 71100, which is the byte
			//    FFX_SaveData_SpendSphereLevels is actually charged against. The
			//    menu-wide cursor at MenuCursorChar is NOT the same field and the two
			//    legitimately differ for a frame during an L1/R1 switch, so gating on
			//    the one that gets charged is the difference between a gate that holds
			//    and a gate that holds except during the one frame somebody is switching.
			if (SphereGridActive())
			{
				const BYTE editing = SphereGridCharacter();
				if (editing != kCharNone)
				{
					const int owner = OwnerOfCharacter((int)editing);
					if (owner != kNoOwner)
						return owner;
				}
				// The work block is not readable yet, which happens for the frame or two
				// while the grid builds itself. Fall through to the opener rather than
				// hand control to nobody.
			}

			// 3. Whoever opened it.
			return (int)g_opener;
		}

		// ---------------------------------------------------------------------------
		// The virtual pad, used only to take the local stick away from the grid.
		// ---------------------------------------------------------------------------
		void ServiceGridStickNeutraliser()
		{
			const bool want = g_started && kNeutraliseGridStick && SphereGridActive();

			if (want)
			{
				// Re-asserted every step rather than set once, because
				// FFX_MenuSys_StepFrame calls the engine's own virtual pad reset on menu
				// entry and an ATEL script can turn it off at any time.
				if (!VirtualPadEnabled())
				{
					if (SetVirtualPadEnabled(true) && !g_virtualPadOn)
					{
						g_virtualPadOn = true;
						Log("menu: the sphere grid is up, so the ATEL virtual pad is on to "
						    "take the local stick away from it. The grid reads the raw pad "
						    "for the stick through FFX_MenuSys_ReadAnalogByte, which the pad "
						    "block does not cover, and the virtual pad's latch keeps every "
						    "axis centred on both machines. The grid is dpad only in a "
						    "session.");
					}
				}
				return;
			}

			if (g_virtualPadOn)
			{
				SetVirtualPadEnabled(false);
				g_virtualPadOn = false;
				Log("menu: the sphere grid is gone, virtual pad off, the stick goes back to "
				    "the local player");
			}
		}

		// ---------------------------------------------------------------------------
		// Part one of the step: stop the engine opening a menu on one machine only.
		// ---------------------------------------------------------------------------
		void SuppressAndRelayLocalOpen(Lockstep* clock)
		{
			if (!MenuOpenRequested())
			{
				// The engine consumed whatever was pending, ours included.
				g_orderedOpenPending = false;
				return;
			}

			// Our own replicated open, on its way to being consumed by
			// FFX_MenuSys_PollOpenAndStep inside this very step. Leave it alone.
			if (g_orderedOpenPending)
				return;

			const int mode = MenuRequestedMode();

			// Only mode 0 is intercepted. The cloud save and save-UI modes take a
			// completely different branch, are not reached from the field Triangle test,
			// and cancelling one would break saving outright. Loud rather than silent,
			// because a special mode opening on one machine alone IS a divergence, just
			// not one this file is allowed to fix by refusing it.
			if (mode != kMenuModeMain)
			{
				Log("menu: the engine asked for menu mode 0x%08X, which is a cloud save or "
				    "save-UI path rather than the ordinary menu. It is being left alone and "
				    "NOT replicated, so it is opening on this machine only.",
				    (unsigned)mode);
				g_orderedOpenPending = true; // so this is said once, not every step
				return;
			}

			// Somebody pressed Triangle on this machine. The engine decided that from
			// g_ffxPlayerPadPressed, which is the local hardware pad and is not
			// replicated, so letting it through would put the two machines in different
			// states on the next step. Cancel it and ask for an ordered one instead.
			CancelMenuOpenRequest();

			MenuOpenCommand payload;
			memset(&payload, 0, sizeof(payload));
			payload.mode = (int32_t)kMenuModeMain;

			if (!clock->RequestCommand((uint8_t)kCommandMenuOpen, &payload, (int)sizeof(payload)))
			{
				// Nothing was opened and nothing was sent, so the two machines still
				// agree. The player presses Triangle again.
				Log("menu: could not ask for a menu open, so the press was dropped. Opening "
				    "locally would have put this machine in a menu the other one is not in.");
				return;
			}

			InterlockedIncrement(&g_opensRelayed);
			Log("menu: Triangle was pressed here, the local open was cancelled and an "
			    "ordered one asked for. Both machines will open on the same step.");
		}

		// ---------------------------------------------------------------------------
		// Part two: anything the host ordered for this step.
		// ---------------------------------------------------------------------------
		void ApplyOpen(const Command& command)
		{
			if (command.length < (int)sizeof(MenuOpenCommand))
			{
				Log("menu: an open command from peer %u is %d bytes and the payload is %d, "
				    "so the two builds do not agree on the protocol",
				    (unsigned)command.issuer, command.length, (int)sizeof(MenuOpenCommand));
				return;
			}

			MenuOpenCommand payload;
			memcpy(&payload, command.data, sizeof(payload));

			if (MenuSystemRunning())
			{
				// A second open while one is already up. Both machines see the same thing,
				// so this is not a divergence, it is two players pressing Triangle at once
				// and the second press losing. Noted rather than acted on.
				Log("menu: ignoring %s's open on step %u, a menu is already up and %s is "
				    "driving it",
				    PeerLabel((int)command.issuer), (unsigned)command.step,
				    PeerLabel(g_driver));
				return;
			}

			// The opener is recorded BEFORE the open is attempted, because rule 3 has to
			// be in place by the time the menu's first sampler call happens, which is
			// inside this same step.
			g_opener = command.issuer;
			g_haveOpener = true;
			ResetRepeatState();

			if (!RequestMenuOpen((int)payload.mode))
			{
				InterlockedIncrement(&g_opensRefused);
				Log("menu: the kit refused to replay %s's open of mode 0x%08X on step %u",
				    PeerLabel((int)command.issuer), (unsigned)payload.mode,
				    (unsigned)command.step);
				return;
			}

			// FFX_MenuSys_RequestOpen stores nothing at all when FFX_SaveUi_IsIdle is 0,
			// and it does not report that. So the result is checked rather than assumed:
			// if the save UI happens to be busy on one machine and idle on the other, the
			// menu opens on one of them and that is exactly the divergence this whole
			// mechanism exists to stop.
			if (!MenuOpenRequested())
			{
				InterlockedIncrement(&g_opensRefused);
				Log("menu: %s's open was ORDERED for step %u but the engine did not store "
				    "it, which means FFX_SaveUi_IsIdle was 0 here. If it was idle on the "
				    "other machine, that machine is now in a menu and this one is not.",
				    PeerLabel((int)command.issuer), (unsigned)command.step);
				return;
			}

			g_orderedOpenPending = true;
			InterlockedIncrement(&g_opensApplied);
			Log("menu: %s opened the menu on step %u and is driving it",
			    PeerLabel((int)command.issuer), (unsigned)command.step);
		}

		void ApplyOverride(const Command& command)
		{
			if (command.length < (int)sizeof(MenuOverrideCommand))
			{
				Log("menu: an override command from peer %u is %d bytes and the payload is "
				    "%d, so the two builds do not agree on the protocol",
				    (unsigned)command.issuer, command.length,
				    (int)sizeof(MenuOverrideCommand));
				return;
			}

			MenuOverrideCommand payload;
			memcpy(&payload, command.data, sizeof(payload));

			if (command.issuer != HostPeer)
			{
				// Only the host may take control, and the check is here as well as at the
				// request so a hostile or stale client cannot do it by sending the command
				// directly.
				Log("menu: ignoring a control override from %s, which is not the host",
				    PeerLabel((int)command.issuer));
				return;
			}

			const bool wanted = payload.held != 0;
			if (wanted == g_override)
				return;

			g_override = wanted;
			ResetRepeatState();

			if (g_override)
				Log("menu: THE HOST HAS TAKEN CONTROL of the menu as of step %u. Every other "
				    "player's menu cursor has stopped responding on purpose, and will come "
				    "back when the host presses ctrl+F4 again.",
				    (unsigned)command.step);
			else
				Log("menu: the host gave control back as of step %u", (unsigned)command.step);
		}

		void ApplyCommandsForStep(Lockstep* clock)
		{
			// Four is already generous. An open and an override on one step is the most
			// that has any meaning, and the same bound as the other command consumers.
			const Command* commands[4];
			const int count = clock->CommandsForStep(commands, 4);

			for (int i = 0; i < count; ++i)
			{
				if (commands[i]->kind == (uint8_t)kCommandMenuOpen)
					ApplyOpen(*commands[i]);
				else if (commands[i]->kind == (uint8_t)kCommandMenuOverride)
					ApplyOverride(*commands[i]);
			}
		}

		// ---------------------------------------------------------------------------
		// The hook body. Runs inside FFX_MenuSys_SamplePad, after the original has
		// filled the block from the local pad, and before FFX_Module_StepAll walks every
		// screen. That is the only point in the frame where a written value is both
		// unclobbered and seen.
		// ---------------------------------------------------------------------------
		void __cdecl PadCallback(void)
		{
			if (!g_started || !g_armed)
			{
				// Leave the block exactly as the engine filled it. This is the path solo
				// play takes, so the shipped behaviour is untouched when nothing is
				// running.
				return;
			}

			// And check for a clock here as well as in the decider, because the decider
			// is only reached from the lockstep gate and the gate stops being called the
			// moment the clock stops. Without this, a session ending while a menu was up
			// would leave the last decided frame being written into the block on every
			// sample for the rest of the process, which looks exactly like a stuck button.
			if (!ActiveLockstep())
			{
				g_armed = false;
				return;
			}

			WriteMenuPad(&g_inject);
		}

	} // namespace

	bool InstallMenuSync()
	{
		if (g_installed)
			return true;

		g_inject = NeutralMenuPad();
		ResetRepeatState();

		g_installed = HookMenuSamplePad(&PadCallback);
		if (g_installed)
		{
			Log("menu: pad hook installed on FFX_MenuSys_SamplePad. The menu will run off "
			    "replicated input while a session is active.");
			strcpy_s(g_status, sizeof(g_status), "menu: hooked, idle");
		}
		else
		{
			Log("menu: could not hook FFX_MenuSys_SamplePad, so both players will be "
			    "driving one shared menu cursor from two controllers. The first sphere "
			    "spent or piece of gear equipped would be a divergence.");
			strcpy_s(g_status, sizeof(g_status), "menu: HOOK FAILED");
		}

		return g_installed;
	}

	bool MenuSyncInstalled()
	{
		return g_installed;
	}

	void StartMenuSync()
	{
		g_started = true;
		g_armed = false;
		g_decided = false;
		g_orderedOpenPending = false;
		g_override = false;
		g_haveOpener = false;
		g_opener = HostPeer;
		g_driver = -1;
		g_inject = NeutralMenuPad();
		ResetRepeatState();

		g_opensRelayed = 0;
		g_opensApplied = 0;
		g_opensRefused = 0;
		g_missingInput = 0;
		g_dialogueClash = 0;
		g_driverChanges = 0;

		if (!g_installed)
		{
			strcpy_s(g_status, sizeof(g_status), "menu: NOT syncing, the hook is not in");
			Log("menu: a session started but the pad hook is not installed, so the first "
			    "menu anybody opens in this session will diverge");
			return;
		}

		strcpy_s(g_status, sizeof(g_status), "menu: syncing, no menu open");
		Log("menu: syncing. Whoever opens a menu drives it, the sphere grid follows the "
		    "owner of the character being edited, and ctrl+F4 lets the host take over.");
	}

	void StopMenuSync()
	{
		g_started = false;
		g_armed = false;
		g_decided = false;
		g_driver = -1;
		g_override = false;

		// Never leave the local controller locked out of the grid because a session went
		// away while it was up.
		if (g_virtualPadOn)
		{
			SetVirtualPadEnabled(false);
			g_virtualPadOn = false;
		}

		strcpy_s(g_status, sizeof(g_status),
		    g_installed ? "menu: hooked, idle" : "menu: not hooked");
	}

	bool MenuSyncActive()
	{
		return g_started;
	}

	void PrepareMenuInput()
	{
		if (!g_started || !g_installed)
			return;

		Lockstep* clock = ActiveLockstep();
		if (!clock)
		{
			g_armed = false;
			return;
		}

		const uint32_t step = clock->CurrentStep();

		// Once per step. A refused step asks the gate again for the same step number,
		// and running the repeat counters a second time would double the repeat rate on
		// whichever machine happened to stall.
		if (g_decided && step == g_decidedStep)
			return;
		g_decidedStep = step;
		g_decided = true;

		// Order matters here and it is the only thing keeping the two halves from
		// fighting. Suppression runs FIRST, so an open ordered a few lines below is not
		// seen and cancelled by the pass that is meant to catch local ones.
		SuppressAndRelayLocalOpen(clock);
		ApplyCommandsForStep(clock);

		if (!MenuSystemRunning())
		{
			// No menu, so the block is not ours to touch. The engine's own sampler is
			// not even being called, but the arm flag is cleared anyway so that the
			// frame a menu comes up cannot inherit a stale decision.
			g_armed = false;
			g_driver = -1;
			if (g_haveOpener)
			{
				// The menu closed. Nothing was sent to make that happen: both machines
				// stepped the same modules from the same injected pad block, so both
				// reached a zero module active mask on this step.
				g_haveOpener = false;
				ResetRepeatState();
				Log("menu: the menu closed on step %u, driven by the replicated input "
				    "rather than by a command",
				    (unsigned)step);
			}
			ServiceGridStickNeutraliser();
			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "menu: syncing, no menu open, %ld opened so far%s", g_opensApplied,
			    g_override ? ", HOST HOLDS CONTROL" : "");
			return;
		}

		ServiceGridStickNeutraliser();

		const int driver = DecideDriver();
		if (driver != g_driver)
		{
			if (g_driver >= 0)
			{
				InterlockedIncrement(&g_driverChanges);
				Log("menu: the menu driver changed from %s to %s on step %u%s",
				    PeerLabel(g_driver), PeerLabel(driver), (unsigned)step,
				    SphereGridActive() ? " (sphere grid, following the character being "
				                         "edited)"
				                       : "");
			}
			g_driver = driver;

			// The repeat counters belong to a driver, not to the menu. Carrying them
			// across a handover would make the new driver's first held direction repeat
			// immediately.
			ResetRepeatState();
		}

		// A message window and the menu share this 192-byte block. They cannot normally
		// both be live in one step, because FFX_MesWin_StepAll is only reached from
		// FFX_MainStep's gameplay block and that block is skipped whenever
		// g_ffxMenuSysRunning is set. There are two narrow steps where both can run, the
		// step a menu closes on and the step one opens on, so this is counted rather
		// than assumed. If it ever climbs, the precedence between this file and
		// DialogueSync needs stating properly instead of being a consequence of call
		// order.
		if (DialogueOpen())
			InterlockedIncrement(&g_dialogueClash);

		if (g_driver < 0 || g_driver >= MaxPlayers)
		{
			g_inject = NeutralMenuPad();
			g_armed = true;
			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "menu: %s, NOBODY is driving it, which is a bug", MenuScreenName());
			return;
		}

		// Both machines read the SAME frame out of the ring, including the driver's own
		// machine. Using the live local pad for the driver would make it a step early
		// relative to the passenger, which is the whole reason the field layer does the
		// same thing.
		const InputFrame* frame = clock->InputForStep((uint8_t)g_driver);
		const bool have = clock->HasInputForStep((uint8_t)g_driver);

		if (!frame || !have)
		{
			// No input rather than stale input. A zeroed set of masks means "no buttons",
			// which is a state the shipped game produces itself, and a stale one means a
			// keypress repeating.
			//
			// With the gate enforcing this should never happen, because the gate refuses
			// the step instead. With the gate only measuring it will, and it is counted
			// so that "the menu feels like it drops inputs" has a number behind it.
			InterlockedIncrement(&g_missingInput);
			g_inject = NeutralMenuPad();
			g_armed = true;
			ResetRepeatState();
			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "menu: %s, waiting on %s's input, %ld steps so far", MenuScreenName(),
			    PeerLabel(g_driver), g_missingInput);
			return;
		}

		const WORD held = (WORD)(frame->buttons & kMenuButtons);

		// The edge, from two replicated step values rather than from the engine's
		// wall-clock timers. A first step with no history reports no press, which is
		// right: a button already held when a menu opened must not count as the press
		// that confirms the first row.
		const WORD previous = g_haveLastHeld ? g_lastHeld : held;
		const WORD pressed = (WORD)(held & ~previous);
		g_lastHeld = held;
		g_haveLastHeld = true;

		const WORD repeat = DeriveRepeat(held, pressed);

		g_inject = SpreadMasks(held, pressed, repeat);
		g_armed = true;

		_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
		    "menu: %s on %s, %s driving, held %04X rpt %04X%s", MenuScreenName(),
		    (g_driver == LocalPeerId()) ? "ours" : "theirs", PeerLabel(g_driver),
		    (unsigned)held, (unsigned)repeat,
		    g_override ? ", HOST OVERRIDE" : "");
	}

	void RequestMenuControlOverride()
	{
		Session* session = ActiveSession();
		Lockstep* clock = ActiveLockstep();

		if (!session || !clock)
		{
			Log("menu: ctrl+F4 does nothing without a running session. Alone, the menu is "
			    "already entirely yours.");
			return;
		}

		if (!session->IsHost())
		{
			Log("menu: ctrl+F4 is the HOST's override. Ask them to press it.");
			return;
		}

		MenuOverrideCommand payload;
		memset(&payload, 0, sizeof(payload));
		payload.held = (uint8_t)(g_override ? 0 : 1);

		if (!clock->RequestCommand((uint8_t)kCommandMenuOverride, &payload, (int)sizeof(payload)))
		{
			Log("menu: could not send the control override, so nothing changed. Flipping it "
			    "locally would mean the two machines disagreed about who is driving, which "
			    "is the one failure here that cannot be recovered from.");
			return;
		}

		Log("menu: asking to %s control. It takes effect on the step the host stamps it "
		    "for, on both machines at once.",
		    payload.held ? "TAKE" : "give back");
	}

	bool MenuControlOverrideHeld()
	{
		return g_override;
	}

	int MenuDriver()
	{
		return g_driver;
	}

	int MenuHashRegions(DWORD* out, int maxRegions)
	{
		if (!out || maxRegions <= 0)
			return 0;
		if (!MenuSystemRunning())
			return 0;

		// FNV-1a over the values, built by hand rather than over a memory range,
		// because the things worth comparing are scattered across three different
		// globals and a heap block.
		DWORD h = 2166136261u;
		int written = 0;

#define MENUSYNC_MIX(value)                          \
	do                                               \
	{                                                \
		const DWORD v__ = (DWORD)(value);            \
		for (int b__ = 0; b__ < 4; ++b__)            \
		{                                            \
			h ^= (DWORD)((v__ >> (b__ * 8)) & 0xFF); \
			h *= 16777619u;                          \
		}                                            \
	} while (0)

		// Region 0, THE SHELL AND THE DRIVER. If the two machines are on different
		// screens, or have decided on different drivers, it shows up here and nowhere
		// else. The driver is in the hash on purpose: two machines acting on different
		// owners is the failure with no way back, so it is worth catching on the step it
		// happens rather than by noticing the cursor moved in two places.
		MENUSYNC_MIX(ActiveModuleMask());
		MENUSYNC_MIX(SuspendedModuleMask());
		MENUSYNC_MIX((DWORD)ActiveMenuScreen());
		MENUSYNC_MIX((DWORD)MenuEnteredMode());
		MENUSYNC_MIX((DWORD)MenuRequestedMode());
		MENUSYNC_MIX((DWORD)g_driver);
		MENUSYNC_MIX((DWORD)g_opener);
		MENUSYNC_MIX((DWORD)(g_override ? 1 : 0));
		for (int id = 0; id < kMenuModuleCount; ++id)
		{
			int state = 0;
			MENUSYNC_MIX(ModuleState(id, &state) ? (DWORD)(state + 1) : 0u);
		}
		out[written++] = h;
		if (written >= maxRegions)
			return written;

		// Region 1, THE CURSOR, THE GRID AND THE INJECTED INPUT.
		//
		// Both character fields are in here, because MenuCursorChar and
		// SphereGridCharacter can legitimately differ for a frame during an L1/R1 switch
		// and that difference is exactly the kind of thing worth being able to see.
		//
		// The grid's own pad struct is in here too, and it is the one thing in this hash
		// that is expected to catch a KNOWN hole rather than an unknown one: the grid
		// reads the raw stick itself, so if a player nudges a stick on that screen these
		// three words come apart on the next step and the detector says so.
		h = 2166136261u;
		MENUSYNC_MIX((DWORD)MenuCursorChar());
		MENUSYNC_MIX((DWORD)MenuCharCursorIndex());
		MENUSYNC_MIX((DWORD)MenuCharListLength());
		MENUSYNC_MIX((DWORD)SphereGridCharacter());

		BYTE prevChar = kCharNone, direction = 0;
		if (SphereGridSwitchState(&prevChar, &direction))
		{
			MENUSYNC_MIX((DWORD)prevChar);
			MENUSYNC_MIX((DWORD)direction);
		}

		int node = -1, cost = -1;
		SphereGridNodeIndex(&node);
		SphereGridPendingCost(&cost);
		MENUSYNC_MIX((DWORD)node);
		MENUSYNC_MIX((DWORD)cost);

		WORD gridHeld = 0, gridPressed = 0, gridHeldOrRepeat = 0;
		if (SphereGridPadState(&gridHeld, &gridPressed, &gridHeldOrRepeat))
		{
			MENUSYNC_MIX((DWORD)gridHeld);
			MENUSYNC_MIX((DWORD)gridPressed);
			MENUSYNC_MIX((DWORD)gridHeldOrRepeat);
		}

		MENUSYNC_MIX((DWORD)g_inject.held);
		MENUSYNC_MIX((DWORD)g_inject.pressed);
		MENUSYNC_MIX((DWORD)g_inject.repeat);
		out[written++] = h;

#undef MENUSYNC_MIX

		return written;
	}

	bool MenuWantsPerStepChecksum()
	{
		return g_started && MenuSystemRunning();
	}

	const char* MenuSyncStatus()
	{
		return g_status;
	}

	void LogMenuSync()
	{
		Log("=== menu ===");
		Log("%s", g_status);

		if (!g_installed)
		{
			Log("the pad hook is not installed, so one shared menu cursor is being driven "
			    "from two controllers at once");
			return;
		}

		if (!g_started)
		{
			Log("not syncing, so the menu is entirely local. That is correct alone and is a "
			    "divergence in a session.");
			return;
		}

		Log("screen: %s, running %s, driver %s, opener %s%s", MenuScreenName(),
		    MenuSystemRunning() ? "yes" : "no",
		    (g_driver >= 0) ? PeerLabel(g_driver) : "nobody",
		    g_haveOpener ? PeerLabel((int)g_opener) : "nobody yet, defaulting to the host",
		    g_override ? "  HOST OVERRIDE IS ON" : "");

		if (MenuSystemRunning())
		{
			Log("cursor char %s, sphere grid char %s%s", CharacterName(MenuCursorChar()),
			    CharacterName(SphereGridCharacter()),
			    SphereGridActive() ? "  (the grid is up, so the grid char is the gate)" : "");

			MenuPadFrame live;
			if (ReadMenuPad(&live))
				Log("block: held %04X pressed %04X repeat %04X sticky %04X, analog %02X %02X",
				    (unsigned)live.held, (unsigned)live.pressed, (unsigned)live.repeat,
				    (unsigned)live.heldSticky, (unsigned)live.analogX, (unsigned)live.analogY);

			Log("injecting: held %04X pressed %04X repeat %04X, armed %s",
			    (unsigned)g_inject.held, (unsigned)g_inject.pressed,
			    (unsigned)g_inject.repeat, g_armed ? "yes" : "no");
		}

		Log("%ld opens relayed, %ld applied, %ld refused by the engine, %ld driver "
		    "handovers, %ld steps with no driver input, %ld steps with a box open too",
		    g_opensRelayed, g_opensApplied, g_opensRefused, g_driverChanges,
		    g_missingInput, g_dialogueClash);
		Log("repeat is DERIVED from the step counter, %d steps then every %d. The shipped "
		    "engine paces it off the wall clock and its interval is unstable to within a "
		    "frame, so there is nothing faithful to transmit. See MENU_SYNC.md.",
		    kFirstRepeatSteps, kRepeatIntervalSteps);
		Log("virtual pad %s, which is how the sphere grid's raw stick read is neutralised",
		    g_virtualPadOn ? "ON (we turned it on)" : "off");

		LogOwnership();
	}

} // namespace pilgrimage
