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
		constexpr float kArrived = 12.0f;        // this close to the spot: there (then put exactly on it)
		constexpr float kSettleIn = 2.5f;        // seconds standing at their spot before they start betting
		constexpr float kAtSpot = 40.0f;         // ...and only while they're this close to it
		constexpr float kStallResend = 6.0f;     // no closer in this long: the walk is sent again
		constexpr int   kMinBudget = 25;         // what a settler puts down in a round: 25 to 100 caps
		constexpr int   kMaxBudget = 100;
		constexpr float kSnap = 6.0f;            // arrived further than this from their spot: put exactly on it (well inside the walk package's 10: there, it's done)
		constexpr float kPersonalSpace = 55.0f;  // a spot with someone (anyone) standing this close isn't free

		// vanilla idles (Fallout4.esm IDLE)
		// (just the thinking idle: the shopping looks turn them about, as if browsing a shop's shelves)
		constexpr std::array<RE::TESFormID, 1> kWaitIdles{ 0x001E81AC };  // thinking
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
		// two along the players' side and two along the far side, by the layout, well apart; filled front, back,
		// front, back
		static constexpr std::array<std::pair<double, int>, kSpots> spots{ { { 17.0, -1 }, { 42.0, 1 }, { 89.0, -1 }, { 102.0, 1 } } };
		const auto& [x, side] = spots[static_cast<std::size_t>(a_spot) % spots.size()];
		const double y = side < 0 ? kPlayersEdgeY - kStepBack : kFarEdgeY + kStepBack;
		a_heading = HeadingOf(a_frame, 0.0, side < 0 ? 1.0 : -1.0);
		return ToWorld(a_frame, x, y, 0.0);
	}

	namespace
	{
		// (the turning log: what each held settler's doing, every half second)
		struct TurnWatch
		{
			float                                 yaw{ 0.0f };
			std::chrono::steady_clock::time_point at{};
			int                                   quiet{ 0 };
		};
		std::unordered_map<RE::TESFormID, TurnWatch> g_turns;
		// (the idle log: what each held settler's playing, every change; and which idles were ours, sent when)
		struct IdleWatch
		{
			RE::TESFormID                         current{ 0 }, last{ 0 };
			float                                 yaw{ 0.0f };
			std::chrono::steady_clock::time_point since{};
		};
		std::unordered_map<RE::TESFormID, IdleWatch> g_idles;

		std::string FormName(const RE::TESForm* a_form)
		{
			if (!a_form) {
				return "none";
			}
			const char* id = a_form->GetFormEditorID();
			return std::format("{:08X}{}{}", a_form->GetFormID(), id && *id ? " " : "", id ? id : "");
		}
	}

	void Game::WatchTurn(RE::Actor* a_actor, float a_heading)
	{
		// every idle a held settler starts, ours or the game's own, and what it did to their facing ([idle]): the
		// twist between our idles is whatever plays there
		if (a_actor && Settings::Get().logTurning) {
			auto*       proc = a_actor->currentProcess;
			auto*       mh = proc ? proc->middleHigh : nullptr;
			const auto  cur = mh && mh->currentIdle ? mh->currentIdle->GetFormID() : 0;
			const auto  lastPlayed = mh && mh->lastIdlePlayed ? mh->lastIdlePlayed->GetFormID() : 0;
			auto&       w = g_idles[a_actor->GetFormID()];
			const auto  now = std::chrono::steady_clock::now();
			if (cur != w.current || lastPlayed != w.last) {
				const float held = w.since.time_since_epoch().count() ? std::chrono::duration<float>(now - w.since).count() : 0.0f;
				const auto  ours = Npc::LastIdleSent(a_actor);
				const bool  isOurs = ours.first != 0 && (ours.first == cur || ours.first == lastPlayed) &&
				                    std::chrono::duration<float>(now - ours.second).count() < 3.0f;
				log::info("[idle] {} ({:08X}): now {} (last played {}), {}; the one before for {:.1f} s; facing {:.0f} (the table's {:.0f}, it turned {:+.0f} since)",
					a_actor->GetDisplayFullName() ? a_actor->GetDisplayFullName() : "?", a_actor->GetFormID(),
					cur ? FormName(mh->currentIdle) : std::string("none"), lastPlayed ? FormName(mh->lastIdlePlayed) : std::string("none"),
					isOurs ? "ours" : "NOT OURS (the game's own)", held, a_actor->data.angle.z * 57.2958f, a_heading * 57.2958f,
					util::AngleDiff(w.yaw, a_actor->data.angle.z) * 57.2958f);
				w.current = cur;
				w.last = lastPlayed;
				w.since = now;
				w.yaw = a_actor->data.angle.z;
			}
		}
		// What a settler held at a table is doing while they turn (Settings: bLogTurning): their facing against the
		// table's, how fast it's changing, the package they're running and its target, what their animation graph says
		// about turning and moving, what they're looking at. Logged every half second while they're turning or off the
		// table's heading, and once a minute anyway
		if (!a_actor || !Settings::Get().logTurning) {
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		auto&      w = g_turns[a_actor->GetFormID()];
		if (w.at.time_since_epoch().count() == 0) {
			w = { a_actor->data.angle.z, now, 0 };
			return;
		}
		const float secs = std::chrono::duration<float>(now - w.at).count();
		if (secs < 0.5f) {
			return;
		}
		const float yaw = a_actor->data.angle.z;
		const float rate = util::AngleDiff(w.yaw, yaw) / secs * 57.2958f;   // degrees a second
		const float off = util::AngleDiff(a_heading, yaw) * 57.2958f;        // degrees off the table's heading
		w.yaw = yaw;
		w.at = now;
		const bool turning = std::abs(rate) > 4.0f || std::abs(off) > 6.0f;
		if (!turning && ++w.quiet < 120) {
			return;
		}
		w.quiet = 0;
		std::string pkg = "none", target = "none", proc = "";
		if (auto* p = a_actor->currentProcess) {
			pkg = FormName(p->currentPackage.package);
			if (auto t = p->currentPackage.target.get()) {
				target = FormName(t.get());
				if (auto* base = t->GetObjectReference()) {
					target += " (" + FormName(base) + ")";
				}
			}
			proc = std::format(" procedure {}", p->currentPackage.currentProcedureIndex);
		}
		auto graphF = [&](const char* a_name) {
			float v = 0.0f;
			return a_actor->GetGraphVariableImplFloat(a_name, v) ? std::format("{:.2f}", v) : std::string("-");
		};
		auto graphB = [&](const char* a_name) {
			bool v = false;
			return a_actor->GetGraphVariableImplBool(a_name, v) ? (v ? "1" : "0") : "-";
		};
		log::info("[turn] {} ({:08X}): facing {:.0f}, the table's {:.0f} ({:+.0f} off), turning {:+.0f}/s; package {}{} at {}; "
				  "graph TurnDelta {} TurnDeltaDamped {} Speed {} Direction {} bAnimationDriven {} IsSyncIdle {}; in combat {}, "
				  "talking {}",
			a_actor->GetDisplayFullName() ? a_actor->GetDisplayFullName() : "?", a_actor->GetFormID(), yaw * 57.2958f, a_heading * 57.2958f,
			off, rate, pkg, proc, target, graphF("TurnDelta"), graphF("TurnDeltaDamped"), graphF("Speed"), graphF("Direction"),
			graphB("bAnimationDriven"), graphB("bIsSynced"), a_actor->IsInCombat(), a_actor->IsTalking());
	}

	void Game::FaceTowards(RE::Actor* a_actor, float a_heading)
	{
		WatchTurn(a_actor, a_heading);
		// (no longer turned back: snapped back to the table whenever they'd turned away with an idle, they stuttered
		// round. They're put on their spot facing the table when they get there, and left to it after that)
		(void)a_heading;
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
		if (const auto* you = RE::PlayerCharacter::GetSingleton(); you && you->IsInCombat()) {
			return;  // (nobody sits down in a fight)
		}
		const auto& s = Settings::Get();
		if (!s.bettors || static_cast<int>(_t.seats.size()) >= s.maxBettors || _t.phase != Phase::kBetting) {
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
				if (!Usable(actor) || actor == player || actor->IsChild() || actor->IsInCombat() || Npc::Busy(actor) || (player && actor->GetHostileToActor(player)) ||
					(npcType && !actor->HasKeywordHelper(npcType, nullptr)) ||
					std::ranges::any_of(_t.seats, [&](const Seat& st) { return st.id == actor->GetFormID(); }) ||
					std::ranges::any_of(_other.seats, [&](const Seat& st) { return st.id == actor->GetFormID(); }) ||  // (at the other table)
					PkHasSeat(actor->GetFormID()) ||                                                                 // (playing poker)
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
		for (int i = 0; i < kSpots && spot < 0; ++i) {
			if (std::ranges::any_of(_t.seats, [&](const Seat& st) { return st.spot == i; })) {
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
		_t.seats.push_back(std::move(seat));
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
		_t.seatTimer -= a_dt;
		if (_t.seatTimer <= 0.0f) {
			_t.seatTimer = s.bettorEvery * std::uniform_real_distribution<float>(0.7f, 1.3f)(_rng);
			FindBettor(a_frame);
		}
		std::uniform_real_distribution<float> U(0.0f, 1.0f);
		for (auto& seat : _t.seats) {
			auto  ref = seat.actor.get();
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			// (a fight: everyone up. Held, their AI is off and they'd not notice it themselves)
			const auto* you = RE::PlayerCharacter::GetSingleton();
			if (!Usable(actor) || actor->IsInCombat() || (you && you->IsInCombat()) || Npc::AtPool(actor)) {  // (or called over to pool)
				seat.step = Seat::Step::kLeaving;
				continue;
			}
			float      heading = 0.0f;
			const auto spot = SeatSpot(a_frame, seat.spot, heading);
			switch (seat.step) {
			case Seat::Step::kWalking:
				if (WalkTo(actor, seat.walk, a_frame, spot, heading, a_dt, false, 40.0f)) {
					seat.step = Seat::Step::kPlaying;
					// (put exactly on their spot and turned to the table, always: a step put, their walk's last stride and its
					// turn to face are cut off, where they'd turn hard while the table held them facing it: the spin the
					// turning log showed on arriving, turning 100-180 degrees a second for a moment)
					// (their walk package stays on, linked to the spot they're now on: its travel done, it holds them there.
					// Unlinked, as it was, it went on travelling with nowhere to go: stepping and turning on the spot, the
					// turning log showed, procedure 0 at speed 15)
					if (auto marker = seat.walk.marker.get(); marker && Distance2D(actor->data.location, spot) > 0.5f) {
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
				// standing at their spot for a moment before they play (not while still coming up to it)
				if (Distance2D(actor->data.location, spot) < kAtSpot) {
					seat.atSpot += a_dt;
				} else {
					seat.atSpot = 0.0f;
				}
				if (seat.reactIn >= 0.0f) {
					seat.reactIn -= a_dt;
					if (seat.reactIn < 0.0f) {
						SeatIdle(seat, actor, seat.roundNet > 0 ? 2 : seat.roundNet < 0 ? 3 : 4);
						seat.nextIdle = std::uniform_real_distribution<float>(8.0f, 16.0f)(_rng);
					}
				} else if (_t.phase != Phase::kResult) {
					seat.nextIdle -= a_dt;
					if (seat.nextIdle <= 0.0f) {
						seat.nextIdle = std::uniform_real_distribution<float>(9.0f, 22.0f)(_rng);
						SeatIdle(seat, actor, 0);
					}
				}
				if (_t.phase == Phase::kBetting) {
					if (seat.rounds <= 0 || Purse(seat.id) + OnTable(seat.id) < 1) {
						if (!HasBets(seat.id)) {
							Say(std::format("{} {}", Upper(seat.name), Purse(seat.id) < 1 ? "IS OUT OF CAPS" : "CALLS IT A NIGHT"));
							seat.step = Seat::Step::kLeaving;
						}
						break;
					}
					if (seat.atSpot < kSettleIn) {
						break;
					}
					if (!seat.betThisRound && _t.bettingTime > 2.0f) {
						SeatBets(seat);
					}
					if (seat.chipsToPlace > 0 && seat.budget > 0) {
						seat.nextChip -= a_dt;
						if (seat.nextChip <= 0.0f) {
							seat.nextChip = std::uniform_real_distribution<float>(2.0f, 4.0f)(_rng);
							--seat.chipsToPlace;
							// the bet: even money half the time, dozens and columns a fifth, the rest on numbers
							const auto all = rl::AllBets(_t.wheel);
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
		for (auto& seat : _t.seats) {
			if (seat.step == Seat::Step::kLeaving) {
				ReleaseSeat(seat, nullptr);
			}
		}
		std::erase_if(_t.seats, [](const Seat& st) { return st.step == Seat::Step::kLeaving; });
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
		if (_t.phase == Phase::kBetting) {
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
