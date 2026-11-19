#pragma once

#include "Settings.h"
#include "Util.h"

// WastelandWagers.esl's records (tools/make_esl.py) and the vanilla ones the plugin uses.
namespace fr::Forms
{
	inline constexpr std::uint32_t kTableID = 0x800;     // ACTI WastelandWagersTable: Meshes\WastelandWagers\RouletteTable.nif
	inline constexpr std::uint32_t kBallID = 0x801;      // STAT WastelandWagersBall
	inline constexpr std::uint32_t kChipID = 0x802;      // STAT WastelandWagersChip1, 5, 25, 100 at 0x802-0x805
	inline constexpr std::uint32_t kLinkID = 0x806;      // KYWD WastelandWagersLinkSpot
	inline constexpr std::uint32_t kSeatLinkID = 0x848;  // KYWD WastelandWagersLinkSeat (a poker player's chair: their Sit package's)
	inline constexpr std::uint32_t kPackageID = 0x807;   // PACK WastelandWagersWalkToSpot
	inline constexpr std::uint32_t kQuestID = 0x808;     // QUST WastelandWagersQuest
	inline constexpr std::uint32_t kKitID = 0x80A;       // ALCH WastelandWagersTableKit
	inline constexpr std::uint32_t kCardID = 0x810;      // STAT WastelandWagersCard_2c .. _As: 0x810 + suit * 13 + rank
	inline constexpr std::uint32_t kPokerTableID = 0x844;  // ACTI WastelandWagersPokerTable: Meshes\WastelandWagers\PokerTable.nif
	inline constexpr std::uint32_t kPokerKitID = 0x846;  // ALCH WastelandWagersPokerKit
	inline constexpr std::uint32_t kBlackjackTableID = 0x84A;  // ACTI WastelandWagersBlackjackTable: Meshes\WastelandWagers\BlackjackTable.nif
	inline constexpr std::uint32_t kBlackjackKitID = 0x84C;    // ALCH WastelandWagersBlackjackKit
	inline constexpr std::uint32_t kWorkshopMenuID = 0x850;    // KYWD WastelandWagersWorkshopMenu: the Wasteland Wagers workshop category
	inline constexpr std::uint32_t kSlotMachineID = 0x84E;     // FURN WastelandWagersSlotMachine: Meshes\WastelandWagers\SlotMachine.nif (the
	                                                           // Vault-Tec Workshop's machine, re-skinned; its sitting animations)
	inline constexpr std::uint32_t kSlotPullID = 0x852;        // IDLE WastelandWagersSlotPullIdle: sat at the machine, a pull (the DLC's)
	inline constexpr std::uint32_t kSlotHoldID = 0x853;
	// the Used ones (post-war textures, the same models): played just as the clean ones
	inline constexpr std::uint32_t kTableUsedID = 0x854;           // ACTI WastelandWagersTableUsed
	inline constexpr std::uint32_t kPokerTableUsedID = 0x856;      // ACTI WastelandWagersPokerTableUsed
	inline constexpr std::uint32_t kBlackjackTableUsedID = 0x858;  // ACTI WastelandWagersBlackjackTableUsed
	inline constexpr std::uint32_t kSlotMachineUsedID = 0x85A;     // FURN WastelandWagersSlotMachineUsed        // IDLE WastelandWagersSlotHoldIdle: sat at it, still (its sitting loop, looping)

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

	// a ref's base is ours: the clean one or its Used twin
	inline bool IsOurs(const RE::TESObjectREFR* a_ref, std::uint32_t a_clean, std::uint32_t a_used)
	{
		auto* base = a_ref ? a_ref->GetObjectReference() : nullptr;
		return base && (base == Own(a_clean) || base == Own(a_used));
	}
	inline bool IsTable(const RE::TESObjectREFR* a_ref) { return IsOurs(a_ref, kTableID, kTableUsedID); }

	inline RE::TESBoundObject* PokerTable()
	{
		auto* f = Own(kPokerTableID);
		return f && f->IsBoundObject() ? static_cast<RE::TESBoundObject*>(f) : nullptr;
	}
	inline RE::TESBoundObject* Card(int a_card) { return util::ResolveForm(Settings::Get().plugin, kCardID + static_cast<std::uint32_t>(std::clamp(a_card, 0, 51))); }
	inline bool IsPokerTable(const RE::TESObjectREFR* a_ref) { return IsOurs(a_ref, kPokerTableID, kPokerTableUsedID); }
	inline RE::TESBoundObject* BlackjackTable()
	{
		auto* f = Own(kBlackjackTableID);
		return f && f->IsBoundObject() ? static_cast<RE::TESBoundObject*>(f) : nullptr;
	}
	inline bool IsBlackjackTable(const RE::TESObjectREFR* a_ref) { return IsOurs(a_ref, kBlackjackTableID, kBlackjackTableUsedID); }
	inline RE::TESBoundObject* SlotMachine()
	{
		auto* f = Own(kSlotMachineID);
		return f && f->IsBoundObject() ? static_cast<RE::TESBoundObject*>(f) : nullptr;
	}
	inline bool IsSlotMachine(const RE::TESObjectREFR* a_ref) { return IsOurs(a_ref, kSlotMachineID, kSlotMachineUsedID); }
	// the workshop menu's Wasteland Wagers category (every table's and the slot machine's recipe is in it): in the
	// Furniture menu, right after Tables, when the game's data is loaded. The menu's form lists aren't overridden in
	// the plugin (that would undo other mods' changes to them): the category's added here, each time the game starts.
	// The Furniture menu is the form list (or lists, mods' copies too) with Tables in it
	inline void AddWorkshopMenu()
	{
		auto* data = RE::TESDataHandler::GetSingleton();
		auto* ours = Own(kWorkshopMenuID);
		auto* tables = RE::TESForm::GetFormByID(0x00118F59);  // WorkshopRecipeFilterFurniture02Tables
		if (!data || !ours || !tables) {
			log::warn("workshop menu: the Wasteland Wagers category ({}) or the Tables one ({}) isn't there", ours != nullptr, tables != nullptr);
			return;
		}
		int added = 0;
		for (auto* list : data->GetFormArray<RE::BGSListForm>()) {
			if (!list) {
				continue;
			}
			auto& forms = list->arrayOfForms;
			const auto at = std::ranges::find(forms, tables);
			if (at == forms.end() || std::ranges::find(forms, ours) != forms.end()) {
				continue;
			}
			forms.insert(at + 1, ours);
			++added;
			log::info("workshop menu: Wasteland Wagers added to {:08X} (the Furniture menu, after Tables)", list->GetFormID());
		}
		if (added == 0) {
			log::warn("workshop menu: the Furniture menu (the list with Tables in it) wasn't found: the Wasteland Wagers category isn't in the workshop menu");
		}
	}

	// a pull at the slot machine (the DLC's own pull, played on whoever's sat at it)
	inline RE::TESForm* SlotPull() { return Own(kSlotPullID); }
	// ...sat still at it (the sitting loop alone: no idle flavours, so no pulls of its own)
	inline RE::TESForm* SlotHold() { return Own(kSlotHoldID); }

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
