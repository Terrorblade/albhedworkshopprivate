#pragma once

// The developers' own battle cheats, which shipped in the retail binary.
//
// There is a 48 byte block of plain .data at RVA 0xD2A8F8 that the dev team used to
// test battles. It is zero at startup, nothing in the game ever writes it, and it is
// NOT gated on the game's debug mode. So god mode, free casting and instant kill are
// one byte each, with no detour and no patched call site.
//
//     ffx::SetBattleCheat(ffx::CheatPartyInvincible, true);
//
// The list is table driven on purpose: iterate BattleCheatCount and you get every
// cheat with its name and its caveat, so a UI never has to hardcode them.
//
// Addresses are in addresses/Battle.h and the research is in reversing/CHEAT_BATTLE.md.

#include <windows.h>

namespace ffx
{

	enum BattleCheat
	{
		// The big three.
		CheatPartyInvincible = 0, // no HP or MP damage reaches the party
		CheatFreeCasting,         // every MP cost reads as 0, player side only
		CheatEnemiesEnterAt1Hp,   // the instant kill, see the note on it

		// Overdrives.
		CheatOverdriveAnyTime, // usable at any gauge
		CheatOverdriveNeverFills,

		// Damage overrides. Each one forces the number, both sides.
		CheatMaxDamage,
		CheatDamageIs10000,
		CheatDamageIs1,

		// Turn order and command flow.
		CheatPauseTurnOrder,
		CheatSameTurnOrder,
		CheatAutoExecute,
		CheatSkipCommand,

		// Formula switches, for working out what a number is made of.
		CheatNoRandomSpread,
		CheatNoCriticals,
		CheatAlwaysCritical,
		CheatNoHitProbability,
		CheatNoOverkill,

		// The other side, for completeness.
		CheatEnemiesInvincible,
		CheatPartyEntersAt1Hp,

		CheatCount
	};

	// False when the flag byte is not writable. Writes take effect on the next thing
	// that reads them, which for the per-battle ones means the next battle.
	bool SetBattleCheat(BattleCheat which, bool on);
	bool BattleCheatOn(BattleCheat which);

	// How many cheats are on, for a status line.
	int BattleCheatsOn();

	// Turns every one of them off. Worth calling before reporting a bug.
	void ClearBattleCheats();

	// A short label and the caveat that goes with it. Never NULL, so a UI can draw a
	// row for every index without checking. The note is the thing worth reading: some
	// of these are not side selective and some do less than their name suggests.
	const char* BattleCheatName(BattleCheat which);
	const char* BattleCheatNote(BattleCheat which);

	// ---------------------------------------------------------------------------
	// Buttons rather than flags
	// ---------------------------------------------------------------------------

	// Sets every live enemy to 1 HP right now, which is the mid-battle instant kill.
	// The game's own helper, and it guards itself on there being a battle, so calling
	// it outside one is safe and does nothing.
	//
	// Why 1 HP rather than maximum damage: the damage cap is 99999 and several bosses
	// have millions of HP, so a big number is not a kill. Dropping them to 1 means the
	// next hit kills, and because they really die of damage the bestiary, overkill,
	// drops and AP all still work.
	bool SetAllEnemiesTo1Hp();

	// ---------------------------------------------------------------------------
	// Overdrives
	// ---------------------------------------------------------------------------

	// The 20 overdrive modes. The ids are the game's, and the display order is its own
	// table rather than 0..19.
	const int kOverdriveModeCount = 20;

	// record+0x88, one bit per mode. Minus one when there is no game.
	int OverdriveModeMask(BYTE charIndex);

	// Sets all 20 bits. This also suppresses the "learned a new overdrive mode" popups,
	// because the grant loop only fires on a mode whose bit is still clear.
	bool UnlockAllOverdriveModes(BYTE charIndex);
	bool SetOverdriveModeMask(BYTE charIndex, int mask);

	// record+0x38, which mode is selected. Minus one on failure.
	int OverdriveMode(BYTE charIndex);
	bool SetOverdriveMode(BYTE charIndex, int modeId);

	// The mode id at a position in the game's own display order, or -1.
	int OverdriveModeInDisplayOrder(int position);

	// record+0x60 + 2*modeId, the progress counter toward earning the mode. 0xFFFF
	// means the mode is not available to this character at all, which is why it is
	// returned rather than clamped.
	int OverdriveModeProgress(BYTE charIndex, int modeId);
	bool SetOverdriveModeProgress(BYTE charIndex, int modeId, int value);

	// ---------------------------------------------------------------------------
	// The gauge itself
	// ---------------------------------------------------------------------------
	//
	// "FULL" IS NOT A NUMBER HERE. The gauge is 0 to the character's own max and the
	// engine's test is gauge == max, not gauge >= 100, so filling it means copying the
	// max over the gauge. The shipped idiom, in the booster's invincible refill, is
	// exactly that one byte copy. The max is per character and per overdrive mode, so
	// never write a constant.
	//
	// TWO COPIES OF THE SAME NUMBER, and they are both real. The save record pair is
	// what persists and what the next battle loads from, and the battle actor pair is
	// what the current battle reads. BtlLoadUnitParams copies record+0x38/0x39/0x3A
	// into actor+0x5BB/0x5BC/0x5BD at battle start, so mid battle the record is the
	// stale one. Write the actor with ffx::SetUnitOverdrive in ffx/Battle.h for a
	// battle that is already running, and these for one that has not started.

	// record+0x39 and record+0x3A. Minus one when there is no game.
	int OverdriveGauge(BYTE charIndex);
	int OverdriveGaugeMax(BYTE charIndex);

	// Clamped to 0..max, because a gauge above max is a state the engine never
	// produces and the full test is an equality.
	bool SetOverdriveGauge(BYTE charIndex, int value);

	// gauge = max, which is the engine's own one-liner for "full".
	bool FillOverdriveGauge(BYTE charIndex);

	// All 18 records, the ten aeons included. Returns how many took.
	int FillAllOverdriveGauges();

} // namespace ffx
