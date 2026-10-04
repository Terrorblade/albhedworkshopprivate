#include "ffx/VerifyLayout.h"

#include <windows.h>

#include "ffx/Addresses.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

using workshop::Log;

namespace ffx
{
	namespace
	{

		// Every address area the library knows, checked against SizeOfImage so a typo
		// or a wrong build is caught at startup rather than by a fault later.
		//
		// Each area file in include/ffx/addresses/ owns its own list, which is the whole
		// point: adding a subsystem is a new file plus one line here, and two people
		// mapping two subsystems never edit the same list.
		typedef const DWORD* (*RvaListFn)(int* count);

		struct AddressArea
		{
			const char* name;
			RvaListFn list;
		};

		const AddressArea addressAreas[] = {
			{ "character", &Rva::CharacterRvaList },
			{ "input", &Rva::InputRvaList },
			{ "main loop", &Rva::MainLoopRvaList },
			{ "game state", &Rva::GameStateRvaList },
			{ "atel", &Rva::AtelRvaList },
			{ "esc menu", &Rva::EscMenuRvaList },
			{ "menu system", &Rva::MenuSystemRvaList },
			{ "cutscene", &Rva::CutsceneRvaList },
			{ "encounter", &Rva::EncounterRvaList },
			{ "world state", &Rva::WorldStateRvaList },
			{ "battle", &Rva::BattleRvaList },
			{ "minigames", &Rva::MinigamesRvaList },
			{ "magic dll", &Rva::MagicDllRvaList },
			{ "data file", &Rva::DataFileRvaList },
			{ "debug", &Rva::DebugRvaList },
			{ "memory", &Rva::MemoryRvaList },
			// Add new areas here, one line each, after adding the include to
			// include/ffx/Addresses.h.
		};

		// Every address called through a function pointer, so the prologue check has a
		// name to report against.
		struct NamedFunction
		{
			const char* name;
			DWORD rva;
		};

		const NamedFunction calledFunctions[] = {
			{ "ChAllocate", Rva::ChAllocate },
			{ "ChDisposeIfLive", Rva::ChDisposeIfLive },
			{ "ChIsLive", Rva::ChIsLive },
			{ "ChCountLive", Rva::ChCountLive },
			{ "ChRomRead", Rva::ChRomRead },
			{ "ChDataReadSync", Rva::ChDataReadSync },
			{ "ChMotionSetReadStart", Rva::ChMotionSetReadStart },
			{ "ChMotionSetReadSync", Rva::ChMotionSetReadSync },
			{ "ChLoadMotionSetSync", Rva::ChLoadMotionSetSync },
			{ "ChSetByte184", Rva::ChSetByte184 },
			{ "ChSetPartyIndex", Rva::ChSetPartyIndex },
			{ "ChSetPos", Rva::ChSetPos },
			{ "ChGetPos", Rva::ChGetPos },
			{ "ChSetRot", Rva::ChSetRot },
			{ "ChSetMoveSpeed", Rva::ChSetMoveSpeed },
			{ "ChSetMoveDir", Rva::ChSetMoveDir },
			{ "ChSetFlags1Bit400", Rva::ChSetFlags1Bit400 },
			{ "ChGetPlayerChr", Rva::ChGetPlayerChr },
			{ "ChWalkmeshMove", Rva::ChWalkmeshMove },
			{ "ChSetGroundMode", Rva::ChSetGroundMode },
			// The player driver and the camera read it anchors to, both called through a
			// pointer by ffx::StepPlayerControlFor. The driver's CALL SITE and the four
			// camera call sites are deliberately NOT here: they are mid-function
			// addresses and would fail a prologue check for being exactly right.
			{ "PlayerStepControl", Rva::PlayerStepControl },
			{ "PlayerCameGetYawThunk", Rva::PlayerCameGetYawThunk },
			{ "InputGet", Rva::InputGet },
			{ "InputPoll", Rva::InputPoll },
			{ "InputSetPollOverride", Rva::InputSetPollOverride },
			{ "PadReadButtons16", Rva::PadReadButtons16 },
			{ "PadReadAnalogByte", Rva::PadReadAnalogByte },
			{ "AtelSelectContext", Rva::AtelSelectContext },
			{ "AtelRestoreContext", Rva::AtelRestoreContext },
			{ "AtelGetActor", Rva::AtelGetActor },
			{ "AtelGetActorKind", Rva::AtelGetActorKind },
			{ "AtelGetActorPos", Rva::AtelGetActorPos },
			{ "AtelFireActorEvent", Rva::AtelFireActorEvent },
			{ "AtelFireActorEventSync", Rva::AtelFireActorEventSync },
			{ "AtelGetEventChannel", Rva::AtelGetEventChannel },
			{ "AtelGetEventScriptEntry", Rva::AtelGetEventScriptEntry },
			{ "AtelSetPlayerActorId", Rva::AtelSetPlayerActorId },
			{ "SaveDataAddItem", Rva::SaveDataAddItem },
			{ "SaveDataGetItemCount", Rva::SaveDataGetItemCount },
			{ "SaveDataSetGil", Rva::SaveDataSetGil },
			{ "SaveDataSpendGil", Rva::SaveDataSpendGil },
			{ "SaveDataTestKeyItemFlag", Rva::SaveDataTestKeyItemFlag },
			{ "SaveDataSetKeyItemFlag", Rva::SaveDataSetKeyItemFlag },
			{ "SaveDataGetEquipEntry", Rva::SaveDataGetEquipEntry },
			{ "SaveDataSetCharEquip", Rva::SaveDataSetCharEquip },
			{ "SaveDataAddEquipEntry", Rva::SaveDataAddEquipEntry },
			{ "SaveDataCountEquipEntries", Rva::SaveDataCountEquipEntries },
			{ "SaveDataGetCharBaseStats", Rva::SaveDataGetCharBaseStats },
			{ "SaveDataGetCharCurrentStats", Rva::SaveDataGetCharCurrentStats },
			{ "SaveDataGetCaptureCount", Rva::SaveDataGetCaptureCount },
			{ "SaveDataAddCaptureCount", Rva::SaveDataAddCaptureCount },
			{ "MainStepIsFadeBlocking", Rva::MainStepIsFadeBlocking },
			{ "PlayerGetSubStepCount", Rva::PlayerGetSubStepCount },
			{ "PlayerReadPad", Rva::PlayerReadPad },
		};

		bool EveryAddressInImage()
		{
			const int areas = (int)(sizeof(addressAreas) / sizeof(addressAreas[0]));
			int checked = 0;

			for (int a = 0; a < areas; ++a)
			{
				int count = 0;
				const DWORD* list = addressAreas[a].list(&count);
				for (int i = 0; i < count; ++i)
				{
					if (!workshop::RvaInImage(list[i]))
					{
						Log("  guard: %s RVA 0x%08X is past SizeOfImage 0x%08X",
						    addressAreas[a].name, list[i], workshop::ModuleImageSize());
						return false;
					}
				}
				checked += count;
			}

			Log("  guard: %d addresses across %d areas are inside the image", checked, areas);
			return true;
		}

		// ---------------------------------------------------------------------------
		// A vtable slot test that survives another plugin getting there first.
		//
		// THIS USED TO BE A BARE "found == expected" AND IT DISABLED PILGRIMAGE
		// ENTIRELY whenever the cheats plugin was also installed. Load order put
		// cheats first, cheats hooked the animate slot, and then Pilgrimage's layout
		// check read 0x6B957570, saw that it was not the engine function, and logged
		// LAYOUT CHECK FAILED, nothing will be hooked. Both plugins were fine. The
		// check was wrong.
		//
		// The point of the test is "are these the addresses for this build". A slot
		// that has already been detoured cannot answer that question either way, so
		// the honest result is to say who holds it and carry on, not to fail.
		//
		// A slot still pointing somewhere else INSIDE the host image is a real
		// failure, because no hook does that and a different engine function sitting
		// there means a different build.
		// ---------------------------------------------------------------------------
		bool SlotMatchesOrIsHooked(DWORD slotRva, DWORD expectedRva, const char* what)
		{
			const DWORD found = *(volatile DWORD*)workshop::ModuleAddress(slotRva);
			const DWORD expected = (DWORD)(UINT_PTR)workshop::ModuleAddress(expectedRva);

			if (found == expected)
			{
				Log("  vtable slot %-7s = 0x%08X expected 0x%08X OK", what, found, expected);
				return true;
			}

			if (workshop::InsideImage((const void*)(UINT_PTR)found))
			{
				Log("  vtable slot %-7s = 0x%08X expected 0x%08X MISMATCH, and it points "
				    "into the game itself, so these addresses are for a different build",
				    what, found, expected);
				return false;
			}

			// Outside the image, so something detoured it. Name the owner.
			wchar_t owner[MAX_PATH] = { 0 };
			HMODULE module = NULL;
			if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
			            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			        (LPCWSTR)(UINT_PTR)found, &module)
			    && module)
			{
				GetModuleFileNameW(module, owner, MAX_PATH);
			}

			if (owner[0] == 0)
			{
				Log("  vtable slot %-7s = 0x%08X is hooked by something with no module, "
				    "probably a generated trampoline. Accepting it, since a detoured slot "
				    "cannot confirm or deny the build.",
				    what, found);
				return true;
			}

			Log("  vtable slot %-7s = 0x%08X is already hooked by %S. That is expected "
			    "when another workshop plugin loaded first, so it is not a failure.",
			    what, found, owner);
			return true;
		}

		bool AnimateSlotMatches()
		{
			return SlotMatchesOrIsHooked(Rva::AnimateVtableSlot, Rva::AnimateExpected, "animate");
		}

		// The frame slot that latches the input mask, three slots past animate. Checked
		// separately from animate because input injection depends on this one
		// specifically.
		bool UpdateSlotMatches()
		{
			return SlotMatchesOrIsHooked(Rva::UpdateVtableSlot, Rva::UpdateExpected, "update");
		}

		bool EveryCallTargetLooksCallable()
		{
			const int count = (int)(sizeof(calledFunctions) / sizeof(calledFunctions[0]));
			bool allGood = true;
			int thunks = 0;

			for (int i = 0; i < count; ++i)
			{
				const char* name = calledFunctions[i].name;
				const DWORD rva = calledFunctions[i].rva;
				const BYTE* bytes = (const BYTE*)workshop::ModuleAddress(rva);

				const BYTE* thunkTarget = NULL;
				const bool isThunk = workshop::IsJumpThunk(rva, &thunkTarget);
				if (isThunk)
					++thunks;

				if (!workshop::LooksLikeFunctionStart(rva))
				{
					Log("  guard: %s at RVA 0x%08X starts %02X %02X %02X %02X %02X, "
					    "not a prologue and not a resolvable thunk",
					    name, rva, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4]);
					allGood = false;
				}
				else if (isThunk)
				{
					// Worth logging, because a thunk is exactly what broke this check once.
					Log("  note: %s is a jmp thunk to 0x%08X (RVA 0x%08X), which is fine",
					    name, (unsigned)(UINT_PTR)thunkTarget,
					    (unsigned)(thunkTarget - workshop::ModuleBase()));
				}
			}
			Log("  checked %d call targets, %d of them jmp thunks, result: %s",
			    count, thunks, allGood ? "PASS" : "FAIL");
			return allGood;
		}

	} // namespace

	bool VerifyLayout()
	{
		if (!workshop::ReadPeHeaders())
			return false;
		if (!EveryAddressInImage())
			return false;
		if (!AnimateSlotMatches())
			return false;
		if (!UpdateSlotMatches())
			return false;
		return EveryCallTargetLooksCallable();
	}

} // namespace ffx
