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

		// The full warp: unbind the player CHR, request the change, set the fade, set the
		// transition frames.
		//
		// DO NOT CALL THIS. It opens with
		//     if (*(char *)g_ffxAtelCtx >= 0 && !FFX_IsDebugMode()) return;
		// and then CLEARS that same bit, so it needs bit 0x80 of the ATEL context's byte 0
		// set and it consumes it. A second call in the same context is silently refused.
		// Use MapRequestChange and set the fade separately.
		const DWORD MapWarpTo = 0x0046FEC0;

		// The AutoTestManager stdin command "JumpMap <mapname>". Reachable in retail today
		// with stdin attached and no patching, which makes it the cheapest way to test map
		// warping at all. Goes through MapWarpTo, so it carries that gate.
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
		// Every constant above, for the layout check.
		// ---------------------------------------------------------------------------
		inline const DWORD* WorldStateRvaList(int* count)
		{
			static const DWORD list[] = {
			    SaveFileInstallBlock, SaveDataReencodeCharNames, SphereGridRecomputeDerived,
			    SaveDataClearEquipStatBonus, AtelSysSave087, SaveDataSetSceneAndSub,
			    MapRequestChange, MapRequestResumeFromCheckpoint, MapSetTransitionFrames,
			    MapWarpTo, AutoTestJumpMap, MapChangePending, MapChangeDelayFrames,
			    MapChangeDelayFlag, SceneLoaded, QuitPseudoMapId, DebugMode,
			};

			if (count)
				*count = (int)(sizeof(list) / sizeof(list[0]));

			return list;
		}
	} // namespace Rva
} // namespace ffx
