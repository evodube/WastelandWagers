#pragma once

#include "Settings.h"
#include "Util.h"

// fo4roulette.esl's records (tools/make_esl.py) and the vanilla ones the plugin uses.
namespace fr::Forms
{
	inline constexpr std::uint32_t kTableID = 0x800;     // ACTI fo4rouletteTable: Meshes\fo4roulette\RouletteTable.nif
	inline constexpr std::uint32_t kBallID = 0x801;      // STAT fo4rouletteBall
	inline constexpr std::uint32_t kChipID = 0x802;      // STAT fo4rouletteChip1, 5, 25, 100 at 0x802-0x805
	inline constexpr std::uint32_t kLinkID = 0x806;      // KYWD fo4rouletteLinkSpot
	inline constexpr std::uint32_t kSeatLinkID = 0x848;  // KYWD fo4rouletteLinkSeat (a poker player's chair: their Sit package's)
	inline constexpr std::uint32_t kPackageID = 0x807;   // PACK fo4rouletteWalkToSpot
	inline constexpr std::uint32_t kQuestID = 0x808;     // QUST fo4rouletteQuest
	inline constexpr std::uint32_t kKitID = 0x80A;       // ALCH fo4rouletteTableKit
	inline constexpr std::uint32_t kCardID = 0x810;      // STAT fo4rouletteCard_2c .. _As: 0x810 + suit * 13 + rank
	inline constexpr std::uint32_t kPokerTableID = 0x844;  // ACTI fo4roulettePokerTable: Meshes\fo4roulette\PokerTable.nif
	inline constexpr std::uint32_t kPokerKitID = 0x846;  // ALCH fo4roulettePokerKit
	inline constexpr std::uint32_t kBlackjackTableID = 0x84A;  // ACTI fo4rouletteBlackjackTable: Meshes\fo4roulette\BlackjackTable.nif
	inline constexpr std::uint32_t kBlackjackKitID = 0x84C;    // ALCH fo4rouletteBlackjackKit
	inline constexpr std::uint32_t kWorkshopMenuID = 0x850;    // KYWD fo4rouletteWorkshopMenuMiniGames: the Mini Games workshop category
	inline constexpr std::uint32_t kSlotMachineID = 0x84E;     // FURN fo4rouletteSlotMachine: Meshes\fo4roulette\SlotMachine.nif (the
	                                                           // Vault-Tec Workshop's machine, re-skinned; its sitting animations)

	inline constexpr RE::TESFormID kXMarkerHeading = 0x00000034;
	inline constexpr RE::TESFormID kCaps = 0x0000000F;
	inline constexpr RE::TESFormID kActorTypeNPC = 0x00013794;

	inline RE::TESForm* Own(std::uint32_t a_id) { return util::ResolveAnyForm(Settings::Get().plugin, a_id); }

	inline RE::TESBoundObject* Table()
	{
		auto* f = Own(kTableID);
		return f && f->IsBoundObject() ? static_cast<RE::TESBoundObject*>(f) : nullptr;
	}
	inline RE::TESBoundObject* Ball() { return util::ResolveForm(Settings::Get().plugin, kBallID); }
	inline RE::TESBoundObject* Chip(int a_index) { return util::ResolveForm(Settings::Get().plugin, kChipID + static_cast<std::uint32_t>(std::clamp(a_index, 0, 3))); }
	inline RE::BGSKeyword*     LinkKeyword()
	{
		auto* f = Own(kLinkID);
		return f ? f->As<RE::BGSKeyword>() : nullptr;
	}
	inline RE::BGSKeyword* SeatKeyword()
	{
		auto* f = Own(kSeatLinkID);
		return f ? f->As<RE::BGSKeyword>() : nullptr;
	}
	inline RE::TESQuest* Quest()
	{
		auto* f = Own(kQuestID);
		return f ? f->As<RE::TESQuest>() : nullptr;
	}
	inline RE::TESForm* Package() { return Own(kPackageID); }
	inline RE::TESBoundObject* Marker()
	{
		auto* f = RE::TESForm::GetFormByID(kXMarkerHeading);
		return f && f->IsBoundObject() ? static_cast<RE::TESBoundObject*>(f) : nullptr;
	}
	inline RE::TESBoundObject* Caps()
	{
		auto* f = RE::TESForm::GetFormByID(kCaps);
		return f && f->IsBoundObject() ? static_cast<RE::TESBoundObject*>(f) : nullptr;
	}

	inline bool IsTable(const RE::TESObjectREFR* a_ref)
	{
		auto* base = a_ref ? a_ref->GetObjectReference() : nullptr;
		auto* table = Table();
		return base && table && base == table;
	}

	inline RE::TESBoundObject* PokerTable()
	{
		auto* f = Own(kPokerTableID);
		return f && f->IsBoundObject() ? static_cast<RE::TESBoundObject*>(f) : nullptr;
	}
	inline RE::TESBoundObject* Card(int a_card) { return util::ResolveForm(Settings::Get().plugin, kCardID + static_cast<std::uint32_t>(std::clamp(a_card, 0, 51))); }
	inline bool IsPokerTable(const RE::TESObjectREFR* a_ref)
	{
		auto* base = a_ref ? a_ref->GetObjectReference() : nullptr;
		auto* table = PokerTable();
		return base && table && base == table;
	}
	inline RE::TESBoundObject* BlackjackTable()
	{
		auto* f = Own(kBlackjackTableID);
		return f && f->IsBoundObject() ? static_cast<RE::TESBoundObject*>(f) : nullptr;
	}
	inline bool IsBlackjackTable(const RE::TESObjectREFR* a_ref)
	{
		auto* base = a_ref ? a_ref->GetObjectReference() : nullptr;
		auto* table = BlackjackTable();
		return base && table && base == table;
	}
	inline RE::TESBoundObject* SlotMachine()
	{
		auto* f = Own(kSlotMachineID);
		return f && f->IsBoundObject() ? static_cast<RE::TESBoundObject*>(f) : nullptr;
	}
	inline bool IsSlotMachine(const RE::TESObjectREFR* a_ref)
	{
		auto* base = a_ref ? a_ref->GetObjectReference() : nullptr;
		auto* machine = SlotMachine();
		return base && machine && base == machine;
	}
	// the workshop menu's Mini Games category (every table's and the slot machine's recipe is in it): put in the Furniture
	// menu, beside Tables, when the game's data is loaded. The menu's form list isn't overridden in the plugin (that
	// would undo other mods' changes to it): our category's added to it here, each time the game starts
	inline void AddWorkshopMenu()
	{
		auto* data = RE::TESDataHandler::GetSingleton();
		auto* ours = Own(kWorkshopMenuID);
		auto* tables = RE::TESForm::GetFormByID(0x00118F59);  // WorkshopRecipeFilterFurniture02Tables
		if (!data || !ours || !tables) {
			log::warn("workshop menu: the Mini Games category ({}) or the Tables one ({}) isn't there", ours != nullptr, tables != nullptr);
			return;
		}
		int added = 0;
		for (auto* list : data->GetFormArray<RE::BGSListForm>()) {
			if (!list) {
				continue;
			}
			bool hasTables = false, hasOurs = false;
			for (auto* f : list->arrayOfForms) {
				hasTables = hasTables || f == tables;
				hasOurs = hasOurs || f == ours;
			}
			if (hasTables && !hasOurs) {
				// (right after Tables)
				auto at = std::ranges::find(list->arrayOfForms, tables);
				list->arrayOfForms.insert(at == list->arrayOfForms.end() ? at : at + 1, ours);
				++added;
				log::info("workshop menu: Mini Games added to {:08X} (the menu with Tables in it)", list->GetFormID());
			}
		}
		if (added == 0) {
			log::warn("workshop menu: no menu with Tables in it found: the Mini Games category isn't in the workshop menu");
		}
	}

	// the Vault-Tec Workshop's own: its slot machine's disappointed look (DLC06_SlotMachineDisappointedIdle)
	inline RE::TESForm* SlotDisappointed()
	{
		static RE::TESForm* idle = nullptr;
		if (!idle) {
			if (auto* data = RE::TESDataHandler::GetSingleton()) {
				idle = data->LookupForm(0x00256A, "DLCworkshop03.esm");
			}
		}
		return idle;
	}
}
