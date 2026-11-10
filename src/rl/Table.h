#pragma once

// The roulette table's geometry, in the table's own space: game units, origin at the middle of its footprint on the
// floor, X along its length (the wheel at -X), Z up, the players' side at -Y. These come from the model
// (tools/build_meshes.py scales it by 5.6 about model point (12.005, 3.755, -8.22)); the layout's cells are the ones
// tools/make_textures.py draws.

namespace rl::table
{
	inline constexpr double kScale = 5.6;  // (6.6 until 0.4.3: a table as high as the poker table's, 85% the size)
	inline constexpr double kWide = 0.9;   // (0.5.2) then 10% narrower across (Y), the wheel kept round...
	inline constexpr double kHigh = 1.05;  // ...and 5% higher (Z): build_meshes.py's WIDE and HIGH

	// the wheel
	inline constexpr double kWheelX = -79.233 * kScale / 6.6;  // (build_meshes.py's wheel_centre)
	inline constexpr double kWheelY = -24.783 * kScale / 6.6 * kWide;
	inline constexpr double kBallRadius = 0.16 * kScale;
	inline constexpr double kPocketInner = 2.83 * kScale;    // the pocket ring (the pockets' floor)
	inline constexpr double kPocketOuter = 3.73 * kScale;
	inline constexpr double kPocketCentre = 3.28 * kScale;
	inline constexpr double kNumberInner = 3.78 * kScale;    // the sloped number ring outside it
	inline constexpr double kRotorEdge = 4.45 * kScale;      // where the rotor meets the bowl
	inline constexpr double kConeTop = 5.2 * kScale;         // the bowl's cone, from the rotor up to the ball track
	inline constexpr double kTrackWall = 5.9 * kScale;       // the wall the ball runs against
	inline constexpr double kDiamondR = 4.88 * kScale;       // the eight diamonds (deflectors) on the cone
	inline constexpr double kDiamondHalf = 0.19 * kScale;

	// heights (Z) of the bowl's surfaces
	inline constexpr double Z(double a_modelZ) { return (a_modelZ + 8.22) * kScale * kHigh; }
	inline constexpr double kPocketFloorZ = Z(1.356);
	inline constexpr double kNumberInnerZ = Z(1.481);
	inline constexpr double kRotorEdgeZ = Z(1.713);
	inline constexpr double kConeTopZ = Z(2.108);
	inline constexpr double kTrackZ = Z(2.132);

	// the felt and the layout drawn on it
	inline constexpr double X(double a_modelX) { return (a_modelX - 12.005) * kScale; }
	inline constexpr double Y(double a_modelY) { return (a_modelY - 3.755) * kScale * kWide; }
	inline constexpr double kFeltZ = Z(1.19);
	inline constexpr double kZeroX0 = X(10.2);                 // the zero column (wheel side)...
	inline constexpr double kGridX0 = X(10.2 + 2.2);           // ...then 12 columns of numbers...
	inline constexpr double kCellW = 1.45 * kScale;
	inline constexpr double kGridX1 = kGridX0 + 12 * kCellW;
	inline constexpr double kColumnX1 = kGridX1 + 1.8 * kScale; // ...and the 2-to-1 cells
	inline constexpr double kGridY0 = Y(-3.0);                 // bottom of the row of 1, 4, 7 (players' side)
	inline constexpr double kRowH = 2.7 * kScale * kWide;
	inline constexpr double kGridY1 = kGridY0 + 3 * kRowH;
	inline constexpr double kDozenY0 = kGridY0 - 1.5 * kScale * kWide;
	inline constexpr double kOutsideY0 = kDozenY0 - 1.5 * kScale * kWide;

	// chips
	inline constexpr double kChipRadius = 0.34 * kScale;
	inline constexpr double kChipHeight = 0.51 * kScale / 6.6;  // the chip set's chips at this size (tools/build_chips.py)

	// the table's edges (for walking round it)
	inline constexpr double kHalfLength = 138.2 * kScale / 6.6;
	inline constexpr double kPlayersEdgeY = Y(-8.86);
	inline constexpr double kFarEdgeY = Y(11.29);
}
