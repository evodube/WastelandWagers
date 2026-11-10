// The blackjack rules (src/bj): counts, blackjacks, returns, the book, and the house edge played by the book over many
// hands (it should be small: about half a percent)
#include "bj/Blackjack.h"

#include <cassert>
#include <cstdio>

using namespace bj;

namespace
{
	Card C(int a_rank, int a_suit = 0) { return a_suit * 13 + a_rank; }  // rank 0 two .. 8 ten .. 12 ace
	constexpr int A = 12, K = 11, T = 8;

	double Play(std::mt19937& a_rng, Shoe& a_shoe, int& a_hands)
	{
		if (a_shoe.PastCut()) {
			a_shoe.Shuffle(a_rng);
		}
		std::vector<Card> dealer{ a_shoe.Draw(a_rng), a_shoe.Draw(a_rng) };
		std::vector<std::vector<Card>> hands{ { a_shoe.Draw(a_rng), a_shoe.Draw(a_rng) } };
		std::vector<double>            bets{ 1.0 };
		std::vector<bool>              split{ false };
		double                         net = 0.0;
		++a_hands;
		if (IsBlackjack(dealer)) {
			return Return(hands[0], false, dealer) - 1.0;
		}
		for (std::size_t h = 0; h < hands.size(); ++h) {
			while (true) {
				auto&      hand = hands[h];
				const bool first = hand.size() == 2;
				const bool splitAces = split[h] && Rank(hand[0]) == A;
				if (splitAces || Count(hand).value >= 21) {
					break;
				}
				const auto m = Basic(hand, dealer[0], first, first && hands.size() == 1);
				if (m == Move::kStand) {
					break;
				}
				if (m == Move::kSplit) {
					const Card second = hand[1];
					hand.pop_back();
					hands.push_back({ second });  // (hand's no good after this: the vector may move)
					bets.push_back(bets[h]);
					split[h] = true;
					split.push_back(true);
					hands[h].push_back(a_shoe.Draw(a_rng));
					hands.back().push_back(a_shoe.Draw(a_rng));
					continue;
				}
				hand.push_back(a_shoe.Draw(a_rng));
				if (m == Move::kDouble) {
					bets[h] *= 2;
					break;
				}
			}
		}
		while (Count(dealer).value < 17) {
			dealer.push_back(a_shoe.Draw(a_rng));
		}
		for (std::size_t h = 0; h < hands.size(); ++h) {
			net += bets[h] * (Return(hands[h], split[h], dealer) - 1.0);
		}
		return net;
	}
}

int main()
{
	assert(Count({ C(A), C(K) }).value == 21 && Count({ C(A), C(K) }).soft);
	assert(Count({ C(A), C(A), C(T) }).value == 12 && !Count({ C(A), C(A), C(T) }).soft);
	assert(Count({ C(A), C(5) }).value == 18 && Count({ C(A), C(5) }).soft);
	assert(IsBlackjack({ C(A), C(T) }) && !IsBlackjack({ C(A), C(T) }, true) && !IsBlackjack({ C(5), C(4), C(A) }));
	assert(IsPair({ C(T), C(K) }) && !IsPair({ C(T), C(A) }));
	assert(Return({ C(A), C(K) }, false, { C(T), C(T) }) == 2.5);
	assert(Return({ C(A), C(K) }, false, { C(A), C(T) }) == 1.0);
	assert(Return({ C(T), C(T) }, false, { C(A), C(T) }) == 0.0);
	assert(Return({ C(T), C(T), C(T) }, false, { C(T), C(4), C(T) }) == 0.0);  // (busted first: lost)
	assert(Return({ C(T), C(7) }, false, { C(T), C(4), C(T) }) == 2.0);
	assert(Basic({ C(A), C(A) }, C(T), true, true) == Move::kSplit);
	assert(Basic({ C(7), C(0) }, C(5), true, true) == Move::kDouble);  // 9 + 2: 11 vs a 7, double
	assert(Basic({ C(T), C(4) }, C(T), true, true) == Move::kHit);     // 16 vs a ten: hit
	assert(Basic({ C(T), C(4) }, C(4), true, true) == Move::kStand);   // 16 vs a six: stand
	assert(Basic({ C(T), C(T) }, C(4), true, true) == Move::kStand);   // tens: never split
	std::mt19937 rng(7);
	Shoe         shoe;
	shoe.Shuffle(rng);
	int    hands = 0;
	double net = 0.0;
	for (int i = 0; i < 400000; ++i) {
		net += Play(rng, shoe, hands);
	}
	const double edge = -net / hands * 100.0;
	std::printf("blackjack: %d hands by the book, the house's edge %.2f%%\n", hands, edge);
	assert(edge > 0.0 && edge < 1.2);  // (about half a percent, played by the book)
	std::printf("blackjack tests passed\n");
	return 0;
}
