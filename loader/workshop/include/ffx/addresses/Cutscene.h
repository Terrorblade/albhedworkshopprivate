#pragma once

#include <windows.h>

// Cutscenes, dialogue and FMV.
//
// Three mechanically unrelated things share this file because a player calls all
// three "a cutscene":
//
//   an ATEL event scene   the opcode script VM, stepped once per simulation step
//   a voiced line in one  the same VM, but the frame pacing switches to a timing
//                         track read off the millisecond clock
//   an FMV                a Phyre WebM player on its own decode thread
//
// A dialogue box is a fourth thing, driven by ATEL syscalls but stepped by its
// own module inside the same sub-step loop. It can be up outside a cutscene.
//
// Addresses are RVAs, which is the IDA VA minus 0x00400000. The derivation, the
// call chains and the evidence are in reversing\CUTSCENE.md.
//
// ALIASES, FOR THE OWNER TO RECONCILE. Five addresses here are the same
// addresses other agents' area headers already carry, under their names. They
// are repeated under distinct names rather than included, so this area keeps
// compiling if those headers are renamed or moved. Delete the duplicates
// whenever you reconcile:
//
//   MesWinBlockingKind         == Atel.h   AtelMesWinBlockingKind
//   EventPadPressed            == Atel.h   AtelPadPressed
//   EventContextPtr            == Atel.h   AtelContextPtr
//   EventSteppingContextIndex  == Atel.h   AtelPadSuppressState
//   MesWinPadBlock             == Input.h and MenuSystem.h   MenuPadBlock
//
// Nothing here duplicates addresses\MainLoop.h. PacingMode, PendingSteps,
// CatchUpExtraSteps and StepScale88 live there and Cutscene.cpp reads them from
// there.

namespace ffx
{
	namespace Rva
	{

		// ---------------------------------------------------------------------------
		// THE SYNCDATA TIMING TRACK, which is the answer to "what puts the game into
		// pacing mode 1".
		//
		// FFX_StepPacing_LoadTimingTrack is the ONLY writer of PacingMode in the whole
		// binary, and FFX_StepPacing calls it every frame that PendingSteps drained to
		// zero, before testing the mode. So the mode is re-derived continuously from
		// one question: does the selected timing track have entries left.
		//
		// Where a track comes from:
		//
		//   map load -> SyncDataRequestForMap(mapId)
		//                 checks SyncList, async-loads the map's sync data, and CLEARS
		//                 the cursor, so a map change always drops back to mode 0
		//
		//   an ATEL call-actor-script opcode -> FFX_Atel_CallActorScript0
		//                 -> SyncDataSelectTimingTrack(saveField, op1, op2, op3)
		//                    ON A MATCH it sets the cursor. THAT is what arms mode 1.
		//                    SelectTimingTrack has exactly one caller in the binary.
		//
		//   every frame -> SyncDataReadNextTimingEntry
		//                    12 bytes per entry: float dueTime, float tolerance,
		//                    int step88. Returns 999 at the terminator and for "no
		//                    track", and nulls the cursor, which is the way back to
		//                    mode 0.
		//
		// The other tables in the same file are read by ATEL core syscalls 213, 214 and
		// 215, whose debug prints are "VoiceStandbyInit!", "VoiceStartInit!" and
		// "VoiceStartResult!", so a timing track is a VOICED line keeping the
		// simulation lined up with its streamed audio.
		//
		// THE PART THAT MATTERS FOR LOCKSTEP. Mode 1 is wall-clock PACED but the
		// content of each step is still authored data: while mode 1 is active,
		// FFX_MainStep_PublishStepScale takes StepScale88 straight out of
		// TimingTrackStepTicks rather than from the game clock delta. Both machines
		// consume the same entries in the same order and get the same per-step motion
		// scale. Only the number of steps per presented frame differs, and lockstep
		// already owns that through PendingSteps.
		// ---------------------------------------------------------------------------
		const DWORD StepPacingLoadTimingTrack = 0x00421D60;   // void __cdecl (void), sets PacingMode
		const DWORD SyncDataMgr = 0x008E9014;                 // void *, 0x44 bytes, see SyncDataLayout::
		const DWORD SyncDataGet = 0x0027A8D0;                 // void *__cdecl (void)
		const DWORD SyncDataCreate = 0x0027A820;              // void __cdecl (void)
		const DWORD SyncDataRequestForMap = 0x0027A8E0;       // int __cdecl (int mapId), 1..500
		const DWORD SyncDataLoadSyncList = 0x0027A950;        // int __cdecl (void), parses SyncList.txt
		const DWORD SyncDataReadNextTimingEntry = 0x0027A930; // int __cdecl (float *due, float *tol, int *step88)
		const DWORD SyncDataSelectTimingTrack = 0x0027A9B0;   // int __cdecl (u16 key, int, int, int)
		const DWORD SyncDataLookupVoiceSync = 0x0027A9D0;     // int __cdecl (int voiceId, int kind 1..3)

		// The track's own state, all of it written by FFX_StepPacing and
		// FFX_StepPacing_LoadTimingTrack and nothing else.
		const DWORD TimingTrackNextTime = 0x00EFB8AC;  // float, the entry just read, milliseconds
		const DWORD TimingTrackTolerance = 0x00EFB8B0; // float, milliseconds of slack
		const DWORD TimingTrackDueTime = 0x00EFB8A4;   // float, when this step was due
		const DWORD TimingTrackElapsed = 0x00EFB8A8;   // float, seconds since the track started
		const DWORD TimingTrackStartMs = 0x00EFB818;   // __int64, FFX_Time_NowMilliseconds at track start
		const DWORD TimingTrackFirstStep = 0x008494D4; // int, 1 until the first step of a track has run
		const DWORD TimingTrackStepTicks = 0x00EFB828; // DWORD[30], 8.8 fixed step scales from the file

		// THE PRIMITIVE A LOCKSTEP STALL NEEDS. The Esc menu's own per-frame step
		// writes the milliseconds it was open into Pending, and FFX_StepPacing folds
		// that into Total and subtracts Total from the track's elapsed time. So the
		// engine already ships a supported way to tell a running track "pretend these
		// N milliseconds did not happen". A gate that stalls during mode 1 MUST use it
		// or the track burns catch-up steps the moment the gate opens.
		const DWORD TimingTrackPauseMsPending = 0x008CC868; // __int64, consumed and zeroed by FFX_StepPacing
		const DWORD TimingTrackPauseMsTotal = 0x00EFB7E8;   // __int64, the running total

		// Non-zero means frame skipping is allowed at all. A long per-scene exception
		// list, and it consults both MesWinBlockingKind and FmvIsPlaying, which is
		// the game's own evidence that those two reads mean what this area says.
		const DWORD StepPacingAllowCatchUp = 0x0041FD30; // BOOL __cdecl (void)

		// ---------------------------------------------------------------------------
		// THE DIALOGUE BOX.
		//
		// Three ATEL core syscalls do all of it, and between them they account for
		// roughly 33000 script uses across 359 event files, which is the whole game's
		// dialogue:
		//
		//   core:100  MesWinSetMessage  pops a window index and a message id
		//   core:106  MesWinShow        record+20 = 1, builds and activates the draw object
		//   core:107  MesWinClose       record+20 = 3 through MesWinRequestClose
		//
		// Core 124, 125, 132 and 489 are the blocking waits that hold the script thread
		// until the box closes, so the script does not advance until the player has
		// pressed.
		//
		// MesWinRequestClose is the better of the two edges to watch, because its nine
		// call sites catch the script close, the wait-syscall close and the
		// player-driven close alike.
		// ---------------------------------------------------------------------------
		const DWORD AtelMesWinRecords = 0x00F26D70;      // 2 buffers of 8 x 44 bytes, see MesWinRecord::
		const DWORD AtelMesWinBufSel = 0x00F26B82;       // BYTE, 0 or 1, picks the live buffer
		const DWORD AtelGetMessageWindow = 0x0046BE90;   // char *__cdecl (int index), clamps to 0..7
		const DWORD AtelGetMessageText = 0x0046BF30;     // void *__cdecl (int messageId)
		const DWORD AtelGetMessageAttr = 0x0046BF10;     // int __cdecl (int messageId)
		const DWORD AtelMesWinRequestClose = 0x004640F0; // char *__cdecl (int index)
		const DWORD AtelMesWinResetAll = 0x00463B10;     // int __cdecl (void), both buffers

		const DWORD AtelOpMesWinSetMessage = 0x00457870; // core:100 handler
		const DWORD AtelOpMesWinShow = 0x00458B50;       // core:106 handler
		const DWORD AtelOpMesWinClose = 0x00459060;      // core:107 handler

		// THE OBSERVABLE. Pure reads of two globals, no call out, safe anywhere.
		//   0  no message window is open
		//   1  a window is up and is not waiting on the player
		//   2  a window is up and WAITS FOR A BUTTON before it will close
		// Its prologue embeds an absolute address so it is not a clean detour target,
		// but nothing needs to detour it, only call it.
		const DWORD MesWinBlockingKind = 0x0046C8B0; // int __cdecl (void)

		// ---------------------------------------------------------------------------
		// THE MESSAGE WINDOW MODULE, which is where the advance happens.
		//
		// MesWinStepAll runs from the FFX_MainStep sub-step loop, so a dialogue box
		// advances once per SIMULATION step and never off a real clock. It samples the
		// pad first and then walks the 8 windows, so a value written inside a hook on
		// the sampler is exactly what the advance read sees, in the same step.
		//
		// THE ADVANCE READ IS MesWinOnConfirmPressed:
		//
		//   008B64A3  test byte ptr g_ffxMenuPadHeldTrig+2, 20h
		//
		// which is MesWinPadBlock + 0x14, the newly-pressed mask, bit 0x20, which is
		// ffx::Btn::Circle. One global, one bit, no port and no player id.
		//
		// TWO CO-OP HAZARDS IN THE SAMPLER:
		//  1. FFX_Pad__readButtons16(0, 0, 0) and FFX_Pad__readAnalogByte(0, ...), so
		//     PORT 0 IS HARDCODED. The second player's pad never reaches a box.
		//  2. The hold timers come from FFX_Input__getTimeSeconds, the wall clock, with
		//     a first repeat at 0.2333 s and then 0.1333 s. Held directions in a choice
		//     list are therefore not deterministic. A single confirm press IS, because
		//     that path is a pure edge test.
		//
		// THIS IS A SECOND SAMPLER OF THE SAME BLOCK. MenuSysSamplePad is the in-game
		// menu's, with 0.4667 s and 0.3 s. Both write the same 0xC0 bytes. Whoever owns
		// menu replication needs to know that.
		// ---------------------------------------------------------------------------
		const DWORD MesWinStepAll = 0x004AB910;           // int __cdecl (void), from the sub-step loop
		const DWORD MesWinSamplePadPort0 = 0x004B7CD0;    // THE HOOK POINT. Clean 5-byte prologue.
		const DWORD MesWinOnConfirmPressed = 0x004B64A0;  // int __cdecl (int index), the advance read
		const DWORD MesWinAdvanceText = 0x004B8CA0;       // what the advance read calls
		const DWORD MesWinRunConfirmHandler = 0x004B6B70; // int __cdecl (int index), gates on sub-state 2
		const DWORD MesWinRunStepHandler = 0x004B5850;    // int __cdecl (int index)
		const DWORD MesWinActivate = 0x004ADE10;          // int __cdecl (int index)
		const DWORD MesWinDeactivate = 0x004AB8F0;        // int __cdecl (int index)
		const DWORD MesWinResetDrawState = 0x004ADA30;    // installs the confirm handler at +252

		// The three parallel arrays of 8 draw-state pointers each.
		const DWORD MesWinObjects = 0x01465B3C;       // void *[8]
		const DWORD MesWinTextObjects = 0x014676F0;   // void *[8], handlers at +248 +252 +276
		const DWORD MesWinChoiceObjects = 0x01468A90; // void *[8]

		// The shared menu pad block, 0xC0 bytes. Duplicated from Input.h and
		// MenuSystem.h, see the banner at the top. The two offsets this area needs are
		// MesWinPad::Held and MesWinPad::Pressed in ffx/Cutscene.h.
		const DWORD MesWinPadBlock = 0x021D09C0;

		// ---------------------------------------------------------------------------
		// THE ATEL SCRIPT VM, AND WHY A CUTSCENE REPLICATES FOR FREE.
		//
		// AtelStepOnce is called from inside the FFX_MainStep SUB-STEP LOOP:
		//
		//   FFX_MainStep
		//     the sub-step loop
		//       AtelStepOnce
		//         -> AtelStepFieldFrame's parent, which calls
		//            AtelStepContextRange(0, 6)
		//              -> per context: ctx + 0x4C, the step callback
		//                 -> FFX_Atel_StepFrame
		//                    -> FFX_Atel_RunScript per actor, the 123-case switch
		//
		// and separately FFX_MenuSys_StepFrame drives AtelStepMenuContext, which is
		// AtelStepContextRange(6, 7).
		//
		// NO CLOCK READ ANYWHERE ON THAT PATH. The opcode VM advances once per
		// simulation step. The only non-determinism left in an ATEL cutscene is the
		// seeded RNG stream, drawn by exactly four script-visible syscalls
		// (core:166, core:169, came:128 which draws twice, btl:235), so syncing the RNG
		// stream is the whole requirement.
		//
		// AtelStepContextRange bumps a per-context step counter at ctx + 500 while the
		// context's flag bit 8 is set. That is the closest thing the game has to "how
		// far into this cutscene are we" and it is the right desync cross-check.
		// ---------------------------------------------------------------------------
		const DWORD AtelStepOnce = 0x0048D3D0;              // int __cdecl (void), from the sub-step loop
		const DWORD AtelStepContextRange = 0x004688F0;      // int __cdecl (int first, int last)
		const DWORD AtelStepFieldContexts = 0x004666D0;     // int __cdecl (void), range 0..5
		const DWORD AtelStepMenuContext = 0x0046DD60;       // int __cdecl (void), range 6..6
		const DWORD AtelContextStepCallback = 0x00467710;   // what lands in ctx + 0x4C
		const DWORD EventContextPtr = 0x00F26B28;           // BYTE **, the selected context. See Atel.h.
		const DWORD EventSteppingContextIndex = 0x00F26B30; // DWORD, which context is being stepped
		const DWORD EventPadPressed = 0x00F270D0;           // WORD, port 0 edge pressed. See Atel.h.
		const DWORD AtelGetPadPressed = 0x0046AF70;         // __int16 __cdecl (void)
		const DWORD RandStream = 0x003988F0;                // the seeded stream the script RNG draws from

		// ---------------------------------------------------------------------------
		// FMV, which CANNOT be lockstepped and has to be a barrier.
		//
		// The PS2 movie_* syscall surface is still in the binary and every one of those
		// handlers is a stub that prints "Virtuos Warning: Movie on Windows, PS3 & PS
		// Vita is not impelemented yet!". The real player is Phyre's:
		// FmvCreatePlayer resolves a video id to a path and starts
		// PVideoPlaybackWin32::_VideoDecodingThread, which parses the file with
		// libwebm's mkvparser, so THE VIDEO IS WEBM, decoded on its own thread with a
		// GetTickCount timebase and a 29.97 fallback frame rate.
		//
		// WHY THAT TURNS OUT NOT TO MATTER MUCH. FFX_MainStep early-returns on
		// FmvGetPlaybackState() == 2, so NO SIMULATION RUNS during an FMV. The step
		// counter, the game clock, the RNG and the script are all frozen. Both machines
		// sit on the same simulation step for the whole movie, so the design only has
		// to agree on when it starts, when it ends, and who may skip it.
		//
		// For the start, replicate FmvScriptRunning, because that is set and cleared by
		// ATEL Movie syscalls and so flips on the same opcode on both machines. For the
		// end, barrier on both machines reporting FmvIsPlaying() == 0.
		// ---------------------------------------------------------------------------
		const DWORD FmvState = 0x008CC830;               // void *, lazily built, 0x340 bytes
		const DWORD FmvGetPlaybackState = 0x002411E0;    // int __cdecl (void), 2 means the movie owns the frame
		const DWORD FmvPlayerManager = 0x008DED3C;       // void *, PhyFMVPlayerManager, see FmvManager::
		const DWORD FmvGetManager = 0x002D73A0;          // void *__cdecl (void), prints if not built yet
		const DWORD FmvIsPlaying = 0x00241CF0;           // int __cdecl (void), manager + 1744
		const DWORD FmvIsDecoderBusy = 0x00241CE0;       // int __cdecl (void), the decode queue drain
		const DWORD FmvScriptRunning = 0x00D2A008;       // int, the ATEL script's own view. REPLICATE THIS.
		const DWORD FmvCreatePlayer = 0x002D9C80;        // __thiscall (mgr, int videoId, char hd, char flag)
		const DWORD FmvDestroyPlayer = 0x002D71B0;       // __thiscall (mgr)
		const DWORD FmvVideoDecodingThread = 0x00627F20; // the WebM decode thread entry

		// ---------------------------------------------------------------------------
		// THE CUTSCENE CAMERA, which is ONE GLOBAL PER SCREEN AND NOT PER PLAYER.
		//
		// All 66 FFX_AtelSys_Came_* syscalls that need a handle come through
		// CameGetHandleForClass(screenArg, kind, slot), which caches one handle per
		// (class, screen) in four arrays of three:
		//
		//   kind 0 -> handle class 3, Event   CameEventHandles   <- cutscene scripts
		//   kind 1 -> handle class 1, Map     CameMapHandles
		//   kind 2 -> handle class 5, Battle  CameBattleHandles
		//   kind 3 -> handle class 7, Viewer  CameViewerHandles
		//
		// So a cutscene drives one global event camera per screen, it outranks the
		// class 1 player camera, and the client sees the host's framing for free as
		// long as the same script runs. Handles are cached forever once opened, so a
		// non-zero entry is NOT an "a cutscene is running" test.
		//
		// If a passenger-keeps-their-own-view design is ever wanted, the route is the
		// game's own 3-screen support, not a second class 3 handle, because the
		// priority arbitration picks one winner per screen.
		//
		// The FMV path does not use a handle at all: ATEL Movie syscalls 1 and 4 call
		// CameSetMatrixOverride, which bypasses every mode and priority while
		// CameOverrideFlag is set.
		// ---------------------------------------------------------------------------
		const DWORD CameGetHandleForClass = 0x003BA780; // int __cdecl (int screenArg, int kind, unsigned slot)
		const DWORD CameEventHandles = 0x00D36F94;      // int[3], handle class 3
		const DWORD CameMapHandles = 0x00D36F88;        // int[3], handle class 1
		const DWORD CameBattleHandles = 0x00D36FA0;     // int[3], handle class 5
		const DWORD CameViewerHandles = 0x00D36FAC;     // int[3], handle class 7
		const DWORD CameSetMatrixOverride = 0x003C06D0; // (enable, proj, view, refPos, camPos, scrDpt)
		const DWORD CameOverrideFlag = 0x01F114C4;      // int, non-zero forces the slot every step

		// ---------------------------------------------------------------------------
		// Every address above, for the startup build check. VerifyLayout walks this so
		// a typo is caught at startup rather than by a fault later.
		//
		// Keep it in step with the constants. If you add an address and forget this
		// list, nothing breaks today and something breaks confusingly in a year.
		// ---------------------------------------------------------------------------
		inline const DWORD* CutsceneRvaList(int* count)
		{
			static const DWORD list[] = {
				StepPacingLoadTimingTrack,
				SyncDataMgr,
				SyncDataGet,
				SyncDataCreate,
				SyncDataRequestForMap,
				SyncDataLoadSyncList,
				SyncDataReadNextTimingEntry,
				SyncDataSelectTimingTrack,
				SyncDataLookupVoiceSync,
				TimingTrackNextTime,
				TimingTrackTolerance,
				TimingTrackDueTime,
				TimingTrackElapsed,
				TimingTrackStartMs,
				TimingTrackFirstStep,
				TimingTrackStepTicks,
				TimingTrackPauseMsPending,
				TimingTrackPauseMsTotal,
				StepPacingAllowCatchUp,

				AtelMesWinRecords,
				AtelMesWinBufSel,
				AtelGetMessageWindow,
				AtelGetMessageText,
				AtelGetMessageAttr,
				AtelMesWinRequestClose,
				AtelMesWinResetAll,
				AtelOpMesWinSetMessage,
				AtelOpMesWinShow,
				AtelOpMesWinClose,
				MesWinBlockingKind,

				MesWinStepAll,
				MesWinSamplePadPort0,
				MesWinOnConfirmPressed,
				MesWinAdvanceText,
				MesWinRunConfirmHandler,
				MesWinRunStepHandler,
				MesWinActivate,
				MesWinDeactivate,
				MesWinResetDrawState,
				MesWinObjects,
				MesWinTextObjects,
				MesWinChoiceObjects,
				MesWinPadBlock,

				AtelStepOnce,
				AtelStepContextRange,
				AtelStepFieldContexts,
				AtelStepMenuContext,
				AtelContextStepCallback,
				EventContextPtr,
				EventSteppingContextIndex,
				EventPadPressed,
				AtelGetPadPressed,
				RandStream,

				FmvState,
				FmvGetPlaybackState,
				FmvPlayerManager,
				FmvGetManager,
				FmvIsPlaying,
				FmvIsDecoderBusy,
				FmvScriptRunning,
				FmvCreatePlayer,
				FmvDestroyPlayer,
				FmvVideoDecodingThread,

				CameGetHandleForClass,
				CameEventHandles,
				CameMapHandles,
				CameBattleHandles,
				CameViewerHandles,
				CameSetMatrixOverride,
				CameOverrideFlag,
			};
			if (count)
				*count = (int)(sizeof(list) / sizeof(list[0]));
			return list;
		}

	} // namespace Rva
} // namespace ffx
