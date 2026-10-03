#pragma once

#include <windows.h>

// The in-game menu: party, system, equipment, abilities, items and the Sphere
// Grid. The native PS2-era menu, not the PC options overlay.
//
// What a co-op plugin needs from this area is four things: is the menu up and
// which screen, what buttons did the menu see this frame, how do I make it see
// somebody else's buttons, and which character is the cursor on. That is the
// whole file.
//
// Every raw pointer read goes through workshop::Readable and returns false
// rather than faulting, because all of it is meant to be callable from the frame
// path. The pad block, the module table and the character cursor are statics, so
// those reads effectively never fail. The menu work block and the page table are
// heap pointers and genuinely can be null before the menu has built itself.
//
// ===========================================================================
// THE CO-OP MODEL, AND WHETHER THE CODE SUPPORTS IT
// ===========================================================================
//
// The plan is "replicate input, not state": both machines run the same menu code
// and receive the same button masks. The code supports that, with three caveats
// that are all in this header.
//
// What makes it work:
//
//   * ONE INPUT SURFACE. FFX_MenuSys_SamplePad fills a single 192-byte global
//     once per frame, and every screen reads only that. There is no port
//     argument anywhere. Overwrite the block after the original sample runs and
//     every screen in the same frame sees the replicated buttons.
//   * NO RNG ON THE DECISION PATH. A call-graph walk from MenuSysStepFrame plus
//     all 16 menu module entry points reaches no RNG function at all, except
//     through the Config screen's environment reload, which is an asset load and
//     not a menu decision. See NoRandomOnMenuPath in MENU_SYSTEM.md.
//   * NO STATE SCRIBBLING. Nothing in the menu writes the save block by address.
//     Every commit goes through a named function, about ten of them, listed in
//     addresses\MenuSystem.h. So a divergence has a place to be caught.
//
// Caveat one, THE REPEAT MASK IS WALL-CLOCK DERIVED, SO DERIVE IT FROM A STEP
// COUNT INSTEAD. FFX_MenuSys_SamplePad reads Phyre's process clock to run the 16
// auto-repeat timers, and that clock is per-process uptime, so two machines are
// not even in the same epoch.
//
// An earlier version of this header said to transmit the mask. That was wrong, and
// the reason is worth knowing: there is no stable shipped behaviour to be faithful
// to. The threshold and the reset value are exact 30 fps frame counts and the game
// runs at 29.97, so under ordinary frame jitter the shipped interval flutters
// between 5 and 6 steps on a single machine. Transmitting it would faithfully
// replicate noise, and it would turn every repeat tick of one held direction into
// its own latency-sensitive wire event.
//
// The supported approach is to derive the mask on both machines from the
// replicated HELD mask and the simulation step counter: fire on the press, then
// 14 steps, then every 5. MenuPadFrame still carries a repeat field and
// WriteMenuPad still writes it, because something has to go in the block. See
// MenuPadBlock::RepeatIntervalSeconds for where the numbers come from, and
// reversing\MENU_SYNC.md for the derivation and the worked margins.
//
// Caveat two, THE STICKY COPY AT +0x18. The Sphere Grid does not use the normal
// held mask. It reads the sticky copy and runs its own frame-counted repeat.
// A replicator that writes only the main masks works everywhere except on the
// grid, which is the screen the requirement cares most about. WriteMenuPad
// writes both.
//
// Caveat three, OPENING IS NOT PART OF THE INPUT BLOCK. The open-menu Triangle
// test happens in FFX_MainStep, reading the field player's pad mask, before the
// menu system exists. So the open has to be replicated separately, and
// RequestMenuOpen below is how the passenger's machine is told to open.
//
// ===========================================================================
// WHERE TO HOOK, AND WHY THERE
// ===========================================================================
//
// HookMenuSamplePad installs a detour on FFX_MenuSys_SamplePad. The callback
// runs AFTER the original, which matters: let the game fill the block from the
// local pad, then overwrite it. Filling it yourself and skipping the original
// also works but means reimplementing the stick-to-dpad synthesis and the hold
// counters, for no benefit.
//
//   static void __cdecl OnMenuPad(void)
//   {
//       ffx::MenuPadFrame f;
//       if (MyRoleIsDriver()) {
//           ffx::ReadMenuPad(&f);       // what the local pad produced
//           MySendToPeer(&f);
//       } else if (MyPeerFrameReady(&f)) {
//           ffx::WriteMenuPad(&f);      // what the driver produced
//       } else {
//           ffx::ClearMenuPad();        // no input rather than wrong input
//       }
//   }
//
//   ffx::HookMenuSamplePad(OnMenuPad);
//
// The driver still has to transmit, because the passenger must not act on its
// own buttons at all. ClearMenuPad on a dropped frame is the right default: the
// game itself calls the same wipe from FFX_Module_Stop, so a fully zeroed block
// is a state the shipped code produces.
//
// ===========================================================================
// THE OTHER INJECTION CHANNEL, AND WHEN TO PREFER IT
// ===========================================================================
//
// The game ships a script-driven fake controller for the menu, used by cutscene
// and tutorial ATEL scripts. Enable it and FFX_MenuSys_SamplePad takes its
// buttons from your mask instead of the pad, with no detour needed at all.
//
//   ffx::SetVirtualPadEnabled(true);
//   ffx::SetVirtualPadButtons(mask);   // EVERY frame. It self-clears.
//
// In that mode held, pressed and repeat all come back as the same mask, so a
// one-frame mask is one clean press and there are no edge or timer semantics to
// get wrong. That is genuinely nicer than overwriting the block.
//
// The reason it is not the default recommendation: it is global and
// all-or-nothing. While it is on, the real controller cannot reach the menu on
// EITHER machine, so the driver would have to route its own input through it
// too. Use it for "the passenger watches a scripted sequence", prefer the pad
// hook for "the driver plays and the passenger mirrors".
//
// The derivation and the evidence are in reversing\MENU_SYSTEM.md.

namespace ffx
{

	// ---------------------------------------------------------------------------
	// Offsets. Not addresses, so they live here rather than in
	// addresses\MenuSystem.h.
	// ---------------------------------------------------------------------------

	// The 192-byte input block. Offsets from Rva::MenuPadBlock.
	//
	// Hold counters and hold timers are the auto-repeat bookkeeping. They are state,
	// not input, and WriteMenuPad deliberately leaves them alone: the repeat mask
	// they produce is replicated directly, so recreating the counters that would
	// have produced it is work for nothing. The passenger's counters will drift from
	// the driver's and it does not matter, because nothing but the repeat
	// calculation reads them.
	namespace MenuPadBlock
	{
		const DWORD Size = 0xC0;

		const DWORD HoldCount = 0x02;      // BYTE[16], frames held, index = bit no
		const DWORD Held = 0x12;           // WORD
		const DWORD Pressed = 0x14;        // WORD, the newly-pressed edge
		const DWORD Word10 = 0x16;         // WORD, the pad layer's own repeat
		const DWORD HeldSticky = 0x18;     // WORD, held OR previous ring frame
		const DWORD PressedSticky = 0x1A;  // WORD, pressed OR previous ring frame
		const DWORD Word10Sticky = 0x1C;   // WORD, word10 OR previous ring frame
		const DWORD AnalogX = 0x1E;        // BYTE, left stick X, 0x80 centred
		const DWORD AnalogY = 0x1F;        // BYTE, left stick Y, 0x80 centred
		const DWORD AnalogDword = 0x1E;    // DWORD, the whole analog word
		const DWORD SynthHeld = 0x22;      // WORD, held + dpad from the stick
		const DWORD SynthPressed = 0x24;   // WORD, the edge of SynthHeld
		const DWORD Repeat = 0x26;         // WORD, WALL-CLOCK DERIVED
		const DWORD HoldTimer = 0x40;      // float[16], seconds held
		const DWORD LastSampleTime = 0x80; // float[16], last sample timestamp

		// What the game writes when it centres the sticks.
		const DWORD AnalogCentred = 0x80808080u;
		const BYTE AnalogCentre = 0x80;

		// The stick-to-dpad thresholds FFX_MenuSys_SamplePad uses.
		const BYTE StickLowThreshold = 0x18;
		const BYTE StickHighThreshold = 0xE8;

		// The auto-repeat constants. DERIVING the mask from a step count is the
		// supported approach, see the long note in the header, and these are the
		// numbers that derivation comes from.
		//
		// FirstRepeatSeconds is a THRESHOLD: FFX_MenuSys_SamplePad accumulates real
		// elapsed seconds per button and fires the repeat when the accumulator passes
		// it.
		//
		// LaterRepeatSeconds is NOT the later interval, whatever its name suggests. It
		// is the value the accumulator is RESET TO when a repeat fires. The comparison
		// is still against FirstRepeatSeconds, so the gap between repeats is the
		// difference between the two, which is why RepeatIntervalSeconds below is a
		// sixth of a second and not three tenths.
		//
		// Both of these are exact PS2 frame counts at a flat 30 fps, 14/30 and 9/30, and
		// the port turned the first into a threshold and the second into a seed. That
		// silently changed the shipped interval from 9 frames to 5. The engine still
		// writes holdCount[i] = 9 when a repeat fires and that store is dead: holdCount
		// is only ever tested == 0, and only on a fresh-press frame.
		const float FirstRepeatSeconds = 0.46666664f;
		const float LaterRepeatSeconds = 0.29999998f;
		const float RepeatIntervalSeconds = 0.46666664f - 0.29999998f; // 0.16666666
	}

	// A module descriptor. 25 of them, reached through Rva::ModuleTable.
	namespace ModuleDesc
	{
		const DWORD Exec = 0x00;       // fn *, per-frame step, ModuleStepAll calls it
		const DWORD Draw = 0x04;       // fn *, ModuleDrawAll calls it
		const DWORD Prepare = 0x08;    // fn *, ModuleRegister calls it with the mode
		const DWORD Init = 0x0C;       // fn *, ModuleStart calls it with the arg
		const DWORD Suspend = 0x10;    // fn *, ModuleSuspend calls it
		const DWORD Term = 0x14;       // fn *, ModuleStop calls it
		const DWORD Active = 0x18;     // BYTE, ModuleStart writes the word 0x0101 here
		const DWORD DrawActive = 0x19; // BYTE
		const DWORD ActiveAlt = 0x1A;  // BYTE, the shadow ModuleStepAll also accepts
		const DWORD State = 0x1C;      // int, THE MODULE'S OWN STATE MACHINE
	}

	// The menu work block, reached through Rva::MenuWork. Only the fields that
	// matter to co-op are listed. The block is far bigger and mostly holds the 2D
	// camera and a pile of matrices.
	namespace MenuWork
	{
		const DWORD NodeCount = 0x0004;      // int16, how many node records
		const DWORD NodeRecords = 43028;     // 20 bytes each, see NodeRecord below
		const DWORD ScreenHandler = 71080;   // fn *, the current screen's handler
		const DWORD SavedHandler = 71088;    // fn *, pushed while a popup is up
		const DWORD OpenFadeCounter = 71096; // BYTE, ++ until it passes 0x14

		// THE SPHERE GRID'S CHARACTER. A character record index, written by
		// MenuSphereGridSwitchChar from MenuCursorChar, and the value
		// FFX_SaveData_SpendSphereLevels is charged against.
		const DWORD SphereGridChar = 71100;      // BYTE
		const DWORD SphereGridCharPrev = 71101;  // BYTE, before the last L1 or R1
		const DWORD SphereGridSwitchDir = 71102; // BYTE, 1 = L1, 2 = R1, 0 = idle

		const DWORD SphereLevelsToSpend = 71196; // int, the cost of the pending node
		const DWORD SphereGridNodeIndex = 71224; // BYTE, the cursor's node

		// The Sphere Grid's own pad decode, filled by MenuSphereGridReadPad. Frame
		// counted, not clock counted, so this part of the menu is deterministic
		// given the same held mask.
		const DWORD GridPadHeld = 71276;            // WORD
		const DWORD GridPadPressed = 71278;         // WORD
		const DWORD GridPadPrevHeld = 71280;        // WORD
		const DWORD GridPadHeldOrRepeat = 71282;    // WORD, L1 and R1 are read here
		const DWORD GridPadPressedOrRepeat = 71284; // WORD
		const DWORD GridPadHoldFrames = 71286;      // BYTE[16], first repeat at 6
		const DWORD GridPadRepeatFrames = 71302;    // BYTE[16], then every 3

		const DWORD FrameCounterA = 71320; // int, ++ per MenuStep
		const DWORD FrameCounterB = 71324; // int
		const DWORD FrameCounterC = 71328; // int, clamped at 100
		const DWORD DirtyFlag = 71340;     // int, set when the grid changed
	}

	// One Sphere Grid node record inside MenuWork::NodeRecords.
	namespace NodeRecord
	{
		const DWORD Size = 20;
		// Bit (1 << characterIndex). Cleared for the acting character when a node is
		// activated, so this is the menu's working copy of who has taken what.
		const DWORD CharMask = 0x00; // BYTE
		const DWORD Flags = 0x02;    // BYTE, bit 0x08 means "selected"
	}

	// The page table, reached through Rva::MenuPages.
	namespace MenuPages
	{
		const DWORD PageSize = 832;
		const DWORD PageCount = 12;
		const DWORD PageStepHook = 0x34;   // fn * inside each page record
		const DWORD PageSelector = 0x2700; // int16, 9984. Negative means no page.
		// Selector 7 is special cased by MenuStep and routes to the Sphere Grid
		// character switch.
		const int SelectorSphereGridCharSwitch = 7;
	}

	// ---------------------------------------------------------------------------
	// Module ids. Named where the identification is proved, see MENU_SYSTEM.md for
	// what proved each one. The ones left as kMenuModuleUnknownN are honest: they
	// exist, they are registered for the menu, and nothing yet pins them to a
	// screen.
	// ---------------------------------------------------------------------------
	enum MenuModuleId
	{
		kMenuModuleBoot = 0,      // boot and title sequencer
		kMenuModuleMain = 1,      // THE MAIN MENU TOP LEVEL
		kMenuModuleAbility = 2,   // Abilities. Spends MP.
		kMenuModuleCustomise = 3, // Customise. Spends items, edits equipment.
		kMenuModuleEquip = 4,     // Equip. The only screen calling SetCharEquip.
		kMenuModuleUnknown6 = 6,
		kMenuModuleItem = 7, // Items. Calls AddItem.
		kMenuModuleUnknown9 = 9,
		kMenuModuleConfig = 10,     // Config. The row table and the env reload.
		kMenuModuleSaveFront = 11,  // the sa_menu save frontend, game mode 0x400000
		kMenuModuleShop = 12,       // a shop, game mode 0x40000
		kMenuModuleShopAlt = 13,    // a second shop, game mode 0x20000
		kMenuModuleMovie = 14,      // FMV, game mode 0x80000
		kMenuModuleSaveLoadA = 15,  // save and load, game mode 0x200000
		kMenuModuleSaveLoadB = 16,  // same exec as 15, game mode 0x100000
		kMenuModuleSound = 17,      // game mode 0x10000
		kMenuModuleSphereGrid = 19, // THE SPHERE GRID. Proved, see MENU_SYSTEM.md.
		kMenuModuleUnknown20 = 20,
		kMenuModuleOverdrive = 21, // probably Overdrive mode. Writes record+0x38.
		kMenuModuleDevTest = 22,   // the TK dev harness, game mode 0x800000
		kMenuModuleUnknown23 = 23,
		kMenuModuleCount = 25
	};

	// The main menu's cursor result codes, as MenuStartSubmoduleByResult maps them.
	// Useful for "which row did the player pick" without decoding the cursor.
	int MenuModuleForMainMenuResult(int result);

	// ---------------------------------------------------------------------------
	// Menu open modes. Mode 0 is the ordinary main menu and is the only one a co-op
	// build should be replicating.
	// ---------------------------------------------------------------------------
	const int kMenuModeMain = 0;
	const int kMenuModeCloudSaveA = 0x40100000; // takes a different branch entirely
	const int kMenuModeCloudSaveB = 0x40200000;
	const int kMenuModeGated = 0x42000000; // refused unless MenuOpenPending
	const int kMenuModeNothingPending = -1;

	// ---------------------------------------------------------------------------
	// Is the menu up, and which screen?
	// ---------------------------------------------------------------------------

	// Either of the two running flags. Reads the same globals MainLoop.h exposes as
	// MenuSysRunning and MenuSysRunningAlt, so this and ffx::ReadPauseState agree.
	bool MenuSystemRunning();

	// The module active mask. Bit N is set while module N is running. 0 when the
	// address is not readable.
	DWORD ActiveModuleMask();

	// The suspend mask. Bit N set means module N's exec is skipped this frame even
	// though it is active. Zeroed at the top of every menu frame, so a set bit is
	// normally the current frame's own doing.
	DWORD SuspendedModuleMask();

	// Bit id of the active mask. False for an out of range id.
	bool ModuleActive(int id);

	// Is this module both active and not suspended, which is the condition
	// FFX_Module_StepAll actually tests. This is the honest "is it stepping".
	bool ModuleStepping(int id);

	// The module's own state machine integer, descriptor + 0x1C. Returns false when
	// the slot is empty, holds the .data sentinel -1, or is not readable. The value
	// means something different per module, so it is a fingerprint rather than a
	// number to reason about, but it is exactly what a desync detector wants: two
	// machines on the same screen should be in the same state.
	bool ModuleState(int id, int* out);

	// The descriptor pointer for a module, or NULL. Validated, including the -1
	// sentinel the .data initialiser leaves behind.
	BYTE* ModuleDescriptor(int id);

	// A stable English name for a module id, never NULL, and it does not read the
	// game. Returns "unidentified submenu" for the ids nothing has pinned down.
	const char* ModuleName(int id);

	// The best single answer to "which screen is the player looking at". Walks the
	// active mask and names the most specific active module, preferring a submenu
	// over the main menu because a submenu suspends the main menu rather than
	// stopping it. Never NULL. Returns "none" when the menu is not up.
	const char* MenuScreenName();

	// The id MenuScreenName picked, or -1. Use this when you want to compare two
	// machines rather than log a string.
	int ActiveMenuScreen();

	// ---------------------------------------------------------------------------
	// THE INPUT BLOCK, which is the replication surface.
	//
	// MenuPadFrame holds the masks that actually matter, which is every field a
	// screen can observe. The hold counters and hold timers are NOT in here on
	// purpose: they are the inputs to the repeat calculation, the repeat mask is
	// the output, and the output is what gets replicated. See the note on
	// MenuPadBlock above.
	//
	// Button bit names are ffx::Btn::* in ffx/Input.h. The menu's own conventions,
	// read off the Config screen: Cross (0x40) confirms, Left (0x8000) and Right
	// (0x2000) change a value, Up (0x1000) and Down (0x4000) move the cursor, and
	// L1 (0x04) and R1 (0x08) change character.
	// ---------------------------------------------------------------------------
	struct MenuPadFrame
	{
		WORD held;          // +0x12
		WORD pressed;       // +0x14
		WORD word10;        // +0x16, the pad layer's repeat, what GetRepeat prefers
		WORD heldSticky;    // +0x18, THE SPHERE GRID READS THIS ONE
		WORD pressedSticky; // +0x1A
		WORD word10Sticky;  // +0x1C
		WORD synthHeld;     // +0x22, held with the stick folded into the dpad bits
		WORD synthPressed;  // +0x24
		WORD repeat;        // +0x26, wall-clock paced in the engine. DERIVE this from
		                    // the step counter, do not transmit it. See caveat one.
		BYTE analogX;       // +0x1E, 0x80 centred
		BYTE analogY;       // +0x1F, 0x80 centred
	};

	// Snapshots the block. False when it is not readable, which in practice means
	// never, because it is a static.
	bool ReadMenuPad(MenuPadFrame* out);

	// Overwrites the block from a frame. Writes all nine masks and both analog
	// bytes and leaves the hold counters and timers alone.
	//
	// WHERE TO CALL IT: from a HookMenuSamplePad callback, which runs after the
	// original sample. Anywhere else in the frame and either the original will
	// overwrite you or the screens will already have read the old values.
	bool WriteMenuPad(const MenuPadFrame* in);

	// Zeroes the whole 192 bytes and re-centres the analog word, which is exactly
	// what FFX_MenuSys_ClearPad does.
	//
	// IT CLEARS FAR MORE THAN THE INPUT, and an earlier version of this comment
	// recommended it for the passenger's "no buttons this step" case. Do not use it
	// for that. The 0xC0 covers:
	//
	//   +0x00, +0x01   the two ARM bytes FFX_MesWin_SamplePadPort0 bails out on. That
	//                  sampler writes this same block, and it zeroes one of them on
	//                  its way out, so something has to re-arm it. Clearing them from
	//                  a replicator every step can hold the message window's pad read
	//                  disarmed, which stops a dialogue box being advanceable while
	//                  the menu looks perfectly fine.
	//   +0x02..+0x11   the 16 hold counters, shared with that sampler
	//   +0x40..+0xBF   holdTimer and lastSampleTime, 32 floats of THIS MACHINE's
	//                  process clock. Zeroing lastSampleTime makes the next sample
	//                  accumulate the entire process uptime into holdTimer in one go.
	//
	// The game gets away with it because it only ever calls it on a module boundary,
	// from FFX_Module_Stop and FFX_MenuSys_Enter. Every step is not a module boundary.
	//
	// TO MEAN "NO BUTTONS", write a zeroed MenuPadFrame through WriteMenuPad with both
	// analog bytes at MenuPadBlock::AnalogCentre. That touches the nine masks and the
	// two analog bytes and nothing else, which is exactly the right surface, and the
	// fact that MenuPadFrame has no field for the arm bytes is what makes it safe.
	// Note the analog bytes must be 0x80 and not 0: zero is full deflection and
	// synthesises a held Up and Left.
	bool ClearMenuPad();

	// Convenience readers that answer what a screen would see, including the
	// synthesised and sticky fallbacks the game's own accessors apply. Pure reads,
	// no game call, so they are safe from any thread.
	WORD MenuHeld();    // what FFX_MenuSys_GetHeld would return
	WORD MenuPressed(); // what FFX_MenuSys_GetPressed would return
	WORD MenuRepeat();  // what FFX_MenuSys_GetRepeat would return

	// ---------------------------------------------------------------------------
	// The sample hook.
	//
	// A detour on FFX_MenuSys_SamplePad 0x004BE500. Five stolen bytes,
	// 55 8B EC 51 56, no branch and no absolute address among them.
	//
	// The callback runs AFTER the original, so the block is already filled from the
	// local pad when you get it. That is the point: read it to transmit, overwrite
	// it to receive.
	//
	// The function takes no real argument. Hex-Rays types it __thiscall because it
	// passes ecx on to FFX_Input__getTimeSeconds, which ignores it, and the call
	// site leaves a stray byte value in ecx. Nothing after the call reads ecx, so a
	// __cdecl hook is safe here. That was checked instruction by instruction rather
	// than assumed.
	//
	// The callback may be null, and installing twice is a no-op that just swaps the
	// callback. Install once at startup and gate at runtime.
	// ---------------------------------------------------------------------------
	typedef void(__cdecl* MenuSamplePadFn)(void);
	typedef void(__cdecl* MenuPadObserverFn)(void); // runs after the original

	bool HookMenuSamplePad(MenuPadObserverFn after);
	bool MenuSamplePadHookInstalled();
	MenuSamplePadFn OriginalMenuSamplePad();

	// How many times the hook has run. A cheap "is the menu actually sampling"
	// health metric, and a way to tell a frozen menu from a closed one.
	DWORD MenuPadSampleCount();

	// ---------------------------------------------------------------------------
	// The ATEL script virtual pad, the other injection channel.
	//
	// While enabled, FFX_MenuSys_SamplePad ignores the real controller completely
	// and fills the block from the mask you set. The mask self-clears every frame,
	// so SetVirtualPadButtons has to be called every frame a button should be down.
	// In this mode held, pressed and repeat are all the same value.
	//
	// IT IS GLOBAL. Turning it on takes the local controller away from the menu.
	// ---------------------------------------------------------------------------
	bool VirtualPadEnabled();
	bool SetVirtualPadEnabled(bool enabled);

	// The mask for the NEXT frame. FFX_MenuSys_SamplePad latches it and clears it.
	bool SetVirtualPadButtons(DWORD mask);
	bool VirtualPadButtons(DWORD* outLive, DWORD* outNext);

	// axis 0 right X, 1 right Y, 2 left X, 3 left Y. 0x80 is centred, and the latch
	// resets all four to 0x80 every frame.
	bool SetVirtualPadAxis(int axis, BYTE value);

	// Clears both mask slots and disables, which is the game's own reset.
	bool ResetVirtualPad();

	// ---------------------------------------------------------------------------
	// Open and close.
	//
	// Opening is a request, not an action. RequestMenuOpen stores the mode and the
	// menu comes up on the NEXT frame, when FFX_MainStep gets to
	// FFX_MenuSys_PollOpenAndStep. That one-frame delay is convenient for lockstep:
	// both machines can be told to open on the same simulation step.
	//
	// RequestMenuOpen goes through the game's own FFX_MenuSys_RequestOpen 0x421750
	// rather than writing the global, because the real function also refuses the
	// gated mode, clears the enter result and takes the cloud-save branch where
	// relevant. A direct write would skip all of that.
	// ---------------------------------------------------------------------------

	// Is a menu open pending but not yet serviced? True when the requested mode is
	// not -1.
	bool MenuOpenRequested();

	// The pending mode, or kMenuModeNothingPending. Mode 0 is the ordinary menu.
	int MenuRequestedMode();

	// The mode the menu was actually entered with, valid while it is running.
	int MenuEnteredMode();

	// The result of the last enter attempt. 0 means it succeeded.
	int MenuEnterResult();

	// Asks for the menu. Pass kMenuModeMain for the ordinary one. Returns false when
	// the game function is not callable or the mode is one this wrapper refuses,
	// which is every mode except kMenuModeMain unless you pass allowSpecialModes.
	// The special modes reach cloud save and save-UI paths that have nothing to do
	// with co-op, so the default is to refuse them rather than let a typo open one.
	bool RequestMenuOpen(int mode, bool allowSpecialModes);

	// Overload for the common case. Same as RequestMenuOpen(mode, false).
	inline bool RequestMenuOpen(int mode)
	{
		return RequestMenuOpen(mode, false);
	}

	// Cancels a pending request by writing -1. Only useful between the request and
	// the next frame, which is a narrow window, but a lockstep gate that refuses a
	// step needs it.
	bool CancelMenuOpenRequest();

	// ---------------------------------------------------------------------------
	// THE PER-CHARACTER GATE.
	//
	// One menu-wide answer to "which character is the cursor on", used by every
	// screen that acts on one person. The value is a character record index, the
	// same identity ffx/GameState.h calls a character index, so it goes straight
	// into ActiveSlotOfCharacter or an ownership table.
	//
	// For the Sphere Grid requirement, "only a character's owner may move or act on
	// that character's grid", there are two fields and they are not the same thing:
	//
	//   MenuCursorChar()      the menu-wide cursor, 46 readers, the one the Equip
	//                         and Abilities screens use.
	//   SphereGridCharacter() the grid's own copy at MenuWork + 71100, written from
	//                         the cursor by the L1 and R1 handler, and THE ONE
	//                         FFX_SaveData_SpendSphereLevels IS CHARGED AGAINST.
	//
	// Gate against the second one for the grid. Audit both if you want to catch a
	// desync, because they can legitimately differ for a frame during a switch.
	// ---------------------------------------------------------------------------

	// MenuCharList[MenuCharCursor]. A character record index 0..17, or
	// ffx::kCharNone (0xFF) when it cannot be read. Pure reads, no game call.
	BYTE MenuCursorChar();

	// The raw cursor index and the list length, for walking the list. The list is
	// physically EIGHT bytes with the first of its two counts immediately after it,
	// so the length is clamped to 8 however big the count reads.
	int MenuCharCursorIndex();
	int MenuCharListLength();

	// Entry n of the menu's character list, 0..7, or kCharNone.
	BYTE MenuCharListEntry(int n);

	// The Sphere Grid's own character, from the menu work block. kCharNone when the
	// grid is not up or the work block is not readable. THIS is the gate field.
	BYTE SphereGridCharacter();

	// What the grid was on before the last L1 or R1, and which way it moved.
	// direction is 1 for L1 (previous), 2 for R1 (next), 0 when idle.
	bool SphereGridSwitchState(BYTE* outPrevChar, BYTE* outDirection);

	// The Sphere Grid's own decoded pad struct, at MenuWork + 71276, as filled by
	// FFX_Menu_SphereGridReadPad. Three of the five words, which are the ones a
	// screen acts on.
	//
	// WHY THIS IS WORTH READING RATHER THAN JUST READING THE BLOCK. The grid does not
	// use FFX_MenuSys_GetRepeat at all. It takes the sticky held copy at pad block
	// +0x18 and runs its OWN frame-counted repeat, first at 6 frames then every 3, in
	// counters at MenuWork + 71286 and + 71302. Frame counted rather than clock
	// counted, so it is deterministic given the same held mask, which is convenient.
	//
	// And it ORs in a dpad bit synthesised from the RAW PAD. The analog read goes
	// through FFX_MenuSys_ReadAnalogByte, which reaches FFX_Pad__readAnalogByte and
	// never looks at the pad block, with a +-64 threshold rather than the 0x18 / 0xE8
	// the menu sampler uses. FFX_Menu_SphereGridReadStick 0xA56BD0 does the same for
	// both axes to pan and zoom the cursor. So THE PAD BLOCK IS NOT THE WHOLE INPUT
	// SURFACE FOR MODULE 19, and a replicator that overwrites the block still leaves
	// the grid partly driven by whichever controller is plugged into each machine.
	//
	// These three words are where that shows up first, which is why they are exposed:
	// hash them and a divergence on the grid is caught on the step it happens.
	//
	// The two ways to close the hole: enable the ATEL virtual pad while module 19 is
	// up, which redirects FFX_MenuSys_ReadAnalogByte to an axis set the latch keeps
	// centred at 0x80, or replicate real stick axes and serve them from a detour.
	// Neither function can take a 5-byte detour as it stands, both have a rel32 call
	// inside the first nine bytes.
	bool SphereGridPadState(WORD* outHeld, WORD* outPressed, WORD* outHeldOrRepeat);

	// The grid's pending node cost in sphere levels, and the node the cursor is on.
	// Reading these every frame is how a detector notices the two machines are
	// pointing at different nodes before either one commits.
	bool SphereGridPendingCost(int* out);
	bool SphereGridNodeIndex(int* out);

	// Is the Sphere Grid actually up, which is module 19 stepping rather than merely
	// registered.
	bool SphereGridActive();

	// ---------------------------------------------------------------------------
	// THE CONFIG SCREEN'S ROW TABLE, which is the one screen a mod can add to.
	//
	// Module 10 draws its rows out of two plain globals: a POINTER to an array of
	// row-object POINTERS, and a row COUNT that is an ordinary int rather than a
	// compile-time constant. Both are in the zero-filled tail of .data, both are
	// writable, and the game rewrites both from FFX_Menu_ConfigSelectRowTable. So a
	// mod can point them at a longer array of its own and get native rows, drawn by
	// the game, driven by the game's own cursor.
	//
	// WHO CALLS WHAT, because the install timing falls straight out of it:
	//
	//   FFX_Menu_ConfigSelectRowTable  module 10's PREPARE slot. FFX_Module_Register
	//                                  runs it once per FFX_Module_SwitchGameMode,
	//                                  which is once per MENU OPEN, not once per
	//                                  entry to the Config screen. It picks one of
	//                                  four arrays on language and an asset check and
	//                                  sets the count to 8.
	//   FFX_Menu_ConfigInitRowValues   module 10's INIT slot, so it runs on every
	//                                  entry to the Config screen. It resets the
	//                                  cursor to 0 and calls EVERY row's getter to
	//                                  seed that row's current value. It walks the
	//                                  live count, so appended rows are seeded by the
	//                                  game for free.
	//
	// Which means nothing has to be detoured to install rows. Notice that the table
	// has gone back to a shipped array, and re-apply. There is plenty of time: the
	// earliest the player can reach the Config screen is several steps after the
	// menu opened.
	//
	// A DETOUR ON FFX_Menu_ConfigSelectRowTable IS NOT POSSIBLE ANYWAY. That function
	// has no prologue at all. Its first instruction is E8 9B 0F FE FF, a rel32 call,
	// which is exactly the five bytes a jmp wants, so workshop/Detour.h refuses it
	// and is right to.
	//
	// THREE THINGS THAT WILL BITE, all read off the disassembly rather than guessed:
	//
	//   1. g_ffxMenuConfigCursorRow is a SIGNED CHAR and nothing clamps it against a
	//      count that shrank. So never shorten the array while the Config screen is
	//      up. Put the table back when the screen is gone, or at the very least set
	//      the cursor to 0 in the same breath. SetConfigRowTable does the second for
	//      you and says so.
	//   2. Up and Down loop until they land on a row whose selectable flag is 1. If
	//      no row is selectable the loop never ends and the game is gone. Keep at
	//      least one.
	//   3. Row pitch is 740 / count from y = 220, and the glyphs do not shrink with
	//      it. Eight rows is the shipped 92 pixels each. Thirteen is 57, which still
	//      clears the 18 pixel glyph height. Somewhere past twenty they touch.
	// ---------------------------------------------------------------------------

	// One row object, 44 bytes. Field offsets rather than a struct, to match the rest
	// of this file and because the value-name ids are an array whose stride is not
	// the element size.
	namespace ConfigRow
	{
		const DWORD Unused00 = 0x00;     // int, 0 in all ten shipped rows
		const DWORD ValueCount = 0x04;   // int, how many settings the row cycles
		const DWORD CurrentValue = 0x08; // int, seeded from the getter
		const DWORD Selectable = 0x0C;   // int, Up/Down skip anything that is not 1
		const DWORD Getter = 0x10;       // void (__cdecl *)(row), must not be null
		const DWORD Setter = 0x14;       // void (__cdecl *)(row)
		const DWORD LabelId = 0x18;      // WORD, a group 7 string id
		const DWORD Unknown1A = 0x1A;    // WORD, 3 in all ten shipped rows, unread
		const DWORD ValueIds = 0x1C;     // WORD each, STRIDE 4, count = ValueCount
		const DWORD ValueIdStride = 4;
		const DWORD Size = 0x2C;
	} // namespace ConfigRow

	// How many value-name ids a 44 byte row holds: +0x1C to +0x28 at stride 4.
	const int kConfigRowMaxValues = 4;

	// What the shipped game has. Eight rows drawn from a ten row pool at 0xC5A39C,
	// and all four candidate arrays hold the same eight pointers in the same order in
	// this build, so the language and asset branches are vestigial here.
	const int kConfigShippedRowCount = 8;

	// The cursor is a signed char, so this is a hard ceiling rather than taste.
	const int kConfigMaxRowCount = 100;

	// A row callback. Both slots take the row pointer and nothing else.
	typedef void(__cdecl* ConfigRowFn)(void* row);

	// The live row count and the live array. Both report 0 and NULL before the menu
	// has been opened once, because the globals start zeroed and only
	// FFX_Menu_ConfigSelectRowTable fills them.
	int ConfigRowCount();
	void** ConfigRowTable();

	// One row, or NULL. Bounds checked against the live count, which the game's own
	// FFX_Menu_ConfigRowAt does not do.
	void* ConfigRowAt(int index);

	// The cursor. Read as a signed char by the game, so this reports -1 when it is
	// not readable rather than 255.
	int ConfigCursorRow();
	bool SetConfigCursorRow(int index);

	// Is the live table one of the game's own four arrays, which is to say has nobody
	// replaced it. True also when it is NULL, because an empty table is the game's
	// state too.
	bool ConfigRowTableIsShipped();

	// Copies the live array's pointers into out. This is how a mod builds its own
	// longer array without caring which of the four the game picked, and without ever
	// writing to a game row. Returns false and writes nothing when the live table is
	// not readable.
	bool CopyConfigRowTable(void** out, int maxRows, int* outCount);

	// Points the two globals at rows and count. Pass the saved originals back to
	// restore. rows may be NULL only when count is 0.
	//
	// It CLAMPS g_ffxMenuConfigCursorRow into the new count, because nothing in the
	// game does and a cursor left past the end is an out of bounds row-pointer read
	// on the next Up or Down press. That does not make shrinking the table while the
	// screen is up safe, it only makes it survivable.
	bool SetConfigRowTable(void** rows, int count);

	// Typed access to one row's mutable fields. They work on a game row as well as on
	// a mod's own, but see the warning on BuildConfigRow.
	bool ConfigRowValueCount(const void* row, int* out);
	bool ConfigRowCurrentValue(const void* row, int* out);
	bool ConfigRowSelectable(const void* row, int* out);
	bool SetConfigRowCurrentValue(void* row, int value);
	bool SetConfigRowSelectable(void* row, bool selectable);
	WORD ConfigRowLabelId(const void* row);
	WORD ConfigRowValueId(const void* row, int value);

	// Fills a 44 byte row object a mod owns. valueIds must hold valueCount entries
	// and valueCount must be 0 to kConfigRowMaxValues. A valueCount of 0 draws the
	// label and the separator and no values, which is what a header row wants.
	//
	// NEVER POINT THIS AT A GAME ROW. The eight shipped rows are writable and
	// overwriting one would persist for the life of the process, so the worst failure
	// stops being a missing row and starts being a corrupted Config screen.
	bool BuildConfigRow(void* row, int valueCount, bool selectable, ConfigRowFn getter,
	    ConfigRowFn setter, WORD labelId, const WORD* valueIds);

	// Is module 10 actually stepping, as opposed to merely registered. The question to
	// ask before changing the table.
	bool ConfigScreenActive();

	// ---------------------------------------------------------------------------
	// FFX TEXT, so a mod can put its own words on screen.
	//
	// FFX_Text_DrawString 0x905AB0 is the only text entry the in-game menu uses and it
	// takes a plain caller-owned char buffer, so arbitrary text is drawable once the
	// encoding is known. It is known, and it is not ASCII.
	//
	// THE ENCODING. byte = 0x30 + glyph index, and the glyph table is
	//
	//   index  0..9   '0'..'9'
	//   index 10..31  space ! " # $ % & ' ( ) * + , - . / : ; < = > ?
	//   index 32..57  'A'..'Z'
	//   index 58..63  [ \ ] ^ _ `
	//   index 64..89  'a'..'z'
	//   index 90..93  { | } ~
	//
	// So it is ASCII order with the ten digits lifted to the front and '@' dropped. A
	// space is 0x3A, '-' is 0x47, ':' is 0x4A, 'A' is 0x50 and 'a' is 0x70. Terminate
	// with 0x00.
	//
	// HOW THAT WAS ESTABLISHED, because an encoding nobody can check is worthless.
	// FFX_Text_DecodeGlyph 0x8B7240 computes the glyph index as byte - 0x30 and the
	// sheet coordinates from it, and its fallback branch for an unmapped low byte
	// draws glyph 10, which is the space. Then the table was round-tripped against the
	// shipped /FFX_Data/GameData/PS3Data/LocKit/FFX_LOC_KIT_PS3_US.BIN, whose lines
	// the PC port hands straight to FFX_Text_DrawString: lines 461, 462 and 464 are
	// "Music", "Original" and "Arranged", and all three encode byte for byte.
	//
	// WHAT NOT TO DO. Bytes below 0x30 are control codes and most of them eat a
	// following byte, so never hand raw ASCII to the draw. 0x00 to 0x03 end the line,
	// 0x0B is the button-icon escape and takes an icon id in the next byte. And a RUN
	// of 0x47, the dash, is collapsed into one stretched glyph, which is how the game
	// draws a horizontal rule, so "--" does not come out as two dashes.
	// ---------------------------------------------------------------------------

	// How many bytes EncodeFfxText needs for this string, terminator included.
	int FfxTextEncodedLength(const char* ascii);

	// Encodes ASCII into out. Characters outside 0x20..0x7E, and '@' which has no
	// glyph, become a space rather than being dropped, so the result is always the
	// same length as the input and a bad character shows up instead of silently
	// shifting the text along. Returns false when out is too small, and then leaves
	// out as a valid empty string rather than a truncated one.
	bool EncodeFfxText(const char* ascii, char* out, int outBytes);

	// ---------------------------------------------------------------------------
	// SUPPLYING YOUR OWN UI STRINGS.
	//
	// Every label on the Config screen is a group 7 string id looked up at draw time,
	// so a mod that answers for ids of its own gets the native draw to do the whole
	// row: label, value names, greying, centring and the help line.
	//
	// THERE ARE TWO LOOKUPS AND A MOD HAS TO ANSWER BOTH. One kernel table row is four
	// WORD offsets, { name lang0, name lang1, description lang0, description lang1 }.
	// FFX_KernelString_Get 0x78FCF0 reads the first pair and is what
	// FFX_Menu_GetUiString calls. FFX_KernelString_GetDescription 0x78FBB0 reads the
	// second pair and is what FFX_Menu_GetUiStringDesc calls for the help line under
	// the selected row. Both are detoured here and both pass everything through
	// unless the provider answers.
	//
	// FFX_Menu_GetUiString itself CANNOT be detoured: its fourth byte starts a rel32
	// call. The two functions under it can, 55 / 8B EC / 8B 45 08, six whole bytes
	// with nothing position dependent among them.
	//
	// PICK IDS WELL ABOVE THE REAL TABLE. The id is masked to 12 bits, so the usable
	// space is 0 to 4095, and the shipped Config rows use 4 to 0x25. Out of range is
	// not dangerous by itself, because FFX_KernelTable_GetRow falls back to the first
	// range descriptor rather than reading wild, but an id that collides with a real
	// one means the provider has quietly replaced a game string somewhere else.
	//
	// The provider runs ON THE GAME THREAD INSIDE THE DRAW. Keep it to filling a
	// static buffer. Return NULL for anything it does not own, which has to include
	// every group other than the one it reserved ids in.
	//
	// THE RETURNED POINTER HAS TO OUTLIVE THE FRAME. The label is drawn immediately,
	// but FFX_Menu_SetHelpString keeps the description POINTER rather than copying it
	// and the bottom line is drawn from it later. A static buffer per id is the simple
	// answer, and no buffer may be shared by two ids that are both live in one frame.
	// ---------------------------------------------------------------------------

	// group is the kernel string group, 7 for the in-game menu's UI strings. id has
	// already been masked to 12 bits. wantDescription is false for the label and true
	// for the help line. Return an FFX-ENCODED, NUL terminated string, or NULL to let
	// the game answer.
	typedef const char*(__cdecl* UiStringProviderFn)(int group, int id, bool wantDescription);

	// Installs both detours. Idempotent, and replacing the provider on a later call is
	// allowed. Returns false when either prologue does not match, and in that case
	// NEITHER detour is left installed.
	bool InstallUiStringOverride(UiStringProviderFn provider);
	bool UiStringOverrideInstalled();

	// How many lookups the provider has answered, so a readout can say whether the
	// mechanism is doing anything at all.
	DWORD UiStringOverrideHits();

	// ---------------------------------------------------------------------------
	// Diagnostics
	// ---------------------------------------------------------------------------

	// One summary line set: running flags, the active and suspend masks with the
	// screen name, the module states of whatever is active, the current pad frame
	// and the character cursor. Worth calling once at startup to confirm the
	// bindings landed, and on a divergence to see which machine is on which screen.
	void LogMenuSystemState();

	// Just the pad frame, for watching input replication work. Cheap enough to call
	// every frame while debugging, far too noisy to leave in.
	void LogMenuPadFrame();

} // namespace ffx
