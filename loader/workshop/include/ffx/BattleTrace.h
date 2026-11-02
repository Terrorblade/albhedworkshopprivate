#pragma once

#include <windows.h>

// Watching a running battle from the outside: cheap reads of the state a recorder wants
// every step, and observer hooks on the battle's decision points. Built for the probe
// plugin's battle recipe (loader/plugins/probe, reversing/PROBE_FIXTURES.md section 10),
// and kept here because any plugin that logs or checks a battle wants the same things.
//
// ffx/Battle.h is the other battle header and it answers different questions: is a command
// legal, commit one, take the player's commit over. Everything there validates each read
// with workshop::Readable, which is a VirtualQuery, and that is fine for a co-op layer
// asking a handful of questions a turn. A recorder reads every unit every step, so the
// readers here validate once per pointer value instead and copy the rest plainly, the same
// rule the motion and camera readers follow.
//
// THE OBSERVER HOOKS. Each calls the observer before the original with afterCall false,
// runs the original with the same arguments, then calls the observer again with afterCall
// true and the result. They never change an argument or a result, and with no observer
// installed they are a plain pass through. They chain with another plugin's detour or call
// site patch on the same function (workshop/Detour.h). Install once, gate in the observer.
//
// They nest. FFX_Btl_RunAiAndCommit commits, which pushes, and a kind 2 AI run can kill a
// unit through property 242, whose defeat runs the kind 4 AI inside the outer run. So an
// observer sees entry, entry, exit, exit and must keep its own stack.
//
// All of it runs on the game's step thread, inside FFX_MainStep's battle sub step.
//
// The addresses are in ffx/addresses/Battle.h. The derivation is reversing/BATTLE_COMMAND.md,
// MONSTER_AI.md, CHEAT_BATTLE.md and BATTLE_START_AND_END.md.

namespace ffx
{

	// ---------------------------------------------------------------------------
	// Reads
	// ---------------------------------------------------------------------------

	struct BattleTraceView
	{
		const BYTE* units;       // the 31 unit records of 3984 bytes, NULL when no battle runs
		const BYTE* sideRecords; // the 62 side records of 912 bytes, NULL when not readable
		const BYTE* work;        // Rva::BtlWorkBlockBytes from g_ffxBtlSubPhase, NULL if unreadable
		int subPhase;            // g_ffxBtlSubPhase as the engine reads it (signed byte), 0 no battle
	};

	// True when a battle is running: the sub phase is not 0 and the unit array is allocated
	// and readable. The work block is filled battle or not, so the sub phase can be read on
	// the title screen too (it is 0 there, all of it reads as zero before a game).
	//
	// One VirtualQuery per new unit or side record pointer, then none. The engine allocates
	// both from its own heap, which stays mapped, so a pointer that was readable once stays
	// readable for as long as it holds that value.
	bool ReadBattleTraceView(BattleTraceView* out);

	// The 68 script RNG stream states (FFX_Rand_Stream 0x7988F0), g_ffxBattleRandState
	// (FFX_Btl_Rand 0x7989A0, a single LCG) and the effect RNG. Plain reads after the first
	// call. Any out pointer may be NULL.
	bool ReadBattleRandom(DWORD* streams68, DWORD* battleLcg, DWORD* effectLcg);

	// The private class 3 storage of battle event group 0 (ATEL context 2, the party and the
	// battle field scripts) or 1 (context 3, the monsters), where AI scripts keep their
	// variables. Fills the engine's pointer and size, NULL and 0 outside a battle. The
	// pointer is not validated, copy through it under __try.
	bool BattleClass3Storage(int group, const BYTE** base, DWORD* bytes);

	// ---------------------------------------------------------------------------
	// Observer hooks
	// ---------------------------------------------------------------------------

	enum BattleCall
	{
		// FFX_Btl_RunAiAndCommit 0x7ACEB0, cdecl, retn. 6 args:
		//   (u8 unit, int issuer, int kind, int aeon, unsigned mode, u8 *fill)
		// The unit's AI script for one event kind, then the commit of what it staged.
		BattleCallAiRun = 1,

		// FFX_Btl_CmdQueue_Push 0x7B0B90, cdecl. 5 args:
		//   (int unit, const void *cmd72, char postKind, char prio, char variant)
		// Its one caller is FFX_Btl_CommitCommand. Appends at the old count.
		BattleCallQueuePush = 2,

		// FFX_Btl_CmdQueue_Insert 0x7B0A30, cdecl. 7 args:
		//   (int unit, const void *cmd72, char postKind, int prio, char variant, char execState, int issuer)
		// Callers: FFX_Btl_CommitCommandPriority, FFX_Btl_QueueAeonTransition and
		// FFX_Btl_QueueDeltaAttack. Push and Insert are every way a command enters the queue.
		BattleCallQueueInsert = 3,

		// FFX_Btl_ComputeEffectForTarget 0x78E630, cdecl. 11 args:
		//   (int atkIdx, BTLUNIT *atk, int tgtIdx, BTLUNIT *tgt, const u8 *ability,
		//    int commandId, BTLHIT *hit44, int *drain3, int, int, int *tallyOut)
		// One hit, staged into the 44 byte hit record: amounts at +0x20, the apply mask at
		// +0x18 (0x100 critical), reducedBy at +0x03 (1 a miss). Returns the HP amount.
		BattleCallHitCompute = 4,

		// The call to FFX_Btl_CommitActorsToSave 0x785FC0 in FFX_Btl_MainStep's teardown.
		// No args. After it returns the save block holds the battle's writeback and the
		// unit records are still allocated, FFX_Btl_Shutdown is the next call.
		BattleCallCommitToSave = 5,
	};

	// args points at the call's arguments as raw dwords, argCount of them, the same values
	// on both sides of the call. A pointer argument is the engine's own, copy through it
	// under __try. result is 0 on entry.
	typedef void(__cdecl* BattleCallObserverFn)(int call, const DWORD* args, int argCount, bool afterCall,
	    int result);

	// One observer per module, shared by every hook below. Installing again replaces it and
	// keeps the hooks. Each returns true when its hook is in (or already was).
	bool HookBattleAiRun(BattleCallObserverFn observer);
	bool HookBattleQueueAdds(BattleCallObserverFn observer); // Push and Insert, true only when both are in
	bool HookBattleHitCompute(BattleCallObserverFn observer);
	bool HookBattleCommitToSave(BattleCallObserverFn observer);

} // namespace ffx
