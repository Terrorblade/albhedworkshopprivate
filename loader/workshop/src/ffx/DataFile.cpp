#include "ffx/DataFile.h"

#include "ffx/addresses/DataFile.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

#include <string.h>

namespace ffx
{

	using namespace workshop;

	namespace
	{

		// 9 for the engine's own "../../../" root, and there is no reason for a mount root
		// to be long, so a value past this means the manager pointer is wrong.
		const int MaxRootPathLen = 64;

		// The longest path in the shipped FFX_Data.vbf is 109 characters. 512 leaves room
		// for a much deeper tree and still keeps the buffer on the stack.
		const int MaxLookupPath = 512;

		// FFX_Data.vbf holds 71,979 entries and FFX2_Data.vbf 137,386. A count past this
		// means the header pointer is garbage rather than that the archive is big.
		const int MaxEntriesSane = 4 * 1000 * 1000;

		// The engine's file handle is {HANDLE, FFX_VbfStream*} and nothing else, so a
		// caller's two dwords are a complete one. Both halves are checked directly rather
		// than through FFX_fios_FileHandle__isOpen, which is a three instruction test on
		// exactly these two fields.
		struct FileHandle
		{
			DWORD osHandle;
			DWORD vbfStream;
		};

		typedef int(__thiscall* OpenFileFn)(void* self, const char* path, int readOnly,
		    int unused4, int unused5, int unused6);
		typedef DWORD(__thiscall* GetSizeFn)(void* self);
		typedef DWORD(__thiscall* ReadFn)(void* self, void* buffer, DWORD bytes);
		typedef char(__thiscall* CloseFn)(void* self);
		typedef bool(__cdecl* ExistsFn)(const char* path);

		bool FiosReady()
		{
			return LooksLikeFunctionStart(Rva::FiosOpenFile) &&
			       LooksLikeFunctionStart(Rva::FiosFileHandleGetSize) &&
			       LooksLikeFunctionStart(Rva::FiosFileHandleRead) &&
			       LooksLikeFunctionStart(Rva::FiosFileHandleClose) &&
			       LooksLikeFunctionStart(Rva::FiosFileExists);
		}

		const BYTE* Manager()
		{
			const DWORD* slot = (const DWORD*)ModuleAddress(Rva::VbfManagerPtr);
			if (!Readable(slot, sizeof(DWORD)))
				return nullptr;

			const BYTE* manager = (const BYTE*)(UINT_PTR)*slot;
			if (!Readable(manager, Rva::VbfManagerBytes))
				return nullptr;
			return manager;
		}

		// How many bytes the manager will skip before it hashes. Negative means the
		// manager is not usable.
		int RootPathLen(const BYTE* manager)
		{
			DWORD held = *(const DWORD*)(manager + Rva::VbfManagerRootPathLen);
			if (held > (DWORD)MaxRootPathLen)
				return -1;
			return (int)held;
		}

		// Builds the string FFX_VbfManager__openFileStream wants: rootPathLen bytes of
		// prefix, then the plain archive path. The manager skips the prefix without
		// looking at it, so what matters is only that it is the right LENGTH, but the
		// manager's own root string is used when it reads back cleanly so that the loose
		// file fallback gets a path that means something too.
		bool BuildLookupPath(const BYTE* manager, const char* archivePath, char* out,
		    int outBytes)
		{
			if (!archivePath || !*archivePath)
				return false;

			int rootLen = RootPathLen(manager);
			if (rootLen < 0)
				return false;

			size_t pathLen = strlen(archivePath);
			if ((int)pathLen + rootLen + 1 > outBytes)
				return false;

			const char* root = *(const char* const*)(manager + Rva::VbfManagerRootPath);
			if (rootLen > 0 && Readable(root, (SIZE_T)rootLen + 1) &&
			    strlen(root) == (size_t)rootLen)
			{
				memcpy(out, root, (size_t)rootLen);
			}
			else
			{
				// Any filler of the right length produces the same MD5, because the
				// manager hashes from out + rootLen. Dots keep it a legal relative path.
				memset(out, '.', (size_t)rootLen);
			}

			memcpy(out + rootLen, archivePath, pathLen + 1);
			return true;
		}

		bool Open(const char* archivePath, FileHandle* handle)
		{
			handle->osHandle = 0xFFFFFFFFu;
			handle->vbfStream = 0;

			if (!FiosReady())
				return false;

			const BYTE* manager = Manager();
			if (!manager)
				return false;

			char path[MaxLookupPath];
			if (!BuildLookupPath(manager, archivePath, path, MaxLookupPath))
				return false;

			// readOnly must be 1. With 0 the loose file fallback opens with OPEN_ALWAYS and
			// would create an empty file on disk for a path that does not exist.
			OpenFileFn open = (OpenFileFn)ModuleAddress(Rva::FiosOpenFile);
			open(handle, path, 1, 0, 0, 1);

			return handle->osHandle != 0xFFFFFFFFu || handle->vbfStream != 0;
		}

		void Close(FileHandle* handle)
		{
			// Always, even when the read failed. The 15 stream slots per archive are
			// shared with the engine's own loader.
			CloseFn close = (CloseFn)ModuleAddress(Rva::FiosFileHandleClose);
			close(handle);
		}

		// ---------------------------------------------------------------------------
		// The resident archive headers, for listing
		// ---------------------------------------------------------------------------

		struct Mount
		{
			const BYTE* headerBegin;
			const BYTE* headerEnd;
			const BYTE* entries;
			int count;
		};

		Mount g_mounts[Rva::VbfManagerSlotCount];
		int g_mountsUsed = 0;
		int g_mountsTotal = 0;
		const BYTE* g_mountsFor = nullptr;
		bool g_mountsBuilt = false;

		// One slot's header, or false when it holds nothing usable. The layout here is the
		// same arithmetic tools/vbf.py uses, and that reader is verified byte for byte
		// against the shipped archive.
		bool ReadMount(const BYTE* archive, Mount* out)
		{
			if (!Readable(archive, Rva::VbfArchiveStreams + 4))
				return false;

			const BYTE* header =
			    *(const BYTE* const*)(archive + Rva::VbfArchiveHeader);
			if (!Readable(header, 16))
				return false;

			DWORD headerBytes = *(const DWORD*)(header + 4);
			DWORD count = *(const DWORD*)(header + Rva::VbfHeaderEntryCount);
			if (count == 0 || count > (DWORD)MaxEntriesSane)
				return false;

			// Where the 32 byte rows start, and the smallest header that could hold them.
			DWORD entriesAt = Rva::VbfHeaderKeyArray + 16 * count;
			DWORD entriesEnd = entriesAt + Rva::VbfEntryBytes * count;
			if (headerBytes < entriesEnd + 4)
				return false;

			// The whole block in one go, so walking it afterwards is a range check rather
			// than a VirtualQuery per entry. A 9.8 MB allocation is its own region, so
			// this is expected to hold, and if it ever does not the slot is skipped and
			// LogDataFiles says which.
			if (!Readable(header, headerBytes))
				return false;

			out->headerBegin = header;
			out->headerEnd = header + headerBytes;
			out->entries = header + entriesAt;
			out->count = (int)count;
			return true;
		}

		void EnsureMounts()
		{
			const BYTE* manager = Manager();
			if (g_mountsBuilt && g_mountsFor == manager)
				return;

			g_mountsBuilt = true;
			g_mountsFor = manager;
			g_mountsUsed = 0;
			g_mountsTotal = 0;
			if (!manager)
				return;

			for (int slot = 0; slot < (int)Rva::VbfManagerSlotCount; ++slot)
			{
				DWORD held = *(const DWORD*)(manager + Rva::VbfManagerSlots + 4 * slot);
				if (held == 0)
					continue;

				Mount built;
				if (!ReadMount((const BYTE*)(UINT_PTR)held, &built))
				{
					Log("datafile: mount slot %d is present but its header did not read "
					    "back, so it will not be listed",
					    slot);
					continue;
				}

				g_mounts[g_mountsUsed] = built;
				++g_mountsUsed;
				g_mountsTotal += built.count;
			}
		}

	} // namespace

	bool DataFileSystemReady()
	{
		const BYTE* manager = Manager();
		return manager != nullptr && RootPathLen(manager) >= 0 && FiosReady();
	}

	bool DataFileExists(const char* archivePath)
	{
		if (!FiosReady())
			return false;

		const BYTE* manager = Manager();
		if (!manager)
			return false;

		char path[MaxLookupPath];
		if (!BuildLookupPath(manager, archivePath, path, MaxLookupPath))
			return false;

		ExistsFn exists = (ExistsFn)ModuleAddress(Rva::FiosFileExists);
		return exists(path);
	}

	int DataFileSize(const char* archivePath)
	{
		FileHandle handle;
		if (!Open(archivePath, &handle))
			return 0;

		GetSizeFn getSize = (GetSizeFn)ModuleAddress(Rva::FiosFileHandleGetSize);
		DWORD bytes = getSize(&handle);
		Close(&handle);

		if (bytes > 0x7FFFFFFFu)
			return 0;
		return (int)bytes;
	}

	void* ReadDataFile(const char* archivePath, int* outBytes)
	{
		if (outBytes)
			*outBytes = 0;

		FileHandle handle;
		if (!Open(archivePath, &handle))
			return nullptr;

		GetSizeFn getSize = (GetSizeFn)ModuleAddress(Rva::FiosFileHandleGetSize);
		DWORD bytes = getSize(&handle);
		if (bytes == 0 || bytes > 0x7FFFFFFEu)
		{
			Close(&handle);
			Log("datafile: %s opened but its size came back as %u", archivePath,
			    (unsigned)bytes);
			return nullptr;
		}

		// One byte past the end, kept at zero, so a text file is usable as a C string.
		BYTE* buffer = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes + 1);
		if (!buffer)
		{
			Close(&handle);
			Log("datafile: out of memory for %u bytes of %s", (unsigned)bytes,
			    archivePath);
			return nullptr;
		}

		// A VBF read clamps to the bytes left in the entry, so one read of the full size
		// from position 0 pulls the whole file and there is no loop to write here.
		ReadFn read = (ReadFn)ModuleAddress(Rva::FiosFileHandleRead);
		DWORD got = read(&handle, buffer, bytes);
		Close(&handle);

		if (got != bytes)
		{
			Log("datafile: %s is %u bytes but only %u came back", archivePath,
			    (unsigned)bytes, (unsigned)got);
			HeapFree(GetProcessHeap(), 0, buffer);
			return nullptr;
		}

		if (outBytes)
			*outBytes = (int)bytes;
		return buffer;
	}

	void FreeDataFile(void* buffer)
	{
		if (buffer)
			HeapFree(GetProcessHeap(), 0, buffer);
	}

	int DataFileCount()
	{
		EnsureMounts();
		return g_mountsTotal;
	}

	bool DataFileEntryAt(int index, DataFileEntry* out)
	{
		if (!out || index < 0)
			return false;

		EnsureMounts();
		if (index >= g_mountsTotal)
			return false;

		int mount = 0;
		while (mount < g_mountsUsed && index >= g_mounts[mount].count)
		{
			index -= g_mounts[mount].count;
			++mount;
		}
		if (mount >= g_mountsUsed)
			return false;

		const Mount& held = g_mounts[mount];
		const BYTE* row = held.entries + (size_t)index * Rva::VbfEntryBytes;

		// Everything below is inside the block ReadMount already proved readable, so these
		// are range checks and not memory probes.
		if (row < held.headerBegin || row + Rva::VbfEntryBytes > held.headerEnd)
			return false;

		const char* name = *(const char* const*)(row + Rva::VbfEntryNamePtr);
		if ((const BYTE*)name < held.headerBegin || (const BYTE*)name >= held.headerEnd)
			return false;

		// The name has to terminate before the block does, or it is not a path.
		const char* scan = name;
		while (scan < (const char*)held.headerEnd && *scan)
			++scan;
		if (scan >= (const char*)held.headerEnd)
			return false;

		unsigned __int64 size = *(const unsigned __int64*)(row + Rva::VbfEntrySize);
		if (size > 0x7FFFFFFFu)
			size = 0x7FFFFFFFu;

		out->path = name;
		out->bytes = (int)size;
		out->slot = mount;
		return true;
	}

	void LogDataFiles()
	{
		const BYTE* manager = Manager();
		if (!manager)
		{
			Log("datafile: the VBF manager is null, so the file system is not up yet");
			return;
		}

		int rootLen = RootPathLen(manager);
		const char* root = *(const char* const*)(manager + Rva::VbfManagerRootPath);
		bool rootOk = rootLen > 0 && Readable(root, (SIZE_T)rootLen + 1);
		Log("datafile: manager 0x%08X, root \"%s\" (%d bytes skipped before the md5), "
		    "fios %s",
		    (unsigned)(UINT_PTR)manager, rootOk ? root : "<unreadable>", rootLen,
		    FiosReady() ? "ready" : "NOT CALLABLE");

		EnsureMounts();
		Log("datafile: %d of %d mount slots usable, %d entries in total", g_mountsUsed,
		    (int)Rva::VbfManagerSlotCount, g_mountsTotal);
		for (int i = 0; i < g_mountsUsed; ++i)
			Log("datafile: mount %d  header 0x%08X..0x%08X, %d entries", i,
			    (unsigned)(UINT_PTR)g_mounts[i].headerBegin,
			    (unsigned)(UINT_PTR)g_mounts[i].headerEnd, g_mounts[i].count);
	}

} // namespace ffx
