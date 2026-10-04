#pragma once

#include <windows.h>

// Random encounters: the distance accumulator, the roll, and the deferred battle request.
//
// Derived in reversing\RANDOM_ENCOUNTER.md. Read that before changing anything here, in
// particular its unsettled note about the formation weight shift.
//
// THE SHAPE, because it decides what co-op has to do:
//
//   FFX_MainStep
//     -> FFX_Atel_StepOnce -> FFX_Atel_StepFieldAll -> FFX_Atel_StepFieldFrame
//          computes dist = |ctx+552..560 prev - ctx+536..544 cur| for the BOUND player
//          -> FFX_Field_StepRandomEncounter(sceneId, encZone, dist)       the only caller
//               g_ffxEncDistRemain += dist, g_ffxEncDistTotal += dist     SINGLE GLOBALS
//               needs remain > 10.0 to roll, each roll consumes 10.0
//               steps = total / 10, rate = zoneRecord[3]
//               if steps > rate/2: thr = ((steps - rate/2) << 8) / (4*rate)
//               fires when (u8)FFX_Rand_Stream(0) < thr                   STREAM 0, not 18
//               on a hit: sets the deferred request below, then resets the accumulator
//     -> FFX_Btl_MainStep polls g_ffxBattlePendingKind and calls FFX_Btl_BeginBattle
//
// THREE THINGS THAT MATTER FOR CO-OP:
//
//  1. The distance is a PARAMETER, not something the check reads off the world. So a
//     detour can substitute it without any save and restore of engine state, which is
//     much cheaper than the per-player trigger pass had to be.
//  2. The accumulators are single globals, so calling the check once per player would
//     multiply the encounter rate by the number of players. The mod folds the per-player
//     distances into one number instead. See ffx::SetEncounterDistanceHook.
//  3. The result is a DEFERRED FLAG, four bytes, polled by a later subsystem in the same
//     frame. That is the whole replication surface for "a battle started", and a flag is
//     far easier to agree on than a call.

namespace ffx
{
	namespace Rva
	{
		// ---------------------------------------------------------------------------
		// The check itself.
		// ---------------------------------------------------------------------------

		// int __cdecl (int sceneId, int encZone, float distThisFrame).
		// Sole caller FFX_Atel_StepFieldFrame at 0x871D4A.
		// Prologue 55 8B EC 83 EC 10, six bytes, all position independent.
		//
		// OWNS this address. EscMenu.h used to call it EncounterRollReader because all it
		// knew was that the function reads the booster setting once. It is the encounter
		// check.
		const DWORD FieldStepRandomEncounter = 0x00380D10;

		// void __cdecl (int). Zeroes the distance accumulators. Called by the check on a
		// hit, so a mod that wants to suppress a battle has to deal with the reset too.
		const DWORD FieldResetEncounterAccum = 0x00380FF0;

		// ---------------------------------------------------------------------------
		// The accumulators. Both floats, both single globals.
		// ---------------------------------------------------------------------------

		// Distance since the last roll. The check needs this above 10.0 to roll at all and
		// takes 10.0 off per roll.
		const DWORD EncDistRemain = 0x00D2A9D8;

		// Distance since the last battle. Drives the rising chance, as total / 10 steps.
		// The dev BattleInfo window prints this divided by 10 and calls it "steps".
		const DWORD EncDistTotal = 0x00D2A9DC;

		// ---------------------------------------------------------------------------
		// The gates.
		// ---------------------------------------------------------------------------

		// byte. The No Encounters ARMOUR ABILITY, aggregated over the party from
		// charRecord+0x4E bit 1. Not a debug switch, a real game mechanic, so a co-op mod
		// must not bypass it.
		const DWORD EncountersEnabled = 0x00D2A9D7;

		// The PS2 debug menu's encounter switch.
		const DWORD DebugEncountersOn = 0x008421CC;

		// NOT DECLARED HERE. g_boosterEncounterRate 0xC421D8 lives in EscMenu.h as
		// Rva::BoosterEncounterRate, because the settings menu owns the booster toggles.
		// Values: 0 zeroes the distance and disables, 1 defers to EncountersEnabled,
		// 2 multiplies the distance by 10. It scales the simulation, so it is one of the
		// four booster globals that MUST match across a session.

		// NOT DECLARED HERE. FFX_Rand_Stream 0x7988F0 lives in Cutscene.h as
		// Rva::RandStream, which found it first for the script RNG. The encounter chance
		// roll is stream 0 and the formation pick is stream 1. Stream 0 has exactly one
		// call site in the whole binary, which is this check, and that makes it the easiest
		// RNG consumer in the game to reason about under lockstep.

		// ---------------------------------------------------------------------------
		// The deferred battle request. Four bytes, and this is the replication surface.
		// ---------------------------------------------------------------------------

		// byte. 1 means a random encounter is pending. FFX_Btl_MainStep polls it at
		// 0x790F70 and hands off to FFX_Btl_BeginBattle.
		const DWORD BattlePendingKind = 0x00D2A8E2;

		// dword. The check writes the scene index into the HIGH word and leaves the low
		// word alone, so a reader wants HIWORD of this.
		const DWORD BattleSceneAndMap = 0x00D2C254;

		const DWORD BattleZoneIndex = 0x00D2C258;      // byte, the walkmesh enc attribute
		const DWORD BattleFormationIndex = 0x00D2C259; // byte, which formation was rolled

		const DWORD BtlBeginBattle = 0x00381020; // what the poll calls once the flag is up
		const DWORD BtlMainStep = 0x00390C10;    // where the poll lives

		// dword. 1 means no battle may start. Both battle request paths test it first,
		// so anything that wants to know whether a request will be honoured reads this
		// and FFX_Btl_GetPhase (Rva::BtlGetPhase in addresses/Battle.h) rather than
		// trusting a return value.
		const DWORD BattleDisabled = 0x00D2CA2C;

		// THE SCRIPTED BATTLE REQUEST, which is pending kind 2 rather than 1, is
		// Rva::BtlRequestScriptedBattle in addresses/Minigames.h along with
		// BtlResolveBattleId and DebugBeginSelectedBattle. It is declared there because
		// the Monster Arena and the butterfly penalty fights are its only interesting
		// callers. Note it ALWAYS returns -1, success or not, so the only honest test is
		// to read BattlePendingKind afterwards and see whether it became 2.

		// ---------------------------------------------------------------------------
		// The per-map encounter data. One 4096-byte file, loaded once at battle init,
		// resolved through the shipped cd index to
		// ffx_ps2/ffx/master/jppc/battle/kernel/btl.bin.
		// ---------------------------------------------------------------------------

		const DWORD EncTableBlob = 0x00D2A9C4; // the loaded file

		// CORRECTED 2026-10-04: these two are POINTER SLOTS holding a base address, not
		// the tables themselves, and the map table is indexed by SCENE index rather than
		// by map id. FFX_Btl_GetEncounterMapEntry returns g_ffxEncMapTable + 14*scene,
		// and the map id is a field inside the entry. The count is EncMapCount, not a
		// hardcoded 96.
		const DWORD EncMapTable = 0x00D2A9C8; // ptr, EncMapEntryBytes per scene
		const DWORD EncZoneBlob = 0x00D2A9CC; // ptr, the variable length zone records

		// CORRECTED AGAIN, same day: THIS IS A SIGNED 16-BIT WORD, NOT AN INT. It is
		// written "mov g_ffxEncMapCount, cx" at 0x79D26D and read back twice with
		// "movsx eax, g_ffxEncMapCount", at 0x79D19A and 0x79D1C4. Reading four bytes
		// here drags in EncTableSize as the high half and gives a count in the tens of
		// millions, which fails a sanity check and reads as "no table loaded".
		//
		// FFX_Btl_InitEncounterTablePtrs computes it as (hdr[+8] - hdr[+4]) / 14, which
		// is the magic-divide sequence around the 0x24924925 constant.
		const DWORD EncMapCount = 0x00D2A9D0; // s16, how many scenes the file declares
		const DWORD EncTableSize = 0x00D2A9D2; // s16, and the reason the above is not 4

		// ---------------------------------------------------------------------------
		// ENUMERATING EVERY SCRIPTED BATTLE FROM MEMORY. This is the whole battle list,
		// debug and test fights included, and it needs no baked data at all:
		//
		//   for scene in 0 .. EncMapCount-1                   <- a WORD, read it as s16
		//       entry     = BtlGetEncounterMapEntry(scene)
		//       mapId     = *(s16 *)(entry + 0)
		//       sceneName = entry + 6, 8 chars, not NUL terminated
		//       zoneTable = BtlGetEncounterZoneTable(scene)
		//       for zone in 0 .. zoneTable[1]-1
		//           rec = BtlGetEncounterZoneRecord(scene, zone)
		//           for i in 0 .. rec[0]-1
		//               encounterId = rec[5 + 2*i]        <- a BYTE, not a word
		//               battleId    = (mapId << 16) | encounterId
		//
		// That battleId is what Rva::BtlRequestScriptedBattle takes, and the search it
		// does internally is exactly the walk above, which is where this came from. The
		// encounter id really is one byte: ResolveBattleId masks the low WORD of the
		// battle id but compares it against a u8 read, at 0x78284B, so an encounter id
		// above 255 can never match anything. FFX_Btl_MainStep and sub_782970 compose
		// the live battle id the same way, and that same byte is the "%02d" in the
		// battle scene filename.
		//
		// ONE TRAP IN THE SEARCH: FFX_Btl_FindEncounterSceneByMapId also requires
		// mapId > 0 ("test esi, esi / jg" at 0x79D1E7), so map id 0 never resolves
		// through it even if a scene declares it. The walk above does not go through
		// that function and is not affected.
		// ---------------------------------------------------------------------------
		// THE ASSET INDEX FOR ONE FIGHT, which names its battle field file:
		//
		//     fieldAssetIndex = (s16)sceneEntry[+4]
		//                     + sum(zoneRecord[z][0] for z < zone)   // formation counts
		//                     + formationSlot                        // flat across zones
		//
		// then the path is AssetResolvePath(AssetKindBaseTable[14] + fieldAssetIndex),
		// which gives "host0:/ffx/master/jppc/battle/btl/bjyt02_00/bjyt02_00.bin".
		//
		// The engine has its own at this RVA. ITS SIGNATURE IS NOT SETTLED, it works out
		// of registers, so the kit computes the formula above instead and this is
		// recorded for whoever settles it. 862 of the 863 fights resolve to a shipped
		// file this way, the one that does not being scene 0 slot 0, whose path is the
		// developers' own host0:/home/$USER$/battle/jp/btl/output.bin.
		const DWORD BtlResolveFieldAssetIndex = 0x0039D0E0;

		// ptr, the monster section of the battle field file for the fight that is
		// running. Live during a battle only. FFX_Btl_SetupUnitRoster reads the 8 type
		// ids out of it at +12, stepping 2, and stores each into enemyUnit+14.
		const DWORD BtlFieldMonsterSection = 0x00D2A9C0;

		// THE BATTLE FIELD FILE, asset kind 14. Four u32 section offsets at +4, +8, +12
		// and +16, each relative to the file base. The THIRD one, at +12, is the monster
		// section. The other three are undecoded.
		const int BtlFieldMonsterSectionOff = 12;

		// Inside the monster section:
		//   +0,+1,+2  u8 V, R, F, which is the dev window's "V%1dR%1dF%2d". +2 feeds
		//             FFX_Btl_LoadFootData
		//   +3        u8
		//   +4..+11   zero in every shipped file, purpose unknown
		//   +12..+27  THE 8 ENEMY TYPE IDS, u16 each
		//
		// 0xFFFF is an empty slot. Otherwise the low 12 bits are the kernel monster id,
		// 0..365, the same id space KernelName(KernelMonsters, id) takes, and the high
		// nibble is 1 in all 1624 non-empty entries across all 862 files.
		//
		// THE ROSTER CANNOT CHANGE MID BATTLE. FFX_Btl_SetupUnitRoster is the only writer
		// of enemyUnit+14, and actor+4 is derived from it and never recomputed. So these
		// 8 are the complete set including units that only appear later, which is why
		// Penance's arms and Yu Yevon's pagodas are already in the file.
		const int BtlFieldMonsterFirstId = 12;
		const int BtlFieldMonsterSlots = 8;
		const int BtlFieldMonsterEntryBytes = 2;
		const WORD BtlFieldMonsterEmpty = 0xFFFF;
		const WORD BtlFieldMonsterIdMask = 0x0FFF;

		const DWORD BtlFindEncounterSceneByMapId = 0x0039D1C0; // int (int mapId), -1 none
		const DWORD BtlGetEncounterMapEntry = 0x0039D190;      // int (int scene), clamps
		const DWORD BtlGetEncounterZoneTable = 0x0039D170;     // int (int scene)
		const DWORD BtlGetEncounterZoneRecord = 0x0039D210;    // u8 * (int scene, int zone)

		const int EncMapEntryBytes = 14;
		const int EncMapEntryMapId = 0;   // s16, read with movsx
		const int EncMapEntryZoneOff = 2; // s16 offset into the zone blob, so max 0x7FFF

		// s16, THE ASSET INDEX BASE FOR THIS SCENE'S FIGHTS, and the missing link that
		// lets a fight name its monsters. An older comment on the entry getter claimed
		// nothing reads this. Wrong: FFX_Btl_ResolveFieldAssetIndex reads it at 0x79D15E
		// as "movsx eax, word ptr [eax+4]".
		//
		// SEVEN SCENES SHARE ANOTHER SCENE'S BASE and so reuse its files: 20 shares 19's
		// 59, 23 shares 22's 72, 37 and 38 both use 170, 51 uses hiku02's 243, 83 uses
		// cdsp00's 7. That is why the 8 character scene NAME must not be used to build
		// the path, it names a directory that does not exist for those. The name buffer
		// FFX_Btl_BeginBattle fills is for sound banks and localised text, not for this.
		const int EncMapEntryFieldAssetBase = 4;
		const int EncMapEntrySceneName = 6;      // 8 chars, NOT NUL terminated
		const int EncMapEntrySceneNameBytes = 8;

		const int EncZoneTableCountOff = 1; // u8, how many zone records follow at +2

		const int EncZoneRecFormations = 0;  // u8, how many formation entries
		const int EncZoneRecMapLow = 1;      // u8
		const int EncZoneRecMapHigh = 2;     // u8
		const int EncZoneRecRate = 3;        // u8, 0 means no random battles in this zone
		const int EncZoneRecWeightTotal = 4; // u8, the modulo total
		const int EncZoneRecFirstEntry = 5;  // the formation entries start here
		const int EncZoneRecEntryStride = 2; // +0 u8 encounter id, +1 u8 weight

		// THE WEIGHT NIBBLE IS NOT SETTLED. FFX_Field_StepRandomEncounter does read it
		// as "byte >> 4", but an earlier note on that function records the shipped
		// btl.bin storing those weights raw rather than shifted up, which would make
		// the running accumulator always 0. Nothing in this project depends on the
		// weight, the encounter id next to it is what starts a fight, so it is reported
		// and not relied on.

		// How the zone index is chosen, which is a SECOND per-player input nobody asked
		// about: it is the 2-bit walkmesh "enc" ground attribute under the BOUND player's
		// feet, read from CHR+0x828 >> 7 & 3 and cached at g_ffxSaveData+16. Refreshed per
		// frame for the bound actor only. So a remote player standing in a different
		// walkmesh region does not change which encounter table is consulted.
		const DWORD MapGroundAttrGetEnc = 0x0043D820;

		// NOT DECLARED HERE. FFX_Atel_PullActorPosFromChr 0x869E40 lives in Atel.h as
		// Rva::AtelPullActorPosFromChr, which owns it because that function's job is the
		// ATEL position cache and the enc attribute is a side effect it happens to refresh.

		// ---------------------------------------------------------------------------
		// Every constant above, for the layout check. See Addresses.h for the rule on
		// collisions: an address lives in the area that OWNS the thing, and the other area
		// gets a comment pointing here rather than a second declaration.
		// ---------------------------------------------------------------------------
		inline const DWORD* EncounterRvaList(int* count)
		{
			static const DWORD list[] = {
				FieldStepRandomEncounter,
				FieldResetEncounterAccum,
				EncDistRemain,
				EncDistTotal,
				EncountersEnabled,
				DebugEncountersOn,
				BattlePendingKind,
				BattleSceneAndMap,
				BattleZoneIndex,
				BattleFormationIndex,
				BtlBeginBattle,
				BtlMainStep,
				BattleDisabled,
				EncTableBlob,
				EncMapTable,
				EncMapCount,
				EncTableSize,
				EncZoneBlob,
				BtlResolveFieldAssetIndex,
				BtlFieldMonsterSection,
				BtlFindEncounterSceneByMapId,
				BtlGetEncounterMapEntry,
				BtlGetEncounterZoneTable,
				BtlGetEncounterZoneRecord,
				MapGroundAttrGetEnc,
			};

			if (count)
				*count = (int)(sizeof(list) / sizeof(list[0]));

			return list;
		}
	} // namespace Rva
} // namespace ffx
