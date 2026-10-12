#pragma once

#include <windows.h>

// Place names: the 349 row table that calls a map "Besaid Village", and the save field
// that remembers which one the player is standing in.
//
// This is the only human readable name FFX has for a location, and it is the only name
// available for an NPC either, because the game ships no NPC name table at all. See
// ffx/EventPlaces.h, which reads the table and the event packages directly rather than
// calling any of this.
//
// THE ID LIVES IN THE SAVE BLOCK at +0x0E, with a scripted override at +0xD4 guarded by
// a flag at +0xD6. Read it through SaveDataGetMapNameId, not straight off +0x0E, or a
// scripted rename is missed.
//
// WHERE THE STRINGS COME FROM. macrodic.dcp opens with a directory of sixteen u32, and
// place names are entry 11, which is file offset 0x2C. A section is a u16 holding four
// times its row count, then that many bytes of four byte rows, and each row is two u16
// offsets relative to the section base. The string offset for one row is
//
//     base + 4 * id + 2 * alt        alt = (g_ffxSaveDataOptionFlags >> 3) & 1
//
// so the array starts at base + 0 and row 0's first offset is the count field itself,
// which works out because the first string sits immediately after a 4 * rows array. The
// two offsets per row are the same string in every row of new_uspc and different in
// every row of jppc.
//
// Text is the usual FFX glyph encoding, so it needs ffx::DecodeKernelText.

namespace ffx
{
	namespace Rva
	{

		// ---------------------------------------------------------------------------
		// The save side
		// ---------------------------------------------------------------------------

		// int __cdecl (). The id of the place the player is in, 0 for none, which is
		// also row 0 and reads "None". Returns the u16 at saveData+0xD4 when the byte at
		// +0xD6 is set, otherwise the u16 at +0x0E. Both are read with movzx, so the id
		// is unsigned everywhere and there is no -1 sentinel anywhere in this subsystem.
		const DWORD SaveDataGetMapNameId = 0x0046C410;

		// char* __cdecl (int id, ...). The string for an id out of the table below.
		//
		// Clamps with "id = (id >= rowCount) ? 0 : id" against the row count read live
		// from the section header, so an out of range id reads "None" rather than
		// faulting. The compare is signed, so a NEGATIVE id is not clamped and reads in
		// front of the table. Nothing in the engine can produce one.
		const DWORD SaveDataMapNameIdToString = 0x0046C430;

		// int __cdecl (int groundDic). Writes saveData+0x0E from the loaded event
		// package's own table. This is what decides the place name on every map.
		//
		// Reads the u16 at atel+0x1E. With bit 15 clear that value IS the id. With it
		// set, the low 15 bits are a count and the u32 at atel+0x28 is an offset from
		// the atel base to a u16 array of that many ids, indexed by the ground dic.
		//
		// EVERY SHIPPED PACKAGE IS ONE OF TWO SHAPES: 347 hold 0x8010, a table of
		// sixteen, and the other 50 hold 0, meaning no place. The index is only two bits
		// wide, so entries 4 to 15 are never reached and in practice hold a copy of
		// entry 0. The clamp is "idx = (dic >= count) ? 0 : dic", signed again.
		const DWORD SaveDataSetMapNameIdFromGroundDic = 0x00470EF0;

		// The scripted override, from two atel syscall handlers. Set takes an id, clear
		// also runs on map unload and from the save UI step.
		const DWORD SaveDataSetMapNameOverride = 0x00470140;
		const DWORD SaveDataClearMapNameOverride = 0x004638B0;

		// int __cdecl (). (g_ffxSaveDataOptionFlags >> 3) & 1, which picks between the
		// two strings a row holds. 0 and 1 are the same text in the English table.
		const DWORD SaveDataGetMapNameAltFlag = 0x003851F0;

		// int __thiscall (void* ground). (*(u32*)(ground + 0x828) >> 11) & 3, so the
		// ground dic is a TWO BIT field and only entries 0 to 3 of a package's table can
		// ever be selected. Callers pass a literal 0 when the character has no ground.
		const DWORD MapGroundAttrGetDic = 0x0043D7A0;

		// ---------------------------------------------------------------------------
		// The menu resource side, which owns the loaded macrodic
		// ---------------------------------------------------------------------------

		// void* __cdecl (). The place name section, as "mov eax, [eax+2Ch]" on the
		// loaded macrodic, which is directory entry 11.
		const DWORD MenuResGetMapNameTable = 0x004B7F60;

		// Loads macrodic.dcp and REWRITES ITS DIRECTORY IN PLACE. Walks offsets 0 to
		// 0x3C in steps of four and replaces each non-zero entry with blob + entry, so
		// after this runs the directory holds pointers and not offsets. Anything reading
		// the loaded copy has to know that. The file on disk holds offsets.
		const DWORD MenuResLoadMacroDic = 0x004B85C0;

		// void* __cdecl (int sectionId). The same directory lookup done as
		// "[bank + 4 * sectionId - 0x4C]", which is a script visible section id biased
		// by 19. So script id 30 is directory entry 11, the place names.
		const DWORD MenuResGetDicSection = 0x004B70A0;

		// ---------------------------------------------------------------------------
		// DO NOT USE THIS ONE. It is a second reader of the same table and it is wrong.
		//
		// It resolves a row's u16 against the ROW ADDRESS rather than the section base:
		// "lea ecx, [eax+esi*4]" then "movzx eax, [ecx+2]" then "add eax, ecx". The
		// shipped data is section base relative, so this overshoots by 4 * id and is
		// only correct for id 0. There is no bounds check either. It is reached when
		// FFX_GetLanguage() is not 0, so the English build never runs it.
		//
		// Listed so that nobody takes it for evidence about the format, and so a future
		// localisation pass knows there is a real engine bug here to work around.
		// ---------------------------------------------------------------------------
		const DWORD MenuResAppendMapNameToBufBroken = 0x004B43E0;

		// Builds "/ffx_ps2/ffx/master/new_<lang>/menu/<name>.dcp" with two sprintf
		// calls and loads it. The language switch is case 0 jppc, 2 frpc, 3 sppc,
		// 4 depc, 5 itpc, 9 krpc, 10 chpc, and everything else uspc, all of them with
		// the "new_" prefix. All ten roots ship a macrodic.
		//
		// ffx/EventPlaces.h hardcodes the uspc path. That is a known English only
		// shortcut and this is the call that would fix it.
		const DWORD LoadLocalizedBin = 0x0048CAC0;

		// ---------------------------------------------------------------------------
		// How a model gets placed, which is the other half of ffx/EventPlaces.h
		// ---------------------------------------------------------------------------

		// The atel syscall core:1 handler. Pops a chr id, stores it at actor+0xA8 and
		// sets actor+0xAA to 1. There is NO model field in an .ebp: a package names its
		// models by calling this, so the authored ids are the operands of the pushimm in
		// front of every core:1 call site. 5689 of the 5690 sites in the shipped
		// packages have one, the exception being a pushvar in the dev package test02.
		const DWORD AtelOpSetActorModel = 0x0045D000;

		// Gates on actor+0xAA == 1, so it is what actually turns that stored id into a
		// loaded character.
		const DWORD FieldChrRegTask = 0x00461850;

		// ---------------------------------------------------------------------------
		// Save block offsets, which are not addresses and so are not in the list below
		// ---------------------------------------------------------------------------
		const DWORD SaveMapNameId = 0x0E;          // u16
		const DWORD SaveMapNameOverride = 0xD4;    // u16
		const DWORD SaveMapNameOverrideSet = 0xD6; // BYTE, non-zero means use +0xD4

		// macrodic.dcp layout, also not addresses.
		const DWORD DcpDirectoryEntries = 16;
		const DWORD DcpPlaceNameEntry = 11; // file offset 0x2C
		const DWORD DcpRecordBytes = 4;
		const DWORD DcpPlaceNameRows = 349;

		// An .ebp atel block, same reason.
		const DWORD AtelPlaceNameField = 0x1E; // u16, bit 15 means "a table follows"
		const DWORD AtelPlaceNameTable = 0x28; // u32, offset from the atel base
		const DWORD AtelActorModelChrId = 0xA8;
		const DWORD AtelActorModelSet = 0xAA;

		inline const DWORD* EventPlacesRvaList(int* count)
		{
			static const DWORD list[] = {
				SaveDataGetMapNameId,
				SaveDataMapNameIdToString,
				SaveDataSetMapNameIdFromGroundDic,
				SaveDataSetMapNameOverride,
				SaveDataClearMapNameOverride,
				SaveDataGetMapNameAltFlag,
				MapGroundAttrGetDic,
				MenuResGetMapNameTable,
				MenuResLoadMacroDic,
				MenuResGetDicSection,
				MenuResAppendMapNameToBufBroken,
				LoadLocalizedBin,
				AtelOpSetActorModel,
				FieldChrRegTask,
			};

			if (count)
				*count = (int)(sizeof(list) / sizeof(list[0]));

			return list;
		}
	} // namespace Rva
} // namespace ffx
