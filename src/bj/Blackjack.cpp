#include "bj/Blackjack.h"

#include <algorithm>

namespace bj
{
	Total Count(const std::vector<Card>& a_cards)
	{
		int  sum = 0;
		int  aces = 0;
		for (Card c : a_cards) {
			sum += Points(c);
			aces += Rank(c) == 12;
		}
		while (sum > 21 && aces > 0) {
			sum -= 10;
			--aces;
		}
		return { sum, aces > 0 };
	}

	bool IsBlackjack(const std::vector<Card>& a_cards, bool a_split)
	{
		return !a_split && a_cards.size() == 2 && Count(a_cards).value == 21;
	}

	bool IsPair(const std::vector<Card>& a_cards)
	{
		return a_cards.size() == 2 && Points(a_cards[0]) == Points(a_cards[1]);
	}

	void Shoe::Shuffle(std::mt19937& a_rng)
	{
		cards.clear();
		for (int d = 0; d < kDecks; ++d) {
			for (Card c = 0; c < 52; ++c) {
				cards.push_back(c);
			}
		}
		std::shuffle(cards.begin(), cards.end(), a_rng);
		next = 0;
		// the cut card: about three quarters in, give or take
		cut = cards.size() * 3 / 4 + std::uniform_int_distribution<std::size_t>(0, 20)(a_rng) - 10;
	}

	Card Shoe::Draw(std::mt19937& a_rng)
	{
		if (next >= cards.size()) {
			Shuffle(a_rng);
		}
		return cards[next++];
	}

	const char* MoveName(Move a_m)
	{
		switch (a_m) {
		case Move::kHit: return "HITS";
		case Move::kStand: return "STANDS";
		case Move::kDouble: return "DOUBLES DOWN";
		case Move::kSplit: return "SPLITS";
		}
		return "";
	}

	Move Basic(const std::vector<Card>& a_hand, Card a_dealerUp, bool a_canDouble, bool a_canSplit)
	{
		const int  up = Points(a_dealerUp);  // 2..11
		const auto t = Count(a_hand);
		auto       dbl = [&](Move a_else) { return a_canDouble ? Move::kDouble : a_else; };
		if (a_canSplit && IsPair(a_hand)) {
			switch (Points(a_hand[0])) {
			case 11: return Move::kSplit;
			case 8: return Move::kSplit;
			case 9:
				if (up != 7 && up != 10 && up != 11) {
					return Move::kSplit;
				}
				break;
			case 7:
				if (up <= 7) {
					return Move::kSplit;
				}
				break;
			case 6:
				if (up <= 6) {
					return Move::kSplit;
				}
				break;
			case 4:
				if (up == 5 || up == 6) {
					return Move::kSplit;
				}
				break;
			case 3:
			case 2:
				if (up <= 7) {
					return Move::kSplit;
				}
				break;
			default:
				break;  // (fives and tens: never)
			}
		}
		if (t.soft && t.value <= 21) {
			switch (t.value) {
			case 21:
			case 20:
			case 19: return t.value == 19 && up == 6 ? dbl(Move::kStand) : Move::kStand;
			case 18:
				if (up >= 2 && up <= 6) {
					return dbl(Move::kStand);
				}
				return up <= 8 ? Move::kStand : Move::kHit;
			case 17: return up >= 3 && up <= 6 ? dbl(Move::kHit) : Move::kHit;
			case 16:
			case 15: return up >= 4 && up <= 6 ? dbl(Move::kHit) : Move::kHit;
			case 14:
			case 13: return up >= 5 && up <= 6 ? dbl(Move::kHit) : Move::kHit;
			default: return Move::kHit;
			}
		}
		const int v = t.value;
		if (v >= 17) {
			return Move::kStand;
		}
		if (v >= 13) {
			return up <= 6 ? Move::kStand : Move::kHit;
		}
		if (v == 12) {
			return up >= 4 && up <= 6 ? Move::kStand : Move::kHit;
		}
		if (v == 11) {
			return up <= 10 ? dbl(Move::kHit) : Move::kHit;
		}
		if (v == 10) {
			return up <= 9 ? dbl(Move::kHit) : Move::kHit;
		}
		if (v == 9) {
			return up >= 3 && up <= 6 ? dbl(Move::kHit) : Move::kHit;
		}
		return Move::kHit;
	}

	double Return(const std::vector<Card>& a_hand, bool a_split, const std::vector<Card>& a_dealer)
	{
		const int  me = Count(a_hand).value;
		const bool myBj = IsBlackjack(a_hand, a_split);
		const bool dealerBj = IsBlackjack(a_dealer);
		if (me > 21) {
			return 0.0;  // (busted: lost, whatever the dealer does)
		}
		if (myBj) {
			return dealerBj ? 1.0 : 2.5;
		}
		if (dealerBj) {
			return 0.0;
		}
		const int dealer = Count(a_dealer).value;
		if (dealer > 21 || me > dealer) {
			return 2.0;
		}
		return me == dealer ? 1.0 : 0.0;
	}
}
