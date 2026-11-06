#pragma once

// The betting layout: what bet a point on the felt is, where its chips go, what it pays and whether it won.
// Points are in the table's own space (Table.h), X and Y. No game code (tests/layout_test.cpp).

#include "rl/Wheel.h"

#include <optional>
#include <string>
#include <vector>

namespace rl
{
	struct Vec2
	{
		double x{ 0.0 };
		double y{ 0.0 };
	};

	enum class BetKind
	{
		kStraight,   // one number                 35 to 1
		kSplit,      // two                         17 to 1
		kStreet,     // a row of three              11 to 1
		kTrio,       // 0 with 1-2 or 2-3 (00-2-3)  11 to 1
		kCorner,     // four                         8 to 1
		kFirstFour,  // 0, 1, 2, 3 (European)        8 to 1
		kTopLine,    // 0, 00, 1, 2, 3 (American)    6 to 1
		kSixLine,    // two rows                     5 to 1
		kDozen,      // 1-12, 13-24, 25-36           2 to 1
		kColumn,     // a 2-to-1 column              2 to 1
		kRed,
		kBlack,
		kOdd,
		kEven,
		kLow,        // 1-18
		kHigh,       // 19-36                        even money
	};

	struct Bet
	{
		BetKind          kind{ BetKind::kStraight };
		std::vector<int> numbers;  // sorted; 37 = 00
		Vec2             spot;     // where its chips sit
		std::string      key;      // the same bet always has the same key ("S:17", "P:17,20", "RED")

		int         Payout() const;  // to 1
		bool        Wins(int a_number) const;
		std::string Name() const;    // "STRAIGHT UP 17", "SPLIT 17/20", "RED"
	};

	// The bet at a point on the felt, if there's one there.
	std::optional<Bet> BetAt(WheelType a_type, Vec2 a_point);

	// A bet by its key (for the save and for re-betting the last round). Empty if the key doesn't make sense for
	// this wheel.
	std::optional<Bet> BetFromKey(WheelType a_type, const std::string& a_key);

	// Every distinct bet on this layout (for NPCs picking one, and the tests).
	std::vector<Bet> AllBets(WheelType a_type);

	// The area the cursor can move in.
	Vec2 LayoutMin();
	Vec2 LayoutMax();
	Vec2 LayoutCentre();

	// The chips that make up an amount, biggest first, for stacking: 100s, 25s, 5s, 1s.
	std::vector<int> ChipsFor(int a_amount);
	inline constexpr int kChipValues[4] = { 1, 5, 25, 100 };
}
