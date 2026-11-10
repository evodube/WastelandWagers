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
	// Papyrus Actor.SetLookAt / ClearLookAt
	bool LookAt(RE::Actor* a_actor, RE::TESObjectREFR* a_target);
	bool ClearLookAt(RE::Actor* a_actor);

	// a vanilla idle (Fallout4.esm IDLE form ID) through the quest's PlayIdleOn
	void PlayIdle(RE::Actor* a_actor, RE::TESFormID a_idle);

	// Busy with something else, so not to be called over to a table: in furniture (a chair, a workstation, a guard
	// post), or held by another quest's alias that gives them packages of its own (a pool game, a companion, a mod's
	// scene). Our own quest's aliases don't count.
	bool Busy(RE::Actor* a_actor);

	// The AI package they're on, for the log.
	std::string PackageName(RE::Actor* a_actor);
}
