#include "ProbeSettings.h"

#include <string.h>

#include "workshop/Log.h"
#include "workshop/Settings.h"

namespace probe
{

	using namespace workshop;

	Settings settings;

	const wchar_t* const SettingsFileName = L"albhed_probe.txt";

	namespace
	{

		FILETIME lastSeen = { 0, 0 };
		bool haveLastSeen = false;

		// 0 when the file is missing, which compares unequal to any real timestamp.
		bool FileTimeOf(FILETIME* out)
		{
			wchar_t path[MAX_PATH];
			if (!SettingsPathFor(SettingsFileName, path, MAX_PATH))
				return false;
			WIN32_FILE_ATTRIBUTE_DATA data;
			if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data))
			{
				out->dwLowDateTime = 0;
				out->dwHighDateTime = 0;
				return true;
			}
			*out = data.ftLastWriteTime;
			return true;
		}

		void RememberFileTime()
		{
			haveLastSeen = FileTimeOf(&lastSeen);
		}

		bool FileExists()
		{
			wchar_t path[MAX_PATH];
			if (!SettingsPathFor(SettingsFileName, path, MAX_PATH))
				return false;
			return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
		}

		void Register(const char* key, const char* label, SettingKind kind, void* storage,
		    float minimum, float maximum, float step, int textCapacity)
		{
			Setting record;
			ZeroMemory(&record, sizeof(record));
			record.key = key;
			record.label = label;
			record.kind = kind;
			record.scope = SettingLocal; // nothing here ever has to match another machine
			record.storage = storage;
			record.minimum = minimum;
			record.maximum = maximum;
			record.step = step;
			record.choices = NULL;
			record.textCapacity = textCapacity;
			RegisterSetting(record);
		}

		void ApplyDefaults()
		{
			settings.capture = 0;
			settings.pacing = 1;
			settings.camera = 1;
			settings.motion = 1;
			settings.joints = 0;
			settings.jointsChr = JointsFilterControlled;
			settings.jointsMax = 136;
			settings.hotkey = VK_SCROLL;
			settings.maxSteps = 0;
			settings.budgetMegabytes = 64;
			settings.outputDir[0] = 0;
		}

		void RegisterAll()
		{
			Register("probe.capture", "recording", SettingBool, (void*)&settings.capture, 0, 1, 1, 0);
			Register("probe.pacing", "record FFX_StepPacing", SettingBool, (void*)&settings.pacing,
			    0, 1, 1, 0);
			Register("probe.camera", "record FFX_Came_ActivateScreen", SettingBool,
			    (void*)&settings.camera, 0, 1, 1, 0);
			Register("probe.motion", "record FFX_Ch_UpdateMotionAll", SettingBool,
			    (void*)&settings.motion, 0, 1, 1, 0);
			Register("probe.joints", "record FFX_Mot_AdvanceFrame, heavy, off by default",
			    SettingBool, (void*)&settings.joints, 0, 1, 1, 0);
			Register("probe.joints_chr", "-1 the controlled CHR, -2 every CHR, else an m_id",
			    SettingInt, (void*)&settings.jointsChr, -2, 65535, 1, 0);
			Register("probe.joints_max", "joints per record, larger skeletons are cut", SettingInt,
			    (void*)&settings.jointsMax, 8, 512, 8, 0);
			Register("probe.hotkey", "virtual key that toggles recording, 0 for none", SettingInt,
			    (void*)&settings.hotkey, 0, 255, 1, 0);
			Register("probe.max_steps", "stop after this many steps, 0 for no limit", SettingInt,
			    (void*)&settings.maxSteps, 0, 10000000, 1, 0);
			Register("probe.budget_mb", "memory per file waiting for the disk, in MB", SettingInt,
			    (void*)&settings.budgetMegabytes, 8, 1024, 8, 0);
			Register("probe.output_dir", "output folder, empty for LOCALAPPDATA", SettingText,
			    (void*)settings.outputDir, 0, 0, 0, (int)sizeof(settings.outputDir));
		}

	} // namespace

	void LoadProbeSettings()
	{
		ApplyDefaults();
		RegisterAll();
		if (FileExists())
		{
			LoadSettings(SettingsFileName);
		}
		else
		{
			// A first run gets a file with every key in it, so there is something to edit.
			Log("settings: no %S yet, writing one with the defaults", SettingsFileName);
			SaveSettings(SettingsFileName);
		}
		RememberFileTime();
	}

	void SaveProbeSettings()
	{
		SaveSettings(SettingsFileName);
		RememberFileTime();
	}

	bool ProbeSettingsFileChanged()
	{
		FILETIME now;
		if (!FileTimeOf(&now))
			return false;
		if (!haveLastSeen)
		{
			lastSeen = now;
			haveLastSeen = true;
			return false;
		}
		if (CompareFileTime(&now, &lastSeen) == 0)
			return false;
		lastSeen = now;
		// A missing file is not an edit worth acting on, the values in memory stand.
		return now.dwLowDateTime != 0 || now.dwHighDateTime != 0;
	}

	void ReloadProbeSettings()
	{
		LoadSettings(SettingsFileName);
		RememberFileTime();
	}

} // namespace probe
