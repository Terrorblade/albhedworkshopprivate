#pragma once

#include <windows.h>

// The input layer: the button mask, the analogue sticks, the keyboard path and
// the emulated scePad ports.
//
// Addresses are RVAs, which is the IDA VA minus 0x00400000. Names and signatures
// were read out of the IDB rather than assumed. Everything in the FFX_Input and
// FFX_Pad families is __cdecl unless a comment says __thiscall.
//
// The one thing to know before using any of this: FFX collapses every physical
// device into ONE global 32-bit mask plus FOUR global axis floats, both inside
// the FFX_Input singleton. There is no port argument anywhere above that point,
// so emulated scePad port 1 is a byte-for-byte clone of port 0 and is not a
// second controller. A real second pad comes from PhyreEngine's 18 pad slots,
// which ffx/Pad.h already wraps. See reversing/INPUT_LAYER.md for the derivation.

namespace ffx
{
	namespace Rva
	{

		// ---------------------------------------------------------------------------
		// The input singleton, and the frame slot that fills it.
		//
		// InputSingleton is the FFX_Input object itself, not a pointer to it. Its layout
		// is in InputState:: in ffx/Input.h. The whole binary reaches it through exactly
		// one function, InputGet, which is a 6-byte leaf that returns its address, so
		// that one detour covers every reader and every writer in the game.
		//
		// UpdateVtableSlot is the frame slot that latches the mask. It sits three slots
		// after AnimateVtableSlot in the same FFXApplication vtable and Phyre calls it
		// immediately BEFORE animate, which is what makes the existing animate hook a
		// usable injection point. See ffx/Input.h for the caveats.
		// ---------------------------------------------------------------------------
		const DWORD InputSingleton = 0x008CB170; // FFX_Input, the object. 184 bytes used.
		const DWORD InputGet = 0x00231300;       // void *(void), returns &InputSingleton

		const DWORD UpdateVtableSlot = 0x0070D9B4; // FFXApplication vtable +0x1C
		const DWORD UpdateExpected = 0x0002F770;   // what that slot should contain

		// ---------------------------------------------------------------------------
		// Reading the mask and the sticks. Neither takes a port or device argument,
		// which is the whole co-op problem in two function signatures.
		// ---------------------------------------------------------------------------
		const DWORD InputGetButtonMask = 0x00230C80; // int (void), the level mask
		const DWORD InputGetAnalogAxes = 0x00230C90; // float *(void), 4 floats LX LY RX RY

		const DWORD InputIsHeld = 0x00230CD0;      // BOOL (int mask), cur & mask
		const DWORD InputIsPressed = 0x00230CF0;   // BOOL (int mask), rising edge
		const DWORD InputIsReleased = 0x00230D10;  // BOOL (int mask), falling edge
		const DWORD InputIsRepeating = 0x00230D30; // int  (int mask), edge or auto repeat

		// The same four against an explicit singleton pointer, __thiscall(void *, int).
		// Useful if a mod keeps a shadow copy and wants the engine's own edge logic.
		const DWORD InputMaskIsHeld = 0x00231340;
		const DWORD InputMaskIsPressed = 0x00231360;
		const DWORD InputMaskIsReleased = 0x00231380;
		const DWORD InputMaskIsRepeating = 0x002313A0;

		// ---------------------------------------------------------------------------
		// The writers. Everything that puts a value into the mask is in this block.
		// ---------------------------------------------------------------------------

		// __thiscall(void *singleton) with the frame delta already on the FPU stack.
		// Walks the 75-entry action map, ORs in a PS2 bit per action that is down, reads
		// the four axes, then does prev = cur, cur = newMask. The single collapse point.
		const DWORD InputPoll = 0x00232A30;

		// void (void). Called from FFXApplication__update, the pre-animate slot. Runs
		// InputPoll only while ThreadedPadMode is 0.
		const DWORD InputUpdateFromApp = 0x00230E80;

		// int (void). Called from FFXApplication__animate when the esc menu is open, and
		// it re-runs InputPoll. A pre-animate injection is overwritten by this one.
		const DWORD InputPollForMenu = 0x00244990;

		// The threaded path. int (void) pops one queued sample into the singleton, and
		// it is called from the TOP of FFX_MainStepLoop, which is inside animate, once
		// per catch-up substep. So in threaded mode a pre-animate injection is lost.
		const DWORD InputConsumeThreadedSample = 0x00230DE0;
		const DWORD InputPopThreadedSample = 0x00231550; // __thiscall(void *)

		// Dword. Decides which of the two writers above runs, and therefore whether an
		// animate-time injection survives. 0 = latched before animate, non-zero = latched
		// inside it.
		const DWORD ThreadedPadMode = 0x00F3C930;

		// ---------------------------------------------------------------------------
		// The engine's own poll override. Nothing in the retail binary installs it.
		//
		// _DWORD *(_DWORD *outPreviousTriple, int ctx, int arg, void *callback). The
		// triple lands at singleton+172/+176/+180 and the previous one comes back through
		// the out parameter. While installed, InputPoll calls callback(ctx, arg, &skip)
		// at its very top and, when skip is non-zero, abandons the entire poll without
		// touching cur, prev or the axes. That is a supported suppression and injection
		// point built into the game, and it covers both the threaded and non-threaded
		// paths because both go through InputPoll.
		// ---------------------------------------------------------------------------
		const DWORD InputSetPollOverride = 0x00230DF0;

		// ---------------------------------------------------------------------------
		// Hold timers and the auto-repeat these feed.
		// ---------------------------------------------------------------------------
		const DWORD InputUpdateHoldTimers = 0x002330E0; // __thiscall(void *), tail of poll
		const DWORD InputClearHoldTimers = 0x00231270;  // __thiscall(void *)
		const DWORD InputGetTimeSeconds = 0x00230C60;   // float (void), wall clock

		// ---------------------------------------------------------------------------
		// The action map, which is how the bit names were established.
		//
		// InputBuildActionMap runs once from FFXApplication__initApplication and pairs
		// each remappable action index with a PS2 bit. ConfigGetBindingForAction proves
		// action index N lives at settings offset 96 + 4*N, and DefaultKeyBindings gives
		// each action's shipped default: a key code for the keyboard actions and a Phyre
		// joypad semantic for the pad actions. Those semantics are what name the bits.
		// ---------------------------------------------------------------------------
		const DWORD InputBuildActionMap = 0x002315C0; // __thiscall(void *, int)
		const DWORD GameSettings = 0x01EFB504;        // void *, the settings block POINTER

		const DWORD ConfigGetBindingForAction = 0x002311B0;        // __thiscall(void *settings, int action)
		const DWORD ConfigGetDefaultBindingForAction = 0x00002BE0; // __stdcall(int action)
		const DWORD DefaultKeyBindings = 0x0088A538;               // DWORD[>=75], 0 means unbound

		// Keyboard naming and the modifier aliases.
		const DWORD InputKeyCodeToName = 0x00005AF0; // const char *(unsigned keyCode), 0 if none
		const DWORD KeyCodeNames = 0x0088ABE0;       // const char *[128]
		const DWORD KeyAliasTable = 0x00832270;      // 4 rows of {srcKey, dstKey, latchByte}

		// The options-screen tables, which tie an action index to a localised label. Only
		// needed if a mod wants to show the player's own binding for something.
		const DWORD ControllerPageTable = 0x00839EBC; // 14 rows of {action, msgId, order}
		const DWORD KeyboardPageTable = 0x008399EC;   // 23 rows of {action, altAction, msgId, order}

		// ---------------------------------------------------------------------------
		// The emulated scePad layer. This is what gameplay actually reads.
		//
		// PadFillSceReadData and PadFillAnalogAndSynthDpad take port and slot arguments,
		// ignore them when fetching the data, and feed both ports from the one singleton.
		// Making those two port aware is the cleanest way to give player 2 a pad inside
		// the game's own abstraction, because the whole atel event path and the battle
		// code already call the two-port API.
		// ---------------------------------------------------------------------------
		const DWORD PadPortState = 0x00F30288;    // 2 ports of 256 bytes, see PortState::
		const DWORD PadGetPortState = 0x00488EC0; // char *(int port, int slot)

		const DWORD PadReadButtons16 = 0x00488D70;  // short (int port, int slot, int ring)
		const DWORD PadReadPressed16 = 0x00488E80;  // short (int port, int slot, int ring)
		const DWORD PadReadReleased16 = 0x00488EA0; // short (int port, int slot, int ring)
		const DWORD PadReadAnalogByte = 0x00488C20; // char (int port, int slot, int axis, char ring)

		const DWORD PadFillSceReadData = 0x004898A0;        // int (int port, int slot, int state, int buf)
		const DWORD PadFillAnalogAndSynthDpad = 0x00489570; // char (int state, int buf, short mask)
		const DWORD PadUpdateAll = 0x00489A80;              // called from FFX_MainStep, inside animate

		// ---------------------------------------------------------------------------
		// The player-input globals. One slot each for the whole game, which is the
		// co-op blocker proper. PlayerReadPad fills them from port 0 slot 0 and
		// PlayerStepControl turns them into movement on the single controlled character.
		//
		// The analogue bytes are deliberately listed low address first, because they sit
		// in one dword in the order LY, LX, RY, RX and that ordering is easy to get
		// backwards. Unsigned, 0x80 is centre, Y POSITIVE IS DOWN.
		// ---------------------------------------------------------------------------
		const DWORD PlayerAnalogLY = 0x01FC44A4; // BYTE
		const DWORD PlayerAnalogLX = 0x01FC44A5; // BYTE
		const DWORD PlayerAnalogRY = 0x01FC44A6; // BYTE
		const DWORD PlayerAnalogRX = 0x01FC44A7; // BYTE

		const DWORD PlayerPadPressed = 0x01FC44A8; // DWORD, cur & (cur ^ prev), low 16 bits used
		const DWORD PlayerPadButtons = 0x01FC44AC; // DWORD, the level mask, low 16 bits used
		const DWORD PlayerPadPrev = 0x01FC44B0;    // DWORD

		const DWORD PlayerReadPad = 0x0042D070;         // char (void), the latch
		const DWORD PlayerGetPadButtons = 0x0042D050;   // int (void), CLEARS PlayerPadPressed
		const DWORD PlayerStepControl = 0x0042D180;     // the input to movement driver
		const DWORD PlayerCamReadPadInput = 0x0043F4B0; // right stick to camera

		// Post-deadzone stick values PlayerStepControl derives, ints centred on 128.
		const DWORD PlayerStickLX = 0x00F007A8;
		const DWORD PlayerStickLY = 0x00F007AC;

		// Float radians, the heading PlayerStepControl asks the character to face.
		const DWORD PlayerDesiredHeading = 0x00F007A0;

		// Dword. PlayerStepControl forces move speed to 0 when this is 0, but still sets
		// flags1 bit 0x400, so it is a movement gate and not an input gate.
		const DWORD PlayerControlEnabled = 0x008496D8;

		// The four direction ramps, ints stepping by 32 and clamped to 256. Shared state,
		// so two players driven through PlayerStepControl would smear into each other
		// even if the mask were per character.
		const DWORD PlayerDirRampUp = 0x00F0078C;
		const DWORD PlayerDirRampDown = 0x00F00790;
		const DWORD PlayerDirRampRight = 0x00F00794;
		const DWORD PlayerDirRampLeft = 0x00F00798;

		// ---------------------------------------------------------------------------
		// The other single-slot input consumers worth knowing about.
		// ---------------------------------------------------------------------------
		// The menu system's one input read and the 0xC0 byte block it fills now live in
		// addresses/MenuSystem.h, which owns that subsystem. Both passes derived the same
		// two addresses independently, so see Rva::MenuSysSamplePad and Rva::MenuPadBlock
		// there, along with the three masks inside the block that have to be transmitted
		// rather than rederived.

		// The atel event VM's per-frame pad snapshot used to be declared here. It now
		// lives in addresses/Atel.h, with the rest of that subsystem and with the two
		// edge masks that turned out to be the ones actually read. Two separate passes
		// over this binary arrived at the same addresses for the held masks, which is a
		// better cross-check than either pass on its own.
		//
		// See Rva::AtelPadPressed, AtelPadReleased, AtelPadPort0Buttons and
		// AtelPadPort1Buttons in addresses/Atel.h.

		// ---------------------------------------------------------------------------
		// PhyreEngine's pad API, for the second physical controller.
		//
		// ffx/Pad.h reads the device bytes directly and that is still the recommended
		// route. These are here so the alternative is documented rather than rediscovered,
		// and because PhyreGetAnalogChannel is the only way to get an UNDEADZONED axis.
		// Rva::Application lives in addresses/Character.h.
		// ---------------------------------------------------------------------------
		const DWORD PhyreIsPadButtonDown = 0x00229CA0;  // __thiscall(app, int semantic, unsigned pad)
		const DWORD PhyreGetPadAxis = 0x00228460;       // __thiscall(app, int axis, unsigned pad) -> float
		const DWORD PhyreGetAnalogChannel = 0x00228AD0; // __thiscall(device, int semantic) -> float

		// ---------------------------------------------------------------------------
		// Every address above, for the startup build check. VerifyLayout walks this so a
		// typo is caught at startup rather than by a fault later.
		//
		// Keep it in step with the constants. If you add an address and forget this list,
		// nothing breaks today and something breaks confusingly in a year.
		// ---------------------------------------------------------------------------
		inline const DWORD* InputRvaList(int* count)
		{
			static const DWORD list[] = {
				InputSingleton,
				InputGet,
				UpdateVtableSlot,
				UpdateExpected,
				InputGetButtonMask,
				InputGetAnalogAxes,
				InputIsHeld,
				InputIsPressed,
				InputIsReleased,
				InputIsRepeating,
				InputMaskIsHeld,
				InputMaskIsPressed,
				InputMaskIsReleased,
				InputMaskIsRepeating,
				InputPoll,
				InputUpdateFromApp,
				InputPollForMenu,
				InputConsumeThreadedSample,
				InputPopThreadedSample,
				ThreadedPadMode,
				InputSetPollOverride,
				InputUpdateHoldTimers,
				InputClearHoldTimers,
				InputGetTimeSeconds,
				InputBuildActionMap,
				GameSettings,
				ConfigGetBindingForAction,
				ConfigGetDefaultBindingForAction,
				DefaultKeyBindings,
				InputKeyCodeToName,
				KeyCodeNames,
				KeyAliasTable,
				ControllerPageTable,
				KeyboardPageTable,
				PadPortState,
				PadGetPortState,
				PadReadButtons16,
				PadReadPressed16,
				PadReadReleased16,
				PadReadAnalogByte,
				PadFillSceReadData,
				PadFillAnalogAndSynthDpad,
				PadUpdateAll,
				PlayerAnalogLY,
				PlayerAnalogLX,
				PlayerAnalogRY,
				PlayerAnalogRX,
				PlayerPadPressed,
				PlayerPadButtons,
				PlayerPadPrev,
				PlayerReadPad,
				PlayerGetPadButtons,
				PlayerStepControl,
				PlayerCamReadPadInput,
				PlayerStickLX,
				PlayerStickLY,
				PlayerDesiredHeading,
				PlayerControlEnabled,
				PlayerDirRampUp,
				PlayerDirRampDown,
				PlayerDirRampRight,
				PlayerDirRampLeft,
				PhyreIsPadButtonDown,
				PhyreGetPadAxis,
				PhyreGetAnalogChannel,
			};
			if (count)
				*count = (int)(sizeof(list) / sizeof(list[0]));
			return list;
		}

	} // namespace Rva
} // namespace ffx
