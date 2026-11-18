// The blackjack table: WastelandWagers.esl's Blackjack Table, the rules in bj/Blackjack.h (six decks from the shoe, the
// dealer stands on every 17, blackjack pays 3 to 2, insurance 2 to 1, double on any first two cards, split once). One
// blackjack table is in play at a time, the nearest within reach. Seven places round the arc: you (activate the table:
// the place nearest you) and settlers with their own caps (the roulette purses); a settler stands behind the chip rack
// and deals, when there's one free (the table deals itself when there isn't).
//
// The table's space (tools/build_blackjack.py): X across (the shoe at +X, the discard holder at -X), Y towards the
// dealer (the chip rack along +Y's edge), Z up, the felt 59.875 up. The players' places are round an arc whose middle
// is (0, kArcY): a betting circle, and the card box in front of it, towards the dealer (make_blackjack.py's felt).
// The cards are the 52 card models, slid out of the shoe onto the card boxes and turned over; the bets are chip stacks
// on the circles.

#include "Game.h"

#include "Audio.h"
#include "Forms.h"
#include "Npc.h"
#include "Settings.h"
#include "Util.h"
#include "rl/Layout.h"
#include "rl/Table.h"

namespace fr
{
	extern std::mutex                             g_npcMutex;
	extern std::unordered_map<RE::TESFormID, int> g_purses;

	namespace
	{
		constexpr float kFeltZ = 59.875f;
		constexpr float kArcY = 24.08f;      // the players' arc's middle
		constexpr float kEdgeR = 72.8f;      // the table's edge round it
		constexpr float kStandR = 94.0f;     // where the players stand
		constexpr float kCardBoxR = 44.65f;  // the card boxes
		constexpr float kInsureR = 36.0f;    // the insurance line
		constexpr float kCardT = 0.06f;
		constexpr float kChipH = static_cast<float>(rl::table::kChipHeight);
		constexpr float kLift = 0.08f;
		constexpr float kStep = kChipH + 0.05f;
		constexpr float kArrived = 30.0f;
		constexpr float kPersonalSpace = 50.0f;
		// the betting circles, left to right from the players' side (make_blackjack.py's SPOTS)
		constexpr std::array<std::pair<float, float>, 7> kSpots{ {
			{ -53.20f, 14.76f }, { -43.34f, -8.16f }, { -24.20f, -24.20f }, { -0.05f, -29.93f }, { 24.24f, -24.15f }, { 43.38f, -8.16f },
			{ 53.24f, 14.76f },
		} };
		const RE::NiPoint3 kShoe{ 31.5f, 29.5f, 0.0f };     // where the cards come out of the shoe
		const RE::NiPoint3 kDiscard{ -40.1f, 37.5f, 0.0f }; // the discard holder
		const RE::NiPoint3 kRack{ 0.0f, 43.5f, 0.0f };      // the dealer's chip rack
		constexpr float    kDealerRowY = 31.0f;              // the dealer's cards, in a row in front of the rack
		constexpr float    kDealerStand = 66.0f;             // the dealer stands behind the rack, facing the players
		constexpr float    kDealGap = 0.32f;                 // seconds between the cards going out
		constexpr float    kBetWindow = 12.0f;               // seconds for bets once the first one's down
		constexpr std::array<RE::TESFormID, 2> kWinIdles{ 0x0010BA3A, 0x0010F9C0 };  // cheering, fight winner
		// sitting (the chairs option): the poker table's chair, out from the edge as there; their seated cheers
		constexpr RE::TESFormID                kChair = 0x000E37D3;  // NpcChairIndustrialMetalDiningSit01
		constexpr float                        kChairOut = 20.0f;
		constexpr std::array<RE::TESFormID, 1> kSeatedWin{ 0x0010BA3B };

		float Distance2D(const RE::NiPoint3& a, const RE::NiPoint3& b) { return std::hypot(a.x - b.x, a.y - b.y); }

		std::string Upper(std::string a_s)
		{
			std::ranges::transform(a_s, a_s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			return a_s;
		}

		std::string Commas(int a_n)
		{
			std::string s = std::to_string(std::abs(a_n));
			for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) {
				s.insert(static_cast<std::size_t>(i), ",");
			}
			return a_n < 0 ? "-" + s : s;
		}

		bool InFreeCamera()
		{
			auto* camera = RE::PlayerCamera::GetSingleton();
			auto* free = camera ? camera->cameraStates[RE::CameraStates::kFree].get() : nullptr;
			return camera && free && camera->currentState.get() == free;
		}

		bool Usable(RE::Actor* a_actor)
		{
			return a_actor && !a_actor->IsDisabled() && !a_actor->IsDead(false) && a_actor->Get3D() &&
			       !RE::PowerArmor::ActorInPowerArmor(*a_actor);  // (nobody in power armour plays or deals)
		}

		// (as at the poker table: a still chip or card's last frame's place set to where it is, so the upscaler and frame
		// generation don't see it moving and shake it; and no fading in or out)
		void Still(RE::NiAVObject* a_obj)
		{
			if (!a_obj) {
				return;
			}
			a_obj->previousWorld = a_obj->world;
			if (auto* node = a_obj->IsNode()) {
				for (auto& child : node->children) {
					Still(child.get());
				}
			}
		}

		void NoFade(const RE::ObjectRefHandle& a_handle)
		{
			if (auto ref = a_handle.get()) {
				if (auto* root = ref->Get3D()) {
					if (auto* fade = root->IsFadeNode()) {
						fade->currentFade = 1.0f;
						fade->currentDecalFade = 1.0f;
					}
				}
			}
		}

		RE::NiMatrix3 Lying(float a_yaw, float a_flip)
		{
			const RE::NiPoint3 y{ std::sin(a_yaw), std::cos(a_yaw), 0.0f };
			const RE::NiPoint3 x{ y.y, -y.x, 0.0f };
			const RE::NiPoint3 z{ 0.0f, 0.0f, 1.0f };
			const float        c = std::cos(a_flip), s = std::sin(a_flip);
			return util::FromAxes(x * c - z * s, y, x * s + z * c);
		}

		// a place's way out from the arc's middle (unit), and along it (towards the players' right)
		RE::NiPoint3 Out(int a_seat)
		{
			const float dx = kSpots[a_seat].first, dy = kSpots[a_seat].second - kArcY;
			const float l = std::max(0.01f, std::hypot(dx, dy));
			return { dx / l, dy / l, 0.0f };
		}
		RE::NiPoint3 Along(int a_seat)
		{
			const auto o = Out(a_seat);
			return { -o.y, o.x, 0.0f };
		}
		RE::NiPoint3 Circle(int a_seat) { return { kSpots[a_seat].first, kSpots[a_seat].second, 0.0f }; }
		RE::NiPoint3 AtR(int a_seat, float a_r) { return RE::NiPoint3{ 0.0f, kArcY, 0.0f } + Out(a_seat) * a_r; }
		float        CardYaw(int a_seat)
		{
			const auto o = Out(a_seat);
			return std::atan2(-o.x, -o.y);  // (the cards' tops towards the dealer: the right way up for the player)
		}

		// where a hand's bet goes: on the circle (a split pair's two side by side on it)
		RE::NiPoint3 BetAt(int a_seat, int a_hand, int a_hands)
		{
			const float side = a_hands > 1 ? (a_hand == 0 ? -1.9f : 1.9f) : 0.0f;
			return Circle(a_seat) + Along(a_seat) * side;
		}

		// the k-th card of a hand: in the card box, each a little further in and along than the one before (a split
		// pair's two hands side by side)
		RE::NiPoint3 CardAt(int a_seat, int a_hand, int a_hands, int a_k)
		{
			// (each one on top of the one before, a card's thickness up, so they lie on each other instead of through each
			// other; far enough in and along that the corner of every card under it shows)
			const float k = static_cast<float>(a_k);
			const float side = a_hands > 1 ? (a_hand == 0 ? -3.4f : 3.4f) : 0.0f;
			const float fan = a_hands > 1 && a_hand == 0 ? -1.0f : 1.0f;  // (a split pair's two away from each other)
			auto        p = AtR(a_seat, kCardBoxR + 1.6f - k * 1.6f) + Along(a_seat) * (side + fan * k * 1.25f);
			p.z = k * (kCardT + 0.04f);
			return p;
		}

		RE::NiPoint3 DealerCardAt(int a_k) { return { -6.9f + static_cast<float>(a_k) * 4.6f, kDealerRowY, 0.0f }; }

		int TakePurse(RE::TESFormID a_id, int a_most)
		{
			std::scoped_lock lock(g_npcMutex);
			auto             it = g_purses.find(a_id);
			if (it == g_purses.end()) {
				return 0;
			}
			const int take = std::min(it->second, a_most);
			it->second -= take;
			return take;
		}

		std::string ValueText(const std::vector<int>& a_cards, bool a_split)
		{
			if (a_cards.empty()) {
				return {};
			}
			if (bj::IsBlackjack(a_cards, a_split)) {
				return "BLACKJACK";
			}
			const auto t = bj::Count(a_cards);
			if (t.value > 21) {
				return std::format("BUST {}", t.value);
			}
			return t.soft && t.value < 21 ? std::format("SOFT {}", t.value) : std::to_string(t.value);
		}
	}

	// ---------------------------------------------------------------- the table

	bool Game::BjFrame(Frame& a_out) const
	{
		auto table = _bj.table.get();
		return table && table->Get3D() && FrameOf(table.get(), a_out);
	}

	bool Game::BjHasSeat(RE::TESFormID a_id) const
	{
		return a_id != 0 && (_bj.dealerId == a_id || std::ranges::any_of(_bj.seats, [&](const BjSeat& s) { return s.used && !s.you && s.id == a_id; }));
	}

	RE::NiPoint3 Game::BjStand(const Frame& a_frame, int a_seat, float& a_heading) const
	{
		// (a chair: beside it, to sit from)
		const auto p = AtR(a_seat, Settings::Get().bjChairs ? kEdgeR + kChairOut + 18.0f : kStandR);
		const auto o = Out(a_seat);
		a_heading = HeadingOf(a_frame, -o.x, -o.y);
		return ToWorld(a_frame, p.x, p.y, 0.0);
	}

	void Game::BjScan()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->GetParentCell()) {
			return;
		}
		const auto&        s = Settings::Get();
		RE::TESObjectREFR* nearest = nullptr;
		float              nearestD = s.activeDistance;
		util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(player->data.location, s.activeDistance + 500.0f, [&](RE::TESObjectREFR* a_ref) {
				if (a_ref && Forms::IsBlackjackTable(a_ref) && !a_ref->IsDisabled() && a_ref->Get3D() && !IsKitPreview(a_ref)) {
					const float d = (a_ref->data.location - player->data.location).Length();
					if (d < nearestD) {
						nearestD = d;
						nearest = a_ref;
					}
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		if (auto current = _bj.table.get()) {
			const bool  gone = current->IsDisabled() || !current->Get3D();
			const float d = gone ? 1e9f : (current->data.location - player->data.location).Length();
			if (gone || d > s.activeDistance + 400.0f) {
				BjEnd(gone ? "the table's gone" : "you walked away");
			} else {
				return;
			}
		}
		if (nearest && !_bj.table) {
			BjStart(nearest);
		}
	}

	void Game::BjStart(RE::TESObjectREFR* a_table)
	{
		Settings::Get().Load();
		_bj = BlackjackState{};
		_bj.table = a_table->GetHandle();
		_bj.shoe.Shuffle(_rng);
		log::info("blackjack table {:08X} in play", a_table->GetFormID());
	}

	void Game::BjEnd(const char* a_why)
	{
		if (!_bj.table) {
			return;
		}
		if (_bjSeated) {
			BjStandUp();
		}
		// a hand on (or bets down): everyone's caps back (nobody wins a hand that never finished)
		for (auto& st : _bj.seats) {
			if (!st.used) {
				continue;
			}
			int back = st.bet + st.insurance;
			for (const auto& h : st.hands) {
				back += h.result < 0.0 ? h.bet : 0;
			}
			if (back > 0) {
				if (st.you) {
					GivePlayer(back);
				} else {
					AddToPurse(st.id, back);
				}
			}
			st.bet = st.insurance = 0;
			BjDropHands(st);
		}
		for (int i = 0; i < kBjSeats; ++i) {
			if (_bj.seats[i].used && !_bj.seats[i].you) {
				BjRelease(i, a_why);
			}
		}
		// (everything off now, not slid away: nothing's left to move it after this)
		for (auto& st : _bj.seats) {
			for (auto& h : st.betChips) {
				Remove(h);
			}
		}
		for (auto& c : _bj.cards) {
			Remove(c.ref);
		}
		for (auto& m : _bj.moves) {
			Remove(m.ref);
		}
		if (auto ref = _bj.dealerActor.get(); ref && ref->As<RE::Actor>()) {
			auto* actor = ref->As<RE::Actor>();
			if (_bj.dealerHeld) {
				Npc::SetRestrained(actor, false);
			}
			Npc::Leave(actor);
		}
		Remove(_bj.dealerWalk.marker);
		for (auto& h : _bj.chairs) {
			Remove(h);
		}
		log::info("blackjack table no longer in play: {}", a_why);
		_bj = BlackjackState{};
		_bjYou = -1;
	}

	void Game::BjSay(const std::string& a_text, int a_tone)
	{
		if (!_bj.log.empty() && _bj.log.back().text == a_text) {
			return;  // (the same again, a bet over the most tried again say: once is enough)
		}
		log::info("[blackjack] {}", a_text);
		_bj.log.push_back({ a_text, a_tone });
		while (_bj.log.size() > 6) {
			_bj.log.pop_front();
		}
	}

	void Game::BjSound(Audio::Bank a_bank, float a_gain)
	{
		Frame frame;
		if (BjFrame(frame)) {
			Audio::Play(a_bank, ToWorld(frame, 0.0f, 0.0f, kFeltZ), a_gain);
		}
	}

	// ---------------------------------------------------------------- the tick

	void Game::TickBlackjack(float a_dt)
	{
		if (!_bj.table) {
			return;
		}
		Frame frame;
		if (!BjFrame(frame)) {
			return;
		}
		if (_bjSeated) {
			if (auto* player = RE::PlayerCharacter::GetSingleton(); player && player->IsInCombat()) {
				log::info("blackjack: you're in a fight: up from the table");
				BjStandUp();
				Notify("Blackjack: trouble! You're up from the table");
			}
		}
		// (the chairs option) a chair at each place while it's on; none when it's off
		{
			const bool want = Settings::Get().bjChairs;
			for (int i = 0; i < kBjSeats; ++i) {
				auto& h = _bj.chairs[i];
				if (!want) {
					if (h) {
						Remove(h);
					}
					continue;
				}
				if (!h.get()) {
					auto* base = RE::TESForm::GetFormByID(kChair);
					auto* form = base && base->IsBoundObject() ? static_cast<RE::TESBoundObject*>(base) : nullptr;
					const auto at = AtR(i, kEdgeR + kChairOut);
					const auto o = Out(i);
					if (form) {
						// (the one kept through a save, if it's there: else a new one)
						const auto pos = ToWorld(frame, at.x, at.y, 0.0);
						h = FindPlaced(form, pos, 8.0f);
						if (!h.get()) {
							h = Spawn(form, pos, HeadingOf(frame, -o.x, -o.y));
						}
						if (auto c = h.get()) {
							Npc::ClaimChair(c.get());  // (yours till someone plays in it: nobody passing by sits in it)
						}
					}
				}
			}
		}
		_bj.bannerAge += a_dt;
		_bj.seatedAge += a_dt;
		BjTickSeats(frame, a_dt);
		BjTickRound(a_dt);
		const bool animating = std::chrono::steady_clock::now() - _bjAnimAt < std::chrono::milliseconds(250);
		BjSpawnCards(frame);
		if (!animating) {
			BjTickCards(frame, a_dt);
			BjTickMoves(frame, a_dt);
		}
		if ((_bj.sweep -= a_dt) <= 0.0f) {
			_bj.sweep = 2.0f;
			BjSweep(frame);
		}
		if (_bjSeated && _bjYou >= 0) {
			BjCamera(frame, a_dt);
		}
		// the chime: bets open (and none of yours down yet), insurance asked, your turn (each hand of a split)
		{
			int prompt = 0;
			if (_bjSeated && _bjYou >= 0) {
				const auto& me = _bj.seats[_bjYou];
				if (_bj.phase == BjPhase::kBetting && me.bet == 0) {
					prompt = 1;
				} else if (_bj.phase == BjPhase::kInsurance) {
					prompt = 2;
				} else if (_bj.phase == BjPhase::kPlayers && _bj.turnSeat == _bjYou) {
					prompt = 10 + _bj.turnHand;
				}
			}
			if (prompt != 0 && prompt != _bjPrompt && Settings::Get().turnChime) {
				Audio::PlayUI(Audio::Bank::kTurn, 0.35f);
			}
			_bjPrompt = prompt;
		}
	}

	void Game::BjAnimate()
	{
		const auto now = std::chrono::steady_clock::now();
		float      dt = _bjAnimHave ? std::chrono::duration<float>(now - _bjAnimLast).count() : 0.0f;
		_bjAnimLast = now;
		_bjAnimHave = true;
		if (!_bj.table) {
			return;
		}
		Frame frame;
		if (!BjFrame(frame)) {
			return;
		}
		_bjAnimAt = now;
		dt = std::clamp(dt, 0.0f, 0.05f);
		BjTickCards(frame, dt);
		BjTickMoves(frame, dt);
		std::unordered_set<RE::TESFormID> moving;
		for (const auto& m : _bj.moves) {
			if (auto r = m.ref.get()) {
				moving.insert(r->GetFormID());
			}
		}
		auto still = [&](const RE::ObjectRefHandle& a_h) {
			NoFade(a_h);
			if (auto r = a_h.get(); r && !moving.contains(r->GetFormID())) {
				Still(r->Get3D());
			}
		};
		for (const auto& st : _bj.seats) {
			std::ranges::for_each(st.betChips, still);
			std::ranges::for_each(st.insChips, still);
			for (const auto& h : st.hands) {
				std::ranges::for_each(h.chips, still);
				std::ranges::for_each(h.paid, still);
			}
		}
		for (const auto& c : _bj.cards) {
			NoFade(c.ref);
		}
	}

	// ---------------------------------------------------------------- settlers

	void Game::BjFindPlayer(const Frame& a_frame)
	{
		if (const auto* you = RE::PlayerCharacter::GetSingleton(); you && you->IsInCombat()) {
			return;
		}
		// after a load: who was sat here when it was saved, back in their place first (Roster.cpp)
		if (auto tbl = _bj.table.get()) {
			std::int32_t rs = -1;
			while (auto* back = RosterNext(kRosterBlackjack, tbl->GetFormID(), rs, false)) {
				if (rs >= 0 && rs < kBjSeats && !_bj.seats[rs].used) {
					BjSeatIn(rs, back);
				}
			}
		}
		const auto& s = Settings::Get();
		int         npcs = 0;
		for (const auto& st : _bj.seats) {
			npcs += st.used && !st.you;
		}
		if (!s.pkPlayers || npcs >= s.pkMaxPlayers || _bj.phase != BjPhase::kBetting) {
			return;
		}
		auto*        player = RE::PlayerCharacter::GetSingleton();
		static auto* npcType = RE::TESForm::GetFormByID<RE::BGSKeyword>(Forms::kActorTypeNPC);
		std::vector<std::pair<float, RE::Actor*>> found;
		util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(a_frame.origin, s.bettorRange, [&](RE::TESObjectREFR* a_ref) {
				auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
				if (!Usable(actor) || actor == player || actor->IsChild() || actor->IsInCombat() || Npc::Busy(actor) ||
					(player && actor->GetHostileToActor(player)) || (npcType && !actor->HasKeywordHelper(npcType, nullptr)) ||
					BjHasSeat(actor->GetFormID()) || PkHasSeat(actor->GetFormID()) || SlHasSeat(actor->GetFormID()) || IsCroupier(actor->GetFormID()) || RosterHolds(actor->GetFormID()) ||
					std::ranges::any_of(_t.seats, [&](const Seat& st) { return st.id == actor->GetFormID(); }) ||
					std::ranges::any_of(_other.seats, [&](const Seat& st) { return st.id == actor->GetFormID(); })) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				if (Purse(actor->GetFormID()) < 20) {
					AddToPurse(actor->GetFormID(), 150 + static_cast<int>((actor->GetFormID() * 2654435761u) % 250u));
				}
				found.emplace_back((actor->data.location - a_frame.origin).Length(), actor);
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		if (found.empty()) {
			return;
		}
		std::ranges::sort(found, {}, &std::pair<float, RE::Actor*>::first);
		auto* actor = found[std::uniform_int_distribution<std::size_t>(0, std::min<std::size_t>(found.size(), 4) - 1)(_rng)].second;
		std::vector<int> free;
		for (int i = 0; i < kBjSeats; ++i) {
			if (_bj.seats[i].used) {
				continue;
			}
			float      heading = 0.0f;
			const auto at = BjStand(a_frame, i, heading);
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
				free.push_back(i);
			}
		}
		if (free.empty()) {
			return;
		}
		const int seat = free[std::uniform_int_distribution<std::size_t>(0, free.size() - 1)(_rng)];
		BjSeatIn(seat, actor);
	}

	void Game::BjSeatIn(int seat, RE::Actor* actor)
	{
		const auto& s = Settings::Get();
		(void)s;
		auto&     st = _bj.seats[seat];
		st = BjSeat{};
		st.used = true;
		st.actor = actor->GetHandle();
		st.id = actor->GetFormID();
		st.name = actor->GetDisplayFullName();
		st.rounds = std::uniform_int_distribution<int>(5, 14)(_rng);
		log::info("blackjack: {} ({:08X}) takes place {} ({} caps in their purse)", st.name, st.id, seat, Purse(st.id));
		BjSay(std::format("{} JOINS THE TABLE", Upper(st.name)));
	}

	void Game::BjFindDealer(const Frame& a_frame)
	{
		if (_bj.dealerId != 0) {
			return;
		}
		// after a load: who was dealing here when it was saved, back at it first (Roster.cpp)
		if (auto tbl = _bj.table.get()) {
			std::int32_t rs = -1;
			if (auto* back = RosterNext(kRosterBlackjack, tbl->GetFormID(), rs, true)) {
				BjDealerIn(back);
				return;
			}
		}
		const auto& s = Settings::Get();
		auto*       player = RE::PlayerCharacter::GetSingleton();
		static auto* npcType = RE::TESForm::GetFormByID<RE::BGSKeyword>(Forms::kActorTypeNPC);
		RE::Actor*  best = nullptr;
		float       bestD = 1e9f;
		util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(a_frame.origin, s.bettorRange, [&](RE::TESObjectREFR* a_ref) {
				auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
				if (!Usable(actor) || actor == player || actor->IsChild() || actor->IsInCombat() || Npc::Busy(actor) ||
					(player && actor->GetHostileToActor(player)) || (npcType && !actor->HasKeywordHelper(npcType, nullptr)) ||
					BjHasSeat(actor->GetFormID()) || PkHasSeat(actor->GetFormID()) || SlHasSeat(actor->GetFormID()) || IsCroupier(actor->GetFormID()) || RosterHolds(actor->GetFormID()) ||
					std::ranges::any_of(_t.seats, [&](const Seat& st) { return st.id == actor->GetFormID(); }) ||
					std::ranges::any_of(_other.seats, [&](const Seat& st) { return st.id == actor->GetFormID(); })) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const float d = (actor->data.location - a_frame.origin).Length();
				if (d < bestD) {
					bestD = d;
					best = actor;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		if (!best) {
			return;
		}
		BjDealerIn(best);
	}

	void Game::BjDealerIn(RE::Actor* best)
	{
		const auto& s = Settings::Get();
		(void)s;
		_bj.dealerActor = best->GetHandle();
		_bj.dealerId = best->GetFormID();
		_bj.dealerName = best->GetDisplayFullName();
		_bj.dealerWalk = Walker{};
		_bj.dealerHeld = false;
		log::info("blackjack: {} ({:08X}) comes over to deal", _bj.dealerName, _bj.dealerId);
		BjSay(std::format("{} DEALS", Upper(_bj.dealerName)));
	}

	void Game::BjDropHands(BjSeat& a_st)
	{
		// a place's hands off the table: the chips on them gone, the cards to the discard holder (left out of the round)
		for (auto& h : a_st.hands) {
			for (auto& c : h.chips) {
				Remove(c);
			}
			for (auto& c : h.paid) {
				Remove(c);
			}
			for (const auto i : h.refs) {
				if (i >= _bj.cards.size()) {
					continue;
				}
				auto& c = _bj.cards[i];
				if (!c.ref.get()) {
					c.mucked = true;  // (not out yet: it never will be)
					c.spawned = true;
					c.t = 1.0f;
					continue;
				}
				if (c.mucked) {
					continue;
				}
				const float e = 1.0f - (1.0f - c.t) * (1.0f - c.t);
				c.from = c.from + (c.to - c.from) * e;
				c.to = kDiscard;
				c.t = 0.0f;
				c.delay = 0.0f;
				c.spin = 0.0f;
				c.mucked = true;
				c.faceUp = false;
			}
		}
		for (auto& c : a_st.insChips) {
			Remove(c);
		}
		a_st.insChips.clear();
		a_st.hands.clear();
	}

	void Game::BjRelease(int a_seat, const char* a_why)
	{
		auto& st = _bj.seats[a_seat];
		if (!st.used || st.you) {
			return;
		}
		if (st.bet > 0) {
			AddToPurse(st.id, st.bet);  // (a bet down for a hand not dealt yet: back)
			st.bet = 0;
		}
		for (auto& h : st.betChips) {
			Remove(h);
		}
		BjDropHands(st);  // (a hand on the table: its chips off, its cards to the discard)
		auto  ref = st.actor.get();
		auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
		if (actor) {
			if (st.held && !Settings::Get().bjChairs) {
				Npc::SetRestrained(actor, false);
			}
			if (auto* proc = actor->currentProcess; proc && proc->GetOccupiedFurniture()) {
				actor->InitiateGetUpPackage();  // (up out of their chair)
			}
			if (auto c = _bj.chairs[a_seat].get()) {
				Npc::ClaimChair(c.get());  // (yours again: nobody else sits in it)
			}
			Npc::Leave(actor);
		}
		Remove(st.walk.marker);
		log::info("blackjack: {} leaves ({})", st.name, a_why ? a_why : "");
		st = BjSeat{};
	}

	void Game::BjTickSeats(const Frame& a_frame, float a_dt)
	{
		const auto& s = Settings::Get();
		_bj.seatTimer -= a_dt;
		if (_bj.seatTimer <= 0.0f) {
			_bj.seatTimer = std::uniform_real_distribution<float>(6.0f, 12.0f)(_rng);
			BjFindDealer(a_frame);
			if (s.pkPlayers) {
				BjFindPlayer(a_frame);
			}
		}
		const auto* you = RE::PlayerCharacter::GetSingleton();
		// the dealer: behind the chip rack, facing the players
		_bj.dealerCool -= a_dt;
		if (auto ref = _bj.dealerActor.get(); _bj.dealerId != 0) {
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			Npc::KeepFromPool(actor);  // (in a game here and called over to the pool table: kept here)
			if (!Usable(actor) || actor->IsInCombat() || (you && you->IsInCombat())) {
				if (actor) {
					if (_bj.dealerHeld) {
						Npc::SetRestrained(actor, false);
					}
					Npc::Leave(actor);
				}
				Remove(_bj.dealerWalk.marker);
				_bj.dealerActor = {};
				_bj.dealerId = 0;
				_bj.dealerHeld = false;
			} else if (_bj.dealerHeld) {
				HoldFacing(_bj.dealerWalk, actor);
				// a standing fidget of their own between their gestures (the game's unarmed standing fidgets and
				// talking hands: dyn_Flavor, played where they stand)
				_bj.dealerIdleIn -= a_dt;
				if (_bj.dealerIdleIn <= 0.0f) {
					_bj.dealerIdleIn = std::uniform_real_distribution<float>(10.0f, 20.0f)(_rng);
					if (_bj.dealerCool <= 0.0f) {
						static constexpr RE::TESFormID kStand[] = { 0x002179C7, 0x002179CA };
						Npc::PlayIdle(actor, kStand[std::uniform_int_distribution<int>(0, 1)(_rng)]);
					}
				}
				// now and then turned to the table's middle
				_bj.dealerPutBackIn -= a_dt;
				_bj.dealerFaceIn -= a_dt;
				if (_bj.dealerFaceIn <= 0.0f) {
					_bj.dealerFaceIn = 3.0f;
					TurnToFace(actor, a_frame.origin.x, a_frame.origin.y);
					// off their spot by more than a step (pushed away, or gone home over a load): put back on it, at most
					// every 20 s (a few units off is left alone: that was the back and forth)
					const auto  spot = ToWorld(a_frame, 0.0, kDealerStand, 0.0);
					const float off = Distance2D(actor->data.location, spot);
					if (auto marker = _bj.dealerWalk.marker.get(); marker && off > 20.0f && (off > 300.0f || _bj.dealerPutBackIn <= 0.0f)) {
						log::info("blackjack: dealer {:.0f} off their spot: put back on it", off);
						Npc::MoveTo(actor, marker.get());
						_bj.dealerPutBackIn = 20.0f;
					} else if (!marker && off > 60.0f) {
						log::info("blackjack: dealer {:.0f} off their spot: walking back", off);
						Npc::SetRestrained(actor, false);
						_bj.dealerHeld = false;
						_bj.dealerWalk.walked = _bj.dealerWalk.stall = 0.0f;
						_bj.dealerWalk.best = 1e9f;
					}
				}
			} else {
				const float heading = HeadingOf(a_frame, 0.0, -1.0);
				const auto  spot = ToWorld(a_frame, 0.0, kDealerStand, 0.0);
				if (WalkTo(actor, _bj.dealerWalk, a_frame, spot, heading, a_dt, true, 15.0f)) {
					// their walk ended short of the spot (more than 15 off): put on it, once (never again while they
					// stand there: put back over and over, they jumped back and forth)
					if (auto marker = _bj.dealerWalk.marker.get(); marker && Distance2D(actor->data.location, spot) > 8.0f) {
						log::info("blackjack: dealer stopped {:.0f} short of their spot: put on it", Distance2D(actor->data.location, spot));
						Npc::MoveTo(actor, marker.get());
					}
					_bj.dealerHeld = Npc::SetRestrained(actor, true);
					_bj.dealerFaceIn = 0.5f;  // (turned to the table straight after)
				}
			}
		}
		for (int i = 0; i < kBjSeats; ++i) {
			auto& st = _bj.seats[i];
			if (!st.used || st.you) {
				continue;
			}
			auto  ref = st.actor.get();
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			Npc::KeepFromPool(actor);  // (in a game here and called over to the pool table: kept here)
			if (!Usable(actor) || actor->IsInCombat() || (you && you->IsInCombat())) {
				BjRelease(i, "gone");
				continue;
			}
			float      heading = 0.0f;
			const auto spot = BjStand(a_frame, i, heading);
			const bool chairs = s.bjChairs && _bj.chairs[i].get();
			if (st.step == Seat::Step::kWalking) {
				if (chairs && Distance2D(actor->data.location, spot) < 110.0f) {
					// (chairs: near enough, put straight in it)
					st.step = Seat::Step::kPlaying;
					st.sitCheck = 0.0f;
					st.sitTries = 0;
					continue;
				}
				if (WalkTo(actor, st.walk, a_frame, spot, heading, a_dt, false, 15.0f)) {
					st.step = Seat::Step::kPlaying;  // (where their walk ended: no step-put, a jump)
				}
				continue;
			}
			if (chairs) {
				// sat in their chair (the quest's Sit package keeps them there; checked now and then, sat again if
				// they're up, moved onto it after a couple of goes)
				auto chair = _bj.chairs[i].get();
				TrackSit("blackjack", st.name, actor, chair.get(), st.sitWas);
				st.sitCheck -= a_dt;
				if (chair && st.sitCheck <= 0.0f) {
					st.sitCheck = 3.0f;
					const auto sit = actor->DoGetSitSleepState();
					const bool down = sit == RE::SIT_SLEEP_STATE::kWantToSit || sit == RE::SIT_SLEEP_STATE::kWaitingForSitAnim ||
					                  sit == RE::SIT_SLEEP_STATE::kIsSitting;
					// (on their way and not sat for three checks running, walking into the chair from behind, say: put
					// in it, once)
					st.notSat = sit == RE::SIT_SLEEP_STATE::kIsSitting ? 0 : st.notSat + 1;
					if (st.notSat >= 3) {
						st.notSat = 0;
						log::info("[sit] blackjack: {} not sat in 9 s (sit state {}): put in their chair", st.name, static_cast<int>(sit));
						Npc::SitOn(actor, chair.get());
					} else if (down) {
						st.sitTries = 0;
					} else {
						// (walked over and sat down the chair's way: never snapped in or moved onto it, a jump)
						++st.sitTries;
						log::info("[sit] blackjack: {} isn't in their chair (sit state {}): sat down again", st.name, static_cast<int>(sit));
						Npc::SitDown(actor, chair.get());
					}
				}
				st.held = true;
				// (their time at the table counts once they're sat: they bet, and are dealt in, sitting down)
				// (out of the chair again, or not in it yet: back to nothing, and no bet goes down in front of them)
				st.atSpot = !chair || actor->DoGetSitSleepState() == RE::SIT_SLEEP_STATE::kIsSitting ? st.atSpot + a_dt : 0.0f;
			} else {
				if (!st.held) {
					st.held = Npc::SetRestrained(actor, true);
				}
				HoldFacing(st.walk, actor);
				st.atSpot = Distance2D(actor->data.location, spot) < kArrived + 10.0f ? st.atSpot + a_dt : 0.0f;
			}
			// between hands: out of caps, or had enough
			if (_bj.phase == BjPhase::kBetting && st.bet == 0 && (st.rounds <= 0 || Purse(st.id) < 5)) {
				BjSay(std::format("{} {}", Upper(st.name), Purse(st.id) < 5 ? "IS OUT OF CAPS" : "LEAVES THE TABLE"));
				BjRelease(i, "done");
			}
		}
	}

	void Game::BjIdle(int a_seat, int a_kind)
	{
		if (a_seat < 0 || a_seat >= kBjSeats || !Settings::Get().bettorIdles) {
			return;
		}
		auto& st = _bj.seats[a_seat];
		auto  ref = st.actor.get();
		auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
		if (!st.used || st.you || !st.held || !actor) {
			return;
		}
		// (only the cheers, as at the poker table: the game's other idles step and turn them; seated, the seated ones)
		if (a_kind == 3) {
			if (Settings::Get().bjChairs && _bj.chairs[a_seat].get()) {
				Npc::PlayIdle(actor, kSeatedWin[std::uniform_int_distribution<std::size_t>(0, kSeatedWin.size() - 1)(_rng)]);
			} else {
				Npc::PlayIdle(actor, kWinIdles[std::uniform_int_distribution<std::size_t>(0, kWinIdles.size() - 1)(_rng)]);
			}
		}
	}

	// ---------------------------------------------------------------- chips and cards

	void Game::BjStack(std::vector<RE::ObjectRefHandle>& a_chips, int a_amount, const RE::NiPoint3& a_at)
	{
		// a stack of the fewest chips, the big ones at the bottom, on top of whatever's there
		Frame frame;
		if (!BjFrame(frame) || a_amount <= 0) {
			return;
		}
		std::uniform_real_distribution<float> spin(0.0f, 6.283f);
		std::uniform_real_distribution<float> slop(-0.08f, 0.08f);
		for (int v : rl::ChipsFor(a_amount)) {
			const int  d = static_cast<int>(std::ranges::find(rl::kChipValues, v) - std::begin(rl::kChipValues));
			const auto z = kLift + static_cast<float>(a_chips.size()) * kStep;
			a_chips.push_back(Spawn(Forms::Chip(d), ToWorld(frame, a_at.x + slop(_rng), a_at.y + slop(_rng), kFeltZ + z), frame.yaw + spin(_rng)));
		}
	}

	void Game::BjSlide(std::vector<RE::ObjectRefHandle>& a_chips, const RE::NiPoint3& a_to, bool a_remove, float a_delay)
	{
		Frame frame;
		if (!BjFrame(frame)) {
			for (auto& h : a_chips) {
				Remove(h);
			}
			a_chips.clear();
			return;
		}
		int k = 0;
		for (auto& h : a_chips) {
			auto ref = h.get();
			if (!ref) {
				continue;
			}
			std::erase_if(_bj.moves, [&](const PkChipMove& a_m) { return a_m.ref == h; });
			const auto local = frame.rot * ((ref->data.location - frame.origin) * (1.0f / frame.scale));
			PkChipMove m;
			m.ref = h;
			m.from = { local.x, local.y, local.z - kFeltZ };
			m.to = { a_to.x, a_to.y, kLift + static_cast<float>(k) * kStep };
			m.delay = a_delay + 0.03f * static_cast<float>(k);
			m.dur = 0.5f;
			m.remove = a_remove;
			_bj.moves.push_back(m);
			++k;
		}
		if (a_remove) {
			a_chips.clear();
		}
		if (k > 0) {
			BjSound(Audio::Bank::kChip, 0.6f);
		}
	}

	std::size_t Game::BjDeal(int a_card, const RE::NiPoint3& a_to, float a_yaw, bool a_faceUp, float a_delay)
	{
		PkCard c;
		c.card = a_card;
		c.from = kShoe;
		c.to = a_to;
		c.yaw = a_yaw;
		c.delay = a_delay;
		c.faceUp = a_faceUp;
		c.spin = std::uniform_real_distribution<float>(-1.2f, 1.2f)(_rng);
		_bj.cards.push_back(c);
		return _bj.cards.size() - 1;
	}

	void Game::BjOurs(std::unordered_set<RE::TESFormID>& a_ours) const
	{
		auto add = [&](const RE::ObjectRefHandle& a_h) {
			if (auto r = a_h.get()) {
				a_ours.insert(r->GetFormID());
			}
		};
		for (const auto& c : _bj.cards) {
			add(c.ref);
		}
		for (const auto& m : _bj.moves) {
			add(m.ref);
		}
		for (const auto& st : _bj.seats) {
			std::ranges::for_each(st.betChips, add);
			std::ranges::for_each(st.insChips, add);
			for (const auto& h : st.hands) {
				std::ranges::for_each(h.chips, add);
				std::ranges::for_each(h.paid, add);
			}
		}
	}

	void Game::BjSweep(const Frame& a_frame)
	{
		// cards and chips on the felt that none of ours know of (left behind: a save made with them out, a place left
		// mid-hand, an older version's): gone. Only on this table's felt, and never the poker table's
		std::unordered_set<RE::TESFormID> ours;
		BjOurs(ours);
		PkOurs(ours);
		std::unordered_set<const RE::TESBoundObject*> kinds;
		for (int d = 0; d < 4; ++d) {
			kinds.insert(Forms::Chip(d));
		}
		for (int c = 0; c < 52; ++c) {
			kinds.insert(Forms::Card(c));
		}
		kinds.erase(nullptr);
		std::vector<RE::ObjectRefHandle> strays;
		util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(a_frame.origin, 95.0f * a_frame.scale, [&](RE::TESObjectREFR* a_ref) {
				if (!a_ref || a_ref->IsDisabled() || ours.contains(a_ref->GetFormID()) || !kinds.contains(a_ref->GetObjectReference())) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const auto local = a_frame.rot * ((a_ref->data.location - a_frame.origin) * (1.0f / a_frame.scale));
				if (std::abs(local.x) < 78.0f && local.y > -56.0f && local.y < 58.0f && local.z > kFeltZ - 4.0f && local.z < kFeltZ + 20.0f) {
					strays.push_back(a_ref->GetHandle());
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		if (!strays.empty()) {
			log::warn("blackjack: {} cards and chips on the table that aren't anyone's (left behind): taken off", strays.size());
			for (auto& h : strays) {
				Remove(h);
			}
		}
	}

	void Game::BjSpawnCards(const Frame& a_frame)
	{
		// the cards whose turn it is to leave the shoe, made: once each, and only from the tick (the camera's update
		// and the tick both making one left a copy on the table)
		for (auto& c : _bj.cards) {
			if (c.delay > 0.0f || c.mucked || c.spawned) {
				continue;
			}
			c.spawned = true;
			c.ref = Spawn(Forms::Card(c.card), ToWorld(a_frame, c.from.x, c.from.y, kFeltZ + 2.0f), a_frame.yaw);
		}
	}

	void Game::BjTickCards(const Frame& a_frame, float a_dt)
	{
		for (auto& c : _bj.cards) {
			if (c.delay > 0.0f) {
				c.delay -= a_dt;
				continue;
			}
			auto ref = c.ref.get();
			if (!ref) {
				continue;  // (not made yet: BjSpawnCards, from the tick)
			}
			if (c.t < 1.0f) {
				c.t = std::min(1.0f, c.t + a_dt / (c.mucked ? 0.45f : 0.38f));
				if (c.t >= 1.0f && !c.mucked) {
					BjSound(Audio::Bank::kCard, 0.45f);
				}
			}
			if (c.mucked && c.t >= 1.0f) {
				Remove(c.ref);
				continue;
			}
			const float want = c.faceUp ? 1.0f : 0.0f;
			if (c.flip != want && (c.t >= 0.6f || c.mucked)) {
				c.flip = c.flip < want ? std::min(want, c.flip + a_dt / 0.3f) : std::max(want, c.flip - a_dt / 0.3f);
			}
			if (!ref->Get3D()) {
				continue;
			}
			const float  e = 1.0f - (1.0f - c.t) * (1.0f - c.t);
			const auto   p = c.from + (c.to - c.from) * e;
			const float  flipAngle = (1.0f - c.flip) * 3.14159265f;
			const float  arc = c.t < 1.0f ? std::sin(c.t * 3.14159265f) * (c.mucked ? 0.8f : 2.2f) : 0.0f;
			const float  lift = std::sin(c.flip * 3.14159265f) * 2.5f + (c.flip < 0.5f ? kCardT : 0.0f) + 0.02f + arc + c.lift;
			const float  yaw = c.yaw + c.spin * (1.0f - e);
			PlaceRoot(ref.get(), ToWorld(a_frame, p.x, p.y, kFeltZ + p.z + lift), Lying(yaw, flipAngle) * a_frame.rot);
		}
	}

	void Game::BjTickMoves(const Frame& a_frame, float a_dt)
	{
		for (auto& m : _bj.moves) {
			if (m.delay > 0.0f) {
				m.delay -= a_dt;
				continue;
			}
			auto ref = m.ref.get();
			if (!ref) {
				m.t = 1.0f;
				continue;
			}
			if (!ref->Get3D()) {
				continue;
			}
			m.t = std::min(1.0f, m.t + a_dt / m.dur);
			const float e = m.t * m.t * (3.0f - 2.0f * m.t);
			const auto  p = m.from + (m.to - m.from) * e;
			const float up = m.t < 0.25f ? m.t / 0.25f : m.t > 0.8f ? (1.0f - m.t) / 0.2f : 1.0f;
			NoFade(m.ref);
			if (!m.haveRot) {
				m.rot = ref->Get3D()->world.rotate;
				m.haveRot = true;
			}
			PlaceRoot(ref.get(), ToWorld(a_frame, p.x, p.y, kFeltZ + p.z + up * 2.2f), m.rot);
			if (m.t >= 1.0f && m.remove) {
				Remove(m.ref);
			}
		}
		std::erase_if(_bj.moves, [](const PkChipMove& m) { return m.t >= 1.0f; });
	}

	void Game::BjPlaceHandCards(int a_seat, int a_hand)
	{
		// (after a split: the hands' cards slid over to where they go now)
		auto&     st = _bj.seats[a_seat];
		const int n = static_cast<int>(st.hands.size());
		auto&     h = st.hands[a_hand];
		for (std::size_t k = 0; k < h.refs.size(); ++k) {
			auto& c = _bj.cards[h.refs[k]];
			const auto to = CardAt(a_seat, a_hand, n, static_cast<int>(k));
			if (Distance2D(c.to, to) > 0.01f) {
				const float e = 1.0f - (1.0f - c.t) * (1.0f - c.t);
				c.from = c.from + (c.to - c.from) * e;
				c.to = to;
				c.t = 0.0f;
				c.spin = 0.0f;
			}
		}
	}

	// ---------------------------------------------------------------- the round

	void Game::BjBet(int a_amount)
	{
		if (_bjYou < 0 || _bj.phase != BjPhase::kBetting) {
			return;
		}
		auto& st = _bj.seats[_bjYou];
		const auto& s = Settings::Get();
		if (a_amount <= 0) {
			if (st.bet > 0) {
				GivePlayer(st.bet);
				st.bet = 0;
				for (auto& h : st.betChips) {
					Remove(h);
				}
				st.betChips.clear();
				BjSay("YOUR BET'S BACK");
			}
			return;
		}
		const int add = std::min(a_amount, s.maxBet - st.bet);
		if (add <= 0) {
			BjSay(std::format("THE MOST ON A HAND IS {}", s.maxBet), 2);
			return;
		}
		if (!TakeFromPlayer(add)) {
			BjSay("NOT ENOUGH CAPS", 2);
			return;
		}
		st.bet += add;
		BjStack(st.betChips, add, Circle(_bjYou));
		BjSound(Audio::Bank::kChip, 0.5f);
		if (_bj.dealIn < 0.0f) {
			_bj.dealIn = kBetWindow;
		}
	}

	void Game::BjStartDeal()
	{
		// the hands: everyone with a bet down
		int players = 0;
		for (auto& st : _bj.seats) {
			st.hands.clear();
			st.insurance = 0;
			st.last.clear();
			if (st.used && st.bet > 0 && (st.you || st.step == Seat::Step::kPlaying)) {
				BjHand h;
				h.bet = st.bet;
				h.chips = std::move(st.betChips);
				st.betChips.clear();
				st.lastBet = st.bet;
				st.bet = 0;
				st.hands.push_back(std::move(h));
				++players;
				if (!st.you) {
					--st.rounds;
				}
			}
		}
		if (players == 0) {
			_bj.dealIn = -1.0f;
			return;
		}
		if (_bj.shoe.PastCut()) {
			_bj.shoe.Shuffle(_rng);
			BjSay("THE DEALER SHUFFLES THE SHOE");
		}
		_bj.dealer.clear();
		_bj.dealerRefs.clear();
		_bj.holeUp = false;
		_bj.bannerTitle.clear();
		// two rounds from the dealer's left (the players' right: the last place first), the dealer's card after each:
		// the first face up, the second face down
		float delay = 0.2f;
		for (int round = 0; round < 2; ++round) {
			for (int i = kBjSeats - 1; i >= 0; --i) {
				auto& st = _bj.seats[i];
				if (st.hands.empty()) {
					continue;
				}
				const int card = _bj.shoe.Draw(_rng);
				st.hands[0].cards.push_back(card);
				st.hands[0].refs.push_back(BjDeal(card, CardAt(i, 0, 1, round), CardYaw(i), true, delay));
				delay += kDealGap;
			}
			const int card = _bj.shoe.Draw(_rng);
			_bj.dealer.push_back(card);
			_bj.dealerRefs.push_back(BjDeal(card, DealerCardAt(round), 0.0f, round == 0, delay));
			delay += kDealGap;
		}
		_bj.phase = BjPhase::kDealing;
		_bj.timer = delay + 0.5f;
		_bj.dealIn = -1.0f;
		BjSay("CARDS");
		_bj.pointed = false;
		_bj.dealerCool = 0.0f;
		DealerGesture(_bj.dealerActor, _bj.dealerHeld, _bj.dealerCool, Gesture::kDeal, 0.0, kDealerStand, 0.0, -1.0);
	}

	void Game::BjNextTurn()
	{
		// the next hand to play: from the dealer's left (the last place) round to the first; within a place, its hands
		// in turn. Hands that are done (a blackjack, 21, busted, stood, doubled) are passed over
		for (int i = _bj.turnSeat < 0 ? kBjSeats - 1 : _bj.turnSeat; i >= 0; --i) {
			auto& st = _bj.seats[i];
			for (int h = (i == _bj.turnSeat ? _bj.turnHand : 0); h < static_cast<int>(st.hands.size()); ++h) {
				auto& hand = st.hands[h];
				const auto t = bj::Count(hand.cards);
				const bool splitAces = hand.split && bj::Rank(hand.cards[0]) == 12;
				if (hand.done || t.value >= 21 || (splitAces && hand.cards.size() >= 2)) {
					hand.done = true;
					continue;
				}
				if (_bj.turnSeat != i && !_bj.pointed) {
					_bj.pointed = true;  // (once a round: at the first to play)
					const auto at = Circle(i);
					DealerGesture(_bj.dealerActor, _bj.dealerHeld, _bj.dealerCool, Gesture::kPoint, 0.0, kDealerStand, 0.0, -1.0, at.x,
						at.y);  // (the dealer points at whose turn it is)
				}
				_bj.turnSeat = i;
				_bj.turnHand = h;
				_bj.think = st.you ? 0.0f : std::uniform_real_distribution<float>(0.9f, 2.0f)(_rng);
				return;
			}
		}
		// everyone's played: the dealer's turn
		_bj.turnSeat = -1;
		_bj.turnHand = 0;
		_bj.phase = BjPhase::kDealer;
		_bj.timer = 0.6f;
	}

	void Game::BjAct(bj::Move a_move)
	{
		if (_bj.phase != BjPhase::kPlayers || _bj.turnSeat < 0) {
			return;
		}
		const int i = _bj.turnSeat;
		auto&     st = _bj.seats[i];
		auto&     hand = st.hands[_bj.turnHand];
		const bool first = hand.cards.size() == 2;
		auto pay = [&](int a_caps) {
			if (st.you) {
				return TakeFromPlayer(a_caps);
			}
			const int got = TakePurse(st.id, a_caps);
			if (got < a_caps) {
				AddToPurse(st.id, got);  // (not enough: what was taken back)
				return false;
			}
			return true;
		};
		const std::string who = st.you ? std::string("YOU") : Upper(st.name);
		switch (a_move) {
		case bj::Move::kHit:
			{
				const int card = _bj.shoe.Draw(_rng);
				hand.cards.push_back(card);
				hand.refs.push_back(BjDeal(card, CardAt(i, _bj.turnHand, static_cast<int>(st.hands.size()), static_cast<int>(hand.cards.size()) - 1),
					CardYaw(i), true, 0.0f));
				st.last = std::format("HIT · {}", ValueText(hand.cards, hand.split));
				BjSay(std::format("{} {} · {}", who, st.you ? "HIT" : "HITS", ValueText(hand.cards, hand.split)));
				if (bj::Count(hand.cards).value > 21) {
					hand.done = true;
					st.last = "BUST";
				}
				break;
			}
		case bj::Move::kStand:
			hand.done = true;
			st.last = std::format("STANDS · {}", ValueText(hand.cards, hand.split));
			BjSay(std::format("{} {} ON {}", who, st.you ? "STAND" : "STANDS", ValueText(hand.cards, hand.split)));
			break;
		case bj::Move::kDouble:
			{
				if (!first || !pay(hand.bet)) {
					if (st.you) {
						BjSay(first ? "NOT ENOUGH CAPS TO DOUBLE" : "YOU CAN ONLY DOUBLE ON TWO CARDS", 2);
					}
					return BjAct(bj::Move::kHit);
				}
				std::vector<RE::ObjectRefHandle> more;
				BjStack(more, hand.bet, BetAt(i, _bj.turnHand, static_cast<int>(st.hands.size())) + Out(i) * 4.0f);
				hand.chips.insert(hand.chips.end(), more.begin(), more.end());
				hand.bet *= 2;
				hand.doubled = true;
				const int card = _bj.shoe.Draw(_rng);
				hand.cards.push_back(card);
				hand.refs.push_back(BjDeal(card, CardAt(i, _bj.turnHand, static_cast<int>(st.hands.size()), static_cast<int>(hand.cards.size()) - 1),
					CardYaw(i) + 1.5708f, true, 0.0f));  // (the double's card across the others)
				hand.done = true;
				st.last = std::format("DOUBLED · {}", ValueText(hand.cards, hand.split));
				BjSay(std::format("{} {} DOWN · {}", who, st.you ? "DOUBLE" : "DOUBLES", ValueText(hand.cards, hand.split)));
				break;
			}
		case bj::Move::kSplit:
			{
				if (!first || !bj::IsPair(hand.cards) || st.hands.size() > 1 || !pay(hand.bet)) {
					if (st.you) {
						BjSay(st.hands.size() > 1 ? "ONE SPLIT A HAND" : "NOT ENOUGH CAPS TO SPLIT", 2);
					}
					return;
				}
				BjHand second;
				second.bet = hand.bet;
				second.split = true;
				hand.split = true;
				second.cards.push_back(hand.cards[1]);
				second.refs.push_back(hand.refs[1]);
				hand.cards.pop_back();
				hand.refs.pop_back();
				const int bet = hand.bet;
				st.hands.push_back(std::move(second));
				auto& h0 = st.hands[0];
				auto& h1 = st.hands[1];
				// the chips: the first bet slid to one side of the circle, the second's made on the other
				BjSlide(h0.chips, BetAt(i, 0, 2), false, 0.0f);
				BjStack(h1.chips, bet, BetAt(i, 1, 2));
				BjPlaceHandCards(i, 0);
				BjPlaceHandCards(i, 1);
				// a card on each
				for (int k = 0; k < 2; ++k) {
					auto&     hk = st.hands[k];
					const int card = _bj.shoe.Draw(_rng);
					hk.cards.push_back(card);
					hk.refs.push_back(BjDeal(card, CardAt(i, k, 2, 1), CardYaw(i), true, 0.35f + 0.3f * static_cast<float>(k)));
				}
				st.last = "SPLITS";
				BjSay(std::format("{} {}", who, st.you ? "SPLIT" : "SPLITS"));
				break;
			}
		}
		_bj.timer = 0.5f;  // (a moment before the next)
		BjNextTurn();
	}

	void Game::BjDealerPlay(float a_dt)
	{
		_bj.timer -= a_dt;
		if (_bj.timer > 0.0f) {
			return;
		}
		if (!_bj.holeUp) {
			_bj.holeUp = true;
			if (_bj.dealerRefs.size() > 1) {
				_bj.cards[_bj.dealerRefs[1]].faceUp = true;
			}
			BjSay(std::format("THE DEALER HAS {}", ValueText(_bj.dealer, false)));
			_bj.timer = 0.9f;
			return;
		}
		// nobody left in (everyone busted): no need to draw
		bool live = false;
		for (const auto& st : _bj.seats) {
			for (const auto& h : st.hands) {
				live |= bj::Count(h.cards).value <= 21 && !bj::IsBlackjack(h.cards, h.split);
			}
		}
		if (live && bj::Count(_bj.dealer).value < 17) {
			const int card = _bj.shoe.Draw(_rng);
			_bj.dealer.push_back(card);
			_bj.dealerRefs.push_back(BjDeal(card, DealerCardAt(static_cast<int>(_bj.dealer.size()) - 1), 0.0f, true, 0.0f));
			BjSay(std::format("THE DEALER DRAWS · {}", ValueText(_bj.dealer, false)));
			_bj.timer = 0.9f;
			return;
		}
		BjSettle();
	}

	void Game::BjSettle()
	{
		_bj.dealerCool = 0.0f;
		DealerGesture(_bj.dealerActor, _bj.dealerHeld, _bj.dealerCool, Gesture::kTalk, 0.0, kDealerStand, 0.0, -1.0);
		const int dealerValue = bj::Count(_bj.dealer).value;
		if (dealerValue > 21) {
			BjSay("THE DEALER BUSTS", 1);
		}
		int  youNet = 0;
		bool youIn = false;
		for (int i = 0; i < kBjSeats; ++i) {
			auto& st = _bj.seats[i];
			if (st.hands.empty()) {
				continue;
			}
			int net = 0;
			for (int h = 0; h < static_cast<int>(st.hands.size()); ++h) {
				auto& hand = st.hands[h];
				hand.result = bj::Return(hand.cards, hand.split, _bj.dealer);
				const int back = static_cast<int>(std::floor(hand.bet * hand.result + 0.5));
				net += back - hand.bet;
				if (back > 0) {
					if (st.you) {
						GivePlayer(back);
					} else {
						AddToPurse(st.id, back);
					}
				}
				const auto at = BetAt(i, h, static_cast<int>(st.hands.size()));
				if (hand.result == 0.0) {
					BjSlide(hand.chips, kRack, true, 0.2f);  // (lost: to the rack)
				} else if (back > hand.bet) {
					// won: the winnings from the rack set down beside the bet (and then all of it the player's)
					Frame frame;
					if (BjFrame(frame)) {
						std::vector<RE::ObjectRefHandle> win;
						BjStack(win, back - hand.bet, kRack);
						BjSlide(win, at + Along(i) * (h == 0 ? -3.6f : 3.6f), false, 0.3f);
						hand.paid = std::move(win);
					}
				}
			}
			// insurance: paid 2 to 1 on a dealer's blackjack, gone otherwise
			if (st.insurance > 0) {
				if (bj::IsBlackjack(_bj.dealer)) {
					const int back = st.insurance * 3;
					net += back - st.insurance;
					if (st.you) {
						GivePlayer(back);
					} else {
						AddToPurse(st.id, back);
					}
				} else {
					net -= st.insurance;
					BjSlide(st.insChips, kRack, true, 0.1f);
				}
			}
			const std::string who = st.you ? std::string("YOU") : Upper(st.name);
			if (!st.you) {
				auto  ref = st.actor.get();
				auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
				const bool natural = std::ranges::any_of(st.hands, [](const BjHand& h) { return h.result >= 2.5; });
				Voice(actor, net > 0 ? (natural ? 2 : 1) : net < 0 ? -1 : 0);
			}
			if (net > 0) {
				BjSay(std::format("{} {} {}", who, st.you ? "WIN" : "WINS", net), 1);
				BjIdle(i, 3);
			} else if (net < 0) {
				BjSay(std::format("{} {} {}", who, st.you ? "LOSE" : "LOSES", -net), st.you ? 2 : 0);
			} else {
				BjSay(std::format("{} {}", who, "PUSH"));
			}
			if (st.you) {
				youIn = true;
				youNet = net;
			}
		}
		if (youIn) {
			_bj.bannerTitle = youNet > 0 ? std::format("YOU WIN {}", youNet) : youNet < 0 ? std::format("YOU LOSE {}", -youNet) : std::string("PUSH");
			_bj.bannerSub = std::format("THE DEALER {}", dealerValue > 21 ? std::string("BUSTS") : "HAS " + ValueText(_bj.dealer, false));
			_bj.bannerTone = youNet > 0 ? 1 : youNet < 0 ? 2 : 0;
			_bj.bannerAge = 0.0f;
		}
		_bj.phase = BjPhase::kSettle;
		_bj.timer = 2.6f;
	}

	void Game::BjClear()
	{
		// the cards into the discard holder, the chips left on the table to their players (gone)
		for (auto& c : _bj.cards) {
			if (!c.ref.get()) {
				c.mucked = true;  // (not out yet: it never will be)
				c.spawned = true;
				c.t = 1.0f;
				continue;
			}
			if (c.mucked) {
				continue;
			}
			const float e = 1.0f - (1.0f - c.t) * (1.0f - c.t);
			c.from = c.from + (c.to - c.from) * e;
			c.to = kDiscard;
			c.t = 0.0f;
			c.delay = 0.0f;
			c.spin = 0.0f;
			c.mucked = true;
			c.faceUp = false;
		}
		for (int i = 0; i < kBjSeats; ++i) {
			auto& st = _bj.seats[i];
			const auto off = AtR(i, kEdgeR + 6.0f);
			for (auto& h : st.hands) {
				BjSlide(h.chips, off, true, 0.0f);
				BjSlide(h.paid, off, true, 0.1f);
			}
			BjSlide(st.insChips, off, true, 0.0f);
			st.hands.clear();
			st.insurance = 0;
			if (st.you && !_bjSeated && i == _bjYou) {
				st = BjSeat{};  // (you'd left the table while your hand was played out: your place is free now)
				_bjYou = -1;
			}
		}
		_bj.dealer.clear();
		_bj.dealerRefs.clear();
		_bj.holeUp = false;
		_bj.turnSeat = -1;
	}

	void Game::BjTickRound(float a_dt)
	{
		const auto& s = Settings::Get();
		switch (_bj.phase) {
		case BjPhase::kBetting:
			{
				// the settlers put their bets down
				for (int i = 0; i < kBjSeats; ++i) {
					auto& st = _bj.seats[i];
					if (!st.used || st.you || st.step != Seat::Step::kPlaying || st.bet > 0 || st.atSpot < 1.0f || st.rounds <= 0) {
						continue;
					}
					const int purse = Purse(st.id);
					const int want = std::clamp((purse / 12 / 5) * 5, 5, std::min(100, s.maxBet));
					const int bet = TakePurse(st.id, want);
					if (bet <= 0) {
						continue;
					}
					st.bet = bet;
					BjStack(st.betChips, bet, Circle(i));
					BjSound(Audio::Bank::kChip, 0.4f);
					if (_bj.dealIn < 0.0f) {
						_bj.dealIn = kBetWindow;
					}
				}
				if (_bj.dealIn >= 0.0f) {
					_bj.dealIn -= a_dt;
					// (someone at the table still sitting down: the cards wait for them, a while: 20 s)
					const bool sitting = std::ranges::any_of(_bj.seats, [](const BjSeat& st) {
						return st.used && !st.you && st.bet == 0 && st.rounds > 0 && st.atSpot < 1.0f;
					});
					if (_bj.dealIn <= 0.0f && sitting && _bj.sitWait < 20.0f) {
						_bj.sitWait += a_dt;
					} else if (_bj.dealIn <= 0.0f) {
						_bj.sitWait = 0.0f;
						BjStartDeal();
					}
				}
				break;
			}
		case BjPhase::kDealing:
			_bj.timer -= a_dt;
			if (_bj.timer <= 0.0f) {
				// the dealer showing an ace: insurance offered (to you; settlers never take it); a ten or an ace: the dealer
				// looks under it for blackjack
				const int up = bj::Points(_bj.dealer[0]);
				const bool youIn = _bjYou >= 0 && !_bj.seats[_bjYou].hands.empty();
				if (up == 11 && youIn && _bjSeated) {
					_bj.phase = BjPhase::kInsurance;
					_bj.timer = 12.0f;
					BjSay("THE DEALER SHOWS AN ACE · INSURANCE?");
					break;
				}
				if ((up == 11 || up == 10) && bj::IsBlackjack(_bj.dealer)) {
					_bj.holeUp = true;
					_bj.cards[_bj.dealerRefs[1]].faceUp = true;
					BjSay("THE DEALER HAS BLACKJACK", 2);
					BjSettle();
					break;
				}
				_bj.phase = BjPhase::kPlayers;
				_bj.turnSeat = -1;
				_bj.turnHand = 0;
				BjNextTurn();
			}
			break;
		case BjPhase::kInsurance:
			_bj.timer -= a_dt;
			if (_bj.timer <= 0.0f) {
				BjKey(65);  // (no answer: no insurance)
			}
			break;
		case BjPhase::kPlayers:
			{
				if (_bj.turnSeat < 0) {
					BjNextTurn();
					break;
				}
				_bj.timer -= a_dt;
				if (_bj.timer > 0.0f) {
					break;
				}
				auto& st = _bj.seats[_bj.turnSeat];
				if (!st.used) {
					st.hands[_bj.turnHand].done = true;
					BjNextTurn();
					break;
				}
				if (st.you) {
					break;  // (your buttons)
				}
				st.last = "THINKING";
				_bj.think -= a_dt;
				if (_bj.think > 0.0f) {
					break;
				}
				const auto& hand = st.hands[_bj.turnHand];
				const bool  first = hand.cards.size() == 2;
				const auto  move = bj::Basic(hand.cards, _bj.dealer[0], first && Purse(st.id) >= hand.bet, first && st.hands.size() == 1 && Purse(st.id) >= hand.bet);
				BjAct(move);
				break;
			}
		case BjPhase::kDealer:
			BjDealerPlay(a_dt);
			break;
		case BjPhase::kSettle:
			_bj.timer -= a_dt;
			if (_bj.timer <= 0.0f) {
				BjClear();
				_bj.phase = BjPhase::kClear;
				_bj.timer = 1.2f;
			}
			break;
		case BjPhase::kClear:
			_bj.timer -= a_dt;
			if (_bj.timer <= 0.0f) {
				std::erase_if(_bj.cards, [](const PkCard& c) { return c.mucked && !c.ref.get(); });
				if (_bj.cards.size() > 0 && std::ranges::all_of(_bj.cards, [](const PkCard& c) { return c.mucked; })) {
					// (still on their way: a moment longer)
					_bj.timer = 0.3f;
					break;
				}
				for (auto& c : _bj.cards) {
					Remove(c.ref);  // (anything still out: off, not left behind)
				}
				_bj.cards.clear();
				_bj.phase = BjPhase::kBetting;
				_bj.dealIn = -1.0f;
				DealerGesture(_bj.dealerActor, _bj.dealerHeld, _bj.dealerCool, Gesture::kBeckon, 0.0, kDealerStand, 0.0, -1.0);  // (bets)
			}
			break;
		}
	}

	// ---------------------------------------------------------------- you

	void Game::BjSitDown(RE::TESObjectREFR* a_table)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* camera = RE::PlayerCamera::GetSingleton();
		if (!player || !camera || _seated || _pkSeated || _bjSeated) {
			return;
		}
		if (_bj.table.get().get() != a_table) {
			BjEnd("you sat down at another blackjack table");
			BjStart(a_table);
		} else {
			Settings::Get().Load();
		}
		Frame frame;
		if (!BjFrame(frame)) {
			return;
		}
		// your place: the one you had, or the free one nearest you
		int best = _bjYou;
		if (best < 0) {
			float bestD = 1e9f;
			for (int i = 0; i < kBjSeats; ++i) {
				if (_bj.seats[i].used) {
					continue;
				}
				float      heading = 0.0f;
				const auto at = BjStand(frame, i, heading);
				const float d = Distance2D(at, player->data.location);
				if (d < bestD) {
					bestD = d;
					best = i;
				}
			}
		}
		if (best < 0) {
			Notify("Blackjack: the table's full");
			return;
		}
		auto& st = _bj.seats[best];
		if (!st.used) {
			st = BjSeat{};
			st.used = true;
			st.you = true;
			st.name = player->GetDisplayFullName();
			if (st.name.empty()) {
				st.name = "You";
			}
			st.step = Seat::Step::kPlaying;
		}
		_bjYou = best;
		if (!InFreeCamera()) {
			camera->ToggleFreeCameraMode(false);
		}
		_entryYaw = player->data.angle.z;
		_entryPitch = player->data.angle.x;
		if (auto* third = static_cast<RE::ThirdPersonState*>(camera->cameraStates[RE::CameraStates::k3rdPerson].get())) {
			_entryFreeRotation = third->freeRotation;
		}
		_haveEntryView = true;
		{
			std::scoped_lock lock(_camMutex);
			_camMapped = false;
			_mapFrames = 0;
			if (camera->cameraRoot) {
				_camPos = camera->cameraRoot->world.translate;
				_haveCamPos = true;
				_lastCam = std::chrono::steady_clock::now();
			} else {
				_haveCamPos = false;
			}
			_bjLookYaw = _bjLookPitch = 0.0f;
			_mouseDX = _mouseDY = 0.0;
		}
		_bjTopView = false;
		_bj.seatedAge = 0.0f;
		_padUsed = false;
		_pkPtrX = _pkPtrY = -1.0;
		_bjSeated = true;
		BlockControls(true);
		QuietNpcs(true);
		log::info("blackjack: you take place {}", best);
		BjSay("YOU TAKE A PLACE · PUT A BET DOWN");
	}

	void Game::BjStandUp()
	{
		_afterTable = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() + 1500;
		if (!_bjSeated) {
			return;
		}
		_bjSeated = false;
		if (_bjYou >= 0) {
			auto& st = _bj.seats[_bjYou];
			// a bet down and not dealt yet: back; hands being played: they stand where they are (and are settled as usual)
			if (st.bet > 0) {
				GivePlayer(st.bet);
				st.bet = 0;
				for (auto& h : st.betChips) {
					Remove(h);
				}
				st.betChips.clear();
			}
			for (auto& h : st.hands) {
				h.done = true;
			}
			if (st.hands.empty()) {
				st = BjSeat{};
				_bjYou = -1;
			}
		}
		_camPitch = -1.0f;
		HoldPlayerView();
		_haveEntryView = false;
		BlockControls(false);
		QuietNpcs(false);
		PkHoldMenuKeys(false);
		if (auto* camera = RE::PlayerCamera::GetSingleton(); camera && InFreeCamera()) {
			camera->ToggleFreeCameraMode(false);
		}
		log::info("blackjack: you leave the table");
	}

	void Game::BjKey(int a_key)
	{
		if (_bjYou < 0) {
			return;
		}
		auto& st = _bj.seats[_bjYou];
		if (a_key >= 80 && a_key <= 83) {
			// the 1-4 keys (A, B, X, Y): the chips while bets are open, the insurance answer (1 yes, 2 no), or on your
			// turn hit, stand, double, split
			const int n = a_key - 80;
			if (_bj.phase == BjPhase::kBetting) {
				a_key = 50 + n;
			} else if (_bj.phase == BjPhase::kInsurance) {
				if (n > 1) {
					return;
				}
				a_key = 64 + n;
			} else {
				a_key = 60 + n;
			}
		}
		switch (a_key) {
		case 50:
		case 51:
		case 52:
		case 53: BjBet(rl::kChipValues[a_key - 50]); return;
		case 54: BjBet(0); return;
		case 56:
			// the same bet as last time
			if (_bj.phase == BjPhase::kBetting && st.bet == 0 && st.lastBet > 0) {
				BjBet(st.lastBet);
			}
			return;
		case 55:
			if (_bj.phase == BjPhase::kBetting && st.bet > 0) {
				BjStartDeal();  // (you're ready: the cards now)
			}
			return;
		case 66: _bjTopView = !_bjTopView; return;
		case 69: _bjShowHand = !_bjShowHand; return;
		case 70: _bjShowDealer = !_bjShowDealer; return;
		case 67: BjStandUp(); return;
		case 68:
			if (auto table = _bj.table.get(); table && IsKitTable(table.get()) && _bj.phase == BjPhase::kBetting) {
				BjStandUp();
				PackUpBlackjackTable(table.get());
			}
			return;
		case 64:
		case 65:
			if (_bj.phase != BjPhase::kInsurance) {
				return;
			}
			if (a_key == 64 && !st.hands.empty()) {
				const int cost = st.hands[0].bet / 2;
				if (cost > 0 && TakeFromPlayer(cost)) {
					st.insurance = cost;
					BjStack(st.insChips, cost, AtR(_bjYou, kInsureR));
					BjSay(std::format("YOU TAKE INSURANCE FOR {}", cost));
				}
			}
			// the dealer looks under the ace
			if (bj::IsBlackjack(_bj.dealer)) {
				_bj.holeUp = true;
				_bj.cards[_bj.dealerRefs[1]].faceUp = true;
				BjSay("THE DEALER HAS BLACKJACK", 2);
				BjSettle();
			} else {
				if (st.insurance > 0) {
					BjSay("NO BLACKJACK · THE INSURANCE IS LOST");
				}
				_bj.phase = BjPhase::kPlayers;
				_bj.turnSeat = -1;
				BjNextTurn();
			}
			return;
		default: break;
		}
		// your turn
		if (_bj.phase != BjPhase::kPlayers || _bj.turnSeat != _bjYou || _bj.timer > 0.0f) {
			return;
		}
		switch (a_key) {
		case 60: BjAct(bj::Move::kHit); break;
		case 61: BjAct(bj::Move::kStand); break;
		case 62: BjAct(bj::Move::kDouble); break;
		case 63: BjAct(bj::Move::kSplit); break;
		default: break;
		}
	}

	void Game::BjCamera(const Frame& a_frame, float a_dt)
	{
		const auto  o = Out(_bjYou);
		const float baseYaw = HeadingOf(a_frame, -o.x, -o.y);
		const auto& cs = Settings::Get();
		const bool  focus = util::GameHasFocus();
		const bool  left = focus && cs.keyTurnLeft > 0 && (::GetAsyncKeyState(cs.keyTurnLeft) & 0x8000);
		const bool  right = focus && cs.keyTurnRight > 0 && (::GetAsyncKeyState(cs.keyTurnRight) & 0x8000);
		const int   turn = std::clamp((right ? 1 : 0) - (left ? 1 : 0) + _padTurn.load(), -1, 1);
		const bool  lookUp = focus && cs.keyLookUp > 0 && (::GetAsyncKeyState(cs.keyLookUp) & 0x8000);
		const bool  lookDown = focus && cs.keyLookDown > 0 && (::GetAsyncKeyState(cs.keyLookDown) & 0x8000);
		const int   tilt = (lookDown ? 1 : 0) - (lookUp ? 1 : 0);
		PkHoldMenuKeys(left || right || lookUp || lookDown);
		// from behind your place, over your card box, across to the dealer's cards
		const float up = cs.pkSeatHeight;
		const auto  eyeAt = AtR(_bjYou, kCardBoxR + cs.pkSeatBack);
		const auto  eye = ToWorld(a_frame, eyeAt.x, eyeAt.y, kFeltZ + up);
		const float basePitch = std::atan2(up, cs.pkSeatBack + 10.0f);
		std::scoped_lock lock(_camMutex);
		float sw, sh;
		{
			std::scoped_lock blk(_btnMutex);
			sw = _scrW;
			sh = _scrH;
		}
		const double k = sh / 1080.0 * cs.cursorSpeed / 0.07;
		if (_pkPtrX < 0.0) {
			_pkPtrX = sw * 0.5;
			_pkPtrY = sh * 0.75;
		}
		if (_mouseDX != 0.0 || _mouseDY != 0.0) {
			_padUsed = false;
		}
		_pkPtrX = std::clamp(_pkPtrX + _mouseDX * k, 0.0, static_cast<double>(sw - 1.0f));
		_pkPtrY = std::clamp(_pkPtrY + _mouseDY * k, 0.0, static_cast<double>(sh - 1.0f));
		_mouseDX = _mouseDY = 0.0;
		_bjLookYaw = std::clamp(_bjLookYaw + turn * 1.6f * a_dt, -1.4f, 1.4f);
		// the look keys tilt it: from a little above level (the dealer, the room) to steeply down (your cards)
		_bjLookPitch = std::clamp(_bjLookPitch + tilt * 1.0f * a_dt, -0.5f - basePitch, 1.2f - basePitch);
		if (_bjTopView) {
			_camWant = ToWorld(a_frame, 0.0, kArcY - 14.0, kFeltZ + cs.pkTopHeight * 1.15f);
			_camYaw = baseYaw + _bjLookYaw;
			_camPitch = -1.0f;
		} else {
			_camWant = eye;
			_camYaw = baseYaw + _bjLookYaw;
			_camPitch = basePitch + _bjLookPitch;
		}
	}

	void Game::CardKey(int a_key)
	{
		if (_slSeated) {
			SlKey(a_key);
			return;
		}
		if (_bjSeated) {
			BjKey(a_key);
		} else {
			PkKey(a_key);
		}
	}

	// ---------------------------------------------------------------- the HUD

	void Game::BjHud(HudState& a_h) const
	{
		a_h.blackjack = true;
		a_h.bjShowHand = _bjShowHand;
		a_h.bjShowDealer = _bjShowDealer;
		a_h.poker = false;
		a_h.seated = true;
		a_h.pkTitle = "BLACKJACK · PAYS 3 TO 2";
		a_h.pkBoardLabel = "YOUR BET";
		a_h.caps = PlayerCaps();
		a_h.pkAge = _bj.seatedAge;
		a_h.pkPad = _padUsed.load();
		a_h.pkTop = _bjTopView;
		if (auto table = _bj.table.get()) {
			a_h.pkKit = IsKitTable(table.get());
		}
		{
			std::scoped_lock lock(_camMutex);
			a_h.pkPtrX = static_cast<float>(_pkPtrX);
			a_h.pkPtrY = static_cast<float>(_pkPtrY);
		}
		const BjSeat* me = _bjYou >= 0 ? &_bj.seats[_bjYou] : nullptr;
		// the places: you at the top, then left to right
		for (int pass = 0; pass < 2; ++pass) {
			for (int i = 0; i < kBjSeats; ++i) {
				const auto& st = _bj.seats[i];
				if (!st.used || st.you != (pass == 0)) {
					continue;
				}
				HudState::PokerRow r;
				r.name = st.you ? std::string("YOU") : Upper(st.name);
				r.you = st.you;
				r.stack = st.you ? PlayerCaps() : Purse(st.id);
				int bet = st.bet + st.insurance;
				std::string value;
				for (const auto& h : st.hands) {
					bet += h.bet;
					value += (value.empty() ? "" : " / ") + ValueText(h.cards, h.split);
				}
				r.bet = bet;
				r.toAct = _bj.phase == BjPhase::kPlayers && _bj.turnSeat == i;
				r.status = !st.last.empty() && st.last != "THINKING" ? st.last : value;
				if (r.toAct && !st.you) {
					r.status = "THINKING";
				}
				if (!st.you && st.step != Seat::Step::kPlaying) {
					r.status = "ON THE WAY";
				}
				a_h.pkRows.push_back(std::move(r));
			}
		}
		// the dealer's cards
		for (std::size_t k = 0; k < _bj.dealer.size(); ++k) {
			const bool shown = k < _bj.dealerRefs.size() && _bj.cards.size() > _bj.dealerRefs[k] && _bj.cards[_bj.dealerRefs[k]].ref.get();
			if (!shown && _bj.phase == BjPhase::kDealing) {
				continue;  // (not out of the shoe yet)
			}
			a_h.bjDealer.push_back(k == 1 && !_bj.holeUp ? 52 : _bj.dealer[k]);
		}
		if (!_bj.dealer.empty()) {
			a_h.bjDealerValue = _bj.holeUp ? ValueText(_bj.dealer, false) : std::format("SHOWS {}", bj::Points(_bj.dealer[0]) == 11 ? std::string("AN ACE") : std::to_string(bj::Points(_bj.dealer[0])));
		}
		// your hands
		a_h.bjPhase = 1;
		if (me) {
			a_h.bjBet = me->bet;
			a_h.bjLastBet = me->lastBet;
			a_h.pkPot = me->bet;
			for (int hi = 0; hi < static_cast<int>(me->hands.size()); ++hi) {
				const auto&       h = me->hands[hi];
				HudState::BjHand  out;
				out.cards = h.cards;
				out.value = ValueText(h.cards, h.split);
				out.bet = h.bet;
				out.active = _bj.phase == BjPhase::kPlayers && _bj.turnSeat == _bjYou && _bj.turnHand == hi;
				if (h.result >= 0.0) {
					const int back = static_cast<int>(std::floor(h.bet * h.result + 0.5));
					out.result = h.result == 0.0 ? std::string("LOST") : h.result == 1.0 ? std::string("PUSH") : std::format("WON {}", back - h.bet);
				}
				a_h.pkPot += h.bet;
				a_h.bjHands.push_back(std::move(out));
			}
			a_h.pkPot += me->insurance;
		}
		const bool yourTurn = me && _bj.phase == BjPhase::kPlayers && _bj.turnSeat == _bjYou;
		a_h.pkYourTurn = yourTurn;
		switch (_bj.phase) {
		case BjPhase::kBetting:
			a_h.bjPhase = 0;
			a_h.pkInfo = _bj.dealIn >= 0.0f ? std::format("BETS CLOSE IN {}", static_cast<int>(std::ceil(_bj.dealIn))) : std::string("PLACE YOUR BETS");
			break;
		case BjPhase::kInsurance:
			a_h.bjPhase = 3;
			a_h.pkInfo = "INSURANCE?";
			if (me && !me->hands.empty()) {
				a_h.bjInsurance = me->hands[0].bet / 2;
			}
			break;
		case BjPhase::kPlayers:
			a_h.bjPhase = yourTurn ? 2 : 1;
			a_h.pkInfo = yourTurn ? std::string("YOUR TURN") : std::string("PLAYERS' TURNS");
			break;
		case BjPhase::kDealer:
			a_h.bjPhase = 4;
			a_h.pkInfo = "THE DEALER PLAYS";
			break;
		case BjPhase::kSettle:
		case BjPhase::kClear:
			a_h.bjPhase = 5;
			a_h.pkInfo = "NEXT HAND COMING UP";
			break;
		default:
			a_h.pkInfo = "CARDS";
			break;
		}
		a_h.bjDealIn = _bj.phase == BjPhase::kBetting ? _bj.dealIn : -1.0f;
		if (yourTurn) {
			const auto& h = me->hands[_bj.turnHand];
			a_h.pkHand = ValueText(h.cards, h.split);
			a_h.bjCanDouble = h.cards.size() == 2 && PlayerCaps() >= h.bet;
			a_h.bjCanSplit = h.cards.size() == 2 && bj::IsPair(h.cards) && me->hands.size() == 1 && PlayerCaps() >= h.bet;
		} else if (me && !me->hands.empty()) {
			a_h.pkHand = ValueText(me->hands[0].cards, me->hands[0].split);
		}
		a_h.log = _bj.log;
		if (_bj.bannerAge < 5.0f && !_bj.bannerTitle.empty()) {
			a_h.banner = _bj.bannerTone == 1 ? 2 : _bj.bannerTone == 2 ? 3 : 4;
			a_h.bannerTitle = _bj.bannerTitle;
			a_h.bannerSub = _bj.bannerSub;
			a_h.bannerAge = _bj.bannerAge;
		}
	}

	// ---------------------------------------------------------------- saves

	void Game::BjBeforeSave()
	{
		// nothing of ours in the save: a hand on is called off (everyone's bets back), the cards and chips off
		if (!_bj.table) {
			return;
		}
		for (auto& st : _bj.seats) {
			if (!st.used) {
				continue;
			}
			int back = st.insurance;
			for (const auto& h : st.hands) {
				back += h.result < 0.0 ? h.bet : 0;
			}
			if (back > 0) {
				if (st.you) {
					GivePlayer(back);
				} else {
					AddToPurse(st.id, back);
				}
			}
			if (st.you && st.bet > 0) {
				_bj.lifted = st.bet;  // (your bet for the next hand: in your pocket while the save's written)
				GivePlayer(st.bet);
			}
			for (auto& h : st.hands) {
				for (auto& c : h.chips) {
					Remove(c);
				}
				for (auto& c : h.paid) {
					Remove(c);
				}
			}
			st.hands.clear();
			st.insurance = 0;
			for (auto& h : st.betChips) {
				Remove(h);
			}
			st.betChips.clear();
			for (auto& h : st.insChips) {
				Remove(h);
			}
			st.insChips.clear();
			if (!st.you && st.bet > 0) {
				AddToPurse(st.id, st.bet);
				st.bet = 0;
			}
		}
		// (the chairs, the dealer and the players stay as they are: taken off for the save, as they were, everyone
		// sat in a chair was got up out of it and the dealer let go. Who's where goes in the co-save: SaveRoster)
		for (auto& c : _bj.cards) {
			Remove(c.ref);
		}
		_bj.cards.clear();
		for (auto& m : _bj.moves) {
			Remove(m.ref);
		}
		_bj.moves.clear();
		_bj.dealer.clear();
		_bj.dealerRefs.clear();
		_bj.phase = BjPhase::kBetting;
		_bj.dealIn = -1.0f;
		_bj.turnSeat = -1;
	}

	void Game::BjAfterSave()
	{
		if (_bj.lifted > 0 && _bjYou >= 0) {
			const int bet = _bj.lifted;
			_bj.lifted = 0;
			_bj.seats[_bjYou].bet = 0;
			BjBet(bet);  // (put back down)
		}
		_bj.lifted = 0;
	}
}
