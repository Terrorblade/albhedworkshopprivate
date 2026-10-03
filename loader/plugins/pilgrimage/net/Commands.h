#pragma once

#include <stdint.h>

// The mod's command kinds.
//
// A command is for state that CANNOT be derived from replicated input. That is the whole
// test for whether something belongs here, and it rules out more than it lets in.
//
// Interaction, for instance, needs no command at all: both machines hold the same
// positions because they stepped the same input, so both run an identical trigger pass and
// both fire the same event on the same step. Putting that on the command channel would be
// adding a network round trip to something that already replicates for free.
//
// What genuinely does not come from input is a SETTING. Somebody pressing F3 to turn off
// random encounters is not a simulation input, it is a change to the rules the simulation
// runs under, and the engine applies it immediately to a global. Two machines running
// under different rules diverge, so the change has to be ordered: the host picks a step,
// everybody applies it on that step.
//
// The host assigns the id and therefore the order, which is why a client asks rather than
// issues. Two clients deciding for themselves would give the two machines different orders
// for the same pair of commands.

namespace pilgrimage
{

	enum CommandKind
	{
		// The booster settings, as VALUES not as keypresses. See BoosterSync.h for why
		// replaying the press is not safe.
		kCommandBoosters = 1,

		// One battle action, as the engine's own 72 byte command record. See
		// BattleSync.h. This is the clearest case for the channel there is: the choice
		// is made by one player in a menu whose entire state is a single global, so
		// there is no input to replicate and no way to derive it.
		kCommandBattleCommit = 2,
	};

	// The booster payload. Four ints rather than a packed bitfield because
	// g_boosterSpeedIndex and g_boosterEncounterRate are small integers, not flags, and
	// because 16 bytes is nothing against MaxCommandBytes.
	struct BoosterCommand
	{
		int32_t speedIndex;    // 0 is 1x, 1 is 2x, 2 is 4x
		int32_t encounterRate; // 0 off, 1 normal, 2 high
		int32_t invincible;
		int32_t autoBattle;
	};

	// One battle action.
	//
	// The record goes on the wire VERBATIM rather than being unpacked into fields. Two
	// reasons, and the second is the important one. It is already a flat 72 bytes with
	// no pointers and no padding, so there is nothing to serialise. And unpacking it
	// would mean this file deciding which fields matter, which is a decision that
	// belongs to the engine: the four action entries each carry two dwords nobody has
	// found a writer for, and "reliably zero on the paths that were read" is not the
	// same as "safe to drop".
	//
	// The acting unit is inside the record at +0, which is where FFX_Btl_CommitCommand
	// reads it from, so there is deliberately no separate unit field to disagree with
	// it. unitForLog is a copy for the log and nothing reads it as truth.
	//
	// 80 bytes, against MaxCommandBytes of 96. That 96 exists for this struct.
	struct BattleCommitCommand
	{
		uint8_t record[72]; // ffx::BattleCommandRecord, byte for byte
		int32_t gilCost;    // what the menu staged, for Bribe and for paying Yojimbo
		uint8_t variant;    // 0 a normal CTB turn, 1 a counter or extra turn
		uint8_t unitForLog; // record[0] again, for the log only
		uint8_t reserved[2];
	};

} // namespace pilgrimage
