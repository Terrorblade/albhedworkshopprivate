#include "ffx/DebugMenu.h"

#include "ffx/GameState.h"
#include "ffx/WorldState.h"
#include "ffx/addresses/Debug.h"
#include "ffx/addresses/WorldState.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

namespace ffx
{

	using namespace workshop;

	namespace
	{

		// One row per page: how to fetch it, how to drive it, where its enabled byte is,
		// what mode opens it, and whether its handler also wants the debug flag.
		struct PageSpec
		{
			const char* title;
			int mode; // -1 when the mode number was never pinned down
			DWORD getter;
			DWORD input;
			DWORD moveCursor;
			int enabledOff;
			bool needsDebugFlag;
		};

		const PageSpec g_pages[DebugPageCount] = {
			{ "Character Texture Animation", 5, Rva::DebugPageGetCharTexAnim,
			    Rva::DebugPageCharTexAnimInput, Rva::DebugPageCharTexAnimMoveCursor,
			    Rva::DebugPageEnabledOffCharTexAnim, false },
			{ "Basic Debug Information", 6, Rva::DebugPageGetBasicInfo,
			    Rva::DebugPageBasicInfoInput, Rva::DebugPageBasicInfoMoveCursor,
			    Rva::DebugPageEnabledOffBasicInfo, false },
			{ "MapSwitching", 7, Rva::DebugPageGetMapSwitching,
			    Rva::DebugPageMapSwitchingInput, Rva::DebugPageMapSwitchingMoveCursor,
			    Rva::DebugPageEnabledOffMapSwitching, true },
			{ "battle character parameter config", 8, Rva::DebugPageGetBattleCharParams,
			    Rva::DebugPageBattleCharParamsInput,
			    Rva::DebugPageBattleCharParamsMoveCursor,
			    Rva::DebugPageEnabledOffBattleCharParams, false },
			{ "Game parameter config", 9, Rva::DebugPageGetGameParams,
			    Rva::DebugPageGameParamsInput, Rva::DebugPageGameParamsMoveCursor,
			    Rva::DebugPageEnabledOffGameParams, false },
			{ "Battle Debug Information", -1, Rva::DebugPageGetBattleInfo,
			    Rva::DebugPageBattleInfoInput, Rva::DebugPageBattleInfoMoveCursor,
			    Rva::DebugPageEnabledOffBattleInfo, false },
			{ "Character Animation", -1, Rva::DebugPageGetCharAnim,
			    Rva::DebugPageCharAnimInput, Rva::DebugPageCharAnimMoveCursor,
			    Rva::DebugPageEnabledOffCharAnim, true },
			{ "CharacterSwitching", -1, Rva::DebugPageGetCharSwitching,
			    Rva::DebugPageCharSwitchingInput, Rva::DebugPageCharSwitchingMoveCursor,
			    Rva::DebugPageEnabledOffCharSwitching, true },
		};

		typedef void*(__cdecl* GetPtrFn)(void);
		typedef int(__thiscall* GetModeFn)(void* self);
		typedef char(__thiscall* SetModeFn)(void* self, int mode);
		typedef void(__thiscall* PageInputFn)(void* page, void* app);
		typedef int(__cdecl* MoveCursorFn)(int dir);
		typedef void(__cdecl* NoArgFn)(void);
		typedef void(__cdecl* IntArgFn)(int value);
		typedef int(__cdecl* SetCursorFn)(int x, int y);
		typedef int(__cdecl* GetIntFn)(void);

		template <class T>
		T At(DWORD rva)
		{
			return (T)ModuleAddress(rva);
		}

		bool ValidPage(DebugPage page)
		{
			return page >= 0 && page < DebugPageCount;
		}

		void* Manager()
		{
			// The singleton pointer, read rather than the getter called, so this answers
			// before the getter would be safe to call.
			void** slot = (void**)ModuleAddress(Rva::DebugMenuManagerSingleton);
			if (!Readable(slot, sizeof(void*)))
				return nullptr;
			return *slot;
		}

		void* PageObject(DebugPage page)
		{
			if (!ValidPage(page))
				return nullptr;

			GetPtrFn get = At<GetPtrFn>(g_pages[page].getter);
			if (!Readable((void*)get, 1))
				return nullptr;

			void* object = get();
			if (!Readable(object, (size_t)g_pages[page].enabledOff + 1))
				return nullptr;
			return object;
		}

		bool PageEnabled(DebugPage page)
		{
			void* object = PageObject(page);
			if (!object)
				return false;
			return ((const BYTE*)object)[g_pages[page].enabledOff] != 0;
		}

		// The cheat functions all have the same shape, so they share one caller.
		bool CallSaveCheat(DWORD rva, const char* what)
		{
			if (!GameLoaded())
				return false;

			NoArgFn fn = At<NoArgFn>(rva);
			if (!Readable((void*)fn, 1))
				return false;

			fn();
			Log("debug menu: ran the game's own %s", what);
			return true;
		}

	} // namespace

	const char* DebugPageName(DebugPage page)
	{
		return ValidPage(page) ? g_pages[page].title : "?";
	}

	int DebugPageMode(DebugPage page)
	{
		return ValidPage(page) ? g_pages[page].mode : -1;
	}

	bool DebugPageNeedsDebugFlag(DebugPage page)
	{
		return ValidPage(page) ? g_pages[page].needsDebugFlag : false;
	}

	bool SetDebugMode(bool on)
	{
		BYTE* flag = (BYTE*)ModuleAddress(Rva::DebugMode);
		if (!Readable(flag, 1))
			return false;

		// Written directly rather than through SetDebugModeOrphan, which is equivalent
		// but refuses when its own gate byte is clear. Three instructions in the binary
		// write this flag and none of them is a config read, so there is nothing to
		// coordinate with.
		*flag = on ? (BYTE)1 : (BYTE)0;

		Log("debug menu: debug mode %s. That also unlocks FFX_Map_WarpTo's gate, the "
		    "shipped noclip and the SG developer GUI, so it is more than one thing.",
		    on ? "on" : "off");
		return true;
	}

	bool DebugMenuAvailable()
	{
		return Manager() != nullptr;
	}

	int DebugMenuMode()
	{
		void* manager = Manager();
		if (!manager)
			return -1;

		GetModeFn get = At<GetModeFn>(Rva::DebugMenuManagerGetMode);
		if (!Readable((void*)get, 1))
			return -1;
		return get(manager);
	}

	bool OpenDebugPage(DebugPage page)
	{
		if (!ValidPage(page))
			return false;

		const int mode = g_pages[page].mode;
		if (mode < 0)
		{
			Log("debug menu: %s has no settled mode number, so SetMode cannot open it. "
			    "Its input handler still works if something else enables the page.",
			    g_pages[page].title);
			return false;
		}

		void* manager = Manager();
		if (!manager)
			return false;

		SetModeFn set = At<SetModeFn>(Rva::DebugMenuManagerSetMode);
		if (!Readable((void*)set, 1))
			return false;

		// The call the retail build never makes. SetMode's only caller is Close.
		set(manager, mode);

		if (g_pages[page].needsDebugFlag && !IsDebugMode())
			Log("debug menu: opened %s, but its input handler tests the debug flag as "
			    "well as the enabled byte, so turn debug mode on or it will draw and not "
			    "respond.",
			    g_pages[page].title);
		else
			Log("debug menu: opened %s, mode %d", g_pages[page].title, mode);
		return true;
	}

	bool CloseDebugMenu()
	{
		void* manager = Manager();
		if (!manager)
			return false;

		SetModeFn set = At<SetModeFn>(Rva::DebugMenuManagerSetMode);
		if (!Readable((void*)set, 1))
			return false;

		set(manager, kDebugMenuClosed);
		return true;
	}

	DebugPage OpenDebugPageNow()
	{
		// The pages are asked rather than the manager, because the enabled bytes are
		// what the draw and input functions actually test.
		for (int i = 0; i < DebugPageCount; ++i)
		{
			if (PageEnabled((DebugPage)i))
				return (DebugPage)i;
		}
		return DebugPageCount;
	}

	bool PumpDebugMenuInput()
	{
		const DebugPage page = OpenDebugPageNow();
		if (page == DebugPageCount)
			return false;

		void* object = PageObject(page);
		if (!object)
			return false;

		GetPtrFn getApp = At<GetPtrFn>(Rva::PhyreGetApplication);
		if (!Readable((void*)getApp, 1))
			return false;

		void* app = getApp();
		if (!app)
			return false;

		PageInputFn input = At<PageInputFn>(g_pages[page].input);
		if (!Readable((void*)input, 1))
			return false;

		// The orphaned handler. Nothing in the binary calls this, which is the entire
		// reason the menu draws and does not respond.
		input(object, app);
		return true;
	}

	bool MoveDebugPageCursor(DebugPage page, int dir)
	{
		if (!ValidPage(page) || (dir != 0 && dir != 1))
			return false;
		if (!PageEnabled(page))
			return false;

		MoveCursorFn move = At<MoveCursorFn>(g_pages[page].moveCursor);
		if (!Readable((void*)move, 1))
			return false;

		move(dir);
		return true;
	}

	// ---------------------------------------------------------------------------
	// The cheats
	// ---------------------------------------------------------------------------

	bool DebugFullGil()
	{
		return CallSaveCheat(Rva::DebugFullGil, "full gil, 999999999");
	}

	bool DebugFullItems()
	{
		return CallSaveCheat(Rva::DebugFullItem, "99 of every item");
	}

	bool DebugOneOfEveryItem()
	{
		return CallSaveCheat(Rva::DebugOneOfEveryItem, "one of every item");
	}

	bool DebugClearInventory()
	{
		return CallSaveCheat(Rva::DebugClearInventory, "clear inventory");
	}

	bool DebugFullSphereLevels()
	{
		return CallSaveCheat(Rva::DebugFullSphereLevels, "98 sphere levels for everybody");
	}

	bool DebugMaxHpMpAndStats()
	{
		return CallSaveCheat(Rva::DebugMaxHpMpAndStats, "max HP, MP and base stats");
	}

	bool DebugApplyViewerSave(int id)
	{
		// The Basic Info page's Game Section row offers 1 to 113, which is the only
		// statement of the range there is.
		if (id < 1 || id > 113)
			return false;

		IntArgFn fn = At<IntArgFn>(Rva::DebugApplyViewerSave);
		if (!Readable((void*)fn, 1))
			return false;

		Log("debug menu: loading savesforviewer/%d over the live save block and warping "
		    "to whatever map it names. This reads straight off disk with fopen, not "
		    "through the archive, so it does nothing at all if those files were not "
		    "shipped.",
		    id);
		fn(id);
		return true;
	}

	// ---------------------------------------------------------------------------
	// The SG developer GUI
	// ---------------------------------------------------------------------------

	bool OpenSgDebugGui()
	{
		NoArgFn open = At<NoArgFn>(Rva::SgDebugGuiOpenOrphan);
		if (!Readable((void*)open, 1))
			return false;

		if (!IsDebugMode())
		{
			// Both of its main-loop pumps test the flag, so opening it with the flag off
			// builds the windows and then draws nothing.
			Log("debug menu: refusing to open the SG GUI with debug mode off, because "
			    "both of its pumps test the flag and it would build windows that never "
			    "draw. Turn debug mode on first.");
			return false;
		}

		open();
		Log("debug menu: opened the SG developer GUI root window. Its pointer device read "
		    "is stubbed out, so feed FeedSgDebugGuiMouse every frame or the cursor will "
		    "not move.");
		return true;
	}

	bool FeedSgDebugGuiMouse(int buttons, int deltaX, int deltaY)
	{
		int* devButtons = (int*)ModuleAddress(Rva::SgDebugGuiDevButtons);
		int* devDeltaX = (int*)ModuleAddress(Rva::SgDebugGuiDevDeltaX);
		int* devDeltaY = (int*)ModuleAddress(Rva::SgDebugGuiDevDeltaY);

		if (!Readable(devButtons, 4) || !Readable(devDeltaX, 4) || !Readable(devDeltaY, 4))
			return false;

		// The three fields the stubbed read was supposed to fill. They have exactly one
		// reference in the whole binary, the push inside that poll, and no writer at all,
		// so nothing competes for them.
		*devButtons = buttons;
		*devDeltaX = deltaX;
		*devDeltaY = deltaY;
		return true;
	}

	bool SgDebugGuiCursor(int* outX, int* outY)
	{
		GetIntFn getX = At<GetIntFn>(Rva::SgDebugGuiGetCursorX);
		GetIntFn getY = At<GetIntFn>(Rva::SgDebugGuiGetCursorY);
		if (!Readable((void*)getX, 1) || !Readable((void*)getY, 1))
			return false;

		if (outX)
			*outX = getX();
		if (outY)
			*outY = getY();
		return true;
	}

	bool SetSgDebugGuiCursor(int x, int y)
	{
		SetCursorFn set = At<SetCursorFn>(Rva::SgDebugGuiSetCursorPos);
		if (!Readable((void*)set, 1))
			return false;

		// Through the engine's setter, because it writes the two float accumulators as
		// well as the ints and the next frame recomputes the ints from those.
		set(x, y);
		return true;
	}

	void LogDebugMenu()
	{
		Log("debug menu: debug mode is %s, manager %s, mode %d",
		    IsDebugMode() ? "ON" : "off", DebugMenuAvailable() ? "exists" : "MISSING",
		    DebugMenuMode());

		for (int i = 0; i < DebugPageCount; ++i)
		{
			const DebugPage page = (DebugPage)i;
			Log("debug menu: %-34s mode %2d, enabled at +%d, %s%s", g_pages[i].title,
			    g_pages[i].mode, g_pages[i].enabledOff,
			    PageEnabled(page) ? "OPEN" : "closed",
			    g_pages[i].needsDebugFlag ? ", needs the debug flag" : "");
		}
	}

} // namespace ffx
