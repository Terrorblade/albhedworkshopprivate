#pragma once

#include <windows.h>

// The in-game menu: party, system, equipment, abilities, items and the Sphere
// Grid. The native PS2-era menu, not the PC options overlay. The overlay is
// escmenu.swf driven by FFEscMenu__* and IggyMenu__*, which is a different
// system and lives in MainLoop.h as EscMenu / EscMenuIsOpen.
//
// Addresses are RVAs, which is the IDA VA minus 0x00400000.
//
// ALREADY DECLARED IN addresses\MainLoop.h, DO NOT REDECLARE: MenuSysRunning
// (0x00F40824), MenuSysRunningAlt (0x00F40828), MenuOpenPending (0x00F4082C),
// SaveUiState (0x008CB994), EscMenuIsOpen and EscMenu. Those four globals are
// the menu's effect on the frame loop, which is why they live over there.
// Everything here is the menu's own machinery.
//
// The derivation, the evidence and the per-screen identification are in
// reversing\MENU_SYSTEM.md.

namespace ffx
{
	namespace Rva
	{

		// ---------------------------------------------------------------------------
		// WHY THIS AREA MATTERS TO CO-OP
		//
		// The menu must be openable and operable by both players, with whoever opened
		// it driving, and the Sphere Grid gated so only a character's owner may act on
		// that character's grid. The design is "replicate input, not state", and the
		// code supports it: the whole menu is deterministic native C reading ONE
		// 192-byte global input block, and it commits to the save block through a short
		// list of named functions rather than by scribbling on it.
		//
		// The frame, in order:
		//
		//   FFX_MainStep 0x00420AE0
		//     -> MenuSysPollOpenAndStep                   three call sites per frame
		//          not running, nothing pending:
		//            PlayerPadPressed & 0x10 (Triangle) -> MenuSysRequestOpen(0)
		//          pending:
		//            MenuSysEnter(mode) -> MenuSysRunFrame(mode)   first frame, inits
		//          running:
		//            MenuSysRunFrame(mode)                         every later frame
		//              -> MenuSysStepFrame
		//                   MenuSysSamplePad     <- THE ONLY PAD READ. Hook here.
		//                   ModuleStepAll        <- every screen's exec, reads the block
		//                   ModuleDrawAll
		// ---------------------------------------------------------------------------

		// ---------------------------------------------------------------------------
		// Open, close and the running flags.
		//
		// MenuSysRequestOpen stores a mode and returns. The open happens on the NEXT
		// frame, when MenuSysPollOpenAndStep sees the pending mode. Mode 0 is the
		// ordinary main menu. 0x40100000 and 0x40200000 are the cloud save paths and
		// take a completely different branch, and 0x42000000 is refused unless
		// MenuOpenPending is already set.
		// ---------------------------------------------------------------------------
		const DWORD MenuSysPollOpenAndStep = 0x00420750; // BOOL __cdecl (void)
		const DWORD MenuSysRequestOpen = 0x00421750;     // int  __cdecl (int mode)
		const DWORD MenuSysEnter = 0x004BE840;           // int  __cdecl (int mode)
		const DWORD MenuSysRunFrame = 0x004AB030;        // the per-frame body
		const DWORD MenuSysStepFrame = 0x004A9CA0;       // sample pad, step all, draw all
		const DWORD SaveUiIsIdle = 0x00248100;           // int __cdecl (void)

		const DWORD MenuRequestedMode = 0x00EFBC38; // int, -1 means nothing pending
		const DWORD MenuEnterResult = 0x00EFBC3C;   // int, 0 means the enter succeeded
		const DWORD MenuEnterMode = 0x0146A214;     // int, the mode it was entered with
		const DWORD MenuGetEnterMode = 0x004BE4F0;  // int __cdecl (void)

		// ---------------------------------------------------------------------------
		// THE INPUT BLOCK. The single most useful thing in this file.
		//
		// One 0xC0 byte global, filled once per frame by MenuSysSamplePad and read by
		// every screen through the five accessors below. There is no port argument
		// anywhere on the path: FFX_Pad__readButtons16 is called with port 0 slot 0,
		// hardcoded. So the menu has no notion of a second player, and replication is a
		// matter of overwriting this one block.
		//
		// Field layout, as offsets from MenuPadBlock. The typed names are in
		// ffx/MenuSystem.h under namespace MenuPadBlock, so use those rather than
		// counting bytes here.
		//
		//   +0x02  16 bytes  per-button hold frame counters
		//   +0x12  2         held mask
		//   +0x14  2         newly-pressed mask
		//   +0x16  2         the readWord10 mask, which is the pad layer's own repeat
		//   +0x18  2         held, OR'd with the previous pad ring frame
		//   +0x1A  2         pressed, OR'd with the previous pad ring frame
		//   +0x1C  2         word10, OR'd with the previous pad ring frame
		//   +0x1E  4         analog: byte 0 left stick X, byte 1 left stick Y
		//   +0x22  2         held with dpad bits synthesised from the stick
		//   +0x24  2         the newly-pressed edge of that synthesised mask
		//   +0x26  2         auto-repeat mask
		//   +0x40  64        16 floats, seconds held
		//   +0x80  64        16 floats, last sample timestamp
		//
		// THE REPEAT MASK AT +0x26 IS WALL-CLOCK DERIVED. MenuSysSamplePad calls
		// FFX_Input__getTimeSeconds, which is Phyre's process clock, and the first
		// repeat fires at 0.4667 s and then every 0.3 s. Two machines running the same
		// buttons WILL compute different repeat masks. That does not break the plan, it
		// just means the repeat mask is part of the replicated payload rather than
		// something the passenger may rederive. Start (0x800) and Select (0x100) are
		// excluded from repeat entirely.
		//
		// The button bit names are ffx::Btn::* in ffx/Input.h. Same 16-bit PS2 layout.
		// ---------------------------------------------------------------------------
		const DWORD MenuPadBlock = 0x021D09C0;     // 0xC0 bytes
		const DWORD MenuSysSamplePad = 0x004BE500; // void (void), ecx is garbage
		const DWORD MenuSysClearPad = 0x004BE3D0;  // memset 0xC0, then analog = 0x80808080

		// The five readers. The first three are called at the top of essentially every
		// screen's exec, 34 or 35 call sites each. The sticky pair at +0x18 and +0x1A is
		// read only by the Sphere Grid, which is exactly why a replicator that writes
		// +0x12 and forgets +0x18 works everywhere except on the grid.
		const DWORD MenuSysGetHeld = 0x004BE410;          // __int16 (void)
		const DWORD MenuSysGetRepeat = 0x004BE470;        // __int16 (void)
		const DWORD MenuSysGetPressed = 0x004BE4B0;       // __int16 (void)
		const DWORD MenuSysGetHeldSticky = 0x004BE440;    // __int16 (void), +0x18
		const DWORD MenuSysGetPressedSticky = 0x004BE4E0; // __int16 (void), +0x1A, unused
		const DWORD MenuSysGetWord10Sticky = 0x004BE4A0;  // __int16 (void), +0x1C, unused
		const DWORD MenuSysReadAnalogByte = 0x004BE450;   // char (port, slot, axis, ring)

		// ---------------------------------------------------------------------------
		// THE ATEL SCRIPT VIRTUAL PAD, which is a shipped input injection channel.
		//
		// MenuSysSamplePad checks VirtualPadEnabled first. When it is set, the whole
		// block is filled from this double-buffered fake controller instead of the real
		// pad, and the analog-synthesised dpad fallback is skipped. The shipped use is
		// cutscene and tutorial scripts driving the menu: ATEL syscalls 501 (enable),
		// 502 and 562 (buttons) and 556 (analog).
		//
		// VirtualPadLatch, which MenuSysSamplePad calls, flips Next into the live slot
		// and resets Next to zero buttons and centred analog. So a mask has to be
		// written EVERY FRAME, and a mask written once produces exactly one frame of
		// input. In this mode held, pressed and repeat all come back as the same value,
		// because all three getters return the same dword, which makes a one-frame mask
		// one clean button press with no edge bookkeeping.
		//
		// That is a cleaner injection surface than overwriting the pad block, and it is
		// code the game already exercises. The catch is that it is global and
		// all-or-nothing: while it is on, the real controller cannot reach the menu at
		// all. Good for "the passenger watches", wrong for "both players poke at it".
		// ---------------------------------------------------------------------------
		const DWORD VirtualPadEnabled = 0x0146A430;     // BYTE, non-zero takes over
		const DWORD VirtualPadButtons = 0x0146A420;     // DWORD, the live mask
		const DWORD VirtualPadButtonsNext = 0x0146A424; // DWORD, written, latched later
		const DWORD VirtualPadAxis0 = 0x0146A42C;       // BYTE, right stick X
		const DWORD VirtualPadAxis0Next = 0x0146A42D;   // BYTE
		const DWORD VirtualPadAxis1 = 0x0146A42E;       // BYTE, right stick Y
		const DWORD VirtualPadAxis1Next = 0x0146A42F;   // BYTE
		const DWORD VirtualPadAxis2 = 0x0146A428;       // BYTE, left stick X
		const DWORD VirtualPadAxis2Next = 0x0146A429;   // BYTE
		const DWORD VirtualPadAxis3 = 0x0146A42A;       // BYTE, left stick Y
		const DWORD VirtualPadAxis3Next = 0x0146A42B;   // BYTE

		const DWORD VirtualPadIsEnabled = 0x004CA510;  // int  __cdecl (void)
		const DWORD VirtualPadSetEnabled = 0x004CA570; // char __cdecl (char on)
		const DWORD VirtualPadSetButtons = 0x004CA560; // int  __cdecl (int mask)
		const DWORD VirtualPadSetAxis = 0x004CA370;    // char __cdecl (int axis, char v)
		const DWORD VirtualPadGetButtons = 0x004CA500; // int  __cdecl (void)
		const DWORD VirtualPadGetAxis = 0x004CA320;    // char __cdecl (int axis)
		const DWORD VirtualPadLatch = 0x004CA490;      // flip Next into live
		const DWORD VirtualPadReset = 0x004CA540;      // clear both and disable

		// ---------------------------------------------------------------------------
		// The module manager, which is the menu's backbone.
		//
		// 25 slots. Each slot holds a pointer to a static descriptor with six function
		// pointers, an active byte and the module's own state integer. Every screen of
		// the menu is one module, so "which screen is up" is a bitmask read.
		//
		// Descriptor layout is in ffx/MenuSystem.h under namespace ModuleDesc. The
		// descriptors live in .data and the ACTIVE BYTE AND THE STATE INTEGER ARE
		// WRITTEN AT RUNTIME, so they are legitimate read targets.
		//
		// The table's .data initialiser is all 0xFFFFFFFF. Entries are filled by
		// ModuleRegister during ModuleSwitchGameMode, so a slot can hold 0, -1 or a real
		// pointer and every read has to tolerate all three.
		//
		// ModuleStepAll runs a slot's exec when (active byte or its shadow) is non-zero
		// AND the slot's bit is clear in ModuleSuspendMask. So the active mask alone
		// does not tell you a module is stepping. ModuleSuspendMask is zeroed at the top
		// of every MenuSysStepFrame, which makes a suspend last exactly one frame unless
		// something re-sets it.
		// ---------------------------------------------------------------------------
		const DWORD ModuleTable = 0x01440888;       // void *[25], index = module id
		const DWORD ModuleActiveMask = 0x014408EC;  // DWORD, bit N set while N runs
		const DWORD ModuleSuspendMask = 0x014408F0; // DWORD, bit N set skips N's exec
		const DWORD ModuleGameMode = 0x014408F8;    // DWORD, mode & 0x3FFFFFFF
		const DWORD ModuleDrawPrio = 0x01441C64;    // depth bias around start and stop

		const DWORD ModuleStart = 0x004AA100;          // void __cdecl (int id, int arg)
		const DWORD ModuleStop = 0x004AAB20;           // void __cdecl (int id)
		const DWORD ModuleSuspend = 0x004AAD70;        // void __cdecl (int id)
		const DWORD ModuleStepAll = 0x004AA200;        // int  __cdecl (void)
		const DWORD ModuleDrawAll = 0x004AA290;        // int  __cdecl (void)
		const DWORD ModuleGetActiveMask = 0x004AA460;  // DWORD __cdecl (void)
		const DWORD ModuleRegister = 0x004AAAB0;       // void __cdecl (int id, desc *)
		const DWORD ModuleSwitchGameMode = 0x004AA780; // void __cdecl (mode, arg)
		const DWORD ModuleSetDrawPrio = 0x004AB260;    // void __cdecl (int prio)

		// ---------------------------------------------------------------------------
		// The screens, by module id and by what they commit.
		//
		// Module 1 is the main menu top level. Every submenu calls ModuleStart(1, 0)
		// when it closes, and ModuleSwitchGameMode picks module 1 for game mode 0.
		//
		// MenuStartSubmoduleByResult maps the main menu's cursor selection to a module:
		// result 0 -> 3, 1 -> 2, 2 -> 4, 4 -> 6, 5 -> 7, 7 -> 9, 8 -> 10, 10 -> 15,
		// 15 -> 21, 20 -> 20. Module 1's own exec starts 19 and 11 directly.
		// ---------------------------------------------------------------------------
		const DWORD MenuExecModule1Main = 0x004E0350;        // the main menu top level
		const DWORD MenuStartSubmoduleByResult = 0x004E24C0; // cursor result -> module
		const DWORD MenuExecModule19SphereGrid = 0x004E26D0; // module 19's exec

		// Per-screen execs that were positively identified. Each comment says what
		// proved it, because an unproved screen name is worse than no name.
		const DWORD MenuExecModule2Ability = 0x004C60B0;    // writes current MP
		const DWORD MenuExecModule3Customise = 0x004C5450;  // AddItem + SwapEquipEntries
		const DWORD MenuExecModule4Equip = 0x004CF020;      // SetCharEquip, KEEPHP:%d
		const DWORD MenuExecModule7Item = 0x004CC330;       // AddItem, TK:SMN:Learn
		const DWORD MenuExecModule10Config = 0x004CB0E0;    // the row table, env reload
		const DWORD MenuExecModule11SaveFront = 0x004BEF50; // sa_menu_*, suspend j_sa_menu_stop
		const DWORD MenuExecModule12Shop = 0x004D6E40;      // SpendGil + RemoveEquipEntry
		const DWORD MenuExecModule15SaveLoad = 0x004B15D0;  // saves/ and .SAV, shared with 16
		const DWORD MenuExecModule21Overdrive = 0x004D0460; // TK:SND:OVERDRIVE, record+0x38

		// ---------------------------------------------------------------------------
		// THE SPHERE GRID. Module 19, proved rather than inferred.
		//
		// The proof is the string "Got ABMAP pad input!!!" referenced from module 19's
		// subtree (abmap is the ability map, which is what the sphere grid is called
		// internally), plus "setSpheConePrim error MAXSPHECONE=%d", plus the fact that
		// the only caller in the whole binary of FFX_SaveData_SpendSphereLevels sits in
		// the same 0x65xxxx page-handler family that module 19's builder owns.
		//
		// MenuStep is reached as module 19's exec -> state 1 -> MenuStep every frame.
		// Both globals below are heap pointers, so both need a Readable check.
		// ---------------------------------------------------------------------------
		const DWORD MenuStep = 0x00653570;  // the real per-frame step for module 19
		const DWORD MenuBuild = 0x00654B40; // the one-shot builder, 18 addMenuPrim sites
		const DWORD MenuWork = 0x01F05834;  // void *, the big per-menu work block
		const DWORD MenuPages = 0x01686108; // void *, 12 x 832 byte page records

		// Sphere grid behaviour that a gate or a detector attaches to.
		const DWORD MenuSphereGridActivateNode = 0x00656160; // charges a node, see below
		const DWORD MenuSphereGridSwitchChar = 0x00658EC0;   // L1 and R1 change character
		const DWORD MenuSphereGridReadPad = 0x00657520;      // its own FRAME-COUNTED repeat

		// ---------------------------------------------------------------------------
		// THE PER-CHARACTER GATE, which is the answer to the Sphere Grid ownership
		// requirement.
		//
		// There is one menu-wide answer to "which character is the cursor on", and it is
		// two plain statics plus a getter with 46 call sites. Every screen that acts on
		// one person goes through it: the Abilities screen, the Equip screen, the Sphere
		// Grid character switch and the HP and MP writers.
		//
		//   MenuCursorChar() == MenuCharList[MenuCharCursor]
		//
		// The value is a character record index 0..17, the same identity GameState.h
		// calls a character index. No pointer chase, no allocation, so it is cheap and
		// safe to read every frame.
		//
		// The Sphere Grid additionally keeps its own copy at MenuWork + 71100, written
		// by MenuSphereGridSwitchChar from the cursor getter. That is the byte
		// FFX_SaveData_SpendSphereLevels is charged against, so it is the one to audit.
		// ---------------------------------------------------------------------------
		const DWORD MenuCharList = 0x01441C14;       // BYTE[], character record indices
		const DWORD MenuCharListCountA = 0x01441C1C; // int, used when the alt flag is 0
		const DWORD MenuCharListCountB = 0x01441C20; // int, used otherwise
		const DWORD MenuCharCursor = 0x01441C28;     // int, index into MenuCharList
		const DWORD MenuCharCursorDir = 0x01440886;  // BYTE, 1 forward, 0xFF back

		const DWORD MenuGetCursorChar = 0x004A9860;    // int __cdecl (void), 46 callers
		const DWORD MenuGetCharListCount = 0x004A9B30; // int __cdecl (void)
		const DWORD MenuCursorCharPrev = 0x004AAFA0;   // L1
		const DWORD MenuCursorCharNext = 0x004AAF10;   // R1

		// ---------------------------------------------------------------------------
		// WHERE THE MENU COMMITS TO THE SAVE BLOCK.
		//
		// This is the result that decides whether input replication is enough, so it is
		// worth stating plainly: NO FUNCTION IN THE MENU TOUCHES THE SAVE BLOCK BY
		// ADDRESS. Every write goes through a named FFX_SaveData_* function or through
		// one of three thin menu wrappers that call FFX_SaveData__getCharRecord and
		// store one field. That was checked two ways, by enumerating every direct
		// reference into the 26,816-byte save run and by enumerating every caller of
		// FFX_GetSaveData, and the only menu-range hits are debug and dev-harness code
		// plus one config toggle.
		//
		// So a desync detector has about a dozen attachment points, not a memory watch.
		// The ones the in-game menu actually reaches:
		// ---------------------------------------------------------------------------
		const DWORD SaveDataSwapEquipEntries = 0x003AB9F0; // the equipment re-sort
		const DWORD MenuSetCharCurrentHp = 0x004AAE90;     // record+0x1C, clears +0x3D
		const DWORD MenuSetCharCurrentMp = 0x004AAEF0;     // record+0x20
		const DWORD MenuSetCharRecordByte38 = 0x004C2C90;  // record+0x38, module 21 only
		const DWORD MenuConfigRowSetSaveBit = 0x004CB0B0;  // saveData+0x5EC bit 0

		// FFX_SaveData_AddItem 0x00390550 is the inventory commit and is deliberately
		// NOT redeclared here, because addresses\GameState.h already owns it. The menu
		// reaches it from the Item screen, the Customise screen, both shops and one
		// 0x65xxxx menu helper.

		// ---------------------------------------------------------------------------
		// The Config screen's row table, because config is the one screen a co-op build
		// may reasonably let both players run independently.
		//
		// Rows live at ConfigRows as an array of pointers. Per row: +4 value count,
		// +8 current value index, +12 selectable flag, +20 the apply callback. Only one
		// row reaches the save block, and that is MenuConfigRowSetSaveBit above.
		// Everything else is a local preference.
		// ---------------------------------------------------------------------------
		const DWORD MenuConfigRows = 0x0146A44C;      // row pointer array
		const DWORD MenuConfigRowCount = 0x0146A448;  // int
		const DWORD MenuConfigCursorRow = 0x0146A444; // BYTE

		// ---------------------------------------------------------------------------
		// Every address above, for the startup build check. VerifyLayout walks this so
		// a typo is caught at startup rather than by a fault later.
		//
		// Keep it in step with the constants. If you add an address and forget this
		// list, nothing breaks today and something breaks confusingly in a year.
		// ---------------------------------------------------------------------------
		// The five save-block commit functions the menu drives are declared in
		// addresses/GameState.h, which owns that subsystem. Both passes derived the
		// same five addresses independently. Which one belongs to which screen is the
		// part this pass added, and it is recorded on each of them there:
		//
		//   SaveDataSetCharEquip          the equipment screen's commit
		//   SaveDataSpendSphereLevels     the sphere grid's commit, one caller only
		//   SaveDataAddEquipEntry         a shop buy
		//   SaveDataRemoveEquipEntry      a shop sell
		//   SaveDataSpendGil              shops
		//
		// That short list is the whole menu's write surface. No menu function touches
		// the save block by address.

		inline const DWORD* MenuSystemRvaList(int* count)
		{
			static const DWORD list[] = {
				MenuSysPollOpenAndStep,
				MenuSysRequestOpen,
				MenuSysEnter,
				MenuSysRunFrame,
				MenuSysStepFrame,
				SaveUiIsIdle,
				MenuRequestedMode,
				MenuEnterResult,
				MenuEnterMode,
				MenuGetEnterMode,

				MenuPadBlock,
				MenuSysSamplePad,
				MenuSysClearPad,
				MenuSysGetHeld,
				MenuSysGetRepeat,
				MenuSysGetPressed,
				MenuSysGetHeldSticky,
				MenuSysGetPressedSticky,
				MenuSysGetWord10Sticky,
				MenuSysReadAnalogByte,

				VirtualPadEnabled,
				VirtualPadButtons,
				VirtualPadButtonsNext,
				VirtualPadAxis0,
				VirtualPadAxis0Next,
				VirtualPadAxis1,
				VirtualPadAxis1Next,
				VirtualPadAxis2,
				VirtualPadAxis2Next,
				VirtualPadAxis3,
				VirtualPadAxis3Next,
				VirtualPadIsEnabled,
				VirtualPadSetEnabled,
				VirtualPadSetButtons,
				VirtualPadSetAxis,
				VirtualPadGetButtons,
				VirtualPadGetAxis,
				VirtualPadLatch,
				VirtualPadReset,

				ModuleTable,
				ModuleActiveMask,
				ModuleSuspendMask,
				ModuleGameMode,
				ModuleDrawPrio,
				ModuleStart,
				ModuleStop,
				ModuleSuspend,
				ModuleStepAll,
				ModuleDrawAll,
				ModuleGetActiveMask,
				ModuleRegister,
				ModuleSwitchGameMode,
				ModuleSetDrawPrio,

				MenuExecModule1Main,
				MenuStartSubmoduleByResult,
				MenuExecModule19SphereGrid,
				MenuExecModule2Ability,
				MenuExecModule3Customise,
				MenuExecModule4Equip,
				MenuExecModule7Item,
				MenuExecModule10Config,
				MenuExecModule11SaveFront,
				MenuExecModule12Shop,
				MenuExecModule15SaveLoad,
				MenuExecModule21Overdrive,

				MenuStep,
				MenuBuild,
				MenuWork,
				MenuPages,
				MenuSphereGridActivateNode,
				MenuSphereGridSwitchChar,
				MenuSphereGridReadPad,

				MenuCharList,
				MenuCharListCountA,
				MenuCharListCountB,
				MenuCharCursor,
				MenuCharCursorDir,
				MenuGetCursorChar,
				MenuGetCharListCount,
				MenuCursorCharPrev,
				MenuCursorCharNext,

				SaveDataSwapEquipEntries,
				MenuSetCharCurrentHp,
				MenuSetCharCurrentMp,
				MenuSetCharRecordByte38,
				MenuConfigRowSetSaveBit,

				MenuConfigRows,
				MenuConfigRowCount,
				MenuConfigCursorRow,
			};
			if (count)
				*count = (int)(sizeof(list) / sizeof(list[0]));
			return list;
		}

	} // namespace Rva
} // namespace ffx
