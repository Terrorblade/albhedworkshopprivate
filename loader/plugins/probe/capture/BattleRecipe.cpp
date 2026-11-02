// Recipe 5, battles, for Project Zanarkand PROGRESS 13 (13.1, 13.4, 13.5, 13.6, 13.8, 13.13).
//
// reversing/PROBE_FIXTURES.md section 10. Three files, because the three record shapes
// differ in size by two orders of magnitude and a fixture file holds one record size:
//
//   kind 5 battle       a snapshot at the start of every step while a battle runs: the sub
//                       phase, the 68 RNG streams and the battle LCG, the turn queue, the
//                       first 16 command queue slots, the battle flow globals and 356 bytes
//                       of each of the 31 unit records. About 13 KB a step.
//   kind 6 battleevent  one record per call of the four decision points, written when the
//                       call returns, with the arguments, the RNG on both sides and what the
//                       call produced: the staged command and the class 3 AI variables of an
//                       AI run, where a queued command landed, the 44 byte hit record of a
//                       hit. 1860 bytes a call.
//   kind 7 battledump   the whole unit and side record arrays, the save block, the battle
//                       work globals and both class 3 storages, at six moments: seen, sub
//                       phase 1, first menu, committed to the save block, gone, and after
//                       the results screen. About 213 KB each.
//
// Reads over hooks. The per step snapshot rides the pacing hook, which already runs once a
// step, and needs no hook of its own. The four hooks are the places a per step snapshot
// cannot see: several AI runs, commits and hits can happen inside one step.
//
// Cost rules as elsewhere. No VirtualQuery per step: the module globals are validated when
// a capture starts, the unit arrays once per pointer value inside ffx::ReadBattleTraceView,
// and anything behind a pointer an argument hands us is copied under __try. Nothing here
// allocates, the records are filled in place in the writer's buffer.

#include "capture/Capture.h"

#include <string.h>

#include "ProbeSettings.h"
#include "ffx/BattleTrace.h"
#include "ffx/StepPacing.h"
#include "ffx/addresses/Battle.h"
#include "ffx/addresses/GameState.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

namespace probe
{
	namespace
	{

		namespace Rva = ffx::Rva;

		static_assert(BattleUnitStride == (DWORD)Rva::BattleUnitBytes, "unit stride agrees with the kit");
		static_assert(BattleUnitCount == Rva::BattleUnitCount, "unit count agrees with the kit");
		static_assert(BattleSideRecordStride == (DWORD)Rva::BattleSideRecordBytes, "side stride agrees with the kit");
		static_assert(BattleSideRecordCount == Rva::BattleSideRecordCount, "side count agrees with the kit");
		static_assert(SaveBlockBytes == (int)Rva::SaveDataSize, "save block size agrees with the kit");
		static_assert(BattleWorkBytes == Rva::BtlWorkBlockBytes, "work block size agrees with the kit");
		static_assert(BattleTurnQueueSlots * BattleTurnEntryBytes <= 0x1F0, "turn queue fits before the command queue");
		static_assert(BattleCommandBytes == Rva::BtlCommandRecordBytes, "command size agrees with the kit");

		// Where the queues and their counts sit inside the work block, which starts at
		// g_ffxBtlSubPhase. All inside the block, checked in BattleSpansConsistent.
		const DWORD TurnQueueAt = Rva::BtlTurnQueue - Rva::BtlSubPhase;
		const DWORD CmdQueueAt = Rva::BtlCmdQueue - Rva::BtlSubPhase;
		const DWORD TurnCountAt = Rva::BtlTurnQueueCount - Rva::BtlSubPhase;
		const DWORD CmdCountAt = Rva::BtlCmdQueueCount - Rva::BtlSubPhase;
		const DWORD GilAt = Rva::BtlPendingGilCost - Rva::BtlSubPhase;
		const DWORD AiGlobalsRva = 0x00D36A68; // the AI staging span, as in BattleGlobalSpans
		const DWORD MenuOpenMaskRva = Rva::BtlMenuOpenMask;

		bool g_globalsOk = false; // the module spans were readable, set by BattleSpansConsistent

		// ---------------------------------------------------------------------------
		// Per session battle tracking, step thread only
		// ---------------------------------------------------------------------------

		struct Track
		{
			DWORD session; // the session these numbers belong to
			bool sessionKnown;
			bool active;       // a battle is being followed
			DWORD ordinal;     // battleOrdinal of the battle being followed, or the last one
			DWORD step;        // battleStep
			DWORD battlesSeen; // in this session
			bool dumpedSubPhase1;
			bool dumpedMenu;
			bool resultsPending; // the battle ended, waiting for the results screen to close
			bool resultsSeen;    // the menu ran since the battle ended
			DWORD lastTick;
			DWORD nextCallId;
		};
		Track g_track;

		int ClampCount(BYTE raw, int capacity)
		{
			const int value = (int)(signed char)raw;
			if (value <= 0)
				return 0;
			return value > capacity ? capacity : value;
		}

		BYTE WorkByte(const BYTE* work, DWORD at)
		{
			return work ? work[at] : 0;
		}

		void ReadRng(DWORD* streams, DWORD* battle, DWORD* effect, BYTE* flags, BYTE bit)
		{
			if (ffx::ReadBattleRandom(streams, battle, effect))
				*flags |= bit;
		}

		// Plain C on purpose: a function with __try cannot hold C++ objects that unwind.
		bool CopyGuarded(void* out, const void* from, DWORD bytes)
		{
			if (!from || !bytes)
				return false;
			__try
			{
				memcpy(out, from, bytes);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		// The packed unit bytes of all 31 units. False when a copy faulted.
		bool CopyUnits(BYTE* out, const BYTE* units)
		{
			__try
			{
				for (int u = 0; u < BattleUnitCount; ++u)
				{
					const BYTE* unit = units + (DWORD)u * BattleUnitStride;
					BYTE* at = out + u * BattleUnitPackedBytes;
					for (int i = 0; i < BattleUnitSpanCount; ++i)
					{
						memcpy(at, unit + BattleUnitSpans[i].offset, BattleUnitSpans[i].bytes);
						at += BattleUnitSpans[i].bytes;
					}
				}
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		void CopyGlobals(BYTE* out)
		{
			BYTE* at = out;
			for (int i = 0; i < BattleGlobalSpanCount; ++i)
			{
				memcpy(at, workshop::ModuleAddress(BattleGlobalSpans[i].rva), BattleGlobalSpans[i].bytes);
				at += BattleGlobalSpans[i].bytes;
			}
		}

		// ---------------------------------------------------------------------------
		// Kind 5, the step record
		// ---------------------------------------------------------------------------

		void WriteStep(const ffx::BattleTraceView& view, bool began, bool ended)
		{
			workshop::RecordFile* file = FileFor(KindBattle);
			if (!file)
				return;
			BattleStepRecord* r = (BattleStepRecord*)workshop::ReserveRecord(file);
			if (!r)
				return; // the budget is spent and the drop is counted

			FillPrefix(&r->prefix, KindBattle, 0, 0);
			r->battleOrdinal = g_track.ordinal;
			r->battleStep = g_track.step;
			r->unitsBase = (DWORD)(UINT_PTR)view.units;
			r->sideRecordsBase = (DWORD)(UINT_PTR)view.sideRecords;
			r->subPhase = (BYTE)view.subPhase;
			if (began)
				r->prefix.flags |= BattleStepBegan;
			if (ended)
				r->prefix.flags |= BattleStepEnded;

			ReadRng(r->streams, &r->battleRand, &r->effectRand, &r->prefix.flags, BattleStepRng);

			if (view.work && g_globalsOk)
			{
				const int turns = ClampCount(view.work[TurnCountAt], BattleTurnQueueSlots);
				const int cmds = ClampCount(view.work[CmdCountAt], Rva::BtlCmdQueueCapacity);
				r->turnQueueCount = (BYTE)turns;
				r->cmdQueueCount = (BYTE)cmds;
				const int copied = cmds > BattleCmdQueueSlotsRecorded ? BattleCmdQueueSlotsRecorded : cmds;
				r->cmdQueueCopied = (BYTE)copied;
				if (cmds > BattleCmdQueueSlotsRecorded)
					r->prefix.flags |= BattleStepCmdTruncated;
				// The whole turn queue, and the live command slots, so stale bytes past the
				// count never read as a command.
				memcpy(r->turnQueue, view.work + TurnQueueAt, sizeof(r->turnQueue));
				memcpy(r->cmdQueue, view.work + CmdQueueAt, (DWORD)copied * BattleCommandBytes);
				CopyGlobals(r->globals);
				r->prefix.flags |= BattleStepGlobals;
			}

			if (view.units && !ended)
			{
				if (CopyUnits(r->units, view.units))
					r->prefix.flags |= BattleStepUnits;
				else
					r->prefix.flags |= BattleStepFaulted;
			}
			workshop::CommitRecord(file);
		}

		// ---------------------------------------------------------------------------
		// Kind 7, the dump
		// ---------------------------------------------------------------------------

		void WriteDump(BattleDumpReason reason, const ffx::BattleTraceView& view)
		{
			workshop::RecordFile* file = FileFor(KindBattleDump);
			if (!file)
				return;
			BattleDump* d = (BattleDump*)workshop::ReserveRecord(file);
			if (!d)
				return;

			BattleDumpHead& h = d->head;
			FillPrefix(&h.prefix, KindBattleDump, (WORD)reason, 0);
			h.reason = (DWORD)reason;
			h.battleOrdinal = g_track.ordinal;
			h.battleStep = g_track.step;
			h.subPhase = (BYTE)view.subPhase;
			h.unitsBase = (DWORD)(UINT_PTR)view.units;
			h.sideRecordsBase = (DWORD)(UINT_PTR)view.sideRecords;
			if (ffx::ReadBattleRandom(h.streams, &h.battleRand, &h.effectRand))
				h.sections |= BattleDumpRng;

			bool faulted = false;
			if (view.units)
			{
				if (CopyGuarded(d->units, view.units, sizeof(d->units)))
					h.sections |= BattleDumpUnits;
				else
					faulted = true;
			}
			if (view.sideRecords)
			{
				if (CopyGuarded(d->sides, view.sideRecords, sizeof(d->sides)))
					h.sections |= BattleDumpSides;
				else
					faulted = true;
			}
			if (g_globalsOk)
			{
				memcpy(d->save, workshop::ModuleAddress(Rva::SaveData), sizeof(d->save));
				memcpy(d->ai, workshop::ModuleAddress(AiGlobalsRva), sizeof(d->ai));
				h.sections |= BattleDumpSave | BattleDumpAi;
			}
			if (view.work)
			{
				memcpy(d->work, view.work, sizeof(d->work));
				h.sections |= BattleDumpWork;
			}
			for (int group = 0; group < 2; ++group)
			{
				const BYTE* base = NULL;
				DWORD bytes = 0;
				if (!ffx::BattleClass3Storage(group, &base, &bytes))
					continue;
				h.class3Base[group] = (DWORD)(UINT_PTR)base;
				h.class3Bytes[group] = bytes;
				const DWORD copy = bytes > (DWORD)BattleClass3Capacity ? (DWORD)BattleClass3Capacity : bytes;
				if (copy && CopyGuarded(d->class3[group], base, copy))
					h.sections |= group == 0 ? BattleDumpClass3Party : BattleDumpClass3Monster;
				else if (copy)
					faulted = true;
			}
			if (faulted)
				h.sections |= BattleDumpFaulted;
			workshop::CommitRecord(file);
		}

		bool MenuOpen()
		{
			return g_globalsOk && *(const volatile DWORD*)workshop::ModuleAddress(MenuOpenMaskRva) != 0;
		}

		// A menu of any kind holds the step. The results screen is menu module 17, and
		// while MenuSysRunningAlt is up FFX_StepPacing does not run at all, which shows as a
		// gap in presented frames between two pacing calls.
		bool MenuHeld(DWORD tick)
		{
			ffx::StepPacingVars vars;
			if (ffx::ReadStepPacingVars(&vars) && (vars.menuSysRunning || vars.menuSysRunningAlt || vars.menuOpenPending))
				return true;
			return g_track.lastTick && tick - g_track.lastTick > 30;
		}

		void ResetTrack(DWORD session)
		{
			ZeroMemory(&g_track, sizeof(g_track));
			g_track.session = session;
			g_track.sessionKnown = true;
		}

		// ---------------------------------------------------------------------------
		// Kind 6, the hooks
		// ---------------------------------------------------------------------------

		const int MaxDepth = 8;

		struct OpenCall
		{
			bool valid;
			WORD type;
			DWORD callId;
			DWORD parentCallId;
			DWORD args[BattleEventArgCount];
			BYTE subPhase;
			BYTE cmdCount;
			BYTE turnCount;
			int gil;
			bool rngOk;
			DWORD battleRand;
			DWORD streams[BattleStreamCount];
			BYTE block[BattleCommandBytes];
			bool faulted;
			BYTE queueBefore[62 * BattleCommandBytes]; // Insert only, to find where it landed
		};
		OpenCall g_open[MaxDepth];
		int g_depth = 0;
		volatile long g_offThread = 0;
		long g_offThreadLogged = 0; // step thread only

		WORD EventType(int call)
		{
			switch (call)
			{
			case ffx::BattleCallAiRun:
				return BattleEventAiRun;
			case ffx::BattleCallQueuePush:
				return BattleEventQueuePush;
			case ffx::BattleCallQueueInsert:
				return BattleEventQueueInsert;
			case ffx::BattleCallHitCompute:
				return BattleEventHit;
			}
			return 0;
		}

		void OnEntry(OpenCall& c, WORD type, const DWORD* args, int argCount, int depth)
		{
			ZeroMemory(c.args, sizeof(c.args));
			c.type = type;
			c.callId = g_track.nextCallId++;
			c.parentCallId = NoParentCall;
			for (int up = depth - 1; up >= 0; --up)
			{
				if (g_open[up].valid)
				{
					c.parentCallId = g_open[up].callId;
					break;
				}
			}
			for (int i = 0; i < argCount && i < BattleEventArgCount; ++i)
				c.args[i] = args[i];

			ffx::BattleTraceView view;
			ffx::ReadBattleTraceView(&view);
			c.subPhase = (BYTE)view.subPhase;
			c.cmdCount = (BYTE)ClampCount(WorkByte(view.work, CmdCountAt), Rva::BtlCmdQueueCapacity);
			c.turnCount = (BYTE)ClampCount(WorkByte(view.work, TurnCountAt), BattleTurnQueueSlots);
			c.gil = view.work ? *(const int*)(view.work + GilAt) : 0;
			c.rngOk = ffx::ReadBattleRandom(c.streams, &c.battleRand, NULL);

			ZeroMemory(c.block, sizeof(c.block));
			c.faulted = false;
			if (type == BattleEventAiRun && args[5])
				c.faulted = !CopyGuarded(c.block, (const void*)(UINT_PTR)args[5], BattleCommandBytes);
			else if (type == BattleEventQueuePush || type == BattleEventQueueInsert)
				c.faulted = !CopyGuarded(c.block, (const void*)(UINT_PTR)args[1], BattleCommandBytes);
			else if (type == BattleEventHit)
				c.faulted = !CopyGuarded(c.block, (const void*)(UINT_PTR)args[6], 44);

			if (type == BattleEventQueueInsert && view.work)
				memcpy(c.queueBefore, view.work + CmdQueueAt, (DWORD)c.cmdCount * BattleCommandBytes);
		}

		void OnExit(OpenCall& c, int depth, int result)
		{
			workshop::RecordFile* file = FileFor(KindBattleEvent);
			if (!file)
				return;
			BattleEventRecord* r = (BattleEventRecord*)workshop::ReserveRecord(file);
			if (!r)
				return;

			FillPrefix(&r->prefix, KindBattleEvent, (WORD)depth, 0);
			r->type = c.type;
			r->depth = (WORD)depth;
			r->callId = c.callId;
			r->parentCallId = c.parentCallId;
			r->battleOrdinal = g_track.ordinal;
			r->battleStep = g_track.step;
			memcpy(r->args, c.args, sizeof(r->args));
			r->result = result;
			r->subPhaseEntry = c.subPhase;
			r->cmdQueueCountEntry = c.cmdCount;
			r->turnQueueCountEntry = c.turnCount;
			r->pendingGil = c.gil;
			r->battleRandEntry = c.battleRand;
			memcpy(r->streamsEntry, c.streams, sizeof(r->streamsEntry));
			memcpy(r->blockEntry, c.block, sizeof(r->blockEntry));
			r->slot = NoQueueSlot;
			r->class3Group = NoClass3Group;
			bool faulted = c.faulted;

			ffx::BattleTraceView view;
			ffx::ReadBattleTraceView(&view);
			r->subPhaseExit = (BYTE)view.subPhase;
			const int cmds = ClampCount(WorkByte(view.work, CmdCountAt), Rva::BtlCmdQueueCapacity);
			r->cmdQueueCountExit = (BYTE)cmds;
			r->turnQueueCountExit = (BYTE)ClampCount(WorkByte(view.work, TurnCountAt), BattleTurnQueueSlots);
			if (c.rngOk && ffx::ReadBattleRandom(r->streamsExit, &r->battleRandExit, NULL))
				r->prefix.flags |= BattleEventRng;

			if (c.type == BattleEventAiRun)
			{
				if (g_globalsOk)
				{
					const BYTE* ai = (const BYTE*)workshop::ModuleAddress(AiGlobalsRva);
					memcpy(r->blockExit, ai + (Rva::BtlAiStagedCmd - AiGlobalsRva), BattleCommandBytes);
					r->aiPriority = *(const int*)(ai + (Rva::BtlAiStagedPriority - AiGlobalsRva));
					r->aiNoReorder = *(const int*)(ai + (Rva::BtlAiStagedNoReorder - AiGlobalsRva));
				}
				// FFX_Btl_RunAiAndCommit takes a u8 unit and runs an enemy slot (20..27) in
				// context 3, anything else in context 2.
				const DWORD unit = c.args[0] & 0xFF;
				const int group = (unit - 20u) <= 7u ? 1 : 0;
				const BYTE* base = NULL;
				DWORD bytes = 0;
				if (ffx::BattleClass3Storage(group, &base, &bytes))
				{
					r->class3Group = (DWORD)group;
					r->class3Base = (DWORD)(UINT_PTR)base;
					r->class3Bytes = bytes;
					DWORD copy = bytes;
					if (copy > (DWORD)BattleClass3Capacity)
					{
						copy = BattleClass3Capacity;
						r->prefix.flags |= BattleEventClass3Truncated;
					}
					if (!copy || CopyGuarded(r->class3, base, copy))
						r->prefix.flags |= BattleEventClass3;
					else
						faulted = true;
				}
			}
			else if ((c.type == BattleEventQueuePush || c.type == BattleEventQueueInsert) && view.work && result != 0)
			{
				// Both return -1 when the record went in and 0 when the queue was full.
				int slot = -1;
				if (c.type == BattleEventQueuePush)
				{
					slot = c.cmdCount;
				}
				else
				{
					// Everything from the insertion point moved down one, so the first slot
					// that differs from before is the new one. Equal bytes either side of it
					// make the choice between them immaterial.
					const BYTE* now = view.work + CmdQueueAt;
					slot = c.cmdCount;
					for (int i = 0; i < c.cmdCount; ++i)
					{
						if (memcmp(now + i * BattleCommandBytes, c.queueBefore + i * BattleCommandBytes, BattleCommandBytes) != 0)
						{
							slot = i;
							break;
						}
					}
				}
				if (slot >= 0 && slot < cmds)
				{
					r->slot = (BYTE)slot;
					r->prefix.flags |= BattleEventSlotFound;
					memcpy(r->blockExit, view.work + CmdQueueAt + (DWORD)slot * BattleCommandBytes, BattleCommandBytes);
				}
			}
			else if (c.type == BattleEventHit)
			{
				if (!CopyGuarded(r->blockExit, (const void*)(UINT_PTR)c.args[6], 44))
					faulted = true;
				if (c.args[7] && !CopyGuarded(r->hitExtra, (const void*)(UINT_PTR)c.args[7], 3 * sizeof(int)))
					faulted = true;
				if (c.args[10] && !CopyGuarded(&r->hitExtra[3], (const void*)(UINT_PTR)c.args[10], sizeof(int)))
					faulted = true;
			}
			if (faulted)
				r->prefix.flags |= BattleEventFaulted;
			workshop::CommitRecord(file);
		}

		void CommittedDump()
		{
			if (!settings.battle || !g_track.active || !BeginRecord())
				return;
			if (g_track.sessionKnown && g_track.session == SessionIndex())
			{
				ffx::BattleTraceView view;
				ffx::ReadBattleTraceView(&view);
				WriteDump(BattleDumpCommitted, view);
			}
			EndRecord();
		}

		void __cdecl OnBattleCall(int call, const DWORD* args, int argCount, bool afterCall, int result)
		{
			// A record file has one producer. Every one of these runs inside the battle sub
			// step, so this never fires, but a call from a job worker would corrupt a chunk.
			if (!OnStepThread())
			{
				InterlockedIncrement(&g_offThread);
				return;
			}
			if (call == ffx::BattleCallCommitToSave)
			{
				if (afterCall)
					CommittedDump();
				return;
			}
			const WORD type = EventType(call);
			if (!type)
				return;

			if (!afterCall)
			{
				const int depth = g_depth++;
				if (depth >= MaxDepth)
					return;
				OpenCall& c = g_open[depth];
				// Only inside a battle the step snapshot is following, so every event has a
				// battle ordinal and a step it belongs to.
				c.valid = settings.battle && Recording() && g_track.active && g_track.sessionKnown && g_track.session == SessionIndex();
				if (c.valid)
					OnEntry(c, type, args, argCount, depth);
				return;
			}

			if (g_depth <= 0)
				return; // an exit without its entry, the hook went in mid call
			const int depth = --g_depth;
			if (depth >= MaxDepth)
				return;
			OpenCall& c = g_open[depth];
			if (!c.valid)
				return;
			c.valid = false;
			if (!BeginRecord())
				return;
			// The session can only change while nothing records, so a stash from another
			// session is dropped here rather than written with the wrong numbers.
			if (g_track.session == SessionIndex())
				OnExit(c, depth, result);
			EndRecord();
		}

		bool SpansOk(const ChrSpan* spans, int count, int total, DWORD stride, const char* what)
		{
			int sum = 0;
			int previousEnd = 0;
			for (int i = 0; i < count; ++i)
			{
				if (spans[i].offset < previousEnd || spans[i].offset + spans[i].bytes > (int)stride)
				{
					workshop::Log("battle recipe: %s span %d at +0x%03X overlaps or runs off the record, "
					              "battle recording is off",
					    what, i, spans[i].offset);
					return false;
				}
				previousEnd = spans[i].offset + spans[i].bytes;
				sum += spans[i].bytes;
			}
			if (sum != total)
			{
				workshop::Log("battle recipe: the %s spans add up to %d bytes and the record holds %d, "
				              "battle recording is off",
				    what, sum, total);
				return false;
			}
			return true;
		}

	} // namespace

	void OnBattleStep(DWORD frameTick)
	{
		// Reported here rather than from the refusing thread, which could be a job worker in
		// the middle of a battle load. One line per change, so it costs nothing when the count
		// stays 0, which it should: every hooked call is reached from the battle sub step.
		const long offThread = g_offThread;
		if (offThread != g_offThreadLogged)
		{
			g_offThreadLogged = offThread;
			workshop::Log("battle recipe: %ld battle calls so far came from another thread and were not recorded",
			    offThread);
		}
		if (!settings.battle || !BeginRecord())
			return;
		if (!FileFor(KindBattle) && !FileFor(KindBattleDump))
		{
			EndRecord();
			return;
		}
		if (!g_track.sessionKnown || g_track.session != SessionIndex())
			ResetTrack(SessionIndex());

		ffx::BattleTraceView view;
		const bool running = ffx::ReadBattleTraceView(&view);
		if (running)
		{
			bool began = false;
			if (!g_track.active)
			{
				g_track.active = true;
				g_track.ordinal = g_track.battlesSeen++;
				g_track.step = 0;
				g_track.dumpedSubPhase1 = false;
				g_track.dumpedMenu = false;
				g_track.resultsPending = false;
				began = true;
			}
			else
			{
				++g_track.step;
			}
			WriteStep(view, began, false);
			if (began)
				WriteDump(BattleDumpSeen, view);
			if (!g_track.dumpedSubPhase1 && view.subPhase == 1)
			{
				g_track.dumpedSubPhase1 = true;
				WriteDump(BattleDumpSubPhase1, view);
			}
			if (!g_track.dumpedMenu && MenuOpen())
			{
				g_track.dumpedMenu = true;
				WriteDump(BattleDumpFirstMenu, view);
			}
		}
		else if (g_track.active)
		{
			// The battle tore down inside the last step: FFX_Btl_MainStep commits, shuts
			// down and queues the field boot in one call. One closing step record, the save
			// block as it stands, and then wait for the results screen.
			++g_track.step;
			WriteStep(view, false, true);
			WriteDump(BattleDumpGone, view);
			g_track.active = false;
			g_track.resultsPending = true;
			g_track.resultsSeen = false;
		}
		else if (g_track.resultsPending)
		{
			if (MenuHeld(frameTick))
			{
				g_track.resultsSeen = true;
			}
			else if (g_track.resultsSeen)
			{
				g_track.resultsPending = false;
				WriteDump(BattleDumpResultsClosed, view);
			}
		}
		g_track.lastTick = frameTick;
		EndRecord();
	}

	bool BattleSpansConsistent()
	{
		if (!SpansOk(BattleUnitSpans, BattleUnitSpanCount, BattleUnitPackedBytes, BattleUnitStride, "unit"))
			return false;

		int sum = 0;
		for (int i = 0; i < BattleGlobalSpanCount; ++i)
		{
			const GlobalSpan& span = BattleGlobalSpans[i];
			if (!workshop::RvaInImage(span.rva) || !workshop::RvaInImage(span.rva + span.bytes - 1))
			{
				workshop::Log("battle recipe: global span %d at RVA 0x%08X is outside FFX.exe, battle "
				              "recording is off",
				    i, span.rva);
				return false;
			}
			sum += (int)span.bytes;
		}
		if (sum != BattleGlobalBytes)
		{
			workshop::Log("battle recipe: the global spans add up to %d bytes and the record holds %d, "
			              "battle recording is off",
			    sum, BattleGlobalBytes);
			return false;
		}
		if (CmdQueueAt + (DWORD)Rva::BtlCmdQueueCapacity * BattleCommandBytes > (DWORD)BattleWorkBytes || TurnQueueAt + BattleTurnQueueSlots * BattleTurnEntryBytes > CmdQueueAt || GilAt + 4 > (DWORD)BattleWorkBytes || CmdCountAt >= (DWORD)BattleWorkBytes)
		{
			workshop::Log("battle recipe: the queues are not inside the work block, battle recording is off");
			return false;
		}

		// Module globals, so one check answers for the life of the process. The control
		// thread runs this when a capture starts, never the step thread.
		if (!g_globalsOk)
		{
			bool ok = workshop::Readable(workshop::ModuleAddress(Rva::SaveData), SaveBlockBytes) && workshop::Readable(workshop::ModuleAddress(AiGlobalsRva), BattleAiGlobalBytes);
			for (int i = 0; ok && i < BattleGlobalSpanCount; ++i)
				ok = workshop::Readable(workshop::ModuleAddress(BattleGlobalSpans[i].rva), BattleGlobalSpans[i].bytes);
			if (!ok)
			{
				workshop::Log("battle recipe: a battle global is not readable, battle recording is off");
				return false;
			}
			g_globalsOk = true;
		}
		return true;
	}

	DWORD BattleExtensionBytes(void* out, DWORD capacity)
	{
		BattleExtension e;
		e.unitStride = BattleUnitStride;
		e.unitCount = BattleUnitCount;
		e.unitPackedBytes = BattleUnitPackedBytes;
		e.streamCount = BattleStreamCount;
		e.turnQueueSlots = BattleTurnQueueSlots;
		e.cmdQueueSlots = BattleCmdQueueSlotsRecorded;
		e.globalBytes = BattleGlobalBytes;
		e.unitSpanCount = (DWORD)BattleUnitSpanCount;
		memcpy(e.unitSpans, BattleUnitSpans, sizeof(e.unitSpans));
		e.globalSpanCount = (DWORD)BattleGlobalSpanCount;
		memcpy(e.globalSpans, BattleGlobalSpans, sizeof(e.globalSpans));
		if (capacity < sizeof(e))
			return 0;
		memcpy(out, &e, sizeof(e));
		return sizeof(e);
	}

	DWORD BattleEventExtensionBytes(void* out, DWORD capacity)
	{
		BattleEventExtension e;
		e.argCount = BattleEventArgCount;
		e.streamCount = BattleStreamCount;
		e.blockBytes = BattleCommandBytes;
		e.class3Capacity = BattleClass3Capacity;
		if (capacity < sizeof(e))
			return 0;
		memcpy(out, &e, sizeof(e));
		return sizeof(e);
	}

	DWORD BattleDumpExtensionBytes(void* out, DWORD capacity)
	{
		BattleDumpExtension e;
		e.headBytes = sizeof(BattleDumpHead);
		e.unitStride = BattleUnitStride;
		e.unitCount = BattleUnitCount;
		e.sideStride = BattleSideRecordStride;
		e.sideCount = BattleSideRecordCount;
		e.saveBytes = SaveBlockBytes;
		e.workRva = Rva::BtlSubPhase;
		e.workBytes = BattleWorkBytes;
		e.aiRva = AiGlobalsRva;
		e.aiBytes = BattleAiGlobalBytes;
		e.class3Capacity = BattleClass3Capacity;
		if (capacity < sizeof(e))
			return 0;
		memcpy(out, &e, sizeof(e));
		return sizeof(e);
	}

	bool InstallBattleRecipe()
	{
		const bool ai = ffx::HookBattleAiRun(&OnBattleCall);
		const bool queue = ffx::HookBattleQueueAdds(&OnBattleCall);
		const bool hit = ffx::HookBattleHitCompute(&OnBattleCall);
		const bool commit = ffx::HookBattleCommitToSave(&OnBattleCall);
		workshop::Log("battle recipe: RunAiAndCommit %s, queue Push and Insert %s, ComputeEffectForTarget %s, "
		              "CommitActorsToSave call site %s. The per step snapshot rides the pacing hook",
		    ai ? "hooked" : "NOT HOOKED", queue ? "hooked" : "NOT HOOKED", hit ? "hooked" : "NOT HOOKED",
		    commit ? "patched" : "NOT PATCHED, no committed dump");
		// The step snapshot and the dumps need no hook of their own and work whenever the
		// pacing hook is in. This answers for the four hooks only.
		return ai || queue || hit || commit;
	}

} // namespace probe
