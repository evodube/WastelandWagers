#pragma once

// The Steam overlay (Shift+Tab): Fallout 4 runs on under it, and it's no menu and takes no focus, so nothing noticed.
// Steam tells the game it opened and closed (its GameOverlayActivated callback): heard here, the game's pause menu
// opened with it (everything stops: the world, the NPCs, the tables) and closed again after, if it opened it.
// [Table] bPauseOnOverlay.
namespace fr::overlay
{
	void Listen();      // (at game data ready: Steam's up by then)
	bool Open();        // the overlay's up now
}
