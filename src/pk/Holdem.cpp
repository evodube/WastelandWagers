#include "pk/Holdem.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace pk
{
	std::string ActionName(const Action& a_a, int a_paid)
	{
		switch (a_a.kind) {
		case Action::Kind::kFold: return "FOLDS";
		case Action::Kind::kCheck: return "CHECKS";
		case Action::Kind::kCall: return "CALLS " + std::to_string(a_paid);
		case Action::Kind::kRaise: return "RAISES TO " + std::to_string(a_a.to);
		default: return "GOES ALL IN";
		}
	}

	const char* ArchetypeName(Archetype a_a)
	{
		switch (a_a) {
		case Archetype::kTightAggressive: return "TIGHT-AGGRESSIVE";
		case Archetype::kLooseAggressive: return "LOOSE-AGGRESSIVE";
		case Archetype::kCallingStation: return "CALLING STATION";
		case Archetype::kManiac: return "MANIAC";
		case Archetype::kRock: return "ROCK";
		case Archetype::kRaider: return "RAIDER";
		case Archetype::kGhoul: return "GHOUL";
		case Archetype::kSynth: return "SYNTH";
		case Archetype::kPrewarSnob: return "PRE-WAR SNOB";
		default: return "";
		}
	}

	Style Style::Of(Archetype a_a, std::uint32_t a_seed)
	{
		std::mt19937                           rng(a_seed * 2654435761u + 17u);
		std::uniform_real_distribution<double> u(-1.0, 1.0);
		//                         loose  aggr  bluff  stubborn trap  draw  noise tilt
		struct P { double l, a, b, s, t, d, n, tp; };
		static constexpr std::array<P, static_cast<std::size_t>(Archetype::kCount)> kP{ {
			{ 0.30, 0.75, 0.10, 0.05, 0.15, 0.55, 0.12, 0.35 },  // tight-aggressive
			{ 0.75, 0.80, 0.20, 0.10, 0.10, 0.70, 0.18, 0.50 },  // loose-aggressive
			{ 0.70, 0.15, 0.03, 0.85, 0.05, 0.15, 0.15, 0.30 },  // calling station
			{ 0.90, 0.95, 0.35, 0.20, 0.02, 0.80, 0.30, 0.80 },  // maniac
			{ 0.10, 0.35, 0.02, 0.05, 0.25, 0.20, 0.08, 0.10 },  // rock
			{ 0.70, 0.85, 0.28, 0.25, 0.05, 0.65, 0.28, 0.95 },  // raider
			{ 0.25, 0.45, 0.06, 0.15, 0.45, 0.35, 0.10, 0.00 },  // ghoul
			{ 0.30, 0.75, 0.10, 0.05, 0.18, 0.55, 0.02, 0.00 },  // synth
			{ 0.25, 0.25, 0.15, 0.10, 0.20, 0.25, 0.12, 0.60 },  // pre-war snob
		} };
		const auto& p = kP[static_cast<std::size_t>(a_a)];
		auto        wob = [&](double a_v, double a_by) { return std::clamp(a_v + u(rng) * a_by, 0.0, 1.0); };
		Style       s;
		s.archetype = a_a;
		s.looseness = wob(p.l, 0.08);
		s.aggression = wob(p.a, 0.08);
		s.bluffing = wob(p.b, 0.03);
		s.stubborn = wob(p.s, 0.05);
		s.trapping = wob(p.t, 0.05);
		s.drawing = wob(p.d, 0.08);
		s.noise = p.n;
		s.tiltProne = p.tp;
		return s;
	}

	Style Style::From(std::uint32_t a_seed)
	{
		// most tables: a mix of the classic kinds and the wasteland's (the plugin picks ghouls and synths by who they are)
		std::mt19937 rng(a_seed * 747796405u + 2891336453u);
		static constexpr std::array<Archetype, 7> kCommon{ Archetype::kTightAggressive, Archetype::kLooseAggressive, Archetype::kCallingStation,
			Archetype::kManiac, Archetype::kRock, Archetype::kRaider, Archetype::kPrewarSnob };
		static constexpr std::array<double, 7> kWeight{ 3, 2, 3, 1, 2, 2, 1 };
		std::discrete_distribution<int>        pick(kWeight.begin(), kWeight.end());
		return Of(kCommon[static_cast<std::size_t>(pick(rng))], a_seed);
	}

	int Hand::Live() const
	{
		return static_cast<int>(std::ranges::count_if(players, [](const Player& p) { return !p.folded; }));
	}

	int Hand::CanBet() const
	{
		return static_cast<int>(std::ranges::count_if(players, [](const Player& p) { return !p.folded && !p.allIn; }));
	}

	int Hand::NextSeat(int a_from, bool a_needChips) const
	{
		const int n = static_cast<int>(players.size());
		for (int k = 1; k <= n; ++k) {
			const int s = (a_from + k) % n;
			const auto& p = players[s];
			if (p.folded || (a_needChips && p.allIn)) {
				continue;
			}
			return s;
		}
		return -1;
	}

	int Hand::PotTotal() const
	{
		return std::accumulate(players.begin(), players.end(), 0, [](int a, const Player& p) { return a + p.committed; });
	}

	int Hand::ToCall(int a_seat) const
	{
		const auto& p = players[a_seat];
		return std::min(p.stack, std::max(0, _currentBet - p.bet));
	}

	int Hand::MinRaiseTo(int a_seat) const
	{
		return std::min(MaxRaiseTo(a_seat), _currentBet + std::max(_lastRaise, bigBlind));
	}

	int Hand::MaxRaiseTo(int a_seat) const
	{
		const auto& p = players[a_seat];
		return p.bet + p.stack;
	}

	void Hand::Start(std::vector<Player> a_players, int a_button, int a_small, int a_big, std::mt19937& a_rng)
	{
		players = std::move(a_players);
		board.clear();
		pots.clear();
		paid.assign(players.size(), 0);
		scores.assign(players.size(), 0);
		street = Street::kPreflop;
		smallBlind = a_small;
		bigBlind = a_big;
		showdown = false;
		lastAggressor = -1;
		preflopRaiser = -1;
		for (auto& p : players) {
			p.bet = p.committed = 0;
			p.folded = p.stack <= 0;  // (no chips: sitting this one out)
			p.allIn = false;
			p.acted = false;
		}
		button = players[a_button].folded ? NextSeat(a_button, false) : a_button;
		_deck.Shuffle(a_rng);
		// two cards each, one at a time from the left of the button
		for (int round = 0; round < 2; ++round) {
			int s = button;
			for (int k = 0; k < Live(); ++k) {
				s = NextSeat(s, false);
				players[s].hole[round] = _deck.Deal();
			}
		}
		// the blinds (heads up, the button posts the small one)
		auto post = [&](int a_seat, int a_amount) {
			auto&     p = players[a_seat];
			const int put = std::min(a_amount, p.stack);
			p.stack -= put;
			p.bet += put;
			p.committed += put;
			p.allIn = p.stack == 0;
		};
		const int sb = Live() == 2 ? button : NextSeat(button, false);
		const int bb = NextSeat(sb, false);
		post(sb, a_small);
		post(bb, a_big);
		_currentBet = a_big;
		_lastRaise = a_big;
		_toAct = NextSeat(bb, true);
		if (RoundDone()) {
			EndStreet();
		}
	}

	bool Hand::RoundDone() const
	{
		if (Live() <= 1) {
			return true;
		}
		int waiting = 0;
		for (const auto& p : players) {
			if (p.folded || p.allIn) {
				continue;
			}
			if (p.bet < _currentBet) {
				return false;
			}
			if (!p.acted) {
				++waiting;
			}
		}
		// everyone has acted and matched; or only one can still bet and nobody's bet over them
		return waiting == 0 || CanBet() <= 1;
	}

	int Hand::Apply(Action a_action)
	{
		if (_toAct < 0 || Over()) {
			return 0;
		}
		auto&     p = players[_toAct];
		const int toCall = ToCall(_toAct);
		auto      pay = [&](int a_amount) {
			const int put = std::clamp(a_amount, 0, p.stack);
			p.stack -= put;
			p.bet += put;
			p.committed += put;
			if (p.stack == 0) {
				p.allIn = true;
			}
			return put;
		};
		int paidIn = 0;
		using K = Action::Kind;
		if (a_action.kind == K::kCheck && toCall > 0) {
			a_action.kind = K::kFold;
		}
		if (a_action.kind == K::kAllIn) {
			a_action = { K::kRaise, MaxRaiseTo(_toAct) };
		}
		if (a_action.kind == K::kRaise && MaxRaiseTo(_toAct) <= _currentBet) {
			a_action.kind = K::kCall;  // can't raise: not enough behind
		}
		switch (a_action.kind) {
		case K::kFold:
			p.folded = true;
			break;
		case K::kCheck:
			break;
		case K::kCall:
			paidIn = pay(toCall);
			break;
		case K::kRaise:
			{
				const int to = std::clamp(a_action.to, MinRaiseTo(_toAct), MaxRaiseTo(_toAct));
				paidIn = pay(to - p.bet);
				const int raise = p.bet - _currentBet;
				if (raise > 0) {
					if (raise >= _lastRaise) {
						_lastRaise = raise;
						for (auto& q : players) {
							q.acted = false;  // a full raise: everyone gets to act again
						}
					}
					_currentBet = p.bet;
					lastAggressor = _toAct;
					++raises;
					if (street == Street::kPreflop) {
						preflopRaiser = _toAct;
					}
				}
				break;
			}
		default:
			break;
		}
		p.acted = true;
		if (Live() <= 1) {
			Finish();
		} else if (RoundDone()) {
			EndStreet();
		} else {
			_toAct = NextSeat(_toAct, true);
		}
		return paidIn;
	}

	void Hand::Fold(int a_seat)
	{
		if (Over() || a_seat < 0 || a_seat >= static_cast<int>(players.size()) || players[a_seat].folded) {
			return;
		}
		if (a_seat == _toAct) {
			Apply({ Action::Kind::kFold });
			return;
		}
		players[a_seat].folded = true;
		if (Live() <= 1) {
			Finish();
		} else if (RoundDone()) {
			EndStreet();
		}
	}

	void Hand::EndStreet()
	{
		for (auto& p : players) {
			p.bet = 0;
			p.acted = false;
		}
		_currentBet = 0;
		_lastRaise = bigBlind;
		if (Live() <= 1) {
			Finish();
			return;
		}
		switch (street) {
		case Street::kPreflop:
			_deck.Burn();
			for (int i = 0; i < 3; ++i) {
				board.push_back(_deck.Deal());
			}
			street = Street::kFlop;
			break;
		case Street::kFlop:
			_deck.Burn();
			board.push_back(_deck.Deal());
			street = Street::kTurn;
			break;
		case Street::kTurn:
			_deck.Burn();
			board.push_back(_deck.Deal());
			street = Street::kRiver;
			break;
		default:
			Finish();
			return;
		}
		BeginStreet();
	}

	void Hand::BeginStreet()
	{
		raises = 0;
		if (CanBet() <= 1) {
			EndStreet();  // nobody left to bet against: the rest of the board, straight to the showdown
			return;
		}
		_toAct = NextSeat(button, true);
	}

	void Hand::Finish()
	{
		street = Street::kShowdown;
		_toAct = -1;
		const int n = static_cast<int>(players.size());
		paid.assign(n, 0);
		scores.assign(n, 0);
		pots.clear();
		// a bet nobody could match comes back
		{
			int top = -1, second = 0;
			for (int s = 0; s < n; ++s) {
				if (top < 0 || players[s].committed > players[top].committed) {
					top = s;
				}
			}
			for (int s = 0; s < n; ++s) {
				if (s != top) {
					second = std::max(second, players[s].committed);
				}
			}
			if (top >= 0 && players[top].committed > second) {
				const int back = players[top].committed - second;
				players[top].committed -= back;
				players[top].stack += back;
				paid[top] += back;
			}
		}
		showdown = Live() > 1;
		if (showdown) {
			// the rest of the board if it never came (everyone all in)
			while (board.size() < 5) {
				_deck.Burn();
				board.push_back(_deck.Deal());
			}
			for (int s = 0; s < n; ++s) {
				if (!players[s].folded) {
					std::array<Card, 7> cards{ players[s].hole[0], players[s].hole[1], board[0], board[1], board[2], board[3], board[4] };
					scores[s] = Evaluate(cards);
				}
			}
		}
		// the pots: a main pot and a side pot for each all-in level among those still in
		std::vector<int> levels;
		for (const auto& p : players) {
			if (!p.folded && p.committed > 0) {
				levels.push_back(p.committed);
			}
		}
		std::ranges::sort(levels);
		levels.erase(std::unique(levels.begin(), levels.end()), levels.end());
		int prev = 0;
		for (std::size_t li = 0; li < levels.size(); ++li) {
			const int L = levels[li];
			const bool last = li + 1 == levels.size();
			Pot pot;
			for (int s = 0; s < n; ++s) {
				const int c = players[s].committed;
				// (folded players' chips past the last level all go in the last pot)
				pot.amount += (last ? std::max(0, c - prev) : std::max(0, std::min(c, L) - prev));
				if (!players[s].folded && c >= L) {
					pot.eligible.push_back(s);
				}
			}
			prev = L;
			if (pot.amount <= 0) {
				continue;
			}
			// who takes it
			Score best = 0;
			for (int s : pot.eligible) {
				best = std::max(best, showdown ? scores[s] : 1u);
			}
			for (int s : pot.eligible) {
				if ((showdown ? scores[s] : 1u) == best) {
					pot.winners.push_back(s);
				}
			}
			// split; odd caps to the first winner after the button
			const int share = pot.amount / static_cast<int>(pot.winners.size());
			int       odd = pot.amount - share * static_cast<int>(pot.winners.size());
			for (int k = 1; k <= n; ++k) {
				const int s = (button + k) % n;
				if (std::ranges::find(pot.winners, s) != pot.winners.end()) {
					paid[s] += share + (odd > 0 ? 1 : 0);
					odd = std::max(0, odd - 1);
				}
			}
			pots.push_back(std::move(pot));
		}
		// the winnings into the stacks (an uncalled bet's already back)
		{
			std::vector<int> win(n, 0);
			for (const auto& pot : pots) {
				const int share = pot.amount / static_cast<int>(pot.winners.size());
				int       odd = pot.amount - share * static_cast<int>(pot.winners.size());
				for (int k = 1; k <= n; ++k) {
					const int s = (button + k) % n;
					if (std::ranges::find(pot.winners, s) != pot.winners.end()) {
						win[s] += share + (odd > 0 ? 1 : 0);
						odd = std::max(0, odd - 1);
					}
				}
			}
			for (int s = 0; s < n; ++s) {
				players[s].stack += win[s];
			}
		}
	}

	double ChenScore(Card a_a, Card a_b)
	{
		const int hi = std::max(Rank(a_a), Rank(a_b)), lo = std::min(Rank(a_a), Rank(a_b));
		// the high card: ace 10, king 8, queen 7, jack 6, then half its face value
		static constexpr std::array<double, 13> kHigh{ 1, 1.5, 2, 2.5, 3, 3.5, 4, 4.5, 5, 6, 7, 8, 10 };
		double s = kHigh[hi];
		if (hi == lo) {
			return std::max(5.0, s * 2);  // a pair: double, at least 5
		}
		if (Suit(a_a) == Suit(a_b)) {
			s += 2;
		}
		const int gap = hi - lo - 1;
		s -= gap == 0 ? 0 : gap == 1 ? 1 : gap == 2 ? 2 : gap == 3 ? 4 : 5;
		if (gap <= 1 && hi < 10) {
			s += 1;  // connected, below a queen: straights
		}
		return std::ceil(s);
	}

	namespace
	{
		// where a seat sits relative to the button among the players dealt in: 0 first to act .. 1 the button
		double Position(const Hand& a_hand, int a_seat)
		{
			const int n = static_cast<int>(a_hand.players.size());
			int       order = 0, of = 0;
			for (int k = 1; k <= n; ++k) {
				const int s = (a_hand.button + k) % n;
				if (a_hand.players[s].stack + a_hand.players[s].committed <= 0) {
					continue;
				}
				++of;
				if (s == a_seat) {
					order = of;
				}
			}
			return of <= 1 ? 1.0 : static_cast<double>(order - 1) / (of - 1);
		}
	}

	namespace
	{
		// a drawing hand after the flop (one of their own cards in it): four to a flush, or four to an open-ended straight
		bool OnADraw(const std::array<Card, 2>& a_hole, const std::vector<Card>& a_board)
		{
			if (a_board.size() < 3 || a_board.size() >= 5) {
				return false;
			}
			for (int s = 0; s < 4; ++s) {
				int n = 0, mine = 0;
				for (auto c : a_board) {
					n += Suit(c) == s;
				}
				for (auto c : a_hole) {
					n += Suit(c) == s;
					mine += Suit(c) == s;
				}
				if (n == 4 && mine > 0) {
					return true;
				}
			}
			std::array<bool, 14> have{}, mine{};
			auto mark = [&](Card c, bool a_mine) {
				const int r = Rank(c) + 1;  // 1 = two .. 13 = ace
				have[r] = true;
				mine[r] = mine[r] || a_mine;
				if (r == 13) {
					have[0] = true;  // (the ace low too)
					mine[0] = mine[0] || a_mine;
				}
			};
			for (auto c : a_board) {
				mark(c, false);
			}
			for (auto c : a_hole) {
				mark(c, true);
			}
			for (int lo = 1; lo + 3 <= 12; ++lo) {  // four in a row with room at both ends
				bool run = true, own = false;
				for (int r = lo; r < lo + 4; ++r) {
					run = run && have[r];
					own = own || mine[r];
				}
				if (run && own) {
					return true;
				}
			}
			return false;
		}
	}

	Action Decide(const Hand& a_hand, int a_seat, std::mt19937& a_rng, double* a_equity)
	{
		const auto&                            p = a_hand.players[a_seat];
		std::uniform_real_distribution<double> u(0.0, 1.0);
		// their style as it is right now: on tilt, looser and wilder; and every decision wobbles a little (a style's
		// noise), so the same spot isn't always played the same way
		Style st = p.style;
		{
			const double tilt = std::clamp(p.tilt, 0.0, 1.0);
			std::uniform_real_distribution<double> w(-st.noise, st.noise);
			st.looseness = std::clamp(st.looseness + 0.35 * tilt + w(a_rng), 0.0, 1.0);
			st.aggression = std::clamp(st.aggression + 0.3 * tilt + w(a_rng), 0.0, 1.0);
			st.bluffing = std::clamp(st.bluffing + 0.15 * tilt, 0.0, 0.6);
		}
		const int                              opponents = std::max(1, a_hand.Live() - 1);
		const int                              toCall = a_hand.ToCall(a_seat);
		const int                              pot = a_hand.PotTotal();
		const bool                             preflop = a_hand.street == Street::kPreflop;
		const double                           pos = Position(a_hand, a_seat);
		auto raiseTo = [&](double a_potShare) {
			const int bet = static_cast<int>(std::round(std::max<double>(a_hand.bigBlind, (pot + toCall) * a_potShare)));
			const int to = p.bet + toCall + bet;
			Action    a{ Action::Kind::kRaise, std::clamp(to, a_hand.MinRaiseTo(a_seat), a_hand.MaxRaiseTo(a_seat)) };
			if (a.to >= a_hand.MaxRaiseTo(a_seat)) {
				a.kind = Action::Kind::kAllIn;
			}
			return a;
		};
		const Action check{ toCall == 0 ? Action::Kind::kCheck : Action::Kind::kFold };

		if (preflop) {
			// before the flop: the starting hand (Chen), where they sit and what's happened so far
			const double chen = ChenScore(p.hole[0], p.hole[1]);
			const double loose = st.looseness * 3.0 + pos * 2.5;  // looser players and the late seats play more hands
			const bool   unraised = a_hand.raises == 0;
			const bool   foldedTo = unraised && a_hand.Live() == static_cast<int>(a_hand.players.size()) &&
			                      toCall <= a_hand.bigBlind && pos >= 0.6;  // (nobody's come in: a chance to steal the blinds)
			if (a_equity) {
				*a_equity = chen / 20.0;
			}
			if (chen >= 12 - st.aggression * 2 || (unraised && chen >= 9.5 - loose * 0.5)) {
				// a strong hand, or a good one nobody's raised yet: raise (three times the big blind, more over a raise)
				if (a_hand.raises >= 2 && chen < 14) {
					return toCall <= p.stack / 3 ? Action{ Action::Kind::kCall } : check;
				}
				const double size = unraised ? 2.0 + u(a_rng) : 2.2 + u(a_rng) * 0.8;
				return raiseTo(size * a_hand.bigBlind / std::max(1, pot + toCall) * (unraised ? 1.0 : 1.4));
			}
			if (foldedTo && chen >= 7 - loose * 0.6 && u(a_rng) < 0.35 + 0.5 * st.aggression) {
				return raiseTo(2.5 * a_hand.bigBlind / std::max(1, pot + toCall));  // a steal: raise to take the blinds
			}
			// calling: cheap and a playable hand (more of them for loose players, in late seats, and in the big blind)
			const double price = static_cast<double>(toCall) / std::max(1, a_hand.bigBlind);
			const double need = 6.5 - loose * 0.7 + std::min(price, 8.0) * 0.6 + (a_hand.raises >= 1 ? 1.5 : 0.0);
			if (toCall == 0 || chen >= need) {
				return toCall == 0 ? Action{ Action::Kind::kCheck } : Action{ Action::Kind::kCall };
			}
			return check;
		}

		// after the flop: how often the hand wins against the players still in, the price, who has the betting lead
		const int    trials = 350;
		const double equity = Equity(p.hole, a_hand.board, opponents, trials, a_rng);
		if (a_equity) {
			*a_equity = equity;
		}
		const double rel = equity * (opponents + 1);  // 1 = an average hand against this many
		const double odds = toCall > 0 ? static_cast<double>(toCall) / (pot + toCall) : 0.0;
		const double strong = 1.45 - 0.35 * st.aggression;
		const bool draw = OnADraw(p.hole, a_hand.board);
		const bool monster = rel > strong + 0.8 && a_hand.street != Street::kRiver;
		if (toCall == 0) {
			if (monster && u(a_rng) < st.trapping) {
				return { Action::Kind::kCheck };  // the slow-play: let them bet into it
			}
			if (draw && u(a_rng) < st.drawing * (0.5 + 0.5 * st.aggression)) {
				return raiseTo(0.45 + 0.25 * u(a_rng));  // the semi-bluff: a bet that can win now, or hit later
			}
			if (rel > strong && u(a_rng) < 0.6 + 0.35 * st.aggression) {
				// value: bigger the stronger (a third of the pot up to the whole of it)
				return raiseTo(std::clamp(0.33 + (rel - strong) * 0.5 + 0.2 * u(a_rng), 0.33, 1.0));
			}
			// the continuation bet: they raised before the flop, so they keep betting (now and then, into few players)
			if (a_hand.street == Street::kFlop && a_hand.preflopRaiser == a_seat && opponents <= 2 && u(a_rng) < 0.45 + 0.35 * st.aggression) {
				return raiseTo(0.4 + 0.2 * u(a_rng));
			}
			if (u(a_rng) < (st.bluffing + 0.08 * st.aggression) * (opponents <= 1 ? 2.0 : 0.8) && pos > 0.5) {
				return raiseTo(0.4 + 0.25 * u(a_rng));  // a stab at it (a bluff, or a weak made hand), from a late seat
			}
			return { Action::Kind::kCheck };
		}
		// facing a bet: they believe big bets more (a bigger bet means a stronger hand), tight players most
		const double betShare = static_cast<double>(toCall) / std::max(1, pot);
		const double doubt = 1.0 - std::min(0.5, (0.15 + betShare * 0.35) * (1.2 - st.looseness));
		const double eq = equity * (doubt + (1.0 - doubt) * st.stubborn);  // (a calling station doesn't believe you)
		if (monster && u(a_rng) < st.trapping) {
			return { Action::Kind::kCall };  // the slow-play: just call, raise on a later street
		}
		if (rel * doubt > strong + 0.4 && u(a_rng) < 0.35 + 0.45 * st.aggression * (1.0 - st.stubborn)) {
			return raiseTo(0.6 + 0.4 * u(a_rng));  // a raise for value
		}
		if (draw && toCall * 2 < pot && u(a_rng) < st.drawing * st.aggression * 0.5) {
			return raiseTo(0.7);  // the semi-bluff raise
		}
		const double need = odds * (1.25 - 0.35 * st.looseness) + 0.1 * (1.2 - st.looseness);  // (a bet usually means something)
		if (eq >= need) {
			return { Action::Kind::kCall };
		}
		if (u(a_rng) < st.bluffing * 0.4 && toCall * 3 < pot && opponents <= 1) {
			return raiseTo(0.8);  // a bluff raise
		}
		return check;
	}

	double Confidence(const Hand& a_hand, int a_seat, const Action& a_action, double a_equity)
	{
		// an easy fold or a strong hand: quick; a call or a fold that was close: slow
		const int    toCall = a_hand.ToCall(a_seat);
		const int    pot = a_hand.PotTotal();
		const double odds = toCall > 0 ? static_cast<double>(toCall) / (pot + toCall) : 0.0;
		const double margin = std::abs(a_equity - odds);
		switch (a_action.kind) {
		case Action::Kind::kCheck: return toCall == 0 ? 0.7 : 0.5;
		case Action::Kind::kFold: return std::clamp(margin * 3.0, 0.2, 1.0);
		case Action::Kind::kCall: return std::clamp(margin * 2.0, 0.0, 0.6);
		default: return std::clamp(0.4 + a_equity, 0.3, 1.0);
		}
	}
}
