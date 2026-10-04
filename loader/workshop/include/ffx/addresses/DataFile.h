#pragma once

#include <windows.h>

// Reading a shipped data file at runtime, through the engine's own file layer.
//
// Almost every name table the game has lives inside data\FFX_Data.vbf rather than in the
// exe: item names, ability names, monster names, the 402 entry event id table, the whole
// chr and map trees. A UI that wants to be built from game data has to get in there, and
// the engine already has everything needed, so nothing here reimplements the archive.
//
// Two separate routes, and they answer different questions.
//
// READ ONE FILE. FFX_fios_openFile on your own 8 byte handle, then getSize, read, close.
// ffx/DataFile.h wraps it, prefer that over touching these.
//
//     DWORD h[2] = { 0xFFFFFFFF, 0 };
//     openFile(h, "../../../ffx_ps2/ffx/proj/event/header/eventid.bin", 1, 0, 0, 1);
//
// LIST EVERY FILE. The archive header never leaves memory, so the complete path list is a
// pointer walk. See VbfArchiveHeader below. This is how a model picker or a map picker
// gets its options without a hardcoded table.
//
// Read reversing\DATA_FILES.md for the call recipe and the thread safety answer, and
// reversing\VBF_FORMAT.md for the on-disk format.

namespace ffx
{
	namespace Rva
	{

		// ---------------------------------------------------------------------------
		// The fios file handle, which is the layer to actually call
		// ---------------------------------------------------------------------------

		// An FFX_fios_FileHandle is only 8 bytes and has no vtable, so a caller can keep
		// one on the stack:
		//     +0 HANDLE  0xFFFFFFFF when there is no loose file
		//     +4 stream  FFX_VbfStream*, 0 when the path is not in an archive
		// Exactly one of the two is live. Every method below branches on +4 first, so the
		// same handle serves an archived file and a loose one.
		const DWORD FiosFileHandleBytes = 8;

		// int __thiscall (handle, const char* path, int readOnly, 0, 0, 1).
		// Five stack dwords, retn 14h. Only path and readOnly are read, the last three are
		// ignored, and FFX_File_Open passes 0, 0, 1 so the kit does too.
		//
		// Returns 0 on success, 1 for a null or empty path, 10 when nothing opened, and 12
		// for the odd case where opening for write failed but opening for read worked.
		// Check with IsOpen rather than the return value.
		//
		// readOnly MUST be nonzero. With 0 the loose file fallback uses CreateFileW with
		// OPEN_ALWAYS, which CREATES an empty file on disk when the path is missing.
		//
		// The path is normalised into a stack copy, so the caller's string is never
		// touched: backslashes become forward slashes and the whole thing is lowercased
		// before the archive lookup. A separate UTF-16 copy of the ORIGINAL goes to
		// CreateFileW if the archive misses.
		//
		// Side effect worth knowing: every call logs through Phyre_TtyPrintf at level 2 as
		// "[FFX_section_data_win32] %s", so a loop over a thousand files writes a thousand
		// lines through whatever the engine's TTY callback is.
		const DWORD FiosOpenFile = 0x00207F40;

		// _DWORD* __thiscall (handle, path, readOnly, 0, 0, 1). Zeroes the handle to
		// {-1, 0} and calls FiosOpenFile. Fine to use instead of initialising by hand.
		const DWORD FiosFileHandleCtor = 0x00207BC0;

		// bool __thiscall (handle). True when either half of the pair is live. This is the
		// success test.
		const DWORD FiosFileHandleIsOpen = 0x00207F20;

		// DWORD __thiscall (handle). The uncompressed size. For an archived file it is the
		// u64 at entry+8 truncated to the low dword, for a loose file GetFileSizeEx's low
		// dword. 0 when nothing is open.
		const DWORD FiosFileHandleGetSize = 0x00207DC0;

		// DWORD __thiscall (handle, void* buffer, DWORD bytes). Returns the byte count.
		// The archive path clamps to the bytes left in the entry, so one read of GetSize
		// bytes from position 0 pulls the whole file.
		const DWORD FiosFileHandleRead = 0x00208090;

		// DWORD __thiscall (handle, LONG distance, DWORD method). method is the Win32 set
		// of 0 begin, 1 current, 2 end. Returns the new position, or -1 out of range.
		const DWORD FiosFileHandleSeek = 0x002080E0;

		// char __thiscall (handle). Always returns 1. Releases the stream back to the
		// archive's pool and, for a loose file, SetEndOfFile then CloseHandle. ALWAYS call
		// this, the 15 stream slots are shared with the engine's own loader.
		const DWORD FiosFileHandleClose = 0x00207D80;

		// bool __cdecl (const char* path). Same normalisation as the open, then an archive
		// existence check with a CreateFileW OPEN_EXISTING fallback. Opens no stream.
		const DWORD FiosFileExists = 0x00207E00;

		// Unused by the kit but part of the same object: write, skip, and the empty ctor.
		const DWORD FiosFileHandleWrite = 0x00208160;
		const DWORD FiosFileHandleSkip = 0x00208120;
		const DWORD FiosFileHandleCtorEmpty = 0x00207C00;

		// char* __cdecl (char* path). Lowercases in place. The open already does this, so
		// a caller never needs it.
		const DWORD FiosToLowerInPlace = 0x00207D50;

		// ---------------------------------------------------------------------------
		// The named C wrapper, and why the kit does NOT use it
		// ---------------------------------------------------------------------------

		// _DWORD* __cdecl (const char* path, char readOnly). Heap allocates the 8 byte
		// handle and calls the ctor.
		//
		// DO NOT CALL THIS FOR A PATH THAT MIGHT BE ABSENT. On failure it pops a localised
		// MessageBoxW out of string resource 30774 and then sends WM_CLOSE to the game
		// window, so a typo in a plugin's path string shuts the game down. The fios layer
		// above just reports the failure.
		const DWORD FileOpen = 0x00279730;
		const DWORD FileRead = 0x00279800;  // DWORD __cdecl (handle, buffer, bytes)
		const DWORD FileClose = 0x00279510; // int __cdecl (handle), 0 clean, 9 otherwise
		const DWORD FileGetSize = 0x00279570;
		const DWORD FileExists = 0x002796F0;
		const DWORD FileSeek = 0x00279710;

		// ---------------------------------------------------------------------------
		// The VBF manager
		// ---------------------------------------------------------------------------

		// 68 bytes:
		//     +0  HCRYPTPROV        CryptAcquireContextA, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT
		//     +4  HANDLE            request pool semaphore, created at count 30
		//     +8  DWORD             pool block count, 30
		//     +12 void*             pool block array, 96 bytes each
		//     +16 VbfArchive*[5]    the mount slots
		//     +36 CRITICAL_SECTION  24 bytes, guards the request pool bookkeeping
		//     +60 DWORD             rootPathLen
		//     +64 char*             rootPath copy
		// Nothing in it changes after FFX_InitFileSystem.
		const DWORD VbfManagerPtr = 0x008C9C48; // g_ffxVbfManager, 0 before init
		const DWORD VbfManagerBytes = 68;
		const DWORD VbfManagerSlots = 16; // VbfArchive*[5]
		const DWORD VbfManagerSlotCount = 5;
		const DWORD VbfManagerCritSec = 36;
		const DWORD VbfManagerRootPathLen = 60;
		const DWORD VbfManagerRootPath = 64;

		// int __cdecl (). Returns g_ffxVbfManager.
		const DWORD VbfManagerGet = 0x0021BDB0;

		// THE MD5 PATH RULE, which is the one real trap in all of this.
		//
		// Both lookups below hash the string starting rootPathLen BYTES IN, confirmed from
		// the disassembly as "add esi, [edi+3Ch]" with esi the path argument. They do not
		// check the prefix, they just skip it. FFX_InitFileSystem sets the root to
		// "../../../", so rootPathLen is 9 on this build.
		//
		// So the string you pass has to be rootPathLen filler bytes followed by the plain
		// archive path, and the key is md5() of the archive path alone, lowercased, forward
		// slashes, no leading slash. Verified: the stored key for
		// ffx_ps2/ffx/proj/event/header/eventid.bin is 9c18e87364e52beebc431648d290b6a0,
		// which is md5 of exactly that string.
		//
		// Read rootPathLen at runtime rather than assuming 9, and use the manager's own
		// root string as the filler so the loose file fallback gets a sane path too.
		const DWORD VbfRootPathLen = 9;

		// int __thiscall (manager, const char* path). Returns an FFX_VbfStream* bound to
		// the entry, or 0 when no mount holds the key. Walks slots 0..4 and takes the FIRST
		// hit, so the lowest numbered slot wins a duplicate path.
		const DWORD VbfManagerOpenFileStream = 0x0021BF10;

		// char __thiscall (manager, const char* path). Existence only, opens no stream.
		const DWORD VbfManagerFileExists = 0x0021BE40;

		// int __thiscall, 7 args. The streaming read, called from FFX_VbfStream__read when
		// the request is not already in the stream's 64 KiB window. Does the fread and the
		// inflate inline on the CALLING thread, which is what makes the whole path safe to
		// call from anywhere. See DATA_FILES.md.
		const DWORD VbfManagerReadStream = 0x0021BFE0;

		const DWORD VbfManagerMountArchive = 0x0021C150;
		const DWORD VbfManagerSetRootPath = 0x0021C3A0;
		const DWORD VbfManagerCreateSingleton = 0x0021B590;
		const DWORD VbfManagerCtor = 0x0021BDD0;

		// Sets the data root, "../../..", 8 characters and no trailing slash. Note that is
		// NOT the VBF root, which has the slash and so is 9. FFX_fiosUnifyFilename
		// concatenates this with a path that already starts with a slash, which is how the
		// engine's own 9 byte prefix comes out.
		const DWORD FiosDataRootPtr = 0x008C9698; // g_ffxDataRoot
		const DWORD FiosGetDataRoot = 0x00207D30;
		const DWORD FiosSetDataRoot = 0x00207D40;
		const DWORD FiosUnifyFilename = 0x00279820;
		const DWORD InitFileSystem = 0x002795B0;

		// ---------------------------------------------------------------------------
		// The request pool, which is the whole blocking story
		// ---------------------------------------------------------------------------

		// int __thiscall (pool, DWORD timeoutMs). WaitForSingleObject on the semaphore and
		// then an InterlockedCompareExchange scan for a free 96 byte block.
		//
		// The pool holds 30 blocks and the semaphore starts at 30, and a block is released
		// by the same thread at the end of FFX_VbfManager__readStream, so there is no other
		// thread to wait for. 30 reads have to be in flight before anything blocks.
		const DWORD VbfReqPoolAcquireBlock = 0x0021B4A0;
		const DWORD VbfReqPoolInit = 0x0021C2C0;
		const DWORD VbfReqPoolBeginRequest = 0x0021B740;
		const DWORD VbfReqPoolBlocks = 30;

		// ---------------------------------------------------------------------------
		// VbfStream, 32 bytes, 15 preallocated per archive
		// ---------------------------------------------------------------------------

		//     +0  BYTE   inUse
		//     +4  FILE*  this slot's own fopen handle on the .vbf
		//     +8  VbfArchive* owner
		//     +12 BYTE*  the 32 byte entry row
		//     +16 u64    read position
		//     +24 void*  24 byte cache block, [0] is a 64 KiB window
		const DWORD VbfStreamBytes = 32;
		const DWORD VbfStreamEntry = 12;
		const DWORD VbfStreamPosition = 16;

		const DWORD VbfStreamCtor = 0x0021A860;
		const DWORD VbfStreamDtor = 0x0021A960;
		const DWORD VbfStreamBindEntry = 0x0021AA10;
		const DWORD VbfStreamRelease = 0x0021AA50;        // back to the pool
		const DWORD VbfStreamReadAndAdvance = 0x0021AA60; // read then bump the position
		const DWORD VbfStreamRead = 0x0021AA90;           // no position update
		const DWORD VbfStreamSeek = 0x0021AC30;
		const DWORD VbfStreamGetSize = 0x0021ACB0;
		const DWORD VbfStreamOpenHandle = 0x0021ACC0;

		// ---------------------------------------------------------------------------
		// VbfArchive, and the resident header that makes a runtime file listing free
		// ---------------------------------------------------------------------------

		//     +0  void*  the ENTIRE header block, 9,861,832 bytes for FFX_Data.vbf
		//     +4  Md5Map red-black tree, one node per entry, built during open
		//     +52 DWORD  total chunk count
		//     +56 u16*   chunk stored size table, points into the header block
		//     +60 BYTE*  per chunk raw/compressed flags
		//     +64 char*  the .vbf path, 260 byte buffer
		//     +68 CRITICAL_SECTION guarding the stream slots
		//     +92 void*  the 15 stream slots, 32 bytes each
		const DWORD VbfArchiveHeader = 0;
		const DWORD VbfArchivePath = 64;
		const DWORD VbfArchiveCritSec = 68;
		const DWORD VbfArchiveStreams = 92;
		const DWORD VbfArchiveStreamSlots = 15;

		// Inside the resident header block, all offsets in bytes from header+0:
		//     +0           u32 magic 0x4B595253
		//     +4           u32 headerSize
		//     +8           u32 entryCount
		//     +16          md5 key array, 16 * entryCount
		//     ENTRIES      16 + 16 * entryCount, then 32 bytes each
		//     nameBlobSize u32 at ENTRIES + 32 * entryCount
		//     nameBlob     4 bytes after that
		//
		// And per 32 byte entry row:
		//     +0x08 u64    uncompressed size
		//     +0x18 char*  ABSOLUTE pointer to the lowercased path. The stored value on
		//                  disk is relative to the name blob and FFX_VbfArchive__open
		//                  rewrites it in place, which is why this is already a pointer.
		//
		// That makes listing every shipped file a pointer walk with no I/O and no MD5.
		// 71,979 entries in FFX_Data.vbf. The arithmetic matches tools/vbf.py, which is
		// verified byte for byte against the shipped archive.
		const DWORD VbfHeaderEntryCount = 8;
		const DWORD VbfHeaderKeyArray = 16;
		const DWORD VbfEntryBytes = 32;
		const DWORD VbfEntrySize = 8;
		const DWORD VbfEntryNamePtr = 24;

		const DWORD VbfArchiveOpen = 0x0021DAC0;
		const DWORD VbfArchiveAcquireStream = 0x0021D9C0;
		const DWORD VbfArchiveReleaseStream = 0x0021DF70;
		const DWORD VbfArchiveHasEntryMd5 = 0x0021DEC0;
		const DWORD VbfArchiveOpenStreamByMd5 = 0x0021DF10;
		const DWORD Md5MapFind = 0x0021D140;
		const DWORD Md5MapInsert = 0x0021DE30;

		inline const DWORD* DataFileRvaList(int* count)
		{
			// Struct offsets, sizes and counts are deliberately not in this list. The
			// startup check range checks everything here against the image size, and 68 is
			// not an address.
			static const DWORD list[] = {
				FiosOpenFile,
				FiosFileHandleCtor,
				FiosFileHandleIsOpen,
				FiosFileHandleGetSize,
				FiosFileHandleRead,
				FiosFileHandleSeek,
				FiosFileHandleClose,
				FiosFileExists,
				FiosFileHandleWrite,
				FiosFileHandleSkip,
				FiosFileHandleCtorEmpty,
				FiosToLowerInPlace,
				FileOpen,
				FileRead,
				FileClose,
				FileGetSize,
				FileExists,
				FileSeek,
				VbfManagerPtr,
				VbfManagerGet,
				VbfManagerOpenFileStream,
				VbfManagerFileExists,
				VbfManagerReadStream,
				VbfManagerMountArchive,
				VbfManagerSetRootPath,
				VbfManagerCreateSingleton,
				VbfManagerCtor,
				FiosDataRootPtr,
				FiosGetDataRoot,
				FiosSetDataRoot,
				FiosUnifyFilename,
				InitFileSystem,
				VbfReqPoolAcquireBlock,
				VbfReqPoolInit,
				VbfReqPoolBeginRequest,
				VbfStreamCtor,
				VbfStreamDtor,
				VbfStreamBindEntry,
				VbfStreamRelease,
				VbfStreamReadAndAdvance,
				VbfStreamRead,
				VbfStreamSeek,
				VbfStreamGetSize,
				VbfStreamOpenHandle,
				VbfArchiveOpen,
				VbfArchiveAcquireStream,
				VbfArchiveReleaseStream,
				VbfArchiveHasEntryMd5,
				VbfArchiveOpenStreamByMd5,
				Md5MapFind,
				Md5MapInsert,
			};

			if (count)
				*count = (int)(sizeof(list) / sizeof(list[0]));

			return list;
		}
	} // namespace Rva
} // namespace ffx
