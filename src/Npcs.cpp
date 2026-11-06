// The dealer (a settler you pick, who comes to the table in play and spins) and the settlers who come over to bet.
// Walking them there is fo4roulette.esl's quest package (Npc.h); once there they're held with SetRestrained and
// kept facing the table.

#include "Game.h"

#include "Forms.h"
#include "Npc.h"
#include "Settings.h"
#include "Util.h"
#include "rl/Table.h"

namespace fr
{
	namespace
	{
		constexpr float kArrived = 45.0f;        // this close to the spot: there
		constexpr float kStallResend = 6.0f;     // no closer in this long: the walk is sent again
		constexpr float kSummonRange = 6000.0f;  // a dealer further from the table than this isn't called over
		constexpr float kStepBack = 32.0f;       // where people stand: this far out from the table's edge

		float Distance2D(const RE::NiPoint3& a, const RE::NiPoint3& b) { return std::hypot(a.x - b.x, a.y - b.y); }

		std::string Upper(std::string a_s)
		{
			std::ranges::transform(a_s, a_s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			return a_s;
		}

		std::uint32_t Hash(std::uint32_t a)
		{
			a = a * 0x9E3779B1u + 0x7F4A7C15u;
			a ^= a >> 13;
			a *= 0x85EBCA6Bu;
			a ^= a >> 16;
			return a;
		}

		bool Usable(RE::Actor* a_actor)
		{
			return a_actor && !a_actor->IsDisabled() && !a_actor->IsDead(false) && a_actor->Get3D();
		}
	}

	// shared with Save.cpp
	std::mutex                                 g_npcMutex;
	std::unordered_map<RE::TESFormID, RE::TESFormID> g_dealers;  // table -> the settler who deals there
	std::unordered_map<RE::TESFormID, int>     g_purses;          // an NPC's caps for roulette

	int Game::Purse(RE::TESFormID a_id)
	{
		std::scoped_lock lock(g_npcMutex);
		auto it = g_purses.find(a_id);
		if (it == g_purses.end()) {
			// a settler's first visit: 40 to 240 caps, fixed per NPC
			it = g_purses.emplace(a_id, 40 + static_cast<int>(Hash(a_id) % 201)).first;
		}
		return it->second;
	}

	void Game::AddToPurse(RE::TESFormID a_id, int a_caps)
	{
		Purse(a_id);
		std::scoped_lock lock(g_npcMutex);
		g_purses[a_id] = std::max(0, g_purses[a_id] + a_caps);
	}

	// ---------------------------------------------------------------- the dealer

	RE::Actor* Game::PickActor() const
	{
		// the NPC under the crosshair, within 600
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* camera = RE::PlayerCamera::GetSingleton();
		if (!player || !camera || !camera->cameraRoot) {
			return nullptr;
		}
		const auto         origin = camera->cameraRoot->GetWorldTranslate();
		const float        pitch = player->data.angle.x;
		const float        yaw = player->data.angle.z;
		const RE::NiPoint3 dir{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) };
		constexpr float    kReach = 600.0f;
		RE::Actor*         best = nullptr;
		float              bestT = std::numeric_limits<float>::max();
		util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(origin, kReach + 100.0f, [&](RE::TESObjectREFR* a_ref) {
				auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
				if (!Usable(actor) || actor == player) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const auto& b = actor->Get3D()->worldBound;
				const auto  oc = b.center - origin;
				const float t = oc.Dot(dir);
				const float miss2 = oc.SqrLength() - t * t;
				const float reach = std::max(30.0f, b.fRadius * 0.7f);
				if (t > 0.0f && t < kReach + b.fRadius && miss2 <= reach * reach && t < bestT) {
					bestT = t;
					best = actor;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		return best;
	}

	void Game::AssignDealer()
	{
		auto* actor = PickActor();
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!actor || !player) {
			Notify("Roulette: look at a settler to make them the dealer");
			return;
		}
		static auto* npcType = RE::TESForm::GetFormByID<RE::BGSKeyword>(Forms::kActorTypeNPC);
		if (actor->IsChild() || actor->GetHostileToActor(player) || (npcType && !actor->HasKeywordHelper(npcType, nullptr))) {
			Notify(std::format("Roulette: {} can't deal", actor->GetDisplayFullName()));
			return;
		}
		// the table: the one in play, or the nearest roulette table to them
		RE::TESObjectREFR* table = _table.get().get();
		if (!table) {
			float best = 3000.0f;
			for (const auto& h : _nearTables) {
				if (auto t = h.get(); t && (t->data.location - actor->data.location).Length() < best) {
					best = (t->data.location - actor->data.location).Length();
					table = t.get();
				}
			}
		}
		if (!table) {
			Notify("Roulette: no roulette table nearby (build one from the workshop's Furniture > Tables)");
			return;
		}
		const RE::TESFormID tableID = table->GetFormID();
		const RE::TESFormID actorID = actor->GetFormID();
		std::string         name = actor->GetDisplayFullName();
		bool                stop = false;
		{
			std::scoped_lock lock(g_npcMutex);
			auto it = g_dealers.find(tableID);
			stop = it != g_dealers.end() && it->second == actorID;
			if (stop) {
				g_dealers.erase(it);
			} else {
				// one table each
				for (auto i = g_dealers.begin(); i != g_dealers.end();) {
					i = i->second == actorID ? g_dealers.erase(i) : std::next(i);
				}
				g_dealers[tableID] = actorID;
			}
		}
		// a bettor at the table who's now the dealer stops betting
		for (auto& seat : _seats) {
			if (seat.id == actorID) {
				ReleaseSeat(seat, nullptr);
			}
		}
		std::erase_if(_seats, [&](const Seat& s) { return s.id == actorID; });
		if (_table.get().get() == table || (stop && _dealer.id == actorID)) {
			ReleaseDealer();
			_dealer = {};
			if (!stop) {
				LoadDealerFor(table);
			}
		}
		Notify(stop ? std::format("Roulette: {} stops dealing", name) : std::format("Roulette: {} is the dealer now", name));
	}

	void Game::LoadDealerFor(RE::TESObjectREFR* a_table)
	{
		RE::TESFormID id = 0;
		{
			std::scoped_lock lock(g_npcMutex);
			if (auto it = g_dealers.find(a_table->GetFormID()); it != g_dealers.end()) {
				id = it->second;
			}
		}
		if (!id) {
			return;
		}
		_dealer = {};
		_dealer.id = id;
		auto* actor = RE::TESForm::GetFormByID<RE::Actor>(id);
		_dealer.name = actor ? std::string(actor->GetDisplayFullName()) : std::string("the dealer");
		if (actor) {
			_dealer.actor = actor->GetHandle();
		}
		log::info("dealer here: {} ({:08X})", _dealer.name, id);
	}

	RE::Actor* Game::DealerActor() const
	{
		auto ref = _dealer.actor.get();
		auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
		return Usable(actor) ? actor : nullptr;
	}

	bool Game::DealerReady() const
	{
		return _dealer.id && _dealer.arrived && DealerActor();
	}

	RE::NiPoint3 Game::DealerSpot(const Frame& a_frame, float& a_heading) const
	{
		// at the wheel's end of the table, behind the wheel, facing down the layout
		a_heading = HeadingOf(a_frame, 1.0, 0.0);
		return ToWorld(a_frame, -rl::table::kHalfLength - kStepBack, rl::table::kWheelY, 0.0);
	}

	RE::NiPoint3 Game::SeatSpot(const Frame& a_frame, int a_spot, float& a_heading) const
	{
		using namespace rl::table;
		// four along the players' side, then two on the far side, all by the layout
		static constexpr std::array<std::pair<double, int>, 6> spots{ { { 50.0, -1 }, { 85.0, -1 }, { 15.0, -1 }, { 118.0, -1 }, { 60.0, 1 },
			{ 105.0, 1 } } };
		const auto& [x, side] = spots[static_cast<std::size_t>(a_spot) % spots.size()];
		const double y = side < 0 ? kPlayersEdgeY - kStepBack : kFarEdgeY + kStepBack;
		a_heading = HeadingOf(a_frame, 0.0, side < 0 ? 1.0 : -1.0);
		return ToWorld(a_frame, x, y, 0.0);
	}

	void Game::FaceTowards(RE::Actor* a_actor, float a_heading)
	{
		if (a_actor && std::abs(util::AngleDiff(a_actor->data.angle.z, a_heading)) > 0.03f) {
			a_actor->SetHeading(a_heading);
		}
	}

	bool Game::WalkTo(RE::Actor* a_actor, Walker& a_walk, const Frame& a_frame, const RE::NiPoint3& a_spot, float a_heading, float a_dt,
		bool a_dealer, float a_timeout)
	{
		(void)a_frame;
		auto marker = a_walk.marker.get();
		if (!marker) {
			// the spot, on the floor where they stand: their package walks them to it
			RE::NiPoint3 spot = a_spot;
			spot.z = a_actor->data.location.z;
			a_walk.marker = Spawn(Forms::Marker(), spot, a_heading);
			marker = a_walk.marker.get();
			if (!marker) {
				return false;
			}
			if (Npc::Joined(a_actor)) {
				Npc::Walk(a_actor, marker.get(), false);
			}
			a_walk.joinSent = false;
		}
		if (!a_walk.joinSent) {
			a_walk.joinSent = true;
			if (Npc::Ready()) {
				Npc::Join(a_actor, marker.get(), a_dealer);
			}
		}
		const float d = Distance2D(a_actor->data.location, a_spot);
		if (d < kArrived) {
			return true;
		}
		a_walk.walked += a_dt;
		if (d < a_walk.best - 10.0f) {
			a_walk.best = d;
			a_walk.stall = 0.0f;
		} else {
			a_walk.stall += a_dt;
		}
		// sat down somewhere on the way (their sandbox's doing): up
		a_walk.getUp -= a_dt;
		if (auto* proc = a_actor->currentProcess; proc && proc->GetOccupiedFurniture() && a_walk.getUp <= 0.0f) {
			a_walk.getUp = 3.0f;
			a_actor->InitiateGetUpPackage();
		}
		if (a_walk.stall > kStallResend) {
			log::info("{}: no closer to the table in {:.0f}s ({:.0f} away, package {}); sending the walk again", a_actor->GetDisplayFullName(),
				a_walk.stall, d, Npc::PackageName(a_actor));
			a_walk.stall = 0.0f;
			a_walk.best = d;
			if (Npc::Joined(a_actor)) {
				Npc::Walk(a_actor, marker.get(), true);
			} else {
				a_walk.joinSent = false;
			}
		}
		if (a_walk.walked > a_timeout) {
			// a last resort (tight spaces round a table can't always be walked): put them there
			log::info("{}: still {:.0f} from the table after {:.0f}s; put there", a_actor->GetDisplayFullName(), d, a_walk.walked);
			Npc::MoveTo(a_actor, marker.get());
			a_walk.walked = 0.0f;
		}
		return false;
	}

	void Game::TickDealer(const Frame& a_frame, float a_dt)
	{
		if (!_dealer.id) {
			return;
		}
		auto* actor = DealerActor();
		if (!actor) {
			if (!_dealer.actor) {
				if (auto* a = RE::TESForm::GetFormByID<RE::Actor>(_dealer.id)) {
					_dealer.actor = a->GetHandle();  // (loaded since)
				}
			}
			_dealer.arrived = false;
			return;
		}
		if (actor->IsInCombat()) {
			if (_dealer.arrived || _dealer.held) {
				ReleaseDealer();
				_dealer.arrived = false;
				_dealer.walk = {};
				Say(std::format("{} HAS BETTER THINGS TO DO", Upper(_dealer.name)), 2);
			}
			return;
		}
		if (Distance2D(actor->data.location, a_frame.origin) > kSummonRange && !_dealer.walk.joinSent) {
			return;  // somewhere else entirely
		}
		float      heading = 0.0f;
		const auto spot = DealerSpot(a_frame, heading);
		if (!_dealer.arrived) {
			if (WalkTo(actor, _dealer.walk, a_frame, spot, heading, a_dt, true, Settings::Get().dealerWalkTimeout)) {
				_dealer.arrived = true;
				Npc::Arrived(actor);
				Say(std::format("{} IS DEALING", Upper(_dealer.name)), 1);
			}
			return;
		}
		if (!_dealer.held) {
			_dealer.held = Npc::SetRestrained(actor, true);
		}
		FaceTowards(actor, heading);
		if (Distance2D(actor->data.location, spot) > kArrived * 3.0f) {
			// pushed off the spot (or the table was moved in the workshop): back they go
			if (_dealer.held) {
				Npc::SetRestrained(actor, false);
				_dealer.held = false;
			}
			_dealer.arrived = false;
			Remove(_dealer.walk.marker);
			_dealer.walk = {};
		}
	}

	void Game::ReleaseDealer()
	{
		auto* actor = DealerActor();
		if (actor) {
			if (_dealer.held) {
				Npc::SetRestrained(actor, false);
			}
			if (_dealer.watching) {
				Npc::ClearLookAt(actor);
			}
			Npc::Leave(actor);
		}
		_dealer.held = false;
		_dealer.watching = false;
		_dealer.arrived = false;
		Remove(_dealer.walk.marker);
		_dealer.walk = {};
	}

	// ---------------------------------------------------------------- settlers betting

	void Game::FindBettor(const Frame& a_frame)
	{
		const auto& s = Settings::Get();
		if (!s.bettors || static_cast<int>(_seats.size()) >= s.maxBettors || _phase != Phase::kBetting) {
			return;
		}
		if (std::uniform_real_distribution<float>(0.0f, 1.0f)(_rng) > 0.6f) {
			return;  // not this time
		}
		auto*        player = RE::PlayerCharacter::GetSingleton();
		static auto* npcType = RE::TESForm::GetFormByID<RE::BGSKeyword>(Forms::kActorTypeNPC);
		std::vector<std::pair<float, RE::Actor*>> found;
		util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(a_frame.origin, s.bettorRange, [&](RE::TESObjectREFR* a_ref) {
				auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
				if (!Usable(actor) || actor == player || actor->IsChild() || actor->IsInCombat() || (player && actor->GetHostileToActor(player)) ||
					(npcType && !actor->HasKeywordHelper(npcType, nullptr)) || actor->GetFormID() == _dealer.id ||
					std::ranges::any_of(_seats, [&](const Seat& st) { return st.id == actor->GetFormID(); }) ||
					std::ranges::any_of(found, [&](const auto& f) { return f.second == actor; })) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				// (only settlers and the like who are someone's: anyone with a name and nobody's enemy)
				if (Purse(actor->GetFormID()) < 5) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				found.emplace_back((actor->data.location - a_frame.origin).Length(), actor);
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		if (found.empty()) {
			return;
		}
		std::ranges::sort(found, {}, &std::pair<float, RE::Actor*>::first);
		const std::size_t pick = std::uniform_int_distribution<std::size_t>(0, std::min<std::size_t>(found.size(), 4) - 1)(_rng);
		auto*             actor = found[pick].second;
		// a free spot
		int spot = -1;
		for (int i = 0; i < 6 && spot < 0; ++i) {
			if (std::ranges::none_of(_seats, [&](const Seat& st) { return st.spot == i; })) {
				spot = i;
			}
		}
		if (spot < 0) {
			return;
		}
		Seat seat;
		seat.actor = actor->GetHandle();
		seat.id = actor->GetFormID();
		seat.name = actor->GetDisplayFullName();
		seat.spot = spot;
		seat.rounds = std::uniform_int_distribution<int>(s.bettorMinRounds, s.bettorMaxRounds)(_rng);
		log::info("{} ({:08X}) comes to play: {} caps, {} rounds, spot {}", seat.name, seat.id, Purse(seat.id), seat.rounds, spot);
		Say(std::format("{} COMES TO PLAY", Upper(seat.name)));
		_seats.push_back(std::move(seat));
	}

	void Game::SeatBets(Seat& a_seat)
	{
		// one to three chips this round, on the kind of bets people like: mostly the outside, now and then a number
		a_seat.betThisRound = true;
		a_seat.chipsToPlace = std::uniform_int_distribution<int>(1, 3)(_rng);
		a_seat.nextChip = std::uniform_real_distribution<float>(0.5f, 2.5f)(_rng);
	}

	void Game::TickSeats(const Frame& a_frame, float a_dt)
	{
		const auto& s = Settings::Get();
		_seatTimer -= a_dt;
		if (_seatTimer <= 0.0f) {
			_seatTimer = s.bettorEvery * std::uniform_real_distribution<float>(0.7f, 1.3f)(_rng);
			FindBettor(a_frame);
		}
		std::uniform_real_distribution<float> U(0.0f, 1.0f);
		for (auto& seat : _seats) {
			auto  ref = seat.actor.get();
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (!Usable(actor) || actor->IsInCombat()) {
				seat.step = Seat::Step::kLeaving;
				continue;
			}
			float      heading = 0.0f;
			const auto spot = SeatSpot(a_frame, seat.spot, heading);
			switch (seat.step) {
			case Seat::Step::kWalking:
				if (WalkTo(actor, seat.walk, a_frame, spot, heading, a_dt, false, 40.0f)) {
					seat.step = Seat::Step::kPlaying;
					Npc::Arrived(actor);
				}
				break;
			case Seat::Step::kPlaying:
				if (!seat.held) {
					seat.held = Npc::SetRestrained(actor, true);
				}
				FaceTowards(actor, heading);
				if (_phase == Phase::kBetting) {
					if (seat.rounds <= 0 || Purse(seat.id) + OnTable(seat.id) < 1) {
						if (!HasBets(seat.id)) {
							Say(std::format("{} {}", Upper(seat.name), Purse(seat.id) < 1 ? "IS OUT OF CAPS" : "CALLS IT A NIGHT"));
							seat.step = Seat::Step::kLeaving;
						}
						break;
					}
					if (!seat.betThisRound && _bettingTime > 2.0f) {
						SeatBets(seat);
					}
					if (seat.chipsToPlace > 0) {
						seat.nextChip -= a_dt;
						if (seat.nextChip <= 0.0f) {
							seat.nextChip = std::uniform_real_distribution<float>(0.8f, 2.2f)(_rng);
							--seat.chipsToPlace;
							// the bet: even money half the time, dozens and columns a fifth, the rest on numbers
							const auto all = rl::AllBets(_wheel);
							const float roll = U(_rng);
							std::vector<const rl::Bet*> pool;
							for (const auto& b : all) {
								const int pays = b.Payout();
								if ((roll < 0.5f && pays == 1) || (roll >= 0.5f && roll < 0.7f && pays == 2) || (roll >= 0.7f && pays > 2)) {
									pool.push_back(&b);
								}
							}
							if (!pool.empty()) {
								const auto* bet = pool[std::uniform_int_distribution<std::size_t>(0, pool.size() - 1)(_rng)];
								const int   purse = Purse(seat.id);
								static constexpr std::array<int, 6> sizes{ 1, 2, 5, 5, 10, 25 };
								int amount = sizes[std::uniform_int_distribution<std::size_t>(0, sizes.size() - 1)(_rng)];
								amount = std::min({ amount, purse, std::max(1, purse / 4) });
								if (amount > 0 && AddChips(seat.id, *bet, amount)) {
									Say(std::format("{}: {} ON {}", Upper(seat.name), amount, bet->Name()));
								}
							}
						}
					}
				}
				break;
			case Seat::Step::kLeaving:
				break;
			}
		}
		for (auto& seat : _seats) {
			if (seat.step == Seat::Step::kLeaving) {
				ReleaseSeat(seat, nullptr);
			}
		}
		std::erase_if(_seats, [](const Seat& st) { return st.step == Seat::Step::kLeaving; });
	}

	void Game::ReleaseSeat(Seat& a_seat, const char* a_why)
	{
		auto  ref = a_seat.actor.get();
		auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
		if (_phase == Phase::kBetting) {
			TakeAllBack(a_seat.id);
		}
		if (actor) {
			if (a_seat.held) {
				Npc::SetRestrained(actor, false);
			}
			Npc::Leave(actor);
		}
		a_seat.held = false;
		Remove(a_seat.walk.marker);
		a_seat.step = Seat::Step::kLeaving;
		if (a_why) {
			log::info("{} leaves the table: {}", a_seat.name, a_why);
		}
	}
}
