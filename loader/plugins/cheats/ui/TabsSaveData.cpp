// The tabs that edit the save block: gil, the party, per-character stats,
// equipment and the inventory.
//
// WHY A WRITE HERE IS SAFE. The persistent game state is one contiguous static
// buffer, and the engine re-reads it rather than caching it, with two exceptions
// the kit already knows about: the equipped-gear bonus cache and the per-character
// derived stats. Both are rebuilt by ffx::RecomputeDerivedStats, which is the
// game's own function, and every edit below that could invalidate them calls it.
// That is also why the base stats are edited rather than the effective ones: the
// effective value at +0x2F is an output, and writing it just gets overwritten.

#include "ui/Tabs.h"
#include "ui/CheatPanel.h"

#include "ffx/GameLists.h"
#include "ffx/Battle.h"
#include "ffx/GameState.h"
#include "ffx/KernelTables.h"
#include "ffx/WorldState.h"
#include "workshop/Log.h"
#include "workshop/OverlayWidgets.h"

#include "imgui.h"

#include <stdio.h>

namespace cheats
{

	using namespace workshop;

	namespace
	{

		// The eight stat bytes in record order. ffx::StatOrder gives the indices, this
		// gives them labels.
		const char* const kStatNames[ffx::kStatCount] = {
		    "Strength", "Defence", "Magic", "Magic defence",
		    "Agility", "Luck", "Evasion", "Accuracy",
		};

		// The record field a stat edit has to land in, so the engine's rebuild keeps
		// it. See the file header.
		BYTE* BaseStatByte(ffx::CharRecordData* rec, int stat)
		{
			if (!rec || stat < 0 || stat >= ffx::kStatCount)
				return nullptr;
			return (BYTE*)rec + ffx::CharRecord::BaseStats + stat;
		}

		DWORD* RecordDword(ffx::CharRecordData* rec, DWORD offset)
		{
			if (!rec)
				return nullptr;
			return (DWORD*)((BYTE*)rec + offset);
		}

		BYTE* RecordByte(ffx::CharRecordData* rec, DWORD offset)
		{
			if (!rec)
				return nullptr;
			return (BYTE*)rec + offset;
		}

		// Shared across the character tab and the equipment tab, so picking Auron in
		// one and then switching tabs keeps him picked.
		int g_character = ffx::kCharTidus;
		PickerState g_characterPick;

	} // namespace

	// ---------------------------------------------------------------------------
	// Game
	// ---------------------------------------------------------------------------

	void DrawGameTab()
	{
		ImGui::TextDisabled("event %d, debug mode %s", ffx::CurrentEventId(),
		    ffx::IsDebugMode() ? "ON" : "off");

		char name[16];
		if (ffx::EventNameForId(ffx::CurrentEventId(), name, (int)sizeof(name)))
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(%s)", name);
		}

		ImGui::Separator();

		if (!RequireGame())
			return;

		// Gil. The game clamps to kGilMax itself, so the row just has to not go
		// negative.
		int gil = (int)ffx::Gil();
		if (IntRow("gil", &gil, 0, ffx::kGilMax, 1000))
			ffx::SetGil((DWORD)gil);

		ImGui::SameLine();
		if (ImGui::SmallButton("max gil"))
			ffx::SetGil((DWORD)ffx::kGilMax);

		ImGui::Separator();
		ImGui::Text("party members: %d of 18", ffx::PartyMemberCount());
		ImGui::Text("equipment: %s", ffx::EquipmentList().Describe());
		ImGui::Text("inventory: %s", ffx::InventoryList().Describe());
	}

	// ---------------------------------------------------------------------------
	// Party
	// ---------------------------------------------------------------------------

	void DrawPartyTab()
	{
		if (!RequireGame())
			return;

		const PickerList& people = ffx::PartyCharacterList();

		ImGui::TextWrapped("The three active slots. A character has to be in the party "
		                   "before a slot can hold them, which the toggles below do.");

		static PickerState slotPick[ffx::kActivePartySize];
		int chosen[ffx::kActivePartySize];
		bool changed = false;

		for (int slot = 0; slot < ffx::kActivePartySize; ++slot)
		{
			chosen[slot] = (int)ffx::ActivePartyMember(slot);

			char label[24];
			_snprintf(label, sizeof(label) - 1, "slot %d", slot + 1);
			label[sizeof(label) - 1] = 0;

			if (PickerById(label, people.Items(), people.Count(), &slotPick[slot],
			        &chosen[slot]))
				changed = true;
		}

		static int refused = 0;

		if (changed)
		{
			// The engine's own slot setter, one slot at a time so a refusal can be named.
			// Writing the three bytes raw also works but leaves the 17-slot bench out of
			// step, because the active slots and the bench are one 20-byte array.
			ffx::SetFieldParty((BYTE)chosen[0], (BYTE)chosen[1], (BYTE)chosen[2], &refused);

			// Writing the party is not enough on a map that is already loaded. Which
			// CHRs exist is map data, which of them are VISIBLE is this mask, and nothing
			// else rebuilds it.
			ffx::RefreshPartyVisibility();

			Log("cheats: active party set to %d, %d, %d, refusal mask 0x%X", chosen[0],
			    chosen[1], chosen[2], refused);
		}

		if (refused)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
			    "the engine refused slots %s%s%s", (refused & 1) ? "1 " : "",
			    (refused & 2) ? "2 " : "", (refused & 4) ? "3 " : "");
			ImGui::TextWrapped("A slot is refused when the character is not in the party "
			                   "at all, or when either side of the swap is locked into its "
			                   "slot. Both are in the table below.");
		}

		if (ffx::BattleRunning())
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
			    "A battle is running, so this edit will be thrown away when it ends. The "
			    "engine copies the battle party back over the field party at battle end.");

		ImGui::Separator();
		ImGui::TextDisabled("who is in the party at all, and who is nailed to their slot");

		if (ImGui::BeginTable("##partyflags", 4,
		        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
		            | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("character");
			ImGui::TableSetupColumn("in party");
			ImGui::TableSetupColumn("slot locked");
			ImGui::TableSetupColumn("notes");
			ImGui::TableHeadersRow();

			for (BYTE i = 0; i < (BYTE)ffx::kCharCount; ++i)
			{
				if (!ffx::CharacterRecord(i))
					continue;

				const char* who = ffx::UnitDisplayName(i);
				if (!who || !who[0])
					who = ffx::CharacterName(i);

				ImGui::PushID((int)i);
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				ImGui::Text("%d %s", (int)i, who ? who : "?");

				ImGui::TableNextColumn();
				bool in = ffx::CharacterInParty(i);
				if (ImGui::Checkbox("##in", &in))
				{
					// The game's own setter, which also drives the equipment lock bit and
					// the aeon order array. It takes exactly 0 or 1, and the kit clamps
					// the index, because the engine's version has no bound at all.
					ffx::SetCharacterInParty(i, in);
					ffx::RefreshPartyVisibility();
					Log("cheats: %s %s the party", who ? who : "?", in ? "joins" : "leaves");
				}

				ImGui::TableNextColumn();
				bool locked = ffx::CharacterLockedInSlot(i);
				if (ImGui::Checkbox("##lock", &locked))
				{
					ffx::SetCharacterLockedInSlot(i, locked);
					Log("cheats: %s is %s its party slot", who ? who : "?",
					    locked ? "locked into" : "free to leave");
				}

				ImGui::TableNextColumn();
				const int slot = ffx::ActiveSlotOfCharacter(i);
				if (slot >= 0)
					ImGui::Text("active slot %d%s", slot + 1,
					    ffx::CharacterPermanent(i) ? ", permanent" : "");
				else if (ffx::CharacterPermanent(i))
					ImGui::TextDisabled("permanent");
				else if (!ffx::CharacterSelectable(i))
					ImGui::TextDisabled("not selectable in menus");
				else
					ImGui::TextDisabled("-");

				ImGui::PopID();
			}
			ImGui::EndTable();
		}
	}

	// ---------------------------------------------------------------------------
	// Characters
	// ---------------------------------------------------------------------------

	void DrawCharactersTab()
	{
		if (!RequireGame())
			return;

		const PickerList& people = ffx::CharacterList();
		PickerById("character", people.Items(), people.Count(), &g_characterPick,
		    &g_character);

		ffx::CharRecordData* rec = ffx::CharacterRecord((BYTE)g_character);
		if (!rec)
		{
			ImGui::TextDisabled("that record is not readable");
			return;
		}

		ImGui::Separator();

		// HP and MP. The current values have kit writers, the maxima are the BASE
		// fields because the effective ones at +0x24 and +0x28 are rebuilt.
		int hp = (int)ffx::RecordCurrentHp(rec);
		if (IntRow("current HP", &hp, 0, 99999, 10))
			ffx::SetRecordCurrentHp(rec, (DWORD)hp);

		int mp = (int)ffx::RecordCurrentMp(rec);
		if (IntRow("current MP", &mp, 0, 9999, 10))
			ffx::SetRecordCurrentMp(rec, (DWORD)mp);

		bool rebuild = false;

		DWORD* baseHp = RecordDword(rec, ffx::CharRecord::BaseMaxHp);
		if (baseHp)
		{
			int value = (int)*baseHp;
			if (IntRow("base max HP", &value, 0, 99999, 10))
			{
				*baseHp = (DWORD)value;
				rebuild = true;
			}
		}

		DWORD* baseMp = RecordDword(rec, ffx::CharRecord::BaseMaxMp);
		if (baseMp)
		{
			int value = (int)*baseMp;
			if (IntRow("base max MP", &value, 0, 9999, 10))
			{
				*baseMp = (DWORD)value;
				rebuild = true;
			}
		}

		ImGui::Separator();
		ImGui::TextDisabled("base stats. the effective value in brackets is what the game "
		                    "computes from these plus equipment");

		for (int stat = 0; stat < ffx::kStatCount; ++stat)
		{
			BYTE* value = BaseStatByte(rec, stat);
			if (!value)
				continue;

			char label[40];
			_snprintf(label, sizeof(label) - 1, "%s  (%d)", kStatNames[stat],
			    (int)ffx::RecordStat(rec, stat));
			label[sizeof(label) - 1] = 0;

			if (ByteRow(label, value, 1, 255))
				rebuild = true;
		}

		ImGui::Separator();

		DWORD* ap = RecordDword(rec, ffx::CharRecord::TotalAp);
		if (ap)
		{
			int value = (int)*ap;
			if (IntRow("total AP", &value, 0, 9999999, 100))
				*ap = (DWORD)value;
		}

		BYTE* levels = RecordByte(rec, ffx::CharRecord::SphereLevels);
		if (levels)
			ByteRow("sphere levels available", levels, 0, 255);

		ImGui::SameLine();
		if (ImGui::SmallButton("+99 S.Lv") && levels)
		{
			const int raised = (int)*levels + 99;
			*levels = (BYTE)(raised > 255 ? 255 : raised);
		}

		if (rebuild)
		{
			// The game's own rebuild. Without it the edit sits in the base field and
			// the effective stats keep their old values, which looks like the write
			// failed.
			if (!ffx::RecomputeDerivedStats())
				Log("cheats: a stat edit landed but the derived-stat rebuild failed, so "
				    "the change will not show until the game reloads the save");
		}
	}

	// ---------------------------------------------------------------------------
	// Equipment
	// ---------------------------------------------------------------------------

	void DrawEquipmentTab()
	{
		if (!RequireGame())
			return;

		int unlocked = 0;
		int free = 0;
		if (ffx::CountEquipment(&unlocked, &free))
			ImGui::TextDisabled("%d in use, %d free of %d", unlocked, free,
			    ffx::kEquipSlots);

		const PickerList& gear = ffx::EquipmentList();
		static PickerState gearPick;
		static int slotId = 0;
		PickerById("piece", gear.Items(), gear.Count(), &gearPick, &slotId);

		ffx::EquipEntryData* entry = ffx::EquipEntryFromSlotId((WORD)slotId);
		if (!entry)
		{
			ImGui::TextDisabled("nothing picked");
			return;
		}

		ImGui::Separator();

		// The engine's name, not the kernel row read at offset zero. A w_name.bin row
		// holds one name per character and the piece's forChar byte picks which, so
		// reading the row's first string gets Tidus's name for everybody's gear.
		const char* pieceName = ffx::EquipName((WORD)slotId);
		ImGui::Text("name      : %s", pieceName ? pieceName : "(the engine would not name it)");
		ImGui::Text("kind      : %s", ffx::EquipEntryKind(entry) == 0 ? "weapon" : "armour");
		ImGui::Text("model for : %s", ffx::CharacterName(ffx::EquipEntryForChar(entry)));
		ImGui::Text("name id   : 0x%04X", ffx::EquipEntryNameId(entry));
		ImGui::Text("model id  : 0x%04X", ffx::EquipEntryModelId(entry));

		const BYTE owner = ffx::EquipEntryOwner(entry);
		if (owner == (BYTE)ffx::kCharNone)
			ImGui::Text("worn by   : nobody");
		else
			ImGui::Text("worn by   : %s", ffx::CharacterName(owner));

		ImGui::Separator();

		// The list offered is the kind-appropriate one, from the game's own customise
		// recipe table, because that is the only table that says which auto-abilities
		// belong on a weapon and which on an armour. The toggle falls back to all 134
		// for the four real ones that have no recipe, plus the five Extra placeholders
		// that do nothing.
		static bool showEverything = false;
		ImGui::Checkbox("offer every auto-ability, not just the legal ones", &showEverything);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Off: the %d weapon and %d armour abilities the Customise "
			                  "screen can make. On: all %d rows of a_ability.bin, which "
			                  "includes five placeholders that do nothing.",
			    ffx::WeaponAbilityList().Count(), ffx::ArmourAbilityList().Count(),
			    ffx::KernelList(ffx::KernelAutoAbilities).Count());

		const bool isArmour = ffx::EquipEntryKind(entry) != 0;

		// Two id spaces meet here. The recipe lists already carry the 0x8000 tag the
		// entry stores, the kernel table is keyed on the bare index, so which one is
		// in the picker decides whether the tag has to be put back on.
		const PickerList& autos = showEverything
		    ? ffx::KernelList(ffx::KernelAutoAbilities)
		    : (isArmour ? ffx::ArmourAbilityList() : ffx::WeaponAbilityList());
		const bool listIsTagged = !showEverything;

		ImGui::TextDisabled("auto abilities, %d on offer. Type in a picker to filter.",
		    autos.Count());

		static PickerState abilityPick[ffx::kEquipAbilitySlots];

		for (int i = 0; i < ffx::kEquipAbilitySlots; ++i)
		{
			const WORD stored = ffx::EquipEntryAbility(entry, i);
			int pickId = listIsTagged ? (int)stored : ffx::KernelIdFromTagged((int)stored);

			char label[24];
			_snprintf(label, sizeof(label) - 1, "slot %d", i);
			label[sizeof(label) - 1] = 0;

			ImGui::PushID(i);
			if (PickerById(label, autos.Items(), autos.Count(), &abilityPick[i], &pickId))
			{
				const WORD write = listIsTagged
				    ? (WORD)pickId
				    : (WORD)(ffx::kAbilityIdSpace | (pickId & 0x0FFF));

				// The kit's setter writes the word and then recomputes the piece's
				// name, which in FFX is a function of the ability set rather than a
				// stored string, so skipping that leaves the piece misnamed.
				if (ffx::SetEquipEntryAbility(entry, i, write))
				{
					ffx::RecomputeDerivedStats();
					RefreshCheatLists();
				}
			}

			ImGui::SameLine();
			if (ImGui::SmallButton("clear"))
			{
				// Direct, because no engine function removes an auto-ability. The
				// adder only ever appends.
				if (ffx::SetEquipEntryAbility(entry, i, 0))
				{
					ffx::RecomputeDerivedStats();
					RefreshCheatLists();
				}
			}

			if (!stored)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("empty");
			}
			ImGui::PopID();
		}

		if (autos.Empty())
			ImGui::TextDisabled("that ability list has not loaded. The names come out of "
			                    "the archive and the recipe table is filled during boot, "
			                    "so both want the game past the title screen.");

		ImGui::Separator();
		ImGui::TextDisabled("equip it on somebody");

		const PickerList& people = ffx::PartyCharacterList();
		static PickerState wearerPick;
		static int wearer = ffx::kCharTidus;
		PickerById("wearer", people.Items(), people.Count(), &wearerPick, &wearer);

		const bool armour = ffx::EquipEntryKind(entry) != 0;
		if (ImGui::Button(armour ? "equip as armour" : "equip as weapon"))
		{
			// The game's own setter, which fixes up the old and new entries' owner
			// byte. Writing the record field by hand leaves the old piece still
			// marked as worn.
			if (ffx::EquipItemOn((BYTE)wearer, armour, (WORD)slotId))
			{
				ffx::RecomputeDerivedStats();
				RefreshCheatLists();
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("unequip"))
		{
			if (ffx::EquipItemOn((BYTE)wearer, armour, (WORD)ffx::kEquipSlotIdNone))
			{
				ffx::RecomputeDerivedStats();
				RefreshCheatLists();
			}
		}
	}

	// ---------------------------------------------------------------------------
	// Inventory
	// ---------------------------------------------------------------------------

	void DrawInventoryTab()
	{
		if (!RequireGame())
			return;

		ImGui::TextDisabled("%d of %d slots free", ffx::FreeItemSlots(), ffx::kItemSlots);

		const PickerList& held = ffx::InventoryList();
		static PickerState itemPick;
		static int itemId = 0;
		PickerById("item you already hold", held.Items(), held.Count(), &itemPick,
		    &itemId);

		if (itemId)
		{
			ImGui::Text("you have %d", ffx::ItemCount((WORD)itemId));

			static int delta = 99;
			IntRow("change by", &delta, -99, 99);

			if (ImGui::Button("apply"))
			{
				// AddItem is the game's single add and remove funnel, which is why it
				// is used rather than writing the count byte. It also maintains the
				// two change masks the menu reads.
				ffx::AddItem((WORD)itemId, delta);
				RefreshCheatLists();
			}
		}

		ImGui::Separator();
		ImGui::TextDisabled("give yourself anything. all 112 items, filter by name.");

		const PickerList& all = ffx::KernelList(ffx::KernelItems);
		if (all.Empty())
		{
			ImGui::TextDisabled("the item name table has not loaded yet. It is read out "
			                    "of the archive once the game has booted, so press "
			                    "refresh lists.");
			return;
		}

		static PickerState spawnPick;
		static int spawnKernelId = 0;
		PickerById("item", all.Items(), all.Count(), &spawnPick, &spawnKernelId);

		static int spawnCount = 99;
		IntRow("how many", &spawnCount, 1, ffx::kItemCountMax);

		if (ImGui::Button("add"))
		{
			// AddItem wants the tagged id, the same space the inventory stores.
			const WORD tagged = (WORD)(ffx::kItemIdSpace | (spawnKernelId & 0x0FFF));
			ffx::AddItem(tagged, spawnCount);
			RefreshCheatLists();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("you hold %d",
		    ffx::ItemCount((WORD)(ffx::kItemIdSpace | (spawnKernelId & 0x0FFF))));
	}

} // namespace cheats
