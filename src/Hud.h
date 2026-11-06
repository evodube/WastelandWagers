#pragma once

namespace fr::Hud
{
	// Registers the table HUD (and the settings pages) with F4SE Menu Framework, if it's installed. Safe to call more
	// than once.
	void Install();

	// The terminal HUD is up (Menu Framework there, its fonts loaded), so messages don't need the game's own
	// notifications.
	bool Showing();
}
