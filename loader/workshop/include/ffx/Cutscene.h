#pragma once

#include <windows.h>

// Cutscenes, dialogue and FMV.
//
// What a co-op plugin needs to answer four questions: is a cutscene running, is
// a dialogue box waiting for somebody to press a button, is the frame pacing
// about to become non-deterministic, and may I let either player advance the
// text.
//
// Every reader here goes through workshop::Readable and returns false rather
// than faulting, because all of it is meant to be callable from the frame path.
//
// Two exceptions, both documented where they are declared: CatchUpAllowed and
// FmvDecoderBusy call into the game. FmvDecoderBusy is a pure read of the FMV
// manager with no allocation, but it is a call, so ReadFmvState is a call too
// and both want the game thread.
//
// ===========================================================================
// THE ONE-PARAGRAPH VERSION
// ===========================================================================
//
// An ATEL cutscene replicates for free. The script VM steps inside the
// FFX_MainStep sub-step loop, once per simulation step, and never reads a
// clock. An FMV does not replicate and has to be a barrier, because it is WebM
// decoded on its own thread. In between sits the one genuine hazard: a VOICED
// line switches the frame pacing to a timing track read off the millisecond
// clock, and a machine that is ahead of its track BUSY-WAITS. Even there, the
// content of each step comes out of the data file rather than the clock, so
// lockstep wins by owning the step count rather than by rewriting the pacing.
//
// CORRECTION, AND IT MATTERS. An earlier version of this file said no
// simulation runs while an FMV plays. That was wrong, and so was the claim in
// reversing\CUTSCENE.md section 7.2 that it came from. THE SIMULATION KEEPS
// RUNNING THROUGH A MOVIE. What is frozen is the one script that asked for the
// movie, because it is parked in an ATEL poll handler waiting for it. See the
// FMV section near the bottom of this file for the evidence and for what that
// changes about the barrier.
//
// ===========================================================================
// THE THREE THINGS A PLAYER CALLS A CUTSCENE
// ===========================================================================
//
//   an ATEL event scene   the opcode script VM. One step per simulation step.
//                         Deterministic given a synced RNG stream.
//   a voiced line in one  the same VM, but FFX_StepPacing switches to the
//                         SyncData timing track and paces off the clock.
//   an FMV                a Phyre WebM player on its own decode thread. The
//                         simulation keeps running. What is frozen is the one
//                         script parked in the movie wait, and THAT is what the
//                         barrier has to line up.
//
// A dialogue box is a fourth thing, driven by ATEL syscalls but stepped by its
// own module inside the same sub-step loop. It can be up outside a cutscene,
// which is why DialogueState() is a separate question from CutsceneActive().
//
// ===========================================================================
// PACING MODE 1, WHICH WAS THE PROJECT'S BIGGEST OPEN QUESTION
// ===========================================================================
//
// FFX_StepPacing_LoadTimingTrack is the ONLY writer of the pacing mode in the
// whole binary, and FFX_StepPacing calls it every frame that the pending step
// count drained to zero, before testing the mode. So mode 1 means exactly one
// thing and it is re-derived every frame:
//
//   a SyncData timing track is selected and has entries left
//
// A track is selected by an ATEL call-actor-script opcode whose operand tuple
// matches an entry in the current map's sync table. The tables beside it in the
// same file are read by the script's voice commands, so a timing track is a
// voiced line keeping the simulation lined up with its streamed audio. A
// cutscene with no voiced line never leaves mode 0.
//
// What mode 1 does: it compares elapsed milliseconds against the track's due
// time, takes extra simulation steps when it is behind, and SPINS in a
// for (i = 0; i < 10000; ++i) loop when it is more than 100 ms ahead.
//
// What mode 1 does NOT do: make a step's contents depend on the clock. While
// mode 1 is active, FFX_MainStep_PublishStepScale takes the per-step motion
// scale straight out of the track data rather than from the game clock delta.
// Both machines consume the same entries in the same order and get the same
// scale. Only the step count per presented frame differs.
//
// SO THE RECIPE IS: let the track run, own the step count, and tell the track
// about any time the gate stole. The last part is the bit that is easy to miss
// and it has a shipped primitive:
//
//   if (ffx::PacingMode() == 1) {
//       DWORD before = GetTickCount();
//       ... hold the simulation while waiting for the peer ...
//       ffx::AddTimingTrackPauseMs(GetTickCount() - before);
//   }
//
// Without that, the track believes it fell behind by however long the gate held
// and burns catch-up steps the instant the gate opens, which looks like the
// cutscene lurching forward.
//
// ONE MORE TRAP. The busy-wait lives inside FFX_StepPacing, inside
// FFX_MainStep, inside animate. A gate in the animate hook runs BEFORE it, so
// that is fine. A gate in the narrow FFX_MainStep detour runs AFTER
// FFX_StepPacing has already spun, so a machine ahead of its track burns up to
// 100 ms before the gate is even asked. Use the animate hook during a cutscene.
//
// ===========================================================================
// THE DIALOGUE ADVANCE, AND WHY IT IS EASY
// ===========================================================================
//
// One global, one bit, no port and no player id:
//
//   the shared menu pad block + 0x14, bit ffx::Btn::Circle
//
// read by FFX_MesWin_OnConfirmPressed as a plain edge test. The block is filled
// by FFX_MesWin_SamplePadPort0 from pad PORT 0 ONLY, and FFX_MesWin_StepAll
// samples and then immediately walks the 8 windows, so a value written inside
// a hook on the sampler is exactly what the advance read sees in the same step.
//
//   bool __cdecl MyMesWinPadHook(void)   // installed by HookDialoguePad
//   {
//       if (PeerPressedConfirm()) ffx::InjectDialogueConfirm(true);
//       return true;
//   }
//
// The sampler's prologue is a clean five bytes with no branch and no absolute
// address, which is why it is the hook point rather than the confirm handler
// (absolute address in the prologue) or the step function (relative call at
// offset 2).
//
// TWO HAZARDS WORTH KNOWING EVEN THOUGH NEITHER BREAKS THE PLAN:
//   - port 0 is hardcoded, so the second pad never reaches a box on its own
//   - the auto-repeat timers are wall-clock derived, first repeat 0.2333 s then
//     0.1333 s, so HELD DIRECTIONS in a choice list are not deterministic. A
//     single confirm press is, because that is a pure edge test.
//
// The derivation and the evidence are in reversing\CUTSCENE.md.

namespace ffx
{

	// ---------------------------------------------------------------------------
	// Field offsets. Not addresses, so they live here rather than in
	// addresses/Cutscene.h.
	// ---------------------------------------------------------------------------

	// The 0x44-byte SyncData manager. The cursor at +8 being non-null is the
	// precondition for pacing mode 1.
	namespace SyncDataLayout
	{
		const DWORD TrackDataArray = 0x04; // void **, parallel with the key arrays
		const DWORD TrackCursor = 0x08;    // float *, THE thing that arms mode 1
		const DWORD LoadedMapId = 0x0C;    // int
		const DWORD SelectEntries = 0x10;  // int, track-select table row count
		const DWORD VoiceEntries = 0x14;   // int, voice-sync table row count
		const DWORD SyncListCount = 0x18;  // int
		const DWORD MapHasSyncData = 0x1C; // int, non-zero means this map has a file
		const DWORD LoadFinished = 0x20;   // int, the async load completed
		const DWORD SyncListArray = 0x40;  // int *, the map ids that have sync data
	} // namespace SyncDataLayout

	// One of the 8 ATEL message window records. Stride 44, and the whole array of 8
	// is double buffered with a 352-byte stride, so always go through
	// MessageWindow() rather than indexing the base.
	namespace MesWinRecord
	{
		const DWORD Stride = 44;
		const DWORD BufferStride = 352;
		const DWORD RectX = 0x00;        // short
		const DWORD RectY = 0x02;        // short
		const DWORD RectW = 0x04;        // short
		const DWORD RectH = 0x06;        // short
		const DWORD TextPtr = 0x08;      // char *, from FFX_Atel_GetMessageText
		const DWORD TextPtrAlso = 0x0C;  // char *, the same value
		const DWORD State = 0x14;        // WORD (byte 20), see MesWinState below
		const DWORD Attr = 0x16;         // WORD (byte 22), the message table's attribute
		const DWORD StyleIndex = 0x1C;   // BYTE (byte 28)
		const DWORD Flags = 0x1D;        // BYTE (byte 29), see MesWinFlag below
		const DWORD ChosenOption = 0x1E; // short (byte 30), -1 for none
		                                 // Bytes 31 through 35 are the choice window's geometry and are passed straight
		                                 // to the draw module. Which is which was NOT established, so they are
		                                 // deliberately not named here. See reversing\CUTSCENE.md.
	} // namespace MesWinRecord

	// Values of MesWinRecord::State. 0, 1 and 3 were read directly from the writes.
	// 2 and 4 are inferred from the core:125 wait handler, which reports completion
	// on 0 and 4 and reads the player's choice on 2. See reversing\CUTSCENE.md.
	namespace MesWinState
	{
		const WORD Idle = 0;
		const WORD Shown = 1;
		const WORD WaitingOnPlayer = 2; // inferred
		const WORD Closing = 3;
		const WORD Finished = 4; // inferred
	} // namespace MesWinState

	// Bits of MesWinRecord::Flags. The byte is computed by the show syscall from its
	// operand. Three of these are proved by what the code does with them, two are
	// inferred from which draw call they select.
	namespace MesWinFlag
	{
		const BYTE FullRect = 0x08;    // proved: selects the 4-short rect draw over the 2-short one
		const BYTE BlocksSoft = 0x10;  // proved: counts as "a box is up" but does not wait
		const BYTE BlocksWait = 0x22;  // proved: waits for a button before it will close
		const BYTE ChoiceArmed = 0x20; // proved: the choice wait syscall ORs this in
		const BYTE SubDrawA = 0x02;    // inferred, selects one extra draw call
		const BYTE SubDrawB = 0x40;    // inferred, selects a different one and suppresses 0x02
	} // namespace MesWinFlag

	// The two offsets this area needs inside the shared menu pad block. The full
	// layout is the menu system's, in addresses\MenuSystem.h.
	namespace MesWinPad
	{
		const DWORD Held = 0x12;    // WORD, the level mask
		const DWORD Pressed = 0x14; // WORD, the newly-pressed edge. THE ADVANCE READ.
	} // namespace MesWinPad

	// PhyFMVPlayerManager, the fields read out of FFX_Fmv_CreatePlayer, plus the
	// skip and completion fields read out of FFX_Fmv_PollSkipButtons 0x6D9460,
	// FFX_Fmv_StepPresentation 0x6D7680 and FFX_Fmv_PlaybackFinished 0x645DC0.
	namespace FmvManager
	{
		const DWORD FilePath = 0x4F0;  // char[], the resolved video path
		const DWORD IsPlaying = 0x6D0; // BYTE (1744), what FFX_Fmv_IsPlaying reads
		const DWORD HasPlayer = 0x6D2; // BYTE (1746), a player object exists
		const DWORD VideoId = 0x6DC;   // int (1756)
		const DWORD WidthF = 0x6F4;    // float (1780)
		const DWORD HeightF = 0x6F8;   // float (1784)

		// BYTE (1764). The decode side reached the end of the video.
		// FFX_Fmv_PlaybackFinished returns this while IsPlaying is set, and 1 when
		// it is clear, and that return is what the Movie:9 and Movie:10 script
		// waits key off. So "the movie is over" is IsPlaying == 0 OR this byte.
		const DWORD PlaybackComplete = 0x6E4;

		// BYTE (1788). The Phyre player's own pause flag, toggled by
		// FFX_Fmv_TogglePause. The suspend overlays and the window focus handlers
		// are what set it, which is how alt-tab pauses a movie.
		const DWORD Paused = 0x6FC;

		// The three skip fields. FFX_Fmv_PollSkipButtons needs SkipAllowed to arm
		// the prompt, sets SkipPromptUp when Start is pressed, and refuses the whole
		// thing while SkipForbidden is set.
		const DWORD SkipPromptUp = 0x74C;  // BYTE (1868), the prompt is on screen
		const DWORD SkipAllowed = 0x74D;   // BYTE (1869), this movie may be skipped
		const DWORD SkipForbidden = 0x74E; // BYTE (1870), skipping is refused

		// int (1752) and int (1760). The frame index FFX_Fmv_StepPresentation
		// advances from the player's own time, and the previous value of it. The
		// skip forces both to 65534, which is the past-the-end sentinel the
		// presentation step tests against with "< 65534".
		const DWORD FrameIndex = 0x6D8;
		const DWORD FrameIndexPrev = 0x6E0;

		// BYTE (1808). Set to 1 both by the skip and by FFX_Fmv_StepPresentation's
		// own stop branch, and cleared when the manager is constructed. So:
		// presentation has stopped.
		const DWORD PresentationStopped = 0x710;

		// BYTE (1824). Guards the skip's clearing of IsPlaying and the three writes
		// in the presentation step's stop branch, so a 1 here would mean "stopping
		// does not actually stop playback".
		//
		// NOTHING IN THE BINARY EVER WRITES A 1. Checked by scanning .text for every
		// "mov byte ptr [reg+720h], imm" encoding: the only two writers are
		// FFX_Fmv_CreatePlayer and the manager constructor and both write 0. The
		// guard is still honoured rather than assumed away, because it costs one
		// read.
		const DWORD StopSuppressed = 0x720;
	} // namespace FmvManager

	// The lazily built 0x340-byte object behind the function the addresses file
	// calls FrameGetSuspendState.
	//
	// THAT NAME IS WRONG AND THIS IS NOT AN FMV OBJECT. Every one of the three
	// fields is a USER-DRIVEN full-screen suspend, and no FMV playback code writes
	// any of them. The IDB now calls the function FFX_Frame_GetSuspendState. Walk
	// the writers yourself if you want to check, there are only three:
	//
	//   Overlay   0x338  toggled by remappable action 32, mask 0x100000, through
	//                    FFX_Frame_ToggleSuspendOverlay 0x680320. It pauses audio
	//                    and pauses the Phyre video. Only other writer is the
	//                    constructor sub_67FE70, which zeroes it.
	//   Cutscene  0x33C  the Start-button in-cutscene pause overlay, written only
	//                    by FFX_Frame_SetCutsceneSuspend 0x6803A0, reached only
	//                    from FFX_Cutscene_StepPauseOverlay 0x8AB340 and
	//                    FFX_Cutscene_EnterPauseOverlay 0x8AB3D0.
	//   State     0x000  the debug free camera, remappable action 30, mask
	//                    0x40000, unbound by default. FFX_Frame_StepSuspendOverlay
	//                    0x680030 FORCES it to 0 whenever a movie is running.
	//
	// So FFX_MainStep's early return on "state == 2" is the PC port's pause, not
	// the FMV path. The consequence is in the FMV section below.
	namespace FrameSuspendLayout
	{
		const DWORD State = 0x000;    // int, the debug free camera mode
		const DWORD FlagA = 0x338;    // BYTE, the action 32 suspend overlay
		const DWORD FlagB = 0x33C;    // BYTE, the Start-button cutscene pause
		const DWORD Overlay = 0x338;  // the same byte under the name it earned
		const DWORD Cutscene = 0x33C; // ditto
	} // namespace FrameSuspendLayout

	// The ATEL context field this area cares about. The rest is addresses\Atel.h.
	namespace AtelContext
	{
		const DWORD StepCounter = 500; // int, ++ per step while the context is active
	} // namespace AtelContext

	// ---------------------------------------------------------------------------
	// IS A CUTSCENE RUNNING
	//
	// There is no single boolean in the binary and the obvious candidate is a dead
	// end: g_ffxPlayerControlEnabled's only writer has zero call sites, so it is an
	// exported plugin API the shipped game never uses.
	//
	// What does exist is four cheap reads, three of which the engine itself
	// consults. CutsceneState gathers them in one pass so the values agree with
	// each other.
	// ---------------------------------------------------------------------------
	struct CutsceneState
	{
		int dialogueKind;       // 0 none, 1 a box is up, 2 a box waits for a button
		int pacingMode;         // 1 means a voiced segment is pacing off the clock
		int fmvPlaybackState;   // 2 means an FMV owns the frame
		bool fmvScriptRunning;  // the script's own view of a movie
		bool fmvPlaying;        // the manager's "a video is playing" byte
		int atelStepCounter;    // the live ATEL context's own step number, -1 if unreadable
		bool anyCutsceneSignal; // any of the above says "this is not plain gameplay"
	};
	bool ReadCutsceneState(CutsceneState* out);

	// The cheap composite. True when ANY of the four signals is up. Use this for
	// "something scripted owns the screen", not for "the simulation is frozen",
	// which is ffx::IsSimulationPaused in MainLoop.h.
	bool CutsceneActive();

	// The nearest thing to a cutscene identity. There is no cutscene name or id in
	// the binary, so this is the live ATEL context's own per-step counter at
	// ctx + 500, which advances once per simulation step while the context is
	// active. Use it as a desync cross-check, not as a name. Returns false if the
	// context pointer is not usable yet.
	bool AtelStepCounter(int* out);

	// THE ONE OBSERVABLE A CO-OP LAYER SHOULD GATE INPUT ON. True while a dialogue
	// box, a voiced cutscene segment or an FMV owns the screen. Pure reads except
	// for the one call into FFX_Atel_MesWinBlockingKind, which only touches two
	// globals, so this is safe from the frame path.
	bool GameplayInputShouldBeIgnored();

	// ---------------------------------------------------------------------------
	// THE DIALOGUE BOX
	// ---------------------------------------------------------------------------

	// Values of DialogueState::kind, which is FFX_Atel_MesWinBlockingKind's return.
	const int dialogueNone = 0;
	const int dialogueOpen = 1;    // a box is up and is not waiting on the player
	const int dialogueWaiting = 2; // a box is up and WANTS A BUTTON PRESS

	struct DialogueState
	{
		int kind;           // dialogueNone, dialogueOpen or dialogueWaiting
		int windowIndex;    // the first window that is not idle, -1 if none
		WORD state;         // that window's MesWinRecord::State
		WORD attr;          // that window's message attribute
		BYTE flags;         // that window's MesWinRecord::Flags
		short chosenOption; // the choice it has recorded, -1 for none
		int openWindows;    // how many of the 8 are not idle
	};
	bool ReadDialogueState(DialogueState* out);

	// True while any message window is open at all. A pure read: the records are
	// walked here rather than through the game's own getter, so every dialogue
	// reader in this file is safe from any thread.
	bool DialogueOpen();

	// True only while a box is actually waiting for somebody to press confirm.
	// This is the state in which InjectDialogueConfirm does something.
	bool DialogueWaitingForInput();

	// The record for one of the 8 windows, with the double buffer resolved the same
	// way the game's own getter does it. Null if the index is out of range or the
	// array is not readable yet.
	BYTE* MessageWindow(int index);

	// The message text pointer a window is showing, read straight out of the
	// record. Returns NULL rather than an empty string if there is nothing there,
	// so a caller can tell "no box" from "an empty box".
	const char* MessageText(int windowIndex);

	// Ask the game to close a window, exactly as ATEL core syscall 107 does. This
	// is the script's own close path, so it is safe, but remember the SCRIPT is
	// what is waiting on the box: closing it from outside unblocks the script
	// thread and the cutscene carries on. Use it to make a remote advance land, not
	// to dismiss a box the local player has not answered.
	bool CloseDialogue(int windowIndex);

	// ---------------------------------------------------------------------------
	// LETTING EITHER PLAYER ADVANCE
	//
	// HookDialoguePad detours the message window's pad sampler. The callback runs
	// AFTER the game has filled the block from port 0 and BEFORE the 8 windows are
	// walked, which is the only window in the frame where an injected bit is both
	// unclobbered and seen.
	//
	// Returning false from the callback does not cancel anything, it is just there
	// so a gate can report that it declined to inject.
	//
	// The callback may be null, so the hook can be installed once at startup and
	// armed later.
	// ---------------------------------------------------------------------------
	typedef bool(__cdecl* DialoguePadFn)(void);

	bool HookDialoguePad(DialoguePadFn callback);
	bool DialoguePadHookInstalled();

	// OR the confirm bit into the shared pad block. Call this from inside a
	// DialoguePadFn callback and nowhere else, because anywhere else in the frame
	// either the sampler overwrites it or the windows have already been walked.
	// alsoHeld sets the held mask as well, which matters only for the held-repeat
	// paths in a choice list.
	bool InjectDialogueConfirm(bool alsoHeld);

	// The general form, for a remote mask that is more than just confirm. Same
	// timing rule. The mask is the 16-bit PS2 layout, so ffx::Btn::* values.
	bool InjectDialoguePad(WORD pressedMask, WORD heldMask);

	// REPLACES the block instead of OR-ing into it, which is the form a lockstep
	// layer needs.
	//
	// The difference matters and it is not a detail. InjectDialoguePad ORs, so a local
	// press and a remote press in the same step both land, which is exactly right for
	// "let either player advance a box". Under lockstep it is wrong: the sampler has
	// already filled the block from the LIVE local pad, which is undelayed and therefore
	// a different value on the two machines. OR-ing a replicated mask on top of that
	// leaves the local press in there, and the box advances a step early on whichever
	// machine the player is sitting at.
	//
	// So this one assigns. Everything the message windows see then comes from the
	// replicated mask and nothing comes from the hardware. Same timing rule as the other
	// two: from inside a DialoguePadFn callback and nowhere else.
	//
	// Note this also discards the engine's auto-repeat for the bits it covers, which is a
	// bonus rather than a loss. Those timers are wall-clock derived, first repeat at
	// 0.2333 s and then 0.1333 s, so a held direction in a choice list was never going to
	// be deterministic while the engine owned the edges. A caller that wants repeat has to
	// do its own, from the step counter rather than the clock.
	bool SetDialoguePad(WORD pressedMask, WORD heldMask);

	// What the block currently holds, for logging and for a desync check. Both
	// halves of the one dword the advance read tests.
	bool ReadDialoguePad(WORD* held, WORD* pressed);

	// ---------------------------------------------------------------------------
	// PACING AND THE TIMING TRACK
	//
	// PacingMode duplicates the read in MainLoop.h deliberately, so a plugin that
	// only cares about cutscenes does not have to include both. Same address, same
	// value.
	// ---------------------------------------------------------------------------
	int PacingMode();
	const char* PacingModeName(); // "accumulator" or "timing track, wall clock"

	struct TimingTrackState
	{
		int pacingMode;      // 1 while a track is driving the pacing
		bool trackSelected;  // the manager's cursor is armed
		bool mapHasSyncData; // this map shipped a sync file at all
		bool loadFinished;   // the async load completed, so a read will not block
		int loadedMapId;
		bool firstStep;   // the track has not taken its first step yet
		float nextTimeMs; // the entry just read
		float toleranceMs;
		float dueTimeMs;
		float elapsedSeconds;
		int extraStepsThisFrame; // how many catch-up steps the track asked for
		int stepScale88;         // the authored per-step motion scale in use
	};
	bool ReadTimingTrackState(TimingTrackState* out);

	// True while a voiced cutscene segment is pacing itself off the millisecond
	// clock. Equivalent to PacingMode() == 1, named for what it means.
	bool TimingTrackDriving();

	// TELL A RUNNING TIMING TRACK TO IGNORE SOME WALL-CLOCK TIME. This is the
	// shipped Esc-menu compensation, reached from outside. Call it with the number
	// of milliseconds a lockstep gate held the simulation, and the next
	// FFX_StepPacing folds it in and subtracts it from the track's elapsed time.
	//
	// It ADDS rather than overwrites, so a gate stall and an Esc-menu pause in the
	// same frame cannot lose one another. In pacing mode 0 nothing reads or clears
	// the value, so a write made outside a track just sits there, which is
	// harmless but means it is best written close to when it matters.
	bool AddTimingTrackPauseMs(DWORD milliseconds);
	bool TimingTrackPauseMs(DWORD* pending, DWORD* total);

	// Whether the engine is currently willing to skip frames at all. A long
	// per-scene exception list, and it consults both the dialogue observable and
	// the FMV one, which is the game's own evidence that those reads mean what this
	// file says. CALL FROM THE FRAME THREAD ONLY, it walks several subsystems.
	bool CatchUpAllowed();

	// ---------------------------------------------------------------------------
	// FMV
	//
	// An FMV cannot be lockstepped. It is WebM parsed by libwebm and decoded on its
	// own thread with a GetTickCount timebase, and the two players' machines will
	// never present the same video frame on the same simulation step.
	//
	// WHAT CHANGED, AND IT CHANGES THE WHOLE SHAPE OF THE BARRIER.
	//
	// The old version of this comment said FFX_MainStep early-returns while a movie
	// owns the frame, so no simulation runs and both machines sit on the same step
	// for the whole movie. That is not true. The early return tests the suspend
	// state object described above, and NO FMV CODE WRITES IT. Four independent
	// things say so:
	//
	//   1. The only writers of all three suspend fields are the action-32 overlay
	//      toggle, the Start-button cutscene pause and the debug free camera.
	//   2. FFX_Frame_StepSuspendOverlay 0x680030 explicitly forces the free camera
	//      field to 0 whenever the script says a movie is running, which is the
	//      opposite of arming a suspend.
	//   3. The script's movie waits are ATEL POLL HANDLERS (Movie library functions
	//      1, 9, 10 and 11). The ATEL VM is stepped from the FFX_MainStep sub-step
	//      loop, which is AFTER that early return. If the step were skipped the
	//      polls would never run and a movie could never finish. This one is
	//      decisive on its own.
	//   4. FFX_StepPacing_AllowCatchUp goes out of its way to refuse frame-skipping
	//      while a movie plays. There would be nothing to refuse if the step were
	//      being thrown away anyway.
	//
	// SO THE SIMULATION RUNS THROUGH A MOVIE, and what is frozen is only the one
	// script that asked for it. The co-op problem is therefore not "both machines
	// are stopped, agree on when to start again". It is narrower and sharper:
	//
	//   both machines run the same script and park it on the same simulation step,
	//   then each one UNPARKS IT on a step decided by its own decode thread, and
	//   from the first step after that the two scripts are permanently offset
	//
	// So the barrier belongs on the SCRIPT WAIT, not on the simulation. Withhold
	// the poll's completion until every machine has agreed, then let it complete on
	// one agreed step. HookFmvWaits below is that mechanism.
	//
	// And the hold byte is exactly the wrong tool here, which is worth saying out
	// loud because it is the obvious first guess. Holding the simulation stops
	// FFX_MainStep, which stops the mod's own lockstep clock, while the machine
	// still playing its movie keeps stepping and keeps advancing its clock. The two
	// clocks then disagree about which step is which, and every input frame
	// afterwards is applied to the wrong step. A pause can use the hold because
	// every machine stops. A movie cannot, because the one that is still playing
	// must not.
	//
	//   start   needs nothing. Movie:0 start and Movie:10 start are ATEL opcodes,
	//           so two lockstepped machines reach them on the same step. The only
	//           asymmetry is a local FFX_Fmv_StartVideo failure, and that one is
	//           not recoverable by any amount of messaging.
	//   end     withhold the script wait until every machine reports done, and
	//           release it on one ordered step.
	//   skip    Start then Square, see RequestFmvSkip. Local input, so it has to
	//           become a networked command or one player watches the rest alone.
	// ---------------------------------------------------------------------------
	struct FmvState
	{
		// 2 means a user-driven suspend overlay owns the frame. It is NOT an FMV
		// signal, see FrameSuspendLayout above. The field keeps the name because
		// "playbackState" is what everyone will search for, and the address file now
		// agrees: Rva::FrameGetSuspendState and Rva::FrameSuspendState.
		int playbackState;
		bool playing;          // the manager's IsPlaying byte, 1744
		bool hasPlayer;        // a player object exists and has not been released
		bool decoderBusy;      // FFX_Fmv_IsDecoderBusy, the real decode queue read
		bool playbackComplete; // the manager's PlaybackComplete byte, 1764
		bool scriptRunning;    // the ATEL script's own view
		bool scriptBlocking;   // the Movie:10 wait is armed, g_ffxFmvScriptBlocking
		bool skipRequested;    // g_ffxFmvSkipRequested, somebody skipped this movie
		bool skipPromptUp;     // Start has put the skip prompt on screen
		bool skipAllowed;      // this movie may be skipped at all
		bool paused;           // the Phyre player's own pause flag
		int videoId;           // -1 if the manager is not readable
		float width;
		float height;
		const char* path; // the resolved file path, NULL if unreadable
	};
	bool ReadFmvState(FmvState* out);

	// True while the manager says a video is playing. This is the manager's own
	// IsPlaying byte and nothing else.
	//
	// IT NO LONGER ORS IN playbackState == 2, because that value turned out to mean
	// "a suspend overlay is up" rather than anything about a movie, so OR-ing it
	// made this return true for an alt-tab with no video anywhere.
	bool FmvPlaying();

	// True while anything about a movie is live on this machine: the manager is
	// playing, the decoder is still draining, or the script thinks a movie is on.
	// This is the honest "do not treat this as plain gameplay" read for an FMV.
	bool FmvInProgress();

	// True while a user-driven suspend overlay owns the frame, which is the only
	// thing that actually makes FFX_MainStep skip the simulation. Named for what it
	// means rather than for what the address file calls it.
	bool FrameSuspended();

	// True on the frame the manager stopped reporting a video, computed by comparing
	// against the previous call. Call it once per frame from one place, or the edge
	// belongs to whoever called last. Returns false until it has been called twice.
	bool FmvJustEnded();

	// The script's own flag, set by the Movie:9 and Movie:10 handlers and cleared by
	// Movie:0, Movie:1 and Movie:4. It changes on an opcode rather than on a
	// decode-thread transition, which is why it is the one that is in step across
	// two machines.
	bool FmvScriptRunning();

	// g_ffxFmvScriptBlocking, set by Movie:10 start ONLY WHEN THE PLAYER WAS
	// ACTUALLY CREATED and cleared by the map-change teardown. False during a
	// Movie:10 wait means this machine failed to open the video, which is the one
	// FMV asymmetry nothing can repair.
	bool FmvScriptBlocking();

	// FFX_Fmv_IsDecoderBusy. Any decode slot still active, or the manager's queue
	// not drained. Three of the four script movie waits consult it.
	bool FmvDecoderBusy();

	// The manager's PlaybackComplete byte, which the decode side sets when the video
	// reaches its end.
	bool FmvPlaybackComplete();

	// The engine's own "this movie is over" test, FFX_Fmv_PlaybackFinished 0x645DC0,
	// replicated as pure reads: the completion byte while a video is playing, and
	// true when none is. This is the predicate the Movie:9 and Movie:10 waits use.
	bool FmvPlaybackFinished();

	// ---------------------------------------------------------------------------
	// SKIPPING A MOVIE
	//
	// Found at FFX_Fmv_PollSkipButtons 0x6D9460, reached from
	// FFX_Fmv_StepFromMainStep 0x645DF0 at the very top of FFX_MainStep. It is TWO
	// presses and not one:
	//
	//   START  (ffx::Btn::Start, 0x800)  arms the on-screen prompt, but only while
	//                                    FmvManager::SkipAllowed is set
	//   SQUARE (ffx::Btn::Square, 0x80)  with the prompt up, performs the skip
	//
	// Both go through FFX_Input__isPressed, which is a newly-pressed edge test on
	// the PC action map. That map is the merged keyboard-and-pad state with no port
	// in it, so the skip is purely local input: in co-op each player would skip
	// their own movie and nobody else's.
	//
	// What the skip actually does, in order: hide the prompt, force FrameIndex and
	// FrameIndexPrev to 65534, seek the Phyre player to its own duration, set
	// PresentationStopped, set g_ffxFmvSkipRequested, and clear IsPlaying unless
	// StopSuppressed is set.
	//
	// WHICH OF THOSE IS LOAD BEARING, because the first guess was wrong and it is
	// worth writing down. Setting g_ffxFmvSkipRequested ALONE DOES NOT END A MOVIE.
	// The flag has exactly five xrefs: the skip sets it, FFX_Fmv_CreatePlayer clears
	// it per movie, and three functions read it.
	//
	//   FFX_Fmv_DrawFrame             reads it and stops drawing. Immediate.
	//   FFX_Fmv_IsFinishedOrSkipped   reads it and returns 1. This is Movie:11's
	//                                 completion test, so Movie:11 does end on the
	//                                 flag alone.
	//   FFX_Fmv_StepPresentation      reads it, but only as the second half of
	//                                 "time < duration && skip == 0", and that
	//                                 whole term is unreachable while
	//                                 PlaybackComplete is 0, because the condition
	//                                 above it short circuits on PlaybackComplete
	//                                 == 0. During normal playback it is 0. So the
	//                                 flag does NOT make the presentation step take
	//                                 its stop branch.
	//
	// Movie:1 tests FFX_Fmv_IsPlaying and FFX_Fmv_IsDecoderBusy, and Movie:9 and
	// Movie:10 test FFX_Fmv_PlaybackFinished, which is "IsPlaying ? PlaybackComplete
	// : 1". None of those read the flag. So the thing that actually ends a movie for
	// three of the four script waits is CLEARING THE MANAGER'S IsPlaying BYTE, which
	// also stops FFX_Fmv_StepFromMainStep from doing anything at all, since that is
	// gated on the same byte.
	//
	// RequestFmvSkip below therefore writes the whole set and not just the flag.
	// ---------------------------------------------------------------------------

	// g_ffxFmvSkipRequested. Cleared per movie by FFX_Fmv_CreatePlayer and set only
	// by the skip handler, so this is both "somebody skipped" and a clean edge to
	// watch for a local skip without hooking anything.
	bool FmvSkipRequested();

	// Make this machine skip the movie it is playing, the way a networked cancel
	// has to. Writes the same fields FFX_Fmv_PollSkipButtons writes when Square is
	// pressed, in the same order, so every one of the four script waits ends the
	// same way it would have for a local press.
	//
	// WHAT IT DOES NOT DO, both deliberately:
	//
	//   The Phyre seek to the player's own duration. That needs two unnamed Phyre
	//   methods on the embedded player at manager+56 whose signatures are only
	//   inferred from one call site, and it changes nothing that matters: clearing
	//   IsPlaying stops FFX_Fmv_StepFromMainStep, so the presentation step is not
	//   called again and the player's time is never read again before teardown.
	//
	//   The prompt-hide draw call sub_6E78C0. The prompt flag is cleared, which is
	//   what FFX_Fmv_DrawFrame reads, and the draw call is a UI object this layer
	//   has no wrapper for.
	//
	// Returns false when there is no manager yet, which is the normal answer early
	// in startup.
	bool RequestFmvSkip();

	// ---------------------------------------------------------------------------
	// THE BARRIER: withholding the script's movie wait
	//
	// The ATEL Movie library's waits are poll handlers, and FFX_Atel_SysFuncPoll
	// calls them as int __cdecl (int actor, int script) through a table of 16-byte
	// entries at g_ffxAtelSysFuncLib11_Movie, laid out [start, poll, resf, resi].
	// The table is in writable .data, so this hooks by replacing four pointers
	// rather than by patching code. A returned 0 has none of the status bits set,
	// which is how the engine's own waits say "not finished, call me again".
	//
	// Four slots are waits:
	//
	//   Movie:1   wait for an already-open movie. THE ONLY ONE WITH SIDE EFFECTS ON
	//             ITS DONE PATH: it clears FmvScriptRunning and calls sub_63DF30. So
	//             this hook never calls it speculatively. It mirrors the engine's
	//             own test with pure reads and calls the real handler only on the
	//             step the gate opens, which is also where those side effects then
	//             happen, identically on both machines.
	//   Movie:9   wait, no side effects.
	//   Movie:10  play and wait, no side effects. Its first test is
	//             g_ffxFmvScriptBlocking, so it reports done immediately on a
	//             machine whose FFX_Fmv_StartVideo failed.
	//   Movie:11  wait, or a skip, no side effects.
	//
	// THE GATE IS CALLED ON EVERY POLL, not only when the wait has finished, and
	// localComplete says which of the two it is. That is deliberate and it is the
	// reason this signature has two arguments:
	//
	//   localComplete == false  the movie is still playing. The return value is
	//                           IGNORED, because the engine is going to keep waiting
	//                           anyway. What the call is for is the TICK: it is the
	//                           only thing in this kit that fires once per
	//                           simulation step for the whole length of a movie,
	//                           which is exactly the window a networked cancel has
	//                           to be agreed in.
	//   localComplete == true   the engine's own wait has finished. Return true to
	//                           let it finish, false to keep the script parked for
	//                           another step.
	//
	// So a gate that always returns true leaves behaviour byte for byte unchanged,
	// and so does a null gate. A null gate means "never withhold", which is how the
	// hook can go in at startup and be armed when a session starts, the rule the
	// rest of this kit follows.
	//
	// Called from the ATEL VM, so from inside the FFX_MainStep sub-step loop, on the
	// game thread. It can fire more than once in a step if the sub-step count is
	// above one, so anything per-step inside it has to guard on the step number.
	// ---------------------------------------------------------------------------
	namespace FmvWait
	{
		const int WaitForOpenMovie = 1; // Movie:1
		const int WaitFullscreen = 9;   // Movie:9
		const int PlayAndWait = 10;     // Movie:10
		const int WaitOrSkip = 11;      // Movie:11
	} // namespace FmvWait

	typedef bool(__cdecl* FmvWaitGateFn)(int movieFunction, bool localComplete);

	bool HookFmvWaits(FmvWaitGateFn gate);
	bool FmvWaitHookInstalled();

	// How many poll calls this hook has turned into "keep waiting", and which slot
	// it last did it for. Diagnostics only.
	unsigned long FmvWaitsWithheld();
	int FmvWaitLastSlot();

	// ---------------------------------------------------------------------------
	// THE CUTSCENE CAMERA
	//
	// One global handle per (class, screen), not per player. A cutscene opens a
	// class 3 Event handle, it outranks the class 1 player camera, and the client
	// sees the host's framing for free as long as the same script runs. So for the
	// stated requirement "the client should see what the host sees" there is
	// nothing to do.
	//
	// Handles are cached forever once opened, so a non-zero entry is NOT a
	// "cutscene is running" test. These readers exist to answer "has an event
	// camera ever been installed on this screen" and for logging.
	// ---------------------------------------------------------------------------
	const int cameraScreenCount = 3;

	struct CameraHandles
	{
		int event[cameraScreenCount];  // handle class 3
		int map[cameraScreenCount];    // handle class 1
		int battle[cameraScreenCount]; // handle class 5
		int viewer[cameraScreenCount]; // handle class 7
		bool matrixOverrideActive;     // the FMV and pre-rendered background path
	};
	bool ReadCameraHandles(CameraHandles* out);

	// True while something has forced the camera matrix, bypassing every mode and
	// every priority. The FMV path does this, so during a movie the camera is not
	// coming from a handle at all.
	bool CameraMatrixOverridden();

	// ---------------------------------------------------------------------------
	// Diagnostics
	//
	// Logs the whole picture once. Worth calling at startup to confirm the bindings
	// landed on something sensible, and during a cutscene on both machines when
	// something has gone out of step.
	// ---------------------------------------------------------------------------
	void LogCutsceneState();

} // namespace ffx
