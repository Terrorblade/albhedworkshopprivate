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
		const DWORD PadReadStagingAnalogByte = 0x00488C00; // char (port, slot, axis)

		// The pad ring entry is 32 bytes. +2/+4 held, +6 pressed, +8 released are all
		// pure button math and deterministic. +10 and +28 are WALL CLOCK auto repeat
		// masks, so avoid them: eight ATEL syscalls expose them to script
		// (core:69/73/79/83 and core:591..594) and only core:593 is used by shipped
		// data, in hiku2100.
		const DWORD PadReadWord10 = 0x00488E50; // short (port, slot, ring), auto repeat
		const DWORD PadReadWord28 = 0x00488E30; // short (port, slot, ring), auto repeat

		const DWORD PadCommitRingSlot = 0x00489790;          // short (port, slot)
		const DWORD PadStepAutoRepeatAllGroups = 0x00489940; // groups 0..3, ORs the masks
		const DWORD PadStepAutoRepeatMask = 0x00489980;      // one group, reads the clock
		// The clock call inside PadStepAutoRepeatMask. Patch this, not the clock.
		const DWORD PadAutoRepeatClockCallSite = 0x00489989;

		const DWORD PadFillSceReadData = 0x004898A0;        // int (int port, int slot, int state, int buf)
		const DWORD PadFillAnalogAndSynthDpad = 0x00489570; // char (int state, int buf, short mask)
		const DWORD PadUpdateAll = 0x00489A80;              // called from FFX_MainStep, inside animate

		// ---------------------------------------------------------------------------
		// THE PAD RING, AND THE CLEANEST INJECTION POINT IN THE GAME.
		//
		// A 256 byte port state is a FOUR ENTRY RING of 32 byte samples starting at
		// offset 0, plus a STAGING area at +0x80. PadCommitAllPorts advances the cursor at
		// +0x9C and calls PadCommitRingSlot once per port, and PadCommitRingSlot builds the
		// new entry out of the staging bytes plus the previous entry:
		//
		//   ring +0x00 / +0x01   status and mode, from staging +0x80 and +0x81
		//   ring +0x02 / +0x04   the held mask, both copied from staging +0x98
		//   ring +0x06           pressed  = held & (held ^ prevHeld)
		//   ring +0x08           released = prevHeld & (held ^ prevHeld)
		//   ring +0x0C..+0x1B    16 analog bytes, copied straight from staging +0x84
		//   ring +0x0A / +0x1C   the auto repeat masks. WALL CLOCK, the only two
		//   ring +0x1E           staging +0x9A carried through
		//
		// SO: WRITE PadStagingHeldOff AND THE 16 BYTES AT PadStagingAnalogOff, THEN LET THE
		// ENGINE DERIVE THE REST. Pressed, released and the analog copy are pure integer
		// math over the previous entry, so two peers fed the same staging bytes for the
		// same number of commits produce bit identical +0x02, +0x04, +0x06 and +0x08.
		//
		// That is cleaner than writing ring entries by hand and much cleaner than the menu
		// globals, because everything above the ring already reads through the two port API
		// and needs no change at all.
		//
		// THE CADENCE IS THE ONE THING TO GET RIGHT. PadCommitAllPorts has exactly three
		// callers. The MainStep and StepPacing ones are the two arms of a single if/else at
		// PadCommitAllPortsBranchSite, so between them it runs ONCE PER FRAME, and before
		// the sub step loop that starts at RVA 0x00420FFA. The third is inside BtlMenuStep,
		// gated on ThreadedPadMode, and that one runs once per SUB STEP. In the overdrive
		// family only Lulu's Fury sets that flag.
		//
		// So the ring normally advances once per frame and once per sub step during Lulu's
		// Fury, and an injector has to advance it the same number of times on both peers,
		// because the overdrive minigames read lag -1 as well as lag 0.
		// ---------------------------------------------------------------------------
		const DWORD PadCommitAllPorts = 0x004893E0; // int (void), loops ports 0 and 1
		const DWORD PadGetRingSlot = 0x00488B60;    // char *(char *portState, char lag)
		const DWORD PadGetReadBuffer = 0x00488B80;  // (int port, int slot, int lag)
		const DWORD PadGetPortBuffer = 0x00488E10;  // (int port, int slot)
		const DWORD PadClearRingSlot = 0x00488970;  // zeroes one entry
		
		// FFX_Pad__getRingLagWindow 0x00487DC0 is declared in addresses/MagicDll.h,
		// because it exists only to answer magic host API slot 305 and the exe never
		// calls it. It returns a constant 1, and a DLL uses it as lag = 1 - ret, so a
		// magic DLL's pad reads only ever see the CURRENT ring slot.

		// PadGetRingSlot takes the PORT STATE POINTER, not a port index, and returns
		// portState + 32 * ((lag + portState[0x9C]) & 3). lag 0 is the sample just
		// committed, lag -1 the one before it.
		const int PadPortStateBytes = 256;
		const int PadRingEntryBytes = 32;
		const int PadRingEntries = 4;

		// Staging offsets inside a port state. The two marked INJECT are the whole point.
		const int PadStagingStatusOff = 0x80;
		const int PadStagingModeOff = 0x81;      // 1 builds a fresh entry, 2 copies the last
		const int PadStagingStatus2Off = 0x82;
		const int PadStagingAnalogOff = 0x84;    // INJECT. 16 bytes
		const int PadStagingHeldOff = 0x98;      // INJECT. the held button word
		const int PadStagingHeld2Off = 0x9A;     // feeds the ring +0x1C auto repeat
		const int PadRingCursorOff = 0x9C;       // advanced by PadCommitAllPorts
		const int PadStagingClearFlagOff = 0x9E; // zero makes the commit clear the new entry

		// Ring entry offsets. +0x0A and +0x1C are the ONLY wall-clock derived words in an
		// entry. Everything else is pure math, so hash or replicate the rest freely.
		const int PadRingStatusOff = 0x00;
		const int PadRingModeOff = 0x01;
		const int PadRingHeldOff = 0x02;
		const int PadRingHeld2Off = 0x04;   // what PadReadButtons16 returns
		const int PadRingPressedOff = 0x06;
		const int PadRingReleasedOff = 0x08;
		const int PadRingRepeatAOff = 0x0A; // WALL CLOCK
		const int PadRingAnalogOff = 0x0C;  // 16 bytes
		const int PadRingRepeatBOff = 0x1C; // WALL CLOCK
		const int PadRingHeld2CarryOff = 0x1E;

		// Commit internals, for a patcher that wants to verify before it writes.
		const DWORD PadRingCursorAdvanceSite = 0x004897CA;    // mov [esi+9Ch], cl
		const DWORD PadCommitEdgeMathSite = 0x00489846;       // mov [ebx+6], ax
		const DWORD PadCommitAutoRepeatCallSite = 0x00489867; // the only clock in a commit

		// The three callers, and the if/else that picks between the first two.
		const DWORD PadCommitAllPortsBranchSite = 0x00420B90;      // jnz, inside MainStep
		const DWORD PadCommitAllPortsCallSiteMainStep = 0x00420BA3;
		const DWORD PadCommitAllPortsCallSitePacing = 0x0042221F;
		const DWORD PadCommitAllPortsCallSiteBtlMenu = 0x0049AE65; // once per SUB step

		// The button remap layer, which is identity on this build. Listed so nobody chases
		// it: PadMapButtonCode returns 0..3 unchanged, PadRemapButtonMask is an identity
		// lookup through PadButtonRemapTable, and the table's only writer is PadResetPort
		// writing the identity back.
		const DWORD PadMapButtonCode = 0x00488CF0;
		const DWORD PadRemapButtonMask = 0x00488C40;
		const DWORD PadButtonRemapTable = 0x00F30488;
		const DWORD PadSetButtonRemapEntry = 0x004894F0;
		const DWORD PadResetPort = 0x00489360;

		// The function older notes meant when they said 0x00230C40 was the input clock. It
		// is not, InputGetTimeSeconds above is. This one clears the threaded sample queues.
		const DWORD InputClearThreadedSampleQueues = 0x00230C40;
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

		// THE ONLY CALL TO IT IN THE BINARY, inside FFX_MainStep, once per substep. The
		// five bytes there are E8 6B C1 00 00.
		//
		// This is the seam for driving more than one character. Patch this one call to a
		// function that loops, and every player character gets the engine's own driver at
		// exactly the point in the step the engine wanted it, on the engine's own substep
		// cadence. Patching the site rather than detouring the function is also the only
		// option that works here, see workshop::PatchCallSite for why.
		const DWORD PlayerStepControlCallSite = 0x00421010;

		// THE CAMERA READS INSIDE THE PLAYER DRIVER, all four of them.
		//
		// Every one is "call j_FFX_Came_GetYaw" immediately followed by "fstp
		// g_ffxPlayerCamYaw", and that store is the only way the live camera gets into the
		// driver at all. Patch these four and the whole function becomes a pure fuction of
		// the 0x30 state block, the pad globals and a handful of shared mode flags, which
		// is what makes it safe to run per character on two machines.
		//
		// The thunk is a bare "jmp FFX_Came_GetYaw" at 0x43F2D0. FFX_Came_GetYaw itself is
		// __fastcall returning a double in ST(0), takes one ecx argument that it writes
		// through and then reads back, and pushes NOTHING on the stack. So a replacement
		// declared "double __cdecl f(void)" is stack compatible with it.
		const DWORD PlayerCameGetYawThunk = 0x0043F2D0;
		const DWORD PlayerCameGetYaw = 0x003BCF20;
		const DWORD PlayerDriverYawSite0 = 0x0042D6A9; // re-anchor on a >20 degree turn
		const DWORD PlayerDriverYawSite1 = 0x0042D6E0; // gfx state 412
		const DWORD PlayerDriverYawSite2 = 0x0042D6ED; // gfx state 544
		const DWORD PlayerDriverYawSite3 = 0x0042D720; // the non-mode-3 path, every step

		// BYTE, and it sits three bytes below PlayerControlEnabled rather than in the
		// player state block. 3 selects the anchored camera-relative path in the driver,
		// anything else takes the branch that refreshes the anchor every step and clears
		// the heading offset. Script state, so it is the same on both machines.
		const DWORD PlayerCtrlMode = 0x008496D5;
		const DWORD PlayerCamReadPadInput = 0x0043F4B0; // right stick to camera

		// Post-deadzone stick values PlayerStepControl derives, ints centred on 128.
		const DWORD PlayerStickLX = 0x00F007A8;
		const DWORD PlayerStickLY = 0x00F007AC;

		// Float radians, the heading PlayerStepControl asks the character to face.
		const DWORD PlayerDesiredHeading = 0x00F007A0;

		// Dword. PlayerStepControl forces move speed to 0 when this is 0, but still sets
		// flags1 bit 0x400, so it is a movement gate and not an input gate.
		const DWORD PlayerControlEnabled = 0x008496D8;

		// The four direction ramps, ints stepping by 32 and clamped to 0..256. Shared
		// state, so two players driven through PlayerStepControl would smear into each
		// other even if the mask were per character.
		//
		// They are also the movement SMOOTHING, and they are pure integer arithmetic with
		// no clock anywhere in them, so they replicate exactly as long as each character
		// gets its own copy. That is the whole reason the swap below is worth doing rather
		// than reimplementing the driver.
		const DWORD PlayerDirRampUp = 0x00F0078C;
		const DWORD PlayerDirRampDown = 0x00F00790;
		const DWORD PlayerDirRampRight = 0x00F00794;
		const DWORD PlayerDirRampLeft = 0x00F00798;

		// ---------------------------------------------------------------------------
		// THE REST OF THE PER-PLAYER STATE, which completes the set.
		//
		// Everything PlayerStepControl both reads and writes lives in one contiguous
		// block, RVA 0x00F00780 through 0x00F007AF, and these are the entries that were
		// missing from the list above. Driving a second character through the engine's own
		// driver means saving this block, writing that character's copy in, calling, and
		// saving the result back out. See the function comment on PlayerStepControl in the
		// IDB for the full derivation and for why transmitting the OUTPUT instead does not
		// work under delayed input.
		//
		// PlayerStateBlock and PlayerStateBlockBytes name the span so a swap can memcpy it
		// rather than naming ten fields and getting one wrong later.
		// ---------------------------------------------------------------------------
		const DWORD PlayerStateBlock = 0x00F00780;
		const DWORD PlayerStateBlockBytes = 0x30;

		// Float radians. Subtracted from the desired heading before the camera yaw is
		// applied, and reset to 0 by the branches that re-anchor off the live camera.
		const DWORD PlayerHeadingOffset = 0x00F00780;

		// Float radians, the engine's cached camera yaw. Written live from
		// FFX_Came_GetYaw in four separate branches of PlayerStepControl, which is what
		// makes the camera the one genuine obstacle to driving a remote character here:
		// the camera is per machine.
		const DWORD PlayerCamYaw = 0x00F0079C;

		// Float radians, last step's desired heading. Drives the re-anchor test, which
		// fires when the heading moves more than 0.34906578 radians, about 20 degrees.
		const DWORD PlayerPrevHeading = 0x00F007A4;

		// THE WAY PAST THE CAMERA, and the engine ships it. The final line of
		// PlayerStepControl is
		//     dir = -((DesiredHeading - HeadingOffset) - (UseFixedYaw ? FixedYaw : CamYaw))
		// so setting the pair replaces the camera in the part that reaches the character.
		//
		// IT DOES NOT replace the camera in the earlier branches that re-anchor
		// HeadingOffset and PrevHeading. A replicator that wants a result independent of
		// the local camera has to supply the owner's yaw as FixedYaw AND force
		// HeadingOffset to 0, which collapses the line to dir = -(DesiredHeading - yaw).
		const DWORD PlayerUseFixedYaw = 0x00F007C4; // BYTE, non-zero selects FixedYaw
		const DWORD PlayerFixedYaw = 0x00F007C8;    // float radians

		// void __cdecl (float degrees, bool enable). The engine's own setter for the pair
		// above, so the fixed-yaw mode does not have to be poked directly.
		// WAS 0x42D9A0, which is 0x10 INTO this function rather than at it. Nothing
		// caught it because nothing called it yet. Takes (float useFixed, float degrees),
		// both as floats even though the first is really a boolean, and writes
		// PlayerUseFixedYaw and PlayerFixedYaw.
		const DWORD PlayerSetFixedYawDeg = 0x0042D990;

		// The two functions that write the driver's camera anchor from OUTSIDE the driver.
		// Each has exactly one caller and both callers are ATEL script opcodes, library 0
		// functions 63 and 64, so this is script re-anchoring the player's movement frame.
		//
		// syncCamYaw latches the camera into PlayerCamYaw and zeroes PlayerHeadingOffset.
		// setHeadingOffsetDeg latches the camera and sets the offset to
		// wrap(deg * pi / 180) + PlayerDesiredHeading.
		//
		// Both write the LIVE block, which under a per-character swap is scratch space, so
		// anything replicating the driver has to notice the write and push it into every
		// character's own copy. world/PlayerDrive.cpp does that by watching the two fields.
		const DWORD PlayerSyncCamYaw = 0x0042D930;
		const DWORD PlayerSetHeadingOffsetDeg = 0x0042D950;

		// Shared speed config, the same for every character, so a swap leaves it alone.
		// WalkSpeed is a TWO entry table indexed by ((buttons & 0x40) == 0), so entry 1 is
		// walk and entry 0 is run, and the final speed is
		//     (locomotion == 2 ? SwimSpeed : WalkSpeed[idx]) * rampMagnitude * (1/256)
		const DWORD PlayerWalkSpeed = 0x00F007B4; // float[2]
		const DWORD PlayerSwimSpeed = 0x00F007BC; // float, used when locomotion mode is 2

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
				PadReadStagingAnalogByte,
				PadReadWord10,
				PadReadWord28,
				PadCommitRingSlot,
				PadStepAutoRepeatAllGroups,
				PadStepAutoRepeatMask,
				PadAutoRepeatClockCallSite,
				PadFillSceReadData,
				PadFillAnalogAndSynthDpad,
				PadUpdateAll,
				PadCommitAllPorts,
				PadGetRingSlot,
				PadGetReadBuffer,
				PadGetPortBuffer,
				PadClearRingSlot,
				PadRingCursorAdvanceSite,
				PadCommitEdgeMathSite,
				PadCommitAutoRepeatCallSite,
				PadCommitAllPortsBranchSite,
				PadCommitAllPortsCallSiteMainStep,
				PadCommitAllPortsCallSitePacing,
				PadCommitAllPortsCallSiteBtlMenu,
				PadMapButtonCode,
				PadRemapButtonMask,
				PadButtonRemapTable,
				PadSetButtonRemapEntry,
				PadResetPort,
				InputClearThreadedSampleQueues,
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
				PlayerStepControlCallSite,
				PlayerCameGetYawThunk,
				PlayerCameGetYaw,
				PlayerDriverYawSite0,
				PlayerDriverYawSite1,
				PlayerDriverYawSite2,
				PlayerDriverYawSite3,
				PlayerCtrlMode,
				PlayerSyncCamYaw,
				PlayerSetHeadingOffsetDeg,
				PlayerCamReadPadInput,
				PlayerStickLX,
				PlayerStickLY,
				PlayerDesiredHeading,
				PlayerControlEnabled,
				PlayerDirRampUp,
				PlayerDirRampDown,
				PlayerDirRampRight,
				PlayerDirRampLeft,
				PlayerStateBlock,
				PlayerHeadingOffset,
				PlayerCamYaw,
				PlayerPrevHeading,
				PlayerUseFixedYaw,
				PlayerFixedYaw,
				PlayerSetFixedYawDeg,
				PlayerWalkSpeed,
				PlayerSwimSpeed,
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
