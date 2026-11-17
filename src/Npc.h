#pragma once

// Making NPCs walk to the table and stay there, through fo4roulette.esl's quest (fo4rouletteQuest): two collection
// aliases, Dealers (unused since 0.2.1) and Bettors, whose package (fo4rouletteWalkToSpot) walks each one to their linked ref (keyword
// fo4rouletteLinkSpot) and faces it there. Alias packages go by quest priority (100), so this outranks the sandbox
// packages settlers usually have. The plugin places an XMarkerHeading where each should stand and links them to it.
// Plus a few Papyrus Actor functions. Main thread only; the calls run on the script thread, later.
namespace fr::Npc
{
	bool Ready();  // fo4roulette.esl's quest and keyword are there

	// into Dealers (a_dealer) or Bettors, linked to a_target: they walk there
	void Join(RE::Actor* a_actor, RE::TESObjectREFR* a_target, bool a_dealer);
	bool Joined(const RE::Actor* a_actor);  // the quest said it holds them
	// the walk sent again (linked ref off and on, AI reset if a_reset): one that isn't getting going
	void Walk(RE::Actor* a_actor, RE::TESObjectREFR* a_target, bool a_reset);
	// there: the linked ref comes off so the package stops stepping and turning them (they're held with
	// SetRestrained instead)
	void Arrived(RE::Actor* a_actor);
	// out of both collections, unlinked: their own AI again
	void Leave(RE::Actor* a_actor);
	// everyone out (a save loaded: whoever the collections hold, the plugin doesn't know)
	void ClearAll();

	// Papyrus ObjectReference.MoveTo: put exactly on a_target, facing its way
	bool MoveTo(RE::Actor* a_actor, RE::TESObjectREFR* a_target);
	// Papyrus Actor.SetRestrained: kept where they are. Saved with the game, so undone before every save
	bool SetRestrained(RE::Actor* a_actor, bool a_on);

	// at another game: Playable Pool's (playing or watching pool)
	bool AtPool(RE::Actor* a_actor);

	// (the idle log) the idle we last sent them, and when
	std::pair<RE::TESFormID, std::chrono::steady_clock::time_point> LastIdleSent(RE::Actor* a_actor);
	// Papyrus Actor.SetLookAt / ClearLookAt
	bool LookAt(RE::Actor* a_actor, RE::TESObjectREFR* a_target);
	bool ClearLookAt(RE::Actor* a_actor);

	// a vanilla idle (Fallout4.esm IDLE form ID) through the quest's PlayIdleOn
	void PlayIdle(RE::Actor* a_actor, RE::TESFormID a_idle);

	// the quest watching a_actor's animation for their hand pulling a slot machine's lever (furnitureIdleFlavor1Slave):
	// each one comes back through the quest's native LeverDown (Slots.cpp). Off again with a_on false
	void WatchLever(RE::Actor* a_actor, bool a_on);

	// you sat in a slot machine (its activation's default processing, past its block), and up out of it again (the
	// game's get-up: ActionInteractionExit)
	void SitPlayerIn(RE::TESObjectREFR* a_furniture);
	void StandPlayerUp();

	// a line of the game's: the custom-subtype topic with that keyword (Fallout4.esm form ID)
	void Say(RE::Actor* a_actor, RE::TESFormID a_keyword);

	// Papyrus Actor.SnapIntoInteraction: sat down in a chair
	// into a poker chair: off the walk, onto the Sit package with the chair as its furniture (the quest's Sitters), and
	// put in it; the chair theirs while they're in it
	void SitOn(RE::Actor* a_actor, RE::TESObjectREFR* a_chair);
	void ClaimChair(RE::TESObjectREFR* a_chair);  // a chair kept for the game: yours, so nobody else sits in it

	// Busy with something else, so not to be called over to a table: a settler with a job, in furniture (a chair, a
	// workstation, a guard post), at the pool table (Playable Pool), or a companion.
	const char* Busy(RE::Actor* a_actor);  // why they're busy, or nullptr: free to come over

	// The AI package they're on, for the log.
	std::string PackageName(RE::Actor* a_actor);
}
