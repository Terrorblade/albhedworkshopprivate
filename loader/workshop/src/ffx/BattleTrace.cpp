#include "ffx/BattleTrace.h"

#include <string.h>

#include "ffx/addresses/Battle.h"
#include "ffx/addresses/Minigames.h"
#include "workshop/Detour.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

// Implementation notes.
//
// NO SYSCALL PER STEP. The work block, the RNG globals and the event groups are module
// globals, checked once. The unit and side record arrays are behind pointers, checked once
// per pointer value. The rest is plain reads.
//
// THE HOOKS, all read off the disassembly on 2026-10-30, not the decompile:
//
//   RunAiAndCommit 0x7ACEB0   55 / 8B EC / 53 / 8B 5D 08           push ebp, mov ebp esp,
//                                                                  push ebx, mov ebx [ebp+8]
//   CmdQueue_Push 0x7B0B90    55 / 8B EC / 51 / 53                 push ebp, mov ebp esp,
//                                                                  push ecx, push ebx
//   CmdQueue_Insert 0x7B0A30  55 / 8B EC / 83 EC 08                push ebp, mov ebp esp, sub esp 8
//   ComputeEffect 0x78E630    55 / 8B EC / 81 EC B4 00 00 00       push ebp, mov ebp esp,
//                                                                  sub esp 0B4h
//
// No relative operand in any of them. All four end in a plain retn and their callers clean
// the stack (add esp after every call site read), so they are cdecl and the hooks below are
// too. A char argument still arrives as a whole dword slot and is passed on unchanged.
//
// FFX_Btl_CommitActorsToSave 0x785FC0 starts push ebx / push esi / push edi / call, so a
// five byte steal would take a rel32. Its one caller is FFX_Btl_MainStep at 0x790EB2, and
// that call site is patched instead, see workshop::PatchCallSite.

namespace ffx
{

	using workshop::Log;
	using workshop::LooksLikePointer;
	using workshop::ModuleAddress;
	using workshop::Readable;

	namespace
	{

		template <typename T>
		T Global(DWORD rva)
		{
			return *(const volatile T*)ModuleAddress(rva);
		}

		// 0 not asked yet, 1 readable, -1 not.
		int workVerdict = 0;
		int randomVerdict = 0;
		int groupsVerdict = 0;
		int unitsVerdict = 0;
		int sidesVerdict = 0;

		bool ModuleSpanOnce(int* verdict, DWORD rva, DWORD bytes, const char* what)
		{
			if (*verdict == 0)
			{
				*verdict = Readable(ModuleAddress(rva), bytes) ? 1 : -1;
				if (*verdict < 0)
					Log("battle trace: the %s at RVA 0x%08X is not readable, that reader stays off", what, rva);
			}
			return *verdict > 0;
		}

		// A pointer and whether the span behind it was readable when it took this value.
		struct CheckedPointer
		{
			const BYTE* value;
			bool ok;
		};
		CheckedPointer units = { NULL, false };
		CheckedPointer sides = { NULL, false };

		const BYTE* Checked(CheckedPointer* cache, const BYTE* p, DWORD bytes)
		{
			if (!p || !LooksLikePointer((DWORD)(UINT_PTR)p))
				return NULL;
			if (p != cache->value)
			{
				cache->value = p;
				cache->ok = Readable(p, bytes);
			}
			return cache->ok ? p : NULL;
		}

		BattleCallObserverFn observer = NULL;

		// ---------------------------------------------------------------------------
		// FFX_Btl_RunAiAndCommit
		// ---------------------------------------------------------------------------

		DETOUR_DECLARE(RunAiAndCommit, int, (DWORD unit, DWORD issuer, DWORD kind, DWORD aeon, DWORD mode, DWORD fill));
		DETOUR_PROLOGUE(RunAiAndCommit) = {
			0x55,            // push ebp
			0x8B, 0xEC,      // mov  ebp, esp
			0x53,            // push ebx
			0x8B, 0x5D, 0x08 // mov  ebx, [ebp+8]
		};

		int __cdecl RunAiAndCommitHook(DWORD unit, DWORD issuer, DWORD kind, DWORD aeon, DWORD mode, DWORD fill)
		{
			const BattleCallObserverFn watch = observer;
			const DWORD args[6] = { unit, issuer, kind, aeon, mode, fill };
			if (watch)
				watch(BattleCallAiRun, args, 6, false, 0);
			const int result = DETOUR_ORIGINAL(RunAiAndCommit)(unit, issuer, kind, aeon, mode, fill);
			if (watch)
				watch(BattleCallAiRun, args, 6, true, result);
			return result;
		}

		// ---------------------------------------------------------------------------
		// FFX_Btl_CmdQueue_Push and FFX_Btl_CmdQueue_Insert
		// ---------------------------------------------------------------------------

		DETOUR_DECLARE(QueuePush, int, (DWORD unit, DWORD cmd, DWORD postKind, DWORD prio, DWORD variant));
		DETOUR_PROLOGUE(QueuePush) = {
			0x55,       // push ebp
			0x8B, 0xEC, // mov  ebp, esp
			0x51,       // push ecx
			0x53        // push ebx
		};

		int __cdecl QueuePushHook(DWORD unit, DWORD cmd, DWORD postKind, DWORD prio, DWORD variant)
		{
			const BattleCallObserverFn watch = observer;
			const DWORD args[5] = { unit, cmd, postKind, prio, variant };
			if (watch)
				watch(BattleCallQueuePush, args, 5, false, 0);
			const int result = DETOUR_ORIGINAL(QueuePush)(unit, cmd, postKind, prio, variant);
			if (watch)
				watch(BattleCallQueuePush, args, 5, true, result);
			return result;
		}

		DETOUR_DECLARE(QueueInsert, int,
		    (DWORD unit, DWORD cmd, DWORD postKind, DWORD prio, DWORD variant, DWORD execState, DWORD issuer));
		DETOUR_PROLOGUE(QueueInsert) = {
			0x55,            // push ebp
			0x8B, 0xEC,      // mov  ebp, esp
			0x83, 0xEC, 0x08 // sub  esp, 8
		};

		int __cdecl QueueInsertHook(DWORD unit, DWORD cmd, DWORD postKind, DWORD prio, DWORD variant,
		    DWORD execState, DWORD issuer)
		{
			const BattleCallObserverFn watch = observer;
			const DWORD args[7] = { unit, cmd, postKind, prio, variant, execState, issuer };
			if (watch)
				watch(BattleCallQueueInsert, args, 7, false, 0);
			const int result = DETOUR_ORIGINAL(QueueInsert)(unit, cmd, postKind, prio, variant, execState, issuer);
			if (watch)
				watch(BattleCallQueueInsert, args, 7, true, result);
			return result;
		}

		// ---------------------------------------------------------------------------
		// FFX_Btl_ComputeEffectForTarget
		// ---------------------------------------------------------------------------

		DETOUR_DECLARE(ComputeEffect, int,
		    (DWORD a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5, DWORD a6, DWORD a7, DWORD a8, DWORD a9, DWORD a10,
		        DWORD a11));
		DETOUR_PROLOGUE(ComputeEffect) = {
			0x55,                              // push ebp
			0x8B, 0xEC,                        // mov  ebp, esp
			0x81, 0xEC, 0xB4, 0x00, 0x00, 0x00 // sub  esp, 0B4h
		};

		int __cdecl ComputeEffectHook(DWORD a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5, DWORD a6, DWORD a7,
		    DWORD a8, DWORD a9, DWORD a10, DWORD a11)
		{
			const BattleCallObserverFn watch = observer;
			const DWORD args[11] = { a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11 };
			if (watch)
				watch(BattleCallHitCompute, args, 11, false, 0);
			const int result = DETOUR_ORIGINAL(ComputeEffect)(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
			if (watch)
				watch(BattleCallHitCompute, args, 11, true, result);
			return result;
		}

		// ---------------------------------------------------------------------------
		// The FFX_Btl_CommitActorsToSave call site
		// ---------------------------------------------------------------------------

		workshop::CallSitePatch commitSite = { 0, false, NULL, false };

		// The engine function returns a char nobody reads. eax goes back as it came.
		typedef int(__cdecl* CommitToSaveFn)(void);

		int __cdecl CommitToSaveSiteHook()
		{
			const BattleCallObserverFn watch = observer;
			if (watch)
				watch(BattleCallCommitToSave, NULL, 0, false, 0);
			const int result = ((CommitToSaveFn)commitSite.previous)();
			if (watch)
				watch(BattleCallCommitToSave, NULL, 0, true, result);
			return result;
		}

	} // namespace

	bool ReadBattleTraceView(BattleTraceView* out)
	{
		if (!out)
			return false;
		ZeroMemory(out, sizeof(*out));
		if (!ModuleSpanOnce(&workVerdict, Rva::BtlSubPhase, Rva::BtlWorkBlockBytes, "battle work block"))
			return false;
		out->work = (const BYTE*)ModuleAddress(Rva::BtlSubPhase);
		out->subPhase = (int)*(const volatile signed char*)out->work;
		if (out->subPhase == 0)
			return false;

		if (!ModuleSpanOnce(&unitsVerdict, Rva::BattleUnits, 4, "battle unit pointer"))
			return false;
		out->units = Checked(&units, Global<const BYTE*>(Rva::BattleUnits),
		    (DWORD)Rva::BattleUnitCount * Rva::BattleUnitBytes);
		if (ModuleSpanOnce(&sidesVerdict, Rva::BattleSideRecords, 4, "battle side record pointer"))
			out->sideRecords = Checked(&sides, Global<const BYTE*>(Rva::BattleSideRecords),
			    (DWORD)Rva::BattleSideRecordCount * Rva::BattleSideRecordBytes);
		return out->units != NULL;
	}

	bool ReadBattleRandom(DWORD* streams68, DWORD* battleLcg, DWORD* effectLcg)
	{
		if (randomVerdict == 0)
		{
			randomVerdict = Readable(ModuleAddress(Rva::RandStreamState), Rva::RandStreamStateBytes) && Readable(ModuleAddress(Rva::BattleRandState), 4) && Readable(ModuleAddress(Rva::EffectRandState), 4)
			                    ? 1
			                    : -1;
			if (randomVerdict < 0)
				Log("battle trace: the RNG globals are not readable, no RNG in the battle records");
		}
		if (randomVerdict < 0)
			return false;
		if (streams68)
			memcpy(streams68, ModuleAddress(Rva::RandStreamState), Rva::RandStreamStateBytes);
		if (battleLcg)
			*battleLcg = Global<DWORD>(Rva::BattleRandState);
		if (effectLcg)
			*effectLcg = Global<DWORD>(Rva::EffectRandState);
		return true;
	}

	bool BattleClass3Storage(int group, const BYTE** base, DWORD* bytes)
	{
		if (base)
			*base = NULL;
		if (bytes)
			*bytes = 0;
		if (group < 0 || group > 1)
			return false;
		if (!ModuleSpanOnce(&groupsVerdict, Rva::BtlEvGroups, (DWORD)Rva::BtlEvGroupBytes * Rva::BtlEvGroupCount,
		        "battle event groups"))
			return false;
		const DWORD at = Rva::BtlEvGroups + (DWORD)group * Rva::BtlEvGroupBytes;
		const BYTE* p = Global<const BYTE*>(at + Rva::BtlEvGroupClass3Off);
		const DWORD n = Global<DWORD>(at + Rva::BtlEvGroupClass3BytesOff);
		if (!p || !LooksLikePointer((DWORD)(UINT_PTR)p))
			return false;
		if (base)
			*base = p;
		if (bytes)
			*bytes = n;
		return true;
	}

	bool HookBattleAiRun(BattleCallObserverFn fn)
	{
		observer = fn;
		if (DETOUR_INSTALLED(RunAiAndCommit))
			return true;
		return DETOUR_INSTALL(RunAiAndCommit, Rva::BtlRunAiAndCommit);
	}

	bool HookBattleQueueAdds(BattleCallObserverFn fn)
	{
		observer = fn;
		const bool push = DETOUR_INSTALLED(QueuePush) || DETOUR_INSTALL(QueuePush, Rva::BtlCmdQueuePush);
		const bool insert = DETOUR_INSTALLED(QueueInsert) || DETOUR_INSTALL(QueueInsert, Rva::BtlCmdQueueInsert);
		return push && insert;
	}

	bool HookBattleHitCompute(BattleCallObserverFn fn)
	{
		observer = fn;
		if (DETOUR_INSTALLED(ComputeEffect))
			return true;
		return DETOUR_INSTALL(ComputeEffect, Rva::BtlComputeEffectForTarget);
	}

	bool HookBattleCommitToSave(BattleCallObserverFn fn)
	{
		observer = fn;
		if (commitSite.patched)
			return true;
		return workshop::PatchCallSite(commitSite, Rva::BtlCommitActorsToSaveCallSite, Rva::BtlCommitActorsToSave,
		    (void*)&CommitToSaveSiteHook, "battle commit to save");
	}

} // namespace ffx
