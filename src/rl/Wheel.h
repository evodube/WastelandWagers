#pragma once

// The wheel and the ball: which numbers sit where, and a spin worked out start to finish before it's shown.
//
// Everything here is in the table's own space (Table.h), as polar coordinates round the wheel's axis. Angles are
// radians, counter-clockwise seen from above. The rotor's own frame turns with it: pocket k of the wheel's sequence
// is centred at 90 degrees - k * (360 / N) in that frame (pocket 0 is the 0, at +Y), going clockwise seen from above;
// that's how tools/make_textures.py paints the rotor.
//
// No game code here: it builds and is tested on its own (tests/wheel_test.cpp).

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace rl
{
	inline constexpr int kDoubleZero = 37;  // "00" on the American wheel

	enum class WheelType
	{
		kEuropean,  // 37 pockets, single 0
		kAmerican,  // 38 pockets, 0 and 00
	};

	enum class Colour
	{
		kGreen,
		kRed,
		kBlack,
	};

	Colour      ColourOf(int a_number);
	std::string NumberName(int a_number);  // "17", "0", "00"
	std::string ColourName(int a_number);  // "RED", "BLACK", "GREEN"

	// The wheel's pockets in order (clockwise seen from above, starting at 0).
	const std::vector<int>& Sequence(WheelType a_type);
	int                     PocketCount(WheelType a_type);
	int                     PocketOf(WheelType a_type, int a_number);  // the pocket a number is in, or -1
	// The middle of pocket a_pocket in the rotor's frame.
	double PocketAngle(WheelType a_type, int a_pocket);
	// The pocket at an angle in the rotor's frame.
	int PocketAt(WheelType a_type, double a_rotorAngle);

	// One moment of a spin, for playing it back: the ball in polar coordinates round the wheel's axis (the bowl's
	// frame), its height, and the rotor's angle.
	struct Sample
	{
		float t;       // seconds since the ball was thrown
		float r;
		float theta;   // the ball's angle
		float z;       // the ball's centre
		float rotor;   // the rotor's angle
	};

	// Something the ball did that makes a sound, for the plugin to play at the moment it happens.
	struct SpinEvent
	{
		enum class Kind : std::uint8_t
		{
			kDiamond,  // knocked off a diamond on the cone
			kLand,     // came down off a hop (on the cone, the rotor or into a pocket)
			kFret,     // hit a fret between pockets
			kSettle,   // came to rest in its pocket
		};
		float t;         // seconds since the throw
		Kind  kind;
		float strength;  // 0..1, how hard
	};

	struct SpinPlan
	{
		std::vector<Sample>    samples;  // every 1/60 s
		std::vector<SpinEvent> events;   // in time order
		int                 pocket{ 0 };
		int                 number{ 0 };
		float               dropTime{ 0.0f };    // the ball left the track
		float               rotorTime{ 0.0f };   // the ball reached the rotor
		float               settleTime{ 0.0f };  // the ball came to rest in its pocket
		int                 frets{ 0 };          // frets it hit on the way
		double              travel{ 0.0 };       // pockets it went past on the rotor (either way)
		double              rotorSpeed{ 0.0 };   // the rotor's speed (rad/s) when the plan ends, for carrying on
		int                 tries{ 0 };          // spins thrown away before one landed in the drawn pocket
		bool                drawn{ true };       // landed in the pocket drawn at random (false: the sim's own pocket)
	};

	struct SpinParams
	{
		double throwAngle{ 3.14159265 };  // where on the track the ball is thrown (bowl frame)
		double ballSpeed{ -16.0 };        // the ball's angular speed (rad/s); the opposite way to the rotor
		double rotorAngle{ 0.0 };
		double rotorSpeed{ 3.0 };         // rad/s
	};

	// Throws the ball. The winning pocket is drawn first, fairly, from a_rng; the physics is then run with
	// randomised throws until one ends in that pocket (so the result is exactly uniform whatever the physics
	// does), and that run is what's shown.
	SpinPlan PlanSpin(WheelType a_type, const SpinParams& a_base, std::mt19937& a_rng);

	// One run of the physics with these exact parameters and this noise (the plan's tries).
	SpinPlan SimulateSpin(WheelType a_type, const SpinParams& a_params, std::mt19937& a_noise);

	// The bowl's surface height at a distance from the axis (the ball's centre is this plus its radius).
	double SurfaceZ(double a_r);

	// The rotor slowing down on its own between spins.
	double RotorDrag(double a_speed, double a_dt);
}
