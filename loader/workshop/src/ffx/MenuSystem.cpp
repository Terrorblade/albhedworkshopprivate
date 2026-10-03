#include "ffx/MenuSystem.h"

#include <windows.h>
#include <string.h>

// The area address files directly rather than through Addresses.h, so this
// compiles whether or not Addresses.h has been wired up yet.
//
// addresses\MainLoop.h is included because it already owns the two menu running
// flags and MenuOpenPending, and redeclaring an address in two places is how a
// pair of constants quietly drift apart. GameState.h is included for kCharNone,
// kCharCount and CharacterName, so the "no character" value this file returns is
// literally the same constant the rest of the kit uses.
#include "ffx/addresses/MenuSystem.h"
#include "ffx/addresses/MainLoop.h"
#include "ffx/GameState.h"
#include "workshop/Detour.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

// Implementation notes worth reading before changing anything here.
//
// Three different kinds of memory are touched and they deserve different levels
// of paranoia:
//
//   statics    the pad block, the module table, the character cursor and the
//              virtual pad. All in .data, always mapped. The Readable checks on
//              them are belt and braces and cost a page query.
//   descriptors also .data, but reached through the module table, whose .data
//              initialiser is 0xFFFFFFFF. So every table read has to reject 0
//              AND -1 before dereferencing. That sentinel is the one real trap
//              in this file.
//   work block a heap pointer that is null until the menu builds itself, and
//              the offsets reach 71340, so the check has to cover the whole
//              span rather than a pointer-sized peek.
//
// Reads are volatile because the game thread writes them behind us.

namespace ffx
{

	using workshop::Log;
	using workshop::ModuleAddress;
	using workshop::Readable;

	namespace
	{

		// ---------------------------------------------------------------------------
		// Typed access to one global, with the Readable check that keeps a bad read off
		// the frame path.
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
			*out = *(volatile const T*)p;
			return true;
		}

		inline BYTE Rd8(const BYTE* p)
		{
			return *(volatile const BYTE*)p;
		}
		inline WORD Rd16(const BYTE* p)
		{
			return *(volatile const WORD*)p;
		}
		inline DWORD Rd32(const BYTE* p)
		{
			return *(volatile const DWORD*)p;
		}

		inline void Wr8(BYTE* p, BYTE v)
		{
			*(volatile BYTE*)p = v;
		}
		inline void Wr16(BYTE* p, WORD v)
		{
			*(volatile WORD*)p = v;
		}
		inline void Wr32(BYTE* p, DWORD v)
		{
			*(volatile DWORD*)p = v;
		}

		// The pad block base, checked once for the whole 192 bytes.
		BYTE* PadBlock()
		{
			BYTE* p = (BYTE*)ModuleAddress(Rva::MenuPadBlock);
			return Readable(p, MenuPadBlock::Size) ? p : NULL;
		}

		// The menu work block. A heap pointer, so two checks: the slot, then the object.
		// The largest offset anything here touches is MenuWork::DirtyFlag, so one span
		// check covers every accessor below.
		const DWORD kMenuWorkSpan = MenuWork::DirtyFlag + sizeof(int);

		BYTE* MenuWorkBlock()
		{
			BYTE** slot = At<BYTE*>(Rva::MenuWork);
			if (!slot)
				return NULL;
			// "BYTE *volatile *" rather than "volatile BYTE **": the POINTER is what the
			// game thread rewrites, the bytes behind it are read volatile separately.
			BYTE* work = *(BYTE* volatile*)slot;
			if (!Readable(work, kMenuWorkSpan))
				return NULL;
			return work;
		}

		// The module table's .data initialiser. A slot that still holds this has never
		// been registered, and dereferencing it would fault on a near-null address.
		const DWORD kModuleSlotEmpty = 0xFFFFFFFFu;

		bool ValidModuleId(int id)
		{
			return id >= 0 && id < kMenuModuleCount;
		}

		// ---------------------------------------------------------------------------
		// Calling into the game. Only two answers here need a game call, so the
		// prologue is checked once and the verdict cached. A failed check degrades to a
		// documented fallback rather than faulting.
		// ---------------------------------------------------------------------------
		typedef int(__cdecl* RequestOpenFn)(int mode);

		int requestOpenVerdict = 0;

		bool CallableVerdict(DWORD rva, int* cached, const char* what)
		{
			if (*cached == 0)
			{
				*cached = workshop::LooksLikeFunctionStart(rva) ? 1 : -1;
				if (*cached < 0)
				{
					Log("menu: %s at RVA 0x%08X does not look callable, refusing to call it",
					    what, rva);
				}
			}
			return *cached > 0;
		}

		// ---------------------------------------------------------------------------
		// The detour on FFX_MenuSys_SamplePad.
		//
		// Verified in IDA at 0x8BE500. Five bytes, four instructions, no branch and no
		// absolute address among them, so they relocate to the trampoline unchanged.
		//
		//   55        push ebp
		//   8B EC     mov  ebp, esp
		//   51        push ecx
		//   56        push esi
		//
		// The function takes no real argument. Hex-Rays types it __thiscall because it
		// hands ecx to FFX_Input__getTimeSeconds, which ignores it, and the call site in
		// FFX_MenuSys_StepFrame happens to leave a byte value there. Nothing after the
		// call reads ecx, so __cdecl is safe. Checked instruction by instruction.
		// ---------------------------------------------------------------------------
		DETOUR_DECLARE(MenuSamplePad, void, (void));

		DETOUR_PROLOGUE(MenuSamplePad) = {
			0x55,       // push ebp
			0x8B, 0xEC, // mov  ebp, esp
			0x51,       // push ecx
			0x56        // push esi
		};

		MenuPadObserverFn padObserver = NULL;
		volatile LONG padSampleCount = 0;

		void __cdecl MenuSamplePadHook(void)
		{
			// Always run the original first. The block then holds whatever the local pad
			// produced, including the stick-to-dpad synthesis and the hold counters,
			// which is both what a driver wants to transmit and a sane base for a
			// passenger to overwrite.
			DETOUR_ORIGINAL(MenuSamplePad)();
			InterlockedIncrement(&padSampleCount);
			if (padObserver)
				padObserver();
		}

		// ---------------------------------------------------------------------------
		// Module names. Only the proved ones get a real name, see MENU_SYSTEM.md for
		// what proved each. An unproved screen name is worse than no name, because
		// somebody will build an ownership rule on it.
		// ---------------------------------------------------------------------------
		struct ModuleNameEntry
		{
			int id;
			const char* name;
		};

		const ModuleNameEntry kModuleNames[] = {
			{ kMenuModuleBoot, "boot and title" },
			{ kMenuModuleMain, "main menu" },
			{ kMenuModuleAbility, "abilities" },
			{ kMenuModuleCustomise, "customise" },
			{ kMenuModuleEquip, "equip" },
			{ kMenuModuleItem, "items" },
			{ kMenuModuleConfig, "config" },
			{ kMenuModuleSaveFront, "save frontend" },
			{ kMenuModuleShop, "shop" },
			{ kMenuModuleShopAlt, "shop (alt)" },
			{ kMenuModuleMovie, "movie" },
			{ kMenuModuleSaveLoadA, "save and load" },
			{ kMenuModuleSaveLoadB, "save and load (alt)" },
			{ kMenuModuleSound, "sound" },
			{ kMenuModuleSphereGrid, "sphere grid" },
			{ kMenuModuleOverdrive, "overdrive mode" },
			{ kMenuModuleDevTest, "dev test harness" }
		};

		// Which module wins when several are active. A submenu suspends the main menu
		// rather than stopping it, so the main menu's bit stays set the whole time and
		// picking the lowest set bit would always answer "main menu". This order puts
		// the specific screens first.
		const int kScreenPriority[] = {
			kMenuModuleSphereGrid, kMenuModuleEquip, kMenuModuleItem,
			kMenuModuleAbility, kMenuModuleCustomise, kMenuModuleOverdrive,
			kMenuModuleConfig, kMenuModuleSaveLoadA, kMenuModuleSaveLoadB,
			kMenuModuleSaveFront, kMenuModuleShop, kMenuModuleShopAlt,
			kMenuModuleUnknown6, kMenuModuleUnknown9, kMenuModuleUnknown20,
			kMenuModuleUnknown23, kMenuModuleMovie, kMenuModuleSound,
			kMenuModuleDevTest, kMenuModuleMain, kMenuModuleBoot
		};

		// The main menu's cursor result to module map, read off
		// FFX_Menu_StartSubmoduleByResult 0x8E24C0.
		struct ResultMapEntry
		{
			int result;
			int module;
		};

		const ResultMapEntry kMainMenuResultMap[] = {
			{ 0, kMenuModuleCustomise },
			{ 1, kMenuModuleAbility },
			{ 2, kMenuModuleEquip },
			{ 4, kMenuModuleUnknown6 },
			{ 5, kMenuModuleItem },
			{ 7, kMenuModuleUnknown9 },
			{ 8, kMenuModuleConfig },
			{ 10, kMenuModuleSaveLoadA },
			{ 15, kMenuModuleOverdrive },
			{ 20, kMenuModuleUnknown20 }
		};

		const int kDpadBits = 0xF000; // ffx::Btn::Dpad, repeated here to avoid the include

		// g_ffxMenuCharList is EIGHT BYTES, 0x1841C14 to 0x1841C1B, with the first of
		// its two counts sitting immediately after at 0x1841C1C. Bounding this at
		// kCharCount (18) would read the counts as character indices, so the capacity is
		// its own constant and every list read is clamped to it.
		const int kMenuCharListCapacity = 8;

	} // namespace

	// ---------------------------------------------------------------------------
	// Is the menu up, and which screen
	// ---------------------------------------------------------------------------

	bool MenuSystemRunning()
	{
		// Both flags, because FFX_MenuSys_PollOpenAndStep and FFX_MenuSys_Enter both
		// test them as a pair and either one being set means the menu system is up.
		// The addresses come from addresses\MainLoop.h, which already owns them.
		int running = 0;
		bool any = false;
		if (ReadGlobal<int>(Rva::MenuSysRunning, &running) && running != 0)
			any = true;
		if (ReadGlobal<int>(Rva::MenuSysRunningAlt, &running) && running != 0)
			any = true;
		return any;
	}

	DWORD ActiveModuleMask()
	{
		DWORD mask = 0;
		return ReadGlobal<DWORD>(Rva::ModuleActiveMask, &mask) ? mask : 0;
	}

	DWORD SuspendedModuleMask()
	{
		DWORD mask = 0;
		return ReadGlobal<DWORD>(Rva::ModuleSuspendMask, &mask) ? mask : 0;
	}

	bool ModuleActive(int id)
	{
		if (!ValidModuleId(id))
			return false;
		return (ActiveModuleMask() & (1u << id)) != 0;
	}

	BYTE* ModuleDescriptor(int id)
	{
		if (!ValidModuleId(id))
			return NULL;

		const DWORD* table = (const DWORD*)ModuleAddress(Rva::ModuleTable);
		if (!Readable(table, kMenuModuleCount * sizeof(DWORD)))
			return NULL;

		const DWORD slot = *(volatile const DWORD*)(table + id);
		if (slot == 0 || slot == kModuleSlotEmpty)
			return NULL;
		if (!workshop::LooksLikePointer(slot))
			return NULL;

		BYTE* desc = (BYTE*)slot;
		if (!Readable(desc, ModuleDesc::State + sizeof(int)))
			return NULL;
		return desc;
	}

	bool ModuleStepping(int id)
	{
		const BYTE* desc = ModuleDescriptor(id);
		if (!desc)
			return false;
		if ((SuspendedModuleMask() & (1u << id)) != 0)
			return false;
		if (Rd32(desc + ModuleDesc::Exec) == 0)
			return false;
		// Exactly the test FFX_Module_StepAll makes: either the active byte or its
		// shadow is enough.
		return Rd8(desc + ModuleDesc::Active) != 0 || Rd8(desc + ModuleDesc::ActiveAlt) != 0;
	}

	bool ModuleState(int id, int* out)
	{
		const BYTE* desc = ModuleDescriptor(id);
		if (!desc || !out)
			return false;
		*out = (int)Rd32(desc + ModuleDesc::State);
		return true;
	}

	const char* ModuleName(int id)
	{
		if (!ValidModuleId(id))
			return "invalid module id";
		for (int i = 0; i < (int)(sizeof(kModuleNames) / sizeof(kModuleNames[0])); ++i)
		{
			if (kModuleNames[i].id == id)
				return kModuleNames[i].name;
		}
		return "unidentified submenu";
	}

	int ActiveMenuScreen()
	{
		const DWORD mask = ActiveModuleMask();
		if (mask == 0)
			return -1;
		for (int i = 0; i < (int)(sizeof(kScreenPriority) / sizeof(kScreenPriority[0])); ++i)
		{
			const int id = kScreenPriority[i];
			if ((mask & (1u << id)) != 0)
				return id;
		}
		return -1;
	}

	const char* MenuScreenName()
	{
		const int id = ActiveMenuScreen();
		if (id < 0)
			return "none";
		return ModuleName(id);
	}

	int MenuModuleForMainMenuResult(int result)
	{
		for (int i = 0; i < (int)(sizeof(kMainMenuResultMap) / sizeof(kMainMenuResultMap[0])); ++i)
		{
			if (kMainMenuResultMap[i].result == result)
				return kMainMenuResultMap[i].module;
		}
		return -1;
	}

	// ---------------------------------------------------------------------------
	// The input block
	// ---------------------------------------------------------------------------

	bool ReadMenuPad(MenuPadFrame* out)
	{
		const BYTE* b = PadBlock();
		if (!b || !out)
			return false;

		MenuPadFrame f;
		f.held = Rd16(b + MenuPadBlock::Held);
		f.pressed = Rd16(b + MenuPadBlock::Pressed);
		f.word10 = Rd16(b + MenuPadBlock::Word10);
		f.heldSticky = Rd16(b + MenuPadBlock::HeldSticky);
		f.pressedSticky = Rd16(b + MenuPadBlock::PressedSticky);
		f.word10Sticky = Rd16(b + MenuPadBlock::Word10Sticky);
		f.synthHeld = Rd16(b + MenuPadBlock::SynthHeld);
		f.synthPressed = Rd16(b + MenuPadBlock::SynthPressed);
		f.repeat = Rd16(b + MenuPadBlock::Repeat);
		f.analogX = Rd8(b + MenuPadBlock::AnalogX);
		f.analogY = Rd8(b + MenuPadBlock::AnalogY);

		*out = f;
		return true;
	}

	bool WriteMenuPad(const MenuPadFrame* in)
	{
		BYTE* b = PadBlock();
		if (!b || !in)
			return false;

		Wr16(b + MenuPadBlock::Held, in->held);
		Wr16(b + MenuPadBlock::Pressed, in->pressed);
		Wr16(b + MenuPadBlock::Word10, in->word10);
		Wr16(b + MenuPadBlock::HeldSticky, in->heldSticky);
		Wr16(b + MenuPadBlock::PressedSticky, in->pressedSticky);
		Wr16(b + MenuPadBlock::Word10Sticky, in->word10Sticky);
		Wr16(b + MenuPadBlock::SynthHeld, in->synthHeld);
		Wr16(b + MenuPadBlock::SynthPressed, in->synthPressed);
		Wr16(b + MenuPadBlock::Repeat, in->repeat);
		Wr8(b + MenuPadBlock::AnalogX, in->analogX);
		Wr8(b + MenuPadBlock::AnalogY, in->analogY);

		// The hold counters at +0x02 and the two float arrays at +0x40 and +0x80 are
		// left alone on purpose. They exist only to produce the repeat mask, and the
		// repeat mask was just written directly, so reconstructing the counters that
		// would have produced it is work for nothing. Nothing else in the binary
		// reads them.
		return true;
	}

	bool ClearMenuPad()
	{
		BYTE* b = PadBlock();
		if (!b)
			return false;
		// Exactly what FFX_MenuSys_ClearPad does, reimplemented rather than called
		// so this stays a pure write and needs no prologue check.
		memset(b, 0, MenuPadBlock::Size);
		Wr32(b + MenuPadBlock::AnalogDword, MenuPadBlock::AnalogCentred);
		return true;
	}

	WORD MenuHeld()
	{
		MenuPadFrame f;
		if (!ReadMenuPad(&f))
			return 0;
		if (VirtualPadEnabled())
			return f.held;
		return (f.held & kDpadBits) == 0 ? f.synthHeld : f.held;
	}

	WORD MenuPressed()
	{
		MenuPadFrame f;
		if (!ReadMenuPad(&f))
			return 0;
		if (VirtualPadEnabled())
			return f.pressed;
		// The game tests the HELD mask's dpad bits here, not the pressed mask's.
		return (f.held & kDpadBits) == 0 ? f.synthPressed : f.pressed;
	}

	WORD MenuRepeat()
	{
		MenuPadFrame f;
		if (!ReadMenuPad(&f))
			return 0;
		if (VirtualPadEnabled())
			return f.word10;
		return (f.held & kDpadBits) == 0 ? f.repeat : f.word10;
	}

	// ---------------------------------------------------------------------------
	// The sample hook
	// ---------------------------------------------------------------------------

	bool HookMenuSamplePad(MenuPadObserverFn after)
	{
		padObserver = after;
		if (DETOUR_INSTALLED(MenuSamplePad))
			return true;
		return DETOUR_INSTALL(MenuSamplePad, Rva::MenuSysSamplePad);
	}

	bool MenuSamplePadHookInstalled()
	{
		return DETOUR_INSTALLED(MenuSamplePad);
	}

	MenuSamplePadFn OriginalMenuSamplePad()
	{
		return DETOUR_INSTALLED(MenuSamplePad)
		           ? (MenuSamplePadFn)(UINT_PTR)MenuSamplePadDetour.trampoline
		           : NULL;
	}

	DWORD MenuPadSampleCount()
	{
		return (DWORD)padSampleCount;
	}

	// ---------------------------------------------------------------------------
	// The ATEL script virtual pad
	// ---------------------------------------------------------------------------

	bool VirtualPadEnabled()
	{
		BYTE on = 0;
		if (!ReadGlobal<BYTE>(Rva::VirtualPadEnabled, &on))
			return false;
		// The game tests == 1, not "non-zero", in all six places it reads this. Match
		// it, so a stray value does not make this function and the game disagree.
		return on == 1;
	}

	bool SetVirtualPadEnabled(bool enabled)
	{
		BYTE* p = At<BYTE>(Rva::VirtualPadEnabled);
		if (!p)
			return false;
		Wr8(p, enabled ? (BYTE)1 : (BYTE)0);
		return true;
	}

	bool SetVirtualPadButtons(DWORD mask)
	{
		DWORD* p = At<DWORD>(Rva::VirtualPadButtonsNext);
		if (!p)
			return false;
		// The Next slot, because FFX_MenuSys_SamplePad latches Next into the live
		// slot and then clears Next. Writing the live slot directly would be undone
		// on the very next sample.
		Wr32((BYTE*)p, mask);
		return true;
	}

	bool VirtualPadButtons(DWORD* outLive, DWORD* outNext)
	{
		DWORD live = 0;
		DWORD next = 0;
		if (!ReadGlobal<DWORD>(Rva::VirtualPadButtons, &live))
			return false;
		if (!ReadGlobal<DWORD>(Rva::VirtualPadButtonsNext, &next))
			return false;
		if (outLive)
			*outLive = live;
		if (outNext)
			*outNext = next;
		return true;
	}

	bool SetVirtualPadAxis(int axis, BYTE value)
	{
		DWORD rva = 0;
		switch (axis)
		{
		case 0:
			rva = Rva::VirtualPadAxis0Next;
			break;
		case 1:
			rva = Rva::VirtualPadAxis1Next;
			break;
		case 2:
			rva = Rva::VirtualPadAxis2Next;
			break;
		case 3:
			rva = Rva::VirtualPadAxis3Next;
			break;
		default:
			Log("menu: virtual pad axis %d is out of the 0 to 3 range", axis);
			return false;
		}
		BYTE* p = At<BYTE>(rva);
		if (!p)
			return false;
		Wr8(p, value);
		return true;
	}

	bool ResetVirtualPad()
	{
		DWORD* live = At<DWORD>(Rva::VirtualPadButtons);
		DWORD* next = At<DWORD>(Rva::VirtualPadButtonsNext);
		BYTE* on = At<BYTE>(Rva::VirtualPadEnabled);
		if (!live || !next || !on)
			return false;
		Wr32((BYTE*)live, 0);
		Wr32((BYTE*)next, 0);
		Wr8(on, 0);
		return true;
	}

	// ---------------------------------------------------------------------------
	// Open and close
	// ---------------------------------------------------------------------------

	int MenuRequestedMode()
	{
		int mode = kMenuModeNothingPending;
		return ReadGlobal<int>(Rva::MenuRequestedMode, &mode) ? mode : kMenuModeNothingPending;
	}

	bool MenuOpenRequested()
	{
		return MenuRequestedMode() != kMenuModeNothingPending;
	}

	int MenuEnteredMode()
	{
		int mode = 0;
		return ReadGlobal<int>(Rva::MenuEnterMode, &mode) ? mode : 0;
	}

	int MenuEnterResult()
	{
		int result = -1;
		return ReadGlobal<int>(Rva::MenuEnterResult, &result) ? result : -1;
	}

	bool RequestMenuOpen(int mode, bool allowSpecialModes)
	{
		if (mode != kMenuModeMain && !allowSpecialModes)
		{
			Log("menu: refusing menu mode 0x%08X. Only mode 0 is the ordinary menu, "
			    "pass allowSpecialModes if you really mean a cloud save path.",
			    mode);
			return false;
		}

		if (!CallableVerdict(Rva::MenuSysRequestOpen, &requestOpenVerdict,
		        "FFX_MenuSys_RequestOpen"))
		{
			return false;
		}

		// Through the game's own function, not a write to the global. The real one
		// also refuses the gated mode, clears the enter result and takes the cloud
		// save branch where relevant, and a bare write skips all of it.
		RequestOpenFn request = (RequestOpenFn)(UINT_PTR)ModuleAddress(Rva::MenuSysRequestOpen);
		request(mode);
		return true;
	}

	bool CancelMenuOpenRequest()
	{
		int* p = At<int>(Rva::MenuRequestedMode);
		if (!p)
			return false;
		Wr32((BYTE*)p, (DWORD)kMenuModeNothingPending);
		return true;
	}

	// ---------------------------------------------------------------------------
	// The per-character gate
	// ---------------------------------------------------------------------------

	int MenuCharCursorIndex()
	{
		int cursor = -1;
		return ReadGlobal<int>(Rva::MenuCharCursor, &cursor) ? cursor : -1;
	}

	int MenuCharListLength()
	{
		// The game picks between two counts on a flag this file does not expose, so
		// the larger of the two is the bound for walking the list. Clamped to the
		// array's real capacity either way.
		int a = 0;
		int b = 0;
		const bool okA = ReadGlobal<int>(Rva::MenuCharListCountA, &a);
		const bool okB = ReadGlobal<int>(Rva::MenuCharListCountB, &b);
		if (!okA && !okB)
			return 0;
		const int n = (a > b) ? a : b;
		if (n < 0)
			return 0;
		return (n > kMenuCharListCapacity) ? kMenuCharListCapacity : n;
	}

	BYTE MenuCharListEntry(int n)
	{
		if (n < 0 || n >= kMenuCharListCapacity)
			return kCharNone;
		const BYTE* list = (const BYTE*)ModuleAddress(Rva::MenuCharList);
		if (!Readable(list, kMenuCharListCapacity))
			return kCharNone;
		const BYTE value = Rd8(list + n);
		return (value < kCharCount) ? value : kCharNone;
	}

	BYTE MenuCursorChar()
	{
		// Reimplemented rather than calling FFX_Menu_GetCursorChar, so this stays a
		// pure read and is safe from any thread. The game's version does not bound
		// the cursor, and a cursor of -1 happens briefly while a screen is building,
		// so the bound here is not theoretical.
		const int cursor = MenuCharCursorIndex();
		if (cursor < 0 || cursor >= kMenuCharListCapacity)
			return kCharNone;
		return MenuCharListEntry(cursor);
	}

	BYTE SphereGridCharacter()
	{
		const BYTE* work = MenuWorkBlock();
		if (!work)
			return kCharNone;
		const BYTE value = Rd8(work + MenuWork::SphereGridChar);
		return (value < kCharCount) ? value : kCharNone;
	}

	bool SphereGridSwitchState(BYTE* outPrevChar, BYTE* outDirection)
	{
		const BYTE* work = MenuWorkBlock();
		if (!work)
			return false;
		if (outPrevChar)
		{
			const BYTE prev = Rd8(work + MenuWork::SphereGridCharPrev);
			*outPrevChar = (prev < kCharCount) ? prev : kCharNone;
		}
		if (outDirection)
			*outDirection = Rd8(work + MenuWork::SphereGridSwitchDir);
		return true;
	}

	bool SphereGridPendingCost(int* out)
	{
		const BYTE* work = MenuWorkBlock();
		if (!work || !out)
			return false;
		*out = (int)Rd32(work + MenuWork::SphereLevelsToSpend);
		return true;
	}

	bool SphereGridNodeIndex(int* out)
	{
		const BYTE* work = MenuWorkBlock();
		if (!work || !out)
			return false;
		*out = (int)Rd8(work + MenuWork::SphereGridNodeIndex);
		return true;
	}

	bool SphereGridActive()
	{
		return ModuleStepping(kMenuModuleSphereGrid);
	}

	// ---------------------------------------------------------------------------
	// Diagnostics
	// ---------------------------------------------------------------------------

	void LogMenuPadFrame()
	{
		MenuPadFrame f;
		if (!ReadMenuPad(&f))
		{
			Log("menu pad: the block is not readable, which should never happen");
			return;
		}
		Log("menu pad: held %04X pressed %04X repeat %04X word10 %04X  "
		    "sticky held %04X pressed %04X",
		    (unsigned)f.held, (unsigned)f.pressed, (unsigned)f.repeat,
		    (unsigned)f.word10, (unsigned)f.heldSticky, (unsigned)f.pressedSticky);
		Log("menu pad: synth held %04X pressed %04X  stick %02X,%02X  "
		    "accessors would report held %04X pressed %04X repeat %04X%s",
		    (unsigned)f.synthHeld, (unsigned)f.synthPressed,
		    (unsigned)f.analogX, (unsigned)f.analogY,
		    (unsigned)MenuHeld(), (unsigned)MenuPressed(), (unsigned)MenuRepeat(),
		    VirtualPadEnabled() ? "  [VIRTUAL PAD IS DRIVING]" : "");
	}

	void LogMenuSystemState()
	{
		const DWORD active = ActiveModuleMask();
		const DWORD suspend = SuspendedModuleMask();

		Log("menu: %s  screen \"%s\" (module %d)  active mask %08X  suspend mask %08X",
		    MenuSystemRunning() ? "RUNNING" : "not running",
		    MenuScreenName(), ActiveMenuScreen(), (unsigned)active, (unsigned)suspend);
		Log("menu: requested mode %d  entered mode 0x%08X  enter result %d  "
		    "pad samples %lu  sample hook %s",
		    MenuRequestedMode(), (unsigned)MenuEnteredMode(), MenuEnterResult(),
		    (unsigned long)MenuPadSampleCount(),
		    MenuSamplePadHookInstalled() ? "installed" : "not installed");

		for (int id = 0; id < kMenuModuleCount; ++id)
		{
			if ((active & (1u << id)) == 0)
				continue;
			int state = 0;
			const bool haveState = ModuleState(id, &state);
			Log("menu:   module %2d \"%s\"  state %s%d  %s", id, ModuleName(id),
			    haveState ? "" : "(unreadable) ", haveState ? state : 0,
			    ModuleStepping(id) ? "stepping" : "suspended or idle");
		}

		const BYTE cursorChar = MenuCursorChar();
		const BYTE gridChar = SphereGridCharacter();
		Log("menu: character cursor %d of %d -> character %d (%s)",
		    MenuCharCursorIndex(), MenuCharListLength(), (int)cursorChar,
		    CharacterName(cursorChar));

		if (SphereGridActive())
		{
			int cost = 0;
			int node = 0;
			BYTE prev = kCharNone;
			BYTE dir = 0;
			SphereGridPendingCost(&cost);
			SphereGridNodeIndex(&node);
			SphereGridSwitchState(&prev, &dir);
			Log("menu: SPHERE GRID is up on character %d (%s)  node %d  pending cost %d  "
			    "last switch from %d dir %d",
			    (int)gridChar, CharacterName(gridChar), node, cost, (int)prev, (int)dir);
			Log("menu: gate the grid on the character above, not on the cursor. "
			    "FFX_SaveData_SpendSphereLevels is charged against it.");
		}

		LogMenuPadFrame();
	}

} // namespace ffx
