#pragma once

#include <windows.h>

// The engine's own heap: one big reserved range, pages committed on demand, blocks
// coalesced on free.
//
// WHY THIS AREA EXISTS. There is an intermittent crash on this project inside
// FFX_Free -> FFX_MemFreeLocked -> sub_943130 -> sub_9435A0 reading a free list node
// that is not one, and no frame of ours anywhere on the stack. That is the shape of
// heap corruption committed earlier by someone else and discovered by the allocator
// much later. The only way to pin it on a culprit is to check the heap at points of
// our own choosing, which needs these addresses.
//
// HOW THE HEAP IS SHAPED, read out of sub_6FB9B0, sub_942A40, sub_942DC0 and
// sub_942B60.
//
// FFX_MemAllocLocked and FFX_MemFreeLocked both do the same two steps: take the
// critical section whose pointer lives at HeapCriticalSection, then work on the
// descriptor that sits immediately after it. A CRITICAL_SECTION is 24 bytes on win32,
// so the descriptor is at *(void **)HeapCriticalSection + 24. The whole thing comes
// from one malloc(0x30) in sub_6FB9B0.
//
//     descriptor[0]  the pool span in bytes, page rounded
//     descriptor[1]  the smallest block worth putting on a free list. It is zero, so
//                    the "too small to list" tag of 3 is never actually used.
//     descriptor[2]  span - 0x100000
//     descriptor[3]  the first pool, or 0 before the first allocation
//     descriptor[4]  the free list node pool
//
// The span is negotiated at startup: sub_6FB9B0 asks for 0x30000000 (768 MB) and drops
// by 0x1000000 (16 MB) until a reservation plus a test allocation succeeds, then stores
// what it settled on at HeapSizeChosen. So the span is NOT a constant and has to be
// read at runtime.
//
// A POOL, from sub_942DC0:
//
//     pool+0      255 free list bucket heads, 16 bytes each, so 4080 bytes. Each head
//                 is itself a node and starts self linked, +4 prev and +8 next both
//                 pointing at the head.
//     pool+4080   the next pool pointer, which is why the pool walk in sub_943130
//                 steps by +4080 and why that number looks arbitrary. It is not, it is
//                 255 * 16.
//     pool+4084   three dwords of HeapBlockMagic as a guard.
//     pool+4096   the first block header, a 16 byte in-use sentinel with no payload.
//                 It exists so backward coalescing has something to stop against.
//     pool+4112   one free block covering span - 4128 bytes.
//     pool+span-16 the closing sentinel, also 16 bytes and in use.
//
// A BLOCK HEADER is 16 bytes and the payload follows it, so the pointer the game hands
// out is always header+16:
//
//     +0   HeapBlockMagic. Every live header has it. This is what makes a walk
//          verifiable rather than a guess.
//     +4   the size of this block INCLUDING the header, always a multiple of 16.
//          sub_942B60 computes it as ((request + 15) & ~15) + 16.
//     +8   the distance back to the previous block's header, so prev = header - this.
//     +12  the tag. 2 means in use. Anything else is read as a pointer to a free list
//          node, and that is the dereference that crashes when this field is smashed.
//
// A FREE LIST NODE is { block, prev, next, size }. sub_9435A0 unlinks one with
// prev->next = next and next->prev = prev, and the faulting instruction in every report
// so far is the load of that node's prev at +4.
//
// FFX_MemAllocLocked memsets every block it returns to 0xCD, so an uninitialized read
// out of engine memory shows up as 0xCDCDCDCD rather than as zeroes.

namespace ffx
{
	namespace Rva
	{
		// ---------------------------------------------------------------------------
		// The heap itself
		// ---------------------------------------------------------------------------

		// LPCRITICAL_SECTION *. The descriptor is at *HeapCriticalSection + 24.
		const DWORD HeapCriticalSection = 0x008E901C;

		// void *, the 16 byte block sub_6FB9B0 allocates to prove the heap works.
		const DWORD HeapProbeBlock = 0x008E9020;

		// DWORD, the span sub_6FB9B0 settled on after stepping down from 768 MB.
		const DWORD HeapSizeChosen = 0x008E9024;

		// Bytes from the start of a pool to its next-pool pointer, and to its first
		// block header. 4080 is 255 * 16, the bucket array.
		const DWORD HeapPoolNextOffset = 4080;
		const DWORD HeapPoolFirstBlock = 4096;

		// Every live block header starts with this.
		const DWORD HeapBlockMagic = 0xABCDEF12;

		// The tag at header+12 when the block is in use.
		const DWORD HeapTagInUse = 2;

		// The tag the allocator would use for a free block too small to list. The
		// minimum list size is zero in this build, so nothing ever carries it.
		const DWORD HeapTagFreeUnlisted = 3;

		// ---------------------------------------------------------------------------
		// The functions, wrapped so a plugin can allocate the way the engine does
		// ---------------------------------------------------------------------------
		const DWORD MemAlloc = 0x00287020;           // void *(size_t)
		const DWORD MemAllocLocked = 0x002FB910;     // void *(size_t), fills with 0xCD
		const DWORD MemFree = 0x002FB8B0;            // void (void *), FFX_MemFreeLocked
		const DWORD Free = 0x00230390;               // void (void *), FFX_Free
		const DWORD HeapCreate = 0x002FB9B0;         // the startup negotiation
		const DWORD HeapPoolCreate = 0x00542DC0;     // sub_942DC0
		const DWORD HeapBlockAlloc = 0x00542B60;     // sub_942B60
		const DWORD HeapBlockFree = 0x00543130;      // sub_943130
		const DWORD HeapNodeUnlink = 0x005435A0;     // sub_9435A0, where the crash lands
		const DWORD HeapCommitPages = 0x00547570;    // sub_947570
		const DWORD HeapDecommitPages = 0x00547670;  // sub_947670

		inline const DWORD* MemoryRvaList(int* count)
		{
			static const DWORD list[] = {
				HeapCriticalSection,
				HeapProbeBlock,
				HeapSizeChosen,
				MemAlloc,
				MemAllocLocked,
				MemFree,
				Free,
				HeapCreate,
				HeapPoolCreate,
				HeapBlockAlloc,
				HeapBlockFree,
				HeapNodeUnlink,
				HeapCommitPages,
				HeapDecommitPages,
			};

			if (count)
				*count = (int)(sizeof(list) / sizeof(list[0]));

			return list;
		}
	} // namespace Rva
} // namespace ffx
