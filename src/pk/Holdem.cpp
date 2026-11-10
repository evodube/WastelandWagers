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

	Style Style::From(std::uint32_t a_seed)
	{
		std::mt19937                           rng(a_seed * 2654435761u + 17u);
		std::uniform_real_distribution<double> u(0.0, 1.0);
		Style                                  s;
		s.looseness = 0.2 + 0.6 * u(rng);
		s.aggression = 0.2 + 0.6 * u(rng);
		s.bluffing = 0.03 + 0.15 * u(rng);
		return s;
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

	Action Decide(const Hand& a_hand, int a_seat, std::mt19937& a_rng, double* a_equity)
	{
		const auto&                            p = a_hand.players[a_seat];
		const auto&                            st = p.style;
		std::uniform_real_distribution<double> u(0.0, 1.0);
		const int                              opponents = std::max(1, a_hand.Live() - 1);
		const int                              trials = a_hand.board.empty() ? 250 : 350;
		const double                           equity = Equity(p.hole, a_hand.board, opponents, trials, a_rng);
		if (a_equity) {
			*a_equity = equity;
		}
		const int    toCall = a_hand.ToCall(a_seat);
		const int    pot = a_hand.PotTotal();
		const double rel = equity * (opponents + 1);  // 1 = an average hand against this many
		const double odds = toCall > 0 ? static_cast<double>(toCall) / (pot + toCall) : 0.0;
		const bool   preflop = a_hand.street == Street::kPreflop;
		auto raiseTo = [&](double a_potShare) {
			const int bet = static_cast<int>(std::round(std::max<double>(a_hand.bigBlind, (pot + toCall) * a_potShare)));
			const int to = p.bet + toCall + bet;
			Action    a{ Action::Kind::kRaise, std::clamp(to, a_hand.MinRaiseTo(a_seat), a_hand.MaxRaiseTo(a_seat)) };
			if (a.to >= a_hand.MaxRaiseTo(a_seat)) {
				a.kind = Action::Kind::kAllIn;
			}
			return a;
		};
		const double strong = 1.75 - 0.45 * st.aggression - (preflop ? 0.0 : 0.1);
		if (toCall == 0) {
			if (rel > strong && u(a_rng) < 0.55 + 0.4 * st.aggression) {
				return raiseTo(0.45 + 0.45 * u(a_rng));  // value
			}
			if (!preflop && u(a_rng) < st.bluffing && opponents <= 2) {
				return raiseTo(0.4 + 0.3 * u(a_rng));  // a bluff
			}
			return { Action::Kind::kCheck };
		}
		if (rel > strong + 0.35 && u(a_rng) < 0.4 + 0.45 * st.aggression) {
			return raiseTo(0.6 + 0.5 * u(a_rng));
		}
		// calling: the hand's chances against the price, looser players taking worse prices
		const double need = odds * (1.15 - 0.45 * st.looseness);
		if (equity >= need) {
			return { Action::Kind::kCall };
		}
		if (!preflop && u(a_rng) < st.bluffing * 0.5 && toCall * 3 < pot && opponents <= 2) {
			return raiseTo(0.7);  // a bluff raise
		}
		return { toCall == 0 ? Action::Kind::kCheck : Action::Kind::kFold };  // (a settler never folds when checking is free)
	}
}
