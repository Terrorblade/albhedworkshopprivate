#pragma once

#include <windows.h>

// RNG state and the script minigame surface.
//
// The script minigames (chocobo race, butterflies, lightning dodge, Jecht Shot,
// Cactuar hunt, Via Purifico, Blitzball) are ATEL bytecode stepped inside
// FFX_MainStep, so the lockstep gate already covers them. What they need is
// agreement on the RNG state and on the flags that pick which bytecode a map
// loads.
//
// The pad addresses live in addresses/Input.h, the clocks in addresses/MainLoop.h
// and the overdrive minigames in addresses/Battle.h. The save block side of the
// bestiary and the capture counters is in addresses/GameState.h, and the ATEL VM
// itself is in addresses/Atel.h.

namespace ffx
{
	namespace Rva
	{
		// The 68 channel script RNG. 272 bytes is the whole mutable state.
		// Outside the save block, so the world transfer does not carry it.
		const DWORD RandStreamState = 0x00D35EE0;
		const int RandStreamStateBytes = 272;

		// Single state LCG, battle formation and party slot picks.
		const DWORD BattleRandState = 0x00842200;

		// Effect and particle RNG, float bits. 51 writers, all effect code. Chains
		// into CRT srand, which per character simulation code reads.
		const DWORD EffectRandState = 0x0080A000;
		const DWORD EffectRandInited = 0x0088F728;

		// Seeds both shared RNGs from constants, one shot from FFX_MainInit.
		const DWORD RandInit = 0x003FF360;
		// Seeds all 68 streams. Reachable from the ATEL save-RAM-clear syscall, so
		// it can reseed mid session.
		const DWORD RandSeedAllStreams = 0x00398890;

		// RandStream, the 68 stream LCG function itself, is declared in
		// addresses/Cutscene.h. Use Rva::RandStream from there. Scripts, Blitzball and the
		// Monster Arena all draw on stream 2 and all of them go through the two syscalls
		// below, so those two are where to count draws if a desync hunt needs a counter.
		const DWORD AtelSysCoreRandBounded = 0x00457400;       // core:166, rand(n), stream 2
		const DWORD AtelSysCoreRandRaw = 0x00457680;           // core:169, raw 16 bit
		const DWORD RandSeedValueFromSystemClock = 0x00398950; // the ONLY wall clock here
		const DWORD RandStreamMultiplierTable = 0x00842208;    // u32[68], read only
		const DWORD RandStreamXorTable = 0x00842318;           // u16[68], read only

		// The 4 KB script work area shadow, which is how a script saves and restores its
		// own variables around a minigame. Worth knowing because a restore can silently
		// undo a replicated write.
		const DWORD ScriptWorkAreaBackup = 0x00F2D570; // 4 KB, outside the save block
		const DWORD SaveDataBackupScriptWorkArea = 0x0047E720;
		const DWORD SaveDataRestoreScriptWorkArea = 0x0047E6F0;
		const DWORD SaveDataClearScriptWorkArea = 0x0046D4E0; // memset(save+0x1EC, 0, 4096)

		// Gates core:465, the second WaitFrames form.
		const DWORD ScriptWaitGate = 0x00F3C913;

		// These swap the bytecode package for the same map id, so a mismatch runs
		// different scripts with nothing in the save block to show it.
		// nagi0000 -> dbg_nagi0000, kami0400 -> 200thunder_kami0400.
		const DWORD ChocoboGameDebugEnable = 0x008CCAB8;
		const DWORD ThunderPlainTreasureEnable = 0x01685BA4;
		const DWORD FullNagi0700Enable = 0x01685BA8;

		// ---------------------------------------------------------------------------
		// BLITZBALL.
		//
		// A match is an ATEL event package, bl/bltz0000, stepped by FFX_Atel_StepOnce
		// inside the sub step loop. So the lockstep gate already covers it and there is no
		// separate Blitzball simulation to sync. What matters is the save block and the one
		// slot that says which actor is the player.
		//
		// Two pointers rather than copies: AtelSamplePadsBothPorts in addresses/Atel.h
		// samples port 1 every step and nothing ever reads it, so that unread sample is the
		// lever for a second Blitzball player. Rva::AtelActorClampCount, also in Atel.h, is
		// a free desync canary.
		// ---------------------------------------------------------------------------
		const DWORD BlitzSaveBlock = 0x00D2DC7C;       // 2560 bytes, saveData+0x11EC
		const DWORD BlitzPlayerByteArray = 0x00D2E00E; // 60 bytes, one per player
		const DWORD BlitzDebugFullBlitz = 0x003845B0;  // the only native writer of the block
		const DWORD BlitzCheatEnabled = 0x008CCACC;    // read only by core:613
		const DWORD AtelSysCore613CheatFlag = 0x0045A7F0;
		const DWORD AtelSysCore067BecomePlayer = 0x0045C9A0; // 10 Blitzball actors call it

		const DWORD EvLoadEventPackage = 0x00472EF0;
		const DWORD EvCurrentEventId = 0x00EFBC40;      // IDB still has this as a maybe_ name
		const DWORD EvCurrentEventName = 0x01FCBC60;    // holds "bl/bltz0000" in a match
		const DWORD EvPackageBase = 0x01FCBD70;         // the .ebp image, realloced per load
		const DWORD AtelActorPool = 0x01FCBD78;         // 110064 bytes for bltz0000
		const DWORD EvMesWinBlitzTextMode = 0x00F27100; // 1 in a match, text measuring only
		const DWORD MesWinFontScale = 0x0085D784;       // 0.9 in a match, text only

		// ---------------------------------------------------------------------------
		// THE MONSTER ARENA AND MONSTER CAPTURE.
		//
		// The capture decision is battle code rather than arena code, and it is one branch:
		// the killing command's row carries 0x40000, the actor has the Capture auto-ability
		// at +0x6C0 bit 2, the victim has a capture species index at +0x6D6, and BtlScriptMode
		// is 0. Then TryCapture bumps one byte.
		//
		// NOTHING ON THAT PATH RECORDS WHO DID IT. The counters are global. So in co-op a
		// capture is a world fact, not a player fact, and both peers have to apply it or
		// their arenas part company.
		//
		// The counters and the two bestiary masks live in the save block, so they are
		// declared in addresses/GameState.h. Use Rva::MonsterCaptureCounts,
		// Rva::MonsterSeenMask, Rva::MonsterMask2, Rva::SaveDataGetCaptureCount and
		// Rva::SaveDataAddCaptureCount from there.
		//
		// CAPTURE COUNTS ARE INDEXED BY A CAPTURE SPECIES INDEX 0..138, NOT by monster id.
		// Only the two masks are indexed by monster type id. An older note had that the
		// other way round.
		// ---------------------------------------------------------------------------
		const DWORD BtlOnUnitDefeated = 0x0038C740;       // the only caller of TryCapture
		const DWORD BtlApplyActionToTargets = 0x003892E0; // sets the capture pending flag
		const DWORD SaveDataTryCapture = 0x00390B30;      // (victimActor, speciesIndex)
		const DWORD SaveDataMarkMonsterBit = 0x00390BE0;
		const DWORD SaveDataGetMonsterDefeatedMask = 0x00390B10;
		const DWORD SaveDataGetMonsterSeenMask = 0x00390B20;
		const DWORD BtlIsEnemySlot = 0x0039AEF0;              // (index - 20) <= 7, host 293
		const DWORD BtlScriptMode = 0x00D2C9E5;               // must be 0 to flag a capture
		const DWORD BtlMonstersKilledThisBattle = 0x00D2C9EB; // byte
		const DWORD AtelSysBtlSetScriptMode1 = 0x003A8090;    // btl:275, the only writers
		const DWORD AtelSysBtlSetScriptMode2 = 0x003A80A0;    // btl:276

		const DWORD ArenaSpeciesTable = 0x00886708;            // 139 x 8, one xref in total
		const DWORD ArenaCountSpeciesWithAtLeast = 0x00472AA0; // (mode, key, minCount)
		const DWORD ArenaGetSpeciesCount = 0x0065CB20;         // returns 139, a constant
		const int ArenaSpeciesCount = 139;

		// Battle actor offsets the capture branch reads.
		const int BtlActorCaptureAbilityOff = 0x06C0; // bit 2 is the Capture auto-ability
		const int BtlActorCaptureSpeciesOff = 0x06D6; // word, 255 means not capturable
		const int BtlActorCapturePendingOff = 0x0DD0; // byte, then the result
		const int BtlActorCaptureResultOff = 0x0DD1;  // byte, for the AI getter

		// The arena's script side. Everything it does to the counters goes through these,
		// and core:429 is the creation unlock with 35 call sites.
		const DWORD AtelSysCore428GetCaptureCount = 0x00458950;
		const DWORD AtelSysCore429AddCapture1 = 0x00458B30;
		const DWORD AtelSysCore430SubCapture1 = 0x00458DF0;      // no shipped script calls it
		const DWORD AtelSysCore431AddCaptureN = 0x00458E70;
		const DWORD AtelSysCore536GetCaptureCount = 0x0045D3F0;  // the one scripts use
		const DWORD AtelSysCore537AddCaptureN = 0x0045D500;
		const DWORD AtelSysCore538CountAreaSpecies = 0x0045D780;  // mode 0, 52 uses
		const DWORD AtelSysCore539CountSpeciesGroup = 0x0045D8C0; // mode 1, 109 uses
		const DWORD AtelSysCore540CountAllSpecies = 0x0045DB90;   // mode 2
		const DWORD AtelSysCore572AllWorldFlagsSet = 0x0045C600;  // (base, n)

		// ---------------------------------------------------------------------------
		// The timed minigames: chocobo racing, lightning dodging, butterflies.
		//
		// None of the three has a clock. All three count ATEL frames through core:0
		// WaitFrames and show the count through a message window, so they ride the step
		// gate like everything else. The three debug flags above are the real hazard,
		// because they swap the bytecode package for the same map id.
		// ---------------------------------------------------------------------------
		const DWORD AtelSysCore527ChocoboRace = 0x0045C100; // 12 calls, nagi0000 only
		const DWORD DebugIsChocoboGameDebugEnabled = 0x002BC960;
		const DWORD IggyChocoboSwfEnable = 0x01584C6C;      // gates the chocobo.swf load
		const DWORD IggyChocoboSwfLoaderThunk = 0x00243DE0; // body at 0x0026F480
		// The loaded chocobo.swf hangs off GfxContext + 0x10DDC, and GfxContext is declared
		// in addresses/Character.h.

		// Butterflies hand out equipment with fixed abilities, 266 grants from one script
		// entry. Each grant writes four ability words into an equip record, so a peer that
		// misses one ends up with different gear and no flag anywhere to show it.
		const DWORD AtelSysCore533GrantEquip = 0x0045D170;
		const DWORD EquipSetAbilitySlotsFromTable = 0x004C3170; // equip +0x0E..+0x15
		const DWORD EquipAbilityRowTables = 0x00886D00;         // 18 tables, 0x10 apart
		const DWORD AtelSysCore378GrantA = 0x0045D210;          // 397 calls in mcfr0100
		const DWORD AtelSysCore379GrantB = 0x0045D3D0;          // 31 calls in mcfr0100
		// SaveDataRecomputeAllCharDerived runs after each grant. It is declared in
		// addresses/MenuSystem.h, use Rva::SaveDataRecomputeAllCharDerived from there.

		// The asset path override the three debug flags work through. All three package
		// swaps are inside this one function.
		const DWORD AssetResolvePathWithDebugOverrides = 0x00642C00;
		const DWORD AssetResolvedPathBuf = 0x01685C30;    // 255 bytes
		const DWORD AssetSubstitutionKeys = 0x01685BB0;   // 16 entries
		const DWORD AssetSubstitutionValues = 0x01685BF0; // 16 entries

		inline const DWORD* MinigamesRvaList(int* count)
		{
			static const DWORD list[] = {
				RandStreamState,
				BattleRandState,
				EffectRandState,
				EffectRandInited,
				RandInit,
				RandSeedAllStreams,
				AtelSysCoreRandBounded,
				AtelSysCoreRandRaw,
				RandSeedValueFromSystemClock,
				RandStreamMultiplierTable,
				RandStreamXorTable,
				ScriptWorkAreaBackup,
				SaveDataBackupScriptWorkArea,
				SaveDataRestoreScriptWorkArea,
				SaveDataClearScriptWorkArea,
				ScriptWaitGate,
				ChocoboGameDebugEnable,
				ThunderPlainTreasureEnable,
				FullNagi0700Enable,
				BlitzSaveBlock,
				BlitzPlayerByteArray,
				BlitzDebugFullBlitz,
				BlitzCheatEnabled,
				AtelSysCore613CheatFlag,
				AtelSysCore067BecomePlayer,
				EvLoadEventPackage,
				EvCurrentEventId,
				EvCurrentEventName,
				EvPackageBase,
				AtelActorPool,
				EvMesWinBlitzTextMode,
				MesWinFontScale,
				BtlOnUnitDefeated,
				BtlApplyActionToTargets,
				SaveDataTryCapture,
				SaveDataMarkMonsterBit,
				SaveDataGetMonsterDefeatedMask,
				SaveDataGetMonsterSeenMask,
				BtlIsEnemySlot,
				BtlScriptMode,
				BtlMonstersKilledThisBattle,
				AtelSysBtlSetScriptMode1,
				AtelSysBtlSetScriptMode2,
				ArenaSpeciesTable,
				ArenaCountSpeciesWithAtLeast,
				ArenaGetSpeciesCount,
				AtelSysCore428GetCaptureCount,
				AtelSysCore429AddCapture1,
				AtelSysCore430SubCapture1,
				AtelSysCore431AddCaptureN,
				AtelSysCore536GetCaptureCount,
				AtelSysCore537AddCaptureN,
				AtelSysCore538CountAreaSpecies,
				AtelSysCore539CountSpeciesGroup,
				AtelSysCore540CountAllSpecies,
				AtelSysCore572AllWorldFlagsSet,
				AtelSysCore527ChocoboRace,
				DebugIsChocoboGameDebugEnabled,
				IggyChocoboSwfEnable,
				IggyChocoboSwfLoaderThunk,
				AtelSysCore533GrantEquip,
				EquipSetAbilitySlotsFromTable,
				EquipAbilityRowTables,
				AtelSysCore378GrantA,
				AtelSysCore379GrantB,
				AssetResolvePathWithDebugOverrides,
				AssetResolvedPathBuf,
				AssetSubstitutionKeys,
				AssetSubstitutionValues,
			};
			if (count)
				*count = (int)(sizeof(list) / sizeof(list[0]));
			return list;
		}
	}
}
