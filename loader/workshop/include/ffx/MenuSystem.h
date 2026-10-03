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
// Caveat one, THE REPEAT MASK IS WALL-CLOCK DERIVED. FFX_MenuSys_SamplePad reads
// Phyre's process clock to run the 16 auto-repeat timers. Two machines pressing
// the same button on the same frame will not agree on when it repeats. So the
// repeat mask is part of the replicated payload. MenuPadFrame carries it, and
// WriteMenuPad writes it. Do not try to rederive it on the passenger.
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

		// The auto-repeat constants, for anyone who wants to predict the mask
		// rather than replicate it. Doing that is not recommended, see the header
		// comment, but the numbers should be written down somewhere.
		const float FirstRepeatSeconds = 0.46666664f;
		const float LaterRepeatSeconds = 0.29999998f;
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
		WORD repeat;        // +0x26, WALL-CLOCK DERIVED, must be replicated
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
	// what FFX_MenuSys_ClearPad does. This is a legal state: the game calls the same
	// wipe from FFX_Module_Stop. Use it on the passenger when a peer frame is
	// missing, because no input is always safer than stale input.
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

	// The grid's pending node cost in sphere levels, and the node the cursor is on.
	// Reading these every frame is how a detector notices the two machines are
	// pointing at different nodes before either one commits.
	bool SphereGridPendingCost(int* out);
	bool SphereGridNodeIndex(int* out);

	// Is the Sphere Grid actually up, which is module 19 stepping rather than merely
	// registered.
	bool SphereGridActive();

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
