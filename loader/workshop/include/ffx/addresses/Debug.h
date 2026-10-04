#pragma once

#include <windows.h>

// The game's own debug mode, its two debug menus, and the loose cheat functions.
//
// Derived in reversing\DEBUG_MODE.md. Read that before wiring any of this up, in particular its
// section 6 on what is broken and section 9 on what is not settled.
//
// THERE ARE TWO MENUS AND THEY ARE UNRELATED.
//
//  1. DebugMenuManager. A Phyre-based overlay the HD Remaster team ported. Its singleton is
//     created on every normal boot by FFX_GraphicInitialize, and all nine of its pages still
//     draw every frame from FFX_Frame_PresentScene the moment a page is enabled. Pages include
//     a warp menu, a battle test launcher, a live stat editor and a cheat page. RECOMMENDED.
//
//  2. SG_DebugGui. The original PS2 developer window system, 95 window procedures, a root
//     window with seven tabs (CHR, MAP, SYS, BTL, CAM, EFF, EVENT), a real text renderer with
//     its own escape codes. Its draw pump and its input pump are BOTH still called from the
//     main loop behind g_ffxDebugMode.
//
// WHAT IS BROKEN, in both cases the same shape: the call was removed, the code stayed. There is
// no nullsub and no "return a constant" anywhere in either menu, so nothing needs patching.
//
//  * DebugMenuManagerSetMode has exactly one caller in the binary and that caller is
//    DebugMenuManagerClose. So nothing ever opens a page. Call it yourself.
//  * All eight per-page input handlers below are ORPHANED. Zero call rel32 and zero absolute
//    pointers anywhere in the image, proven by a raw byte sweep of every segment. The bodies are
//    intact. Call the right one once per frame with the page singleton and the Phyre application
//    and the page becomes navigable.
//  * SgDebugGuiOpenOrphan is likewise unreferenced, but it is a complete, stack-balanced,
//    no-argument cdecl function. Call it once and the SG root window opens.
//  * The SG GUI is MOUSE driven, and its pointer device read was stubbed to xor eax,eax / ret.
//    The read reports success and fills nothing, so the cursor can never move. Fix it by writing
//    the three dwords the read should have filled, see SgDebugGuiDevButtons below. Do NOT patch
//    the stub itself, it is shared with the save and file device paths.
//
// THE MINIMUM SEQUENCE for the PC menu:
//
//    *(BYTE*)(base + Rva::DebugMode) = 1;                  // WorldState.h owns that address
//    void* mgr  = DebugMenuManagerGet();                   // non null after graphics init
//    DebugMenuManagerSetMode(mgr, 7);                      // __thiscall, 7 = MapSwitching
//    // then every frame:
//    void* page = DebugPageGetMapSwitching();
//    DebugPageMapSwitchingInput(page, PhyreGetApplication());
//
// Mode numbers for SetMode: 5 Character Texture Animation, 6 Basic Debug Information,
// 7 MapSwitching, 8 battle character parameter config, 9 Game parameter config, 10 off.
// Modes 0 to 4 are real pages whose identity is not settled yet.

namespace ffx
{
	namespace Rva
	{
		// ---------------------------------------------------------------------------
		// The debug flag itself.
		//
		// NOT DECLARED HERE. g_ffxDebugMode 0xF3C910 lives in WorldState.h as Rva::DebugMode,
		// which found it first for the MapWarpTo gate. Three instructions in the whole binary
		// write it: an argv[1] == "debug" test in ParseCommandLineAndInitDebug, a reset inside
		// the MainStep region, and SetDebugModeOrphan below. No ini, no env var, no config key.
		// A plugin should just write the byte.
		// ---------------------------------------------------------------------------

		const DWORD IsDebugMode = 0x00422550;    // int (void), returns the byte. 8 callers.
		const DWORD IsDebugModeDup = 0x00487C50; // the same function again. 25 callers, including
		                                         // FFX_Map_WarpTo and the battle init path.

		// void __cdecl (int on). The only real setter. Refuses when the byte at
		// DebugSetterGate is clear, normalises the argument to 0 or 1. ZERO callers, so it
		// exists only for a mod. Writing the flag directly is equivalent and simpler.
		const DWORD SetDebugModeOrphan = 0x00487C80;

		// int __cdecl (int argc, char** argv). Sets the flag when argv[1] string-compares equal
		// to "debug" or "DEBUG", case sensitive, then runs InitSubsystems1 and MainInit.
		const DWORD ParseCommandLineAndInitDebug = 0x00422490;

		// byte, 1 in the loaded image. Gates SetDebugModeOrphan and nothing else that matters.
		const DWORD DebugSetterGate = 0x0075EC4F;

		// ---------------------------------------------------------------------------
		// DebugMenuManager, the live PC debug menu.
		// ---------------------------------------------------------------------------

		const DWORD DebugMenuManagerSingleton = 0x008CCAB0; // void*, created at boot
		const DWORD DebugMenuManagerCtor = 0x002B4900;
		const DWORD DebugMenuManagerCreateSingleton = 0x002B4950;
		const DWORD DebugMenuManagerDestroySingleton = 0x002B4990;
		const DWORD DebugMenuManagerGet = 0x002B49B0;     // void* (void)
		const DWORD DebugMenuManagerGetMode = 0x002B4A80; // int __thiscall (this)

		// char __thiscall (this, int mode). THE ENTRY POINT. mode %= 11, all ten pages off, then
		// the selected one on. Its only caller is Close, so a mod has to call it.
		const DWORD DebugMenuManagerSetMode = 0x002B4D10;
		const DWORD DebugMenuManagerClose = 0x002B4A70; // SetMode(10)

		// int __stdcall (float fps). Called every frame from FFX_Frame_UpdateBoostersAndOverlays.
		// The one draw path in this menu that really does test the debug flag, and it only fires
		// for mode 5.
		const DWORD DebugMenuDrawIfActive = 0x00272000;

		// Called every frame from FFX_Frame_PresentScene. Draws all nine pages, each gated only
		// on its own enabled byte, NOT on the debug flag.
		const DWORD FramePresentDebugPageDraw = 0x00268930;

		// Calls all eight label initialisers once, from the boot path.
		const DWORD DebugPageInitAllLabels = 0x00257A50;

		const DWORD DebugMenuMode5Overlay = 0x002BC210;  // int (float), the mode 5 extra overlay
		const DWORD DebugPageRefreshLine = 0x002BC410;   // int (int row), 20 call sites
		const DWORD DebugInfoState = 0x008CCAC0;         // shared page state block
		const DWORD DebugPageGetInfoState = 0x002B6370;

		// ---------------------------------------------------------------------------
		// The nine page singletons. Each global holds a pointer, each getter is six bytes.
		// ---------------------------------------------------------------------------

		const DWORD DebugPageBasicInfo = 0x008CCAD0;
		const DWORD DebugPageBattleCharParams = 0x008CCAE0;
		const DWORD DebugPageBattleInfo = 0x008CCAD8;
		const DWORD DebugPageCharAnim = 0x008CCADC;
		const DWORD DebugPageCharSwitching = 0x008CCAC4;
		const DWORD DebugPageCharTexAnim = 0x008CCAD4;
		const DWORD DebugPageGameParams = 0x008CCAE4;
		const DWORD DebugPageMapSwitching = 0x008CCAC8;

		const DWORD DebugPageGetBasicInfo = 0x002B62F0;
		const DWORD DebugPageGetBattleCharParams = 0x002B6300;
		const DWORD DebugPageGetBattleInfo = 0x002B6310;
		const DWORD DebugPageGetCharAnim = 0x002B6320;
		const DWORD DebugPageGetCharSwitching = 0x002B6330;
		const DWORD DebugPageGetCharTexAnim = 0x002B6340;
		const DWORD DebugPageGetGameParams = 0x002B6350;
		const DWORD DebugPageGetMapSwitching = 0x002B6360;

		// ---------------------------------------------------------------------------
		// THE ORPHANED INPUT HANDLERS. All of them void __thiscall (page, Phyre::PApplication*).
		// Nothing calls them. Call one per frame to restore its page.
		//
		// Every one of them reads input the same way, with Phyre key enum values and not Win32
		// VK codes: keys 36 and 38 through ConsumeKeyDown move the cursor up and down, keys 37
		// and 39 through IsKeyDown change the value on the selected row, pad axis 3 is the
		// vertical and axis 2 the horizontal with a 0.5 deadzone, pad buttons 23/25 move and
		// 26/24 change. Key 77 with pad button 13 is the confirm on the Basic Info page.
		// ---------------------------------------------------------------------------

		// THE ENABLED BYTE IS AT A DIFFERENT OFFSET ON EVERY PAGE, measured out of the raw
		// disassembly rather than taken as uniform, and THREE of the eight also call
		// IsDebugMode and bail when the flag is clear. The offsets and the gate:
		//
		//   CharAnim        page+192  DEBUG MODE REQUIRED
		//   CharTexAnim     page+208
		//   CharSwitching   page+212  DEBUG MODE REQUIRED
		//   GameParams      page+232
		//   BattleCharParams page+252
		//   BasicInfo       page+256
		//   BattleInfo      page+452
		//   MapSwitching    page+500  DEBUG MODE REQUIRED
		//
		// So for the warp page, enabling it is not enough, the flag has to be on too.
		const int DebugPageEnabledOffCharAnim = 192;
		const int DebugPageEnabledOffCharTexAnim = 208;
		const int DebugPageEnabledOffCharSwitching = 212;
		const int DebugPageEnabledOffGameParams = 232;
		const int DebugPageEnabledOffBattleCharParams = 252;
		const int DebugPageEnabledOffBasicInfo = 256;
		const int DebugPageEnabledOffBattleInfo = 452;
		const int DebugPageEnabledOffMapSwitching = 500;

		const DWORD DebugPageBasicInfoInput = 0x002B7630;        // 13 rows, encounters, CRC skip
		const DWORD DebugPageBattleCharParamsInput = 0x002B7AF0; // the live stat editor
		const DWORD DebugPageBattleInfoInput = 0x002B81D0;       // battle test, god mode
		const DWORD DebugPageCharAnimInput = 0x002B85C0;
		const DWORD DebugPageCharSwitchingInput = 0x002B8820; // model swap, spawns a CHR
		const DWORD DebugPageCharTexAnimInput = 0x002B8A20;
		const DWORD DebugPageGameParamsInput = 0x002B8CE0;   // the cheat page
		const DWORD DebugPageMapSwitchingInput = 0x002B9160; // the warp page

		// ---------------------------------------------------------------------------
		// The per-frame draw functions, all live. int __thiscall (page, window, float fps).
		// ---------------------------------------------------------------------------

		const DWORD DebugPageBasicInfoDraw = 0x002BA1A0;
		const DWORD DebugPageBattleCharParamsDraw = 0x002BA240;
		const DWORD DebugPageBattleInfoDraw = 0x002BA2E0;
		const DWORD DebugPageCharAnimDraw = 0x002BA390;
		const DWORD DebugPageCharSwitchingDraw = 0x002BA430;
		const DWORD DebugPageCharTexAnimDraw = 0x002BA4D0;
		const DWORD DebugPageGameParamsDraw = 0x002BA570;
		const DWORD DebugPageMapSwitchingDraw = 0x002BA610;

		// The cursor movers, int __cdecl (int dir) where 0 is up and 1 is down. Only the orphaned
		// input handlers call these, so they are a second way in if you would rather drive the
		// cursor yourself than replay the whole input handler.
		const DWORD DebugPageBasicInfoMoveCursor = 0x002BA8C0;
		const DWORD DebugPageBattleCharParamsMoveCursor = 0x002BA9B0;
		const DWORD DebugPageBattleInfoMoveCursor = 0x002BAAA0;
		const DWORD DebugPageCharAnimMoveCursor = 0x002BAB90;
		const DWORD DebugPageCharSwitchingMoveCursor = 0x002BAC60;
		const DWORD DebugPageCharTexAnimMoveCursor = 0x002BAD30;
		const DWORD DebugPageGameParamsMoveCursor = 0x002BAE00;
		const DWORD DebugPageMapSwitchingMoveCursor = 0x002BAEF0;

		// ---------------------------------------------------------------------------
		// The enable functions SetMode dispatches to. char (int on) unless noted.
		//
		// WARNING. The IDB name FFX_DebugPage_battleCharParams_enable on 0x2BB6D0 is WRONG. That
		// is the mode 5 Character Texture Animation page. The real battle character parameter
		// page is mode 8, DebugPageBattleCharParamsEnable below.
		// ---------------------------------------------------------------------------

		const DWORD DebugPageEnableMode0 = 0x002AA5F0; // page identity not settled
		const DWORD DebugPageEnableMode1 = 0x002984F0; // page identity not settled
		const DWORD DebugPageEnableMode2 = 0x002BB670; // page identity not settled
		const DWORD DebugPageEnableMode3 = 0x002BB880; // page identity not settled
		const DWORD DebugPageEnableMode4 = 0x002BB5F0; // page identity not settled
		const DWORD DebugPageCharTexAnimEnable = 0x002BB6D0;     // mode 5, __thiscall (page, on)
		const DWORD DebugPageBasicInfoEnable = 0x002BAF60;       // mode 6
		const DWORD DebugPageMapSwitchingEnable = 0x002BB480;    // mode 7
		const DWORD DebugPageBattleCharParamsEnable = 0x002BB3B0; // mode 8, the REAL stat page
		const DWORD DebugPageGameParamsEnable = 0x002BB7C0;      // mode 9, __thiscall (page, on)

		// ---------------------------------------------------------------------------
		// The label initialisers. These hold the menu text, which is the fastest way to see what
		// each page can do without decompiling the input handler.
		// ---------------------------------------------------------------------------

		const DWORD DebugPageBasicInfoInitLabels = 0x002B9360;        // 13 labels
		const DWORD DebugPageBattleCharParamsInitLabels = 0x002B94A0; // 17 labels, HP..LoveParam
		const DWORD DebugPageBattleInfoInitLabels = 0x002B9600;       // "player never die" etc
		const DWORD DebugPageCharAnimInitLabels = 0x002B97F0;
		const DWORD DebugPageCharSwitchingInitLabels = 0x002B9970; // the 7 CHR category letters
		const DWORD DebugPageCharTexAnimInitLabels = 0x002B9AA0;
		const DWORD DebugPageGameParamsInitLabels = 0x002B9CD0;    // FullItem, FullGill, ...
		const DWORD DebugPageMapSwitchingInitLabels = 0x002B9E20;  // the 42 map area prefixes

		const DWORD DebugPageBasicInfoFormatLine = 0x002B69A0;
		const DWORD DebugPageBattleCharParamsFormatLine = 0x002B6BB0;
		const DWORD DebugPageGameParamsFormatLine = 0x002B72E0;

		// ---------------------------------------------------------------------------
		// SG_DebugGui, the PS2 developer window system.
		// ---------------------------------------------------------------------------

		// void __cdecl (void). ORPHANED, and the way in. Runs SG_InitProtocolDriver (failure is
		// harmless, the error branch only calls a one-byte stub), then opens the root window at
		// x 0x1B0, y 0, w 0x50, h 0x52 with SgDebugGuiRootWindowProc, then nine state resets.
		// Stack balanced, no prologue, no arguments. Set the debug flag first.
		const DWORD SgDebugGuiOpenOrphan = 0x0044F100;

		const DWORD SgDebugGuiRootWindowProc = 0x0044F550; // seven tabs, 3607 bytes
		const DWORD SgDebugGuiOpenWindow = 0x0044A920;     // void* (x, y, w, h, flags, proc)
		const DWORD SgDebugGuiOpenSubWindow = 0x003C86E0;  // about eleven sub windows by index

		// Both of these are LIVE, called once per frame from DrawDebugOverlay and StepPacing
		// respectively, each behind a plain test of the debug flag and nothing else.
		const DWORD SgDebugGuiDrawAll = 0x00449850;
		const DWORD SgDebugGuiUpdateInput = 0x0044A460;

		// int __cdecl (void). The pointer device poll, and the only genuinely broken thing in the
		// SG GUI. SgDevDeviceReadStubbed bottoms out in SgDevRemoteReadStubRet0, which is three
		// bytes, 33 C0 C3. So the read says success and fills nothing.
		const DWORD SgDebugGuiPollPointerDevice = 0x00451920;
		const DWORD SgDevDeviceReadStubbed = 0x00455BC0;
		const DWORD SgDevRemoteReadStubRet0 = 0x00644750;

		// int __cdecl (int x, int y). Sets the cursor and both float accumulators consistently.
		// The clean way to drive the cursor absolutely instead of by delta.
		const DWORD SgDebugGuiSetCursorPos = 0x00451AE0;

		// int __cdecl (void). Turns SgDebugGuiButtons into the flags the window procs test:
		// 1 / 0x100 / 0x10000 held for left, middle, right, 2 / 0x200 / 0x20000 pressed this
		// frame, 4 / 0x400 / 0x40000 released, 8 double click.
		const DWORD SgDebugGuiGetMouseEdgeFlags = 0x004517C0;
		const DWORD SgDebugGuiGetCursorX = 0x00451750;
		const DWORD SgDebugGuiGetCursorY = 0x00451760;

		const DWORD SgDebugGuiCursorX = 0x00F25240;      // int, 0..512
		const DWORD SgDebugGuiCursorY = 0x00F25244;      // int, 0..416
		const DWORD SgDebugGuiCursorAccumX = 0x00F25248; // float 0..1024, cursorX = 0.5 * this
		const DWORD SgDebugGuiCursorAccumY = 0x00F2524C; // float 0..1248, cursorY = this / 3
		const DWORD SgDebugGuiButtons = 0x00F25260;      // int, copied from DevButtons each frame

		// The 32-byte destination of the stubbed device read. Exactly one reference in the whole
		// binary, the push offset inside the poll above, and the three fields below have no
		// writer anywhere. THIS IS THE REPAIR POINT: write the real mouse into these three every
		// frame before the game steps and the SG GUI becomes usable. Nothing in the engine
		// competes for them. Remember the scaling, X is halved and Y divided by three on the way
		// to the cursor.
		const DWORD SgDebugGuiDeviceBuf = 0x00F25280;
		const DWORD SgDebugGuiDevButtons = 0x00F25288; // int, bit0 left, bit1 middle, bit2 right
		const DWORD SgDebugGuiDevDeltaX = 0x00F2528C;  // int, pixels this frame
		const DWORD SgDebugGuiDevDeltaY = 0x00F25290;  // int, pixels this frame

		const DWORD SgDebugGuiTextLineY = 0x00F25920; // Printf line cursor, 7 px per line
		const DWORD DebugMapJumpTarget = 0x00F25928;  // the SG warp menu's selected map

		const DWORD SgDebugGuiPrintf = 0x00453E40; // int (const char*, ...), one line

		// void __usercall (int, int item). On item 44 it reads the selected map out of the list
		// widget, stores it in DebugMapJumpTarget and calls FFX_Map_WarpTo(target, 0). The PS2
		// debug menu's warp.
		const DWORD SgDebugGuiMapJumpItemProc = 0x00454DF0;

		// int __cdecl (CHR* chr). Calls FFX_Ch_SetPlayerChr(chr) and then opens a per-CHR window.
		// Line one is the whole prize, "possess this actor". If that is all you want, call
		// FFX_Ch_SetPlayerChr directly, everything after line one is window dressing.
		const DWORD SgDebugWinOpenChrAndTakeControl = 0x0044CEA0;

		const DWORD SgDebugGuiOpenChrInfo = 0x004537B0; // the CHR field dump window
		const DWORD SgDebugGuiFillChrList = 0x0044CE10;
		const DWORD SgDebugGuiFillChrList2 = 0x00453DC0;
		const DWORD SgDebugWinAddWidget = 0x0044A470; // about 400 call sites
		const DWORD SgDebugWinSetTitle = 0x0044B6A0;
		const DWORD SgDebugWinSetTitleRaw = 0x00449610;

		const DWORD SgPrintfStub = 0x0022F500; // one byte, C3. Every SG console print is a no-op.

		// void __cdecl (int, int, const char*). Real code, but it prefixes the path with "host0:",
		// the PS2 dev-host prefix, so MemDump, MotDump, LgtDump and Capture produce nothing
		// without redirecting that prefix. Failure raises a yiAssert, it does not crash.
		const DWORD SgPcWrite = 0x0043AFE0;

		// ---------------------------------------------------------------------------
		// The two main-loop doors into the SG GUI, and their secondary gates.
		// ---------------------------------------------------------------------------

		// int __cdecl (void). Called unconditionally from FFX_MainStep. Inside, three conditions
		// have to hold before SgDebugGuiDrawAll runs: the debug flag is set, sub_83A620 returns
		// zero (both of its globals are in the zero-filled tail of .data so they start at 0), and
		// SgDrawEnable is non zero (MainInit sets it to 1 outside any debug gate). So in practice
		// setting the debug flag is enough.
		const DWORD DrawDebugOverlay = 0x004207F0;

		// NOT DECLARED HERE. FFX_StepPacing 0x421E80 lives in MainLoop.h as Rva::StepPacing. It
		// calls SgDebugGuiUpdateInput at 0x42222D behind a two-instruction test of the debug flag.

		const DWORD SgDrawEnable = 0x00EFBBB3;    // byte, 1 after MainInit
		const DWORD SetSgDrawEnable = 0x00420420; // int (char), writes the byte above

		// The other half of the FFX_MainStep debug gate, and a dead end. EffectDebugTick is 28
		// bytes and only bumps two effect-system counters. It draws nothing and needs nothing.
		const DWORD EffectDebugTickEnable = 0x00EFBBB8; // byte, 1 after MainInit
		const DWORD EffectDebugTick = 0x003E6560;

		// ---------------------------------------------------------------------------
		// The cheat functions. No arguments, no gate, they write the live save data. These are
		// the cheapest thing in this whole file to use.
		// ---------------------------------------------------------------------------

		const DWORD DebugFullGil = 0x003849C0;         // 999999999
		const DWORD DebugFullItem = 0x003849D0;        // 99 of all 112 items
		const DWORD DebugOneOfEveryItem = 0x00384C20;  // 1 of all 112
		const DWORD DebugClearInventory = 0x00384CD0;  // all 112 slots to empty
		const DWORD DebugFullSphereLevels = 0x00384AE0; // record[0x3B] = 98, all 18 records
		const DWORD DebugMaxHpMpAndStats = 0x00384B00; // 99999 HP, 9999 MP, stats to 0xFF

		// void __cdecl (int id), id 1..113. Loads
		// <dataroot>/FFX_Data/GameData/PS3Data/savesforviewer/<id> with a raw fopen, not through
		// the VBF, memcpys 0x68C0 bytes over the live save block, then warps to the map that save
		// names. If those files ship this is a one-call jump to any point in the game.
		const DWORD DebugApplyViewerSave = 0x004B55E0;
		const DWORD DebugLoadSaveForViewer = 0x00249040; // the fopen under it

		// void __cdecl (int on). Writes Rva::DebugEncountersOn, which Encounter.h owns. The
		// Basic Info page's "Battle Enable" row calls this.
		const DWORD BtlSetRandomEncountersEnabled = 0x00382F70;

		const DWORD SaveCrcSkip = 0x008CB9A4; // byte, the "Disable CRC check" row
		const DWORD Show43Frame = 0x00D36FC0; // int, the "Show 4:3 Frame" row, three states

		// NOT DECLARED HERE. FFX_Blitz_DebugFullBlitz 0x3845B0, g_ffxBlitzCheatEnabled 0x8CCACC
		// and g_ffxChocoboGameDebugEnable 0x8CCAB8 live in Minigames.h, which owns the minigame
		// cheats. g_ffxDebugEncountersOn 0x8421CC lives in Encounter.h. g_ffxDebugCharNames
		// 0x83432C lives in GameState.h. FFX_Ch_DebugSpawnByName 0x4295E0 lives in Character.h.

		// NOT DECLARED HERE EITHER. FFX_Debug_IsChocoboGameDebugEnabled 0x2BC960 is already in
		// Minigames.h as Rva::DebugIsChocoboGameDebugEnabled.

		// ---------------------------------------------------------------------------
		// Shipped noclip, and the dead leftovers.
		// ---------------------------------------------------------------------------

		// int (void). FFX_Ch_WalkmeshMove combines this with the is-player CHR flag to let the
		// player leave the walkmesh. FFX_Player__readPad sets the global, and that write is
		// itself behind the debug flag, so noclip needs debug mode on.
		const DWORD PlayerIsDebugNoClipHeld = 0x0042D830;
		const DWORD PlayerDebugNoClip = 0x01FC44B4;

		const DWORD ChIdToDebugName = 0x00438060;
		const DWORD ViewerManagerCtor = 0x002B5870;            // a model viewer
		const DWORD ViewerManagerSetCharacterByName = 0x002B66D0; // sole caller of the CHR spawn

		// char __cdecl (void). 6313 bytes, zero callers, no pointer anywhere. The ATEL event
		// debug screen, strings TK:ATEL_DEBUG, INFO WAVE, INFO SE, INFO MUSIC. It navigates off
		// pad PORT 1, so the developers drove it with controller two. Hex-Rays refuses it.
		// Reviving this is a project of its own, do not start here.
		const DWORD AtelDebugScreenOrphan = 0x0047BD90;

		// Orphaned 52-byte pollers for raw Phyre keys I and J. They only set an edge flag that
		// nothing reads, so restoring them buys nothing on their own.
		const DWORD BoosterPollDebugKeyI = 0x002F7640;
		const DWORD BoosterPollDebugKeyJ = 0x002F7680;
		const DWORD BoosterDebugKeyIEdge = 0x008E82D8;
		const DWORD BoosterDebugKeyJEdge = 0x008E82DC;

		const DWORD DebugCharModeNames = 0x00834318; // const char*[]
		const DWORD DebugAuthorNames = 0x008448F0;   // const char*[]

		// ---------------------------------------------------------------------------
		// The Phyre input entry points the orphaned handlers need. A plugin that calls one of
		// those handlers has to hand it the application pointer, and these are how the handlers
		// read the keyboard and pad.
		//
		// NOT DECLARED HERE. PhyreGetPadAxis 0x228460 and PhyreIsPadButtonDown 0x229CA0 live in
		// Input.h, which owns the pad layer.
		// ---------------------------------------------------------------------------

		const DWORD PhyreGetApplication = 0x00223B90; // void* (void)
		const DWORD PhyreConsumeKeyDown = 0x00226A00; // bool __thiscall (app, int key, int)
		const DWORD PhyreIsKeyDown = 0x00229D50;      // bool __thiscall (app, int key, int)

		// ---------------------------------------------------------------------------
		// Every constant above, for the layout check. See Addresses.h for the rule on
		// collisions: an address lives in the area that OWNS the thing, and the other area gets
		// a comment pointing here rather than a second declaration.
		// ---------------------------------------------------------------------------
		inline const DWORD* DebugRvaList(int* count)
		{
			static const DWORD list[] = {
			    IsDebugMode,
			    IsDebugModeDup,
			    SetDebugModeOrphan,
			    ParseCommandLineAndInitDebug,
			    DebugSetterGate,
			    DebugMenuManagerSingleton,
			    DebugMenuManagerCtor,
			    DebugMenuManagerCreateSingleton,
			    DebugMenuManagerDestroySingleton,
			    DebugMenuManagerGet,
			    DebugMenuManagerGetMode,
			    DebugMenuManagerSetMode,
			    DebugMenuManagerClose,
			    DebugMenuDrawIfActive,
			    FramePresentDebugPageDraw,
			    DebugPageInitAllLabels,
			    DebugMenuMode5Overlay,
			    DebugPageRefreshLine,
			    DebugInfoState,
			    DebugPageGetInfoState,
			    DebugPageBasicInfo,
			    DebugPageBattleCharParams,
			    DebugPageBattleInfo,
			    DebugPageCharAnim,
			    DebugPageCharSwitching,
			    DebugPageCharTexAnim,
			    DebugPageGameParams,
			    DebugPageMapSwitching,
			    DebugPageGetBasicInfo,
			    DebugPageGetBattleCharParams,
			    DebugPageGetBattleInfo,
			    DebugPageGetCharAnim,
			    DebugPageGetCharSwitching,
			    DebugPageGetCharTexAnim,
			    DebugPageGetGameParams,
			    DebugPageGetMapSwitching,
			    DebugPageBasicInfoInput,
			    DebugPageBattleCharParamsInput,
			    DebugPageBattleInfoInput,
			    DebugPageCharAnimInput,
			    DebugPageCharSwitchingInput,
			    DebugPageCharTexAnimInput,
			    DebugPageGameParamsInput,
			    DebugPageMapSwitchingInput,
			    DebugPageBasicInfoDraw,
			    DebugPageBattleCharParamsDraw,
			    DebugPageBattleInfoDraw,
			    DebugPageCharAnimDraw,
			    DebugPageCharSwitchingDraw,
			    DebugPageCharTexAnimDraw,
			    DebugPageGameParamsDraw,
			    DebugPageMapSwitchingDraw,
			    DebugPageBasicInfoMoveCursor,
			    DebugPageBattleCharParamsMoveCursor,
			    DebugPageBattleInfoMoveCursor,
			    DebugPageCharAnimMoveCursor,
			    DebugPageCharSwitchingMoveCursor,
			    DebugPageCharTexAnimMoveCursor,
			    DebugPageGameParamsMoveCursor,
			    DebugPageMapSwitchingMoveCursor,
			    DebugPageEnableMode0,
			    DebugPageEnableMode1,
			    DebugPageEnableMode2,
			    DebugPageEnableMode3,
			    DebugPageEnableMode4,
			    DebugPageCharTexAnimEnable,
			    DebugPageBasicInfoEnable,
			    DebugPageMapSwitchingEnable,
			    DebugPageBattleCharParamsEnable,
			    DebugPageGameParamsEnable,
			    DebugPageBasicInfoInitLabels,
			    DebugPageBattleCharParamsInitLabels,
			    DebugPageBattleInfoInitLabels,
			    DebugPageCharAnimInitLabels,
			    DebugPageCharSwitchingInitLabels,
			    DebugPageCharTexAnimInitLabels,
			    DebugPageGameParamsInitLabels,
			    DebugPageMapSwitchingInitLabels,
			    DebugPageBasicInfoFormatLine,
			    DebugPageBattleCharParamsFormatLine,
			    DebugPageGameParamsFormatLine,
			    SgDebugGuiOpenOrphan,
			    SgDebugGuiRootWindowProc,
			    SgDebugGuiOpenWindow,
			    SgDebugGuiOpenSubWindow,
			    SgDebugGuiDrawAll,
			    SgDebugGuiUpdateInput,
			    SgDebugGuiPollPointerDevice,
			    SgDevDeviceReadStubbed,
			    SgDevRemoteReadStubRet0,
			    SgDebugGuiSetCursorPos,
			    SgDebugGuiGetMouseEdgeFlags,
			    SgDebugGuiGetCursorX,
			    SgDebugGuiGetCursorY,
			    SgDebugGuiCursorX,
			    SgDebugGuiCursorY,
			    SgDebugGuiCursorAccumX,
			    SgDebugGuiCursorAccumY,
			    SgDebugGuiButtons,
			    SgDebugGuiDeviceBuf,
			    SgDebugGuiDevButtons,
			    SgDebugGuiDevDeltaX,
			    SgDebugGuiDevDeltaY,
			    SgDebugGuiTextLineY,
			    DebugMapJumpTarget,
			    SgDebugGuiPrintf,
			    SgDebugGuiMapJumpItemProc,
			    SgDebugWinOpenChrAndTakeControl,
			    SgDebugGuiOpenChrInfo,
			    SgDebugGuiFillChrList,
			    SgDebugGuiFillChrList2,
			    SgDebugWinAddWidget,
			    SgDebugWinSetTitle,
			    SgDebugWinSetTitleRaw,
			    SgPrintfStub,
			    SgPcWrite,
			    DrawDebugOverlay,
			    SgDrawEnable,
			    SetSgDrawEnable,
			    EffectDebugTickEnable,
			    EffectDebugTick,
			    DebugFullGil,
			    DebugFullItem,
			    DebugOneOfEveryItem,
			    DebugClearInventory,
			    DebugFullSphereLevels,
			    DebugMaxHpMpAndStats,
			    DebugApplyViewerSave,
			    DebugLoadSaveForViewer,
			    BtlSetRandomEncountersEnabled,
			    SaveCrcSkip,
			    Show43Frame,
			    PlayerIsDebugNoClipHeld,
			    PlayerDebugNoClip,
			    ChIdToDebugName,
			    ViewerManagerCtor,
			    ViewerManagerSetCharacterByName,
			    AtelDebugScreenOrphan,
			    BoosterPollDebugKeyI,
			    BoosterPollDebugKeyJ,
			    BoosterDebugKeyIEdge,
			    BoosterDebugKeyJEdge,
			    DebugCharModeNames,
			    DebugAuthorNames,
			    PhyreGetApplication,
			    PhyreConsumeKeyDown,
			    PhyreIsKeyDown,
			};

			if (count)
				*count = (int)(sizeof(list) / sizeof(list[0]));

			return list;
		}
	} // namespace Rva
} // namespace ffx
