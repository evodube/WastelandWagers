#pragma once

// The mod's old name (fo4roulette, before Wasteland Wagers): its files removed from the game's folders, so a new
// version unpacked over the old one leaves nothing of it behind. At the plugin's load (everything but the old .esl,
// which the game's still to read) and again when the game's data is loaded (the .esl too: the game's done with it).
namespace fr::cleanup
{
	void RemoveOldFiles(bool a_dataLoaded);

	// the old plugin was running alongside this one (loaded before it was removed): a restart wanted, once
	bool OldWasRunning();
}
