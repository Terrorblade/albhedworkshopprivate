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
		const DWORD EncMapTable = 0x00D2A9C8;  // 96 entries of 14 bytes, indexed by map
		const DWORD EncZoneBlob = 0x00D2A9CC;  // the zone records, rate byte at [3]

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
				EncZoneBlob,
				MapGroundAttrGetEnc,
			};

			if (count)
				*count = (int)(sizeof(list) / sizeof(list[0]));

			return list;
		}
	} // namespace Rva
} // namespace ffx
