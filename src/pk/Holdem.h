#pragma once

// No-limit Texas Hold'em: one hand at a time, the rules only (no game headers: tests/poker_test.cpp plays it on its
// own). The plugin (Poker.cpp) seats the players, asks whoever's to act (you through the HUD, settlers through
// Decide), and shows what the hand does.

#include "pk/Cards.h"

#include <optional>

namespace pk
{
	struct Action
	{
		enum class Kind : std::uint8_t
		{
			kFold,
			kCheck,
			kCall,
			kRaise,  // (a bet, when nobody has yet): a_to is the total this street
			kAllIn,
		};
		Kind kind{ Kind::kFold };
		int  to{ 0 };  // for a raise: what the player's bet comes to this street
	};
	std::string ActionName(const Action& a_a, int a_paid);  // "FOLDS", "CALLS 20", "RAISES TO 60"

	// How a settler plays: how loose (plays weaker hands), how aggressive (raises rather than calls), how often they
	// bluff. Fixed per settler (from their form ID), so the regulars have a style.
	// The kinds of player at the table: the classic ones, and some of the wasteland's own
	enum class Archetype : std::uint8_t
	{
		kTightAggressive,  // few hands, played hard
		kLooseAggressive,  // lots of hands, lots of pressure
		kCallingStation,   // calls anything, rarely raises, never believes you
		kManiac,           // raises everything
		kRock,             // waits for aces
		kRaider,           // loose, aggressive, bluffs too much, tilts easily
		kGhoul,            // two hundred years of patience: tight, traps, never tilts
		kSynth,            // plays the odds exactly, hardly any randomness
		kPrewarSnob,       // tight and passive, then a big bluff to show you how it's done
		kCount
	};
	const char* ArchetypeName(Archetype a_a);  // "TIGHT-AGGRESSIVE", "GHOUL"...

	struct Style
	{
		Archetype archetype{ Archetype::kTightAggressive };
		double    looseness{ 0.5 };   // 0 tight .. 1 loose (hands played)
		double    aggression{ 0.5 };  // 0 passive .. 1 aggressive (raises over calls)
		double    bluffing{ 0.1 };    // chance of betting with nothing
		double    stubborn{ 0.0 };    // 0 .. 1: calls rather than folds (the calling station)
		double    trapping{ 0.1 };    // chance of slow-playing a monster (check / call, raise later)
		double    drawing{ 0.4 };     // chance of betting a draw (the semi-bluff)
		double    noise{ 0.15 };      // how much each decision wobbles (0: the same every time)
		double    tiltProne{ 0.5 };   // how much a bad beat gets to them
		static Style From(std::uint32_t a_seed);               // a random kind, from a seed (the same settler, the same kind)
		static Style Of(Archetype a_a, std::uint32_t a_seed);  // that kind, with a little variety
	};

	struct Player
	{
		std::uint32_t       id{ 0 };       // whatever the plugin knows them by (0: you)
		int                 stack{ 0 };    // caps in front of them
		int                 bet{ 0 };      // put in this street
		int                 committed{ 0 };  // put in this hand
		bool                folded{ false };
		bool                allIn{ false };
		bool                acted{ false };  // has acted since the last raise
		std::array<Card, 2> hole{};
		Style               style;
		double              tilt{ 0.0 };  // 0 .. 1: steaming after a bad loss (looser, wilder; cools off hand by hand)
	};

	struct Pot
	{
		int              amount{ 0 };
		std::vector<int> eligible;  // seats that can win it
		std::vector<int> winners;   // after the showdown
	};

	enum class Street : std::uint8_t
	{
		kPreflop,
		kFlop,
		kTurn,
		kRiver,
		kShowdown,  // the cards are shown and the pots paid (Hand::pots, Hand::paid)
	};

	class Hand
	{
	public:
		// a new hand: a_players in seat order (at least two with chips), the button at a_button (a seat index), the
		// blinds; shuffles and deals two cards each, posts the blinds
		void Start(std::vector<Player> a_players, int a_button, int a_small, int a_big, std::mt19937& a_rng);

		int  ToAct() const { return _toAct; }  // the seat whose turn it is, or -1 (nobody: the hand's over)
		bool Over() const { return street == Street::kShowdown; }

		// for the player to act
		int  ToCall(int a_seat) const;        // what calling costs them
		int  MinRaiseTo(int a_seat) const;    // the smallest legal raise (their bet this street comes to this)
		int  MaxRaiseTo(int a_seat) const;    // all in
		bool CanCheck(int a_seat) const { return ToCall(a_seat) == 0; }
		int  PotTotal() const;                // everything in the middle and in front of the players

		// what the player to act does (anything illegal is made the nearest legal thing: a raise too small is the
		// minimum, a check facing a bet is a fold); returns what they actually paid in
		int Apply(Action a_action);

		// a player out of turn walks away (or stands up): their hand's folded, what they put in stays
		void Fold(int a_seat);

		// the board so far, and the hand's progress
		std::vector<Player> players;
		std::vector<Card>   board;
		Street              street{ Street::kPreflop };
		int                 button{ 0 };
		int                 smallBlind{ 0 };
		int                 bigBlind{ 0 };
		int                 lastAggressor{ -1 };
		int                 preflopRaiser{ -1 };  // who raised last before the flop (they're the one to bet the flop)
		int                 raises{ 0 };          // raises this street
		bool                showdown{ false };  // ended with cards shown (not everyone else folding)
		std::vector<Pot>    pots;               // after the hand: each pot and who took it
		std::vector<int>    paid;               // after the hand: what each seat got back
		std::vector<Score>  scores;             // after a showdown: each live seat's hand

		int Live() const;         // seats still in (not folded)
		int CanBet() const;       // seats still in with chips behind
		int NextSeat(int a_from, bool a_needChips) const;

	private:
		void BeginStreet();
		void EndStreet();
		void Finish();
		bool RoundDone() const;

		Deck _deck;
		int  _toAct{ -1 };
		int  _currentBet{ 0 };   // the bet to match this street
		int  _lastRaise{ 0 };    // the size of the last raise (the minimum next raise)
	};

	// What a settler does at their turn: from how likely their hand is to win (Monte Carlo against the players still
	// in), the price, the pot and their style.
	Action Decide(const Hand& a_hand, int a_seat, std::mt19937& a_rng, double* a_equity = nullptr);

	// how sure a settler is of what they're doing (0 a hard choice .. 1 obvious): how long they take over it
	double Confidence(const Hand& a_hand, int a_seat, const Action& a_action, double a_equity);

	// a starting hand's strength (Bill Chen's formula: about -1 for 7-2 off suit .. 20 for aces)
	double ChenScore(Card a_a, Card a_b);
}
