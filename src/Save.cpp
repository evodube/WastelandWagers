// The F4SE co-save: each NPC's caps for roulette, and the tables set up from kits (Kit.cpp). (0.2.0 and older also wrote 'DLRS', the dealers: skipped now.)

#include "Game.h"

namespace fr
{
	extern std::mutex                                       g_npcMutex;
	extern std::unordered_map<RE::TESFormID, int>           g_purses;
	extern std::unordered_map<RE::TESFormID, Game::PkMemory> g_pkMemory;

	namespace
	{
		constexpr std::uint32_t kPursesType = 0x50525345;   // 'PRSE'
		constexpr std::uint32_t kVersion = 1;
	}

	void Game::Save(const F4SE::SerializationInterface* a_intfc)
	{
		std::scoped_lock lock(g_npcMutex);
		if (a_intfc->OpenRecord(kPursesType, kVersion)) {
			const auto n = static_cast<std::uint32_t>(g_purses.size());
			a_intfc->WriteRecordData(&n, sizeof(n));
			for (const auto& [actor, caps] : g_purses) {
				a_intfc->WriteRecordData(&actor, sizeof(actor));
				a_intfc->WriteRecordData(&caps, sizeof(caps));
			}
		}
		log::info("co-save: {} purses written", g_purses.size());
		SaveKit(a_intfc);
		SavePokerMemory(a_intfc);
	}

	void Game::Load(const F4SE::SerializationInterface* a_intfc)
	{
		std::scoped_lock lock(g_npcMutex);
		g_purses.clear();
		g_pkMemory.clear();
		RevertKit();
		std::uint32_t type = 0, version = 0, length = 0;
		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			if (type == kKitRecordType) {
				LoadKit(a_intfc, version);
				continue;
			}
			if (type == kPkMemoryType) {
				LoadPokerMemory(a_intfc, version);
				continue;
			}
			if (version != kVersion || type != kPursesType) {
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
				if (type == kPursesType) {
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
		log::info("co-save: {} purses read", g_purses.size());
	}

	void Game::Revert(const F4SE::SerializationInterface*)
	{
		{
			std::scoped_lock lock(g_npcMutex);
			g_purses.clear();
			g_pkMemory.clear();
		}
		RevertKit();
	}
}
