#pragma once

#include <windows.h>

// Read a shipped data file into memory at runtime, using FFX.exe's own file layer.
//
//     int bytes = 0;
//     void* file = ffx::ReadDataFile("ffx_ps2/ffx/proj/event/header/eventid.bin", &bytes);
//     if (file)
//     {
//         // bytes is 6741 and the first dword is 3216
//         ffx::FreeDataFile(file);
//     }
//
// Pass the plain archive path, lowercase with forward slashes and no leading slash, exactly
// as tools\vbf.py prints it. The root prefix the engine's MD5 lookup expects is added here.
//
// Nothing in here parses the archive. The call goes to FFX_fios_openFile, which checks the
// five mounted VBF slots first and falls back to a loose file on disk, so a loose copy of a
// path the archive does not hold also works.
//
// SAFE FROM ANY THREAD, including a render thread ImGui panel. The read is synchronous on
// the calling thread, the only lock it takes is released before it returns, and the request
// pool it waits on has 30 slots. It is not safe before the game has booted, because the VBF
// manager does not exist until FFX_InitFileSystem has run. DataFileSystemReady says when.
//
// It does BLOCK, though. A cold read is a seek plus an fread plus a zlib inflate, so budget
// for disk latency and do not do it every frame. Read once and cache.
//
// Read reversing\DATA_FILES.md for the call recipe and what else is worth reading.

namespace ffx
{

	// False until FFX_InitFileSystem has created the VBF manager, which is early in boot
	// but not at DllMain time. Every other call here returns a failure before this is
	// true, so checking it is optional, it is there so a panel can say why.
	bool DataFileSystemReady();

	// Is this path in a mounted archive, or on disk as a loose file? Opens nothing.
	bool DataFileExists(const char* archivePath);

	// Reads the whole file. Returns null on any failure, including a missing path.
	// outBytes is optional and is set to the byte count on success, 0 on failure.
	//
	// The buffer is one byte longer than the file and that extra byte is a NUL, so a text
	// file comes back usable as a C string without a copy. outBytes does NOT count it.
	//
	// The buffer comes from the process heap, not the engine's allocator, so pass it to
	// FreeDataFile and nothing else.
	void* ReadDataFile(const char* archivePath, int* outBytes);

	// Null is fine and does nothing.
	void FreeDataFile(void* buffer);

	// Size without reading the contents. 0 for a missing path. This still opens and closes
	// a stream, so if you are about to read the file anyway just read it.
	int DataFileSize(const char* archivePath);

	// ---------------------------------------------------------------------------
	// Listing what is in the archives
	// ---------------------------------------------------------------------------

	// Every mounted archive keeps its whole header resident, including the path strings, so
	// enumerating the shipped file list costs a pointer walk and no I/O. 71,979 entries in
	// FFX_Data.vbf.
	//
	// This is how a picker gets its options for anything that is a FILE rather than a table
	// row, models and maps most obviously:
	//
	//     ffx::DataFileEntry e;
	//     for (int i = 0; ffx::DataFileEntryAt(i, &e); ++i)
	//         if (strstr(e.path, "/chr/mon/") && strstr(e.path, ".chr"))
	//             list.Add(i, e.path);
	//
	// The index is a flat index across all five mount slots, and it is stable for as long
	// as nothing mounts or unmounts, which on a retail boot is forever.
	struct DataFileEntry
	{
		const char* path; // points into the engine's resident header, do not free or write
		int bytes;        // uncompressed size, clamped to INT_MAX
		int slot;         // which of the 5 mount slots it came from, 0 wins a duplicate
	};

	// 0 before the file system is up, or if the header does not read back as plausible.
	int DataFileCount();

	// False for an out of range index or an unreadable header.
	bool DataFileEntryAt(int index, DataFileEntry* out);

	// What a startup check and a control panel want: mount slots, entry counts, root path.
	void LogDataFiles();

} // namespace ffx
