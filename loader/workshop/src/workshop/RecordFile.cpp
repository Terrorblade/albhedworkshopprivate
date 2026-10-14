#include "workshop/RecordFile.h"

#include <stdlib.h>
#include <string.h>

#include "workshop/Log.h"

namespace workshop
{
	namespace
	{

		// A run of whole records. data follows the struct in the same allocation.
		struct Chunk
		{
			Chunk* next;
			RecordFile* owner;
			DWORD used;
			DWORD startTick;
		};

		BYTE* ChunkData(Chunk* chunk)
		{
			return (BYTE*)(chunk + 1);
		}

		// How long a partly filled chunk may sit before the producer hands it over anyway.
		const DWORD AgeLimitMs = 1000;

		// The smallest chunk worth a WriteFile, and how many records a chunk holds at least.
		const DWORD MinChunkBytes = 256 * 1024;
		const DWORD MinRecordsPerChunk = 16;

		const DWORD DefaultBudgetBytes = 64u * 1024u * 1024u;

		// How many files may be open at once. Each probe recipe is one file.
		const int MaxOpenFiles = 16;

		// How long Close waits for the writer before giving up on the rest.
		const DWORD CloseTimeoutMs = 15000;

	} // namespace

	struct RecordFile
	{
		HANDLE handle;
		DWORD headerBytes;
		DWORD recordBytes;
		DWORD chunkBytes;
		LONG maxChunks;
		LONG chunksAllocated;

		// Producer only.
		Chunk* current;

		// Under the lock.
		Chunk* freeList;
		Chunk* writing;                  // the chunk the writer has popped and not finished
		unsigned long long writingStart; // durableBytes when that write began

		// Chunks queued or being written. Interlocked.
		volatile LONG pending;

		// The file offset up to which every byte is a finished write, header included.
		// Written by the writer under the lock.
		unsigned long long durableBytes;

		volatile LONGLONG committed;
		volatile LONGLONG dropped;
		volatile LONGLONG bytesWritten;
		volatile LONG writeErrors;
	};

	namespace
	{

		INIT_ONCE g_initOnce = INIT_ONCE_STATIC_INIT;
		CRITICAL_SECTION g_lock;
		HANDLE g_wake = NULL;
		HANDLE g_thread = NULL;

		// The write queue, oldest first, across every file.
		Chunk* g_queueHead = NULL;
		Chunk* g_queueTail = NULL;

		RecordFile* g_files[MaxOpenFiles];

		BOOL CALLBACK InitShared(PINIT_ONCE, PVOID, PVOID*)
		{
			InitializeCriticalSection(&g_lock);
			g_wake = CreateEventW(NULL, FALSE, FALSE, NULL);
			return TRUE;
		}

		bool WriteAll(HANDLE handle, const BYTE* data, DWORD bytes)
		{
			while (bytes > 0)
			{
				DWORD wrote = 0;
				if (!WriteFile(handle, data, bytes, &wrote, NULL) || wrote == 0)
					return false;
				data += wrote;
				bytes -= wrote;
			}
			return true;
		}

		DWORD WINAPI WriterThread(LPVOID)
		{
			for (;;)
			{
				WaitForSingleObject(g_wake, 200);
				for (;;)
				{
					EnterCriticalSection(&g_lock);
					Chunk* chunk = g_queueHead;
					if (chunk)
					{
						g_queueHead = chunk->next;
						if (!g_queueHead)
							g_queueTail = NULL;
						chunk->next = NULL;
						chunk->owner->writing = chunk;
						chunk->owner->writingStart = chunk->owner->durableBytes;
					}
					LeaveCriticalSection(&g_lock);
					if (!chunk)
						break;

					RecordFile* file = chunk->owner;
					const bool ok = WriteAll(file->handle, ChunkData(chunk), chunk->used);
					if (!ok)
					{
						// The file position is now unknown, so nothing after this point can be
						// trusted to land on a record boundary. Log once per error and keep
						// going, the reader truncates to whole records either way.
						InterlockedIncrement(&file->writeErrors);
						Log("record file: WriteFile failed with %lu, %lu bytes may be missing",
						    GetLastError(), chunk->used);
					}

					EnterCriticalSection(&g_lock);
					if (ok)
					{
						file->durableBytes += chunk->used;
						InterlockedExchangeAdd64(&file->bytesWritten, chunk->used);
					}
					file->writing = NULL;
					chunk->next = file->freeList;
					file->freeList = chunk;
					LeaveCriticalSection(&g_lock);

					InterlockedDecrement(&file->pending);
				}
			}
		}

		bool StartWriter()
		{
			InitOnceExecuteOnce(&g_initOnce, &InitShared, NULL, NULL);
			if (!g_wake)
				return false;
			if (g_thread)
				return true;
			g_thread = CreateThread(NULL, 0, &WriterThread, NULL, 0, NULL);
			if (!g_thread)
			{
				Log("record file: could not start the writer thread, error %lu", GetLastError());
				return false;
			}
			return true;
		}

		Chunk* TakeChunk(RecordFile* file)
		{
			EnterCriticalSection(&g_lock);
			Chunk* chunk = file->freeList;
			if (chunk)
				file->freeList = chunk->next;
			LeaveCriticalSection(&g_lock);

			if (!chunk && file->chunksAllocated < file->maxChunks)
			{
				chunk = (Chunk*)malloc(sizeof(Chunk) + file->chunkBytes);
				if (chunk)
					++file->chunksAllocated;
			}
			if (chunk)
			{
				chunk->next = NULL;
				chunk->owner = file;
				chunk->used = 0;
				chunk->startTick = GetTickCount();
			}
			return chunk;
		}

		void Submit(RecordFile* file, Chunk* chunk)
		{
			if (chunk->used == 0)
			{
				EnterCriticalSection(&g_lock);
				chunk->next = file->freeList;
				file->freeList = chunk;
				LeaveCriticalSection(&g_lock);
				return;
			}
			InterlockedIncrement(&file->pending);
			EnterCriticalSection(&g_lock);
			chunk->next = NULL;
			if (g_queueTail)
				g_queueTail->next = chunk;
			else
				g_queueHead = chunk;
			g_queueTail = chunk;
			LeaveCriticalSection(&g_lock);
			SetEvent(g_wake);
		}

		void FreeList(Chunk* chunk)
		{
			while (chunk)
			{
				Chunk* next = chunk->next;
				free(chunk);
				chunk = next;
			}
		}

	} // namespace

	RecordFile* OpenRecordFile(const wchar_t* path, const void* header, DWORD headerBytes,
	    DWORD recordBytes, DWORD budgetBytes)
	{
		if (!path || recordBytes == 0 || (headerBytes && !header))
			return NULL;
		if (!StartWriter())
			return NULL;

		HANDLE handle = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
		    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		if (handle == INVALID_HANDLE_VALUE)
		{
			Log("record file: could not create %S, error %lu", path, GetLastError());
			return NULL;
		}
		if (headerBytes && !WriteAll(handle, (const BYTE*)header, headerBytes))
		{
			Log("record file: could not write the header of %S, error %lu", path, GetLastError());
			CloseHandle(handle);
			return NULL;
		}

		RecordFile* file = (RecordFile*)calloc(1, sizeof(RecordFile));
		if (!file)
		{
			CloseHandle(handle);
			return NULL;
		}
		file->handle = handle;
		file->headerBytes = headerBytes;
		file->recordBytes = recordBytes;

		DWORD perChunk = MinChunkBytes / recordBytes;
		if (perChunk < MinRecordsPerChunk)
			perChunk = MinRecordsPerChunk;
		file->chunkBytes = perChunk * recordBytes;

		const DWORD budget = budgetBytes ? budgetBytes : DefaultBudgetBytes;
		file->maxChunks = (LONG)(budget / file->chunkBytes);
		if (file->maxChunks < 2)
			file->maxChunks = 2;
		file->durableBytes = headerBytes;

		EnterCriticalSection(&g_lock);
		bool registered = false;
		for (int i = 0; i < MaxOpenFiles; ++i)
		{
			if (!g_files[i])
			{
				g_files[i] = file;
				registered = true;
				break;
			}
		}
		LeaveCriticalSection(&g_lock);
		if (!registered)
		{
			Log("record file: %d files are already open, refusing %S", MaxOpenFiles, path);
			CloseHandle(handle);
			free(file);
			return NULL;
		}
		return file;
	}

	void* ReserveRecord(RecordFile* file)
	{
		if (!file)
			return NULL;

		Chunk* chunk = file->current;
		if (chunk && chunk->used > 0 && (chunk->used + file->recordBytes > file->chunkBytes || GetTickCount() - chunk->startTick > AgeLimitMs))
		{
			Submit(file, chunk);
			chunk = NULL;
			file->current = NULL;
		}
		if (!chunk)
		{
			chunk = TakeChunk(file);
			file->current = chunk;
		}
		if (!chunk)
		{
			InterlockedIncrement64(&file->dropped);
			return NULL;
		}
		if (chunk->used == 0)
			chunk->startTick = GetTickCount();

		BYTE* record = ChunkData(chunk) + chunk->used;
		memset(record, 0, file->recordBytes);
		return record;
	}

	void CommitRecord(RecordFile* file)
	{
		if (!file || !file->current)
			return;
		file->current->used += file->recordBytes;
		InterlockedIncrement64(&file->committed);
	}

	bool AppendRecord(RecordFile* file, const void* record)
	{
		void* space = ReserveRecord(file);
		if (!space)
			return false;
		memcpy(space, record, file->recordBytes);
		CommitRecord(file);
		return true;
	}

	DWORD RecordFileRecordBytes(const RecordFile* file)
	{
		return file ? file->recordBytes : 0;
	}

	void ReadRecordFileStats(const RecordFile* file, RecordFileStats* out)
	{
		if (!out)
			return;
		ZeroMemory(out, sizeof(*out));
		if (!file)
			return;
		out->recordsCommitted = (unsigned long long)file->committed;
		out->recordsDropped = (unsigned long long)file->dropped;
		out->bytesWritten = (unsigned long long)file->bytesWritten;
		out->writeErrors = (DWORD)file->writeErrors;
	}

	void CloseRecordFile(RecordFile* file)
	{
		if (!file)
			return;

		if (file->current)
		{
			Submit(file, file->current);
			file->current = NULL;
		}

		const DWORD start = GetTickCount();
		while (file->pending > 0)
		{
			if (GetTickCount() - start > CloseTimeoutMs)
			{
				Log("record file: the writer did not finish within %lu ms, closing with %ld "
				    "chunks unwritten",
				    CloseTimeoutMs, file->pending);
				break;
			}
			Sleep(1);
		}

		EnterCriticalSection(&g_lock);
		for (int i = 0; i < MaxOpenFiles; ++i)
			if (g_files[i] == file)
				g_files[i] = NULL;
		const bool safeToFree = file->pending == 0;
		Chunk* spare = file->freeList;
		file->freeList = NULL;
		LeaveCriticalSection(&g_lock);

		FreeList(spare);
		CloseHandle(file->handle);

		// A timed out close leaves chunks the writer still owns pointing at this record, so
		// it is leaked rather than freed. That only happens on a dead disk.
		if (safeToFree)
			free(file);
	}

	void FlushRecordFilesAtProcessExit()
	{
		// No lock. Every other thread is gone and one of them may have died holding it, so
		// taking it here could hang the exit. The reads below are of a stopped world.
		for (int i = 0; i < MaxOpenFiles; ++i)
		{
			RecordFile* file = g_files[i];
			if (!file)
				continue;

			// Back to the last finished write, so whatever follows lands on a record
			// boundary even if the writer died halfway through a WriteFile.
			LARGE_INTEGER at;
			at.QuadPart = (LONGLONG)file->durableBytes;
			if (!SetFilePointerEx(file->handle, at, NULL, FILE_BEGIN) || !SetEndOfFile(file->handle))
				continue;

			// The chunk the writer was busy with, unless its write already counted.
			Chunk* busy = file->writing;
			if (busy && file->writingStart == file->durableBytes)
				WriteAll(file->handle, ChunkData(busy), busy->used);

			for (Chunk* chunk = g_queueHead; chunk; chunk = chunk->next)
				if (chunk->owner == file)
					WriteAll(file->handle, ChunkData(chunk), chunk->used);

			if (file->current && file->current->used)
				WriteAll(file->handle, ChunkData(file->current), file->current->used);

			CloseHandle(file->handle);
			g_files[i] = NULL;
		}
	}

} // namespace workshop
