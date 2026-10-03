#include "workshop/Settings.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "workshop/Log.h"

namespace workshop
{
	namespace
	{

		// Fixed capacity on purpose. The registry is built once at startup and read on
		// the frame path, so it never allocates and a record's address never moves.
		const int MaxSettings = 96;

		Setting table[MaxSettings];
		int tableCount = 0;
		unsigned long revision = 0;

		volatile long* AsLong(const Setting& s)
		{
			return (volatile long*)s.storage;
		}
		volatile float* AsFloat(const Setting& s)
		{
			return (volatile float*)s.storage;
		}
		char* AsText(const Setting& s)
		{
			return (char*)s.storage;
		}

		int ChoiceCount(const Setting& s)
		{
			if (!s.choices)
				return 0;
			int n = 0;
			while (s.choices[n])
				++n;
			return n;
		}

		// Builds <game dir>\AlBhedWorkshop\<fileName>, the same place the log goes.
		bool SettingsFilePath(const wchar_t* fileName, wchar_t* out, size_t count)
		{
			wchar_t exePath[MAX_PATH];
			if (GetModuleFileNameW(NULL, exePath, MAX_PATH) == 0)
				return false;

			wchar_t* lastSeparator = NULL;
			for (wchar_t* p = exePath; *p; ++p)
				if (*p == L'\\' || *p == L'/')
					lastSeparator = p;
			if (!lastSeparator)
				return false;
			*lastSeparator = 0;

			_snwprintf_s(out, count, _TRUNCATE, L"%s\\AlBhedWorkshop\\%s", exePath, fileName);
			return true;
		}

		unsigned long Fnv1a(const char* text)
		{
			unsigned long hash = 2166136261u;
			for (const char* p = text; p && *p; ++p)
			{
				hash ^= (unsigned char)*p;
				hash *= 16777619u;
			}
			return hash;
		}

		void TrimInPlace(char* text)
		{
			char* start = text;
			while (*start == ' ' || *start == '\t')
				++start;
			if (start != text)
				memmove(text, start, strlen(start) + 1);

			size_t length = strlen(text);
			while (length > 0)
			{
				const char last = text[length - 1];
				if (last != ' ' && last != '\t' && last != '\r' && last != '\n')
					break;
				text[--length] = 0;
			}
		}

	} // namespace

	bool RegisterSetting(const Setting& setting)
	{
		if (tableCount >= MaxSettings)
		{
			Log("settings: table full at %d, dropped '%s'", MaxSettings, setting.key);
			return false;
		}
		if (!setting.key || !*setting.key || !setting.storage)
		{
			Log("settings: rejected a record with no key or no storage");
			return false;
		}
		if (FindSetting(setting.key))
		{
			Log("settings: '%s' is already registered, ignoring the second one", setting.key);
			return false;
		}
		if (setting.kind == SettingEnum && ChoiceCount(setting) < 2)
		{
			Log("settings: enum '%s' needs at least two choices", setting.key);
			return false;
		}
		if (setting.kind == SettingText && setting.textCapacity < 2)
		{
			Log("settings: text '%s' needs a buffer, textCapacity was %d",
			    setting.key, setting.textCapacity);
			return false;
		}

		table[tableCount] = setting;

		// Bool and enum ranges come from the kind rather than the declaration, so a
		// caller cannot get them wrong and nothing has to repeat them.
		Setting& stored = table[tableCount];
		if (stored.kind == SettingBool)
		{
			stored.minimum = 0.0f;
			stored.maximum = 1.0f;
			stored.step = 1.0f;
		}
		else if (stored.kind == SettingEnum)
		{
			stored.minimum = 0.0f;
			stored.maximum = (float)(ChoiceCount(stored) - 1);
			stored.step = 1.0f;
		}

		++tableCount;
		return true;
	}

	int SettingCount()
	{
		return tableCount;
	}

	const Setting* SettingAt(int index)
	{
		if (index < 0 || index >= tableCount)
			return NULL;
		return &table[index];
	}

	const Setting* FindSetting(const char* key)
	{
		if (!key)
			return NULL;
		for (int i = 0; i < tableCount; ++i)
			if (_stricmp(table[i].key, key) == 0)
				return &table[i];
		return NULL;
	}

	float ReadSetting(const Setting& setting)
	{
		// A text setting has no numeric value. Returning zero is better than
		// reinterpreting the first four bytes of a string as a long.
		if (setting.kind == SettingText)
			return 0.0f;
		if (setting.kind == SettingFloat)
			return *AsFloat(setting);
		return (float)*AsLong(setting);
	}

	const char* ReadSettingText(const Setting& setting)
	{
		if (setting.kind != SettingText)
			return "";
		return AsText(setting);
	}

	void WriteSettingText(const Setting& setting, const char* value)
	{
		if (setting.kind != SettingText)
			return;

		char* buffer = AsText(setting);
		strncpy_s(buffer, (size_t)setting.textCapacity, value ? value : "", _TRUNCATE);

		// The file is one key=value per line, so a newline in a value would produce a
		// file that cannot be read back. Strip rather than refuse, since the usual
		// source of one is a paste.
		for (char* p = buffer; *p; ++p)
			if (*p == '\r' || *p == '\n')
				*p = ' ';

		++revision;
	}

	void WriteSetting(const Setting& setting, float value)
	{
		if (setting.kind == SettingText)
			return;
		if (value < setting.minimum)
			value = setting.minimum;
		if (value > setting.maximum)
			value = setting.maximum;

		if (setting.kind == SettingFloat)
		{
			*AsFloat(setting) = value;
		}
		else
		{
			// Round rather than truncate, so stepping a float-carried int by 1.0 does
			// not get stuck one below where it should be.
			*AsLong(setting) = (long)(value + (value < 0.0f ? -0.5f : 0.5f));
		}
		++revision;
	}

	void StepSetting(const Setting& setting, int direction)
	{
		if (direction == 0)
			return;
		if (setting.kind == SettingText)
			return;

		const float step = (setting.step != 0.0f) ? setting.step : 1.0f;
		float value = ReadSetting(setting) + step * (float)(direction > 0 ? 1 : -1);

		// Enums and bools wrap, because a menu row that cycles on one button is
		// useless if it dead ends. Numbers clamp, because a volume that jumps from
		// maximum to minimum is a trap.
		if (setting.kind == SettingBool || setting.kind == SettingEnum)
		{
			const float span = setting.maximum - setting.minimum + 1.0f;
			if (value > setting.maximum)
				value -= span;
			if (value < setting.minimum)
				value += span;
		}

		WriteSetting(setting, value);
	}

	const char* FormatSetting(const Setting& setting, char* out, size_t count)
	{
		if (!out || count == 0)
			return out;

		if (setting.kind == SettingText)
		{
			strncpy_s(out, count, AsText(setting), _TRUNCATE);
			return out;
		}

		const float value = ReadSetting(setting);
		switch (setting.kind)
		{
		case SettingBool:
			strncpy_s(out, count, (value != 0.0f) ? "on" : "off", _TRUNCATE);
			break;
		case SettingEnum:
		{
			const int index = (int)value;
			const int choices = ChoiceCount(setting);
			if (index >= 0 && index < choices)
				strncpy_s(out, count, setting.choices[index], _TRUNCATE);
			else
				_snprintf_s(out, count, _TRUNCATE, "? (%d)", index);
			break;
		}
		case SettingInt:
			_snprintf_s(out, count, _TRUNCATE, "%d", (int)value);
			break;
		case SettingFloat:
		default:
			_snprintf_s(out, count, _TRUNCATE, "%.2f", value);
			break;
		}
		return out;
	}

	unsigned long SettingsRevision()
	{
		return revision;
	}

	bool SaveSettings(const wchar_t* fileName)
	{
		wchar_t path[MAX_PATH];
		if (!SettingsFilePath(fileName, path, MAX_PATH))
			return false;

		// Write a temporary alongside and move it into place, so an interrupted save
		// leaves the previous file intact rather than a truncated one.
		wchar_t tempPath[MAX_PATH];
		_snwprintf_s(tempPath, _countof(tempPath), _TRUNCATE, L"%s.tmp", path);

		HANDLE file = CreateFileW(tempPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		    FILE_ATTRIBUTE_NORMAL, NULL);
		if (file == INVALID_HANDLE_VALUE)
		{
			Log("settings: could not open the settings file for writing, error %lu",
			    GetLastError());
			return false;
		}

		char block[512];
		int length = _snprintf_s(block, sizeof(block), _TRUNCATE,
		    "# AlBhedWorkshop settings. Written by the mod, safe to edit by hand.\r\n"
		    "# Unknown keys are ignored and out of range values fall back to\r\n"
		    "# the default, so a stale file will not stop the mod loading.\r\n");
		DWORD written = 0;
		WriteFile(file, block, (DWORD)length, &written, NULL);

		for (int i = 0; i < tableCount; ++i)
		{
			const Setting& setting = table[i];
			char value[256];
			if (setting.kind == SettingText)
				_snprintf_s(value, sizeof(value), _TRUNCATE, "%s", AsText(setting));
			else if (setting.kind == SettingFloat)
				_snprintf_s(value, sizeof(value), _TRUNCATE, "%.6g", ReadSetting(setting));
			else
				_snprintf_s(value, sizeof(value), _TRUNCATE, "%d", (int)ReadSetting(setting));

			length = _snprintf_s(block, sizeof(block), _TRUNCATE, "%s=%s\r\n", setting.key, value);
			if (length > 0)
				WriteFile(file, block, (DWORD)length, &written, NULL);
		}
		CloseHandle(file);

		if (!MoveFileExW(tempPath, path, MOVEFILE_REPLACE_EXISTING))
		{
			Log("settings: wrote the temporary file but could not replace the real one, error %lu",
			    GetLastError());
			return false;
		}
		return true;
	}

	bool LoadSettings(const wchar_t* fileName)
	{
		wchar_t path[MAX_PATH];
		if (!SettingsFilePath(fileName, path, MAX_PATH))
			return false;

		HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file == INVALID_HANDLE_VALUE)
		{
			// No file on a first run is the normal case, not a failure worth shouting
			// about. The defaults the mod already applied stand.
			Log("settings: no saved settings file, keeping defaults");
			return false;
		}

		const DWORD maxBytes = 64 * 1024;
		DWORD size = GetFileSize(file, NULL);
		if (size == INVALID_FILE_SIZE || size > maxBytes)
		{
			Log("settings: the settings file is missing a size or is implausibly large, ignoring it");
			CloseHandle(file);
			return false;
		}

		char* text = (char*)malloc(size + 1);
		if (!text)
		{
			CloseHandle(file);
			return false;
		}
		DWORD read = 0;
		ReadFile(file, text, size, &read, NULL);
		CloseHandle(file);
		text[read] = 0;

		int applied = 0, skipped = 0;
		char* cursor = text;
		while (*cursor)
		{
			char* lineEnd = cursor;
			while (*lineEnd && *lineEnd != '\n')
				++lineEnd;
			const bool lastLine = (*lineEnd == 0);
			*lineEnd = 0;

			char line[256];
			strncpy_s(line, sizeof(line), cursor, _TRUNCATE);
			cursor = lastLine ? lineEnd : lineEnd + 1;

			TrimInPlace(line);
			if (!line[0] || line[0] == '#' || line[0] == ';')
				continue;

			char* equals = strchr(line, '=');
			if (!equals)
			{
				++skipped;
				continue;
			}
			*equals = 0;
			char* key = line;
			char* value = equals + 1;
			TrimInPlace(key);
			TrimInPlace(value);

			const Setting* setting = FindSetting(key);
			if (!setting)
			{
				Log("settings: '%s' is not a setting this build knows, skipped", key);
				++skipped;
				continue;
			}

			if (setting->kind == SettingText)
			{
				WriteSettingText(*setting, value);
				++applied;
				continue;
			}

			const float parsed = (float)atof(value);
			if (parsed < setting->minimum || parsed > setting->maximum)
			{
				Log("settings: '%s' was %s, outside %.3g to %.3g, keeping the default",
				    key, value, setting->minimum, setting->maximum);
				++skipped;
				continue;
			}

			WriteSetting(*setting, parsed);
			++applied;
		}

		free(text);
		Log("settings: loaded %d value%s, skipped %d", applied, applied == 1 ? "" : "s", skipped);
		return applied > 0;
	}

	unsigned long SharedSettingsHash()
	{
		// Added rather than mixed in sequence, so registration order does not matter.
		// Two peers running builds that declared the same settings in a different
		// order still agree when the values do, and the hash only moves when a value
		// or a key actually differs.
		unsigned long total = 0;
		for (int i = 0; i < tableCount; ++i)
		{
			const Setting& setting = table[i];
			if (setting.scope != SettingShared)
				continue;

			unsigned long bits;
			if (setting.kind == SettingText)
			{
				bits = Fnv1a(AsText(setting));
			}
			else
			{
				const float value = ReadSetting(setting);
				memcpy(&bits, &value, sizeof(bits));
			}
			total += Fnv1a(setting.key) * 31u + bits;
		}
		return total;
	}

} // namespace workshop
