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
		// chips sit a hair off the felt and off each other: face on face (exactly the felt's height, exactly a chip's
		// thickness apart), the two surfaces fight over which is drawn and the chips flicker
		constexpr float kLift = 0.08f;
		constexpr float kStep = kChipH * 1.04f;
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

		// sitting at the table (the chairs option): a dining chair of the game's, and the seated reactions
		constexpr RE::TESFormID                kChair = 0x000E37D3;  // NpcChairIndustrialMetalDiningSit01
		constexpr std::array<RE::TESFormID, 3> kSeatedWin{ 0x0010FF89, 0x0010FF8C, 0x00118014 };  // cheering sitting (A), laughing
		constexpr float                        kChairOut = 14.0f;   // a chair's middle this far out from the rail

		// voice: the Combat Zone crowd's lines (custom topics by keyword, every common voice has them)
		constexpr RE::TESFormID kCheer = 0x00110396;   // Crowd_ApprovalSounds_TopicSubType
		constexpr RE::TESFormID kLaugh = 0x00110395;   // Crowd_Laughter_TopicSubType
		constexpr RE::TESFormID kGroan = 0x00110398;   // Crowd_DisapprovalSounds_TopicSubType

		// the seats, clockwise from the dealer's place (+X): the table's cup holders, table space
		constexpr std::array<std::pair<float, float>, 7> kHolders{ {
			{ 27.9f, -47.1f }, { 1.1f, -71.2f }, { -25.8f, -47.1f }, { -25.8f, -0.2f }, { -25.8f, 46.7f }, { 1.1f, 70.8f }, { 27.9f, 46.7f },
		} };

		// from the dealer's side (+X, the chip tray) in: the deck, the board, then the pot
		const RE::NiPoint3 kDeck{ 15.5f, -17.0f, kFeltZ };  // where cards come from (by the tray, to one side of the board)
		const RE::NiPoint3 kMuck{ 15.5f, 17.0f, kFeltZ };   // where folded hands go (the other side)
		const RE::NiPoint3 kPot{ 8.0f, -21.5f, kFeltZ };    // the pot: beside the board, by the dealer (not in front of the
		                                                   // board, where it hid the cards from the players)

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

		// (the chairs option) where a seat's chair goes, and which way it faces (table space; into the table)
		RE::NiPoint3 ChairSpot(int a_seat, RE::NiPoint3& a_facing)
		{
			RE::NiPoint3 e, n;
			Stadium(kHolders[a_seat].first, kHolders[a_seat].second, kRailR, kRailC, e, n);
			a_facing = { -n.x, -n.y, 0.0f };
			return e + n * kChairOut;
		}


		// a player's caps as they'd keep them: a stack of each colour (ones, fives, twenty-fives, hundreds), some of each,
		// rather than the fewest chips
		constexpr int kColMax = 30;  // the most chips shown in a stack
		std::array<int, 4> StackCounts(int a_amount)
		{
			std::array<int, 4> n{};
			int                left = std::max(0, a_amount);
			constexpr std::array<int, 3> kWant{ 10, 10, 8 };  // how many of the small ones a player likes to have
			for (int d = 0; d < 3; ++d) {
				const int unit = rl::kChipValues[d], next = rl::kChipValues[d + 1], step = next / unit;
				n[d] = (left % next) / unit;  // what has to be in this colour
				left -= n[d] * unit;
				while (n[d] + step <= kWant[d] && left >= next) {  // a bigger chip broken down, while there's room
					n[d] += step;
					left -= next;
				}
			}
			n[3] = left / rl::kChipValues[3];
			return n;
		}

		// where a player's stack of one colour stands: four in a square by their cards, the hundreds and twenty-fives
		// further in, the fives and ones nearer them
		// how far a point (table space) is inside the felt's edge
		float InsideFelt(const RE::NiPoint3& a_p)
		{
			if (std::abs(a_p.y) <= kFeltC) {
				return kFeltR - std::abs(a_p.x);
			}
			return kFeltR - std::hypot(a_p.x, std::abs(a_p.y) - kFeltC);
		}

		RE::NiPoint3 ColumnSpot(int a_seat, int a_d)
		{
			RE::NiPoint3 e, n;
			Stadium(kHolders[a_seat].first, kHolders[a_seat].second, kFeltR, kFeltC, e, n);
			const RE::NiPoint3 side{ n.y, -n.x, 0.0f };
			// (beside their cards, 2 clear of them)
			const RE::NiPoint3 base = e - n * 5.0f - side * (kCardW + 0.4f + 2.0f + kChipR * 2.2f);
			const float        a = kChipR * 1.2f;  // (clear of each other)
			float in = 0.0f;  // (how far all four are moved in)
			auto  spot = [&](int d) { return base - n * in + n * (d >= 2 ? -a : a) + side * (d == 1 || d == 3 ? a : -a); };
			// all four moved in together, as far as it takes for every chip to be clear of the edge (round the ends,
			// where the felt curves away, it does)
			const float need = kChipR + 0.8f;
			for (int tries = 0; tries < 12; ++tries) {
				float least = 1e9f;
				for (int d = 0; d < 4; ++d) {
					least = std::min(least, InsideFelt(spot(d)));
				}
				if (least >= need) {
					break;
				}
				in += need - least + 0.2f;
			}
			const RE::NiPoint3 p = spot(a_d);
			return { p.x, p.y, kFeltZ };
		}

		// the pot: a stack of each colour side by side, ones to hundreds along the table (a stack past 16 starts another
		// beside it)
		RE::NiPoint3 PotSpot(int a_d, int a_n)
		{
			// four stacks in a square (ones and fives nearer the board, twenty-fives and hundreds further off); a stack
			// past 16 starts another, further from the board
			constexpr int kPotStack = 16;
			const float   gap = kChipR * 2.3f;
			const int     col = a_n / kPotStack, row = a_n % kPotStack;
			const float   x = kPot.x + (a_d % 2 == 0 ? -0.5f : 0.5f) * gap;
			const float   y = kPot.y + (a_d < 2 ? 0.5f : -0.5f) * gap - static_cast<float>(col) * gap * 2.0f;
			return { x, y, kLift + static_cast<float>(row) * kStep };
		}

		int ChipIndex(const RE::TESObjectREFR* a_ref)
		{
			const auto* base = a_ref ? a_ref->GetObjectReference() : nullptr;
			for (int d = 0; d < 4; ++d) {
				if (base && base == Forms::Chip(d)) {
					return d;
				}
			}
			return 0;
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
			// in front of them, and further in if that's where their stacks are (round the table's ends they're moved in)
			RE::NiPoint3 p = e - n * 13.0f;
			const float  clear = kChipR * 2.0f + 1.5f;
			for (int tries = 0; tries < 20; ++tries) {
				float least = 1e9f;
				for (int d = 0; d < 4; ++d) {
					const auto c = ColumnSpot(a_seat, d);
					least = std::min(least, std::hypot(p.x - c.x, p.y - c.y));
				}
				if (least >= clear) {
					break;
				}
				p = p - n * 1.0f;
			}
			return { p.x, p.y, kFeltZ };
		}

		RE::NiPoint3 BoardSpot(int a_k)
		{
			return { 10.5f, (static_cast<float>(a_k) - 2.0f) * 5.0f, kFeltZ };  // (by the chip tray)
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

		// a new object's 3D fades in over a moment (the game's way with things that load in): chips and cards on the table
		// are there at once, so they move rather than fade from place to place
		void NoFade(const RE::ObjectRefHandle& a_handle)
		{
			if (auto ref = a_handle.get()) {
				if (auto* root = ref->Get3D()) {
					if (auto* fade = root->IsFadeNode()) {
						fade->currentFade = 1.0f;  // (no fading in as it's made, nor out with distance)
						fade->currentDecalFade = 1.0f;
					}
				}
			}
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
		const RE::NiPoint3 p = e + n * (Settings::Get().pkChairs ? kChairOut + 18.0f : kStandOff);  // (a chair: next to it, to sit from)
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
		for (auto& seat : _pk.stackChips) {
			for (auto& v : seat) {
				for (auto& h : v) {
					Remove(h);
				}
			}
		}
		for (auto& v : _pk.landed) {
			for (auto& h : v) {
				Remove(h);
			}
		}
		for (auto& m : _pk.moves) {
			Remove(m.ref);
		}
		for (auto& h : _pk.chairs) {
			Remove(h);
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
		_pk.potCount.fill(0);
		_pk.potShown = -1;
		_pk.boardShown = 0;
		_pk.peeking = false;
		if (a_cards) {
			// (chips still on their way onto someone's stacks keep going: theirs. Only those that were to go are gone)
			for (auto& m : _pk.moves) {
				if (m.remove) {
					Remove(m.ref);
				}
			}
			std::erase_if(_pk.moves, [](const PkChipMove& a_m) { return a_m.remove || !a_m.ref.get(); });
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

	// The cards and chips on their way somewhere, moved in step with the frame: from the camera's own update (the
	// main thread, once a frame, just before the frame's drawn) while you're at the table. The rest of the table's
	// work (the hand, the settlers, what's made and what goes) is the task each frame queues from the render thread,
	// which doesn't keep step: some frames it ran twice and some not at all, so a chip on the move went in little jumps
	// (and opening a menu and closing it again put it back in step for a while)
	void Game::PkAnimate()
	{
		const auto now = std::chrono::steady_clock::now();
		float      dt = _animHave ? std::chrono::duration<float>(now - _animLast).count() : 0.0f;
		_animLast = now;
		_animHave = true;
		if (!_pk.table || !_pkSeated) {
			return;
		}
		dt = std::clamp(dt, 0.0f, 0.05f);
		Frame frame;
		if (!PkFrame(frame)) {
			return;
		}
		if (!_animHad) {
			_animHad = true;
			log::info("poker: the cards and chips moved with the camera's update, once a frame");
		}
		_animAt = now;
		PkTickCards(frame, dt);
		PkTickMoves(frame, dt);
	}

	bool Game::PkAnimating() const
	{
		return _pkSeated && std::chrono::steady_clock::now() - _animAt < std::chrono::milliseconds(250);
	}

	void Game::TickPoker(float a_dt)
	{
		if (!_pk.table) {
			return;
		}
		Frame frame;
		if (!PkFrame(frame)) {
			return;
		}
		// trouble: a fight breaks out (you're in combat) and you're up from the table, your seat and caps kept
		if (_pkSeated) {
			if (auto* player = RE::PlayerCharacter::GetSingleton(); player && player->IsInCombat()) {
				log::info("poker: you're in a fight: up from the table");
				PkStandUp(false);
				Notify("Poker: trouble! You're up from the table (your seat and caps are kept)");
			}
		}
		// (the chairs option) a chair at each seat while it's on; none when it's off
		{
			const bool want = Settings::Get().pkChairs;
			for (int i = 0; i < kPkSeats; ++i) {
				auto& h = _pk.chairs[i];
				if (!want) {
					if (h) {
						Remove(h);
					}
					continue;
				}
				if (!h.get()) {
					auto*        chairBase = RE::TESForm::GetFormByID(kChair);
					auto*        chairForm = chairBase && chairBase->IsBoundObject() ? static_cast<RE::TESBoundObject*>(chairBase) : nullptr;
					RE::NiPoint3 facing;
					const auto   at = ChairSpot(i, facing);
					if (chairForm) {
						h = Spawn(chairForm, ToWorld(frame, at.x, at.y, 0.0), HeadingOf(frame, facing.x, facing.y));
					}
				}
			}
		}
		_pk.bannerAge += a_dt;
		_pk.potHold = std::max(0.0f, _pk.potHold - a_dt);
		_pk.pushAge += a_dt;
		_pk.seatedAge += a_dt;
		PkTickSeats(frame, a_dt);
		PkTickHand(frame, a_dt);
		if (!PkAnimating()) {
			PkTickCards(frame, a_dt);
		}
		PkMakeChips(frame);
		if (!PkAnimating()) {
			PkTickMoves(frame, a_dt);  // (when the camera's update isn't moving them: away from the table)
		}
		for (const auto& seat : _pk.stackChips) {
			for (const auto& v : seat) {
				for (const auto& h : v) {
					NoFade(h);
				}
			}
		}
		for (const auto& v : _pk.landed) {
			for (const auto& h : v) {
				NoFade(h);
			}
		}
		for (const auto& v : _pk.betChips) {
			for (const auto& h : v) {
				NoFade(h);
			}
		}
		for (const auto& h : _pk.potChips) {
			NoFade(h);
		}
		for (const auto& c : _pk.cards) {
			NoFade(c.ref);
		}
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
			// (in once the river's out or someone's all in, and held there through the showdown until the hand's
			// cleared away: no going in and out)
			bool allIn = false;
			if (_pk.inHand) {
				for (const auto& p : _pk.hand.players) {
					allIn |= p.allIn && !p.folded;
				}
			}
			const bool  pushIn = _pk.inHand && (_pk.boardShown >= 5 || allIn || _pk.phase == PkPhase::kShowdown);
			_pk.pushLevel += ((pushIn ? 1.0f : 0.0f) - _pk.pushLevel) * std::min(1.0f, a_dt * (pushIn ? 2.2f : 1.2f));
			const float push = std::clamp(_pk.pushLevel, 0.0f, 1.0f);
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
			const int   turn = std::clamp((right ? 1 : 0) - (left ? 1 : 0) + _padTurn.load(), -1, 1);
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
				if (_mouseDX != 0.0 || _mouseDY != 0.0) {
					_padUsed = false;  // (the mouse again: the buttons without the controller's names)
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
		if (const auto* you = RE::PlayerCharacter::GetSingleton(); you && you->IsInCombat()) {
			return;  // (nobody sits down in a fight)
		}
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
		{
			// the wasteland's own: ghouls and synths play like what they are
			auto*       race = actor->race;
			const char* raceId = race ? race->formEditorID.c_str() : nullptr;
			if (raceId && std::strstr(raceId, "Ghoul")) {
				st.style = pk::Style::Of(pk::Archetype::kGhoul, st.id);
			} else if (raceId && std::strstr(raceId, "Synth")) {
				st.style = pk::Style::Of(pk::Archetype::kSynth, st.id);
			}
		}
		log::info("poker: {} plays like a {}", st.name, pk::ArchetypeName(st.style.archetype));
		st.hands = std::uniform_int_distribution<int>(6, 20)(_rng);
		st.stack = TakePurse(st.id, s.pkNpcBuyIn);
		log::info("poker: {} ({:08X}) sits in at seat {} with {} caps", st.name, st.id, seat, st.stack);
		PkSay(std::format("{} SITS IN WITH {}", Upper(st.name), st.stack));
		PkGreet(st);
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
			if (st.held && !Settings::Get().pkChairs) {
				Npc::SetRestrained(actor, false);
			}
			if (auto* proc = actor->currentProcess; proc && proc->GetOccupiedFurniture()) {
				actor->InitiateGetUpPackage();  // (up out of their chair)
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
		const int     kind = a_kind;
		if (Settings::Get().pkChairs) {
			// sitting: only the seated reactions there are (a cheer or a laugh for a win)
			if (a_kind != 3) {
				return;
			}
			a_kind = 99;
			idle = pick(kSeatedWin);
		}
		switch (a_kind) {
		case 0: idle = pick(kLookIdles); break;
		case 1: idle = kThinkIdle; break;
		case 2: idle = kChipsIdle; break;
		case 3: idle = pick(kWinIdles); break;
		case 99: break;
		default: idle = kNoIdle; break;
		}
		Npc::PlayIdle(actor, idle);
		// and a word or two: a cheer or a laugh for a win, a groan for a lost showdown (now and then)
		if (Settings::Get().pkVoices && (kind == 3 || (kind == 4 && _pk.phase == PkPhase::kShowdown)) &&
			std::uniform_real_distribution<float>(0.0f, 1.0f)(_rng) < 0.7f) {
			const bool laugh = kind == 3 && std::uniform_real_distribution<float>(0.0f, 1.0f)(_rng) < 0.35f;
			Npc::Say(actor, kind == 3 ? (laugh ? kLaugh : kCheer) : kGroan);
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
			// (a fight: everyone up. Held, their AI is off and they'd not notice it themselves)
			const auto* you = RE::PlayerCharacter::GetSingleton();
			if (!Usable(actor) || actor->IsInCombat() || (you && you->IsInCombat())) {
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
				if (s.pkChairs) {
					// sat in their chair: the chair holds them (and turns them to the table); now and then, if they've
					// got up, back down
					st.sitCheck -= a_dt;
					auto chair = _pk.chairs[i].get();
					auto* proc = actor->currentProcess;
					if (chair && st.sitCheck <= 0.0f && !(proc && proc->GetOccupiedFurniture())) {
						Npc::SitOn(actor, chair.get());
						st.sitCheck = 3.0f;
					}
					st.held = true;  // (for the idles: they're where they should be)
					st.atSpot += a_dt;
					break;
				}
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
			p.tilt = st.tilt;
			st.handStart = st.stack;
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
			PkSound(Audio::Bank::kChip, 0.6f);
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
					_pk.think = s.pkThink * static_cast<float>(1.3 + (1.0 - sure) * 3.2) * std::uniform_real_distribution<float>(0.85f, 1.2f)(_rng);
					_pk.thinkTotal = std::max(0.1f, _pk.think);
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
							PkRemember(st, st.stack - st.handStart);
							--st.hands;
							// a bad loss puts them on tilt (how much: their kind); it cools off a hand at a time
							st.tilt *= 0.75;
							if (st.handStart > 0 && st.stack < st.handStart * 0.6 && _pk.hand.showdown) {
								st.tilt = std::min(1.0, st.tilt + st.style.tiltProne * 0.8);
								log::info("poker: {} is on tilt ({:.2f})", st.name, st.tilt);
							}
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
		// a tie: the main pot split between two or more
		std::vector<int> splitSeats;
		if (!h.pots.empty() && h.pots.front().winners.size() > 1) {
			for (int w : h.pots.front().winners) {
				for (int i = 0; i < kPkSeats; ++i) {
					if (_pk.seats[i].used && _pk.seats[i].hi == w) {
						splitSeats.push_back(i);
					}
				}
			}
			_pk.bannerTitle = "TIE - SPLIT POT";
			_pk.bannerSub = h.showdown ? pk::HandName(h.scores[h.pots.front().winners.front()]) : std::string();
			bool youSplit = false;
			for (int i : splitSeats) {
				youSplit |= _pk.seats[i].you;
			}
			_pk.bannerTone = youSplit ? 1 : youIn ? 2 : 0;
			PkSay("TIE - SPLIT POT", 1);
		}
		// your session's count
		if (youIn) {
			++_pk.session.hands;
			for (const auto& st : _pk.seats) {
				if (st.used && st.you && st.hi >= 0 && won[st.hi] > 0) {
					++_pk.session.won;
					_pk.session.biggest = std::max(_pk.session.biggest, won[st.hi]);
				}
			}
		}
		_pk.bannerAge = 0.0f;
		// the pot (and the last bets) slide over to the winner's stack, once their cards are up
		const float reveal = h.showdown ? 1.2f : 0.4f;
		for (auto& v : _pk.betChips) {
			PkSlideChips(v, winnerSeat, reveal);
		}
		_pk.betShown.fill(0);
		if (splitSeats.size() > 1) {
			// (a split: the pot's chips dealt out between them, one each in turn)
			std::vector<std::vector<RE::ObjectRefHandle>> shares(splitSeats.size());
			for (std::size_t c = 0; c < _pk.potChips.size(); ++c) {
				shares[c % shares.size()].push_back(_pk.potChips[c]);
			}
			_pk.potChips.clear();
			for (std::size_t k = 0; k < shares.size(); ++k) {
				PkSlideChips(shares[k], splitSeats[k], reveal);
			}
		}
		PkSlideChips(_pk.potChips, winnerSeat, reveal);
		_pk.potCount.fill(0);
		_pk.potShown = 0;
		_pk.potHold = reveal + 0.8f;
		(void)youResult;
	}

	// ---------------------------------------------------------------- cards and chips

	void Game::PkMuck(int a_seat)
	{
		PkSound(Audio::Bank::kSlide, 0.4f);
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
					PkSound(Audio::Bank::kCard, 0.45f);
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
					PkSound(Audio::Bank::kCard, 0.25f);
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

	void Game::PkSlideChips(std::vector<RE::ObjectRefHandle>& a_chips, int a_seat, float a_delay)
	{
		// each chip off where it is onto the winner's stack of its colour: theirs from then on (their stacks take them
		// in when they're next made: nothing made again, nothing flickers)
		Frame frame;
		if (!PkFrame(frame) || a_seat < 0) {
			for (auto& h : a_chips) {
				Remove(h);
			}
			a_chips.clear();
			return;
		}
		std::array<int, 4> on{};
		for (int d = 0; d < 4; ++d) {
			on[d] = static_cast<int>(_pk.stackChips[a_seat][d].size());
		}
		for (const auto& h : _pk.landed[a_seat]) {
			if (auto r = h.get()) {
				++on[ChipIndex(r.get())];
			}
		}
		int k = 0;
		for (auto& h : a_chips) {
			auto ref = h.get();
			if (!ref) {
				continue;
			}
			std::erase_if(_pk.moves, [&](const PkChipMove& a_m) { return a_m.ref == h; });  // (still on its way somewhere: from here now)
			const int  d = ChipIndex(ref.get());
			const auto to = ColumnSpot(a_seat, d);
			const auto local = frame.rot * ((ref->data.location - frame.origin) * (1.0f / frame.scale));
			PkChipMove m;
			m.ref = h;
			m.from = { local.x, local.y, local.z - kFeltZ };
			m.to = { to.x, to.y, kLift + static_cast<float>(on[d]++) * kStep };
			m.delay = a_delay + 0.025f * static_cast<float>(k);
			m.dur = 0.55f;
			m.yaw = ref->data.angle.z;
			m.remove = false;
			_pk.moves.push_back(m);
			_pk.landed[a_seat].push_back(h);
			++k;
		}
		a_chips.clear();
		if (k > 0) {
			PkSound(Audio::Bank::kChip, 0.6f);
		}
	}

	void Game::PkThrowChips(std::vector<RE::ObjectRefHandle>& a_chips, float a_delay)
	{
		// thrown in as they are (no change made), each onto the pot's stack of its colour: a stack of ones, of fives, of
		// twenty-fives and of hundreds, side by side
		Frame frame;
		if (!PkFrame(frame)) {
			for (auto& h : a_chips) {
				Remove(h);
			}
			a_chips.clear();
			return;
		}
		std::uniform_real_distribution<float> u(0.0f, 1.0f);
		int                                   k = 0;
		for (auto& h : a_chips) {
			auto ref = h.get();
			if (!ref) {
				continue;
			}
			std::erase_if(_pk.moves, [&](const PkChipMove& a_m) { return a_m.ref == h; });  // (still on its way somewhere: from here now)
			const int  d = ChipIndex(ref.get());
			const auto local = frame.rot * ((ref->data.location - frame.origin) * (1.0f / frame.scale));
			PkChipMove m;
			m.ref = h;
			m.from = { local.x, local.y, local.z - kFeltZ };
			m.to = PotSpot(d, _pk.potCount[d]++);
			m.delay = a_delay + 0.03f * static_cast<float>(k);
			m.dur = 0.5f + u(_rng) * 0.1f;
			m.yaw = ref->data.angle.z;
			m.remove = false;
			_pk.moves.push_back(m);
			_pk.potChips.push_back(h);
			++k;
		}
		a_chips.clear();
		if (k > 0) {
			PkSound(Audio::Bank::kChip, 0.6f);
		}
	}

	void Game::PkSound(Audio::Bank a_bank, float a_gain)
	{
		// at the table, not in your ear: quieter further off, gone past five metres (Audio::Play)
		Frame frame;
		if (PkFrame(frame)) {
			Audio::Play(a_bank, ToWorld(frame, 0.0f, 0.0f, kFeltZ), a_gain);
		}
	}

	void Game::PkTickMoves(const Frame& a_frame, float a_dt)
	{
		for (auto& m : _pk.moves) {
			if (m.delay > 0.0f) {
				m.delay -= a_dt;
				continue;
			}
			auto ref = m.ref.get();
			if (!ref) {
				m.t = 1.0f;  // (gone)
				continue;
			}
			if (!ref->Get3D()) {
				continue;  // a chip just made: its 3D isn't loaded yet. It starts once it's there, rather than jumping to partway
			}
			m.t = std::min(1.0f, m.t + a_dt / m.dur);
			// picked up, carried over and set down: up quickly, across, down onto the stack
			const float e = m.t * m.t * (3.0f - 2.0f * m.t);
			const auto  p = m.from + (m.to - m.from) * e;
			const float up = m.t < 0.25f ? m.t / 0.25f : m.t > 0.8f ? (1.0f - m.t) / 0.2f : 1.0f;
			NoFade(m.ref);
			// moved as it is, turned as the game turned it: our own yaw matrix turned it the other way round from the
			// game's, so a chip snapped round as it set off and again whenever the game put it back to its own
			if (!m.haveRot) {
				m.rot = ref->Get3D()->world.rotate;
				m.haveRot = true;
			}
			PlaceRoot(ref.get(), ToWorld(a_frame, p.x, p.y, kFeltZ + p.z + up * 2.2f), m.rot);
			if (m.t >= 1.0f && m.remove) {
				Remove(m.ref);
			}
		}
		std::erase_if(_pk.moves, [](const PkChipMove& m) { return m.t >= 1.0f; });
	}

	void Game::PkMakeChips(const Frame& a_frame)
	{
		std::uniform_real_distribution<float> spin(0.0f, 6.283f);
		std::uniform_real_distribution<float> slop(-0.1f, 0.1f);  // (no stack quite straight)
		auto value = [](const std::array<int, 4>& a_n) {
			int v = 0;
			for (int d = 0; d < 4; ++d) {
				v += a_n[d] * rl::kChipValues[d];
			}
			return v;
		};
		auto colourOf = [](int a_value) { return static_cast<int>(std::ranges::find(rl::kChipValues, a_value) - std::begin(rl::kChipValues)); };
		// a chip made on top of a player's stack of that colour (unless it's so tall the rest aren't shown)
		auto putOn = [&](int a_seat, int a_d) {
			auto& col = _pk.stackChips[a_seat][a_d];
			if (static_cast<int>(col.size()) >= kColMax) {
				return;
			}
			const auto spot = ColumnSpot(a_seat, a_d);
			col.push_back(Spawn(Forms::Chip(a_d),
				ToWorld(a_frame, spot.x + slop(_rng), spot.y + slop(_rng), kFeltZ + kLift + static_cast<float>(col.size()) * kStep),
				a_frame.yaw + spin(_rng)));
		};
		auto remake = [&](int a_seat, int a_have) {
			for (auto& c : _pk.stackChips[a_seat]) {
				for (auto& h : c) {
					Remove(h);
				}
				c.clear();
			}
			_pk.stackCols[a_seat] = StackCounts(a_have);
			for (int d = 0; d < 4; ++d) {
				for (int c = 0; c < std::min(_pk.stackCols[a_seat][d], kColMax); ++c) {
					putOn(a_seat, d);
				}
			}
		};
		// each player's caps in front of them, a stack of each colour, kept as real chips: a bet takes the chips it's
		// paid with off the tops (those very chips slide out), change is made only when it has to be, and chips won
		// are taken in as they land. Nothing is made again, so nothing flickers
		std::array<std::vector<RE::ObjectRefHandle>, kPkSeats> paid;           // (taken off for a bet, this tick)
		std::array<std::array<int, 4>, kPkSeats>               paidColours{};  // (all it was paid with, shown or not)
		for (int i = 0; i < kPkSeats; ++i) {
			const auto& st = _pk.seats[i];
			int         have = 0;
			if (st.used && !st.away && (st.you || st.step == Seat::Step::kPlaying)) {
				have = _pk.inHand && st.hi >= 0 ? _pk.hand.players[st.hi].stack : st.stack;
			}
			if (_pk.potHold > 0.0f && _pk.phase == PkPhase::kShowdown) {
				continue;  // (the pot's on its way over: the winner's stacks take it in once it's there)
			}
			if (have == _pk.stackShown[i]) {
				continue;
			}
			auto& n = _pk.stackCols[i];  // how many of each colour they have (shown up to kColMax)
			auto& cols = _pk.stackChips[i];
			// chips won, landed (or landing) on their stacks: theirs now
			for (auto& h : _pk.landed[i]) {
				if (auto r = h.get()) {
					const int d = ChipIndex(r.get());
					if (static_cast<int>(cols[d].size()) < kColMax) {
						cols[d].push_back(h);
					} else {
						Remove(h);
					}
					++n[d];
				}
			}
			_pk.landed[i].clear();
			if (have <= 0) {
				remake(i, 0);
			} else if (_pk.stackShown[i] < 0 || value(n) <= 0) {
				remake(i, have);  // (sat down, or back from a save)
			} else if (have > value(n)) {
				// more (a win the chips didn't cover, or a bet handed back): put on
				for (int v : rl::ChipsFor(have - value(n))) {
					putOn(i, colourOf(v));
					++n[colourOf(v)];
				}
			} else if (have < value(n)) {
				// less: paid out of the stacks, the biggest chips that fit first; change made only when it has to be (the
				// smallest bigger chip broken into what's owed and what's left of it)
				int owe = value(n) - have;
				for (int pass = 0; pass < 2 && owe > 0; ++pass) {
					for (int d = 3; d >= 0 && owe > 0; --d) {
						while (n[d] > 0 && rl::kChipValues[d] <= owe) {
							--n[d];
							owe -= rl::kChipValues[d];
							++paidColours[i][d];
							if (static_cast<int>(cols[d].size()) > n[d]) {
								paid[i].push_back(cols[d].back());  // (the top chip: the one that goes)
								cols[d].pop_back();
							}
						}
					}
					if (owe > 0) {
						int b = 0;
						while (b < 4 && (n[b] == 0 || rl::kChipValues[b] <= owe)) {
							++b;
						}
						if (b == 4) {
							break;
						}
						--n[b];
						if (static_cast<int>(cols[b].size()) > n[b]) {
							Remove(cols[b].back());
							cols[b].pop_back();
						}
						for (int v : rl::ChipsFor(owe)) {
							putOn(i, colourOf(v));
							++n[colourOf(v)];
						}
						for (int v : rl::ChipsFor(rl::kChipValues[b] - owe)) {
							putOn(i, colourOf(v));
							++n[colourOf(v)];
						}
					}
				}
				if (owe > 0 || value(n) != have) {
					log::warn("poker: seat {} stacks made again ({} owed)", i, owe);
					remake(i, have);
				}
			}
			_pk.stackShown[i] = have;
		}
		if (!_pk.inHand || _pk.phase == PkPhase::kShowdown) {
			for (auto& v : paid) {
				for (auto& h : v) {
					Remove(h);
				}
			}
			return;
		}
		// the pot: everything put in on earlier streets
		int pot = 0;
		for (const auto& p : _pk.hand.players) {
			pot += p.committed - p.bet;
		}
		const bool potGrew = pot > _pk.potShown && _pk.potShown >= 0;
		if (_pk.potShown < 0 && pot > 0 && _pk.potChips.empty()) {
			// (back from a save, the chips taken off for it: the pot made again)
			for (int v : rl::ChipsFor(pot)) {
				const int  d = colourOf(v);
				const auto p = PotSpot(d, _pk.potCount[d]++);
				_pk.potChips.push_back(Spawn(Forms::Chip(d), ToWorld(a_frame, p.x, p.y, kFeltZ + p.z), a_frame.yaw + spin(_rng)));
			}
		}
		_pk.potShown = pot;
		// each player's bet this street, a little pile in front of them: the chips they paid it with, slid out off their
		// stacks; at the end of a street, thrown into the pot as it is
		for (int i = 0; i < kPkSeats; ++i) {
			const auto& st = _pk.seats[i];
			const int   bet = st.used && st.hi >= 0 ? _pk.hand.players[st.hi].bet : 0;
			auto&       chips = _pk.betChips[i];
			const auto  b = BetSpot(i);
			auto        place = [&](std::size_t a_k) {
                const int col = static_cast<int>(a_k) / kMaxStack, row = static_cast<int>(a_k) % kMaxStack;
                // (a pile past eight chips starts another, further in towards the middle: away from their stacks)
                const float l = std::max(1.0f, std::hypot(b.x, b.y));
                const float in = static_cast<float>(col) * kChipR * 2.3f;
                return RE::NiPoint3{ b.x - b.x / l * in + slop(_rng) * 2.0f, b.y - b.y / l * in + slop(_rng) * 2.0f,
                    kLift + static_cast<float>(row) * kStep };
			};
			if (bet == _pk.betShown[i]) {
				for (auto& h : paid[i]) {
					Remove(h);  // (paid out with no bet to show for it)
				}
				continue;
			}
			const bool fromStack = _pk.betShown[i] >= 0 && bet > _pk.betShown[i];
			if (!fromStack) {
				if (bet == 0 && potGrew) {
					PkThrowChips(chips, 0.1f);
				} else {
					for (auto& h : chips) {
						Remove(h);
					}
					chips.clear();
				}
				for (auto& h : paid[i]) {
					Remove(h);
				}
				_pk.betShown[i] = bet;
				// (a bet with no stack it came from, after a save: made where it is)
				for (int v : rl::ChipsFor(bet)) {
					const auto p = place(chips.size());
					chips.push_back(Spawn(Forms::Chip(colourOf(v)), ToWorld(a_frame, p.x, p.y, kFeltZ + p.z), a_frame.yaw + spin(_rng)));
				}
				continue;
			}
			_pk.betShown[i] = bet;
			// the chips they paid with, slid out off their stacks (any too high up a stack to be shown: made at its top)
			std::vector<std::pair<RE::ObjectRefHandle, RE::NiPoint3>> out;
			std::array<int, 4>                                        left = paidColours[i];
			for (auto& h : paid[i]) {
				auto r = h.get();
				if (!r) {
					continue;
				}
				--left[ChipIndex(r.get())];
				const auto local = a_frame.rot * ((r->data.location - a_frame.origin) * (1.0f / a_frame.scale));
				out.emplace_back(h, RE::NiPoint3{ local.x, local.y, local.z - kFeltZ });
			}
			for (int d = 0; d < 4; ++d) {
				for (int c = 0; c < left[d]; ++c) {
					const auto  spot = ColumnSpot(i, d);
					const float z = kLift + static_cast<float>(_pk.stackChips[i][d].size()) * kStep;
					out.emplace_back(Spawn(Forms::Chip(d), ToWorld(a_frame, spot.x, spot.y, kFeltZ + z), a_frame.yaw + spin(_rng)),
						RE::NiPoint3{ spot.x, spot.y, z });
				}
			}
			int k = 0;
			for (auto& [h, from] : out) {
				auto       r = h.get();
				const auto to = place(chips.size());
				chips.push_back(h);
				std::erase_if(_pk.moves, [&](const PkChipMove& a_m) { return a_m.ref == h; });
				PkChipMove m;
				m.ref = h;
				m.from = from;
				m.to = to;
				m.delay = 0.04f * static_cast<float>(k++);
				m.dur = 0.45f;
				m.yaw = r ? r->data.angle.z : a_frame.yaw;
				m.remove = false;
				_pk.moves.push_back(m);
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
			st.name = player->GetDisplayFullName();  // (your character's name on the HUD; "YOU" in what's said)
			if (st.name.empty()) {
				st.name = "You";
			}
			st.stack = buyIn;
			st.step = Seat::Step::kPlaying;
			_pk.session = PkSession{};
			_pk.session.buyIn = buyIn;
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
		_pk.seatedAge = 0.0f;
		_pkShowCards = false;
		_padUsed = false;
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
		PkSessionEnd(st.stack);
		st = PkSeat{};
		_pkYou = -1;
	}

	void Game::PkStandUp(bool a_cashOut)
	{
		_afterTable = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() + 1500;
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
				if (r.toAct && !st.you && _pk.think > 0.0f) {
					r.think = std::clamp(_pk.think / _pk.thinkTotal, 0.0f, 1.0f);
				}
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
		std::ranges::stable_partition(a_h.pkRows, [](const auto& a_r) { return a_r.you; });  // (you at the top)
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
		a_h.pkAge = _pk.seatedAge;
		a_h.pkPad = _padUsed.load();
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
		_pk.potCount.fill(0);
		_pk.potShown = -1;
		_pk.peeking = false;
		for (auto& seat : _pk.stackChips) {
			for (auto& v : seat) {
				for (auto& h : v) {
					Remove(h);
				}
				v.clear();
			}
		}
		_pk.stackShown.fill(-1);
		for (auto& c : _pk.stackCols) {
			c.fill(0);
		}
		for (auto& v : _pk.landed) {
			for (auto& h : v) {
				Remove(h);
			}
			v.clear();
		}
		for (auto& h : _pk.chairs) {
			Remove(h);  // (back on the next tick, and whoever was in one sits again)
		}
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
		if (!a_event) {
			return false;
		}
		if (!_pkSeated) {
			// just up from the table: the mouse button or pad button that got you up (Step away, Cash out) is kept from
			// the game until it's let go, or it'd draw your gun or swing at the table. (For a second and a half at
			// most)
			const auto until = _afterTable.load();
			if (until == 0) {
				return false;
			}
			const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
			if (now > until) {
				_afterTable = 0;
				return false;
			}
			const auto dev = a_event->device.get();
			if (dev == RE::INPUT_DEVICE::kMouse || dev == RE::INPUT_DEVICE::kGamepad) {
				if (a_event->QAnalogValue() == 0.0f) {
					_afterTable = 0;  // (let go: everything the game's again from the next one)
				}
				return true;
			}
			return false;
		}
		const auto& id = a_event->QUserEvent();
		const char* name = id.c_str() ? id.c_str() : "";
		const auto  code = a_event->QIDCode();
		const auto& s = Settings::Get();
		const bool  key = a_event->device.get() == RE::INPUT_DEVICE::kKeyboard && code != 0 &&
		                 (static_cast<int>(code) == s.keyTurnLeft || static_cast<int>(code) == s.keyTurnRight);
		const bool pad = a_event->device.get() == RE::INPUT_DEVICE::kGamepad;  // (the controller's all ours at the table)
		if (key || pad || std::string_view(name) == "Workshop") {
			static std::set<std::string> logged;
			if (logged.insert(name).second) {
				log::info("poker: the {} key ({}) kept from the game's menus while you're at the table", name, code);
			}
			return true;
		}
		return false;
	}
}
