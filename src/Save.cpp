// The F4SE co-save: which settler deals at which table, and each NPC's caps for roulette.

#include "Game.h"

namespace fr
{
	extern std::mutex                                       g_npcMutex;
	extern std::unordered_map<RE::TESFormID, RE::TESFormID> g_dealers;
	extern std::unordered_map<RE::TESFormID, int>           g_purses;

	namespace
	{
		constexpr std::uint32_t kDealersType = 0x444C5253;  // 'DLRS'
		constexpr std::uint32_t kPursesType = 0x50525345;   // 'PRSE'
		constexpr std::uint32_t kVersion = 1;
	}

	void Game::Save(const F4SE::SerializationInterface* a_intfc)
	{
		std::scoped_lock lock(g_npcMutex);
		if (a_intfc->OpenRecord(kDealersType, kVersion)) {
			const auto n = static_cast<std::uint32_t>(g_dealers.size());
			a_intfc->WriteRecordData(&n, sizeof(n));
			for (const auto& [table, actor] : g_dealers) {
				a_intfc->WriteRecordData(&table, sizeof(table));
				a_intfc->WriteRecordData(&actor, sizeof(actor));
			}
		}
		if (a_intfc->OpenRecord(kPursesType, kVersion)) {
			const auto n = static_cast<std::uint32_t>(g_purses.size());
			a_intfc->WriteRecordData(&n, sizeof(n));
			for (const auto& [actor, caps] : g_purses) {
				a_intfc->WriteRecordData(&actor, sizeof(actor));
				a_intfc->WriteRecordData(&caps, sizeof(caps));
			}
		}
		log::info("co-save: {} dealers, {} purses written", g_dealers.size(), g_purses.size());
	}

	void Game::Load(const F4SE::SerializationInterface* a_intfc)
	{
		std::scoped_lock lock(g_npcMutex);
		g_dealers.clear();
		g_purses.clear();
		std::uint32_t type = 0, version = 0, length = 0;
		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			if (version != kVersion) {
				continue;
			}
			std::uint32_t n = 0;
			if (!a_intfc->ReadRecordData(&n, sizeof(n))) {
				continue;
			}
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::TESFormID a = 0;
				if (!a_intfc->ReadRecordData(&a, sizeof(a))) {
					break;
				}
				const auto resolvedA = a_intfc->ResolveFormID(a);
				if (type == kDealersType) {
					RE::TESFormID b = 0;
					if (!a_intfc->ReadRecordData(&b, sizeof(b))) {
						break;
					}
					const auto resolvedB = a_intfc->ResolveFormID(b);
					if (resolvedA && resolvedB) {
						g_dealers[*resolvedA] = *resolvedB;
					}
				} else if (type == kPursesType) {
					int caps = 0;
					if (!a_intfc->ReadRecordData(&caps, sizeof(caps))) {
						break;
					}
					if (resolvedA) {
						g_purses[*resolvedA] = caps;
					}
				}
			}
		}
		log::info("co-save: {} dealers, {} purses read", g_dealers.size(), g_purses.size());
	}

	void Game::Revert(const F4SE::SerializationInterface*)
	{
		std::scoped_lock lock(g_npcMutex);
		g_dealers.clear();
		g_purses.clear();
	}
}
