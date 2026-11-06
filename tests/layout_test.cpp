// The betting layout: hit-testing, every bet's numbers and payouts.
//   g++ -std=c++20 -O2 -Isrc tests/layout_test.cpp src/rl/Layout.cpp src/rl/Wheel.cpp -o layout_test && ./layout_test

#include "rl/Layout.h"
#include "rl/Table.h"

#include <cmath>
#include <cstdio>
#include <map>

using namespace rl;
using namespace rl::table;

namespace
{
	int g_failed = 0;

	void Check(bool a_ok, const std::string& a_what)
	{
		if (!a_ok) {
			std::printf("FAIL: %s\n", a_what.c_str());
			++g_failed;
		}
	}

	std::string At(WheelType a_type, double a_x, double a_y)
	{
		auto b = BetAt(a_type, { a_x, a_y });
		return b ? b->key : std::string("-");
	}

	double Cx(int c) { return kGridX0 + (c + 0.5) * kCellW; }
	double Cy(int r) { return kGridY0 + (r + 0.5) * kRowH; }
}

int main()
{
	const auto eu = WheelType::kEuropean;
	const auto us = WheelType::kAmerican;

	// cells
	Check(At(eu, Cx(0), Cy(0)) == "S:1", "1");
	Check(At(eu, Cx(0), Cy(2)) == "S:3", "3");
	Check(At(eu, Cx(11), Cy(2)) == "S:36", "36");
	Check(At(eu, Cx(5), Cy(1)) == "S:17", "17");
	// lines
	Check(At(eu, kGridX0 + 6 * kCellW, Cy(1)) == "P:17,20", "split across");
	Check(At(eu, Cx(5), kGridY0 + 2 * kRowH) == "P:17,18", "split up");
	Check(At(eu, kGridX0 + 6 * kCellW, kGridY0 + 2 * kRowH) == "C:17,18,20,21", "corner");
	Check(At(eu, Cx(5), kGridY0 + 0.01) == "R:16,17,18", "street");
	Check(At(eu, kGridX0 + 6 * kCellW, kGridY0 + 0.01) == "X:16,17,18,19,20,21", "six line");
	Check(At(eu, kGridX0, kGridY0 + 0.01) == "F:0,1,2,3", "first four");
	Check(At(us, kGridX0, kGridY0 + 0.01) == "L:0,1,2,3,37", "top line");
	Check(At(eu, kGridX0, Cy(1)) == "P:0,2", "zero split");
	Check(At(us, kGridX0, Cy(2)) == "P:3,37", "00 split");
	Check(At(eu, kGridX0, kGridY0 + kRowH) == "T:0,1,2", "trio");
	Check(At(us, kGridX0, kGridY0 + 2 * kRowH) == "T:2,3,37", "00 trio");
	// zeros
	Check(At(eu, (kZeroX0 + kGridX0) / 2, Cy(2)) == "S:0", "0");
	Check(At(us, (kZeroX0 + kGridX0) / 2, Cy(2)) == "S:37", "00");
	Check(At(us, (kZeroX0 + kGridX0) / 2, Cy(0)) == "S:0", "0 (US)");
	Check(At(us, (kZeroX0 + kGridX0) / 2, kGridY0 + 1.5 * kRowH) == "P:0,37", "0/00");
	// outside
	Check(At(eu, (kGridX1 + kColumnX1) / 2, Cy(0)).starts_with("K:1,4,"), "column 1");
	Check(At(eu, Cx(5), (kDozenY0 + kGridY0) / 2).starts_with("D:13,"), "2nd 12");
	Check(At(eu, Cx(4), (kOutsideY0 + kDozenY0) / 2) == "RED", "red");
	Check(At(eu, Cx(6), (kOutsideY0 + kDozenY0) / 2) == "BLACK", "black");
	Check(At(eu, Cx(0), (kOutsideY0 + kDozenY0) / 2) == "LOW", "1-18");
	Check(At(eu, Cx(11), (kOutsideY0 + kDozenY0) / 2) == "HIGH", "19-36");
	Check(At(eu, Cx(5), kOutsideY0 - 5) == "-", "off the layout");

	// every bet: counts, the right numbers for its kind, and a fair price (the house edge is the zeros, the same on
	// every bet: 1/37, or 2/38 in America; the top line is the one worse bet, 35/38)
	for (auto type : { eu, us }) {
		const auto all = AllBets(type);
		const int  pockets = PocketCount(type);
		std::map<BetKind, int> kinds;
		for (const auto& b : all) {
			++kinds[b.kind];
			const double ev = (b.Payout() + 1.0) * b.numbers.size() / pockets;
			const double fair = type == eu ? 36.0 / 37.0 : (b.kind == BetKind::kTopLine ? 35.0 / 38.0 : 36.0 / 38.0);
			Check(std::abs(ev - fair) < 1e-9, "fair price: " + b.Name());
			int wins = 0;
			for (int n : Sequence(type)) {
				wins += b.Wins(n);
			}
			Check(wins == static_cast<int>(b.numbers.size()), "wins on its numbers: " + b.Name());
			auto again = BetFromKey(type, b.key);
			Check(again && again->key == b.key, "key round trip: " + b.key);
			auto there = BetAt(type, b.spot);
			Check(there && there->key == b.key, "its chips sit on it: " + b.Name());
		}
		const bool isUS = type == us;
		Check(kinds[BetKind::kStraight] == (isUS ? 38 : 37), "straights");
		Check(kinds[BetKind::kSplit] == (isUS ? 62 : 60), "splits");
		Check(kinds[BetKind::kStreet] == 12, "streets");
		Check(kinds[BetKind::kCorner] == 22, "corners");
		Check(kinds[BetKind::kSixLine] == 11, "six lines");
		Check(kinds[BetKind::kTrio] == 2, "trios");
		Check(kinds[BetKind::kFirstFour] == (isUS ? 0 : 1), "first four");
		Check(kinds[BetKind::kTopLine] == (isUS ? 1 : 0), "top line");
		Check(kinds[BetKind::kDozen] == 3 && kinds[BetKind::kColumn] == 3, "dozens and columns");
		std::printf("%s: %zu bets\n", isUS ? "American" : "European", all.size());
	}

	// chips
	Check((ChipsFor(137) == std::vector<int>{ 100, 25, 5, 5, 1, 1 }), "chips for 137");
	Check(ChipsFor(0).empty(), "no chips for 0");

	if (g_failed) {
		std::printf("%d check(s) failed\n", g_failed);
		return 1;
	}
	std::printf("layout: all checks passed\n");
	return 0;
}
