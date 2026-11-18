// Who was where when the game was saved: each table's dealer and players, and whoever sat at a slot machine, in the
// co-save ('RSTR'), so a load puts them back. The save itself leaves them as they are (sat, held: Before*Save); on a
// load, the quest's clear keeps them in its collections (RosterKeep), and each table's finders take them first
// (RosterNext), back in the same seat, within two minutes of the load.

#include "Game.h"

namespace fr
{
	std::vector<Game::RosterEntry>              Game::s_roster;
	std::chrono::steady_clock::time_point       Game::s_rosterLoaded{};

	namespace
	{
		constexpr std::uint32_t kRosterType = 0x52535452;  // 'RSTR'
		constexpr std::uint32_t kRosterVersion = 1;

		RE::TESFormID IdOf(const RE::ObjectRefHandle& a_h)
		{
			auto r = a_h.get();
			return r ? r->GetFormID() : 0;
		}
	}

	void Game::SaveRoster(const F4SE::SerializationInterface* a_intfc)
	{
		auto&                    g = Get();
		std::vector<RosterEntry> out;
		for (const auto* t : { &g._t, &g._other }) {
			const auto table = IdOf(t->table);
			if (!table) {
				continue;
			}
			if (t->croupier.id) {
				out.push_back({ kRosterRoulette, table, -1, t->croupier.id });
			}
			for (const auto& st : t->seats) {
				if (st.id) {
					out.push_back({ kRosterRoulette, table, st.spot, st.id });
				}
			}
		}
		if (const auto table = IdOf(g._pk.table)) {
			if (g._pk.croupier.id) {
				out.push_back({ kRosterPoker, table, -1, g._pk.croupier.id });
			}
			for (int i = 0; i < static_cast<int>(g._pk.seats.size()); ++i) {
				if (g._pk.seats[i].used && !g._pk.seats[i].you && g._pk.seats[i].id) {
					out.push_back({ kRosterPoker, table, i, g._pk.seats[i].id });
				}
			}
		}
		if (const auto table = IdOf(g._bj.table)) {
			if (g._bj.dealerId) {
				out.push_back({ kRosterBlackjack, table, -1, g._bj.dealerId });
			}
			for (int i = 0; i < static_cast<int>(g._bj.seats.size()); ++i) {
				if (g._bj.seats[i].used && !g._bj.seats[i].you && g._bj.seats[i].id) {
					out.push_back({ kRosterBlackjack, table, i, g._bj.seats[i].id });
				}
			}
		}
		for (const auto& m : g._sl) {
			if (const auto machine = IdOf(m.ref); machine && m.step != 0 && !m.you && m.id) {
				out.push_back({ kRosterSlots, machine, 0, m.id });
			}
		}
		if (!a_intfc->OpenRecord(kRosterType, kRosterVersion)) {
			return;
		}
		const auto n = static_cast<std::uint32_t>(out.size());
		a_intfc->WriteRecordData(&n, sizeof(n));
		for (const auto& e : out) {
			a_intfc->WriteRecordData(&e.kind, sizeof(e.kind));
			a_intfc->WriteRecordData(&e.table, sizeof(e.table));
			a_intfc->WriteRecordData(&e.seat, sizeof(e.seat));
			a_intfc->WriteRecordData(&e.actor, sizeof(e.actor));
		}
		log::info("co-save: {} at the tables written (dealers and players)", out.size());
	}

	void Game::LoadRoster(const F4SE::SerializationInterface* a_intfc, std::uint32_t a_version)
	{
		if (a_version != kRosterVersion) {
			return;
		}
		std::uint32_t n = 0;
		if (!a_intfc->ReadRecordData(&n, sizeof(n))) {
			return;
		}
		for (std::uint32_t i = 0; i < n; ++i) {
			RosterEntry e;
			if (!a_intfc->ReadRecordData(&e.kind, sizeof(e.kind)) || !a_intfc->ReadRecordData(&e.table, sizeof(e.table)) ||
				!a_intfc->ReadRecordData(&e.seat, sizeof(e.seat)) || !a_intfc->ReadRecordData(&e.actor, sizeof(e.actor))) {
				break;
			}
			const auto table = a_intfc->ResolveFormID(e.table);
			const auto actor = a_intfc->ResolveFormID(e.actor);
			if (table && actor) {
				e.table = *table;
				e.actor = *actor;
				s_roster.push_back(e);
			}
		}
		s_rosterLoaded = std::chrono::steady_clock::now();
		log::info("co-save: {} at the tables read (back in their places once the tables are up)", s_roster.size());
	}

	std::vector<std::int32_t> Game::RosterKeep()
	{
		std::vector<std::int32_t> keep;
		for (const auto& e : s_roster) {
			keep.push_back(static_cast<std::int32_t>(e.actor));
		}
		return keep;
	}

	RE::Actor* Game::RosterNext(std::uint8_t a_kind, RE::TESFormID a_table, std::int32_t& a_seat, bool a_dealer)
	{
		if (s_roster.empty()) {
			return nullptr;
		}
		if (std::chrono::steady_clock::now() - s_rosterLoaded > std::chrono::minutes(2)) {
			log::info("roster: {} not back at their tables two minutes after the load: let be", s_roster.size());
			s_roster.clear();
			return nullptr;
		}
		for (auto it = s_roster.begin(); it != s_roster.end(); ++it) {
			if (it->kind != a_kind || it->table != a_table || (a_dealer ? it->seat != -1 : it->seat < 0)) {
				continue;
			}
			const auto e = *it;
			s_roster.erase(it);
			auto* actor = RE::TESForm::GetFormByID<RE::Actor>(e.actor);
			if (!actor || actor->IsDisabled() || actor->IsDead(false) || !actor->Get3D()) {
				log::info("roster: {:08X} (seat {}) isn't about: not back", e.actor, e.seat);
				return RosterNext(a_kind, a_table, a_seat, a_dealer);
			}
			a_seat = e.seat;
			log::info("roster: {} ({:08X}) back to seat {} (as when the game was saved)", actor->GetDisplayFullName(), e.actor, e.seat);
			return actor;
		}
		return nullptr;
	}
}
