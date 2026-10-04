#include "ffx/Input.h"

#include <stdio.h>
#include <string.h>

#include "ffx/addresses/Character.h" // Rva::ControlledChr lives there, it owns the CHR
#include "ffx/addresses/Input.h"
#include "ffx/addresses/Minigames.h" // the pad auto repeat timer lives there
#include "workshop/Detour.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

namespace ffx
{

	using workshop::Log;
	using workshop::ModuleAddress;
	using workshop::Readable;

	namespace
	{

		typedef void*(__cdecl* StepControlFn)(void);

		// Everything the swap has to stand in for, gathered so the save and the restore
		// cannot drift apart. The state block is the contiguous span, the rest are the
		// inputs the driver reads from elsewhere.
		struct ControlGlobals
		{
			BYTE* block;      // Rva::PlayerStateBlock, 0x30 bytes
			void** chr;       // Rva::ControlledChr
			BYTE* analogLY;   // Rva::PlayerAnalogLY, LX is the next byte
			DWORD* buttons;   // Rva::PlayerPadButtons
			BYTE* useFixed;   // Rva::PlayerUseFixedYaw
			float* fixedYaw;  // Rva::PlayerFixedYaw
			StepControlFn fn; // Rva::PlayerStepControl
		};

		// Resolve and guard the whole set in one go. Returns false if any part of it is
		// unreachable, because a partial swap would leave the engine driving the wrong
		// character with the wrong ramps, which is worse than not driving at all.
		bool ResolveControlGlobals(ControlGlobals* g)
		{
			g->block = (BYTE*)ModuleAddress(Rva::PlayerStateBlock);
			g->chr = (void**)ModuleAddress(Rva::ControlledChr);
			g->analogLY = (BYTE*)ModuleAddress(Rva::PlayerAnalogLY);
			g->buttons = (DWORD*)ModuleAddress(Rva::PlayerPadButtons);
			g->useFixed = (BYTE*)ModuleAddress(Rva::PlayerUseFixedYaw);
			g->fixedYaw = (float*)ModuleAddress(Rva::PlayerFixedYaw);
			g->fn = (StepControlFn)ModuleAddress(Rva::PlayerStepControl);

			if (!Readable(g->block, Rva::PlayerStateBlockBytes))
				return false;
			if (!Readable(g->chr, sizeof(void*)) || !Readable(g->analogLY, 2))
				return false;
			if (!Readable(g->buttons, sizeof(DWORD)) || !Readable(g->useFixed, 1))
				return false;
			if (!Readable(g->fixedYaw, sizeof(float)) || !Readable((void*)g->fn, 1))
				return false;

			return true;
		}

		// The heading offset is the first field of the block. Forced to zero before each
		// call on the FALLBACK path only, see the header.
		const DWORD kHeadingOffsetInBlock = Rva::PlayerHeadingOffset - Rva::PlayerStateBlock;

		// ---------------------------------------------------------------------------
		// The camera override.
		// ---------------------------------------------------------------------------

		typedef double(__fastcall* CameGetYawFn)(int);

		workshop::CallSitePatch g_yawSites[4];
		bool g_yawPatched = false;


		// Set by StepPlayerControlFor around the one call, read by the replacement. A
		// plain pair of globals and not an atomic anything: the driver, the call sites and
		// this are all on the game thread, inside one function call, with nothing in
		// between that could yield.
		bool g_yawActive = false;
		float g_yawValue = 0.0f;

		// Stands in for FFX_Came_GetYaw at the four sites inside the driver.
		//
		// Declared __cdecl with no arguments rather than __fastcall with one. The real
		// function takes its argument in ecx and pushes nothing, so the two are stack
		// compatible, and the sites pass whatever happens to be in ecx, which the
		// decompiler shows as an uninitialised variable because that is exactly what it
		// is. Reading it would be reading rubbish.
		double __cdecl DriverYawHook()
		{
			if (g_yawActive)
				return (double)g_yawValue;

			// Not driving anybody, so this is the shipped game asking the shipped
			// question. Pass it through, with the ecx argument the site set up, which
			// means doing it in assembly rather than calling a typed pointer.
			CameGetYawFn fn = (CameGetYawFn)ModuleAddress(Rva::PlayerCameGetYawThunk);
			if (!Readable((void*)fn, 1))
				return 0.0;

			// The real function writes through its ecx argument and reads it back, so it
			// needs a real pointer rather than whatever was in the register. A local is
			// the honest thing to give it: the value it leaves there is the yaw, which is
			// also what it returns.
			int scratch = 0;
			return fn((int)&scratch);
		}

	} // namespace

	bool InstallPlayerCameraOverride()
	{
		if (g_yawPatched)
			return true;

		const DWORD sites[4] = { Rva::PlayerDriverYawSite0, Rva::PlayerDriverYawSite1,
			Rva::PlayerDriverYawSite2, Rva::PlayerDriverYawSite3 };

		// All four or none. Verify every site reads the way we expect BEFORE writing any
		// of them, because a driver that reads the owner's yaw in three places and this
		// machine's in the fourth would diverge in exactly the situations the fourth
		// covers, which is the hardest kind of bug to find later.
		for (int i = 0; i < 4; ++i)
		{
			const BYTE* call = (const BYTE*)ModuleAddress(sites[i]);
			if (!Readable(call, 5) || *call != 0xE8)
			{
				Log("player camera override: site %d at RVA 0x%08X is not a call, refusing "
				    "to patch any of the four",
				    i, sites[i]);
				return false;
			}

			INT32 relative = 0;
			memcpy(&relative, call + 1, sizeof(relative));
			if (call + 5 + relative != (const BYTE*)ModuleAddress(Rva::PlayerCameGetYawThunk))
			{
				Log("player camera override: site %d at RVA 0x%08X does not call the camera "
				    "yaw thunk, refusing to patch any of the four",
				    i, sites[i]);
				return false;
			}
		}

		for (int i = 0; i < 4; ++i)
		{
			if (!workshop::PatchCallSite(g_yawSites[i], sites[i], Rva::PlayerCameGetYawThunk,
			        (void*)&DriverYawHook, "player driver camera"))
			{
				// Cannot happen after the pass above, short of another thread writing
				// .text, but if it does then some sites are patched and some are not and
				// that is the one state worth shouting about.
				Log("player camera override: site %d refused AFTER verifying. The driver is "
				    "now half patched, which will diverge. Restart the game.",
				    i);
				return false;
			}
		}

		g_yawPatched = true;
		Log("player camera override: the player driver now anchors to the owner's "
		    "replicated camera yaw, so the engine's own re-anchor smoothing replicates "
		    "instead of being bypassed");
		return true;
	}

	bool PlayerCameraOverrideInstalled()
	{
		return g_yawPatched;
	}

	void ResetPlayerControlState(PlayerControlState* out)
	{
		if (!out)
			return;

		// Zero is the right "never driven" value for all ten fields: the ramps start at
		// no deflection, the headings at zero, and the offset at zero. The engine's own
		// block is zero-filled .data at process start, so this is the state the shipped
		// game begins from.
		memset(out->bytes, 0, sizeof(out->bytes));
		out->seeded = false;
	}

	bool PlayerControlDriverAvailable()
	{
		ControlGlobals g;
		return ResolveControlGlobals(&g);
	}

	bool ReadPlayerAnalogBytes(BYTE* outLX, BYTE* outLY)
	{
		// One dword in the order LY, LX, RY, RX, so LY is first and LX is the next byte.
		// Easy to get backwards, and addresses/Input.h says so for the same reason.
		const BYTE* analog = (const BYTE*)ModuleAddress(Rva::PlayerAnalogLY);
		if (!Readable(analog, 2))
			return false;

		if (outLY)
			*outLY = analog[0];
		if (outLX)
			*outLX = analog[1];
		return true;
	}

	bool StepPlayerControlFor(Character* chr, PlayerControlState* state,
	    const PlayerControlInput& in)
	{
		if (!chr || !state)
			return false;

		ControlGlobals g;
		if (!ResolveControlGlobals(&g))
			return false;

		// ---- save what is live -------------------------------------------------
		BYTE savedBlock[Rva::PlayerStateBlockBytes];
		memcpy(savedBlock, g.block, sizeof(savedBlock));
		void* savedChr = *g.chr;
		const BYTE savedLY = g.analogLY[0];
		const BYTE savedLX = g.analogLY[1];
		const DWORD savedButtons = *g.buttons;
		const BYTE savedUseFixed = *g.useFixed;
		const float savedFixedYaw = *g.fixedYaw;

		// ---- swap this character in ---------------------------------------------
		// A character that has never been driven starts from a zeroed block rather than
		// from whoever ran last, or it inherits their ramps and lurches on its first
		// step.
		if (!state->seeded)
		{
			memset(state->bytes, 0, sizeof(state->bytes));
			state->seeded = true;
		}

		memcpy(g.block, state->bytes, Rva::PlayerStateBlockBytes);

		*g.chr = (void*)chr;
		g.analogLY[0] = in.analogLY;
		g.analogLY[1] = in.analogLX;
		*g.buttons = (DWORD)in.buttons;

		if (g_yawPatched)
		{
			// The good path. The four camera reads inside the driver answer with this,
			// so the anchor, the 20 degree re-anchor test, PrevHeading and HeadingOffset
			// all run the engine's own way on this character's own replicated state.
			// UseFixedYaw and FixedYaw are left alone, which means a script that sets a
			// fixed yaw still works and still replicates.
			g_yawValue = in.cameraYaw;
			g_yawActive = true;
		}
		else
		{
			// The fallback. Stand in for the camera at the final line and collapse the
			// offset. See the header for what this costs.
			*(float*)(g.block + kHeadingOffsetInBlock) = 0.0f;
			*g.useFixed = 1;
			*g.fixedYaw = in.cameraYaw;
		}

		// ---- run the game's own driver ------------------------------------------
		g.fn();
		g_yawActive = false;

		// ---- keep the ramps, put everything else back ---------------------------
		memcpy(state->bytes, g.block, Rva::PlayerStateBlockBytes);

		memcpy(g.block, savedBlock, sizeof(savedBlock));
		*g.chr = savedChr;
		g.analogLY[0] = savedLY;
		g.analogLY[1] = savedLX;
		*g.buttons = savedButtons;
		*g.useFixed = savedUseFixed;
		*g.fixedYaw = savedFixedYaw;

		return true;
	}

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
