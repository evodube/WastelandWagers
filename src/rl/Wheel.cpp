#include "rl/Wheel.h"

#include "rl/Table.h"

#include <algorithm>
#include <cmath>

namespace rl
{
	namespace
	{
		constexpr double kPi = 3.14159265358979323846;
		constexpr double kTwoPi = 2.0 * kPi;
		constexpr double kG = 687.0;            // gravity, game units/s^2 (9.81 m/s^2 at 1.428 cm per unit)
		constexpr double kStep = 1.0 / 240.0;
		constexpr int    kSampleEvery = 4;      // 60 samples a second
		constexpr double kMaxSeconds = 30.0;
		constexpr double kAfterSettle = 1.0;    // recorded after the ball comes to rest

		const std::vector<int> kEuropean{ 0, 32, 15, 19, 4, 21, 2, 25, 17, 34, 6, 27, 13, 36, 11, 30, 8, 23, 10, 5, 24, 16, 33, 1, 20,
			14, 31, 9, 22, 18, 29, 7, 28, 12, 35, 3, 26 };
		const std::vector<int> kAmerican{ 0, 28, 9, 26, 30, 11, 7, 20, 32, 17, 5, 22, 34, 15, 3, 24, 36, 13, 1, kDoubleZero, 27, 10, 25,
			29, 12, 8, 19, 31, 18, 6, 21, 33, 16, 4, 23, 35, 14, 2 };

		double Wrap(double a)
		{
			a = std::fmod(a, kTwoPi);
			return a < 0.0 ? a + kTwoPi : a;
		}

		double Sign(double a) { return a < 0.0 ? -1.0 : 1.0; }

		// slows w by a_decel (rad/s^2) for a_dt without passing through zero
		double Slow(double a_w, double a_decel, double a_dt)
		{
			const double d = a_decel * a_dt;
			return std::abs(a_w) <= d ? 0.0 : a_w - Sign(a_w) * d;
		}

		// the slope of the bowl's surface at a_r, as the inward pull it gives a rolling ball
		double InwardPull(double a_r)
		{
			using namespace table;
			auto pull = [](double a_rise, double a_run) {
				const double a = std::atan2(a_rise, a_run);
				return kG * std::sin(a) * std::cos(a) * (5.0 / 7.0);  // a rolling ball: 5/7 of a sliding one
			};
			if (a_r >= kConeTop) {
				return 40.0;  // the track leans in a little
			}
			if (a_r >= kRotorEdge) {
				return pull(kConeTopZ - kRotorEdgeZ, kConeTop - kRotorEdge);
			}
			const double ring = pull(kRotorEdgeZ - kNumberInnerZ, kRotorEdge - kNumberInner);
			if (a_r >= kNumberInner) {
				return ring;
			}
			// the pockets' floor is flat; the pull eases off over their outer end, so a ball resting there doesn't chatter
			return ring * std::clamp((a_r - (kPocketOuter - 1.0)) / (kNumberInner - kPocketOuter + 1.0), 0.0, 1.0);
		}
	}

	Colour ColourOf(int a_number)
	{
		static constexpr std::array<int, 18> kRed{ 1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27, 30, 32, 34, 36 };
		if (a_number <= 0 || a_number >= kDoubleZero) {
			return Colour::kGreen;
		}
		return std::ranges::find(kRed, a_number) != kRed.end() ? Colour::kRed : Colour::kBlack;
	}

	std::string NumberName(int a_number)
	{
		return a_number == kDoubleZero ? std::string("00") : std::to_string(a_number);
	}

	std::string ColourName(int a_number)
	{
		switch (ColourOf(a_number)) {
		case Colour::kRed: return "RED";
		case Colour::kBlack: return "BLACK";
		default: return "GREEN";
		}
	}

	const std::vector<int>& Sequence(WheelType a_type)
	{
		return a_type == WheelType::kAmerican ? kAmerican : kEuropean;
	}

	int PocketCount(WheelType a_type)
	{
		return static_cast<int>(Sequence(a_type).size());
	}

	int PocketOf(WheelType a_type, int a_number)
	{
		const auto& s = Sequence(a_type);
		const auto  it = std::ranges::find(s, a_number);
		return it == s.end() ? -1 : static_cast<int>(it - s.begin());
	}

	double PocketAngle(WheelType a_type, int a_pocket)
	{
		const double step = kTwoPi / PocketCount(a_type);
		return Wrap(kPi / 2.0 - a_pocket * step);
	}

	int PocketAt(WheelType a_type, double a_rotorAngle)
	{
		const int    n = PocketCount(a_type);
		const double step = kTwoPi / n;
		const double f = Wrap(kPi / 2.0 - a_rotorAngle) / step + 0.5;
		return static_cast<int>(std::floor(f)) % n;
	}

	double SurfaceZ(double a_r)
	{
		using namespace table;
		auto lerp = [](double a, double b, double t) { return a + (b - a) * std::clamp(t, 0.0, 1.0); };
		if (a_r >= kConeTop) {
			return lerp(kConeTopZ, kTrackZ, (a_r - kConeTop) / (kTrackWall - kConeTop));
		}
		if (a_r >= kRotorEdge) {
			return lerp(kRotorEdgeZ, kConeTopZ, (a_r - kRotorEdge) / (kConeTop - kRotorEdge));
		}
		if (a_r >= kNumberInner) {
			return lerp(kNumberInnerZ, kRotorEdgeZ, (a_r - kNumberInner) / (kRotorEdge - kNumberInner));
		}
		return kPocketFloorZ;
	}

	double RotorDrag(double a_speed, double a_dt)
	{
		return Slow(a_speed, 0.015 + 0.006 * std::abs(a_speed), a_dt);
	}

	SpinPlan SimulateSpin(WheelType a_type, const SpinParams& a_params, std::mt19937& a_noise)
	{
		using namespace table;
		std::uniform_real_distribution<double> U(0.0, 1.0);
		auto uni = [&](double a, double b) { return a + (b - a) * U(a_noise); };

		const int    n = PocketCount(a_type);
		const double step = kTwoPi / n;
		const double rTrack = kTrackWall - kBallRadius;
		const double rInnerWall = kPocketInner + kBallRadius;

		// this throw's own feel
		const double trackDrag = uni(0.45, 0.75);       // rad/s^2
		const double airDrag = uni(0.010, 0.014);       // per rad/s, squared speed
		const double dropSpeed = uni(5.2, 6.2);         // rad/s: slower than this and it leaves the track
		const double diamondChance = uni(0.45, 0.7);

		double r = rTrack, vr = 0.0;
		double th = Wrap(a_params.throwAngle), w = a_params.ballSpeed;
		double h = 0.0, vz = 0.0;  // a hop off the surface
		double phi = Wrap(a_params.rotorAngle), wr = a_params.rotorSpeed;
		bool   onTrack = true;
		double descent = 0.0;      // 0..1 down the cone after leaving the track
		const double descentTime = uni(0.9, 1.5);
		double still = 0.0;        // seconds it's been resting in a pocket
		bool   settled = false;
		double settledFor = 0.0;
		double restAngle = 0.0;    // in the rotor's frame, once settled

		SpinPlan plan;
		plan.samples.reserve(static_cast<std::size_t>(20.0 * 60.0));
		int    stepNo = 0;
		double t = 0.0;
		auto record = [&]() {
			plan.samples.push_back({ static_cast<float>(t), static_cast<float>(r), static_cast<float>(Wrap(th)),
				static_cast<float>(SurfaceZ(r) + kBallRadius + h), static_cast<float>(Wrap(phi)) });
		};
		record();

		for (; t < kMaxSeconds + kAfterSettle; ++stepNo) {
			const double dt = kStep;
			t += dt;
			wr = RotorDrag(wr, dt);
			phi += wr * dt;

			if (settled) {
				// riding round in its pocket, easing into the middle of it
				settledFor += dt;
				const double target = PocketAngle(a_type, plan.pocket);
				restAngle += std::remainder(target - restAngle, kTwoPi) * std::min(1.0, dt * 6.0);
				r += (kPocketCentre - r) * std::min(1.0, dt * 5.0);
				th = phi + restAngle;
				w = wr;
				h = 0.0;
				if (stepNo % kSampleEvery == 0) {
					record();
				}
				if (settledFor >= kAfterSettle) {
					break;
				}
				continue;
			}

			// hops
			if (h > 0.0 || vz > 0.0) {
				vz -= kG * dt;
				h += vz * dt;
				if (h <= 0.0) {
					h = 0.0;
					vz = std::abs(vz) > 20.0 ? -vz * 0.3 : 0.0;
				}
			}

			if (onTrack) {
				// round the track against the wall, slowing
				w = Slow(w, trackDrag + airDrag * w * w, dt);
				th += w * dt;
				if (std::abs(w) < dropSpeed) {
					onTrack = false;
					plan.dropTime = static_cast<float>(t);
				}
			} else if (descent < 1.0) {
				// off the track and spiralling down the cone to the rotor (a set path rather than physics: a ball losing
				// speed this fast is mostly friction, which a simple model gets wrong), knocked about by the diamonds
				const double thBefore = th;
				descent = std::min(1.0, descent + dt / descentTime);
				const double e = descent * descent;
				const double rWas = r;
				r = rTrack - (rTrack - kRotorEdge) * e;
				w = Slow(w, 0.8, dt) * std::sqrt(rWas / r);  // a little of the speed-up of going in
				th += w * dt;
				if (std::abs(r - kDiamondR) < kDiamondHalf + kBallRadius + 0.5 && h < 1.0) {
					const double eighth = kPi / 4.0;
					if (std::floor(Wrap(thBefore) / eighth) != std::floor(Wrap(th) / eighth) && U(a_noise) < diamondChance) {
						w *= uni(-0.25, 0.55);
						vz = uni(25.0, 70.0);
						descent = std::max(0.3, descent - uni(0.05, 0.25));  // thrown back up the cone a little
					}
				}
				if (descent >= 1.0) {
					plan.rotorTime = static_cast<float>(t);
					vr = -2.0 * (rTrack - kRotorEdge) / descentTime;
				}
			} else {
				const double thBefore = th;
				const double rBefore = r;
				// radial: the slope's pull inward against a share of what the turn throws outward
				vr += (0.2 * r * w * w - InwardPull(r)) * dt;
				vr *= std::exp(-(r < kNumberInner + 1.0 ? 3.0 : 1.5) * dt);  // the frets and pocket floor soak up bouncing
				r += vr * dt;
				// angular: dragged towards the rotor's speed, hard once it's down among the pockets
				const bool onRotor = r < kRotorEdge;
				if (onRotor) {
					const double drag = r < kNumberInner ? 1.7 : 0.7;
					w = wr + Slow(w - wr, drag, dt);
				} else {
					w = Slow(w, 0.8, dt);
				}
				th += w * dt;

				if (r > kConeTop) {
					r = kConeTop;
					vr = vr > 0.0 ? -vr * 0.3 : vr;
				}
				if (r < rInnerWall) {
					r = rInnerWall;
					vr = vr < 0.0 ? -vr * 0.35 : vr;
				}

				// the frets between the pockets
				if (r < kNumberInner && rBefore < kNumberInner + 1.0 && h < 0.8) {
					const double aBefore = thBefore - (phi - wr * dt);
					const double aNow = th - phi;
					const double fBefore = std::floor(Wrap(kPi / 2.0 - aBefore) / step + 0.5);
					const double fNow = std::floor(Wrap(kPi / 2.0 - aNow) / step + 0.5);
					if (fBefore != fNow) {
						const double rel = w - wr;
						th = thBefore + wr * dt;  // stays in its pocket this step...
						w = wr - rel * uni(0.35, 0.7);  // ...bouncing back off the fret
						vr += uni(-10.0, 55.0) * std::min(1.0, std::abs(rel) / 3.0);
						vz = std::abs(rel) * r * uni(0.15, 0.4);
					}
				}

				// at rest in a pocket?
				const bool resting = r < kNumberInner && std::abs(w - wr) < 0.15 && std::abs(vr) < 5.0 && h < 0.05;
				still = resting ? still + dt : 0.0;
				if (still > 0.3 || t >= kMaxSeconds) {
					settled = true;
					plan.settleTime = static_cast<float>(t);
					plan.pocket = PocketAt(a_type, th - phi);
					restAngle = Wrap(th - phi);
				}
			}

			if (stepNo % kSampleEvery == 0) {
				record();
			}
		}
		record();
		plan.number = Sequence(a_type)[static_cast<std::size_t>(plan.pocket)];
		plan.rotorSpeed = wr;
		return plan;
	}

	SpinPlan PlanSpin(WheelType a_type, const SpinParams& a_base, std::mt19937& a_rng)
	{
		const int n = PocketCount(a_type);
		const int target = std::uniform_int_distribution<int>(0, n - 1)(a_rng);
		std::uniform_real_distribution<double> U(0.0, 1.0);
		SpinPlan last;
		for (int tries = 0; tries < 600; ++tries) {
			SpinParams p = a_base;
			p.ballSpeed = a_base.ballSpeed * (0.9 + 0.2 * U(a_rng));
			p.throwAngle = a_base.throwAngle + (U(a_rng) - 0.5) * 0.3;
			std::mt19937 noise(a_rng());
			last = SimulateSpin(a_type, p, noise);
			if (last.pocket == target) {
				last.tries = tries;
				last.drawn = true;
				return last;
			}
		}
		last.tries = 600;
		last.drawn = false;
		return last;
	}
}
