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
				// is a world direction. See the InputFrame comment in Protocol.h for why
				// this is not optional: a camera relative stick means two machines compute
				// two different headings from one input.
				//
				// The engine stores left stick Y positive-DOWN, so forward is -leftY. That
				// negation is the whole difference between walking where you pushed and
				// walking backwards.
				const float right = sticks.leftX;
				const float forward = -sticks.leftY;
				const float magnitude = sqrtf(right * right + forward * forward);

				// Pass the raw pair through when there is no camera to resolve against, so
				// a missing camera degrades to the old behaviour rather than to standing
				// still. It is wrong either way, but one of the two is debuggable.
				float worldX = right;
				float worldZ = forward;

				float cameraYaw = 0.0f;
				if (magnitude > 0.0f && ActiveCameraYaw(&cameraYaw))
				{
					// Exactly the engine's own transform, from FFX_Player__stepControl:
					//     desired   = atan2(right, forward)
					//     m_moveDir = cameraYaw - desired
					// re-emitted as a direction so the receiver needs no camera. The
					// heading convention is 0 along +X and pi/2 along +Z, which is what
					// FFX_Ch_UpdateMotionAll integrates against.
					const float desired = atan2f(right, forward);
					const float heading = cameraYaw - desired;
					worldX = cosf(heading) * magnitude;
					worldZ = sinf(heading) * magnitude;
				}

				frame.leftX = QuantiseStick(worldX);
				frame.leftY = QuantiseStick(worldZ);
				frame.rightX = QuantiseStick(sticks.rightX);
				frame.rightY = QuantiseStick(sticks.rightY);
			}

			clock.SubmitLocalInput(frame);

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

		// Start from the game's own step counter rather than from zero, so the two
		// numbers never have to be translated and a log line from either layer means
		// the same thing.
		DWORD step = 0;
		if (!MainStepCounter(&step))
			step = StepCount();

		clock.Start(session, (uint32_t)step);
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

	void ServiceLockstep(void* application)
	{
		if (!clock.Running())
		{
			if (holdApplied)
			{
				ReleaseSimulationHold(application);
				holdApplied = false;
			}
			return;
		}

		// Escalate a long wait from a refused step to the shipped pause mechanism.
		// Refusing a step is fine for a frame or two, but it leaves the render path
		// presenting a scene whose display list was not rebuilt, so it is not where
		// to sit for half a second.
		const bool wantHold = enforced && clock.CurrentStall() >= EscalateAfterStalls;
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
			uint32_t buckets[kBucketCount];
			for (int i = 0; i < (int)kBucketCount; ++i)
				buckets[i] = (uint32_t)hash.bucket[i];
			clock.SubmitChecksum((uint32_t)hash.combined, buckets, (int)kBucketCount);
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
