#include "net/LockstepLink.h"

#include <stdio.h>
#include <string.h>

#include "ModState.h"
#include "ffx/Cutscene.h"
#include "ffx/GameState.h"
#include "ffx/Input.h"
#include "ffx/MainLoop.h"
#include "net/NetLink.h"
#include "workshop/Lockstep.h"
#include "workshop/Log.h"
#include "world/RemotePlayers.h"
#include "world/WorldSync.h"
#include "battle/BattleSync.h"
#include "menu/CoopConfig.h"
#include "menu/MenuSync.h"
#include "menu/PauseSync.h"
#include "world/BoosterSync.h"
#include "world/DialogueSync.h"
#include "world/TriggerPass.h"

#include <math.h>

#include "ffx/Camera.h"
#include "workshop/Session.h"

namespace pilgrimage
{

	using namespace ffx;
	using namespace workshop;

	namespace
	{

		Lockstep clock;

		bool installed = false;
		bool enforced = false;

		// How many refused steps in a row before escalating to the real simulation hold.
		// Two is roughly 67 ms of jitter absorbed with the cheap mechanism, which covers
		// the ordinary case without ever putting the render path somewhere the shipped
		// game would not.
		const uint32_t EscalateAfterStalls = 2;

		bool holdApplied = false;

		// A bitmask of HoldReason. Anything that wants the game frozen raises a bit here
		// rather than writing the hold byte itself, because that byte has to be re-asserted
		// every animate and this file is the one place that does it.
		int holdReasons = 0;

		// When the hold went on, in milliseconds, and nothing else reads the clock.
		//
		// This exists because of a trap the cutscene research turned up. During a voiced
		// cutscene the engine paces itself off the millisecond clock against an authored
		// timing track, taking extra simulation steps when it falls behind. If a lockstep
		// gate holds the game for 200 ms and says nothing, the track comes back 200 ms
		// behind and burns catch-up steps to get level, which on a lockstepped pair is a
		// burst of steps one machine takes and the other does not.
		//
		// The engine already ships the fix, because its own pause menu has exactly this
		// problem: a pending-pause value the step pacer subtracts from the track's
		// elapsed time. So tell it how long we held, and the track behaves as though the
		// hold never happened.
		DWORD holdStartedMs = 0;

		char summary[220] = "lockstep: not installed";

		// ---------------------------------------------------------------------------
		// The gate, called from inside FFX_MainStep before anything else in the step.
		// ---------------------------------------------------------------------------

		bool __cdecl GateCallback()
		{
			if (!clock.Running())
				return true;

			// Sample what the local player is pressing and stamp it for a step a little
			// way ahead. Routing the sticks through the wire quantisation here, not just
			// on the way out, is the point: both machines then simulate from the same
			// numbers rather than one of them using the full float.
			InputFrame frame = NeutralInput();
			frame.buttons = ButtonMask();

			Sticks sticks;
			if (ReadSticks(&sticks))
			{
				// Resolve the left stick against OUR camera here, so what goes on the wire
				// is a world heading. See the InputFrame comment in Protocol.h for why this
				// is not optional: a camera relative stick means two machines compute two
				// different headings from one input.
				//
				// The engine stores left stick Y positive-DOWN, so forward is -leftY. That
				// negation is the whole difference between walking where you pushed and
				// walking backwards.
				const float right = sticks.leftX;
				const float forward = -sticks.leftY;

				float magnitude = sqrtf(right * right + forward * forward);

				// Deadzone HERE, before anything is encoded, so a resting stick sends an
				// exact zero. Doing it on the receiving side instead would mean the noise
				// had already been quantised, replicated and stored in the ring, and a
				// remote character creeping from stick hum is one of the more annoying
				// things to chase down later.
				if (magnitude < kStickDeadzone)
					magnitude = 0.0f;
				else if (magnitude > 1.0f)
					magnitude = 1.0f;

				if (magnitude > 0.0f)
				{
					// Exactly the engine's own transform, from FFX_Player__stepControl:
					//     desired   = atan2(right, forward)
					//     m_moveDir = cameraYaw - desired
					// The heading convention is 0 along +X and pi/2 along +Z, which is what
					// FFX_Ch_UpdateMotionAll integrates against.
					const float desired = atan2f(right, forward);

					// With no camera to resolve against, send the stick's own angle. That
					// is what the previous vector encoding degraded to in this case, so the
					// behaviour is unchanged: wrong, but wrong in a debuggable way rather
					// than standing still.
					float cameraYaw = 0.0f;
					const float heading = ActiveCameraYaw(&cameraYaw)
					                          ? (cameraYaw - desired)
					                          : atan2f(forward, right);

					frame.moveAngle = QuantiseAngle(heading);
					frame.moveMag = QuantiseMagnitude(magnitude);
				}

				// The right stick is deliberately not sent. It drives the camera, the
				// camera is local, and nothing simulated reads it. Those are the two bytes
				// the heading precision is paid for with.
			}

			clock.SubmitLocalInput(frame);

			// Decide what a message box will be fed on this step. Here rather than after
			// the step, because the sampler that applies it runs INSIDE FFX_MainStep, so
			// anything decided afterwards would arrive a step late.
			PrepareDialogueInput();

			// And the same for the in-game menu, for the same reason:
			// FFX_MenuSys_SamplePad is reached from FFX_MainStep too. This one also
			// applies the ordered menu commands, because an open replayed here is
			// consumed by FFX_MenuSys_PollOpenAndStep inside this very step, which is
			// what puts both machines in the menu on the same step number.
			PrepareMenuInput();

			// And the co-op Config rows, which carry the ownership bindings. Same
			// reason again: an ordered binding has to be consumed on the exact step it
			// was stamped for, and a frame-path consumer drops one on every catch-up
			// step. For ownership that is the unrecoverable split, not a dropped frame.
			ServiceCoopConfigStep();

			// A peer's pause, raised on the exact step the host named. The RELEASE is
			// not here, it is in StepPauseSync on the frame path, because by the time
			// anybody is paused there are no steps left to consume a command on. See
			// menu/PauseSync.h.
			ApplyPauseCommands();

			const StepGateResult gate = clock.StepGate();
			if (gate != GateWaiting)
				return true;

			// Not enforcing means measure, do not interfere. The counters still move, so
			// the log and the panel show exactly how often a real gate would have held
			// the game, which is the number worth knowing before switching it on.
			return !enforced;
		}

		void __cdecl AfterStepCallback(float)
		{
			// The extra per-player trigger passes belong here rather than in the frame hook,
			// because the engine runs its own trigger set once per SIMULATION step and one
			// presented frame can be several steps. Driving it per frame would make a remote
			// player's triggers depend on the frame rate, which is exactly the class of bug
			// lockstep exists to prevent.
			//
			// The zero is the delta time the three steppers take and provably never read,
			// checked at the disassembly level rather than trusted from the decompiler. Passing
			// a fabricated millisecond count would imply a meaning it does not have.
			//
			// It runs whether or not the clock is running, so the pass can be tested solo
			// against a spawned clone with no second machine involved.
			// Settings before bodies. A booster command that lands on this step changes
			// the rules this step runs under, most importantly the encounter rate, so it
			// has to be applied before anything reads those rules.
			StepBoosterSync();

			// Battle commands next, and before the bodies, for the same reason: a
			// committed command changes what the battle step after this one does, and
			// both machines have to commit it at the same point in the same step or the
			// command queues are in a different order.
			StepBattleSync();

			// Order matters. Drive the remote bodies first so the trigger pass sees where
			// they are this step, not where they were last step.
			StepRemotePlayers();
			RunRemoteTriggerPass(0);

			if (!clock.Running())
				return;
			clock.AdvanceStep();
		}

	} // namespace

	bool InstallLockstep()
	{
		if (installed)
			return true;

		installed = HookMainStep(&GateCallback, &AfterStepCallback);
		if (installed)
			Log("lockstep: step hook installed on FFX_MainStep, gate is %s",
			    enforced ? "ENFORCING" : "measuring only");
		else
			Log("lockstep: could not install the step hook, so the clock cannot run");

		return installed;
	}

	bool LockstepInstalled()
	{
		return installed;
	}

	Lockstep* ActiveLockstep()
	{
		return clock.Running() ? &clock : NULL;
	}

	void BeginLockstep()
	{
		Session* session = ActiveSession();
		if (!session)
		{
			Log("lockstep: no session to attach to");
			return;
		}
		if (!installed)
		{
			Log("lockstep: the step hook is not installed, so the clock would count "
			    "nothing. Not starting.");
			return;
		}

		// WHERE THE FIRST STEP NUMBER COMES FROM, which is not a cosmetic choice.
		//
		// The step number is a SHARED LABEL, not a local counter. Ordered commands are
		// stamped with an absolute step by whoever issues them and matched against the
		// other machine's clock, and peers exchange input keyed by step, so the two
		// clocks have to agree on what to call a given step or none of it lines up.
		//
		// The host is the authority and seeds from the game's own g_ffxMainStepCounter,
		// so its log lines from either layer mean the same thing. A CLIENT MUST NOT do
		// that: its counter is a different process's and would be offset by an arbitrary
		// amount forever. It adopts the host's number instead, which it already has,
		// because the host froze itself the instant it took the snapshot and is still
		// sitting on that same step when the client gets here.
		//
		// The debugging cost is worth stating: on a client these step numbers will NOT
		// match that machine's own g_ffxMainStepCounter. They match the host's.
		uint32_t step = 0;
		if (session->IsHost())
		{
			DWORD local = 0;
			if (!MainStepCounter(&local))
				local = StepCount();
			step = (uint32_t)local;
		}
		else if (!AppliedHostStep(&step))
		{
			// Not startable yet rather than startable wrong. NetLink only calls this
			// once WorldSyncReady is true, so reaching here means an installed snapshot
			// carried no host step, and starting anyway would desync silently.
			Log("lockstep: this client has no host step to start from, so the clock is "
			    "NOT starting. A clock seeded from our own counter would put every "
			    "ordered command on a step the host never runs.");
			return;
		}

		clock.Start(session, step);
	}

	void EndLockstep()
	{
		if (!clock.Running())
			return;
		clock.Stop();

		// Never leave the game held because the link went away mid-stall.
		if (holdApplied)
		{
			ReleaseSimulationHold(Application());
			holdApplied = false;
		}
	}

	bool LockstepRunning()
	{
		return clock.Running();
	}

	void SetLockstepEnforced(bool wanted)
	{
		if (enforced == wanted)
			return;
		enforced = wanted;
		Log("lockstep: the gate is now %s",
		    enforced ? "ENFORCING, a step can be refused"
		             : "measuring only, no step will be refused");
	}

	bool LockstepEnforced()
	{
		return enforced;
	}

	void HoldSimulationFor(int reason)
	{
		const int before = holdReasons;
		holdReasons |= reason;
		if (holdReasons != before)
			Log("lockstep: a hold was requested, reasons now 0x%X. The owner applies it on "
			    "the next animate.",
			    holdReasons);
	}

	void ReleaseSimulationFor(int reason)
	{
		const int before = holdReasons;
		holdReasons &= ~reason;
		if (holdReasons != before)
			Log("lockstep: a hold reason was dropped, reasons now 0x%X", holdReasons);
	}

	int SimulationHoldReasons()
	{
		return holdReasons;
	}

	void ServiceLockstep(void* application)
	{
		// A hold reason can be raised before the clock starts, which is exactly the world
		// transfer case: the host is sending while a joining client has not begun stepping.
		// So the reasons are honoured whether the clock runs or not, and only the stall
		// escalation needs a running clock.
		if (!clock.Running())
		{
			const bool wantIdleHold = holdReasons != 0;
			if (wantIdleHold != holdApplied)
			{
				const bool ok = wantIdleHold ? HoldSimulation(application)
				                             : ReleaseSimulationHold(application);
				if (ok)
					holdApplied = wantIdleHold;
			}
			else if (holdApplied)
			{
				// Re-assert it. The engine's own code writes this byte too, so a hold that
				// is set once and not refreshed does not stay set.
				HoldSimulation(application);
			}

			return;
		}

		// Escalate a long wait from a refused step to the shipped pause mechanism.
		// Refusing a step is fine for a frame or two, but it leaves the render path
		// presenting a scene whose display list was not rebuilt, so it is not where
		// to sit for half a second.
		const bool wantHold =
		    (enforced && clock.CurrentStall() >= EscalateAfterStalls) || holdReasons != 0;
		if (wantHold != holdApplied)
		{
			const bool ok = wantHold ? HoldSimulation(application)
			                         : ReleaseSimulationHold(application);
			if (ok)
			{
				holdApplied = wantHold;

				if (wantHold)
				{
					holdStartedMs = GetTickCount();
					Log("lockstep: holding the simulation after %lu refused steps%s",
					    (unsigned long)clock.CurrentStall(),
					    TimingTrackDriving()
					        ? ", and a voiced timing track is running so the hold will "
					          "be credited back to it"
					        : "");
				}
				else
				{
					// Credit the stolen time back before anything else, so the track
					// never sees a frame in which it was behind.
					const DWORD heldMs = GetTickCount() - holdStartedMs;
					if (TimingTrackDriving() && heldMs > 0)
						AddTimingTrackPauseMs(heldMs);
					Log("lockstep: released the hold after %lu ms", (unsigned long)heldMs);
				}
			}
		}

		// While a menu is up, check every step instead of every thirtieth. A menu
		// desync reproduces, it happens while nothing else in the game is moving, and
		// the two machines are generating no other traffic, so there is no reason to
		// find out up to a second late. If the two machines disagree about whether a
		// menu is open then they are checking at different rates, and the comparison
		// only happens on steps they both hashed, which is still every thirtieth. That
		// is the case where the disagreement IS the desync, so it gets caught either
		// way, just a little later.
		clock.SetChecksumInterval(MenuWantsPerStepChecksum() ? 1 : DefaultChecksumInterval);

		// Feed the desync detector. Lockstep only sends one on its own interval, so
		// offering it every frame costs a hash and nothing else. Playtime ticks on
		// its own every frame, so it is excluded from the combined value by
		// HashGameState rather than here.
		GameStateHash hash;
		if (HashGameState(&hash) && hash.valid)
		{
			// The kit's game-state layer speaks DWORD and the wire format speaks
			// uint32_t. They are the same width here, but copying rather than casting
			// means the day one of them changes is a compile error instead of a
			// checksum that quietly means nothing.
			uint32_t buckets[kBucketCount + kMenuHashRegions];
			for (int i = 0; i < (int)kBucketCount; ++i)
				buckets[i] = (uint32_t)hash.bucket[i];

			// The menu's own replicable state goes in the spare slots past the 13 save
			// block buckets. ChecksumPayload carries 16, so this needs no protocol
			// change, and HandleChecksum reports "first difference in region N", which
			// for N >= 13 means the menu rather than the save block.
			//
			// It is mixed into the combined value as well, not just carried alongside,
			// because the combined value is the only thing HandleChecksum compares. A
			// region that is sent but not folded in would never trigger a report.
			DWORD menuRegions[kMenuHashRegions];
			const int menuCount = MenuHashRegions(menuRegions, kMenuHashRegions);
			uint32_t combined = (uint32_t)hash.combined;
			for (int i = 0; i < menuCount; ++i)
			{
				buckets[(int)kBucketCount + i] = (uint32_t)menuRegions[i];
				combined ^= (uint32_t)menuRegions[i];
			}

			clock.SubmitChecksum(combined, buckets, (int)kBucketCount + menuCount);
		}
	}

	const char* LockstepSummary()
	{
		if (!installed)
		{
			strcpy_s(summary, sizeof(summary), "lockstep: no step hook");
			return summary;
		}
		if (!clock.Running())
		{
			_snprintf_s(summary, sizeof(summary), _TRUNCATE,
			    "lockstep: idle, gate %s, game step %lu",
			    enforced ? "enforcing" : "measuring", (unsigned long)StepCount());
			return summary;
		}

		char line[200];
		_snprintf_s(summary, sizeof(summary), _TRUNCATE, "%s%s",
		    clock.Summary(line, sizeof(line)),
		    enforced ? "" : "  [measuring only]");
		return summary;
	}

} // namespace pilgrimage
