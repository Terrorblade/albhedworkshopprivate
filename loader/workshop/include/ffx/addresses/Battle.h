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

		// EVERY BATTLE MENU PAGE HAS A WALL-CLOCK INPUT LOCKOUT, and this is where it
		// comes from. BtlMenuStep walks BtlMenuPages through a six state machine:
		// state 2 stamps record+0xDC from FFX_Time_AppElapsedSeconds, state 4 writes
		// record+0xE0 = now - startTime, and eight of the nine state 4 callbacks refuse
		// to run their page proc until elapsed passes 0.30 s or 0.45 s. So the number of
		// SUB STEPS a page is deaf for depends on the frame rate, and a player's first
		// press after a page opens lands on a different step on each peer.
		//
		// Patch the two call sites, not the clock: it has 30 callers and most are UI
		// pulsing that nothing reads back. Both sites verified as 0xE8 rel32 to
		// 0x00241410. At one step they become 9 and 13 steps, which is deterministic.
		const DWORD BtlMenuPageStartClockCallSite = 0x0049AEF6;   // state 2, startTime
		const DWORD BtlMenuPageElapsedClockCallSite = 0x0049AF39; // state 4, elapsed
		// BtlMenuPages is declared further down, in the page stack block.

		// The overdrive minigame clock. A STEP COUNTER, not a wall clock: it adds 1.0
		// per sub step and divides by 30. The 25.0 path is dead, its selector
		// MesWinFontMode is computed in FFX_MainInit from a byte that is still zero.
		// It does burn one FFX_Btl_Rand draw per sub step to randomise the countdown's
		// hundredths digit, which is cosmetic but moves the battle RNG.
		const DWORD BtlOdStepSharedTimer = 0x00491AC0;
		const DWORD BtlOdResetSharedTimer = 0x00497F00;
		const DWORD BtlOdGetTimeRemaining = 0x00497780;
		const DWORD BtlOdSetTimeBudget = 0x0049A2A0;
		const DWORD BtlOdStepCounter = 0x00F3F788;   // float, += 1.0 per sub step
		const DWORD BtlOdTimeBudget = 0x00F3F78C;    // float
		const DWORD BtlOdTimeRemaining = 0x00F3F790; // float
		const DWORD BtlOdMinigamePhase = 0x00F3F77C; // dword
		const DWORD MesWinFontMode = 0x01465F00;     // picks 25 vs 30, always 0

		// Lulu's Fury is the one native overdrive minigame that reads a wall clock, and
		// the clock reaches exactly one thing: clearing the two KEYBOARD key latches
		// after half a real second with the stick out of all four corner quadrants. The
		// pad path is pure math off the pad ring at lag -1 and lag 0, and THE TIME LIMIT
		// IS NOT WALL CLOCK EITHER. BtlOdTimeRemaining is budget minus stepCounter / fps
		// and BtlOdStepSharedTimer is its only writer, so it replicates. Hash these two
		// floats to catch a keyboard player diverging, and leave the rest alone.
		const DWORD BtlOdLuluClockNow = 0x00F3C920;  // float, wall clock
		const DWORD BtlOdLuluClockMark = 0x00F3C924; // float, wall clock

		// The two call sites to patch if that latch timeout has to become step derived.
		// Both are E8 rel32 to Rva::TimeAppElapsedSeconds, verified in the IDB.
		const DWORD BtlOdLuluClockCallSiteNow = 0x00491BB6;
		const DWORD BtlOdLuluClockCallSiteMark = 0x00491BD6;

		// NOT A CALL SITE, so do not try to patch it like one. It is the
		// fld flt_B5EDC8 / fcomp now - mark / test ah,5 / jp compare that guards the
		// latch clear, and BtlOdLuluDeadZoneWindowConst is the float it loads.
		const DWORD BtlOdLuluDeadZoneClockSite = 0x00491DA3;
		const DWORD BtlOdLuluDeadZoneWindowConst = 0x0075EDC8; // float 0.5001, .rdata
		const DWORD BtlOdLuluKeyboardReader = 0x00245EB0;
		const DWORD BtlOdLuluKeyA = 0x00F3C928;         // dword, keyboard latch
		const DWORD BtlOdLuluKeyB = 0x00F3C92C;         // dword, keyboard latch
		const DWORD BtlOdLuluKeyboardHitSite = 0x00491E86;

		// BoosterAutoBattle is declared in addresses/EscMenu.h. It forces input in two
		// places in the overdrive family, so a peer with the booster on and a peer
		// without it play a different minigame. Hash it.

		// Rotate-the-stick overdrive minigame. Reached by BtlMenuStep -> FFX_Btl_MainStep,
		// so it is inside the simulation and its result scales overdrive damage. The only
		// clock in it is the keyboard latch timeout above, so the stick path replicates as
		// long as the pad ring does.
		//
		// IT SETS ThreadedPadMode WHILE IT RUNS, and that changes the pad commit cadence
		// from once per frame to once per sub step. See the ring block in addresses/Input.h.
		const DWORD BtlOdLuluFuryStickMinigame = 0x00491B80;
		const DWORD BtlOdLuluSetThreadedPadSite = 0x00491C7E;   // ThreadedPadMode = 1
		const DWORD BtlOdLuluClearThreadedPadSite = 0x00491FD8; // ThreadedPadMode = 0

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
		// THE OVERDRIVE MINIGAME FAMILY.
		//
		// The gate is data driven the same way the command path is. The action phase
		// machine calls BtlOdMaybeStartMinigame, that indexes a 33 byte kind table off the
		// command's kind byte, and only Tidus, Auron, Lulu and Wakka have an entry.
		// Kimahri, Rikku and the parent commands are plain menus.
		//
		// So one overdrive at a time, and replicate the RESULT through
		// BtlOdReportMinigameResult rather than running two minigames. Every piece of live
		// state in the per-character block below is one slot for the whole game.
		// ---------------------------------------------------------------------------
		const DWORD BtlOdMaybeStartMinigame = 0x003AFC90;
		const DWORD BtlOdGateJumpTable = 0x003AFD00;         // 2 entries
		const DWORD BtlOdGateKindIndexTable = 0x003AFD08;    // 33 bytes, kind -> minigame
		const DWORD BtlOdStartMinigameForActor = 0x003AFD30; // magic host API 630
		const DWORD BtlOdIsMinigamePending = 0x003AFDE0;     // host 629
		const DWORD BtlOdReportMinigameResult = 0x003B0470;
		const DWORD BtlActionPhaseStep = 0x00388480;
		const DWORD BtlOdGateCallSite = 0x00388691; // the only call to the gate

		// Offsets into the battle actor record, which is where the handshake and the result
		// live. Already per actor, so nothing to fix here.
		const int BtlActorOdPendingOff = 0x0D24;
		const int BtlActorOdResultOff = 0x0D28;
		const int BtlActorOdTimeLeftOff = 0x0D2C;    // float
		const int BtlActorOdBudgetOff = 0x0D30;      // float
		const int BtlActorWakkaReelLiveOff = 0x0D40; // 31 bytes
		const int BtlActorWakkaReelSrcOff = 0x0D80;
		const int BtlActorAbilityIdOff = 0x0F5C;
		const int BtlActorMagicStatOff = 0x05AA;

		// The 25 vs 30 divisor in BtlOdStepSharedTimer and its whole input chain. The 25
		// path is dead because MesWinFontMode is written once at init from a byte that is
		// still zero, but it is two bytes, so hash them and be sure.
		const DWORD BtlOdFontModeBranchSite = 0x00491AF6;  // the jnz that picks 25 or 30
		const DWORD BtlOdTimerRandBranchSite = 0x00491B2D; // the per-sub-step Rand draw
		const DWORD MesWinGetFontMode = 0x004AC3A0;
		const DWORD MesWinSelectFontForLanguage = 0x004AD900; // only writer, init only
		const DWORD MesWinTextLanguage = 0x00F30830;          // signed byte, the only input
		const DWORD MesWinTextLangVariant = 0x00F30833;       // byte

		const DWORD BtlOdHudX = 0x00F3F780;        // word
		const DWORD BtlOdHudY = 0x00F3F782;        // word
		const DWORD BtlOdTimeDisplay = 0x00F3F794; // float, cosmetic

		// ---------------------------------------------------------------------------
		// THE PER-CHARACTER MINIGAME STATE, which is NOT per character. Every global here
		// is one slot, so this is the set a second simultaneous overdrive would corrupt,
		// and the set to hash for a desync check.
		// ---------------------------------------------------------------------------
		const DWORD BtlOdAuronSuccess = 0x00F3D6F2; // byte
		const DWORD BtlOdAuronState = 0x00F3D6F4;   // word, high word is a HUD anchor
		const DWORD BtlOdAuronSeqIndex = 0x00F3D6FC;
		const DWORD BtlOdAuronSeqLen = 0x00F3D700;
		const DWORD BtlOdAuronSeqPtr = 0x00F3D704;

		const DWORD BtlOdTidusSuccess = 0x00F3D6F3;       // byte
		const DWORD BtlOdTidusState = 0x00F3D734;         // word
		const DWORD BtlOdTidusTrackLeft = 0x00F3D736;     // word
		const DWORD BtlOdTidusTrackWidth = 0x00F3D73A;    // word
		const DWORD BtlOdTidusZoneLow = 0x00F3D73C;       // word
		const DWORD BtlOdTidusZoneHigh = 0x00F3D73E;      // word
		const DWORD BtlOdTidusZoneAndBarPos = 0x00F3D740; // zone width low, bar pos high
		const DWORD BtlOdTidusBarVel = 0x00F3D744;        // signed word

		const DWORD BtlOdLuluState = 0x00F3D708;             // word
		const DWORD BtlOdLuluGaugeMax = 0x00F3D70E;          // word, 192
		const DWORD BtlOdLuluGauge = 0x00F3D71C;             // word
		const DWORD BtlOdLuluRotCount = 0x00F3D71E;          // byte
		const DWORD BtlOdLuluHitCount = 0x00F3D71F;          // byte, capped at 16
		const DWORD BtlOdLuluQuadrantRing = 0x00F3D721;      // 16 bytes
		const DWORD BtlOdLuluRingCount = 0x00F3D731;         // byte
		const DWORD BtlOdLuluHitThresholdTable = 0x00F3F798; // 16 bytes
		const DWORD BtlOdWakkaReelsActive = 0x00F3C93F;      // byte

		// ---------------------------------------------------------------------------
		// The three native minigames: one step function plus launch, reset and draw each.
		// Step is reached from BtlMenuStep, so all three run inside the simulation.
		// ---------------------------------------------------------------------------
		const DWORD BtlOdTidusSwingBarMinigame = 0x00492320;
		const DWORD BtlOdTidusLaunch = 0x00498CA0;
		const DWORD BtlOdTidusReset = 0x00498180;
		const DWORD BtlOdDrawTidusBar = 0x00497240;

		const DWORD BtlOdAuronButtonSeqMinigame = 0x00490F70;
		const DWORD BtlOdAuronLaunch = 0x00498AD0;
		const DWORD BtlOdAuronReset = 0x00497970;
		const DWORD BtlOdDrawAuronSeq = 0x00492740;

		const DWORD BtlOdLuluLaunch = 0x00498BF0;
		const DWORD BtlOdLuluReset = 0x00497F70;
		const DWORD BtlOdDrawLuluGauge = 0x00495660;

		// Wakka's reels live in a magic DLL. These are the host API entries it drives, so
		// the engine only stores the strip and the DLL decides what lands on it.
		const DWORD BtlOdWakkaReelsBegin = 0x00498DC0;        // host 656
		const DWORD BtlOdWakkaReelsEnd = 0x00498230;          // host 655
		const DWORD BtlOdWakkaReelsStopCue = 0x00490F10;      // host 649
		const DWORD BtlOdWakkaReelsDraw = 0x004974A0;         // host 650
		const DWORD BtlOdWakkaReelsIsCuePlaying = 0x004977C0; // host 652
		const DWORD BtlOdWakkaReelsGetStrip = 0x003B1910;     // host 645
		const DWORD BtlOdWakkaReelsGetLevel = 0x003B19F0;     // host 646
		const DWORD BtlOdWakkaReelsPostDone = 0x003B1A70;     // host 647

		// Phase and HUD, all host API entries. BtlOdMinigamePhase is what they move.
		const DWORD BtlOdSetPhaseIdle = 0x00490E70;         // host 648
		const DWORD BtlOdSetPhaseArmed = 0x0049AB20;        // host 659
		const DWORD BtlOdSetPhaseRunning = 0x0049AB50;      // host 660
		const DWORD BtlOdSetPhaseArmedDup = 0x0049ABA0;     // host 662, same effect as 659
		const DWORD BtlOdIsGoodCuePlaying = 0x004977E0;     // host 653
		const DWORD BtlOdPlayGoodCue = 0x0049AB80;          // host 661
		const DWORD BtlOdSetHudPos = 0x0049A2D0;            // host 658
		const DWORD BtlOdDrawTimerHud = 0x004955E0;
		const DWORD BtlOdPressCircleGate = 0x00491A30;
		const DWORD BtlOdArmPressCircleGate = 0x00490EC0;   // host 372
		const DWORD BtlOdPressCircleGateState = 0x00F3F6A8; // word

		// The tunables, so a desync hunt can rule them out. All read only, all indexed by
		// overdrive level 0..3.
		const DWORD BtlOdAuronTimeBudgets = 0x00886B60;      // 4 floats, 4 4 4 3
		const DWORD BtlOdAuronSequences = 0x00886B70;        // 4 rows x 32 bytes, 0xFFFF ends
		const DWORD BtlOdTidusTimeBudgets = 0x00886BF0;      // 4 floats, 3 3 3 2
		const DWORD BtlOdGetAuronSequence = 0x0065CA60;
		const DWORD BtlOdGetAuronBudget = 0x0065CA70;
		const DWORD BtlOdGetLuluBudget = 0x0065CB10;         // always 4.0
		const DWORD BtlOdGetTidusZoneWidth = 0x0065CB30;     // 24 20 20 18
		const DWORD BtlOdGetTidusBarSpeed = 0x0065CB80;      // 11 12 13 14
		const DWORD BtlOdGetTidusBudget = 0x0065CBD0;
		const DWORD BtlOdLoadLuluRotationTable = 0x0065CC70; // 19 cases
		const DWORD BtlOdLuluRotFamilyIndex = 0x0065CEC8;    // 19 bytes
		const DWORD BtlOdGetLuluMagicStat = 0x0039AE90;      // returns actor+0x5AA
		const DWORD BtlOdParseThresholdString = 0x0049A2F0;  // fills the threshold table

		// ---------------------------------------------------------------------------
		// The rest of the battle menu page machine, because Mix, Grand Summon and Ronso
		// Rage are pages rather than minigames.
		//
		// The nine Step4 functions are the state 4 callbacks, one per page kind group.
		// Eight of the nine hold the wall-clock input lockout described up at
		// BtlMenuPageStartClockCallSite, and the two constants are the thresholds they
		// compare against. Patching a constant is an alternative to patching the two clock
		// call sites, and 0x00743D50 IS A DOUBLE rather than a float.
		// ---------------------------------------------------------------------------
		const DWORD BtlMenuPageLoopHeadSite = 0x0049AECD;
		const DWORD BtlMenuInstallPageCallbacks = 0x004A0DE0;
		const DWORD BtlMenuInstallPageLayout = 0x004A10D0;
		const DWORD BtlMenuSetPageKindForCommand = 0x00499870; // Mix and Doublecast cases
		const DWORD BtlMenuDrawRoot = 0x0049B360;

		const DWORD BtlMenuStep4Kind0 = 0x004A8A70;     // 0.45 s lockout
		const DWORD BtlMenuStep4KindCommon = 0x004A8B20; // kinds 1 2 3 4 6 0xE 0x11
		const DWORD BtlMenuStep4Kind5 = 0x004A8B50;
		const DWORD BtlMenuStep4Kind7 = 0x004A8B90;     // kinds 7 and 0xF
		const DWORD BtlMenuStep4KindA = 0x004A87A0;
		const DWORD BtlMenuStep4KindC = 0x004A8A40;
		const DWORD BtlMenuStep4KindD = 0x004A8AF0;
		const DWORD BtlMenuStep4Kind14 = 0x004A8AC0;    // Mix
		const DWORD BtlMenuStep4Kind15 = 0x004A87D0;    // kinds 0x15 and 0x16

		const DWORD BtlMenuPageLockoutConst = 0x0073EB44;      // float 0.30000001
		const DWORD BtlMenuPageLockoutKind0Const = 0x00743D50; // double 0.44999999

		// Page record offsets. BtlMenuPages is the array, stride 240, 8 slots.
		const int BtlMenuPageStride = 240;
		const int BtlMenuPageStateOff = 0x01;
		const int BtlMenuPageKindOff = 0x06;      // from ability row byte 23
		const int BtlMenuPageDrawCbOff = 0x90;
		const int BtlMenuPageEnterCbOff = 0x94;   // null for every kind but 0xA
		const int BtlMenuPagePollCbOff = 0x98;    // state 3
		const int BtlMenuPageStepCbOff = 0x9C;    // state 4
		const int BtlMenuPageExitCbOff = 0xA0;    // state 6
		const int BtlMenuPageStartTimeOff = 0xDC; // WALL CLOCK seconds
		const int BtlMenuPageElapsedOff = 0xE0;   // WALL CLOCK seconds
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
		// THE DAMAGE AND COST PATHS. Derivation in ..\..\..\..\reversing\CHEAT_BATTLE.md.
		//
		// The path is two phased, which is why there are two obvious hook points rather
		// than one. The numbers are computed and PARKED in the target's own record, and a
		// later step applies them:
		//
		//   BtlApplyActionToTargets (GameState.h)   picks the targets
		//     BtlStageEffectsForTarget              snapshots HP/MP/CTB, loops the entries
		//       BtlComputeEffectForTarget           THE FORMULA AND THE CAP
		//   ... animations play ...
		//   BtlEffectCompleteStep                   the completion path
		//     BtlApplyPendingEffects                walks the parked entries
		//       BtlApplyHpDamage / MpDamage / CtbDelay
		//     BtlOnUnitDefeated (GameState.h)       HP <= 0 or the death bit becomes a KO
		//
		// BtlComputeEffectForTarget is the single best hook for both "take zero damage"
		// and "deal the maximum", because it is the one place the three damage dwords
		// exist as plain integers with the attacker and the target both in the argument
		// list. Detour it and fix up entry+0x20/+0x24/+0x28 after the original returns,
		// then RETURN entry+0x20 rather than the original's value, because
		// BtlStageEffectsForTarget sums the return into the predicted-death byte.
		// ---------------------------------------------------------------------------

		// int __cdecl (int atkIdx, int atkActor, int tgtIdx, int tgtActor,
		//              int abilityRow, int a6, u8 *entry44, int, int, int, int *out)
		//
		// Returns the final entry+0x20, the HP delta. Prologue is push ebp / mov ebp,esp /
		// sub esp,0xB8 with no rel32 in the first five bytes, so a plain detour is clean.
		const DWORD BtlComputeEffectForTarget = 0x0038E630;

		// int __cdecl (int atkIdx, int atkActor, int tgtIdx, int tgtActor,
		//              int abilityRow, int a6, u8 *block, int, int, int, int)
		const DWORD BtlStageEffectsForTarget = 0x00389740;

		const DWORD BtlApplyPendingEffects = 0x0038F060;
		const DWORD BtlEffectCompleteStep = 0x0038D980;

		// int __cdecl (int tgtIdx, int tgtActor, int amount, int other, int a5, int a6,
		//              int a7). Positive amount is damage, negative is healing.
		const DWORD BtlApplyHpDamage = 0x0038E230;
		const DWORD BtlApplyMpDamage = 0x0038E3B0;
		const DWORD BtlApplyCtbDelay = 0x0038E1E0; // NOT gated by BtlIsUnitDamageable

		// BOOL __cdecl (int unitIdx). The engine's OWN invincibility gate, and one line:
		//   !(IsEnemySlot(i) && MonInvincible) && !(!IsEnemySlot(i) && PlyInvincible)
		// Three callers: StageEffectsForTarget, ApplyHpDamage, ApplyMpDamage. So setting
		// BtlDbgPlyInvincible covers every HP and MP write with no detour. It does NOT
		// gate statuses or CTB delay.
		const DWORD BtlIsUnitDamageable = 0x0038D3A0;

		const DWORD BtlDamageFormula = 0x00389BF0;
		const DWORD BtlShowFloatingNumber = 0x0039FA00; // kind 0 HP 1 MP 2 CTB 4 miss 5 overkill
		const DWORD ClampInt = 0x0039A0C0;              // int (int v, int lo, int hi)

		// THE DAMAGE CAP. 9999, or 99999 when the ATTACKER's auto-ability word at
		// BtlActorAutoAbil2Off has bit 0x800 (Break Damage Limit). The ability row's flag
		// word at +0x20 overrides: bit 0x80 forces 99999, bit 0x40 forces 9999. The clamp
		// is symmetric, so healing is capped the same way, and it covers all three slots.
		//
		// A plugin should write above the cap and let it clamp, which is exactly what
		// BtlDbgDmgIs100000 does. Do not disable it: the number renderer, the overkill
		// test and the dealt-99999 tracker all assume five digits.
		const DWORD BtlDmgCapSelectSite = 0x0038ECCA;    // mov eax, 800h
		const DWORD BtlDmgCapBdlConstSite = 0x0038ECE3;  // imm32 90000
		const DWORD BtlDmgCapBaseConstSite = 0x0038ECE9; // imm32 9999
		const DWORD BtlDmgClampLoopSite = 0x0038ED78;    // the three-slot clamp loop head
		const int BtlDamageCap = 9999;
		const int BtlDamageCapBreak = 99999;
		const int BtlBreakDamageLimitBit = 0x0800; // in BtlActorAutoAbil2Off

		// The MP cost side. BtlGetCommandMpCost is declared further up. It already does
		//   if (IsEnemySlot(actor) || BtlDbgMagFree) { cost = 0; extra = 0; }
		// so enemies pay nothing already and BtlDbgMagFree only changes the player side.
		// BtlSpendCommandCost is the ONLY cost-side MP subtraction in the binary, checked
		// by scanning every instruction with displacement 0x5D4, and it spends the values
		// BtlCheckCommandCost cached from BtlGetCommandMpCost. So the flag is sufficient
		// and neither PatchCallSite nor a detour is needed.
		const DWORD BtlGetRawMpCost = 0x0038CF70;      // int (u8 actorIdx, int abilityRow)
		const DWORD BtlSpendCommandCost = 0x0038E5A0;  // int (int actorIdx)
		const DWORD BtlGetUsableCommandKinds = 0x0038F700; // bit 1 normal, bit 2 overdrive

		// ---------------------------------------------------------------------------
		// THE BATTLE UNIT RECORD, the fields the damage and overdrive paths touch.
		// All relative to Rva::BattleGetActor(index) from GameState.h. Each one has two
		// independent proofs, the load from the character record and the use on the
		// damage path. See CHEAT_BATTLE.md section 1.2 for the per-field evidence.
		// ---------------------------------------------------------------------------
		const int BtlActorMaxHpOff = 0x0594;      // dword
		const int BtlActorMaxMpOff = 0x0598;      // dword
		const int BtlActorOverkillMaxHpOff = 0x05A4; // dword, the entry bit 0x80 test
		const int BtlActorOdModeOff = 0x05BB;     // byte, 0..19
		const int BtlActorOdGaugeOff = 0x05BC;    // byte
		const int BtlActorOdGaugeMaxOff = 0x05BD; // byte, the "full" threshold
		const int BtlActorCurHpOff = 0x05D0;      // dword
		const int BtlActorCurMpOff = 0x05D4;      // dword
		const int BtlActorStatus1Off = 0x0606;    // word, bit 1 = KO, bit 4 = removed
		const int BtlActorStatus2Off = 0x0616;    // word, bit 0x100 = non-HP death cause
		const int BtlActorDerivedStatusOff = 0x0640; // byte, bit 8 = Zombie
		const int BtlActorCtbCounterOff = 0x065C;    // byte
		const int BtlActorAutoAbil1Off = 0x06BC;  // word, 0x4000 Half MP, 0x8000 One MP
		const int BtlActorAutoAbil2Off = 0x06BE;  // word, 0x800 Break Damage Limit
		const int BtlActorAutoAbil3Off = 0x06C0;  // word
		const int BtlActorCachedMpCostOff = 0x06CC; // byte, spent by SpendCommandCost
		const int BtlActorCachedOdCostOff = 0x06CD; // byte
		const int BtlActorWorkHpOff = 0x06E4;     // dword, seeded from CurHp
		const int BtlActorWorkMpOff = 0x06E8;     // dword
		const int BtlActorWorkCtbOff = 0x06EC;    // dword
		const int BtlActorOdModeCountedOff = 0x06F0; // dword, per-battle mask
		const int BtlActorKoFlagOff = 0x0DCC;     // byte, non-zero means already dead
		const int BtlActorWillDieOff = 0x0DEC;    // byte, predicted
		const int BtlActorOverkillAccumOff = 0x0F60; // dword

		// The pending effect blocks, which is where a damage number lives between being
		// computed and being applied. Two blocks, 728 bytes each, 24 byte header then 16
		// entries of 44, and (728 - 24) / 44 is 16 exactly.
		const int BtlActorPendingEffectsOff = 0x0774;
		const int BtlActorPendingEffects2Off = 0x0A4C;
		const int BtlEffectBlockBytes = 728;
		const int BtlEffectBlockHeaderBytes = 24;
		const int BtlEffectEntryBytes = 44;
		const int BtlEffectEntryCount = 16;

		// Entry fields, relative to the entry.
		const int BtlEffectEntryCountersOff = 0x07; // 13 status turn counters
		const int BtlEffectEntryStatus1Off = 0x14;  // word, copied over actor+0x606
		const int BtlEffectEntryStatus2Off = 0x16;  // word, copied over actor+0x616
		const int BtlEffectEntryMaskOff = 0x18;     // word, 1 HP 2 MP 4 CTB 0x80 overkill
		const int BtlEffectEntryHpOff = 0x20;       // dword
		const int BtlEffectEntryMpOff = 0x24;       // dword
		const int BtlEffectEntryCtbOff = 0x28;      // dword

		// ---------------------------------------------------------------------------
		// THE OVERDRIVE GAUGE AND MODE, both halves.
		//
		// Three fields, all in the 148 byte character record at Rva::CharRecords from
		// addresses/GameState.h, and all three were in that file's "no reader found" gap:
		//
		//   record+0x38  byte      the SELECTED mode id, 0..19
		//   record+0x60  20 words  the per-mode use counter. 0xFFFF = not available to
		//                          this character, 0 = earned, else uses remaining
		//   record+0x88  dword     the UNLOCKED mode bitmask, bit n = mode id n
		//
		// BtlLoadUnitParams copies record+0x38/0x39/0x3A into actor+0x5BB/0x5BC/0x5BD at
		// battle start and BtlCommitActorsToSave copies them back at the end. So write the
		// RECORD out of battle and the ACTOR in battle. Writing the record mid-battle is
		// silently undone by the writeback.
		//
		// The gauge is 0..actor+0x5BD and "full" means gauge == max, not a fixed number.
		// The max is seeded from the ply_save kernel blob, so READ IT, do not assume. The
		// shipped idiom is BoosterInvincibleRefillUnit's actor+0x5BC = actor+0x5BD.
		// ---------------------------------------------------------------------------
		const DWORD BtlAddOverdrive = 0x003B1590; // int (charIdx, actor, amount)
		const DWORD BtlGetOverdriveGauge = 0x00395550; // int (u8 unitIdx), 0 for an enemy
		const DWORD BtlGetOverdriveMax = 0x00395590;   // int (u8 unitIdx)
		const DWORD BtlGetOverdriveMode = 0x003955F0;  // int (u8 unitIdx)

		// int __cdecl (unsigned charIdx, unsigned modeId, int force). Decrements the word
		// at record+0x60+2*modeId. Caps modeId at 0x10, while the grant loop walks to 19.
		const DWORD BtlBumpOverdriveModeCounter = 0x003B10C0;

		// void __cdecl (void), from BtlMainStep. For each of the 7 party-order slots walks
		// modeId 0..19 and sets the record+0x88 bit for any mode whose counter hit 0. It
		// only fires for a mode whose bit is still CLEAR, so setting all 20 bits at once
		// unlocks everything AND suppresses the popups rather than triggering 140 of them.
		const DWORD BtlGrantUnlockedOverdriveModes = 0x003B1180;

		const DWORD BtlLoadUnitParams = 0x0039B4F0;         // record -> actor, per unit
		const DWORD BtlLoadCharRecordIntoActor = 0x0039C5F0; // builds actor+0x6BC/6BE/6C0
		const DWORD BtlCommitActorsToSave = 0x00385FC0;      // actor -> record, battle end

		const int CharRecordBytes = 148;
		const int CharRecCurHpOff = 0x1C;
		const int CharRecCurMpOff = 0x20;
		const int CharRecMaxHpOff = 0x24;
		const int CharRecMaxMpOff = 0x28;
		const int CharRecAbilityFlagsOff = 0x3E;      // 6 words, ability ids 0x3000..0x305F
		const int CharRecOdModeOff = 0x38;            // byte
		const int CharRecOdGaugeOff = 0x39;           // byte
		const int CharRecOdGaugeMaxOff = 0x3A;        // byte
		const int CharRecOdModeCountersOff = 0x60;    // 20 words
		const int CharRecOdModeUnlockMaskOff = 0x88;  // dword
		const DWORD CharRecOdModeUnlockMaskAll = 0x000FFFFF; // bits 0..19

		// ---------------------------------------------------------------------------
		// THE OVERDRIVE MODE LIST, for a data driven picker.
		//
		// BtlOdModeDisplayOrder is the ids in SCREEN order, and its only cross reference
		// in the binary is the eleven line loop in MenuBuildListRows case 1:
		//
		//   mask = MenuGetCharOverdriveModeMask(cursorChar);     // record+0x88
		//   for (j = 0; j < 20; ++j) {
		//       id = BtlOdModeDisplayOrder[j];
		//       if ((1 << id) & mask) addRow(id, kind 3);
		//   }
		//
		// The display NAME is KernelStringGet(1, 4147 + modeId, lang) and the description
		// is KernelStringGetDesc(1, 4147 + modeId, lang), both declared in
		// addresses/MenuSystem.h. lang comes from SaveDataGetStringLang. The bytes are in
		// the FFX glyph encoding, not ASCII. An out of range id is SAFE, KernelTableGetRow
		// falls back to the first range descriptor rather than computing a wild pointer.
		//
		// The names are not in the executable, they come out of the kernel string blob, so
		// the picker has to be populated by calling the getter at runtime.
		//
		// The count 20 is confirmed four separate times: this loop, the grant loop, the
		// debug helper's loop, and the 20 word counter array ending exactly where the
		// unlock mask begins.
		// ---------------------------------------------------------------------------
		const DWORD BtlOdModeDisplayOrder = 0x0088765C; // u8[20], 2 0 1 3 4 .. 19
		const int BtlOdModeCount = 20;
		const int BtlOdModeStringGroup = 1;
		const int BtlOdModeStringBaseId = 4147; // 0x1033, add the mode id

		const DWORD SaveDataGetStringLang = 0x003851F0; // int (void), the lang bit

		// Menu-area helpers the cheat UI calls. Named for what they do rather than for the
		// module, and kept here because the cheat path needs them. If addresses/MenuSystem.h
		// ever adopts any of these, delete the copy here rather than keeping both.
		const DWORD MenuGetCharOverdriveMode = 0x004C1BD0;     // int (u8), record+0x38
		const DWORD MenuGetCharOverdriveModeMask = 0x004C1BF0; // int (u8), record+0x88
		const DWORD MenuBuildListRows = 0x004C2390;            // case 1 is the mode list
		const DWORD MenuGetAbilityName = 0x004C1A20; // const char * (short abilityId)
		const DWORD MenuGetAbilityHelp = 0x004C19E0; // const char * (short abilityId)
		const DWORD MenuCountTableEntries = 0x004D2D80; // (base, 5, charIdx, group)
		const DWORD MenuFindTableEntry = 0x004D2DC0;    // same args, first index

		// MenuSetCharRecordByte38 0x004C2C90 writes record+0x38 and is declared in
		// addresses/MenuSystem.h. MenuExecModule21Overdrive 0x004D0460 is there too.

		// ---------------------------------------------------------------------------
		// PER-CHARACTER OVERDRIVE ABILITIES.
		//
		// BtlSetAbilityFlag is the ONE call to make. It refuses anything whose high
		// nibbles are not 0x3000, then splits: ids below 0x3060 go in the per-character
		// bitmap at record+0x3E, ids 0x3060 and above go in the SHARED bitmap at
		// SaveAbilityFlagsShared with charIdx ignored. Every overdrive ability id is
		// 0x3060 or above, so learned overdrives are global to the save rather than per
		// character, which is consistent because each id belongs to one character anyway.
		//
		// It then calls BattleIsActive and re-syncs the live actor's usable-ability state,
		// which is why you call it instead of setting the bit yourself.
		//
		// This is NOT Rva::SaveDataSetCharAbility 0x00385E00 from addresses/GameState.h.
		// That one writes the wider bitmap at saveData+0x6034. The overdrive path uses
		// this one, see GAME_STATE.md on which of the two means what.
		//
		// The id ranges per character are in reversing/OVERDRIVES.md section 11, derived
		// from the shipped command.bin kind bytes: Tidus 0x3060..0x3063, Auron
		// 0x3064..0x3067, Kimahri 0x3068..0x3073, Wakka 0x3074..0x3077, Lulu
		// 0x3078..0x308A, Rikku 0x308B..0x30CA, parent commands 0x3118..0x311E.
		// ---------------------------------------------------------------------------
		const DWORD BtlSetAbilityFlag = 0x00385C50;  // int (charIdx, abilityId, on)
		const DWORD BtlTestAbilityFlag = 0x00385020; // BOOL (charIdx, abilityId)
		const DWORD BtlTestInnateAbilityFlag = 0x003850B0; // BOOL (abilityId)
		const DWORD SaveAbilityFlagsShared = 0x00D307FC;   // u16[], ids 0x3060 and up
		const int BtlSharedAbilityIdBase = 0x3060;

		// The per-character menu ability table, for enumerating without hardcoding ranges.
		// BtlGetMenuAbilityTableRow(0, &stringBase) gives the base. Entries are 4 bytes,
		// { u8 charIdx, u8 group, u16 abilityId }, terminated when charIdx == 0xFF. Group
		// 72 selects a character's overdrive PARENT command, which is how the Overdrive
		// screen's header gets Swordplay, Bushido, Slots, Fury, Ronso Rage and Mix.
		const DWORD BtlGetMenuAbilityTableRow = 0x00390200;
		const DWORD BtlMenuAbilityTable = 0x00D2A958;   // short *, the table pointer
		const DWORD BtlAbilityEffectTable = 0x00D2A944; // short *, equipment auto-abilities
		const DWORD BtlPlayerAbilityTable = 0x00D2A92C; // short *, BtlGetPlayerAbilityRow
		const int BtlMenuAbilityEntryBytes = 4;
		const int BtlMenuAbilityGroupOverdriveParent = 72;

		// ---------------------------------------------------------------------------
		// THE SHIPPED BATTLE DEBUG FLAG BLOCK, 0x00D2A8F8..0x00D2A927.
		//
		// All zero at startup, all plain .data, and NONE of them gated on
		// g_ffxDebugMode. The only writers in the retail binary are the debug window
		// 0x003C6E20, the two ATEL set/get syscalls 0x003A81F0 and 0x003A27E0, and a bulk
		// reset at 0x003CE450. So a plugin can just write them, and three of them ARE the
		// cheat feature set:
		//
		//   BtlDbgPlyInvincible   party takes no HP or MP damage
		//   BtlDbgMagFree         casting costs nothing, player side only
		//   BtlDbgLimitBreakOn    overdrive usable at any gauge
		//   BtlDbgMonHp1          every enemy enters battle at 1 HP, the instant kill
		//
		// PlyInvincible, MonInvincible, CtbPause, AutoExecute, MonInput, PrintInfo and
		// SkipCommand are already declared further up this file.
		//
		// Two gaps worth stating. PlyInvincible does NOT block statuses, so a Death cast
		// still removes an "invincible" party member, and the floating damage number still
		// appears because BtlShowFloatingNumber runs before the gate. BtlDbgDmgStatusOff
		// blocks statuses but on BOTH sides, so it cannot be part of a party-only god
		// mode. Closing that gap needs the BtlComputeEffectForTarget detour.
		// ---------------------------------------------------------------------------
		const DWORD BtlDbgCtbSameOrder = 0x00D2A8FB;
		const DWORD BtlDbgMagNotEff = 0x00D2A900;
		const DWORD BtlDbgMagFree = 0x00D2A901; // FREE MP
		const DWORD BtlDbgMagNum0 = 0x00D2A902;
		const DWORD BtlDbgSumNotEff = 0x00D2A904;
		const DWORD BtlDbgFullSet = 0x00D2A905;
		const DWORD BtlDbgDmgStatusOff = 0x00D2A906; // both sides, see above
		const DWORD BtlDbgExchgWillDie = 0x00D2A907;
		const DWORD BtlDbgDmgRandomOff = 0x00D2A908;
		const DWORD BtlDbgDmgCritOff = 0x00D2A909;
		const DWORD BtlDbgDmgProbOff = 0x00D2A90A;
		const DWORD BtlDbgLimitBreakOn = 0x00D2A90C;  // overdrive at any gauge
		const DWORD BtlDbgDmgCritOn = 0x00D2A90D;
		const DWORD BtlDbgDmgIs1 = 0x00D2A90E;
		const DWORD BtlDbgDmgIs10000 = 0x00D2A90F;
		const DWORD BtlDbgDmgIs100000 = 0x00D2A910; // MAX DAMAGE, not side selective
		const DWORD BtlDbgOverKillOff = 0x00D2A914;
		const DWORD BtlDbgLimitBreakOff = 0x00D2A916; // stops the gauge filling at all
		const DWORD BtlDbgPlyHp1 = 0x00D2A91D;        // party loads at 1 HP
		const DWORD BtlDbgMonHp1 = 0x00D2A91E;        // INSTANT KILL
		const DWORD BtlDbgDmgHitMiss = 0x00D2A91F;
		const DWORD BtlDbgWeapon = 0x00D2A920;
		const DWORD BtlDbgMagicItem = 0x00D2A921;

		// Three more that live outside that block.
		const DWORD BtlDbgKillAllHp = 0x00D3338C;            // dword, -99999 per HP event
		const DWORD BtlDbgSetKillAllHp = 0x0038E620;         // its only writer
		const DWORD BtlDbgOverdriveAlwaysFull = 0x00D333E8;  // dword, fills the gauge
		const DWORD BtlDbgSetOverdriveAlwaysFull = 0x00390420; // its only writer
		const DWORD BtlDealt9999Flag = 0x00D33390;   // dword, latched at the cap
		const DWORD BtlDealt99999Flag = 0x00D33394;

		// Shipped debug helpers worth binding to buttons. All check BattleIsActive
		// themselves, so they are safe to call from a UI callback.
		// DebugMaxHpMpAndStats 0x00384B00 is declared in addresses/Debug.h.
		const DWORD BtlDebugAllUnitsHp1 = 0x00384B80;    // void (void), all 31 units
		const DWORD BtlDebugSetAllMonsterHp = 0x00384BC0; // void (int toOne), units 20..27
		const DWORD BtlDebugSetAllPartyHp = 0x00384C80;   // void (int toOne), units 0..17
		const DWORD BtlDebugOdModesOneUseAway = 0x00384C40; // void (void)

		// The Invincible booster's per-turn top-up, which is the closest shipped thing to
		// god mode and the reference for a legitimate full-gauge write. It is a between
		// turns restore, so a unit can still be killed inside one turn.
		//
		// Its toggle is Rva::BoosterInvincible 0x00EFB7CC in addresses/EscMenu.h, and that
		// file's DamagePathReader 0x00392A90 is the SAME function as BtlSendMenu above.
		// That is not a clash, it is a cross-check: the booster fires from the turn-claim
		// path right before a party member's menu opens.
		const DWORD BtlBoosterInvincibleRefillUnit = 0x003847C0; // void (int unitIdx)

		// ---------------------------------------------------------------------------
		// The in-battle party, which is a different thing from the field party. The
		// field array in addresses/GameState.h is the save block, this is battle work
		// RAM, and at battle end BtlCommitActorsToSave copies this back over the field
		// party. So a field party edit made during a battle is silently thrown away.
		// ---------------------------------------------------------------------------

		// BYTE[17], the battle BENCH, an exact complement of BattlePartyOrder[0..2].
		// The IDB called this g_ffxBattleAeonOrder, which is wrong, it holds whoever is
		// not in an active slot.
		const DWORD BattleBenchOrder = 0x00D2C8A3;

		// int (unsigned char outIdx, unsigned char inIdx, int mode, char). THE way to
		// change the party mid battle. Mode 0 is a plain Switch, above 0 is the Summon
		// path. Writing BattlePartyOrder by hand is not enough: this also fixes
		// actor+16, actor+3528 and actor+1278, pulls and re-inserts the CTB turn queue
		// entries, and keeps the bench complementary.
		const DWORD BtlSwapUnitIntoPartySlot = 0x003ADAE0;

		// void (void). Pushes and pops the whole arrangement, for the Summon return.
		const DWORD BtlSwapPartyArrangement = 0x003ADE10;

		// int (BattleActor *). The only reader of CharIndexToChrId, and the place
		// Rikku's alternate outfit turns c007 into c041.
		const DWORD BtlResolveUnitChrId = 0x0039A440;

		// BtlCommitActorsToSave is declared above. It runs at battle end and calls
		// BtlCommitPartyToField, which is what overwrites a mid battle field party edit.

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
				BtlMenuPageStartClockCallSite,
				BtlMenuPageElapsedClockCallSite,
				BtlOdStepSharedTimer,
				BtlOdResetSharedTimer,
				BtlOdGetTimeRemaining,
				BtlOdSetTimeBudget,
				BtlOdStepCounter,
				BtlOdTimeBudget,
				BtlOdTimeRemaining,
				BtlOdMinigamePhase,
				MesWinFontMode,
				BtlOdLuluClockNow,
				BtlOdLuluClockMark,
				BtlOdLuluClockCallSiteNow,
				BtlOdLuluClockCallSiteMark,
				BtlOdLuluDeadZoneClockSite,
				BtlOdLuluDeadZoneWindowConst,
				BtlOdLuluKeyboardReader,
				BtlOdLuluKeyA,
				BtlOdLuluKeyB,
				BtlOdLuluKeyboardHitSite,
				BtlOdLuluFuryStickMinigame,
				BtlOdLuluSetThreadedPadSite,
				BtlOdLuluClearThreadedPadSite,
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
				BtlOdMaybeStartMinigame,
				BtlOdGateJumpTable,
				BtlOdGateKindIndexTable,
				BtlOdStartMinigameForActor,
				BtlOdIsMinigamePending,
				BtlOdReportMinigameResult,
				BtlActionPhaseStep,
				BtlOdGateCallSite,
				BtlOdFontModeBranchSite,
				BtlOdTimerRandBranchSite,
				MesWinGetFontMode,
				MesWinSelectFontForLanguage,
				MesWinTextLanguage,
				MesWinTextLangVariant,
				BtlOdHudX,
				BtlOdHudY,
				BtlOdTimeDisplay,
				BtlOdAuronSuccess,
				BtlOdAuronState,
				BtlOdAuronSeqIndex,
				BtlOdAuronSeqLen,
				BtlOdAuronSeqPtr,
				BtlOdTidusSuccess,
				BtlOdTidusState,
				BtlOdTidusTrackLeft,
				BtlOdTidusTrackWidth,
				BtlOdTidusZoneLow,
				BtlOdTidusZoneHigh,
				BtlOdTidusZoneAndBarPos,
				BtlOdTidusBarVel,
				BtlOdLuluState,
				BtlOdLuluGaugeMax,
				BtlOdLuluGauge,
				BtlOdLuluRotCount,
				BtlOdLuluHitCount,
				BtlOdLuluQuadrantRing,
				BtlOdLuluRingCount,
				BtlOdLuluHitThresholdTable,
				BtlOdWakkaReelsActive,
				BtlOdTidusSwingBarMinigame,
				BtlOdTidusLaunch,
				BtlOdTidusReset,
				BtlOdDrawTidusBar,
				BtlOdAuronButtonSeqMinigame,
				BtlOdAuronLaunch,
				BtlOdAuronReset,
				BtlOdDrawAuronSeq,
				BtlOdLuluLaunch,
				BtlOdLuluReset,
				BtlOdDrawLuluGauge,
				BtlOdWakkaReelsBegin,
				BtlOdWakkaReelsEnd,
				BtlOdWakkaReelsStopCue,
				BtlOdWakkaReelsDraw,
				BtlOdWakkaReelsIsCuePlaying,
				BtlOdWakkaReelsGetStrip,
				BtlOdWakkaReelsGetLevel,
				BtlOdWakkaReelsPostDone,
				BtlOdSetPhaseIdle,
				BtlOdSetPhaseArmed,
				BtlOdSetPhaseRunning,
				BtlOdSetPhaseArmedDup,
				BtlOdIsGoodCuePlaying,
				BtlOdPlayGoodCue,
				BtlOdSetHudPos,
				BtlOdDrawTimerHud,
				BtlOdPressCircleGate,
				BtlOdArmPressCircleGate,
				BtlOdPressCircleGateState,
				BtlOdAuronTimeBudgets,
				BtlOdAuronSequences,
				BtlOdTidusTimeBudgets,
				BtlOdGetAuronSequence,
				BtlOdGetAuronBudget,
				BtlOdGetLuluBudget,
				BtlOdGetTidusZoneWidth,
				BtlOdGetTidusBarSpeed,
				BtlOdGetTidusBudget,
				BtlOdLoadLuluRotationTable,
				BtlOdLuluRotFamilyIndex,
				BtlOdGetLuluMagicStat,
				BtlOdParseThresholdString,
				BtlMenuPageLoopHeadSite,
				BtlMenuInstallPageCallbacks,
				BtlMenuInstallPageLayout,
				BtlMenuSetPageKindForCommand,
				BtlMenuDrawRoot,
				BtlMenuStep4Kind0,
				BtlMenuStep4KindCommon,
				BtlMenuStep4Kind5,
				BtlMenuStep4Kind7,
				BtlMenuStep4KindA,
				BtlMenuStep4KindC,
				BtlMenuStep4KindD,
				BtlMenuStep4Kind14,
				BtlMenuStep4Kind15,
				BtlMenuPageLockoutConst,
				BtlMenuPageLockoutKind0Const,
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
				BtlComputeEffectForTarget,
				BtlStageEffectsForTarget,
				BtlApplyPendingEffects,
				BtlEffectCompleteStep,
				BtlApplyHpDamage,
				BtlApplyMpDamage,
				BtlApplyCtbDelay,
				BtlIsUnitDamageable,
				BtlDamageFormula,
				BtlShowFloatingNumber,
				ClampInt,
				BtlDmgCapSelectSite,
				BtlDmgCapBdlConstSite,
				BtlDmgCapBaseConstSite,
				BtlDmgClampLoopSite,
				BtlGetRawMpCost,
				BtlSpendCommandCost,
				BtlGetUsableCommandKinds,
				BtlAddOverdrive,
				BtlGetOverdriveGauge,
				BtlGetOverdriveMax,
				BtlGetOverdriveMode,
				BtlBumpOverdriveModeCounter,
				BtlGrantUnlockedOverdriveModes,
				BtlLoadUnitParams,
				BtlLoadCharRecordIntoActor,
				BtlCommitActorsToSave,
				BtlOdModeDisplayOrder,
				SaveDataGetStringLang,
				MenuGetCharOverdriveMode,
				MenuGetCharOverdriveModeMask,
				MenuBuildListRows,
				MenuGetAbilityName,
				MenuGetAbilityHelp,
				MenuCountTableEntries,
				MenuFindTableEntry,
				BtlSetAbilityFlag,
				BtlTestAbilityFlag,
				BtlTestInnateAbilityFlag,
				SaveAbilityFlagsShared,
				BtlGetMenuAbilityTableRow,
				BtlMenuAbilityTable,
				BtlAbilityEffectTable,
				BtlPlayerAbilityTable,
				BtlDbgCtbSameOrder,
				BtlDbgMagNotEff,
				BtlDbgMagFree,
				BtlDbgMagNum0,
				BtlDbgSumNotEff,
				BtlDbgFullSet,
				BtlDbgDmgStatusOff,
				BtlDbgExchgWillDie,
				BtlDbgDmgRandomOff,
				BtlDbgDmgCritOff,
				BtlDbgDmgProbOff,
				BtlDbgLimitBreakOn,
				BtlDbgDmgCritOn,
				BtlDbgDmgIs1,
				BtlDbgDmgIs10000,
				BtlDbgDmgIs100000,
				BtlDbgOverKillOff,
				BtlDbgLimitBreakOff,
				BtlDbgPlyHp1,
				BtlDbgMonHp1,
				BtlDbgDmgHitMiss,
				BtlDbgWeapon,
				BtlDbgMagicItem,
				BtlDbgKillAllHp,
				BtlDbgSetKillAllHp,
				BtlDbgOverdriveAlwaysFull,
				BtlDbgSetOverdriveAlwaysFull,
				BtlDealt9999Flag,
				BtlDealt99999Flag,
				BtlDebugAllUnitsHp1,
				BtlDebugSetAllMonsterHp,
				BtlDebugSetAllPartyHp,
				BtlDebugOdModesOneUseAway,
				BtlBoosterInvincibleRefillUnit,
				BattleBenchOrder,
				BtlSwapUnitIntoPartySlot,
				BtlSwapPartyArrangement,
				BtlResolveUnitChrId,
			};
			if (count)
				*count = (int)(sizeof(list) / sizeof(list[0]));
			return list;
		}

	} // namespace Rva
} // namespace ffx
