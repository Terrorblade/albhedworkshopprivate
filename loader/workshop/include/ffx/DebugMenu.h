#pragma once

// The game's own debug menu, which shipped in the retail build with the call removed
// rather than the code.
//
//     ffx::SetDebugMode(true);
//     ffx::OpenDebugPage(ffx::DebugPageMapSwitching);
//     // then once per frame, from the hub's step or frame event:
//     ffx::PumpDebugMenuInput();
//
// WHAT IS ACTUALLY BROKEN, AND WHY THIS WORKS. The DebugMenuManager singleton is built
// on every normal boot by FFX_GraphicInitialize, and all nine page draw functions run
// every frame from FFX_Frame_PresentScene with no debug gate at all. Each one returns
// early on its own enabled byte, so the moment a page is enabled it draws. The two
// things missing are both missing calls and nothing else:
//
//   1. SetMode's only caller in the binary is Close, so nothing ever opens a page.
//      OpenDebugPage is that call.
//   2. All eight per-page input handlers are orphaned. The bodies are intact, real
//      code calling live engine functions, with no caller and no pointer anywhere.
//      PumpDebugMenuInput is that call, and it has to happen every frame.
//
// Nothing in this menu was stubbed out. There is no nullsub to patch and no branch to
// re-arm, which is why calling it is safe in a way that re-enabling a gate would not
// be.
//
// ONE PAGE AT A TIME. SetMode disables all ten pages and enables one, so the menu's
// own model is a single open page. The mode number for "closed" is 10.
//
// DEBUG MODE IS A SEPARATE SWITCH from the menu. Only the mode 5 extra overlay needs
// it. It is wanted anyway because it also unlocks FFX_Map_WarpTo, the shipped noclip
// and the SG developer GUI, but the pages themselves do not ask for it.
//
// Research is in reversing/DEBUG_MODE.md.

#include <windows.h>

namespace ffx
{

	// The eight pages whose input handler exists. Five more mode numbers exist, 0 to 4,
	// whose page identity was not settled, so they are not offered here.
	enum DebugPage
	{
		DebugPageCharTexAnim = 0,    // mode 5, material and texture animation
		DebugPageBasicInfo,          // mode 6, 13 rows, encounters and the CRC skip
		DebugPageMapSwitching,       // mode 7, THE WARP PAGE
		DebugPageBattleCharParams,   // mode 8, a live stat editor on the real save
		DebugPageGameParams,         // mode 9, the cheat page
		DebugPageBattleInfo,         // drawn every frame, mode number not settled
		DebugPageCharAnimation,      // the same
		DebugPageCharSwitching,      // the same, and it spawns a CHR by name
		DebugPageCount
	};

	// The mode the menu is closed in.
	const int kDebugMenuClosed = 10;

	// What the page prints as its own title, which is the developers' wording.
	const char* DebugPageName(DebugPage page);

	// The mode number SetMode wants, or -1 for a page whose mode was never pinned
	// down. A page with no mode can still have its input pumped, it just cannot be
	// opened through the manager.
	int DebugPageMode(DebugPage page);

	// True for the three pages whose input handler calls IsDebugMode as well as testing
	// its own enabled byte. MapSwitching, the warp page, is one of them, so the flag is
	// not optional there.
	bool DebugPageNeedsDebugFlag(DebugPage page);

	// The game's own debug flag. The kit deliberately had no setter for this, because
	// it changes more than one thing, so the one here says so and logs it.
	bool SetDebugMode(bool on);

	// True when the manager singleton exists, which it does on any normal boot.
	bool DebugMenuAvailable();

	// The mode the manager is in, or -1 when there is no manager. kDebugMenuClosed
	// means no page is open.
	int DebugMenuMode();

	// Opens one page and closes the rest, which is what the menu's own SetMode does.
	// False when the page has no settled mode number or there is no manager.
	bool OpenDebugPage(DebugPage page);
	bool CloseDebugMenu();

	// Which of the eight pages is open according to its own enabled byte, or
	// DebugPageCount for none. Asks the pages rather than the manager, so it is right
	// even if something else moved the mode.
	DebugPage OpenDebugPageNow();

	// ---------------------------------------------------------------------------
	// The repair
	// ---------------------------------------------------------------------------

	// Runs the open page's orphaned input handler. CALL IT ONCE PER FRAME or the menu
	// draws and does not respond. Returns true when a handler ran.
	//
	// GAME THREAD. The handlers read the Phyre application's keyboard and pad state and
	// write live save data.
	//
	// The keys are Phyre enum values and not Win32 VK codes: 36 and 38 move the cursor,
	// 37 and 39 change the selected row's value, 77 confirms on the Basic Info page. On
	// a pad, axis 3 is vertical and axis 2 horizontal with a 0.5 deadzone. Up and down
	// are edge triggered and consume the key, left and right are level triggered, so a
	// value row changes every frame it is held.
	bool PumpDebugMenuInput();

	// Drives the cursor without replaying the whole input handler, for a panel that
	// would rather offer buttons. dir 0 is up and 1 is down.
	bool MoveDebugPageCursor(DebugPage page, int dir);

	// ---------------------------------------------------------------------------
	// The cheats the menu reaches, which need no menu
	// ---------------------------------------------------------------------------

	// These take no arguments, check nothing, and write the live save block. They are
	// the game's own, reached from the Game parameter config page, and calling them
	// directly skips the menu entirely. Each returns false only when there is no game.
	bool DebugFullGil();          // 999999999
	bool DebugFullItems();        // 99 of all 112
	bool DebugOneOfEveryItem();   // 1 of all 112
	bool DebugClearInventory();   // all 112 slots emptied
	bool DebugFullSphereLevels(); // record[0x3B] = 98 on all 18 records
	bool DebugMaxHpMpAndStats();  // 99999 HP, 9999 MP, every base stat to 0xFF

	// void (int id), ids 1 to 113. Reads
	// <dataroot>/FFX_Data/GameData/PS3Data/savesforviewer/<id> with a raw fopen rather
	// than through the archive, copies 0x68C0 bytes over the live save block and warps
	// to the map that save names. A one-call jump to a point in the game, IF those
	// files ship, which is unverified. False when the id is out of range.
	bool DebugApplyViewerSave(int id);

	// ---------------------------------------------------------------------------
	// The PS2-era developer GUI, which is a second and much larger menu
	// ---------------------------------------------------------------------------

	// Opens the SG developer GUI's root window, seven tabs of it. Needs debug mode on,
	// because both of its main-loop pumps test the flag. The opener is orphaned the
	// same way the page input is, and its body is intact and stack balanced.
	bool OpenSgDebugGui();

	// THE SG GUI'S ONE REAL BREAKAGE. Its pointer device read was replaced with
	// "xor eax, eax / ret", so the read reports success and fills nothing, which leaves
	// the cursor pinned. The repair is not to patch that stub, which has fifteen
	// callers shared with the save and file paths, it is to write the three dwords the
	// stub was supposed to fill. Nothing in the engine competes for them.
	//
	// Call once per frame BEFORE the game steps. Buttons is bit 0 left, bit 1 middle,
	// bit 2 right. The deltas are in pixels, and the engine halves X and divides Y by
	// three on the way to the cursor.
	bool FeedSgDebugGuiMouse(int buttons, int deltaX, int deltaY);

	// The cursor the GUI is actually using, and an absolute setter that keeps the two
	// float accumulators consistent, which poking the ints does not.
	bool SgDebugGuiCursor(int* outX, int* outY);
	bool SetSgDebugGuiCursor(int x, int y);

	void LogDebugMenu();

} // namespace ffx
