#pragma once

#include <windows.h>

// FFX's own input layer: one global button mask, four global stick floats.
//
// This is the layer BELOW ffx/Pad.h. Pad.h gives you a second physical gamepad
// out of PhyreEngine's 18 pad slots, which the game never looks at. This header
// gives you what the game itself believes the player is doing, which is the thing
// you have to read, suppress and forge for co-op.
//
// ## The one sentence that matters
//
// FFX merges every physical device, pad and keyboard alike, into a single 32-bit
// mask and four stick floats inside one singleton. Nothing above that point takes
// a port or device argument. The emulated scePad layer does accept a port number
// and then ignores it, so port 1 is a byte-for-byte clone of port 0 and is NOT a
// second controller. Do not go looking for a second player in there.
//
// ## Reading
//
// ButtonMask() is the level state, ButtonsPressed() and ButtonsReleased() are the
// edges the game itself computes, and ReadSticks() gives the four axes. All of
// them are safe to call from anywhere: they only read, and they go through
// workshop::Readable first. They return 0 or false when the singleton is not yet
// readable, which is the case before the game has booted far enough.
//
// ## Injecting
//
// WriteButtonMask and WriteSticks exist and work, but WHERE you call them decides
// whether they do anything. Read the comment on InjectionPoint below before using
// them, because there are two modes in which a naive animate-time write is thrown
// away by the engine a few microseconds later.
//
// Derivation, evidence and the things that stayed unresolved are in
// reversing/INPUT_LAYER.md.

namespace ffx
{

	// ---------------------------------------------------------------------------
	// The button bits.
	//
	// These are PS2 libpad bit positions, but they were NOT taken from the PS2
	// header. Each name below is read out of FFX.exe:
	//
	//   * FFX_Input__buildActionMap 0x6315C0 pairs each remappable action index with
	//     one of these bits.
	//   * g_defaultKeyBindingsFFX 0xC8A538 gives each action's shipped default, and
	//     for the pad actions that default is a PhyreEngine joypad semantic.
	//   * Phyre__PInputDevicePadXInput__poll 0x62DBA0 shows which XInput bit lands in
	//     which semantic slot, so the semantic to physical button identity is read
	//     from code rather than assumed. The face buttons are identified by POSITION,
	//     which is how a Sony name ends up on an Xbox pad: X (left) is square,
	//     A (bottom) is cross, B (right) is circle, Y (top) is triangle.
	//
	// Three further checks pin the directions without reference to any convention:
	//
	//   * FFX_Pad__fillAnalogAndSynthDpad 0x889570 synthesises the dpad from the left
	//     stick byte, and maps far-left to Left, far-right to Right, low Y to Up and
	//     high Y to Down.
	//   * FFX_Pad__fillSceReadData 0x8898A0 suppresses simultaneous opposites in the
	//     pairs (Left, Right) and (Up, Down).
	//   * FFX_Input__readUiNavEvent 0x6EF320 treats Up as interchangeable with
	//     "left stick Y below -0.3", and so on for the other three.
	//
	// What is NOT established here is gameplay meaning. Which bit is "confirm" is a
	// separate question this header does not answer, and the game's own alternate
	// keyboard defaults are ambiguous about it. See INPUT_LAYER.md.
	// ---------------------------------------------------------------------------
	namespace Btn
	{

		// The 16 PS2 bits. These are the only ones that reach gameplay, because the
		// scePad emulation stores the low 16 bits of the mask and drops the rest.
		const DWORD L2 = 0x00000001;
		const DWORD R2 = 0x00000002;
		const DWORD L1 = 0x00000004;
		const DWORD R1 = 0x00000008;
		const DWORD Triangle = 0x00000010;
		const DWORD Circle = 0x00000020;
		const DWORD Cross = 0x00000040;
		const DWORD Square = 0x00000080;
		const DWORD Select = 0x00000100;
		const DWORD Start = 0x00000800;
		const DWORD Up = 0x00001000;
		const DWORD Right = 0x00002000;
		const DWORD Down = 0x00004000;
		const DWORD Left = 0x00008000;

		// L3 and R3 have bits reserved in the PS2 layout and the FFX hold timers cover
		// them, but NO action in the action map binds either one, so the PC poll can
		// never set them. Treat them as permanently clear. FFX_PlayerCam_ReadPadInput
		// still tests R3 to cycle the debug camera distance, which makes that code
		// unreachable in the retail build.
		const DWORD L3 = 0x00000200;
		const DWORD R3 = 0x00000400;

		// Handy groups.
		const DWORD Dpad = Up | Down | Left | Right;
		const DWORD Face = Triangle | Circle | Cross | Square;
		const DWORD Shoulders = L1 | R1 | L2 | R2;
		const DWORD Ps2Mask = 0x0000FFFFu;

		// The PC-only high bits. Real bits of the same mask, invisible to gameplay
		// because the scePad layer truncates to 16 bits. Each is listed with the action
		// index that sets it and that action's default key.
		const DWORD BoosterSpeed = 0x00010000;       // action 28, F1, cycles 1x 2x 4x
		const DWORD BoosterAutoBattle = 0x00020000;  // action 29, F4
		const DWORD PcAction30 = 0x00040000;         // action 30, unbound by default
		const DWORD PcAction31 = 0x00080000;         // action 31, F5
		const DWORD PcAction32 = 0x00100000;         // action 32, unbound by default
		const DWORD BoosterInvincible = 0x00200000;  // action 33, F2
		const DWORD BoosterNoEncounter = 0x00400000; // action 34, F3
		// 0x00800000 is tested once at 0x6F760F and no action ever sets it. Dead.
		const DWORD EscMenu = 0x01000000; // action 40, Esc

		// Shoulder markers. A pad shoulder press sets BOTH its PS2 bit and its marker,
		// for example L1 arrives as 0x08000004. A keyboard shoulder binding sets only the
		// marker. The HD UI uses the markers for paging so it can tell the two apart
		// without colliding with the 16-bit scePad space.
		const DWORD MarkR1 = 0x02000000; // keyboard action 36, Home
		const DWORD MarkR2 = 0x04000000; // keyboard action 37, End
		const DWORD MarkL1 = 0x08000000; // keyboard action 38, A
		const DWORD MarkL2 = 0x10000000; // keyboard action 39, Q

		// Bits 29, 30 and 31 are unused. There are exactly 29 hold timers in the
		// singleton, one per bit 0 through 28, which is independent confirmation that
		// the mask stops at bit 28.
		const DWORD HoldTimerCount = 29;

	} // namespace Btn

	// ---------------------------------------------------------------------------
	// PhyreEngine joypad button semantics, the complete set.
	//
	// Read out of Phyre__PInputDevicePadXInput__poll 0x62DBA0, which stores one byte
	// per semantic at device+920 in this order, from these XInput bits:
	//   Square  <- 0x4000 X          Select <- 0x0020 BACK
	//   Cross   <- 0x1000 A          Start  <- 0x0010 START
	//   Circle  <- 0x2000 B          L3     <- 0x0040 LEFT_THUMB
	//   Triangle<- 0x8000 Y          R3     <- 0x0080 RIGHT_THUMB
	//   L1      <- 0x0100 LB         Up     <- 0x0001 DPAD_UP
	//   R1      <- 0x0200 RB         Right  <- 0x0008 DPAD_RIGHT
	//   L2      <- bLeftTrigger != 0 Down   <- 0x0002 DPAD_DOWN
	//   R2      <- bRightTrigger != 0 Left  <- 0x0004 DPAD_LEFT
	//
	// NOTE the triggers are digital here. Phyre throws the analogue trigger value
	// away, so there is no trigger pressure anywhere in this game on PC.
	//
	// ffx::PadDevice::Button in ffx/Layout.h is the same numbering and carries ten of
	// these sixteen. This is the full set in one place. If the two ever disagree,
	// this one was checked against the XInput poll.
	// ---------------------------------------------------------------------------
	namespace PadSemantic
	{
		const int Square = 11;
		const int Cross = 12;
		const int Circle = 13;
		const int Triangle = 14;
		const int L1 = 15;
		const int R1 = 16;
		const int L2 = 17;
		const int R2 = 18;
		const int Select = 19;
		const int Start = 20;
		const int L3 = 21;
		const int R3 = 22;
		const int Up = 23;
		const int Right = 24;
		const int Down = 25;
		const int Left = 26;
		const int First = 11;
		const int Count = 16;
	} // namespace PadSemantic

	// ---------------------------------------------------------------------------
	// Field offsets inside the FFX_Input singleton at Rva::InputSingleton.
	//
	// The singleton is the object, not a pointer to one. Offsets come from
	// FFX_Input__get's own documented layout plus the four accessors that index it.
	// ---------------------------------------------------------------------------
	namespace InputState
	{

		const DWORD Current = 0x00;    // DWORD, this frame's level mask
		const DWORD Previous = 0x04;   // DWORD, last frame's level mask
		const DWORD HoldTimers = 0x08; // float[29], seconds held, index is the bit number
		const DWORD Timestamp = 0x88;  // float, when the timers were last advanced

		const DWORD AxisLeftX = 0x8C;  // float
		const DWORD AxisLeftY = 0x90;  // float, POSITIVE IS DOWN
		const DWORD AxisRightX = 0x94; // float
		const DWORD AxisRightY = 0x98; // float, POSITIVE IS DOWN

		const DWORD ActionCount = 0x9C;    // int, 75 in the shipped build
		const DWORD ActionCapacity = 0xA0; // int
		const DWORD ActionData = 0xA4;     // {int bindingId, DWORD ps2Mask}[], 8 bytes each

		// Byte. Set to 1 by the pad block of the poll, cleared by the keyboard blocks.
		// What the HD UI uses to decide whether to draw pad glyphs or key names.
		const DWORD LastInputWasPad = 0xA8;

		// The poll override triple. Non-null OverrideFn makes FFX_Input__poll call
		// fn(ctx, arg, &skip) at its very top and abandon the whole poll when skip comes
		// back non-zero. Nothing in the retail binary installs it.
		const DWORD OverrideCtx = 0xAC;
		const DWORD OverrideArg = 0xB0;
		const DWORD OverrideFn = 0xB4;

		// +0x7C through +0x84 sit between the last hold timer and the timestamp and were
		// not examined. Do not assume they are padding.

	} // namespace InputState

	// ---------------------------------------------------------------------------
	// One emulated scePad port. Two of them exist and they carry identical data.
	//
	// Reachable as Rva::PadPortState + Stride * (slot + port). Yes, added: the game
	// indexes one flat array by slot plus port, and only two ports ever exist, so
	// (port 1, slot 0) and (port 0, slot 1) are the same 256 bytes.
	//
	// The four ring slots sit at the FRONT of the port state, offsets 0 to 127, and
	// FFX_Pad__getRingSlot 0x888B60 is
	//     portState + 32 * ((ringArg + portState[156]) & 3)
	// so the ring argument that FFX_Pad__readButtons16 and friends take is RELATIVE
	// to the current write index, not absolute. 0 is this frame, -1 is last frame.
	// FFX_Pad__commitRingSlot advances the index and fills the new slot.
	// ---------------------------------------------------------------------------
	namespace PortState
	{

		const DWORD Stride = 256;
		const DWORD RingSlots = 4;
		const DWORD RingStride = 32;
		const DWORD RingBase = 0x00;

		// Inside a ring slot, which is what FFX_Pad__readButtons16 and friends index.
		const DWORD RingButtons = 0x04;  // short, the level mask, low 16 PS2 bits
		const DWORD RingPressed = 0x06;  // short, rising edges
		const DWORD RingReleased = 0x08; // short, falling edges
		const DWORD RingAnalog = 0x0C;   // BYTE[4] in the order RX, RY, LX, LY

		// Inside the port state itself.
		const DWORD Status = 0x80;
		const DWORD Mode = 0x82;
		const DWORD StagingRX = 0x84; // BYTE, 0x80 is centre
		const DWORD StagingRY = 0x85;
		const DWORD StagingLX = 0x86;
		const DWORD StagingLY = 0x87;
		const DWORD CurrentMask = 0x98; // short, written twice at 0x98 and 0x9A
		const DWORD CurrentMask2 = 0x9A;
		const DWORD RingWriteIdx = 0x9C;

	} // namespace PortState

	// ---------------------------------------------------------------------------
	// The four stick axes, in the engine's own convention.
	//
	// Range is about -1.004 to 1.0. Phyre computes value = raw / 255.0 where raw is
	// the XInput thumb divided by 128, and DirectInput devices have DIPROP_RANGE set
	// to -255..255 so the two backends agree.
	//
	// Y IS POSITIVE DOWN. That is not a mistake and it matches PadState in
	// ffx/Pad.h. Negative leftY means the player is pushing up.
	//
	// A 0.2 deadzone has already been applied per axis by
	// Phyre__PInputMapper__getPadAxis 0x628470, with a cross-axis relaxation: if the
	// other axis of the same stick is outside its own deadzone, this axis gets a
	// deadzone of zero. So a diagonal push reports small values honestly while a
	// pure single-axis nudge is snapped to zero. If you need the undeadzoned value,
	// go to the device through Rva::PhyreGetAnalogChannel or read the raw ints at
	// PadDevice::AxisLeftX and friends.
	// ---------------------------------------------------------------------------
	struct Sticks
	{
		float leftX, leftY, rightX, rightY;
	};

	// ---------------------------------------------------------------------------
	// Reading. All of these are read only and go through workshop::Readable, so they
	// are safe to call from any thread at any time, including before the game has
	// built its input object.
	// ---------------------------------------------------------------------------

	// The singleton, or null when it is not readable yet.
	BYTE* InputSingletonPtr();

	// This frame's level mask, and last frame's. Zero when unreadable.
	DWORD ButtonMask();
	DWORD PreviousButtonMask();

	// The edges the game itself works from. Pressed is cur & ~prev, Released is
	// ~cur & prev, exactly as FFX_Input__maskIsPressed and maskIsReleased compute
	// them.
	DWORD ButtonsPressed();
	DWORD ButtonsReleased();

	inline bool Held(DWORD mask)
	{
		return (ButtonMask() & mask) != 0;
	}
	inline bool Pressed(DWORD mask)
	{
		return (ButtonsPressed() & mask) != 0;
	}
	inline bool Released(DWORD mask)
	{
		return (ButtonsReleased() & mask) != 0;
	}

	// Fills out and returns true. out is zeroed either way.
	bool ReadSticks(Sticks* out);

	// How long a bit has been held, in seconds. bitIndex is 0 to 28. Returns 0 for
	// an out-of-range index or an unreadable singleton.
	//
	// The engine's auto-repeat, which menus use, fires on the rising edge and then
	// whenever a timer passes 0.2333 s, after which it resets that timer to 0.1333 s.
	float HoldSeconds(int bitIndex);

	// True when the last input the game saw came from the pad rather than the
	// keyboard. This is the game's own flag at InputState::LastInputWasPad.
	bool LastInputWasPad();

	// Non-zero means the mask is latched INSIDE animate rather than before it. Worth
	// logging once at startup, because it changes where injection has to happen.
	DWORD ThreadedPadMode();

	// ---------------------------------------------------------------------------
	// The player-input globals, one step further down the chain.
	//
	// FFX_Player__readPad copies emulated port 0 into these once per frame and
	// FFX_Player__stepControl turns them into movement. If you want to know what the
	// game decided the player is doing, after the scePad round trip and after the
	// dpad synthesis, this is it.
	// ---------------------------------------------------------------------------

	// Low 16 bits are the PS2 mask. Zero when unreadable.
	DWORD PlayerButtons();
	DWORD PlayerButtonsPressed();

	// The player stick bytes converted to -1..1 with the same sign convention as
	// Sticks, so leftY positive is down. The engine stores them as unsigned bytes
	// centred on 0x80. Returns false when unreadable.
	bool ReadPlayerSticks(Sticks* out);

	// ---------------------------------------------------------------------------
	// Names, for logs.
	//
	// Returns a static buffer, so one call per log line. Lists every set bit using
	// the names above, "none" for zero, and "bitNN" for a bit with no name, which
	// should never happen but is better than silence if it does.
	// ---------------------------------------------------------------------------
	const char* ButtonNames(DWORD mask);

	// One name for one semantic, or "?" if out of range. Static string, safe to keep.
	const char* PadSemanticName(int semantic);

	// Mask, edges, sticks, the threaded-mode flag and the player globals, in a few
	// lines. Cheap enough to call once a second from a frame hook while debugging.
	void LogInputState();

	// ---------------------------------------------------------------------------
	// INJECTION. Read this before calling the two writers below.
	//
	// ## Where in the frame the mask is filled
	//
	// Phyre__PApplication__frameTick 0x627940 runs, in this order:
	//
	//   1. Phyre__PInputMapper__latchDeviceStates, which latches all 18 Phyre pad
	//      slots. This is where the physical hardware state becomes visible.
	//   2. vtable +0x1C, FFXApplication__update, which calls FFX_Input__poll and
	//      fills the mask, the edges and the four axes. ONLY when ThreadedPadMode
	//      is 0.
	//   3. vtable +0x10, FFXApplication__animate, which runs the whole game step:
	//      the scePad emulation, FFX_Player__readPad, the menu system, the event VM,
	//      battle. Everything that READS input runs here.
	//   4. render, endFrame.
	//
	// So the Workshop's existing animate hook, the one in ffx/AnimateHook.h, sits in
	// exactly the right place: writing the mask before calling the original animate
	// lands after the latch and before every reader. That is the answer to "where do
	// I inject".
	//
	// ## The two ways that goes wrong
	//
	//   * ThreadedPadMode is non-zero. Then FFX_Input__poll runs on a separate 60 Hz
	//     thread and pushes samples into a queue, and FFX_Input__consumeThreadedSample
	//     pops one into the singleton at the TOP of FFX_MainStepLoop, which is INSIDE
	//     animate and runs once per catch-up substep. A write made before animate is
	//     overwritten before anything reads it. Check ThreadedPadMode() and say so
	//     loudly rather than wondering why injection silently does nothing.
	//
	//   * The esc menu is open. Then update skips the poll entirely and animate calls
	//     FFX_Input__pollForMenu first, which re-polls. Same outcome.
	//
	// ## The hook that is immune to both
	//
	// Both paths go through FFX_Input__poll, and the game ships a supported way to
	// take it over: the override callback at InputState::OverrideFn, installed with
	// Rva::InputSetPollOverride. While installed, poll calls fn(ctx, arg, &skip) at
	// its very top and, when skip comes back non-zero, abandons the entire poll
	// without touching the mask, the edges or the axes. Nothing in the retail binary
	// installs it, so it is free. A mod that owns the poll then owns both the level
	// state and the edges, which it has to maintain itself, because with the poll
	// skipped the engine stops doing prev = cur for you.
	//
	// ## Level alone is not enough
	//
	// The game keeps a previous-frame mask and derives every edge from it. Writing
	// only the current mask leaves prev holding whatever the local pad did, so a
	// remote button that was already down reads as a fresh press, and one that was
	// already up reads as nothing. There are THREE edge caches, all of which have to
	// stay consistent:
	//
	//   1. InputState::Previous, used by FFX_Input__isPressed and isReleased.
	//   2. The scePad ring slots, PortState::RingPressed and RingReleased, which the
	//      menu system, the event VM and the battle code read.
	//   3. Rva::PlayerPadPressed and PlayerPadPrev, which field movement and the
	//      camera read. Note FFX_Player__getPadButtons CLEARS PlayerPadPressed as a
	//      side effect, so the first caller in a frame consumes the edge for
	//      everybody.
	//
	// Caches 2 and 3 are derived from cache 1 every frame, by code that runs inside
	// animate. So writing cache 1 before animate is enough to drive all three. That
	// is the practical reason the pre-animate slot is the right place.
	// ---------------------------------------------------------------------------

	// A named constant for the above, so a plugin can say what it depends on.
	enum InjectionPoint
	{
		// Write the mask from a detour on FFXApplication vtable +0x10 BEFORE calling
		// the original. Correct whenever ThreadedPadMode() is 0 and the esc menu is
		// closed.
		InjectBeforeAnimate = 0,

		// Take over FFX_Input__poll with the engine's own override callback. Correct
		// in every mode, at the cost of having to maintain prev yourself.
		InjectViaPollOverride = 1
	};

	// Writes the level mask, and sets the previous mask to prevMask so the edges come
	// out right. Pass the remote player's last-frame mask as prevMask, not the local
	// one. Returns false and writes nothing when the singleton is not readable.
	//
	// This is a plain store. It does not stop the engine writing the same field later
	// in the frame, which is what the comment above is about.
	bool WriteButtonMask(DWORD mask, DWORD prevMask);

	// Writes the four axes. Same convention as Sticks, so leftY positive is down.
	// Values are stored as given and are not clamped, because the engine does not
	// clamp them either, but anything outside -1..1 will produce stick bytes outside
	// 0x01..0xFF once FFX_Pad__axisFloatToByte truncates them.
	bool WriteSticks(const Sticks& sticks);

	// Clears the mask and centres the sticks, which is how you suppress a player who
	// does not have control. Same frame-slot caveats as WriteButtonMask.
	bool SuppressInput();

} // namespace ffx
