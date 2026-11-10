#pragma once

// Blackjack's rules, on their own (no game in here): the plugin's Blackjack.cpp plays them at the table.
//
// The house rules (as the felt says): six decks dealt from a shoe, reshuffled once the cut card (three quarters in)
// comes out; the dealer draws to 16 and stands on every 17, soft ones too; a blackjack pays 3 to 2; insurance (half
// the bet, when the dealer shows an ace) pays 2 to 1; the dealer checks under an ace or a ten for blackjack before
// anyone plays (a dealer's blackjack takes every bet but a player's blackjack, which pushes); double down on any first
// two cards (once, one more card), after a split too; split a pair once (two hands; split aces get one card each,
// and 21 on a split hand is 21, not a blackjack); no surrender.

#include <array>
#include <cstdint>
#include <random>
#include <vector>

namespace bj
{
	using Card = int;  // 0..51: suit * 13 + rank, rank 0 the two .. 12 the ace (the poker cards' numbering)

	inline constexpr int Rank(Card a_c) { return a_c % 13; }
	// a card's count: 2..9, the tens and faces 10, the ace 11 (counted as 1 when 11 would bust)
	inline constexpr int Points(Card a_c)
	{
		const int r = Rank(a_c);
		return r == 12 ? 11 : r >= 8 ? 10 : r + 2;
	}

	struct Total
	{
		int  value{ 0 };
		bool soft{ false };  // an ace still counted as 11
	};
	Total Count(const std::vector<Card>& a_cards);
	bool  IsBlackjack(const std::vector<Card>& a_cards, bool a_split = false);
	bool  IsPair(const std::vector<Card>& a_cards);  // two cards of the same count (a ten and a king split too)

	inline constexpr int kDecks = 6;
	struct Shoe
	{
		std::vector<Card> cards;
		std::size_t       next{ 0 };
		std::size_t       cut{ 0 };
		void Shuffle(std::mt19937& a_rng);
		Card Draw(std::mt19937& a_rng);  // (an empty shoe shuffles itself)
		bool PastCut() const { return next >= cut; }
		std::size_t Left() const { return cards.size() - next; }
	};

	enum class Move
	{
		kHit,
		kStand,
		kDouble,
		kSplit,
	};
	// the book (basic strategy for six decks, the dealer standing on soft 17, doubling after a split allowed)
	Move Basic(const std::vector<Card>& a_hand, Card a_dealerUp, bool a_canDouble, bool a_canSplit);
	const char* MoveName(Move a_m);

	// what a hand gets back for its bet (the bet itself included): 0 lost, 1 push, 2 won, 2.5 a blackjack
	double Return(const std::vector<Card>& a_hand, bool a_split, const std::vector<Card>& a_dealer);
}
