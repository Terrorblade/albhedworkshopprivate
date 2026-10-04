#pragma once

#include <windows.h>

// Transplanting the world: handing one machine's entire save state to another, and moving
// it to the right map afterwards.
//
// Derived in reversing\WORLD_STATE.md. The short version of why this is cheap:
//
//  - The 0x68C0 save block is PURE DATA. BSS, no relocations, no instruction anywhere
//    stores an address into it, and it round-trips through a file byte for byte under
//    ASLR. It goes on the wire as-is, no serialisation.
//  - The game transplants it with a plain memcpy in FOUR places, one of which is a script
//    opcode whose entire body is the memcpy. So the naive approach is the shipped
//    approach.
//  - Everything derived from it is rebuilt by ONE call,
//    FFX_SphereGrid_RecomputeDerived.
//  - The map change is a DEFERRED REQUEST consumed inside FFX_MainStep one step later, so
//    it lands on a step boundary both peers can agree on. That is the lockstep shape.
//
// THE RECIPE, which is what this area exists to serve:
//
//   1. host:   snapshot the block, 0x68C0 bytes
//   2. client: memcpy it over g_ffxSaveData
//   3. client: byte +0x2A = 1, the game's own "loaded from file" flag
//   4. client: ReencodeCharNames, ONLY if the two machines run different languages
//   5. client: SphereGridRecomputeDerived
//   6. client: MapRequestResumeFromCheckpoint, or MapRequestChange for a specific map
//   7. client: place the character, because the block only names a doorway

namespace ffx
{
	namespace Rva
	{
		// ---------------------------------------------------------------------------
		// The transplant
		// ---------------------------------------------------------------------------

		// NOT DECLARED HERE. The block base is Rva::SaveData in GameState.h, which owns it,
		// and its size is ffx::SaveBlock::Size. The whole transplant is a memcpy of that
		// range, so no function is needed for step 2 of the recipe.

		// unsigned char __cdecl (void *dest, int saveImage). memcpy(dest, saveImage + 64,
		// 0x68C0) then the name re-encode. Listed for the record rather than to be called,
		// because it expects a file image with a 64-byte header and a mod has the bare
		// block.
		const DWORD SaveFileInstallBlock = 0x004B54A0;

		// unsigned char __cdecl (void). Re-encodes the 18 name records at +0x634C for the
		// running language. PURE TEXT, no gameplay state, so two machines on the same
		// language can skip it entirely.
		const DWORD SaveDataReencodeCharNames = 0x00387470;

		// THE one re-derive. Rebuilds g_ffxEquipStatBonus from the sphere grid, then every
		// character's derived stats, effective stats, ability masks and HP/MP clamps.
		// FFX_SaveFile_CommitLoad calls it. FFX_Debug_ApplyViewerSave skips it, which is
		// exactly why that one is not a safe model to copy.
		const DWORD SphereGridRecomputeDerived = 0x00654860;

		// Zeroes the equip stat bonus cache. FFX_Btl_Init calls this and nothing on the
		// battle path visibly refills it, which is recorded as unsettled in WORLD_STATE.md.
		// Calling SphereGridRecomputeDerived on BOTH machines at the same simulation step
		// is safe whatever the answer turns out to be. Calling it on one is not.
		const DWORD SaveDataClearEquipStatBonus = 0x00398820;

		// NOT DECLARED HERE. g_ffxEquipStatBonus is Rva::EquipStatBonus in GameState.h.

		// The ATEL script opcode whose whole body is memcpy(&g_ffxSaveData, arg + 64,
		// 0x68C0) and return 1. Library 12 function 87. Used by exactly one shipped
		// package, a developer test map. Proof that the block is self-contained, and not
		// something to call.
		const DWORD AtelSysSave087 = 0x004781F0;

		// Consumes the byte +0x2A "loaded from file" flag once, taking the previous-scene
		// fields from the +0xC0 history rather than from stale live values. Setting that
		// byte after a transplant removes a whole class of transition bug for one byte.
		const DWORD SaveDataSetSceneAndSub = 0x0048EAF0;

		// ---------------------------------------------------------------------------
		// Moving to a map. All deferred, all consumed inside FFX_MainStep.
		// ---------------------------------------------------------------------------

		// void __cdecl (int mapId, char entryPoint). Five stores and a flag, NO GATE of any
		// kind. This is the one a mod should call.
		//
		// Three special cases: mapId == g_ffxQuitPseudoMapId (399) means "leave the field"
		// and only raises a different flag, mapId == 23 is the title screen and runs extra
		// teardown, and a NEGATIVE mapId means "go to the checkpoint" using +0xBA and +0xB8.
		const DWORD MapRequestChange = 0x0048EA60;

		// void __cdecl (void). "Put me where the save block says I am." Unpacks the
		// checkpoint at +0xB8/+0xBA into the live fields, pulls the previous map out of the
		// +0xC0 history, and raises the deferred request. Exactly the post-transplant step
		// a joining client needs, and the game already contains it.
		const DWORD MapRequestResumeFromCheckpoint = 0x0048DD60;

		// Sets both delay counters, which is "wait n simulation steps before loading".
		const DWORD MapSetTransitionFrames = 0x0048EB50;

		// The full warp: unbind the player CHR, request the change, cancel a pending battle,
		// set the fade, set the transition frames.
		//
		// GATED, but the gate is one call. It opens with
		//     if (*(char *)g_ffxAtelCtx >= 0 && !FFX_IsDebugMode()) return;
		// and then CLEARS that bit, so it needs bit 0x80 of the ATEL context's byte 0 set
		// and it consumes it. Call MapArmWarpGate first, which is what the engine's own
		// warp path does. An earlier note here said DO NOT CALL, which was too strong.
		const DWORD MapWarpTo = 0x0046FEC0;

		// Sets the gate bit and ctx+0x210 bit 2. Three instructions, no gate, no side
		// effects. One of only two setters of that bit in the binary.
		const DWORD MapArmWarpGate = 0x004729C0;

		// int (int mapId, char entryPoint, int useSavedFade). PREFER THIS. Same body as
		// MapWarpTo, and it is what every door, save point and airship destination reaches
		// through ATEL Core 17, 171, 267 and 268. Same gate.
		const DWORD MapWarpToWithSavedFade = 0x0046FF40;

		// void (float). One float store into MapScreenFadeRate. MapWarpTo passes -1/15.
		const DWORD MapSetScreenFadeRate = 0x00477450;
		const DWORD MapScreenFadeRate = 0x00F28310;

		// void (void). 20 teardown calls then MapWarpTo(23, 0). The other setter of the
		// gate bit, and it sets it on context 0 specifically.
		const DWORD MapReturnToTitle = 0x0046DA10;

		// What AtelStepOnce calls once the deferred request matures. Unloads the old map,
		// re-inits the camera, aborts an FMV, loads the package, snapshots the location
		// history. NOT a call a mod makes, listed so nobody mistakes EvLoadEventPackage
		// (addresses/Minigames.h) for the whole job.
		const DWORD SceneInit = 0x0048E070;

		// Raised when mapId == QuitPseudoMapId. FFX_MainStep consumes it.
		const DWORD MapLeaveFieldRequest = 0x00F27108;

		// The AutoTestManager stdin command "JumpMap <mapname>". DOES NOT WORK IN RETAIL,
		// twice over: the name table it searches is empty unless LoadEventIdTable has run,
		// and the stdin command queue is never drained because AutoTestDrainQueue has zero
		// callers. See reversing\CHEAT_WARP.md section 8. Goes through MapWarpTo.
		const DWORD AutoTestJumpMap = 0x00508100;

		// The deferred request state. MapChangePending is the flag FFX_Atel_StepOnce polls.
		const DWORD MapChangePending = 0x00F3084C;
		const DWORD MapChangeDelayFrames = 0x00F30850;
		const DWORD MapChangeDelayFlag = 0x00F30854;
		const DWORD SceneLoaded = 0x00F30858;

		// A constant 0x18F, 399. Equal to this means "leave the field", and -1 means the
		// resume path refuses and raises the leave-field handshake instead.
		const DWORD QuitPseudoMapId = 0x00833480;

		// Lets MapWarpTo past its consuming gate. Read only here, because turning on the
		// game's debug mode has effects well beyond this one function.
		const DWORD DebugMode = 0x00F3C910;

		// ---------------------------------------------------------------------------
		// WHICH map, and WHERE IN IT. Derived in reversing\CHEAT_WARP.md.
		//
		// There is ONE id space, 0..401, and it is both the map list and the event list.
		// A warp picks (packageId, entryPoint) and nothing else. The background map
		// number is DERIVED from the package, not chosen.
		// ---------------------------------------------------------------------------

		// int (void). Save word +0x00. The EVENT/PACKAGE id, i.e. what MapRequestChange
		// takes. This is the kit's mapId.
		const DWORD SaveDataGetMapId = 0x0048D660;

		// int (void). Save byte +0x0C. The doorway index, i.e. the second warp argument.
		const DWORD SaveDataGetEntryPoint = 0x0048D670;

		// int (void). Save word +0x04. The BACKGROUND MAP number, written by the load path
		// from the package's own entry record. Not a warp argument.
		const DWORD SaveDataGetSceneId = 0x0048D690;

		// ---------------------------------------------------------------------------
		// THE MAP/EVENT LIST. Asset kind 12 of the game's own path table, which is always
		// populated, unlike the name table below.
		//
		//   loader = GetAssetLoader();  loader[4](12);
		//   path   = loader[22](AssetEventStride * id);   // nullptr for an unused id
		//
		// A path of nullptr means the id has no package. LOADING ONE OF THOSE SPINS THE
		// GAME IN while(1) INSIDE EvLoadEventPackage. 330 of the 402 ids are usable.
		// ---------------------------------------------------------------------------
		const DWORD GetAssetLoader = 0x0036D0D0;   // void **(void), returns AssetLoaderTable
		const DWORD AssetLoaderTable = 0x01F10C40; // void *[37]. 4 select kind, 5 size, 22 path, 0 read

		const int AssetKindEventObj = 12;  // the event/obj asset class
		const int AssetEventStride = 18;   // path-table entries per event, sub-index 0 is the .ebp
		const int EventIdCount = 402; // the whole id space

		// TWO DIFFERENT COUNTS, and they answer different questions. 330 is what this
		// header's own filter yields: an id needs a live kind-12 path table entry AND to
		// be absent from the baked missing-file list, and only 348 ids have a path entry
		// at all. 376 is the wider measurement taken straight off the archive, which is
		// every named id whose .ebp basename ships whether or not the path table knows
		// about it. The stricter number is the one a picker wants, because an id with no
		// path entry cannot be resolved to a file in process anyway.
		const int EventIdUsableCount = 330;    // path entry present AND the file ships
		const int EventIdShippedCount = 376;   // named ids whose .ebp exists in the archive

		// Slot 22 returns Rva::AssetResolvedPathBuf, declared in addresses/Minigames.h.
		// It is ONE shared 255-byte static, so copy the string out before the next call.

		// The two table slots as DIRECT functions, which is how a mod should call them. On PC
		// FFX_Asset_InstallPcLoaderSlots puts FFX_Asset_GetPathForIndex in slot 22, and slot 4
		// is the same libmscd selector on both platforms, so neither needs the table and
		// neither is a vtable call. Both are plain cdecl taking one int.
		//
		//   if (AssetSelectKind(AssetKindEventObj) < 0) give up;
		//   const char* path = AssetGetPathForIndex(AssetEventStride * eventId);
		//
		// SELECTING A KIND IS A WRITE TO SHARED STATE. The selector stores the kind base in
		// slot 8 and the kind in slot 9, and the engine's own loads read them, so save both
		// and put them back when you are done enumerating.
		const DWORD AssetSelectKind = 0x0036C510;       // int (int kind), the base or -1
		const DWORD AssetGetPathForIndex = 0x00642830; // char *(int index), shared buffer

		// unsigned (int index). SLOT 5, the PC size getter, and THE EXISTENCE TEST a
		// picker needs. It resolves the path and then OPENS THE FILE to measure it,
		// rounding the length up to 16, and returns 0 when it cannot open it. So a
		// non-zero answer means the file really is in the archive right now, which is
		// strictly better evidence than any baked list and is the only way a package an
		// editor ADDS will be noticed.
		//
		// ONE FILE OPEN PER CALL, so probe the id space once and cache it rather than
		// asking per frame. Kind 12 is not in its early-out kind set, so for events it
		// always takes the open path.
		const DWORD AssetGetSizeForIndex = 0x006428A0;
		const DWORD AssetCurrentKindBase = 0x01F10C60; // slot 8, what the selector sets
		const DWORD AssetCurrentKind = 0x01F10C64;     // slot 9

		// char *(int absolutePathIndex), FFX_Asset_ResolvePathWithDebugOverrides.
		//
		// THE ONE TO CALL WHEN YOU WANT A PATH AND NOT A LOAD. It bounds checks the
		// index, reads only AssetPathTable, and touches NEITHER AssetCurrentKind NOR
		// AssetCurrentKindBase, so it needs no kind selection and no save and restore.
		// Slot 4 of the loader table, the selector, writes both of those, so going
		// through slot 22 costs a save and restore that this does not.
		//
		// The index is ABSOLUTE, so add the kind's base from AssetKindBaseTable
		// yourself rather than letting the selector do it.
		//
		// The answer lands in a SHARED 255 BYTE BUFFER, Rva::AssetResolvedPathBuf in
		// addresses/Minigames.h, so copy it before the next call.
		const DWORD AssetResolvePath = 0x00642C00;

		// s16[65], the per-kind base path index, from cdrom.fid. Loader slot 11, and a
		// POINTER SLOT, so read the pointer and then index it. -1 means the kind is
		// unused. Kind 12's base is 562 and kind 14's is 7942 on the shipped archive,
		// but read them rather than baking them.
		const DWORD AssetKindBaseTable = 0x01F10C6C;

		// The path table itself, cdrom.fnd, 811,400 bytes and 16,305 paths. Loader slot
		// 12, also a POINTER SLOT. Layout is u32 count, then count+1 u32 byte offsets
		// RELATIVE TO THE TABLE BASE, then the strings. A zero length slot has no path,
		// which is the validity test. Walking this directly needs no engine call at all.
		const DWORD AssetPathTable = 0x01F10C70;

		const int AssetKindBattleField = 14; // the per-fight battle field .bin

		// THE SHIPPED PATH OVERRIDE HOOK, which the "debug overrides" in the resolver's
		// name actually are. 16 key/value slots, and the resolver replaces EVERY
		// occurrence of the first matching key in EVERY resolved asset path, not just an
		// event one. The value may be longer than the key.
		//
		// The game uses exactly one slot, FFX_Ev_SetLocalizedEventDirForMode mapping the
		// localised event directory, so 15 are free for the life of the process.
		//
		// HAZARD: the engine's register asserts when all 16 are taken and THEN WRITES
		// keys[16] ANYWAY, and keys[16] is values[0]. So a 17th registration silently
		// destroys slot 0's value. ffx::RegisterAssetSubstitution refuses instead.
		const DWORD AssetPathSubstKeys = 0x01685BB0;   // const char *[16]
		const DWORD AssetPathSubstValues = 0x01685BF0; // const char *[16]
		const int AssetPathSubstSlots = 16;

		// int (const char *key, const char *value), returning the slot. IT KEEPS THE
		// POINTERS, so the strings have to outlive the registration.
		const DWORD AssetRegisterPathSubstitution = 0x00643220;
		const DWORD AssetUnregisterPathSubstitution = 0x00642F30; // void (int slot)

		// 128 BYTES PER PATH, and this is where that limit comes from rather than from
		// the resolver. FFX_RomDev_EnqueueRead, loader slot 0, copies the resolved path
		// into a queue with a 128-byte stride, "shl edx, 7" at 0x36BCE1 and
		// "cmp ecx, 80h" at 0x36BD39. The copy stops at the NUL, so a path of 128 bytes
		// or more is stored WITHOUT a terminator and runs into the next slot's buffer.
		//
		// The longest shipped kind-12 sub-index-0 path is 59 bytes, so there is room.
		const int AssetRomReadPathMaxBytes = 128;

		// The engine's own path table swap, void (const char *path). Its only caller is
		// debug only, but it works, and it is the supported alternative to storing a
		// pointer into AssetPathTable by hand.
		const DWORD AssetReloadPathTable = 0x00642B70;

		// void (int tag). Reads /ffx/proj/event/header/eventid.bin and fills the name
		// table below. Safe to call from a mod: the read resolves into the archive. Only
		// reached in retail from debug-mode boot and the AutoTest EnableAutoTest command,
		// so the table is EMPTY unless a mod calls this. The tag argument is only an
		// allocation label.
		const DWORD LoadEventIdTable = 0x00507F50;

		// The filled table. Row stride 16: char name[12] then DWORD id == the row index.
		// CAP THE NAME AT 12 BYTES, one row (251) has no terminator. Rows 101 and 111 are
		// dead. Count reads -1 until loaded, which is the "not loaded" test.
		const DWORD EventIdNameTable = 0x021D5888;  // row *
		const DWORD EventIdNameCount = 0x01534EC8;  // int
		const DWORD EventIdTableLoaded = 0x01534ECC; // int, the once-only guard
		const int EventIdNameRowStride = 16;
		const int EventIdNameMaxChars = 12;

		// int (char *out). Reverse lookup: finds the row whose id equals EvCurrentEventId
		// (addresses/Minigames.h) and sprintf's its name. Id 0 short circuits to "grid00".
		const DWORD MapGetCurrentMapName = 0x00507E70;

		// The AutoTest console, for the record rather than to be used. ExecCommand works
		// when called directly on the singleton, DrainQueue has ZERO CALLERS so the stdin
		// queue is dead, and the singleton itself is built by FFX_GraphicInitialize.
		const DWORD AutoTestExecCommand = 0x002BCE00; // char __thiscall (mgr, char *cmd)
		const DWORD AutoTestDrainQueue = 0x002BCFF0;  // void __thiscall (mgr), orphaned
		const DWORD AutoTestManagerSingleton = 0x008CCB00; // void *

		// ---------------------------------------------------------------------------
		// Every constant above, for the layout check.
		// ---------------------------------------------------------------------------
		inline const DWORD* WorldStateRvaList(int* count)
		{
			static const DWORD list[] = {
			    SaveFileInstallBlock, SaveDataReencodeCharNames, SphereGridRecomputeDerived,
			    SaveDataClearEquipStatBonus, AtelSysSave087, SaveDataSetSceneAndSub,
			    MapRequestChange, MapRequestResumeFromCheckpoint, MapSetTransitionFrames,
			    MapWarpTo, MapArmWarpGate, MapWarpToWithSavedFade, MapSetScreenFadeRate,
			    MapScreenFadeRate, MapReturnToTitle, SceneInit, MapLeaveFieldRequest,
			    AutoTestJumpMap, MapChangePending, MapChangeDelayFrames,
			    MapChangeDelayFlag, SceneLoaded, QuitPseudoMapId, DebugMode,
			    SaveDataGetMapId, SaveDataGetEntryPoint, SaveDataGetSceneId,
			    GetAssetLoader, AssetLoaderTable, AssetSelectKind, AssetGetPathForIndex, AssetGetSizeForIndex,
			    AssetCurrentKindBase, AssetCurrentKind, AssetResolvePath, AssetKindBaseTable,
			    AssetPathTable, AssetPathSubstKeys, AssetPathSubstValues,
			    AssetRegisterPathSubstitution, AssetUnregisterPathSubstitution,
			    AssetReloadPathTable, LoadEventIdTable, EventIdNameTable,
			    EventIdNameCount, EventIdTableLoaded, MapGetCurrentMapName,
			    AutoTestExecCommand, AutoTestDrainQueue, AutoTestManagerSingleton,
			};

			if (count)
				*count = (int)(sizeof(list) / sizeof(list[0]));

			return list;
		}
	} // namespace Rva
} // namespace ffx
