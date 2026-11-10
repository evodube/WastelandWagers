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
	inline constexpr std::uint32_t kPackageID = 0x807;   // PACK fo4rouletteWalkToSpot
	inline constexpr std::uint32_t kQuestID = 0x808;     // QUST fo4rouletteQuest
	inline constexpr std::uint32_t kKitID = 0x80A;       // ALCH fo4rouletteTableKit
	inline constexpr std::uint32_t kCardID = 0x810;      // STAT fo4rouletteCard_2c .. _As: 0x810 + suit * 13 + rank
	inline constexpr std::uint32_t kPokerTableID = 0x844;  // ACTI fo4roulettePokerTable: Meshes\fo4roulette\PokerTable.nif
	inline constexpr std::uint32_t kPokerKitID = 0x846;  // ALCH fo4roulettePokerKit

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
}
