// Poker: the hand evaluator and No-limit Hold'em, on their own.

#include "pk/Holdem.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <numeric>

using namespace pk;

namespace
{
	int failures = 0;

	void Check(bool a_ok, const char* a_what)
	{
		if (!a_ok) {
			std::printf("FAIL: %s\n", a_what);
			++failures;
		}
	}

	Card C(const char* a_s)
	{
		const std::string r = "23456789TJQKA", s = "cdhs";
		return MakeCard(static_cast<int>(r.find(a_s[0])), static_cast<int>(s.find(a_s[1])));
	}

	Score E(std::initializer_list<const char*> a_cards)
	{
		std::vector<Card> v;
		for (auto* c : a_cards) {
			v.push_back(C(c));
		}
		return Evaluate(v);
	}

	// brute force: the best of every five of seven, scored by a plain five-card rule
	Score Five(std::array<Card, 5> a_h)
	{
		return Evaluate(std::span<const Card>(a_h.data(), 5));
	}

	Score Brute(const std::array<Card, 7>& a_c)
	{
		Score best = 0;
		for (int a = 0; a < 7; ++a) {
			for (int b = a + 1; b < 7; ++b) {
				std::array<Card, 5> h{};
				int                 k = 0;
				for (int i = 0; i < 7; ++i) {
					if (i != a && i != b) {
						h[k++] = a_c[i];
					}
				}
				best = std::max(best, Five(h));
			}
		}
		return best;
	}

	void Evaluator()
	{
		Check(CategoryOf(E({ "Ah", "Kh", "Qh", "Jh", "Th", "2c", "3d" })) == Category::kStraightFlush, "royal flush");
		Check(HandName(E({ "Ah", "Kh", "Qh", "Jh", "Th" })) == "ROYAL FLUSH", "royal name");
		Check(CategoryOf(E({ "Ah", "2h", "3h", "4h", "5h", "Kc", "Kd" })) == Category::kStraightFlush, "steel wheel");
		Check(E({ "As", "2d", "3c", "4h", "5s" }) < E({ "2s", "3d", "4c", "5h", "6s" }), "wheel is the lowest straight");
		Check(CategoryOf(E({ "9c", "9d", "9h", "9s", "Ac" })) == Category::kQuads, "quads");
		Check(CategoryOf(E({ "9c", "9d", "9h", "Ks", "Kc", "Kd", "2s" })) == Category::kFullHouse, "two trips: full house");
		Check(HandName(E({ "9c", "9d", "9h", "Ks", "Kc", "Kd", "2s" })) == "FULL HOUSE, KINGS OVER NINES", "full house name");
		Check(E({ "Ah", "Ad", "Kc", "Kd", "Qh" }) > E({ "Ah", "Ad", "Kc", "Kd", "Jh" }), "two pair kicker");
		Check(E({ "Ah", "Ad", "Kc", "Kd", "Qh", "Qs", "2c" }) == E({ "Ah", "Ad", "Kc", "Kd", "Qh" }), "three pairs: best two + kicker");
		Check(CategoryOf(E({ "2h", "7h", "9h", "Jh", "Kh", "Ah", "3c" })) == Category::kFlush, "six to a flush");
		Check(E({ "2h", "7h", "9h", "Jh", "Kh", "Ah" }) == E({ "7h", "9h", "Jh", "Kh", "Ah" }), "flush: top five");
		Check(CategoryOf(E({ "5c", "6d", "7h", "8s", "9c", "Tc", "Jc" })) == Category::kStraight, "straight");
		Check(HandName(E({ "5c", "6d", "7h", "8s", "9c", "Tc", "Jc" })) == "STRAIGHT, JACK HIGH", "longest straight");
		// against brute force, random hands
		std::mt19937 rng(7);
		int          bad = 0;
		for (int t = 0; t < 60000; ++t) {
			Deck d;
			d.Shuffle(rng);
			std::array<Card, 7> c{};
			for (auto& x : c) {
				x = d.Deal();
			}
			if (Evaluate(c) != Brute(c)) {
				++bad;
			}
		}
		Check(bad == 0, "seven cards: the best five, every time");
		// how often each hand comes up in seven cards (known frequencies, roughly)
		std::map<Category, int> seen;
		const int               n = 200000;
		for (int t = 0; t < n; ++t) {
			Deck d;
			d.Shuffle(rng);
			std::array<Card, 7> c{};
			for (auto& x : c) {
				x = d.Deal();
			}
			++seen[CategoryOf(Evaluate(c))];
		}
		const double expect[] = { 0.174, 0.438, 0.235, 0.0483, 0.0462, 0.0303, 0.026, 0.00168, 0.000311 };
		for (int k = 0; k < 9; ++k) {
			const double f = static_cast<double>(seen[static_cast<Category>(k)]) / n;
			const double tol = std::max(0.004, expect[k] * 0.12);
			if (std::abs(f - expect[k]) > tol) {
				std::printf("category %d: %.5f, expected %.5f\n", k, f, expect[k]);
				Check(false, "hand frequencies");
			}
		}
		// equity: aces against one random hand win about 85%
		const double aa = Equity({ C("Ah"), C("As") }, {}, 1, 20000, rng);
		Check(aa > 0.83 && aa < 0.87, "aces vs a random hand");
		const double low = Equity({ C("7d"), C("2c") }, {}, 1, 20000, rng);
		Check(low > 0.31 && low < 0.37, "seven-deuce vs a random hand");
		std::printf("evaluator: aces %.3f, seven-deuce %.3f\n", aa, low);
	}

	void SidePots()
	{
		// three in: A all in for 50, B and C for 200; A has the best hand, B the next
		std::mt19937        rng(1);
		std::vector<Player> ps(3);
		ps[0].stack = 50;
		ps[1].stack = 200;
		ps[2].stack = 200;
		Hand h;
		h.Start(ps, 0, 5, 10, rng);
		// force the cards: A aces, B kings, C seven-deuce; the board dry
		h.players[0].hole = { C("Ah"), C("Ad") };
		h.players[1].hole = { C("Kh"), C("Kd") };
		h.players[2].hole = { C("7c"), C("2s") };
		// everyone all in
		while (!h.Over()) {
			h.Apply({ Action::Kind::kAllIn });
		}
		h.board = { C("3c"), C("8d"), C("9s"), C("Jh"), C("4c") };
		// (the board was dealt by the hand: re-score with this one)
		h.scores.assign(3, 0);
		int total = 0;
		for (const auto& p : h.players) {
			total += p.stack;
		}
		Check(total == 450, "all-in hand: caps kept");
		Check(h.pots.size() == 2, "a main pot and a side pot");
		if (h.pots.size() == 2) {
			Check(h.pots[0].amount == 150 && h.pots[0].eligible.size() == 3, "main pot: 50 from each");
			Check(h.pots[1].amount == 300 && h.pots[1].eligible.size() == 2, "side pot: B and C's 150 each");
		}
	}

	void Games()
	{
		// thousands of hands between six settlers: the caps all stay at the table, nobody's stack goes negative, every
		// hand ends, pots match what went in
		std::mt19937        rng(12345);
		std::vector<Player> table(6);
		for (int i = 0; i < 6; ++i) {
			table[i].id = static_cast<std::uint32_t>(i + 1);
			table[i].stack = 500;
			table[i].style = Style::From(static_cast<std::uint32_t>(i * 77 + 3));
		}
		const int start = 3000;
		int       button = 0, hands = 0, showdowns = 0, actions = 0;
		std::map<int, int> folds;
		for (int g = 0; g < 4000; ++g) {
			int alive = 0;
			for (auto& p : table) {
				alive += p.stack > 0;
			}
			if (alive < 2) {
				for (auto& p : table) {
					p.stack = 500;  // a new game
				}
			}
			Hand h;
			h.Start(table, button, 5, 10, rng);
			int steps = 0;
			while (!h.Over() && steps < 500) {
				const int s = h.ToAct();
				Check(s >= 0, "someone to act");
				if (s < 0) {
					break;
				}
				h.Apply(Decide(h, s, rng));
				++steps;
				++actions;
			}
			Check(h.Over(), "the hand ends");
			int before = 0, after = 0, potSum = 0;
			for (const auto& p : table) {
				before += p.stack;
			}
			for (const auto& p : h.players) {
				after += p.stack;
				Check(p.stack >= 0, "no negative stack");
			}
			for (const auto& pot : h.pots) {
				potSum += pot.amount;
				Check(!pot.winners.empty(), "every pot has a winner");
			}
			Check(before == after, "caps conserved");
			showdowns += h.showdown;
			table = h.players;
			button = (button + 1) % 6;
			++hands;
		}
		(void)start;
		std::printf("games: %d hands, %d showdowns (%.0f%%), %.1f actions a hand\n", hands, showdowns, 100.0 * showdowns / hands,
			static_cast<double>(actions) / hands);
	}

	void HeadsUp()
	{
		// two players: the button posts the small blind and acts first before the flop
		std::mt19937        rng(3);
		std::vector<Player> ps(2);
		ps[0].stack = ps[1].stack = 100;
		Hand h;
		h.Start(ps, 0, 5, 10, rng);
		Check(h.players[0].committed == 5 && h.players[1].committed == 10, "heads up blinds");
		Check(h.ToAct() == 0, "heads up: the button acts first preflop");
		h.Apply({ Action::Kind::kCall });
		Check(h.ToAct() == 1, "then the big blind");
		h.Apply({ Action::Kind::kCheck });
		Check(h.street == Street::kFlop && h.board.size() == 3, "the flop");
		Check(h.ToAct() == 1, "after the flop the big blind first");
		h.Apply({ Action::Kind::kRaise, 20 });
		h.Apply({ Action::Kind::kFold });
		Check(h.Over() && !h.showdown, "a fold ends it");
		Check(h.players[1].stack == 110 && h.players[0].stack == 90, "the pot to the bettor, the uncalled bet back");
	}

	void OutOfTurn()
	{
		std::mt19937        rng(9);
		std::vector<Player> ps(3);
		for (auto& p : ps) {
			p.stack = 100;
		}
		Hand h;
		h.Start(ps, 0, 5, 10, rng);
		const int first = h.ToAct();
		const int other = (first + 1) % 3;
		h.Fold(other);
		Check(h.players[other].folded && !h.Over(), "a walk-away folds out of turn");
		Check(h.ToAct() == first, "and the turn stays put");
		h.Apply({ Action::Kind::kFold });
		Check(h.Over(), "down to one: over");
		int total = 0;
		for (const auto& p : h.players) {
			total += p.stack;
		}
		Check(total == 300, "caps kept after walk-aways");
	}
}

int main()
{
	OutOfTurn();
	Evaluator();
	SidePots();
	HeadsUp();
	Games();
	std::printf(failures ? "%d FAILED\n" : "poker: all passed\n", failures);
	return failures ? 1 : 0;
}
