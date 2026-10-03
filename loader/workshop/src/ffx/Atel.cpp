#include "ffx/Atel.h"

#include "workshop/HostModule.h"
#include "workshop/Log.h"
#include "ffx/addresses/Atel.h"
#include "ffx/addresses/Character.h"
#include "ffx/Layout.h"

#include <string.h>

// Implementation notes worth reading before changing anything here.
//
// This file resolves its own function pointers rather than going through
// ffx::Game, the same choice GameState.cpp made and for the same reason: the
// shared Api.h is not this subsystem's to edit, and the cost is one Resolve
// helper while the benefit is that Atel works whether or not BindApi has run.
//
// TWO THINGS THAT WILL BITE WHOEVER EDITS THIS NEXT.
//
// 1. Rva::AtelContextPtr names a POINTER SLOT, not the context struct. Every
//    read is two indirections. Ctx0() is the only place that knows it, so if a
//    field read is ever wrong, it is wrong there and nowhere else.
//
// 2. Several game functions read whatever context happens to be selected rather
//    than selecting one themselves. FFX_Atel_GetActorKind, FFX_Atel_GetActor,
//    FFX_Atel_FindActorByPartyChar and FFX_Atel_FindActorIdByChr are all like
//    that. So every call to one of them here is bracketed with
//    SelectContext(0) / RestoreContext, which is exactly what the engine's own
//    public getters do. Forgetting the bracket does not fault, it quietly reads
//    a different world, which is far worse.
//
// Everything is read volatile, because the game thread writes these behind us.
//
// The derivation is in ..\..\..\reversing\INTERACTION_PATH.md.

namespace ffx
{

	using workshop::Log;
	using workshop::LooksLikePointer;
	using workshop::ModuleAddress;
	using workshop::Readable;

	namespace
	{

		// ---------------------------------------------------------------------------
		// Game function types. All __cdecl, all read back out of the IDB.
		// ---------------------------------------------------------------------------
		typedef int(__cdecl* SelectContextFn)(int index);
		typedef int(__cdecl* RestoreContextFn)(int saved);
		typedef int(__cdecl* GetActorFn)(int actorId);
		typedef int(__cdecl* GetActorKindFn)(int actorId);
		typedef int(__cdecl* GetPlayerActorIdFn)(void);
		typedef int(__cdecl* FireActorEventFn)(int callerActorId, int targetActorId, int eventKind);
		typedef int(__cdecl* GetEventChannelFn)(int actorKind, int eventKind);
		typedef int(__cdecl* GetEventScriptEntryFn)(int actorKind, int eventKind, const WORD* override);
		typedef int(__cdecl* FindActorIdByChrFn)(int chr);
		typedef int(__cdecl* FindActorByPartyCharFn)(int charIndex);
		typedef int(__cdecl* MesWinBlockingKindFn)(void);
		typedef int(__cdecl* GrantTalkBonusOnceFn)(int actorId, int eventKind);

		// The three trigger steppers. The second argument is a delta time that none of
		// the three actually reads, which was checked rather than assumed. It is passed
		// through anyway so the call matches the engine's.
		typedef int(__cdecl* TestPlayerProximityFn)(void* actor, int dt, void* playerActor);
		typedef void(__cdecl* StepLineTriggerFn)(void* actor, int dt, void* playerActor);
		typedef void(__cdecl* StepBoxTriggerFn)(void* actor, int dt, void* playerActor);

		template <class T>
		T Resolve(DWORD rva)
		{
			return (T)ModuleAddress(rva);
		}

		inline BYTE Rd8(const BYTE* p)
		{
			return *(volatile const BYTE*)p;
		}
		inline WORD Rd16(const BYTE* p)
		{
			return *(volatile const WORD*)p;
		}
		inline DWORD Rd32(const BYTE* p)
		{
			return *(volatile const DWORD*)p;
		}
		inline float RdF(const BYTE* p)
		{
			return *(volatile const float*)p;
		}

		inline void Wr8(BYTE* p, BYTE v)
		{
			*(volatile BYTE*)p = v;
		}
		inline void Wr16(BYTE* p, WORD v)
		{
			*(volatile WORD*)p = v;
		}
		inline void WrF(BYTE* p, float v)
		{
			*(volatile float*)p = v;
		}

		// A guarded read of one field. Returns false rather than faulting, which is the
		// only acceptable behaviour on the frame path.
		inline bool Field8(const BYTE* base, DWORD off, BYTE* out)
		{
			if (!Readable(base + off, 1))
				return false;
			*out = Rd8(base + off);
			return true;
		}

		inline bool Field16(const BYTE* base, DWORD off, WORD* out)
		{
			if (!Readable(base + off, 2))
				return false;
			*out = Rd16(base + off);
			return true;
		}

		inline bool Field32(const BYTE* base, DWORD off, DWORD* out)
		{
			if (!Readable(base + off, 4))
				return false;
			*out = Rd32(base + off);
			return true;
		}

		// ---------------------------------------------------------------------------
		// The context, and the only place the double indirection lives.
		// ---------------------------------------------------------------------------

		// Whatever is selected right now.
		BYTE* CtxSelected()
		{
			const BYTE* slot = (const BYTE*)ModuleAddress(Rva::AtelContextPtr);
			if (!Readable(slot, sizeof(void*)))
				return NULL;
			DWORD raw = Rd32(slot);
			if (!LooksLikePointer(raw))
				return NULL;
			BYTE* ctx = (BYTE*)(UINT_PTR)raw;
			if (!Readable(ctx, AtelCtx::Size))
				return NULL;
			return ctx;
		}

		// Context 0, which is the one every public ATEL getter uses and the one a mod
		// should walk. Taken from the array base rather than the pointer slot, so it is
		// correct even mid-swap.
		BYTE* Ctx0()
		{
			BYTE* ctx = (BYTE*)ModuleAddress(Rva::AtelContextArray);
			if (!Readable(ctx, AtelCtx::Size))
				return NULL;
			return ctx;
		}

		// The actor count out of context 0, or 0.
		int CtxActorCount()
		{
			BYTE* ctx = Ctx0();
			if (!ctx)
				return 0;
			return (int)Rd16(ctx + AtelCtx::ActorCount);
		}

		bool IdInRange(int actorId)
		{
			int count = CtxActorCount();
			return count > 0 && actorId >= 0 && actorId < count;
		}

		// Clamped so a caller cannot run the engine's unbounded table index off the end.
		bool KindInRange(int eventKind)
		{
			return eventKind >= 0 && eventKind < kAtelEventKindCount;
		}

		// The select / restore bracket. Kept as two calls rather than an object because
		// the house style here has no classes.
		int SelectCtx0()
		{
			return Resolve<SelectContextFn>(Rva::AtelSelectContext)(0);
		}

		void RestoreCtx(int saved)
		{
			Resolve<RestoreContextFn>(Rva::AtelRestoreContext)(saved);
		}

		// The actor record, with context 0 selected for the lookup. Caller has already
		// bounds checked the id.
		BYTE* ActorPtrUnchecked(int actorId)
		{
			int saved = SelectCtx0();
			int raw = Resolve<GetActorFn>(Rva::AtelGetActor)(actorId);
			RestoreCtx(saved);

			if (!LooksLikePointer((DWORD)(unsigned)raw))
				return NULL;
			BYTE* actor = (BYTE*)(UINT_PTR)(unsigned)raw;
			// The smallest group in the pool is 48 bytes, so that is the only size that
			// is safe to demand up front. Every field above that validates itself.
			if (!Readable(actor, 48))
				return NULL;
			return actor;
		}

		int ActorKindUnchecked(int actorId)
		{
			int saved = SelectCtx0();
			int kind = Resolve<GetActorKindFn>(Rva::AtelGetActorKind)(actorId);
			RestoreCtx(saved);
			return kind;
		}

		// Where this kind's position sub-struct sits, or 0 for "it has none". Mirrors
		// the engine's own three-way test exactly:
		//     if (kind == 4) none; else if ((u8)(kind - 5) > 1) +1368; else +644;
		DWORD PosOffsetForKind(int kind)
		{
			if (kind == kAtelActorPlain)
				return 0;
			if (kind == kAtelActorPathTrigger || kind == kAtelActorVolumeTrigger)
			{
				return AtelActor::PosPathVol;
			}
			return AtelActor::PosDefault;
		}

		// The position sub-struct for an actor, or NULL.
		BYTE* ActorPosPtr(int actorId)
		{
			if (!IdInRange(actorId))
				return NULL;
			BYTE* actor = ActorPtrUnchecked(actorId);
			if (!actor)
				return NULL;

			DWORD off = PosOffsetForKind(ActorKindUnchecked(actorId));
			if (off == 0)
				return NULL;
			if (!Readable(actor + off, AtelPos::Size))
				return NULL;
			return actor + off;
		}

		// Squared horizontal-and-vertical distance from an actor to a point, or a
		// negative value when the actor has no position.
		float ActorDistSq(int actorId, const float* xyz)
		{
			BYTE* pos = ActorPosPtr(actorId);
			if (!pos)
				return -1.0f;
			float dx = RdF(pos + AtelPos::X) - xyz[0];
			float dy = RdF(pos + AtelPos::Y) - xyz[1];
			float dz = RdF(pos + AtelPos::Z) - xyz[2];
			return dx * dx + dy * dy + dz * dz;
		}

		// The CHR pool, resolved without BindApi so this file has no ordering
		// dependency on it.
		BYTE* ChrPoolBase()
		{
			const BYTE* slot = (const BYTE*)ModuleAddress(Rva::ChrArray);
			if (!Readable(slot, sizeof(void*)))
				return NULL;
			DWORD raw = Rd32(slot);
			if (!LooksLikePointer(raw))
				return NULL;
			return (BYTE*)(UINT_PTR)raw;
		}

		int ChrPoolCount()
		{
			const int* slot = (const int*)ModuleAddress(Rva::ChrCount);
			if (!Readable(slot, sizeof(int)))
				return 0;
			int n = *(volatile const int*)slot;
			return (n > 0) ? n : 0;
		}

		// A thread list can in principle be corrupt. Walking it is a frame-path read, so
		// it gets a hard iteration bound rather than trusting the terminator.
		const int kThreadWalkLimit = 64;

		// The sub-kind byte at actor+170. 1 means CHR backed, and the proximity test
		// refuses to run at all on anything else, so it is worth logging.
		int ActorSubKindUnchecked(int actorId)
		{
			BYTE* actor = ActorPtrUnchecked(actorId);
			if (!actor)
				return -1;
			BYTE sub = 0;
			if (!Field8(actor, AtelActor::SubKind, &sub))
				return -1;
			return (int)sub;
		}

	} // namespace

	// ---------------------------------------------------------------------------
	// Is there a world to look at?
	// ---------------------------------------------------------------------------

	BYTE* AtelContext()
	{
		return CtxSelected();
	}

	BYTE* AtelContext0()
	{
		return Ctx0();
	}

	bool AtelReady()
	{
		BYTE* ctx = Ctx0();
		if (!ctx)
			return false;
		if (Rd16(ctx + AtelCtx::ActorCount) == 0)
			return false;
		// The pool pointer is the thing that is NULL at the title screen while the
		// rest of the struct already reads as plausible zeroes.
		return LooksLikePointer(Rd32(ctx + AtelCtx::ActorPool));
	}

	int ActorCount()
	{
		return CtxActorCount();
	}

	// ---------------------------------------------------------------------------
	// The bound player
	// ---------------------------------------------------------------------------

	int BoundPlayerActorId()
	{
		if (!Ctx0())
			return kAtelActorIdNone;
		// Through the game's getter so a future change to the sentinel handling
		// follows the engine. It maps 0xFFFF to -1, so map it back.
		int saved = SelectCtx0();
		int id = Resolve<GetPlayerActorIdFn>(Rva::AtelGetPlayerActorId)();
		RestoreCtx(saved);
		return (id < 0) ? kAtelActorIdNone : id;
	}

	bool SetBoundPlayerActorId(int actorId)
	{
		BYTE* ctx = Ctx0();
		if (!ctx)
			return false;
		if (actorId != kAtelActorIdNone && !IdInRange(actorId))
			return false;

		// Deliberately a raw write rather than FFX_Atel_SetPlayerActorId. The game's
		// setter also copies the new actor's position into BOTH halves of the
		// position cache, which throws away the previous position and so loses any
		// swept trigger crossing in flight. If you want the engine's exact
		// behaviour, follow this with SetPlayerPosCacheFromActor and pass the same
		// position twice.
		Wr16(ctx + AtelCtx::BoundPlayerId, (WORD)actorId);
		return true;
	}

	// ---------------------------------------------------------------------------
	// Reading an actor
	// ---------------------------------------------------------------------------

	AtelActorData* ActorRecord(int actorId)
	{
		if (!IdInRange(actorId))
			return NULL;
		return (AtelActorData*)ActorPtrUnchecked(actorId);
	}

	int ActorKind(int actorId)
	{
		// The engine returns 7 for an out of range id and 7 has no channel in any
		// table, which is its own safety net. Bounds check anyway so the answer does
		// not depend on the engine's clamp.
		if (!IdInRange(actorId))
			return kAtelActorInvalid;
		return ActorKindUnchecked(actorId);
	}

	bool ActorPosition(int actorId, float* xyz)
	{
		if (!xyz)
			return false;
		BYTE* pos = ActorPosPtr(actorId);
		if (!pos)
			return false;
		xyz[0] = RdF(pos + AtelPos::X);
		xyz[1] = RdF(pos + AtelPos::Y);
		xyz[2] = RdF(pos + AtelPos::Z);
		return true;
	}

	bool ActorPreviousPosition(int actorId, float* xyz)
	{
		if (!xyz)
			return false;
		BYTE* pos = ActorPosPtr(actorId);
		if (!pos)
			return false;
		xyz[0] = RdF(pos + AtelPos::PrevX);
		xyz[1] = RdF(pos + AtelPos::PrevY);
		xyz[2] = RdF(pos + AtelPos::PrevZ);
		return true;
	}

	int ActorTargetId(int actorId)
	{
		BYTE* pos = ActorPosPtr(actorId);
		if (!pos)
			return kAtelActorIdNone;
		return (int)Rd16(pos + AtelPos::TargetId);
	}

	BYTE ActorEventMask(int actorId)
	{
		if (!IdInRange(actorId))
			return 0;
		BYTE* actor = ActorPtrUnchecked(actorId);
		if (!actor)
			return 0;
		BYTE mask = 0;
		if (!Field8(actor, AtelActor::EventMask, &mask))
			return 0;
		return mask;
	}

	int ActorPartyCharacter(int actorId)
	{
		if (!IdInRange(actorId))
			return -1;
		BYTE* actor = ActorPtrUnchecked(actorId);
		if (!actor)
			return -1;
		DWORD raw = 0;
		if (!Field32(actor, AtelActor::PartyCharacter, &raw))
			return -1;
		// The field is written as both -1 and 255 for "nobody", depending on which
		// engine path filled it in.
		if (raw == 0xFFFFFFFFu || raw == 0xFFu)
			return -1;
		if (raw > 7u)
			return -1;
		return (int)raw;
	}

	float ActorTriggerRadius(int actorId)
	{
		BYTE* pos = ActorPosPtr(actorId);
		if (!pos)
			return 0.0f;
		return RdF(pos + AtelPos::TriggerRadius);
	}

	float ActorExamineRadius(int actorId)
	{
		BYTE* pos = ActorPosPtr(actorId);
		if (!pos)
			return 0.0f;
		return RdF(pos + AtelPos::ExamineRadius);
	}

	float AtelPlayerRadius()
	{
		BYTE* ctx = Ctx0();
		if (!ctx)
			return 0.0f;
		return RdF(ctx + AtelCtx::PlayerRadius);
	}

	// ---------------------------------------------------------------------------
	// Firing an event
	// ---------------------------------------------------------------------------

	int FireActorEventAs(int callerActorId, int actorId, int eventKind)
	{
		// The engine function does NOT do either of these checks and needs both. It
		// dereferences actor+68 and actor+171 before its own gates can reject
		// anything, through a pointer FFX_Atel_GetActor built from a CLAMPED index,
		// and it indexes an 8-entry table with the raw event kind.
		if (!KindInRange(eventKind))
			return 0;
		if (!IdInRange(actorId))
			return 0;
		if (callerActorId != kAtelNoCaller && !IdInRange(callerActorId))
			return 0;
		if (!AtelReady())
			return 0;

		return Resolve<FireActorEventFn>(Rva::AtelFireActorEvent)(callerActorId, actorId, eventKind);
	}

	int FireActorEvent(int actorId, int eventKind)
	{
		return FireActorEventAs(kAtelNoCaller, actorId, eventKind);
	}

	int FireActorEventSyncAs(int callerActorId, int actorId, int eventKind)
	{
		if (!KindInRange(eventKind))
			return 0;
		if (!IdInRange(actorId))
			return 0;
		if (callerActorId != kAtelNoCaller && !IdInRange(callerActorId))
			return 0;
		if (!AtelReady())
			return 0;

		return Resolve<FireActorEventFn>(Rva::AtelFireActorEventSync)(callerActorId, actorId, eventKind);
	}

	// ---------------------------------------------------------------------------
	// Would the command do anything?
	// ---------------------------------------------------------------------------

	int EventChannel(int actorKind, int eventKind)
	{
		if (!KindInRange(eventKind))
			return kAtelChannelNone;
		// No context needed, this is a pure table lookup on two small integers.
		int channel = Resolve<GetEventChannelFn>(Rva::AtelGetEventChannel)(actorKind, eventKind);
		if (channel < 0 || channel > 255)
			return kAtelChannelNone;
		return channel;
	}

	int EventScriptEntry(int actorId, int eventKind)
	{
		if (!KindInRange(eventKind))
			return kAtelScriptEntryNone;
		if (!IdInRange(actorId))
			return kAtelScriptEntryNone;

		BYTE* actor = ActorPtrUnchecked(actorId);
		if (!actor)
			return kAtelScriptEntryNone;

		// Gate 2's first half: the actor's own override table at actor+68 beats all
		// four per-kind default tables when it is non-null. Validate it before
		// handing it to the engine, because the engine indexes it blind.
		const WORD* overrideTable = NULL;
		DWORD raw = 0;
		if (Field32(actor, AtelActor::EventScriptOverride, &raw) && LooksLikePointer(raw))
		{
			const WORD* candidate = (const WORD*)(UINT_PTR)raw;
			if (Readable(candidate, sizeof(WORD) * kAtelEventKindCount))
				overrideTable = candidate;
		}

		int kind = ActorKindUnchecked(actorId);
		int entry = Resolve<GetEventScriptEntryFn>(Rva::AtelGetEventScriptEntry)(
		    kind, eventKind, overrideTable);
		return entry & 0xFFFF;
	}

	bool ActorCanFireEvent(int actorId, int eventKind)
	{
		if (!KindInRange(eventKind))
			return false;
		if (!IdInRange(actorId))
			return false;
		if (!AtelReady())
			return false;

		BYTE* actor = ActorPtrUnchecked(actorId);
		if (!actor)
			return false;

		int kind = ActorKindUnchecked(actorId);

		// Gate 1. The per-kind channel table, 255 meaning this actor kind has no
		// channel for that event. Kind 1 reads one table, kinds 2 and 3 a second,
		// kinds 5 and 6 a third, and every other kind including the out-of-range
		// kind 7 falls through to a hardcoded 255. Read through the engine's own
		// lookup so this cannot drift from it.
		if (EventChannel(kind, eventKind) == kAtelChannelNone)
			return false;

		// Gate 2. A script entry has to exist. The actor's own override table wins
		// when present, otherwise one of four per-kind default word tables is
		// indexed. 0xFFFF means none.
		if (EventScriptEntry(actorId, eventKind) == kAtelScriptEntryNone)
			return false;

		// Gate 3. The per-actor enable bit: (1 << eventKind) & actor[171]. This is
		// the one that varies per placed object, so it is the one that makes a
		// scenery prop with a script still refuse to respond.
		BYTE mask = 0;
		if (!Field8(actor, AtelActor::EventMask, &mask))
			return false;
		return (mask & (BYTE)(1u << (unsigned)eventKind)) != 0;
	}

	bool ActorEventChannelBusy(int actorId, int eventKind)
	{
		if (!KindInRange(eventKind))
			return false;
		if (!IdInRange(actorId))
			return false;

		int channel = EventChannel(ActorKindUnchecked(actorId), eventKind);
		if (channel == kAtelChannelNone)
			return false;

		BYTE* actor = ActorPtrUnchecked(actorId);
		if (!actor)
			return false;

		DWORD head = 0;
		if (!Field32(actor, AtelActor::ThreadList, &head))
			return false;

		// Exactly FFX_Atel_StartThreadIfChannelFree's walk: busy means some thread
		// on the list is both unfinished and on the channel this event would use.
		for (int i = 0; i < kThreadWalkLimit && LooksLikePointer(head); ++i)
		{
			const BYTE* thread = (const BYTE*)(UINT_PTR)head;
			if (!Readable(thread, 16))
				return false;
			BYTE state = Rd8(thread + AtelThread::State);
			BYTE chan = (BYTE)(Rd8(thread + AtelThread::ChannelAndFlag) & 0x0F);
			if (state != AtelThread::StateFinished && (int)chan == channel)
				return true;
			head = Rd32(thread + AtelThread::Next);
		}
		return false;
	}

	int InteractionBlockedKind()
	{
		if (!AtelReady())
			return 1;
		return Resolve<MesWinBlockingKindFn>(Rva::AtelMesWinBlockingKind)();
	}

	// ---------------------------------------------------------------------------
	// What is interactable near here?
	// ---------------------------------------------------------------------------

	namespace
	{

		// Insertion into a nearest-first list. Distances are recomputed on demand rather
		// than kept in a parallel buffer, which costs a few reads per compare and in
		// exchange puts no cap on maxIds and allocates nothing.
		void InsertNearest(int* ids, int* held, int maxIds, int actorId,
		    float distSq, const float* xyz)
		{
			int at = *held;
			while (at > 0 && ActorDistSq(ids[at - 1], xyz) > distSq)
			{
				if (at < maxIds)
					ids[at] = ids[at - 1];
				--at;
			}
			if (at >= maxIds)
				return;
			ids[at] = actorId;
			if (*held < maxIds)
				++(*held);
		}

		int FindActorsNearFiltered(const float* xyz, float radius, int eventKind,
		    int* outIds, int maxIds)
		{
			if (!xyz)
				return 0;
			if (radius <= 0.0f)
				return 0;
			if (!AtelReady())
				return 0;

			const float limitSq = radius * radius;
			const int count = CtxActorCount();
			const bool filter = (eventKind >= 0);
			const bool collect = (outIds != NULL && maxIds > 0);

			int held = 0;
			int matched = 0;

			for (int id = 0; id < count; ++id)
			{
				float distSq = ActorDistSq(id, xyz);
				if (distSq < 0.0f)
					continue; // kind 4 and anything unreadable
				if (distSq > limitSq)
					continue;
				if (filter && !ActorCanFireEvent(id, eventKind))
					continue;

				++matched;
				if (collect)
					InsertNearest(outIds, &held, maxIds, id, distSq, xyz);
			}

			return collect ? held : matched;
		}

	} // namespace

	int FindActorsNear(const float* xyz, float radius, int* outIds, int maxIds)
	{
		return FindActorsNearFiltered(xyz, radius, -1, outIds, maxIds);
	}

	int FindActorsNearForEvent(const float* xyz, float radius, int eventKind,
	    int* outIds, int maxIds)
	{
		if (!KindInRange(eventKind))
			return 0;
		return FindActorsNearFiltered(xyz, radius, eventKind, outIds, maxIds);
	}

	int NearestExaminableActorId()
	{
		BYTE* ctx = Ctx0();
		if (!ctx)
			return kAtelActorIdNone;
		return (int)Rd16(ctx + AtelCtx::NearestActorId);
	}

	// ---------------------------------------------------------------------------
	// The actor to CHR link
	// ---------------------------------------------------------------------------

	void* ChrForActor(int actorId)
	{
		if (!IdInRange(actorId))
			return NULL;
		BYTE* actor = ActorPtrUnchecked(actorId);
		if (!actor)
			return NULL;

		// Only a sub-kind 1 actor drives a CHR. Every other actor's +156 is either
		// zero or something else entirely, so the test is not belt and braces, it is
		// the engine's own precondition in FFX_Atel_FindActorIdByChr.
		BYTE subKind = 0;
		if (!Field8(actor, AtelActor::SubKind, &subKind))
			return NULL;
		if (subKind != 1)
			return NULL;

		DWORD raw = 0;
		if (!Field32(actor, AtelActor::Chr, &raw))
			return NULL;
		if (!LooksLikePointer(raw))
			return NULL;
		return (void*)(UINT_PTR)raw;
	}

	int ChrSlotForActor(int actorId)
	{
		void* chr = ChrForActor(actorId);
		if (!chr)
			return -1;

		BYTE* base = ChrPoolBase();
		int count = ChrPoolCount();
		if (!base || count <= 0)
			return -1;

		ptrdiff_t delta = (BYTE*)chr - base;
		if (delta < 0)
			return -1;
		if ((delta % (ptrdiff_t)Chr::Stride) != 0)
			return -1;
		int slot = (int)(delta / (ptrdiff_t)Chr::Stride);
		return (slot < count) ? slot : -1;
	}

	int ActorIdForChrSlot(int slot)
	{
		if (slot < 0)
			return -1;
		BYTE* base = ChrPoolBase();
		int count = ChrPoolCount();
		if (!base || slot >= count)
			return -1;
		if (!AtelReady())
			return -1;

		BYTE* chr = base + Chr::Stride * (DWORD)slot;
		if (!Readable(chr, 1))
			return -1;

		// FFX_Atel_FindActorIdByChr reads the AMBIENT context, so it gets the
		// bracket. It is a linear scan over every actor looking for sub-kind 1 and a
		// matching CHR pointer.
		int saved = SelectCtx0();
		int id = Resolve<FindActorIdByChrFn>(Rva::AtelFindActorIdByChr)((int)(UINT_PTR)chr);
		RestoreCtx(saved);

		return IdInRange(id) ? id : -1;
	}

	int ActorIdForPartyCharacter(int charIndex)
	{
		if (charIndex < 0 || charIndex > 7)
			return -1;
		if (!AtelReady())
			return -1;

		// Same ambient-context problem, and worse here: FFX_Atel_GetActor is INLINED
		// into this function, so it does not even get the engine's own getter's
		// protection.
		int saved = SelectCtx0();
		int id = Resolve<FindActorByPartyCharFn>(Rva::AtelFindActorByPartyChar)(charIndex);
		RestoreCtx(saved);

		return IdInRange(id) ? id : -1;
	}

	// ---------------------------------------------------------------------------
	// The player position cache
	// ---------------------------------------------------------------------------

	bool SavePlayerPosCache(AtelPlayerPosCache* out)
	{
		if (!out)
			return false;
		memset(out, 0, sizeof(*out));

		BYTE* ctx = Ctx0();
		if (!ctx)
			return false;

		for (int i = 0; i < 3; ++i)
		{
			out->current[i] = RdF(ctx + AtelCtx::PlayerPos + 4u * (DWORD)i);
			out->previous[i] = RdF(ctx + AtelCtx::PlayerPrevPos + 4u * (DWORD)i);
		}
		out->valid = true;
		return true;
	}

	bool RestorePlayerPosCache(const AtelPlayerPosCache* in)
	{
		if (!in || !in->valid)
			return false;
		BYTE* ctx = Ctx0();
		if (!ctx)
			return false;

		for (int i = 0; i < 3; ++i)
		{
			WrF(ctx + AtelCtx::PlayerPos + 4u * (DWORD)i, in->current[i]);
			WrF(ctx + AtelCtx::PlayerPrevPos + 4u * (DWORD)i, in->previous[i]);
		}
		return true;
	}

	bool SetPlayerPosCache(const float* current, const float* previous)
	{
		if (!current)
			return false;
		BYTE* ctx = Ctx0();
		if (!ctx)
			return false;

		const float* prev = previous ? previous : current;
		for (int i = 0; i < 3; ++i)
		{
			WrF(ctx + AtelCtx::PlayerPos + 4u * (DWORD)i, current[i]);
			WrF(ctx + AtelCtx::PlayerPrevPos + 4u * (DWORD)i, prev[i]);
		}
		return true;
	}

	bool SetPlayerPosCacheFromActor(int actorId)
	{
		float now[3];
		float before[3];
		if (!ActorPosition(actorId, now))
			return false;
		// Keeping the real previous position is the whole point. The engine's own
		// actor-swap path writes current into both halves, which is why a swept
		// crossing is lost on its swap frame.
		if (!ActorPreviousPosition(actorId, before))
			return SetPlayerPosCache(now, NULL);
		return SetPlayerPosCache(now, before);
	}

	// ---------------------------------------------------------------------------
	// Running the trigger set as a second character
	// ---------------------------------------------------------------------------

	namespace
	{

		// The bound player's actor, which is the pointer the steppers get handed and the
		// only place the pass facing can be written. NULL when nothing is bound, which
		// is the same condition FFX_Atel_StepActor refuses on.
		BYTE* BoundPlayerActorPtr()
		{
			BYTE* ctx = Ctx0();
			if (!ctx)
				return NULL;
			const WORD id = Rd16(ctx + AtelCtx::BoundPlayerId);
			if (id == kAtelActorIdNone)
				return NULL;
			if (!IdInRange((int)id))
				return NULL;
			return ActorPtrUnchecked((int)id);
		}

		// The bound player's facing float, which lives in that actor's own position
		// sub-struct. NULL when there is no bound player or it has no position.
		BYTE* BoundPlayerFacingPtr()
		{
			BYTE* ctx = Ctx0();
			if (!ctx)
				return NULL;
			const WORD id = Rd16(ctx + AtelCtx::BoundPlayerId);
			if (id == kAtelActorIdNone)
				return NULL;
			BYTE* pos = ActorPosPtr((int)id);
			if (!pos)
				return NULL;
			if (!Readable(pos + AtelPos::RotationY, 4))
				return NULL;
			return pos + AtelPos::RotationY;
		}

	} // namespace

	bool SaveAtelPass(AtelPassState* out)
	{
		if (!out)
			return false;
		memset(out, 0, sizeof(*out));

		BYTE* ctx = Ctx0();
		if (!ctx)
			return false;

		for (int i = 0; i < 3; ++i)
		{
			out->current[i] = RdF(ctx + AtelCtx::PlayerPos + 4u * (DWORD)i);
			out->previous[i] = RdF(ctx + AtelCtx::PlayerPrevPos + 4u * (DWORD)i);
		}
		out->playerRadius = RdF(ctx + AtelCtx::PlayerRadius);
		out->examineArmed = Rd8(ctx + AtelCtx::ExamineArmed);
		out->examineEventKind = Rd8(ctx + AtelCtx::ExamineEventKind);
		out->nearestActorId = Rd16(ctx + AtelCtx::NearestActorId);
		out->nearestDistSq = RdF(ctx + AtelCtx::NearestDistSq);

		// No bound player is not a failure here. It means the facing cannot be
		// swapped, and the pass itself will refuse for the same reason the engine's
		// own trigger dispatch refuses.
		const BYTE* facing = BoundPlayerFacingPtr();
		out->facing = facing ? RdF(facing) : 0.0f;

		out->valid = true;
		return true;
	}

	bool RestoreAtelPass(const AtelPassState* in)
	{
		if (!in || !in->valid)
			return false;
		BYTE* ctx = Ctx0();
		if (!ctx)
			return false;

		for (int i = 0; i < 3; ++i)
		{
			WrF(ctx + AtelCtx::PlayerPos + 4u * (DWORD)i, in->current[i]);
			WrF(ctx + AtelCtx::PlayerPrevPos + 4u * (DWORD)i, in->previous[i]);
		}
		WrF(ctx + AtelCtx::PlayerRadius, in->playerRadius);
		Wr8(ctx + AtelCtx::ExamineArmed, in->examineArmed);
		Wr8(ctx + AtelCtx::ExamineEventKind, in->examineEventKind);
		Wr16(ctx + AtelCtx::NearestActorId, in->nearestActorId);
		WrF(ctx + AtelCtx::NearestDistSq, in->nearestDistSq);

		BYTE* facing = BoundPlayerFacingPtr();
		if (facing)
			WrF(facing, in->facing);
		return true;
	}

	bool SetAtelPassPlayer(const float* current, const float* previous, float facing)
	{
		if (!current)
			return false;
		BYTE* ctx = Ctx0();
		if (!ctx)
			return false;

		const float* prev = previous ? previous : current;
		for (int i = 0; i < 3; ++i)
		{
			WrF(ctx + AtelCtx::PlayerPos + 4u * (DWORD)i, current[i]);
			WrF(ctx + AtelCtx::PlayerPrevPos + 4u * (DWORD)i, prev[i]);
		}

		// The facing goes onto the bound player's actor because that is the pointer
		// the steppers are handed. It is the one engine field outside the context
		// that this pass writes, and RestoreAtelPass puts it back.
		BYTE* f = BoundPlayerFacingPtr();
		if (!f)
			return false;
		WrF(f, facing);
		return true;
	}

	bool ArmAtelPassExamine(int eventKind)
	{
		if (eventKind != kAtelEventExamine && eventKind != kAtelEventExamineAlt)
		{
			return false;
		}
		BYTE* ctx = Ctx0();
		if (!ctx)
			return false;

		// Exactly what FFX_Atel_StepFrame does when it arms a scan, in the same
		// order: the kind, the armed flag, then clear the winner. Clearing the
		// winner is what makes this pass independent of the one before it.
		Wr8(ctx + AtelCtx::ExamineEventKind, (BYTE)eventKind);
		Wr8(ctx + AtelCtx::ExamineArmed, 1);
		Wr16(ctx + AtelCtx::NearestActorId, kAtelActorIdNone);
		return true;
	}

	bool ClearAtelPassExamine()
	{
		BYTE* ctx = Ctx0();
		if (!ctx)
			return false;
		Wr8(ctx + AtelCtx::ExamineArmed, 0);
		Wr16(ctx + AtelCtx::NearestActorId, kAtelActorIdNone);
		return true;
	}

	int AtelPassExamineWinner()
	{
		BYTE* ctx = Ctx0();
		if (!ctx)
			return kAtelActorIdNone;
		return (int)Rd16(ctx + AtelCtx::NearestActorId);
	}

	DWORD ActorTriggerState(int actorId)
	{
		if (!IdInRange(actorId))
			return 0xFFFFFFFFu;
		BYTE* actor = ActorPtrUnchecked(actorId);
		if (!actor)
			return 0xFFFFFFFFu;

		WORD step = 0;
		WORD frame = 0;
		if (!Field16(actor, AtelActor::StepFlags, &step))
			return 0xFFFFFFFFu;
		if (!Field16(actor, AtelActor::FrameFlags, &frame))
			return 0xFFFFFFFFu;

		return (DWORD)(step & kAtelStepFlagsTriggerMask) | ((DWORD)(frame & kAtelFrameFlagsTriggerMask) << 16);
	}

	bool SetActorTriggerState(int actorId, DWORD state)
	{
		if (state == 0xFFFFFFFFu)
			return false;
		if (!IdInRange(actorId))
			return false;
		BYTE* actor = ActorPtrUnchecked(actorId);
		if (!actor)
			return false;
		if (!Readable(actor + AtelActor::StepFlags, 2))
			return false;
		if (!Readable(actor + AtelActor::FrameFlags, 2))
			return false;

		// Read modify write on the masked bits only. Every other bit in these two
		// words belongs to the engine, including the step gate at 0x80 and the
		// script bookkeeping that FFX_Atel_StepFrame's second loop rewrites. A
		// wholesale write here would stop actors stepping.
		const WORD step = Rd16(actor + AtelActor::StepFlags);
		const WORD frame = Rd16(actor + AtelActor::FrameFlags);

		Wr16(actor + AtelActor::StepFlags,
		    (WORD)((step & ~kAtelStepFlagsTriggerMask) | (WORD)(state & kAtelStepFlagsTriggerMask)));
		Wr16(actor + AtelActor::FrameFlags,
		    (WORD)((frame & ~kAtelFrameFlagsTriggerMask) | (WORD)((state >> 16) & kAtelFrameFlagsTriggerMask)));
		return true;
	}

	int ActorTriggerStepKind(int actorId)
	{
		if (!IdInRange(actorId))
			return kAtelTriggerStepNone;
		switch (ActorKindUnchecked(actorId))
		{
		case kAtelActorCharacter:
			return kAtelTriggerStepProximity;
		case kAtelActorLineTrigger:
			return kAtelTriggerStepLine;
		case kAtelActorBoxTrigger:
			return kAtelTriggerStepBox;
		default:
			return kAtelTriggerStepNone;
		}
	}

	bool StepActorTriggers(int actorId, int dtMs)
	{
		const int which = ActorTriggerStepKind(actorId);
		if (which == kAtelTriggerStepNone)
			return false;

		BYTE* actor = ActorPtrUnchecked(actorId);
		if (!actor)
			return false;

		// FFX_Atel_StepActor's own gate, and it is the signed BYTE at +52 rather
		// than the word, so bit 0x80 and nothing above it.
		BYTE gate = 0;
		if (!Field8(actor, AtelActor::StepFlags, &gate))
			return false;
		if ((gate & 0x80) == 0)
			return false;

		BYTE* playerActor = BoundPlayerActorPtr();
		if (!playerActor)
			return false;

		// Context 0 selected for the duration, because all three steppers read the
		// position cache off whatever context the pointer slot currently names.
		const int saved = SelectCtx0();
		switch (which)
		{
		case kAtelTriggerStepProximity:
			Resolve<TestPlayerProximityFn>(Rva::AtelTestPlayerProximity)(
			    actor, dtMs, playerActor);
			break;
		case kAtelTriggerStepLine:
			Resolve<StepLineTriggerFn>(Rva::AtelStepLineTrigger)(
			    actor, dtMs, playerActor);
			break;
		case kAtelTriggerStepBox:
			Resolve<StepBoxTriggerFn>(Rva::AtelStepBoxTrigger)(
			    actor, dtMs, playerActor);
			break;
		default:
			break;
		}
		RestoreCtx(saved);
		return true;
	}

	int AtelGeneration()
	{
		BYTE* ctx = Ctx0();
		if (!ctx)
			return -1;
		return (int)Rd16(ctx + AtelCtx::Generation);
	}

	bool ExamineAllowed()
	{
		const BYTE* flag = (const BYTE*)ModuleAddress(Rva::AtelExamineAllowed);
		if (!Readable(flag, 4))
			return false;
		return Rd32(flag) != 0;
	}

	bool GrantTalkBonusOnce(int actorId, int eventKind)
	{
		if (!IdInRange(actorId))
			return false;
		if (!KindInRange(eventKind))
			return false;

		// Context 0 selected, same as every other bound call here, because the
		// engine's own call site runs with the field context selected.
		const int saved = SelectCtx0();
		Resolve<GrantTalkBonusOnceFn>(Rva::AtelGrantTalkBonusOnce)(actorId, eventKind);
		RestoreCtx(saved);
		return true;
	}

	// ---------------------------------------------------------------------------
	// Diagnostics
	// ---------------------------------------------------------------------------

	void LogAtelWorld()
	{
		BYTE** slot = (BYTE**)ModuleAddress(Rva::AtelContextPtr);
		BYTE* selected = CtxSelected();
		BYTE* ctx = Ctx0();

		Log("atel: context slot at 0x%08X, selected 0x%08X, context 0 at 0x%08X",
		    (unsigned)(UINT_PTR)slot, (unsigned)(UINT_PTR)selected, (unsigned)(UINT_PTR)ctx);

		if (!ctx)
		{
			Log("atel: context 0 is not readable, nothing else here will work");
			return;
		}
		if (selected && selected != ctx)
		{
			// Not an error. Some engine paths genuinely run on another context, and
			// everything in this file reads context 0 on purpose.
			Log("atel: note, a context other than 0 is selected right now");
		}

		int count = CtxActorCount();
		int bound = BoundPlayerActorId();
		Log("atel: ready=%d actors=%d capacity=%u pool=0x%08X generation=%u",
		    AtelReady() ? 1 : 0, count,
		    (unsigned)Rd16(ctx + AtelCtx::ActorCapacity),
		    (unsigned)Rd32(ctx + AtelCtx::ActorPool),
		    (unsigned)Rd16(ctx + AtelCtx::Generation));

		Log("atel: groups 1464=%u 1368=%u 744=%u, flags=0x%02X",
		    (unsigned)Rd16(ctx + AtelCtx::Count1464),
		    (unsigned)Rd16(ctx + AtelCtx::Count1368),
		    (unsigned)Rd16(ctx + AtelCtx::Count744),
		    (unsigned)Rd8(ctx + AtelCtx::Flags));

		if (bound == kAtelActorIdNone)
		{
			Log("atel: no player actor bound, the field trigger pass does not run");
		}
		else
		{
			float p[3] = { 0.0f, 0.0f, 0.0f };
			int charIndex = ActorPartyCharacter(bound);
			int chrSlot = ChrSlotForActor(bound);
			ActorPosition(bound, p);
			Log("atel: bound player actor %d, kind %d, party char %d, chr slot %d, at %.2f %.2f %.2f",
			    bound, ActorKind(bound), charIndex, chrSlot, p[0], p[1], p[2]);
		}

		Log("atel: examine armed=%u kind=%u winner=%d distSq=%.2f playerRadius=%.2f blocked=%d",
		    (unsigned)Rd8(ctx + AtelCtx::ExamineArmed),
		    (unsigned)Rd8(ctx + AtelCtx::ExamineEventKind),
		    NearestExaminableActorId(),
		    RdF(ctx + AtelCtx::NearestDistSq),
		    AtelPlayerRadius(),
		    InteractionBlockedKind());

		Log("atel: player pos cache now %.2f %.2f %.2f, previous %.2f %.2f %.2f",
		    RdF(ctx + AtelCtx::PlayerPos + 0),
		    RdF(ctx + AtelCtx::PlayerPos + 4),
		    RdF(ctx + AtelCtx::PlayerPos + 8),
		    RdF(ctx + AtelCtx::PlayerPrevPos + 0),
		    RdF(ctx + AtelCtx::PlayerPrevPos + 4),
		    RdF(ctx + AtelCtx::PlayerPrevPos + 8));

		const WORD* pressed = (const WORD*)ModuleAddress(Rva::AtelPadPressed);
		const WORD* port1 = (const WORD*)ModuleAddress(Rva::AtelPadPort1Buttons);
		const BYTE* allowed = (const BYTE*)ModuleAddress(Rva::AtelExamineAllowed);
		if (Readable(pressed, 2) && Readable(port1, 2) && Readable(allowed, 4))
		{
			// Port 1 is sampled by the engine and then discarded. Logged so nobody
			// spends an afternoon rediscovering that.
			Log("atel: pad pressed(port0)=0x%04X port1 held(unused)=0x%04X examineAllowed=%u",
			    (unsigned)*(volatile const WORD*)pressed,
			    (unsigned)*(volatile const WORD*)port1,
			    (unsigned)Rd32(allowed));
		}
	}

	void LogAtelActors(int maxActors)
	{
		if (!AtelReady())
		{
			Log("atel: no world loaded, no actors to list");
			return;
		}

		int count = CtxActorCount();
		int limit = (maxActors > 0 && maxActors < count) ? maxActors : count;
		Log("atel: listing %d of %d actors", limit, count);

		for (int id = 0; id < limit; ++id)
		{
			int kind = ActorKind(id);
			float p[3] = { 0.0f, 0.0f, 0.0f };
			bool hasPos = ActorPosition(id, p);

			// Which event kinds would actually respond. This is the line that tells
			// you whether a chest you are standing next to is wired up at all.
			char events[kAtelEventKindCount + 1];
			int at = 0;
			for (int k = 0; k < kAtelEventKindCount; ++k)
			{
				if (ActorCanFireEvent(id, k))
					events[at++] = (char)('0' + k);
			}
			events[at] = 0;

			Log("atel: actor %3d kind %d sub %d char %d chrSlot %d mask 0x%02X target %d "
			    "events [%s] pos %.2f %.2f %.2f%s",
			    id, kind, ActorSubKindUnchecked(id),
			    ActorPartyCharacter(id), ChrSlotForActor(id),
			    (unsigned)ActorEventMask(id), ActorTargetId(id),
			    events, p[0], p[1], p[2], hasPos ? "" : " (no position)");
		}
	}

	void LogAtelEventTables()
	{
		struct TableRef
		{
			DWORD rva;
			const char* name;
			bool isWord;
		};

		static const TableRef kTables[] = {
			{ Rva::AtelEventChannelKind1, "channel kind1    ", false },
			{ Rva::AtelEventChannelKind2Kind3, "channel kind2/3  ", false },
			{ Rva::AtelEventChannelKind5Kind6, "channel kind5/6  ", false },
			{ Rva::AtelEventScriptDefault, "script default   ", true },
			{ Rva::AtelEventScriptKind2Kind3, "script kind2/3   ", true },
			{ Rva::AtelEventScriptKind5, "script kind5     ", true },
			{ Rva::AtelEventScriptKind6, "script kind6     ", true }
		};

		// Column order is the event kind, 0 to 7. 255 and 0xFFFF both mean "none".
		Log("atel: event kinds 0 examine, 1 examineAlt, 2 collide, 3 proximity,"
		    " 4 touch, 5 enter, 6 leave, 7 unreachable");

		for (int t = 0; t < (int)(sizeof(kTables) / sizeof(kTables[0])); ++t)
		{
			const BYTE* table = (const BYTE*)ModuleAddress(kTables[t].rva);
			SIZE_T bytes = (SIZE_T)kAtelEventKindCount * (kTables[t].isWord ? 2u : 1u);
			if (!Readable(table, bytes))
			{
				Log("atel: %s NOT READABLE at RVA 0x%08X", kTables[t].name, (unsigned)kTables[t].rva);
				continue;
			}
			if (kTables[t].isWord)
			{
				Log("atel: %s %04X %04X %04X %04X %04X %04X %04X %04X", kTables[t].name,
				    (unsigned)Rd16(table + 0), (unsigned)Rd16(table + 2),
				    (unsigned)Rd16(table + 4), (unsigned)Rd16(table + 6),
				    (unsigned)Rd16(table + 8), (unsigned)Rd16(table + 10),
				    (unsigned)Rd16(table + 12), (unsigned)Rd16(table + 14));
			}
			else
			{
				Log("atel: %s   %02X   %02X   %02X   %02X   %02X   %02X   %02X   %02X",
				    kTables[t].name,
				    (unsigned)Rd8(table + 0), (unsigned)Rd8(table + 1),
				    (unsigned)Rd8(table + 2), (unsigned)Rd8(table + 3),
				    (unsigned)Rd8(table + 4), (unsigned)Rd8(table + 5),
				    (unsigned)Rd8(table + 6), (unsigned)Rd8(table + 7));
			}
		}
	}

} // namespace ffx
