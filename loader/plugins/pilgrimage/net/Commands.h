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

		// The in-game menu opening. See MenuSync.h. The only part of the menu that
		// needs a command: the Triangle test lives in FFX_MainStep reading the local
		// hardware pad mask, before the menu system exists, so without this the
		// person who pressed it opens a menu and the other machine does not. Closing
		// needs nothing, because the close is decided inside the menu from the
		// replicated pad block.
		kCommandMenuOpen = 3,

		// The host taking or giving back control of whatever menu is up, from
		// ctrl+F4. Ordered rather than a local flag flip, because the two machines
		// disagreeing about who is driving is the one failure in that subsystem that
		// cannot be recovered from.
		kCommandMenuOverride = 4,

		// One peer's Esc pause menu opening or closing. See PauseSync.h, and read the
		// asymmetry note there before using this: the OPEN is what this command is
		// for, and the close is observed rather than relied on arriving, because a
		// command can only be consumed on the step it was stamped for and nobody's
		// clock is moving while everybody is paused.
		kCommandPause = 5,

		// One peer taking one character. See coop/Ownership.h, and read the warning
		// on SetCharacterOwner before touching this: a binding applied on one
		// machine and not the other is the one failure in that subsystem with no way
		// back, which is exactly why it is on this channel.
		//
		// It is here rather than being derived because the binding is a CHOICE
		// somebody made in a menu, not a consequence of input. Both machines do run
		// the Config screen from the same replicated pad block and would reach the
		// same setter on the same step, so in the normal case this is belt to the
		// braces. It earns its place in the abnormal one: if the two machines ever
		// have different rows installed, or if the pad replication has a hole,
		// deriving the binding locally would split the ownership table and nothing
		// afterwards would notice.
		kCommandCharOwner = 6,
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

	// Which menu to open. One int rather than nothing at all, because the mode
	// decides which branch FFX_MenuSys_Enter takes and only mode 0 is the ordinary
	// menu. Sending it means the two machines cannot end up in two different modes
	// if a later version ever replicates one of the save-UI paths.
	struct MenuOpenCommand
	{
		int32_t mode; // ffx::kMenuModeMain, which is 0
	};

	// The host override, as the WANTED STATE rather than as "toggle".
	//
	// A toggle would be wrong for the same reason BoosterSync sends values instead of
	// keypresses: two commands crossing, or one arriving twice, would leave the
	// machines on opposite settings. A state is idempotent.
	struct MenuOverrideCommand
	{
		uint8_t held; // 1 the host is driving every menu, 0 back to normal
		uint8_t reserved[3];
	};

	// One peer's pause state, again as a state and not an edge, and for the same
	// reason. The peer is not in here because CommandPayload already carries the
	// issuer and two fields that have to agree are one field too many.
	struct PauseCommand
	{
		uint8_t paused;
		uint8_t reserved[3];
	};

	// One peer taking one character, as an absolute binding rather than a swap.
	//
	// The pair goes straight into SetCharacterOwner, so the exclusivity rule is the
	// one documented there: binding a peer to a character takes that peer off
	// whatever it had. Sending the whole table instead was considered and dropped,
	// because one binding is already idempotent and the host orders them, so two
	// crossing commands leave both machines in the same state whichever way round
	// they arrive.
	struct CharOwnerCommand
	{
		uint8_t peer;      // 0 is the host, and it has to be a real peer id
		uint8_t charIndex; // a save block character index 0..7, or 0xFF to unbind
		uint8_t reserved[2];
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
