#include "ffx/GameState.h"

#include "workshop/HostModule.h"
#include "workshop/Log.h"
#include "ffx/addresses/GameState.h"

#include <string.h>

// Implementation notes worth reading before changing anything here.
//
// This file resolves its own function pointers rather than going through
// ffx::Game, because ffx/Api.h is a shared file and the game-state work is not
// allowed to edit it. The cost is one Resolve helper, and the benefit is that
// GameState works whether or not BindApi has run.
//
// Every read of the save block goes through SaveBlockBase, which does the
// Readable check once. The block is a static buffer in .data so the check
// almost never fails, but "the buffer exists" and "there is a game loaded" are
// different questions and GameLoaded answers the second one.
//
// Fields are read volatile, because the game thread writes them behind us.

namespace ffx
{

	using workshop::Log;
	using workshop::ModuleAddress;
	using workshop::Readable;

	namespace
	{

		// ---------------------------------------------------------------------------
		// Game function types. All __cdecl, all verified in the IDB.
		// ---------------------------------------------------------------------------
		typedef int(__cdecl* AddItemFn)(int itemId, int delta);
		typedef int(__cdecl* GetItemCountFn)(int itemId);
		typedef int(__cdecl* SetGilFn)(int amount);
		typedef int(__cdecl* SpendGilFn)(int cost);
		typedef int(__cdecl* TestKeyItemFn)(short id);
		typedef int(__cdecl* SetKeyItemFn)(short id);
		typedef char*(__cdecl* GetEquipEntryFn)(int slotId, int* outStringBase);
		typedef int(__cdecl* SetCharEquipFn)(unsigned char charIndex, int armour, short slotId);
		typedef int(__cdecl* AddEquipEntryFn)(const void* src);
		typedef int(__cdecl* CountEquipFn)(int* outFree);
		typedef int(__cdecl* GetCaptureCountFn)(short monsterId);
		typedef int(__cdecl* AddCaptureCountFn)(short monsterId, int delta);
		typedef char*(__cdecl* GetCharBaseStatsFn)(unsigned int charIndex, int* out);
		typedef int(__cdecl* GetCharCurrentStatsFn)(int charIndex, void* out);

		template <class T>
		T Resolve(DWORD rva)
		{
			return (T)ModuleAddress(rva);
		}

		// The save block, checked once.
		BYTE* Block()
		{
			BYTE* base = (BYTE*)ModuleAddress(Rva::SaveData);
			if (!Readable(base, SaveBlock::Size))
				return NULL;
			return base;
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

		inline void Wr8(BYTE* p, BYTE v)
		{
			*(volatile BYTE*)p = v;
		}
		inline void Wr32(BYTE* p, DWORD v)
		{
			*(volatile DWORD*)p = v;
		}

		const char* const kCharacterNames[kCharCount] = {
			"Tidus", "Yuna", "Auron", "Kimahri", "Wakka", "Lulu", "Rikku", "Seymour",
			"Valefor", "Ifrit", "Ixion", "Shiva", "Bahamut",
			"Anima", "Yojimbo", "Cindy", "Sandy", "Mindy"
		};

		// Rikku's alternate chr id, which the game substitutes for 7.
		const DWORD kRikkuAltChrId = 41;

		// The +100 offset between a chr id and its high detail variant.
		const DWORD kHighDetailOffset = 100;

		// ---------------------------------------------------------------------------
		// Hash bucket table. Offsets into the save block, half open ranges.
		//
		// The gap between kBucketCheckpoint and kBucketProgress is the playtime dword at
		// +0xBC, which is deliberately not covered because it advances every frame.
		// ---------------------------------------------------------------------------
		struct BucketRange
		{
			DWORD begin;
			DWORD end;
			const char* name;
			bool inCombined;
		};

		const BucketRange kBuckets[kBucketCount] = {
			{ 0x0000, 0x00B8, "Header", true },
			{ 0x00B8, 0x00BC, "MapId", false },   // diverges for a frame on a warp
			{ 0x00C0, 0x3D0C, "Progress", true }, // 0xBC..0xC0 is playtime, skipped
			{ 0x3D0C, 0x3ECC, "Config", true },
			{ 0x3ECC, 0x41CC, "Inventory", true },
			{ 0x41CC, 0x420C, "ItemMasks", false }, // menu state, not game state
			{ 0x420C, 0x448C, "Monsters", true },
			{ 0x448C, 0x449C, "KeyItems", true },
			{ 0x449C, 0x55CC, "Equipment", true },
			{ 0x55CC, 0x6034, "Characters", true },
			{ 0x6034, 0x634C, "Abilities", true },
			{ 0x634C, 0x64B4, "Names", true },
			{ 0x64B4, 0x68C0, "Tail", true }
		};

		const DWORD kFnvOffset = 2166136261u;
		const DWORD kFnvPrime = 16777619u;

		DWORD Fnv1a(const BYTE* p, DWORD bytes, DWORD seed)
		{
			DWORD h = seed;
			for (DWORD i = 0; i < bytes; ++i)
			{
				h ^= (DWORD)(*(volatile const BYTE*)(p + i));
				h *= kFnvPrime;
			}
			return h;
		}

	} // namespace

	// ---------------------------------------------------------------------------
	// Is there a game to read?
	// ---------------------------------------------------------------------------

	bool SaveBlockReadable()
	{
		return Block() != NULL;
	}

	BYTE* SaveBlockBase()
	{
		return Block();
	}

	bool GameLoaded()
	{
		BYTE* base = Block();
		if (!base)
			return false;

		// The block is static .data, so it is readable and all zero before a game
		// exists. Checking the active party is NOT enough on its own, because a
		// zeroed party slot reads as character 0, which is Tidus.
		//
		// So check two things a zeroed block cannot fake and a playable state can
		// never fail. Record 0 has a non-zero base max HP the moment ply_save has
		// been read, and somebody is always in the party. Deliberately not
		// "Tidus is in the party", because the game does split the party.
		const BYTE* rec0 = base + SaveBlock::CharRecords;
		if (Rd32(rec0 + CharRecord::BaseMaxHp) == 0)
			return false;

		for (int i = 0; i < kCharCount; ++i)
		{
			const BYTE* rec = base + SaveBlock::CharRecords + CharRecord::Size * (DWORD)i;
			if ((Rd8(rec + CharRecord::Flags) & CharRecord::FlagInParty) != 0)
				return true;
		}
		return false;
	}

	// ---------------------------------------------------------------------------
	// The party
	// ---------------------------------------------------------------------------

	BYTE ActivePartyMember(int slot)
	{
		if (slot < 0 || slot >= kActivePartySize)
			return (BYTE)kCharNone;
		BYTE* base = Block();
		if (!base)
			return (BYTE)kCharNone;
		return Rd8(base + SaveBlock::FieldParty + (DWORD)slot);
	}

	int ActiveSlotOfCharacter(BYTE charIndex)
	{
		if (charIndex == kCharNone)
			return -1;
		for (int i = 0; i < kActivePartySize; ++i)
		{
			if (ActivePartyMember(i) == charIndex)
				return i;
		}
		return -1;
	}

	bool SetActiveParty(BYTE slot0, BYTE slot1, BYTE slot2)
	{
		BYTE* base = Block();
		if (!base)
			return false;
		BYTE* party = base + SaveBlock::FieldParty;
		Wr8(party + 0, slot0);
		Wr8(party + 1, slot1);
		Wr8(party + 2, slot2);
		return true;
	}

	BYTE BattlePartyMember(int slot)
	{
		if (slot < 0 || slot >= kBattlePartySize)
			return (BYTE)kCharNone;
		BYTE* roster = (BYTE*)ModuleAddress(Rva::BattlePartyOrder);
		if (!Readable(roster, kBattlePartySize))
			return (BYTE)kCharNone;
		return Rd8(roster + (DWORD)slot);
	}

	bool CharacterInParty(BYTE charIndex)
	{
		const CharRecordData* rec = CharacterRecord(charIndex);
		if (!rec)
			return false;
		return (RecordFlags(rec) & CharRecord::FlagInParty) != 0;
	}

	int PartyMemberCount()
	{
		BYTE* base = Block();
		if (!base)
			return 0;
		int n = 0;
		for (int i = 0; i < kCharCount; ++i)
		{
			const BYTE* rec = base + SaveBlock::CharRecords + CharRecord::Size * (DWORD)i;
			if ((Rd8(rec + CharRecord::Flags) & CharRecord::FlagInParty) != 0)
				++n;
		}
		return n;
	}

	// ---------------------------------------------------------------------------
	// Which character is which
	// ---------------------------------------------------------------------------

	bool RikkuAltOutfit()
	{
		BYTE* base = Block();
		if (!base)
			return false;
		return Rd8(base + SaveBlock::RikkuAltOutfit) != 0;
	}

	DWORD ChrIdForCharacter(BYTE charIndex, bool wantAlternate)
	{
		if (charIndex >= kCharCount)
			return 0;
		const BYTE* table = (const BYTE*)ModuleAddress(Rva::CharIndexToChrId);
		if (!Readable(table, 4 * kCharCount))
			return 0;
		DWORD chrId = Rd32(table + 4 * (DWORD)charIndex);
		if (charIndex == kCharRikku && wantAlternate)
			return kRikkuAltChrId;
		return chrId;
	}

	BYTE CharacterForChrId(DWORD chrId)
	{
		if (chrId == kRikkuAltChrId)
			return (BYTE)kCharRikku;

		// High detail variants are the same ids plus 100. Only fold the eight
		// playable ones, because the aeons live in the 0x3000 space and 0x3001+100
		// is not a variant of anything.
		DWORD probe = chrId;
		if (probe > kHighDetailOffset && probe <= kHighDetailOffset + (DWORD)kCharPlayableCount + 1u)
		{
			probe -= kHighDetailOffset;
		}

		const BYTE* table = (const BYTE*)ModuleAddress(Rva::CharIndexToChrId);
		if (!Readable(table, 4 * kCharCount))
			return (BYTE)kCharNone;
		for (int i = 0; i < kCharCount; ++i)
		{
			if (Rd32(table + 4 * (DWORD)i) == probe)
				return (BYTE)i;
		}
		return (BYTE)kCharNone;
	}

	const char* CharacterName(BYTE charIndex)
	{
		if (charIndex >= kCharCount)
			return "?";
		return kCharacterNames[charIndex];
	}

	bool CharacterDisplayName(BYTE charIndex, char* out, int outBytes)
	{
		if (!out || outBytes <= 0)
			return false;
		if (charIndex >= kCharCount)
			return false;
		BYTE* base = Block();
		if (!base)
			return false;

		const BYTE* entry = base + SaveBlock::CharNames + CharName::Size * (DWORD)charIndex;
		int room = outBytes - 1;
		if (room > (int)CharName::Language)
			room = (int)CharName::Language;

		int i = 0;
		for (; i < room; ++i)
		{
			char c = (char)Rd8(entry + (DWORD)i);
			if (c == 0)
				break;
			out[i] = c;
		}
		out[i] = 0;
		return true;
	}

	// ---------------------------------------------------------------------------
	// Per-character stats
	// ---------------------------------------------------------------------------

	CharRecordData* CharacterRecord(BYTE charIndex)
	{
		if (charIndex >= kCharCount)
			return NULL;
		BYTE* base = Block();
		if (!base)
			return NULL;
		return (CharRecordData*)(base + SaveBlock::CharRecords + CharRecord::Size * (DWORD)charIndex);
	}

	namespace
	{
		inline const BYTE* Rec(const CharRecordData* rec)
		{
			return (const BYTE*)rec;
		}
	}

	DWORD RecordCurrentHp(const CharRecordData* rec)
	{
		return rec ? Rd32(Rec(rec) + CharRecord::CurrentHp) : 0;
	}
	DWORD RecordMaxHp(const CharRecordData* rec)
	{
		return rec ? Rd32(Rec(rec) + CharRecord::MaxHp) : 0;
	}
	DWORD RecordCurrentMp(const CharRecordData* rec)
	{
		return rec ? Rd32(Rec(rec) + CharRecord::CurrentMp) : 0;
	}
	DWORD RecordMaxMp(const CharRecordData* rec)
	{
		return rec ? Rd32(Rec(rec) + CharRecord::MaxMp) : 0;
	}
	DWORD RecordBaseMaxHp(const CharRecordData* rec)
	{
		return rec ? Rd32(Rec(rec) + CharRecord::BaseMaxHp) : 0;
	}
	DWORD RecordBaseMaxMp(const CharRecordData* rec)
	{
		return rec ? Rd32(Rec(rec) + CharRecord::BaseMaxMp) : 0;
	}
	DWORD RecordTotalAp(const CharRecordData* rec)
	{
		return rec ? Rd32(Rec(rec) + CharRecord::TotalAp) : 0;
	}
	DWORD RecordApToNext(const CharRecordData* rec)
	{
		return rec ? Rd32(Rec(rec) + CharRecord::ApToNextSLv) : 0;
	}

	BYTE RecordStat(const CharRecordData* rec, int stat)
	{
		if (!rec || stat < 0 || stat >= kStatCount)
			return 0;
		return Rd8(Rec(rec) + CharRecord::Stats + (DWORD)stat);
	}

	BYTE RecordBaseStat(const CharRecordData* rec, int stat)
	{
		if (!rec || stat < 0 || stat >= kStatCount)
			return 0;
		return Rd8(Rec(rec) + CharRecord::BaseStats + (DWORD)stat);
	}

	BYTE RecordFlags(const CharRecordData* rec)
	{
		return rec ? Rd8(Rec(rec) + CharRecord::Flags) : 0;
	}
	BYTE RecordSphereLevels(const CharRecordData* rec)
	{
		return rec ? Rd8(Rec(rec) + CharRecord::SphereLevels) : 0;
	}
	BYTE RecordSphereSpent(const CharRecordData* rec)
	{
		return rec ? Rd8(Rec(rec) + CharRecord::SphereSpent) : 0;
	}
	BYTE RecordWeaponSlot(const CharRecordData* rec)
	{
		return rec ? Rd8(Rec(rec) + CharRecord::WeaponSlot) : (BYTE)kEquipSlotIdNone;
	}
	BYTE RecordArmourSlot(const CharRecordData* rec)
	{
		return rec ? Rd8(Rec(rec) + CharRecord::ArmourSlot) : (BYTE)kEquipSlotIdNone;
	}

	bool SetRecordCurrentHp(CharRecordData* rec, DWORD hp)
	{
		if (!rec)
			return false;
		Wr32((BYTE*)rec + CharRecord::CurrentHp, hp);
		return true;
	}

	bool SetRecordCurrentMp(CharRecordData* rec, DWORD mp)
	{
		if (!rec)
			return false;
		Wr32((BYTE*)rec + CharRecord::CurrentMp, mp);
		return true;
	}

	bool ReadBaseStats(BYTE charIndex, BaseStats* out)
	{
		if (!out || charIndex >= kCharCount)
			return false;
		if (!Block())
			return false;

		int raw[10];
		memset(raw, 0, sizeof(raw));
		GetCharBaseStatsFn fn = Resolve<GetCharBaseStatsFn>(Rva::SaveDataGetCharBaseStats);
		fn((unsigned int)charIndex, raw);

		for (int i = 0; i < kStatCount; ++i)
			out->stats[i] = raw[i];
		out->maxHp = raw[8];
		out->maxMp = raw[9];
		return true;
	}

	bool ReadCurrentStats(BYTE charIndex, CurrentStats* out)
	{
		if (!out || charIndex >= kCharCount)
			return false;
		if (!Block())
			return false;

		// The game's struct is 20 bytes: byte 0 sphere levels, +4 HP, +8 MP,
		// +12..19 the eight stats. Writing bytes 1..3 is the game's business, so
		// the buffer is the full 20 even though only 17 bytes carry meaning.
		BYTE raw[20];
		memset(raw, 0, sizeof(raw));
		GetCharCurrentStatsFn fn = Resolve<GetCharCurrentStatsFn>(Rva::SaveDataGetCharCurrentStats);
		fn((int)charIndex, raw);

		out->sphereLevels = raw[0];
		memcpy(&out->currentHp, raw + 4, sizeof(DWORD));
		memcpy(&out->currentMp, raw + 8, sizeof(DWORD));
		for (int i = 0; i < kStatCount; ++i)
			out->stats[i] = raw[12 + i];
		return true;
	}

	// ---------------------------------------------------------------------------
	// Inventory and gil
	// ---------------------------------------------------------------------------

	DWORD Gil()
	{
		BYTE* base = Block();
		if (!base)
			return 0;
		return Rd32(base + SaveBlock::Gil);
	}

	bool SetGil(DWORD amount)
	{
		if (!Block())
			return false;
		if (amount > (DWORD)kGilMax)
			amount = (DWORD)kGilMax;
		Resolve<SetGilFn>(Rva::SaveDataSetGil)((int)amount);
		return true;
	}

	bool SpendGil(int cost)
	{
		if (!Block())
			return false;
		return Resolve<SpendGilFn>(Rva::SaveDataSpendGil)(cost) == 0;
	}

	bool AddGil(int amount)
	{
		// The game has no add. A negative cost is how its own chest path adds gil,
		// and SpendGil routes the write through SetGil so the clamp still applies.
		return SpendGil(-amount);
	}

	WORD ItemIdAt(int slot)
	{
		if (slot < 0 || slot >= kItemSlots)
			return (WORD)kItemIdNone;
		BYTE* base = Block();
		if (!base)
			return (WORD)kItemIdNone;
		return Rd16(base + SaveBlock::ItemIds + 2 * (DWORD)slot);
	}

	BYTE ItemCountAt(int slot)
	{
		if (slot < 0 || slot >= kItemSlots)
			return 0;
		BYTE* base = Block();
		if (!base)
			return 0;
		return Rd8(base + SaveBlock::ItemCounts + (DWORD)slot);
	}

	int ItemCount(WORD itemId)
	{
		if (!Block())
			return 0;
		return Resolve<GetItemCountFn>(Rva::SaveDataGetItemCount)((int)itemId);
	}

	int CountItemInSlots(WORD itemId)
	{
		if (itemId == (WORD)kItemIdNone)
			return 0;
		BYTE* base = Block();
		if (!base)
			return 0;
		int total = 0;
		for (int i = 0; i < kItemSlots; ++i)
		{
			if (Rd16(base + SaveBlock::ItemIds + 2 * (DWORD)i) != itemId)
				continue;
			total += (int)Rd8(base + SaveBlock::ItemCounts + (DWORD)i);
		}
		return total;
	}

	int FreeItemSlots()
	{
		BYTE* base = Block();
		if (!base)
			return 0;
		int freeSlots = 0;
		for (int i = 0; i < kItemSlots; ++i)
		{
			WORD id = Rd16(base + SaveBlock::ItemIds + 2 * (DWORD)i);
			BYTE n = Rd8(base + SaveBlock::ItemCounts + (DWORD)i);
			// The game's own free test, from FFX_SaveData_AddItem: id == 0 or the
			// count is zero. Note that id 0xFF with count 0 is also free, which the
			// id == 0 half does not catch, hence both halves.
			if (id == 0 || n == 0)
				++freeSlots;
		}
		return freeSlots;
	}

	bool AddItem(WORD itemId, int delta)
	{
		if (!Block())
			return false;
		if (delta == 0)
			return false;
		if (((DWORD)itemId & 0xFFFFF000u) != kItemIdSpace)
			return false;
		return Resolve<AddItemFn>(Rva::SaveDataAddItem)((int)itemId, delta) == 0;
	}

	bool HasKeyItem(WORD keyItemId)
	{
		if (!Block())
			return false;
		return Resolve<TestKeyItemFn>(Rva::SaveDataTestKeyItemFlag)((short)keyItemId) != 0;
	}

	bool SetKeyItem(WORD keyItemId, bool held)
	{
		BYTE* base = Block();
		if (!base)
			return false;
		if (held)
		{
			Resolve<SetKeyItemFn>(Rva::SaveDataSetKeyItemFlag)((short)keyItemId);
			return true;
		}
		// The clear function is a separate entry point that is not in the address
		// list, so do it by hand against the same 128-bit array the game uses.
		DWORD index = (DWORD)keyItemId & 0xFFFu;
		if (index >= 0x80)
			return false;
		BYTE* word = base + SaveBlock::KeyItemFlags + 2 * (index / 16);
		WORD v = Rd16(word);
		v = (WORD)(v & ~(1u << (keyItemId & 0xF)));
		*(volatile WORD*)word = v;
		return true;
	}

	// ---------------------------------------------------------------------------
	// Monster Arena and the bestiary
	// ---------------------------------------------------------------------------

	int MonsterCaptureCount(WORD monsterId)
	{
		if (!Block())
			return 0;
		return Resolve<GetCaptureCountFn>(Rva::SaveDataGetCaptureCount)((short)monsterId);
	}

	int AddMonsterCaptures(WORD monsterId, int delta)
	{
		if (!Block())
			return 0;
		return Resolve<AddCaptureCountFn>(Rva::SaveDataAddCaptureCount)((short)monsterId, delta);
	}

	bool MonsterSeen(WORD monsterId)
	{
		BYTE* base = Block();
		if (!base)
			return false;
		DWORD index = (DWORD)monsterId & 0xFFFu;
		if (index >= 512)
			return false;
		WORD word = Rd16(base + SaveBlock::MonsterSeenMask + 2 * (index / 16));
		return (word & (1u << (index & 0xF))) != 0;
	}

	// ---------------------------------------------------------------------------
	// Equipment
	// ---------------------------------------------------------------------------

	EquipEntryData* EquipEntryFromSlotId(WORD slotId)
	{
		if (slotId == (WORD)kEquipSlotIdNone)
			return NULL;
		if (!Block())
			return NULL;

		// The game's decoder clamps an out of range main-array index to 0 rather
		// than failing, so a garbage slot id silently reads as equipment entry 0.
		// Reject that here, because a caller asking about a bad slot wants NULL,
		// not somebody else's sword. The two scratch arrays (high nibbles 7 and 11)
		// have their own bound of 8 and are left to the game.
		DWORD nibble = ((DWORD)slotId >> 12) & 0xFu;
		if (nibble != 7 && nibble != 11 && ((DWORD)slotId & 0xFFFu) >= (DWORD)kEquipSlots)
			return NULL;

		char* entry = Resolve<GetEquipEntryFn>(Rva::SaveDataGetEquipEntry)((int)slotId, NULL);
		if (!Readable(entry, kEquipEntrySize))
			return NULL;
		return (EquipEntryData*)entry;
	}

	EquipEntryData* EquipEntryAt(int index)
	{
		if (index < 0 || index >= kEquipSlots)
			return NULL;
		BYTE* base = Block();
		if (!base)
			return NULL;
		return (EquipEntryData*)(base + SaveBlock::Equipment + EquipEntry::Size * (DWORD)index);
	}

	namespace
	{
		inline const BYTE* Eq(const EquipEntryData* e)
		{
			return (const BYTE*)e;
		}
	}

	bool EquipEntryInUse(const EquipEntryData* e)
	{
		return e && Rd8(Eq(e) + EquipEntry::InUse) != 0;
	}
	BYTE EquipEntryKind(const EquipEntryData* e)
	{
		return e ? Rd8(Eq(e) + EquipEntry::Kind) : (BYTE)0xFF;
	}
	BYTE EquipEntryForChar(const EquipEntryData* e)
	{
		return e ? Rd8(Eq(e) + EquipEntry::ForChar) : (BYTE)kCharNone;
	}
	BYTE EquipEntryOwner(const EquipEntryData* e)
	{
		return e ? Rd8(Eq(e) + EquipEntry::OwnerChar) : (BYTE)kCharNone;
	}
	WORD EquipEntryNameId(const EquipEntryData* e)
	{
		return e ? Rd16(Eq(e) + EquipEntry::NameId) : (WORD)0;
	}
	WORD EquipEntryModelId(const EquipEntryData* e)
	{
		return e ? Rd16(Eq(e) + EquipEntry::ModelId) : (WORD)0;
	}
	BYTE EquipEntryFlags(const EquipEntryData* e)
	{
		return e ? Rd8(Eq(e) + EquipEntry::Flags) : (BYTE)0;
	}

	WORD EquipEntryAbility(const EquipEntryData* e, int index)
	{
		if (!e || index < 0 || index >= kEquipAbilitySlots)
			return 0;
		WORD id = Rd16(Eq(e) + EquipEntry::Abilities + 2 * (DWORD)index);
		// The game treats both of these as empty, so normalise to 0 and spare every
		// caller the double test.
		if (id == EquipEntry::AbilityNoneA || id == EquipEntry::AbilityNoneB)
			return 0;
		return id;
	}

	int EquipEntryAbilityCount(const EquipEntryData* e)
	{
		int n = 0;
		for (int i = 0; i < kEquipAbilitySlots; ++i)
		{
			if (EquipEntryAbility(e, i) != 0)
				++n;
		}
		return n;
	}

	WORD EquippedWeaponSlotId(BYTE charIndex)
	{
		return RecordWeaponSlot(CharacterRecord(charIndex));
	}

	WORD EquippedArmourSlotId(BYTE charIndex)
	{
		return RecordArmourSlot(CharacterRecord(charIndex));
	}

	EquipEntryData* EquippedWeapon(BYTE charIndex)
	{
		return EquipEntryFromSlotId(EquippedWeaponSlotId(charIndex));
	}

	EquipEntryData* EquippedArmour(BYTE charIndex)
	{
		return EquipEntryFromSlotId(EquippedArmourSlotId(charIndex));
	}

	bool EquipItemOn(BYTE charIndex, bool armour, WORD slotId)
	{
		if (charIndex >= kCharCount)
			return false;
		if (!Block())
			return false;
		Resolve<SetCharEquipFn>(Rva::SaveDataSetCharEquip)(
		    (unsigned char)charIndex, armour ? 1 : 0, (short)slotId);
		return true;
	}

	bool AddEquipment(const void* entry22Bytes, WORD* outSlotId)
	{
		if (!entry22Bytes)
			return false;
		if (!Readable(entry22Bytes, kEquipEntrySize))
			return false;
		if (!Block())
			return false;
		int slotId = Resolve<AddEquipEntryFn>(Rva::SaveDataAddEquipEntry)(entry22Bytes);
		if (slotId == 0)
			return false; // array full
		if (outSlotId)
			*outSlotId = (WORD)slotId;
		return true;
	}

	bool CountEquipment(int* outUnlocked, int* outFree)
	{
		if (!Block())
			return false;
		int freeCount = 0;
		int unlocked = Resolve<CountEquipFn>(Rva::SaveDataCountEquipEntries)(&freeCount);
		if (outUnlocked)
			*outUnlocked = unlocked;
		if (outFree)
			*outFree = freeCount;
		return true;
	}

	// ---------------------------------------------------------------------------
	// The desync hash
	// ---------------------------------------------------------------------------

	const char* HashBucketName(int bucket)
	{
		if (bucket < 0 || bucket >= kBucketCount)
			return "?";
		return kBuckets[bucket].name;
	}

	bool HashBucketRange(int bucket, DWORD* outBegin, DWORD* outEnd)
	{
		if (bucket < 0 || bucket >= kBucketCount)
			return false;
		if (outBegin)
			*outBegin = kBuckets[bucket].begin;
		if (outEnd)
			*outEnd = kBuckets[bucket].end;
		return true;
	}

	int HashBucketForOffset(DWORD offset)
	{
		for (int i = 0; i < kBucketCount; ++i)
		{
			if (offset >= kBuckets[i].begin && offset < kBuckets[i].end)
				return i;
		}
		return -1; // the playtime dword at +0xBC, or past the end of the block
	}

	bool HashGameState(GameStateHash* out)
	{
		if (!out)
			return false;
		memset(out, 0, sizeof(*out));

		BYTE* base = Block();
		if (!base)
			return false;

		DWORD combined = kFnvOffset;
		for (int i = 0; i < kBucketCount; ++i)
		{
			const BucketRange& b = kBuckets[i];
			DWORD h = Fnv1a(base + b.begin, b.end - b.begin, kFnvOffset);
			out->bucket[i] = h;
			if (!b.inCombined)
				continue;
			// Fold the bucket hash into the combined one byte at a time, so the
			// combined value depends on which bucket a change was in rather than
			// just on the set of bytes.
			const BYTE* p = (const BYTE*)&h;
			for (int k = 0; k < 4; ++k)
			{
				combined ^= p[k];
				combined *= kFnvPrime;
			}
		}
		out->combined = combined;
		out->valid = true;
		return true;
	}

	// ---------------------------------------------------------------------------
	// Diagnostics
	// ---------------------------------------------------------------------------

	void LogGameState()
	{
		BYTE* base = Block();
		if (!base)
		{
			Log("gamestate: save block at RVA 0x%08X is not readable", (unsigned)Rva::SaveData);
			return;
		}
		Log("gamestate: block at 0x%08X, %u bytes, loaded=%d",
		    (unsigned)(UINT_PTR)base, (unsigned)SaveBlock::Size, GameLoaded() ? 1 : 0);

		for (int slot = 0; slot < kActivePartySize; ++slot)
		{
			BYTE c = ActivePartyMember(slot);
			if (c == kCharNone)
			{
				Log("gamestate: active slot %d empty", slot);
				continue;
			}
			char display[20];
			if (!CharacterDisplayName(c, display, sizeof(display)))
				display[0] = 0;
			const CharRecordData* rec = CharacterRecord(c);
			Log("gamestate: active slot %d = char %u %s (\"%s\") chrId %u, HP %u/%u MP %u/%u SLv %u",
			    slot, (unsigned)c, CharacterName(c), display,
			    (unsigned)ChrIdForCharacter(c, c == kCharRikku && RikkuAltOutfit()),
			    (unsigned)RecordCurrentHp(rec), (unsigned)RecordMaxHp(rec),
			    (unsigned)RecordCurrentMp(rec), (unsigned)RecordMaxMp(rec),
			    (unsigned)RecordSphereLevels(rec));
		}

		int usedItems = kItemSlots - FreeItemSlots();
		int equipUnlocked = 0, equipFree = 0;
		CountEquipment(&equipUnlocked, &equipFree);
		Log("gamestate: gil %u, party members %d, item slots %d/%d used, equipment %d/%d used",
		    (unsigned)Gil(), PartyMemberCount(), usedItems, kItemSlots,
		    kEquipSlots - equipFree, kEquipSlots);

		GameStateHash h;
		if (HashGameState(&h))
		{
			Log("gamestate: hash combined=0x%08X inventory=0x%08X equipment=0x%08X characters=0x%08X",
			    (unsigned)h.combined, (unsigned)h.bucket[kBucketInventory],
			    (unsigned)h.bucket[kBucketEquipment], (unsigned)h.bucket[kBucketCharacters]);
		}
	}

} // namespace ffx
