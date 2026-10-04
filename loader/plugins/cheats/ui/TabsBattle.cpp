// God mode, instant kill, free casting and overdrives.
//
// None of this is a patch or a detour. The developers' own battle debug flags
// shipped in the retail binary, 48 bytes of plain data that nothing else writes
// and that are not gated on the game's debug mode, so every cheat here is one byte.
// ffx/BattleDebug.h is the wrapper and reversing/CHEAT_BATTLE.md is the research.
//
// The rows are generated from the kit's table rather than listed here, so a cheat
// added to the kit appears in this panel with its caveat and needs no edit here.

#include "ui/Tabs.h"
#include "ui/CheatPanel.h"

#include "ffx/Battle.h"
#include "ffx/BattleDebug.h"
#include "ffx/GameLists.h"
#include "ffx/KernelTables.h"
#include "ffx/GameState.h"
#include "workshop/OverlayWidgets.h"

#include "imgui.h"

namespace cheats
{

	using namespace workshop;

	namespace
	{

		// The three that answer the brief, drawn first and larger. The rest of the
		// table is still offered below, because they are genuinely useful and they cost
		// nothing to expose.
		const ffx::BattleCheat kHeadline[] = {
		    ffx::CheatPartyInvincible,
		    ffx::CheatFreeCasting,
		    ffx::CheatEnemiesEnterAt1Hp,
		    ffx::CheatOverdriveAnyTime,
		};

		bool IsHeadline(ffx::BattleCheat which)
		{
			for (int i = 0; i < (int)(sizeof(kHeadline) / sizeof(kHeadline[0])); ++i)
				if (kHeadline[i] == which)
					return true;
			return false;
		}

		void CheatRow(ffx::BattleCheat which)
		{
			bool on = ffx::BattleCheatOn(which);

			ImGui::PushID((int)which);
			if (ImGui::Checkbox(ffx::BattleCheatName(which), &on))
				ffx::SetBattleCheat(which, on);

			const char* note = ffx::BattleCheatNote(which);
			if (note && note[0])
			{
				ImGui::SameLine();
				ImGui::TextDisabled("(?)");
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::PushTextWrapPos(380.0f);
					ImGui::TextUnformatted(note);
					ImGui::PopTextWrapPos();
					ImGui::EndTooltip();
				}
			}
			ImGui::PopID();
		}

	} // namespace

	void DrawBattleTab()
	{
		ImGui::Text("battle running: %s, command phase: %s, %d cheat%s on",
		    ffx::BattleRunning() ? "yes" : "no",
		    ffx::BattleCommandPhase() ? "yes" : "no", ffx::BattleCheatsOn(),
		    ffx::BattleCheatsOn() == 1 ? "" : "s");

		ImGui::SameLine();
		if (ImGui::SmallButton("all off"))
			ffx::ClearBattleCheats();

		ImGui::Separator();
		ImGui::TextDisabled("the ones you probably came for");

		for (int i = 0; i < (int)(sizeof(kHeadline) / sizeof(kHeadline[0])); ++i)
			CheatRow(kHeadline[i]);

		ImGui::Spacing();

		// The mid-battle kill. The flag above only applies at battle start, so this is
		// the one that does something to the fight on screen.
		if (!ffx::BattleRunning())
			ImGui::BeginDisabled();
		if (ImGui::Button("set every enemy to 1 HP now"))
			ffx::SetAllEnemiesTo1Hp();
		if (!ffx::BattleRunning())
			ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::TextDisabled("1 HP rather than a big number, because the damage cap is "
		                    "99999 and some bosses have millions");

		ImGui::Separator();

		if (ImGui::CollapsingHeader("every other debug flag"))
		{
			ImGui::TextDisabled("read the tooltips. several of these hit both sides.");
			for (int i = 0; i < ffx::CheatCount; ++i)
			{
				const ffx::BattleCheat which = (ffx::BattleCheat)i;
				if (!IsHeadline(which))
					CheatRow(which);
			}
		}

		ImGui::Separator();

		if (!RequireGame())
			return;

		ImGui::TextDisabled("overdrive modes");

		const PickerList& people = ffx::PartyCharacterList();
		static PickerState whoPick;
		static int who = ffx::kCharTidus;
		PickerById("character", people.Items(), people.Count(), &whoPick, &who);

		const int mask = ffx::OverdriveModeMask((BYTE)who);
		if (mask < 0)
		{
			ImGui::TextDisabled("that record is not readable");
			return;
		}

		int unlocked = 0;
		for (int i = 0; i < ffx::kOverdriveModeCount; ++i)
			if (mask & (1 << i))
				++unlocked;

		ImGui::Text("%d of %d modes unlocked, mode %d selected", unlocked,
		    ffx::kOverdriveModeCount, ffx::OverdriveMode((BYTE)who));

		// The gauge, and the two copies of it. A running battle reads the battle actor
		// and nothing copies the record back into it until the battle ends, so writing
		// the record mid battle looks like it did nothing.
		const int gauge = ffx::OverdriveGauge((BYTE)who);
		const int gaugeMax = ffx::OverdriveGaugeMax((BYTE)who);
		if (gauge >= 0 && gaugeMax >= 0)
		{
			int value = gauge;
			if (IntRow("gauge, saved", &value, 0, gaugeMax))
				ffx::SetOverdriveGauge((BYTE)who, value);
			ImGui::SameLine();
			if (ImGui::SmallButton("fill"))
				ffx::FillOverdriveGauge((BYTE)who);
			ImGui::TextDisabled("full means gauge == max, and the max is %d for this "
			                    "character and mode. It is not a fixed 100, so fill "
			                    "copies the max rather than writing a number.",
			    gaugeMax);
		}

		// unit index IS the character index for 0..6, which is the engine's own choice
		// rather than an assumption, and 7 is Seymour who is never a battle unit.
		const int liveGauge = ffx::UnitOverdrive(who);
		const int liveMax = ffx::UnitOverdriveMax(who);
		if (ffx::BattleRunning() && liveGauge >= 0 && liveMax >= 0)
		{
			int value = liveGauge;
			if (IntRow("gauge, this battle", &value, 0, liveMax))
				ffx::SetUnitOverdrive(who, value);
			ImGui::SameLine();
			if (ImGui::SmallButton("fill##live"))
				ffx::FillUnitOverdrive(who);
			ImGui::SameLine();
			if (ImGui::SmallButton("fill everyone"))
				ffx::FillAllyOverdrives();
			ImGui::TextDisabled("this is the one the running battle reads. The saved row "
			                    "above is loaded at battle start and does nothing until "
			                    "the next one.");
		}
		else if (ffx::BattleRunning())
		{
			ImGui::TextDisabled("that character is not a unit in this battle");
		}

		if (ImGui::Button("unlock all 20"))
			ffx::UnlockAllOverdriveModes((BYTE)who);
		ImGui::SameLine();
		if (ImGui::Button("lock all"))
			ffx::SetOverdriveModeMask((BYTE)who, 0);

		// Walked in the game's own display order rather than 0..19, because that is the
		// order the player sees them in on the overdrive screen.
		for (int position = 0; position < ffx::kOverdriveModeCount; ++position)
		{
			const int modeId = ffx::OverdriveModeInDisplayOrder(position);
			if (modeId < 0)
				continue;

			const bool held = (mask & (1 << modeId)) != 0;
			const int progress = ffx::OverdriveModeProgress((BYTE)who, modeId);

			ImGui::PushID(modeId);

			bool on = held;
			if (ImGui::Checkbox("##on", &on))
			{
				const int next = on ? (mask | (1 << modeId)) : (mask & ~(1 << modeId));
				ffx::SetOverdriveModeMask((BYTE)who, next);
			}

			ImGui::SameLine();
			if (ImGui::SmallButton("select"))
				ffx::SetOverdriveMode((BYTE)who, modeId);

			ImGui::SameLine();
			// 0xFFFF means the mode is not available to this character at all, which is
			// different from "not earned yet", so it is said rather than shown as a
			// number.
			if (progress == 0xFFFF)
				ImGui::TextDisabled("mode %2d  not available to this character", modeId);
			else
				ImGui::Text("mode %2d  progress %d", modeId, progress);

			ImGui::PopID();
		}

		// ---------------------------------------------------------------
		// The monster name table
		// ---------------------------------------------------------------
		//
		// All 366 of them load from the battle kernel at boot and nothing was offering
		// them, which was the one name table in the game with no picker on it. It is a
		// LOOKUP, not a spawner: a monster id is not an encounter id, and the thing that
		// starts a fight takes (map, encounter). Naming a fight by its monsters needs
		// the formation data, which is a separate question.
		if (ImGui::CollapsingHeader("Monster names"))
		{
			const PickerList& monsters = ffx::KernelList(ffx::KernelMonsters);

			if (monsters.Empty())
				ImGui::TextDisabled("the battle kernel has not been read. It needs the "
				                    "game's file system, so this is the state until the "
				                    "first simulation step.");
			else
			{
				static PickerState monsterPick;
				static int monsterId = 0;
				PickerById("monster", monsters.Items(), monsters.Count(), &monsterPick,
				    &monsterId);
				ImGui::TextDisabled("%s", monsters.Describe());
				ImGui::TextDisabled("id %d, 0x%03X", monsterId, (unsigned)monsterId);

				const char* name = ffx::KernelName(ffx::KernelMonsters, monsterId);
				ImGui::TextDisabled("name as the kernel spells it: %s",
				    name ? name : "(none)");
			}

			ImGui::TextWrapped("monster1.bin, monster2.bin and monster3.bin merged, ids "
			                   "0 to 365. The ordering is the game's own, so a run of "
			                   "ids is usually one area's bestiary.");
		}

		ImGui::Separator();

		PendingNote("Mode names",
		    "The 20 names are not in the exe. They live in the kernel string blob and "
		    "need a runtime call to fetch, and which table holds them has not been "
		    "found yet, so the rows above are numbered. Everything else about a mode, "
		    "its bit, its progress counter and its availability, is read from the game.");
	}

} // namespace cheats
