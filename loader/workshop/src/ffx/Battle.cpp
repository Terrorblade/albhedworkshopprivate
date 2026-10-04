#include "ffx/Battle.h"

#include "ffx/addresses/Battle.h"
#include "ffx/addresses/GameState.h"
#include "ffx/GameState.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

#include <stdio.h>
#include <string.h>

// Implementation notes, and the two things that will bite whoever edits this next.
//
// 1. THE ACTOR ARRAY IS BEHIND A POINTER AND THE ENGINE DOES NOT CHECK IT.
//    FFX_Battle_GetActor is base + 3984 * index where base is a pointer global, and
//    it tests only the index. Outside a battle that global is zero, so the function
//    cheerfully returns an address near zero. So every unit read here goes through
//    BattleUnit, which checks the index range first, then that the array is
//    allocated, and then validates the record the engine handed back with Readable.
//    The index range comes first on purpose: 31 and up is a different array with a
//    different stride and none of the offsets below mean anything there.
//
// 2. THE HOOKS ARE CALL SITE PATCHES, NOT INLINE DETOURS.
//    Both targets start with a rel32 call, so there are no five position independent
//    bytes at either entry and workshop::InstallDetour relocates stolen bytes
//    verbatim. Rewriting the caller's rel32 instead leaves both engine functions
//    untouched, needs no trampoline at all (the original is still callable by
//    address), and intercepts exactly the call sites we mean rather than every
//    caller of the function. The cost is that the sites have to be located, so each
//    one is verified to be an E8 whose target is the function we expect before a
//    single byte is written. A mismatch logs and refuses, same discipline as the
//    detour prologue check.
//
// Everything is read volatile, because the game thread writes it behind us.
//
// The derivation is in ..\..\..\reversing\BATTLE_COMMAND.md.

namespace ffx
{

	using workshop::Log;
	using workshop::LooksLikePointer;
	using workshop::ModuleAddress;
	using workshop::Readable;

	namespace
	{

		// ---------------------------------------------------------------------------
		// Per unit field offsets. Grouped so a wrong offset is one edit rather than a
		// scattered cast, same rule as ffx/Layout.h.
		//
		// All relative to the 3984 byte record for units 0..30. The 912 byte monster
		// records at 31..92 share NONE of these, which is why BattleUnit refuses them.
		// ---------------------------------------------------------------------------
		namespace Unit
		{
			const DWORD Stride = 3984; // 0xF90

			const DWORD BattleSlot = 0x4FE;        // byte, 0..2 for a party member
			const DWORD Overdrive = 0x5BC;         // byte
			const DWORD OverdriveMax = 0x5BD;      // byte
			const DWORD CurrentMp = 0x5D4;         // dword
			const DWORD CtbCounter = 0x65C;        // byte
			const DWORD LastCommand = 0x72C;       // 72 bytes, the last executed record
			const DWORD Present = 0xDC8;           // byte
			const DWORD Ko = 0xDCC;                // byte
			const DWORD Escaped = 0xDCE;           // byte
			const DWORD Ready = 0xDD6;             // byte
			const DWORD TurnQueueIndex = 0xDE4;    // byte, 0xFF for none
			const DWORD CommandQueueIndex = 0xDE5; // byte, 0xFF for none
			const DWORD TurnEntryCount = 0xDE6;    // byte
			const DWORD CommandEntryCount = 0xDE7; // byte
			const DWORD ActionState = 0xDF0;       // byte
			const DWORD AiControlled = 0xDF3;      // byte
		}

		const BYTE kNoQueueSlot = 0xFF;
		const BYTE kNoMenuOwner = 255;
		const DWORD kTurnEntryStride = 8;
		const int kTurnQueueCapacity = 62;

		// ---------------------------------------------------------------------------
		// Game function types. All __cdecl, all read back out of the IDB.
		// ---------------------------------------------------------------------------
		typedef int(__cdecl* CommitCommandFn)(BYTE* record, int gilCost, unsigned variant);
		typedef int(__cdecl* MenuOpenFn)(int unitIndex);
		typedef int(__cdecl* IsActiveFn)(void);
		typedef int(__cdecl* IsPendingOrActiveFn)(void);
		typedef BYTE*(__cdecl* GetActorFn)(unsigned char unitIndex);
		typedef BYTE*(__cdecl* GetAbilityRecordFn)(int abilityId, DWORD* outStringBase);

		template <class T>
		T Resolve(DWORD rva)
		{
			return (T)ModuleAddress(rva);
		}

		inline BYTE Rd8(const BYTE* p)
		{
			return *(volatile const BYTE*)p;
		}
		inline DWORD Rd32(const BYTE* p)
		{
			return *(volatile const DWORD*)p;
		}

		// A guarded read of one field. Returns false rather than faulting, which is the
		// only acceptable behaviour on the frame path.
		inline bool Field8(const BYTE* base, DWORD off, BYTE* out)
		{
			if (!base || !Readable(base + off, 1))
				return false;
			*out = Rd8(base + off);
			return true;
		}

		inline bool Field32(const BYTE* base, DWORD off, DWORD* out)
		{
			if (!base || !Readable(base + off, 4))
				return false;
			*out = Rd32(base + off);
			return true;
		}

		// A byte global, or NULL when it is not readable yet.
		const BYTE* ByteGlobal(DWORD rva)
		{
			const BYTE* p = (const BYTE*)ModuleAddress(rva);
			return Readable(p, 1) ? p : NULL;
		}

		// Is the ally actor array allocated.
		//
		// FFX_Battle_IsActive is a one line function that RETURNS that array pointer, so
		// calling it is the same read as looking at the global, and the global's address
		// is not one this library carries. A plausible pointer rather than merely non
		// zero, because the value is used as a base for 3984 byte strides.
		bool AllyActorsAllocated()
		{
			const int raw = Resolve<IsActiveFn>(Rva::BattleIsActive)();
			return raw != 0 && LooksLikePointer((DWORD)(unsigned)raw);
		}

		bool UnitIndexInRange(int unitIndex)
		{
			return unitIndex >= 0 && unitIndex <= kBattleMaxUnitIndex;
		}

		// The command queue base, validated for the whole 62 slot block, because every
		// caller either walks it or indexes it and one check is cheaper than 62.
		BYTE* CommandQueueBase()
		{
			BYTE* base = (BYTE*)ModuleAddress(Rva::BtlCmdQueue);
			const DWORD bytes = (DWORD)Rva::BtlCmdQueueCapacity * kBattleCommandBytes;
			return Readable(base, bytes) ? base : NULL;
		}

		BYTE* TurnQueueBase()
		{
			BYTE* base = (BYTE*)ModuleAddress(Rva::BtlTurnQueue);
			return Readable(base, kTurnQueueCapacity * kTurnEntryStride) ? base : NULL;
		}

		// The queue counts are stored as signed bytes and the engine reads them with a
		// (char) cast, so a count is clamped the same way here rather than trusted. A
		// negative count means the battle has not set up yet.
		int SignedByteCount(DWORD rva, int capacity)
		{
			const BYTE* p = ByteGlobal(rva);
			if (!p)
				return 0;
			const int value = (int)(signed char)Rd8(p);
			if (value <= 0)
				return 0;
			return (value > capacity) ? capacity : value;
		}

		bool CopyRecord(const BYTE* from, BattleCommandRecord* out)
		{
			if (!from || !out || !Readable(from, kBattleCommandBytes))
				return false;
			memcpy(out, from, kBattleCommandBytes);
			return true;
		}

		// ---------------------------------------------------------------------------
		// The call site patch.
		//
		// One dword written, after proving the five bytes are the call we think they
		// are. Nothing is relocated and the target function is never modified, so the
		// original stays callable by address and the other callers of it are untouched.
		// ---------------------------------------------------------------------------
		struct CallSite
		{
			DWORD rva;
			bool patched;
		};

		bool PatchCallSite(CallSite& site, DWORD siteRva, DWORD expectedTargetRva,
		    void* replacement, const char* what)
		{
			site.rva = siteRva;
			site.patched = false;

			BYTE* call = (BYTE*)ModuleAddress(siteRva);
			if (!Readable(call, 5))
			{
				Log("battle: %s call site RVA 0x%08X is not readable, refusing", what, siteRva);
				return false;
			}
			if (Rd8(call) != 0xE8)
			{
				Log("battle: %s call site RVA 0x%08X holds 0x%02X not an E8 call, refusing. "
				    "Re-derive this site from the IDB.",
				    what, siteRva, (unsigned)Rd8(call));
				return false;
			}

			INT32 relative = 0;
			memcpy(&relative, call + 1, sizeof(relative));
			const BYTE* target = call + 5 + relative;
			const BYTE* expected = (const BYTE*)ModuleAddress(expectedTargetRva);
			if (target != expected)
			{
				Log("battle: %s call site RVA 0x%08X calls 0x%08X, expected 0x%08X. Refusing "
				    "to patch, because that is not the function this hook means.",
				    what, siteRva, (unsigned)(UINT_PTR)target, (unsigned)(UINT_PTR)expected);
				return false;
			}

			// PAGE_EXECUTE_READWRITE and NOT workshop::WriteProtectedDword, which asks
			// for PAGE_READWRITE. That helper is for a vtable slot in a data section.
			// Taking execute off a .text page, even for the three instructions this
			// takes, is a window in which another thread running through that page
			// faults. workshop::InstallDetour makes the same choice for the same reason.
			const INT32 wanted = (INT32)((BYTE*)replacement - (call + 5));
			DWORD previousProtect = 0;
			if (!VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &previousProtect))
			{
				Log("battle: %s call site RVA 0x%08X could not be made writable, "
				    "GetLastError=%lu",
				    what, siteRva, GetLastError());
				return false;
			}
			memcpy(call + 1, &wanted, sizeof(wanted));

			DWORD ignored = 0;
			VirtualProtect(call, 5, previousProtect, &ignored);
			FlushInstructionCache(GetCurrentProcess(), call, 5);

			site.patched = true;
			Log("battle: %s hooked at call site RVA 0x%08X, now calling 0x%08X",
			    what, siteRva, (unsigned)(UINT_PTR)replacement);
			return true;
		}

		// ---------------------------------------------------------------------------
		// Where the five player side commits are.
		//
		// Each is an offset from a function this library already knows the address of,
		// measured in the IDB, and each is verified byte for byte before use. Written
		// as offsets rather than as five more address constants because the offset is
		// the thing that was actually derived: "the single call to CommitCommand inside
		// this function", and the verification proves it rather than trusting it.
		// ---------------------------------------------------------------------------
		struct MenuCommitSite
		{
			DWORD functionRva;
			DWORD offset;
			const char* name;
		};

		const MenuCommitSite kMenuCommitSites[] = {
			{ Rva::BtlMenuConfirmCommand, 0x12, "menu confirm" },
			{ Rva::BtlMenuPageProcRoot, 0xB4, "page root escape" },
			{ Rva::BtlMenuPageProcB, 0xAB, "page B escape" },
			{ Rva::BtlMenuPageProcC, 0x130, "page C escape" },
			{ Rva::BtlMenuPageProcD, 0x130, "page D escape" },
		};
		const int kMenuCommitSiteCount = (int)(sizeof(kMenuCommitSites) / sizeof(kMenuCommitSites[0]));

		CallSite menuCommitSites[kMenuCommitSiteCount];
		int menuCommitSitesPatched = 0;
		bool menuCommitHookTried = false;

		CallSite menuOpenSite;
		bool menuOpenHookTried = false;

		BattleMenuCommitFn menuCommitCallback = NULL;
		BattleMenuOpenFn menuOpenCallback = NULL;

		volatile LONG commitsSeen = 0;
		volatile LONG commitsTaken = 0;
		volatile LONG opensSuppressed = 0;

		// ---------------------------------------------------------------------------
		// The two replacements.
		//
		// Both must match the engine signature exactly, because the call site pushes
		// the arguments and cleans the stack itself.
		// ---------------------------------------------------------------------------

		int __cdecl MenuCommitThunk(BYTE* record, int gilCost, unsigned variant)
		{
			InterlockedIncrement(&commitsSeen);

			if (menuCommitCallback && Readable(record, kBattleCommandBytes))
			{
				// A copy, so a callback cannot scribble on the menu's staging buffer.
				// That buffer is rebuilt from the page stack every frame anyway, but a
				// mod reaching into it is the kind of thing that works until the frame
				// it does not.
				BattleCommandRecord copy;
				memcpy(&copy, record, kBattleCommandBytes);

				if (menuCommitCallback(&copy, gilCost, variant))
				{
					InterlockedIncrement(&commitsTaken);

					// Zero is what FFX_Btl_CommitCommand itself returns after a
					// successful push. None of the five callers reads the value, which
					// was checked at each site, so this is for form rather than for
					// effect.
					return 0;
				}
			}

			return Resolve<CommitCommandFn>(Rva::BtlCommitCommand)(record, gilCost, variant);
		}

		int __cdecl MenuOpenThunk(int unitIndex)
		{
			if (menuOpenCallback && !menuOpenCallback(unitIndex))
			{
				InterlockedIncrement(&opensSuppressed);

				// FFX_BtlMenu_Open returns the phase when it declines to open, and its
				// one caller ignores the value. Zero is "a phase that is not 1", which
				// is the honest answer to "did a menu open".
				return 0;
			}
			return Resolve<MenuOpenFn>(Rva::BtlMenuOpen)(unitIndex);
		}

		const char* AbilityClassName(int abilityClass)
		{
			switch (abilityClass)
			{
			case kBattleAbilityCharacter:
				return "char";
			case kBattleAbilityItem:
				return "item";
			case kBattleAbilityPlayer:
				return "ability";
			case kBattleAbilityMonster:
				return "monster";
			case kBattleAbilityOther:
				return "other";
			default:
				return "class?";
			}
		}

	} // namespace

	// ---------------------------------------------------------------------------
	// Is there a battle to look at
	// ---------------------------------------------------------------------------

	bool BattleRunning()
	{
		return AllyActorsAllocated();
	}

	int BattlePhase()
	{
		// The same byte FFX_Btl_GetPhase returns, read rather than called, so this is
		// safe before the battle code has ever run. Sign extended because the engine's
		// own getter is a movsx, so 0xFF reads as -1 there and should here too.
		const BYTE* p = ByteGlobal(Rva::BtlSubPhase);
		return p ? (int)(signed char)Rd8(p) : 0;
	}

	bool BattlePendingOrActive()
	{
		return Resolve<IsPendingOrActiveFn>(Rva::BtlIsBattlePendingOrActive)() != 0;
	}

	bool BattleCommandPhase()
	{
		return BattleRunning() && BattlePhase() == 1;
	}

	// ---------------------------------------------------------------------------
	// Units
	// ---------------------------------------------------------------------------

	bool IsAllyUnit(int unitIndex)
	{
		return UnitIndexInRange(unitIndex);
	}

	BYTE* BattleUnit(int unitIndex)
	{
		// The index range is checked BEFORE the engine's accessor is called, not after.
		// FFX_Battle_GetActor sends 31..92 to the 912 byte monster array and anything
		// from 93 up to a dummy record, and none of the offsets in this file mean
		// anything there.
		if (!UnitIndexInRange(unitIndex))
			return NULL;
		if (!AllyActorsAllocated())
			return NULL;

		BYTE* unit = Resolve<GetActorFn>(Rva::BattleGetActor)((unsigned char)unitIndex);
		if (!LooksLikePointer((DWORD)(UINT_PTR)unit) || !Readable(unit, Unit::Stride))
			return NULL;
		return unit;
	}

	bool UnitIsAiControlled(int unitIndex)
	{
		BYTE value = 0;
		return Field8(BattleUnit(unitIndex), Unit::AiControlled, &value) && value != 0;
	}

	bool UnitPresent(int unitIndex)
	{
		BYTE value = 0;
		return Field8(BattleUnit(unitIndex), Unit::Present, &value) && value != 0;
	}

	bool UnitKo(int unitIndex)
	{
		BYTE value = 0;
		return Field8(BattleUnit(unitIndex), Unit::Ko, &value) && value != 0;
	}

	bool UnitEscaped(int unitIndex)
	{
		BYTE value = 0;
		return Field8(BattleUnit(unitIndex), Unit::Escaped, &value) && value != 0;
	}

	bool UnitReady(int unitIndex)
	{
		BYTE value = 0;
		return Field8(BattleUnit(unitIndex), Unit::Ready, &value) && value != 0;
	}

	int UnitBattleSlotField(int unitIndex)
	{
		BYTE value = 0;
		if (!Field8(BattleUnit(unitIndex), Unit::BattleSlot, &value))
			return -1;
		return (value < kBattleActiveSlots) ? (int)value : -1;
	}

	int UnitForBattleSlot(int slot)
	{
		if (slot < 0 || slot >= kBattleActiveSlots)
			return -1;

		// The battle side roster, which holds a CHARACTER index, and for units 0..17
		// the character index and the unit index are the same number. Read from the
		// roster rather than from unit+0x4FE so an aeon's aeon index cannot be mistaken
		// for a party slot.
		const BYTE member = BattlePartyMember(slot);
		if (member == (BYTE)kCharNone)
			return -1;
		return UnitIndexInRange((int)member) ? (int)member : -1;
	}

	int BattleSlotOfUnit(int unitIndex)
	{
		if (!UnitIndexInRange(unitIndex))
			return -1;
		for (int slot = 0; slot < kBattleActiveSlots; ++slot)
		{
			if (UnitForBattleSlot(slot) == unitIndex)
				return slot;
		}
		return -1;
	}

	int UnitCurrentMp(int unitIndex)
	{
		DWORD value = 0;
		if (!Field32(BattleUnit(unitIndex), Unit::CurrentMp, &value))
			return -1;
		return (int)value;
	}

	int UnitOverdrive(int unitIndex)
	{
		BYTE value = 0;
		return Field8(BattleUnit(unitIndex), Unit::Overdrive, &value) ? (int)value : -1;
	}

	int UnitOverdriveMax(int unitIndex)
	{
		BYTE value = 0;
		return Field8(BattleUnit(unitIndex), Unit::OverdriveMax, &value) ? (int)value : -1;
	}

	int UnitCtbCounter(int unitIndex)
	{
		BYTE value = 0;
		return Field8(BattleUnit(unitIndex), Unit::CtbCounter, &value) ? (int)value : -1;
	}

	bool SetUnitOverdrive(int unitIndex, int value)
	{
		BYTE* unit = BattleUnit(unitIndex);
		BYTE max = 0;
		if (!Field8(unit, Unit::OverdriveMax, &max))
			return false;
		if (!Readable(unit + Unit::Overdrive, 1))
			return false;

		if (value < 0)
			value = 0;
		// The engine's full test is gauge == max, so a gauge above max would read as
		// not full rather than as more than full.
		if (value > (int)max)
			value = (int)max;

		unit[Unit::Overdrive] = (BYTE)value;
		return true;
	}

	bool FillUnitOverdrive(int unitIndex)
	{
		BYTE* unit = BattleUnit(unitIndex);
		BYTE max = 0;
		if (!Field8(unit, Unit::OverdriveMax, &max))
			return false;
		if (!Readable(unit + Unit::Overdrive, 1))
			return false;

		// Copying the max over the gauge, which is what the booster's invincible refill
		// does. The max is per character and per mode, so a constant would be wrong.
		unit[Unit::Overdrive] = max;
		return true;
	}

	int FillAllyOverdrives()
	{
		if (!BattleRunning())
			return 0;

		int filled = 0;
		// The ally array is 0..30 with the eight enemies at 20..27 inside it, so the
		// enemy band is skipped rather than relying on Present alone.
		for (int i = 0; i <= kBattleMaxUnitIndex; ++i)
		{
			if (i >= 20 && i <= 27)
				continue;
			if (!UnitPresent(i))
				continue;
			if (FillUnitOverdrive(i))
				++filled;
		}
		return filled;
	}

	int UnitActionState(int unitIndex)
	{
		BYTE value = 0;
		return Field8(BattleUnit(unitIndex), Unit::ActionState, &value) ? (int)value : -1;
	}

	// ---------------------------------------------------------------------------
	// The command queue
	// ---------------------------------------------------------------------------

	int BattleCommandQueueCount()
	{
		return SignedByteCount(Rva::BtlCmdQueueCount, Rva::BtlCmdQueueCapacity);
	}

	int BattleCommandQueueCapacity()
	{
		return Rva::BtlCmdQueueCapacity;
	}

	bool ReadBattleCommandSlot(int slot, BattleCommandRecord* out)
	{
		if (!out || slot < 0 || slot >= BattleCommandQueueCount())
			return false;
		BYTE* base = CommandQueueBase();
		if (!base)
			return false;
		return CopyRecord(base + (DWORD)slot * kBattleCommandBytes, out);
	}

	bool ReadBattleCommandForUnit(int unitIndex, BattleCommandRecord* out)
	{
		if (!out)
			return false;
		BYTE index = 0;
		if (!Field8(BattleUnit(unitIndex), Unit::CommandQueueIndex, &index))
			return false;
		if (index == kNoQueueSlot)
			return false;

		// The per unit index is written by the engine and trusted by its own accessor.
		// Bounds check it here, because an index past the end would read 72 bytes out
		// of whatever follows the queue, which is the turn queue counts.
		if ((int)index >= Rva::BtlCmdQueueCapacity)
			return false;

		BYTE* base = CommandQueueBase();
		if (!base)
			return false;
		return CopyRecord(base + (DWORD)index * kBattleCommandBytes, out);
	}

	bool ReadBattleLastCommandForUnit(int unitIndex, BattleCommandRecord* out)
	{
		BYTE* unit = BattleUnit(unitIndex);
		if (!unit || !out)
			return false;
		return CopyRecord(unit + Unit::LastCommand, out);
	}

	// ---------------------------------------------------------------------------
	// The turn queue
	// ---------------------------------------------------------------------------

	int BattleTurnQueueCount()
	{
		return SignedByteCount(Rva::BtlTurnQueueCount, kTurnQueueCapacity);
	}

	bool ReadBattleTurnEntry(int index, BattleTurnEntry* out)
	{
		if (!out || index < 0 || index >= BattleTurnQueueCount())
			return false;
		BYTE* base = TurnQueueBase();
		if (!base)
			return false;
		memcpy(out, base + (DWORD)index * kTurnEntryStride, sizeof(*out));
		return true;
	}

	bool ReadBattleTurnHead(BattleTurnEntry* out)
	{
		return ReadBattleTurnEntry(0, out);
	}

	bool ReleaseBattleTurnClaim(int unitIndex)
	{
		if (!UnitIndexInRange(unitIndex))
			return false;

		BYTE* unit = BattleUnit(unitIndex);
		BYTE turnIndex = 0;
		if (!Field8(unit, Unit::TurnQueueIndex, &turnIndex))
			return false;
		if (turnIndex == kNoQueueSlot || (int)turnIndex >= BattleTurnQueueCount())
			return false;

		BYTE* base = TurnQueueBase();
		if (!base)
			return false;

		BYTE* entry = base + (DWORD)turnIndex * kTurnEntryStride;
		if (Rd8(entry) != (BYTE)unitIndex)
			return false; // the unit's own index says this entry is not its own
		if (Rd8(entry + 3) == 0)
			return false; // not claimed, nothing to give back

		*(volatile BYTE*)(entry + 3) = 0;
		Log("battle: gave unit %d's turn back by clearing the claimed flag on turn entry "
		    "%u, so the menu will be offered again",
		    unitIndex, (unsigned)turnIndex);
		return true;
	}

	// ---------------------------------------------------------------------------
	// The menu, read only
	// ---------------------------------------------------------------------------

	bool ReadStagedBattleCommand(BattleCommandRecord* out)
	{
		if (!out)
			return false;
		return CopyRecord((const BYTE*)ModuleAddress(Rva::BtlMenuStagedCmd), out);
	}

	int StagedBattleGil()
	{
		DWORD value = 0;
		const BYTE* p = (const BYTE*)ModuleAddress(Rva::BtlMenuStagedGil);
		if (!Readable(p, 4))
			return 0;
		value = Rd32(p);
		return (int)value;
	}

	int BattleMenuOwnerUnit()
	{
		const BYTE* p = (const BYTE*)ModuleAddress(Rva::BtlMenuOwnerActor);
		if (!Readable(p, 1))
			return -1;
		const BYTE owner = Rd8(p);
		return (owner == kNoMenuOwner) ? -1 : (int)owner;
	}

	bool BattleMenuOpen()
	{
		const BYTE* p = (const BYTE*)ModuleAddress(Rva::BtlMenuOpenMask);
		if (!Readable(p, 4))
			return false;
		return Rd32(p) != 0;
	}

	// ---------------------------------------------------------------------------
	// Committing
	// ---------------------------------------------------------------------------

	int CommitBattleCommand(const BattleCommandRecord* record, int gilCost, unsigned variant)
	{
		if (!record)
			return 0;

		if (!BattleRunning())
		{
			Log("battle: refusing to commit a command for unit %u because no battle is "
			    "running, which would write into work RAM that is not allocated",
			    (unsigned)record->unit);
			return 0;
		}

		if (!UnitIndexInRange((int)record->unit))
		{
			// The engine checks this too, and then does nothing, which looks the same
			// as success from outside. Saying so is the point of checking it here.
			Log("battle: refusing a command for unit %u, which is past the %d the ally "
			    "record array holds",
			    (unsigned)record->unit, kBattleMaxUnitIndex);
			return 0;
		}

		if (variant > 1)
		{
			// FFX_Btl_ExecCommand raises the internal error "com" on this, which is a
			// hard engine error rather than a dropped action.
			Log("battle: refusing a command for unit %u with variant %u, which the "
			    "executor treats as an internal error",
			    (unsigned)record->unit, variant);
			return 0;
		}

		if (record->entryCount < 1 || record->entryCount > kBattleActionEntries)
		{
			// Count 0 is a real engine state, "no choice yet, go ask the AI", and it is
			// what FFX_Btl_SendMenu pushes as a placeholder. It is not something a mod
			// should be pushing, because the executor answers it by running the AI
			// script, and that is a decision each machine would then make for itself.
			Log("battle: refusing a command for unit %u with entry count %u. 0 means the "
			    "executor asks the AI, and anything over %d is off the end of the record.",
			    (unsigned)record->unit, (unsigned)record->entryCount, kBattleActionEntries);
			return 0;
		}

		if (BattleCommandQueueCount() >= Rva::BtlCmdQueueCapacity)
		{
			Log("battle: the command queue is full at %d, so unit %u's command is being "
			    "dropped. The engine would drop it too, silently.",
			    Rva::BtlCmdQueueCapacity, (unsigned)record->unit);
			return 0;
		}

		// A local copy, because FFX_Btl_CmdQueue_Push writes back over the header of the
		// record it is handed and the caller's record should come out unchanged.
		BattleCommandRecord scratch = *record;

		const int result =
		    Resolve<CommitCommandFn>(Rva::BtlCommitCommand)((BYTE*)&scratch, gilCost, variant);

		// CommitCommand returns 0 after a successful push and -1 when it did nothing.
		// That is the opposite way round from what reads naturally, so it is normalised
		// here and nowhere else.
		return (result == 0) ? 1 : -1;
	}

	// ---------------------------------------------------------------------------
	// Ability and item lookups
	// ---------------------------------------------------------------------------

	int BattleAbilityClassOf(int abilityId)
	{
		return (abilityId >> 12) & 0xF;
	}

	int BattleAbilityRowOf(int abilityId)
	{
		return abilityId & 0xFFF;
	}

	bool BattleAbilityIsItem(int abilityId)
	{
		return BattleAbilityClassOf(abilityId) == kBattleAbilityItem;
	}

	BYTE* BattleAbilityRecord(int abilityId, DWORD* outStringBase)
	{
		// Not a defensive flourish. Five of the eight class branches reach a table
		// pointer global and dereference it unchecked, so outside a battle this faults
		// inside the engine where nothing of ours can intervene.
		if (!BattleRunning())
			return NULL;

		// Three of the branches also dereference the out pointer without testing it, so
		// a real one always goes down even when the caller does not want the value.
		DWORD scratch = 0;
		BYTE* row = Resolve<GetAbilityRecordFn>(Rva::BtlGetAbilityRecord)(abilityId, &scratch);
		if (outStringBase)
			*outStringBase = scratch;

		if (!LooksLikePointer((DWORD)(UINT_PTR)row) || !Readable(row, 48))
			return NULL;
		return row;
	}

	const char* DescribeBattleCommand(const BattleCommandRecord* record, char* out, int bytes)
	{
		if (!out || bytes <= 0)
			return "";
		out[0] = 0;

		if (!record)
		{
			_snprintf_s(out, (size_t)bytes, _TRUNCATE, "no record");
			return out;
		}

		_snprintf_s(out, (size_t)bytes, _TRUNCATE, "unit %u variant %u entries %u",
		    (unsigned)record->unit, (unsigned)record->variant, (unsigned)record->entryCount);

		const int count = (record->entryCount > kBattleActionEntries)
		                      ? kBattleActionEntries
		                      : (int)record->entryCount;
		for (int i = 0; i < count; ++i)
		{
			char piece[96];
			const int id = (int)record->entries[i].abilityId;
			_snprintf_s(piece, sizeof(piece), _TRUNCATE, " | [%d] id 0x%04X %s row %d sub "
			                                             "0x%04X targets 0x%08lX",
			    i, (unsigned)id, AbilityClassName(BattleAbilityClassOf(id)),
			    BattleAbilityRowOf(id), (unsigned)record->entries[i].subId,
			    (unsigned long)record->entries[i].targetMask);

			// strncat_s with _TRUNCATE rather than strcat_s, deliberately. A full
			// record is four entries of about 60 characters plus the header, so a
			// caller with a 240 byte buffer would overflow, and strcat_s answers that
			// by calling the invalid parameter handler, which ends the process. A log
			// line is never worth a crash, so a short buffer loses the tail instead.
			strncat_s(out, (size_t)bytes, piece, _TRUNCATE);
		}
		return out;
	}

	// ---------------------------------------------------------------------------
	// The hooks
	// ---------------------------------------------------------------------------

	bool HookBattleMenuCommit(BattleMenuCommitFn callback)
	{
		menuCommitCallback = callback;

		if (menuCommitHookTried)
			return menuCommitSitesPatched == kMenuCommitSiteCount;
		menuCommitHookTried = true;

		for (int i = 0; i < kMenuCommitSiteCount; ++i)
		{
			const MenuCommitSite& site = kMenuCommitSites[i];
			if (PatchCallSite(menuCommitSites[i], site.functionRva + site.offset,
			        Rva::BtlCommitCommand, (void*)&MenuCommitThunk, site.name))
			{
				++menuCommitSitesPatched;
			}
		}

		if (menuCommitSitesPatched == kMenuCommitSiteCount)
		{
			Log("battle: all %d player commit sites hooked", kMenuCommitSiteCount);
		}
		else
		{
			Log("battle: only %d of %d player commit sites hooked. The ones that failed "
			    "will commit straight to the engine, which in a session means this "
			    "machine acting on a command the other one never hears about.",
			    menuCommitSitesPatched, kMenuCommitSiteCount);
		}
		return menuCommitSitesPatched == kMenuCommitSiteCount;
	}

	bool BattleMenuCommitHookInstalled()
	{
		return menuCommitSitesPatched > 0;
	}

	int BattleMenuCommitHookSites()
	{
		return menuCommitSitesPatched;
	}

	bool HookBattleMenuOpen(BattleMenuOpenFn callback)
	{
		menuOpenCallback = callback;

		if (menuOpenHookTried)
			return menuOpenSite.patched;
		menuOpenHookTried = true;

		// The one call to FFX_BtlMenu_Open in the binary, inside FFX_Btl_SendMenu. Given
		// as an address rather than scanned for, because PatchCallSite verifies the
		// opcode and the displacement before it writes, so a build that moved gets a
		// clean refusal with a log line instead of a wild jump.
		return PatchCallSite(menuOpenSite, Rva::BtlMenuOpenCallSite, Rva::BtlMenuOpen,
		    (void*)&MenuOpenThunk, "menu open");
	}

	bool BattleMenuOpenHookInstalled()
	{
		return menuOpenSite.patched;
	}

	// ---------------------------------------------------------------------------
	// Diagnostics
	// ---------------------------------------------------------------------------

	long BattleMenuCommitsSeen()
	{
		return commitsSeen;
	}

	long BattleMenuCommitsTaken()
	{
		return commitsTaken;
	}

	long BattleMenuOpensSuppressed()
	{
		return opensSuppressed;
	}

	void LogBattleState()
	{
		Log("=== battle state ===");
		Log("phase %d, %s, menu %s owner %d", BattlePhase(),
		    BattleRunning() ? "actors allocated" : "NO BATTLE",
		    BattleMenuOpen() ? "open" : "shut", BattleMenuOwnerUnit());

		if (!BattleRunning())
		{
			Log("nothing else to read: the actor arrays live behind a pointer that is null "
			    "outside a battle");
			return;
		}

		Log("commands queued %d of %d, turns queued %d", BattleCommandQueueCount(),
		    BattleCommandQueueCapacity(), BattleTurnQueueCount());

		BattleTurnEntry head;
		if (ReadBattleTurnHead(&head))
		{
			Log("turn head: unit %u variant %u real %u claimed %u prio %u",
			    (unsigned)head.unit, (unsigned)head.variant, (unsigned)head.realTurn,
			    (unsigned)head.claimed, (unsigned)head.priority);
		}
		else
		{
			Log("turn head: none, so nobody's turn is being handed out");
		}

		for (int slot = 0; slot < kBattleActiveSlots; ++slot)
		{
			const int unit = UnitForBattleSlot(slot);
			if (unit < 0)
			{
				Log("slot %d: empty", slot);
				continue;
			}
			Log("slot %d: unit %d  mp %d  overdrive %d/%d  ctb %d  state %d  %s%s%s%s",
			    slot, unit, UnitCurrentMp(unit), UnitOverdrive(unit),
			    UnitOverdriveMax(unit), UnitCtbCounter(unit), UnitActionState(unit),
			    UnitPresent(unit) ? "present " : "absent ",
			    UnitKo(unit) ? "KO " : "", UnitReady(unit) ? "ready " : "",
			    UnitIsAiControlled(unit) ? "AI DRIVEN" : "player driven");
		}

		char line[320];
		BattleCommandRecord staged;
		if (ReadStagedBattleCommand(&staged))
		{
			Log("staged: %s, gil %d", DescribeBattleCommand(&staged, line, sizeof(line)),
			    StagedBattleGil());
		}

		Log("commit hook: %d of %d sites, %ld commits seen, %ld taken over",
		    menuCommitSitesPatched, kMenuCommitSiteCount, commitsSeen, commitsTaken);
		Log("open hook: %s, %ld menus suppressed",
		    menuOpenSite.patched ? "installed" : "NOT installed", opensSuppressed);
	}

	void LogBattleQueues()
	{
		if (!BattleRunning())
		{
			Log("battle queues: no battle");
			return;
		}

		char line[320];
		const int commands = BattleCommandQueueCount();
		Log("=== battle queues: %d commands, %d turns ===", commands, BattleTurnQueueCount());

		for (int i = 0; i < commands; ++i)
		{
			BattleCommandRecord record;
			if (!ReadBattleCommandSlot(i, &record))
				continue;
			Log("cmd %2d: %s", i, DescribeBattleCommand(&record, line, sizeof(line)));
		}

		const int turns = BattleTurnQueueCount();
		for (int i = 0; i < turns; ++i)
		{
			BattleTurnEntry entry;
			if (!ReadBattleTurnEntry(i, &entry))
				continue;
			Log("turn %2d: unit %u variant %u claimed %u prio %u", i, (unsigned)entry.unit,
			    (unsigned)entry.variant, (unsigned)entry.claimed, (unsigned)entry.priority);
		}
	}

} // namespace ffx
