// The poker table: No-limit Texas Hold'em (pk/Holdem.h) at fo4roulette.esl's Poker Table. One poker table is in play
// at a time, the nearest within reach. Up to seven seats: you (activate the table: the nearest free seat) and settlers
// who come over with their own caps (the roulette purses). The table deals itself; the cards are the 52 card models,
// slid out from the dealer's tray and turned over; the bets and the pot are chip stacks.
//
// The table's space (tools/build_poker.py): X across (the chip tray, where a dealer would sit, at +X), Y along, Z up;
// the felt 53 up, a stadium 20 across its middle and 63 to each end; the padded rail round it out to 45 / 89.

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
		constexpr float kFeltZ = 63.0f;  // (build_poker.py: 53 at the model's proportions, raised 10)
		constexpr float kFeltR = 20.0f;   // the felt: half its width (and its ends' radius)...
		constexpr float kFeltC = 43.0f;   // ...and where its straight sides end
		constexpr float kRailR = 45.0f;   // the rail's outside
		constexpr float kRailC = 44.0f;
		constexpr float kStandOff = 26.0f;  // people stand this far out from the rail
		constexpr float kCardT = 0.06f;     // a card's thickness (build_poker.py)
		constexpr float kCardW = 3.9f;      // (build_poker.py: 3.9 x 6.24)
		constexpr float kChipH = static_cast<float>(rl::table::kChipHeight);
		constexpr float kChipR = static_cast<float>(rl::table::kChipRadius);
		constexpr int   kMaxStack = 8;
		constexpr float kArrived = 30.0f;
		constexpr float kSnap = 12.0f;
		constexpr float kPersonalSpace = 50.0f;
		constexpr float kSettleIn = 2.0f;

		// settlers' idles at the table (vanilla Fallout4.esm, as at the roulette table)
		constexpr std::array<RE::TESFormID, 1> kLookIdles{ 0x001E81AC };  // thinking (the shopping looks turned them about)
		constexpr RE::TESFormID                kThinkIdle = 0x001E81AC;                          // thinking: weighing up their hand
		constexpr RE::TESFormID                kChipsIdle = 0x001A8A0D;                          // IdlePlaceEnvelopeOnTable: chips in
		constexpr std::array<RE::TESFormID, 2> kWinIdles{ 0x0010BA3A, 0x0010F9C0 };              // cheering, fight winner
		constexpr RE::TESFormID                kNoIdle = 0x00038C7A;                             // HeadShakeNo: a fold, a lost showdown

		// voice: the Combat Zone crowd's lines (custom topics by keyword, every common voice has them)
		constexpr RE::TESFormID kCheer = 0x00110396;   // Crowd_ApprovalSounds_TopicSubType
		constexpr RE::TESFormID kLaugh = 0x00110395;   // Crowd_Laughter_TopicSubType
		constexpr RE::TESFormID kGroan = 0x00110398;   // Crowd_DisapprovalSounds_TopicSubType

		// the seats, clockwise from the dealer's place (+X): the table's cup holders, table space
		constexpr std::array<std::pair<float, float>, 7> kHolders{ {
			{ 27.9f, -47.1f }, { 1.1f, -71.2f }, { -25.8f, -47.1f }, { -25.8f, -0.2f }, { -25.8f, 46.7f }, { 1.1f, 70.8f }, { 27.9f, 46.7f },
		} };

		// from the dealer's side (+X, the chip tray) in: the deck, the board, then the pot
		const RE::NiPoint3 kDeck{ 15.0f, 0.0f, kFeltZ };  // where cards come from (by the tray)
		const RE::NiPoint3 kMuck{ 15.0f, 13.0f, kFeltZ }; // where folded hands go
		const RE::NiPoint3 kPot{ -6.0f, 0.0f, kFeltZ };

		// the stadium of half-width a_r with straight sides to +-a_c: the nearest point on its edge to (x, y), and the
		// outward normal there
		void Stadium(float a_x, float a_y, float a_r, float a_c, RE::NiPoint3& a_edge, RE::NiPoint3& a_normal)
		{
			if (std::abs(a_y) <= a_c) {
				const float sx = a_x >= 0.0f ? 1.0f : -1.0f;
				a_normal = { sx, 0.0f, 0.0f };
				a_edge = { sx * a_r, a_y, 0.0f };
				return;
			}
			const float cy = a_y > 0.0f ? a_c : -a_c;
			float       dx = a_x, dy = a_y - cy;
			const float l = std::max(1e-4f, std::hypot(dx, dy));
			dx /= l;
			dy /= l;
			a_normal = { dx, dy, 0.0f };
			a_edge = { dx * a_r, cy + dy * a_r, 0.0f };
		}

		// a seat's direction out from the table (unit, table space)
		RE::NiPoint3 SeatNormal(int a_seat)
		{
			RE::NiPoint3 e, n;
			Stadium(kHolders[a_seat].first, kHolders[a_seat].second, kRailR, kRailC, e, n);
			return n;
		}

		// a seat's hole cards (k = 0, 1), on the felt in front of them
		RE::NiPoint3 HoleSpot(int a_seat, int a_k)
		{
			RE::NiPoint3 e, n;
			Stadium(kHolders[a_seat].first, kHolders[a_seat].second, kFeltR, kFeltC, e, n);
			const RE::NiPoint3 side{ n.y, -n.x, 0.0f };
			const RE::NiPoint3 p = e - n * 6.0f + side * ((a_k == 0 ? -1.0f : 1.0f) * (kCardW * 0.5f + 0.4f));
			return { p.x, p.y, kFeltZ };
		}

		// a player's own caps: on the felt to the left of their cards
		RE::NiPoint3 StackSpot(int a_seat)
		{
			RE::NiPoint3 e, n;
			Stadium(kHolders[a_seat].first, kHolders[a_seat].second, kFeltR, kFeltC, e, n);
			const RE::NiPoint3 side{ n.y, -n.x, 0.0f };
			const RE::NiPoint3 p = e - n * 5.0f - side * (kCardW + 4.5f);
			return { p.x, p.y, kFeltZ };
		}

		// the best five of seven: which cards make it (for lifting them at the showdown)
		std::vector<int> BestFive(const std::array<pk::Card, 2>& a_hole, const std::vector<pk::Card>& a_board)
		{
			std::vector<pk::Card> all{ a_hole[0], a_hole[1] };
			all.insert(all.end(), a_board.begin(), a_board.end());
			std::vector<int> best;
			pk::Score        top = 0;
			const int        n = static_cast<int>(all.size());
			for (int a = 0; a < n; ++a) {
				for (int b = a + 1; b < n; ++b) {
					std::array<pk::Card, 5> five{};
					int                     k = 0;
					for (int i = 0; i < n && k < 5; ++i) {
						if (i != a && i != b) {
							five[k++] = all[i];
						}
					}
					if (k < 5) {
						continue;
					}
					const auto sc = pk::Evaluate(std::span<const pk::Card>(five.data(), 5));
					if (sc > top) {
						top = sc;
						best.assign(five.begin(), five.end());
					}
				}
			}
			return best;
		}

		RE::NiPoint3 BetSpot(int a_seat)
		{
			RE::NiPoint3 e, n;
			Stadium(kHolders[a_seat].first, kHolders[a_seat].second, kFeltR, kFeltC, e, n);
			const RE::NiPoint3 p = e - n * 13.0f;
			return { p.x, p.y, kFeltZ };
		}

		RE::NiPoint3 BoardSpot(int a_k)
		{
			return { 7.0f, (static_cast<float>(a_k) - 2.0f) * 5.0f, kFeltZ };
		}

		float Distance2D(const RE::NiPoint3& a, const RE::NiPoint3& b) { return std::hypot(a.x - b.x, a.y - b.y); }

		std::string Upper(std::string a_s)
		{
			std::ranges::transform(a_s, a_s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			return a_s;
		}

		bool InFreeCamera()
		{
			auto* camera = RE::PlayerCamera::GetSingleton();
			auto* free = camera ? camera->cameraStates[RE::CameraStates::kFree].get() : nullptr;
			return camera && free && camera->currentState.get() == free;
		}

		bool Usable(RE::Actor* a_actor)
		{
			return a_actor && !a_actor->IsDisabled() && !a_actor->IsDead(false) && a_actor->Get3D();
		}

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

		// a rotation for something lying on the felt: its top (+Y) along a_yaw (table space, radians from +Y towards
		// +X), turned over about its long axis by a_flip (0 face up, pi face down); world = this * the table's
		RE::NiMatrix3 Lying(float a_yaw, float a_flip)
		{
			const RE::NiPoint3 y{ std::sin(a_yaw), std::cos(a_yaw), 0.0f };
			const RE::NiPoint3 x{ y.y, -y.x, 0.0f };
			const RE::NiPoint3 z{ 0.0f, 0.0f, 1.0f };
			const float        c = std::cos(a_flip), s = std::sin(a_flip);
			const RE::NiPoint3 x2 = x * c - z * s;
			const RE::NiPoint3 z2 = x * s + z * c;
			return util::FromAxes(x2, y, z2);
		}
	}

	// ---------------------------------------------------------------- the table

	bool Game::PkFrame(Frame& a_out) const
	{
		auto table = _pk.table.get();
		return table && table->Get3D() && FrameOf(table.get(), a_out);
	}

	bool Game::PkHasSeat(RE::TESFormID a_id) const
	{
		return std::ranges::any_of(_pk.seats, [&](const PkSeat& s) { return s.used && !s.you && s.id == a_id; });
	}

	RE::NiPoint3 Game::PkStand(const Frame& a_frame, int a_seat, float& a_heading) const
	{
		RE::NiPoint3 e, n;
		Stadium(kHolders[a_seat].first, kHolders[a_seat].second, kRailR, kRailC, e, n);
		const RE::NiPoint3 p = e + n * kStandOff;
		a_heading = HeadingOf(a_frame, -n.x, -n.y);
		return ToWorld(a_frame, p.x, p.y, 0.0);
	}

	void Game::PkScan()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->GetParentCell()) {
			return;
		}
		const auto&       s = Settings::Get();
		RE::TESObjectREFR* nearest = nullptr;
		float             nearestD = s.activeDistance;
		util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(player->data.location, s.activeDistance + 500.0f, [&](RE::TESObjectREFR* a_ref) {
				if (a_ref && Forms::IsPokerTable(a_ref) && !a_ref->IsDisabled() && a_ref->Get3D() && !IsKitPreview(a_ref)) {
					const float d = (a_ref->data.location - player->data.location).Length();
					if (d < nearestD) {
						nearestD = d;
						nearest = a_ref;
					}
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		if (auto current = _pk.table.get()) {
			const bool gone = current->IsDisabled() || !current->Get3D();
			const float d = gone ? 1e9f : (current->data.location - player->data.location).Length();
			if (gone || d > s.activeDistance + 400.0f) {
				PkEnd(gone ? "the table's gone" : "you walked away");
			} else {
				return;
			}
		}
		if (nearest && !_pk.table) {
			PkStart(nearest);
		}
	}

	void Game::PkStart(RE::TESObjectREFR* a_table)
	{
		Settings::Get().Load();
		_pk = PokerState{};
		_pk.table = a_table->GetHandle();
		log::info("poker table {:08X} in play", a_table->GetFormID());
	}

	void Game::PkEnd(const char* a_why)
	{
		if (!_pk.table) {
			return;
		}
		// a hand on: everyone's chips in it back to them (nobody wins a hand that never finished)
		if (_pk.inHand) {
			for (auto& p : _pk.hand.players) {
				p.stack += p.committed;
				p.committed = 0;
			}
			for (int s = 0; s < kPkSeats; ++s) {
				if (_pk.seats[s].used && _pk.seats[s].hi >= 0) {
					_pk.seats[s].stack = _pk.hand.players[_pk.seats[s].hi].stack;
				}
			}
			_pk.inHand = false;
		}
		if (_pkSeated) {
			PkStandUp(true);
		}
		PkCashOut();  // (away from it: your stack back in your pocket)
		for (int s = 0; s < kPkSeats; ++s) {
			if (_pk.seats[s].used) {
				PkRelease(s, a_why);
			}
		}
		PkClearTable();
		for (auto& v : _pk.stackChips) {
			for (auto& h : v) {
				Remove(h);
			}
		}
		log::info("poker table no longer in play: {}", a_why);
		_pk = PokerState{};
	}

	void Game::PkClearTable(bool a_cards)
	{
		for (auto& c : _pk.cards) {
			if (a_cards) {
				Remove(c.ref);
			}
		}
		if (a_cards) {
			_pk.cards.clear();
		}
		for (auto& v : _pk.betChips) {
			for (auto& h : v) {
				Remove(h);
			}
			v.clear();
		}
		_pk.betShown.fill(0);
		for (auto& h : _pk.potChips) {
			Remove(h);
		}
		_pk.potChips.clear();
		_pk.potShown = -1;
		_pk.boardShown = 0;
		_pk.peeking = false;
		if (a_cards) {
			for (auto& m : _pk.moves) {
				Remove(m.ref);
			}
			_pk.moves.clear();
		}
	}

	void Game::PkSay(const std::string& a_text, int a_tone)
	{
		_pk.log.push_back({ a_text, a_tone });
		while (_pk.log.size() > 5) {
			_pk.log.pop_front();
		}
		log::info("[poker] {}", a_text);
	}

	// ---------------------------------------------------------------- the tick

	void Game::TickPoker(float a_dt)
	{
		if (!_pk.table) {
			return;
		}
		Frame frame;
		if (!PkFrame(frame)) {
			return;
		}
		_pk.bannerAge += a_dt;
		_pk.potHold = std::max(0.0f, _pk.potHold - a_dt);
		_pk.pushAge += a_dt;
		PkTickSeats(frame, a_dt);
		PkTickHand(frame, a_dt);
		PkTickCards(frame, a_dt);
		PkMakeChips(frame);
		PkTickMoves(frame, a_dt);
		if (_pkSeated && _pkYou >= 0) {
			// the view from your seat: from just behind your cards, straight across the table. The mouse looks about
			// (the turn keys, Q and E, turn too); looking down at your cards turns them up for you
			const auto  n = SeatNormal(_pkYou);
			const auto  a = HoleSpot(_pkYou, 0), b = HoleSpot(_pkYou, 1);
			const auto  mid = (a + b) * 0.5f;
			const float baseYaw = HeadingOf(frame, -n.x, -n.y);
			// (back far enough to see the rail in front of you)
			const auto& cs = Settings::Get();
			// a push in for the river or an all in (in over a moment, held, back out); at the showdown, a drift
			// towards the winner
			const float pa = _pk.pushAge;
			const float push = pa < 0.5f ? pa / 0.5f : pa < 2.2f ? 1.0f : pa < 3.0f ? 1.0f - (pa - 2.2f) / 0.8f : 0.0f;
			const float pe = push * push * (3.0f - 2.0f * push);
			const float back = cs.pkSeatBack * (1.0f - 0.25f * pe), up = cs.pkSeatHeight * (1.0f - 0.15f * pe);
			{
				float target = 0.0f;
				if (_pk.phase == PkPhase::kShowdown && _pk.winnerSeat >= 0 && _pk.winnerSeat != _pkYou && _pk.potHold > 0.0f) {
					const auto w = (HoleSpot(_pk.winnerSeat, 0) + HoleSpot(_pk.winnerSeat, 1)) * 0.5f;
					const float to = HeadingOf(frame, w.x - mid.x, w.y - mid.y);
					target = std::clamp(util::AngleDiff(to, baseYaw) * 0.4f, -0.6f, 0.6f);
				}
				_pk.driftYaw += (target - _pk.driftYaw) * std::min(1.0f, a_dt * 2.0f);
			}
			const auto  eye = ToWorld(frame, mid.x + n.x * back, mid.y + n.y * back, kFeltZ + up);
			const float basePitch = std::atan2(up, back + cs.pkSeatAim);  // (down onto the felt that far in front of your cards)
			const bool  focus = util::GameHasFocus();
			const auto& ks = Settings::Get();
			const bool  left = focus && ks.keyTurnLeft > 0 && (::GetAsyncKeyState(ks.keyTurnLeft) & 0x8000);
			const bool  right = focus && ks.keyTurnRight > 0 && (::GetAsyncKeyState(ks.keyTurnRight) & 0x8000);
			const int   turn = (right ? 1 : 0) - (left ? 1 : 0);
			PkHoldMenuKeys(left || right);
			float yaw, pitch;
			{
				std::scoped_lock lock(_camMutex);
				// the mouse moves the pointer for the buttons; the turn keys turn the view
				float sw, sh;
				{
					std::scoped_lock blk(_btnMutex);
					sw = _scrW;
					sh = _scrH;
				}
				const double k = sh / 1080.0 * Settings::Get().cursorSpeed / 0.07;
				if (_pkPtrX < 0.0) {
					_pkPtrX = sw * 0.5;
					_pkPtrY = sh * 0.75;
				}
				_pkPtrX = std::clamp(_pkPtrX + _mouseDX * k, 0.0, static_cast<double>(sw - 1.0f));
				_pkPtrY = std::clamp(_pkPtrY + _mouseDY * k, 0.0, static_cast<double>(sh - 1.0f));
				_pkLookYaw = std::clamp(_pkLookYaw + turn * 1.6f * a_dt, -1.4f, 1.4f);
				_mouseDX = _mouseDY = 0.0;
				yaw = baseYaw + _pkLookYaw + _pk.driftYaw;
				pitch = basePitch + _pkLookPitch;
				if (_pkTopView) {
					// from above the middle of the table, your side at the bottom of the screen (the turn keys turn it)
					_camWant = ToWorld(frame, n.x * cs.pkTopShift, n.y * cs.pkTopShift, kFeltZ + cs.pkTopHeight);
					_camYaw = baseYaw + _pkLookYaw;
					_camPitch = -1.0f;  // (straight down)
				} else {
					_camWant = eye;
					_camYaw = yaw;
					_camPitch = pitch;
				}
			}
			// where the middle of the view meets the felt: on your cards, they're up for you (put back down when you
			// look away); not at a showdown, when they're up for everyone anyway
			// (or the SHOW MY CARDS button, or the right mouse button held, in either view)
			bool over = _pkShowCards || (focus && (::GetAsyncKeyState(VK_RBUTTON) & 0x8000));
			if (!over && !_pkTopView && pitch > 0.05f) {
				const float  reach = up / std::tan(pitch);
				const auto   at = eye + RE::NiPoint3{ std::sin(yaw) * reach, std::cos(yaw) * reach, -up };
				const auto   cards = ToWorld(frame, mid.x, mid.y, kFeltZ);
				over = std::hypot(at.x - cards.x, at.y - cards.y) < kCardW * 1.6f;
			}
			const bool peek = over && _pk.inHand && _pk.phase != PkPhase::kShowdown;
			if (peek != _pk.peeking) {
				_pk.peeking = peek;
				for (auto& c : _pk.cards) {
					if (c.seat == _pkYou && !c.mucked && _pk.phase != PkPhase::kShowdown) {
						c.faceUp = peek;
					}
				}
			}
		} else {
			PkHoldMenuKeys(false);
		}
	}

	void Game::PkHoldMenuKeys(bool a_hold)
	{
		// the turn keys (Q and E) turn the view at the poker table: while either's down, the game's menus (the workshop's among them)
		// don't open
		if (a_hold == _pkMenusHeld) {
			return;
		}
		auto* manager = RE::BSInputEnableManager::GetSingleton();
		if (!manager) {
			return;
		}
		if (!_pkMenuLayer && !manager->AllocateNewLayer(_pkMenuLayer, "fo4roulette poker turning")) {
			return;
		}
		using Flag = RE::UserEvents::USER_EVENT_FLAG;
		manager->EnableUserEvent(_pkMenuLayer->layerID, Flag::kMenu, !a_hold, RE::UserEvents::SENDER_ID::kGameplay);
		_pkMenusHeld = a_hold;
	}

	// ---------------------------------------------------------------- settlers

	void Game::PkFindPlayer(const Frame& a_frame)
	{
		const auto& s = Settings::Get();
		int         npcs = 0;
		for (const auto& st : _pk.seats) {
			npcs += st.used && !st.you;
		}
		if (!s.pkPlayers || npcs >= s.pkMaxPlayers || _pk.inHand) {
			return;  // (nobody joins in the middle of a hand: they wait for it to finish)
		}
		auto*        player = RE::PlayerCharacter::GetSingleton();
		static auto* npcType = RE::TESForm::GetFormByID<RE::BGSKeyword>(Forms::kActorTypeNPC);
		std::vector<std::pair<float, RE::Actor*>> found;
		std::map<std::string, int>                busy;  // (for the log: why nobody came)
		util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(a_frame.origin, s.bettorRange, [&](RE::TESObjectREFR* a_ref) {
				auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
				if (Usable(actor) && actor != player && !actor->IsChild()) {
					if (const char* why = Npc::Busy(actor)) {
						++busy[why];
						return RE::BSContainer::ForEachResult::kContinue;
					}
				}
				if (!Usable(actor) || actor == player || actor->IsChild() || actor->IsInCombat() || (player && actor->GetHostileToActor(player)) ||
					(npcType && !actor->HasKeywordHelper(npcType, nullptr)) || PkHasSeat(actor->GetFormID()) ||
					std::ranges::any_of(_t.seats, [&](const Seat& st) { return st.id == actor->GetFormID(); }) ||
					std::ranges::any_of(_other.seats, [&](const Seat& st) { return st.id == actor->GetFormID(); })) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				// (a settler with no caps left: a while later, some again)
				if (Purse(actor->GetFormID()) < s.pkBigBlind * 4) {
					AddToPurse(actor->GetFormID(), 150 + static_cast<int>((actor->GetFormID() * 2654435761u) % 250u));
				}
				found.emplace_back((actor->data.location - a_frame.origin).Length(), actor);
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		if (found.empty()) {
			std::string why;
			for (const auto& [k, n] : busy) {
				why += std::format("{}{} {}", why.empty() ? "" : ", ", n, k);
			}
			log::info("poker: nobody free to sit in{}", why.empty() ? std::string(" (no settlers in range)") : " (" + why + ")");
			return;
		}
		std::ranges::sort(found, {}, &std::pair<float, RE::Actor*>::first);
		auto* actor = found[std::uniform_int_distribution<std::size_t>(0, std::min<std::size_t>(found.size(), 4) - 1)(_rng)].second;
		// a free seat nobody's standing at
		std::vector<int> free;
		for (int i = 0; i < kPkSeats; ++i) {
			if (_pk.seats[i].used) {
				continue;
			}
			float      heading = 0.0f;
			const auto at = PkStand(a_frame, i, heading);
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
		auto&     st = _pk.seats[seat];
		st = PkSeat{};
		st.used = true;
		st.actor = actor->GetHandle();
		st.id = actor->GetFormID();
		st.name = actor->GetDisplayFullName();
		st.style = pk::Style::From(st.id);
		st.hands = std::uniform_int_distribution<int>(6, 20)(_rng);
		st.stack = TakePurse(st.id, s.pkNpcBuyIn);
		log::info("poker: {} ({:08X}) sits in at seat {} with {} caps", st.name, st.id, seat, st.stack);
		PkSay(std::format("{} SITS IN WITH {}", Upper(st.name), st.stack));
	}

	void Game::PkRelease(int a_seat, const char* a_why)
	{
		auto& st = _pk.seats[a_seat];
		if (!st.used || st.you) {
			return;
		}
		// out of the hand (if one's on), their chips back in their purse
		if (_pk.inHand && st.hi >= 0 && !_pk.hand.Over()) {
			_pk.hand.Fold(st.hi);
			PkMuck(a_seat);
		}
		if (_pk.inHand && st.hi >= 0) {
			st.stack = _pk.hand.players[st.hi].stack;
			_pk.hand.players[st.hi].stack = 0;
		}
		AddToPurse(st.id, st.stack);
		auto  ref = st.actor.get();
		auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
		if (actor) {
			if (st.held) {
				Npc::SetRestrained(actor, false);
			}
			Npc::Leave(actor);
		}
		Remove(st.walk.marker);
		log::info("poker: {} leaves ({}), {} caps back in their purse", st.name, a_why ? a_why : "", st.stack);
		st = PkSeat{};
	}

	void Game::PkIdle(int a_seat, int a_kind)
	{
		if (a_kind < 0 || a_seat < 0 || a_seat >= kPkSeats || !Settings::Get().bettorIdles) {
			return;
		}
		auto& st = _pk.seats[a_seat];
		if (!st.used || st.you || !st.held || st.step != Seat::Step::kPlaying) {
			return;
		}
		auto  ref = st.actor.get();
		auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
		if (!actor) {
			return;
		}
		auto pick = [&](auto& a_list) { return a_list[std::uniform_int_distribution<std::size_t>(0, a_list.size() - 1)(_rng)]; };
		RE::TESFormID idle = 0;
		switch (a_kind) {
		case 0: idle = pick(kLookIdles); break;
		case 1: idle = kThinkIdle; break;
		case 2: idle = kChipsIdle; break;
		case 3: idle = pick(kWinIdles); break;
		default: idle = kNoIdle; break;
		}
		Npc::PlayIdle(actor, idle);
		// and a word or two: a cheer or a laugh for a win, a groan for a lost showdown (now and then)
		if (Settings::Get().pkVoices && (a_kind == 3 || (a_kind == 4 && _pk.phase == PkPhase::kShowdown)) &&
			std::uniform_real_distribution<float>(0.0f, 1.0f)(_rng) < 0.7f) {
			const bool laugh = a_kind == 3 && std::uniform_real_distribution<float>(0.0f, 1.0f)(_rng) < 0.35f;
			Npc::Say(actor, a_kind == 3 ? (laugh ? kLaugh : kCheer) : kGroan);
		}
		st.nextIdle = std::uniform_real_distribution<float>(10.0f, 22.0f)(_rng);  // (no idle on top of this one for a while)
	}

	void Game::PkTickSeats(const Frame& a_frame, float a_dt)
	{
		const auto& s = Settings::Get();
		_pk.seatTimer -= a_dt;
		if (_pk.seatTimer <= 0.0f) {
			_pk.seatTimer = s.bettorEvery * std::uniform_real_distribution<float>(0.6f, 1.2f)(_rng);
			PkFindPlayer(a_frame);
		}
		for (int i = 0; i < kPkSeats; ++i) {
			auto& st = _pk.seats[i];
			if (!st.used || st.you) {
				continue;
			}
			auto  ref = st.actor.get();
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (!Usable(actor) || actor->IsInCombat()) {
				PkRelease(i, "gone");
				continue;
			}
			float      heading = 0.0f;
			const auto spot = PkStand(a_frame, i, heading);
			switch (st.step) {
			case Seat::Step::kWalking:
				if (WalkTo(actor, st.walk, a_frame, spot, heading, a_dt, false, 15.0f)) {
					st.step = Seat::Step::kPlaying;
					Npc::Arrived(actor);
					if (auto marker = st.walk.marker.get(); marker && Distance2D(actor->data.location, spot) > kSnap) {
						Npc::MoveTo(actor, marker.get());
					}
				}
				break;
			case Seat::Step::kPlaying:
				if (!st.held) {
					st.held = Npc::SetRestrained(actor, true);
				}
				FaceTowards(actor, heading);
				st.atSpot = Distance2D(actor->data.location, spot) < kArrived + 10.0f ? st.atSpot + a_dt : 0.0f;
				// now and then, while it isn't their turn: a look over the table
				st.nextIdle -= a_dt;
				if (st.nextIdle <= 0.0f && !(_pk.inHand && st.hi >= 0 && _pk.hand.ToAct() == st.hi)) {
					PkIdle(i, 0);
				}
				break;
			default:
				break;
			}
			// between hands: out of caps, or had enough
			if (!_pk.inHand && st.step == Seat::Step::kPlaying && (st.stack < s.pkBigBlind || st.hands <= 0)) {
				PkSay(std::format("{} {}", Upper(st.name), st.stack < s.pkBigBlind ? "IS OUT OF CAPS" : "CASHES OUT"));
				PkRelease(i, "done");
			}
		}
	}

	// ---------------------------------------------------------------- the hand

	void Game::PkStartHand()
	{
		const auto& s = Settings::Get();
		std::vector<pk::Player> players;
		std::vector<int>        seatOf;
		for (int i = 0; i < kPkSeats; ++i) {
			auto& st = _pk.seats[i];
			st.hi = -1;
			st.last.clear();
			const bool ready = st.used && (st.you ? !st.away : (st.step == Seat::Step::kPlaying && st.atSpot > kSettleIn)) && st.stack >= s.pkBigBlind;
			if (!ready) {
				continue;
			}
			pk::Player p;
			p.id = st.you ? 0 : st.id;
			p.stack = st.stack;
			p.style = st.style;
			st.hi = static_cast<int>(players.size());
			players.push_back(p);
			seatOf.push_back(i);
		}
		if (players.size() < 2) {
			for (auto& st : _pk.seats) {
				st.hi = -1;
			}
			return;
		}
		// the button: the next seat in play round from the last one
		int next = 0;
		for (int k = 1; k <= kPkSeats; ++k) {
			const int seat = (_pk.button + k + kPkSeats) % kPkSeats;
			if (_pk.seats[seat].hi >= 0) {
				next = _pk.seats[seat].hi;
				_pk.button = seat;
				break;
			}
		}
		_pk.hand.Start(std::move(players), next, s.pkSmallBlind, s.pkBigBlind, _rng);
		_pk.inHand = true;
		_pk.phase = PkPhase::kDealing;
		_pk.boardShown = 0;
		_pk.bannerAge = 99.0f;
		_pk.winnerSeat = -1;
		PkClearTable();
		// two cards each, face down in front of them (yours too: you see them on the HUD), round from the button
		float delay = 0.2f;
		for (int round = 0; round < 2; ++round) {
			for (int k = 1; k <= kPkSeats; ++k) {
				const int seat = (_pk.button + k) % kPkSeats;
				const int hi = _pk.seats[seat].hi;
				if (hi < 0) {
					continue;
				}
				// (the right way up for whoever's there: yours turn up while your mouse is over them)
				const auto n = SeatNormal(seat);
				const float jitter = std::uniform_real_distribution<float>(-0.12f, 0.12f)(_rng);  // (never quite square)
				PkDeal(_pk.hand.players[hi].hole[round], HoleSpot(seat, round), std::atan2(-n.x, -n.y) + jitter, false, delay, seat);
				delay += 0.14f;
			}
		}
		_pk.timer = delay + 0.4f;
		_pk.think = 0.0f;
		PkSay(std::format("NEW HAND · BLINDS {}/{}", s.pkSmallBlind, s.pkBigBlind));
		Audio::PlayUI(Audio::Bank::kShuffle, 0.6f);
	}

	void Game::PkAct(pk::Action a_action)
	{
		const int hi = _pk.hand.ToAct();
		if (hi < 0) {
			return;
		}
		int seat = -1;
		for (int i = 0; i < kPkSeats; ++i) {
			if (_pk.seats[i].used && _pk.seats[i].hi == hi) {
				seat = i;
			}
		}
		const int  toCall = _pk.hand.ToCall(hi);
		const auto street = _pk.hand.street;
		const int  paid = _pk.hand.Apply(a_action);
		const auto& p = _pk.hand.players[hi];
		// what they did, as it came out (a check facing a bet is a fold, a raise too big is all in)
		std::string what;
		if (p.folded) {
			what = "FOLDS";
		} else if (p.allIn && paid > 0) {
			what = std::format("ALL IN ({})", p.committed);
			_pk.pushAge = 0.0f;
		} else if (paid == 0) {
			what = "CHECKS";
		} else if (paid <= toCall) {
			what = std::format("CALLS {}", paid);
		} else {
			what = std::format("{} {}", toCall == 0 && street != pk::Street::kPreflop ? "BETS" : "RAISES TO",
				street == _pk.hand.street ? p.bet : p.committed);
		}
		if (seat >= 0 && !_pk.seats[seat].you) {
			PkIdle(seat, p.folded ? 4 : paid > 0 ? 2 : -1);
		}
		if (seat >= 0 && p.folded) {
			PkMuck(seat);
		}
		if (seat >= 0) {
			_pk.seats[seat].last = what;
			PkSay(std::format("{} {}", _pk.seats[seat].you ? std::string("YOU") : Upper(_pk.seats[seat].name), what), p.folded ? 2 : 0);
		}
		if (paid > 0) {
			Audio::PlayUI(Audio::Bank::kChip, 0.6f);
		}
		_pk.think = 0.0f;
		_pk.raiseTo = 0;
	}

	void Game::PkTickHand(const Frame& a_frame, float a_dt)
	{
		(void)a_frame;
		const auto& s = Settings::Get();
		_pk.timer -= a_dt;
		switch (_pk.phase) {
		case PkPhase::kWaiting:
			{
				if (_pk.timer > 0.0f) {
					return;
				}
				_pk.timer = 1.0f;
				int ready = 0;
				for (const auto& st : _pk.seats) {
					ready += st.used && (st.you ? !st.away : (st.step == Seat::Step::kPlaying && st.atSpot > kSettleIn)) && st.stack >= s.pkBigBlind;
				}
				if (ready >= 2) {
					PkStartHand();
				}
				return;
			}
		case PkPhase::kDealing:
			if (_pk.timer <= 0.0f) {
				_pk.phase = PkPhase::kBetting;
				_pk.timer = 0.0f;
			}
			return;
		case PkPhase::kBetting:
			{
				// the board catching up with the hand (a street ended): its cards out one by one
				if (_pk.boardShown < _pk.hand.board.size()) {
					if (_pk.timer > 0.0f) {
						return;
					}
					const int k = static_cast<int>(_pk.boardShown);
					float     yaw = 3.14159265f * 0.5f;  // their tops to +X: read from the far side...
					if (_pkSeated && _pkYou >= 0) {
						const auto n = SeatNormal(_pkYou);
						yaw = std::atan2(-n.x, -n.y);  // ...or from yours
					}
					PkDeal(_pk.hand.board[k], BoardSpot(k), yaw, true, 0.0f);
					++_pk.boardShown;
					_pk.timer = _pk.boardShown == 3 || _pk.boardShown == 4 || _pk.boardShown == 5 ? 0.9f : 0.25f;
					if (_pk.boardShown == 3) {
						PkSay("THE FLOP");
					} else if (_pk.boardShown == 4) {
						PkSay("THE TURN");
					} else if (_pk.boardShown == 5) {
						PkSay("THE RIVER");
						_pk.pushAge = 0.0f;
					}
					return;
				}
				if (_pk.hand.Over()) {
					if (_pk.timer > 0.0f) {
						return;
					}
					PkEndHand();
					return;
				}
				if (_pk.timer > 0.0f) {
					return;
				}
				const int hi = _pk.hand.ToAct();
				int       seat = -1;
				for (int i = 0; i < kPkSeats; ++i) {
					if (_pk.seats[i].used && _pk.seats[i].hi == hi) {
						seat = i;
					}
				}
				if (seat < 0) {
					_pk.hand.Fold(hi);  // (nobody at that seat any more)
					return;
				}
				auto& st = _pk.seats[seat];
				if (st.you) {
					if (_pk.raiseTo < _pk.hand.MinRaiseTo(hi)) {
						_pk.raiseTo = _pk.hand.MinRaiseTo(hi);
					}
					return;  // your keys
				}
				st.last = "THINKING";
				if (_pk.think <= 0.0f) {
					// what they'll do is settled at once; how long they take over it gives it away a little: an easy
					// decision is quick, a close call slow (a tell)
					double eq = 0.0;
					_pk.pending = pk::Decide(_pk.hand, hi, _rng, &eq);
					const double sure = pk::Confidence(_pk.hand, hi, _pk.pending, eq);
					_pk.think = s.pkThink * static_cast<float>(0.5 + (1.0 - sure) * 2.2) * std::uniform_real_distribution<float>(0.8f, 1.25f)(_rng);
					if (_pk.think > 1.8f) {
						PkIdle(seat, 1);  // a long think: they look it over
					}
				}
				_pk.think -= a_dt;
				if (_pk.think > 0.0f) {
					return;
				}
				PkAct(_pk.pending);
				_pk.timer = 0.35f;
				return;
			}
		case PkPhase::kShowdown:
			if (_pk.timer <= 0.0f) {
				// the next hand: stacks back to the seats, the table cleared
				for (auto& st : _pk.seats) {
					if (st.used && st.hi >= 0) {
						st.stack = _pk.hand.players[st.hi].stack;
						if (!st.you) {
							--st.hands;
						}
					}
					st.hi = -1;
					st.last.clear();
				}
				_pk.inHand = false;
				PkMuck(-2);                 // the cards back to the dealer...
				PkClearTable(false);        // ...the chips off
				_pk.phase = PkPhase::kWaiting;
				_pk.timer = 2.5f;
			}
			return;
		}
	}

	void Game::PkEndHand()
	{
		auto& h = _pk.hand;
		_pk.phase = PkPhase::kShowdown;
		_pk.timer = h.showdown ? 6.0f : 3.5f;
		// the cards of everyone still in, turned up
		if (h.showdown) {
			// the hands still in turned up one after another, from the last to bet (or the button's left)
			std::array<float, kPkSeats> when{};
			float                       next = 0.3f;
			const int                   first = h.lastAggressor >= 0 ? h.lastAggressor : h.NextSeat(h.button, false);
			for (int k = 0; k < kPkSeats; ++k) {
				for (int s = 0; s < kPkSeats; ++s) {
					const auto& st = _pk.seats[s];
					if (st.used && st.hi >= 0 && !h.players[st.hi].folded &&
						(st.hi - first + static_cast<int>(h.players.size())) % static_cast<int>(h.players.size()) == k) {
						when[s] = next;
						next += 0.7f;
					}
				}
			}
			for (auto& c : _pk.cards) {
				int hi = -1;
				if (c.seat >= 0) {
					hi = _pk.seats[c.seat].hi;
				}
				if (!c.mucked && (c.seat < 0 || (hi >= 0 && !h.players[hi].folded))) {
					c.faceUp = true;
					c.flipWait = c.seat >= 0 && !_pk.seats[c.seat].you ? when[c.seat] : 0.0f;
				}
			}
			_pk.timer = std::max(_pk.timer, next + 3.5f);
		}
		// who won what
		std::vector<int> won(h.players.size(), 0);
		for (const auto& pot : h.pots) {
			const int share = pot.amount / static_cast<int>(pot.winners.size());
			for (int w : pot.winners) {
				won[w] += share;
			}
		}
		// (this hand's banner: the last one's was never cleared, so an old "YOU WIN" kept coming back)
		_pk.bannerTitle.clear();
		_pk.bannerSub.clear();
		_pk.bannerTone = 0;
		bool youIn = false;
		for (const auto& st : _pk.seats) {
			youIn |= st.used && st.you && st.hi >= 0;
		}
		std::string youResult;
		int         winnerSeat = -1;
		for (int i = 0; i < kPkSeats; ++i) {
			const auto& st = _pk.seats[i];
			if (!st.used || st.hi < 0 || won[st.hi] <= 0) {
				continue;
			}
			const std::string who = st.you ? std::string("YOU") : Upper(st.name);
			const std::string with = h.showdown ? " WITH " + pk::HandName(h.scores[st.hi]) : std::string();
			PkSay(std::format("{} {} {}{}", who, st.you ? "WIN" : "WINS", won[st.hi], with), 1);
			if (_pk.bannerTitle.empty() || st.you) {
				_pk.bannerTitle = std::format("{} {} {}", who, st.you ? "WIN" : "WINS", won[st.hi]);
				_pk.bannerSub = h.showdown ? pk::HandName(h.scores[st.hi]) : std::string("EVERYONE ELSE FOLDED");
			}
			if (st.you) {
				youResult = "won";
				_pk.bannerTone = 1;
			}
			PkIdle(i, 3);
			// the winning five raised off the felt a touch (at a showdown)
			if (h.showdown) {
				const auto five = BestFive(h.players[st.hi].hole, h.board);
				for (auto& c : _pk.cards) {
					if ((c.seat < 0 || c.seat == i) && std::ranges::find(five, c.card) != five.end()) {
						c.liftTo = 0.8f;
					}
				}
			}
			if (winnerSeat < 0) {
				winnerSeat = i;
				_pk.winnerSeat = i;
			}
		}
		if (h.showdown) {
			for (int i = 0; i < kPkSeats; ++i) {
				const auto& st = _pk.seats[i];
				if (st.used && st.hi >= 0 && won[st.hi] <= 0 && !h.players[st.hi].folded) {
					PkIdle(i, 4);  // shown down and lost
				}
			}
		}
		if (youIn && youResult.empty()) {
			_pk.bannerTone = 2;  // you were in the hand and didn't take any of it: the banner in red
		}
		_pk.bannerAge = 0.0f;
		// the pot (and the last bets) slide over to the winner's stack, once their cards are up
		const float reveal = h.showdown ? 1.2f : 0.4f;
		const auto  to = winnerSeat >= 0 ? StackSpot(winnerSeat) : kPot;
		for (auto& v : _pk.betChips) {
			PkSlideChips(v, to, reveal);
		}
		_pk.betShown.fill(0);
		PkSlideChips(_pk.potChips, to, reveal);
		_pk.potShown = 0;
		_pk.potHold = reveal + 0.8f;
		(void)youResult;
	}

	// ---------------------------------------------------------------- cards and chips

	void Game::PkMuck(int a_seat)
	{
		Audio::PlayUI(Audio::Bank::kSlide, 0.4f);
		float delay = 0.0f;
		for (auto& c : _pk.cards) {
			if ((a_seat != -2 && c.seat != a_seat) || c.mucked || !c.ref) {
				continue;
			}
			const float e = 1.0f - (1.0f - c.t) * (1.0f - c.t);
			c.from = c.from + (c.to - c.from) * e;  // from wherever it is now
			c.to = kMuck;
			c.t = 0.0f;
			c.delay = a_seat == -2 ? delay : 0.0f;  // (the whole table: one after another)
			delay += 0.05f;
			c.faceUp = false;
			c.mucked = true;
			c.spin = 0.0f;
			c.liftTo = 0.0f;
			c.flipWait = 0.0f;
		}
	}

	void Game::PkDeal(int a_card, const RE::NiPoint3& a_to, float a_yaw, bool a_faceUp, float a_delay, int a_seat)
	{
		PkCard c;
		c.seat = a_seat;
		c.spin = std::uniform_real_distribution<float>(-1.6f, 1.6f)(_rng);  // (a little turn in the air)
		c.card = a_card;
		c.from = kDeck;
		c.to = a_to;
		c.yaw = a_yaw;
		c.delay = a_delay;
		c.faceUp = a_faceUp;
		c.flip = 0.0f;
		_pk.cards.push_back(c);
	}

	void Game::PkTickCards(const Frame& a_frame, float a_dt)
	{
		for (auto& c : _pk.cards) {
			if (c.delay > 0.0f) {
				c.delay -= a_dt;
				continue;
			}
			auto ref = c.ref.get();
			if (!ref) {
				c.ref = Spawn(Forms::Card(c.card), ToWorld(a_frame, c.from.x, c.from.y, kFeltZ + 1.0f), a_frame.yaw);
				ref = c.ref.get();
				if (!ref) {
					continue;
				}
			}
			if (c.t < 1.0f) {
				c.t = std::min(1.0f, c.t + a_dt / (c.mucked ? 0.4f : 0.35f));
				if (c.t >= 1.0f && !c.mucked) {
					Audio::PlayUI(Audio::Bank::kCard, 0.45f);
				}
			}
			if (c.flipWait > 0.0f) {
				c.flipWait -= a_dt;
			}
			if (c.lift != c.liftTo) {
				c.lift = c.lift < c.liftTo ? std::min(c.liftTo, c.lift + a_dt * 2.0f) : std::max(c.liftTo, c.lift - a_dt * 2.0f);
			}
			if (c.mucked && c.t >= 1.0f) {
				Remove(c.ref);  // back in the dealer's pile
				continue;
			}
			const float want = c.faceUp ? 1.0f : 0.0f;
			if (c.flip != want && c.flipWait <= 0.0f) {
				if ((c.flip == 0.0f || c.flip == 1.0f) && c.t >= 1.0f) {
					Audio::PlayUI(Audio::Bank::kCard, 0.25f);
				}
				c.flip = c.flip < want ? std::min(want, c.flip + a_dt / 0.3f) : std::max(want, c.flip - a_dt / 0.3f);
			}
			if (!ref->Get3D()) {
				continue;
			}
			const float  e = 1.0f - (1.0f - c.t) * (1.0f - c.t);  // easing out
			RE::NiPoint3 p = c.from + (c.to - c.from) * e;
			// face down, the card's lying on its face: up by its thickness; turning over, lifted a little; on its way,
			// a low arc through the air, turning as it goes; one of the winning five, raised a touch
			const float flipAngle = (1.0f - c.flip) * 3.14159265f;
			const float arc = c.t < 1.0f ? std::sin(c.t * 3.14159265f) * (c.mucked ? 0.6f : 3.0f) : 0.0f;
			const float lift = std::sin(c.flip * 3.14159265f) * 2.5f + (c.flip < 0.5f ? kCardT : 0.0f) + 0.02f + arc + c.lift;
			const float yaw = c.yaw + c.spin * (1.0f - e);
			const auto  world = ToWorld(a_frame, p.x, p.y, kFeltZ + lift);
			PlaceRoot(ref.get(), world, Lying(yaw, flipAngle) * a_frame.rot);
		}
		std::erase_if(_pk.cards, [](const PkCard& c) { return c.mucked && c.t >= 1.0f; });
	}

	void Game::PkSlideChips(std::vector<RE::ObjectRefHandle>& a_chips, const RE::NiPoint3& a_to, float a_delay)
	{
		// each chip off where it is to a_to (stacked up as they go), then gone
		Frame frame;
		if (!PkFrame(frame)) {
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
			// where it is, back in table space
			const auto d = ref->data.location - frame.origin;
			const auto local = frame.rot * d;
			PkChipMove m;
			m.ref = h;
			m.from = { local.x, local.y, local.z - kFeltZ };
			m.to = { a_to.x, a_to.y, 0.02f + static_cast<float>(k % kMaxStack) * kChipH };
			m.delay = a_delay + 0.015f * static_cast<float>(k);
			m.yaw = ref->data.angle.z;
			_pk.moves.push_back(m);
			++k;
		}
		a_chips.clear();
		if (k > 0) {
			Audio::PlayUI(Audio::Bank::kChip, 0.6f);
		}
	}

	void Game::PkTickMoves(const Frame& a_frame, float a_dt)
	{
		for (auto& m : _pk.moves) {
			if (m.delay > 0.0f) {
				m.delay -= a_dt;
				continue;
			}
			m.t = std::min(1.0f, m.t + a_dt / m.dur);
			const float e = m.t * m.t * (3.0f - 2.0f * m.t);
			const auto  p = m.from + (m.to - m.from) * e;
			if (auto ref = m.ref.get()) {
				PlaceNode(ref.get(), ToWorld(a_frame, p.x, p.y, kFeltZ + p.z + std::sin(m.t * 3.14159265f) * 0.6f), m.yaw, false);
			}
			if (m.t >= 1.0f && m.remove) {
				Remove(m.ref);
			}
		}
		std::erase_if(_pk.moves, [](const PkChipMove& m) { return m.t >= 1.0f; });
	}

	void Game::PkMakeChips(const Frame& a_frame)
	{
		std::uniform_real_distribution<float> spin(0.0f, 6.283f);
		// a_amount in chips at (a_x, a_y), up to three columns; a_fromX/Y: where they come from (they slide over), or NaN
		auto stack = [&](int a_amount, float a_x, float a_y, std::vector<RE::ObjectRefHandle>& a_out, const RE::NiPoint3* a_from = nullptr) {
			auto chips = rl::ChipsFor(a_amount);
			for (std::size_t i = 0; i < chips.size() && i < kMaxStack * 3; ++i) {
				const int   idx = static_cast<int>(std::ranges::find(rl::kChipValues, chips[i]) - std::begin(rl::kChipValues));
				const int   col = static_cast<int>(i) / kMaxStack, row = static_cast<int>(i) % kMaxStack;
				const float x = a_x, y = a_y + (col - 1) * kChipR * 2.2f * (chips.size() > kMaxStack ? 1.0f : 0.0f);
				const float z = 0.02f + row * kChipH;
				const float yaw = a_frame.yaw + spin(_rng);
				if (a_from) {
					// spawned where it's coming from, slid over (kept: not removed at the end)
					auto h = Spawn(Forms::Chip(idx), ToWorld(a_frame, a_from->x, a_from->y, kFeltZ + z), yaw);
					a_out.push_back(h);
					PkChipMove m;
					m.ref = h;
					m.from = { a_from->x, a_from->y, z };
					m.to = { x, y, z };
					m.delay = 0.02f * static_cast<float>(i);
					m.dur = 0.3f;
					m.yaw = yaw;
					m.remove = false;
					_pk.moves.push_back(m);
				} else {
					a_out.push_back(Spawn(Forms::Chip(idx), ToWorld(a_frame, x, y, kFeltZ + z), yaw));
				}
			}
		};
		// each player's caps in front of them
		for (int i = 0; i < kPkSeats; ++i) {
			const auto& st = _pk.seats[i];
			int         have = 0;
			if (st.used && !st.away && (st.you || st.step == Seat::Step::kPlaying)) {
				have = _pk.inHand && st.hi >= 0 ? _pk.hand.players[st.hi].stack : st.stack;
			}
			if (_pk.potHold > 0.0f && _pk.phase == PkPhase::kShowdown) {
				continue;  // (the pot's on its way over: the winner's stack grows once it's there)
			}
			if (have == _pk.stackShown[i]) {
				continue;
			}
			for (auto& h : _pk.stackChips[i]) {
				Remove(h);
			}
			_pk.stackChips[i].clear();
			_pk.stackShown[i] = have;
			if (have > 0) {
				const auto sp = StackSpot(i);
				stack(have, sp.x, sp.y, _pk.stackChips[i]);
			}
		}
		if (!_pk.inHand || _pk.phase == PkPhase::kShowdown) {
			return;
		}
		// the pot: everything put in on earlier streets (built once the bets have slid in)
		int pot = 0;
		for (const auto& p : _pk.hand.players) {
			pot += p.committed - p.bet;
		}
		const bool potGrew = pot > _pk.potShown && _pk.potShown >= 0;
		// each player's bet this street: a new bet slides out from their stack; at the end of a street, into the pot
		for (int i = 0; i < kPkSeats; ++i) {
			const auto& st = _pk.seats[i];
			const int   bet = st.used && st.hi >= 0 ? _pk.hand.players[st.hi].bet : 0;
			if (bet == _pk.betShown[i]) {
				continue;
			}
			if (bet == 0 && potGrew) {
				PkSlideChips(_pk.betChips[i], kPot, 0.1f);
				_pk.potHold = 0.75f;
			} else {
				for (auto& h : _pk.betChips[i]) {
					Remove(h);
				}
				_pk.betChips[i].clear();
			}
			const bool more = bet > _pk.betShown[i];
			_pk.betShown[i] = bet;
			if (bet > 0) {
				const auto b = BetSpot(i);
				const auto from = StackSpot(i);
				stack(bet, b.x, b.y, _pk.betChips[i], more ? &from : nullptr);
			}
		}
		if (_pk.potHold > 0.0f) {
			return;
		}
		if (pot != _pk.potShown) {
			for (auto& h : _pk.potChips) {
				Remove(h);
			}
			_pk.potChips.clear();
			_pk.potShown = pot;
			if (pot > 0) {
				stack(pot, kPot.x, kPot.y, _pk.potChips);
			}
		}
	}

	// ---------------------------------------------------------------- you

	void Game::PkSitDown(RE::TESObjectREFR* a_table)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* camera = RE::PlayerCamera::GetSingleton();
		if (!player || !camera || _seated || _pkSeated) {
			return;
		}
		if (_pk.table.get().get() != a_table) {
			PkEnd("you sat down at another poker table");
			PkStart(a_table);
		} else {
			Settings::Get().Load();
		}
		Frame frame;
		if (!PkFrame(frame)) {
			return;
		}
		const auto& s = Settings::Get();
		// back from being away: your seat and your stack (too short a stack to play: cashed out, a fresh buy-in)
		if (_pkYou >= 0 && _pk.seats[_pkYou].away && _pk.seats[_pkYou].stack < s.pkBigBlind) {
			PkCashOut();
		}
		const bool back = _pkYou >= 0 && _pk.seats[_pkYou].away;
		const int  caps = PlayerCaps();
		if (!back && caps < s.pkBigBlind * 2) {
			Notify(std::format("Poker: you need at least {} caps to sit in", s.pkBigBlind * 2));
			return;
		}
		// the free seat nearest you
		int   best = back ? _pkYou : -1;
		float bestD = 1e9f;
		for (int i = 0; i < kPkSeats && !back; ++i) {
			if (_pk.seats[i].used) {
				continue;
			}
			float      heading = 0.0f;
			const auto at = PkStand(frame, i, heading);
			const float d = Distance2D(at, player->data.location);
			if (d < bestD) {
				bestD = d;
				best = i;
			}
		}
		if (best < 0) {
			Notify("Poker: the table's full");
			return;
		}
		int buyIn = 0;
		if (back) {
			_pk.seats[best].away = false;
			buyIn = _pk.seats[best].stack;
		} else {
			buyIn = std::min(caps, s.pkBuyIn);
			if (!TakeFromPlayer(buyIn)) {
				return;
			}
			auto& st = _pk.seats[best];
			st = PkSeat{};
			st.used = true;
			st.you = true;
			st.name = "You";
			st.stack = buyIn;
			st.step = Seat::Step::kPlaying;
		}
		_pkYou = best;
		// the camera: as at the roulette table, the free camera held where the tick puts it
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
			// the view glides over from where your eyes were, rather than jumping
			if (camera->cameraRoot) {
				_camPos = camera->cameraRoot->world.translate;
				_haveCamPos = true;
				_lastCam = std::chrono::steady_clock::now();
			} else {
				_haveCamPos = false;
			}
		}
		{
			std::scoped_lock lock(_camMutex);
			_pkLookYaw = _pkLookPitch = 0.0f;
			_mouseDX = _mouseDY = 0.0;
		}
		_pkTopView = Settings::Get().pkTopDown;
		_pkShowCards = false;
		_pkPtrX = _pkPtrY = -1.0;
		_pkSeated = true;
		BlockControls(true);
		QuietNpcs(true);
		_pk.raiseTo = 0;
		log::info("poker: you sit in at seat {} with {} caps", best, buyIn);
		const auto key = [](int a_vk) { return Upper(Settings::Key(a_vk)); };
		PkSay(back ? std::format("YOU'RE BACK WITH {}", buyIn) : std::format("YOU SIT IN WITH {}", buyIn));
		(void)key;
	}

	void Game::PkCashOut()
	{
		// your seat given up (you're up from the table already, or never sat): your stack back in your pocket
		if (_pkYou < 0 || _pkSeated) {
			return;
		}
		auto& st = _pk.seats[_pkYou];
		if (_pk.inHand && st.hi >= 0) {
			if (!_pk.hand.Over()) {
				_pk.hand.Fold(st.hi);
				PkMuck(_pkYou);
			}
			st.stack = _pk.hand.players[st.hi].stack;
			_pk.hand.players[st.hi].stack = 0;
		}
		GivePlayer(st.stack);
		log::info("poker: you cash out with {} caps", st.stack);
		Notify(std::format("Poker: you cash out with {} caps", st.stack));
		st = PkSeat{};
		_pkYou = -1;
	}

	void Game::PkStandUp(bool a_cashOut)
	{
		if (!_pkSeated) {
			return;
		}
		auto& st = _pk.seats[_pkYou];
		// out of the hand if you're in one (what you put in stays in the pot); your seat and your stack kept for you
		// while you're away from the table, unless you're cashing out
		if (_pk.inHand && st.hi >= 0 && !_pk.hand.Over()) {
			_pk.hand.Fold(st.hi);
			PkMuck(_pkYou);
		}
		st.away = true;
		st.last.clear();
		_pkSeated = false;
		PkHoldMenuKeys(false);
		_pk.peeking = false;
		if (a_cashOut) {
			PkCashOut();
		} else {
			log::info("poker: you step away from the table (seat {} kept)", _pkYou);
			const int left = _pk.inHand && st.hi >= 0 ? _pk.hand.players[st.hi].stack : st.stack;
			Notify(std::format("Poker: your seat and {} caps are kept. Activate the table to sit back down ({} there cashes out)", left,
				Upper(Settings::Key(Settings::Get().keyClear))));
		}
		_camPitch = -1.0f;
		HoldPlayerView();
		_haveEntryView = false;
		BlockControls(false);
		QuietNpcs(false);
		if (auto* camera = RE::PlayerCamera::GetSingleton(); camera && InFreeCamera()) {
			camera->ToggleFreeCameraMode(false);
		}
	}

	void Game::PkKey(int a_key)
	{
		if (!_pkSeated || _pkYou < 0) {
			return;
		}
		if (a_key == 9) {
			_pkTopView = !_pkTopView;  // the view from your seat <-> from above
			return;
		}
		if (a_key == 10) {
			_pkShowCards = !_pkShowCards;
			return;
		}
		if (a_key == 6 || a_key == 8) {
			PkStandUp(a_key == 8);  // (6: step away, the seat kept; 8: cash out)
			return;
		}
		if (a_key == 7) {
			// a kit table: back into a kit (between hands)
			auto table = _pk.table.get();
			if (table && IsKitTable(table.get()) && !_pk.inHand) {
				PkStandUp(true);
				PackUpPokerTable(table.get());
			} else if (_pk.inHand) {
				PkSay("WAIT FOR THE HAND TO END", 2);
			}
			return;
		}
		const int hi = _pk.seats[_pkYou].hi;
		if (!_pk.inHand || _pk.phase != PkPhase::kBetting || hi < 0 || _pk.hand.ToAct() != hi || _pk.boardShown < _pk.hand.board.size()) {
			return;
		}
		const int step = std::max(1, Settings::Get().pkBigBlind);
		switch (a_key) {
		case 0: PkAct({ pk::Action::Kind::kFold }); break;
		case 1: PkAct({ _pk.hand.CanCheck(hi) ? pk::Action::Kind::kCheck : pk::Action::Kind::kCall }); break;
		case 2: PkAct({ pk::Action::Kind::kRaise, std::max(_pk.raiseTo, _pk.hand.MinRaiseTo(hi)) }); break;
		case 3: PkAct({ pk::Action::Kind::kAllIn }); break;
		case 4: _pk.raiseTo = std::min(_pk.hand.MaxRaiseTo(hi), std::max(_pk.raiseTo, _pk.hand.MinRaiseTo(hi)) + step); break;
		case 5: _pk.raiseTo = std::max(_pk.hand.MinRaiseTo(hi), _pk.raiseTo - step); break;
		default: break;
		}
		if (a_key <= 3) {
			_pk.timer = 0.35f;
		}
	}

	// ---------------------------------------------------------------- the HUD

	void Game::PkHud(HudState& a_h) const
	{
		const auto& s = Settings::Get();
		a_h.poker = true;
		a_h.seated = true;
		a_h.caps = PlayerCaps();
		const auto& h = _pk.hand;
		static constexpr std::array<const char*, 5> kStreet{ "BEFORE THE FLOP", "THE FLOP", "THE TURN", "THE RIVER", "SHOWDOWN" };
		a_h.pkInfo = std::format("BLINDS {}/{} · {}", s.pkSmallBlind, s.pkBigBlind,
			_pk.inHand ? kStreet[static_cast<int>(h.street)] : "BETWEEN HANDS");
		if (_pk.inHand) {
			a_h.pkPot = h.PotTotal();
			for (std::size_t k = 0; k < std::min(_pk.boardShown, h.board.size()); ++k) {
				a_h.pkBoard.push_back(h.board[k]);
			}
		}
		for (int i = 0; i < kPkSeats; ++i) {
			const auto& st = _pk.seats[i];
			if (!st.used) {
				continue;
			}
			HudState::PokerRow r;
			r.name = st.you ? std::string("YOU") : Upper(st.name);
			r.you = st.you;
			r.dealer = i == _pk.button && _pk.inHand;
			r.stack = st.stack;
			if (_pk.inHand && st.hi >= 0) {
				const auto& p = h.players[st.hi];
				r.stack = p.stack;
				r.bet = p.bet;
				r.folded = p.folded;
				r.toAct = h.ToAct() == st.hi && _pk.phase == PkPhase::kBetting;
				r.status = p.folded ? std::string("FOLDED") : p.allIn ? std::string("ALL IN") : st.last;
				if (_pk.phase == PkPhase::kShowdown && h.showdown && !p.folded) {
					r.shown = { p.hole[0], p.hole[1] };
				}
				if (st.you) {
					a_h.pkHole = { p.hole[0], p.hole[1] };
					std::vector<pk::Card> mine{ p.hole[0], p.hole[1] };
					for (std::size_t k = 0; k < std::min(_pk.boardShown, h.board.size()); ++k) {
						mine.push_back(h.board[k]);
					}
					if (!_pk.peeking && !(_pk.phase == PkPhase::kShowdown && h.showdown)) {
						// (what you hold only shows while you're looking at your cards)
					} else if (mine.size() >= 5) {
						a_h.pkHand = pk::HandName(pk::Evaluate(mine));
					} else if (pk::Rank(p.hole[0]) == pk::Rank(p.hole[1])) {
						a_h.pkHand = "A POCKET PAIR";
					}
				}
			} else if (!st.you && st.step != Seat::Step::kPlaying) {
				r.status = "ON THE WAY";
			}
			a_h.pkRows.push_back(std::move(r));
		}
		const auto key = [](int a_vk) { return Upper(Settings::Key(a_vk)); };
		const int  hi = _pkYou >= 0 ? _pk.seats[_pkYou].hi : -1;
		a_h.pkYourTurn = _pk.inHand && hi >= 0 && h.ToAct() == hi && _pk.phase == PkPhase::kBetting && _pk.boardShown >= h.board.size();
		if (a_h.pkYourTurn) {
			const int toCall = h.ToCall(hi);
			const int raiseTo = std::max(_pk.raiseTo, h.MinRaiseTo(hi));
			a_h.keys = std::format("{} FOLD · {} {} · {} RAISE TO {} (UP/DOWN) · {} ALL IN ({})", key(s.keyChip[0]), key(s.keyChip[1]),
				toCall == 0 ? std::string("CHECK") : std::format("CALL {}", toCall), key(s.keyChip[2]), raiseTo, key(s.keyChip[3]),
				h.players[hi].stack + h.players[hi].bet);
		} else {
			auto table = _pk.table.get();
			a_h.keys = std::format("ON YOUR TURN: {} FOLD · {} CHECK/CALL · {} RAISE · {} ALL IN    {} VIEW · {} STEP AWAY · {} CASH OUT{}",
				key(s.keyChip[0]), key(s.keyChip[1]), key(s.keyChip[2]), key(s.keyChip[3]), key(s.keyView), key(s.keyLeave), key(s.keyClear),
				table && IsKitTable(table.get()) ? std::format(" · {} PACK UP", key(s.keyPackUp)) : std::string());
		}
		// what's going on, in a line under the board
		if (!_pk.inHand) {
			int ready = 0, coming = 0;
			for (const auto& st : _pk.seats) {
				const bool r = st.used && (st.you || (st.step == Seat::Step::kPlaying && st.atSpot > kSettleIn));
				ready += r;
				coming += st.used && !r;
			}
			a_h.pkHelp = ready >= 2  ? std::string("NEXT HAND COMING UP")
			             : coming > 0 ? std::string("WAITING FOR PLAYERS TO GET TO THE TABLE")
			                          : std::string("WAITING FOR SETTLERS TO JOIN · THE TABLE DEALS ONCE TWO ARE SEATED");
		} else if (!a_h.pkYourTurn) {
			a_h.pkHelp = hi >= 0 && !h.players[hi].folded ? "WAIT FOR YOUR TURN · LOOK DOWN AT YOUR CARDS (OR HOLD RIGHT MOUSE) TO SEE THEM"
			                                               : "YOU'RE OUT OF THIS HAND · THE NEXT ONE DEALS YOU IN";
		} else if (!_pk.peeking) {
			a_h.pkHelp = "YOUR TURN · LOOK DOWN AT YOUR CARDS (OR HOLD RIGHT MOUSE) TO SEE THEM";
		}
		a_h.log = _pk.log;
		{
			std::scoped_lock lock(_camMutex);
			a_h.pkPtrX = static_cast<float>(_pkPtrX);
			a_h.pkPtrY = static_cast<float>(_pkPtrY);
		}
		a_h.pkPeek = _pk.peeking || _pkShowCards;
		a_h.pkTop = _pkTopView;
		if (auto table = _pk.table.get()) {
			a_h.pkKit = IsKitTable(table.get());
		}
		if (a_h.pkYourTurn) {
			a_h.pkToCall = h.ToCall(hi);
			a_h.pkRaiseTo = std::max(_pk.raiseTo, h.MinRaiseTo(hi));
			a_h.pkAllIn = h.players[hi].stack + h.players[hi].bet;
			a_h.pkMinRaise = h.MinRaiseTo(hi);
			a_h.pkMaxRaise = h.MaxRaiseTo(hi);
		}
		if (_pk.bannerAge < 5.0f && !_pk.bannerTitle.empty()) {
			a_h.banner = _pk.bannerTone == 1 ? 2 : _pk.bannerTone == 2 ? 3 : 4;  // you won / you lost / not yours
			a_h.bannerTitle = _pk.bannerTitle;
			a_h.bannerSub = _pk.bannerSub;
			a_h.bannerAge = _pk.bannerAge;
		}
	}

	// ---------------------------------------------------------------- saves

	void Game::PkBeforeSave()
	{
		// as at the roulette table: nothing of ours in the save; your stack in your pocket while it's written
		if (!_pk.table) {
			return;
		}
		if (_pkYou >= 0) {  // (sitting there, or away with your seat kept)
			const auto& st = _pk.seats[_pkYou];
			_pk.lifted = _pk.inHand && st.hi >= 0 ? _pk.hand.players[st.hi].stack : st.stack;
			GivePlayer(_pk.lifted);
		}
		for (auto& c : _pk.cards) {
			Remove(c.ref);
		}
		for (auto& v : _pk.betChips) {
			for (auto& h : v) {
				Remove(h);
			}
			v.clear();
		}
		_pk.betShown.fill(-1);
		for (auto& h : _pk.potChips) {
			Remove(h);
		}
		_pk.potChips.clear();
		_pk.potShown = -1;
		_pk.peeking = false;
		for (auto& v : _pk.stackChips) {
			for (auto& h : v) {
				Remove(h);
			}
			v.clear();
		}
		_pk.stackShown.fill(-1);
		for (auto& m : _pk.moves) {
			Remove(m.ref);
		}
		_pk.moves.clear();
		for (auto& st : _pk.seats) {
			if (st.used && !st.you && st.held) {
				if (auto ref = st.actor.get(); ref && ref->As<RE::Actor>()) {
					Npc::SetRestrained(ref->As<RE::Actor>(), false);
				}
				st.held = false;
			}
		}
	}

	void Game::PkAfterSave()
	{
		if (_pk.lifted > 0) {
			if (!TakeFromPlayer(_pk.lifted)) {
				log::warn("poker: after the save, your {} caps couldn't be taken back to the table", _pk.lifted);
			}
			_pk.lifted = 0;
		}
		// (the cards and chips come back on the next tick, the settlers are held again)
	}

	void Game::SetPkButtons(std::vector<PkButton> a_buttons, float a_screenW, float a_screenH)
	{
		std::scoped_lock lock(_btnMutex);
		_pkButtons = std::move(a_buttons);
		_scrW = a_screenW;
		_scrH = a_screenH;
	}

	void Game::PkSetRaise(float a_frac)
	{
		if (!_pkSeated || _pkYou < 0 || !_pk.inHand || _pk.phase != PkPhase::kBetting) {
			return;
		}
		const int hi = _pk.seats[_pkYou].hi;
		if (hi < 0 || _pk.hand.ToAct() != hi) {
			return;
		}
		const int lo = _pk.hand.MinRaiseTo(hi), top = _pk.hand.MaxRaiseTo(hi);
		const int step = std::max(1, Settings::Get().pkBigBlind);
		const float f = std::clamp(a_frac, 0.0f, 1.0f);
		int to = lo + static_cast<int>(std::round(f * static_cast<float>(top - lo) / static_cast<float>(step))) * step;
		if (f >= 0.999f) {
			to = top;
		}
		_pk.raiseTo = std::clamp(to, lo, std::max(lo, top));
	}

	bool Game::SwallowButton(const RE::ButtonEvent* a_event)
	{
		if (!_pkSeated || !a_event) {
			return false;
		}
		const auto& id = a_event->QUserEvent();
		const char* name = id.c_str() ? id.c_str() : "";
		const auto  code = a_event->QIDCode();
		const auto& s = Settings::Get();
		const bool  key = a_event->device.get() == RE::INPUT_DEVICE::kKeyboard && code != 0 &&
		                 (static_cast<int>(code) == s.keyTurnLeft || static_cast<int>(code) == s.keyTurnRight);
		if (key || std::string_view(name) == "Workshop") {
			static std::set<std::string> logged;
			if (logged.insert(name).second) {
				log::info("poker: the {} key ({}) kept from the game's menus while you're at the table", name, code);
			}
			return true;
		}
		return false;
	}
}
