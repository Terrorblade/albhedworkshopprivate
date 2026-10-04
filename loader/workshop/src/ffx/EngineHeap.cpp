#include "ffx/EngineHeap.h"

#include "ffx/Addresses.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

#include <stdio.h>

namespace ffx
{
	using workshop::Log;
	using workshop::LooksLikePointer;
	using workshop::ModuleAddress;
	using workshop::Readable;

	namespace
	{
		// A CRITICAL_SECTION is 24 bytes on win32 and the descriptor is the next thing
		// in the same malloc, so this is the offset from the lock to the heap.
		const DWORD kDescriptorOffset = sizeof(CRITICAL_SECTION);

		// Fields of the descriptor, in dwords. See addresses/Memory.h.
		const int kSpan = 0;
		const int kFirstPool = 3;

		// Fields of a block header, in bytes.
		const DWORD kMagic = 0;
		const DWORD kSize = 4;
		const DWORD kBackOffset = 8;
		const DWORD kTag = 12;
		const DWORD kHeaderBytes = 16;

		// A walk that finds more blocks than this has gone wrong, because the smallest
		// block is 16 bytes and the span is well under 768 MB. Stops a corrupt size
		// from turning the walk into a hang.
		const int kMaxBlocksPerPool = 8 * 1000 * 1000;
		const int kMaxPools = 64;

		LPCRITICAL_SECTION HeapLock()
		{
			LPCRITICAL_SECTION* slot = (LPCRITICAL_SECTION*)ModuleAddress(Rva::HeapCriticalSection);
			if (!Readable(slot, sizeof(void*)))
				return NULL;

			LPCRITICAL_SECTION cs = *slot;
			if (!LooksLikePointer((DWORD)(DWORD_PTR)cs))
				return NULL;

			// The lock and the descriptor come from one malloc(0x30), so both have to be
			// readable before either is touched.
			if (!Readable(cs, kDescriptorOffset + 6 * sizeof(DWORD)))
				return NULL;

			return cs;
		}

		// Records a finding and keeps the first of each kind. The walk does not stop on
		// one, because the whole point is to find the fatal one even when a harmless
		// lost magic came first.
		void Note(EngineHeapStats* out, EngineHeapFindingKind kind, DWORD header, DWORD pool,
		    const char* why)
		{
			EngineHeapFinding f;
			f.kind = kind;
			f.header = header;
			f.pool = pool;
			f.why = why;

			if (kind == HeapFindingFatal)
			{
				if (out->fatal == 0)
					out->firstFatal = f;
				++out->fatal;
			}
			else
			{
				if (out->cosmetic == 0)
					out->firstCosmetic = f;
				++out->cosmetic;
			}
		}

		// ---------------------------------------------------------------------------
		// A CACHED REGION, BECAUSE workshop::Readable IS A VirtualQuery SYSCALL.
		//
		// The first version of this walk called Readable once per field, four times per
		// block, and a loaded heap has forty thousand blocks. That is a hundred and
		// seventy thousand syscalls and it stalled the boot for over two seconds, which
		// is the same mistake that once cost this project a ten second freeze in the
		// event name lookup.
		//
		// VirtualQuery already answers for a whole region at a time, so one call covers
		// a long run of committed pages. This holds the last answer and only asks again
		// when an address falls outside it. The engine's heap is one big reservation
		// with pages committed on demand, so in practice a walk of a whole pool makes a
		// handful of calls rather than one per field.
		//
		// NOT THREAD SAFE AND IT DOES NOT NEED TO BE: every caller holds the engine's
		// allocator critical section, and the allocator is the only thing that commits
		// or decommits pages in this range.
		// ---------------------------------------------------------------------------
		struct RegionCache
		{
			DWORD base;
			DWORD end;
			bool readable;
			bool valid;
		};

		RegionCache g_region = { 0, 0, false, false };

		void ForgetRegionCache()
		{
			g_region.valid = false;
		}

		bool RegionReadable(DWORD address)
		{
			if (g_region.valid && address >= g_region.base && address < g_region.end)
				return g_region.readable;

			MEMORY_BASIC_INFORMATION mbi;
			if (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)) != sizeof(mbi))
			{
				g_region.valid = false;
				return false;
			}

			const DWORD base = (DWORD)(DWORD_PTR)mbi.BaseAddress;
			g_region.base = base;
			g_region.end = base + (DWORD)mbi.RegionSize;
			g_region.readable = mbi.State == MEM_COMMIT
			    && (mbi.Protect
			           & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ
			               | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
			        != 0
			    && (mbi.Protect & PAGE_GUARD) == 0;
			g_region.valid = true;

			return g_region.readable;
		}

		bool ReadDword(DWORD address, DWORD* out)
		{
			// A dword never straddles a region boundary here: every header is 16 byte
			// aligned and regions are page aligned, so testing the first byte is enough.
			if (!RegionReadable(address))
				return false;
			*out = *(const DWORD*)address;
			return true;
		}

		// For the hex dumps, which are off the hot path and may look at an address the
		// walk never visited.
		bool ReadableSpan(DWORD address, DWORD bytes)
		{
			return Readable((const void*)address, bytes);
		}
	} // namespace

	const DWORD* EngineHeapDescriptor()
	{
		LPCRITICAL_SECTION cs = HeapLock();
		if (!cs)
			return NULL;
		return (const DWORD*)((const BYTE*)cs + kDescriptorOffset);
	}

	bool EngineHeapReady()
	{
		const DWORD* heap = EngineHeapDescriptor();
		if (!heap)
			return false;

		// Before the first allocation the span is set but the pool chain is empty.
		return heap[kSpan] != 0 && LooksLikePointer(heap[kFirstPool]);
	}

	bool VerifyEngineHeap(EngineHeapStats* out)
	{
		if (!out)
			return false;

		EngineHeapStats blank;
		blank.ok = false;
		blank.pools = 0;
		blank.span = 0;
		blank.blocks = 0;
		blank.inUse = 0;
		blank.free = 0;
		blank.inUseBytes = 0;
		blank.freeBytes = 0;
		blank.fatal = 0;
		blank.cosmetic = 0;
		blank.firstFatal.kind = HeapFindingNone;
		blank.firstFatal.header = 0;
		blank.firstFatal.pool = 0;
		blank.firstFatal.why = NULL;
		blank.firstCosmetic = blank.firstFatal;
		blank.badBlock = 0;
		blank.badPool = 0;
		blank.why = "the heap does not exist yet";
		blank.complete = false;
		*out = blank;

		LPCRITICAL_SECTION cs = HeapLock();
		if (!cs)
			return false;

		const DWORD* heap = (const DWORD*)((const BYTE*)cs + kDescriptorOffset);
		const DWORD span = heap[kSpan];
		out->span = span;

		if (span < Rva::HeapPoolFirstBlock + 2 * kHeaderBytes || (span & 0xFFF) != 0)
		{
			out->why = "the pool span is not a sane page multiple";
			return false;
		}

		if (!LooksLikePointer(heap[kFirstPool]))
		{
			out->why = "no pool has been created yet, so nothing has been allocated";
			out->ok = true; // an empty heap is not a corrupt heap
			out->complete = true;
			return true;
		}

		// HOLD THE ALLOCATOR LOCK FOR THE WHOLE WALK. Another thread allocating halfway
		// through leaves headers half written and the walk reports damage that was
		// never there.
		EnterCriticalSection(cs);
		ForgetRegionCache();

		DWORD pool = heap[kFirstPool];
		bool complete = true;
		bool chainLost = false;

		for (int poolIndex = 0; poolIndex < kMaxPools && pool != 0 && !chainLost; ++poolIndex)
		{
			++out->pools;

			const DWORD poolEnd = pool + span;
			const DWORD lastHeader = poolEnd - kHeaderBytes;

			DWORD header = pool + Rva::HeapPoolFirstBlock;
			int blocks = 0;
			DWORD previous = 0;

			for (; blocks < kMaxBlocksPerPool; ++blocks)
			{
				if (header > lastHeader)
				{
					// Landing exactly on the end is correct. Landing past it means a
					// size field lied, and the walk cannot be trusted from here.
					if (header != poolEnd)
						Note(out, HeapFindingFatal, previous, pool,
						    "the block chain overran the end of its pool, so a size field is wrong");
					break;
				}

				DWORD magic = 0;
				if (!ReadDword(header + kMagic, &magic))
				{
					// A header in a page the allocator gave back. Not damage, but the
					// walk cannot continue past it.
					complete = false;
					break;
				}

				DWORD size = 0;
				DWORD back = 0;
				DWORD tag = 0;
				if (!ReadDword(header + kSize, &size) || !ReadDword(header + kBackOffset, &back)
				    || !ReadDword(header + kTag, &tag))
				{
					complete = false;
					break;
				}

				// SIZE FIRST, because the walk needs it to find the next header, and a
				// wrong one makes everything after this meaningless.
				if (size < kHeaderBytes || (size & 0xF) != 0 || header + size > poolEnd)
				{
					Note(out, HeapFindingFatal, header, pool,
					    "a block size is not a sane multiple of 16 inside the pool, so the chain stops here");
					chainLost = true;
					break;
				}

				// A LOST MAGIC IS NOT FATAL. Nothing in the allocator reads it, so the
				// walk records it and carries on with a chain that is still intact.
				if (magic != Rva::HeapBlockMagic)
				{
					Note(out, HeapFindingCosmetic, header, pool,
					    "a block header has lost its magic, so something wrote over it, but the allocator never reads it");
				}

				// The back offset decides which block a free reads a tag from, so a
				// wrong one here is how a good block's free ends up dereferencing
				// whatever happens to sit 12 bytes into the wrong place.
				if (previous != 0 && header - back != previous)
				{
					Note(out, HeapFindingFatal, header, pool,
					    "a block's back offset does not point at the previous block, so a free will read a tag out of the wrong place");
				}

				if (tag == Rva::HeapTagInUse || tag == Rva::HeapTagFreeUnlisted)
				{
					++out->inUse;
					out->inUseBytes += size;
				}
				else if (!LooksLikePointer(tag) || !ReadableSpan(tag, 16))
				{
					// THE CRASH, CAUGHT BEFORE IT HAPPENS. The allocator will treat
					// this as a free list node and load its prev at +4.
					Note(out, HeapFindingFatal, header, pool,
					    "a free block's tag is not a readable free list node, which is exactly what the allocator faults on");
					out->freeBytes += size;
					++out->free;
				}
				else
				{
					DWORD nodeBlock = 0;
					if (ReadDword(tag, &nodeBlock) && nodeBlock != header)
					{
						Note(out, HeapFindingFatal, header, pool,
						    "a free block's node does not point back at the block, so unlinking it will corrupt the list");
					}
					++out->free;
					out->freeBytes += size;
				}

				previous = header;
				header += size;
			}

			out->blocks += blocks;

			if (blocks >= kMaxBlocksPerPool)
			{
				Note(out, HeapFindingFatal, 0, pool,
				    "the block chain did not end, so a size field is looping the walk");
				chainLost = true;
			}

			if (chainLost)
				break;

			DWORD next = 0;
			if (!ReadDword(pool + Rva::HeapPoolNextOffset, &next))
			{
				complete = false;
				break;
			}
			pool = LooksLikePointer(next) ? next : 0;
		}

		LeaveCriticalSection(cs);

		out->complete = complete;
		out->ok = out->fatal == 0;

		if (out->fatal > 0)
		{
			out->why = out->firstFatal.why;
			out->badBlock = out->firstFatal.header;
			out->badPool = out->firstFatal.pool;
		}
		else if (out->cosmetic > 0)
		{
			out->why = out->firstCosmetic.why;
			out->badBlock = out->firstCosmetic.header;
			out->badPool = out->firstCosmetic.pool;
		}
		else
		{
			out->why = complete ? "every header validated"
			                    : "every header walked validated, but the walk stopped at a decommitted page";
		}

		return out->ok;
	}

	namespace
	{
		// The 16 bytes of a header as hex, for a log line. Returns false when the
		// header is not readable, so the caller can say so instead of printing junk.
		bool HeaderBytes(DWORD header, char* out, int bytes)
		{
			const BYTE* p = (const BYTE*)header;
			if (!ReadableSpan(header, 16))
				return false;

			int n = 0;
			for (int i = 0; i < 16; ++i)
			{
				const int wrote = _snprintf_s(out + n, (size_t)(bytes - n), _TRUNCATE,
				    i == 0 ? "%02X" : " %02X", p[i]);
				if (wrote <= 0)
					return false;
				n += wrote;
			}
			return true;
		}

		// Prints one finding with the bytes of its header and of the block in front,
		// which is the one that most likely wrote over it.
		void LogFinding(const EngineHeapFinding& f, const char* severity, DWORD span)
		{
			Log("engine heap: %s at header 0x%08X in pool 0x%08X: %s", severity,
			    (unsigned)f.header, (unsigned)f.pool, f.why ? f.why : "no reason recorded");

			if (f.header == 0)
				return;

			char hex[80];
			if (HeaderBytes(f.header, hex, sizeof(hex)))
				Log("engine heap:   it reads %s  (magic, size, back offset, tag)", hex);

			DWORD back = 0;
			if (ReadDword(f.header + kBackOffset, &back) && back >= kHeaderBytes && back < span
			    && f.header - back >= f.pool)
			{
				const DWORD prev = f.header - back;
				if (HeaderBytes(prev, hex, sizeof(hex)))
					Log("engine heap:   the block in front, 0x%08X, reads %s. If its size "
					    "is sane then it is the one that overflowed.",
					    (unsigned)prev, hex);
			}
		}
	} // namespace

	bool LogEngineHeap(const char* when)
	{
		const char* label = when ? when : "no moment given";

		// TIMED ON PURPOSE. A full walk is exactly the shape that turns into a stall
		// nobody notices, and an earlier version of this took over two seconds because
		// it asked the kernel whether each field was readable. If this line ever reads
		// in whole seconds again, that is the reason.
		// Zeroed because the && below short circuits, so without this the compiler is
		// right to say "before" may never be written.
		LARGE_INTEGER freq;
		LARGE_INTEGER before;
		LARGE_INTEGER after;
		freq.QuadPart = 0;
		before.QuadPart = 0;
		after.QuadPart = 0;
		const bool timed = QueryPerformanceFrequency(&freq) && QueryPerformanceCounter(&before);

		EngineHeapStats s;
		const bool ok = VerifyEngineHeap(&s);

		double ms = 0.0;
		if (timed && QueryPerformanceCounter(&after) && freq.QuadPart != 0)
			ms = (double)(after.QuadPart - before.QuadPart) * 1000.0 / (double)freq.QuadPart;

		if (ok && s.cosmetic == 0)
		{
			Log("engine heap: ok %s. %d pool%s of %u KB, %d blocks, %u KB in use, "
			    "%u KB free, walked in %.1f ms%s",
			    label, s.pools, s.pools == 1 ? "" : "s", (unsigned)(s.span / 1024), s.blocks,
			    (unsigned)(s.inUseBytes / 1024), (unsigned)(s.freeBytes / 1024), ms,
			    s.complete ? "" : ", walk stopped early at a decommitted page");
			return true;
		}

		if (ok)
		{
			// SURVIVABLE. Something wrote over header magics, which says the game is
			// scribbling, but the allocator never reads that field so it will not fault
			// on this. Worth one line, not an alarm.
			Log("engine heap: survivable %s. %d block%s lost the magic at +0, which the "
			    "allocator never reads, and nothing it DOES read is wrong. %d blocks "
			    "walked in %d pool%s in %.1f ms.",
			    label, s.cosmetic, s.cosmetic == 1 ? "" : "s", s.blocks, s.pools,
			    s.pools == 1 ? "" : "s", ms);
			LogFinding(s.firstCosmetic, "lost magic", s.span);
			return true;
		}

		Log("ENGINE HEAP WILL FAULT %s: %d fatal finding%s and %d lost magic%s, after "
		    "%d blocks in %d pool%s.",
		    label, s.fatal, s.fatal == 1 ? "" : "s", s.cosmetic, s.cosmetic == 1 ? "" : "s",
		    s.blocks, s.pools, s.pools == 1 ? "" : "s");
		LogFinding(s.firstFatal, "FATAL", s.span);
		if (s.cosmetic > 0)
			LogFinding(s.firstCosmetic, "lost magic", s.span);
		return false;
	}

	int RepairEngineHeap()
	{
		LPCRITICAL_SECTION cs = HeapLock();
		if (!cs)
			return 0;

		const DWORD* heap = (const DWORD*)((const BYTE*)cs + kDescriptorOffset);
		const DWORD span = heap[kSpan];
		if (span < Rva::HeapPoolFirstBlock + 2 * kHeaderBytes || (span & 0xFFF) != 0)
			return 0;
		if (!LooksLikePointer(heap[kFirstPool]))
			return 0;

		EnterCriticalSection(cs);
		ForgetRegionCache();

		DWORD pool = heap[kFirstPool];
		int magicsRestored = 0;
		int tagsNeutralised = 0;
		bool chainLost = false;

		for (int poolIndex = 0; poolIndex < kMaxPools && pool != 0 && !chainLost; ++poolIndex)
		{
			const DWORD poolEnd = pool + span;
			const DWORD lastHeader = poolEnd - kHeaderBytes;
			DWORD header = pool + Rva::HeapPoolFirstBlock;

			for (int blocks = 0; blocks < kMaxBlocksPerPool; ++blocks)
			{
				if (header > lastHeader)
					break;

				DWORD magic = 0;
				DWORD size = 0;
				DWORD tag = 0;
				if (!ReadDword(header + kMagic, &magic) || !ReadDword(header + kSize, &size)
				    || !ReadDword(header + kTag, &tag))
					break;

				// THE CHAIN IS NOT REPAIRED, ONLY WALKED. A bad size means this walk
				// cannot know where the next header is, and guessing would turn a
				// small problem into a destroyed heap.
				if (size < kHeaderBytes || (size & 0xF) != 0 || header + size > poolEnd)
				{
					chainLost = true;
					break;
				}

				if (magic != Rva::HeapBlockMagic)
				{
					*(DWORD*)(header + kMagic) = Rva::HeapBlockMagic;
					++magicsRestored;
				}

				// A tag that is neither in use nor a usable node is the one the
				// allocator faults on. Marking it in use costs this block and makes
				// the fault impossible: an in-use block is never coalesced across and
				// its tag is never dereferenced.
				if (tag != Rva::HeapTagInUse && tag != Rva::HeapTagFreeUnlisted)
				{
					bool usable = LooksLikePointer(tag) && ReadableSpan(tag, 16);
					if (usable)
					{
						DWORD nodeBlock = 0;
						if (!ReadDword(tag, &nodeBlock) || nodeBlock != header)
							usable = false;
					}

					if (!usable)
					{
						*(DWORD*)(header + kTag) = Rva::HeapTagInUse;
						++tagsNeutralised;
						Log("engine heap: REPAIRED the tag on block 0x%08X, which held "
						    "0x%08X instead of a free list node. The block is now marked "
						    "in use, so it leaks %u bytes and the allocator cannot fault "
						    "on it.",
						    (unsigned)header, (unsigned)tag, (unsigned)size);
					}
				}

				header += size;
			}

			DWORD next = 0;
			if (!ReadDword(pool + Rva::HeapPoolNextOffset, &next))
				break;
			pool = LooksLikePointer(next) ? next : 0;
		}

		LeaveCriticalSection(cs);

		if (magicsRestored > 0 || tagsNeutralised > 0)
		{
			Log("engine heap: repair restored %d magic%s and neutralised %d unusable "
			    "tag%s.%s",
			    magicsRestored, magicsRestored == 1 ? "" : "s", tagsNeutralised,
			    tagsNeutralised == 1 ? "" : "s",
			    chainLost ? " The walk stopped early on a bad size field, so there may be "
			                "more past it."
			              : "");
		}

		return magicsRestored + tagsNeutralised;
	}

	void* EngineAlloc(DWORD bytes)
	{
		if (bytes == 0 || !EngineHeapReady())
			return NULL;

		typedef void*(__cdecl * AllocFn)(size_t);
		AllocFn alloc = (AllocFn)ModuleAddress(Rva::MemAllocLocked);
		if (!Readable((void*)alloc, 1))
			return NULL;

		return alloc((size_t)bytes);
	}

	bool EngineFree(void* p)
	{
		if (!p || !EngineHeapReady())
			return false;

		typedef void(__cdecl * FreeFn)(void*);
		FreeFn freeFn = (FreeFn)ModuleAddress(Rva::MemFree);
		if (!Readable((void*)freeFn, 1))
			return false;

		freeFn(p);
		return true;
	}

	DWORD EngineBlockSize(const void* payload)
	{
		if (!payload)
			return 0;

		const DWORD header = (DWORD)(DWORD_PTR)payload - kHeaderBytes;
		DWORD magic = 0;
		DWORD size = 0;
		if (!ReadDword(header + kMagic, &magic) || magic != Rva::HeapBlockMagic)
			return 0;
		if (!ReadDword(header + kSize, &size) || size < kHeaderBytes || (size & 0xF) != 0)
			return 0;

		// The payload is what the caller can use, so the header comes back off.
		return size - kHeaderBytes;
	}
} // namespace ffx
