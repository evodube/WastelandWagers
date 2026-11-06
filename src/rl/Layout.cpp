#include "rl/Layout.h"

#include "rl/Table.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>

namespace rl
{
	namespace
	{
		using namespace table;

		constexpr double kEdgeX = 0.24;  // within this share of a cell's width of its side: the bet is on the line
		constexpr double kEdgeY = 0.2;   // ...of its height

		int Number(int a_col, int a_row) { return 3 * a_col + a_row + 1; }
		double CellX(int a_col) { return kGridX0 + a_col * kCellW; }
		double CellY(int a_row) { return kGridY0 + a_row * kRowH; }

		Bet Make(BetKind a_kind, std::vector<int> a_numbers, Vec2 a_spot)
		{
			std::ranges::sort(a_numbers);
			Bet b;
			b.kind = a_kind;
			b.numbers = std::move(a_numbers);
			b.spot = a_spot;
			switch (a_kind) {
			case BetKind::kRed: b.key = "RED"; break;
			case BetKind::kBlack: b.key = "BLACK"; break;
			case BetKind::kOdd: b.key = "ODD"; break;
			case BetKind::kEven: b.key = "EVEN"; break;
			case BetKind::kLow: b.key = "LOW"; break;
			case BetKind::kHigh: b.key = "HIGH"; break;
			default:
				{
					static const std::map<BetKind, const char*> prefix{ { BetKind::kStraight, "S" }, { BetKind::kSplit, "P" },
						{ BetKind::kStreet, "R" }, { BetKind::kTrio, "T" }, { BetKind::kCorner, "C" }, { BetKind::kFirstFour, "F" },
						{ BetKind::kTopLine, "L" }, { BetKind::kSixLine, "X" }, { BetKind::kDozen, "D" }, { BetKind::kColumn, "K" } };
					b.key = std::string(prefix.at(a_kind)) + ":";
					for (std::size_t i = 0; i < b.numbers.size(); ++i) {
						b.key += (i ? "," : "") + std::to_string(b.numbers[i]);
					}
				}
				break;
			}
			return b;
		}

		std::vector<int> Range(int a_from, int a_to, int a_step = 1)
		{
			std::vector<int> v;
			for (int n = a_from; n <= a_to; n += a_step) {
				v.push_back(n);
			}
			return v;
		}

		// a point on the numbers (or within a line's reach of them, from the zero column)
		std::optional<Bet> GridBet(WheelType a_type, Vec2 a_p)
		{
			const bool   us = a_type == WheelType::kAmerican;
			const double gx = (a_p.x - kGridX0) / kCellW;
			const double gy = (a_p.y - kGridY0) / kRowH;
			const int    c = std::clamp(static_cast<int>(std::floor(gx)), 0, 11);
			const int    r = std::clamp(static_cast<int>(std::floor(gy)), 0, 2);
			const double fx = gx - c;
			const double fy = gy - r;
			const bool   nearL = fx < kEdgeX;
			const bool   nearR = fx > 1.0 - kEdgeX;
			const bool   nearB = fy < kEdgeY;
			const bool   nearT = fy > 1.0 - kEdgeY;
			const double cx = CellX(c) + kCellW * 0.5;
			const double cy = CellY(r) + kRowH * 0.5;
			const int    cb = nearL ? c - 1 : c;  // the column to the left of the line the point is near (-1: the zeros)

			// the bottom line of the first row: streets and six lines (and the zeros' corner)
			if (r == 0 && nearB) {
				if (nearL && c == 0) {
					return us ? Make(BetKind::kTopLine, { 0, kDoubleZero, 1, 2, 3 }, { kGridX0, kGridY0 })
					          : Make(BetKind::kFirstFour, { 0, 1, 2, 3 }, { kGridX0, kGridY0 });
				}
				if ((nearL || nearR) && cb >= 0 && cb < 11) {
					return Make(BetKind::kSixLine, Range(Number(cb, 0), Number(cb + 1, 2)), { CellX(cb + 1), kGridY0 });
				}
				return Make(BetKind::kStreet, Range(Number(c, 0), Number(c, 2)), { cx, kGridY0 });
			}
			const int  rb = nearB ? r - 1 : r;  // the row below the line the point is near
			const bool rowLine = (nearB || nearT) && rb >= 0 && rb < 2;
			if ((nearL || nearR) && rowLine && cb < 11) {
				if (cb < 0) {
					if (rb == 0) {
						return Make(BetKind::kTrio, { 0, 1, 2 }, { kGridX0, CellY(1) });
					}
					return Make(BetKind::kTrio, { us ? kDoubleZero : 0, 2, 3 }, { kGridX0, CellY(2) });
				}
				return Make(BetKind::kCorner, { Number(cb, rb), Number(cb, rb + 1), Number(cb + 1, rb), Number(cb + 1, rb + 1) },
					{ CellX(cb + 1), CellY(rb + 1) });
			}
			if ((nearL || nearR) && cb < 11) {
				if (cb < 0) {
					// (in America the 2 sits between both zeros: its lower half splits with 0, its upper half with 00)
					int    zero = 0;
					double y = cy;
					if (us && (r == 2 || (r == 1 && fy >= 0.5))) {
						zero = kDoubleZero;
					}
					if (us && r == 1) {
						y += (fy >= 0.5 ? 0.25 : -0.25) * kRowH;
					}
					return Make(BetKind::kSplit, { zero, Number(0, r) }, { kGridX0, y });
				}
				return Make(BetKind::kSplit, { Number(cb, r), Number(cb + 1, r) }, { CellX(cb + 1), cy });
			}
			if (rowLine) {
				return Make(BetKind::kSplit, { Number(c, rb), Number(c, rb + 1) }, { cx, CellY(rb + 1) });
			}
			return Make(BetKind::kStraight, { Number(c, r) }, { cx, cy });
		}
	}

	int Bet::Payout() const
	{
		switch (kind) {
		case BetKind::kStraight: return 35;
		case BetKind::kSplit: return 17;
		case BetKind::kStreet:
		case BetKind::kTrio: return 11;
		case BetKind::kCorner:
		case BetKind::kFirstFour: return 8;
		case BetKind::kTopLine: return 6;
		case BetKind::kSixLine: return 5;
		case BetKind::kDozen:
		case BetKind::kColumn: return 2;
		default: return 1;
		}
	}

	bool Bet::Wins(int a_number) const
	{
		return std::ranges::find(numbers, a_number) != numbers.end();
	}

	std::string Bet::Name() const
	{
		auto list = [&](const char* a_sep) {
			std::string s;
			for (std::size_t i = 0; i < numbers.size(); ++i) {
				s += (i ? a_sep : "") + NumberName(numbers[i]);
			}
			return s;
		};
		switch (kind) {
		case BetKind::kStraight: return "STRAIGHT UP " + NumberName(numbers[0]);
		case BetKind::kSplit: return "SPLIT " + list("/");
		case BetKind::kStreet: return "STREET " + list("/");
		case BetKind::kTrio: return "TRIO " + list("/");
		case BetKind::kCorner: return "CORNER " + list("/");
		case BetKind::kFirstFour: return "FIRST FOUR 0/1/2/3";
		case BetKind::kTopLine: return "TOP LINE 0/00/1/2/3";
		case BetKind::kSixLine: return std::format("SIX LINE {}-{}", numbers.front(), numbers.back());
		case BetKind::kDozen: return numbers.front() == 1 ? "1ST 12" : numbers.front() == 13 ? "2ND 12" : "3RD 12";
		case BetKind::kColumn: return std::format("COLUMN {} (2 TO 1)", numbers.front());
		case BetKind::kRed: return "RED";
		case BetKind::kBlack: return "BLACK";
		case BetKind::kOdd: return "ODD";
		case BetKind::kEven: return "EVEN";
		case BetKind::kLow: return "1-18";
		case BetKind::kHigh: return "19-36";
		}
		return "?";
	}

	std::optional<Bet> BetAt(WheelType a_type, Vec2 a_p)
	{
		const bool us = a_type == WheelType::kAmerican;
		// the numbers, and the lines between them and the zeros
		if (a_p.y >= kGridY0 && a_p.y < kGridY1 && a_p.x >= kGridX0 - kEdgeX * kCellW && a_p.x < kGridX1) {
			return GridBet(a_type, a_p);
		}
		// the zero column
		if (a_p.y >= kGridY0 && a_p.y < kGridY1 && a_p.x >= kZeroX0 && a_p.x < kGridX0) {
			const double zx = (kZeroX0 + kGridX0) * 0.5;
			if (!us) {
				return Make(BetKind::kStraight, { 0 }, { zx, (kGridY0 + kGridY1) * 0.5 });
			}
			const double mid = kGridY0 + 1.5 * kRowH;
			if (std::abs(a_p.y - mid) < 0.15 * kRowH) {
				return Make(BetKind::kSplit, { 0, kDoubleZero }, { zx, mid });
			}
			return a_p.y < mid ? Make(BetKind::kStraight, { 0 }, { zx, kGridY0 + 0.75 * kRowH })
			                   : Make(BetKind::kStraight, { kDoubleZero }, { zx, kGridY0 + 2.25 * kRowH });
		}
		// the 2-to-1 columns
		if (a_p.y >= kGridY0 && a_p.y < kGridY1 && a_p.x >= kGridX1 && a_p.x < kColumnX1) {
			const int r = std::clamp(static_cast<int>(std::floor((a_p.y - kGridY0) / kRowH)), 0, 2);
			return Make(BetKind::kColumn, Range(r + 1, 34 + r, 3), { (kGridX1 + kColumnX1) * 0.5, CellY(r) + kRowH * 0.5 });
		}
		if (a_p.x < kGridX0 || a_p.x >= kGridX1) {
			return std::nullopt;
		}
		// the dozens
		if (a_p.y >= kDozenY0 && a_p.y < kGridY0) {
			const int d = std::clamp(static_cast<int>(std::floor((a_p.x - kGridX0) / (4 * kCellW))), 0, 2);
			return Make(BetKind::kDozen, Range(12 * d + 1, 12 * d + 12), { CellX(4 * d + 2), (kDozenY0 + kGridY0) * 0.5 });
		}
		// the even-money bets
		if (a_p.y >= kOutsideY0 && a_p.y < kDozenY0) {
			const int  o = std::clamp(static_cast<int>(std::floor((a_p.x - kGridX0) / (2 * kCellW))), 0, 5);
			const Vec2 spot{ CellX(2 * o + 1), (kOutsideY0 + kDozenY0) * 0.5 };
			std::vector<int> nums;
			switch (o) {
			case 0: return Make(BetKind::kLow, Range(1, 18), spot);
			case 1: return Make(BetKind::kEven, Range(2, 36, 2), spot);
			case 2:
			case 3:
				for (int n = 1; n <= 36; ++n) {
					if (ColourOf(n) == (o == 2 ? Colour::kRed : Colour::kBlack)) {
						nums.push_back(n);
					}
				}
				return Make(o == 2 ? BetKind::kRed : BetKind::kBlack, nums, spot);
			case 4: return Make(BetKind::kOdd, Range(1, 35, 2), spot);
			default: return Make(BetKind::kHigh, Range(19, 36), spot);
			}
		}
		return std::nullopt;
	}

	std::vector<Bet> AllBets(WheelType a_type)
	{
		static std::map<WheelType, std::vector<Bet>> cache;
		auto it = cache.find(a_type);
		if (it != cache.end()) {
			return it->second;
		}
		std::map<std::string, Bet> found;
		const Vec2 lo = LayoutMin(), hi = LayoutMax();
		for (double y = lo.y; y <= hi.y; y += 0.25) {
			for (double x = lo.x; x <= hi.x; x += 0.25) {
				if (auto b = BetAt(a_type, { x, y })) {
					found.emplace(b->key, *b);
				}
			}
		}
		std::vector<Bet> out;
		for (auto& [k, b] : found) {
			out.push_back(b);
		}
		cache[a_type] = out;
		return out;
	}

	std::optional<Bet> BetFromKey(WheelType a_type, const std::string& a_key)
	{
		for (const auto& b : AllBets(a_type)) {
			if (b.key == a_key) {
				return b;
			}
		}
		return std::nullopt;
	}

	Vec2 LayoutMin() { return { kZeroX0 - 3.0, kOutsideY0 - 3.0 }; }
	Vec2 LayoutMax() { return { kColumnX1 + 3.0, kGridY1 + 3.0 }; }
	Vec2 LayoutCentre() { return { (kZeroX0 + kColumnX1) * 0.5, (kOutsideY0 + kGridY1) * 0.5 }; }

	std::vector<int> ChipsFor(int a_amount)
	{
		std::vector<int> chips;
		for (int i = 3; i >= 0; --i) {
			while (a_amount >= kChipValues[i]) {
				chips.push_back(kChipValues[i]);
				a_amount -= kChipValues[i];
			}
		}
		return chips;
	}
}
