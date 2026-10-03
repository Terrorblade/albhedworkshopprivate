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

		bool AnimateSlotMatches()
		{
			DWORD found = *(volatile DWORD*)workshop::ModuleAddress(Rva::AnimateVtableSlot);
			DWORD expected = (DWORD)(UINT_PTR)workshop::ModuleAddress(Rva::AnimateExpected);
			Log("  vtable slot animate = 0x%08X expected 0x%08X %s",
			    found, expected, found == expected ? "OK" : "MISMATCH");
			return found == expected;
		}

		// The frame slot that latches the input mask, three slots past animate. Same
		// cheap test: if the vtable entry does not hold the function we expect, this is
		// not the build these addresses came from. Checked separately from animate
		// because input injection depends on this one specifically.
		bool UpdateSlotMatches()
		{
			DWORD found = *(volatile DWORD*)workshop::ModuleAddress(Rva::UpdateVtableSlot);
			DWORD expected = (DWORD)(UINT_PTR)workshop::ModuleAddress(Rva::UpdateExpected);
			Log("  vtable slot update  = 0x%08X expected 0x%08X %s",
			    found, expected, found == expected ? "OK" : "MISMATCH");
			return found == expected;
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
