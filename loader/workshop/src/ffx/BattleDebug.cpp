#include "ffx/BattleDebug.h"

#include "ffx/Battle.h"
#include "ffx/GameState.h"
#include "ffx/addresses/Battle.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

namespace ffx
{

	using namespace workshop;

	namespace
	{

		struct CheatRow
		{
			DWORD rva;
			const char* name;
			const char* note;
		};

		// One row per BattleCheat, in the same order. The notes are the measured
		// behaviour, not a guess at it, which is why several of them contradict what the
		// name suggests.
		const CheatRow g_cheats[CheatCount] = {
		    { Rva::BtlDbgPlyInvincible, "party takes no damage",
		        "Covers every HP and MP write with no detour. It does NOT block status "
		        "effects, so a Death cast still removes a party member." },
		    { Rva::BtlDbgMagFree, "casting is free",
		        "Player side only, which is what you want. Enemies already pay nothing, "
		        "so this flag is read by the same branch that exempts them." },
		    { Rva::BtlDbgMonHp1, "enemies enter battle at 1 HP",
		        "Takes effect when a battle STARTS, so it does nothing to the fight you "
		        "are already in. Use the kill button for that." },

		    { Rva::BtlDbgLimitBreakOn, "overdrive usable at any gauge",
		        "Does not fill the gauge, it removes the check on it." },
		    { Rva::BtlDbgLimitBreakOff, "overdrive gauge never fills",
		        "The opposite of the one above, and it stops the gauge rising at all." },

		    { Rva::BtlDbgDmgIs100000, "every hit does 100000",
		        "BOTH SIDES. Turn on 'party takes no damage' with it or the first enemy "
		        "attack kills you. The number renderer and the overkill accounting both "
		        "handle it, so nothing visibly breaks." },
		    { Rva::BtlDbgDmgIs10000, "every hit does 10000", "Both sides, as above." },
		    { Rva::BtlDbgDmgIs1, "every hit does 1", "Both sides, as above." },

		    { Rva::BtlDbgCtbPause, "turn order paused",
		        "Nobody's turn advances. Useful for reading state, not for playing." },
		    { Rva::BtlDbgCtbSameOrder, "turn order is fixed",
		        "Removes the variation in who acts next." },
		    { Rva::BtlDbgAutoExecute, "commands execute themselves", "" },
		    { Rva::BtlDbgSkipCommand, "commands are skipped", "" },

		    { Rva::BtlDbgDmgRandomOff, "no random damage spread",
		        "Makes a damage formula reproducible, which is the point of it." },
		    { Rva::BtlDbgDmgCritOff, "no criticals", "" },
		    { Rva::BtlDbgDmgCritOn, "always critical", "" },
		    { Rva::BtlDbgDmgProbOff, "hit probability ignored", "Everything connects." },
		    { Rva::BtlDbgOverKillOff, "no overkill", "" },

		    { Rva::BtlDbgMonInvincible, "enemies take no damage",
		        "Here for completeness. This is the one you do not want on." },
		    { Rva::BtlDbgPlyHp1, "party enters battle at 1 HP",
		        "Takes effect at battle start, like the enemy version." },
		};

		// Per character record offsets the overdrive fields live at. Not in
		// addresses/GameState.h's CharRecord block yet, and that file is being worked on,
		// so they are here with the record size as the bounds check.
		const DWORD kRecordOverdriveMode = 0x38;     // byte, the selected mode
		const DWORD kRecordOverdriveGauge = 0x39;    // byte
		const DWORD kRecordOverdriveGaugeMax = 0x3A; // byte, and "full" means equal
		const DWORD kRecordOverdriveProgress = 0x60; // word[20], 0xFFFF = unavailable
		const DWORD kRecordOverdriveMask = 0x88;     // dword, one bit per mode

		BYTE* CheatByte(BattleCheat which)
		{
			if (which < 0 || which >= CheatCount)
				return nullptr;

			BYTE* p = (BYTE*)ModuleAddress(g_cheats[which].rva);
			return Readable(p, 1) ? p : nullptr;
		}

		// Bounds checked against the record size, so a wrong offset cannot walk into the
		// next character's record.
		BYTE* RecordField(BYTE charIndex, DWORD offset, DWORD bytes)
		{
			if (offset + bytes > CharRecord::Size)
				return nullptr;

			CharRecordData* rec = CharacterRecord(charIndex);
			if (!rec)
				return nullptr;

			BYTE* p = (BYTE*)rec + offset;
			return Readable(p, bytes) ? p : nullptr;
		}

	} // namespace

	bool SetBattleCheat(BattleCheat which, bool on)
	{
		BYTE* p = CheatByte(which);
		if (!p)
			return false;

		const BYTE want = on ? 1 : 0;
		if (*p == want)
			return true;

		*p = want;
		Log("battle cheat: %s %s", g_cheats[which].name, on ? "ON" : "off");
		return true;
	}

	bool BattleCheatOn(BattleCheat which)
	{
		const BYTE* p = CheatByte(which);
		return p && *p != 0;
	}

	int BattleCheatsOn()
	{
		int count = 0;
		for (int i = 0; i < CheatCount; ++i)
			if (BattleCheatOn((BattleCheat)i))
				++count;
		return count;
	}

	void ClearBattleCheats()
	{
		for (int i = 0; i < CheatCount; ++i)
			SetBattleCheat((BattleCheat)i, false);
	}

	const char* BattleCheatName(BattleCheat which)
	{
		if (which < 0 || which >= CheatCount)
			return "?";
		return g_cheats[which].name;
	}

	const char* BattleCheatNote(BattleCheat which)
	{
		if (which < 0 || which >= CheatCount)
			return "";
		return g_cheats[which].note;
	}

	bool SetAllEnemiesTo1Hp()
	{
		typedef void(__cdecl * SetAllFn)(int toOne);
		SetAllFn fn = (SetAllFn)ModuleAddress(Rva::BtlDebugSetAllMonsterHp);
		if (!Readable((void*)fn, 1))
			return false;

		// It checks for a running battle itself, so there is no gate to add here.
		fn(1);
		Log("battle cheat: every enemy set to 1 HP");
		return true;
	}

	int OverdriveModeMask(BYTE charIndex)
	{
		const DWORD* p = (const DWORD*)RecordField(charIndex, kRecordOverdriveMask, 4);
		return p ? (int)*p : -1;
	}

	bool SetOverdriveModeMask(BYTE charIndex, int mask)
	{
		DWORD* p = (DWORD*)RecordField(charIndex, kRecordOverdriveMask, 4);
		if (!p)
			return false;
		*p = (DWORD)mask;
		return true;
	}

	bool UnlockAllOverdriveModes(BYTE charIndex)
	{
		// 20 bits. Setting them all also stops the "learned a new mode" popup, because
		// the engine's grant loop only fires on a mode whose bit is still clear.
		if (!SetOverdriveModeMask(charIndex, 0x000FFFFF))
			return false;
		Log("battle cheat: all 20 overdrive modes unlocked for character %d",
		    (int)charIndex);
		return true;
	}

	int OverdriveMode(BYTE charIndex)
	{
		const BYTE* p = RecordField(charIndex, kRecordOverdriveMode, 1);
		return p ? (int)*p : -1;
	}

	bool SetOverdriveMode(BYTE charIndex, int modeId)
	{
		if (modeId < 0 || modeId >= kOverdriveModeCount)
			return false;

		BYTE* p = RecordField(charIndex, kRecordOverdriveMode, 1);
		if (!p)
			return false;

		*p = (BYTE)modeId;

		// In battle the actor holds its own copy and the record one is not re-read, so
		// a change made mid-battle is undone when the battle ends unless the actor is
		// written too. That write needs the battle actor, which is battle work RAM, so
		// it is left to the caller rather than guessed at here.
		if (BattleRunning())
			Log("battle cheat: overdrive mode set on the record while a battle is "
			    "running, so it will not apply until the next one");
		return true;
	}

	int OverdriveModeInDisplayOrder(int position)
	{
		if (position < 0 || position >= kOverdriveModeCount)
			return -1;

		const BYTE* order = (const BYTE*)ModuleAddress(Rva::BtlOdModeDisplayOrder);
		if (!Readable(order, kOverdriveModeCount))
			return -1;
		return (int)order[position];
	}

	int OverdriveModeProgress(BYTE charIndex, int modeId)
	{
		if (modeId < 0 || modeId >= kOverdriveModeCount)
			return -1;

		const WORD* p = (const WORD*)RecordField(charIndex,
		    kRecordOverdriveProgress + (DWORD)(2 * modeId), 2);
		return p ? (int)*p : -1;
	}

	bool SetOverdriveModeProgress(BYTE charIndex, int modeId, int value)
	{
		if (modeId < 0 || modeId >= kOverdriveModeCount || value < 0 || value > 0xFFFF)
			return false;

		WORD* p = (WORD*)RecordField(charIndex,
		    kRecordOverdriveProgress + (DWORD)(2 * modeId), 2);
		if (!p)
			return false;
		*p = (WORD)value;
		return true;
	}

	int OverdriveGauge(BYTE charIndex)
	{
		const BYTE* p = RecordField(charIndex, kRecordOverdriveGauge, 1);
		return p ? (int)*p : -1;
	}

	int OverdriveGaugeMax(BYTE charIndex)
	{
		const BYTE* p = RecordField(charIndex, kRecordOverdriveGaugeMax, 1);
		return p ? (int)*p : -1;
	}

	bool SetOverdriveGauge(BYTE charIndex, int value)
	{
		BYTE* gauge = RecordField(charIndex, kRecordOverdriveGauge, 1);
		const BYTE* max = RecordField(charIndex, kRecordOverdriveGaugeMax, 1);
		if (!gauge || !max)
			return false;

		if (value < 0)
			value = 0;
		// The full test is an equality against this byte, so anything above it is a
		// state the engine never produces and would read as "not full".
		if (value > (int)*max)
			value = (int)*max;

		*gauge = (BYTE)value;
		return true;
	}

	bool FillOverdriveGauge(BYTE charIndex)
	{
		BYTE* gauge = RecordField(charIndex, kRecordOverdriveGauge, 1);
		const BYTE* max = RecordField(charIndex, kRecordOverdriveGaugeMax, 1);
		if (!gauge || !max)
			return false;

		*gauge = *max;
		return true;
	}

	int FillAllOverdriveGauges()
	{
		int filled = 0;
		// All 18 records, so the ten aeons get theirs filled too. They have their own
		// gauge and their own max, and a Grand Summon is the only way the player ever
		// sees one.
		for (BYTE i = 0; i < (BYTE)kCharCount; ++i)
			if (FillOverdriveGauge(i))
				++filled;
		return filled;
	}

} // namespace ffx
