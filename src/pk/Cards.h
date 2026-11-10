#pragma once

// Cards and poker hands, standing alone (no game headers: tested on its own in tests/poker_test.cpp).

#include <array>
#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace pk
{
	// A card: suit * 13 + rank; rank 0 = two .. 12 = ace; suits clubs, diamonds, hearts, spades (the order the card
	// models and their ESL forms come in: Card_2c .. Card_As).
	using Card = std::uint8_t;
	inline constexpr int  kDeck = 52;
	inline constexpr int  Rank(Card a_c) { return a_c % 13; }
	inline constexpr int  Suit(Card a_c) { return a_c / 13; }
	inline constexpr Card MakeCard(int a_rank, int a_suit) { return static_cast<Card>(a_suit * 13 + a_rank); }
	std::string           CardName(Card a_c);  // "Ah", "Tc"
	std::string           CardLong(Card a_c);  // "ACE OF HEARTS"

	// A hand's score: higher beats lower, equal ties. Category in the top bits (0 high card .. 8 straight flush),
	// then the five ranks that decide it, most important first.
	enum class Category : int
	{
		kHighCard,
		kPair,
		kTwoPair,
		kTrips,
		kStraight,
		kFlush,
		kFullHouse,
		kQuads,
		kStraightFlush,
	};
	using Score = std::uint32_t;
	Score       Evaluate(std::span<const Card> a_cards);  // the best five of 5 to 7 cards
	Category    CategoryOf(Score a_s);
	std::string HandName(Score a_s);  // "TWO PAIR, KINGS AND FIVES", "FLUSH, ACE HIGH", "ROYAL FLUSH"

	// A shuffled deck, dealt from the top.
	class Deck
	{
	public:
		void Shuffle(std::mt19937& a_rng);
		Card Deal() { return _cards[_next++]; }
		void Burn() { ++_next; }
		void Remove(Card a_c);  // (for working out odds: cards already seen)
		int  Left() const { return kDeck - _next; }

	private:
		std::array<Card, kDeck> _cards{};
		int                     _next{ 0 };
	};

	// How often a hand wins (ties counted as a share) against a_opponents random hands, the board finished at random:
	// Monte Carlo over a_trials deals. For the settlers' decisions.
	double Equity(std::array<Card, 2> a_hole, std::span<const Card> a_board, int a_opponents, int a_trials, std::mt19937& a_rng);
}
