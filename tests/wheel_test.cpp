// The wheel: pocket layout and the spin physics. Built and run on its own (no game):
//   g++ -std=c++20 -O2 -Isrc tests/wheel_test.cpp src/rl/Wheel.cpp -o wheel_test && ./wheel_test

#include "rl/Table.h"
#include "rl/Wheel.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

using namespace rl;

namespace
{
	int g_failed = 0;

	void Check(bool a_ok, const char* a_what)
	{
		if (!a_ok) {
			std::printf("FAIL: %s\n", a_what);
			++g_failed;
		}
	}
}

int main()
{
	// the sequences
	for (auto type : { WheelType::kEuropean, WheelType::kAmerican }) {
		const auto& s = Sequence(type);
		std::map<int, int> seen;
		for (int v : s) {
			++seen[v];
		}
		Check(static_cast<int>(seen.size()) == PocketCount(type), "every number once");
		Check(s[0] == 0, "starts at 0");
		for (int k = 0; k < PocketCount(type); ++k) {
			Check(PocketAt(type, PocketAngle(type, k)) == k, "pocket angle round trip");
			Check(PocketAt(type, PocketAngle(type, k) + 0.9 * 3.14159265 / PocketCount(type)) == k, "within the pocket");
		}
		// colours alternate round the wheel (apart from the zeros)
		for (std::size_t i = 1; i + 1 < s.size(); ++i) {
			if (s[i] != kDoubleZero && s[i + 1] != kDoubleZero && s[i] != 0 && s[i + 1] != 0) {
				Check(ColourOf(s[i]) != ColourOf(s[i + 1]), "colours alternate");
			}
		}
	}
	Check(PocketCount(WheelType::kEuropean) == 37 && PocketCount(WheelType::kAmerican) == 38, "pocket counts");
	Check(PocketAt(WheelType::kEuropean, 3.14159265 / 2) == 0, "0 at +Y");
	Check(NumberName(kDoubleZero) == "00", "00's name");

	// the physics on its own: where balls end up, how long it takes
	for (auto type : { WheelType::kEuropean, WheelType::kAmerican }) {
		const int n = PocketCount(type);
		std::mt19937 rng(1234);
		std::vector<int> natural(n, 0);
		double sumDrop = 0, sumSettle = 0, minSettle = 1e9, maxSettle = 0;
		int bad = 0;
		const int runs = 2000;
		for (int i = 0; i < runs; ++i) {
			SpinParams p;
			p.ballSpeed = -16.0 * (0.9 + 0.2 * (rng() % 1000) / 1000.0);
			p.rotorAngle = (rng() % 6283) / 1000.0;
			p.rotorSpeed = 1.8;
			std::mt19937 noise(rng());
			auto plan = SimulateSpin(type, p, noise);
			++natural[plan.pocket];
			sumDrop += plan.dropTime;
			sumSettle += plan.settleTime;
			minSettle = std::min<double>(minSettle, plan.settleTime);
			maxSettle = std::max<double>(maxSettle, plan.settleTime);
			// the ball ends in the pocket it's said to
			const auto& last = plan.samples.back();
			if (PocketAt(type, last.theta - last.rotor) != plan.pocket) {
				++bad;
			}
			for (const auto& smp : plan.samples) {
				if (smp.r < table::kPocketInner || smp.r > table::kTrackWall || smp.z < table::kPocketFloorZ || smp.z > table::kTrackZ + 15) {
					++bad;
					break;
				}
			}
		}
		int lo = runs, hi = 0;
		for (int c : natural) {
			lo = std::min(lo, c);
			hi = std::max(hi, c);
		}
		std::printf("%s: drop %.1fs, settle %.1fs avg (%.1f-%.1f); natural pockets %d-%d per %d (fair %.0f); %d bad\n",
			type == WheelType::kEuropean ? "European" : "American", sumDrop / runs, sumSettle / runs, minSettle, maxSettle, lo, hi, runs,
			runs / double(n), bad);
		Check(bad == 0, "every spin ends in its pocket and stays in the bowl");
		Check(sumSettle / runs > 7.0 && sumSettle / runs < 20.0, "a spin takes a sensible time");
		Check(maxSettle < 29.0, "no spin hits the time limit");
		Check(lo > 0, "every pocket is reachable");
	}

	// the drawn result: exactly uniform, always reached
	for (auto type : { WheelType::kEuropean, WheelType::kAmerican }) {
		const int n = PocketCount(type);
		std::mt19937 rng(99);
		std::vector<int> counts(n, 0);
		int undrawn = 0, maxTries = 0, shortFinish = 0;
		double onRotor = 0, frets = 0, lands = 0, travel = 0;
		const int runs = 3000;
		for (int i = 0; i < runs; ++i) {
			SpinParams p;
			p.rotorAngle = (rng() % 6283) / 1000.0;
			p.rotorSpeed = 1.8;
			auto plan = PlanSpin(type, p, rng);
			++counts[plan.pocket];
			undrawn += !plan.drawn;
			maxTries = std::max(maxTries, plan.tries);
			const double rot = plan.settleTime - plan.rotorTime;
			onRotor += rot;
			frets += plan.frets;
			travel += plan.travel;
			for (const auto& ev : plan.events) {
				lands += ev.kind == SpinEvent::Kind::kLand && ev.t > plan.rotorTime;
			}
			shortFinish += rot < 3.0 || plan.frets < 4 || plan.travel < 8.0;
			// events in order, inside the spin
			for (std::size_t k = 1; k < plan.events.size(); ++k) {
				Check(plan.events[k].t >= plan.events[k - 1].t && plan.events[k].t <= plan.samples.back().t, "events in order");
			}
		}
		double chi = 0;
		const double e = runs / double(n);
		for (int c : counts) {
			chi += (c - e) * (c - e) / e;
		}
		std::printf("%s drawn: chi-square %.1f over %d pockets, %d not drawn, most tries %d; on the rotor %.1fs, %.1f frets, %.1f pockets travelled, %.1f bounces avg; %d short\n",
			type == WheelType::kEuropean ? "European" : "American", chi, n, undrawn, maxTries, onRotor / runs, frets / runs, travel / runs, lands / runs,
			shortFinish);
		Check(shortFinish * 50 < runs, "nearly every spin bounces about on the rotor before it sits down");
		Check(undrawn == 0, "every draw is reached");
		Check(chi < 80.0, "results look uniform");
	}

	if (g_failed) {
		std::printf("%d check(s) failed\n", g_failed);
		return 1;
	}
	std::printf("wheel: all checks passed\n");
	return 0;
}
