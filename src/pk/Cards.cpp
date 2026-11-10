#include "pk/Cards.h"

#include <algorithm>
#include <bit>

namespace pk
{
	namespace
	{
		constexpr const char* kRankChar = "23456789TJQKA";
		constexpr const char* kSuitChar = "cdhs";
		constexpr std::array<const char*, 13> kRankName{ "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE", "TEN",
			"JACK", "QUEEN", "KING", "ACE" };
		constexpr std::array<const char*, 13> kRankPlural{ "TWOS", "THREES", "FOURS", "FIVES", "SIXES", "SEVENS", "EIGHTS", "NINES",
			"TENS", "JACKS", "QUEENS", "KINGS", "ACES" };
		constexpr std::array<const char*, 4> kSuitName{ "CLUBS", "DIAMONDS", "HEARTS", "SPADES" };

		Score Make(Category a_cat, std::initializer_list<int> a_ranks)
		{
			Score s = static_cast<Score>(a_cat) << 20;
			int   shift = 16;
			for (int r : a_ranks) {
				s |= static_cast<Score>(r & 15) << shift;
				shift -= 4;
			}
			return s;
		}

		// the top card of the highest straight in a 13-bit rank mask, or -1 (the wheel, A-2-3-4-5, is five high)
		int StraightTop(std::uint32_t a_mask)
		{
			const std::uint32_t m = a_mask | ((a_mask >> 12) & 1 ? 0 : 0);
			for (int top = 12; top >= 4; --top) {
				const std::uint32_t run = 0x1Fu << (top - 4);
				if ((m & run) == run) {
					return top;
				}
			}
			if ((a_mask & 0x100Fu) == 0x100Fu) {  // A, 2, 3, 4, 5
				return 3;
			}
			return -1;
		}

		// the a_n highest ranks in a mask, high first
		std::array<int, 5> TopRanks(std::uint32_t a_mask, int a_n)
		{
			std::array<int, 5> out{ 0, 0, 0, 0, 0 };
			int                k = 0;
			for (int r = 12; r >= 0 && k < a_n; --r) {
				if (a_mask & (1u << r)) {
					out[k++] = r;
				}
			}
			return out;
		}
	}

	std::string CardName(Card a_c)
	{
		return { kRankChar[Rank(a_c)], kSuitChar[Suit(a_c)] };
	}

	std::string CardLong(Card a_c)
	{
		return std::string(kRankName[Rank(a_c)]) + " OF " + kSuitName[Suit(a_c)];
	}

	Score Evaluate(std::span<const Card> a_cards)
	{
		std::array<std::uint32_t, 4> suitMask{};
		std::array<int, 13>          count{};
		std::uint32_t                all = 0;
		for (Card c : a_cards) {
			suitMask[Suit(c)] |= 1u << Rank(c);
			++count[Rank(c)];
			all |= 1u << Rank(c);
		}
		// flushes (and straight flushes)
		for (int s = 0; s < 4; ++s) {
			if (std::popcount(suitMask[s]) >= 5) {
				if (const int top = StraightTop(suitMask[s]); top >= 0) {
					return Make(Category::kStraightFlush, { top });
				}
				const auto t = TopRanks(suitMask[s], 5);
				return Make(Category::kFlush, { t[0], t[1], t[2], t[3], t[4] });
			}
		}
		int quad = -1, trips[2] = { -1, -1 }, pairs[3] = { -1, -1, -1 };
		int nt = 0, np = 0;
		for (int r = 12; r >= 0; --r) {
			if (count[r] == 4 && quad < 0) {
				quad = r;
			} else if (count[r] == 3 && nt < 2) {
				trips[nt++] = r;
			} else if (count[r] == 2 && np < 3) {
				pairs[np++] = r;
			}
		}
		if (quad >= 0) {
			const auto k = TopRanks(all & ~(1u << quad), 1);
			return Make(Category::kQuads, { quad, k[0] });
		}
		if (nt >= 1 && (nt >= 2 || np >= 1)) {
			const int pair = nt >= 2 ? std::max(trips[1], np ? pairs[0] : -1) : pairs[0];
			return Make(Category::kFullHouse, { trips[0], pair });
		}
		if (const int top = StraightTop(all); top >= 0) {
			return Make(Category::kStraight, { top });
		}
		if (nt == 1) {
			const auto k = TopRanks(all & ~(1u << trips[0]), 2);
			return Make(Category::kTrips, { trips[0], k[0], k[1] });
		}
		if (np >= 2) {
			const auto k = TopRanks(all & ~(1u << pairs[0]) & ~(1u << pairs[1]), 1);
			return Make(Category::kTwoPair, { pairs[0], pairs[1], k[0] });
		}
		if (np == 1) {
			const auto k = TopRanks(all & ~(1u << pairs[0]), 3);
			return Make(Category::kPair, { pairs[0], k[0], k[1], k[2] });
		}
		const auto k = TopRanks(all, 5);
		return Make(Category::kHighCard, { k[0], k[1], k[2], k[3], k[4] });
	}

	Category CategoryOf(Score a_s)
	{
		return static_cast<Category>(a_s >> 20);
	}

	std::string HandName(Score a_s)
	{
		auto r = [&](int a_i) { return static_cast<int>((a_s >> (16 - 4 * a_i)) & 15); };
		switch (CategoryOf(a_s)) {
		case Category::kStraightFlush:
			return r(0) == 12 ? std::string("ROYAL FLUSH") : std::string("STRAIGHT FLUSH, ") + kRankName[r(0)] + " HIGH";
		case Category::kQuads: return std::string("FOUR ") + kRankPlural[r(0)];
		case Category::kFullHouse: return std::string("FULL HOUSE, ") + kRankPlural[r(0)] + " OVER " + kRankPlural[r(1)];
		case Category::kFlush: return std::string("FLUSH, ") + kRankName[r(0)] + " HIGH";
		case Category::kStraight: return std::string("STRAIGHT, ") + kRankName[r(0)] + " HIGH";
		case Category::kTrips: return std::string("THREE ") + kRankPlural[r(0)];
		case Category::kTwoPair: return std::string("TWO PAIR, ") + kRankPlural[r(0)] + " AND " + kRankPlural[r(1)];
		case Category::kPair: return std::string("PAIR OF ") + kRankPlural[r(0)];
		default: return std::string(kRankName[r(0)]) + " HIGH";
		}
	}

	void Deck::Shuffle(std::mt19937& a_rng)
	{
		for (int i = 0; i < kDeck; ++i) {
			_cards[i] = static_cast<Card>(i);
		}
		std::shuffle(_cards.begin(), _cards.end(), a_rng);
		_next = 0;
	}

	void Deck::Remove(Card a_c)
	{
		// move it to the dealt end (before _next) so it's never dealt
		for (int i = _next; i < kDeck; ++i) {
			if (_cards[i] == a_c) {
				std::swap(_cards[i], _cards[_next]);
				++_next;
				return;
			}
		}
	}

	double Equity(std::array<Card, 2> a_hole, std::span<const Card> a_board, int a_opponents, int a_trials, std::mt19937& a_rng)
	{
		if (a_opponents <= 0) {
			return 1.0;
		}
		double won = 0.0;
		for (int t = 0; t < a_trials; ++t) {
			Deck d;
			d.Shuffle(a_rng);
			d.Remove(a_hole[0]);
			d.Remove(a_hole[1]);
			for (Card c : a_board) {
				d.Remove(c);
			}
			std::array<Card, 7> mine{};
			std::size_t         n = 0;
			mine[n++] = a_hole[0];
			mine[n++] = a_hole[1];
			for (Card c : a_board) {
				mine[n++] = c;
			}
			std::array<Card, 5> board{};
			std::size_t         nb = 0;
			for (Card c : a_board) {
				board[nb++] = c;
			}
			while (nb < 5) {
				const Card c = d.Deal();
				board[nb++] = c;
				mine[n++] = c;
			}
			const Score me = Evaluate(std::span<const Card>(mine.data(), 7));
			int         ties = 0;
			bool        lost = false;
			for (int o = 0; o < a_opponents && !lost; ++o) {
				std::array<Card, 7> theirs{ d.Deal(), d.Deal(), board[0], board[1], board[2], board[3], board[4] };
				const Score         them = Evaluate(theirs);
				if (them > me) {
					lost = true;
				} else if (them == me) {
					++ties;
				}
			}
			if (!lost) {
				won += 1.0 / (1 + ties);
			}
		}
		return won / a_trials;
	}
}
