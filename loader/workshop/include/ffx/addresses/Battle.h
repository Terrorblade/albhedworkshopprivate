#pragma once

#include <windows.h>

// The battle command path: how a chosen action becomes a committed action.
//
// This area owns the COMMAND path and nothing else. Three other areas already hold
// battle addresses and they keep them, because each owns a different thing:
//
//   addresses/Encounter.h   starting a battle. BattlePendingKind, BtlBeginBattle,
//                           BtlMainStep, the zone and formation rolls.
//   addresses/GameState.h   the party roster and the save block side.
//                           BattlePartyOrder, BtlSetupUnitRoster,
//                           BtlCommitPartyToField, BattleIsActive, BattleGetActor.
//   addresses/EscMenu.h     BoosterAutoBattle, because that is a booster setting.
//
// So BattleGetActor and BattleIsActive are NOT redeclared here. Use
// Rva::BattleGetActor from GameState.h. See the collision rule in Addresses.h.
//
// ===========================================================================
// THE COMMIT POINT
// ===========================================================================
//
// Everything that makes a battle actor perform an action ends up in one store, and
// the player menu and the monster AI both reach it through the same function. That
// was checked by decompiling all ten callers rather than assumed.
//
//   FFX_Btl_CmdQueue_Push       the store itself
//   FFX_Btl_CommitCommand       one level up, and the one to call
//
// The ten callers of CommitCommand: the menu confirm, the four menu page escape
// shortcuts, the three forced-action paths for confuse, berserk and provoke, the AI
// script runner, and FFX_Btl_SendMenu pushing an empty placeholder record.
//
// WHAT THIS MEANS FOR CO-OP. The engine already takes the acting unit as data, so a
// second player's command needs no simulation. Replicate the 72 byte record plus the
// gil cost plus the variant, and call CommitCommand on every machine. Do NOT try to
// run two menus: the menu's staging record is a single global that is rebuilt from
// the page stack every frame, so two cursors would corrupt each other. See the
// single-instance list at the bottom of this file.
//
// GATES AT COMMIT, and this is the good news: the queue's 62 slot capacity, and
// variant <= 1. That is all. No CTB readiness test, no MP check, no status check, no
// "this actor already acted" flag. A record replayed a few steps later is accepted.
// The legality checks live earlier in the menu and again later in the executor.
//
// The derivation is in ..\..\..\..\reversing\BATTLE_COMMAND.md.

namespace ffx
{
	namespace Rva
	{

		// ---------------------------------------------------------------------------
		// The command queue. One global FIFO shared by every actor.
		// ---------------------------------------------------------------------------

		// int __cdecl (int actorIndex, const void *cmd72, char postKind, char prio,
		//              char variant)
		//
		// Appends cmd72 to the queue, then OVERWRITES the record header from its own
		// arguments: +0 = actorIndex, +1 = variant, +2 = 0, +4..5 = 0, +6 = postKind,
		// +7 = prio. Only +3 (the entry count) and the four action entries survive from
		// the caller's record, which is worth knowing before filling a record by hand.
		//
		// It also calls FFX_Btl_Cmd_ConsumeItems, so committing an item command is what
		// decrements the inventory. Under lockstep that means both machines must push,
		// or their inventories part company.
		const DWORD BtlCmdQueuePush = 0x003B0B90;

		// int __cdecl (u8 *cmd72, int gilCost, unsigned variant)
		//
		// THE ONE TO CALL. Reads the acting unit out of the record's +0, sets
		// g_ffxBtlPendingGilCost, and pushes. Passes postKind and prio as 0, so the
		// record's own +6 and +7 are discarded.
		//
		// IT DOES NOT RETIRE THE TURN QUEUE ENTRY, and an earlier pass of this file said
		// it did. Read the return sense carefully, because it is inverted twice over:
		// Push returns -1 for SUCCESS and 0 when the queue is full, so
		// `if (Push(...) != 0) return 0;` is the success branch and the
		// FFX_Btl_TurnQueue_RemoveAt below it only runs when the push FAILED. Normal turn
		// retirement is in sub_78D980, the effect-completion path.
		//
		// This function returns 0 for success and -1 for "did nothing".
		const DWORD BtlCommitCommand = 0x00392D60;

		// The insert variant, used for counters and extra turns. Same record shape,
		// ordered by the priority class rather than appended.
		const DWORD BtlCommitCommandPriority = 0x003929B0;

		const DWORD BtlCmdQueue = 0x00D2AC70;      // 62 slots of 72 bytes
		const DWORD BtlCmdQueueCount = 0x00D2BDE1; // byte
		const int BtlCmdQueueCapacity = 62;
		const int BtlCommandRecordBytes = 72;

		const DWORD BtlCmdQueueGetHead = 0x003B0A10;
		const DWORD BtlCmdQueueGetActorCmd = 0x003B09E0;
		const DWORD BtlCmdQueueGetActorCmdOrLast = 0x003B09B0;
		const DWORD BtlCmdQueueInsert = 0x003B0A30;
		const DWORD BtlCmdQueueRemoveAt = 0x003B0860;
		const DWORD BtlCmdQueueRemoveAllForActor = 0x003B0830;
		const DWORD BtlCmdQueueFindByVariant = 0x003B0720;
		const DWORD BtlCmdQueueFindByPriority = 0x003B0770;
		const DWORD BtlCmdQueueActorHasOtherPending = 0x003B07D0;
		const DWORD BtlCmdConsumeItems = 0x003B0C20;

		// ---------------------------------------------------------------------------
		// The turn queue, which is the CTB order rather than the command list.
		// ---------------------------------------------------------------------------
		const DWORD BtlTurnQueue = 0x00D2AA80;
		const DWORD BtlTurnQueueCount = 0x00D2BDE0; // byte
		const DWORD BtlTurnQueueGetHead = 0x003B22E0;
		const DWORD BtlTurnQueuePush = 0x003B2430;
		const DWORD BtlTurnQueueInsert = 0x003B2300;
		const DWORD BtlTurnQueueRemoveAt = 0x003B20E0;
		const DWORD BtlTurnQueueRemoveAllForActor = 0x003B20B0;
		const DWORD BtlTurnQueueSetClaimed = 0x003B2290;
		const DWORD BtlTurnQueueClaimHeadForActor = 0x003B2020;
		const DWORD BtlTurnQueueActorTurnIsReal = 0x003B2060;
		const DWORD BtlTurnQueueActorHasNonClass4 = 0x003B1FD0;

		// ---------------------------------------------------------------------------
		// The CTB clock.
		// ---------------------------------------------------------------------------
		// The early-out is
		//     if (g_ffxBtlDbgCtbPause != 0 || g_ffxBtlTurnQueueCount != 0) return 0;
		// and it tests the TURN queue count at 0x112BDE0, NOT the command queue count at
		// the adjacent 0x112BDE1. Read off the disassembly at 0x790FC1, because the two
		// globals are one byte apart and an earlier note had the wrong one.
		//
		// That difference is load bearing rather than pedantic. The clock is frozen for
		// the whole span from the tick that pushed a turn entry to the effect-completion
		// path retiring it, which covers a player sitting in a menu and a replicated
		// command still in flight alike. None of the writers of either count lives in the
		// menu module, so an open menu is invisible to the simulation. That is what makes
		// it safe for one machine to open a menu and the other not to.
		const DWORD BtlCtbTick = 0x00390FB0;
		const DWORD BtlIsCtbTickAllowed = 0x00391190;
		const DWORD BtlCtbReadyListPush = 0x0038D4C0;
		const DWORD BtlCtbReadyListClear = 0x0038D4B0;
		const DWORD BtlCtbReadyListSortByCounter = 0x0038D3E0;
		const DWORD BtlCtbReadyList = 0x00D333C4;
		const DWORD BtlCtbReadyCount = 0x00D333C0;
		const DWORD BtlCtbSubTick = 0x00D2BDE2;      // byte
		const DWORD BtlCtbSubTickLimit = 0x00D2BDE3; // byte
		const DWORD BtlBuildCtbPreview = 0x0039A200;

		// ---------------------------------------------------------------------------
		// The executor, and the two gates a replayed command can still fail.
		//
		// Only two things between a committed record and the action happening, which is
		// the whole reason a network command does not need to be re-validated:
		//
		//   ResolveTargets    the target must still be alive. A command aimed at
		//                     something that died in between resolves to nothing.
		//   CheckCommandCost  MP at actor+0x5D4 and overdrive at actor+0x5BC.
		//
		// Both are deterministic given the same world, so under lockstep both machines
		// reach the same answer. They are listed because they are the two places a
		// command can be accepted and then quietly do nothing.
		// ---------------------------------------------------------------------------
		const DWORD BtlExecCommand = 0x00392210;
		const DWORD BtlResolveTargets = 0x00391FA0;
		const DWORD BtlCheckCommandCost = 0x0038AB20;
		const DWORD BtlGetCommandMpCost = 0x0038C690;
		const DWORD BtlResolveAbilityId = 0x0038CE50;
		const DWORD BtlIsCommandLocked = 0x0039A5B0;
		const DWORD BtlFinishActions = 0x003911E0;
		const DWORD BtlTryBeginActionFinish = 0x00391690;
		const DWORD BtlEndPhaseStep = 0x003917D0;
		const DWORD BtlPendingGilCost = 0x00D2BE90;

		// ---------------------------------------------------------------------------
		// Who is an ally, and who the AI drives.
		//
		// BtlIsAllyUnit is literally "index <= 30". Enemies live at 20..27 in the SAME
		// unit array, which is why the command path needs no separate enemy case and why
		// a target is just a bit in a 31 bit mask.
		// ---------------------------------------------------------------------------
		const DWORD BtlIsAllyUnit = 0x00393650;
		const DWORD BtlGetEnemyUnit = 0x00395AA0;
		const DWORD BtlIsActorAiControlled = 0x00392120;
		const DWORD BtlGetPhase = 0x003816F0;
		const DWORD BtlIsBattlePendingOrActive = 0x00380C90;
		const DWORD BtlSubPhase = 0x00D2A8E0; // byte
		const int BtlMaxUnitIndex = 30;

		// ---------------------------------------------------------------------------
		// The menu. Read these, do not drive them.
		// ---------------------------------------------------------------------------
		const DWORD BtlMenuOpen = 0x0049BB10;
		const DWORD BtlMenuClose = 0x0049ADE0;
		const DWORD BtlMenuStep = 0x0049AE20;
		const DWORD BtlMenuPushPage = 0x0049A330;

		// int __cdecl (void). The player's confirm. Builds the staged record and calls
		// BtlCommitCommand. This is the point a co-op layer wants to INTERCEPT on the
		// machine whose player is choosing, so the record can be replicated before it is
		// committed locally.
		const DWORD BtlMenuConfirmCommand = 0x004975A0;
		const DWORD BtlMenuBuildStagedCommand = 0x0049AFC0;
		const DWORD BtlMenuClassifyCommand = 0x0049ACA0;
		const DWORD BtlMenuSaveCursorMemory = 0x004989D0;
		const DWORD BtlMenuRestoreCursorMemory = 0x00499AC0;

		// The ONLY caller of BtlMenuOpen in the whole binary, and the owner of the turn
		// claim. It takes the front turn queue entry, sets its claimed byte at +3, and
		// opens the menu for that unit. The claim is what makes suppressing the open safe:
		// a claimed turn is never offered again, so nothing re-asks and nothing spins.
		const DWORD BtlSendMenu = 0x00392A90;
		const DWORD BtlSendMenuBytes = 0x2CE; // ends at RVA 0x00392D5E

		// The exact E8 inside BtlSendMenu that calls BtlMenuOpen. There is precisely one
		// and this is it.
		//
		// Given as an address rather than found by scanning on purpose. Patching the
		// CALLER's rel32 is the right move here, because neither BtlMenuOpen nor
		// BtlMenuConfirmCommand has five position-independent bytes at its entry for a
		// normal detour to steal: Open starts push ebp / mov ebp,esp / E8, and
		// ConfirmCommand's very first instruction is an E8. Relocating a stolen call
		// leaves it pointing nowhere.
		//
		// Patching the call site is also strictly more precise. It hits the player path
		// only, and NOT the AI runner or the three forced-action paths for confuse,
		// berserk and provoke. Those must not be intercepted: both machines already reach
		// those decisions independently from identical state, so replicating them would
		// double every one of them.
		//
		// A hardcoded site is safe because the patcher verifies before it writes: the byte
		// must be 0xE8 and the displacement must resolve to BtlMenuOpen, or it refuses and
		// says so. On a build that moved, that is a clean refusal rather than a wild jump.
		const DWORD BtlMenuOpenCallSite = 0x00392CF3;

		const DWORD BtlMenuPageProcRoot = 0x0049C900;
		const DWORD BtlMenuPageProcB = 0x0049CDE0;
		const DWORD BtlMenuPageProcC = 0x004A22F0;
		const DWORD BtlMenuPageProcD = 0x004A2690;

		// ---------------------------------------------------------------------------
		// The AI script runner. The other half of the proof that the command path is
		// already data driven.
		//
		// BtlRunAiAndCommit stages into its own 72 byte record and then calls the
		// IDENTICAL BtlCommitCommand. So there is one record shape and one commit for
		// both the player and every monster in the game.
		// ---------------------------------------------------------------------------
		const DWORD BtlRunAiAndCommit = 0x003ACEB0;
		const DWORD BtlAiStagedCmd = 0x00D36A78;      // 72 bytes, one global
		const DWORD BtlAiScriptActorCtx = 0x00D36A68; // the commit mode gate
		const DWORD BtlAiStagedPriority = 0x00D36A70;
		const DWORD BtlAiStagedNoReorder = 0x00D36A6C;
		const DWORD BtlAiResetStagedCommand = 0x003ACB00;
		const DWORD BtlAiAddCommandEntry = 0x003AC9C0;
		const DWORD BtlAiOverrideCurrentEntry = 0x003AC940;
		const DWORD BtlAiGetCurrentEntryId = 0x003ACC90;

		// ---------------------------------------------------------------------------
		// SINGLE INSTANCE STATE ON THE COMMAND PATH. Worst first.
		//
		// This list is the reason the design is "replicate the record, do not run two
		// menus". Every entry is one slot that a second simultaneous commander would
		// share.
		//
		//  1. BtlMenuStagedCmd, 72 bytes. REBUILT FROM SCRATCH EVERY FRAME out of the
		//     page stack, and it stamps record+0 from page 0's owner. Two cursors on it
		//     is not a race, it is one player's command being attributed to the other
		//     player's actor. This is the hard one.
		//  2. BtlMenuPages plus BtlMenuPageDepth. One 8 deep page stack and one depth
		//     cursor for the whole menu.
		//  3. BtlMenuOwnerActor. One owner, 255 meaning none.
		//  4. BtlMenuTargetMask and the three candidate globals. One target cursor.
		//  5. BtlCmdPreview and BtlActingActor are cosmetic, the HUD and the camera.
		//     Worth knowing so a desync hunt does not chase them.
		//
		// NOT a hazard: BtlAiStagedCmd. Only one AI script runs per frame.
		//
		// THE SHAPE TO COPY: BtlMenuCursorMem and BtlMenuCursorMemValid are ALREADY per
		// actor. The shipped menu remembers each character's cursor separately, which is
		// what per player state on this path should look like.
		// ---------------------------------------------------------------------------
		const DWORD BtlMenuStagedCmd = 0x01FCC040; // 72 bytes
		const DWORD BtlMenuStagedGil = 0x01FCC038;
		const DWORD BtlMenuOwnerActor = 0x01FCC088; // byte, 255 for none
		const DWORD BtlMenuOpenMask = 0x01FCC08C;
		const DWORD BtlMenuPages = 0x00F3C950;
		const DWORD BtlMenuPageDepth = 0x01FCC092; // byte
		const int BtlMenuPageCapacity = 8;

		const DWORD BtlMenuTargetMask = 0x00F3D178;
		const DWORD BtlMenuTargetCandMasks = 0x00F3D180;
		const DWORD BtlMenuTargetCandUnits = 0x00F3F0DC;
		const DWORD BtlMenuTargetCandCount = 0x00F3F0D8;
		const DWORD BtlMenuTargetCursor = 0x00F3D1FC;

		const DWORD BtlCurTargetMask = 0x00D2C8C8;
		const DWORD BtlShownTargetMask = 0x00D2C8CC;
		const DWORD BtlCmdPreview = 0x00D2BDF0;
		const DWORD BtlCmdPreviewValid = 0x00D2C9E6; // byte
		const DWORD BtlActingActor = 0x00D2C9DF;     // byte
		const DWORD BtlSetCommandPreview = 0x00392E50;

		const DWORD BtlMenuCursorMem = 0x00F3D788;      // already per actor
		const DWORD BtlMenuCursorMemValid = 0x00F3D770; // already per actor

		// ---------------------------------------------------------------------------
		// Lookups a co-op layer needs to describe a command in a log.
		// ---------------------------------------------------------------------------
		const DWORD BtlGetAbilityRecord = 0x0039A4B0;
		const DWORD BtlGetPlayerAbilityRow = 0x00390A90;
		const DWORD BtlGetMonsterAbilityRow = 0x00390A50;
		const DWORD BtlGetItemRow = 0x003909F0;
		const DWORD BtlGetEscapeCommandId = 0x0038FB80;
		const DWORD BtlEscapeAllowed = 0x00D2CA00;

		// ---------------------------------------------------------------------------
		// Battle randomness. Listed here because it is the obvious lockstep hazard on
		// this path: if the two machines roll differently, identical commands produce
		// different damage. These are the three entry points to check.
		// ---------------------------------------------------------------------------
		const DWORD BtlRand = 0x003989A0;
		const DWORD BtlRandFloat1to2 = 0x00398930;
		const DWORD BtlRandPercentCheck = 0x003A8AD0;

		// ---------------------------------------------------------------------------
		// A shipped debug flag worth one line of comment, because it is proof rather
		// than a feature.
		//
		// Non-zero makes AI driven units open the PLAYER MENU instead of running their
		// script. It is in the retail binary. So the shipped code already contains a
		// path where the menu drives an arbitrary unit, which is the strongest available
		// evidence that per unit menu control is a thing this engine can do.
		// ---------------------------------------------------------------------------
		const DWORD BtlDbgMonInput = 0x00D2A8FA; // byte

		// Also shipped, also useful for testing a command path without dying.
		const DWORD BtlDbgPlyInvincible = 0x00D2A8F9;
		const DWORD BtlDbgMonInvincible = 0x00D2A8F8;
		const DWORD BtlDbgCtbPause = 0x00D2A8E1;
		const DWORD BtlDbgAutoExecute = 0x00D2A8F5;
		const DWORD BtlDbgSkipCommand = 0x00D2A922;
		const DWORD BtlDbgPrintInfo = 0x00D2A90B;

		const DWORD BtlInternalError = 0x0039F210;

		// ---------------------------------------------------------------------------
		// Every address above, for the startup build check.
		// ---------------------------------------------------------------------------
		inline const DWORD* BattleRvaList(int* count)
		{
			static const DWORD list[] = {
				BtlCmdQueuePush,
				BtlCommitCommand,
				BtlCommitCommandPriority,
				BtlCmdQueue,
				BtlCmdQueueCount,
				BtlCmdQueueGetHead,
				BtlCmdQueueGetActorCmd,
				BtlCmdQueueGetActorCmdOrLast,
				BtlCmdQueueInsert,
				BtlCmdQueueRemoveAt,
				BtlCmdQueueRemoveAllForActor,
				BtlCmdQueueFindByVariant,
				BtlCmdQueueFindByPriority,
				BtlCmdQueueActorHasOtherPending,
				BtlCmdConsumeItems,
				BtlTurnQueue,
				BtlTurnQueueCount,
				BtlTurnQueueGetHead,
				BtlTurnQueuePush,
				BtlTurnQueueInsert,
				BtlTurnQueueRemoveAt,
				BtlTurnQueueRemoveAllForActor,
				BtlTurnQueueSetClaimed,
				BtlTurnQueueClaimHeadForActor,
				BtlTurnQueueActorTurnIsReal,
				BtlTurnQueueActorHasNonClass4,
				BtlCtbTick,
				BtlIsCtbTickAllowed,
				BtlCtbReadyListPush,
				BtlCtbReadyListClear,
				BtlCtbReadyListSortByCounter,
				BtlCtbReadyList,
				BtlCtbReadyCount,
				BtlCtbSubTick,
				BtlCtbSubTickLimit,
				BtlBuildCtbPreview,
				BtlExecCommand,
				BtlResolveTargets,
				BtlCheckCommandCost,
				BtlGetCommandMpCost,
				BtlResolveAbilityId,
				BtlIsCommandLocked,
				BtlFinishActions,
				BtlTryBeginActionFinish,
				BtlEndPhaseStep,
				BtlPendingGilCost,
				BtlIsAllyUnit,
				BtlGetEnemyUnit,
				BtlIsActorAiControlled,
				BtlGetPhase,
				BtlIsBattlePendingOrActive,
				BtlSubPhase,
				BtlMenuOpen,
				BtlMenuClose,
				BtlMenuStep,
				BtlMenuPushPage,
				BtlMenuConfirmCommand,
				BtlMenuBuildStagedCommand,
				BtlMenuClassifyCommand,
				BtlMenuSaveCursorMemory,
				BtlMenuRestoreCursorMemory,
				BtlSendMenu,
				BtlMenuOpenCallSite,
				BtlMenuPageProcRoot,
				BtlMenuPageProcB,
				BtlMenuPageProcC,
				BtlMenuPageProcD,
				BtlRunAiAndCommit,
				BtlAiStagedCmd,
				BtlAiScriptActorCtx,
				BtlAiStagedPriority,
				BtlAiStagedNoReorder,
				BtlAiResetStagedCommand,
				BtlAiAddCommandEntry,
				BtlAiOverrideCurrentEntry,
				BtlAiGetCurrentEntryId,
				BtlMenuStagedCmd,
				BtlMenuStagedGil,
				BtlMenuOwnerActor,
				BtlMenuOpenMask,
				BtlMenuPages,
				BtlMenuPageDepth,
				BtlMenuTargetMask,
				BtlMenuTargetCandMasks,
				BtlMenuTargetCandUnits,
				BtlMenuTargetCandCount,
				BtlMenuTargetCursor,
				BtlCurTargetMask,
				BtlShownTargetMask,
				BtlCmdPreview,
				BtlCmdPreviewValid,
				BtlActingActor,
				BtlSetCommandPreview,
				BtlMenuCursorMem,
				BtlMenuCursorMemValid,
				BtlGetAbilityRecord,
				BtlGetPlayerAbilityRow,
				BtlGetMonsterAbilityRow,
				BtlGetItemRow,
				BtlGetEscapeCommandId,
				BtlEscapeAllowed,
				BtlRand,
				BtlRandFloat1to2,
				BtlRandPercentCheck,
				BtlDbgMonInput,
				BtlDbgPlyInvincible,
				BtlDbgMonInvincible,
				BtlDbgCtbPause,
				BtlDbgAutoExecute,
				BtlDbgSkipCommand,
				BtlDbgPrintInfo,
				BtlInternalError,
			};
			if (count)
				*count = (int)(sizeof(list) / sizeof(list[0]));
			return list;
		}

	} // namespace Rva
} // namespace ffx
