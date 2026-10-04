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

		// ---------------------------------------------------------------------------
		// THE LIVE SCRIPT WORK AREA. This is where most minigame state actually lives,
		// so it is the backing store for a "edit the running minigame" UI.
		//
		// AtelScriptWork is SaveData + 0x1EC, and the bound is tight: +0x2000 lands
		// exactly on g_ffxSphereGridNodes. Cross-checked three more ways, +0x1000 is
		// BlitzSaveBlock and +0x1392 is BlitzPlayerByteArray, both below.
		//
		// A script variable is NOT a raw offset into this block. The bytecode carries an
		// index into an 8-byte descriptor table at *(u32 *)(actorDef + 0x14):
		//     type   = desc >> 28        0 u8 1 s8 2 u16 3 s16 4 u32 5 s32 6 f32
		//     class  = (desc >> 25) & 7  which storage area
		//     offset = desc & 0xFFFFFF   bit 24 is a flag, NOT part of the offset
		//     count  = *(u16 *)(desc + 4)
		// Class 0 is this block. Classes 2 to 6 live inside the .ebp image and the actor
		// pool, which are FREED AND REALLOCATED on every map change, so re-resolve those
		// bases every frame and never cache a pointer across a load. Full formula in
		// reversing/CHEAT_MODELS.md.
		// ---------------------------------------------------------------------------
		const DWORD AtelScriptWork = 0x00D2CC7C; // the LIVE class 0 area
		const int AtelScriptWorkBytes = 0x2000;
		const DWORD AtelResolveVarAddress = 0x0046C2E0;     // char *(actor *, u32 desc)
		const DWORD AtelResolveVarElemAddress = 0x0046C570; // adds a bounds-checked index
		const DWORD AtelOpStoreVar = 0x0046DFB0;            // opcodes 0x20/21/23/24

		// The resolver's first argument is the LIVE POOL ACTOR, not a context, which is
		// what makes calling it cheap: actor+0 is the actor definition record inside the
		// package image and actor+4 is the ATEL block base, so the engine already holds
		// every base the formula needs. FFX_Atel_GetActorKind proving actor+0 is a
		// pointer (it does **(u8 **)actor) and the resolver dereferencing actor+0 at
		// +0x14/+0x28/+0x2C/+0x30 are the two halves of that.
		//
		// A SYNTHETIC DESCRIPTOR WORKS. The resolver only ever looks at bits 25..27 for
		// the class and bits 0..23 for the offset, so (class << 25) | offset resolves a
		// known field without going near the package's own descriptor table. That is how
		// a named field table addresses a live minigame variable.
		const int AtelDescClassShift = 25;
		const int AtelDescTypeShift = 28;
		const DWORD AtelDescOffsetMask = 0x00FFFFFF;

		// The ATEL block header, which is pkgBase + *(u32 *)(pkgBase + 4). Verified in
		// FFX_Ev_LoadEventPackage, which computes exactly that and passes it on to the
		// context init, where it lands at ctx+0x48.
		const int AtelBlockClass6Base = 0x20;  // u32, the class 6 base offset
		const int AtelBlockCodeBase = 0x30;    // u32
		const int AtelBlockActorCount = 0x34;  // u16, the length of the array at +0x38
		const int AtelBlockLargeActors = 0x36; // u16, actors from here use the 48 byte stride
		const int AtelBlockActorOffsets = 0x38; // u32[count], each an offset from the block

		// The actor definition record, 52 bytes, inside the package image.
		const int AtelActorDefKind = 0x00;        // byte, 0 subroutine 1 character 2 line ...
		const int AtelActorDefEntryCount = 0x08;  // u16, script entry points
		const int AtelActorDefLabelCount = 0x0A;  // u16
		const int AtelActorDefVarTable = 0x14;    // u32 offset, 8 bytes per descriptor
		const int AtelActorDefIntPool = 0x18;     // u32 offset. The gap from +0x14 is the
		                                          // descriptor count times 8, which is the
		                                          // only way to count them in process
		const int AtelActorDefEntryTable = 0x20;  // u32 offset, u32 code offsets
		const int AtelActorDefClass2Base = 0x28;
		const int AtelActorDefClass3Base = 0x2C;
		const int AtelActorDefClass4Base = 0x30;
		const int AtelVarDescriptorBytes = 8;
		const int AtelActorDefBytes = 52;

		// The ATEL context fields the resolver reads. Everything else about the context
		// is in addresses/Atel.h and ffx/Atel.h's AtelCtx namespace.
		const int AtelCtxClass0Base = 0x2C; // = g_ffxSaveData + 0x1EC
		const int AtelCtxClass1Base = 0x30; // never written to anything but 0. Unused
		const int AtelCtxClass3Hook = 0x54; // when non-null class 3 tail calls it instead
		const int AtelCtxBlockBase = 0x48;  // the ATEL block, written by the context init

		const DWORD SaveDataClearScriptWorkArea = 0x0046D4E0; // memset(save+0x1EC, 0, 4096)

		// THE SHADOW IS DEAD CODE, and an earlier comment here was wrong to warn that a
		// restore could silently undo a replicated write. It cannot. Both accessors have
		// exactly ONE xref each and both come from maybe_FFX_TkHarness_Init_DEAD, which
		// installs them as function pointers behind a predicate that returns a constant
		// 0, so the pointers stay null and nothing ever calls them. Note also that Clear
		// above wipes only the first 4096 of the 8192 bytes and is not on the map load
		// path, so class 0 is genuinely persistent during play.
		const DWORD ScriptWorkAreaBackup = 0x00F2D570; // 4 KB, outside the save block
		const DWORD SaveDataBackupScriptWorkArea = 0x0047E720;
		const DWORD SaveDataRestoreScriptWorkArea = 0x0047E6F0;

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
		// NOT a Chocobo race function, despite all 12 of its call sites being in
		// nagi0000. The body is popInt -> set the random-encounter scene override ->
		// reset the encounter accumulator and rebuild the zone weights. It reads and
		// writes no race variable. nagi0000 uses it to swap the encounter table for the
		// race course. Renamed from AtelSysCore527ChocoboRace, which had no users.
		const DWORD AtelSysCore527SetEncounterSceneOverride = 0x0045C100;
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

		// ---------------------------------------------------------------------------
		// LAUNCHING A MINIGAME.
		//
		// There is no per-minigame native entry point for the field minigames, and
		// looking for one is the wrong shape. Every map, cutscene and field minigame is
		// an ATEL event package identified by one integer, and that integer is the same
		// number in three places: EvLoadEventPackage's argument, MapRequestChange's map
		// id, and the row index of eventid.bin. So the launcher is generic.
		//
		// MapRequestChange 0x0048EA60, LoadEventIdTable 0x00507F50, EventIdNameTable
		// 0x021D5888, EventIdNameCount 0x01534EC8 and MapGetCurrentMapName 0x00507E70
		// are all declared in addresses/WorldState.h. Use those.
		//
		// SAFETY, AND IT IS A HARD HANG: EvLoadEventPackage validates the package with
		//     if (buf[0] != 'E' || buf[1] != 'V') while (1) ;
		// an infinite loop, not a return. Measured against the archive, 376 of the 402
		// ids have a shipped package. The 24 named-but-absent ids are 40, 186, 216, 228,
		// 229, 231, 232, 233, 246, 251, 262, 300, 304, 342, 350, 353, 357, 358, 360, 369,
		// 373, 379, 400, 401, and 101 and 111 are unnamed rows. A picker MUST filter
		// those out, and that filter is the one thing in this area that has to be baked.
		// ---------------------------------------------------------------------------

		// int (int caller, int actor, int kind, int channel, int entry). Runs an
		// arbitrary actor script entry, which is what actually starts a field minigame
		// once its map is loaded. No dedupe, and 0xFFFF as the caller means none.
		// AtelCreateThread, which this wraps, is in addresses/Atel.h.
		const DWORD AtelStartThread = 0x0046E990;

		// int (int caller, int actor, int kind, int entryOrNegative). The nicer shipped
		// helper: keeps the engine's channel and actor+171 event-mask checks but lets you
		// name the entry. An out of range entry falls back to entry record 0.
		const DWORD AtelFireActorEventWithEntry = 0x004766D0;

		// int (int battleId, char, char) where battleId = (mapId << 16) | encounterId.
		// Deferred, so this is the safe way to start a specific battle. Gated on the
		// battle-disabled flag and on BtlGetPhase() == 0. The Monster Arena's 36 fights
		// are (601 << 16) | 105, (603 << 16) | {76,77,79..83,92..99} and
		// (604 << 16) | {0..18}, and the butterfly penalty battles are (310 << 16) | 0..5.
		//
		// IT ALWAYS RETURNS -1, on the accepted path and on every refusal, so the return
		// value says nothing. Test the two gates first (Rva::BattleDisabled and
		// Rva::BtlGetPhase, both in other headers) and then read Rva::BattlePendingKind
		// in addresses/Encounter.h, which becomes 2 only when the request was taken.
		const DWORD BtlRequestScriptedBattle = 0x00381C90;
		const DWORD BtlResolveBattleId = 0x003827F0;      // what proves the id packing
		const DWORD DebugBeginSelectedBattle = 0x003C6D80; // shipped one-call launcher

		// The overdrive minigame entry points are BtlOdStartMinigameForActor and the four
		// per-character launchers, all declared in addresses/Battle.h along with the whole
		// live state set. BtlOdMinigamePhase there is the "an overdrive minigame is
		// running" discriminator, and it is a single global set, so two characters cannot
		// be in one at once.

		// The shipped debug toggles. Each is worth more than a hand-rolled equivalent
		// because it flips exactly what the game expects.
		const DWORD DebugToggleFullNagi0700 = 0x006431F0; // Monster Arena fully unlocked
		const DWORD DebugSetThunderPlainTreasureEnable = 0x00643210; // the 200-dodge swap
		const DWORD DebugStepChocoboGameDebugEnable = 0x002B6790;    // needs 4 presses
		// The "always full overdrive" toggle, which is the cheapest route to the four
		// battle overdrive minigames, is declared in addresses/Battle.h as
		// BtlDbgSetOverdriveAlwaysFull. Use that rather than a second constant.

		// ---------------------------------------------------------------------------
		// LIVE MINIGAME STATE, for editing whatever is running right now.
		//
		// WHICH MINIGAME IS RUNNING: read the char[256] at EvCurrentEventName above and
		// match the tail after the '/'. It holds "bl/bltz0000" in a Blitzball match,
		// "na/nagi0000" in the chocobo race, "ka/kami0000" on the Thunder Plains,
		// "mc/mcfr0100" for butterflies, "sw/swin0000" for the Jecht Shot. Two caveats:
		// nothing clears it on map unload so it goes stale rather than empty, and a battle
		// does not reload the package, so during an encounter it still names the field
		// map. For battle minigames use Battle.h's BtlOdMinigamePhase instead.
		//
		// The LIVE map id is SaveData word +0x00, which SaveDataGetMapId in
		// addresses/WorldState.h returns. GameState.h's g_ffxCurrentMapId at SaveData+0xB8
		// is the CHECKPOINT, not the live map.
		// ---------------------------------------------------------------------------

		// Blitzball live match state. The two scores are verified from twelve goal sites
		// in the match controller plus the debug cheat, and they are class 0 so they are
		// in the save block and they stick. The match clock and half length are class 6,
		// so they need the runtime base formula, and the clock is in SECONDS (proved by
		// its own MM:SS digit decomposition at radix 600/60/10/1). Half length ships 300.
		const DWORD BlitzHomeScore = 0x00D2E0CE; // u8, class 0 off 0x1452
		const DWORD BlitzAwayScore = 0x00D2E0CF; // u8, class 0 off 0x1453
		const DWORD BlitzHalfFlag = 0x00D2E0D0;  // u8, class 0 off 0x1454. Name is a GUESS
		const int BlitzClockSecondsClass6Off = 0x144; // s32
		const int BlitzHalfLengthClass6Off = 0x148;   // s32

		// PREFER THIS over poking the package image. With BlitzCheatEnabled set to 1 the
		// match script itself polls the pad: L1+Up home +1, L1+Down away +1, L1+Left
		// reset the clock, L1+Right end the half. Those are ordinary ATEL writes, so they
		// cannot desync the script interpreter the way a raw class 6 poke can.

		// Chocobo race. Best times are persistent class 0, and there are FOUR courses of
		// (u8 min, u8 sec, u8 tenths), not three as MINIGAMES_TIMED.md 3.5 has it.
		// The live race state is class 6: clock tenths/sec/min at +0xB0/+0xB1/+0xB2,
		// course 1..4 at +0xBC, state at +0xDF (0 running 1 timed out 2 finished),
		// player balloons +0xD3 and birds +0xD5. The score is NOT in chocobo.swf.
		const DWORD ChocoboBestTimes = 0x00D2CD24; // u8[4][3], class 0 off 0xA8

		// Lightning dodge. The live streak is class 6 at +0x12 (u16) and the dodge window
		// is class 6 at +0x7 (u8), set to 45 by a Circle press and counted down, so
		// holding it non-zero dodges every bolt. These are the persistent halves.
		const DWORD LightningBoltsSeen = 0x00D2CE8C;   // u16, class 0 off 0x210
		const DWORD LightningBoltsDodged = 0x00D2CE8E; // u16, class 0 off 0x212
		const DWORD LightningBestStreak = 0x00D2CE90;  // u16, class 0 off 0x214
		const DWORD LightningRewardBits = 0x00D2CE84;  // u8,  class 0 off 0x208

		// Butterflies are class 3, which is PER ACTOR, so they need the actor base too.
		// Countdown seconds and tenths are actor 37 +0x0 and +0x4 (s32), and the gauge is
		// actor 38 +0x16 (u8), starting at 128 with blues +3 and reds -3 and a >= 89 test
		// gating the reward. The caught count and the red penalty are the SAME variable,
		// there is no separate counter, and mcfr0100 declares no class 0 vars at all.

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
				AtelScriptWork,
				AtelResolveVarAddress,
				AtelResolveVarElemAddress,
				AtelOpStoreVar,
				AtelStartThread,
				AtelFireActorEventWithEntry,
				BtlRequestScriptedBattle,
				BtlResolveBattleId,
				DebugBeginSelectedBattle,
				DebugToggleFullNagi0700,
				DebugSetThunderPlainTreasureEnable,
				DebugStepChocoboGameDebugEnable,
				BlitzHomeScore,
				BlitzAwayScore,
				BlitzHalfFlag,
				ChocoboBestTimes,
				LightningBoltsSeen,
				LightningBoltsDodged,
				LightningBestStreak,
				LightningRewardBits,
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
				AtelSysCore527SetEncounterSceneOverride,
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
