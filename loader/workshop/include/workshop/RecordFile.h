#pragma once

#include <windows.h>

// Fixed size binary records appended from a hot thread and written to disk by another.
//
// Built for recording from inside the game's step: the producer copies a record into
// memory and returns, and a single shared writer thread does every WriteFile. So the
// step never waits on a disk, and a slow disk costs dropped records, counted, rather
// than a stalled game.
//
//     RecordFile* f = OpenRecordFile(L"C:\\...\\pacing.bin", &header, sizeof(header), 160);
//     // on the producer thread, per record:
//     void* r = ReserveRecord(f);     // NULL means the budget is spent, the record is dropped
//     if (r) { Fill(r); CommitRecord(f); }
//     // once the producer has stopped for good:
//     CloseRecordFile(f);             // blocks until everything is on disk
//
// THE FILE IS ALWAYS PARSEABLE. The header goes in first, synchronously, and after that
// only whole records are ever written, in order. A crash or a kill loses at most the
// records still in memory, and a reader recovers the count from the file size. The
// header therefore has no record count in it, on purpose.
//
// THREADS. One producer per file, and it must be the same thread for the life of the
// file. Open and Close may be called from any other thread, but Close must not race the
// producer: stop producing first, make sure the producer has seen it, then close. The
// probe plugin does that with a counter around each record, see its Capture.cpp.
//
// AGEING. A chunk that has held records for about a second is handed to the writer even
// if it is not full, so a slow trickle of records still reaches the disk promptly. The
// check is a GetTickCount, which reads shared memory and is not a syscall.

namespace workshop
{

	struct RecordFile;

	struct RecordFileStats
	{
		unsigned long long recordsCommitted;
		unsigned long long recordsDropped; // the memory budget was spent
		unsigned long long bytesWritten;   // records only, the header is not counted
		DWORD writeErrors;                 // a failed or short WriteFile, see the log
	};

	// Creates or truncates path and writes the header. headerBytes may be 0. Returns NULL
	// and logs on failure. budgetBytes caps the memory this file may hold waiting for the
	// disk. 0 picks 64 MB.
	RecordFile* OpenRecordFile(const wchar_t* path, const void* header, DWORD headerBytes,
	    DWORD recordBytes, DWORD budgetBytes = 0);

	// Producer thread only. Space for one record, zero filled, or NULL when the budget is
	// spent, in which case the drop is already counted and nothing else is needed.
	// Every non-NULL Reserve must be followed by exactly one Commit before the next Reserve.
	void* ReserveRecord(RecordFile* file);
	void CommitRecord(RecordFile* file);

	// Reserve, copy, Commit. False when the record was dropped.
	bool AppendRecord(RecordFile* file, const void* record);

	DWORD RecordFileRecordBytes(const RecordFile* file);
	void ReadRecordFileStats(const RecordFile* file, RecordFileStats* out);

	// Hands the partial chunk to the writer, waits for the disk, closes the handle and
	// frees the file. The producer must already have stopped.
	void CloseRecordFile(RecordFile* file);

	// For DLL_PROCESS_DETACH when the process is terminating. The writer thread is gone by
	// then, so this writes whatever is still in memory synchronously, after cutting each
	// file back to the last record the writer is known to have finished. Does nothing for
	// a file that has been closed.
	void FlushRecordFilesAtProcessExit();

} // namespace workshop
