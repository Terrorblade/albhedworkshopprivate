#include "ffx/Input.h"

#include <stdio.h>
#include <string.h>

#include "ffx/addresses/Input.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

namespace ffx
{

	using workshop::Log;
	using workshop::ModuleAddress;
	using workshop::Readable;

	namespace
	{

		// The whole singleton in one guard. 0xB8 covers every field InputState:: names,
		// and the object is a single static so there is no chance of a partial mapping,
		// but checking costs nothing and the rule is the rule.
		const SIZE_T kSingletonBytes = 0xB8;

		// Reads a dword out of the host image at an RVA, or returns fallback.
		DWORD ReadHostDword(DWORD rva, DWORD fallback)
		{
			const void* p = ModuleAddress(rva);
			if (!Readable(p, sizeof(DWORD)))
				return fallback;
			return *(const volatile DWORD*)p;
		}

		BYTE ReadHostByte(DWORD rva, BYTE fallback)
		{
			const void* p = ModuleAddress(rva);
			if (!Readable(p, 1))
				return fallback;
			return *(const volatile BYTE*)p;
		}

		// An unsigned stick byte, 0x80 centre, to -1..1 keeping the engine's sign.
		float StickByteToFloat(BYTE raw)
		{
			return (float)((int)raw - 0x80) / 127.0f;
		}

		struct NamedBit
		{
			DWORD mask;
			const char* name;
		};

		// Order matters only for how a log line reads. The PS2 bits come first because
		// they are the ones that reach gameplay.
		const NamedBit kButtonNames[] = {
			{ Btn::Up, "Up" },
			{ Btn::Down, "Down" },
			{ Btn::Left, "Left" },
			{ Btn::Right, "Right" },
			{ Btn::Triangle, "Triangle" },
			{ Btn::Circle, "Circle" },
			{ Btn::Cross, "Cross" },
			{ Btn::Square, "Square" },
			{ Btn::L1, "L1" },
			{ Btn::R1, "R1" },
			{ Btn::L2, "L2" },
			{ Btn::R2, "R2" },
			{ Btn::Start, "Start" },
			{ Btn::Select, "Select" },
			{ Btn::L3, "L3?" }, // never set by the PC poll, so flag it if seen
			{ Btn::R3, "R3?" },
			{ Btn::BoosterSpeed, "Spd" },
			{ Btn::BoosterAutoBattle, "Auto" },
			{ Btn::PcAction30, "Pc30" },
			{ Btn::PcAction31, "Pc31" },
			{ Btn::PcAction32, "Pc32" },
			{ Btn::BoosterInvincible, "Invin" },
			{ Btn::BoosterNoEncounter, "NoEnc" },
			{ Btn::EscMenu, "Esc" },
			{ Btn::MarkR1, "+R1" },
			{ Btn::MarkR2, "+R2" },
			{ Btn::MarkL1, "+L1" },
			{ Btn::MarkL2, "+L2" },
		};

		const char* kPadSemanticNames[PadSemantic::Count] = {
			"Square", "Cross", "Circle", "Triangle",
			"L1", "R1", "L2", "R2",
			"Select", "Start", "L3", "R3",
			"Up", "Right", "Down", "Left"
		};

	} // namespace

	// ---------------------------------------------------------------------------
	// Reading
	// ---------------------------------------------------------------------------

	BYTE* InputSingletonPtr()
	{
		BYTE* p = (BYTE*)ModuleAddress(Rva::InputSingleton);
		if (!Readable(p, kSingletonBytes))
			return NULL;
		return p;
	}

	DWORD ButtonMask()
	{
		BYTE* s = InputSingletonPtr();
		if (!s)
			return 0;
		return *(volatile DWORD*)(s + InputState::Current);
	}

	DWORD PreviousButtonMask()
	{
		BYTE* s = InputSingletonPtr();
		if (!s)
			return 0;
		return *(volatile DWORD*)(s + InputState::Previous);
	}

	DWORD ButtonsPressed()
	{
		BYTE* s = InputSingletonPtr();
		if (!s)
			return 0;
		const DWORD cur = *(volatile DWORD*)(s + InputState::Current);
		const DWORD prev = *(volatile DWORD*)(s + InputState::Previous);
		return cur & ~prev;
	}

	DWORD ButtonsReleased()
	{
		BYTE* s = InputSingletonPtr();
		if (!s)
			return 0;
		const DWORD cur = *(volatile DWORD*)(s + InputState::Current);
		const DWORD prev = *(volatile DWORD*)(s + InputState::Previous);
		return prev & ~cur;
	}

	bool ReadSticks(Sticks* out)
	{
		memset(out, 0, sizeof(*out));
		BYTE* s = InputSingletonPtr();
		if (!s)
			return false;
		out->leftX = *(volatile float*)(s + InputState::AxisLeftX);
		out->leftY = *(volatile float*)(s + InputState::AxisLeftY);
		out->rightX = *(volatile float*)(s + InputState::AxisRightX);
		out->rightY = *(volatile float*)(s + InputState::AxisRightY);
		return true;
	}

	float HoldSeconds(int bitIndex)
	{
		if (bitIndex < 0 || (DWORD)bitIndex >= Btn::HoldTimerCount)
			return 0.0f;
		BYTE* s = InputSingletonPtr();
		if (!s)
			return 0.0f;
		return *(volatile float*)(s + InputState::HoldTimers + 4 * (DWORD)bitIndex);
	}

	bool LastInputWasPad()
	{
		BYTE* s = InputSingletonPtr();
		if (!s)
			return false;
		return *(volatile BYTE*)(s + InputState::LastInputWasPad) != 0;
	}

	DWORD ThreadedPadMode()
	{
		return ReadHostDword(Rva::ThreadedPadMode, 0);
	}

	// ---------------------------------------------------------------------------
	// The player-input globals
	// ---------------------------------------------------------------------------

	DWORD PlayerButtons()
	{
		return ReadHostDword(Rva::PlayerPadButtons, 0);
	}

	DWORD PlayerButtonsPressed()
	{
		return ReadHostDword(Rva::PlayerPadPressed, 0);
	}

	bool ReadPlayerSticks(Sticks* out)
	{
		memset(out, 0, sizeof(*out));
		// All four bytes sit in one dword, so one guard covers them.
		const void* p = ModuleAddress(Rva::PlayerAnalogLY);
		if (!Readable(p, 4))
			return false;
		out->leftY = StickByteToFloat(ReadHostByte(Rva::PlayerAnalogLY, 0x80));
		out->leftX = StickByteToFloat(ReadHostByte(Rva::PlayerAnalogLX, 0x80));
		out->rightY = StickByteToFloat(ReadHostByte(Rva::PlayerAnalogRY, 0x80));
		out->rightX = StickByteToFloat(ReadHostByte(Rva::PlayerAnalogRX, 0x80));
		return true;
	}

	// ---------------------------------------------------------------------------
	// Names
	// ---------------------------------------------------------------------------

	const char* ButtonNames(DWORD mask)
	{
		static char text[256];
		text[0] = 0;
		if (mask == 0)
			return "none";

		DWORD remaining = mask;
		const int count = (int)(sizeof(kButtonNames) / sizeof(kButtonNames[0]));
		for (int i = 0; i < count; ++i)
		{
			if ((mask & kButtonNames[i].mask) == 0)
				continue;
			if (text[0] != 0)
				strcat_s(text, sizeof(text), " ");
			strcat_s(text, sizeof(text), kButtonNames[i].name);
			remaining &= ~kButtonNames[i].mask;
		}

		// Should be unreachable. If it fires, the mask grew a bit we have not mapped,
		// and that is worth seeing in a log rather than dropping on the floor.
		if (remaining != 0)
		{
			char extra[24];
			sprintf_s(extra, sizeof(extra), " +0x%08X", (unsigned)remaining);
			strcat_s(text, sizeof(text), extra);
		}
		return text;
	}

	const char* PadSemanticName(int semantic)
	{
		const int index = semantic - PadSemantic::First;
		if (index < 0 || index >= PadSemantic::Count)
			return "?";
		return kPadSemanticNames[index];
	}

	void LogInputState()
	{
		BYTE* s = InputSingletonPtr();
		if (!s)
		{
			Log("input: singleton at RVA 0x%08X is not readable yet",
			    (unsigned)Rva::InputSingleton);
			return;
		}

		Sticks sticks;
		ReadSticks(&sticks);

		Log("input: mask 0x%08X [%s]", (unsigned)ButtonMask(), ButtonNames(ButtonMask()));
		Log("  pressed  0x%08X [%s]", (unsigned)ButtonsPressed(), ButtonNames(ButtonsPressed()));
		Log("  released 0x%08X [%s]", (unsigned)ButtonsReleased(), ButtonNames(ButtonsReleased()));
		Log("  sticks L %.3f %.3f  R %.3f %.3f (Y positive is down)",
		    sticks.leftX, sticks.leftY, sticks.rightX, sticks.rightY);
		Log("  lastInputWasPad=%d threadedPadMode=%lu%s",
		    LastInputWasPad() ? 1 : 0, (unsigned long)ThreadedPadMode(),
		    ThreadedPadMode() ? "  WARNING: mask is latched inside animate" : "");

		Sticks player;
		const bool havePlayer = ReadPlayerSticks(&player);
		Log("  player  buttons 0x%04X pressed 0x%04X",
		    (unsigned)(PlayerButtons() & 0xFFFFu),
		    (unsigned)(PlayerButtonsPressed() & 0xFFFFu));
		if (havePlayer)
			Log("  player  sticks L %.3f %.3f  R %.3f %.3f",
			    player.leftX, player.leftY, player.rightX, player.rightY);
	}

	// ---------------------------------------------------------------------------
	// Injection
	// ---------------------------------------------------------------------------

	bool WriteButtonMask(DWORD mask, DWORD prevMask)
	{
		BYTE* s = InputSingletonPtr();
		if (!s)
			return false;
		// Previous first. If something reads between the two stores, a stale prev
		// paired with a fresh cur invents an edge, whereas a fresh prev paired with a
		// stale cur only loses one.
		*(volatile DWORD*)(s + InputState::Previous) = prevMask;
		*(volatile DWORD*)(s + InputState::Current) = mask;
		return true;
	}

	bool WriteSticks(const Sticks& sticks)
	{
		BYTE* s = InputSingletonPtr();
		if (!s)
			return false;
		*(volatile float*)(s + InputState::AxisLeftX) = sticks.leftX;
		*(volatile float*)(s + InputState::AxisLeftY) = sticks.leftY;
		*(volatile float*)(s + InputState::AxisRightX) = sticks.rightX;
		*(volatile float*)(s + InputState::AxisRightY) = sticks.rightY;
		return true;
	}

	bool SuppressInput()
	{
		BYTE* s = InputSingletonPtr();
		if (!s)
			return false;
		// BOTH, and prev first. Zeroing only cur leaves prev holding whatever was
		// down a moment ago, and Released is prev & ~cur, so every button the player
		// happened to be holding when control was taken away would fire a release
		// edge. Zeroing both makes the frame read as "nothing happened", which is
		// what suppression should look like. The cost is that a genuinely held button
		// never reports its release, so code that acts on release will not see it.
		*(volatile DWORD*)(s + InputState::Previous) = 0;
		*(volatile DWORD*)(s + InputState::Current) = 0;

		Sticks centred;
		centred.leftX = centred.leftY = centred.rightX = centred.rightY = 0.0f;
		WriteSticks(centred);
		return true;
	}

} // namespace ffx
