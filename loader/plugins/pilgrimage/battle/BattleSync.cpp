#include "battle/BattleSync.h"

#include <stdio.h>
#include <string.h>

#include "battle/BattleOwnership.h"
#include "ffx/Battle.h"
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

		// How many commands we have captured and asked for but not yet seen come back
		// from the host. Not a queue, just a count, because the engine only lets one
		// turn be claimed at a time so this is 0 or 1 in practice. It is the number
		// worth watching: a non-zero value that stays non-zero is a battle that is
		// waiting on a command the host is never going to issue.
		LONG g_awaiting = 0;

		LONG g_sent = 0;
		LONG g_applied = 0;
		LONG g_dropped = 0;    // arrived, and the engine would not take it
		LONG g_suppressed = 0; // a commit on a unit we do not own, which should be 0
		LONG g_sendFailures = 0;

		char g_status[200] = "battle: not syncing";

		// The step the last applied command landed on, for the log.
		uint32_t g_lastAppliedStep = 0;

		// ---------------------------------------------------------------------------
		// Capture. Runs inside the battle step, on the game thread, at the exact
		// instant the engine was about to commit the player's choice.
		//
		// Returning true means "I have taken this over, do not commit it locally".
		// Returning false lets the engine commit exactly as shipped, which is what a
		// solo game gets and what anything this file cannot handle gets.
		// ---------------------------------------------------------------------------
		bool __cdecl OnMenuCommit(const BattleCommandRecord* record, int gilCost, unsigned variant)
		{
			if (!g_started || !record)
				return false;

			Lockstep* clock = ActiveLockstep();
			Session* session = ActiveSession();
			if (!clock || !session)
				return false;

			const int unit = (int)record->unit;

			// A commit for a unit this machine does not own should be impossible,
			// because the menu was never opened for it here. It is caught anyway, and
			// DROPPED rather than passed through, because passing it through would put
			// an action in this machine's queue that no other machine has, and that is
			// a divergence with no way back. Losing one action is recoverable, a
			// divergence is not.
			if (!LocalOwnsUnit(unit))
			{
				InterlockedIncrement(&g_suppressed);
				Log("battle: dropped a commit for unit %d, which peer %d owns, not us. The "
				    "menu should never have opened here, so the suppression hook is not "
				    "doing its job.",
				    unit, BattleOwnerOfUnit(unit));
				return true;
			}

			// Everything that goes on the wire comes from the arguments the engine was
			// handed, not from the staged globals. Those are the same bytes today, and
			// reading the arguments means the four menu page escape shortcuts are
			// covered by the same code without this file having to know that they pass
			// a gil cost of 0 rather than the staged one.
			BattleCommitCommand msg;
			memset(&msg, 0, sizeof(msg));
			memcpy(msg.record, record, sizeof(msg.record));
			msg.gilCost = (int32_t)gilCost;
			msg.variant = (uint8_t)(variant & 0xFF);
			msg.unitForLog = (uint8_t)unit;

			char line[320];
			DescribeBattleCommand(record, line, sizeof(line));

			if (!clock->RequestCommand((uint8_t)kCommandBattleCommit, &msg, (int)sizeof(msg)))
			{
				InterlockedIncrement(&g_sendFailures);
				Log("battle: could not send %s, so it is being dropped. Committing it "
				    "locally would act on one machine only.",
				    line);

				// The engine has already claimed this unit's turn entry, and
				// FFX_Btl_SendMenu never offers a claimed turn again, so dropping the
				// command here would hang the battle outright. Giving the turn back is
				// the one recovery there is: the menu opens again next frame and the
				// player picks again.
				//
				// It writes a byte the other machine is not writing, which is acceptable
				// precisely here: the send failed, so the other machine never heard about
				// this turn at all and has nothing to be out of step with.
				if (!ReleaseBattleTurnClaim(unit))
				{
					Log("battle: and the turn could not be given back either, so this "
					    "battle is now stuck on unit %d's turn",
					    unit);
				}
				return true;
			}

			InterlockedIncrement(&g_sent);
			InterlockedIncrement(&g_awaiting);
			Log("battle: asked for %s, gil %d", line, gilCost);

			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "battle: waiting on unit %d's command", unit);
			return true;
		}

		// ---------------------------------------------------------------------------
		// Suppression. The one call to FFX_BtlMenu_Open in the binary.
		//
		// This is the mechanism and the capture hook is the safety net, not the other
		// way round. A menu that never opens cannot stage a command, cannot move the
		// target cursor, and cannot read this machine's pad into the other player's
		// turn.
		// ---------------------------------------------------------------------------
		bool __cdecl OnMenuOpen(int unitIndex)
		{
			if (!g_started)
				return true;
			if (LocalOwnsUnit(unitIndex))
				return true;

			// No log here. This is the common case, once per enemy-side and remote-side
			// turn for the whole battle, and a log line per turn would bury everything
			// else. The count is in the status line and ffx::BattleMenuOpensSuppressed.
			return false;
		}

		void ApplyOne(const Command& command)
		{
			if (command.length < (int)sizeof(BattleCommitCommand))
			{
				InterlockedIncrement(&g_dropped);
				Log("battle: a command from peer %u is %d bytes, and the payload is %d. "
				    "That is a protocol mismatch, so the two builds do not agree.",
				    (unsigned)command.issuer, command.length, (int)sizeof(BattleCommitCommand));
				return;
			}

			BattleCommitCommand msg;
			memcpy(&msg, command.data, sizeof(msg));

			// The record is rebuilt from the wire and nothing else. No staged global, no
			// local menu state, no local unit lookup. That is the point: the two machines
			// commit identical bytes because they are the same bytes.
			BattleCommandRecord record;
			memcpy(&record, msg.record, sizeof(record));

			Session* session = ActiveSession();
			if (session && command.issuer == session->LocalPeer() && g_awaiting > 0)
				InterlockedDecrement(&g_awaiting);

			char line[320];
			DescribeBattleCommand(&record, line, sizeof(line));

			if (!BattleCommandPhase())
			{
				// The battle ended between the choice and its step. Both machines should
				// reach this together, because the thing that ended the battle is
				// simulated state. If only one of them does, that is already a
				// divergence and this log line is where it will show up first.
				InterlockedIncrement(&g_dropped);
				Log("battle: peer %u's command for step %u arrived with no battle running, "
				    "dropping it. %s",
				    (unsigned)command.issuer, (unsigned)command.step, line);
				return;
			}

			const int result = CommitBattleCommand(&record, (int)msg.gilCost, (unsigned)msg.variant);
			if (result <= 0)
			{
				InterlockedIncrement(&g_dropped);
				Log("battle: peer %u's command was refused (%d) on step %u. %s",
				    (unsigned)command.issuer, result, (unsigned)command.step, line);
				_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
				    "battle: a command was refused on step %u", (unsigned)command.step);
				return;
			}

			InterlockedIncrement(&g_applied);
			g_lastAppliedStep = command.step;
			Log("battle: peer %u's command committed on step %u. %s",
			    (unsigned)command.issuer, (unsigned)command.step, line);

			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "battle: %ld committed, last on step %u", g_applied, (unsigned)command.step);
		}

	} // namespace

	bool InstallBattleSync()
	{
		if (g_installed)
			return true;

		// Two independent hooks, and losing either one is worth a different sentence,
		// so they are reported separately rather than as one bool.
		const bool capture = HookBattleMenuCommit(&OnMenuCommit);
		const bool suppress = HookBattleMenuOpen(&OnMenuOpen);

		if (!capture)
		{
			Log("battle: only %d of the player commit sites were hooked, so some battle "
			    "actions will be committed on this machine alone and the two simulations "
			    "will come apart on the first one",
			    BattleMenuCommitHookSites());
		}
		if (!suppress)
		{
			Log("battle: the menu open hook is not in, so BOTH machines will open a menu "
			    "for the same unit. They share one staging record, so the two cursors "
			    "will corrupt each other's command.");
		}

		g_installed = capture && suppress;
		if (g_installed)
			Log("battle: command sync hooks installed, idle until a session starts");

		return g_installed;
	}

	bool BattleSyncInstalled()
	{
		return g_installed;
	}

	void StartBattleSync()
	{
		ResetBattleOwnership();

		g_started = true;
		g_awaiting = 0;
		g_sent = 0;
		g_applied = 0;
		g_dropped = 0;
		g_suppressed = 0;
		g_sendFailures = 0;
		g_lastAppliedStep = 0;

		if (!g_installed)
		{
			strcpy_s(g_status, sizeof(g_status),
			    "battle: NOT syncing, the hooks are not in");
			Log("battle: a session started but the hooks are not installed, so a battle in "
			    "this session will diverge");
			return;
		}

		strcpy_s(g_status, sizeof(g_status), "battle: syncing, no commands yet");
		Log("battle: syncing commands. %s", BattleOwnershipStatus());
	}

	void StopBattleSync()
	{
		g_started = false;
		g_awaiting = 0;
		strcpy_s(g_status, sizeof(g_status), "battle: not syncing");
	}

	bool BattleSyncActive()
	{
		return g_started;
	}

	void StepBattleSync()
	{
		if (!g_started)
			return;

		Lockstep* clock = ActiveLockstep();
		if (!clock)
			return;

		// Eight is the same bound BoosterSync uses and is far more than can land on one
		// step: the engine only lets one turn be claimed at a time, so one command per
		// player per turn is the real ceiling.
		const Command* commands[8];
		const int count = clock->CommandsForStep(commands, 8);
		for (int i = 0; i < count; ++i)
		{
			if (commands[i]->kind != (uint8_t)kCommandBattleCommit)
				continue;
			ApplyOne(*commands[i]);
		}
	}

	const char* BattleSyncStatus()
	{
		return g_status;
	}

	void LogBattleSync()
	{
		Log("=== battle command sync ===");
		Log("%s", g_status);

		if (!g_installed)
		{
			Log("the hooks are not installed. Capture sites: %d of 5. Open hook: %s.",
			    BattleMenuCommitHookSites(),
			    BattleMenuOpenHookInstalled() ? "in" : "NOT in");
		}

		if (!g_started)
		{
			Log("not syncing, so every battle menu on this machine is the local player's "
			    "and nothing is sent. That is correct alone and a divergence in a session.");
		}

		Log("%ld sent, %ld applied, %ld refused, %ld awaiting a reply", g_sent, g_applied,
		    g_dropped, g_awaiting);
		Log("%ld send failures, %ld commits dropped for a unit we do not own", g_sendFailures,
		    g_suppressed);
		Log("engine side: %ld commits seen, %ld taken over, %ld menus suppressed",
		    BattleMenuCommitsSeen(), BattleMenuCommitsTaken(), BattleMenuOpensSuppressed());

		if (g_awaiting > 0)
		{
			Log("STILL WAITING. The acting unit's turn queue entry is claimed, and "
			    "FFX_Btl_SendMenu never offers a claimed turn again, so this battle is "
			    "held until the host issues that command.");
		}

		LogBattleOwnership();
		LogBattleState();
	}

} // namespace pilgrimage
