// The settlers who come over to bet, each at their own spot round the layout, with a few vanilla idles while they play.
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
		constexpr int   kMinBudget = 25;         // what a settler puts down in a round: 25 to 100 caps
		constexpr int   kMaxBudget = 100;
		constexpr float kSnap = 12.0f;           // arrived further than this from their spot: put exactly on it
		constexpr float kPersonalSpace = 55.0f;  // a spot with someone (anyone) standing this close isn't free

		// vanilla idles (Fallout4.esm IDLE)
		constexpr std::array<RE::TESFormID, 4> kWaitIdles{ 0x0001F86C, 0x0001FA11, 0x0001FA12, 0x001E81AC };  // shopping looks A-C, thinking
		constexpr RE::TESFormID                kPlaceIdle = 0x001A8A0D;  // IdlePlaceEnvelopeOnTable
		constexpr std::array<RE::TESFormID, 2> kWinIdles{ 0x0010BA3A, 0x0010F9C0 };  // cheering, fight winner
		constexpr RE::TESFormID                kLoseIdle = 0x00038C7A;               // HeadShakeNo
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
		std::unordered_map<RE::TESFormID, int>     g_purses;          // an NPC's caps for roulette

	int Game::Purse(RE::TESFormID a_id)
	{
		std::scoped_lock lock(g_npcMutex);
		auto it = g_purses.find(a_id);
		if (it == g_purses.end()) {
			// a settler's first visit: 250 to 1000 caps, fixed per NPC
			it = g_purses.emplace(a_id, 250 + static_cast<int>(Hash(a_id) % 751)).first;
		}
		return it->second;
	}

	void Game::AddToPurse(RE::TESFormID a_id, int a_caps)
	{
		Purse(a_id);
		std::scoped_lock lock(g_npcMutex);
		g_purses[a_id] = std::max(0, g_purses[a_id] + a_caps);
	}

	// ---------------------------------------------------------------- settlers walking over

	RE::NiPoint3 Game::SeatSpot(const Frame& a_frame, int a_spot, float& a_heading) const
	{
		using namespace rl::table;
		// three along the players' side, two on the far side, all by the layout and a body's width apart
		static constexpr std::array<std::pair<double, int>, 5> spots{ { { 62.0, -1 }, { 0.0, -1 }, { 124.0, -1 }, { 30.0, 1 }, { 95.0, 1 } } };
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
					(npcType && !actor->HasKeywordHelper(npcType, nullptr)) ||
					std::ranges::any_of(_seats, [&](const Seat& st) { return st.id == actor->GetFormID(); }) ||
					std::ranges::any_of(found, [&](const auto& f) { return f.second == actor; })) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				// (only settlers and the like who are someone's: anyone with a name and nobody's enemy); broke since
				// their last visit: a while later, some caps again
				if (Purse(actor->GetFormID()) < kMinBudget) {
					AddToPurse(actor->GetFormID(), 150 + static_cast<int>(Hash(actor->GetFormID() ^ 0x5EED) % 250));
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
		// a free spot: nobody else's, and nobody (a settler sandboxing there, you) standing on it
		int spot = -1;
		for (int i = 0; i < 5 && spot < 0; ++i) {
			if (std::ranges::any_of(_seats, [&](const Seat& st) { return st.spot == i; })) {
				continue;
			}
			float      heading = 0.0f;
			const auto at = SeatSpot(a_frame, i, heading);
			bool       taken = false;
			util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
				a_cell->ForEachReferenceInRange(at, kPersonalSpace, [&](RE::TESObjectREFR* a_ref) {
					if (a_ref && a_ref != actor && a_ref->As<RE::Actor>() && !a_ref->IsDisabled()) {
						taken = true;
						return RE::BSContainer::ForEachResult::kStop;
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			});
			if (!taken) {
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
		// 25 to 100 caps this round, in one to three bets, on the kind of bets people like: mostly the outside, now
		// and then a number
		a_seat.betThisRound = true;
		a_seat.budget = std::min(Purse(a_seat.id), std::uniform_int_distribution<int>(kMinBudget, kMaxBudget)(_rng));
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
					if (auto marker = seat.walk.marker.get(); marker && Distance2D(actor->data.location, spot) > kSnap) {
						Npc::MoveTo(actor, marker.get());  // stopped short (maybe at someone's elbow): onto their own spot
					}
					seat.nextIdle = std::uniform_real_distribution<float>(4.0f, 10.0f)(_rng);
				}
				break;
			case Seat::Step::kPlaying:
				if (!seat.held) {
					seat.held = Npc::SetRestrained(actor, true);
				}
				FaceTowards(actor, heading);
				if (seat.reactIn >= 0.0f) {
					seat.reactIn -= a_dt;
					if (seat.reactIn < 0.0f) {
						SeatIdle(seat, actor, seat.roundNet > 0 ? 2 : seat.roundNet < 0 ? 3 : 4);
						seat.nextIdle = std::uniform_real_distribution<float>(8.0f, 16.0f)(_rng);
					}
				} else if (_phase != Phase::kResult) {
					seat.nextIdle -= a_dt;
					if (seat.nextIdle <= 0.0f) {
						seat.nextIdle = std::uniform_real_distribution<float>(9.0f, 22.0f)(_rng);
						SeatIdle(seat, actor, 0);
					}
				}
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
					if (seat.chipsToPlace > 0 && seat.budget > 0) {
						seat.nextChip -= a_dt;
						if (seat.nextChip <= 0.0f) {
							seat.nextChip = std::uniform_real_distribution<float>(2.0f, 4.0f)(_rng);
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
								// this bet's share of the round's budget, in 5s (the last bet takes what's left)
								int amount = seat.budget;
								if (seat.chipsToPlace > 0) {
									amount = std::max(5, (seat.budget / (seat.chipsToPlace + 1)) / 5 * 5);
								}
								amount = std::min({ amount, seat.budget, Purse(seat.id), Settings::Get().maxBet });
								if (amount > 0 && AddChips(seat.id, *bet, amount)) {
									seat.budget -= amount;
									Say(std::format("{}: {} ON {}", Upper(seat.name), amount, bet->Name()));
									if (std::uniform_real_distribution<float>(0.0f, 1.0f)(_rng) < 0.5f) {
										SeatIdle(seat, actor, 1);
									}
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

	void Game::SeatIdle(Seat& a_seat, RE::Actor* a_actor, int a_kind)
	{
		if (!Settings::Get().bettorIdles || !a_actor || !a_seat.held) {
			return;
		}
		auto pick = [&](const auto& a_list) { return a_list[std::uniform_int_distribution<std::size_t>(0, a_list.size() - 1)(_rng)]; };
		RE::TESFormID idle = 0;
		switch (a_kind) {
		case 0: idle = pick(kWaitIdles); break;
		case 1: idle = kPlaceIdle; break;
		case 2: idle = pick(kWinIdles); break;
		case 3: idle = kLoseIdle; break;
		default: break;
		}
		if (idle) {
			Npc::PlayIdle(a_actor, idle);
		}
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
