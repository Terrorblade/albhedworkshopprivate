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

		// The kernel string group the in-game menu's UI strings live in.
		const int kUiStringGroup = 7;

		// ---------------------------------------------------------------------------
		// The FFX glyph table for page 0, as a byte per printable ASCII character.
		//
		// Built from the formula rather than typed out, so there is one place the rule
		// lives. It returns -1 for a character with no glyph, which is only '@'.
		//
		// The rule, from FFX_Text_DecodeGlyph: byte = 0x30 + glyph index, and the sheet
		// is ASCII order with the ten digits lifted to the front and '@' dropped.
		// ---------------------------------------------------------------------------
		const int kFfxTextNoGlyph = -1;

		// The encoded space, which is what an unmappable character becomes. Named
		// because 0x3A reads like a colon and is not one.
		const int kFfxTextSpace = 0x3A;

		int FfxTextByteFor(char ascii)
		{
			const unsigned char c = (unsigned char)ascii;

			if (c >= '0' && c <= '9')
				return 0x30 + (c - '0'); // glyph 0..9

			if (c >= 0x20 && c <= 0x2F)
				return 0x30 + 10 + (c - 0x20); // space through '/', glyph 10..25

			if (c >= 0x3A && c <= 0x3F)
				return 0x30 + 26 + (c - 0x3A); // ':' through '?', glyph 26..31

			if (c >= 'A' && c <= 'Z')
				return 0x30 + 32 + (c - 'A'); // glyph 32..57

			if (c >= 0x5B && c <= 0x60)
				return 0x30 + 58 + (c - 0x5B); // '[' through '`', glyph 58..63

			if (c >= 'a' && c <= 'z')
				return 0x30 + 64 + (c - 'a'); // glyph 64..89

			if (c >= 0x7B && c <= 0x7E)
				return 0x30 + 90 + (c - 0x7B); // '{' through '~', glyph 90..93

			// '@' lands here, and so does every byte outside 0x20..0x7E. There is no
			// glyph for it on the sheet.
			return kFfxTextNoGlyph;
		}

		// ---------------------------------------------------------------------------
		// The two string lookup detours.
		//
		// Verified in IDA at 0x78FCF0 and 0x78FBB0. Identical prologues, six bytes,
		// three whole instructions, nothing position dependent:
		//
		//   55        push ebp
		//   8B EC     mov  ebp, esp
		//   8B 45 08  mov  eax, [ebp+arg_0]
		//
		// Both are __cdecl(int group, int id, char lang) and both return a char * into
		// the loaded kernel table blob, or 0 when that group's blob is not resident.
		//
		// WHY BOTH. One group 7 row carries four WORD offsets, the name pair at +0 and
		// the description pair at +8. KernelStringGet reads the name, KernelStringGetDesc
		// reads the description, and the Config screen uses the first for a row's label
		// and value names and the second for the help line under the selected value. A
		// mod answering only the first gets its label drawn and then a help line looked
		// up from the real table with its own out-of-range id, which lands on whatever
		// the first range descriptor points at.
		// ---------------------------------------------------------------------------
		DETOUR_DECLARE(KernelStringGet, const char*, (int group, int id, char lang));
		DETOUR_DECLARE(KernelStringGetDesc, const char*, (int group, int id, char lang));

		DETOUR_PROLOGUE(KernelStringGet) = {
			0x55,            // push ebp
			0x8B, 0xEC,      // mov  ebp, esp
			0x8B, 0x45, 0x08 // mov  eax, [ebp+arg_0]
		};

		DETOUR_PROLOGUE(KernelStringGetDesc) = {
			0x55,            // push ebp
			0x8B, 0xEC,      // mov  ebp, esp
			0x8B, 0x45, 0x08 // mov  eax, [ebp+arg_0]
		};

		UiStringProviderFn uiStringProvider = NULL;
		volatile LONG uiStringHits = 0;

		// The id the game will actually look up. Both originals mask to 12 bits before
		// doing anything, so the provider is handed the masked value and a caller
		// cannot be surprised by an id that works in one place and not another.
		const int kUiStringIdMask = 0xFFF;

		const char* __cdecl KernelStringGetHook(int group, int id, char lang)
		{
			if (uiStringProvider)
			{
				const char* ours = uiStringProvider(group, id & kUiStringIdMask, false);
				if (ours)
				{
					InterlockedIncrement(&uiStringHits);
					return ours;
				}
			}
			return DETOUR_ORIGINAL(KernelStringGet)(group, id, lang);
		}

		const char* __cdecl KernelStringGetDescHook(int group, int id, char lang)
		{
			if (uiStringProvider)
			{
				const char* ours = uiStringProvider(group, id & kUiStringIdMask, true);
				if (ours)
				{
					InterlockedIncrement(&uiStringHits);
					return ours;
				}
			}
			return DETOUR_ORIGINAL(KernelStringGetDesc)(group, id, lang);
		}

		// ---------------------------------------------------------------------------
		// Config row helpers. A row is either one of the game's, reached through the
		// live table, or one a mod owns. Either way it is 44 bytes somewhere, so every
		// access is Readable checked over the whole object rather than per field.
		// ---------------------------------------------------------------------------
		BYTE* CheckedRow(const void* row)
		{
			BYTE* p = (BYTE*)row;
			return Readable(p, ConfigRow::Size) ? p : NULL;
		}

		bool ReadRowInt(const void* row, DWORD offset, int* out)
		{
			BYTE* p = CheckedRow(row);
			if (!p || !out)
				return false;
			*out = (int)Rd32(p + offset);
			return true;
		}

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

	bool SphereGridPadState(WORD* outHeld, WORD* outPressed, WORD* outHeldOrRepeat)
	{
		const BYTE* work = MenuWorkBlock();
		if (!work)
			return false;

		// All three or none. A caller hashing these to catch a divergence wants to know
		// it got a consistent snapshot, and the work block is a heap pointer that is
		// genuinely null before the grid builds itself.
		if (outHeld)
			*outHeld = Rd16(work + MenuWork::GridPadHeld);
		if (outPressed)
			*outPressed = Rd16(work + MenuWork::GridPadPressed);
		if (outHeldOrRepeat)
			*outHeldOrRepeat = Rd16(work + MenuWork::GridPadHeldOrRepeat);
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

	// ---------------------------------------------------------------------------
	// The Config screen's row table
	// ---------------------------------------------------------------------------

	int ConfigRowCount()
	{
		int count = 0;
		if (!ReadGlobal<int>(Rva::MenuConfigRowCount, &count))
			return 0;

		// Zero before the menu has ever been opened, and a nonsense value would mean
		// something else has written the global, so it is clamped rather than trusted.
		if (count < 0 || count > kConfigMaxRowCount)
			return 0;
		return count;
	}

	void** ConfigRowTable()
	{
		void*** slot = At<void**>(Rva::MenuConfigRows);
		if (!slot)
			return NULL;

		// "void **volatile *" rather than "volatile void ***": the POINTER is what the
		// game thread rewrites, and the array behind it is read separately.
		void** table = *(void** volatile*)slot;
		const int count = ConfigRowCount();
		if (count <= 0)
			return NULL;
		return Readable(table, sizeof(void*) * (SIZE_T)count) ? table : NULL;
	}

	void* ConfigRowAt(int index)
	{
		void** table = ConfigRowTable();
		if (!table || index < 0 || index >= ConfigRowCount())
			return NULL;

		void* row = (void*)Rd32((const BYTE*)(table + index));
		return CheckedRow(row);
	}

	int ConfigCursorRow()
	{
		// A signed char in the game, and it is compared against the int row count as
		// one, so it is read back as one here too.
		const BYTE* p = At<BYTE>(Rva::MenuConfigCursorRow);
		if (!p)
			return -1;
		return (int)(signed char)Rd8(p);
	}

	bool SetConfigCursorRow(int index)
	{
		BYTE* p = At<BYTE>(Rva::MenuConfigCursorRow);
		if (!p || index < -128 || index > 127)
			return false;
		Wr8(p, (BYTE)(signed char)index);
		return true;
	}

	bool ConfigRowTableIsShipped()
	{
		void*** slot = At<void**>(Rva::MenuConfigRows);
		if (!slot)
			return true; // cannot tell, and claiming it is ours would be worse

		const DWORD live = Rd32((const BYTE*)slot);
		if (live == 0)
			return true; // the menu has never been opened, which is the game's state

		const DWORD shipped[] = {
			(DWORD)(UINT_PTR)ModuleAddress(Rva::MenuConfigRowArrayIntlHdd),
			(DWORD)(UINT_PTR)ModuleAddress(Rva::MenuConfigRowArrayIntlNoHdd),
			(DWORD)(UINT_PTR)ModuleAddress(Rva::MenuConfigRowArrayJpHdd),
			(DWORD)(UINT_PTR)ModuleAddress(Rva::MenuConfigRowArrayJpNoHdd)
		};

		for (int i = 0; i < 4; ++i)
			if (live == shipped[i])
				return true;

		return false;
	}

	bool CopyConfigRowTable(void** out, int maxRows, int* outCount)
	{
		if (outCount)
			*outCount = 0;
		if (!out || maxRows <= 0)
			return false;

		void** table = ConfigRowTable();
		const int count = ConfigRowCount();
		if (!table || count <= 0 || count > maxRows)
			return false;

		for (int i = 0; i < count; ++i)
			out[i] = (void*)Rd32((const BYTE*)(table + i));

		if (outCount)
			*outCount = count;
		return true;
	}

	bool SetConfigRowTable(void** rows, int count)
	{
		if (count < 0 || count > kConfigMaxRowCount)
			return false;
		if (count > 0 && !Readable(rows, sizeof(void*) * (SIZE_T)count))
			return false;

		void*** slot = At<void**>(Rva::MenuConfigRows);
		int* countSlot = At<int>(Rva::MenuConfigRowCount);
		if (!slot || !countSlot)
			return false;

		// ORDER MATTERS, and it is not symmetric. The game's draw loop re-reads the
		// count on every iteration and indexes the array with it, so there must never
		// be an instant where the count promises more rows than the live array holds.
		// Shrinking: drop the count first. Growing: swap the pointer first and raise
		// the count after. Either way the pair is only ever over-safe in between.
		const int before = ConfigRowCount();
		if (count < before)
			Wr32((BYTE*)countSlot, (DWORD)count);

		Wr32((BYTE*)slot, (DWORD)(UINT_PTR)rows);
		Wr32((BYTE*)countSlot, (DWORD)count);

		// And the cursor, because nothing in the game clamps it. FFX_Menu_ExecModule10
		// reads it as a signed char and indexes the array with it unchecked, so a
		// cursor left at row 11 of a table that just became 8 long is an out of bounds
		// row-pointer read on the player's next Up or Down.
		const int cursor = ConfigCursorRow();
		if (count == 0 || cursor < 0 || cursor >= count)
			SetConfigCursorRow(0);

		return true;
	}

	bool ConfigRowValueCount(const void* row, int* out)
	{
		return ReadRowInt(row, ConfigRow::ValueCount, out);
	}

	bool ConfigRowCurrentValue(const void* row, int* out)
	{
		return ReadRowInt(row, ConfigRow::CurrentValue, out);
	}

	bool ConfigRowSelectable(const void* row, int* out)
	{
		return ReadRowInt(row, ConfigRow::Selectable, out);
	}

	bool SetConfigRowCurrentValue(void* row, int value)
	{
		BYTE* p = CheckedRow(row);
		if (!p)
			return false;

		// Clamped into the row's own range, because the game wraps on a press and then
		// indexes the value-name array with the result. A value past the count would
		// read a value id off the end of the row.
		const int count = (int)Rd32(p + ConfigRow::ValueCount);
		if (count <= 0)
			return false;
		if (value < 0 || value >= count)
			return false;

		Wr32(p + ConfigRow::CurrentValue, (DWORD)value);
		return true;
	}

	bool SetConfigRowSelectable(void* row, bool selectable)
	{
		BYTE* p = CheckedRow(row);
		if (!p)
			return false;
		Wr32(p + ConfigRow::Selectable, selectable ? 1u : 0u);
		return true;
	}

	WORD ConfigRowLabelId(const void* row)
	{
		BYTE* p = CheckedRow(row);
		return p ? Rd16(p + ConfigRow::LabelId) : (WORD)0;
	}

	WORD ConfigRowValueId(const void* row, int value)
	{
		BYTE* p = CheckedRow(row);
		if (!p || value < 0 || value >= kConfigRowMaxValues)
			return 0;
		return Rd16(p + ConfigRow::ValueIds + ConfigRow::ValueIdStride * (DWORD)value);
	}

	bool BuildConfigRow(void* row, int valueCount, bool selectable, ConfigRowFn getter,
	    ConfigRowFn setter, WORD labelId, const WORD* valueIds)
	{
		BYTE* p = CheckedRow(row);
		if (!p)
			return false;
		if (valueCount < 0 || valueCount > kConfigRowMaxValues)
			return false;
		if (valueCount > 0 && !valueIds)
			return false;

		// A null getter is a fault rather than a missing row:
		// FFX_Menu_ConfigInitRowValues calls every row's getter unconditionally on
		// entry to the screen. The setter is only reached from a Left or Right press on
		// a selectable row, so a null one is survivable, but it is refused anyway
		// because there is no reason to have one.
		if (!getter || !setter)
			return false;

		memset(p, 0, ConfigRow::Size);
		Wr32(p + ConfigRow::ValueCount, (DWORD)valueCount);
		Wr32(p + ConfigRow::CurrentValue, 0);
		Wr32(p + ConfigRow::Selectable, selectable ? 1u : 0u);
		Wr32(p + ConfigRow::Getter, (DWORD)(UINT_PTR)getter);
		Wr32(p + ConfigRow::Setter, (DWORD)(UINT_PTR)setter);
		Wr16(p + ConfigRow::LabelId, labelId);

		// 3, because every one of the ten shipped rows has 3 here. Nothing in the draw
		// or the exec reads it, so this is cargo cult on purpose: matching the shipped
		// rows costs nothing and a future reader of a memory dump will not have to
		// wonder why ours are different.
		Wr16(p + ConfigRow::Unknown1A, 3);

		for (int i = 0; i < valueCount; ++i)
		{
			Wr16(p + ConfigRow::ValueIds + ConfigRow::ValueIdStride * (DWORD)i,
			    valueIds[i]);
		}
		return true;
	}

	bool ConfigScreenActive()
	{
		return ModuleStepping(kMenuModuleConfig);
	}

	// ---------------------------------------------------------------------------
	// FFX text
	// ---------------------------------------------------------------------------

	int FfxTextEncodedLength(const char* ascii)
	{
		if (!ascii)
			return 1;

		int n = 0;
		while (ascii[n] != '\0')
			++n;
		return n + 1;
	}

	bool EncodeFfxText(const char* ascii, char* out, int outBytes)
	{
		if (!out || outBytes <= 0)
			return false;

		out[0] = '\0';
		if (!ascii)
			return true;

		const int needed = FfxTextEncodedLength(ascii);
		if (needed > outBytes)
			return false;

		int i = 0;
		for (; ascii[i] != '\0'; ++i)
		{
			const int encoded = FfxTextByteFor(ascii[i]);
			out[i] = (char)((encoded == kFfxTextNoGlyph) ? kFfxTextSpace : encoded);
		}
		out[i] = '\0';
		return true;
	}

	// ---------------------------------------------------------------------------
	// The UI string override
	// ---------------------------------------------------------------------------

	bool InstallUiStringOverride(UiStringProviderFn provider)
	{
		// Set the provider BEFORE the detours go in, so there is no window where a
		// hooked lookup runs with no provider. Harmless either way, since the hook
		// checks, but a window that does not exist cannot be reasoned about wrongly.
		uiStringProvider = provider;

		if (DETOUR_INSTALLED(KernelStringGet) && DETOUR_INSTALLED(KernelStringGetDesc))
			return true;

		const bool name = DETOUR_INSTALL(KernelStringGet, Rva::KernelStringGet);
		const bool desc = DETOUR_INSTALL(KernelStringGetDesc, Rva::KernelStringGetDesc);

		if (name && desc)
			return true;

		// All or nothing. Half of this is worse than none of it: the label would come
		// from the provider and the help line under it from the real table with the
		// provider's own out-of-range id, which draws whatever the kernel table's first
		// range descriptor happens to point at.
		Log("menu: the UI string override did not install (name hook %s, description "
		    "hook %s), so nothing will be answered. A mod's own Config rows would be "
		    "drawn with the wrong text.",
		    name ? "ok" : "REFUSED", desc ? "ok" : "REFUSED");
		uiStringProvider = NULL;
		return false;
	}

	bool UiStringOverrideInstalled()
	{
		return DETOUR_INSTALLED(KernelStringGet) && DETOUR_INSTALLED(KernelStringGetDesc) &&
		       uiStringProvider != NULL;
	}

	DWORD UiStringOverrideHits()
	{
		return (DWORD)uiStringHits;
	}

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
