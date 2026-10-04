#include "ffx/Cutscene.h"

#include <windows.h>
#include <stdio.h>

// The area files are included directly rather than through ffx/Addresses.h,
// because nothing here needs the rest of the kit and a narrow include keeps this
// translation unit compiling on its own. MainLoop's address file is here because
// the pacing mode, the catch-up step count and the step scale live there and
// this area reads them rather than redefining them. ffx/Input.h is here for
// ffx::Btn::Circle, which is the button a dialogue box is advanced with.
#include "ffx/addresses/Cutscene.h"
#include "ffx/addresses/MainLoop.h"
#include "ffx/Input.h"
#include "workshop/Detour.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

namespace ffx
{
	namespace
	{

		using workshop::Log;
		using workshop::ModuleAddress;
		using workshop::Readable;

		// The layout of one entry in an ATEL syscall library table, which is the
		// same for all 20-odd libraries. Only the poll slot is used here, and the
		// table itself is Rva::AtelMovieLibTable.
		const DWORD atelSysFuncEntryBytes = 16;
		const DWORD atelSysFuncPollSlot = 4;

		// ---------------------------------------------------------------------------
		// Typed access to one global, with the Readable check that keeps a bad read off
		// the frame path. Same three helpers MainLoop.cpp uses, kept local so the two
		// areas do not have to share a private header.
		// ---------------------------------------------------------------------------
		template <typename T>
		T* At(DWORD rva)
		{
			T* p = (T*)ModuleAddress(rva);
			return Readable(p, sizeof(T)) ? p : NULL;
		}

		template <typename T>
		bool ReadGlobal(DWORD rva, T* out)
		{
			const T* p = At<T>(rva);
			if (!p || !out)
				return false;
			*out = *p;
			return true;
		}

		template <typename T>
		T* Field(void* object, DWORD offset)
		{
			if (!object)
				return NULL;
			T* p = (T*)((BYTE*)object + offset);
			return Readable(p, sizeof(T)) ? p : NULL;
		}

		// The object behind a global that holds a pointer, both hops checked.
		BYTE* Singleton(DWORD rva, DWORD bytesNeeded)
		{
			BYTE** slot = At<BYTE*>(rva);
			if (!slot)
				return NULL;
			BYTE* object = *slot;
			if (!Readable(object, bytesNeeded))
				return NULL;
			return object;
		}

		// ---------------------------------------------------------------------------
		// Calling into the game. Only two things here need it, and both degrade to a
		// documented fallback rather than faulting.
		//
		// Deliberately NOT called: FFX_Fmv_GetPlaybackState. It lazily ALLOCATES the
		// 0x340-byte state object when it does not exist, and a read should not have
		// that side effect, so the FMV readers replicate its two-flag logic against the
		// pointer instead and report "no object yet" as state 0.
		// ---------------------------------------------------------------------------
		typedef int(__cdecl* IntNoArgsFn)(void);
		typedef int(__cdecl* IntOneArgFn)(int);

		int closeWindowVerdict = 0;
		int allowCatchUpVerdict = 0;
		int decoderBusyVerdict = 0;

		bool Callable(DWORD rva, int* cachedVerdict, const char* what)
		{
			if (*cachedVerdict == 0)
			{
				*cachedVerdict = workshop::LooksLikeFunctionStart(rva) ? 1 : -1;
				if (*cachedVerdict < 0)
				{
					Log("cutscene: %s at RVA 0x%08X does not look callable, falling back", what, rva);
				}
			}
			return *cachedVerdict > 0;
		}

		// ---------------------------------------------------------------------------
		// The message window records, read directly.
		//
		// Eight records of 44 bytes, double buffered as two 352-byte blocks with a
		// selector byte. The game's own getter does the same arithmetic, and doing it
		// here keeps every dialogue read a pure read, which means they are safe to call
		// from a UI thread as well as from the frame path.
		// ---------------------------------------------------------------------------
		const int mesWinCount = 8;

		BYTE* RecordBase()
		{
			BYTE* records = (BYTE*)ModuleAddress(Rva::AtelMesWinRecords);
			if (!Readable(records, 2 * MesWinRecord::BufferStride))
				return NULL;

			const BYTE* sel = At<BYTE>(Rva::AtelMesWinBufSel);
			// A selector that is anything but 0 or 1 means the engine has not built this
			// yet, or something has scribbled on it. Fall back to buffer 0 rather than
			// indexing off the end.
			const DWORD buffer = (sel && *sel == 1) ? 1u : 0u;
			return records + buffer * MesWinRecord::BufferStride;
		}

		BYTE* Record(int index)
		{
			if (index < 0 || index >= mesWinCount)
				return NULL;
			BYTE* base = RecordBase();
			if (!base)
				return NULL;
			return base + (DWORD)index * MesWinRecord::Stride;
		}

		// FFX_Atel_MesWinBlockingKind 0x86C8B0, replicated. The game's own version is
		// this exact loop, and it is what FFX_StepPacing_AllowCatchUp and
		// FFX_Atel_StepFrame both consult, which is why these three return values are
		// the project's definition of "a dialogue box is up".
		int BlockingKindFromRecords()
		{
			BYTE* base = RecordBase();
			if (!base)
				return dialogueNone;

			int kind = dialogueNone;
			for (int i = 0; i < mesWinCount; ++i)
			{
				const BYTE* w = base + (DWORD)i * MesWinRecord::Stride;
				const WORD state = *(const WORD*)(w + MesWinRecord::State);
				if (state != MesWinState::Shown && state != MesWinState::WaitingOnPlayer)
					continue;

				const BYTE flags = w[MesWinRecord::Flags];
				if ((flags & MesWinFlag::BlocksWait) != 0)
					return dialogueWaiting;
				if (*(const WORD*)(w + MesWinRecord::Attr) != 0 ||
				    (flags & MesWinFlag::BlocksSoft) != 0)
				{
					kind = dialogueOpen;
				}
			}
			return kind;
		}

		// ---------------------------------------------------------------------------
		// The shared menu pad block. Two WORDs out of 0xC0 bytes, which is all this
		// area touches.
		// ---------------------------------------------------------------------------
		WORD* PadField(DWORD offset)
		{
			BYTE* block = (BYTE*)ModuleAddress(Rva::MesWinPadBlock);
			if (!Readable(block, 0xC0))
				return NULL;
			return (WORD*)(block + offset);
		}

		// ---------------------------------------------------------------------------
		// The detour on the message window's pad sampler, which is the hook point for
		// "either player may advance the dialogue".
		//
		// Signature: int __cdecl (void). Hex-Rays reports __thiscall because the
		// function does push ecx for a local and then hands that garbage ecx to
		// FFX_Input__getTimeSeconds, which takes no argument. The call site in
		// FFX_MesWin_StepAll pushes nothing and does no stack cleanup, so cdecl with no
		// arguments is right.
		//
		// The hook runs the original first, so the callback sees a block the game has
		// already filled from port 0, and FFX_MesWin_StepAll walks the eight windows
		// immediately afterwards. That is the only point in the frame where an injected
		// bit is both unclobbered and actually read.
		// ---------------------------------------------------------------------------
		DETOUR_DECLARE(MesWinPad, int, (void));

		// Verified in IDA at 0x8B7CD0. Five bytes, four instructions, no branch and no
		// absolute address among them, so they relocate to the trampoline unchanged and
		// the expected-bytes check is stable across ASLR.
		DETOUR_PROLOGUE(MesWinPad) = {
			0x55,       // push ebp
			0x8B, 0xEC, // mov  ebp, esp
			0x51,       // push ecx
			0x56        // push esi
		};

		DialoguePadFn dialoguePadCallback = NULL;
		volatile LONG dialoguePadSampleCount = 0;

		int __cdecl MesWinPadHook(void)
		{
			const int result = DETOUR_ORIGINAL(MesWinPad)();
			InterlockedIncrement(&dialoguePadSampleCount);
			if (dialoguePadCallback)
				dialoguePadCallback();
			return result;
		}

		// FmvJustEnded's one bit of remembered state. Documented in the header as
		// belonging to whoever calls it, because an edge cannot have two owners.
		bool fmvWasPlaying = false;
		bool fmvEdgeInitialised = false;

		// ---------------------------------------------------------------------------
		// THE FMV BARRIER HOOK.
		//
		// Four pointers in the ATEL Movie library's table, swapped for shims. No code
		// is patched, which is why this needs no prologue bytes and no trampoline: the
		// table is plain writable .data and the original pointer is simply kept.
		//
		// A returned 0 has none of FFX_Atel_SysFuncPoll's status bits set, so the
		// script stays parked on the opcode and the poll is called again next step.
		// That is exactly what the engine's own waits return while a movie plays, so
		// withholding is not a new state for the VM to be in.
		// ---------------------------------------------------------------------------
		typedef int(__cdecl* AtelPollFn)(int actor, int script);

		struct FmvWaitSlot
		{
			int movieFunction;      // the Movie library function index
			AtelPollFn original;    // what the table held before we swapped it
			AtelPollFn replacement; // what we put there
			bool installed;
		};

		FmvWaitGateFn fmvWaitGate = NULL;
		volatile LONG fmvWaitsWithheld = 0;
		volatile LONG fmvWaitLastSlot = -1;

		AtelPollFn* FmvWaitPollSlot(int movieFunction)
		{
			BYTE* table = (BYTE*)ModuleAddress(Rva::AtelMovieLibTable);
			const DWORD offset = (DWORD)movieFunction * atelSysFuncEntryBytes + atelSysFuncPollSlot;
			AtelPollFn* slot = (AtelPollFn*)(table + offset);
			return Readable(slot, sizeof(AtelPollFn)) ? slot : NULL;
		}

		// The one place the gate is asked and the one place the counters move.
		//
		// IT IS ASKED ON EVERY POLL, not only once the wait has finished, and that is
		// not an accident. A lockstep gate needs a per-simulation-step tick for the
		// WHOLE length of a movie, because the thing it has to agree on with the other
		// machine, a cancel, happens while the movie is still playing. There is no
		// other hook in this kit that fires once per step for that window. When
		// localComplete is false the answer is ignored, because the engine is going to
		// keep waiting regardless.
		bool FmvWaitGateAllows(const FmvWaitSlot& slot, bool localComplete)
		{
			if (!fmvWaitGate)
				return true;

			const bool allow = fmvWaitGate(slot.movieFunction, localComplete);
			if (!localComplete)
				return false; // nothing to allow yet, and the caller ignores this

			if (allow)
				return true;

			InterlockedIncrement(&fmvWaitsWithheld);
			InterlockedExchange(&fmvWaitLastSlot, (LONG)slot.movieFunction);
			return false;
		}

		FmvWaitSlot fmvWaitSlots[4];

		// Movie:1. The ONE wait with side effects on its done path, which are clearing
		// the script's movie flag and a call to sub_63DF30, so the original is never
		// called speculatively. This mirrors its own test, which is
		//     if (FFX_Fmv_IsPlaying() || FFX_Fmv_IsDecoderBusy()) return 0;
		// and then calls the real handler only on the step the gate opens, so those
		// side effects happen there and nowhere else.
		int __cdecl FmvWait01Hook(int actor, int script)
		{
			FmvWaitSlot& slot = fmvWaitSlots[0];
			if (!slot.original)
				return 0;

			const bool complete = !FmvPlaying() && !FmvDecoderBusy();
			if (!FmvWaitGateAllows(slot, complete))
				return 0;
			return slot.original(actor, script);
		}

		// Movie:9, Movie:10 and Movie:11 have no side effects on either path, so the
		// original IS the completion test and calling it every step costs nothing and
		// cannot change anything.
		int FmvWaitPassThrough(FmvWaitSlot& slot, int actor, int script)
		{
			if (!slot.original)
				return 0;

			const int result = slot.original(actor, script);
			if (!FmvWaitGateAllows(slot, result != 0))
				return 0;
			return result;
		}

		int __cdecl FmvWait09Hook(int actor, int script)
		{
			return FmvWaitPassThrough(fmvWaitSlots[1], actor, script);
		}

		int __cdecl FmvWait10Hook(int actor, int script)
		{
			return FmvWaitPassThrough(fmvWaitSlots[2], actor, script);
		}

		int __cdecl FmvWait11Hook(int actor, int script)
		{
			return FmvWaitPassThrough(fmvWaitSlots[3], actor, script);
		}

	} // namespace

	// ---------------------------------------------------------------------------
	// Is a cutscene running
	// ---------------------------------------------------------------------------

	bool AtelStepCounter(int* out)
	{
		if (!out)
			return false;
		BYTE* ctx = Singleton(Rva::EventContextPtr, AtelContext::StepCounter + sizeof(int));
		const int* counter = Field<int>(ctx, AtelContext::StepCounter);
		if (!counter)
			return false;
		*out = *counter;
		return true;
	}

	bool ReadCutsceneState(CutsceneState* out)
	{
		if (!out)
			return false;

		CutsceneState s;
		s.dialogueKind = BlockingKindFromRecords();
		s.pacingMode = PacingMode();
		s.fmvPlaybackState = 0;
		s.fmvScriptRunning = false;
		s.fmvPlaying = false;
		s.atelStepCounter = -1;
		s.anyCutsceneSignal = false;

		FmvState fmv;
		if (ReadFmvState(&fmv))
		{
			s.fmvPlaybackState = fmv.playbackState;
			s.fmvScriptRunning = fmv.scriptRunning;
			s.fmvPlaying = fmv.playing;
		}

		AtelStepCounter(&s.atelStepCounter);

		s.anyCutsceneSignal = s.dialogueKind != dialogueNone ||
		                      s.pacingMode == 1 ||
		                      s.fmvPlaybackState == 2 ||
		                      s.fmvScriptRunning ||
		                      s.fmvPlaying;

		*out = s;
		return true;
	}

	bool CutsceneActive()
	{
		CutsceneState s;
		if (!ReadCutsceneState(&s))
			return false;
		return s.anyCutsceneSignal;
	}

	bool GameplayInputShouldBeIgnored()
	{
		// Same four signals. Kept as its own function rather than an alias so the
		// policy can diverge from "is a cutscene running" later without every
		// caller having to change.
		return CutsceneActive();
	}

	// ---------------------------------------------------------------------------
	// The dialogue box
	// ---------------------------------------------------------------------------

	BYTE* MessageWindow(int index)
	{
		return Record(index);
	}

	bool ReadDialogueState(DialogueState* out)
	{
		if (!out)
			return false;

		DialogueState d;
		d.kind = dialogueNone;
		d.windowIndex = -1;
		d.state = MesWinState::Idle;
		d.attr = 0;
		d.flags = 0;
		d.chosenOption = -1;
		d.openWindows = 0;

		BYTE* base = RecordBase();
		if (!base)
		{
			*out = d;
			return false;
		}

		d.kind = BlockingKindFromRecords();

		for (int i = 0; i < mesWinCount; ++i)
		{
			const BYTE* w = base + (DWORD)i * MesWinRecord::Stride;
			const WORD state = *(const WORD*)(w + MesWinRecord::State);
			if (state == MesWinState::Idle)
				continue;

			++d.openWindows;
			if (d.windowIndex >= 0)
				continue;

			d.windowIndex = i;
			d.state = state;
			d.attr = *(const WORD*)(w + MesWinRecord::Attr);
			d.flags = w[MesWinRecord::Flags];
			d.chosenOption = *(const short*)(w + MesWinRecord::ChosenOption);
		}

		*out = d;
		return true;
	}

	bool DialogueOpen()
	{
		return BlockingKindFromRecords() != dialogueNone;
	}

	bool DialogueWaitingForInput()
	{
		return BlockingKindFromRecords() == dialogueWaiting;
	}

	const char* MessageText(int windowIndex)
	{
		BYTE* w = Record(windowIndex);
		if (!w)
			return NULL;
		char** slot = Field<char*>(w, MesWinRecord::TextPtr);
		if (!slot)
			return NULL;
		char* text = *slot;
		// One byte is enough to tell "a real string" from "a stale pointer", and the
		// caller is going to walk it, so anything unreadable has to come back null.
		if (!Readable(text, 1))
			return NULL;
		return text;
	}

	bool CloseDialogue(int windowIndex)
	{
		if (windowIndex < 0 || windowIndex >= mesWinCount)
			return false;
		if (!Callable(Rva::AtelMesWinRequestClose, &closeWindowVerdict, "message window close"))
		{
			return false;
		}
		IntOneArgFn close = (IntOneArgFn)(UINT_PTR)ModuleAddress(Rva::AtelMesWinRequestClose);
		close(windowIndex);
		return true;
	}

	// ---------------------------------------------------------------------------
	// Letting either player advance
	// ---------------------------------------------------------------------------

	bool HookDialoguePad(DialoguePadFn callback)
	{
		dialoguePadCallback = callback;

		if (DETOUR_INSTALLED(MesWinPad))
			return true;
		return DETOUR_INSTALL(MesWinPad, Rva::MesWinSamplePadPort0);
	}

	bool DialoguePadHookInstalled()
	{
		return DETOUR_INSTALLED(MesWinPad);
	}

	bool InjectDialoguePad(WORD pressedMask, WORD heldMask)
	{
		WORD* held = PadField(MesWinPad::Held);
		WORD* pressed = PadField(MesWinPad::Pressed);
		if (!held || !pressed)
			return false;

		// OR rather than assign, so a local press and a remote press in the same
		// step both land and neither cancels the other.
		*held = (WORD)(*held | heldMask);
		*pressed = (WORD)(*pressed | pressedMask);
		return true;
	}

	bool SetDialoguePad(WORD pressedMask, WORD heldMask)
	{
		WORD* held = PadField(MesWinPad::Held);
		WORD* pressed = PadField(MesWinPad::Pressed);
		if (!held || !pressed)
			return false;

		// Assign, deliberately. See the header for why OR is wrong here.
		*held = heldMask;
		*pressed = pressedMask;
		return true;
	}

	bool InjectDialogueConfirm(bool alsoHeld)
	{
		const WORD confirm = (WORD)Btn::Circle;
		return InjectDialoguePad(confirm, alsoHeld ? confirm : (WORD)0);
	}

	bool ReadDialoguePad(WORD* held, WORD* pressed)
	{
		const WORD* h = PadField(MesWinPad::Held);
		const WORD* p = PadField(MesWinPad::Pressed);
		if (!h || !p)
			return false;
		if (held)
			*held = *h;
		if (pressed)
			*pressed = *p;
		return true;
	}

	// ---------------------------------------------------------------------------
	// Pacing and the timing track
	// ---------------------------------------------------------------------------

	int PacingMode()
	{
		int mode = 0;
		if (!ReadGlobal<int>(Rva::PacingMode, &mode))
			return 0;
		return mode;
	}

	const char* PacingModeName()
	{
		switch (PacingMode())
		{
		case 0:
			return "accumulator";
		case 1:
			return "timing track, wall clock";
		default:
			return "unknown";
		}
	}

	bool TimingTrackDriving()
	{
		return PacingMode() == 1;
	}

	bool ReadTimingTrackState(TimingTrackState* out)
	{
		if (!out)
			return false;

		TimingTrackState t;
		t.pacingMode = PacingMode();
		t.trackSelected = false;
		t.mapHasSyncData = false;
		t.loadFinished = false;
		t.loadedMapId = -1;
		t.firstStep = false;
		t.nextTimeMs = 0.0f;
		t.toleranceMs = 0.0f;
		t.dueTimeMs = 0.0f;
		t.elapsedSeconds = 0.0f;
		t.extraStepsThisFrame = 0;
		t.stepScale88 = 0;

		ReadGlobal<float>(Rva::TimingTrackNextTime, &t.nextTimeMs);
		ReadGlobal<float>(Rva::TimingTrackTolerance, &t.toleranceMs);
		ReadGlobal<float>(Rva::TimingTrackDueTime, &t.dueTimeMs);
		ReadGlobal<float>(Rva::TimingTrackElapsed, &t.elapsedSeconds);
		ReadGlobal<int>(Rva::CatchUpExtraSteps, &t.extraStepsThisFrame);
		ReadGlobal<int>(Rva::StepScale88, &t.stepScale88);

		int firstStep = 0;
		if (ReadGlobal<int>(Rva::TimingTrackFirstStep, &firstStep))
			t.firstStep = firstStep != 0;

		BYTE* mgr = Singleton(Rva::SyncDataMgr, SyncDataLayout::SyncListArray + sizeof(void*));
		if (mgr)
		{
			void** cursor = Field<void*>(mgr, SyncDataLayout::TrackCursor);
			const int* hasData = Field<int>(mgr, SyncDataLayout::MapHasSyncData);
			const int* done = Field<int>(mgr, SyncDataLayout::LoadFinished);
			const int* mapId = Field<int>(mgr, SyncDataLayout::LoadedMapId);
			if (cursor)
				t.trackSelected = *cursor != NULL;
			if (hasData)
				t.mapHasSyncData = *hasData != 0;
			if (done)
				t.loadFinished = *done != 0;
			if (mapId)
				t.loadedMapId = *mapId;
		}

		*out = t;
		return true;
	}

	bool AddTimingTrackPauseMs(DWORD milliseconds)
	{
		if (milliseconds == 0)
			return true;

		// The engine stores this as a 64-bit millisecond count and FFX_StepPacing
		// folds it in and zeroes it on the next mode 1 step. Adding rather than
		// overwriting means a gate stall and an Esc-menu pause in the same frame
		// cannot lose one another.
		LONGLONG* pending = At<LONGLONG>(Rva::TimingTrackPauseMsPending);
		if (!pending)
			return false;
		*pending = *pending + (LONGLONG)milliseconds;
		return true;
	}

	bool TimingTrackPauseMs(DWORD* pending, DWORD* total)
	{
		const LONGLONG* p = At<LONGLONG>(Rva::TimingTrackPauseMsPending);
		const LONGLONG* t = At<LONGLONG>(Rva::TimingTrackPauseMsTotal);
		if (!p || !t)
			return false;
		if (pending)
			*pending = (DWORD)*p;
		if (total)
			*total = (DWORD)*t;
		return true;
	}

	bool CatchUpAllowed()
	{
		if (!Callable(Rva::StepPacingAllowCatchUp, &allowCatchUpVerdict, "allow catch-up"))
		{
			// The honest fallback is "yes", because that is what the engine does on
			// every ordinary field map and a false negative here would make a caller
			// think the game is in a special scene when it is not.
			return true;
		}
		IntNoArgsFn allow = (IntNoArgsFn)(UINT_PTR)ModuleAddress(Rva::StepPacingAllowCatchUp);
		return allow() != 0;
	}

	// ---------------------------------------------------------------------------
	// FMV
	// ---------------------------------------------------------------------------

	bool ReadFmvState(FmvState* out)
	{
		if (!out)
			return false;

		FmvState f;
		f.playbackState = 0;
		f.playing = false;
		f.hasPlayer = false;
		f.decoderBusy = false;
		f.playbackComplete = false;
		f.scriptRunning = false;
		f.scriptBlocking = false;
		f.skipRequested = false;
		f.skipPromptUp = false;
		f.skipAllowed = false;
		f.paused = false;
		f.videoId = -1;
		f.width = 0.0f;
		f.height = 0.0f;
		f.path = NULL;

		int script = 0;
		if (ReadGlobal<int>(Rva::FmvScriptRunning, &script))
			f.scriptRunning = script != 0;

		int blocking = 0;
		if (ReadGlobal<int>(Rva::FmvScriptBlocking, &blocking))
			f.scriptBlocking = blocking != 0;

		BYTE skip = 0;
		if (ReadGlobal<BYTE>(Rva::FmvSkipRequested, &skip))
			f.skipRequested = skip != 0;

		// The suspend-state function replicated rather than called, because the
		// game's version lazily allocates the object and a read must not do that. No
		// object yet means no overlay is up, which is state 0. And remember what this
		// value actually means: a user-driven suspend, not a movie. See the header.
		BYTE* state = Singleton(Rva::FrameSuspendState, FrameSuspendLayout::Cutscene + 1);
		if (state)
		{
			const BYTE* overlay = Field<BYTE>(state, FrameSuspendLayout::Overlay);
			const BYTE* cutscene = Field<BYTE>(state, FrameSuspendLayout::Cutscene);
			const int* value = Field<int>(state, FrameSuspendLayout::State);
			if (overlay && cutscene && (*overlay != 0 || *cutscene != 0))
			{
				f.playbackState = 2;
			}
			else if (value)
			{
				f.playbackState = *value;
			}
		}

		BYTE* mgr = Singleton(Rva::FmvPlayerManager, FmvManager::SkipForbidden + 1);
		if (mgr)
		{
			const BYTE* playing = Field<BYTE>(mgr, FmvManager::IsPlaying);
			const BYTE* hasPlayer = Field<BYTE>(mgr, FmvManager::HasPlayer);
			const BYTE* complete = Field<BYTE>(mgr, FmvManager::PlaybackComplete);
			const BYTE* paused = Field<BYTE>(mgr, FmvManager::Paused);
			const BYTE* promptUp = Field<BYTE>(mgr, FmvManager::SkipPromptUp);
			const BYTE* skippable = Field<BYTE>(mgr, FmvManager::SkipAllowed);
			const BYTE* forbidden = Field<BYTE>(mgr, FmvManager::SkipForbidden);
			const int* videoId = Field<int>(mgr, FmvManager::VideoId);
			const float* w = Field<float>(mgr, FmvManager::WidthF);
			const float* h = Field<float>(mgr, FmvManager::HeightF);
			if (playing)
				f.playing = *playing != 0;
			if (hasPlayer)
				f.hasPlayer = *hasPlayer != 0;
			if (complete)
				f.playbackComplete = *complete != 0;
			if (paused)
				f.paused = *paused != 0;
			if (promptUp)
				f.skipPromptUp = *promptUp != 0;
			if (skippable && forbidden)
				f.skipAllowed = *skippable != 0 && *forbidden == 0;
			if (videoId)
				f.videoId = *videoId;
			if (w)
				f.width = *w;
			if (h)
				f.height = *h;

			const char* path = (const char*)(mgr + FmvManager::FilePath);
			if (Readable(path, 1) && path[0] != '\0')
				f.path = path;
		}

		// The real decode-queue read this time, rather than the "has a player and is
		// playing" stand-in the first cut of this used. Three of the four script
		// movie waits consult exactly this function, so a barrier that wants to agree
		// with the engine about when a movie is over has to ask the same question.
		f.decoderBusy = FmvDecoderBusy();

		*out = f;
		return true;
	}

	bool FmvPlaying()
	{
		BYTE* mgr = Singleton(Rva::FmvPlayerManager, FmvManager::IsPlaying + 1);
		const BYTE* playing = Field<BYTE>(mgr, FmvManager::IsPlaying);
		return playing && *playing != 0;
	}

	bool FrameSuspended()
	{
		FmvState f;
		if (!ReadFmvState(&f))
			return false;
		return f.playbackState == 2;
	}

	bool FmvDecoderBusy()
	{
		// Called rather than replicated. It walks the manager's decode slot array,
		// whose count and base live at manager+4 and manager+0xC, and it finishes with
		// a test of manager+0x704 whose sense is inverted in a way nobody has
		// explained. Reproducing that from the disassembly would be guessing at two
		// fields, and three of the four script movie waits consult this exact
		// function, so asking the engine is also the only way to be sure the barrier
		// agrees with it.
		//
		// THE ONE REAL HAZARD, and it is why the manager is checked first:
		// FFX_Fmv_IsDecoderBusy hands FFX_Fmv_GetManager's result straight to a
		// __thiscall that dereferences it with no null check of its own, and
		// FFX_Fmv_GetManager returns 0 with a TTY warning before the singleton exists.
		// So calling this early would fault. No manager means no movie, which is not
		// busy.
		BYTE* mgr = Singleton(Rva::FmvPlayerManager, FmvManager::SkipForbidden + 1);
		if (!mgr)
			return false;

		if (!Callable(Rva::FmvIsDecoderBusy, &decoderBusyVerdict, "fmv decoder busy"))
		{
			// The honest fallback is "not busy", because the alternative would make
			// every barrier built on this read wait forever.
			return false;
		}
		IntNoArgsFn busy = (IntNoArgsFn)(UINT_PTR)ModuleAddress(Rva::FmvIsDecoderBusy);
		return busy() != 0;
	}

	bool FmvPlaybackComplete()
	{
		BYTE* mgr = Singleton(Rva::FmvPlayerManager, FmvManager::PlaybackComplete + 1);
		const BYTE* complete = Field<BYTE>(mgr, FmvManager::PlaybackComplete);
		return complete && *complete != 0;
	}

	bool FmvPlaybackFinished()
	{
		// FFX_Fmv_PlaybackFinished 0x645DC0, replicated as the two reads it is:
		//     return mgr[IsPlaying] ? mgr[PlaybackComplete] : 1;
		// Replicated rather than called because it is two byte reads and calling it
		// would mean parking another address.
		BYTE* mgr = Singleton(Rva::FmvPlayerManager, FmvManager::PlaybackComplete + 1);
		if (!mgr)
			return true; // no manager means no movie, which reads as finished

		const BYTE* playing = Field<BYTE>(mgr, FmvManager::IsPlaying);
		if (!playing || *playing == 0)
			return true;

		const BYTE* complete = Field<BYTE>(mgr, FmvManager::PlaybackComplete);
		return complete && *complete != 0;
	}

	bool FmvInProgress()
	{
		if (FmvScriptRunning() || FmvScriptBlocking())
			return true;
		if (FmvPlaying())
			return true;
		return FmvDecoderBusy();
	}

	bool FmvJustEnded()
	{
		const bool now = FmvPlaying();
		if (!fmvEdgeInitialised)
		{
			fmvEdgeInitialised = true;
			fmvWasPlaying = now;
			return false;
		}
		const bool ended = fmvWasPlaying && !now;
		fmvWasPlaying = now;
		return ended;
	}

	bool FmvScriptRunning()
	{
		int script = 0;
		if (!ReadGlobal<int>(Rva::FmvScriptRunning, &script))
			return false;
		return script != 0;
	}

	bool FmvScriptBlocking()
	{
		int blocking = 0;
		if (!ReadGlobal<int>(Rva::FmvScriptBlocking, &blocking))
			return false;
		return blocking != 0;
	}

	bool FmvSkipRequested()
	{
		BYTE skip = 0;
		if (!ReadGlobal<BYTE>(Rva::FmvSkipRequested, &skip))
			return false;
		return skip != 0;
	}

	bool RequestFmvSkip()
	{
		BYTE* flag = At<BYTE>(Rva::FmvSkipRequested);
		if (!flag)
			return false;

		// Both the global and the manager live in writable .data, so there is nothing
		// to unprotect, and FFX_Fmv_CreatePlayer resets all of this for the next
		// movie, so none of it has to be undone.
		BYTE* mgr = Singleton(Rva::FmvPlayerManager, FmvManager::SkipForbidden + 1);
		if (!mgr)
		{
			// No manager means no movie, which makes this a no-op rather than a
			// failure, but say false so a caller that was expecting to end something
			// can log it.
			return false;
		}

		// The same writes FFX_Fmv_PollSkipButtons does on the Square press, in the
		// same order. See the header for which of them actually ends a movie: the
		// short version is that the flag on its own only ends Movie:11, and clearing
		// IsPlaying is what ends the other three.
		int* frameIndex = Field<int>(mgr, FmvManager::FrameIndex);
		int* framePrev = Field<int>(mgr, FmvManager::FrameIndexPrev);
		BYTE* stopped = Field<BYTE>(mgr, FmvManager::PresentationStopped);
		BYTE* suppressed = Field<BYTE>(mgr, FmvManager::StopSuppressed);
		BYTE* playing = Field<BYTE>(mgr, FmvManager::IsPlaying);
		BYTE* prompt = Field<BYTE>(mgr, FmvManager::SkipPromptUp);

		if (!frameIndex || !framePrev || !stopped || !suppressed || !playing || !prompt)
			return false;

		*framePrev = 65534;
		*frameIndex = 65534;
		*stopped = 1;
		*flag = 1;

		// The engine honours this guard, so this does too, even though nothing in the
		// binary ever sets the byte. If it ever were set, the real button path would
		// leave the movie playing as well, so matching it is the honest choice.
		if (*suppressed == 0)
			*playing = 0;

		*prompt = 0;
		return true;
	}

	// ---------------------------------------------------------------------------
	// The barrier hook
	// ---------------------------------------------------------------------------

	bool HookFmvWaits(FmvWaitGateFn gate)
	{
		fmvWaitGate = gate;

		if (FmvWaitHookInstalled())
			return true;

		// Filled here rather than as a static initialiser so the function pointers
		// and the slot numbers are written next to each other and cannot drift.
		fmvWaitSlots[0].movieFunction = FmvWait::WaitForOpenMovie;
		fmvWaitSlots[0].replacement = &FmvWait01Hook;
		fmvWaitSlots[1].movieFunction = FmvWait::WaitFullscreen;
		fmvWaitSlots[1].replacement = &FmvWait09Hook;
		fmvWaitSlots[2].movieFunction = FmvWait::PlayAndWait;
		fmvWaitSlots[2].replacement = &FmvWait10Hook;
		fmvWaitSlots[3].movieFunction = FmvWait::WaitOrSkip;
		fmvWaitSlots[3].replacement = &FmvWait11Hook;

		// All four or none. A half-installed barrier would withhold some of the
		// script's movie waits and not others, which is worse than withholding none
		// of them: the two machines would then disagree about which waits are gated.
		for (int i = 0; i < 4; ++i)
		{
			AtelPollFn* slot = FmvWaitPollSlot(fmvWaitSlots[i].movieFunction);
			if (!slot)
			{
				Log("cutscene: the ATEL Movie library table is not readable at RVA "
				    "0x%08X, so the FMV barrier cannot be installed",
				    Rva::AtelMovieLibTable);
				return false;
			}
			if (*slot == NULL)
			{
				Log("cutscene: Movie:%d has no poll handler registered, so this is not "
				    "the analysed build and the FMV barrier is not being installed",
				    fmvWaitSlots[i].movieFunction);
				return false;
			}
			if (*slot == fmvWaitSlots[i].replacement)
			{
				Log("cutscene: Movie:%d already points at our shim, which should be "
				    "impossible, refusing rather than losing the original",
				    fmvWaitSlots[i].movieFunction);
				return false;
			}
			fmvWaitSlots[i].original = *slot;
		}

		// Second pass writes, so nothing is swapped unless every original was
		// captured. A pointer write to .data is atomic on x86 and the VM reads the
		// slot fresh on every poll, so there is no window where a half-written
		// pointer could be called.
		for (int i = 0; i < 4; ++i)
		{
			AtelPollFn* slot = FmvWaitPollSlot(fmvWaitSlots[i].movieFunction);
			*slot = fmvWaitSlots[i].replacement;
			fmvWaitSlots[i].installed = true;
		}

		Log("cutscene: FMV barrier installed on Movie:1, Movie:9, Movie:10 and "
		    "Movie:11 poll handlers. With no gate armed every wait completes exactly "
		    "as the shipped game's does.");
		return true;
	}

	bool FmvWaitHookInstalled()
	{
		return fmvWaitSlots[0].installed && fmvWaitSlots[1].installed &&
		       fmvWaitSlots[2].installed && fmvWaitSlots[3].installed;
	}

	unsigned long FmvWaitsWithheld()
	{
		return (unsigned long)fmvWaitsWithheld;
	}

	int FmvWaitLastSlot()
	{
		return (int)fmvWaitLastSlot;
	}

	// ---------------------------------------------------------------------------
	// The cutscene camera
	// ---------------------------------------------------------------------------

	bool ReadCameraHandles(CameraHandles* out)
	{
		if (!out)
			return false;

		CameraHandles c;
		for (int i = 0; i < cameraScreenCount; ++i)
		{
			c.event[i] = 0;
			c.map[i] = 0;
			c.battle[i] = 0;
			c.viewer[i] = 0;
		}
		c.matrixOverrideActive = false;

		const SIZE_T arrayBytes = (SIZE_T)cameraScreenCount * sizeof(int);

		const int* ev = (const int*)ModuleAddress(Rva::CameEventHandles);
		const int* mp = (const int*)ModuleAddress(Rva::CameMapHandles);
		const int* bt = (const int*)ModuleAddress(Rva::CameBattleHandles);
		const int* vw = (const int*)ModuleAddress(Rva::CameViewerHandles);

		if (Readable(ev, arrayBytes) && Readable(mp, arrayBytes) &&
		    Readable(bt, arrayBytes) && Readable(vw, arrayBytes))
		{
			for (int i = 0; i < cameraScreenCount; ++i)
			{
				c.event[i] = ev[i];
				c.map[i] = mp[i];
				c.battle[i] = bt[i];
				c.viewer[i] = vw[i];
			}
		}

		c.matrixOverrideActive = CameraMatrixOverridden();

		*out = c;
		return true;
	}

	bool CameraMatrixOverridden()
	{
		int flag = 0;
		if (!ReadGlobal<int>(Rva::CameOverrideFlag, &flag))
			return false;
		return flag != 0;
	}

	// ---------------------------------------------------------------------------
	// Diagnostics
	// ---------------------------------------------------------------------------

	void LogCutsceneState()
	{
		CutsceneState s;
		if (!ReadCutsceneState(&s))
		{
			Log("cutscene: nothing readable yet, the game has probably not built the event system");
			return;
		}

		Log("cutscene: dialogue %d (%s)  pacing %d (%s)  fmv state %d  atel step %d",
		    s.dialogueKind,
		    s.dialogueKind == dialogueWaiting ? "waiting for a button"
		    : s.dialogueKind == dialogueOpen  ? "a box is up"
		                                      : "none",
		    s.pacingMode, PacingModeName(), s.fmvPlaybackState, s.atelStepCounter);

		DialogueState d;
		if (ReadDialogueState(&d))
		{
			const char* text = d.windowIndex >= 0 ? MessageText(d.windowIndex) : NULL;
			char preview[32];
			preview[0] = '\0';
			if (text)
			{
				// The message bytes are the event package's own encoding, not plain
				// ASCII, so this is a sanity peek and not a readable string.
				_snprintf_s(preview, sizeof(preview), _TRUNCATE, "%02X %02X %02X %02X",
				    (unsigned)(BYTE)text[0], (unsigned)(BYTE)text[1],
				    (unsigned)(BYTE)text[2], (unsigned)(BYTE)text[3]);
			}
			Log("cutscene: windows open %d  first %d  state %u  attr 0x%04X  flags 0x%02X  "
			    "chosen %d  first bytes %s",
			    d.openWindows, d.windowIndex, (unsigned)d.state, (unsigned)d.attr,
			    (unsigned)d.flags, (int)d.chosenOption, preview[0] ? preview : "none");
		}

		WORD held = 0, pressed = 0;
		if (ReadDialoguePad(&held, &pressed))
		{
			Log("cutscene: pad block held 0x%04X pressed 0x%04X  confirm %s  "
			    "samples %lu  hook %s",
			    (unsigned)held, (unsigned)pressed,
			    (pressed & Btn::Circle) ? "PRESSED" : "no",
			    (unsigned long)dialoguePadSampleCount,
			    DialoguePadHookInstalled() ? "installed" : "not installed");
		}

		TimingTrackState t;
		if (ReadTimingTrackState(&t))
		{
			Log("cutscene: track selected %d  map %d has data %d loaded %d  first step %d",
			    (int)t.trackSelected, t.loadedMapId, (int)t.mapHasSyncData,
			    (int)t.loadFinished, (int)t.firstStep);
			Log("cutscene: track due %.1f ms  next %.1f ms  tol %.1f ms  elapsed %.3f s  "
			    "extra steps %d  step scale 0x%X",
			    (double)t.dueTimeMs, (double)t.nextTimeMs, (double)t.toleranceMs,
			    (double)t.elapsedSeconds, t.extraStepsThisFrame, (unsigned)t.stepScale88);
		}

		DWORD pausePending = 0, pauseTotal = 0;
		if (TimingTrackPauseMs(&pausePending, &pauseTotal))
		{
			Log("cutscene: track pause pending %lu ms  total %lu ms",
			    (unsigned long)pausePending, (unsigned long)pauseTotal);
		}

		FmvState f;
		if (ReadFmvState(&f))
		{
			Log("cutscene: fmv playing %d player %d complete %d decoder %d paused %d  "
			    "video %d  %.0fx%.0f  %s",
			    (int)f.playing, (int)f.hasPlayer, (int)f.playbackComplete,
			    (int)f.decoderBusy, (int)f.paused, f.videoId, (double)f.width,
			    (double)f.height, f.path ? f.path : "no path");
			Log("cutscene: fmv script %d blocking %d  skip requested %d prompt %d "
			    "allowed %d  wait hook %s, withheld %lu, last slot %d",
			    (int)f.scriptRunning, (int)f.scriptBlocking, (int)f.skipRequested,
			    (int)f.skipPromptUp, (int)f.skipAllowed,
			    FmvWaitHookInstalled() ? "installed" : "not installed",
			    FmvWaitsWithheld(), FmvWaitLastSlot());
			Log("cutscene: frame suspend state %d. That is the action-32 overlay, the "
			    "Start-button cutscene pause or the debug camera, and NOT an FMV. "
			    "While it is 2 FFX_MainStep skips the simulation.",
			    f.playbackState);
		}

		CameraHandles c;
		if (ReadCameraHandles(&c))
		{
			Log("cutscene: camera event %08X %08X %08X  map %08X %08X %08X  override %d",
			    (unsigned)c.event[0], (unsigned)c.event[1], (unsigned)c.event[2],
			    (unsigned)c.map[0], (unsigned)c.map[1], (unsigned)c.map[2],
			    (int)c.matrixOverrideActive);
		}

		Log("cutscene: catch-up allowed %d", (int)CatchUpAllowed());
	}

} // namespace ffx
