#include "ffx/EscMenu.h"

#include "workshop/HostModule.h"
#include "workshop/Log.h"
#include "ffx/addresses/EscMenu.h"

// addresses/MainLoop.h for Rva::EscMenu, the singleton pointer. That area owns
// it and this one must not declare a second copy of the address, so include the
// header rather than retype the number.
#include "ffx/addresses/MainLoop.h"

// Implementation notes worth reading before changing anything here.
//
// This file resolves its own function pointers rather than going through
// ffx::Game, because ffx/Api.h is a shared file and this area is not allowed to
// edit it. The cost is one Callable helper, the benefit is that the Esc menu
// work is usable whether or not BindApi has run.
//
// There are two objects in play and both can be absent, for different reasons.
// The config object is built in WinMain and lives for the process, so it is
// absent only during very early startup. The FFEscMenu singleton is created
// lazily the first time the player opens the menu, so it is absent for most of a
// normal session and a caller must handle NULL as an ordinary outcome rather
// than an error.
//
// Fields are read volatile, because the game thread writes them behind us.
//
// Nothing here calls into the host unless a caller asked for something only a
// game function can do, which is exactly three things: write a config value,
// save the file, and talk to the Flash movie. Everything else, the player handle
// included, is reimplemented as plain reads so it needs no prologue check and
// works from any thread.

namespace ffx
{

	using workshop::Log;
	using workshop::ModuleAddress;
	using workshop::Readable;

	// ---------------------------------------------------------------------------
	// The AS3 function names and widget type strings, straight out of .rdata.
	// ---------------------------------------------------------------------------
	const char* const kAs3AddVideoPageItem = "addVideoPageItem";
	const char* const kAs3AddAudioPageItem = "addAudioPageItem";
	const char* const kAs3AddKeyboardPageItem = "addKeyboardPageItem";
	const char* const kAs3AddControllerPageItem = "addControllerPageItem";
	const char* const kAs3AddSpecialFeaturePageItem = "addSpecialFeaturePageItem";
	const char* const kAs3AddChangeParaPageItem = "addChangeParaPageItem";

	const char* const kWidgetSpinner = "spinner";
	const char* const kWidgetSpinnerWithList = "spinnerWithList";
	const char* const kWidgetSpinnerWithBar = "spinnerWithBar";
	const char* const kWidgetSpinnerButton = "spinnerButton";
	const char* const kWidgetKeyboardBinding = "keyboardBindingItem";
	const char* const kWidgetControllerBinding = "controllerBindingItem";

	namespace
	{

		// How far into the FFEscMenu object this file ever reads. ModeList is the last
		// field we touch and OpenIndex, which ffx/MainLoop.h owns, is the last field
		// anyone touches, so validating to 0xE8 covers both.
		const SIZE_T kEscMenuCheckedBytes = 0xE8;

		// ---------------------------------------------------------------------------
		// Typed reads, with the Readable check that keeps a bad read off the frame path.
		// ---------------------------------------------------------------------------
		template <typename T>
		T* At(DWORD rva)
		{
			T* p = (T*)ModuleAddress(rva);
			return Readable(p, sizeof(T)) ? p : NULL;
		}

		template <typename T>
		bool ReadGlobal(DWORD rva, T* out)
		{
			const T* p = At<T>(rva);
			if (!p || !out)
				return false;
			*out = *(volatile const T*)p;
			return true;
		}

		template <typename T>
		bool WriteGlobal(DWORD rva, T value)
		{
			T* p = At<T>(rva);
			if (!p)
				return false;
			*(volatile T*)p = value;
			return true;
		}

		inline int Rd32(const BYTE* p)
		{
			return (int)*(volatile const DWORD*)p;
		}
		inline DWORD Rd32u(const BYTE* p)
		{
			return *(volatile const DWORD*)p;
		}

		// Follows a host pointer slot and vets what it found. Used for both singletons,
		// because both are "a void * in .data that is null until the game builds it".
		void* FollowSingleton(DWORD rva, SIZE_T checkedBytes)
		{
			const void* slot = ModuleAddress(rva);
			if (!Readable(slot, sizeof(void*)))
				return NULL;

			// Read the slot as a dword rather than as a void **, because a volatile read
			// through void ** yields a volatile void * and MSVC will not drop the
			// qualifier for us.
			void* object = (void*)(UINT_PTR) * (volatile const DWORD*)slot;
			if (!object)
				return NULL;
			if (!workshop::LooksLikePointer((DWORD)(UINT_PTR)object))
				return NULL;
			if (!Readable(object, checkedBytes))
				return NULL;
			return object;
		}

		// ---------------------------------------------------------------------------
		// Calling into the host. Every entry point is checked once and the verdict
		// cached, and anything that fails degrades to "return false" rather than
		// faulting, because a bad call from the frame path takes the whole game down.
		// ---------------------------------------------------------------------------
		int setValueVerdict = 0;
		int saveFileVerdict = 0;
		int callAs3Verdict = 0;
		int makeArrayVerdict = 0;
		int registerCbVerdict = 0;

		bool Callable(DWORD rva, int* cachedVerdict, const char* what)
		{
			if (*cachedVerdict == 0)
			{
				*cachedVerdict = workshop::LooksLikeFunctionStart(rva) ? 1 : -1;
				if (*cachedVerdict < 0)
				{
					Log("esc menu: %s at RVA 0x%08X does not look callable, giving up on it",
					    what, rva);
				}
			}
			return *cachedVerdict > 0;
		}

		// The UINT_PTR hop is the project's idiom for a data-to-function cast.
		template <typename T>
		T ResolveFn(DWORD rva)
		{
			return (T)(UINT_PTR)ModuleAddress(rva);
		}

		// ---------------------------------------------------------------------------
		// The key name table. 24 char* in host .data, read through rather than copied,
		// so a caller always sees the spelling the game's own parser accepts.
		// ---------------------------------------------------------------------------
		const char* KeyNameFromHost(int keyIndex)
		{
			if (keyIndex < 0 || keyIndex >= kConfigKeyCount)
				return NULL;

			const char* const* table = (const char* const*)ModuleAddress(Rva::ConfigKeyNames);
			if (!Readable(table, kConfigKeyCount * sizeof(const char*)))
				return NULL;

			const char* name = table[keyIndex];
			if (!Readable(name, 1))
				return NULL;
			return name;
		}

		DWORD StringLength(const char* s)
		{
			DWORD n = 0;
			while (s[n] != 0)
				++n;
			return n;
		}

	} // namespace

	// ---------------------------------------------------------------------------
	// IggyValue builders
	// ---------------------------------------------------------------------------

	void SetIggyNumber(IggyValue* v, int value)
	{
		if (!v)
			return;
		v->tag = kIggyTagNumber;
		v->reserved = 0;
		// The game's own builder narrows the int to float and widens it back, which
		// rounds anything above 2^24. We store the exact double instead. For every
		// value a menu row carries, a row index or a small enum, the two are bit
		// identical, and where they differ ours is the one the movie can trust.
		v->payload.number = (double)value;
	}

	void SetIggyBool(IggyValue* v, bool value)
	{
		if (!v)
			return;
		v->tag = kIggyTagBool;
		v->reserved = 0;
		v->payload.boolean = value ? 1u : 0u;
	}

	void SetIggyString(IggyValue* v, const char* text)
	{
		if (!v)
			return;
		if (!text)
			text = "";

		v->tag = kIggyTagString;
		v->reserved = 0;
		v->payload.string.text = text;
		v->payload.string.length = StringLength(text);
	}

	void SetIggyArray(IggyValue* v, DWORD arrayRef)
	{
		if (!v)
			return;
		v->tag = kIggyTagArray;
		v->reserved = 0;
		v->payload.array.ref = arrayRef;
		v->payload.array.unused = 0;
	}

	// ---------------------------------------------------------------------------
	// The two objects
	// ---------------------------------------------------------------------------

	BYTE* ConfigObject()
	{
		// NULL until WinMain has run the constructor. Validated for the full object
		// so a half built one reads as absent.
		return (BYTE*)FollowSingleton(Rva::ConfigObjectPtr, ConfigLayout::Size);
	}

	BYTE* EscMenuPtr()
	{
		// NULL until the player opens the menu for the first time.
		return (BYTE*)FollowSingleton(Rva::EscMenu, kEscMenuCheckedBytes);
	}

	void* EscMenuIggyPlayer()
	{
		BYTE* menu = EscMenuPtr();
		if (!menu)
			return NULL;

		// Two derefs, exactly as the host's own getter does it. Reimplemented rather
		// than called, so this needs no prologue check and works from any thread,
		// and the host version is only three instructions so there is nothing here
		// to get wrong.
		const void* holder = (const void*)(UINT_PTR)Rd32u(menu + EscMenuFields::PlayerHolder);
		if (!holder || !Readable(holder, sizeof(void*)))
			return NULL;

		const DWORD player = *(volatile const DWORD*)holder;
		if (!player || !workshop::LooksLikePointer(player))
			return NULL;
		return (void*)(UINT_PTR)player;
	}

	// ---------------------------------------------------------------------------
	// Reading and writing the 24 options
	// ---------------------------------------------------------------------------

	bool ReadConfigValue(int keyIndex, int* out)
	{
		if (!out)
			return false;
		if (keyIndex < 0 || keyIndex >= kConfigKeyCount)
			return false;

		BYTE* cfg = ConfigObject();
		if (!cfg)
			return false;

		*out = Rd32(cfg + ConfigLayout::Values + 4 * keyIndex);
		return true;
	}

	bool WriteConfigValue(int keyIndex, int value)
	{
		if (keyIndex < 0 || keyIndex >= kConfigKeyCount)
			return false;

		BYTE* cfg = ConfigObject();
		if (!cfg)
			return false;

		// Through the game's own setter rather than a direct store. Today the body
		// is nothing but cfg[keyIndex] = value, so this buys no behaviour, but it is
		// the one place 19 other callers go through and if a patch ever gives it a
		// side effect we inherit that for free.
		if (!Callable(Rva::ConfigSetValue, &setValueVerdict, "FFX_Config_SetValue"))
			return false;

		ConfigSetValueFn setValue = ResolveFn<ConfigSetValueFn>(Rva::ConfigSetValue);
		setValue(cfg, keyIndex, value);
		return true;
	}

	const char* ConfigKeyName(int keyIndex)
	{
		return KeyNameFromHost(keyIndex);
	}

	bool ConfigFilePath(wchar_t* out, int count)
	{
		if (!out || count <= 0)
			return false;

		BYTE* cfg = ConfigObject();
		if (!cfg)
			return false;

		const wchar_t* src = (const wchar_t*)(cfg + ConfigLayout::FilePath);

		// The path field runs from 0x430 to the defaults flag at 0x638, which is 260
		// wide characters. Copy within that and within the caller's buffer,
		// whichever runs out first, and always terminate.
		const int maxChars = (ConfigLayout::DefaultsFlag16 - ConfigLayout::FilePath) / 2;
		int i = 0;
		while (i < count - 1 && i < maxChars)
		{
			const wchar_t c = *(volatile const wchar_t*)(src + i);
			if (c == 0)
				break;
			out[i] = c;
			++i;
		}
		out[i] = 0;
		return true;
	}

	bool SaveConfigFile()
	{
		BYTE* cfg = ConfigObject();
		if (!cfg)
			return false;

		if (!Callable(Rva::ConfigSaveFile, &saveFileVerdict, "FFX_Config_SaveFile"))
			return false;

		typedef int(__thiscall * SaveFileFn)(void* cfg);
		SaveFileFn save = ResolveFn<SaveFileFn>(Rva::ConfigSaveFile);
		save(cfg);
		return true;
	}

	// ---------------------------------------------------------------------------
	// What has to match between machines
	// ---------------------------------------------------------------------------

	bool ConfigKeyAffectsSimulation(int keyIndex)
	{
		if (keyIndex < 0 || keyIndex >= kConfigKeyCount)
			return false;

		// Every one of the 24 is presentation only, and that is a finding rather
		// than a default. The method was: the 24 value dwords have exactly one
		// writer, FFX_Config_SetValue, whose callers are the file parser and the Esc
		// menu commit handlers. Every reader of a value dword is a renderer or audio
		// path, except indices 19 and 20 which have no reader at all in FFX.exe. The
		// three key-binding arrays are per-player by definition.
		//
		// So the answer is a flat false, and a caller that drives a sync table off
		// this function gets the right behaviour without a human reading a comment.
		// If a later finding changes that for one index, change it here and every
		// caller follows.
		return false;
	}

	// ---------------------------------------------------------------------------
	// The boosters that ffx/MainLoop.h does not already have
	// ---------------------------------------------------------------------------

	bool BoosterAutoBattle(int* out)
	{
		return ReadGlobal<int>(Rva::BoosterAutoBattle, out);
	}
	bool BoosterEncounterRate(int* out)
	{
		return ReadGlobal<int>(Rva::BoosterEncounterRate, out);
	}
	bool BoosterInvincible(int* out)
	{
		return ReadGlobal<int>(Rva::BoosterInvincible, out);
	}

	bool SetBoosterAutoBattle(int value)
	{
		// A bare store is correct here. The game writes this global in exactly one
		// place, the once-per-frame hotkey poll, and the 22 battle functions that
		// read it read the global itself rather than a cached copy, so nothing
		// overwrites our value until the player presses the key again.
		return WriteGlobal<int>(Rva::BoosterAutoBattle, value);
	}

	bool SetBoosterEncounterRate(int value)
	{
		if (value < 0 || value > 2)
			return false; // 0 off, 1 normal, 2 high
		return WriteGlobal<int>(Rva::BoosterEncounterRate, value);
	}

	bool SetBoosterInvincible(int value)
	{
		return WriteGlobal<int>(Rva::BoosterInvincible, value);
	}

	// ---------------------------------------------------------------------------
	// Adding our own rows
	// ---------------------------------------------------------------------------

	bool CallAS3(const char* as3Name, const IggyValue* args, int argCount)
	{
		if (!as3Name)
			return false;
		if (argCount < 0)
			return false;
		if (argCount > 0 && !args)
			return false;
		if (argCount > 0 && !Readable(args, (SIZE_T)argCount * sizeof(IggyValue)))
			return false;

		BYTE* menu = EscMenuPtr();
		if (!menu)
			return false;

		if (!Callable(Rva::IggyMenuCallAS3Function, &callAs3Verdict,
		        "FFX_IggyMenu_CallAS3Function"))
		{
			return false;
		}

		IggyCallAS3Fn call = ResolveFn<IggyCallAS3Fn>(Rva::IggyMenuCallAS3Function);

		// resultPath 0 and reserved 1 are what every one of the game's own call
		// sites passes, from the six-argument row builders down to the zero-argument
		// onCancelKey, which is where the zero-argument shape was confirmed.
		call(menu, NULL, as3Name, argCount, argCount > 0 ? args : NULL, 1);
		return true;
	}

	DWORD MakeChoiceArray(const char** strings, int count)
	{
		if (!strings || count <= 0)
			return 0;
		if (!Readable(strings, (SIZE_T)count * sizeof(const char*)))
			return 0;

		void* player = EscMenuIggyPlayer();
		if (!player)
			return 0;

		if (!Callable(Rva::IggyMakeStringArray, &makeArrayVerdict,
		        "FFX_Iggy_MakeStringArray"))
		{
			return 0;
		}

		IggyMakeStringArrayFn make = ResolveFn<IggyMakeStringArrayFn>(Rva::IggyMakeStringArray);

		IggyArrayRef out;
		out.player = NULL;
		out.ref = 0;
		make(&out, player, strings, count);

		// The ref is the SECOND dword, not the first. The first is the player handle
		// the constructor stashed, and feeding that to SetIggyArray would hand the
		// movie a pointer where it expects an array reference.
		return out.ref;
	}

	bool AddVideoPageRow(int rowIndex, const char* widgetType, const char* label,
	    const char* currentValueText, DWORD choiceArrayRef, int barMax)
	{
		if (rowIndex < 0 || !widgetType || !label)
			return false;
		if (!currentValueText)
			currentValueText = "";

		// Six values, in declaration order. The wrapper the game uses passes these
		// by value on the stack, which is why its twelve call sites all do
		// add esp, 0x68. We hand the array to the non-variadic primitive instead and
		// never build that stack frame at all.
		IggyValue args[6];
		SetIggyNumber(&args[0], rowIndex);
		SetIggyString(&args[1], widgetType);
		SetIggyString(&args[2], label);
		SetIggyString(&args[3], currentValueText);
		SetIggyArray(&args[4], choiceArrayRef);
		SetIggyNumber(&args[5], barMax);

		return CallAS3(kAs3AddVideoPageItem, args, 6);
	}

	bool AddSpecialFeaturePageRow(int rowIndex, const char* label, const char* hint,
	    const char* descLine1, const char* descLine2)
	{
		if (rowIndex < 0 || !label)
			return false;
		if (!hint)
			hint = "";
		if (!descLine1)
			descLine1 = "";
		if (!descLine2)
			descLine2 = "";

		IggyValue args[5];
		SetIggyNumber(&args[0], rowIndex);
		SetIggyString(&args[1], label);
		SetIggyString(&args[2], hint);
		SetIggyString(&args[3], descLine1);
		SetIggyString(&args[4], descLine2);

		return CallAS3(kAs3AddSpecialFeaturePageItem, args, 5);
	}

	int VideoRowsBuilt()
	{
		BYTE* menu = EscMenuPtr();
		if (!menu)
			return -1;

		// The builder's very first act is to test the display mode list pointer. A
		// null list means it skipped the Resolution row and emitted indices 1..11,
		// eleven rows. A list means it emitted 0..11, twelve. Either way the highest
		// index it passed was 11, which is why kFirstFreeVideoRowIndex is a constant
		// and this is not.
		return Rd32u(menu + EscMenuFields::ModeList) != 0 ? 12 : 11;
	}

	bool RegisterAS3Callback(const char* as3Name, As3CallbackFn handler)
	{
		if (!as3Name || !handler)
			return false;

		BYTE* menu = EscMenuPtr();
		if (!menu)
			return false;

		if (!Callable(Rva::IggyRegisterAS3Callback, &registerCbVerdict,
		        "FFX_Iggy_RegisterAS3Callback"))
		{
			return false;
		}

		IggyRegisterCallbackFn reg =
		    ResolveFn<IggyRegisterCallbackFn>(Rva::IggyRegisterAS3Callback);
		reg(menu, as3Name, (void*)(UINT_PTR)handler);
		return true;
	}

	// ---------------------------------------------------------------------------
	// Diagnostics
	// ---------------------------------------------------------------------------

	void LogEscMenuState()
	{
		BYTE* cfg = ConfigObject();
		if (!cfg)
		{
			Log("esc menu: the config object pointer is null, WinMain has not got that far");
		}
		else
		{
			wchar_t path[280];
			if (ConfigFilePath(path, 280))
			{
				Log("esc menu: config object at 0x%08X, file %ls",
				    (unsigned)(UINT_PTR)cfg, path);
			}
			else
			{
				Log("esc menu: config object at 0x%08X, file path unreadable",
				    (unsigned)(UINT_PTR)cfg);
			}

			const unsigned defaults =
			    (unsigned)*(volatile const WORD*)(cfg + ConfigLayout::DefaultsFlag16);
			Log("esc menu: %d bindings per game, defaults flag 0x%04X "
			    "(non-zero means no file was ever read)",
			    Rd32(cfg + ConfigLayout::BindingCount), defaults);

			for (int i = 0; i < kConfigKeyCount; ++i)
			{
				int value = 0;
				if (!ReadConfigValue(i, &value))
					continue;
				const char* name = ConfigKeyName(i);
				if (i == kConfigResolution)
				{
					Log("esc menu:   [%2d] %-16s %d  (%dx%d)",
					    i, name ? name : "?", value,
					    ResolutionWidth(value), ResolutionHeight(value));
				}
				else
				{
					Log("esc menu:   [%2d] %-16s %d", i, name ? name : "?", value);
				}
			}
		}

		BYTE* menu = EscMenuPtr();
		if (!menu)
		{
			Log("esc menu: no FFEscMenu singleton yet, the player has not opened the menu");
		}
		else
		{
			Log("esc menu: FFEscMenu at 0x%08X  iggy player 0x%08X  display modes %d  "
			    "selected %d  video rows built %d",
			    (unsigned)(UINT_PTR)menu, (unsigned)(UINT_PTR)EscMenuIggyPlayer(),
			    Rd32(menu + EscMenuFields::ModeCount),
			    Rd32(menu + EscMenuFields::ModeIndex),
			    VideoRowsBuilt());
			Log("esc menu: cached video row values %d %d %d %d %d %d %d %d %d %d %d %d",
			    Rd32(menu + EscMenuFields::VideoRowCache + 0),
			    Rd32(menu + EscMenuFields::VideoRowCache + 4),
			    Rd32(menu + EscMenuFields::VideoRowCache + 8),
			    Rd32(menu + EscMenuFields::VideoRowCache + 12),
			    Rd32(menu + EscMenuFields::VideoRowCache + 16),
			    Rd32(menu + EscMenuFields::VideoRowCache + 20),
			    Rd32(menu + EscMenuFields::VideoRowCache + 24),
			    Rd32(menu + EscMenuFields::VideoRowCache + 28),
			    Rd32(menu + EscMenuFields::VideoRowCache + 32),
			    Rd32(menu + EscMenuFields::VideoRowCache + 36),
			    Rd32(menu + EscMenuFields::VideoRowCache + 40),
			    Rd32(menu + EscMenuFields::VideoRowCache + 44));
		}

		int autoBattle = 0, encounterRate = 0, invincible = 0;
		const bool haveAuto = BoosterAutoBattle(&autoBattle);
		const bool haveEnc = BoosterEncounterRate(&encounterRate);
		const bool haveInv = BoosterInvincible(&invincible);
		Log("esc menu: boosters that MUST MATCH - autoBattle %s  encounterRate %s  "
		    "invincible %s  (fast forward is BoosterSpeedIndex in ffx/MainLoop.h)",
		    haveAuto ? (autoBattle ? "ON" : "off") : "unreadable",
		    haveEnc ? (encounterRate == 0 ? "off" : encounterRate == 1 ? "normal"
		                                                               : "high")
		            : "unreadable",
		    haveInv ? (invincible ? "ON" : "off") : "unreadable");
	}

} // namespace ffx
