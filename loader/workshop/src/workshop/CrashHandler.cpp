#include "workshop/CrashHandler.h"

#include <windows.h>
#include <intrin.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN64)
#error The crash handler walks the x86 frame chain and reads the 32-bit PEB. FFX.exe is 32-bit.
#endif

// Rules this file follows, because it runs when the process is already broken:
//
//   - no allocation, no CRT formatting, no locks, nothing from the rest of the library
//   - no call that takes the loader lock, since a fault in a DllMain holds it already
//   - every read of game or stack memory sits inside __try
//
// That is why the numbers are formatted by hand and the module list comes from a PEB
// walk instead of GetModuleHandleEx or EnumProcessModules.

namespace workshop
{
	namespace
	{

		// -------------------------------------------------------------------------------
		// State shared by every module that links this
		// -------------------------------------------------------------------------------

		const int kContextSlots = 16;
		const int kContextBytes = 112;
		const DWORD kStateMagic = 0x57435241; // "ARCW"

		// Two plugins and the proxy each get their own copy of every static in here, so
		// anything that decides "has someone already done this" has to live outside the
		// module. A named section is the cheapest process-wide place that needs no loader
		// lock to reach.
		struct Shared
		{
			DWORD magic;
			DWORD ownerBase;  // base of the module whose filter is installed
			DWORD gameThread; // noted by ReassertCrashHandler, which runs on a step
			LONG reporting;   // so a fault inside the handler cannot loop
			DWORD reports;
			LONG contextSeq;
			DWORD contextThread[kContextSlots];
			char context[kContextSlots][kContextBytes];
		};

		Shared g_fallbackState;
		Shared* g_state = NULL;
		LONG g_stateClaim = 0;

		Shared* State()
		{
			if (g_state)
				return g_state;

			// A benign race here would map the same section twice and leak one view, so
			// one claim keeps it to a single attempt per module.
			if (InterlockedCompareExchange(&g_stateClaim, 1, 0) != 0)
			{
				// Another thread in this module is mapping it. Nothing here can wait, so
				// use the local copy for this one call.
				return g_state ? g_state : &g_fallbackState;
			}

			HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
			    sizeof(Shared), L"Local\\AlBhedWorkshopCrashState");
			if (map)
			{
				void* view = MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared));
				if (view)
				{
					Shared* s = (Shared*)view;

					// A fresh section is zero filled, so whoever stamps the magic first
					// is the one that created it. Everyone else attaches to it as is.
					InterlockedCompareExchange((LONG*)&s->magic, (LONG)kStateMagic, 0);
					g_state = s;
					return g_state;
				}
				CloseHandle(map);
			}

			// No section, so this module reports on its own. Worst case two modules each
			// write a report for the same crash, which is noisy but not wrong.
			g_fallbackState.magic = kStateMagic;
			g_state = &g_fallbackState;
			return g_state;
		}

		// -------------------------------------------------------------------------------
		// Per-module state
		// -------------------------------------------------------------------------------

		LPTOP_LEVEL_EXCEPTION_FILTER g_previous = NULL;
		DWORD g_selfBase = 0;
		DWORD g_claim = 0; // what this module put in ownerBase, never 0
		char g_selfName[48] = "";
		bool g_owner = false;

		// -------------------------------------------------------------------------------
		// The module list, from the PEB
		// -------------------------------------------------------------------------------

		struct ModuleRow
		{
			DWORD base;
			DWORD size;
			DWORD preferred; // OptionalHeader.ImageBase, so a frame can be given an IDA address
			bool ours;       // loaded out of AlBhedWorkshop, so one of this project's
			char name[48];
		};

		const int kMaxModules = 192;
		ModuleRow g_mods[kMaxModules];
		int g_modCount = 0;

		// THE PREFERRED BASE HAS TO COME OFF THE DISK, not out of the mapped header.
		//
		// Reading OptionalHeader.ImageBase from memory looks right and is useless: when
		// the loader relocates a module it REWRITES that field in the mapped image to the
		// address it actually chose. So the in-memory value always equals the load base,
		// the subtraction gives zero slide, and the IDA column comes out identical to the
		// raw address. Two runs of the test exe at different ASLR bases both reported a
		// preferred base equal to their load base, which is what proves it.
		//
		// The file still holds the real one, so open it. This is why the disk reads are
		// done at install and on a step rather than in the handler, and cached.
		DWORD PreferredBaseFromFile(const wchar_t* path)
		{
			HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
			    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if (f == INVALID_HANDLE_VALUE)
				return 0;

			BYTE head[0x400];
			DWORD got = 0;
			const BOOL ok = ReadFile(f, head, sizeof(head), &got, NULL);
			CloseHandle(f);

			if (!ok || got < 0x100)
				return 0;
			if (*(const WORD*)head != 0x5A4D) // MZ
				return 0;

			const DWORD nt = *(const DWORD*)(head + 0x3C);
			if (nt < 0x40 || nt + 0x38 > got)
				return 0;
			if (*(const DWORD*)(head + nt) != 0x00004550) // PE\0\0
				return 0;
			if (*(const WORD*)(head + nt + 0x18) != 0x010B) // PE32 optional header magic
				return 0;

			return *(const DWORD*)(head + nt + 0x34); // OptionalHeader.ImageBase
		}

		// Module bases do not move for the life of the process, so one disk read each is
		// all this ever costs, and the handler itself never does one.
		struct PrefRow
		{
			DWORD base;
			DWORD pref;
		};

		PrefRow g_pref[kMaxModules];
		int g_prefCount = 0;

		DWORD CachedPref(DWORD base)
		{
			for (int i = 0; i < g_prefCount; ++i)
				if (g_pref[i].base == base)
					return g_pref[i].pref;
			return 0;
		}

		void CachePref(DWORD base, DWORD pref)
		{
			if (g_prefCount < kMaxModules)
			{
				g_pref[g_prefCount].base = base;
				g_pref[g_prefCount].pref = pref;
				++g_prefCount;
			}
		}

		void NarrowCopy(char* out, int outBytes, const wchar_t* src, int chars)
		{
			int n = 0;
			for (; n < chars && n < outBytes - 1 && src[n]; ++n)
			{
				const wchar_t c = src[n];
				out[n] = (c > 0 && c < 127) ? (char)c : '?';
			}
			out[n] = 0;
		}

		bool PathLooksLikeOurs(const wchar_t* path, int chars)
		{
			// "AlBhedWorkshop" anywhere in the full path, case insensitive. That is the
			// plugins folder and the proxy's own folder, so it marks this project's
			// modules and nothing else.
			const wchar_t* needle = L"albhedworkshop";
			const int needleLen = 14;
			for (int i = 0; i + needleLen <= chars; ++i)
			{
				int k = 0;
				for (; k < needleLen; ++k)
				{
					wchar_t c = path[i + k];
					if (c >= L'A' && c <= L'Z')
						c = (wchar_t)(c + 32);
					if (c != needle[k])
						break;
				}
				if (k == needleLen)
					return true;
			}
			return false;
		}

		// PEB -> Ldr -> InMemoryOrderModuleList. The offsets below are the 32-bit layout
		// and have not moved since XP. Reads only, so no lock and nothing to deadlock on.
		// allowDiskReads must be false on the crash path. Everywhere else it is true, and
		// it is what fills the preferred base cache the crash path then reads.
		void SnapshotModules(bool allowDiskReads)
		{
			// volatile because it is written inside __try and read after __except, where
			// a register copy would not survive the unwind.
			volatile int found = 0;
			__try
			{
				const DWORD peb = __readfsdword(0x30);
				if (!peb)
					__leave;
				const DWORD ldr = *(const DWORD*)(peb + 0x0C);
				if (!ldr)
					__leave;

				const DWORD head = ldr + 0x14;
				DWORD cur = *(const DWORD*)head;

				for (int i = 0; i < kMaxModules && cur && cur != head; ++i)
				{
					// The list links sit at +0x08 of the record, so step back to its start.
					const DWORD rec = cur - 8;

					const DWORD base = *(const DWORD*)(rec + 0x18); // DllBase
					const DWORD size = *(const DWORD*)(rec + 0x20); // SizeOfImage

					if (base && size)
					{
						ModuleRow& m = g_mods[found];
						m.base = base;
						m.size = size;
						m.preferred = CachedPref(base);
						m.name[0] = 0;
						m.ours = false;

						// BaseDllName, a UNICODE_STRING: Length at +0x2C, Buffer at +0x30.
						const WORD nameBytes = *(const WORD*)(rec + 0x2C);
						const wchar_t* nameBuf = *(const wchar_t* const*)(rec + 0x30);
						if (nameBuf && nameBytes)
							NarrowCopy(m.name, (int)sizeof(m.name), nameBuf, nameBytes / 2);
						if (!m.name[0])
							NarrowCopy(m.name, (int)sizeof(m.name), L"?", 1);

						// FullDllName, same shape, at +0x24 and +0x28.
						const WORD pathBytes = *(const WORD*)(rec + 0x24);
						const wchar_t* pathBuf = *(const wchar_t* const*)(rec + 0x28);
						if (pathBuf && pathBytes)
						{
							m.ours = PathLooksLikeOurs(pathBuf, pathBytes / 2);

							if (!m.preferred && allowDiskReads)
							{
								// The UNICODE_STRING is not promised to be terminated at
								// Length, so copy before handing it to an API.
								wchar_t full[MAX_PATH];
								int n = pathBytes / 2;
								if (n > MAX_PATH - 1)
									n = MAX_PATH - 1;
								for (int k = 0; k < n; ++k)
									full[k] = pathBuf[k];
								full[n] = 0;

								const DWORD pref = PreferredBaseFromFile(full);
								if (pref)
								{
									CachePref(base, pref);
									m.preferred = pref;
								}
							}
						}

						++found;
					}

					cur = *(const DWORD*)cur;
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				// Keep whatever was gathered before the list went bad.
			}

			g_modCount = found;
		}

		const ModuleRow* ModuleOf(DWORD addr)
		{
			for (int i = 0; i < g_modCount; ++i)
				if (addr >= g_mods[i].base && addr < g_mods[i].base + g_mods[i].size)
					return &g_mods[i];
			return NULL;
		}

		// -------------------------------------------------------------------------------
		// Formatting, by hand
		// -------------------------------------------------------------------------------

		struct Buf
		{
			char* p;
			int len;
			int cap;
		};

		void Put(Buf& b, char c)
		{
			if (b.len + 1 < b.cap)
				b.p[b.len++] = c;
		}

		void PutStr(Buf& b, const char* s)
		{
			while (s && *s)
				Put(b, *s++);
		}

		void PutLine(Buf& b)
		{
			Put(b, '\r');
			Put(b, '\n');
		}

		void PutHex(Buf& b, DWORD v, int digits)
		{
			const char* d = "0123456789ABCDEF";
			for (int i = digits - 1; i >= 0; --i)
				Put(b, d[(v >> (i * 4)) & 0xF]);
		}

		void PutHex8(Buf& b, DWORD v)
		{
			PutStr(b, "0x");
			PutHex(b, v, 8);
		}

		void PutDec(Buf& b, DWORD v)
		{
			char t[12];
			int n = 0;
			if (!v)
			{
				Put(b, '0');
				return;
			}
			while (v && n < 11)
			{
				t[n++] = (char)('0' + (v % 10));
				v /= 10;
			}
			while (n > 0)
				Put(b, t[--n]);
		}

		void PutDecPad(Buf& b, DWORD v, int width)
		{
			DWORD scale = 1;
			for (int i = 1; i < width; ++i)
				scale *= 10;
			for (; scale > 1; scale /= 10)
			{
				if (v >= scale)
					break;
				Put(b, '0');
			}
			PutDec(b, v);
		}

		void PutPad(Buf& b, int toColumn, int from)
		{
			while (b.len - from < toColumn)
				Put(b, ' ');
		}

		// The one line that matters: raw address, which module, the offset into it, and
		// the address that module's own IDB uses so it pastes straight into IDA.
		void PutAddr(Buf& b, DWORD addr)
		{
			const int start = b.len;
			PutHex8(b, addr);

			const ModuleRow* m = ModuleOf(addr);
			if (!m)
			{
				PutStr(b, "  (not in any loaded module)");
				return;
			}

			PutPad(b, 12, start);
			PutStr(b, "  ");
			PutStr(b, m->name);
			Put(b, '+');
			PutHex8(b, addr - m->base);
			if (m->preferred)
			{
				PutStr(b, "  ida ");
				PutHex8(b, (addr - m->base) + m->preferred);
			}
			else
				PutStr(b, "  ida unknown");
			if (m->ours)
				PutStr(b, "  <- Al Bhed");
		}

		const char* ExceptionName(DWORD code)
		{
			switch (code)
			{
			case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
			case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
			case EXCEPTION_BREAKPOINT: return "BREAKPOINT";
			case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
			case EXCEPTION_FLT_DENORMAL_OPERAND: return "FLT_DENORMAL_OPERAND";
			case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "FLT_DIVIDE_BY_ZERO";
			case EXCEPTION_FLT_INEXACT_RESULT: return "FLT_INEXACT_RESULT";
			case EXCEPTION_FLT_INVALID_OPERATION: return "FLT_INVALID_OPERATION";
			case EXCEPTION_FLT_OVERFLOW: return "FLT_OVERFLOW";
			case EXCEPTION_FLT_STACK_CHECK: return "FLT_STACK_CHECK";
			case EXCEPTION_FLT_UNDERFLOW: return "FLT_UNDERFLOW";
			case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
			case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
			case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
			case EXCEPTION_INT_OVERFLOW: return "INT_OVERFLOW";
			case EXCEPTION_INVALID_DISPOSITION: return "INVALID_DISPOSITION";
			case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
			case EXCEPTION_PRIV_INSTRUCTION: return "PRIV_INSTRUCTION";
			case EXCEPTION_SINGLE_STEP: return "SINGLE_STEP";
			case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
			case 0xE06D7363: return "a C++ exception nobody caught";
			case 0x40010005: return "CTRL_C";
			default: return "unknown";
			}
		}

		// -------------------------------------------------------------------------------
		// Walking the stack
		// -------------------------------------------------------------------------------

		// WHO THE REPORT IS ABOUT, when that is not whoever is running Report.
		//
		// A HANG REPORT IS WRITTEN BY A WATCHDOG THREAD ABOUT THE GAME THREAD, and
		// without this the report would carry the watchdog's own thread id and, far
		// worse, the watchdog's stack bounds. Every address on the game thread's stack
		// would then fail the bounds test and both call chains would come out empty.
		DWORD g_subjectThread = 0;
		DWORD g_subjectStackLo = 0;
		DWORD g_subjectStackHi = 0;

		// The stack bounds of a thread that is not this one. VirtualQuery rather than
		// its TEB, because finding another thread's TEB needs
		// NtQueryInformationThread and this needs nothing but the stack pointer we
		// already have from its context.
		//
		// A thread stack is one reservation holding three or four regions: the guard
		// page, the committed part, and the reserved rest. They all share one
		// AllocationBase, so walking forward while that stays the same finds the high
		// end exactly.
		bool StackBoundsOf(DWORD esp, DWORD* lo, DWORD* hi)
		{
			MEMORY_BASIC_INFORMATION mbi;
			memset(&mbi, 0, sizeof(mbi));
			if (VirtualQuery((LPCVOID)esp, &mbi, sizeof(mbi)) != sizeof(mbi))
				return false;
			if (mbi.State != MEM_COMMIT || !mbi.AllocationBase)
				return false;

			void* allocation = mbi.AllocationBase;
			DWORD end = (DWORD)(UINT_PTR)mbi.BaseAddress + (DWORD)mbi.RegionSize;

			// 16 is far more regions than a stack ever has, it is only here so a
			// surprise cannot turn this into a long loop on a hung game.
			for (int i = 0; i < 16; ++i)
			{
				MEMORY_BASIC_INFORMATION next;
				memset(&next, 0, sizeof(next));
				if (VirtualQuery((LPCVOID)end, &next, sizeof(next)) != sizeof(next))
					break;
				if (next.AllocationBase != allocation)
					break;
				end = (DWORD)(UINT_PTR)next.BaseAddress + (DWORD)next.RegionSize;
			}

			*lo = esp;
			*hi = end;
			return *hi > *lo;
		}

		void StackBounds(DWORD* lo, DWORD* hi)
		{
			if (g_subjectStackHi > g_subjectStackLo)
			{
				*lo = g_subjectStackLo;
				*hi = g_subjectStackHi;
				return;
			}

			// NT_TIB, which is the front of the TEB: StackBase at +0x04 is the HIGH end,
			// StackLimit at +0x08 is the low end. The filter runs on the faulting thread,
			// so this is that thread's stack.
			*hi = __readfsdword(0x04);
			*lo = __readfsdword(0x08);
		}

		int WalkFrames(DWORD ebp, DWORD lo, DWORD hi, DWORD* out, int max)
		{
			volatile int n = 0;
			__try
			{
				DWORD cur = ebp;
				for (int i = 0; i < 256 && n < max; ++i)
				{
					if (cur < lo || cur + 8 > hi || (cur & 3) != 0)
						__leave;

					const DWORD ret = *(const DWORD*)(cur + 4);
					const DWORD next = *(const DWORD*)(cur);

					if (ModuleOf(ret))
						out[n++] = ret;

					// A frame chain only ever grows towards the high end. Anything else is
					// a wild EBP and following it would wander.
					if (next <= cur)
						__leave;
					cur = next;
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
			}
			return n;
		}

		// Is the code just before this address a call? Release x86 drops frame pointers,
		// so the chain above often stops after a frame or two and this is what fills the
		// gap. It over-reports, because a stale return address from an older deeper call
		// is indistinguishable from a live one, which is why the report labels it.
		bool LooksLikeCallSite(DWORD ret)
		{
			volatile bool answer = false;
			__try
			{
				const BYTE* p = (const BYTE*)ret;

				if (p[-5] == 0xE8) // call rel32
					answer = true;
				else if (p[-7] == 0x9A) // far call, basically never
					answer = true;
				else
				{
					// call r/m32 is FF /2, and the whole instruction is 2 to 7 bytes.
					for (int back = 2; back <= 7; ++back)
					{
						if (p[-back] == 0xFF && ((p[-back + 1] >> 3) & 7) == 2)
						{
							answer = true;
							break;
						}
					}
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				answer = false;
			}
			return answer;
		}

		int ScanStack(DWORD esp, DWORD lo, DWORD hi, DWORD* outAt, DWORD* outRet, int max)
		{
			volatile int n = 0;
			__try
			{
				DWORD at = esp & ~3u;
				if (at < lo)
					at = lo;

				// 32 KB of stack. Deeper than that and the frames are ancient history.
				const DWORD stop = (hi < at + 0x8000) ? hi : at + 0x8000;

				for (; at + 4 <= stop && n < max; at += 4)
				{
					const DWORD v = *(const DWORD*)at;
					if (!ModuleOf(v))
						continue;
					if (!LooksLikeCallSite(v))
						continue;

					outAt[n] = at;
					outRet[n] = v;
					++n;
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
			}
			return n;
		}

		// -------------------------------------------------------------------------------
		// Where the file goes
		// -------------------------------------------------------------------------------

		bool CrashFilePath(wchar_t* out, int count)
		{
			wchar_t exePath[MAX_PATH];
			if (GetModuleFileNameW(NULL, exePath, MAX_PATH) == 0)
				return false;

			int cut = -1;
			for (int i = 0; exePath[i]; ++i)
				if (exePath[i] == L'\\' || exePath[i] == L'/')
					cut = i;
			if (cut < 0)
				return false;
			exePath[cut] = 0;

			const wchar_t* tail = L"\\AlBhedWorkshop\\albhed_crash.log";
			int n = 0;
			for (; exePath[n] && n < count - 1; ++n)
				out[n] = exePath[n];
			for (int i = 0; tail[i] && n < count - 1; ++i, ++n)
				out[n] = tail[i];
			out[n] = 0;
			return n > 0;
		}

		bool WriteOut(const char* text, int len)
		{
			wchar_t path[MAX_PATH];
			if (!CrashFilePath(path, MAX_PATH))
				return false;

			HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
			    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (f == INVALID_HANDLE_VALUE)
				return false;

			DWORD written = 0;
			WriteFile(f, text, (DWORD)len, &written, NULL);

			// The process is usually a few microseconds from gone, so do not leave the
			// report sitting in a buffer.
			FlushFileBuffers(f);
			CloseHandle(f);
			return written == (DWORD)len;
		}

		// -------------------------------------------------------------------------------
		// The report
		// -------------------------------------------------------------------------------

		char g_reportBuf[48 * 1024];

		void PutContextBlock(Buf& b, Shared* s)
		{
			const LONG seq = s->contextSeq;
			if (seq <= 0)
			{
				PutStr(b, "  (none were left)");
				PutLine(b);
				return;
			}

			// Oldest first, so it reads as a sequence.
			const LONG have = (seq < kContextSlots) ? seq : kContextSlots;
			for (LONG i = have; i > 0; --i)
			{
				const LONG index = (seq - i) % kContextSlots;
				PutStr(b, "  ");
				PutDecPad(b, (DWORD)(have - i) + 1, 2);
				PutStr(b, "  tid ");
				PutDec(b, s->contextThread[index]);
				PutStr(b, "  ");
				PutStr(b, s->context[index]);
				PutLine(b);
			}
		}

		void PutFrames(Buf& b, const DWORD* frames, int count)
		{
			for (int i = 0; i < count; ++i)
			{
				PutStr(b, "  ");
				if (i < 10)
					Put(b, ' ');
				PutDec(b, (DWORD)i);
				PutStr(b, "  ");
				PutAddr(b, frames[i]);
				PutLine(b);
			}
			if (count == 0)
			{
				PutStr(b, "  (nothing resolved)");
				PutLine(b);
			}
		}

		// Builds and writes one report. info may be NULL, in which case ctx is used and
		// the report says it was asked for rather than caused.
		bool Report(EXCEPTION_POINTERS* info, const CONTEXT* ctx, const char* why)
		{
			Shared* s = State();

			// Fresh, because a module may have loaded since the last step. NO DISK READS:
			// this runs on the crash path, where the preferred bases come from the cache
			// the step filled.
			SnapshotModules(info != NULL ? false : true);

			Buf b;
			b.p = g_reportBuf;
			b.len = 0;
			b.cap = (int)sizeof(g_reportBuf);

			const EXCEPTION_RECORD* rec = info ? info->ExceptionRecord : NULL;
			if (info && info->ContextRecord)
				ctx = info->ContextRecord;

			SYSTEMTIME now;
			GetLocalTime(&now);

			PutLine(b);
			PutStr(b, "================================================================");
			PutLine(b);
			PutStr(b, rec ? "CRASH  " : "REPORT ON DEMAND  ");
			PutDecPad(b, now.wYear, 4);
			Put(b, '-');
			PutDecPad(b, now.wMonth, 2);
			Put(b, '-');
			PutDecPad(b, now.wDay, 2);
			Put(b, ' ');
			PutDecPad(b, now.wHour, 2);
			Put(b, ':');
			PutDecPad(b, now.wMinute, 2);
			Put(b, ':');
			PutDecPad(b, now.wSecond, 2);
			Put(b, '.');
			PutDecPad(b, now.wMilliseconds, 3);
			PutLine(b);
			PutStr(b, "================================================================");
			PutLine(b);

			if (why && *why)
			{
				PutStr(b, "why        : ");
				PutStr(b, why);
				PutLine(b);
			}

			PutStr(b, "handler in : ");
			PutStr(b, g_selfName[0] ? g_selfName : "?");
			PutStr(b, "  base ");
			PutHex8(b, g_selfBase);
			PutLine(b);

			// The subject, not the writer. They differ for a hang report.
			const DWORD thisThread = g_subjectThread ? g_subjectThread : GetCurrentThreadId();
			PutStr(b, "thread     : ");
			PutDec(b, thisThread);
			if (g_subjectThread)
			{
				PutStr(b, "  (read by the watchdog on thread ");
				PutDec(b, GetCurrentThreadId());
				Put(b, ')');
			}
			if (!s->gameThread)
				PutStr(b, "  (no step has run yet, so the game thread is unknown)");
			else if (s->gameThread == thisThread)
				PutStr(b, "  (this IS the game thread)");
			else
			{
				PutStr(b, "  (NOT the game thread, which is ");
				PutDec(b, s->gameThread);
				Put(b, ')');
			}
			PutLine(b);

			if (rec)
			{
				PutStr(b, "exception  : ");
				PutHex8(b, rec->ExceptionCode);
				Put(b, ' ');
				PutStr(b, ExceptionName(rec->ExceptionCode));

				if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
				    && rec->NumberParameters >= 2)
				{
					const ULONG_PTR kind = rec->ExceptionInformation[0];
					PutStr(b, kind == 0 ? " reading " : (kind == 1 ? " writing " : " executing "));
					PutHex8(b, (DWORD)rec->ExceptionInformation[1]);
				}
				PutLine(b);

				PutStr(b, "faulted at : ");
				PutAddr(b, (DWORD)(UINT_PTR)rec->ExceptionAddress);
				PutLine(b);
			}

			// The question that actually gets asked: was it us.
			{
				const ModuleRow* at = rec ? ModuleOf((DWORD)(UINT_PTR)rec->ExceptionAddress) : NULL;
				if (at)
				{
					PutStr(b, "blame      : the faulting instruction is in ");
					PutStr(b, at->name);
					PutStr(b, at->ours ? ", which is one of ours." : ", which is not ours.");
					PutLine(b);
				}
			}

			if (ctx)
			{
				PutStr(b, "registers  : eax=");
				PutHex(b, ctx->Eax, 8);
				PutStr(b, " ebx=");
				PutHex(b, ctx->Ebx, 8);
				PutStr(b, " ecx=");
				PutHex(b, ctx->Ecx, 8);
				PutStr(b, " edx=");
				PutHex(b, ctx->Edx, 8);
				PutLine(b);
				PutStr(b, "             esi=");
				PutHex(b, ctx->Esi, 8);
				PutStr(b, " edi=");
				PutHex(b, ctx->Edi, 8);
				PutStr(b, " ebp=");
				PutHex(b, ctx->Ebp, 8);
				PutStr(b, " esp=");
				PutHex(b, ctx->Esp, 8);
				PutLine(b);
				PutStr(b, "             eip=");
				PutHex(b, ctx->Eip, 8);
				PutStr(b, " flg=");
				PutHex(b, ctx->EFlags, 8);
				PutLine(b);

				// The bytes at EIP identify the instruction without needing the frame to
				// resolve, which is handy when the module list came up short.
				PutStr(b, "bytes at ei: ");
				__try
				{
					const BYTE* at = (const BYTE*)ctx->Eip;
					for (int i = 0; i < 16; ++i)
					{
						PutHex(b, at[i], 2);
						Put(b, ' ');
					}
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
					PutStr(b, "(unreadable)");
				}
				PutLine(b);
			}

			PutLine(b);
			PutStr(b, "what it was doing, newest last:");
			PutLine(b);
			PutContextBlock(b, s);

			if (ctx)
			{
				DWORD lo = 0;
				DWORD hi = 0;
				StackBounds(&lo, &hi);

				PutLine(b);
				PutStr(b, "call chain, frame pointer walk:");
				PutLine(b);

				// EIP IS FRAME 0. The walk starts at the caller, so without this the
				// chain looks like it is missing the crash site. This is also why the
				// first frame can be the only honest one: release x86 drops frame
				// pointers, so EBP on entry to the faulting function often still belongs
				// to something further out.
				DWORD frames[65];
				frames[0] = ctx->Eip;
				const int nf = 1 + WalkFrames(ctx->Ebp, lo, hi, frames + 1, 64);
				PutFrames(b, frames, nf);

				PutLine(b);
				PutStr(b, "call chain, stack scan. Release builds drop frame pointers so the");
				PutLine(b);
				PutStr(b, "walk above usually stops early. THIS LIST HAS STALE ENTRIES IN IT:");
				PutLine(b);
				PutStr(b, "every dword on the stack that points just past a call shows up,");
				PutLine(b);
				PutStr(b, "live or left over. Read it as candidates, nearest frame first.");
				PutLine(b);

				DWORD at[48];
				DWORD ret[48];
				const int ns = ScanStack(ctx->Esp, lo, hi, at, ret, 48);
				for (int i = 0; i < ns; ++i)
				{
					PutStr(b, "  esp+");
					PutHex(b, at[i] - ctx->Esp, 4);
					PutStr(b, "  ");
					PutAddr(b, ret[i]);
					PutLine(b);
				}
				if (ns == 0)
				{
					PutStr(b, "  (nothing resolved)");
					PutLine(b);
				}
			}

			PutLine(b);
			PutStr(b, "modules, for turning any other address into an RVA:");
			PutLine(b);
			for (int i = 0; i < g_modCount; ++i)
			{
				const ModuleRow& m = g_mods[i];
				PutStr(b, "  ");
				PutHex8(b, m.base);
				PutStr(b, "  size ");
				PutHex8(b, m.size);
				PutStr(b, "  pref ");
				PutHex8(b, m.preferred);
				// Signed, because a module can land below its preferred base and
				// "slide 0xFFE20000" reads like a bad value rather than minus 2 MB.
				PutStr(b, "  slide ");
				if (!m.preferred)
					PutStr(b, "unknown   ");
				else if (m.base >= m.preferred)
				{
					Put(b, '+');
					PutHex8(b, m.base - m.preferred);
				}
				else
				{
					Put(b, '-');
					PutHex8(b, m.preferred - m.base);
				}
				PutStr(b, "  ");
				PutStr(b, m.name);
				if (m.ours)
					PutStr(b, "  <- Al Bhed");
				PutLine(b);
			}

			PutLine(b);

			s->reports++;

			// A short line to the debugger as well, so a live session sees it immediately.
			OutputDebugStringA("AlBhedWorkshop: crash report written to albhed_crash.log\r\n");

			return WriteOut(g_reportBuf, b.len);
		}

		// -------------------------------------------------------------------------------
		// The filter
		// -------------------------------------------------------------------------------

		LONG CALLBACK Filter(EXCEPTION_POINTERS* info)
		{
			Shared* s = State();

			// One report per crash. Three things want this flag.
			//
			// A fault inside the handler must not loop, though the __try below should
			// catch that first. A second thread faulting at the same moment must not
			// interleave its report with this one. And a chain can hold more than one of
			// our handlers, because the proxy installs one and a plugin may install
			// another after the game displaced it, so whichever runs first claims the
			// report and the rest pass straight through.
			//
			// THE FLAG IS NEVER RELEASED HERE. Reaching a top level filter means nothing
			// handled the exception, so the process is going down and there is no later
			// crash to report. Releasing it is exactly what would let the next handler in
			// the chain write a duplicate.
			if (InterlockedCompareExchange(&s->reporting, 1, 0) != 0)
				return EXCEPTION_CONTINUE_SEARCH;

			__try
			{
				Report(info, NULL, NULL);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				OutputDebugStringA("AlBhedWorkshop: the crash handler itself faulted\r\n");
			}

			// Hand it on. The game's own filter, if it had one, still runs, and with no
			// chain the system default does, so WER behaves exactly as it did before.
			// Nothing here changes whether the process dies.
			if (g_previous)
				return g_previous(info);
			return EXCEPTION_CONTINUE_SEARCH;
		}

	} // namespace

	// -----------------------------------------------------------------------------------
	// Public
	// -----------------------------------------------------------------------------------

	bool InstallCrashHandler()
	{
		Shared* s = State();

		// Find our own module without asking the loader, by locating the one that
		// contains this function.
		// NO DISK READS HERE. This is callable from DllMain, where the loader lock is
		// held and the proxy's own rule is that nothing touches the disk. All install
		// needs from the snapshot is this module's base and name. WarmCrashHandler does
		// the file reads later, off the lock.
		SnapshotModules(false);
		{
			const ModuleRow* self = ModuleOf((DWORD)(UINT_PTR)&InstallCrashHandler);
			if (self)
			{
				g_selfBase = self->base;
				int i = 0;
				for (; self->name[i] && i < (int)sizeof(g_selfName) - 1; ++i)
					g_selfName[i] = self->name[i];
				g_selfName[i] = 0;
			}
		}

		// NEVER CLAIM WITH 0. ownerBase 0 means "nobody owns it", so if the module
		// lookup came up empty, claiming with 0 would succeed for every module in turn
		// and every one of them would install a filter and write its own report.
		g_claim = g_selfBase ? g_selfBase : 1;

		// First caller in the process wins, so one crash makes one report however many
		// plugins are loaded.
		if (InterlockedCompareExchange((LONG*)&s->ownerBase, (LONG)g_claim, 0) != 0)
			return false;

		g_previous = SetUnhandledExceptionFilter(&Filter);
		g_owner = true;
		return true;
	}

	void WarmCrashHandler()
	{
		SnapshotModules(true);
	}

	void ReassertCrashHandler()
	{
		Shared* s = State();

		// Whoever calls this is on a step, so it is the game thread by definition. Worth
		// recording from any module, owner or not, because a report that can say the
		// fault was off the game thread has said something useful.
		s->gameThread = GetCurrentThreadId();

		// Asking is the same call as setting, so this always installs ours and then looks
		// at what it displaced.
		LPTOP_LEVEL_EXCEPTION_FILTER was = SetUnhandledExceptionFilter(&Filter);
		if (was == &Filter)
			return; // already ours, nothing moved

		// With disk reads, because this is a step and not the crash path. It is what
		// keeps the preferred base cache warm for a module that loaded since install.
		SnapshotModules(true);
		const ModuleRow* m = was ? ModuleOf((DWORD)(UINT_PTR)was) : NULL;
		if (m && m->ours)
		{
			// Another Al Bhed module's handler, most likely the proxy's. Put it back and
			// stay out of the way. Two of ours in one chain is not harmful, the reporting
			// flag already stops the duplicate, but one is tidier and keeps the chain
			// ending at whatever the game installed rather than at us twice.
			SetUnhandledExceptionFilter(was);
			return;
		}

		// The game's own filter, or none at all. Ours is in now and the displaced one
		// becomes the next link, so the game's handling still happens after our report.
		g_previous = was;
	}

	void CrashContext(const char* format, ...)
	{
		if (!format || !*format)
			return;

		Shared* s = State();

		char body[kContextBytes];
		va_list args;
		va_start(args, format);
		_vsnprintf_s(body, sizeof(body), _TRUNCATE, format, args);
		va_end(args);

		// Claim a slot, then fill it. A reader during the gap sees the previous text in
		// that slot, which is a cosmetic problem only, and the buffer is never unterminated
		// because every write ends with a NUL inside the array.
		const LONG seq = InterlockedIncrement(&s->contextSeq) - 1;
		const int slot = (int)((DWORD)seq % kContextSlots);

		s->contextThread[slot] = GetCurrentThreadId();
		int i = 0;
		for (; body[i] && i < kContextBytes - 1; ++i)
			s->context[slot][i] = body[i];
		s->context[slot][i] = 0;
	}

	CrashScope::CrashScope(const char* what)
	{
		held[0] = 0;
		if (what)
			for (int i = 0; what[i] && i < (int)sizeof(held) - 1; ++i)
			{
				held[i] = what[i];
				held[i + 1] = 0;
			}
		CrashContext("%s", held);
	}

	CrashScope::~CrashScope()
	{
		CrashContext("%s, returned", held);
	}

	bool CrashHandlerInstalled()
	{
		return State()->ownerBase != 0;
	}

	const char* CrashHandlerOwner()
	{
		return g_owner ? g_selfName : "";
	}

	bool WriteThreadReportNow(unsigned long threadId, const char* why)
	{
		if (threadId == 0 || threadId == GetCurrentThreadId())
			return WriteCrashReportNow(why);

		Shared* s = State();
		if (InterlockedCompareExchange(&s->reporting, 1, 0) != 0)
			return false;

		bool ok = false;
		HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, threadId);
		if (thread)
		{
			// SUSPEND BEFORE GETTHREADCONTEXT. On a running thread the context is a
			// snapshot of something that has already moved on, so EIP and ESP would not
			// agree with each other and the walk would be fiction. On a hung thread it
			// would mostly work, which is worse, because it works until the one time
			// the thread is actually spinning.
			if (SuspendThread(thread) != (DWORD)-1)
			{
				CONTEXT ctx;
				memset(&ctx, 0, sizeof(ctx));
				ctx.ContextFlags = CONTEXT_FULL;
				if (GetThreadContext(thread, &ctx))
				{
					g_subjectThread = threadId;
					StackBoundsOf(ctx.Esp, &g_subjectStackLo, &g_subjectStackHi);

					__try
					{
						ok = Report(NULL, &ctx, why ? why : "asked for by hand, another thread");
					}
					__except (EXCEPTION_EXECUTE_HANDLER)
					{
						ok = false;
					}

					g_subjectThread = 0;
					g_subjectStackLo = 0;
					g_subjectStackHi = 0;
				}

				// ALWAYS, even when everything above failed. Leaving the game thread
				// suspended would turn a report into a permanent freeze.
				ResumeThread(thread);
			}
			CloseHandle(thread);
		}

		InterlockedExchange(&s->reporting, 0);
		return ok;
	}

	bool WriteCrashReportNow(const char* why)
	{
		Shared* s = State();
		if (InterlockedCompareExchange(&s->reporting, 1, 0) != 0)
			return false;

		CONTEXT ctx;
		memset(&ctx, 0, sizeof(ctx));
		ctx.ContextFlags = CONTEXT_FULL;

		// RtlCaptureContext lives in kernel32 on x86 and in ntdll everywhere. Both are
		// already loaded, so this is a lookup and not a load.
		typedef void(WINAPI * CaptureFn)(CONTEXT*);
		CaptureFn capture = NULL;
		HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
		if (k32)
			capture = (CaptureFn)GetProcAddress(k32, "RtlCaptureContext");
		if (!capture)
		{
			HMODULE nt = GetModuleHandleW(L"ntdll.dll");
			if (nt)
				capture = (CaptureFn)GetProcAddress(nt, "RtlCaptureContext");
		}

		if (capture)
			capture(&ctx);
		else
		{
			// No capture function, so take the two registers the walk actually needs.
			// The locals must not be called ebp or esp: the assembler reads a register
			// name as the register, so "mov ebp, ebp" would be a no-op and the walk
			// would start from garbage.
			DWORD framePtr = 0;
			DWORD stackPtr = 0;
			__asm mov framePtr, ebp
			__asm mov stackPtr, esp
			ctx.Ebp = framePtr;
			ctx.Esp = stackPtr;
			ctx.Eip = (DWORD)(UINT_PTR)&WriteCrashReportNow;
		}

		bool ok = false;
		__try
		{
			ok = Report(NULL, &ctx, why ? why : "asked for by hand");
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			ok = false;
		}

		InterlockedExchange(&s->reporting, 0);
		return ok;
	}

} // namespace workshop
