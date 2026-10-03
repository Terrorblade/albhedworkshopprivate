#include "workshop/Log.h"

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace workshop
{
	namespace
	{

		CRITICAL_SECTION fileLock;
		bool fileLockReady = false;
		wchar_t logFileName[64] = L"workshop.log";
		char statusLine[128] = "";

		// Builds <game dir>\AlBhedWorkshop\<logFileName>. Returns false if the host path cannot
		// be read, which should never happen but is not worth crashing over.
		bool LogFilePath(wchar_t* out, size_t count)
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

			_snwprintf_s(out, count, _TRUNCATE, L"%s\\AlBhedWorkshop\\%s", exePath, logFileName);
			return true;
		}

	} // namespace

	void OpenLog(const wchar_t* fileName)
	{
		if (fileName && *fileName)
			wcsncpy_s(logFileName, _countof(logFileName), fileName, _TRUNCATE);
		InitializeCriticalSection(&fileLock);
		fileLockReady = true;
	}

	void Log(const char* format, ...)
	{
		char body[768];
		va_list args;
		va_start(args, format);
		_vsnprintf_s(body, sizeof(body), _TRUNCATE, format, args);
		va_end(args);

		SYSTEMTIME now;
		GetLocalTime(&now);
		char line[900];
		_snprintf_s(line, sizeof(line), _TRUNCATE, "[%02d:%02d:%02d.%03d] %s\r\n",
		    now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, body);
		OutputDebugStringA(line);

		wchar_t path[MAX_PATH];
		if (!LogFilePath(path, MAX_PATH))
			return;

		if (fileLockReady)
			EnterCriticalSection(&fileLock);
		HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
		    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file != INVALID_HANDLE_VALUE)
		{
			DWORD written = 0;
			WriteFile(file, line, (DWORD)strlen(line), &written, NULL);
			CloseHandle(file);
		}
		if (fileLockReady)
			LeaveCriticalSection(&fileLock);
	}

	void SetStatus(const char* format, ...)
	{
		char body[sizeof(statusLine)];
		va_list args;
		va_start(args, format);
		_vsnprintf_s(body, sizeof(body), _TRUNCATE, format, args);
		va_end(args);

		// A UI thread reads this while the game thread writes it. A torn read is
		// cosmetic, but running off the end of the array would not be, so only ever
		// write into [0 .. n-2] and leave the final byte as a permanent NUL. That
		// makes an unsynchronised reader safe without taking a lock on the frame path.
		strncpy_s(statusLine, sizeof(statusLine) - 1, body, _TRUNCATE);
		statusLine[sizeof(statusLine) - 1] = 0;
	}

	const char* Status()
	{
		return statusLine;
	}

} // namespace workshop
