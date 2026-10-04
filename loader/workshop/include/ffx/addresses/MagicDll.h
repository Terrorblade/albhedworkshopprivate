#pragma once

#include <windows.h>

// The game's own plugin surface: the magic DLLs and the host API table it hands them.
//
// FFX ships 581 magic_NNNN.dll files in magicFiles\FFX (and a second set for FFX-2). Each
// is a native DLL holding one ability's effect code. FFX.exe loads one, calls its
// InitMagicPRX export with a pointer to a 947 slot function pointer table, and the DLL
// calls back through that table for everything it needs.
//
// WHY THE KIT CARES. This is a second, already-shipped extension point, and a lot of
// gameplay lives behind it rather than in the exe. A plugin can read the table to find a
// function it has no name for, and it can replace a slot to intercept what every magic DLL
// does without patching 581 files. Overdrive minigames, for instance, are in the DLLs.
//
//     DWORD* table = (DWORD*)workshop::ModuleAddress(Rva::MagicHostApiTable);
//     void* readPressed = (void*)table[Rva::MagicSlotPadReadPressed16];
//
// Read reversing\MAGIC_DLL.md for the file format, and tools\magicdll.py for a reader.

namespace ffx
{
	namespace Rva
	{

		// ---------------------------------------------------------------------------
		// The host API table
		// ---------------------------------------------------------------------------

		// 947 slots, indices 0..946: 913 function starts, 33 pointers into .data, and
		// one NULL, counted out of the IDB. Slot index * 4 is the byte displacement a
		// DLL uses, which is what a DLL disassembly shows as mov eax, [eax + disp32].
		// Not every slot is a function, so check before you call one. The shipped DLLs
		// copy 32 of the 33 globals into their own storage at init, all but slot 743.
		//
		// 608 slots are actually called by the shipped DLLs, across 74612 call sites.
		// tools/magicdll.py counts them, and its scanner was wrong until 2026-10-04,
		// so any figure quoted before that date is about a third low.
		const DWORD MagicHostApiTable = 0x00864CE8;
		const DWORD MagicHostApiSlots = 947;

		// How the table reaches a DLL: start() passes it to the DLL's InitMagicPRX.
		const DWORD MagicFileStart = 0x005DA7F0;       // passes the table
		const DWORD MagicFileOnDllLoaded = 0x005DB110; // GetProcAddress("InitMagicPRX")
		const DWORD MagicInitMagicPrxPtr = 0x01EFB6A0; // the resolved export pointer

		// Called from inside FFX_MainStep's sub-step loop, so magic DLL overlay code runs
		// a step-deterministic number of times rather than once per presented frame.
		const DWORD MagicCallOverlayStep = 0x00387E00;

		// ---------------------------------------------------------------------------
		// Slot indices worth naming
		// ---------------------------------------------------------------------------

		// The ONLY four pad entries in all 947 slots. Nothing exposes the port state
		// struct, the menu pad block, readWord28, readReleased16 or readAnalogByte, so a
		// magic DLL can reach exactly these.
		const DWORD MagicSlotPadReadButtons16 = 302; // held
		const DWORD MagicSlotPadReadWord10 = 303;    // ring +0x0A, WALL CLOCK derived
		const DWORD MagicSlotPadReadPressed16 = 304; // ring +0x06, newly pressed
		const DWORD MagicSlotPadGetRingLagWindow = 305;

		// Slot 305 is a three byte "mov eax,1; ret". Every caller uses it as
		// lag = 1 - ret, so on PC a DLL's pad reads only ever see the CURRENT ring slot.
		// Ring history only matters for the exe's own minigames, which read lag -1.
		const DWORD PadGetRingLagWindow = 0x00487DC0;

		// Slot 0 is the character allocator, which makes the clone spawner's entry point
		// a shipped code path rather than a guess: 28 DLLs call it at 47 sites. Slot 632
		// reparents a character to a bone, which is how a carried object is held.
		const DWORD MagicSlotChAllocate = 0;
		const DWORD MagicSlotChAttachToParentBone = 632;

		// The battle RNG, and the single biggest determinism hazard behind this table:
		// 92 DLLs draw from it at 274 sites, so an ability that runs on one machine and
		// not the other moves the shared stream.
		const DWORD MagicSlotBtlRand = 324;

		// Globals rather than functions, so read these, do not call them.
		const DWORD MagicSlotEffectUnitPtrs = 743;  // the one global DLLs do NOT copy
		const DWORD MagicSlotMainStepCounter = 764;
		const DWORD MagicSlotInMagicOverlayCall = 827;
		const DWORD MagicSlotIsCatchUpStep = 936;

		// Where overdrive work went. 645..647 are Wakka specific, 648..662 are shared.
		const DWORD MagicSlotWakkaGetStrip = 645;
		const DWORD MagicSlotWakkaGetLevel = 646;
		const DWORD MagicSlotWakkaPostDone = 647;
		const DWORD MagicSlotOdGetTimeRemaining = 651;
		const DWORD MagicSlotWakkaReelsBegin = 656;
		const DWORD MagicSlotOdSetTimeBudget = 657;

		// The route by which most overdrives actually start. The native gate only passes
		// four ability kinds, and everything else, including all of Auron's and all of
		// Wakka's, starts by the DLL calling this slot.
		const DWORD MagicSlotStartMinigame = 630;

		inline const DWORD* MagicDllRvaList(int* count)
		{
			// Slot INDICES are deliberately not in this list. The startup check range
			// checks everything here against the image size, and 302 is not an address.
			static const DWORD list[] = {
				MagicHostApiTable,
				MagicFileStart,
				MagicFileOnDllLoaded,
				MagicInitMagicPrxPtr,
				MagicCallOverlayStep,
				PadGetRingLagWindow,
			};

			if (count)
				*count = (int)(sizeof(list) / sizeof(list[0]));

			return list;
		}
	} // namespace Rva
} // namespace ffx
