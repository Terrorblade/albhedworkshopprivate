#include "world/FmvSync.h"

#include <stdio.h>
#include <string.h>

#include "coop/Ownership.h"
#include "ffx/Cutscene.h"
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

		bool g_installed = false;
		bool g_started = false;

		// Idle    nothing is being withheld. The normal state, including for the whole
		//         length of a movie, because withholding only starts once the local
		//         wait has finished.
		// Waiting the local wait is finished and the script is parked because at least
		//         one other machine has not said it is finished too. This is the only
		//         state in which this file changes anything about the game.
		// Open    the barrier opened on a step every machine agreed on. Normally this
		//         lasts no time at all, because the same gate call that opened it lets
		//         the wait through and drops straight back to Idle. It only lingers on
		//         the give-up path, where the forced skip needs a step or two before
		//         the engine's own wait agrees it has finished.
		enum Phase
		{
			PhaseIdle,
			PhaseWaiting,
			PhaseOpen
		};
		Phase g_phase = PhaseIdle;

		// Which Movie library function is parked, for the readout and the log. Not a
		// decision input.
		int g_waitSlot = -1;

		// A bit per peer that has reported its movie wait finished, filled ONLY from
		// ordered commands, including this machine's own coming back. That is what
		// makes the step the barrier opens on identical everywhere.
		//
		// There is deliberately no barrier identity matched against here. A report is
		// stamped a few steps ahead and consumed on exactly that step, the barrier
		// opens on the step the LAST report lands, so by the time it opens no report
		// for it can still be in flight. A sequence number would have to stay in
		// agreement between machines, and the one case where it could not is a peer
		// that never parked at all, which is exactly when the extra state would be
		// wrong. See the header.
		int g_reportedMask = 0;

		// The barrier count, local, sent in the payload for the log only.
		uint8_t g_barrierTag = 0;

		uint32_t g_localDoneStep = 0; // the step the local wait finished, the timeout base
		uint32_t g_openStep = 0;      // the step the barrier opened on, from the command

		// This barrier was armed with no movie behind it, so the readout must not tell
		// the player their video finished when they never had one.
		bool g_hadNoMovie = false;

		bool g_cancelHandled = false;    // a cancel has been announced or applied, once per barrier
		bool g_giveUpAsked = false;      // the timeout has already asked, once per barrier
		bool g_skipWasRequested = false; // the engine's skip flag last time we looked

		// Per-step guard. The gate runs inside the FFX_MainStep sub-step loop, so it
		// can fire more than once in a step when the sub-step count is above one, and
		// draining the command queue twice for one step would double-apply a cancel.
		uint32_t g_servicedStep = 0;
		bool g_haveServicedStep = false;

		LONG g_barriers = 0;
		LONG g_cancelsSent = 0;
		LONG g_cancelsApplied = 0;
		LONG g_giveUps = 0;
		LONG g_sendFailures = 0;
		LONG g_startAsymmetries = 0;
		uint32_t g_longestWaitSteps = 0;

		char g_status[240] = "fmv: not syncing";

		// A player's name and peer id, for the log and the readout. Copied in shape
		// from PauseSync for the same reason it exists there: a player whose game has
		// stopped responding has to be told WHO it is waiting for, by name, or it reads
		// as a network fault.
		//
		// Three rotating buffers rather than one static, so two PeerLabel calls in one
		// Log line do not both point at the same bytes and print the same name.
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

		// Every peer we expect a report from, including ourselves, because our own
		// report comes back through the command channel like anybody else's.
		int NeededMask()
		{
			int needed = 0;
			for (int peer = 0; peer < MaxPlayers; ++peer)
			{
				if (PeerInSession(peer))
					needed |= 1 << peer;
			}
			return needed;
		}

		int MissingMask()
		{
			return NeededMask() & ~g_reportedMask;
		}

		void GoIdle()
		{
			g_phase = PhaseIdle;
			g_reportedMask = 0;
			g_cancelHandled = false;
			g_giveUpAsked = false;
			g_hadNoMovie = false;
			g_waitSlot = -1;

			// Seeded from the live flag rather than set to false, because a false means
			// the next look would see a rising edge that never happened and announce a
			// cancel nobody asked for. FFX_Fmv_CreatePlayer clears the flag per movie so
			// it should already be 0, and reading it is cheaper than relying on that.
			g_skipWasRequested = FmvSkipRequested();
		}

		bool SendCommand(Lockstep* clock, uint8_t reason, int videoId)
		{
			FmvCommand payload;
			memset(&payload, 0, sizeof(payload));
			payload.reason = reason;
			payload.barrier = g_barrierTag;
			payload.videoId = (int32_t)videoId;

			if (clock->RequestCommand((uint8_t)kCommandFmv, &payload, (int)sizeof(payload)))
				return true;

			InterlockedIncrement(&g_sendFailures);
			return false;
		}

		int LocalVideoId()
		{
			FmvState f;
			if (!ReadFmvState(&f))
				return -1;
			return f.videoId;
		}

		// Is there a movie behind this wait at all. NOT A DECISION, only something to
		// log, and the difference matters, see the arming comment in the gate.
		//
		// A false here is the signature of FFX_Fmv_StartVideo having failed on this
		// machine. Movie:10 start sets g_ffxFmvScriptRunning and
		// g_ffxFmvScriptBlocking only when the video actually opened, and its poll
		// completes on the first look when blocking is 0, so a machine with a missing
		// or unreadable video file arrives here with every one of these clear.
		bool MovieWasInvolved()
		{
			FmvState f;
			if (!ReadFmvState(&f))
				return false;

			return f.hasPlayer || f.playing || f.decoderBusy || f.scriptRunning ||
			       f.scriptBlocking || f.skipRequested;
		}

		void OpenBarrier(uint32_t step, const char* why)
		{
			g_phase = PhaseOpen;
			g_openStep = step;
			++g_barrierTag;
			InterlockedIncrement(&g_barriers);

			const uint32_t waited = step - g_localDoneStep;
			if (waited > g_longestWaitSteps && waited < 0x80000000u)
				g_longestWaitSteps = waited;

			Log("fmv: barrier open on step %u, %s. The script was parked %u steps, about "
			    "%u ms.",
			    (unsigned)step, why, (unsigned)waited, (unsigned)(waited * 1000u / 30u));
		}

		// The local movie ending. Announced once, and the announcement is what fills
		// our own bit in the reported mask when it comes back.
		void ReportLocalDone(Lockstep* clock, uint32_t step, int movieFunction)
		{
			g_phase = PhaseWaiting;
			g_localDoneStep = step;
			g_waitSlot = movieFunction;

			g_hadNoMovie = !MovieWasInvolved();
			if (g_hadNoMovie)
			{
				// Not fatal any more, which it would have been under the first design.
				// This machine parks at the wait for as long as the other machine's video
				// takes, so the two scripts stay on the same opcode and the same step.
				// All the player here loses is the video itself.
				InterlockedIncrement(&g_startAsymmetries);
				Log("fmv: parked at the Movie:%d wait with no movie behind it, which means "
				    "FFX_Fmv_StartVideo failed on this machine and you are not going to "
				    "see this video. Waiting for the other players to finish theirs so "
				    "the scripts stay in step. Check the video files in FFX_Data.",
				    movieFunction);
			}

			const int videoId = LocalVideoId();
			if (!SendCommand(clock, (uint8_t)kFmvReasonDone, videoId))
			{
				// Not retried, because a retry loop would fire a command per step for as
				// long as the link is down. The consequence is bounded and recoverable:
				// nobody sees our report, every machine including this one reaches the
				// step timeout, and the give-up path makes everybody skip and resume
				// together. A hitch rather than a split.
				Log("fmv: could not report that our video (id %d) finished. Every machine "
				    "will now wait out the %u step timeout and then force a skip.",
				    videoId, (unsigned)kFmvBarrierTimeoutSteps);
			}
		}

		void AskGiveUp(Lockstep* clock, uint32_t step)
		{
			// The counter is bumped where the command is APPLIED and not here, because
			// our own give-up comes back through the command channel like anybody
			// else's, so every machine ends up with the same number.
			g_giveUpAsked = true;

			const int missing = MissingMask();
			for (int peer = 0; peer < MaxPlayers; ++peer)
			{
				if ((missing & (1 << peer)) == 0)
					continue;
				Log("fmv: GIVING UP WAITING. %s has not reported their video finishing in "
				    "%u simulation steps, which is about %u seconds since ours ended. "
				    "Asking every machine to skip the video and carry on.",
				    PeerLabel(peer), (unsigned)kFmvBarrierTimeoutSteps,
				    (unsigned)(kFmvBarrierTimeoutSteps / 30u));
			}

			if (!SendCommand(clock, (uint8_t)kFmvReasonGiveUp, LocalVideoId()))
			{
				// Nothing can arrive to release us, so release ourselves rather than
				// leave the script parked for good. This one genuinely can split the two
				// machines, because the other end will do the same thing on its own step
				// rather than on an agreed one, so it is logged as the failure it is.
				Log("fmv: the give-up could not be sent either, so this machine is "
				    "resuming on its own step. THE TWO GAMES MAY NOW BE OUT OF STEP. "
				    "Disconnect and rejoin to resync.");
				InterlockedIncrement(&g_giveUps);
				RequestFmvSkip();
				OpenBarrier(step, "gave up with no link to agree on it");
			}
		}

		// A local skip, observed rather than hooked. g_ffxFmvSkipRequested is cleared
		// per movie by FFX_Fmv_CreatePlayer and set only by the engine's own skip
		// handler, so its rising edge is a clean "somebody at this machine pressed
		// Start then Square" with nothing to detour.
		void WatchLocalSkip(Lockstep* clock)
		{
			const bool skip = FmvSkipRequested();
			const bool rising = skip && !g_skipWasRequested;
			g_skipWasRequested = skip;

			if (!rising || g_cancelHandled)
				return;

			// Set before the send, so a cancel command of our own coming back does not
			// look like a second cancel to apply.
			g_cancelHandled = true;
			InterlockedIncrement(&g_cancelsSent);

			if (SendCommand(clock, (uint8_t)kFmvReasonCancel, LocalVideoId()))
			{
				Log("fmv: you skipped the video, telling the other machines to skip it "
				    "too. The local skip has already happened, so they are a command "
				    "round trip behind, which is a few hundred milliseconds of video.");
			}
			else
			{
				Log("fmv: you skipped the video but the cancel could not be sent, so the "
				    "other players are going to watch the rest of it. Your script waits "
				    "for them either way, so nothing is out of step.");
			}
		}

		void ApplyCommand(const Command& command)
		{
			if (command.length < (int)sizeof(FmvCommand))
			{
				Log("fmv: a command from peer %u is %d bytes and the payload is %d, so the "
				    "two builds do not agree on the protocol",
				    (unsigned)command.issuer, (int)command.length, (int)sizeof(FmvCommand));
				return;
			}

			FmvCommand payload;
			memcpy(&payload, command.data, sizeof(payload));

			const int peer = (int)command.issuer;
			if (peer < 0 || peer >= MaxPlayers)
				return;

			switch (payload.reason)
			{
			case kFmvReasonDone:
			{
				const int bit = 1 << peer;
				if ((g_reportedMask & bit) != 0)
					return;
				g_reportedMask |= bit;

				// The decision is made here, on the command's own step, rather than from
				// the current step in the gate. They are the same number today, because
				// CommandsForStep only ever hands back commands stamped for the current
				// step. Using the command's is what makes "both machines open on the same
				// step" true by construction rather than by coincidence.
				if (g_phase == PhaseWaiting && MissingMask() == 0)
					OpenBarrier(command.step, "every machine reported their video finished");
				return;
			}

			case kFmvReasonCancel:
			{
				if (g_cancelHandled)
					return;
				g_cancelHandled = true;

				// Our own cancel coming back needs nothing done: the engine already
				// skipped locally when the button was pressed, which is what produced
				// this command in the first place.
				if (peer == LocalPeerIndex())
					return;

				InterlockedIncrement(&g_cancelsApplied);
				if (RequestFmvSkip())
					Log("fmv: %s skipped the video on step %u, so this machine is skipping "
					    "it too",
					    PeerLabel(peer), (unsigned)command.step);
				else
					Log("fmv: %s skipped the video but the skip flag could not be written "
					    "here, so this machine will play it to the end and they will wait",
					    PeerLabel(peer));
				return;
			}

			case kFmvReasonGiveUp:
			{
				// This one can land while our own movie is still playing, so there may be
				// no park to measure. Without this the log prints a wrapped step count.
				if (g_phase != PhaseWaiting)
					g_localDoneStep = command.step;

				// Force the skip whatever state this machine is in. On the machine that
				// asked, this is already done. On a machine whose decode is stuck, THIS is
				// what unparks it: the skip makes its own wait finish, and the open window
				// below is what lets that finish get through.
				RequestFmvSkip();
				InterlockedIncrement(&g_giveUps);

				Log("fmv: %s gave up waiting on step %u, so every machine skips the video "
				    "and resumes. Something was wrong with a video on one of the machines.",
				    PeerLabel(peer), (unsigned)command.step);

				OpenBarrier(command.step, "a peer gave up waiting");
				return;
			}

			default:
				Log("fmv: a command from peer %u has reason %u, which this build does not "
				    "know. Ignoring it.",
				    (unsigned)command.issuer, (unsigned)payload.reason);
				return;
			}
		}

		void ServiceStep(Lockstep* clock, uint32_t step)
		{
			if (g_haveServicedStep && g_servicedStep == step)
				return;
			g_servicedStep = step;
			g_haveServicedStep = true;

			WatchLocalSkip(clock);

			const Command* commands[8];
			const int count = clock->CommandsForStep(commands, 8);
			for (int i = 0; i < count; ++i)
			{
				if (commands[i]->kind != (uint8_t)kCommandFmv)
					continue;
				ApplyCommand(*commands[i]);
			}
		}

		// ---------------------------------------------------------------------------
		// THE GATE. Called by the kit's shim on every poll of a Movie library wait, so
		// once per simulation step for the whole length of a movie and then once per
		// step for as long as the barrier withholds.
		//
		// localComplete false means the movie is still playing and the return value is
		// ignored. The call still matters: it is the per-step tick a cancel has to be
		// agreed in, and there is no other hook that fires once per step in that
		// window.
		// ---------------------------------------------------------------------------
		bool __cdecl WaitGate(int movieFunction, bool localComplete)
		{
			if (!g_started)
				return true;

			Lockstep* clock = ActiveLockstep();
			if (!clock)
			{
				// No clock means no step numbers to agree on and nothing that could ever
				// release a hold. Withholding here would park the script with no way out.
				if (g_phase != PhaseIdle)
				{
					Log("fmv: the lockstep clock went away while the barrier was up, so the "
					    "script is being let go. The two games are almost certainly out of "
					    "step now.");
					GoIdle();
				}
				return true;
			}

			const uint32_t step = clock->CurrentStep();
			ServiceStep(clock, step);

			if (g_phase == PhaseOpen)
			{
				if (localComplete)
				{
					// The wait the barrier opened for has finished, which is the whole
					// job, so the phase is spent here rather than left to time out. In the
					// normal case this is the SAME call the barrier opened in, because the
					// drain above is what opened it, so the step window below never comes
					// into it at all.
					GoIdle();
					return true;
				}

				// Still draining. Only reachable on the give-up path, where the forced
				// skip needs a step or two to make the engine's own wait finish.
				//
				// Unsigned on purpose: a clock that restarted lower wraps to a huge
				// difference and drops straight back to Idle, which is the safe answer.
				if (step - g_openStep <= kFmvBarrierOpenSteps)
					return false; // ignored, and the point is to stay in this phase

				GoIdle();
			}

			if (g_phase == PhaseIdle)
			{
				if (!localComplete)
					return false; // the movie is still playing, and this answer is ignored

				// No peer id yet means the host has not finished the handshake, so a
				// command from us has nowhere to be ordered against. Nobody else in the
				// session means nothing to agree with, and arming would cost a command
				// round trip of parked script on every movie for no reason. That second
				// one is the normal shape right after a host starts listening and before
				// anybody has joined.
				const int local = LocalPeerIndex();
				if (local < 0 || NeededMask() == (1 << local))
					return true;

				// ARMED FOR EVERY ONE OF THE FOUR WAITS, with no test for whether a movie
				// was really involved. The first design asked that question and it was
				// wrong, which is worth writing down because the reasoning looks
				// backwards at first.
				//
				// The answer is read out of LOCAL state, so the one case where the two
				// machines disagree about it is exactly the case where a video failed to
				// open on one of them. Machine A has a movie and arms, machine B has none
				// and does not, so A parks waiting for a report B is never going to send,
				// and A sits there for the whole 600 step timeout before forcing a skip.
				// A twenty second stall and a script offset, from trying to be clever.
				//
				// Arming regardless costs one command round trip, about 200 ms of parked
				// script, on a wait that is already seconds or minutes long. In exchange
				// the failed-video case stops being unrecoverable: B parks for as long as
				// A's video runs and the two scripts come out of the wait together. That
				// trade is not close.
				ReportLocalDone(clock, step, movieFunction);
			}

			if (g_phase != PhaseWaiting)
				return true;

			if (!localComplete)
				return false;

			// A peer that left while we were waiting for it stops being required, which
			// MissingMask picks up on its own because PeerInSession goes false.
			if (MissingMask() == 0)
			{
				// Only reachable when a peer we were waiting for left the session, since
				// the all-reported case already opened inside the drain on the command's
				// own step. This one opens on a LOCAL step, because a peer dropping is
				// not an ordered event, so with three players the two survivors can open
				// a step or two apart. Said out loud rather than hidden, and it only
				// matters above two players because below that there is no survivor left
				// to disagree with.
				OpenBarrier(step, "the peer we were waiting for left the session");
				return true;
			}

			if (!g_giveUpAsked && (step - g_localDoneStep) >= kFmvBarrierTimeoutSteps)
				AskGiveUp(clock, step);

			return false;
		}

		void RefreshStatus()
		{
			if (!g_started)
			{
				_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
				    "fmv: not syncing%s",
				    g_installed ? "" : ", AND THE BARRIER IS NOT INSTALLED");
				return;
			}

			if (g_phase == PhaseWaiting)
			{
				const int missing = MissingMask();
				for (int peer = 0; peer < MaxPlayers; ++peer)
				{
					if ((missing & (1 << peer)) == 0)
						continue;
					if (g_hadNoMovie)
						_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
						    "fmv: SCRIPT HELD at the Movie:%d wait. THIS MACHINE COULD NOT "
						    "OPEN THE VIDEO, waiting for %s to watch theirs. Not a network "
						    "fault.",
						    g_waitSlot, PeerLabel(peer));
					else
						_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
						    "fmv: SCRIPT HELD at the Movie:%d wait, your video finished and "
						    "%s has not. Not a network fault.",
						    g_waitSlot, PeerLabel(peer));
					return;
				}
			}

			FmvState f;
			if (ReadFmvState(&f) && (f.playing || f.scriptRunning || f.scriptBlocking))
			{
				_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
				    "fmv: video %d playing%s, both scripts will resume on one agreed step",
				    f.videoId, f.skipRequested ? " (skipped)" : "");
				return;
			}

			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "fmv: running, %ld barriers, %ld cancels sent, %ld applied, %ld give-ups",
			    g_barriers, g_cancelsSent, g_cancelsApplied, g_giveUps);
		}

	} // namespace

	bool InstallFmvSync()
	{
		if (g_installed)
			return true;

		g_installed = HookFmvWaits(&WaitGate);
		if (g_installed)
			Log("fmv: barrier installed. With no session the gate lets every movie wait "
			    "complete exactly as the shipped game does.");
		else
			Log("fmv: could not install the barrier on the ATEL Movie poll handlers");

		return g_installed;
	}

	bool FmvSyncInstalled()
	{
		return g_installed;
	}

	void StartFmvSync()
	{
		GoIdle();
		g_barrierTag = 0;
		g_haveServicedStep = false;
		g_barriers = 0;
		g_cancelsSent = 0;
		g_cancelsApplied = 0;
		g_giveUps = 0;
		g_sendFailures = 0;
		g_startAsymmetries = 0;
		g_longestWaitSteps = 0;
		g_started = true;

		if (!g_installed)
		{
			Log("fmv: syncing was asked for but the barrier is not installed, so every "
			    "FMV in this session will end at a different simulation step on each "
			    "machine and the two scripts will be permanently offset afterwards.");
			return;
		}

		Log("fmv: syncing. Whoever's video finishes first has their script parked until "
		    "the others finish, and both resume on one agreed step. A skip by either "
		    "player skips it for everybody.");
	}

	void StopFmvSync()
	{
		// Never leave the script parked because the link went away. This is the one
		// thing in this file that absolutely must not be missed, and it is the same
		// hazard PauseSync has with its hold reason.
		if (g_phase == PhaseWaiting)
			Log("fmv: the session ended while the barrier was up, letting the script go");

		g_started = false;
		GoIdle();
		RefreshStatus();
	}

	bool FmvSyncActive()
	{
		return g_started;
	}

	void StepFmvSync()
	{
		// Every decision this file makes is made on the simulation step, inside
		// WaitGate. This is the frame-path half and it deliberately decides nothing:
		// it drops the state when there is nothing left that could release it, and it
		// keeps the readout current.
		if (!g_started)
		{
			if (g_phase != PhaseIdle)
				GoIdle();
			RefreshStatus();
			return;
		}

		if (!ActiveLockstep() && g_phase != PhaseIdle)
		{
			Log("fmv: no lockstep clock, so nothing can report a video finishing and the "
			    "barrier is being dropped");
			GoIdle();
		}

		// NOT DETECTED HERE, deliberately. An earlier cut of this watched for "the
		// script says a movie is blocking but there is no player object" as the
		// failed-video signature, and it is a false positive after every normal movie:
		// g_ffxFmvScriptBlocking is set by Movie:10 start and the ONLY thing in the
		// binary that clears it is FFX_Fmv_AbortForSceneChange, so it stays set long
		// after the player has been destroyed. The real detection is in the gate, on
		// the step the wait is armed, where the state is still meaningful.
		RefreshStatus();
	}

	bool FmvBarrierWaiting()
	{
		return g_started && g_phase == PhaseWaiting;
	}

	const char* FmvSyncStatus()
	{
		return g_status;
	}

	void LogFmvSync()
	{
		Log("=== fmv ===");
		Log("%s", g_status);

		if (!g_installed)
		{
			Log("the barrier is NOT installed, so a movie ends on whichever simulation "
			    "step each machine's decode thread happens to finish on and the two "
			    "scripts are offset by the difference from then on. That is a permanent "
			    "divergence, not a hitch.");
		}

		Log("phase %s, wait slot Movie:%d, reported mask 0x%X, needed 0x%X, barrier tag %u",
		    g_phase == PhaseWaiting ? "WAITING" : (g_phase == PhaseOpen ? "open" : "idle"),
		    g_waitSlot, (unsigned)g_reportedMask, (unsigned)NeededMask(),
		    (unsigned)g_barrierTag);

		Lockstep* clock = ActiveLockstep();
		if (clock && g_phase == PhaseWaiting)
		{
			const uint32_t step = clock->CurrentStep();
			Log("parked since step %u, now step %u, %u of %u steps into the timeout",
			    (unsigned)g_localDoneStep, (unsigned)step,
			    (unsigned)(step - g_localDoneStep), (unsigned)kFmvBarrierTimeoutSteps);
		}

		Log("%ld barriers, %ld cancels sent, %ld cancels applied, %ld give-ups, "
		    "%ld commands that could not be sent, %ld video files that would not open, "
		    "longest park %u steps",
		    g_barriers, g_cancelsSent, g_cancelsApplied, g_giveUps, g_sendFailures,
		    g_startAsymmetries, (unsigned)g_longestWaitSteps);

		Log("the barrier is on the SCRIPT WAIT and not on the simulation, because the "
		    "simulation keeps running through a movie. FFX_MainStep's early return "
		    "tests a user-driven suspend overlay, not playback, and the proof is that "
		    "the movie waits are ATEL poll handlers stepped from inside the sub-step "
		    "loop that the early return would have skipped.");
		Log("kHoldFmv is deliberately never raised. The hold stops FFX_MainStep, which "
		    "stops this machine's lockstep clock, while the machine still playing its "
		    "video keeps stepping. The two clocks would then be on different step "
		    "numbers for the rest of the session.");

		LogCutsceneState();
	}

} // namespace pilgrimage
