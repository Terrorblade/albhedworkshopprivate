#pragma once

#include <windows.h>

// Walks and validates the engine's own heap.
//
// This exists to catch heap corruption at a point of our choosing instead of waiting
// for the allocator to trip over it. The allocator discovers a smashed header only when
// it next coalesces across it, which can be seconds and thousands of allocations after
// the damage, by which time the stack names an innocent caller and none of our frames
// are on it. Call VerifyEngineHeap before and after anything of ours that pokes engine
// memory and the first call that fails names the phase that did it.
//
// Every live block header carries Rva::HeapBlockMagic, so this is a real check rather
// than a plausibility test. See addresses/Memory.h for the layout and where it came
// from.
//
// COSTS A FULL WALK OF EVERY BLOCK IN THE HEAP, which is hundreds of thousands of them
// once the game is loaded. Do not call it per frame. Bracket a suspicious phase with it,
// or put it behind a button.
//
// Takes the engine's own allocator critical section for the duration, so an allocation
// on another thread waits rather than being walked through. Without that the walk reads
// headers mid-update and reports damage that was never there.

namespace ffx
{
	// WHICH ANOMALIES ACTUALLY MATTER, and the answer is not the obvious one.
	//
	// Neither sub_943130 nor sub_9435A0 ever reads the magic at header+0. The
	// allocator writes it and never looks at it again, so a block whose magic is gone
	// is not a block the allocator will trip over. It is still a real sign that
	// something wrote where it should not have, which is worth knowing, but it is not
	// the crash.
	//
	// What the allocator does read on every free is the previous block's TAG at +12,
	// which it treats as a free list node pointer the moment it is not 2. That is the
	// dereference in every crash report on this project. The back offset at +8 matters
	// just as much, because it is what decides which block the tag is read from.
	//
	// So a walk reports these separately: Fatal means the allocator will fault on it,
	// Cosmetic means something was overwritten but the allocator does not care.
	enum EngineHeapFindingKind
	{
		HeapFindingNone = 0,
		HeapFindingCosmetic, // a lost magic, which the allocator never reads
		HeapFindingFatal,    // a tag, size or back offset the allocator will fault on
	};

	struct EngineHeapFinding
	{
		EngineHeapFindingKind kind;
		DWORD header;
		DWORD pool;
		const char* why; // a static string
	};

	struct EngineHeapStats
	{
		// ok is false only when something FATAL was found. A heap with nothing but lost
		// magics still reports ok, with cosmetic > 0, because that is the honest answer
		// about whether the allocator is going to crash.
		bool ok;

		int pools;
		DWORD span;       // bytes per pool, as the heap negotiated at startup
		int blocks;       // headers walked
		int inUse;        // blocks with the in-use tag
		int free;         // blocks carrying a free list node
		DWORD inUseBytes; // payload plus header, so it sums to the span
		DWORD freeBytes;

		int fatal;    // headers the allocator will fault on
		int cosmetic; // headers that merely lost their magic

		// The first finding of each kind, which is the one worth printing. A walk keeps
		// going after either, so these are a sample and the counts are the total.
		EngineHeapFinding firstFatal;
		EngineHeapFinding firstCosmetic;

		DWORD badBlock;   // firstFatal's header if there is one, else firstCosmetic's
		DWORD badPool;
		const char* why;  // a static string, safe to log after this returns

		// How far the walk got before it stopped, so a partial answer is still
		// readable. A walk that ends early because a header is in a decommitted page
		// is not a failure, it is the allocator having given pages back.
		bool complete;
	};

	// False before the first engine allocation, which includes the whole of DllMain.
	bool EngineHeapReady();

	// The heap descriptor, or null when it does not exist yet. Exposed because a
	// plugin may want to read the span or the pool chain itself.
	const DWORD* EngineHeapDescriptor();

	// Walks every pool and every block. Returns out->ok. Never throws and never
	// faults: every header read is guarded.
	bool VerifyEngineHeap(EngineHeapStats* out);

	// Puts a heap the game has damaged back into a state the allocator can survive.
	//
	// THIS WRITES TO THE ENGINE'S HEAP METADATA, so it is opt in and it is deliberately
	// conservative. It only ever does two things:
	//
	//   * restores a lost magic, which changes no behaviour at all because nothing
	//     reads it, and makes the next walk quieter so a NEW problem stands out.
	//   * takes a block whose tag is neither 2 nor a usable free list node and marks it
	//     in use, which costs that one block and nothing else. An in-use block is never
	//     coalesced across and its tag is never dereferenced, so the fault cannot
	//     happen. The block leaks. A leak of one block beats losing the process.
	//
	// It will not touch a block whose size or back offset is wrong, because those
	// describe the chain itself and guessing at them could corrupt a heap that was only
	// slightly damaged. Those are reported and left alone.
	//
	// Returns how many blocks it changed.
	int RepairEngineHeap();

	// Verifies and writes one line to the log. "when" names the moment, so a log reads
	// like "engine heap: ok after the event table load". On a failure it writes the
	// detail instead, including the bytes around the bad header and the block before
	// it, which is the one that most likely overflowed.
	//
	// Returns true when the heap is intact.
	bool LogEngineHeap(const char* when);

	// Allocates and frees the way the engine does, through its own allocator rather
	// than the CRT. Here because a plugin that wants to hand a buffer to an engine
	// function has to allocate it from the engine's heap, since the engine will free
	// it with FFX_Free. Both are null safe and both return false when the heap is not
	// up yet.
	void* EngineAlloc(DWORD bytes);
	bool EngineFree(void* p);

	// The size the allocator recorded for a block the engine handed out, read from its
	// header, or 0 when p does not look like one of its blocks. Useful for checking
	// that an engine buffer is as big as its contents want to be before writing into
	// it.
	DWORD EngineBlockSize(const void* payload);
} // namespace ffx
